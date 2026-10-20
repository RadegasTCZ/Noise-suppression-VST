// Compiles the frozen SNS v2.4.0 HybridDenoise.cpp with renamed exported symbols.
// Everything else in it is static (internal linkage), so it cannot clash with the
// patched copy in Source/Engines/sns.
#include "HybridDenoiseRef.h"

#define HybridDenoiseState           RefHybridDenoiseState
#define hybrid_denoise_init          ref_hybrid_denoise_init
#define hybrid_denoise_cleanup       ref_hybrid_denoise_cleanup
#define hybrid_denoise_process_frame ref_hybrid_denoise_process_frame
#include "HybridDenoise_v240.inc"    // its #include "HybridDenoise.h" is skipped (#pragma once, already included)
