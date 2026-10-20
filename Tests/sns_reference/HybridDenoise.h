#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "kiss_fft.h"
#include "kiss_fftr.h"

#ifdef __cplusplus
}
#endif

#define HD_FRAME_SIZE 480
#define HD_WINDOW_SIZE (2*HD_FRAME_SIZE)
#define HD_FREQ_SIZE (HD_FRAME_SIZE + 1)
#define HD_NB_BANDS 40
#define HD_MIN_TRACK_WINDOW 96  /* frames before min-tracking resets (~1 second) */

struct HybridDenoiseState {
    float analysis_mem[HD_FRAME_SIZE];
    float synthesis_mem[HD_FRAME_SIZE];

    /* Per-band noise estimation (Martin minimum-tracking) */
    float S[HD_NB_BANDS];       /* smoothed power spectrum */
    float Smin[HD_NB_BANDS];    /* running minimum */
    float Stmp[HD_NB_BANDS];    /* temp minimum for sliding window */
    float noise[HD_NB_BANDS];   /* noise estimate per band */

    /* Per-band gain / SNR state */
    float ps[HD_NB_BANDS];      /* current band power spectrum */
    float prior[HD_NB_BANDS];   /* a-priori SNR per band */
    float post[HD_NB_BANDS];    /* a-posteriori SNR per band */
    float gain[HD_NB_BANDS];    /* Ephraim-Malah gain per band */
    float lastg[HD_NB_BANDS];   /* previous clean EM gain (decision-directed) */
    float last_ps[HD_NB_BANDS]; /* previous frame power spectrum */
    float Pframe;               /* frame-level speech probability (OMLSA-style) */

    /* Adaptation counters */
    int nb_adapt;
    int min_count;

    /* FFT configs (standard kissfft real FFT) */
    kiss_fftr_cfg fft_fwd;
    kiss_fftr_cfg fft_inv;

    /* Display */
    float display_noise_db;  /* approximate dBFS for GUI */

    /* Parameters (set before processing) */
    float suppress_db;   /* suppression floor in dB (e.g. -20) */
    int residual_mode;   /* 1 = output removed noise only (diagnostic) */
};

void hybrid_denoise_init(HybridDenoiseState *st);
void hybrid_denoise_cleanup(HybridDenoiseState *st);
void hybrid_denoise_process_frame(HybridDenoiseState *st, float *out, const float *in);
