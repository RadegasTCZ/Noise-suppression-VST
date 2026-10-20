#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include "Engines/ClassicSpeexEngine.h"
#include "Engines/DfnEngine.h"
#include "Engines/RnnoiseEngine.h"
#include "Engines/SnsEngine.h"
#include "Engines/WebRtcEngine.h"
#include "Host/StateMigration.h"
#include "PluginEditor.h"

namespace
{
constexpr int kFrame = nsp::Engine::kFrameSize;
constexpr int k48 = nsp::Engine::kSampleRate;
} // namespace

NoiseSuppressionProcessor::NoiseSuppressionProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, nsp::kStateType, nsp::createParameterLayout())
{
    apvts.state.setProperty (nsp::kStateVersionProperty, nsp::kStateVersion, nullptr);

    engineParam  = apvts.getRawParameterValue (nsp::ids::engine);
    bypassParam  = apvts.getRawParameterValue (nsp::ids::bypass);
    monitorParam = apvts.getRawParameterValue (nsp::ids::monitorResidual);
    limiterParam = apvts.getRawParameterValue (nsp::ids::limiter);
    jassert (engineParam != nullptr && bypassParam != nullptr && monitorParam != nullptr && limiterParam != nullptr);

    host.addEngine (std::make_unique<nsp::ClassicSpeexEngine>()); // index 0 == kEngineClassic
    auto snsEngine = std::make_unique<nsp::SnsEngine>();
    sns = snsEngine.get();
    host.addEngine (std::move (snsEngine));                        // index 1 == kEngineSns
    host.addEngine (std::make_unique<nsp::WebRtcEngine>());        // index 2 == kEngineWebRtc
    auto rnnoiseEngine = std::make_unique<nsp::RnnoiseEngine>();
    rnnoise = rnnoiseEngine.get();
    host.addEngine (std::move (rnnoiseEngine));                    // index 3 == kEngineRnnoise
    auto dfnEngine = std::make_unique<nsp::DfnEngine>();
    dfn = dfnEngine.get();
    host.addEngine (std::move (dfnEngine));                        // index 4 == kEngineDfn
    jassert (host.numEngines() == nsp::engineNames().size());
}

NoiseSuppressionProcessor::~NoiseSuppressionProcessor() = default;

bool NoiseSuppressionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NoiseSuppressionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = juce::roundToInt (sampleRate);
    preparedBlock = juce::jmax (1, samplesPerBlock);
    hostChannels = juce::jlimit (1, kMaxChannels, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    underruns.store (0);

    bool ok = hostRate > 0;

    for (size_t ch = 0; ch < (size_t) kMaxChannels && ok; ++ch)
        ok = inResampler[ch].prepare (hostRate, k48) && outResampler[ch].prepare (k48, hostRate);

    rateSupported.store (ok);

    if (! ok)
    {
        latency = 0;
        setLatencySamples (0); // processBlock passes audio through untouched
        return;
    }

    const int max48 = nsp::Resampler::maxOutputFor (preparedBlock, hostRate, k48);

    // Target first: EngineHost::reset() (called by prepare) starts on the requested engine.
    host.setTarget (juce::roundToInt (engineParam->load()), monitorParam->load() > 0.5f);
    host.updateParameters (apvts);   // so each engine's reset() adopts the saved variant, not its default
    host.prepare (hostChannels, max48);

    const int prefill = inResampler[0].isPassthrough()
                          ? kFrame
                          : (int) std::ceil ((double) kFrame * (double) hostRate / (double) k48) + 2;

    latency = inResampler[0].inputLatency() + prefill + outResampler[0].outputLatency()
            + juce::roundToInt ((double) host.maxExtraLatency() * (double) hostRate / (double) k48);

    const int maxPull48 = max48 + kFrame;
    const int maxHostOut = nsp::Resampler::maxOutputFor (maxPull48, k48, hostRate);

    for (size_t ch = 0; ch < (size_t) kMaxChannels; ++ch)
    {
        outRing[ch].allocate (prefill + maxHostOut + preparedBlock);
        outRing[ch].pushZeros (prefill);
        dryDelay[ch].prepare (juce::jmax (1, latency));
        dryDelay[ch].setDelay (latency);
        dryBuf[ch].assign ((size_t) preparedBlock, 0.0f);
        wetBuf[ch].assign ((size_t) preparedBlock, 0.0f);
        in48[ch].assign ((size_t) max48, 0.0f);
        out48[ch].assign ((size_t) maxPull48, 0.0f);
        hostOut[ch].assign ((size_t) maxHostOut, 0.0f);
        spareIo[ch].assign ((size_t) preparedBlock, 0.0f);
    }

    bypassRamp.assign ((size_t) preparedBlock, 0.0f);
    limiter.prepare ((double) hostRate);

    bypassCount = bypassParam->load() > 0.5f ? kBypassRampSamples : 0;

    setLatencySamples (latency);
}

