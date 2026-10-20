#pragma once
#include "rnnoise.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Test-only references (RnnoiseRefStandard.c / RnnoiseRefLight.c): initialise `st` (rnnoise_get_size()
    bytes) exactly as upstream's default build does with the weights compiled in from upstream's
    src/rnnoise_data.c / rnnoise_data_little.c. 0 on success, -1 on failure.

    Those upstream sources (108 MB) are NOT in the repository. The test project finds them through its
    last header search path, ../_upstream/rnnoise/src next to the repository folder. When a file is not
    found the reference is compiled as a stub: nsp_ref_rnnoise_*_available() returns 0, init returns -1,
    and the tests that need it are skipped. Defining NSP_NO_RNNOISE_REFERENCE forces the stubs. */
int nsp_ref_rnnoise_init_standard (DenoiseState* st);
int nsp_ref_rnnoise_init_light (DenoiseState* st);
int nsp_ref_rnnoise_standard_available (void);
int nsp_ref_rnnoise_light_available (void);

#ifdef __cplusplus
}
#endif
