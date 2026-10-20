#include <JuceHeader.h>
#include "../Source/Engines/dfn/DfnDenoiser.h"
#include "../Source/Engines/dfn/DfnModel.h"
#include "../Source/Engines/dfn/DfnNetwork.h"
#include "AllocCounter.h"
#include "TestPaths.h"
#include "TestSignals.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
using nsp::dfn::Denoiser;
constexpr int H = Denoiser::kHop;

/** Runs whole hops of `in` through `d`; `stages` (optional) gets lastStages() after every hop. */
std::vector<float> run (Denoiser& d, const std::vector<float>& in, std::vector<Denoiser::Stages>* stages = nullptr)
{
    const int hops = (int) in.size() / H;
    std::vector<float> out ((size_t) hops * H);
    for (int h = 0; h < hops; ++h)
    {
        d.process (in.data() + (size_t) h * H, out.data() + (size_t) h * H);
        if (stages != nullptr)
            stages->push_back (d.lastStages());
    }
    return out;
}

bool allFinite (const std::vector<float>& v)
{
    for (auto x : v)
        if (! std::isfinite (x))
            return false;
    return true;
}

float maxAbs (const std::vector<float>& v)
{
    float m = 0.0f;
    for (auto x : v)
        m = std::fmax (m, std::fabs (x));
    return m;
}

std::vector<float> concat (std::vector<float> a, const std::vector<float>& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}

/** A mono 48 kHz WAV as the official CLI reads it (16-bit: sample / 32767, hound + wav_utils.rs). */
bool readWav (const juce::File& f, std::vector<float>& v, bool& is16, double& rate, int& channels)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples > INT_MAX)
        return false;
    rate = r->sampleRate;
    channels = (int) r->numChannels;
    if (channels != 1)
        return true;
    const int len = (int) r->lengthInSamples;
    juce::AudioBuffer<float> b (1, len);
    r->read (&b, 0, len, 0, true, false);
    is16 = r->bitsPerSample == 16 && ! r->usesFloatingPointData;
    v.assign (b.getReadPointer (0), b.getReadPointer (0) + len);
    if (is16)
        for (auto& x : v)
            x = std::round (x * 32768.0f) / 32767.0f;
    return true;
}

class DfnDenoiserTests final : public juce::UnitTest
{
public:
    DfnDenoiserTests() : juce::UnitTest ("DeepFilterNet denoiser", "DfnDenoiser") {}

    void runTest() override
    {
        nsp::dfn::Model model;
        const bool loaded = model.load (BinaryData::dfn3_standard_bin, (size_t) BinaryData::dfn3_standard_binSize);
        nsp::dfn::Network net;
        const bool netOk = loaded && net.init (model);

        beginTest ("ERB band widths: 13 x 2 then 5 5 7 7 8 10 12 13 15 18 20 24 28 31 37 42 50 56 67 (sum 481)");
        {
            const std::array<int, 32> expected { 2,2,2,2,2,2,2,2,2,2,2,2,2, 5,5,7,7,8,10,12,13,15,18,20,24,28,31,37,42,50,56,67 };
            expect (nsp::dfn::erbWidths() == expected);
            int sum = 0;
            for (auto w : nsp::dfn::erbWidths())
                sum += w;
            expectEquals (sum, 481);
        }

        beginTest ("norm alpha for 48 kHz / 480 / tau 1 s is 0.99");
        expectEquals (nsp::dfn::normAlpha (48000, 480, 1.0f), 0.99f);

        beginTest ("post-filter: beta 0 is the identity (bit-exact); beta 0.02 never raises a bin's magnitude");
        {
            constexpr int n = Denoiser::kBins;
            juce::Random rnd (1234);
            std::vector<std::complex<float>> noisy ((size_t) n), enh ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                noisy[(size_t) i] = { rnd.nextFloat() * 2.0f - 1.0f, rnd.nextFloat() * 2.0f - 1.0f };
                enh[(size_t) i] = noisy[(size_t) i] * (rnd.nextFloat() * 1.3f);   // gains 0 .. 1.3 (some above 1)
            }
            enh[5] = 0.0f;                                                         // a fully masked bin

            auto e0 = enh;
            nsp::dfn::postFilter (noisy.data(), e0.data(), n, 0.0f);
            expect (std::memcmp (e0.data(), enh.data(), (size_t) n * sizeof (enh[0])) == 0);

            auto e1 = enh;
            nsp::dfn::postFilter (noisy.data(), e1.data(), n, 0.02f);
            bool neverRaised = true, finite = true;
            int lowered = 0;
            for (int i = 0; i < n; ++i)
            {
                const auto a = e1[(size_t) i], b = enh[(size_t) i];
                neverRaised = neverRaised && std::fabs (a.real()) <= std::fabs (b.real()) && std::fabs (a.imag()) <= std::fabs (b.imag());
                finite = finite && std::isfinite (a.real()) && std::isfinite (a.imag());
                lowered += std::abs (a) < std::abs (b) ? 1 : 0;
            }
            expect (neverRaised);
            expect (finite);
            expectGreaterThan (lowered, n / 2);
            expect (e1[(size_t) n - 1] == enh[(size_t) n - 1], "upstream processes chunks of 4: bin 480 untouched");
        }