void NoiseSuppressionProcessor::releaseResources() {}

void NoiseSuppressionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, false);
}

void NoiseSuppressionProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, true); // same latency as the normal path
}

void NoiseSuppressionProcessor::processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (hostChannels, buffer.getNumChannels());

    if (numSamples <= 0 || numCh <= 0)
        return;

    for (int ch = 0; ch < numCh; ++ch)
        inputPeak.push (buffer.getReadPointer (ch), numSamples);

    if (! rateSupported.load (std::memory_order_relaxed) || preparedBlock == 0)
    {
        for (int ch = 0; ch < numCh; ++ch)
            outputPeak.push (buffer.getReadPointer (ch), numSamples);
        return;
    }

    const int engineIndex = juce::roundToInt (engineParam->load (std::memory_order_relaxed));
    const bool bypass = forceBypass || bypassParam->load (std::memory_order_relaxed) > 0.5f;
    const bool residual = monitorParam->load (std::memory_order_relaxed) > 0.5f;
    const bool limit = limiterParam->load (std::memory_order_relaxed) > 0.5f;

    host.setTarget (engineIndex, residual);
    host.updateParameters (apvts);

    // Blocks larger than announced are split into prepared-size chunks (no reallocation).
    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (preparedBlock, numSamples - offset);
        float* io[kMaxChannels] = {};

        for (int ch = 0; ch < numCh; ++ch)
            io[ch] = buffer.getWritePointer (ch) + offset;

        // EngineHost and the resamplers always run hostChannels in lockstep; a channel the buffer
        // lacks is fed silence and its output discarded.
        for (int ch = numCh; ch < hostChannels; ++ch)
        {
            std::fill (spareIo[(size_t) ch].begin(), spareIo[(size_t) ch].begin() + n, 0.0f);
            io[ch] = spareIo[(size_t) ch].data();
        }

        processChunk (io, hostChannels, n, bypass, limit);
        offset += n;
    }

    for (int ch = 0; ch < numCh; ++ch)
        outputPeak.push (buffer.getReadPointer (ch), numSamples);
}

