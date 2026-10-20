/* Copyright (c) 2026 RadegasTCZ. Local file of Noise Suppression (not upstream); BSD-3-Clause, see ../COPYING. */
/* Noise Suppression local file (not upstream). The RNNoise SIMD setting: SSE2, the
   x64 baseline, chosen at compile time through upstream's own MSVC route - x86/x86_arch_macros.h turns
   OPUS_X86_MAY_HAVE_SSE / OPUS_X86_MAY_HAVE_SSE2 into __SSE__ / __SSE2__, and vec.h then includes
   vec_avx.h (its SSE2 code path). No AVX / AVX2 / SSE4.1, no runtime CPU dispatch
   (RNN_ENABLE_X86_RTCD stays undefined, as in upstream's default ./configure).
   Included first by nnet_sse2.c and nnet_default_sse2.c, the wrappers of the only two RNNoise
   sources that include vec.h, so both see the same setting. Not a project-wide define: JUCE 9's
   bundled Opus (juce_audio_formats) defines and tests the same OPUS_X86_* macros itself. */
#ifndef NSP_RNN_SSE2_H
#define NSP_RNN_SSE2_H

#if !defined(_M_X64) && !defined(__x86_64__)
#error "RNNoise SSE2 build: x64 only (SSE2 is the x64 baseline)"
#endif

#ifdef RNN_ENABLE_X86_RTCD
#error "RNNoise: no runtime CPU dispatch in this build (see rnn_sse2.h)"
#endif

#define OPUS_X86_MAY_HAVE_SSE 1
#define OPUS_X86_MAY_HAVE_SSE2 1

#endif /* NSP_RNN_SSE2_H */
