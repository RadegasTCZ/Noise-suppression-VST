#include <JuceHeader.h>
#include "../Source/Engines/SnsEngine.h"
#include "sns_reference/HybridDenoiseRef.h"
#include "TestSignals.h"
#include "AllocCounter.h"
#include <cstring>
#include <memory>

namespace
{
constexpr int F = nsp::Engine::kFrameSize;
constexpr int R = nsp::Engine::kSampleRate;

/** Noise everywhere, plus a 1 kHz tone in frames [60, 140) so speech-probability paths move. */
std::vector<float> noiseAndTone (int frames, juce::uint32 seed)
{
    auto x = nsp::test::whiteNoise (frames * F, 0.05f, seed);
    const auto s = nsp::test::sine (frames * F, 1000.0, (double) R, 0.25f);
    for (int i = 60 * F; i < juce::jmin (140, frames) * F; ++i)
        x[(size_t) i] += s[(size_t) i];
    return x;
}

struct SnsRun
{
    std::vector<float> out, residual;
};

SnsRun runSns (nsp::SnsEngine& e, int ch, const std::vector<float>& in)
{
    SnsRun r { std::vector<float> (in.size(), 0.0f), std::vector<float> (in.size(), 0.0f) };
    for (size_t pos = 0; pos + (size_t) F <= in.size(); pos += (size_t) F)
    {
        e.processFrame (ch, in.data() + pos, r.out.data() + pos);
        e.getResidual (ch, r.residual.data() + pos);
    }
    return r;
}

bool bitEqual (const float* a, const float* b, size_t n)
{
    return std::memcmp (a, b, n * sizeof (float)) == 0;
}

float lastSecondRmsDb (const std::vector<float>& x)
{
    return nsp::test::rmsDb (x.data() + x.size() - (size_t) R, R);
}

using nsp::test::speechLike;   // moved to TestSignals.h, unchanged

class SnsEngineTests final : public juce::UnitTest
{
public:
    SnsEngineTests() : juce::UnitTest ("SNS engine", "SnsEngine") {}

