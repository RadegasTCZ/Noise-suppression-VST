// Ported from DeepFilterNet libDF, commit d375b2d. Portions Copyright (c) 2021 Hendrik Schröter, used under the
// MIT licence (ThirdParty/deepfilternet/LICENSE-MIT); the port and its changes are part of this project (GPLv3).
#include "DfnDenoiser.h"

// SpeexDSP's kiss_fft.h includes arch.h, which requires FLOATING_POINT (as in sns/HybridDenoise.h).
#if !defined(FLOATING_POINT) && !defined(FIXED_POINT)
#define FLOATING_POINT
#endif
extern "C"
{
#include "kiss_fft.h"
#include "kiss_fftr.h"
}

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace nsp::dfn
{
namespace
{
constexpr float kMinDbThresh = -15.0f, kMaxDbErbThresh = 35.0f, kMaxDbDfThresh = 35.0f;   // P6 (CLI defaults)
constexpr float kMeanNormInit[2] = { -60.0f, -90.0f };      // upstream MEAN_NORM_INIT
constexpr float kUnitNormInit[2] = { 0.001f, 0.0001f };     // upstream UNIT_NORM_INIT
const float kAlpha = normAlpha (48000, Denoiser::kHop, 1.0f);   // config norm_tau = 1 -> 0.99

// lib.rs freq2erb / erb2freq (f32 throughout, as upstream)
float freq2erb (float hz) noexcept { return 9.265f * std::log1p (hz / (24.7f * 9.265f)); }
float erb2freq (float erb) noexcept { return 24.7f * 9.265f * (std::exp (erb / 9.265f) - 1.0f); }
} // namespace

const std::array<int, 32>& erbWidths() noexcept
{
    // lib.rs erb_fb (sr 48000, fft 960, 32 bands, min 2 bins per band)
    static const std::array<int, 32> widths = []
    {
        constexpr int sr = 48000, fft = 960, bands = 32, minFreqs = 2;
        std::array<int, 32> erb {};
        const float freqWidth = (float) sr / (float) fft;
        const float erbLow = freq2erb (0.0f), erbHigh = freq2erb ((float) (sr / 2));
        const float step = (erbHigh - erbLow) / (float) bands;
        int prevFreq = 0, freqOver = 0;
        for (int i = 1; i <= bands; ++i)
        {
            const float f = erb2freq (erbLow + (float) i * step);
            const int fb = (int) std::round (f / freqWidth);
            int nbFreqs = fb - prevFreq - freqOver;
            if (nbFreqs < minFreqs)
            {
                freqOver = minFreqs - nbFreqs;
                nbFreqs = minFreqs;
            }
            else
            {
                freqOver = 0;
            }
            erb[(size_t) i - 1] = nbFreqs;
            prevFreq = fb;
        }
        erb[bands - 1] += 1;
        int sum = 0;
        for (auto w : erb)
            sum += w;
        if (sum > fft / 2 + 1)
            erb[bands - 1] -= sum - (fft / 2 + 1);
        return erb;
    }();
    return widths;
}

float normAlpha (int sr, int hop, float tau) noexcept
{
    // tract.rs calc_norm_alpha
    const float dt = (float) hop / (float) sr;
    const float alpha = std::exp (-dt / tau);
    float a = 1.0f;
    for (int precision = 3; a >= 1.0f && precision <= 9; ++precision)   // cap: upstream would overflow i32 at 10
    {
        float p = 1.0f;
        for (int i = 0; i < precision; ++i)
            p *= 10.0f;
        a = std::round (alpha * p) / p;
    }
    return a;
}

void postFilter (const std::complex<float>* noisy, std::complex<float>* enh, int n, float beta) noexcept
{
    // lib.rs post_filter
    if (! (beta > 0.0f))
        return;
    const float betaP1 = beta + 1.0f, eps = 1.0e-12f, pi = 3.14159265358979f;
    for (int i = 0; i + 4 <= n; i += 4)
        for (int j = i; j < i + 4; ++j)
        {
            const auto e = enh[j], x = noisy[j];
            const float g = std::max (std::min (std::hypot (e.real(), e.imag()) / (std::hypot (x.real(), x.imag()) + eps), 1.0f), eps);
            const float gSin = g * std::sin (g * pi / 2.0f);
            const float r = g / gSin;
            const float pf = (betaP1 * g / (1.0f + beta * (r * r))) / g;
            enh[j] = { e.real() * pf, e.imag() * pf };
        }
}

Denoiser::Denoiser()
{
    // lib.rs DFState::new: Vorbis window sin(pi/2 * sin^2(pi/2 * (i + 0.5) / (fft/2))), in double
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < kFft; ++i)
    {
        const double s = std::sin (0.5 * pi * ((double) i + 0.5) / (double) (kFft / 2));
        window[i] = (float) std::sin (0.5 * pi * s * s);
    }
    const auto& w = erbWidths();
    bandStart[0] = 0;
    for (int b = 0; b < kErb; ++b)
        bandStart[b + 1] = bandStart[b] + w[(size_t) b];
    reset();
}

