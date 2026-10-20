/* Copyright (c) 2026 RadegasTCZ. Local file of Noise Suppression (not upstream); BSD-3-Clause, see ../COPYING. */
/* Noise Suppression local file (not upstream). Compiles upstream denoise.c with USE_WEIGHTS_FILE
   (weights come from a blob via rnnoise_model_from_buffer; no compiled-in model). The define cannot be
   project-wide: JUCE 9's bundled Opus (juce_audio_formats) also tests USE_WEIGHTS_FILE and stops
   compiling. denoise.c itself is in the project with compile="0". */
#define USE_WEIGHTS_FILE 1
#include "denoise.c"