        expect (netOk, "embedded model must load");
        if (! netOk)
            return;

        auto fresh = [&net] (float limDb, float beta)
        {
            auto d = std::make_unique<Denoiser>();
            d->prepare (net);
            d->setStrength (limDb, beta, true);
            return d;
        };

        beginTest ("limit 0 dB: output == input delayed by exactly 1440 samples (STFT round trip), max error < 1e-5");
        {
            auto d = fresh (0.0f, 0.0f);
            const auto in = nsp::test::whiteNoise (2 * 48000, 0.1f, 11);
            const auto out = run (*d, in);
            float err = 0.0f;
            for (size_t n = 0; n + Denoiser::kLatency < out.size(); ++n)
                err = std::fmax (err, std::fabs (out[n + Denoiser::kLatency] - in[n]));
            logMessage ("    max error " + juce::String (err, 9));
            expectLessThan (err, 1.0e-5f);
        }

        beginTest ("safety floor off by default: output bit-identical to a denoiser that never called setSafetyFloor");
        {
            const auto in = nsp::test::whiteNoise (48000, 0.01f, 101);   // -40 dBFS
            auto never = fresh (80.0f, 0.02f);
            auto toggled = fresh (80.0f, 0.02f);
            toggled->setSafetyFloor (true);
            toggled->setSafetyFloor (false);
            auto on = fresh (80.0f, 0.02f);
            on->setSafetyFloor (true);
            const auto a = run (*never, in), b = run (*toggled, in), c = run (*on, in);
            expect (a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
            expect (std::memcmp (a.data(), c.data(), a.size() * sizeof (float)) != 0, "the floor changes this output when on");
        }

        beginTest ("safety floor, cap: -40 dBFS white noise, limit 80 -> last 1 s of 3 s at -80 dBFS +-1.5 dB");
        {
            auto d = fresh (80.0f, 0.02f);
            d->setSafetyFloor (true);
            const auto out = run (*d, nsp::test::whiteNoise (3 * 48000, 0.01f, 102));
            const float db = nsp::test::rmsDb (out.data() + out.size() - 48000, 48000);
            logMessage ("    -40 dBFS in, limit 80, floor on: out " + juce::String (db, 2) + " dBFS");
            expectWithinAbsoluteError (db, -80.0f, 1.5f);
        }

        beginTest ("safety floor, relative: -80 dBFS white and pink noise (silence gate active), limit 80 -> output = input - 20 dB, attenuation 19.5..24.5 dB (windowed minimum + NSP_FLOOR_BIAS)");
        for (const bool pink : { false, true })
        {
            auto d = fresh (80.0f, 0.02f);
            d->setSafetyFloor (true);
            const auto in = pink ? nsp::test::pinkNoise (3 * 48000, 1.0e-4f, 103) : nsp::test::whiteNoise (3 * 48000, 1.0e-4f, 103);
            std::vector<Denoiser::Stages> st;
            const auto out = run (*d, in, &st);
            expect (st.back().silenceGate, "gate active");
            const float inDb = nsp::test::rmsDb (in.data() + in.size() - 48000 - Denoiser::kLatency, 48000);
            const float outDb = nsp::test::rmsDb (out.data() + out.size() - 48000, 48000);
            logMessage (juce::String ("    -80 dBFS ") + (pink ? "pink" : "white") + ", limit 80, floor on: in " + juce::String (inDb, 2)
                        + " dB, out " + juce::String (outDb, 2) + " dB (attenuation " + juce::String (inDb - outDb, 2) + " dB)");
            // The windowed minimum reads below the mean band energy (more so in narrow bands); NSP_FLOOR_BIAS
            // compensates so that white and pink both land near 20 dB. The window is the DfnEngine test's.
            expect (inDb - outDb >= 19.5f && inDb - outDb <= 24.5f, pink ? "pink" : "white");
        }

        beginTest ("safety floor never lowers: limit 0 dB -> still the bit-exact STFT round trip");
        {
            auto d = fresh (0.0f, 0.0f);
            d->setSafetyFloor (true);
            auto ref = fresh (0.0f, 0.0f);
            const auto in = nsp::test::whiteNoise (2 * 48000, 0.1f, 11);
            const auto out = run (*d, in), refOut = run (*ref, in);
            float err = 0.0f;
            for (size_t n = 0; n + Denoiser::kLatency < out.size(); ++n)
                err = std::fmax (err, std::fabs (out[n + Denoiser::kLatency] - in[n]));
            logMessage ("    max error " + juce::String (err, 9));
            expectLessThan (err, 1.0e-5f);
            expect (std::memcmp (out.data(), refOut.data(), out.size() * sizeof (float)) == 0, "bit-identical to floor off");
        }

        beginTest ("safety floor: digital silence in -> all-zero out, no NaN");
        {
            auto d = fresh (80.0f, 0.02f);
            d->setSafetyFloor (true);
            const auto out = run (*d, std::vector<float> (48000, 0.0f));
            expect (allFinite (out));
            expectEquals (maxAbs (out), 0.0f);
        }

        beginTest ("safety floor leaves loud bands untouched: voiced sound with a 1 kHz harmonic, peak -12 dBFS, over -60 dBFS noise, limit 80 -> the 1 kHz ERB band's bins bit-identical with floor on and off wherever that band is above the floor");
        {
            // A plain sine is not usable: DFN treats a tone as noise and removes it (measured: the 1 kHz band ~80 dB
            // down with the floor off, steady or with a 4 Hz envelope, so the floor rightly fills it). The 1 kHz
            // component here is the 5th harmonic of a 200 Hz voiced sound (12 harmonics, 1/k, 4 Hz syllable
            // envelope, peak -12 dBFS), which DFN keeps as speech.
            constexpr int n = 3 * 48000;
            auto in = nsp::test::whiteNoise (n, 0.001f, 104);
            {
                const double twoPi = 2.0 * 3.14159265358979323846;
                std::vector<double> v ((size_t) n);
                double peak = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double t = i / 48000.0;
                    double x = 0.0;
                    for (int k = 1; k <= 12; ++k)
                        x += std::sin (twoPi * 200.0 * k * t) / k;
                    v[(size_t) i] = x * std::abs (std::sin (twoPi * 2.0 * t));
                    peak = std::max (peak, std::abs (v[(size_t) i]));
                }
                const double amp = std::pow (10.0, -12.0 / 20.0);
                for (int i = 0; i < n; ++i)
                    in[(size_t) i] += (float) (amp * v[(size_t) i] / peak);
            }

            // The ERB band holding bin 20 (1 kHz at 50 Hz per bin).
            int lo = 0, hi = 0;
            for (int b = 0, start = 0; b < 32; start += nsp::dfn::erbWidths()[(size_t) b], ++b)
                if (start <= 20 && 20 < start + nsp::dfn::erbWidths()[(size_t) b])
                {
                    lo = start;
                    hi = start + nsp::dfn::erbWidths()[(size_t) b];
                }

            // `noisy` (limit 0 dB) outputs exactly the noisy spectrum it starts from: Y == X[t-2].
            auto on = fresh (80.0f, 0.02f), off = fresh (80.0f, 0.02f), noisy = fresh (0.0f, 0.0f);
            on->setSafetyFloor (true);
            std::vector<float> o ((size_t) H);
            bool same = true, othersFilled = false;
            int loudHops = 0, hops = 0;
            for (int h = 0; h < n / H; ++h, ++hops)
            {
                on->process (in.data() + (size_t) h * H, o.data());
                off->process (in.data() + (size_t) h * H, o.data());
                noisy->process (in.data() + (size_t) h * H, o.data());
                const auto* yOn = on->lastSpectrum();
                const auto* yOff = off->lastSpectrum();
                const auto* x = noisy->lastSpectrum();
                float ex = 0.0f, ey = 0.0f;
                for (int f = lo; f < hi; ++f)
                {
                    ex += std::norm (x[f]);
                    ey += std::norm (yOff[f]);
                }
                // The floor is at most NSP_FLOOR_BELOW x NSP_FLOOR_BIAS x the band's noisy energy (tracked noise <= this
                // hop's energy), so a band at or above that is above the floor and must come through bit-identical.
                if (ey >= NSP_FLOOR_BELOW * NSP_FLOOR_BIAS * ex && ex > 0.0f)
                {
                    ++loudHops;
                    for (int f = lo; f < hi; ++f)
                        same = same && std::abs (yOn[f] - yOff[f]) <= 1.0e-6f * std::abs (yOff[f]);
                }
                for (int f = 0; f < Denoiser::kBins; ++f)
                    othersFilled = othersFilled || yOn[f] != yOff[f];
            }
            logMessage ("    1 kHz band (bins " + juce::String (lo) + ".." + juce::String (hi - 1) + ") above the floor on "
                        + juce::String (loudHops) + " of " + juce::String (hops) + " hops");
            expectGreaterThan (loudHops, hops / 2, "the voiced sound is kept by DFN on most hops");
            expect (same, "1 kHz band unchanged by the floor on those hops");
            expect (othersFilled, "the floor fills some other band");
        }

