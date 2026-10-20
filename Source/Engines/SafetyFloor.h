#ifndef NSP_SAFETY_FLOOR_H
#define NSP_SAFETY_FLOOR_H

/* Safety floor: a per-band energy floor of (room noise - 20 dB) that the RNNoise and DeepFilterNet engines fill
   back in where they would output digital silence, so the output never collapses to -130 dBFS between words.
   Shared by ThirdParty/rnnoise/src/denoise.c (C99) and Source/Engines/dfn/DfnDenoiser.cpp (C++): keep it plain C,
   allocation-free, fixed-size and trivially copyable (RNNoise resets its state by memcpy of a pristine copy), and
   limited to <math.h> / <string.h>.

   Room-noise tracker, per band, once per 10 ms hop (minimum statistics after Martin 2001, simplified):
   1. y = mean band energy of the last NSP_FLOOR_AVG_HOPS hops (40 ms). This takes the edge off single-hop
      fluctuations, which reach far lower in narrow (low-frequency) bands than in wide ones.
   2. The tracked noise is the minimum of y over the last 1.25..1.5 s: NSP_FLOOR_SUBWINS sub-windows of
      NSP_FLOOR_SUBWIN_HOPS hops, one running minimum per sub-window in a ring whose newest slot is still filling.
      Speech and keystrokes do not lift it as long as each band gets a quiet 50 ms at least every ~1.25 s; a brief
      dip in the input (e.g. 150 ms after a click) is forgotten when its sub-window leaves the ring, at most 1.5 s
      later.
   3. It is further limited to this hop's own energy, so a lasting drop in the noise is followed at once (within
      one hop) and the floor never exceeds (this hop - 20 dB + bias).
   4. floor = tracked noise x NSP_FLOOR_BIAS x NSP_FLOOR_BELOW, all bands scaled down together so that the total
      stays <= capE. NSP_FLOOR_BIAS is one fixed compensation for the minimum sitting below the mean noise energy
      (see the constant).
   A band whose energy is 0 (digital silence; also NaN) gets floor 0 and is restarted, so the zeros are not
   remembered and the next non-zero hop starts the band afresh. */

#include <math.h>
#include <string.h>

#define NSP_FLOOR_MAX_BANDS 32
#define NSP_FLOOR_BELOW 0.01f            /* -20 dB in energy */
#define NSP_FLOOR_AVG_HOPS 4             /* 40 ms pre-average before the minimum */
#define NSP_FLOOR_SUBWIN_HOPS 25         /* 250 ms per sub-window at 100 hops/s */
#define NSP_FLOOR_SUBWINS 6              /* ring of 6 slots, the newest filling: window 1.25..1.5 s */
#define NSP_FLOOR_EMPTY 3.0e38f          /* an empty slot (larger than any band energy) */

/* Bias compensation, +1.1 dB (10^0.11). The tracked minimum reads below the mean noise energy, by an amount that
   depends on how much a band's energy fluctuates: narrow low bands dip deeper, so pink noise (energy in the low
   bands) reads lower than white. Measured with this rule and NSP_FLOOR_BIAS = 1 (3 s of stationary noise, last
   1 s, output vs input level): DFN Denoiser -80 dBFS white 21.66 / pink 24.62 dB, DfnEngine Normal white 21.50 /
   pink 23.01 dB, RNNoise -70 dBFS white 22.62..22.70 / pink 24.68 dB. +1.1 dB moves that 21.5..24.7 dB spread
   to ~20.4..23.6 dB (centred ~22 dB), inside the tests' 19.5..24.5 dB window. (Without the 40 ms pre-average the
   spread was 4.9 dB and no single constant fitted.) */
#define NSP_FLOOR_BIAS 1.28825f

