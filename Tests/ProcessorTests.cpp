#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "../Source/PluginProcessor.h"
#include "../Source/Engines/DfnEngine.h"
#include "../Source/Engines/RnnoiseEngine.h"
#include "../Source/Engines/WebRtcEngine.h"
#include "AllocCounter.h"
#include "TestPaths.h"
#include "TestSignals.h"

namespace
{
using Signal = std::vector<std::vector<float>>; // [channel][sample]

void setParam (NoiseSuppressionProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void preparePlay (NoiseSuppressionProcessor& p, double rate, int maxBlock)
{
    p.setRateAndBufferSizeDetails (rate, maxBlock);
    p.prepareToPlay (rate, maxBlock);
}

/** Runs `in` through p in blocks cycling over `blocks`; beforeBlock (pos) runs before each block. */
Signal runBlocks (NoiseSuppressionProcessor& p, const Signal& in, const std::vector<int>& blocks,
                  const std::function<void (int)>& beforeBlock = nullptr)
{
    const int numCh = (int) in.size();
    const int total = (int) in[0].size();
    const int maxBlock = *std::max_element (blocks.begin(), blocks.end());
    juce::AudioBuffer<float> buffer (numCh, maxBlock);
    juce::MidiBuffer midi;
    Signal out ((size_t) numCh, std::vector<float> ((size_t) total, 0.0f));
    size_t blockIndex = 0;

    for (int pos = 0; pos < total;)
    {
        const int n = juce::jmin (blocks[blockIndex++ % blocks.size()], total - pos);

        if (beforeBlock)
            beforeBlock (pos);

        buffer.setSize (numCh, n, false, false, true);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.copyFrom (ch, 0, in[(size_t) ch].data() + pos, n);

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < numCh; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out[(size_t) ch].begin() + pos);

        pos += n;
    }

    return out;
}

std::vector<float> delayed (const std::vector<float>& x, int d)
{
    std::vector<float> y (x.size(), 0.0f);

    for (size_t i = (size_t) d; i < x.size(); ++i)
        y[i] = x[i - (size_t) d];

    return y;
}

float maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, int from = 0)
{
    float m = 0.0f;

    for (size_t i = (size_t) from; i < a.size(); ++i)
        m = juce::jmax (m, std::abs (a[i] - b[i]));

    return m;
}

/** Silence with a -20 dBFS RMS white-noise burst from 0.3 s to 0.6 s; total 1.2 s. */
std::vector<float> noiseBurst (double rate, int seed)
{
    const int total = (int) (1.2 * rate), start = (int) (0.3 * rate), len = (int) (0.3 * rate);
    std::vector<float> x ((size_t) total, 0.0f);
    const auto noise = nsp::test::whiteNoise (len, 0.1f, seed);
    std::copy (noise.begin(), noise.end(), x.begin() + start);
    return x;
}

void setSuppressMinus5 (NoiseSuppressionProcessor& p)
{
    setParam (p, nsp::ids::classicSuppress, -5.0f);
    setParam (p, nsp::ids::snsSuppress, -5.0f);
}

juce::AudioProcessor::BusesLayout layoutOf (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (in);
    l.outputBuses.add (out);
    return l;
}

constexpr int kAllEngines[] = { nsp::kEngineClassic, nsp::kEngineSns, nsp::kEngineWebRtc, nsp::kEngineRnnoise, nsp::kEngineDfn };

nsp::WebRtcEngine& webrtcOf (NoiseSuppressionProcessor& p)
{
    return dynamic_cast<nsp::WebRtcEngine&> (p.getEngineHost().getEngine (nsp::kEngineWebRtc));
}

nsp::RnnoiseEngine& rnnoiseOf (NoiseSuppressionProcessor& p)
{
    return dynamic_cast<nsp::RnnoiseEngine&> (p.getEngineHost().getEngine (nsp::kEngineRnnoise));
}

nsp::DfnEngine& dfnOf (NoiseSuppressionProcessor& p)
{
    return dynamic_cast<nsp::DfnEngine&> (p.getEngineHost().getEngine (nsp::kEngineDfn));
}

bool allFinite (const std::vector<float>& x, float limit)
{
    for (float v : x)
        if (! std::isfinite (v) || std::abs (v) > limit)
            return false;
    return true;
}
} // namespace

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("Processor", "Processor") {}

    void runTest() override
    {
        testLatency48k();
        testDfnModelNotFailed();
        testLatencyResampled();
        testBypassNull();
        testResidualIdentity();
        testSwitchClick();
        testVariantChangeClick();
        testNoAllocation();
        testVariantFlapping();
        testSwitchWithVariantChange();
        testStartupVariantAdoption();
        testOversizedBlock();
        testNoUnderrun();
        testMono();
        testLayouts();
        testNonFiniteInput();
        testHugeFiniteInput();
        testLimiterOn();
        testLimiterOff();
        testLimiterBypassed();
    }

