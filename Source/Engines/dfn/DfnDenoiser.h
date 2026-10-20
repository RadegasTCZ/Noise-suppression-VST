// Ported from DeepFilterNet libDF, commit d375b2d. Portions Copyright (c) 2021 Hendrik Schröter, used under the
// MIT licence (ThirdParty/deepfilternet/LICENSE-MIT); the port and its changes are part of this project (GPLv3).
#pragma once

#include "DfnNetwork.h"
#include "../SafetyFloor.h"
#include <array>
#include <complex>
#include <memory>

struct kiss_fftr_state;   // SpeexDSP kissfft (kiss_fftr_cfg), kept out of this header

namespace nsp::dfn
{
/** Bins per ERB band (32 bands, sum 481), upstream erb_fb (48 kHz, FFT 960, min 2 bins per band). */
const std::array<int, 32>& erbWidths() noexcept;

/** Upstream calc_norm_alpha: exp(-hop/sr/tau), rounded to the fewest decimals (>= 3) that keep it below 1. */
float normAlpha (int sr, int hop, float tau) noexcept;

/** Upstream post_filter on n bins, processed in groups of 4 (a trailing n % 4 bins are left untouched,
    as upstream does with chunks_exact). beta <= 0 leaves enh unchanged. */
void postFilter (const std::complex<float>* noisy, std::complex<float>* enh, int n, float beta) noexcept;

/** One channel of DeepFilterNet3 processing: libDF's STFT (Vorbis window, 960/480), feature extraction,
    LSNR stage gating, ERB mask, deep filter, post-filter and attenuation limit (DfTract::process), with
    the silence gate keeping the STFT running (spec C1). Fixed latency kLatency samples. */
class Denoiser
{
public:
    static constexpr int kHop = 480, kFft = 960, kBins = 481, kLatency = 1440;
    static constexpr int kRampHops = 5;                               // strength ramp ~50 ms
    static constexpr float kNoLimitDb = 100.0f;                       // >= this: no attenuation limit

    /** Safety-floor cap: the one-sided spectrum energy sum_f |X[f]|^2 (f = 0..480, this STFT's scaling) that
        corresponds to an output mean square of 1e-8 (-80 dBFS). Parseval with the analysis scaling:
        X = wnorm * FFT (w . x), wnorm = 2 hop / fft^2 = 1/960. For a stationary signal of mean square s, every
        bin has E|X[f]|^2 = wnorm^2 * s * sum_n w[n]^2, and sum_n w[n]^2 = hop = 480 because the Vorbis window
        is power-complementary at 50 % overlap (w[n]^2 + w[n + hop]^2 = 1). Summed over the 481 bins:
        E = s * 481 * 480 / 960^2 = s * 481 / 1920. Synthesis (unnormalised inverse FFT, window, overlap-add)
        is the exact inverse of analysis, so a fill Y = g * X gives output mean square g^2 * s. Hence the
        output mean square of a spectrum with energy sum E is E * 1920 / 481, and -80 dBFS is E = 1e-8 * 481 / 1920. */
    static constexpr float kFloorCapEnergy = 1.0e-8f * (float) kBins * (float) kHop / (float) (kFft * kFft);

    Denoiser();
    ~Denoiser();
    Denoiser (const Denoiser&) = delete;
    Denoiser& operator= (const Denoiser&) = delete;

    /** Allocates (network state, FFT configs) and resets. Message thread. The network must be
        initialised and must outlive this denoiser. False if an allocation failed. */
    bool prepare (const Network&);

    /** Back to the freshly prepared state, bit-exact; jumps to the target strength. */
    void reset() noexcept;

    /** attenLimDb: attenuation limit in dB (>= kNoLimitDb: none). postFilterBeta: 0 = off.
        immediate = false ramps linearly over kRampHops hops. */
    void setStrength (float attenLimDb, float postFilterBeta, bool immediate) noexcept;

    /** Safety floor (local, not upstream): after the attenuation limit, every ERB band whose output energy is
        below (tracked room noise - 20 dB), capped at kFloorCapEnergy in total, gets the noisy band scaled to
        exactly that floor; louder bands are untouched. The room noise is a per-band windowed minimum over the
        last ~1.5 s (Source/Engines/SafetyFloor.h). Off by default (upstream fidelity); turning it on starts
        a fresh noise tracker. Allocation-free. */
    void setSafetyFloor (bool on) noexcept;

    /** kHop samples; in == out allowed. Before a successful prepare() the output is silence. */
    void process (const float* in, float* out) noexcept;

    struct Stages { bool erb = false, zeroMask = false, df = false, silenceGate = false; float lsnr = 0.0f; };
    Stages lastStages() const noexcept { return stages; }             // what the last hop did (tests)
    const std::complex<float>* lastSpectrum() const noexcept { return Y; }   // kBins, the last hop's output spectrum (tests)

private:
    using cpx = std::complex<float>;
    static constexpr int kErb = 32, kNbDf = 96, kOrder = 5;

    void analysis (const float* in, cpx* X) noexcept;
    void synthesis (const cpx* Y, float* out) noexcept;
    void computeFeatures (const cpx* X) noexcept;
    void advanceRamp() noexcept;
    const cpx* frame (int ago) const noexcept { return ring[(ringPos + kOrder - ago) % kOrder]; }

    const Network* net = nullptr;
    std::unique_ptr<Network::State> state;
    kiss_fftr_state* fftFwd = nullptr;
    kiss_fftr_state* fftInv = nullptr;

    float window[kFft];
    static_assert (kErb <= NSP_FLOOR_MAX_BANDS, "safety floor tracker holds NSP_FLOOR_MAX_BANDS bands");
    int bandStart[kErb + 1];

    float analysisMem[kHop], synthesisMem[kHop];
    float timeBuf[kFft];
    cpx ring[kOrder][kBins];                                          // X[t-4 .. t], ringPos = X[t]
    int ringPos = 0;
    cpx Y[kBins];
    float meanNorm[kErb], unitNorm[kNbDf];
    float featErb[kErb], featSpec[2 * kNbDf];
    int skip = 0;
    Stages stages;

    float lim = 0.0f, beta = 0.0f;                                    // current (per hop)
    float limTarget = 0.0f, betaTarget = 0.0f;
    float limStep = 0.0f, betaStep = 0.0f;
    int rampLeft = 0;

    bool floorOn = false;
    NspFloor safetyFloor {};
};
} // namespace nsp::dfn
