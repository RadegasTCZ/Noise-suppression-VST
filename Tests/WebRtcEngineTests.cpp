#include <JuceHeader.h>
#include "../Source/Engines/WebRtcEngine.h"
#include "TestSignals.h"
#include "AllocCounter.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
constexpr int F = nsp::Engine::kFrameSize;
constexpr int R = nsp::Engine::kSampleRate;

std::vector<float> runFrames (nsp::Engine& e, int ch, const std::vector<float>& in)
{
    std::vector<float> out (in.size(), 0.0f);
    for (size_t pos = 0; pos + (size_t) F <= in.size(); pos += (size_t) F)
        e.processFrame (ch, in.data() + pos, out.data() + pos);
    return out;
}

int mismatches (const std::vector<float>& a, const std::vector<float>& b)
{
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp (&a[i], &b[i], sizeof (float)) != 0)
            ++n;
    return n;
}

/** Output level change in dB over the last 2 s of a 6 s run at `level`. */
float attenuationDb (int level, const std::vector<float>& x)
{
    nsp::WebRtcEngine e;
    e.setLevel (level);
    e.prepare (1);
    const auto y = runFrames (e, 0, x);
    const int from = (int) x.size() - 2 * R;
    return nsp::test::rmsDb (y.data() + from, 2 * R) - nsp::test::rmsDb (x.data() + from, 2 * R);
}

class WebRtcEngineTests final : public juce::UnitTest
{
public:
    WebRtcEngineTests() : juce::UnitTest ("WebRTC NS engine", "WebRtcEngine") {}

