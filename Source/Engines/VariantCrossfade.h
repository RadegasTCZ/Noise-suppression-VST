#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace nsp
{
/** In-engine variant switching, shared by WebRtcEngine (level) and RnnoiseEngine (model).

    Only the active variant runs. When the requested variant differs from the active one, at the start
    of the next frame (the channel-0 call) the incoming variant is reset and pre-rolled over the
    engine's own last preRollFrames input frames of every channel (output discarded), so its output is
    already time-aligned. For that one frame both variants run and the output is the equal-power
    crossfade old * cos + new * sin, t = (i + 0.5) / 480 (as EngineHost). From the next frame on only
    the incoming variant runs. A request made during the crossfade frame is served at the next frame
    start, so never more than two variants run in one frame.

    Channels must be processed in lockstep, channel 0 first, as EngineHost does.
    prepare() allocates (message thread); everything else is allocation-free and lock-free.
    JUCE-free on purpose (plain standard C++, nothing beyond C++17). */
class VariantCrossfade
{
public:
    static constexpr int kFrame = 480;
    static constexpr int kMaxChannels = 2;

    void prepare (int numChannels, int preRollFrames)
    {
        channels = std::clamp (numChannels, 1, kMaxChannels);
        preRoll = std::max (0, preRollFrames);

        for (auto& h : history)
            h.assign ((size_t) (preRoll * kFrame), 0.0f);

        for (int i = 0; i < kFrame; ++i)
        {
            const double t = 1.5707963267948966 * ((double) i + 0.5) / (double) kFrame;
            fadeOut[(size_t) i] = (float) std::cos (t);
            fadeIn[(size_t) i]  = (float) std::sin (t);
        }

        reset();
    }

    /** Adopts the requested variant immediately (no crossfade) and clears the input history.
        The engine resets the active variant's DSP state itself. */
    void reset() noexcept
    {
        active = requested;
        incoming = requested;
        fading = false;
        lastRun = 1;

        for (auto& h : history)
            std::fill (h.begin(), h.end(), 0.0f);
    }

    void setRequested (int variant) noexcept       { requested = variant; }
    int  requestedVariant() const noexcept         { return requested; }
    /** The variant heard once the current crossfade (if any) has finished. */
    int  activeVariant() const noexcept            { return fading ? incoming : active; }
    /** 1 normally, 2 in a crossfade frame (test hook). */
    int  variantsRunLastFrame() const noexcept     { return lastRun; }

    /** One kFrame-sample frame of one channel. in == out allowed.
        resetVariant (int variant): reset every channel of that variant, allocation-free.
        runVariant (int variant, int channel, const float* in, float* out): one frame of DSP. */
    template <typename ResetVariant, typename RunVariant>
    void process (int channel, const float* in, float* out, ResetVariant&& resetVariant, RunVariant&& runVariant) noexcept
    {
        if (channel == 0)
        {
            if (fading)
            {
                active = incoming;
                fading = false;
            }

            if (requested != active)
            {
                incoming = requested;
                fading = true;
                resetVariant (incoming);

                for (int f = 0; f < preRoll; ++f)
                    for (int ch = 0; ch < channels; ++ch)
                        runVariant (incoming, ch, history[(size_t) ch].data() + f * kFrame, scratch.data());
            }

            lastRun = fading ? 2 : 1;
        }

        std::memcpy (inCopy.data(), in, sizeof (float) * (size_t) kFrame);
        runVariant (active, channel, inCopy.data(), out);

        if (fading)
        {
            runVariant (incoming, channel, inCopy.data(), scratch.data());

            for (size_t i = 0; i < (size_t) kFrame; ++i)
                out[i] = out[i] * fadeOut[i] + scratch[i] * fadeIn[i];
        }

        if (preRoll > 0 && channel >= 0 && channel < channels)
        {
            auto& h = history[(size_t) channel];
            std::memmove (h.data(), h.data() + kFrame, sizeof (float) * (h.size() - (size_t) kFrame));
            std::memcpy (h.data() + h.size() - (size_t) kFrame, inCopy.data(), sizeof (float) * (size_t) kFrame);
        }
    }

private:
    std::array<std::vector<float>, kMaxChannels> history;   // last preRoll input frames per channel, oldest first
    std::array<float, kFrame> inCopy {}, scratch {}, fadeIn {}, fadeOut {};
    int channels = 1, preRoll = 0;
    int requested = 0, active = 0, incoming = 0, lastRun = 1;
    bool fading = false;
};
} // namespace nsp
