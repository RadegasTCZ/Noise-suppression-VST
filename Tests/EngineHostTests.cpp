#include <JuceHeader.h>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>
#include "../Source/Host/EngineHost.h"
#include "AllocCounter.h"
#include "PassthroughEngine.h"
#include "TestSignals.h"

namespace
{
constexpr int kFrame = nsp::Engine::kFrameSize;

/** Builds a host with PassthroughEngines {extra, withResidual}, targets `target`, prepares 2 channels. */
std::unique_ptr<nsp::EngineHost> makeHost (std::initializer_list<std::pair<int, bool>> engines, int target, int maxBlock)
{
    auto host = std::make_unique<nsp::EngineHost>();

    for (auto [extra, withResidual] : engines)
        host->addEngine (std::make_unique<nsp::test::PassthroughEngine> (extra, withResidual));

    host->setTarget (target, false);
    host->prepare (2, maxBlock);
    return host;
}

struct Stream
{
    std::vector<float> out0, out1;
};

/** Streams in0/in1 through the host in `block`-sized calls, pulling everything available after each call.
    beforeBlock (pos) runs before each call; afterBlock (framesProcessedThisCall) after each call. */
Stream run (nsp::EngineHost& host, const std::vector<float>& in0, const std::vector<float>& in1, int block,
            const std::function<void (int)>& beforeBlock = nullptr,
            const std::function<void (int)>& afterBlock = nullptr)
{
    Stream s;
    std::vector<float> tmp0 ((size_t) (block + 3 * kFrame)), tmp1 ((size_t) (block + 3 * kFrame));
    const int total = (int) in0.size();

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);

        if (beforeBlock)
            beforeBlock (pos);

        const float* in[2] = { in0.data() + pos, in1.data() + pos };
        host.process (in, 2, n);

        const int avail = host.available();
        float* out[2] = { tmp0.data(), tmp1.data() };
        host.pull (out, 2, avail);
        s.out0.insert (s.out0.end(), tmp0.begin(), tmp0.begin() + avail);
        s.out1.insert (s.out1.end(), tmp1.begin(), tmp1.begin() + avail);

        if (afterBlock)
            afterBlock (avail / kFrame);
    }

    return s;
}

int argMaxAbs (const std::vector<float>& x)
{
    int best = 0;

    for (int i = 1; i < (int) x.size(); ++i)
        if (std::abs (x[(size_t) i]) > std::abs (x[(size_t) best]))
            best = i;

    return best;
}

float crossfadeSum (int i) // fadeOut + fadeIn at position i of the crossfade frame
{
    const double t = juce::MathConstants<double>::halfPi * ((double) i + 0.5) / (double) kFrame;
    return (float) (std::cos (t) + std::sin (t));
}
} // namespace

class EngineHostTests final : public juce::UnitTest
{
public:
    EngineHostTests() : juce::UnitTest ("EngineHost", "EngineHost") {}

    void runTest() override
    {
        testLatency();
        testSwitching();
        testFlapping();
        testResidual();
        testNoAllocation();
        testTargetClamp();
    }

private:
    void testLatency()
    {
        beginTest ("latency: frame buffering + maxExtraLatency for every engine (padding)");

        for (int target : { 0, 1 })
        {
            auto host = makeHost ({ { 0, false }, { 480, false } }, target, 256);
            expectEquals (host->maxExtraLatency(), 480);
            expectEquals (host->activeEngine(), target); // prepare starts on the target, no crossfade

            const int total = 4000, block = 256;
            const auto in0 = nsp::test::impulse (total, 1000);
            const auto in1 = nsp::test::impulse (total, 1500, 0.5f);

            int pushed = 0, pulled = 0, maxRun = 0;
            bool bufferingOk = true, hostUnderrun = false;

            const auto s = run (*host, in0, in1, block,
                                [&] (int pos) { pushed = juce::jmin (total, pos + block); },
                                [&] (int frames)
                                {
                                    pulled += frames * kFrame;
                                    maxRun = juce::jmax (maxRun, host->enginesRunLastFrame());
                                    // a frame is processed exactly when its 480th sample arrives
                                    bufferingOk = bufferingOk && pulled == (pushed / kFrame) * kFrame;
                                    // a host FIFO prefilled with one frame of zeros never runs dry
                                    hostUnderrun = hostUnderrun || (kFrame + pulled < pushed);
                                });

            expect (bufferingOk, "frames must be processed as soon as 480 samples are buffered");
            expect (! hostUnderrun, "480-sample prefill must cover the frame buffering");
            expectEquals (maxRun, 1);

            // pulled stream: delayed by exactly maxExtraLatency, whatever the engine's own extra latency
            expectEquals (argMaxAbs (s.out0), 1000 + 480);
            expectWithinAbsoluteError (s.out0[1480], 1.0f, 1.0e-6f);
            expectEquals (argMaxAbs (s.out1), 1500 + 480);
            expectWithinAbsoluteError (s.out1[1980], 0.5f, 1.0e-6f);

            // host view: 480 prefill + stream delay = 480 + maxExtraLatency = 960
            expectEquals (kFrame + argMaxAbs (s.out0) - 1000, 960);
        }
    }

