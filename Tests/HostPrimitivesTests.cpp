#include <JuceHeader.h>
#include "../Source/Host/RingBuffer.h"
#include "../Source/Host/DelayLine.h"
#include "../Source/Host/PeakMeter.h"
#include "TestSignals.h"
#include "AllocCounter.h"

namespace
{
std::vector<float> ramp (int n, float start)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = start + (float) i;
    return v;
}

class HostPrimitivesTests final : public juce::UnitTest
{
public:
    HostPrimitivesTests() : juce::UnitTest ("Host primitives", "HostPrimitives") {}

    void runTest() override
    {
        ringBufferTests();
        delayLineTests();
        peakHoldTests();
        allocationTests();
    }

private:
    void ringBufferTests()
    {
        beginTest ("RingBuffer: push/pop keeps order across wraparound");
        {
            nsp::RingBuffer rb;
            rb.allocate (8);
            expectEquals (rb.capacity(), 8);
            expectEquals (rb.size(), 0);
            expectEquals (rb.freeSpace(), 8);

            const auto a = ramp (5, 1.0f);                 // 1..5
            expect (rb.push (a.data(), 5));
            float out[8] = {};
            expect (rb.pop (out, 3));
            expectEquals (out[0], 1.0f);
            expectEquals (out[1], 2.0f);
            expectEquals (out[2], 3.0f);

            const auto b = ramp (5, 6.0f);                 // 6..10, write wraps past the end
            expect (rb.push (b.data(), 5));
            expectEquals (rb.size(), 7);
            expectEquals (rb.freeSpace(), 1);
            expect (rb.pop (out, 7));                      // read wraps too
            for (int i = 0; i < 7; ++i)
                expectEquals (out[i], 4.0f + (float) i);
            expectEquals (rb.size(), 0);
        }

        beginTest ("RingBuffer: push refused when it does not fit, nothing written");
        {
            nsp::RingBuffer rb;
            rb.allocate (4);
            const auto a = ramp (3, 1.0f);
            expect (rb.push (a.data(), 3));
            const auto b = ramp (2, 100.0f);
            expect (! rb.push (b.data(), 2));
            expectEquals (rb.size(), 3);
            expectEquals (rb.freeSpace(), 1);
            float out[4] = {};
            expect (rb.pop (out, 3));
            expectEquals (out[0], 1.0f);
            expectEquals (out[1], 2.0f);
            expectEquals (out[2], 3.0f);
            expectEquals (rb.size(), 0);
        }

        beginTest ("RingBuffer: pop refused when not enough data, nothing read");
        {
            nsp::RingBuffer rb;
            rb.allocate (4);
            const auto a = ramp (3, 1.0f);
            expect (rb.push (a.data(), 3));
            float out[4] = { -9.0f, -9.0f, -9.0f, -9.0f };
            expect (! rb.pop (out, 4));
            for (float v : out)
                expectEquals (v, -9.0f);
            expectEquals (rb.size(), 3);
        }

        beginTest ("RingBuffer: pushZeros overwrites stale data, also across wraparound; clear; allocate clears");
        {
            nsp::RingBuffer rb;
            rb.allocate (6);
            const auto a = ramp (4, 1.0f);                 // indices 0..3 hold 1..4
            expect (rb.push (a.data(), 4));
            float out[6] = {};
            expect (rb.pop (out, 4));                      // read = write = 4
            const float seven = 7.0f;
            expect (rb.push (&seven, 1));                  // index 4
            rb.pushZeros (3);                              // indices 5, 0, 1 (wraps over stale 1, 2)
            expectEquals (rb.size(), 4);
            for (auto& v : out)
                v = -1.0f;
            expect (rb.pop (out, 4));
            expectEquals (out[0], 7.0f);
            expectEquals (out[1], 0.0f);
            expectEquals (out[2], 0.0f);
            expectEquals (out[3], 0.0f);

            expect (rb.push (a.data(), 4));
            rb.clear();
            expectEquals (rb.size(), 0);
            expectEquals (rb.freeSpace(), 6);

            expect (rb.push (a.data(), 4));
            rb.allocate (10);
            expectEquals (rb.capacity(), 10);
            expectEquals (rb.size(), 0);
        }
    }

