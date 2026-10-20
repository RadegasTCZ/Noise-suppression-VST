// The frozen reference compiled with the ORIGINAL v2.4.0 window line (before the nsp
// window fix), exported as v240_* so tests can measure what the fix changes.
// All other symbols in the .inc are static, so this second copy cannot clash at link time.
#include "HybridDenoiseRef.h"   // RefHybridDenoiseState + v240_* declarations

#define HD_REF_V240_ORIGINAL_WINDOW
#define HybridDenoiseState           RefHybridDenoiseState
#define hybrid_denoise_init          v240_hybrid_denoise_init
#define hybrid_denoise_cleanup       v240_hybrid_denoise_cleanup
#define hybrid_denoise_process_frame v240_hybrid_denoise_process_frame
#include "HybridDenoise_v240.inc"    // its #include "HybridDenoise.h" is skipped (#pragma once)
