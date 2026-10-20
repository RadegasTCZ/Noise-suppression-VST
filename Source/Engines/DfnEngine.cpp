#include "DfnEngine.h"
#include "../Params.h"
#include <algorithm>

namespace nsp
{
DfnEngine::DfnEngine()
    : DfnEngine (Blob { BinaryData::dfn3_standard_bin, BinaryData::dfn3_standard_binSize })
{
}

DfnEngine::DfnEngine (Blob blob)
    : source (blob)
{
}

void DfnEngine::loadModel()
{
    attempted = true;
    // Model::load copies and transposes the weights out of the blob, so the blob need not outlive it.
    // The Model is a local: Network::init copies the weights again, so ~8.5 MB are freed on return.
    dfn::Model model;
    const bool ok = source.data != nullptr && source.size > 0
                 && model.load (source.data, (size_t) source.size)
                 && network.init (model);
    loaded = ok;
    failed.store (! ok, std::memory_order_relaxed);
}

void DfnEngine::prepare (int channels)
{
    jassert (channels >= 1 && channels <= kMaxChannels);
    numChannels = juce::jlimit (1, kMaxChannels, channels);

    if (! attempted)
        loadModel();

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        auto& d = passthrough[(size_t) ch];
        d.prepare (kLatency);
        d.setDelay (kLatency);

        if (loaded)
        {
            if (denoiser[(size_t) ch] == nullptr)
                denoiser[(size_t) ch] = std::make_unique<dfn::Denoiser>();

            if (! denoiser[(size_t) ch]->prepare (network))
            {
                loaded = false;                        // allocation failure: fall back to passthrough
                failed.store (true, std::memory_order_relaxed);
            }
            denoiser[(size_t) ch]->setSafetyFloor (true);   // always on in the engine (spec: no parameter)
        }
    }

    reset();
    setStrength (requested);
    reset();   // adopt the strength immediately
}

void DfnEngine::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        passthrough[(size_t) ch].reset();
        if (loaded && denoiser[(size_t) ch] != nullptr)
            denoiser[(size_t) ch]->reset();
    }
}

void DfnEngine::processFrame (int channel, const float* in, float* out)
{
    jassert (channel >= 0 && channel < numChannels);
    if (channel < 0 || channel >= numChannels)
    {
        std::fill (out, out + kFrameSize, 0.0f);
        return;
    }

    if (loaded)
        denoiser[(size_t) channel]->process (in, out);
    else
        passthrough[(size_t) channel].process (in, out, kFrameSize);
}

void DfnEngine::setStrength (int s) noexcept
{
    requested = juce::jlimit (0, (int) kNumStrengths - 1, s);
    const auto& p = kPresets[requested];

    if (loaded)
        for (auto& d : denoiser)
            if (d != nullptr)
                d->setStrength (p.attenLimDb, p.postFilterBeta, false);
}

void DfnEngine::setSafetyFloorForTest (bool on) noexcept
{
    for (auto& d : denoiser)
        if (d != nullptr)
            d->setSafetyFloor (on);
}

void DfnEngine::updateParameters (const juce::AudioProcessorValueTreeState& s)
{
    if (auto* p = s.getRawParameterValue (ids::dfnStrength))   // nullptr if the state has no such parameter
        setStrength (juce::roundToInt (p->load (std::memory_order_relaxed)));
}
} // namespace nsp
