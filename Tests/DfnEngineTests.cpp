#include <JuceHeader.h>
#include "../Source/Engines/DfnEngine.h"
#include "TestSignals.h"
#include "AllocCounter.h"
#include <chrono>
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

/** Attenuation in dB over the last second of `in` / `out`. */
float lastSecondAttenuation (const std::vector<float>& in, const std::vector<float>& out)
{
    const int n = (int) in.size() - R;
    return nsp::test::rmsDb (in.data() + n, R) - nsp::test::rmsDb (out.data() + n, R);
}

float attenuation (int strength, const std::vector<float>& noise)
{
    nsp::DfnEngine e;
    e.setStrength (strength);
    e.prepare (1);
    return lastSecondAttenuation (noise, runFrames (e, 0, noise));
}

class DfnEngineTests final : public juce::UnitTest
{
public:
    DfnEngineTests() : juce::UnitTest ("DeepFilterNet engine", "DfnEngine") {}

    void runTest() override
    {
        beginTest ("identity: id dfn, latency 1440, no residual, embedded model loads");
        {
            nsp::DfnEngine e;
            expectEquals (juce::String (e.id()), juce::String ("dfn"));
            expectEquals (e.extraLatencySamples(), 1440);
            expect (! e.providesResidual());
            expect (! e.modelFailed());
            expectEquals (e.strength(), (int) nsp::DfnEngine::kNormal);
            e.prepare (2);
            expect (! e.modelFailed());
        }

        const auto speech = nsp::test::speechLike (3 * R, 81);

        beginTest ("latency: Gentle output lags a noise burst by exactly 1440 samples");
        {
            nsp::DfnEngine e;
            e.setStrength (nsp::DfnEngine::kGentle);
            e.prepare (1);
            // A noise burst, not speechLike: its 150 Hz harmonics repeat every 320 samples, so the
            // correlation peak is ambiguous (measured 1120 = 1440 - 320 after the network's masking).
            std::vector<float> x ((size_t) R, 0.0f);
            const auto burst = nsp::test::whiteNoise (R / 2, 0.1f, 82);
            std::copy (burst.begin(), burst.end(), x.begin());
            const int lag = nsp::test::xcorrLag (x, runFrames (e, 0, x), 3000);
            logMessage ("DfnEngine Gentle measured lag: " + juce::String (lag));
            expectEquals (lag, 1440);
        }

        const auto noise = nsp::test::whiteNoise (3 * R, 0.0316228f, 83);   // -30 dBFS rms

        beginTest ("strength caps on stationary white noise (-30 dBFS, measured over the last second of 3 s)");
        {
            const float gentle = attenuation (nsp::DfnEngine::kGentle, noise);
            const float normal = attenuation (nsp::DfnEngine::kNormal, noise);
            const float strong = attenuation (nsp::DfnEngine::kStrong, noise);
            logMessage ("DfnEngine attenuation Gentle " + juce::String (gentle, 2) + " dB, Normal "
                        + juce::String (normal, 2) + " dB, Strong " + juce::String (strong, 2) + " dB");
            expect (gentle >= 9.0f && gentle <= 12.5f);
            expect (normal >= 18.0f && normal <= 24.5f);
            expect (strong >= normal + 3.0f && strong <= 80.5f);
        }

        beginTest ("strength caps on near-silent input (-80 dBFS): every preset outputs its limited input, never all-zero hops");
        {
            const auto quiet = nsp::test::whiteNoise (2 * R, 1.0e-4f, 84);   // -80 dBFS
            for (int s = 0; s < nsp::DfnEngine::kNumStrengths; ++s)
            {
                nsp::DfnEngine e;
                e.setStrength (s);
                e.prepare (1);
                const auto out = runFrames (e, 0, quiet);
                const float att = lastSecondAttenuation (quiet, out);
                logMessage ("DfnEngine -80 dBFS, strength " + juce::String (s) + ": attenuation " + juce::String (att, 2) + " dB");
                const float cap = nsp::DfnEngine::kPresets[s].attenLimDb;
                expect (att >= -0.5f && att <= cap + 0.5f, "strength " + juce::String (s));

                int zeroHops = 0;
                for (size_t pos = (size_t) R; pos + (size_t) F <= out.size(); pos += (size_t) F)
                    if (nsp::test::rmsDb (out.data() + pos, F) <= -190.0f)
                        ++zeroHops;
                expectEquals (zeroHops, 0, "strength " + juce::String (s));
            }
        }

        beginTest ("safety floor: Strong on -40 dBFS white noise -> -80 +-1.5 dBFS; Normal on -80 dBFS white and pink noise -> attenuation 19.5..24.5 dB; Gentle on -40 dBFS unchanged vs floor off within 0.1 dB");
        {
            const auto loud = nsp::test::whiteNoise (3 * R, 0.01f, 92);    // -40 dBFS
            const auto quiet = nsp::test::whiteNoise (3 * R, 1.0e-4f, 93); // -80 dBFS
            const auto quietPink = nsp::test::pinkNoise (3 * R, 1.0e-4f, 94);
            const int n = (int) loud.size() - R;

            nsp::DfnEngine strong;
            strong.setStrength (nsp::DfnEngine::kStrong);
            strong.prepare (1);
            const float strongDb = nsp::test::rmsDb (runFrames (strong, 0, loud).data() + n, R);

            nsp::DfnEngine normal, normalPink;
            normal.setStrength (nsp::DfnEngine::kNormal);
            normal.prepare (1);
            normalPink.setStrength (nsp::DfnEngine::kNormal);
            normalPink.prepare (1);
            const float normalAtt = lastSecondAttenuation (quiet, runFrames (normal, 0, quiet));
            const float normalPinkAtt = lastSecondAttenuation (quietPink, runFrames (normalPink, 0, quietPink));

            nsp::DfnEngine gentleOn, gentleOff;
            gentleOn.setStrength (nsp::DfnEngine::kGentle);
            gentleOff.setStrength (nsp::DfnEngine::kGentle);
            gentleOn.prepare (1);
            gentleOff.prepare (1);
            gentleOff.setSafetyFloorForTest (false);
            const float gentleOnDb = nsp::test::rmsDb (runFrames (gentleOn, 0, loud).data() + n, R);
            const float gentleOffDb = nsp::test::rmsDb (runFrames (gentleOff, 0, loud).data() + n, R);

            logMessage ("DfnEngine safety floor: Strong -40 dBFS -> " + juce::String (strongDb, 2) + " dBFS; Normal -80 dBFS attenuation white "
                        + juce::String (normalAtt, 2) + " dB, pink " + juce::String (normalPinkAtt, 2) + " dB; Gentle -40 dBFS floor on "
                        + juce::String (gentleOnDb, 3) + " / off " + juce::String (gentleOffDb, 3) + " dBFS");
            expectWithinAbsoluteError (strongDb, -80.0f, 1.5f);
            expect (normalAtt >= 19.5f && normalAtt <= 24.5f, "white");
            expect (normalPinkAtt >= 19.5f && normalPinkAtt <= 24.5f, "pink");
            expectWithinAbsoluteError (gentleOnDb, gentleOffDb, 0.1f);
        }

        beginTest ("strength change is ramped over 5 hops; reset() adopts the requested strength immediately");
        {
            const int hopsBefore = 150, hopsAfter = 20;
            nsp::DfnEngine e;
            e.setStrength (nsp::DfnEngine::kGentle);
            e.prepare (1);
            std::vector<float> out ((size_t) F);
            std::vector<float> db;
            for (int h = 0; h < hopsBefore + hopsAfter; ++h)
            {
                if (h == hopsBefore)
                    e.setStrength (nsp::DfnEngine::kStrong);
                e.processFrame (0, noise.data() + (size_t) h * F, out.data());
                if (h >= hopsBefore - 2)
                    db.push_back (nsp::test::rmsDb (out.data(), F));
            }
            juce::String trace;
            for (float v : db) trace << juce::String (v, 2) << " ";
            logMessage ("DfnEngine strength ramp, hop levels (dB) from 2 hops before the change: " + trace);
            // db[2] is the first hop after the change: the level falls hop by hop while the limit ramps
            // (5 transitions), then stays at the Strong floor, far below the pre-change level.
            for (size_t i = 2; i < 7; ++i)
                expectLessThan (db[i + 1], db[i]);
            for (size_t i = 7; i < db.size(); ++i)
                expectLessThan (db[i], db[2] - 25.0f);

            // reset() adopts the requested strength immediately: bit-identical to an engine prepared with it.
            nsp::DfnEngine adopted, fresh;
            adopted.setStrength (nsp::DfnEngine::kGentle);
            adopted.prepare (1);
            runFrames (adopted, 0, nsp::test::speechLike (R / 2, 85));
            adopted.setStrength (nsp::DfnEngine::kStrong);
            adopted.reset();
            fresh.setStrength (nsp::DfnEngine::kStrong);
            fresh.prepare (1);
            expectEquals (mismatches (runFrames (adopted, 0, speech), runFrames (fresh, 0, speech)), 0);
        }

        beginTest ("strength index is clamped");
        {
            nsp::DfnEngine e;
            e.setStrength (-5);
            expectEquals (e.strength(), 0);
            e.setStrength (99);
            expectEquals (e.strength(), (int) nsp::DfnEngine::kStrong);
        }

        beginTest ("stereo: identical inputs give identical outputs; changing channel 1's input leaves channel 0 bit-identical");
        {
            const auto other = nsp::test::speechLike (3 * R, 86);
            nsp::DfnEngine a, b;
            a.prepare (2);
            b.prepare (2);
            std::vector<float> a0 (speech.size()), a1 (speech.size()), b0 (speech.size()), b1 (speech.size());
            for (size_t pos = 0; pos + (size_t) F <= speech.size(); pos += (size_t) F)
            {
                a.processFrame (0, speech.data() + pos, a0.data() + pos);
                a.processFrame (1, speech.data() + pos, a1.data() + pos);
                b.processFrame (0, speech.data() + pos, b0.data() + pos);
                b.processFrame (1, other.data() + pos, b1.data() + pos);
            }
            expectEquals (mismatches (a0, a1), 0);
            expectEquals (mismatches (a0, b0), 0);
            expect (mismatches (a1, b1) > 0);
        }

        beginTest ("reset() makes the engine behave exactly like a freshly prepared one");
        {
            nsp::DfnEngine used, fresh;
            used.prepare (2);
            fresh.prepare (2);
            const auto warmup = nsp::test::speechLike (R, 87);
            runFrames (used, 0, warmup);
            runFrames (used, 1, warmup);
            used.reset();
            for (int ch = 0; ch < 2; ++ch)
                expectEquals (mismatches (runFrames (used, ch, speech), runFrames (fresh, ch, speech)), 0,
                              "channel " + juce::String (ch));
        }

        beginTest ("re-prepare (2 -> 1 -> 2 channels) gives output bit-identical to a freshly constructed engine");
        {
            nsp::DfnEngine used, fresh;
            used.prepare (2);
            fresh.prepare (2);
            const auto warmup = nsp::test::speechLike (R, 91);
            runFrames (used, 0, warmup);
            runFrames (used, 1, warmup);
            used.prepare (1);
            used.prepare (2);
            for (int ch = 0; ch < 2; ++ch)
                expectEquals (mismatches (runFrames (used, ch, speech), runFrames (fresh, ch, speech)), 0,
                              "channel " + juce::String (ch));
        }

        beginTest ("processFrame, reset and strength changes do not allocate");
        {
            nsp::DfnEngine e;
            e.prepare (2);
            std::vector<float> out ((size_t) F);
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                e.reset();
                for (int f = 0; f < 12; ++f)
                {
                    if (f % 4 == 0)
                        e.setStrength ((f / 4) % 3);
                    for (int ch = 0; ch < 2; ++ch)
                        e.processFrame (ch, speech.data() + (size_t) f * F, out.data());
                }
                e.processFrame (0, out.data(), out.data());   // in place
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }

        beginTest ("corrupt blob: modelFailed, audio passes through delayed by exactly 1440");
        {
            const std::vector<char> truncated (BinaryData::dfn3_standard_bin,
                                               BinaryData::dfn3_standard_bin + BinaryData::dfn3_standard_binSize / 3);
            nsp::DfnEngine e ({ truncated.data(), (int) truncated.size() });
            expect (! e.modelFailed());   // not before prepare() tried
            e.prepare (1);
            expect (e.modelFailed());

            const auto out = runFrames (e, 0, speech);
            std::vector<float> delayed (speech.size(), 0.0f);
            std::copy (speech.begin(), speech.end() - 1440, delayed.begin() + 1440);
            expectEquals (mismatches (out, delayed), 0);

            e.setStrength (nsp::DfnEngine::kStrong);   // harmless without a model
            e.reset();
            expect (e.modelFailed());
        }

        beginTest ("hot input: 0 dBFS noise and +6 dBFS sine in stereo -> finite, max |out| < 4");
        {
            const auto hot0 = nsp::test::whiteNoise (2 * R, 0.577f, 88);
            const auto hot1 = nsp::test::sine (2 * R, 440.0, R, 2.0f);
            for (int s = 0; s < nsp::DfnEngine::kNumStrengths; ++s)
            {
                nsp::DfnEngine e;
                e.setStrength (s);
                e.prepare (2);
                float peak = 0.0f;
                bool finite = true;
                for (int ch = 0; ch < 2; ++ch)
                    for (float v : runFrames (e, ch, ch == 0 ? hot0 : hot1))
                    {
                        finite = finite && std::isfinite (v);
                        peak = juce::jmax (peak, std::abs (v));
                    }
                expect (finite, "strength " + juce::String (s));
                expectLessThan (peak, 4.0f);
            }
        }

        beginTest ("CPU: full engine, stereo, Normal, 10 s speech-like, at least 5x real time in Release");
        {
           #if JUCE_DEBUG
            logMessage ("    skipped in Debug");
           #else
            nsp::DfnEngine e;
            e.prepare (2);
            const auto x = nsp::test::speechLike (10 * R, 89);
            std::vector<float> out ((size_t) F);
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t pos = 0; pos + (size_t) F <= x.size(); pos += (size_t) F)
                for (int ch = 0; ch < 2; ++ch)
                    e.processFrame (ch, x.data() + pos, out.data());
            const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const double factor = 10.0 / seconds;
            logMessage ("DfnEngine Normal stereo: 10 s of audio in " + juce::String (seconds, 3) + " s, "
                        + juce::String (factor, 1) + "x real time");
            expectGreaterThan (factor, 5.0);
           #endif
        }
    }
};

static DfnEngineTests dfnEngineTests;
} // namespace