        beginTest ("C1 silence gate: -80 dBFS noise for 1 s -> gate active, output level = input level - limit (+-1.5 dB) for 12, 24 and 80 dB, never all-zero hops");
        {
            const auto in = nsp::test::whiteNoise (48000, 1.0e-4f, 21);   // mean square 1e-8 < 1e-7
            for (float limDb : { 12.0f, 24.0f, 80.0f })
            {
                auto d = fresh (limDb, limDb > 50.0f ? 0.02f : 0.0f);
                std::vector<Denoiser::Stages> st;
                const auto out = run (*d, in, &st);
                bool gated = true;
                for (size_t h = 10; h < st.size(); ++h)
                    gated = gated && st[h].silenceGate;
                expect (gated, "gate active after 10 hops");

                const int half = 24000;
                const float inDb = nsp::test::rmsDb (in.data() + in.size() - half - Denoiser::kLatency, half);
                const float outDb = nsp::test::rmsDb (out.data() + out.size() - half, half);
                logMessage ("    limit " + juce::String (limDb, 0) + " dB: in " + juce::String (inDb, 2) + " dB, out " + juce::String (outDb, 2) + " dB");
                expectWithinAbsoluteError (outDb, inDb - limDb, 1.5f);

                bool noZeroHop = true;
                for (size_t h = Denoiser::kLatency / H; h < out.size() / H; ++h)
                {
                    bool any = false;
                    for (int i = 0; i < H; ++i)
                        any = any || out[h * H + (size_t) i] != 0.0f;
                    noZeroHop = noZeroHop && any;
                }
                expect (noZeroHop, "every hop after the latency has a non-zero sample");
            }
        }

