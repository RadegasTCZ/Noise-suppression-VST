#include <JuceHeader.h>
#include "../Source/Engines/ClassicSpeexEngine.h"
#include "TestSignals.h"
#include "AllocCounter.h"
#include <cstring>

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

float lastSecondRmsDb (const std::vector<float>& x)
{
    return nsp::test::rmsDb (x.data() + x.size() - (size_t) R, R);
}

bool bitEqual (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

class ClassicEngineTests final : public juce::UnitTest
{
public:
    ClassicEngineTests() : juce::UnitTest ("Classic Speex engine", "ClassicEngine") {}

    void runTest() override
    {
        beginTest ("identity");
        {
            nsp::ClassicSpeexEngine e;
            expectEquals (juce::String (e.id()), juce::String ("classic"));
            expectEquals (e.extraLatencySamples(), 480);
            expect (! e.providesResidual());
        }

        beginTest ("latency: output lags input by exactly extraLatencySamples()");
        {
            nsp::ClassicSpeexEngine e;
            e.setSuppressDb (-5);
            e.prepare (1);
            std::vector<float> in ((size_t) R, 0.0f);                          // 1 s
            const auto burst = nsp::test::whiteNoise (R / 2, 0.1f, 1234);      // 0.5 s, -20 dBFS rms
            std::copy (burst.begin(), burst.end(), in.begin());
            const auto out = runFrames (e, 0, in);
            const int lag = nsp::test::xcorrLag (in, out, 2000);
            logMessage ("Classic measured lag: " + juce::String (lag));
            // STOP rule: if this fails, do NOT edit extraLatencySamples(); report the measured lag.
            expectEquals (lag, e.extraLatencySamples());
            expectEquals (lag, 480);
        }

        beginTest ("suppression: stationary white noise at -30 dBFS, suppress -30 dB, last second >= 10 dB lower");
        {
            nsp::ClassicSpeexEngine e;
            e.setSuppressDb (-30);
            e.prepare (1);
            const auto in = nsp::test::whiteNoise (3 * R, 0.0316228f, 99);
            const auto out = runFrames (e, 0, in);
            const float inDb = lastSecondRmsDb (in), outDb = lastSecondRmsDb (out);
            logMessage ("Classic noise: in " + juce::String (inDb, 1) + " dBFS, out " + juce::String (outDb, 1) + " dBFS");
            expect (outDb <= inDb - 10.0f);
        }

        beginTest ("stereo channels are independent");
        {
            nsp::ClassicSpeexEngine stereo, mono;
            stereo.prepare (2);
            mono.prepare (1);
            const auto noise = nsp::test::whiteNoise (2 * R, 0.1f, 7);
            const std::vector<float> silence ((size_t) F, 0.0f);
            std::vector<float> outS0 (noise.size()), outM0 (noise.size());
            std::vector<float> outS1 ((size_t) F);
            bool ch1Silent = true;
            for (size_t pos = 0; pos < noise.size(); pos += (size_t) F)
            {
                stereo.processFrame (0, noise.data() + pos, outS0.data() + pos);
                stereo.processFrame (1, silence.data(), outS1.data());
                mono.processFrame (0, noise.data() + pos, outM0.data() + pos);
                for (float v : outS1)
                    ch1Silent = ch1Silent && v == 0.0f;
            }
            expect (bitEqual (outS0, outM0), "channel 0 must not be affected by channel 1");
            expect (ch1Silent, "silent input on channel 1 must give silent output");
            expect (nsp::test::rmsDb (outS0.data(), (int) outS0.size()) > -60.0f, "channel 0 must carry signal");
        }

        beginTest ("setSuppressDb takes effect: -40 dB leaves less noise than -5 dB");
        {
            nsp::ClassicSpeexEngine e5, e40;
            e5.prepare (1);
            e40.prepare (1);
            e5.setSuppressDb (-5);       // set after prepare: exercises the ctl path on live states
            e40.setSuppressDb (-40);
            const auto in = nsp::test::whiteNoise (3 * R, 0.0316228f, 3);
            const float db5 = lastSecondRmsDb (runFrames (e5, 0, in));
            const float db40 = lastSecondRmsDb (runFrames (e40, 0, in));
            logMessage ("Classic -5 dB: " + juce::String (db5, 1) + " dBFS, -40 dB: " + juce::String (db40, 1) + " dBFS");
            expect (db40 < db5 - 3.0f);
        }

        beginTest ("reset() makes the engine behave exactly like a freshly prepared one");
        {
            nsp::ClassicSpeexEngine used, fresh;
            used.setSuppressDb (-25);
            fresh.setSuppressDb (-25);
            used.prepare (2);
            fresh.prepare (2);
            const auto warmup = nsp::test::whiteNoise (100 * F, 0.05f, 21);
            runFrames (used, 0, warmup);
            runFrames (used, 1, warmup);
            used.reset();
            const auto in = nsp::test::whiteNoise (50 * F, 0.05f, 22);
            expect (bitEqual (runFrames (used, 0, in), runFrames (fresh, 0, in)), "channel 0");
            expect (bitEqual (runFrames (used, 1, in), runFrames (fresh, 1, in)), "channel 1");
        }

        beginTest ("reset() and processFrame() do not allocate");
        {
            nsp::ClassicSpeexEngine e;
            e.prepare (2);
            const auto in = nsp::test::whiteNoise (10 * F, 0.1f, 5);
            std::vector<float> out ((size_t) F);
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                e.reset();
                for (int f = 0; f < 10; ++f)
                    for (int ch = 0; ch < 2; ++ch)
                        e.processFrame (ch, in.data() + f * F, out.data());
                e.setSuppressDb (-30);
                e.processFrame (0, out.data(), out.data());   // in place
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }
    }
};

static ClassicEngineTests classicEngineTests;
} // namespace
