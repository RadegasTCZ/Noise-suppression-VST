/* compute_band_energy() and interp_band_gain() are adapted from RNNoise src/denoise.c --
   Copyright (c) 2017 Mozilla, 2018 Gregor Richards, 2024 Jean-Marc Valin; BSD-3-Clause,
   see ThirdParty/rnnoise/COPYING. */
/* The noise estimation (S/Smin/Stmp minimum tracking, min_range schedule) is adapted from SpeexDSP
   libspeexdsp/preprocess.c -- Copyright (C) 2003 Epic Games (written by Jean-Marc Valin), 2004-2006 Epic Games; BSD-3-Clause,
   licence text in the header of ThirdParty/speexdsp/preprocess.c. */
/*
  HybridDenoise v2.4.0 - Clean 40-band ERB noise suppressor.

  SpeexDSP-style core with 40 ERB bands:
  - Per-band Martin minimum-tracking noise estimation with speech gate
  - Ephraim-Malah log-MMSE gain (decision-directed, gamma_dd=0.92)
  - Two-level OMLSA speech probability (frame + per-band)
  - Per-band noise-relative gain floor
  - Standard Kiss FFT (960-pt real), computed Vorbis window
*/

#include "HybridDenoise.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Precomputed Vorbis window (half-window, 480 samples) ---- */
static float hd_half_window[HD_FRAME_SIZE];
static bool hd_window_inited = false;

static void hd_init_window() {
    if (hd_window_inited) return;
    for (int n = 0; n < HD_FRAME_SIZE; n++) {
        float s = std::sin((float)M_PI * (n + 0.5f) / HD_WINDOW_SIZE);  /* nsp window fix: power-complementary (v2.4.0 used n: 0.514 % ripple) */
        hd_half_window[n] = std::sin((float)(M_PI / 2.0) * s * s);
    }
    hd_window_inited = true;
}

/* 40-band ERB edge table (42 entries, last is sentinel for interp).
   Bands 0-17: v1's proven ERB edges for 0-3.4kHz (min 2 bins/band).
   Bands 18-39: ERB-spaced from 3.4-20kHz. */
static const int eband40[HD_NB_BANDS + 2] = {
    0, 2, 4, 6, 8, 10, 12, 15, 18, 21, 24, 28, 32, 36, 41,
    47, 53, 60, 68, 74, 80, 87, 95, 103, 111, 121, 131, 142, 154, 167,
    181, 196, 212, 230, 249, 269, 291, 315, 341, 370, 400, 481
};

/* ---- Exponential Integral E1(x) for Ephraim-Malah log-MMSE ---- */
static float expint1(float x) {
    if (x <= 0.0f) return 10.0f;
    if (x < 1.0f) {
        return -0.5772157f - std::log(x) + x * (1.0f + x * (-0.25f + x * (1.0f/18.0f - x/96.0f)));
    }
    float num = x * x + 2.334733f * x + 0.250621f;
    float den = x * x + 3.330657f * x + 1.681534f;
    return std::exp(-x) / x * (num / den);
}

/* ---- DSP functions ---- */

static void compute_band_energy(float *bandE, const kiss_fft_cpx *X) {
    float sum[HD_NB_BANDS + 2] = {0};
    for (int i = 0; i < HD_NB_BANDS + 1; i++) {
        int band_size = eband40[i + 1] - eband40[i];
        for (int j = 0; j < band_size; j++) {
            float frac = (float)j / band_size;
            float tmp = X[eband40[i] + j].r * X[eband40[i] + j].r
                      + X[eband40[i] + j].i * X[eband40[i] + j].i;
            sum[i]     += (1.0f - frac) * tmp;
            sum[i + 1] += frac * tmp;
        }
    }
    sum[1] = (sum[0] + sum[1]) * 2.0f / 3.0f;
    sum[HD_NB_BANDS] = (sum[HD_NB_BANDS] + sum[HD_NB_BANDS + 1]) * 2.0f / 3.0f;
    for (int i = 0; i < HD_NB_BANDS; i++)
        bandE[i] = sum[i + 1];
}

