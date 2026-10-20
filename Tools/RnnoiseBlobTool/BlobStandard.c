/* RnnoiseBlobTool: upstream rnnoise_data.c (Standard model, 0a8755f8) with the weight arrays
   compiled in. DUMP_BINARY_WEIGHTS drops init_rnnoise(); rnnoise_arrays is renamed so the
   Light model can be linked into the same tool. */
#undef USE_WEIGHTS_FILE
#define DISABLE_DEBUG_FLOAT 1   /* as upstream's default ./configure (no --enable-dnn-debug-float) */
#define DUMP_BINARY_WEIGHTS
#define rnnoise_arrays nsp_blob_arrays_standard
#include "rnnoise_data.c"
