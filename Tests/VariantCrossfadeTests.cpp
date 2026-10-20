#include <JuceHeader.h>
#include "../Source/Engines/VariantCrossfade.h"
#include "AllocCounter.h"
#include "TestSignals.h"
#include <array>
#include <cmath>
#include <set>
#include <vector>

namespace
{
constexpr int F = nsp::VariantCrossfade::kFrame;

/** Four stateful fake variants: variant v outputs gain[v] * (its input delayed by delayFrames frames),
    per channel. Records resets and which variants ran. */
struct FakeVariants
{
    static constexpr int kVariants = 4;
    const std::array<float, kVariants> gain { 1.0f, 0.5f, 0.25f, 2.0f };
    int delayFrames = 1;
    std::array<std::array<std::vector<float>, 2>, kVariants> mem;
    std::array<int, kVariants> resets {};
    std::array<bool, kVariants> ran {};   // variants run since clearRan() (fixed storage: no allocation)

    void clearRan() { ran.fill (false); }

    std::set<int> ranSet() const
    {
        std::set<int> s;
        for (int v = 0; v < kVariants; ++v)
            if (ran[(size_t) v])
                s.insert (v);
        return s;
    }

    explicit FakeVariants (int delay) : delayFrames (delay)
    {
        for (auto& v : mem)
            for (auto& ch : v)
                ch.assign ((size_t) (delayFrames * F), 0.0f);
    }

    void reset (int v)
    {
        ++resets[(size_t) v];
        for (auto& ch : mem[(size_t) v])
            std::fill (ch.begin(), ch.end(), 0.0f);
    }

    void run (int v, int ch, const float* in, float* out)
    {
        ran[(size_t) v] = true;
        auto& m = mem[(size_t) v][(size_t) ch];
        float frame[F];
        std::copy (in, in + F, frame);                                   // in may alias out
        for (int i = 0; i < F; ++i)
            out[i] = gain[(size_t) v] * m[(size_t) i];
        std::copy (m.begin() + F, m.end(), m.begin());
        std::copy (frame, frame + F, m.end() - F);
    }
};

struct Rig
{
    nsp::VariantCrossfade xf;
    FakeVariants fake;

    Rig (int numChannels, int preRoll, int delay, int initialVariant) : fake (delay)
    {
        xf.setRequested (initialVariant);
        xf.prepare (numChannels, preRoll);
    }

    void process (int ch, const float* in, float* out)
    {
        xf.process (ch, in, out,
                    [this] (int v) { fake.reset (v); },
                    [this] (int v, int c, const float* i, float* o) { fake.run (v, c, i, o); });
    }
};

float fadeOutAt (int i) { return (float) std::cos (juce::MathConstants<double>::halfPi * ((double) i + 0.5) / F); }
float fadeInAt (int i)  { return (float) std::sin (juce::MathConstants<double>::halfPi * ((double) i + 0.5) / F); }

class VariantCrossfadeTests final : public juce::UnitTest
{
public:
    VariantCrossfadeTests() : juce::UnitTest ("Variant crossfade", "VariantCrossfade") {}

