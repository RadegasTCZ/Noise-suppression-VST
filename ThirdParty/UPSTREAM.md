# Third-party code

Every third-party drop in this folder, where it came from, and what we changed.

## SpeexDSP — `ThirdParty/speexdsp/`

- Upstream: https://gitlab.xiph.org/xiph/speexdsp
- Version: 1.2.1, commit `7a15878` (commit id as recorded when the copy was taken; the local reference copy has no git metadata, its `configure.ac` reads `AC_INIT([speexdsp],[1.2.1])`)
- License: BSD-3-Clause (`COPYING` and the header of each file; Xiph.Org Foundation, Jean-Marc Valin and others, see `AUTHORS`)
- Local reference copy used for comparison: a local copy of upstream at `../_upstream/speexdsp-main` relative to the repository (not part of it)
  (`libspeexdsp/` → this folder, `include/speex/` → `speex/`, `win32/config.h` → `config.h`)

### What is kept

Only what the preprocessor (denoise) and the resampler need, built with `FLOATING_POINT` + `USE_KISS_FFT`:

- Compiled: `preprocess.c`, `resample.c`, `fftwrap.c`, `kiss_fft.c`, `kiss_fftr.c`, `filterbank.c`, `mdf.c`
  (`mdf.c` is kept only because `preprocess.c` links `speex_echo_get_residual`; the echo canceller is never used)
