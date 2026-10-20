#include <JuceHeader.h>
#include "../Source/Engines/RnnoiseEngine.h"
#include "rnnoise.h"
#include "TestSignals.h"
#include "AllocCounter.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
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

/** The library called directly (as in RnnoiseLibraryTests), with the safety floor on as in the engine:
    the engine must equal this bit for bit. */
std::vector<float> runLibrary (const char* blob, int size, const std::vector<float>& x)
{
    std::vector<unsigned char> bytes ((const unsigned char*) blob, (const unsigned char*) blob + size);
    RNNModel* model = rnnoise_model_from_buffer (bytes.data(), (int) bytes.size());
    std::vector<unsigned char> st ((size_t) rnnoise_get_size(), 0);
    auto* state = reinterpret_cast<DenoiseState*> (st.data());
    std::vector<float> y (x.size(), 0.0f);

    if (rnnoise_init (state, model) == 0)
    {
        rnnoise_set_safety_floor (state, 1);   // as RnnoiseEngine::loadModels does
        float a[F], b[F];
        for (size_t pos = 0; pos + (size_t) F <= x.size(); pos += (size_t) F)
        {
            for (int i = 0; i < F; ++i) a[i] = x[pos + (size_t) i] * 32768.0f;
            rnnoise_process_frame (state, b, a);
            for (int i = 0; i < F; ++i) y[pos + (size_t) i] = b[i] * (1.0f / 32768.0f);
        }
    }

    std::free (model);
    return y;
}

class RnnoiseEngineTests final : public juce::UnitTest
{
public:
    RnnoiseEngineTests() : juce::UnitTest ("RNNoise engine", "RnnoiseEngine") {}

