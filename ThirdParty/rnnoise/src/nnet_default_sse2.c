/* Copyright (c) 2026 RadegasTCZ. Local file of Noise Suppression (not upstream); BSD-3-Clause, see ../COPYING. */
/* Noise Suppression local file (not upstream). Compiles upstream nnet_default.c with the SSE2
   setting of rnn_sse2.h. nnet_default.c itself is in the project with compile="0". */
#include "rnn_sse2.h"
#include "nnet_default.c"

#if !defined(__SSE2__) || defined(NO_OPTIMIZATIONS) || defined(__AVX__)
#error "nnet_default.c must build vec.h's SSE2 path (vec_avx.h without AVX)"
#endif