Denoiser::~Denoiser()
{
    // kiss_fftr_free is a macro clashing with speex_free; the configs come from calloc (os_support.h).
    std::free (fftFwd);
    std::free (fftInv);
}

bool Denoiser::prepare (const Network& network)
{
    if (state == nullptr)
        state = std::make_unique<Network::State>();
    if (fftFwd == nullptr)
        fftFwd = kiss_fftr_alloc (kFft, 0, nullptr, nullptr);
    if (fftInv == nullptr)
        fftInv = kiss_fftr_alloc (kFft, 1, nullptr, nullptr);
    const bool ok = fftFwd != nullptr && fftInv != nullptr;
    net = ok ? &network : nullptr;
    reset();
    return ok;
}

void Denoiser::reset() noexcept
{
    std::memset (analysisMem, 0, sizeof analysisMem);
    std::memset (synthesisMem, 0, sizeof synthesisMem);
    std::memset (timeBuf, 0, sizeof timeBuf);
    std::fill (&ring[0][0], &ring[0][0] + kOrder * kBins, cpx {});
    std::fill (Y, Y + kBins, cpx {});
    ringPos = 0;

    // lib.rs init_mean_norm_state / init_unit_norm_state
    const float mStep = (kMeanNormInit[1] - kMeanNormInit[0]) / (float) (kErb - 1);
    for (int i = 0; i < kErb; ++i)
        meanNorm[i] = kMeanNormInit[0] + (float) i * mStep;
    const float uStep = (kUnitNormInit[1] - kUnitNormInit[0]) / (float) (kNbDf - 1);
    for (int i = 0; i < kNbDf; ++i)
        unitNorm[i] = kUnitNormInit[0] + (float) i * uStep;
    std::memset (featErb, 0, sizeof featErb);
    std::memset (featSpec, 0, sizeof featSpec);

    skip = 0;
    stages = {};
    if (state != nullptr)
        state->clear();

    lim = limTarget;
    beta = betaTarget;
    limStep = betaStep = 0.0f;
    rampLeft = 0;

    nsp_floor_reset (&safetyFloor);
}

void Denoiser::setSafetyFloor (bool on) noexcept
{
    if (on && ! floorOn)
        nsp_floor_reset (&safetyFloor);   // the tracker is frozen while off: start fresh
    floorOn = on;
}

void Denoiser::setStrength (float attenLimDb, float postFilterBeta, bool immediate) noexcept
{
    // tract.rs set_atten_lim / set_pf_beta
    const float db = std::fabs (attenLimDb);
    const float newLim = db >= kNoLimitDb ? 0.0f : (db < 0.01f ? 1.0f : std::pow (10.0f, -db / 20.0f));
    const float newBeta = postFilterBeta > 0.0f ? postFilterBeta : 0.0f;

    if (immediate)
    {
        lim = limTarget = newLim;
        beta = betaTarget = newBeta;
        limStep = betaStep = 0.0f;
        rampLeft = 0;
        return;
    }
    if (newLim == limTarget && newBeta == betaTarget)
        return;
    limTarget = newLim;
    betaTarget = newBeta;
    limStep = (limTarget - lim) / (float) kRampHops;
    betaStep = (betaTarget - beta) / (float) kRampHops;
    rampLeft = kRampHops;
}

void Denoiser::advanceRamp() noexcept
{
    if (rampLeft <= 0)
        return;
    if (--rampLeft == 0)
    {
        lim = limTarget;
        beta = betaTarget;
    }
    else
    {
        lim += limStep;
        beta += betaStep;
    }
}