    void testSwitching()
    {
        beginTest ("switching: one-frame equal-power crossfade, aligned output, no click");

        auto host = makeHost ({ { 0, false }, { 480, false } }, 0, kFrame);
        const int frames = 30, s1 = 10, s2 = 20;
        const auto x0 = nsp::test::sine (frames * kFrame, 1000.0f, 48000.0, juce::Decibels::decibelsToGain (-12.0f));
        std::vector<float> x1 (x0.size());

        for (size_t i = 0; i < x0.size(); ++i)
            x1[i] = -0.5f * x0[i];

        std::vector<int> runs;
        const auto s = run (*host, x0, x1, kFrame,
                            [&] (int pos)
                            {
                                if (pos == s1 * kFrame) host->setTarget (1, false);
                                if (pos == s2 * kFrame) host->setTarget (0, false);
                            },
                            [&] (int) { runs.push_back (host->enginesRunLastFrame()); });

        expectEquals ((int) s.out0.size(), frames * kFrame);
        expectEquals (runs[(size_t) 5], 1);
        expectEquals (runs[(size_t) s1], 2);
        expectEquals (runs[(size_t) s1 + 1], 1);
        expectEquals (runs[(size_t) s2], 2);
        expectEquals (host->activeEngine(), 0);

        // Both engines are padded to the same delay and the incoming one is pre-rolled, so outside the
        // crossfade frames the output is x delayed by 480; inside, x delayed * (cos + sin).
        float worst = 0.0f;

        for (int k = 0; k < frames * kFrame; ++k)
        {
            const int f = k / kFrame, i = k % kFrame;
            const float g = (f == s1 || f == s2) ? crossfadeSum (i) : 1.0f;
            const float e0 = k >= kFrame ? x0[(size_t) (k - kFrame)] * g : 0.0f;
            const float e1 = k >= kFrame ? x1[(size_t) (k - kFrame)] * g : 0.0f;
            worst = juce::jmax (worst, std::abs (s.out0[(size_t) k] - e0), std::abs (s.out1[(size_t) k] - e1));
        }

        expectLessThan (worst, 1.0e-5f);

        const float steady = nsp::test::maxAbsStep (s.out0.data() + 3 * kFrame, 5 * kFrame);

        for (int sw : { s1, s2 })
            expectLessOrEqual (nsp::test::maxAbsStep (s.out0.data() + sw * kFrame - 1, kFrame + 2), 2.0f * steady);
    }

    void testFlapping()
    {
        beginTest ("flapping: target toggled every 64-sample block never runs more than two engines");

        auto host = makeHost ({ { 0, false }, { 480, false } }, 0, 64);
        const int total = 2 * 48000;
        const auto x0 = nsp::test::sine (total, 1000.0f, 48000.0, juce::Decibels::decibelsToGain (-12.0f));
        std::vector<float> x1 (x0.size());

        for (size_t i = 0; i < x0.size(); ++i)
            x1[i] = 0.5f * x0[i];

        std::vector<bool> switched;
        int maxRun = 0, lastRequested = 0;

        const auto s = run (*host, x0, x1, 64,
                            [&] (int pos)
                            {
                                lastRequested = (pos / 64) % 2;
                                host->setTarget (lastRequested, false);
                            },
                            [&] (int frames)
                            {
                                if (frames > 0)
                                {
                                    maxRun = juce::jmax (maxRun, host->enginesRunLastFrame());
                                    switched.push_back (host->enginesRunLastFrame() == 2);
                                }
                            });

        expectLessOrEqual (maxRun, 2);
        expectEquals ((int) switched.size(), total / kFrame);

        int numSwitches = 0;
        for (bool b : switched)
            numSwitches += b ? 1 : 0;
        expectGreaterThan (numSwitches, 50);

        // one more frame without a new request: the last requested engine is the one heard
        std::vector<float> zeros ((size_t) kFrame, 0.0f);
        const float* zin[2] = { zeros.data(), zeros.data() };
        host->process (zin, 2, kFrame);
        expectEquals (host->activeEngine(), lastRequested);

        float worst = 0.0f;

        for (int k = 0; k < total; ++k)
        {
            const int f = k / kFrame, i = k % kFrame;
            const float g = switched[(size_t) f] ? crossfadeSum (i) : 1.0f;
            const float e0 = k >= kFrame ? x0[(size_t) (k - kFrame)] * g : 0.0f;
            const float e1 = k >= kFrame ? x1[(size_t) (k - kFrame)] * g : 0.0f;
            worst = juce::jmax (worst, std::abs (s.out0[(size_t) k] - e0), std::abs (s.out1[(size_t) k] - e1));
        }

        expectLessThan (worst, 1.0e-5f);

        const float steady = nsp::test::maxAbsStep (x0.data(), total);
        expectLessOrEqual (nsp::test::maxAbsStep (s.out0.data() + kFrame + 1, total - kFrame - 1), 2.0f * steady);
    }

