#include <JuceHeader.h>
#include <cmath>
#include <limits>
#include <vector>
#include "../Source/Host/SafetyLimiter.h"
#include "AllocCounter.h"
#include "TestSignals.h"

namespace
{
constexpr double kRate = 48000.0;
const float kPlus6 = juce::Decibels::decibelsToGain (6.0f);

class SafetyLimiterTests final : public juce::UnitTest
{
public:
    SafetyLimiterTests() : juce::UnitTest ("Safety limiter", "Limiter") {}

    void runTest() override
    {
        using nsp::SafetyLimiter;

        beginTest ("constants: ceiling -1 dBFS sample peak, release 100 ms");
        {
            expectWithinAbsoluteError (SafetyLimiter::kCeiling, juce::Decibels::decibelsToGain (-1.0f), 1.0e-7f);
            expectEquals (SafetyLimiter::kReleaseSeconds, 0.1);
        }

        beginTest ("below the ceiling: a -3 dBFS sine passes bit-identical");
        {
            SafetyLimiter lim;
            lim.prepare (kRate);
            const auto x = nsp::test::sine (48000, 1000.0, kRate, juce::Decibels::decibelsToGain (-3.0f));
            auto l = x, r = x;
            float* ch[] = { l.data(), r.data() };
            lim.process (ch, 2, (int) l.size());
            int diffs = 0;
            for (size_t i = 0; i < x.size(); ++i)
                if (l[i] != x[i] || r[i] != x[i]) ++diffs;
            expectEquals (diffs, 0);
            expect (lim.currentGain() == 1.0f);
        }

        beginTest ("+6 dBFS sine (and white noise at +6 dBFS): every output sample |x| <= kCeiling");
        {
            for (int kind = 0; kind < 2; ++kind)
            {
                SafetyLimiter lim;
                lim.prepare (kRate);
                auto l = kind == 0 ? nsp::test::sine (48000, 1000.0, kRate, kPlus6)
                                   : nsp::test::whiteNoise (48000, 1.0f, 5);
                if (kind == 1)   // peak-normalise the noise to +6 dBFS
                {
                    float peak = 0.0f;
                    for (float v : l) peak = juce::jmax (peak, std::abs (v));
                    for (auto& v : l) v *= kPlus6 / peak;
                }
                auto r = l;
                for (auto& v : r) v *= -0.5f;
                float* ch[] = { l.data(), r.data() };
                lim.process (ch, 2, (int) l.size());
                float worst = 0.0f;
                for (size_t i = 0; i < l.size(); ++i)
                    worst = juce::jmax (worst, std::abs (l[i]), std::abs (r[i]));
                expectLessOrEqual (worst, SafetyLimiter::kCeiling, kind == 0 ? "sine" : "noise");
            }
        }

        beginTest ("instant attack: the first over-ceiling sample is already at the ceiling");
        {
            SafetyLimiter lim;
            lim.prepare (kRate);
            std::vector<float> x (100, 0.1f);
            x[50] = 2.0f;
            float* ch[] = { x.data() };
            lim.process (ch, 1, (int) x.size());
            expect (x[49] == 0.1f);
            expectWithinAbsoluteError (x[50], SafetyLimiter::kCeiling, 1.0e-6f);
            expectLessOrEqual (x[50], SafetyLimiter::kCeiling);
        }

        beginTest ("release: after a single +6 dBFS spike, gain recovers to >= 0.99 of 1 within 0.5 s and is exactly 1 within 2 s");
        {
            SafetyLimiter lim;
            lim.prepare (kRate);
            std::vector<float> spike (1, kPlus6);
            float* sp[] = { spike.data() };
            lim.process (sp, 1, 1);
            expectLessThan (lim.currentGain(), 0.5f);

            std::vector<float> zeros ((size_t) (0.5 * kRate), 0.0f);
            float* z[] = { zeros.data() };
            lim.process (z, 1, (int) zeros.size());
            expectGreaterOrEqual (lim.currentGain(), 0.99f);

            std::vector<float> more ((size_t) (1.5 * kRate), 0.0f);
            float* m[] = { more.data() };
            lim.process (m, 1, (int) more.size());
            expect (lim.currentGain() == 1.0f, juce::String (lim.currentGain(), 9));
        }

        beginTest ("stereo-linked: a spike on the left only attenuates both channels equally");
        {
            SafetyLimiter lim;
            lim.prepare (kRate);
            std::vector<float> l (64, 0.25f), r (64, 0.25f);
            l[10] = 4.0f;
            float* ch[] = { l.data(), r.data() };
            lim.process (ch, 2, 64);
            const float g = SafetyLimiter::kCeiling / 4.0f;
            expectWithinAbsoluteError (l[10], SafetyLimiter::kCeiling, 1.0e-6f);
            expectWithinAbsoluteError (r[10], 0.25f * g, 1.0e-7f);
            for (int i = 10; i < 64; ++i)
                expectWithinAbsoluteError (r[(size_t) i] / 0.25f, i == 10 ? g : l[(size_t) i] / 0.25f, 1.0e-6f);
            expectLessThan (r[11], 0.25f);
        }

        beginTest ("non-finite input: NaN and +inf samples output 0, all outputs finite, gain recovers, other channel unaffected");
        {
            SafetyLimiter lim;
            lim.prepare (kRate);
            const int n = (int) (kRate * 0.5);
            std::vector<float> l ((size_t) n, 0.25f), r ((size_t) n, 0.25f);
            l[10] = std::numeric_limits<float>::quiet_NaN();
            l[20] = std::numeric_limits<float>::infinity();
            l[30] = -std::numeric_limits<float>::infinity();
            float* ch[] = { l.data(), r.data() };
            lim.process (ch, 2, n);
            expectEquals (l[10], 0.0f);
            expectEquals (l[20], 0.0f);
            expectEquals (l[30], 0.0f);
            bool allFinite = true;
            for (int i = 0; i < n; ++i)
                allFinite = allFinite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
            expect (allFinite, "every output finite");
            expect (lim.currentGain() == 1.0f, "gain unaffected by non-finite samples");
            expectEquals (r[10], 0.25f);
            expectEquals (r[20], 0.25f);
            expectEquals (r[(size_t) n - 1], 0.25f);
        }

        beginTest ("reset(): gain back to 1");
        {
            SafetyLimiter lim;
            lim.prepare (kRate);
            std::vector<float> x (1, 3.0f);
            float* ch[] = { x.data() };
            lim.process (ch, 1, 1);
            expectLessThan (lim.currentGain(), 1.0f);
            lim.reset();
            expect (lim.currentGain() == 1.0f);
        }

        beginTest ("process does not allocate");
        {
            SafetyLimiter lim;
            lim.prepare (kRate);
            auto l = nsp::test::whiteNoise (4800, 2.0f, 9);
            auto r = l;
            float* ch[] = { l.data(), r.data() };
            int count = 0;
            {
                nsp::test::ScopedAllocCheck check;
                lim.process (ch, 2, (int) l.size());
                count = check.count();
            }
            expectEquals (count, 0);
        }
    }
};

static SafetyLimiterTests safetyLimiterTests;
} // namespace