void Denoiser::analysis (const float* in, cpx* X) noexcept
{
    // lib.rs frame_analysis (fft 960, hop 480: analysis_mem is exactly the previous hop)
    for (int i = 0; i < kHop; ++i)
        timeBuf[i] = analysisMem[i] * window[i];
    for (int i = 0; i < kHop; ++i)
        timeBuf[kHop + i] = in[i] * window[kHop + i];
    std::memcpy (analysisMem, in, sizeof analysisMem);
    kiss_fftr (fftFwd, timeBuf, reinterpret_cast<kiss_fft_cpx*> (X));   // unnormalised, like realfft
    const float wnorm = 1.0f / ((float) (kFft * kFft) / (float) (2 * kHop));
    for (int f = 0; f < kBins; ++f)
        X[f] = { X[f].real() * wnorm, X[f].imag() * wnorm };
}

void Denoiser::synthesis (const cpx* spec, float* out) noexcept
{
    // lib.rs frame_synthesis; kissfft ignores the DC / Nyquist imaginary parts, as realfft does
    kiss_fftri (fftInv, reinterpret_cast<const kiss_fft_cpx*> (spec), timeBuf);
    for (int i = 0; i < kFft; ++i)
        timeBuf[i] *= window[i];
    for (int i = 0; i < kHop; ++i)
        out[i] = timeBuf[i] + synthesisMem[i];
    std::memcpy (synthesisMem, timeBuf + kHop, sizeof synthesisMem);
}

void Denoiser::computeFeatures (const cpx* X) noexcept
{
    // lib.rs feat_erb: compute_band_corr, 10 log10, band_mean_norm_erb
    for (int b = 0; b < kErb; ++b)
    {
        const float k = 1.0f / (float) (bandStart[b + 1] - bandStart[b]);
        float p = 0.0f;
        for (int f = bandStart[b]; f < bandStart[b + 1]; ++f)
            p += (X[f].real() * X[f].real() + X[f].imag() * X[f].imag()) * k;
        float v = std::log10 (p + 1.0e-10f) * 10.0f;
        meanNorm[b] = v * (1.0f - kAlpha) + meanNorm[b] * kAlpha;
        v -= meanNorm[b];
        featErb[b] = v / 40.0f;
    }
    // lib.rs feat_cplx: band_unit_norm, laid out [re 96][im 96]
    for (int f = 0; f < kNbDf; ++f)
    {
        unitNorm[f] = std::hypot (X[f].real(), X[f].imag()) * (1.0f - kAlpha) + unitNorm[f] * kAlpha;
        const float s = std::sqrt (std::max (unitNorm[f], 1.0e-12f));   // floor: FTZ must not turn this into inf
        featSpec[f] = X[f].real() / s;
        featSpec[kNbDf + f] = X[f].imag() / s;
    }
}