    void runTest() override
    {
        beginTest ("identity");
        {
            nsp::WebRtcEngine e;
            expectEquals (juce::String (e.id()), juce::String ("webrtc"));
            expectEquals (e.extraLatencySamples(), 334);
            expect (! e.providesResidual());
            e.prepare (2);
            expectEquals (e.activeLevel(), (int) nsp::WebRtcEngine::k12dB);   // the webrtcLevel default
        }

        beginTest ("latency: output lags input by exactly extraLatencySamples() at every level");
        for (int level = 0; level < nsp::WebRtcEngine::kNumLevels; ++level)
        {
            nsp::WebRtcEngine e;
            e.setLevel (level);
            e.prepare (1);
            std::vector<float> in ((size_t) R, 0.0f);
            const auto burst = nsp::test::whiteNoise (R / 2, 0.1f, 1234);
            std::copy (burst.begin(), burst.end(), in.begin());
            const int lag = nsp::test::xcorrLag (in, runFrames (e, 0, in), 2000);
            logMessage ("WebRTC level " + juce::String (level) + " measured lag: " + juce::String (lag));
            // STOP rule: if this fails, do NOT edit kLatency; report the measured lags.
            expectEquals (lag, e.extraLatencySamples());
        }

        beginTest ("suppression ranks 6 < 12 < 18 < 21 dB on white and pink noise, each within 3 dB of its target");
        {
            const float targets[] = { 6.0f, 12.0f, 18.0f, 21.0f };
            const std::pair<const char*, std::vector<float>> signals[] = {
                { "white", nsp::test::whiteNoise (6 * R, 0.05f, 99) },
                { "pink", nsp::test::pinkNoise (6 * R, 0.05f, 98) } };
            for (const auto& [label, x] : signals)
            {
                float previous = 0.0f;
                juce::String line = juce::String ("WebRTC ") + label + " noise attenuation:";
                for (int level = 0; level < nsp::WebRtcEngine::kNumLevels; ++level)
                {
                    const float atten = -attenuationDb (level, x);
                    line << " " << juce::String (atten, 2);
                    expectGreaterThan (atten, previous, juce::String (label) + " level " + juce::String (level));
                    expectWithinAbsoluteError (atten, targets[level], 3.0f, juce::String (label) + " level " + juce::String (level));
                    previous = atten;
                }
                logMessage (line + " dB");
            }
        }

        beginTest ("silence in -> exact silence out; loud input stays finite");
        {
            nsp::WebRtcEngine e;
            e.prepare (1);
            const auto out = runFrames (e, 0, std::vector<float> ((size_t) (20 * F), 0.0f));
            float peak = 0.0f;
            for (float v : out)
                peak = juce::jmax (peak, std::abs (v));
            expectEquals (peak, 0.0f);

            nsp::WebRtcEngine loud;
            loud.setLevel (nsp::WebRtcEngine::k21dB);
            loud.prepare (1);
            bool finite = true;
            for (float v : runFrames (loud, 0, nsp::test::whiteNoise (2 * R, 0.9f, 4)))
                finite = finite && std::isfinite (v);
            expect (finite);
        }

        const auto speech = nsp::test::speechLike (3 * R, 91);

        beginTest ("stereo channels are independent");
        {
            nsp::WebRtcEngine stereo, mono;
            stereo.prepare (2);
            mono.prepare (1);
            const std::vector<float> silence ((size_t) F, 0.0f);
            std::vector<float> outS0 (speech.size()), outM0 (speech.size()), outS1 ((size_t) F);
            bool ch1Silent = true;
            for (size_t pos = 0; pos < speech.size(); pos += (size_t) F)
            {
                stereo.processFrame (0, speech.data() + pos, outS0.data() + pos);
                stereo.processFrame (1, silence.data(), outS1.data());
                mono.processFrame (0, speech.data() + pos, outM0.data() + pos);
                for (float v : outS1)
                    ch1Silent = ch1Silent && v == 0.0f;
            }
            expectEquals (mismatches (outS0, outM0), 0);
            expect (ch1Silent);
        }

        beginTest ("reset() makes the engine behave exactly like a freshly prepared one");
        {
            nsp::WebRtcEngine used, fresh;
            used.setLevel (nsp::WebRtcEngine::k18dB);
            fresh.setLevel (nsp::WebRtcEngine::k18dB);
            used.prepare (2);
            fresh.prepare (2);
            const auto warmup = nsp::test::pinkNoise (R, 0.05f, 92);
            runFrames (used, 0, warmup);
            runFrames (used, 1, warmup);
            used.reset();
            for (int ch = 0; ch < 2; ++ch)
                expectEquals (mismatches (runFrames (used, ch, speech), runFrames (fresh, ch, speech)), 0,
                              "channel " + juce::String (ch));
        }

        beginTest ("a level chosen while the engine was not running is adopted by reset() without a crossfade");
        {
            nsp::WebRtcEngine e, fresh21;
            e.prepare (1);
            fresh21.setLevel (nsp::WebRtcEngine::k21dB);
            fresh21.prepare (1);
            runFrames (e, 0, nsp::test::pinkNoise (R / 2, 0.05f, 93));
            e.setLevel (nsp::WebRtcEngine::k21dB);
            e.reset();
            std::vector<float> out ((size_t) F), ref ((size_t) F);
            e.processFrame (0, speech.data(), out.data());
            fresh21.processFrame (0, speech.data(), ref.data());
            expectEquals (e.levelsRunLastFrame(), 1);
            expectEquals (e.activeLevel(), (int) nsp::WebRtcEngine::k21dB);
            expectEquals (mismatches (out, ref), 0);
        }

        beginTest ("level change: one crossfade frame running both levels, no click");
        {
            nsp::WebRtcEngine e;
            e.setLevel (nsp::WebRtcEngine::k12dB);
            e.prepare (1);
            std::vector<float> out (speech.size(), 0.0f);
            int twoLevelFrames = 0;
            for (int f = 0; f < (int) speech.size() / F; ++f)
            {
                if (f == 100) e.setLevel (nsp::WebRtcEngine::k21dB);
                if (f == 200) e.setLevel (nsp::WebRtcEngine::k6dB);
                e.processFrame (0, speech.data() + f * F, out.data() + f * F);
                if (e.levelsRunLastFrame() == 2)
                {
                    ++twoLevelFrames;
                    expect (f == 100 || f == 200, "crossfade frame " + juce::String (f));
                }
            }
            expectEquals (twoLevelFrames, 2);
            expectEquals (e.activeLevel(), (int) nsp::WebRtcEngine::k6dB);

            const float bound = 2.0f * nsp::test::maxAbsStep (speech.data(), (int) speech.size());
            for (int f : { 100, 200 })
            {
                const float step = nsp::test::maxAbsStep (out.data() + (f - 1) * F, 3 * F);
                logMessage ("WebRTC level switch at frame " + juce::String (f) + ": max step " + juce::String (step, 4)
                            + ", bound " + juce::String (bound, 4));
                expectLessOrEqual (step, bound);
            }
        }

        beginTest ("level flapping every frame: never more than two levels per frame, finite, last request wins");
        {
            nsp::WebRtcEngine e;
            e.prepare (2);
            std::vector<float> out ((size_t) F);
            int worst = 0;
            bool finite = true;
            for (int f = 0; f < 100; ++f)
            {
                e.setLevel ((f % 2) == 0 ? nsp::WebRtcEngine::k6dB : nsp::WebRtcEngine::k21dB);
                for (int ch = 0; ch < 2; ++ch)
                {
                    e.processFrame (ch, speech.data() + f * F, out.data());
                    for (float v : out)
                        finite = finite && std::isfinite (v) && std::abs (v) < 4.0f;
                }
                worst = juce::jmax (worst, e.levelsRunLastFrame());
            }
            expect (finite);
            expectLessOrEqual (worst, 2);
            e.setLevel (nsp::WebRtcEngine::k18dB);
            for (int f = 0; f < 2; ++f)
                for (int ch = 0; ch < 2; ++ch)
                    e.processFrame (ch, speech.data() + f * F, out.data());
            expectEquals (e.activeLevel(), (int) nsp::WebRtcEngine::k18dB);
            expectEquals (e.levelsRunLastFrame(), 1);
        }

        beginTest ("reset(), processFrame() and level changes do not allocate");
        {
            nsp::WebRtcEngine e;
            e.prepare (2);
            std::vector<float> out ((size_t) F);
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                e.reset();
                for (int f = 0; f < 12; ++f)
                {
                    e.setLevel (f % nsp::WebRtcEngine::kNumLevels);
                    for (int ch = 0; ch < 2; ++ch)
                        e.processFrame (ch, speech.data() + f * F, out.data());
                }
                e.processFrame (0, out.data(), out.data());   // in place
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }
    }
};

static WebRtcEngineTests webRtcEngineTests;
} // namespace