    void delayLineTests()
    {
        beginTest ("DelayLine: exact delay for 0, 1, 480 and max, across odd chunk sizes");
        for (int d : { 0, 1, 480, 1000 })
        {
            nsp::DelayLine dl;
            dl.prepare (1000);
            expectEquals (dl.getDelay(), 0);
            dl.setDelay (d);
            expectEquals (dl.getDelay(), d);

            const int n = 2048;
            const auto in = nsp::test::impulse (n, 10, 0.5f);
            std::vector<float> out ((size_t) n, -1.0f);
            const int chunks[] = { 100, 37, 480, 1, 999 };
            int pos = 0, c = 0;
            while (pos < n)
            {
                const int len = juce::jmin (chunks[c++ % 5], n - pos);
                dl.process (in.data() + pos, out.data() + pos, len);
                pos += len;
            }

            int wrongAt = -1;
            for (int i = 0; i < n && wrongAt < 0; ++i)
                if (out[(size_t) i] != (i == 10 + d ? 0.5f : 0.0f))
                    wrongAt = i;
            expectEquals (wrongAt, -1, "delay " + juce::String (d) + ": first wrong sample index");
        }

        beginTest ("DelayLine: in-place processing equals out-of-place");
        {
            const auto in = nsp::test::whiteNoise (3000, 0.3f, 7);
            nsp::DelayLine a, b;
            a.prepare (512);
            b.prepare (512);
            a.setDelay (333);
            b.setDelay (333);
            std::vector<float> outA (in.size());
            std::vector<float> inPlace = in;
            for (size_t pos = 0; pos < in.size(); pos += 250)
            {
                const int len = (int) std::min<size_t> (250, in.size() - pos);
                a.process (in.data() + pos, outA.data() + pos, len);
                b.process (inPlace.data() + pos, inPlace.data() + pos, len);
            }
            expect (outA == inPlace);
            expectEquals (outA[333], in[0]);
        }

        beginTest ("DelayLine: setDelay clears history; reset clears history and keeps delay");
        {
            nsp::DelayLine dl;
            dl.prepare (100);
            dl.setDelay (10);
            std::vector<float> ones (50, 1.0f), zeros (50, 0.0f), out (50, -1.0f);

            dl.process (ones.data(), out.data(), 50);
            dl.setDelay (10);
            dl.process (zeros.data(), out.data(), 50);
            expect (std::all_of (out.begin(), out.end(), [] (float v) { return v == 0.0f; }), "setDelay must clear history");

            dl.process (ones.data(), out.data(), 50);
            dl.reset();
            expectEquals (dl.getDelay(), 10);
            dl.process (zeros.data(), out.data(), 50);
            expect (std::all_of (out.begin(), out.end(), [] (float v) { return v == 0.0f; }), "reset must clear history");
        }
    }

    void peakHoldTests()
    {
        beginTest ("PeakHold: max |x| since last take, negative samples by abs, reset after take, -100 floor");
        {
            nsp::PeakHold ph;
            expectEquals (ph.takeDb(), -100.0f);           // nothing pushed yet

            const float a[] = { 0.1f, -0.7f, 0.3f };
            const float b[] = { 0.2f };
            ph.push (a, 3);
            ph.push (b, 1);
            expectWithinAbsoluteError (ph.takeDb(), 20.0f * std::log10 (0.7f), 1.0e-4f);
            expectEquals (ph.takeDb(), -100.0f);           // reset after take

            const float silence[] = { 0.0f, 0.0f, -0.0f };
            ph.push (silence, 3);
            expectEquals (ph.takeDb(), -100.0f);

            const float tiny[] = { 1.0e-7f };              // -140 dBFS, floored
            ph.push (tiny, 1);
            expectEquals (ph.takeDb(), -100.0f);

            const float one[] = { -1.0f };
            ph.push (one, 1);
            expectWithinAbsoluteError (ph.takeDb(), 0.0f, 1.0e-6f);
        }

        beginTest ("PeakHold: non-finite samples are ignored (an inf must not freeze the meter)");
        {
            nsp::PeakHold ph;
            const float bad[] = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
                                  -std::numeric_limits<float>::infinity(), 0.5f };
            ph.push (bad, 4);
            expectWithinAbsoluteError (ph.takeDb(), 20.0f * std::log10 (0.5f), 1.0e-4f);
        }
    }

    void allocationTests()
    {
        beginTest ("Host primitives do not allocate after allocate/prepare");
        {
            nsp::RingBuffer rb;
            rb.allocate (1024);
            nsp::DelayLine dl;
            dl.prepare (960);
            dl.setDelay (960);
            nsp::PeakHold ph;
            const auto data = nsp::test::whiteNoise (512, 0.1f, 3);
            std::vector<float> out (512);

            int allocs = -1;
            float db = -200.0f;
            {
                nsp::test::ScopedAllocCheck check;
                for (int k = 0; k < 10; ++k)
                {
                    rb.push (data.data(), 512);
                    rb.pushZeros (100);
                    rb.pop (out.data(), 512);
                    rb.pop (out.data(), 100);
                    dl.process (data.data(), out.data(), 512);
                    dl.process (out.data(), out.data(), 512);
                    ph.push (data.data(), 512);
                    db = ph.takeDb();
                }
                dl.setDelay (480);
                dl.reset();
                rb.clear();
                allocs = check.count();
            }
            // expect* builds juce::Strings (allocates), so it must run outside the check scope.
            expectEquals (allocs, 0);
            expect (db > -100.0f);
        }
    }
};

static HostPrimitivesTests hostPrimitivesTests;
} // namespace