void Denoiser::process (const float* in, float* out) noexcept
{
    if (net == nullptr)
    {
        std::fill (out, out + kHop, 0.0f);
        return;
    }
    advanceRamp();

    // tract.rs DfTract::process, with the silence gate after the STFT analysis (C1). The gate counts
    // consecutive hops with mean square < 1e-7 only. Upstream also resets its counter on every hop whose
    // stage-1 mask is applied (zero mask or ERB) and increments it on untouched hops; since lsnr is in
    // [-15, 35] by construction, every hop applies a mask, so upstream's gate never fires in practice.
    // C1 specifies the plain silence trigger, so those two updates are left out.
    float e = 0.0f;
    for (int i = 0; i < kHop; ++i)
        e += in[i] * in[i];
    skip = (e / (float) kHop < 1.0e-7f) ? std::min (skip + 1, 1000) : 0;   // capped: no overflow in long silence

    ringPos = (ringPos + 1) % kOrder;
    analysis (in, ring[ringPos]);

    stages = {};
    if (skip > 5)
    {
        stages.silenceGate = true;   // networks skipped (state frozen); treated as a zero mask below
        stages.lsnr = kMinDbThresh;  // upstream returns -15 here
    }
    else
    {
        computeFeatures (ring[ringPos]);
        net->encode (*state, featErb, featSpec);
        // The SSE min/max in the network can swallow a NaN in lsnr, so the features are checked as well (x * 0 is NaN for inf / NaN).
        float probe = state->lsnr;
        for (int i = 0; i < kErb; ++i)
            probe += featErb[i] * 0.0f;
        for (int i = 0; i < 2 * kNbDf; ++i)
            probe += featSpec[i] * 0.0f;
        if (! std::isfinite (probe))
        {
            // Only reachable with non-physical input (e.g. |x| ~ 1e20). One zero hop is acceptable there, and
            // resetting (allocation-free) stops a NaN from living in the recurrent state for the rest of the session.
            reset();
            std::fill (out, out + kHop, 0.0f);
            return;
        }
        stages.lsnr = state->lsnr;
        if (stages.lsnr < kMinDbThresh)
        {
            stages.zeroMask = true;  // unreachable (lsnr >= -15); kept as upstream has it
        }
        else if (stages.lsnr > kMaxDbErbThresh)
        {
            // untouched: no mask, no DF (upstream: gains None); unreachable (lsnr <= 35)
        }
        else if (stages.lsnr > kMaxDbDfThresh)
        {
            stages.erb = true;       // unreachable with 35 / 35; kept as upstream has it
            net->decodeErb (*state);
        }
        else
        {
            stages.erb = stages.df = true;
            net->decodeErb (*state);
            net->decodeDf (*state);
        }
    }

    // Output spectrum from X[t-2] (lookahead 2): ERB mask (apply_interp_band_gain)
    const cpx* noisy = frame (2);
    std::copy (noisy, noisy + kBins, Y);
    if (stages.erb || stages.zeroMask || stages.silenceGate)
    {
        for (int b = 0; b < kErb; ++b)
        {
            const float g = stages.erb ? state->gains[b] : 0.0f;
            for (int f = bandStart[b]; f < bandStart[b + 1]; ++f)
                Y[f] = { Y[f].real() * g, Y[f].imag() * g };
        }
    }

    // Deep filter on the first 96 bins: Y[f] = sum_k X[t-4+k][f] * c[f][k]  (tract.rs df)
    if (stages.df)
    {
        const float* c = state->coefs;
        for (int f = 0; f < kNbDf; ++f)
        {
            float re = 0.0f, im = 0.0f;
            for (int k = 0; k < kOrder; ++k)
            {
                const cpx s = frame (kOrder - 1 - k)[f];
                const float cr = c[f * 10 + k * 2], ci = c[f * 10 + k * 2 + 1];
                const float pr = s.real() * cr - s.imag() * ci;
                const float pi = s.real() * ci + s.imag() * cr;
                re += pr;
                im += pi;
            }
            Y[f] = { re, im };
        }
    }

    if (stages.erb && beta > 0.0f)
        postFilter (noisy, Y, kBins, beta);

    // Attenuation limit: mix back lim * noisy (none when lim == 0, i.e. upstream atten_lim None)
    if (lim > 0.0f)
    {
        const float keep = 1.0f - lim;
        for (int f = 0; f < kBins; ++f)
            Y[f] = { Y[f].real() * keep + lim * noisy[f].real(), Y[f].imag() * keep + lim * noisy[f].imag() };
    }

    // Safety floor (local, see setSafetyFloor): bands below (room noise - 20 dB, total capped at -80 dBFS; room
    // noise = windowed minimum over ~1.5 s, SafetyFloor.h) get the noisy band scaled to exactly the floor; every
    // stage, including the silence gate.
    if (floorOn)
    {
        float ex[kErb], ey[kErb], floorE[kErb];
        for (int b = 0; b < kErb; ++b)
        {
            float sx = 0.0f, sy = 0.0f;
            for (int f = bandStart[b]; f < bandStart[b + 1]; ++f)
            {
                sx += noisy[f].real() * noisy[f].real() + noisy[f].imag() * noisy[f].imag();
                sy += Y[f].real() * Y[f].real() + Y[f].imag() * Y[f].imag();
            }
            ex[b] = sx;
            ey[b] = sy;
        }
        nsp_floor_update (&safetyFloor, ex, kErb, kFloorCapEnergy, floorE);
        for (int b = 0; b < kErb; ++b)
        {
            const float g = nsp_floor_fill_gain (ex[b], ey[b], floorE[b]);
            if (g > 0.0f)
                for (int f = bandStart[b]; f < bandStart[b + 1]; ++f)
                    Y[f] = { noisy[f].real() * g, noisy[f].imag() * g };
        }
    }

    synthesis (Y, out);
}
} // namespace nsp::dfn
