/* Test-only reference: upstream rnnoise_data_little.c (Light model 0a8755f8, commit 70f1d25) with the
   weights compiled in. See RnnoiseRefStandard.c. */
#if !defined (NSP_NO_RNNOISE_REFERENCE) && defined (__has_include)
 #if __has_include ("rnnoise_data_little.c")
  #define NSP_REF_HAVE_LIGHT 1
 #endif
#endif

#ifdef NSP_REF_HAVE_LIGHT

#undef USE_WEIGHTS_FILE
#define DISABLE_DEBUG_FLOAT 1
#define init_rnnoise nsp_ref_init_rnnoise_light
#define rnnoise_arrays nsp_ref_arrays_light
#include "rnnoise_data_little.c"
#undef init_rnnoise
#undef rnnoise_arrays

#include "RnnoiseRef.h"

int nsp_ref_rnnoise_init_light (DenoiseState* st)
{
    if (rnnoise_init (st, NULL) != 0)
        return -1;
    return nsp_ref_init_rnnoise_light ((RNNoise*) st, nsp_ref_arrays_light) == 0 ? 0 : -1;
}

int nsp_ref_rnnoise_light_available (void) { return 1; }

#else

#include "RnnoiseRef.h"

int nsp_ref_rnnoise_init_light (DenoiseState* st) { (void) st; return -1; }
int nsp_ref_rnnoise_light_available (void) { return 0; }

#endif
