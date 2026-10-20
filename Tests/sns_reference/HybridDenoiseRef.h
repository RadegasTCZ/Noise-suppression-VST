#pragma once

// Frozen SNS v2.4.0 HybridDenoise reference (copies in this folder: HybridDenoise.h is
// byte-identical; HybridDenoise_v240.inc is byte-identical except the documented one-line
// window fix), compiled with every exported name renamed so tests can compare the
// Source/Engines/sns copy against it bit for bit.
//   ref_*  : HybridDenoiseRef.cpp     - v2.4.0 + window fix (guards the refactor)
//   v240_* : HybridDenoiseRefV240.cpp - original v2.4.0 window (measures the fix)

#if ! defined (FLOATING_POINT) && ! defined (FIXED_POINT)
 #define FLOATING_POINT   // SpeexDSP kiss_fft.h -> arch.h needs it (SNS v2 set it project-wide)
#endif

#define HybridDenoiseState           RefHybridDenoiseState
#define hybrid_denoise_init          ref_hybrid_denoise_init
#define hybrid_denoise_cleanup       ref_hybrid_denoise_cleanup
#define hybrid_denoise_process_frame ref_hybrid_denoise_process_frame
#include "HybridDenoise.h"           // resolves to Tests/sns_reference/HybridDenoise.h (same folder first)
#undef HybridDenoiseState
#undef hybrid_denoise_init
#undef hybrid_denoise_cleanup
#undef hybrid_denoise_process_frame

// Same frozen source compiled a second time with HD_REF_V240_ORIGINAL_WINDOW; same struct layout.
void v240_hybrid_denoise_init (RefHybridDenoiseState* st);
void v240_hybrid_denoise_cleanup (RefHybridDenoiseState* st);
void v240_hybrid_denoise_process_frame (RefHybridDenoiseState* st, float* out, const float* in);
