#pragma once

#include <cmath>

namespace nsp
{
/** Output safety net: -1 dBFS sample-peak ceiling, instant attack, exponential release
    (tau = kReleaseSeconds), stereo-linked, no look-ahead (adds no latency). While the gain is
    exactly 1 the samples pass bit-identical. Real-time safe: no allocation, no locks. */
class SafetyLimiter
{
public:
    static constexpr float kCeiling = 0.891250938f;   // -1 dBFS sample peak
    static constexpr double kReleaseSeconds = 0.1;

    void prepare (double sampleRate) noexcept
    {
        releaseCoef = sampleRate > 0.0 ? 1.0 - std::exp (-1.0 / (kReleaseSeconds * sampleRate)) : 1.0;
        reset();
    }

    void reset() noexcept { gain = 1.0; }

    /** In place. The same gain is applied to every channel (stereo-linked). */
    void process (float* const* channels, int numChannels, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float a = std::abs (channels[ch][i]);
                if (a > peak && a <= kFloatMax)   // NaN and inf never drive the gain (they are zeroed below)
                    peak = a;
            }

            const double target = peak > kCeiling ? (double) kCeiling / (double) peak : 1.0;
            const double released = gain + (1.0 - gain) * releaseCoef;
            gain = target < released ? target : released;

            if (gain > 0.99999)
                gain = 1.0;

            const float g = (float) gain;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float x = channels[ch][i];

                if (! (std::abs (x) <= kFloatMax))   // NaN or +-inf: output 0
                {
                    channels[ch][i] = 0.0f;
                    continue;
                }

                if (gain == 1.0)
                    continue;            // idle: finite samples pass bit-identical

                const float y = x * g;
                // float rounding of g * peak may land one ulp above the ceiling: pin it
                channels[ch][i] = y > kCeiling ? kCeiling : (y < -kCeiling ? -kCeiling : y);
            }
        }
    }

    static constexpr float kFloatMax = 3.4028234663852886e38f;

    float currentGain() const noexcept { return (float) gain; }

private:
    // double: in float, (1 - gain) * releaseCoef rounds to 0 near 1 and the release would stall below the snap
    double gain = 1.0;
    double releaseCoef = 1.0;
};
} // namespace nsp