    void runTest() override
    {
        hybridDenoiseTests();
        engineTests();
    }

private:
    void hybridDenoiseTests()
    {
        const auto in = noiseAndTone (200, 11);

        beginTest ("patched hybrid_denoise_process_frame is bit-identical to the frozen reference (v2.4.0 + window fix; residual_mode 0 and 1)");
        for (int mode = 0; mode <= 1; ++mode)
        {
            auto ref = std::make_unique<RefHybridDenoiseState>();
            auto cur = std::make_unique<HybridDenoiseState>();
            ref_hybrid_denoise_init (ref.get());
            hybrid_denoise_init (cur.get());
            ref->residual_mode = cur->residual_mode = mode;
            ref->suppress_db = cur->suppress_db = -25.0f;

            float a[F], b[F];
            int mismatches = 0;
            for (int f = 0; f < 200; ++f)
            {
                ref_hybrid_denoise_process_frame (ref.get(), a, in.data() + f * F);
                hybrid_denoise_process_frame (cur.get(), b, in.data() + f * F);
                if (! bitEqual (a, b, F))
                    ++mismatches;
            }
            ref_hybrid_denoise_cleanup (ref.get());
            hybrid_denoise_cleanup (cur.get());
            expectEquals (mismatches, 0, "residual_mode " + juce::String (mode) + ": mismatching frames");
        }

        beginTest ("dual: out == normal out and residual == residual_mode 1 out, bit-exact over 200 frames");
        {
            auto normal = std::make_unique<HybridDenoiseState>();
            auto resid = std::make_unique<HybridDenoiseState>();
            auto dual = std::make_unique<HybridDenoiseState>();
            hybrid_denoise_init (normal.get());
            hybrid_denoise_init (resid.get());
            hybrid_denoise_init (dual.get());
            normal->suppress_db = resid->suppress_db = dual->suppress_db = -25.0f;
            resid->residual_mode = 1;

            float o[F], r[F], dOut[F], dRes[F];
            int outMismatch = 0, resMismatch = 0;
            for (int f = 0; f < 200; ++f)
            {
                const float* x = in.data() + f * F;
                hybrid_denoise_process_frame (normal.get(), o, x);
                hybrid_denoise_process_frame (resid.get(), r, x);
                hybrid_denoise_process_frame_dual (dual.get(), dOut, dRes, x);
                if (! bitEqual (o, dOut, F)) ++outMismatch;
                if (! bitEqual (r, dRes, F)) ++resMismatch;
            }
            hybrid_denoise_cleanup (normal.get());
            hybrid_denoise_cleanup (resid.get());
            hybrid_denoise_cleanup (dual.get());
            expectEquals (outMismatch, 0, "dual out vs normal out");
            expectEquals (resMismatch, 0, "dual residual vs residual_mode 1");
        }

        beginTest ("window fix: output rms vs original v2.4.0 differs by at most 0.05 dB on a speech-like signal");
        {
            const auto sp = speechLike (4 * R, 31);
            auto orig = std::make_unique<RefHybridDenoiseState>();
            auto cur = std::make_unique<HybridDenoiseState>();
            v240_hybrid_denoise_init (orig.get());
            hybrid_denoise_init (cur.get());
            orig->suppress_db = cur->suppress_db = -20.0f;

            std::vector<float> a (sp.size()), b (sp.size());
            for (size_t pos = 0; pos + (size_t) F <= sp.size(); pos += (size_t) F)
            {
                v240_hybrid_denoise_process_frame (orig.get(), a.data() + pos, sp.data() + pos);
                hybrid_denoise_process_frame (cur.get(), b.data() + pos, sp.data() + pos);
            }
            v240_hybrid_denoise_cleanup (orig.get());
            hybrid_denoise_cleanup (cur.get());

            const int start = R / 2;                       // skip noise-estimator start-up
            const int len = (int) sp.size() - start;
            const float dbOrig = nsp::test::rmsDb (a.data() + start, len);
            const float dbFixed = nsp::test::rmsDb (b.data() + start, len);
            logMessage ("SNS output rms: v2.4.0 window " + juce::String (dbOrig, 4) + " dBFS, fixed window "
                        + juce::String (dbFixed, 4) + " dBFS, diff " + juce::String (dbFixed - dbOrig, 4) + " dB");
            expect (std::abs (dbFixed - dbOrig) <= 0.05f);
        }
    }

