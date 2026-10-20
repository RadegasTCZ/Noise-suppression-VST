/* Test-only reference: upstream rnnoise_data.c (Standard model 0a8755f8, commit 70f1d25) with the
   weights compiled in, exactly as upstream's default ./configure build (DISABLE_DEBUG_FLOAT, no
   USE_WEIGHTS_FILE). Its two external symbols are renamed so they do not clash with the plugin's
   vendored RNNoise (ThirdParty/rnnoise, built with USE_WEIGHTS_FILE). The 78 MB source is NOT in the
   repository: it is found on the header search path (a local upstream checkout, see RnnoiseRef.h).
   Without it this file compiles to a stub and the reference tests are skipped. */
#if !defined (NSP_NO_RNNOISE_REFERENCE) && defined (__has_include)
 #if __has_include ("rnnoise_data.c")
  #define NSP_REF_HAVE_STANDARD 1
 #endif
#endif

#ifdef NSP_REF_HAVE_STANDARD

#undef USE_WEIGHTS_FILE
#define DISABLE_DEBUG_FLOAT 1
#define init_rnnoise nsp_ref_init_rnnoise_standard
#define rnnoise_arrays nsp_ref_arrays_standard
#include "rnnoise_data.c"
#undef init_rnnoise
#undef rnnoise_arrays

#include "RnnoiseRef.h"

/* Upstream's default-build rnnoise_init(st, NULL) is: memset(st, 0); init_rnnoise(&st->model,
   rnnoise_arrays); st->arch = rnn_select_arch() (0 without RNN_ENABLE_X86_RTCD). The vendored USE_WEIGHTS_FILE
   build's rnnoise_init(st, NULL) does the memset and the arch and skips the model; the model - the
   FIRST member of struct DenoiseState (denoise.c), an RNNoise struct - is then filled here from the
   compiled-in arrays. */
int nsp_ref_rnnoise_init_standard (DenoiseState* st)
{
    if (rnnoise_init (st, NULL) != 0)
        return -1;
    return nsp_ref_init_rnnoise_standard ((RNNoise*) st, nsp_ref_arrays_standard) == 0 ? 0 : -1;
}

int nsp_ref_rnnoise_standard_available (void) { return 1; }

#else

#include "RnnoiseRef.h"

int nsp_ref_rnnoise_init_standard (DenoiseState* st) { (void) st; return -1; }
int nsp_ref_rnnoise_standard_available (void) { return 0; }

#endif