private:
    void testLatency48k()
    {
        beginTest ("latency at 48 kHz: reported 1920 for every engine, measured lag exactly 1920");

        for (int engine : kAllEngines)
        {
            NoiseSuppressionProcessor p;
            setParam (p, nsp::ids::engine, (float) engine);
            setSuppressMinus5 (p);
            preparePlay (p, 48000.0, 512);

            // 480 frame buffering + maxExtraLatency 1440 (DeepFilterNet); every other engine is padded to it (D5)
            expectEquals (p.getLatencySamples(), 1920);

            const auto x = noiseBurst (48000.0, 1);
            const auto out = runBlocks (p, { x, x }, { 512 });
            expectEquals (nsp::test::xcorrLag (x, out[0], 3000), 1920, "engine " + juce::String (engine));
            expectEquals (nsp::test::xcorrLag (x, out[1], 3000), 1920, "engine " + juce::String (engine));
        }
    }

    void testDfnModelNotFailed()
    {
        beginTest ("isDfnModelFailed() is false with the embedded model");

        NoiseSuppressionProcessor p;
        setParam (p, nsp::ids::engine, (float) nsp::kEngineDfn);
        preparePlay (p, 48000.0, 512);
        expect (! p.isDfnModelFailed());
    }

    void testLatencyResampled()
    {
        beginTest ("latency at 44.1 / 96 kHz with odd blocks: measured lag within +-1 of reported");

        for (double rate : { 44100.0, 96000.0 })
            for (int block : { 441, 1000, 64 })
                for (int engine : kAllEngines)
                {
                    NoiseSuppressionProcessor p;
                    setParam (p, nsp::ids::engine, (float) engine);
                    setSuppressMinus5 (p);
                    preparePlay (p, rate, block);
                    expect (p.isSampleRateSupported());

                    const auto x = noiseBurst (rate, 2);
                    const auto out = runBlocks (p, { x, x }, { block });
                    const int lag = nsp::test::xcorrLag (x, out[0], 5000);   // 96 kHz: ~4002 with DeepFilterNet
                    const int latency = p.getLatencySamples();

                    expect (std::abs (lag - latency) <= 1,
                            "rate " + juce::String (rate) + " block " + juce::String (block) + " engine "
                              + juce::String (engine) + ": lag " + juce::String (lag) + " vs reported " + juce::String (latency));
                }
    }

    void testBypassNull()
    {
        beginTest ("bypass: output == input delayed by latency, bit-exact at 48 and 44.1 kHz");

        for (double rate : { 48000.0, 44100.0 })
        {
            const auto x0 = nsp::test::whiteNoise ((int) rate, 0.1f, 3);
            const auto x1 = nsp::test::whiteNoise ((int) rate, 0.1f, 4);

            {   // bypassed from the start
                NoiseSuppressionProcessor p;
                setParam (p, nsp::ids::bypass, 1.0f);
                preparePlay (p, rate, 512);
                const int latency = p.getLatencySamples();
                const auto out = runBlocks (p, { x0, x1 }, { 512, 100, 333 });
                expectLessThan (maxAbsDiff (out[0], delayed (x0, latency)), 1.0e-6f);
                expectLessThan (maxAbsDiff (out[1], delayed (x1, latency)), 1.0e-6f);
            }

            {   // bypass switched on mid-stream: exact once the 480-sample ramp has finished
                NoiseSuppressionProcessor p;
                preparePlay (p, rate, 512);
                const int latency = p.getLatencySamples();
                int toggle = -1;
                const auto out = runBlocks (p, { x0, x1 }, { 512 }, [&] (int pos)
                {
                    if (toggle < 0 && pos >= (int) rate / 2)
                    {
                        setParam (p, nsp::ids::bypass, 1.0f);
                        toggle = pos;
                    }
                });
                expectLessThan (maxAbsDiff (out[0], delayed (x0, latency), toggle + 480), 1.0e-6f);
                expectLessThan (maxAbsDiff (out[1], delayed (x1, latency), toggle + 480), 1.0e-6f);
            }
        }
    }

    void testResidualIdentity()
    {
        beginTest ("residual identity: wet + residual == dry delayed, error < -60 dBFS");

        const auto file = nsp::test::testAudioDir().getChildFile ("sample.wav");

        if (! file.existsAsFile())
        {
            logMessage ("    SKIPPED: " + file.getFullPathName() + " not found (set NSP_TEST_AUDIO_DIR or use --audio-dir)");
            return;
        }

        expect (file.existsAsFile(), "residual identity needs " + file.getFullPathName());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        expect (reader != nullptr);

        if (reader == nullptr)
            return;

        const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) 10 * 48000);
        juce::AudioBuffer<float> fileBuffer (2, n);
        reader->read (&fileBuffer, 0, n, 0, true, true);

        Signal in (2);
        for (int ch = 0; ch < 2; ++ch)
            in[(size_t) ch].assign (fileBuffer.getReadPointer (ch), fileBuffer.getReadPointer (ch) + n);

        // sample.wav is 48 kHz stereo; the processor runs at 48 kHz so no resampling is involved
        for (int engine : kAllEngines)
        {
            NoiseSuppressionProcessor wetP, resP;
            setParam (wetP, nsp::ids::engine, (float) engine);
            setParam (resP, nsp::ids::engine, (float) engine);
            setParam (resP, nsp::ids::monitorResidual, 1.0f);
            preparePlay (wetP, 48000.0, 512);
            preparePlay (resP, 48000.0, 512);

            const auto wet = runBlocks (wetP, in, { 512 });
            const auto res = runBlocks (resP, in, { 512 });
            const int latency = wetP.getLatencySamples();

            for (size_t ch = 0; ch < 2; ++ch)
            {
                std::vector<float> sum ((size_t) n);
                for (size_t i = 0; i < (size_t) n; ++i)
                    sum[i] = wet[ch][i] + res[ch][i];

                const auto ref = delayed (in[ch], latency);
                expectLessThan (nsp::test::maxAbsDiffDb (sum.data(), ref.data(), n), -60.0f,
                                "engine " + juce::String (engine) + " channel " + juce::String ((int) ch));
            }
        }
    }

    void testSwitchClick()
    {
        beginTest ("engine switches Classic -> SNS -> WebRTC -> RNNoise -> DeepFilterNet -> Classic on a 1 kHz -12 dBFS sine: max step <= 2x steady-state step");

        NoiseSuppressionProcessor p;
        setParam (p, nsp::ids::engine, (float) nsp::kEngineClassic);
        setSuppressMinus5 (p);
        preparePlay (p, 48000.0, 256);

        const auto x = nsp::test::sine (7 * 48000, 1000.0f, 48000.0, juce::Decibels::decibelsToGain (-12.0f));
        const int sequence[] = { nsp::kEngineSns, nsp::kEngineWebRtc, nsp::kEngineRnnoise, nsp::kEngineDfn, nsp::kEngineClassic };
        int switchAt[5] = { -1, -1, -1, -1, -1 };

        const auto out = runBlocks (p, { x, x }, { 256 }, [&] (int pos)
        {
            for (int k = 0; k < 5; ++k)
                if (switchAt[k] < 0 && pos >= (k + 1) * 48000)
                {
                    setParam (p, nsp::ids::engine, (float) sequence[k]);
                    switchAt[k] = pos;
                }
        });

        // Reference = the input's max step (the steady-state step of a transparent path); engines may
        // attenuate a stationary tone by a varying amount, so their own output is not a stable reference.
        const float bound = 2.0f * nsp::test::maxAbsStep (x.data(), (int) x.size());
        const int window = p.getLatencySamples() + 2 * 480 + 256; // covers the crossfade frame wherever it lands

        for (int k = 0; k < 5; ++k)
            expectLessOrEqual (nsp::test::maxAbsStep (out[0].data() + switchAt[k], window), bound,
                               "switch to engine " + juce::String (sequence[k]));
        expectEquals (p.getEngineHost().activeEngine(), (int) nsp::kEngineClassic);
    }

    void testVariantChangeClick()
    {
        beginTest ("WebRTC level, RNNoise model and DeepFilterNet strength changes on a 1 kHz -12 dBFS sine: max step <= 2x steady-state step");

        const auto x = nsp::test::sine (3 * 48000, 1000.0f, 48000.0, juce::Decibels::decibelsToGain (-12.0f));
        const float bound = 2.0f * nsp::test::maxAbsStep (x.data(), (int) x.size());
        struct Case { int engine; const char* param; float first, second; };
        const Case cases[] = { { nsp::kEngineWebRtc, nsp::ids::webrtcLevel, 3.0f, 0.0f },
                               { nsp::kEngineRnnoise, nsp::ids::rnnModel, 1.0f, 0.0f },
                               { nsp::kEngineDfn, nsp::ids::dfnStrength, 2.0f, 0.0f } };

        for (const auto& c : cases)
        {
            NoiseSuppressionProcessor p;
            setParam (p, nsp::ids::engine, (float) c.engine);
            preparePlay (p, 48000.0, 256);
            int change1 = -1, change2 = -1;

            const auto out = runBlocks (p, { x, x }, { 256 }, [&] (int pos)
            {
                if (change1 < 0 && pos >= 48000) { setParam (p, c.param, c.first); change1 = pos; }
                if (change2 < 0 && pos >= 96000) { setParam (p, c.param, c.second); change2 = pos; }
            });

            const int window = p.getLatencySamples() + 2 * 480 + 256;
            expectLessOrEqual (nsp::test::maxAbsStep (out[0].data() + change1, window), bound, c.param);
            expectLessOrEqual (nsp::test::maxAbsStep (out[0].data() + change2, window), bound, c.param);
        }
    }

    void testNoAllocation()
    {
        beginTest ("no allocation in processBlock across engine switch, bypass, residual and limiter changes");

        for (double rate : { 48000.0, 44100.0 })
        {
            NoiseSuppressionProcessor p;
            const int block = (int) (rate / 100.0);
            preparePlay (p, rate, block);

            const auto x = nsp::test::whiteNoise (block * 200, 0.1f, 7);
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            int allocations = 0;

            for (int b = 0; b < 200; ++b)
            {
                switch (b) // parameter changes happen outside the checked scope (host/message thread)
                {
                    case 20:  setParam (p, nsp::ids::engine, (float) nsp::kEngineClassic); break;
                    case 30:  setParam (p, nsp::ids::limiter, 0.0f); break;
                    case 45:  setParam (p, nsp::ids::limiter, 1.0f); break;
                    case 40:  setParam (p, nsp::ids::engine, (float) nsp::kEngineSns); break;
                    case 50:  setParam (p, nsp::ids::engine, (float) nsp::kEngineWebRtc); break;
                    case 55:  setParam (p, nsp::ids::webrtcLevel, 3.0f); break;
                    case 60:  setParam (p, nsp::ids::bypass, 1.0f); break;
                    case 70:  setParam (p, nsp::ids::bypass, 0.0f); break;
                    case 80:  setParam (p, nsp::ids::engine, (float) nsp::kEngineRnnoise); break;
                    case 90:  setParam (p, nsp::ids::rnnModel, 1.0f); break;
                    case 100: setParam (p, nsp::ids::bypass, 0.0f); break;
                    case 110: setParam (p, nsp::ids::rnnModel, 0.0f); break;
                    case 120: setParam (p, nsp::ids::monitorResidual, 1.0f); break;
                    case 130: setParam (p, nsp::ids::webrtcLevel, 0.0f); break;   // engine not running
                    case 133: setParam (p, nsp::ids::engine, (float) nsp::kEngineDfn); break;
                    case 135: setParam (p, nsp::ids::dfnStrength, 2.0f); break;
                    case 138: setParam (p, nsp::ids::dfnStrength, 0.0f); break;
                    case 140: setParam (p, nsp::ids::engine, (float) nsp::kEngineClassic); break;
                    case 150: setParam (p, nsp::ids::engine, (float) nsp::kEngineWebRtc); break;
                    case 155: setParam (p, nsp::ids::limiter, 0.0f); break;
                    case 175: setParam (p, nsp::ids::limiter, 1.0f); break;
                    case 160: setParam (p, nsp::ids::monitorResidual, 0.0f); break;
                    case 170: setParam (p, nsp::ids::webrtcLevel, 2.0f); break;
                    case 180: setParam (p, nsp::ids::bypass, 1.0f); break;
                    default: break;
                }

                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, x.data() + b * block, block);

                int count = 0;
                {
                    nsp::test::ScopedAllocCheck check;
                    p.processBlock (buffer, midi);
                    count = (int) check.count();
                }
                allocations += count;
            }

            expectEquals (allocations, 0, "rate " + juce::String (rate));
        }
    }

    void testVariantFlapping()
    {
        beginTest ("webrtcLevel / rnnModel / dfnStrength automation flapping every 64-sample block: <= 2 variants per frame, no allocation, last value wins");

        struct Case { int engine; const char* param; float a, b, last; };
        const Case cases[] = { { nsp::kEngineWebRtc, nsp::ids::webrtcLevel, 0.0f, 3.0f, 2.0f },
                               { nsp::kEngineRnnoise, nsp::ids::rnnModel, 0.0f, 1.0f, 1.0f },
                               { nsp::kEngineDfn, nsp::ids::dfnStrength, 0.0f, 2.0f, 2.0f } };

        for (const auto& c : cases)
        {
            NoiseSuppressionProcessor p;
            setParam (p, nsp::ids::engine, (float) c.engine);
            preparePlay (p, 48000.0, 64);
            auto& webrtc = webrtcOf (p);
            auto& rnnoise = rnnoiseOf (p);
            auto& dfnEngine = dfnOf (p);
            const bool isDfn = c.engine == nsp::kEngineDfn;   // ramped inside the denoiser: one variant, no crossfade
            auto variantsRun = [&]
            {
                if (isDfn) return 1;
                return c.engine == nsp::kEngineWebRtc ? webrtc.levelsRunLastFrame() : rnnoise.modelsRunLastFrame();
            };
            auto activeVariant = [&]
            {
                if (isDfn) return dfnEngine.strength();
                return c.engine == nsp::kEngineWebRtc ? webrtc.activeLevel() : rnnoise.activeModel();
            };

            const auto x = nsp::test::speechLike (48000, 101);
            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            std::vector<float> out (x.size(), 0.0f);
            int allocations = 0, worstRun = 0;

            for (int b = 0; b * 64 < (int) x.size(); ++b)
            {
                setParam (p, c.param, (b % 2) == 0 ? c.a : c.b);   // outside the checked scope (host thread)
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, x.data() + b * 64, 64);

                int count = 0;
                {
                    nsp::test::ScopedAllocCheck check;
                    p.processBlock (buffer, midi);
                    count = check.count();
                }
                allocations += count;
                worstRun = juce::jmax (worstRun, variantsRun());
                std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + 64, out.begin() + b * 64);
            }

            expectEquals (allocations, 0, c.param);
            expectLessOrEqual (worstRun, 2, c.param);
            expect (allFinite (out, 4.0f), c.param);

            setParam (p, c.param, c.last);
            for (int b = 0; b < 30; ++b)   // > two frames: the crossfade (if any) has finished
            {
                buffer.clear();
                p.processBlock (buffer, midi);
            }
            expectEquals (activeVariant(), juce::roundToInt (c.last), c.param);
            expectEquals (variantsRun(), 1, c.param);
        }
    }

    void testSwitchWithVariantChange()
    {
        beginTest ("engine switch away mid model change, then back; level changed while WebRTC is inactive");

        NoiseSuppressionProcessor p;
        setParam (p, nsp::ids::engine, (float) nsp::kEngineRnnoise);
        preparePlay (p, 48000.0, 256);
        auto& rnnoise = rnnoiseOf (p);
        auto& webrtc = webrtcOf (p);
        const auto x = nsp::test::speechLike (4 * 48000, 102);
        int allocations = 0;
        bool checkedRnnoiseBack = false, checkedWebRtcIn = false;
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        std::vector<float> out (x.size(), 0.0f);

        for (int b = 0; (b + 1) * 256 <= (int) x.size(); ++b)
        {
            const int pos = b * 256;
            if (pos == 48128)                // ~1 s: model change and engine switch in the same block
            {
                setParam (p, nsp::ids::rnnModel, 1.0f);
                setParam (p, nsp::ids::engine, (float) nsp::kEngineClassic);
            }
            if (pos == 96000)                // 2 s: back to RNNoise
                setParam (p, nsp::ids::engine, (float) nsp::kEngineRnnoise);
            if (pos == 120064)               // ~2.5 s: SNS, then the WebRTC level changes while WebRTC is inactive
                setParam (p, nsp::ids::engine, (float) nsp::kEngineSns);
            if (pos == 130048)
                setParam (p, nsp::ids::webrtcLevel, 3.0f);
            if (pos == 144128)               // ~3 s: switch to WebRTC
                setParam (p, nsp::ids::engine, (float) nsp::kEngineWebRtc);

            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, x.data() + pos, 256);

            int count = 0;
            {
                nsp::test::ScopedAllocCheck check;
                p.processBlock (buffer, midi);
                count = check.count();
            }
            allocations += count;
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + 256, out.begin() + pos);

            // the first block in which the incoming engine ran: it adopted the requested variant on reset
            if (! checkedRnnoiseBack && pos >= 96000 && p.getEngineHost().activeEngine() == nsp::kEngineRnnoise)
            {
                checkedRnnoiseBack = true;
                expectEquals (rnnoise.activeModel(), (int) nsp::RnnoiseEngine::kLight);
                expectEquals (rnnoise.modelsRunLastFrame(), 1);
            }
            if (! checkedWebRtcIn && pos >= 144128 && p.getEngineHost().activeEngine() == nsp::kEngineWebRtc)
            {
                checkedWebRtcIn = true;
                expectEquals (webrtc.activeLevel(), (int) nsp::WebRtcEngine::k21dB);
                expectEquals (webrtc.levelsRunLastFrame(), 1);
            }
        }

        expect (checkedRnnoiseBack && checkedWebRtcIn);
        expectEquals (allocations, 0);
        expect (allFinite (out, 4.0f));
        expectEquals (p.getEngineHost().activeEngine(), (int) nsp::kEngineWebRtc);
    }

    void testStartupVariantAdoption()
    {
        beginTest ("a saved variant is adopted at prepare, not crossfaded from the default");

        for (int engineIndex : { (int) nsp::kEngineRnnoise, (int) nsp::kEngineWebRtc })
        {
            NoiseSuppressionProcessor p;
            setParam (p, nsp::ids::engine, (float) engineIndex);
            setParam (p, nsp::ids::rnnModel, 1.0f);
            setParam (p, nsp::ids::webrtcLevel, 3.0f);
            preparePlay (p, 48000.0, 256);
            auto& rnnoise = rnnoiseOf (p);
            auto& webrtc = webrtcOf (p);
            const bool isRnn = engineIndex == nsp::kEngineRnnoise;

            expectEquals (rnnoise.activeModel(), (int) nsp::RnnoiseEngine::kLight);
            expectEquals (webrtc.activeLevel(), (int) nsp::WebRtcEngine::k21dB);

            const auto x = nsp::test::speechLike (8 * 256, 7);
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            int worstRun = 0;

            for (int b = 0; b < 8; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, x.data() + b * 256, 256);
                p.processBlock (buffer, midi);
                worstRun = juce::jmax (worstRun, isRnn ? rnnoise.modelsRunLastFrame() : webrtc.levelsRunLastFrame());
            }

            expectEquals (worstRun, 1);
            expectEquals (rnnoise.activeModel(), (int) nsp::RnnoiseEngine::kLight);
            expectEquals (webrtc.activeLevel(), (int) nsp::WebRtcEngine::k21dB);
        }
    }

    void testOversizedBlock()
    {
        beginTest ("host block 4x larger than announced == same input in announced-size blocks");

        for (double rate : { 48000.0, 44100.0 })
        {
            const int total = (int) (3 * rate);
            const auto noise = nsp::test::whiteNoise (total, 0.05f, 8);
            const auto tone = nsp::test::sine (total, 440.0f, rate, 0.2f);
            std::vector<float> x0 ((size_t) total), x1 ((size_t) total);

            for (size_t i = 0; i < (size_t) total; ++i)
            {
                x0[i] = noise[i] + tone[i];
                x1[i] = noise[i] - tone[i];
            }

            NoiseSuppressionProcessor big, small;
            preparePlay (big, rate, 256);
            preparePlay (small, rate, 256);

            const auto outBig = runBlocks (big, { x0, x1 }, { 1024 });
            const auto outSmall = runBlocks (small, { x0, x1 }, { 256 });

            expectLessThan (maxAbsDiff (outBig[0], outSmall[0]), 1.0e-6f, "rate " + juce::String (rate));
            expectLessThan (maxAbsDiff (outBig[1], outSmall[1]), 1.0e-6f, "rate " + juce::String (rate));
            expectEquals (big.getUnderrunCount(), 0);
        }
    }

    void testNoUnderrun()
    {
        beginTest ("no output FIFO underrun over 30 s with blocks 441/1000/37");

        for (double rate : { 44100.0, 96000.0, 48000.0 })
        {
            NoiseSuppressionProcessor p;
            preparePlay (p, rate, 1000);
            const auto x = nsp::test::whiteNoise ((int) (30 * rate), 0.05f, 9);
            runBlocks (p, { x, x }, { 441, 1000, 37 });
            expectEquals (p.getUnderrunCount(), 0, "rate " + juce::String (rate));
        }

        for (double rate : { 44100.0, 96000.0 })   // the engine with the largest latency
        {
            NoiseSuppressionProcessor p;
            setParam (p, nsp::ids::engine, (float) nsp::kEngineDfn);
            preparePlay (p, rate, 1000);
            const auto x = nsp::test::whiteNoise ((int) (10 * rate), 0.05f, 19);
            runBlocks (p, { x, x }, { 441, 1000, 37 });
            expectEquals (p.getUnderrunCount(), 0, "DeepFilterNet, rate " + juce::String (rate));
        }
    }

    void testNonFiniteInput()
    {
        beginTest ("non-finite input: bypass stays exact dry, processing output never non-finite (limiter on or off)");

        for (int engine : kAllEngines)
        {
            const double rate = 48000.0;
            const int total = (int) rate * 2, badAt = 4800;
            auto x = nsp::test::whiteNoise (total, 0.1f, 11);
            const float inf = std::numeric_limits<float>::infinity();

            for (int i = 0; i < 64; ++i)
                x[(size_t) (badAt + i)] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN() : (i % 3 == 1 ? inf : -inf);

            for (float limiterOn : { 1.0f, 0.0f })   // processing: the dry path must not leak NaN/inf through the crossfade
            {
                NoiseSuppressionProcessor p;
                setParam (p, nsp::ids::engine, (float) engine);
                setParam (p, nsp::ids::limiter, limiterOn);
                preparePlay (p, rate, 480);
                const auto out = runBlocks (p, { x, x }, { 480 });
                int nonFinite = 0;

                for (const auto& channel : out)
                    for (float v : channel)
                        if (! std::isfinite (v))
                            ++nonFinite;

                expectEquals (nonFinite, 0, "engine " + juce::String (engine) + ", limiter " + juce::String (limiterOn));
                expect (std::abs (out[0].back()) < 10.0f);
            }

            {   // bypass on: output is exactly the delayed dry signal wherever that is finite
                NoiseSuppressionProcessor p;
                setParam (p, nsp::ids::engine, (float) engine);
                setParam (p, nsp::ids::bypass, 1.0f);
                preparePlay (p, rate, 480);
                const int latency = p.getLatencySamples();
                const auto out = runBlocks (p, { x, x }, { 480 });
                const auto ref = delayed (x, latency);
                int mismatches = 0;

                for (int i = 0; i < total; ++i)
                {
                    const float r = ref[(size_t) i], o = out[0][(size_t) i];
                    if (std::isfinite (r) ? o != r : std::isfinite (o))
                        ++mismatches;
                }

                expectEquals (mismatches, 0);
            }

            {   // bypass on AFTER the bad block: finite, exact delayed (finite) dry
                NoiseSuppressionProcessor p;
                setParam (p, nsp::ids::engine, (float) engine);
                preparePlay (p, rate, 480);
                const int latency = p.getLatencySamples();
                int toggle = -1;
                const auto out = runBlocks (p, { x, x }, { 480 }, [&] (int pos)
                {
                    if (toggle < 0 && pos >= badAt + 2400)
                    {
                        setParam (p, nsp::ids::bypass, 1.0f);
                        toggle = pos;
                    }
                });
                expectLessThan (maxAbsDiff (out[0], delayed (x, latency), toggle + 480 + latency), 1.0e-6f);
            }

            {   // bypass after the bad block is exact dry too
                NoiseSuppressionProcessor p;
                setParam (p, nsp::ids::engine, (float) engine);
                setParam (p, nsp::ids::bypass, 1.0f);
                preparePlay (p, rate, 480);
                const int latency = p.getLatencySamples();
                const auto out = runBlocks (p, { x, x }, { 480 });
                expectLessThan (maxAbsDiff (out[0], delayed (x, latency), badAt + 64 + latency), 1.0e-6f);
            }
        }
    }

    void testHugeFiniteInput()
    {
        beginTest ("huge finite input (+-1e20) does not poison any engine: output stays finite and recovers (limiter off)");

        const auto rmsFrom = [] (const std::vector<float>& v, int from)
        {
            double sum = 0.0;
            for (size_t i = (size_t) from; i < v.size(); ++i)
                sum += (double) v[i] * v[i];
            return std::sqrt (sum / (double) (v.size() - (size_t) from));
        };

        for (int engine : kAllEngines)
        {
            const double rate = 48000.0;
            const int total = (int) rate * 4, badAt = 4800;
            const auto clean = nsp::test::whiteNoise (total, 0.1f, 13);
            auto x = clean;

            for (int i = 0; i < 64; ++i)
                x[(size_t) (badAt + i)] = (i % 2 == 0) ? 1.0e20f : -1.0e20f;

            std::vector<std::vector<float>> outs;

            for (const std::vector<float>* input : { &clean, (const std::vector<float>*) &x })
            {
                NoiseSuppressionProcessor p;
                setParam (p, nsp::ids::engine, (float) engine);
                setParam (p, nsp::ids::limiter, 0.0f);
                preparePlay (p, rate, 480);
                outs.push_back (runBlocks (p, Signal { *input, *input }, { 480 })[0]);
            }

            int nonFinite = 0;
            for (float v : outs[1])
                if (! std::isfinite (v))
                    ++nonFinite;

            const juce::String label = "engine " + juce::String (engine);
            expectEquals (nonFinite, 0, label);

            // the last second behaves like a run that never saw the burst (within 6 dB)
            const double rClean = rmsFrom (outs[0], total - (int) rate), rBad = rmsFrom (outs[1], total - (int) rate);
            expect (rBad < rClean * 2.0 && rBad > rClean * 0.5,
                    label + ": last-second rms " + juce::String (rBad) + " vs clean " + juce::String (rClean));
        }
    }

    void testMono()
    {
        beginTest ("mono layout processes exactly like one channel of stereo");

        NoiseSuppressionProcessor monoP, stereoP;
        expect (monoP.setBusesLayout (layoutOf (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
        preparePlay (monoP, 48000.0, 512);
        preparePlay (stereoP, 48000.0, 512);
        expectEquals (monoP.getLatencySamples(), stereoP.getLatencySamples());

        const auto x = noiseBurst (48000.0, 10);
        const auto outMono = runBlocks (monoP, { x }, { 512 });
        const auto outStereo = runBlocks (stereoP, { x, x }, { 512 });
        expectLessThan (maxAbsDiff (outMono[0], outStereo[0]), 1.0e-6f);
        expectEquals (monoP.getUnderrunCount(), 0);
    }

    void testLayouts()
    {
        beginTest ("layouts: mono/mono and stereo/stereo accepted; in != out and >2 channels rejected");

        NoiseSuppressionProcessor p;
        using Set = juce::AudioChannelSet;
        expect (p.checkBusesLayoutSupported (layoutOf (Set::mono(), Set::mono())));
        expect (p.checkBusesLayoutSupported (layoutOf (Set::stereo(), Set::stereo())));
        expect (! p.checkBusesLayoutSupported (layoutOf (Set::mono(), Set::stereo())));
        expect (! p.checkBusesLayoutSupported (layoutOf (Set::stereo(), Set::mono())));
        expect (! p.checkBusesLayoutSupported (layoutOf (Set::quadraphonic(), Set::quadraphonic())));
        expect (! p.checkBusesLayoutSupported (layoutOf (Set::discreteChannels (4), Set::discreteChannels (4))));
    }

    static constexpr float kCeilingBound = 0.8912510f;   // -1 dBFS sample peak

    static float maxAbsFrom (const std::vector<float>& x, int from)
    {
        float m = 0.0f;
        for (size_t i = (size_t) juce::jmax (0, from); i < x.size(); ++i)
            m = juce::jmax (m, std::abs (x[i]));
        return m;
    }

    void testLimiterOn()
    {
        beginTest ("limiter on (default): +6 dBFS sine through each of the 5 engines -> max |out| <= 0.8912510 after the latency");

        const auto x = nsp::test::sine (2 * 48000, 1000.0, 48000.0, juce::Decibels::decibelsToGain (6.0f));

        for (int engine : kAllEngines)
        {
            NoiseSuppressionProcessor p;
            expect (p.apvts.getRawParameterValue (nsp::ids::limiter)->load() > 0.5f, "limiter on by default");
            setParam (p, nsp::ids::engine, (float) engine);
            setSuppressMinus5 (p);
            preparePlay (p, 48000.0, 512);
            const auto out = runBlocks (p, { x, x }, { 512 });
            const int latency = p.getLatencySamples();
            const float peak = juce::jmax (maxAbsFrom (out[0], latency), maxAbsFrom (out[1], latency));
            expectLessOrEqual (peak, kCeilingBound, "engine " + juce::String (engine));
            logMessage ("    engine " + juce::String (engine) + ": limited peak " + juce::String (peak, 7));
        }

        beginTest ("limiter on: speech-like signal peaking at +6 dBFS through each of the 5 engines (RNNoise / DFN keep speech) -> max |out| <= 0.8912510");

        auto speech = nsp::test::speechLike (3 * 48000, 21);
        float speechPeak = 0.0f;
        for (float v : speech) speechPeak = juce::jmax (speechPeak, std::abs (v));
        for (auto& v : speech) v *= juce::Decibels::decibelsToGain (6.0f) / speechPeak;

        for (int engine : kAllEngines)
        {
            NoiseSuppressionProcessor on, off;
            for (auto* p : { &on, &off })
            {
                setParam (*p, nsp::ids::engine, (float) engine);
                preparePlay (*p, 48000.0, 512);
            }
            setParam (off, nsp::ids::limiter, 0.0f);
            const auto a = runBlocks (on, { speech, speech }, { 512 });
            const auto b = runBlocks (off, { speech, speech }, { 512 });
            const float peakOn = juce::jmax (maxAbsFrom (a[0], 0), maxAbsFrom (a[1], 0));
            expectLessOrEqual (peakOn, kCeilingBound, "engine " + juce::String (engine));
            expectGreaterThan (juce::jmax (maxAbsFrom (b[0], 0), maxAbsFrom (b[1], 0)), kCeilingBound,
                               "limiter-off peak exceeds the ceiling (engine " + juce::String (engine) + ")");
            logMessage ("    engine " + juce::String (engine) + ": speech peak limiter off " + juce::String (maxAbsFrom (b[0], 0), 7)
                        + ", on " + juce::String (peakOn, 7));
        }
    }

    void testLimiterOff()
    {
        beginTest ("limiter off: +6 dBFS sine passes unclamped (Classic), processed path unchanged (wet + residual == dry)");

        const auto x = nsp::test::sine (2 * 48000, 1000.0, 48000.0, juce::Decibels::decibelsToGain (6.0f));

        NoiseSuppressionProcessor wetP, resP;
        for (auto* p : { &wetP, &resP })
        {
            setParam (*p, nsp::ids::engine, (float) nsp::kEngineClassic);
            setSuppressMinus5 (*p);
            setParam (*p, nsp::ids::limiter, 0.0f);
        }
        setParam (resP, nsp::ids::monitorResidual, 1.0f);
        preparePlay (wetP, 48000.0, 512);
        preparePlay (resP, 48000.0, 512);

        const auto wet = runBlocks (wetP, { x, x }, { 512 });
        const auto res = runBlocks (resP, { x, x }, { 512 });
        const int latency = wetP.getLatencySamples();
        const float peak = maxAbsFrom (wet[0], latency);
        expectGreaterThan (peak, kCeilingBound, "Classic passes the +6 dBFS sine above the ceiling");
        logMessage ("    Classic, limiter off: peak " + juce::String (peak, 7));

        // the unlimited processed path is the original one: wet + residual reconstructs the dry signal
        const auto ref = delayed (x, latency);
        std::vector<float> sum (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            sum[i] = wet[0][i] + res[0][i];
        expectLessThan (nsp::test::maxAbsDiffDb (sum.data(), ref.data(), (int) x.size()), -60.0f);

        beginTest ("limiter on vs off: a -12 dBFS sine (never above the ceiling) gives bit-identical output");
        {
            const auto quiet = nsp::test::sine (48000, 1000.0, 48000.0, juce::Decibels::decibelsToGain (-12.0f));
            NoiseSuppressionProcessor on, off;
            for (auto* p : { &on, &off })
            {
                setParam (*p, nsp::ids::engine, (float) nsp::kEngineClassic);
                setSuppressMinus5 (*p);
            }
            setParam (off, nsp::ids::limiter, 0.0f);
            preparePlay (on, 48000.0, 512);
            preparePlay (off, 48000.0, 512);
            const auto a = runBlocks (on, { quiet, quiet }, { 512 });
            const auto b = runBlocks (off, { quiet, quiet }, { 512 });
            int diffs = 0;
            for (size_t ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < quiet.size(); ++i)
                    if (a[ch][i] != b[ch][i]) ++diffs;
            expectEquals (diffs, 0);
        }
    }

    void testLimiterBypassed()
    {
        beginTest ("bypassed: +6 dBFS sine -> output == input delayed, bit-exact (limiter not applied)");

        const auto x = nsp::test::sine (48000, 1000.0, 48000.0, juce::Decibels::decibelsToGain (6.0f));

        for (int engine : kAllEngines)
        {
            NoiseSuppressionProcessor p;
            setParam (p, nsp::ids::engine, (float) engine);
            setParam (p, nsp::ids::bypass, 1.0f);   // limiter stays at its default (on)
            preparePlay (p, 48000.0, 512);
            const int latency = p.getLatencySamples();
            const auto out = runBlocks (p, { x, x }, { 512, 100, 333 });
            const auto ref = delayed (x, latency);
            int mismatches = 0;
            for (size_t ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < x.size(); ++i)
                    if (out[ch][i] != ref[i]) ++mismatches;
            expectEquals (mismatches, 0, "engine " + juce::String (engine));
        }
    }
};

static ProcessorTests processorTests;