static void interp_band_gain(float *g, const float *bandE) {
    std::memset(g, 0, HD_FREQ_SIZE * sizeof(float));
    for (int i = 1; i < HD_NB_BANDS; i++) {
        int band_size = eband40[i + 1] - eband40[i];
        for (int j = 0; j < band_size; j++) {
            float frac = (float)j / band_size;
            g[eband40[i] + j] = (1.0f - frac) * bandE[i - 1] + frac * bandE[i];
        }
    }
    for (int j = 0; j < eband40[1]; j++)
        g[j] = bandE[0];
    for (int j = eband40[HD_NB_BANDS]; j < eband40[HD_NB_BANDS + 1]; j++)
        g[j] = bandE[HD_NB_BANDS - 1];
}

/* ---- FFT wrappers ---- */

static void forward_transform(kiss_fftr_cfg cfg, kiss_fft_cpx *out, const float *in) {
    kiss_fftr(cfg, in, out);
    const float scale = 1.0f / HD_WINDOW_SIZE;
    for (int i = 0; i < HD_FREQ_SIZE; i++) {
        out[i].r *= scale;
        out[i].i *= scale;
    }
}

static void inverse_transform(kiss_fftr_cfg cfg, float *out, const kiss_fft_cpx *in) {
    kiss_fftri(cfg, in, out);
}

static void apply_window(float *x) {
    for (int i = 0; i < HD_FRAME_SIZE; i++) {
        x[i] *= hd_half_window[i];
        x[HD_WINDOW_SIZE - 1 - i] *= hd_half_window[i];
    }
}

/* ---- Public API ---- */

void hybrid_denoise_init(HybridDenoiseState *st) {
    std::memset(st, 0, sizeof(*st));

    st->fft_fwd = kiss_fftr_alloc(HD_WINDOW_SIZE, 0, NULL, NULL);
    st->fft_inv = kiss_fftr_alloc(HD_WINDOW_SIZE, 1, NULL, NULL);

    hd_init_window();

    st->suppress_db = -20.0f;

    for (int i = 0; i < HD_NB_BANDS; i++) {
        st->noise[i] = 1e-4f;
        st->lastg[i] = 1.0f;
    }
}

void hybrid_denoise_cleanup(HybridDenoiseState *st) {
    if (st->fft_fwd) { free(st->fft_fwd); st->fft_fwd = nullptr; }
    if (st->fft_inv) { free(st->fft_inv); st->fft_inv = nullptr; }
}

/* nsp patch (Noise Suppression 2.0): overlap-add synthesis factored out of step 10
   so the dual-output path can synthesise two spectra with separate memories. */
static void overlap_add_synthesis(kiss_fftr_cfg cfg, float *out, const kiss_fft_cpx *X, float *mem) {
    float synth[HD_WINDOW_SIZE];
    inverse_transform(cfg, synth, X);
    apply_window(synth);
    for (int i = 0; i < HD_FRAME_SIZE; i++)
        out[i] = synth[i] + mem[i];
    std::memcpy(mem, synth + HD_FRAME_SIZE, HD_FRAME_SIZE * sizeof(float));
}

/* nsp patch: body of v2.4.0's hybrid_denoise_process_frame, unchanged up to step 9.
   residual == nullptr: original behaviour (honours residual_mode).
   residual != nullptr: out = gf*X, residual = (1-gf)*X; residual_mode ignored. */