    void testResidual()
    {
        beginTest ("residual: dryDelayed - wet for plain engines");
        {
            auto host = makeHost ({ { 0, false }, { 480, false } }, 0, 240);
            const auto x0 = nsp::test::whiteNoise (48000, 0.1f, 11);
            const auto x1 = nsp::test::whiteNoise (48000, 0.1f, 12);

            const auto s = run (*host, x0, x1, 240,
                                [&] (int pos) { host->setTarget (pos < 24000 ? 0 : 1, true); });

            float worst = 0.0f;
            for (size_t k = 0; k < s.out0.size(); ++k)
                worst = juce::jmax (worst, std::abs (s.out0[k]), std::abs (s.out1[k]));

            expectLessThan (worst, 1.0e-6f); // passthrough: wet == dry, so the difference residual is zero
        }

        beginTest ("residual: engine residual (padded) for providesResidual engines, selectable per frame");
        {
            auto host = makeHost ({ { 0, true }, { 480, false } }, 0, 240);
            const auto x0 = nsp::test::whiteNoise (48000, 0.1f, 13);
            const auto x1 = nsp::test::whiteNoise (48000, 0.1f, 14);
            const int toggle = 24000; // frame- and block-aligned: frames >= 50 are processed in residual mode

            const auto s = run (*host, x0, x1, 240,
                                [&] (int pos) { host->setTarget (0, pos >= toggle); });

            float worst = 0.0f;

            for (int k = 0; k < (int) s.out0.size(); ++k)
            {
                const float scale = k >= toggle ? 0.5f : 1.0f;
                const float e0 = k >= kFrame ? scale * x0[(size_t) (k - kFrame)] : 0.0f;
                const float e1 = k >= kFrame ? scale * x1[(size_t) (k - kFrame)] : 0.0f;
                worst = juce::jmax (worst, std::abs (s.out0[(size_t) k] - e0), std::abs (s.out1[(size_t) k] - e1));
            }

            expectLessThan (worst, 1.0e-6f);
        }
    }

    void testNoAllocation()
    {
        beginTest ("no allocation in setTarget / process / pull, across switches and residual toggles");

        auto host = makeHost ({ { 0, true }, { 480, false } }, 0, 256);
        const auto x = nsp::test::whiteNoise (256 * 150, 0.1f, 21);
        std::vector<float> o0 (256 + 3 * kFrame), o1 (256 + 3 * kFrame);
        float* out[2] = { o0.data(), o1.data() };

        auto block = [&] (int b)
        {
            const float* in[2] = { x.data() + b * 256, x.data() + b * 256 };
            host->process (in, 2, 256);
            host->pull (out, 2, host->available());
        };

        for (int b = 0; b < 10; ++b)
            block (b);

        int allocations = 0;
        {
            nsp::test::ScopedAllocCheck check;

            for (int b = 10; b < 150; ++b)
            {
                host->setTarget ((b / 20) % 2, (b / 35) % 2 == 1);
                block (b);
            }

            allocations = (int) check.count();
        }

        expectEquals (allocations, 0);
    }

    void testTargetClamp()
    {
        beginTest ("target index out of range is clamped");

        auto host = makeHost ({ { 0, false }, { 480, false } }, 0, 256);
        expectEquals (host->numEngines(), 2);

        std::vector<float> zeros ((size_t) kFrame, 0.0f);
        const float* zin[2] = { zeros.data(), zeros.data() };

        host->setTarget (99, false);
        host->process (zin, 2, kFrame);
        expectEquals (host->activeEngine(), 1);

        host->setTarget (-3, false);
        host->process (zin, 2, kFrame);
        expectEquals (host->activeEngine(), 0);
    }
};

static EngineHostTests engineHostTests;