- Headers: `_kiss_fft_guts.h`, `arch.h`, `fftwrap.h`, `filterbank.h`, `kiss_fft.h`, `kiss_fftr.h`, `math_approx.h`,
  `pseudofloat.h`, `os_support.h`, `resample_sse.h`, `resample_neon.h`, `bfin.h`, `fixed_arm4.h`, `fixed_arm5e.h`, `fixed_bfin.h`,
  `fixed_debug.h`, `fixed_generic.h` (the `fixed_*`/`bfin` headers are referenced by `arch.h`'s fixed-point branches),
  `speex/speex_preprocess.h`, `speex/speex_echo.h`, `speex/speex_resampler.h`, `speex/speexdsp_types.h`
- `COPYING`, `AUTHORS`: verbatim from the upstream root.
- `config.h`: identical to upstream `win32/config.h` apart from line endings (upstream CRLF; stored with LF since the repository's `.gitattributes` normalisation, an EOL-only change). Not used: it is only included when `HAVE_CONFIG_H` is defined, which this project never does.

Removed (not needed by the above): `buffer.c`, `jitter.c`, `scal.c`, `smallft.c/.h`, `test*.c`,
`misc_bfin.h`, `vorbis_psy.h`, `Makefile.am`, `speex/Makefile.am`, `speex/config.h`,
`speex/speex_buffer.h`, `speex/speex_jitter.h`, `speex/speexdsp_config_types.h.in`.

### Local patches

1. `#include "speex_config_win.h"` added at the top of `preprocess.c`, `resample.c`, `fftwrap.c`, `kiss_fftr.c`,
   `filterbank.c`, `mdf.c`, and `kiss_fft.c` (where it replaces a blank line).
2. `preprocess.c`, `resample.c` and `mdf.c` additionally contain, right after that include:
   `#define FLOATING_POINT`, `#define USE_KISS_FFT`, `#define EXPORT`, `#ifdef constant` / `#undef constant` / `#endif`.
3. `speex_config_win.h` is local (not upstream; BSD-3-Clause like the rest of this folder): defines `FLOATING_POINT`, `USE_KISS_FFT`, `EXPORT`; undefines `constant`.
4. `speex_preprocess_state_reset` (Noise Suppression 2.0):
   - `preprocess.c`: new `EXPORT void speex_preprocess_state_reset(SpeexPreprocessState *st)`, placed directly after `speex_preprocess_state_destroy`. It restores every adaptive field to the value `speex_preprocess_state_init` leaves:
     - arrays that `init` gets zero-filled from `speex_alloc` are zeroed again;
     - `noise`, `reverb_estimate`, `old_ps`, `gain`, `post`, `prior`, `update_prob`, `inbuf`, `outbuf`, the AGC state, `speech_prob`, `was_speech`, `nb_adapt` and `min_count` are set as in `init`.

     Parameters, window, filterbank and FFT lookup are kept. It does not allocate. Used by `ClassicSpeexEngine::reset()` on the audio thread; pinned by `ClassicEngine` test "reset() makes the engine behave exactly like a freshly prepared one".
   - `speex/speex_preprocess.h`: declaration added after `speex_preprocess_state_destroy`.

All other kept files are identical to upstream apart from line endings.
(v1.1.2 also had a stray `#define SPEEX_PREPROCESS_GET_FRAME_SIZE 3` in `speex/speex_preprocess.h`; it was unused,
collided with `SPEEX_PREPROCESS_GET_AGC`, and was removed in 2.0.0.)

The project also defines `FLOATING_POINT` and `USE_KISS_FFT` globally, so MSVC reports C4005 (macro redefinition)
for patches 1–2; this is expected and harmless.

### How to re-check

From the parent folder of the repository (the one holding `Noise-suppression-VST/` and the reference copies in `_upstream/`), in Git Bash:

    U=_upstream/speexdsp-main; L=Noise-suppression-VST/ThirdParty/speexdsp
    for f in $(cd $L && ls *.c *.h); do [ -f $U/libspeexdsp/$f ] && { diff -q --strip-trailing-cr $U/libspeexdsp/$f $L/$f >/dev/null || echo "PATCHED $f"; }; done
    for f in $(cd $L/speex && ls); do diff -q --strip-trailing-cr $U/include/speex/$f $L/speex/$f >/dev/null || echo "PATCHED speex/$f"; done

Expected: exactly the seven `.c` files listed in patch 1 (`preprocess.c` additionally carries patch 4), plus `PATCHED speex/speex_preprocess.h` (patch 4 declaration).

## SNS v2.4.0 HybridDenoise (`Source/Engines/sns/`)

- **Origin:** `Source/HybridDenoise.h` and `HybridDenoise.cpp` of the author's separate Spectral Noise Suppression v2 project (v2.4.0, 2026-03-21, not public).
- **Third-party code:** `compute_band_energy()` and `interp_band_gain()` are adapted from RNNoise `src/denoise.c` (Copyright (c) 2017 Mozilla, 2018 Gregor Richards, 2024 Jean-Marc Valin; BSD-3-Clause, see `ThirdParty/rnnoise/COPYING`). The noise estimation (`S` / `Smin` / `Stmp` minimum tracking and the `min_range` schedule) is adapted from SpeexDSP `libspeexdsp/preprocess.c` (Copyright (C) 2003 Epic Games (written by Jean-Marc Valin), 2004-2006 Epic Games; BSD-3-Clause, licence text in the header of `ThirdParty/speexdsp/preprocess.c`). The header comment of `HybridDenoise.cpp` and of the frozen reference `.inc` says so.
- **Frozen reference:** `Tests/sns_reference/`.
  - `HybridDenoise.h` is byte-identical to v2.4.0.
  - `HybridDenoise_v240.inc` is v2.4.0's `.cpp`, byte-identical except the window line in `hd_init_window()` and the comments added at the top of the file. That line carries the same fix as patch 4, selectable back to the original with `HD_REF_V240_ORIGINAL_WINDOW`; this is documented at the top of the file.
  - It is compiled twice with renamed symbols:
    - `HybridDenoiseRef.cpp` (`ref_*`, fixed window) guards the refactor bit-exactly.
    - `HybridDenoiseRefV240.cpp` (`v240_*`, original window) measures the fix.
- **FFT:** built against SpeexDSP's bundled kissfft (`ThirdParty/speexdsp/kiss_fft.c`, `kiss_fftr.c`), not SNS v2's own `ThirdParty/kissfft` copy. This avoids duplicate symbols with the same API. Configs come from `speex_alloc` (`calloc`), so `free()` in `hybrid_denoise_cleanup` is correct.
- **Local patches:**
  1. `HybridDenoise.h`: defines `FLOATING_POINT` when neither `FLOATING_POINT` nor `FIXED_POINT` is defined. SpeexDSP's `arch.h` requires it; SNS v2 set it project-wide.
  2. `HybridDenoise.h`: `float residual_synthesis_mem[HD_FRAME_SIZE];` appended at the end of `HybridDenoiseState`.
  3. `HybridDenoise.cpp`, behaviour-neutral (bit-identical to v2.4.0 before patch 4; commit "feat(engines): add SnsEngine …"):
     - The body of `hybrid_denoise_process_frame` moved unchanged into static `hd_process_frame(st, out, residual, in)`.
     - Step-10 overlap-add moved into static `overlap_add_synthesis()`.
     - New `hybrid_denoise_process_frame_dual(st, out, residual, in)`: one pipeline run gives `out = gf·X` and `residual = (1−gf)·X`, each with its own overlap-add memory (`residual_mode` ignored).
  4. `HybridDenoise.cpp`, `hd_init_window()`, **window fix** (commit "fix(sns): …"): `std::sin((float)M_PI * n / HD_WINDOW_SIZE)` → `std::sin((float)M_PI * (n + 0.5f) / HD_WINDOW_SIZE)`.
     - Reason: v2.4.0's window `hw[n] = sin(π/2·sin²(πn/960))`, mirrored at `959−n` by `apply_window()`, is not power-complementary. `hw[k]² + hw[479−k]²` ranges over 0.99486…1, a 0.514 % (0 to −0.045 dB) periodic ripple. So `out + residual` did not reconstruct the input, with an error up to peak − 45.8 dB.
     - With `n + 0.5` the sum is exactly 1, and 2.4·10⁻⁷ in float32. `out + residual` = input delayed 480 within −100 dBFS (test `SnsEngine` / "perfect reconstruction").
     - Output level change vs v2.4.0 on a speech-like signal: ≤ 0.05 dB RMS (test; NumPy port +0.017 dB).

## RNNoise — `ThirdParty/rnnoise/`

- Upstream: https://gitlab.xiph.org/xiph/rnnoise (mirror https://github.com/xiph/rnnoise), commit `70f1d25` ("Remove outdated TRAINING-README").
- Model: `0a8755f8e2d834eff6a54714ecc7d75f9932e845df35f8b59bc52a7cfe6e8b37` (upstream `model_version`; the SHA-256 of `rnnoise_data-0a8755f8….tar.gz`).
  - Standard = `src/rnnoise_data.c` (SHA-256 `d6021b7697677c4d2274c912975e143765552b0e6f25500aa660fdb4a9849be5`).
  - Light = `src/rnnoise_data_little.c` (SHA-256 `eea1aa6fd8726d161876508a6f7d79022bf6ec82508660b4c1ac27c4f9b76c04`).
- License: BSD-3-Clause (`COPYING`; Jean-Marc Valin, Mozilla, Amazon, Gregor Richards and others, see `AUTHORS`).
- Local reference copy: a checkout of upstream at `../_upstream/rnnoise` relative to the repository, with the model extracted into `src/` (`src/` → `src/`, `include/rnnoise.h` → `include/`). The test project and `Tools/RnnoiseBlobTool/build.bat` read the model sources from there (optional for the tests).

### What is kept

Inference only, **SSE2** (x64 baseline, chosen at compile time; no AVX / AVX2 / SSE4.1, no runtime CPU dispatch), built with `USE_WEIGHTS_FILE` (no compiled-in model):

- Compiled: `src/rnn.c`, `pitch.c`, `celt_lpc.c`, `rnnoise_tables.c`, `parse_lpcnet_weights.c`; `denoise.c`, `kiss_fft.c`, `nnet.c` and `nnet_default.c` through the local wrappers below (in the projects with `compile="0"`); plus the local `src/rnnoise_model_init.c`.
- Headers: `src/_kiss_fft_guts.h`, `arch.h`, `celt_lpc.h`, `common.h`, `cpu_support.h`, `denoise.h`, `kiss_fft.h`, `nnet.h`, `nnet_arch.h`, `opus_types.h`, `pitch.h`, `rnn.h`, `rnnoise_data.h`, `vec.h`, `vec_avx.h` (the SSE2 path: `vec.h` includes it when `__SSE2__` is defined), `x86/x86_arch_macros.h` (included unconditionally by `vec.h`; with MSVC it maps `OPUS_X86_MAY_HAVE_SSE` / `_SSE2` to `__SSE__` / `__SSE2__`), `x86/x86cpu.h` (included by `vec_avx.h`), `include/rnnoise.h`.
- Not copied: `rnnoise_data.c` / `rnnoise_data_little.c` (78 / 30 MB of weight arrays), `vec_neon.h`, `x86/*.c` (the SSE4.1 / AVX2 runtime-dispatch variants and CPU detection) and `x86/dnn_x86.h` (only used with `RNN_ENABLE_X86_RTCD`), training code, scripts, examples.
- This is the code path of upstream's default `./configure` build on x86-64 (GCC/Clang always define `__SSE2__` there; `--enable-x86-rtcd` is off by default). The SSE2 path computes the int8 layers with unsigned-biased inputs (`USE_SU_BIAS` in `vec_avx.h`, reading the `*_subias` arrays, which the blobs contain), so its output differs slightly from a C-only build of the same model; the tests compare like with like (the reference build runs the same SSE2 inference code).
- Only `include/` is on the include path; the `src/` files find each other relative to their own folder, so RNNoise's `arch.h` / `kiss_fft.h` never shadow SpeexDSP's.

### Local files and patches

1. `src/rnnoise_model_init.c` (local, not upstream): `init_rnnoise()` copied verbatim from the end of upstream `rnnoise_data.c` (everything from `#ifndef DUMP_BINARY_WEIGHTS`; identical in `rnnoise_data_little.c`). Upstream defines it in the 78 MB data file, outside the `USE_WEIGHTS_FILE` guard.
2. `src/denoise_weights_file.c` (local): `#define USE_WEIGHTS_FILE 1` then `#include "denoise.c"`. `USE_WEIGHTS_FILE` cannot be a project-wide define: JUCE 9's bundled Opus (`juce_audio_formats`) also tests it and then fails to compile (`'lpcnet': is not a member of 'OpusDecoder'`). `denoise.c` is the only RNNoise source that tests it.
3. `src/rnn_kiss_fft.c` (local): `#include "kiss_fft.c"`. MSBuild names object files after the source file, so RNNoise's `kiss_fft.c` and SpeexDSP's `kiss_fft.c` would both write `kiss_fft.obj` and one would silently replace the other (unresolved `kiss_fft` / `rnn_fft_c` at link time).
4. `src/rnn_sse2.h`, `src/nnet_sse2.c`, `src/nnet_default_sse2.c` (local): the SSE2 setting. `rnn_sse2.h` defines `OPUS_X86_MAY_HAVE_SSE 1` and `OPUS_X86_MAY_HAVE_SSE2 1` (upstream's own MSVC route, see `x86/x86_arch_macros.h`; `#error` unless x64 and without `RNN_ENABLE_X86_RTCD`); the two wrappers include it, then `nnet.c` / `nnet_default.c` (the only sources that include `vec.h`), and `#error` unless `vec.h` took the SSE2 path (`__SSE2__` defined, `NO_OPTIMIZATIONS` and `__AVX__` not). The defines cannot be project-wide: JUCE 9's bundled Opus (`juce_audio_formats`) defines `OPUS_X86_MAY_HAVE_SSE2` itself and keys its own runtime dispatch off these macros. Upstream's "Compiling without any vectorization" message therefore no longer appears.
5. Upstream files are modified only by patch 7 (`src/denoise.c`, `include/rnnoise.h`). No symbol clashes with SpeexDSP or JUCE's Opus: both kissfft copies keep their helpers `static`, RNNoise's exports are `rnn_*` / `rnnoise_*` plus `compute_rnn`, `eband20ms`, `init_rnnoise` (checked by linking everything together; no prefix patch needed).
6. Memory (local `Source/Engines/RnnoiseEngine.cpp`, not upstream): `RnnoiseEngine::prepare()` copies each embedded blob into its own heap buffer (about 5.1 MB per plugin instance), because the `BinaryData` arrays are only byte-aligned and RNNoise reads the weights in place.
7. Safety floor (local patch in upstream `src/denoise.c` and `include/rnnoise.h`, every change marked "Noise Suppression local patch"): `struct DenoiseState` gains `int floor_enabled; NspFloor floor;` at the end; new export `rnnoise_set_safety_floor(DenoiseState *st, int enabled)` (switching it on starts a fresh noise tracker); `denoise.c` includes the shared rule `../../../Source/Engines/SafetyFloor.h` and defines the cap `NSP_RNN_FLOOR_CAP_E` (−80 dBFS output as a sum of `compute_band_energy` band energies for int16-range input, derived in a comment there: `1e-8 * 32768^2 * (2267/6) / 1598`). Call site: in `rnnoise_process_frame`, inside `if (!silence)`, after the `lastg` loop and before `interp_band_gain`: when on, `nsp_floor_update` on `delayed_Ex`, then per band `g[i] = MAX16(g[i], nsp_floor_fill_gain(delayed_Ex[i], g[i]*g[i]*delayed_Ex[i], floorE[i]))`, so a band below (tracked room noise − 20 dB, total capped at −80 dBFS; room noise = per-band windowed minimum over the last ~1.5 s, see `SafetyFloor.h`) is raised to that floor with the noisy band itself and louder bands keep the model's band gain `g[i]` bit for bit (`interp_band_gain` still interpolates band gains across bins, so bins between a kept band and a raised neighbour can come out slightly louder); `lastg` and the silence branch are unchanged. `rnnoise_init`'s `memset` leaves it off with a zeroed tracker, so the library default is upstream behaviour (test "RnnoiseLib / rnnoise_set_safety_floor off (default) leaves output bit-identical to upstream behaviour"). `RnnoiseEngine::loadModels()` switches it on in the pristine `DenoiseState` snapshots, so it is always on in the plugin.

### Model blobs — `ThirdParty/rnnoise/models/`

The model (`0a8755f8`) is distributed by Xiph.Org with RNNoise under the same BSD-3-Clause license (`../COPYING`); upstream ships no separate model license. The blobs are a format conversion of upstream's `rnnoise_data.c` / `rnnoise_data_little.c`.

Generated once by `Tools/RnnoiseBlobTool/build.bat` (upstream `write_weights()` over the compiled-in arrays of `rnnoise_data.c` / `rnnoise_data_little.c`, with `DISABLE_DEBUG_FLOAT` exactly as upstream's default `./configure`, so the int8 layers carry no float copies and inference runs the same int8 path as an upstream build). Embedded in the plugin with JUCE BinaryData; loaded with `rnnoise_model_from_buffer`.

| File | Bytes | SHA-256 |
|---|---|---|
| `rnnoise_standard.bin` | 3544320 | `1ad07b428be34c74d9678d05b3c387b73ccf0e6203a1a5f9ce58689b30b303c8` |
| `rnnoise_light.bin` | 1553664 | `69fbd33c10d90e294673427f71fc4a592398d847efb2db19c99484f88b9343e4` |

The tool keeps upstream `write_weights.c`'s copyright and license header and differs from it in one way: it opens the output with `"wb"` (upstream's `"w"` would turn every `0x0A` byte into `0x0D 0x0A` on Windows).

### Known upstream quirks (handled in `RnnoiseEngine`)

- `rnnoise_init()` with a model allocates (`parse_weights` uses `calloc`/`realloc`/`free`), so it is called only in `prepare()`. The audio-thread reset copies a pristine `DenoiseState` snapshot taken right after `rnnoise_init` (bit-identical to calling `rnnoise_init` again; test "RnnoiseEngine / reset() == fresh").
- `rnnoise_model_from_buffer()` leaves `RNNModel::file` uninitialised, so `rnnoise_model_free()` could `fclose` garbage. Buffer models are released with `free()`.
- Latency: `rnnoise_process_frame` applies the gains to the previous frame's spectrum (`delayed_X`), so the output lags the input by 960 samples (two frames), not 480.

### How to re-check

From the parent folder of the repository (the one holding `Noise-suppression-VST/` and the reference copies in `_upstream/`), in Git Bash:

    U=_upstream/rnnoise; L=Noise-suppression-VST/ThirdParty/rnnoise
    (cd $L/src && for f in *.c *.h x86/*.h; do case $f in rnnoise_model_init.c|denoise_weights_file.c|rnn_kiss_fft.c|rnn_sse2.h|nnet_sse2.c|nnet_default_sse2.c) continue;; esac; diff -q --strip-trailing-cr ../../../../$U/src/$f $f >/dev/null || echo "DIFFERS $f"; done)
    diff -q --strip-trailing-cr $U/include/rnnoise.h $L/include/rnnoise.h
    diff <(sed -n '/^#ifndef DUMP_BINARY_WEIGHTS/,$p' $U/src/rnnoise_data.c) <(sed -n '/^#ifndef DUMP_BINARY_WEIGHTS/,$p' $L/src/rnnoise_model_init.c) && echo "init_rnnoise verbatim"
    sha256sum $L/models/*.bin

Expected: exactly `DIFFERS denoise.c`, a "differ" line for `rnnoise.h` (both: the safety-floor patch 7), `init_rnnoise verbatim`, and the two SHA-256 values above.
Regenerating the blobs (`Tools\RnnoiseBlobTool\build.bat`) must reproduce them byte for byte.

## WebRTC noise suppressor — `ThirdParty/webrtc/`

- Upstream: https://webrtc.googlesource.com/src, revision `b58dcf8fec3ad5291fb99c7e29650737ae7db953` (2026-10-02).
- License: BSD-3-Clause (`LICENSE`) plus the WebRTC patent grant (`PATENTS`); the Ooura FFT has its own permissive license (`common_audio/third_party/ooura/LICENSE`).
- Local reference copy: a checkout of upstream at `../_upstream/webrtc` relative to the repository. Upstream relative paths are kept, so the `#include`s resolve unchanged with `ThirdParty/webrtc` and `ThirdParty/webrtc/shim` on the include path.
- Needs C++20 (`std::span`, `<numbers>`): both projects use `cppLanguageStandard="20"`.

### What is kept

- `modules/audio_processing/ns/*` except `noise_suppressor_unittest.cc` and `BUILD.gn` (13 `.cc`, 15 `.h`).
- `modules/audio_processing/three_band_filter_bank.cc/.h`.
- `common_audio/third_party/ooura/fft_size_256/fft4g.cc/.h` (the only Ooura file `ns_fft.cc` includes) and `common_audio/third_party/ooura/LICENSE`.
- `LICENSE`, `PATENTS`, `AUTHORS` (verbatim from the upstream root).

Upstream's only other includes (excluding unit tests) are `rtc_base/checks.h` and `modules/audio_processing/audio_buffer.h`; both are replaced by shims.

### Shims (local, not upstream) — `ThirdParty/webrtc/shim/`

Both are BSD-3-Clause like the rest of this folder (`LICENSE`).

1. `rtc_base/checks.h`: `RTC_CHECK*` / `RTC_DCHECK*` / `*_NOTREACHED()` become `assert()` in Debug and a no-op in Release (`NDEBUG`; the condition is not evaluated). Never logs, never aborts in Release.
2. `modules/audio_processing/audio_buffer.h`: a minimal `webrtc::AudioBuffer` — a non-owning view of one channel's split bands (`split_bands(0)`, `split_bands_const(0)`) plus `num_channels()` (1), `num_bands()`, `num_frames_per_band()`. `NoiseSuppressor` uses only the two band accessors. No resampling, no splitting filter, no allocation. `WebRtcEngine` owns the band storage and runs `ThreeBandFilterBank` itself.

### Local patch

`modules/audio_processing/ns/noise_suppressor.h/.cc`: new public `void NoiseSuppressor::Reset()`. It restores exactly the state the constructor leaves, without allocating:

- `num_analyzed_frames_ = -1` and `capture_output_used_ = true`;
- per channel, the three estimator members (`SpeechProbabilityEstimator`, `WienerFilter`, `NoiseEstimator`) are destroyed and re-constructed in place (placement new; their constructors only fill fixed-size arrays);
- the analysis/synthesis memories are refilled as in `ChannelState`'s constructor; `process_delay_memory` keeps its storage and is zeroed.

Upstream has no reset, and re-constructing a `NoiseSuppressor` allocates (`ChannelState` holds a `std::vector`). Used by `WebRtcEngine` on the audio thread; pinned by `WebRtcLib` test "NoiseSuppressor::Reset (local patch)" and `WebRtcEngine` test "reset() == fresh". `ThreeBandFilterBank` needs no patch: assigning a default-constructed bank resets it (fixed-size arrays).

### Behaviour notes (upstream, unchanged)

- The three-band filter bank is not perfectly reconstructing (upstream's own comment: about 9.5 dB SNR on analysis + synthesis alone; measured 9.9 dB on white noise, 15.6 dB on a speech-like signal) and delays by 46 samples. With the noise suppressor's 96-sample (16 kHz) overlap the engine's extra latency is 46 + 3 · 96 = 334 samples at 48 kHz.
- The suppressor works in the int16 range (APM converts with `FloatToFloatS16`), so `WebRtcEngine` scales by 32768 in and 1/32768 out.

### How to re-check

From the parent folder of the repository (the one holding `Noise-suppression-VST/` and the reference copies in `_upstream/`), in Git Bash:

    U=_upstream/webrtc; L=Noise-suppression-VST/ThirdParty/webrtc
    for f in $(cd $L && find modules common_audio -type f); do diff -q --strip-trailing-cr $U/$f $L/$f >/dev/null || echo "PATCHED $f"; done
    diff -q --strip-trailing-cr $U/LICENSE $L/LICENSE && diff -q --strip-trailing-cr $U/PATENTS $L/PATENTS && echo licenses-identical

Expected: exactly `PATCHED modules/audio_processing/ns/noise_suppressor.cc`, `PATCHED modules/audio_processing/ns/noise_suppressor.h`, and `licenses-identical`.

## DeepFilterNet — `ThirdParty/deepfilternet/`

- Upstream: https://github.com/Rikorose/DeepFilterNet, commit `d375b2d`. Paper: Schröter et al., "DeepFilterNet: Perceptually Motivated Real-Time Speech Enhancement", arXiv:2305.08227.
- Model: `models/DeepFilterNet3_onnx.tar.gz` from the upstream checkout, SHA-256 `c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616` (`config.ini` + `enc.onnx`, `erb_dec.onnx`, `df_dec.onnx`).
- Blob: `models/dfn3_standard.bin`, 8,539,604 bytes (66 tensors, 2,134,641 floats), SHA-256 `c2d1567a952a97f7fbb227a92f004ca7111baef0b478c54c7dd23ec7564c9fae`. Written by `Tools/DfnBlobTool/dfn_blob_tool.py blob` (format in the script's docstring), embedded as BinaryData and checked by the `DfnNetwork` test.
- **No upstream source is vendored**: the network is ported to `Source/Engines/dfn/` (SSE2 only). The folder holds the blob, `LICENSE`, `LICENSE-MIT` and `LICENSE-APACHE` (verbatim from upstream) and a `README.md`.
- License: code MIT OR Apache-2.0; this project uses the port under the MIT licence. The weights have no separate license file. They are redistributed under the same MIT OR Apache-2.0 terms because (a) the upstream root `LICENSE` grants them for the repository without limiting it to code, (b) upstream itself embeds this exact tarball in its MIT/Apache-2.0 `libDF` crate (`libDF/src/tract.rs` `include_bytes!("../../models/DeepFilterNet3_onnx.tar.gz")`, `libDF/Cargo.toml` `license = "MIT/Apache-2.0"`), and (c) the authors' paper states they were published under an open source license. Upstream issues #697 / #700 asking for an explicit statement are unanswered (2026-10-18).
- Deviations from upstream:
  - Silence gate (C1): counts consecutive hops with mean square < 1e-7 only; after > 5 such hops the networks are skipped while STFT analysis/synthesis and the attenuation limit keep running. Upstream d375b2d resets its counter on every processed hop and, since the model bounds lsnr to [-15, 35], never fires; v0.5.6 zeroes silent hops.
  - Stereo: channels are processed independently; upstream's linked-stereo mode is not reproduced.
  - Strength presets: attenuation limit 12 / 24 / 80 dB, post-filter beta 0.02 for Strong only (upstream CLI default: no limit, no post-filter).
  - Safety floor (local): after the attenuation limit, in every stage including the silence gate, each ERB band whose output energy is below its floor gets the noisy band scaled to exactly that floor; louder bands are untouched. The floor is the tracked room noise − 20 dB (per band: a 40 ms average, its minimum over the last 1.25–1.5 s in six 250 ms sub-windows, never above the current hop, plus a fixed +1.1 dB bias compensation — so it falls at once, ignores speech and keystrokes with quiet gaps, and forgets a brief dip within 1.5 s), capped at −80 dBFS output in total (`Denoiser::kFloorCapEnergy`), using the shared rule in `Source/Engines/SafetyFloor.h`. It is off by default in `dfn::Denoiser` (so the upstream-fidelity and CLI end-to-end tests are unaffected) and always on in `DfnEngine`.
- Reference data: `Tests/dfn_reference/stage_in.bin` (57,344 bytes, SHA-256 `c71a93356b0a5a26057e4d0f723a69c21500ba8bf3731235812f0622b07ebe8b`) and `stage_out.bin` (385,280 bytes, SHA-256 `0a495e37f9dbff155b409421bfea45764a5252cf878cb48dfb6b43f97698b291`): synthetic features and the onnxruntime outputs of the three networks (`dfn_blob_tool.py stage-refs`).
- End-to-end reference (local only, not committed): official CLI `deep-filter-0.5.6-x86_64-pc-windows-msvc.exe` (v0.5.6, SHA-256 `75e11fa16445f560cb6b021521ddb89e89270d13b83089705d98776f58fd7915`), run with `-D` by `dfn_blob_tool.py cli-refs`.

### How to re-check

From the repository root, with the blob tool's venv (`python -m venv Tools\DfnBlobTool\.venv`, then `pip install -r Tools\DfnBlobTool\requirements.txt`; pins numpy 2.5.4, onnx 1.23.2, onnxruntime 1.31.0):

    Tools\DfnBlobTool\.venv\Scripts\python Tools\DfnBlobTool\dfn_blob_tool.py blob ..\_upstream\deepfilternet\models\DeepFilterNet3_onnx.tar.gz ThirdParty\deepfilternet\models\dfn3_standard.bin
    Tools\DfnBlobTool\.venv\Scripts\python Tools\DfnBlobTool\dfn_blob_tool.py stage-refs ..\_upstream\deepfilternet\models\DeepFilterNet3_onnx.tar.gz Tests\dfn_reference

Expected: the tarball and blob SHA-256 above, blob 8,539,604 bytes, `stage_in.bin` / `stage_out.bin` with the sizes and SHA-256 above (`git status` shows no change).