static void hd_process_frame(HybridDenoiseState *st, float *out, float *residual, const float *in) {
    float x[HD_WINDOW_SIZE];
    kiss_fft_cpx X[HD_FREQ_SIZE];
    float gain[HD_NB_BANDS];

    /* 1. Windowed overlap-add analysis */
    std::memcpy(x, st->analysis_mem, HD_FRAME_SIZE * sizeof(float));
    std::memcpy(x + HD_FRAME_SIZE, in, HD_FRAME_SIZE * sizeof(float));
    std::memcpy(st->analysis_mem, in, HD_FRAME_SIZE * sizeof(float));
    apply_window(x);

    /* 2. Forward FFT */
    forward_transform(st->fft_fwd, X, x);

    /* 3. Compute 40-band energy */
    compute_band_energy(st->ps, X);

    /* 4. Per-band noise estimation — SpeexDSP-faithful approach.
       (a) Smoothed power S with spatial smoothing for speech/noise decision.
       (b) Martin minimum-tracking with progressive window length.
       (c) Speech gate: update_prob from S vs Smin ratio.
       (d) Adaptive beta = max(0.03, 1/nb_adapt): fast early, asymptotes to 0.03.
       (e) Noise updated from current ps (not Smin), gated by speech presence. */
    st->nb_adapt++;
    if (st->nb_adapt > 20000)
        st->nb_adapt = 20000;
    st->min_count++;

    /* Adaptive noise smoothing: 1/N ramp, floors at 0.03 (like SpeexDSP) */
    const float beta = std::max(0.03f, 1.0f / (float)st->nb_adapt);
    const float beta_1 = 1.0f - beta;

    /* Progressive min-tracking window (SpeexDSP-style) */
    int min_range;
    if (st->nb_adapt < 100)
        min_range = 15;       /* ~150ms — fast early tracking */
    else if (st->nb_adapt < 1000)
        min_range = 50;       /* ~500ms */
    else if (st->nb_adapt < 10000)
        min_range = 150;      /* ~1.5s */
    else
        min_range = 300;      /* ~3s — fully stable */

    /* Smoothed power S (with spatial smoothing for noise prob) */
    const float alpha_s = 0.80f;
    for (int i = 0; i < HD_NB_BANDS; i++) {
        float band_ps = std::max(st->ps[i], 1e-14f);
        if (st->nb_adapt == 1) {
            st->S[i] = band_ps;
        } else if (i == 0) {
            st->S[i] = alpha_s * st->S[i] + (1.0f - alpha_s) * band_ps;
        } else if (i == HD_NB_BANDS - 1) {
            st->S[i] = alpha_s * st->S[i] + (1.0f - alpha_s) * band_ps;
        } else {
            st->S[i] = alpha_s * st->S[i]
                      + 0.05f * st->ps[i - 1]
                      + 0.10f * band_ps
                      + 0.05f * st->ps[i + 1];
        }
    }

    /* Min-tracking: update Smin/Stmp */
    if (st->nb_adapt == 1) {
        for (int i = 0; i < HD_NB_BANDS; i++) {
            st->Smin[i] = 0.0f;
            st->Stmp[i] = 0.0f;
        }
    }

    if (st->min_count > min_range) {
        st->min_count = 0;
        for (int i = 0; i < HD_NB_BANDS; i++) {
            st->Smin[i] = std::min(st->Stmp[i], st->S[i]);
            st->Stmp[i] = st->S[i];
        }
    } else {
        for (int i = 0; i < HD_NB_BANDS; i++) {
            st->Smin[i] = std::min(st->Smin[i], st->S[i]);
            st->Stmp[i] = std::min(st->Stmp[i], st->S[i]);
        }
    }

    /* Speech presence test + noise update (per band) */
    for (int i = 0; i < HD_NB_BANDS; i++) {
        float band_ps = std::max(st->ps[i], 1e-14f);

        if (st->nb_adapt == 1) {
            st->noise[i] = band_ps;
            st->last_ps[i] = band_ps;
        } else {
            /* Speech gate: 0.4*S > Smin  ⟹  S > 2.5*Smin  ⟹  likely speech */
            bool update_noise = (st->S[i] < 2.5f * std::max(st->Smin[i], 1e-10f));

            /* Update noise when: band looks like noise, OR ps dropped below noise */
            if (update_noise || band_ps < st->noise[i]) {
                st->noise[i] = beta_1 * st->noise[i] + beta * band_ps;
            }
            st->noise[i] = std::max(st->noise[i], 1e-14f);
        }
    }

    /* 5. Display noise level for GUI */
    float mean_noise = 0.0f;
    for (int i = 0; i < HD_NB_BANDS; i++)
        mean_noise += st->noise[i];
    mean_noise /= HD_NB_BANDS;
    st->display_noise_db = 10.0f * std::log10(std::max(mean_noise, 1e-10f)) + 19.0f;

    /* Per-band noise-relative gain floor (SpeexDSP-style).
       noise_floor_power = 10^(suppress_db/10) in power domain.
       gain_floor[i] = sqrt(noise_floor_power * noise[i] / (1 + noise[i]))
       Quiet bands → floor near 0 → full suppression possible.
       Loud bands → floor → 10^(suppress_db/20) (classic flat floor). */
    float noise_floor_power = std::pow(10.0f, st->suppress_db / 10.0f);
    float band_floor[HD_NB_BANDS];
    for (int i = 0; i < HD_NB_BANDS; i++) {
        float n = std::max(st->noise[i], 1e-10f);
        band_floor[i] = std::sqrt(noise_floor_power * n / (1.0f + n));
    }

    /* Flat floor for speech-prob loudness mixing.
       Per-band floor controls EM gain depth (noise suppression).
       Flat floor controls loudness mixing (prevents over-attenuation of speech). */
    float mix_floor = std::pow(10.0f, st->suppress_db / 20.0f);

    /* EM gain with proper temporal stabilization + OMLSA frame speech gate.
       gamma_dd=0.92: proven range (SpeexDSP 0.96, OMLSA 0.95, WebRTC 0.98).
       ps_alpha=0: no IIR on last_ps (gamma_dd handles temporal stability). */
    const float gamma_dd   = 0.92f;
    const float ps_alpha   = 0.0f;

    /* 6. Per-band SNR + Ephraim-Malah log-MMSE gain */
    for (int i = 0; i < HD_NB_BANDS; i++) {
        float ps_safe = std::max(st->ps[i], 1e-10f);
        float noise_safe = std::max(st->noise[i], 1e-10f);

        /* A-posteriori SNR */
        float gamma_k = ps_safe / noise_safe;
        st->post[i] = std::max(gamma_k - 1.0f, 0.0f);

        /* A-priori SNR (decision-directed).
           last_ps already contains gain²*ps from temporal smoothing,
           so dividing by noise gives the prior SNR directly (SpeexDSP-style). */
        float prior_dd = st->last_ps[i] / noise_safe;
        st->prior[i] = gamma_dd * std::max(prior_dd, 0.0f) + (1.0f - gamma_dd) * st->post[i];
        st->prior[i] = std::max(st->prior[i], 1e-3f);

        /* Ephraim-Malah log-MMSE gain */
        float xi = st->prior[i];
        float v = xi / (1.0f + xi) * gamma_k;
        v = std::max(v, 1e-3f);

        float g = xi / (1.0f + xi) * std::exp(0.5f * expint1(v));
        g = std::min(g, 1.0f);
        g = std::max(g, band_floor[i]);
        st->gain[i] = g;
    }

    /* 7. Save clean EM gain for next frame's decision-directed estimate */
    for (int i = 0; i < HD_NB_BANDS; i++)
        st->lastg[i] = st->gain[i];

    /* Smoothed power estimate for decision-directed feedback.
       ps_alpha: 0 at smoothing 0 (no memory), 0.2 at smoothing 10 (original). */
    for (int i = 0; i < HD_NB_BANDS; i++)
        st->last_ps[i] = ps_alpha * st->last_ps[i]
                        + (1.0f - ps_alpha) * st->lastg[i] * st->lastg[i] * st->ps[i];

    /* 8. OMLSA-inspired frame-level speech gate + GH1/GH0 interpolation.
       - Compute mean a-priori SNR across all bands (broadband speech indicator).
       - Derive frame-level speech probability Pframe from mean SNR.
       - GH1 = raw EM gain (per-band, responsive).
       - GH0 = uniform gain floor (no per-band variation = no burbling).
       - Final: G = GH1^p * GH0^(1-p) per band (OMLSA formula).
       Two-level speech probability (OMLSA-style):
       - Pframe: global frame-level indicator (is anyone speaking?)
       - p_local[i]: per-band indicator (is THIS band speech?)
       - Combined: p[i] = p_local[i] * Pframe
       This keeps noise-only bands suppressed even during speech frames. */
    {
        /* Frame-level speech probability from mean a-priori SNR */
        float mean_prior = 0.0f;
        for (int i = 0; i < HD_NB_BANDS; i++)
            mean_prior += st->prior[i];
        mean_prior /= HD_NB_BANDS;

        float mean_prior_dB = 10.0f * std::log10(std::max(mean_prior, 1e-10f));

        /* Soft ramp: -10 dB → Pframe=0 (noise), -5 dB → Pframe=1 (speech).
           Matches OMLSA's xi_fl_dB=-10, xi_fu_dB=-5 thresholds. */
        const float lo_frame = -10.0f, hi_frame = -5.0f;
        float Pf;
        if (mean_prior_dB <= lo_frame)
            Pf = 0.005f;       /* P_min — never fully zero */
        else if (mean_prior_dB >= hi_frame)
            Pf = 1.0f;
        else
            Pf = 0.005f + (mean_prior_dB - lo_frame) / (hi_frame - lo_frame) * 0.995f;

        /* Light smoothing on Pframe to avoid frame-to-frame chatter.
           Fast attack (speech detected quickly), slower release. */
        if (Pf > st->Pframe)
            st->Pframe = 0.3f * st->Pframe + 0.7f * Pf;   /* fast attack */
        else
            st->Pframe = 0.7f * st->Pframe + 0.3f * Pf;   /* slower release */

        /* Per-band speech probability from per-band a-priori SNR.
           Wider ramp than frame-level: band needs stronger local evidence. */
        const float lo_band = -10.0f, hi_band = 0.0f;
        float p_local[HD_NB_BANDS];
        for (int i = 0; i < HD_NB_BANDS; i++) {
            float prior_dB = 10.0f * std::log10(std::max(st->prior[i], 1e-10f));
            if (prior_dB <= lo_band)
                p_local[i] = 0.005f;
            else if (prior_dB >= hi_band)
                p_local[i] = 1.0f;
            else
                p_local[i] = 0.005f + (prior_dB - lo_band) / (hi_band - lo_band) * 0.995f;
        }

        /* GH0: uniform gain floor (same for all bands = no burbling) */
        float GH0 = mix_floor;

        /* Interpolate: G = GH1^p * GH0^(1-p)  (OMLSA power interpolation)
           p[i] = p_local[i] * Pframe: both local and global must agree */
        for (int i = 0; i < HD_NB_BANDS; i++) {
            float p = p_local[i] * st->Pframe;
            float GH1 = st->gain[i];
            gain[i] = std::pow(GH1, p) * std::pow(GH0, 1.0f - p);
        }
    }

    /* 9. Interpolate band gains to full spectrum and apply */
    float gf[HD_FREQ_SIZE];
    interp_band_gain(gf, gain);

    if (residual != nullptr) {
        /* nsp patch: dual output. Removed part (1-gf)*X with its own overlap-add memory;
           same arithmetic as residual_mode 1, so it is bit-identical to that output. */
        kiss_fft_cpx R[HD_FREQ_SIZE];
        for (int i = 0; i < HD_FREQ_SIZE; i++) {
            float r = 1.0f - gf[i];
            R[i].r = X[i].r * r;
            R[i].i = X[i].i * r;
        }
        overlap_add_synthesis(st->fft_inv, residual, R, st->residual_synthesis_mem);
        for (int i = 0; i < HD_FREQ_SIZE; i++) {
            X[i].r *= gf[i];
            X[i].i *= gf[i];
        }
    } else if (st->residual_mode) {
        for (int i = 0; i < HD_FREQ_SIZE; i++) {
            float r = 1.0f - gf[i];
            X[i].r *= r;
            X[i].i *= r;
        }
    } else {
        for (int i = 0; i < HD_FREQ_SIZE; i++) {
            X[i].r *= gf[i];
            X[i].i *= gf[i];
        }
    }

    /* 10. IFFT + overlap-add synthesis */
    overlap_add_synthesis(st->fft_inv, out, X, st->synthesis_mem);
}

void hybrid_denoise_process_frame(HybridDenoiseState *st, float *out, const float *in) {
    hd_process_frame(st, out, nullptr, in);
}

/* nsp patch */
void hybrid_denoise_process_frame_dual(HybridDenoiseState *st, float *out, float *residual, const float *in) {
    hd_process_frame(st, out, residual, in);
}
