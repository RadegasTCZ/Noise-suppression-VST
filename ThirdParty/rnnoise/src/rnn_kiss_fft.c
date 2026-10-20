/* Copyright (c) 2026 RadegasTCZ. Local file of Noise Suppression (not upstream); BSD-3-Clause, see ../COPYING. */
/* Noise Suppression local file (not upstream). Compiles upstream kiss_fft.c (RNNoise's Opus/CELT
   variant, symbols rnn_*) under a unique object-file name: MSBuild names objects after the source
   file, and ThirdParty/speexdsp/kiss_fft.c would otherwise overwrite kiss_fft.obj (and vice versa).
   kiss_fft.c itself is in the project with compile="0". */
#include "kiss_fft.c"