    void runTest() override
    {
        const auto x = nsp::test::whiteNoise (20 * F, 0.1f, 77);

        beginTest ("steady: only the active variant runs; output is that variant's output");
        {
            Rig r (1, 1, 1, 2);
            std::vector<float> out ((size_t) F);
            int mismatches = 0;
            for (int f = 0; f < 5; ++f)
            {
                r.fake.clearRan();
                r.process (0, x.data() + f * F, out.data());
                expect (r.fake.ranSet() == std::set<int> { 2 });
                expectEquals (r.xf.variantsRunLastFrame(), 1);
                if (f > 0)
                    for (int i = 0; i < F; ++i)
                        if (out[(size_t) i] != 0.25f * x[(size_t) ((f - 1) * F + i)])
                            ++mismatches;
            }
            expectEquals (mismatches, 0);
            expectEquals (r.fake.resets[2], 0);
        }

        beginTest ("change: incoming reset once, pre-rolled, one equal-power crossfade frame, then only the new variant");
        {
            Rig r (2, 1, 1, 0);
            std::vector<float> out0 ((size_t) F), out1 ((size_t) F);
            for (int f = 0; f < 4; ++f)
            {
                r.process (0, x.data() + f * F, out0.data());
                r.process (1, x.data() + f * F, out1.data());
            }

            r.xf.setRequested (3);
            r.fake.clearRan();
            const int f = 4;                                             // the crossfade frame
            r.process (0, x.data() + f * F, out0.data());
            r.process (1, x.data() + f * F, out1.data());
            expectEquals (r.fake.resets[3], 1);
            expect (r.fake.ranSet() == std::set<int> { 0, 3 });
            expectEquals (r.xf.variantsRunLastFrame(), 2);
            expectEquals (r.xf.activeVariant(), 3);

            float worst = 0.0f;
            for (int i = 0; i < F; ++i)
            {
                // both variants are aligned: the incoming one was pre-rolled over frame f - 1
                const float prev = x[(size_t) ((f - 1) * F + i)];
                const float expected = (1.0f * prev) * fadeOutAt (i) + (2.0f * prev) * fadeInAt (i);
                worst = juce::jmax (worst, std::abs (out0[(size_t) i] - expected), std::abs (out1[(size_t) i] - expected));
            }
            expectLessThan (worst, 1.0e-6f);

            r.fake.clearRan();
            r.process (0, x.data() + 5 * F, out0.data());
            r.process (1, x.data() + 5 * F, out1.data());
            expect (r.fake.ranSet() == std::set<int> { 3 });
            expectEquals (r.xf.variantsRunLastFrame(), 1);
            int mismatches = 0;
            for (int i = 0; i < F; ++i)
                if (out0[(size_t) i] != 2.0f * x[(size_t) (4 * F + i)])
                    ++mismatches;
            expectEquals (mismatches, 0);
        }

        beginTest ("pre-roll depth: a two-frame-delay variant is aligned when preRollFrames = 2, not when 1");
        {
            for (int preRoll : { 1, 2 })
            {
                Rig r (1, preRoll, 2, 0);
                std::vector<float> out ((size_t) F);
                for (int f = 0; f < 4; ++f)
                    r.process (0, x.data() + f * F, out.data());
                r.xf.setRequested (1);
                r.process (0, x.data() + 4 * F, out.data());

                float worst = 0.0f;
                for (int i = 0; i < F; ++i)
                {
                    const float prev2 = x[(size_t) (2 * F + i)];
                    const float expected = (1.0f * prev2) * fadeOutAt (i) + (0.5f * prev2) * fadeInAt (i);
                    worst = juce::jmax (worst, std::abs (out[(size_t) i] - expected));
                }
                if (preRoll == 2)
                    expectLessThan (worst, 1.0e-6f, "preRoll 2");
                else
                    expectGreaterThan (worst, 1.0e-3f, "preRoll 1 cannot align a two-frame delay");
            }
        }

        beginTest ("in == out is allowed (crossfade frame included)");
        {
            Rig a (1, 1, 1, 0), b (1, 1, 1, 0);
            std::vector<float> outA ((size_t) F), inPlace ((size_t) F);
            int mismatches = 0;
            for (int f = 0; f < 8; ++f)
            {
                if (f == 4) { a.xf.setRequested (2); b.xf.setRequested (2); }
                std::copy (x.begin() + f * F, x.begin() + (f + 1) * F, inPlace.begin());
                a.process (0, x.data() + f * F, outA.data());
                b.process (0, inPlace.data(), inPlace.data());
                for (int i = 0; i < F; ++i)
                    if (outA[(size_t) i] != inPlace[(size_t) i])
                        ++mismatches;
            }
            expectEquals (mismatches, 0);
        }

        beginTest ("flapping: request toggled before every channel call never runs more than two variants per frame");
        {
            Rig r (2, 1, 1, 0);
            std::vector<float> out ((size_t) F);
            int worstDistinct = 0, worstReported = 0, toggle = 0;
            bool finite = true;
            for (int f = 0; f < 20; ++f)
            {
                r.fake.clearRan();
                r.xf.setRequested ((toggle++ % 2) == 0 ? 1 : 3);
                r.process (0, x.data() + f * F, out.data());
                r.xf.setRequested ((toggle++ % 2) == 0 ? 1 : 3);   // a request between ch0 and ch1 waits for the next frame
                r.process (1, x.data() + f * F, out.data());
                worstDistinct = juce::jmax (worstDistinct, (int) r.fake.ranSet().size());
                worstReported = juce::jmax (worstReported, r.xf.variantsRunLastFrame());
                for (float v : out)
                    finite = finite && std::isfinite (v);
            }
            expect (finite);
            expectLessOrEqual (worstDistinct, 2);
            expectLessOrEqual (worstReported, 2);

            r.xf.setRequested (2);                                   // last request wins
            r.process (0, x.data(), out.data());
            r.process (1, x.data(), out.data());
            r.process (0, x.data(), out.data());
            r.process (1, x.data(), out.data());
            expectEquals (r.xf.activeVariant(), 2);
            expectEquals (r.xf.variantsRunLastFrame(), 1);
        }

        beginTest ("reset() adopts the requested variant without a crossfade and clears the history");
        {
            Rig r (1, 1, 1, 0);
            std::vector<float> out ((size_t) F);
            for (int f = 0; f < 3; ++f)
                r.process (0, x.data() + f * F, out.data());
            r.xf.setRequested (1);
            r.xf.reset();
            expectEquals (r.xf.activeVariant(), 1);
            r.fake.clearRan();
            r.process (0, x.data() + 3 * F, out.data());
            expect (r.fake.ranSet() == std::set<int> { 1 });
            expectEquals (r.xf.variantsRunLastFrame(), 1);
            expectEquals (r.fake.resets[1], 0, "the crossfade did not reset it: the engine resets on reset()");

            // history cleared: after reset (the engine also resets the active variant), a change pre-rolls
            // the incoming variant over zeros, so both crossfaded outputs are silent in that frame
            r.xf.reset();
            r.fake.reset (1);
            r.xf.setRequested (2);
            r.process (0, x.data() + 4 * F, out.data());
            float peak = 0.0f;
            for (float v : out)
                peak = juce::jmax (peak, std::abs (v));
            expectEquals (peak, 0.0f);
        }

        beginTest ("process, reset and setRequested do not allocate");
        {
            Rig r (2, 2, 2, 0);
            std::vector<float> out ((size_t) F);
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                for (int f = 0; f < 10; ++f)
                {
                    r.xf.setRequested (f % 4);
                    r.process (0, x.data() + f * F, out.data());
                    r.process (1, x.data() + f * F, out.data());
                }
                r.xf.reset();
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }
    }
};

static VariantCrossfadeTests variantCrossfadeTests;
} // namespace