void NoiseSuppressionProcessor::processChunk (float* const* io, int numCh, int n, bool bypass, bool limit) noexcept
{
    // 1. dry path, host rate, delayed by exactly `latency`
    for (int ch = 0; ch < numCh; ++ch)
        dryDelay[(size_t) ch].process (io[ch], dryBuf[(size_t) ch].data(), n);

    // 2. host -> 48 kHz
    int n48 = 0;
    const float* in48Ptr[kMaxChannels] = {};

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& dst = in48[(size_t) ch];
        const int got = inResampler[(size_t) ch].process (io[ch], n, dst.data(), (int) dst.size());

        if (ch == 0)
            n48 = got;

        jassert (got == n48);
        in48Ptr[ch] = dst.data();
    }

    // Non-finite or absurdly large input (driver glitch, upstream filter) must not poison the recursive engine
    // state: energy sums of +-1e20 overflow to inf inside the engines. +18 dBFS leaves every real signal alone.
    constexpr float kInputClamp = 8.0f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = in48[(size_t) ch].data();

        for (int i = 0; i < n48; ++i)
            d[i] = std::isfinite (d[i]) ? juce::jlimit (-kInputClamp, kInputClamp, d[i]) : 0.0f;
    }

    // 3. engines (48 kHz frames)
    host.process (in48Ptr, numCh, n48);

    const int avail = juce::jmin (host.available(), (int) out48[0].size());
    float* out48Ptr[kMaxChannels] = {};

    for (int ch = 0; ch < numCh; ++ch)
        out48Ptr[ch] = out48[(size_t) ch].data();

    host.pull (out48Ptr, numCh, avail);

    // 4. 48 kHz -> host into the host-rate FIFO, then take n samples
    bool underrun = false;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto c = (size_t) ch;
        const int m = outResampler[c].process (out48[c].data(), avail, hostOut[c].data(), (int) hostOut[c].size());
        [[maybe_unused]] const bool pushed = outRing[c].push (hostOut[c].data(), m);
        jassert (pushed);

        if (! outRing[c].pop (wetBuf[c].data(), n))
        {
            const int have = outRing[c].size();
            outRing[c].pop (wetBuf[c].data(), have);
            std::fill (wetBuf[c].begin() + have, wetBuf[c].begin() + n, 0.0f);
            underrun = true;
        }
    }

    if (underrun)
        underruns.fetch_add (1, std::memory_order_relaxed);

    // 5. bypass ramp
    for (size_t i = 0; i < (size_t) n; ++i)
    {
        bypassCount = bypass ? juce::jmin (kBypassRampSamples, bypassCount + 1) : juce::jmax (0, bypassCount - 1);
        bypassRamp[i] = (float) bypassCount / (float) kBypassRampSamples;
    }

    // 6. safety limiter on the processed (wet or residual) path, every chunk - also while bypassed, so
    //    its gain stays continuous. Off: reset, so switching it on starts idle (gain 1).
    if (limit)
    {
        float* sel[kMaxChannels] = {};

        for (int ch = 0; ch < numCh; ++ch)
            sel[ch] = wetBuf[(size_t) ch].data();

        limiter.process (sel, numCh, n);
    }
    else
    {
        limiter.reset();
    }

    // 7. out = b*dry + (1-b)*sel; exact dry when b == 1
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* out = io[ch];
        const float* dry = dryBuf[(size_t) ch].data();
        const float* sel = wetBuf[(size_t) ch].data();

        for (int i = 0; i < n; ++i)
        {
            const float b = bypassRamp[(size_t) i];

            // Exact selects at the end points: pure dry stays pure dry even if the wet path is non-finite,
            // and pure wet never picks up a NaN/inf from the raw dry input (0 * NaN is NaN).
            if (b >= 1.0f)
            {
                out[i] = dry[i];
                continue;
            }

            if (b <= 0.0f)
            {
                out[i] = sel[i];
                continue;
            }

            out[i] = b * dry[i] + (1.0f - b) * sel[i];
        }
    }
}

juce::AudioProcessorEditor* NoiseSuppressionProcessor::createEditor()
{
    return new NoiseSuppressionEditor (*this);
}

juce::AudioProcessorParameter* NoiseSuppressionProcessor::getBypassParameter() const
{
    return apvts.getParameter (nsp::ids::bypass);
}

float NoiseSuppressionProcessor::getSnsNoiseFloorDb() const noexcept
{
    return sns != nullptr ? sns->noiseFloorDb() : -90.0f;
}

bool NoiseSuppressionProcessor::isRnnoiseModelFailed (int model) const noexcept
{
    return rnnoise != nullptr && rnnoise->modelFailed (model);
}

bool NoiseSuppressionProcessor::isDfnModelFailed() const noexcept
{
    return dfn != nullptr && dfn->modelFailed();
}

void NoiseSuppressionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (nsp::kStateVersionProperty, nsp::kStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NoiseSuppressionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr)
        return; // garbage / truncated / empty: keep the current state

    auto migrated = nsp::migrateState (juce::ValueTree::fromXml (*xml));

    if (migrated.isValid())
        apvts.replaceState (migrated);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoiseSuppressionProcessor();
}
