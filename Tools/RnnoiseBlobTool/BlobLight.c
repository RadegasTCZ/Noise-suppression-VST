/* RnnoiseBlobTool: upstream rnnoise_data_little.c (Light model, 0a8755f8), see BlobStandard.c. */
#undef USE_WEIGHTS_FILE
#define DISABLE_DEBUG_FLOAT 1   /* as upstream's default ./configure (no --enable-dnn-debug-float) */
#define DUMP_BINARY_WEIGHTS
#define rnnoise_arrays nsp_blob_arrays_light
#include "rnnoise_data_little.c"
