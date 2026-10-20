#include <JuceHeader.h>
#include <cmath>
#include <cstring>
#include <type_traits>
#include "../Source/Engines/SafetyFloor.h"

// RNNoise resets its DenoiseState (which holds an NspFloor) by memcpy of a pristine snapshot.
static_assert (std::is_trivially_copyable_v<NspFloor>, "NspFloor must stay trivially copyable");

namespace
{
/** floorE per unit of tracked noise energy, before the cap. */
constexpr float kPerUnit = NSP_FLOOR_BELOW * NSP_FLOOR_BIAS;

class SafetyFloorTests final : public juce::UnitTest
{
public:
    SafetyFloorTests() : juce::UnitTest ("Safety floor", "SafetyFloor") {}

    static void hop (NspFloor& f, float e, float* fl, float capE = 1e9f)
    {
        const float noisy[4] = { e, e, e, e };
        nsp_floor_update (&f, noisy, 4, capE, fl);
    }

    void runTest() override
    {
        beginTest ("stationary noise: floor = noise x NSP_FLOOR_BELOW x NSP_FLOOR_BIAS per band after one hop");
        {
            NspFloor f;
            nsp_floor_reset (&f);
            const float noisy[4] = { 1.0f, 2.0f, 4.0f, 8.0f };
            float fl[4] = {};
            nsp_floor_update (&f, noisy, 4, 1e9f, fl);
            for (int b = 0; b < 4; ++b)
                expectLessThan (std::abs (fl[b] - kPerUnit * noisy[b]) / (kPerUnit * noisy[b]), 1e-6f);
        }

        beginTest ("lasting drop in noise: the new lower level is reached within one hop");
        {
            NspFloor f;
            nsp_floor_reset (&f);
            float fl[4] = {};
            for (int i = 0; i < 300; ++i)
                hop (f, 1.0f, fl);
            hop (f, 0.1f, fl);
            for (int b = 0; b < 4; ++b)
                expectLessThan (std::abs (fl[b] - 0.1f * kPerUnit) / (0.1f * kPerUnit), 1e-6f);
        }

        beginTest ("windowed minimum: a 150 ms dip 25 dB down inside steady noise -> floor back within 1 dB of its pre-dip value no later than 1.6 s after the dip ends (every alignment to the sub-windows)");
        {
            const float dip = std::pow (10.0f, -2.5f);
            float worstLateDb = 0.0f;
            int shortestHold = 1 << 30;
            for (int offset = 0; offset < NSP_FLOOR_SUBWIN_HOPS; ++offset)
            {
                NspFloor f;
                nsp_floor_reset (&f);
                float fl[4] = {};
                for (int i = 0; i < 300 + offset; ++i)
                    hop (f, 1.0f, fl);
                const float before = fl[0];
                for (int i = 0; i < 15; ++i)
                    hop (f, dip, fl);
                // hops after the dip: count how long the dip is still held, then check the level at 1.6 s
                int held = 0;
                for (int i = 1; i <= 160; ++i)
                {
                    hop (f, 1.0f, fl);
                    if (fl[0] < before * 0.5f)
                        held = i;
                }
                shortestHold = juce::jmin (shortestHold, held);
                worstLateDb = juce::jmin (worstLateDb, 10.0f * std::log10 (fl[0] / before));
                expectWithinAbsoluteError (10.0f * std::log10 (fl[0] / before), 0.0f, 1.0f,
                                           "dip at offset " + juce::String (offset));
            }
            logMessage ("    1.6 s after the dip: worst " + juce::String (worstLateDb, 2) + " dB vs before; the dip is held for at least "
                        + juce::String (shortestHold) + " hops");
            // A minimum, not a fast follower: the dip is remembered for at least a second (pauses between words).
            expectGreaterOrEqual (shortestHold, 100);
        }

        beginTest ("10 s of keystrokes (one 30 dB burst every 100 ms) with quiet gaps raise the floor by less than 3 dB");
        {
            NspFloor f;
            nsp_floor_reset (&f);
            float fl[4] = {};
            float highest = 0.0f;
            for (int i = 0; i < 1000; ++i)
            {
                hop (f, (i % 10 == 0) ? 1000.0f : 1.0f, fl);
                if (i > 0)
                    highest = juce::jmax (highest, fl[0]);
            }
            logMessage ("    highest floor after the first hop: " + juce::String (10.0f * std::log10 (highest / kPerUnit), 2) + " dB vs quiet");
            expectLessThan (fl[0], kPerUnit * 1.995f);
            expectLessThan (highest, kPerUnit * 1.995f);
        }

        beginTest ("cap: total floor energy never exceeds capE, shape kept");
        {
            NspFloor f;
            nsp_floor_reset (&f);
            const float noisy[4] = { 1.0f, 1.0f, 2.0f, 4.0f };
            float fl[4] = {};
            nsp_floor_update (&f, noisy, 4, 0.004f, fl);
            expectWithinAbsoluteError (fl[0] + fl[1] + fl[2] + fl[3], 0.004f, 1e-7f);
            expectWithinAbsoluteError (fl[3] / fl[0], 4.0f, 1e-4f);
        }

        beginTest ("digital silence: noisyE 0 gives floor 0 and fill gain 0, never NaN; tracker restarts from the next non-zero hop");
        {
            NspFloor f;
            nsp_floor_reset (&f);
            float fl[4] = {};
            for (int i = 0; i < 50; ++i)
                hop (f, 4.0f, fl);
            for (int i = 0; i < 5; ++i)
            {
                fl[0] = fl[1] = fl[2] = fl[3] = 5.0f;
                hop (f, 0.0f, fl);
                for (int b = 0; b < 4; ++b)
                    expect (fl[b] == 0.0f);
            }
            expect (nsp_floor_fill_gain (0.0f, 0.0f, 0.0f) == 0.0f);
            hop (f, 1.0f, fl);
            for (int b = 0; b < 4; ++b)
            {
                expect (std::isfinite (fl[b]));
                expectWithinAbsoluteError (fl[b], kPerUnit, kPerUnit * 1e-6f);
            }
            // and the zeros are not remembered as a minimum
            for (int i = 0; i < 10; ++i)
                hop (f, 1.0f, fl);
            expectWithinAbsoluteError (fl[0], kPerUnit, kPerUnit * 1e-6f);
        }

        beginTest ("speech first: 50 hops at 100 then noise at 1e-4 -> floor 1e-4 x below x bias on the first quiet hop");
        {
            NspFloor f;
            nsp_floor_reset (&f);
            float fl[4] = {};
            for (int i = 0; i < 50; ++i)
                hop (f, 100.0f, fl);
            hop (f, 1e-4f, fl);
            for (int b = 0; b < 4; ++b)
                expectLessThan (std::abs (fl[b] - 1e-4f * kPerUnit) / (1e-4f * kPerUnit), 1e-5f);
        }

        beginTest ("fill gain: outE >= floorE -> 0; outE < floorE -> sqrt(floorE / noisyE)");
        {
            expect (nsp_floor_fill_gain (4.0f, 0.02f, 0.01f) == 0.0f);
            expectWithinAbsoluteError (nsp_floor_fill_gain (4.0f, 0.001f, 0.01f), 0.05f, 1e-6f);
        }

        beginTest ("reset() zeroes the tracker: all bytes zero, and the next hop starts fresh");
        {
            NspFloor f;
            nsp_floor_reset (&f);
            float fl[4] = {};
            for (int i = 0; i < 40; ++i)
                hop (f, 1.0f, fl);
            nsp_floor_reset (&f);
            NspFloor zero;
            std::memset (&zero, 0, sizeof (zero));
            expect (std::memcmp (&f, &zero, sizeof (NspFloor)) == 0);
            hop (f, 5.0f, fl);
            expectWithinAbsoluteError (fl[0], 5.0f * kPerUnit, 5.0f * kPerUnit * 1e-6f);
        }
    }
};

SafetyFloorTests safetyFloorTests;
}
