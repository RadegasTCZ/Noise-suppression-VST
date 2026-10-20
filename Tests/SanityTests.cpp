#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>
#include "AllocCounter.h"
#include "TestSignals.h"

namespace
{
    class SanityTests final : public juce::UnitTest
    {
    public:
        SanityTests() : juce::UnitTest ("Sanity", "Sanity") {}

        void runTest() override
        {
            beginTest ("xcorrLag finds a known delay");
            {
                const auto ref = nsp::test::whiteNoise (4800, 0.25f, 1234u);
                std::vector<float> sig (ref.size() + 137, 0.0f);

                for (size_t i = 0; i < ref.size(); ++i)
                    sig[i + 137] = ref[i];

                expectEquals (nsp::test::xcorrLag (ref, sig, 400), 137);
                expectEquals (nsp::test::xcorrLag (ref, ref, 400), 0);
            }

            beginTest ("maxAbsStep");
            {
                const float x[] = { 0.0f, 0.5f, -0.25f, -0.25f };
                expectWithinAbsoluteError (nsp::test::maxAbsStep (x, 4), 0.75f, 1.0e-7f);
                expectEquals (nsp::test::maxAbsStep (x, 1), 0.0f);

                const auto s = nsp::test::sine (4800, 1000.0, 48000.0, 1.0f);
                const auto expected = (float) (2.0 * std::sin (juce::MathConstants<double>::pi * 1000.0 / 48000.0));
                expectWithinAbsoluteError (nsp::test::maxAbsStep (s.data(), (int) s.size()), expected, 1.0e-3f);
            }

            beginTest ("rmsDb and maxAbsDiffDb");
            {
                const auto s = nsp::test::sine (48000, 1000.0, 48000.0, 1.0f);
                expectWithinAbsoluteError (nsp::test::rmsDb (s.data(), (int) s.size()), -3.0103f, 0.01f);

                const auto w = nsp::test::whiteNoise (48000, 0.1f, 7u);
                expectWithinAbsoluteError (nsp::test::rmsDb (w.data(), (int) w.size()), -20.0f, 0.2f);

                const std::vector<float> zeros (64, 0.0f);
                expectEquals (nsp::test::rmsDb (zeros.data(), 64), -200.0f);

                auto other = s;
                expectEquals (nsp::test::maxAbsDiffDb (s.data(), other.data(), (int) s.size()), -200.0f);
                other[100] += 0.5f;
                expectWithinAbsoluteError (nsp::test::maxAbsDiffDb (s.data(), other.data(), (int) s.size()),
                                           -6.0206f, 1.0e-3f);
            }

            beginTest ("impulse");
            {
                const auto imp = nsp::test::impulse (10, 3, 0.5f);
                expectEquals ((int) imp.size(), 10);
                expectEquals (imp[3], 0.5f);

                float sumAbs = 0.0f;
                for (auto v : imp)
                    sumAbs += std::abs (v);
                expectEquals (sumAbs, 0.5f);
            }

            beginTest ("ScopedAllocCheck counts allocations on this thread only");
            {
                // new inside the scope counts 1 (count() is read inside, expect*() runs outside:
                // expect*() itself allocates)
                int inside = -1;
                {
                    nsp::test::ScopedAllocCheck check;
                    int* volatile p = new int (7);
                    delete p;
                    inside = check.count();
                }
                expectEquals (inside, 1);

                // new outside the scope counts 0 (delete inside the scope is not an allocation)
                int* volatile before = new int (1);
                int outside = -1;
                {
                    nsp::test::ScopedAllocCheck check;
                    delete before;
                    outside = check.count();
                }
                expectEquals (outside, 0);

                // another thread allocating while this thread is armed is not counted
                std::atomic<int> stage { 0 };
                std::thread worker ([&stage]
                {
                    while (stage.load() != 1)
                        std::this_thread::yield();

                    int* volatile q = new int (3);
                    delete q;
                    stage.store (2);
                });

                int otherThread = -1;
                {
                    nsp::test::ScopedAllocCheck check;
                    stage.store (1);

                    while (stage.load() != 2)
                        std::this_thread::yield();

                    otherThread = check.count();
                }
                worker.join();
                expectEquals (otherThread, 0);
            }
        }
    };

    static SanityTests sanityTests;
}
