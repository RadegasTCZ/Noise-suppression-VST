#include "RnnoiseEngine.h"
#include "rnnoise.h"   // ThirdParty/rnnoise/include; has its own extern "C" guard
#include "../Params.h"
#include <cstdlib>
#include <cstring>

namespace nsp
{
RnnoiseEngine::RnnoiseEngine()
    : RnnoiseEngine (Blob { BinaryData::rnnoise_standard_bin, BinaryData::rnnoise_standard_binSize },
                     Blob { BinaryData::rnnoise_light_bin, BinaryData::rnnoise_light_binSize })
{
}

RnnoiseEngine::RnnoiseEngine (Blob standard, Blob light)
    : source { standard, light }
{
    static_assert (kPreRollFrames == 2, "RNNoise latency is two frames");
    // RNNoise frames are exactly the engine frames (480 at 48 kHz).
    jassert (rnnoise_get_frame_size() == kFrameSize);
}

RnnoiseEngine::~RnnoiseEngine()
{
    // rnnoise_model_from_buffer leaves RNNModel::file uninitialised, so rnnoise_model_free could
    // fclose garbage: release with free() (the same CRT malloc'd it). See ThirdParty/UPSTREAM.md.
    for (auto*& m : model)
    {
        std::free (m);
        m = nullptr;
    }
}

void RnnoiseEngine::loadModels()
{
    attempted = true;
    const auto stateSize = (size_t) rnnoise_get_size();

    for (int m = 0; m < kNumModels; ++m)
    {
        const auto& src = source[(size_t) m];
        auto& bytes = blob[(size_t) m];

        // BinaryData arrays are only byte-aligned and RNNoise reads weights in place, so keep an
        // owned, operator-new-aligned copy that lives as long as the engine.
        if (src.data != nullptr && src.size > 0)
            bytes.assign ((const unsigned char*) src.data, (const unsigned char*) src.data + src.size);

        pristine[(size_t) m].assign (stateSize, 0);

        if (! bytes.empty())
            model[(size_t) m] = rnnoise_model_from_buffer (bytes.data(), (int) bytes.size());

        // rnnoise_init allocates (parse_weights): message thread only. Its result is snapshotted.
        const bool ok = model[(size_t) m] != nullptr
                     && rnnoise_init (reinterpret_cast<DenoiseState*> (pristine[(size_t) m].data()), model[(size_t) m]) == 0;
        loaded[(size_t) m] = ok;
        failed[(size_t) m].store (! ok, std::memory_order_relaxed);

        // Safety floor (local RNNoise patch, see ThirdParty/UPSTREAM.md): always on in the engine (spec: no
        // parameter). Set on the snapshot, so every state copied or reset from it has it, with a fresh tracker.
        if (ok)
            rnnoise_set_safety_floor (reinterpret_cast<DenoiseState*> (pristine[(size_t) m].data()), 1);
    }
}

void RnnoiseEngine::setSafetyFloorForTest (bool on) noexcept
{
    for (int m = 0; m < kNumModels; ++m)
    {
        if (! loaded[(size_t) m])
            continue;
        rnnoise_set_safety_floor (reinterpret_cast<DenoiseState*> (pristine[(size_t) m].data()), on ? 1 : 0);
        for (int ch = 0; ch < kMaxChannels; ++ch)
            if (! state[(size_t) m][(size_t) ch].empty())
                rnnoise_set_safety_floor (stateOf (m, ch), on ? 1 : 0);
    }
}

void RnnoiseEngine::prepare (int channels)
{
    jassert (channels >= 1 && channels <= kMaxChannels);
    numChannels = juce::jlimit (1, kMaxChannels, channels);

    if (! attempted)
        loadModels();

    for (int m = 0; m < kNumModels; ++m)
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            state[(size_t) m][(size_t) ch] = pristine[(size_t) m];   // copies the snapshot (allocates: message thread)
            auto& d = passthrough[(size_t) m][(size_t) ch];
            d.prepare (kLatency);
            d.setDelay (kLatency);
        }

    xfade.prepare (numChannels, kPreRollFrames);
    reset();
}

DenoiseState* RnnoiseEngine::stateOf (int m, int ch) noexcept
{
    return reinterpret_cast<DenoiseState*> (state[(size_t) m][(size_t) ch].data());
}

void RnnoiseEngine::resetModel (int m) noexcept
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        auto& s = state[(size_t) m][(size_t) ch];
        if (! s.empty())
            std::memcpy (s.data(), pristine[(size_t) m].data(), s.size());   // == rnnoise_init, no allocation
        passthrough[(size_t) m][(size_t) ch].reset();
    }
}

void RnnoiseEngine::reset()
{
    xfade.reset();                       // adopts the requested model without a crossfade
    resetModel (xfade.activeVariant());
}

void RnnoiseEngine::runModel (int m, int ch, const float* in, float* out) noexcept
{
    if (! loaded[(size_t) m])
    {
        passthrough[(size_t) m][(size_t) ch].process (in, out, kFrameSize);
        return;
    }

    for (int i = 0; i < kFrameSize; ++i)
        scaledIn[(size_t) i] = in[i] * 32768.0f;

    rnnoise_process_frame (stateOf (m, ch), scaledOut.data(), scaledIn.data());   // voice probability ignored

    for (int i = 0; i < kFrameSize; ++i)
        out[i] = scaledOut[(size_t) i] * (1.0f / 32768.0f);
}

void RnnoiseEngine::processFrame (int channel, const float* in, float* out)
{
    jassert (channel >= 0 && channel < numChannels);
    if (channel < 0 || channel >= numChannels)
    {
        std::fill (out, out + kFrameSize, 0.0f);
        return;
    }

    xfade.process (channel, in, out,
                   [this] (int m) { resetModel (m); },
                   [this] (int m, int ch, const float* x, float* y) { runModel (m, ch, x, y); });
}

void RnnoiseEngine::setModel (int m) noexcept
{
    xfade.setRequested (juce::jlimit (0, kNumModels - 1, m));
}

bool RnnoiseEngine::modelFailed (int m) const noexcept
{
    return juce::isPositiveAndBelow (m, (int) kNumModels) && failed[(size_t) m].load (std::memory_order_relaxed);
}

void RnnoiseEngine::updateParameters (const juce::AudioProcessorValueTreeState& s)
{
    if (auto* p = s.getRawParameterValue (ids::rnnModel))   // nullptr if the state has no such parameter
        setModel (juce::roundToInt (p->load (std::memory_order_relaxed)));
}
} // namespace nsp