        beginTest ("C1 resume: 1 s at -80 dBFS then a 1 kHz -12 dBFS sine: finite, max step in the first 50 ms <= 2x the max step 1-2 s later");
        {
            auto d = fresh (24.0f, 0.0f);
            const float amp = std::pow (10.0f, -12.0f / 20.0f);
            const auto in = concat (nsp::test::whiteNoise (48000, 1.0e-4f, 31),
                                    nsp::test::sine (3 * 48000 + Denoiser::kLatency, 1000.0, 48000.0, amp));
            const auto out = run (*d, in);
            expect (allFinite (out));
            const int onset = 48000 + Denoiser::kLatency;
            const float first = nsp::test::maxAbsStep (out.data() + onset, 2400);
            const float later = nsp::test::maxAbsStep (out.data() + onset + 48000, 48000);
            logMessage ("    max step first 50 ms " + juce::String (first, 5) + ", 1-2 s later " + juce::String (later, 5));
            expectLessOrEqual (first, 2.0f * later);
        }

        beginTest ("stage gating follows lsnr: lsnr < -15 -> zero mask, no DF; -15..35 -> ERB + DF; > 35 -> untouched");
        {
            auto check = [this] (const std::vector<Denoiser::Stages>& st, int& zero, int& erbDf)
            {
                bool ok = true;
                for (const auto& s : st)
                {
                    ok = ok && (! s.zeroMask || ! s.df) && (! s.df || s.erb) && (! s.silenceGate || (! s.erb && ! s.df));
                    if (! s.silenceGate)
                    {
                        if (s.lsnr < -15.0f)       ok = ok && s.zeroMask && ! s.erb && ! s.df;
                        else if (s.lsnr > 35.0f)   ok = ok && ! s.zeroMask && ! s.erb && ! s.df;
                        else                       ok = ok && ! s.zeroMask && s.erb && s.df;
                    }
                    ok = ok && s.lsnr >= -15.0f && s.lsnr <= 35.0f;
                    zero += s.zeroMask ? 1 : 0;
                    erbDf += (s.erb && s.df) ? 1 : 0;
                }
                expect (ok, "stage flags consistent with lsnr on every hop");
            };

            auto d = fresh (Denoiser::kNoLimitDb, 0.0f);
            std::vector<Denoiser::Stages> st;
            run (*d, nsp::test::whiteNoise (2 * 48000, 0.0316f, 41), &st);
            int zero = 0, erbDf = 0;
            check (st, zero, erbDf);
            // Zero-mask / untouched are unreachable: lsnr = sigmoid * 50 - 15 is within [-15, 35] and the P6 thresholds are -15 / 35.
            float lo = 100.0f;
            for (const auto& s : st)
                lo = std::fmin (lo, s.lsnr);
            logMessage ("    white noise -30 dBFS: " + juce::String (zero) + " zero-mask hops of " + juce::String ((int) st.size())
                        + ", min lsnr " + juce::String (lo, 2) + " dB");
            expectEquals (zero, 0);
            expectLessThan (lo, -10.0f);

            d->reset();
            st.clear();
            run (*d, nsp::test::speechLike (3 * 48000, 42), &st);
            zero = erbDf = 0;
            check (st, zero, erbDf);
            logMessage ("    speechLike: " + juce::String (erbDf) + " ERB + DF hops of " + juce::String ((int) st.size()));
            expectGreaterThan (erbDf, 0);
        }