typedef struct NspFloor
{
    float recent[NSP_FLOOR_AVG_HOPS][NSP_FLOOR_MAX_BANDS];  /* last hops' band energies (0 = none) */
    float slotMin[NSP_FLOOR_SUBWINS][NSP_FLOOR_MAX_BANDS];  /* per sub-window running minimum of the average */
    int recentCount[NSP_FLOOR_MAX_BANDS];                   /* valid entries in recent[][b], <= NSP_FLOOR_AVG_HOPS */
    int recentPos;                                          /* where this hop's energy goes in recent */
    int slot;                                               /* the sub-window being filled */
    int hopsInSlot;                                         /* hops already in that sub-window */
    int started;                                            /* 0 until the first update (all-zero = reset) */
} NspFloor;

static inline void nsp_floor_reset (NspFloor* f)
{
    memset (f, 0, sizeof (NspFloor));
}

/* Forget everything about band b (start-up, digital silence). */
static inline void nsp_floor_restart_band (NspFloor* f, int b)
{
    int i;
    for (i = 0; i < NSP_FLOOR_AVG_HOPS; ++i)
        f->recent[i][b] = 0.0f;
    for (i = 0; i < NSP_FLOOR_SUBWINS; ++i)
        f->slotMin[i][b] = NSP_FLOOR_EMPTY;
    f->recentCount[b] = 0;
}

/* Called once per 10 ms hop with the noisy band energies; writes the floor energies (see the comment at the top). */
static inline void nsp_floor_update (NspFloor* f, const float* noisyE, int nb, float capE, float* floorE)
{
    int b, i;
    float total = 0.0f;

    if (! f->started)
    {
        for (b = 0; b < NSP_FLOOR_MAX_BANDS; ++b)
            nsp_floor_restart_band (f, b);
        f->recentPos = 0;
        f->slot = 0;
        f->hopsInSlot = 0;
        f->started = 1;
    }
    else if (f->hopsInSlot >= NSP_FLOOR_SUBWIN_HOPS)
    {
        /* the newest sub-window is full: start the next one, dropping the oldest */
        f->slot = (f->slot + 1) % NSP_FLOOR_SUBWINS;
        f->hopsInSlot = 0;
        for (b = 0; b < NSP_FLOOR_MAX_BANDS; ++b)
            f->slotMin[f->slot][b] = NSP_FLOOR_EMPTY;
    }
    ++f->hopsInSlot;

    for (b = 0; b < nb; ++b)
    {
        const float x = noisyE[b];
        float sum = 0.0f, n;

        if (! (x > 0.0f))
        {
            nsp_floor_restart_band (f, b);
            floorE[b] = 0.0f;
            continue;
        }

        f->recent[f->recentPos][b] = x;
        if (f->recentCount[b] < NSP_FLOOR_AVG_HOPS)
            ++f->recentCount[b];
        for (i = 0; i < NSP_FLOOR_AVG_HOPS; ++i)
            sum += f->recent[i][b];   /* entries older than a restart are 0 */
        sum /= (float) f->recentCount[b];

        if (sum < f->slotMin[f->slot][b])
            f->slotMin[f->slot][b] = sum;
        n = x;
        for (i = 0; i < NSP_FLOOR_SUBWINS; ++i)
            n = fminf (n, f->slotMin[i][b]);

        floorE[b] = n * (NSP_FLOOR_BIAS * NSP_FLOOR_BELOW);
        total += floorE[b];
    }
    f->recentPos = (f->recentPos + 1) % NSP_FLOOR_AVG_HOPS;

    if (total > capE)
    {
        const float scale = capE / total;
        for (b = 0; b < nb; ++b)
            floorE[b] *= scale;
    }
}

/* Amplitude gain to apply to the noisy band to bring a too-quiet output up to the floor; 0 when no fill is needed. */
static inline float nsp_floor_fill_gain (float noisyE, float outE, float floorE)
{
    if (outE < floorE && noisyE > 0.0f)
        return sqrtf (floorE / noisyE);
    return 0.0f;
}

#endif