    void runTest() override
    {
        beginTest ("identity; models not reported failed before prepare, embedded models load");
        {
            nsp::RnnoiseEngine e;
            expectEquals (juce::String (e.id()), juce::String ("rnnoise"));
            expectEquals (e.extraLatencySamples(), 960);
            expect (! e.providesResidual());
            expect (! e.modelFailed (nsp::RnnoiseEngine::kStandard) && ! e.modelFailed (nsp::RnnoiseEngine::kLight));
            e.prepare (2);
            expect (! e.modelFailed (nsp::RnnoiseEngine::kStandard) && ! e.modelFailed (nsp::RnnoiseEngine::kLight));
            expectEquals (e.activeModel(), (int) nsp::RnnoiseEngine::kStandard);
        }

        beginTest ("latency: output lags input by exactly extraLatencySamples()");
        for (int m : { (int) nsp::RnnoiseEngine::kStandard, (int) nsp::RnnoiseEngine::kLight })
        {
            nsp::RnnoiseEngine e;
            e.setModel (m);
            e.prepare (1);
            std::vector<float> in ((size_t) R, 0.0f);
            const auto burst = nsp::test::whiteNoise (R / 2, 0.1f, 1234);
            std::copy (burst.begin(), burst.end(), in.begin());
            const int lag = nsp::test::xcorrLag (in, runFrames (e, 0, in), 2000);
            logMessage ("RNNoise model " + juce::String (m) + " measured lag: " + juce::String (lag));
            // STOP rule: if this fails, do NOT edit kLatency; report the measured lag.
            expectEquals (lag, e.extraLatencySamples());
        }

        beginTest ("latency: a tiny impulse (RNNoise's silence path) comes out unchanged 960 samples later");
        {
            nsp::RnnoiseEngine e;
            e.prepare (1);
            const auto in = nsp::test::impulse (R, 10000, 3.0e-5f);
            const auto out = runFrames (e, 0, in);
            int peak = 0;
            for (int i = 0; i < R; ++i)
                if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) peak]))
                    peak = i;
            expectEquals (peak - 10000, 960);
            expectWithinAbsoluteError (out[(size_t) peak] / 3.0e-5f, 1.0f, 1.0e-3f);
        }

        const auto speech = nsp::test::speechLike (3 * R, 71);

        beginTest ("engine output == the vendored library called directly, bit-exact (Standard and Light)");
        {
            const std::pair<int, std::pair<const char*, int>> models[] = {
                { (int) nsp::RnnoiseEngine::kStandard, { BinaryData::rnnoise_standard_bin, BinaryData::rnnoise_standard_binSize } },
                { (int) nsp::RnnoiseEngine::kLight, { BinaryData::rnnoise_light_bin, BinaryData::rnnoise_light_binSize } } };
            for (const auto& [m, blob] : models)
            {
                nsp::RnnoiseEngine e;
                e.setModel (m);
                e.prepare (1);
                expectEquals (mismatches (runFrames (e, 0, speech), runLibrary (blob.first, blob.second, speech)), 0,
                              "model " + juce::String (m));
            }
        }

        // RNNoise's own residual must sit well below the floor for these two tests to measure the floor: the
        // floor only raises. Measured residuals (floor off, last 1 s of 5 s): -40 dBFS white noise -74.9..-80.3
        // dBFS and -90 dBFS white noise -101..-102 dBFS (only ~11 dB of suppression that quiet), both above the
        // floor; -50 dBFS pink noise -130..-134 dBFS (digital silence: the cap alone sets the level) and -70
        // dBFS white noise -102..-103 dBFS (10 dB under room - 20 dB). Hence those two signals.
        beginTest ("safety floor, cap: -50 dBFS pink noise (RNNoise alone: < -120 dBFS) -> last 1 s of 3 s at -80 +-1.5 dBFS (Standard and Light)");
        for (int m : { (int) nsp::RnnoiseEngine::kStandard, (int) nsp::RnnoiseEngine::kLight })
        {
            nsp::RnnoiseEngine on, off;
            on.setModel (m);
            off.setModel (m);
            on.prepare (1);
            off.prepare (1);
            off.setSafetyFloorForTest (false);
            const auto in = nsp::test::pinkNoise (3 * R, 3.16227766e-3f, 81);   // -50 dBFS: room - 20 dB = -70, above the cap
            const auto outOn = runFrames (on, 0, in), outOff = runFrames (off, 0, in);
            const float onDb = nsp::test::rmsDb (outOn.data() + outOn.size() - R, R);
            const float offDb = nsp::test::rmsDb (outOff.data() + outOff.size() - R, R);
            logMessage ("RNNoise model " + juce::String (m) + ", -50 dBFS pink noise: floor on " + juce::String (onDb, 2)
                        + " dBFS, floor off " + juce::String (offDb, 2) + " dBFS");
            expectLessThan (offDb, -100.0f, "model " + juce::String (m) + ": RNNoise's own residual is far below the cap");
            expectWithinAbsoluteError (onDb, -80.0f, 1.5f, "model " + juce::String (m));
        }

        beginTest ("safety floor, relative: -70 dBFS white and pink noise (RNNoise alone: far below) -> output = input - 20 dB, attenuation 19.5..24.5 dB (windowed minimum + NSP_FLOOR_BIAS)");
        for (const bool pink : { false, true })
        for (int m : { (int) nsp::RnnoiseEngine::kStandard, (int) nsp::RnnoiseEngine::kLight })
        {
            nsp::RnnoiseEngine on, off;
            on.setModel (m);
            off.setModel (m);
            on.prepare (1);
            off.prepare (1);
            off.setSafetyFloorForTest (false);
            const auto in = pink ? nsp::test::pinkNoise (3 * R, 3.16227766e-4f, 82)    // -70 dBFS: floor -90, under the cap
                                 : nsp::test::whiteNoise (3 * R, 3.16227766e-4f, 82);
            const auto outOn = runFrames (on, 0, in), outOff = runFrames (off, 0, in);
            const float inDb = nsp::test::rmsDb (in.data() + in.size() - R - on.extraLatencySamples(), R);
            const float onDb = nsp::test::rmsDb (outOn.data() + outOn.size() - R, R);
            const float offDb = nsp::test::rmsDb (outOff.data() + outOff.size() - R, R);
            const juce::String what = juce::String ("model ") + juce::String (m) + (pink ? ", pink" : ", white");
            logMessage ("RNNoise " + what + " -70 dBFS noise: in " + juce::String (inDb, 2)
                        + " dBFS, floor on " + juce::String (onDb, 2) + " dBFS (attenuation " + juce::String (inDb - onDb, 2)
                        + " dB), floor off " + juce::String (offDb, 2) + " dBFS");
            expectLessThan (offDb, onDb - 6.0f, what + ": RNNoise's own residual is well below the floor");
            expect (inDb - onDb >= 19.5f && inDb - onDb <= 24.5f, what);
        }

        beginTest ("safety floor regression: steady -65 dBFS white noise with a 150 ms dip to -88 dBFS -> 2 s after the dip the output is within 2 dB of its level before the dip; never below -100 dBFS from 0.5 s on except while the tracker remembers the dip (<= 1.5 s), and never below -112 dBFS then");
        for (int m : { (int) nsp::RnnoiseEngine::kStandard, (int) nsp::RnnoiseEngine::kLight })
        {
            // OBS_speech_tails.wav: ~150 ms at -84..-89 dBFS right after loud clicks, room noise -64..-67 dBFS.
            // The old tracker (instant fall, +4 dB/s rise) needed 5..8 s to climb back from such a dip.
            const int dipStart = 3 * R, dipLen = (int) (0.15 * R), total = 6 * R;
            auto in = nsp::test::whiteNoise (total, 5.62341325e-4f, 85);   // -65 dBFS
            const float dipGain = std::pow (10.0f, -23.0f / 20.0f);        // -88 dBFS
            for (int i = dipStart; i < dipStart + dipLen; ++i)
                in[(size_t) i] *= dipGain;

            nsp::RnnoiseEngine e;
            e.setModel (m);
            e.prepare (1);
            const auto out = runFrames (e, 0, in);
            const int lat = e.extraLatencySamples();
            const int win = R / 5;   // 200 ms
            const float beforeDb = nsp::test::rmsDb (out.data() + dipStart + lat - win, win);
            const int afterPos = dipStart + dipLen + 2 * R + lat;
            const float afterDb = nsp::test::rmsDb (out.data() + afterPos, win);

            // 10 ms windows from 0.5 s on. The dip itself and the tracker's memory of it (1.5 s after it ends, plus
            // 50 ms for the 40 ms pre-average and the hop) sit at about dip - 20 dB: judged separately.
            const int memEnd = dipStart + dipLen + lat + R * 3 / 2 + R / 20;
            float lowestOutside = 0.0f, lowestInside = 0.0f;
            for (int pos = R / 2; pos + F <= total; pos += F)
            {
                const float db = nsp::test::rmsDb (out.data() + pos, F);
                if (pos + F > dipStart + lat && pos < memEnd)
                    lowestInside = juce::jmin (lowestInside, db);
                else
                    lowestOutside = juce::jmin (lowestOutside, db);
            }
            juce::String trace;
            for (int t = 0; t <= 20; ++t)
                trace << juce::String (nsp::test::rmsDb (out.data() + dipStart + dipLen + lat + t * (R / 10), R / 10), 1) << " ";
            logMessage ("RNNoise model " + juce::String (m) + " dip regression: before " + juce::String (beforeDb, 2) + " dBFS, 2 s after "
                        + juce::String (afterDb, 2) + " dBFS; lowest 10 ms window outside the dip memory " + juce::String (lowestOutside, 2)
                        + " dBFS, inside " + juce::String (lowestInside, 2) + " dBFS; 100 ms levels from the dip end: " + trace);
            expectWithinAbsoluteError (afterDb, beforeDb, 2.0f, "model " + juce::String (m));
            expectGreaterThan (lowestOutside, -100.0f, "model " + juce::String (m));
            expectGreaterThan (lowestInside, -112.0f, "model " + juce::String (m));
        }

        beginTest ("safety floor: stereo with channel 1 all zeros -> channel 1 output all zeros");
        {
            nsp::RnnoiseEngine e;
            e.prepare (2);
            const auto noisy = nsp::test::whiteNoise (2 * R, 0.01f, 83);
            const std::vector<float> zeros ((size_t) F, 0.0f);
            std::vector<float> out0 ((size_t) F), out1 ((size_t) F);
            bool ch1Zero = true;
            for (size_t pos = 0; pos + (size_t) F <= noisy.size(); pos += (size_t) F)
            {
                e.processFrame (0, noisy.data() + pos, out0.data());
                e.processFrame (1, zeros.data(), out1.data());
                for (float v : out1)
                    ch1Zero = ch1Zero && v == 0.0f;
            }
            expect (ch1Zero);
        }

        beginTest ("safety floor: a -12 dBFS speech-like burst keeps its level within 0.1 dB vs floor off");
        {
            // 1 s of -60 dBFS room noise, then 2 s of the speech-like signal scaled to a -12 dBFS voice peak.
            auto in = nsp::test::whiteNoise (R, 0.001f, 84);
            {
                auto burst = nsp::test::speechLike (2 * R, 85);
                const float scale = std::pow (10.0f, -12.0f / 20.0f) / 0.3f;   // speechLike's voice peaks at 0.3
                for (auto& v : burst)
                    v *= scale;
                in.insert (in.end(), burst.begin(), burst.end());
            }
            for (int m : { (int) nsp::RnnoiseEngine::kStandard, (int) nsp::RnnoiseEngine::kLight })
            {
                nsp::RnnoiseEngine on, off;
                on.setModel (m);
                off.setModel (m);
                on.prepare (1);
                off.prepare (1);
                off.setSafetyFloorForTest (false);
                const auto outOn = runFrames (on, 0, in), outOff = runFrames (off, 0, in);
                const int start = R + on.extraLatencySamples();
                const int n = (int) in.size() - start;
                const float onDb = nsp::test::rmsDb (outOn.data() + start, n);
                const float offDb = nsp::test::rmsDb (outOff.data() + start, n);
                logMessage ("RNNoise model " + juce::String (m) + ", -12 dBFS speech-like burst: floor on " + juce::String (onDb, 3)
                            + " dBFS, off " + juce::String (offDb, 3) + " dBFS");
                expectWithinAbsoluteError (onDb, offDb, 0.1f, "model " + juce::String (m));
                expectGreaterThan (mismatches (outOn, outOff), 0, "the floor fills the -60 dBFS lead-in");
            }
        }

        beginTest ("stereo channels are independent");
        {
            nsp::RnnoiseEngine stereo, mono;
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
            nsp::RnnoiseEngine used, fresh;
            used.setModel (nsp::RnnoiseEngine::kLight);
            fresh.setModel (nsp::RnnoiseEngine::kLight);
            used.prepare (2);
            fresh.prepare (2);
            const auto warmup = nsp::test::speechLike (R, 72);
            runFrames (used, 0, warmup);
            runFrames (used, 1, warmup);
            used.reset();
            for (int ch = 0; ch < 2; ++ch)
                expectEquals (mismatches (runFrames (used, ch, speech), runFrames (fresh, ch, speech)), 0,
                              "channel " + juce::String (ch));
        }

        beginTest ("a model chosen while the engine was not running is adopted by reset() without a crossfade");
        {
            nsp::RnnoiseEngine e, freshLight;
            e.prepare (1);
            freshLight.setModel (nsp::RnnoiseEngine::kLight);
            freshLight.prepare (1);
            runFrames (e, 0, nsp::test::speechLike (R / 2, 73));
            e.setModel (nsp::RnnoiseEngine::kLight);   // e.g. automation while another engine is active
            e.reset();                                 // EngineHost resets the incoming engine on a switch
            std::vector<float> out ((size_t) F), ref ((size_t) F);
            e.processFrame (0, speech.data(), out.data());
            freshLight.processFrame (0, speech.data(), ref.data());
            expectEquals (e.modelsRunLastFrame(), 1);
            expectEquals (e.activeModel(), (int) nsp::RnnoiseEngine::kLight);
            expectEquals (mismatches (out, ref), 0);
        }

        beginTest ("model change: one crossfade frame running both models, no click");
        {
            nsp::RnnoiseEngine e;
            e.prepare (1);
            std::vector<float> out (speech.size(), 0.0f);
            int twoModelFrames = 0;
            for (int f = 0; f < (int) speech.size() / F; ++f)
            {
                if (f == 100) e.setModel (nsp::RnnoiseEngine::kLight);
                if (f == 200) e.setModel (nsp::RnnoiseEngine::kStandard);
                e.processFrame (0, speech.data() + f * F, out.data() + f * F);
                if (e.modelsRunLastFrame() == 2)
                {
                    ++twoModelFrames;
                    expect (f == 100 || f == 200, "crossfade frame " + juce::String (f));
                }
            }
            expectEquals (twoModelFrames, 2);
            expectEquals (e.activeModel(), (int) nsp::RnnoiseEngine::kStandard);

            const float bound = 2.0f * nsp::test::maxAbsStep (speech.data(), (int) speech.size());
            for (int f : { 100, 200 })
            {
                const float step = nsp::test::maxAbsStep (out.data() + (f - 1) * F, 3 * F);
                logMessage ("RNNoise model switch at frame " + juce::String (f) + ": max step " + juce::String (step, 4)
                            + ", bound " + juce::String (bound, 4));
                expectLessOrEqual (step, bound);
            }
        }

        beginTest ("reset(), processFrame() and model changes do not allocate");
        {
            nsp::RnnoiseEngine e;
            e.prepare (2);
            std::vector<float> out ((size_t) F);
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                e.reset();
                for (int f = 0; f < 12; ++f)
                {
                    if (f % 4 == 0)
                        e.setModel ((f / 4) % 2);
                    for (int ch = 0; ch < 2; ++ch)
                        e.processFrame (ch, speech.data() + f * F, out.data());
                }
                e.processFrame (0, out.data(), out.data());   // in place
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }

        beginTest ("corrupt Standard blob: modelFailed, audio passes through delayed by 960; Light still works");
        {
            const std::vector<char> truncated (BinaryData::rnnoise_standard_bin,
                                               BinaryData::rnnoise_standard_bin + BinaryData::rnnoise_standard_binSize / 3);
            nsp::RnnoiseEngine e ({ truncated.data(), (int) truncated.size() },
                                  { BinaryData::rnnoise_light_bin, BinaryData::rnnoise_light_binSize });
            e.prepare (1);
            expect (e.modelFailed (nsp::RnnoiseEngine::kStandard));
            expect (! e.modelFailed (nsp::RnnoiseEngine::kLight));

            const auto out = runFrames (e, 0, speech);
            std::vector<float> delayed (speech.size(), 0.0f);
            std::copy (speech.begin(), speech.end() - 960, delayed.begin() + 960);
            expectEquals (mismatches (out, delayed), 0);

            e.setModel (nsp::RnnoiseEngine::kLight);
            std::vector<float> light ((size_t) (20 * F));
            for (int f = 0; f < 20; ++f)
                e.processFrame (0, speech.data() + f * F, light.data() + f * F);
            expectEquals (e.activeModel(), (int) nsp::RnnoiseEngine::kLight);
            bool finite = true;
            for (float v : light)
                finite = finite && std::isfinite (v);
            expect (finite);
        }

        // SSE2 build (ThirdParty/rnnoise/src/rnn_sse2.h) measured ~27x; the C path (no SIMD) measured ~6x and would fail this bound.
        beginTest ("CPU: Standard model, stereo, at least 10x faster than real time in Release (SSE2)");
        {
           #if JUCE_DEBUG
            logMessage ("    skipped in Debug");
           #else
            nsp::RnnoiseEngine e;
            e.prepare (2);
            const auto x = nsp::test::speechLike (10 * R, 74);
            std::vector<float> out ((size_t) F);
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t pos = 0; pos + (size_t) F <= x.size(); pos += (size_t) F)
                for (int ch = 0; ch < 2; ++ch)
                    e.processFrame (ch, x.data() + pos, out.data());
            const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const double realTimeFactor = 10.0 / seconds;
            logMessage ("RNNoise Standard stereo: 10 s of audio in " + juce::String (seconds, 3) + " s, "
                        + juce::String (realTimeFactor, 1) + "x real time");
            expectGreaterThan (realTimeFactor, 10.0);
           #endif
        }
    }
};

static RnnoiseEngineTests rnnoiseEngineTests;
} // namespace