        beginTest ("reset() makes the denoiser behave exactly like a freshly prepared one (bit-exact)");
        {
            const auto first = nsp::test::speechLike (48000, 51);
            const auto second = concat (nsp::test::whiteNoise (24000, 1.0e-4f, 52), nsp::test::speechLike (72000, 53));

            for (const bool floorOn : { false, true })
            {
                auto a = fresh (12.0f, 0.0f);
                a->setSafetyFloor (floorOn);
                run (*a, first);
                a->setStrength (80.0f, 0.02f, false);   // ramp still pending: reset() must jump to the target
                a->reset();
                const auto outA = run (*a, second);

                auto b = fresh (80.0f, 0.02f);
                b->setSafetyFloor (floorOn);
                const auto outB = run (*b, second);
                expect (outA.size() == outB.size() && std::memcmp (outA.data(), outB.data(), outA.size() * sizeof (float)) == 0,
                        floorOn ? "safety floor on" : "safety floor off");
            }
        }

        beginTest ("process, reset and setStrength do not allocate");
        for (const bool floorOn : { false, true })
        {
            auto d = fresh (24.0f, 0.0f);
            const auto in = nsp::test::speechLike (20 * H, 61);
            std::vector<float> out (in.size());
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                d->setSafetyFloor (floorOn);
                for (int h = 0; h < 20; ++h)
                {
                    if (h == 7)  d->setStrength (80.0f, 0.02f, false);
                    if (h == 14) d->reset();
                    d->process (in.data() + h * H, out.data() + h * H);
                }
                d->setStrength (12.0f, 0.0f, true);
                allocs = check.count();
            }
            expectEquals (allocs, 0, floorOn ? "safety floor on" : "safety floor off");
        }

        beginTest ("hot input: 0 dBFS white noise and a +6 dBFS sine -> output finite, max |out| < 4");
        {
            for (int sig = 0; sig < 2; ++sig)
            {
                auto d = fresh (24.0f, 0.02f);
                const auto in = sig == 0 ? nsp::test::whiteNoise (2 * 48000, 1.0f / std::sqrt (3.0f), 71)
                                         : nsp::test::sine (2 * 48000, 440.0, 48000.0, 2.0f);
                const auto out = run (*d, in);
                expect (allFinite (out));
                expectLessThan (maxAbs (out), 4.0f);
            }
        }

        beginTest ("NaN self-recovery: one hop of 1e20 (and larger) then 1 s of -30 dBFS noise -> all output finite, last 0.5 s non-zero at a sensible level");
        {
            const float spikes[] = { 1.0e20f, 1.0e30f, 3.0e38f };
            for (const float spike : spikes)
            {
                auto d = fresh (24.0f, 0.0f);
                std::vector<float> in (H, spike);
                const auto noise = nsp::test::whiteNoise (48000, 0.0316f, 73);
                in.insert (in.end(), noise.begin(), noise.end());
                const auto out = run (*d, in);
                expect (allFinite (out), "finite, hop value " + juce::String (spike));
                const int n = 24000, start = (int) out.size() - n;
                const float db = nsp::test::rmsDb (out.data() + start, n);
                expectGreaterThan (db, -60.0f);
                expectLessThan (db, -20.0f);
            }
        }

        beginTest ("end-to-end vs official deep-filter v0.5.6 (local only): every 48 kHz mono recording with a reference >= 30 dB match");
       #if JUCE_DEBUG
        logMessage ("    skipped in Debug");
       #else
        endToEnd (net);
       #endif
    }

    /** C2/C3: our output vs the CLI's (-D, defaults: no limit, no post-filter), on the user's recordings.
        The CLI writes 16-bit (trunc (x * 32767)); our output is quantised the same way before comparing.
        The references must come from one CLI run per file (Tools/DfnBlobTool cli-refs): a single run over
        several files keeps the model state from one mono file to the next (enhance_wav.rs only rebuilds the
        model when the channel count changes), which gave 32 dB instead of 103 dB on the second file. */
    void endToEnd (const nsp::dfn::Network& net)
    {
        const auto inDir = nsp::test::testAudioDir();
        const auto refDir = nsp::test::testsDir().getChildFile ("render").getChildFile ("deepfilternet");
        if (! inDir.isDirectory() || ! refDir.isDirectory())
        {
            logMessage ("    skipped: no test recordings (" + inDir.getFullPathName() + ") or no CLI references (" + refDir.getFullPathName() + ")");
            return;
        }
        auto files = inDir.findChildFiles (juce::File::findFiles, false, "*.wav");
        files.sort();
        int compared = 0;
        for (const auto& f : files)
        {
            const auto refFile = refDir.getChildFile (f.getFileName());
            std::vector<float> in, ref;
            bool in16 = false, ref16 = false;
            double rate = 0.0, refRate = 0.0;
            int ch = 0, refCh = 0;
            if (! refFile.existsAsFile() || ! readWav (f, in, in16, rate, ch) || rate != 48000.0 || ch != 1
                || ! readWav (refFile, ref, ref16, refRate, refCh) || refRate != 48000.0 || refCh != 1)
                continue;

            // Input hops the CLI zeroed: v0.5.6 returns zeros for a hop with mean square < 1e-7 before its STFT
            // analysis, so nothing (STFT, norm states, networks) advances on that hop. Feeding such hops to our
            // denoiser instead lets its network state drift from the CLI's for many seconds afterwards (measured:
            // 1 to 25 dB match on the recordings with digital silence), so they are skipped here the same way and
            // our output there is zeros too. The C1 gate is covered by the synthetic tests above.
            const int hops = (int) in.size() / H;
            std::vector<bool> silent ((size_t) hops);
            for (int j = 0; j < hops; ++j)
            {
                float e = 0.0f;
                for (int i = 0; i < H; ++i)
                    e += in[(size_t) (j * H + i)] * in[(size_t) (j * H + i)];
                silent[(size_t) j] = e / (float) H < 1.0e-7f;
            }

            auto d = std::make_unique<Denoiser>();
            d->prepare (net);
            d->setStrength (Denoiser::kNoLimitDb, 0.0f, true);
            std::vector<float> ours ((size_t) hops * H, 0.0f);
            for (int j = 0; j < hops; ++j)
                if (! silent[(size_t) j])
                    d->process (in.data() + (size_t) j * H, ours.data() + (size_t) j * H);
            if (ref16)
                for (auto& x : ours)
                    x = std::trunc (juce::jlimit (-1.0f, 1.0f, x) * 32767.0f) / 32767.0f;

            // Comparison hop m (ref[480m ..]) is our input hop m + 3; exclude it if any of input hops m .. m + 3
            // was silent (the zeroed hop and the 3 hops after it, C2).
            const int cmpHops = juce::jmin ((int) ref.size(), (int) ours.size() - Denoiser::kLatency) / H;
            double sigE = 0.0, errE = 0.0;
            int excluded = 0;
            for (int m = 0; m < cmpHops; ++m)
            {
                bool skipHop = false;
                for (int j = m; j <= m + 3 && j < hops; ++j)
                    skipHop = skipHop || silent[(size_t) j];
                if (skipHop)
                {
                    ++excluded;
                    continue;
                }
                for (int i = 0; i < H; ++i)
                {
                    const size_t n = (size_t) (m * H + i);
                    const double r = ref[n], e = (double) ours[n + Denoiser::kLatency] - r;
                    sigE += r * r;
                    errE += e * e;
                }
            }
            const double db = errE > 0.0 ? 10.0 * std::log10 (sigE / errE) : 200.0;
            logMessage ("    " + f.getFileName() + ": " + juce::String (db, 2) + " dB (" + juce::String (excluded)
                        + " of " + juce::String (cmpHops) + " hops excluded)");
            expectGreaterOrEqual (db, 30.0, f.getFileName());
            ++compared;
        }
        if (compared == 0)
            logMessage ("    skipped: no 48 kHz mono recording with a CLI reference in " + inDir.getFullPathName());
    }
};

static DfnDenoiserTests dfnDenoiserTests;
} // namespace