    void engineTests()
    {
        beginTest ("identity");
        {
            nsp::SnsEngine e;
            expectEquals (juce::String (e.id()), juce::String ("sns"));
            expectEquals (e.extraLatencySamples(), 480);
            expect (e.providesResidual());
            expectEquals (e.noiseFloorDb(), -90.0f);
        }

        beginTest ("a non-finite suppression value (automation / state) falls back to -20 dB: output stays finite");
        {
            const auto in = nsp::test::whiteNoise (R, 0.1f, 77);

            for (float badDb : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
            {
                nsp::SnsEngine e, ref;
                e.prepare (1);
                ref.prepare (1);
                e.setSuppressDb (badDb);
                ref.setSuppressDb (-20.0f);
                const auto r = runSns (e, 0, in);
                const auto rr = runSns (ref, 0, in);
                int nonFinite = 0;
                for (float v : r.out)
                    if (! std::isfinite (v))
                        ++nonFinite;
                expectEquals (nonFinite, 0);
                expect (r.out == rr.out);
            }
        }

        beginTest ("latency: output lags input by exactly extraLatencySamples()");
        {
            nsp::SnsEngine e;
            e.prepare (1);
            e.setSuppressDb (-5.0f);
            std::vector<float> in ((size_t) R, 0.0f);
            const auto burst = nsp::test::whiteNoise (R / 2, 0.1f, 1234);
            std::copy (burst.begin(), burst.end(), in.begin());
            const auto r = runSns (e, 0, in);
            const int lag = nsp::test::xcorrLag (in, r.out, 2000);
            logMessage ("SNS measured lag: " + juce::String (lag));
            // STOP rule: if this fails, do NOT edit extraLatencySamples(); report the measured lag.
            expectEquals (lag, e.extraLatencySamples());
            expectEquals (lag, 480);
        }

        beginTest ("perfect reconstruction: out + residual == input delayed by 480 within -100 dBFS");
        {
            nsp::SnsEngine e;
            e.prepare (1);
            e.setSuppressDb (-30.0f);
            const auto in = speechLike (2 * R, 5);
            const auto r = runSns (e, 0, in);
            const size_t n = in.size();
            std::vector<float> sum (n), delayed (n, 0.0f);
            for (size_t i = 0; i < n; ++i)
            {
                sum[i] = r.out[i] + r.residual[i];
                if (i >= (size_t) F)
                    delayed[i] = in[i - (size_t) F];
            }
            const float errDb = nsp::test::maxAbsDiffDb (sum.data(), delayed.data(), (int) n);
            logMessage ("SNS out+residual vs input delayed by 480: max error " + juce::String (errDb, 1) + " dBFS");
            expect (errDb < -100.0f);
        }

        beginTest ("suppression: stationary white noise at -30 dBFS, suppress -30 dB, last second >= 10 dB lower");
        {
            nsp::SnsEngine e;
            e.prepare (1);
            e.setSuppressDb (-30.0f);
            const auto in = nsp::test::whiteNoise (3 * R, 0.0316228f, 99);
            const auto r = runSns (e, 0, in);
            const float inDb = lastSecondRmsDb (in), outDb = lastSecondRmsDb (r.out);
            logMessage ("SNS noise: in " + juce::String (inDb, 1) + " dBFS, out " + juce::String (outDb, 1) + " dBFS");
            expect (outDb <= inDb - 10.0f);
        }

        beginTest ("noiseFloorDb: -90 before the first frame, tracks -40 dBFS noise, -90 again after reset");
        {
            nsp::SnsEngine e;
            e.prepare (2);
            expectEquals (e.noiseFloorDb(), -90.0f);
            const auto in = nsp::test::whiteNoise (2 * R, 0.01f, 17);   // -40 dBFS rms
            runSns (e, 0, in);
            const float floorDb = e.noiseFloorDb();
            logMessage ("SNS noise floor readout for -40 dBFS white noise: " + juce::String (floorDb, 1));
            expect (std::isfinite (floorDb));
            expect (floorDb > -70.0f && floorDb < -20.0f);
            e.reset();
            expectEquals (e.noiseFloorDb(), -90.0f);
        }

        beginTest ("reset() makes the engine behave exactly like a freshly prepared one (out and residual)");
        {
            nsp::SnsEngine used, fresh;
            used.prepare (2);
            fresh.prepare (2);
            used.setSuppressDb (-25.0f);
            fresh.setSuppressDb (-25.0f);
            const auto warmup = noiseAndTone (100, 21);
            runSns (used, 0, warmup);
            runSns (used, 1, warmup);
            used.reset();
            const auto in = noiseAndTone (50, 22);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto a = runSns (used, ch, in);
                const auto b = runSns (fresh, ch, in);
                expect (bitEqual (a.out.data(), b.out.data(), a.out.size()), "out, channel " + juce::String (ch));
                expect (bitEqual (a.residual.data(), b.residual.data(), a.residual.size()), "residual, channel " + juce::String (ch));
            }
        }

        beginTest ("reset(), processFrame() and getResidual() do not allocate");
        {
            nsp::SnsEngine e;
            e.prepare (2);
            const auto in = nsp::test::whiteNoise (10 * F, 0.1f, 5);
            std::vector<float> out ((size_t) F), res ((size_t) F);
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                e.reset();
                for (int f = 0; f < 10; ++f)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        e.processFrame (ch, in.data() + f * F, out.data());
                        e.getResidual (ch, res.data());
                    }
                }
                e.setSuppressDb (-30.0f);
                e.processFrame (0, out.data(), out.data());   // in place
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }
    }
};

static SnsEngineTests snsEngineTests;
} // namespace
