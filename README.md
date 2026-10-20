# Noise Suppression (VST2 / VST3)

*My very first project I published on GitHub, now in its second generation.*

Real-time noise suppression plugin for voice, built with JUCE. Version 2.0 hosts several
noise-suppression **engines** behind one small window; exactly one engine runs at a time and
you can switch between them while audio plays (10 ms crossfade, no click, no latency change).

## Engines

| Engine | What it is |
|---|---|
| **Classic** ("Classic Speex" in the plugin) | SpeexDSP's `speex_preprocess` noise suppressor, as in v1.x (now at 48 kHz, 10 ms frames). |
| **SNS** ("SNS v2" in the plugin, default) | Spectral Noise Suppressor v2.4: 40-band ERB analysis, decision-directed a-priori SNR, EM gain and OMLSA speech-probability interpolation. Shows its noise-floor estimate. |
| **WebRTC NS** | The noise suppressor from WebRTC's audio processing module, run exactly as WebRTC runs it at 48 kHz (three-band split, 10 ms frames). Four levels: 6, 12, 18, 21 dB. |
| **RNNoise** | Xiph's recurrent-neural-network noise suppressor (model `0a8755f8`). Two models: **Standard** and **Light** (same network size, sparser weights, about half the CPU). |
| **DeepFilterNet** | DeepFilterNet3 (Schröter et al.), a neural full-band suppressor ported to C++; the strongest engine and the heaviest on CPU. Three strengths: Gentle (12 dB), Normal (24 dB), Strong (80 dB + post-filter). |

All engines run internally at 48 kHz; other host sample rates are resampled (SpeexDSP resampler).

## Controls

- **Engine**: Classic Speex, SNS v2, WebRTC NS, RNNoise or DeepFilterNet.
- **Suppress** (Classic and SNS, −40 … −5 dB, default −20): how much noise is removed. Turning the knob **clockwise suppresses more** (towards −40 dB).
- **Level** (WebRTC NS: 6 / 12 / 18 / 21 dB, default 12): WebRTC's suppression level.
- **Model** (RNNoise: Standard / Light, default Standard). Changing the level or model crossfades over 10 ms; the newly selected one then re-adapts to the noise (WebRTC about half a second).
  If an RNNoise model cannot be loaded, the engine passes the audio through and shows "Model failed to load".
- **Strength** (DeepFilterNet: Gentle / Normal / Strong, default Normal).
- **Safety floor** (RNNoise and DeepFilterNet, always on): pauses never drop to digital silence; the floor keeps about the room noise − 20 dB, never above −80 dBFS.
- **Limiter** (default on): a safety net at the very end of the processed signal that keeps the processed signal at or below −1 dBFS (instant attack, 100 ms release, no added latency). It is not a true-peak limiter, and it never touches bypassed audio.
- **Bypass**: the same switch as your host's bypass; fades in and out.
- **Monitor residual**: listen to what is being removed instead of the result.
- **IN / OUT meters**: peak level in dBFS with a 1.5 s peak hold.
- **Floor** (SNS): current noise-floor estimate in dBFS.

Mono and stereo tracks are supported. Latency is **40 ms** (1920 samples at 48 kHz), the same
for every engine (DeepFilterNet needs three frames; the other engines are padded to match, so switching
never changes the latency); at other sample rates it is about 40 ms plus a few samples of resampler
delay, reported to the host.

Version 2 is a new plugin identity for hosts (new name, file names and plugin/manufacturer IDs), so it
installs next to v1.1.2 instead of replacing it, and sessions saved with v1.1.2 will not find it
automatically. State saved by v1.1.2 that is loaded into v2 opens with the Classic engine, your old
suppression value and the old on/off state.

## Building

Requirements: Windows x64, Visual Studio 2026, [JUCE 9.0.3](https://juce.com) installed at `C:\JUCE9`
(the project's module paths point there; if JUCE is elsewhere, re-point the module paths of both
`.jucer` files in Projucer).

1. *(VST2 only)* In Projucer, **File → Global Paths → VST (Legacy) SDK**, set the folder of your VST2 SDK
   (e.g. `C:\vstsdk2.4`). Without it, untick VST (Legacy) in the project's plugin formats.
2. Generate the Visual Studio project:
   ```powershell
   C:\JUCE9\Projucer.exe --resave NoiseSuppression.jucer
   ```
3. Build (or open `Builds\VisualStudio2026\NoiseSuppression.sln`):
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
       Builds\VisualStudio2026\NoiseSuppression.sln -p:Configuration=Release -p:Platform=x64
   ```
   The plugins are under `Builds\VisualStudio2026\x64\Release\`.

### Tests

`Tests/` is a console app that compiles the plugin sources plus the tests. A few tests use files that
are not in this repository; without them those tests are **skipped** (logged as `SKIPPED`), not failed:

- **Test recordings** (the residual test needs `sample.wav`, 48 kHz stereo; `--render` processes every
  `*.wav`): taken from the folder given with `--audio-dir <folder>`, else from the environment variable
  `NSP_TEST_AUDIO_DIR`, else from `_test-audio` next to the repository folder (`<repo>\..\_test-audio`).
- **RNNoise reference sources** (the bit-exactness tests of the embedded model blobs): upstream's
  `src/rnnoise_data.c` and `src/rnnoise_data_little.c` (108 MB, model `0a8755f8`), found through the test
  project's last header search path, `<repo>\..\_upstream\rnnoise\src` (an upstream RNNoise checkout at
  commit `70f1d25` with the model extracted, next to the repository folder). If they are not there, the
  reference is compiled as a stub and those two tests are skipped.

```powershell
C:\JUCE9\Projucer.exe --resave Tests\NoiseSuppressionTests.jucer
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
    Tests\Builds\VisualStudio2026\NoiseSuppressionTests.sln -p:Configuration=Release -p:Platform=x64
Tests\Builds\VisualStudio2026\x64\Release\ConsoleApp\NoiseSuppressionTests.exe              # all tests, exit code 0 = pass
Tests\Builds\VisualStudio2026\x64\Release\ConsoleApp\NoiseSuppressionTests.exe --category UiEditor
Tests\Builds\VisualStudio2026\x64\Release\ConsoleApp\NoiseSuppressionTests.exe --render     # listening renders to Tests\render\
Tests\Builds\VisualStudio2026\x64\Release\ConsoleApp\NoiseSuppressionTests.exe --render --audio-dir D:\my-recordings
```

### RNNoise model blobs

The plugin embeds the two RNNoise models as binary blobs (`ThirdParty/rnnoise/models/*.bin`, committed).
They were generated once with `Tools\RnnoiseBlobTool\build.bat` from the upstream model sources
(`RNNOISE_SRC`, default `<repo>\..\_upstream\rnnoise\src`; Visual Studio is found with `vswhere.exe`);
`ThirdParty/UPSTREAM.md` records their SHA-256 and how to regenerate and re-check them.

The DeepFilterNet3 weights are embedded the same way, generated from the upstream model archive by
`Tools/DfnBlobTool/dfn_blob_tool.py` (see `ThirdParty/UPSTREAM.md` for the recorded checksums and the
regeneration steps).

## Credits & Licenses

- [SpeexDSP](https://github.com/xiph/speexdsp) © Xiph.Org Foundation and contributors (BSD-3-Clause):
  the Classic engine, the resampler and the FFT used by SNS.
- SNS (Spectral Noise Suppressor v2) is my own DSP code, with noise estimation derived from SpeexDSP's preprocessor
  (`libspeexdsp/preprocess.c` © 2003-2006 Epic Games, written by Jean-Marc Valin, BSD-3-Clause);
  its band-energy and band-gain interpolation functions (`compute_band_energy()`, `interp_band_gain()`) are adapted
  from RNNoise's `denoise.c` © 2017 Mozilla, 2018 Gregor Richards, 2024 Jean-Marc Valin (BSD-3-Clause).
- [WebRTC](https://webrtc.googlesource.com/src) noise suppressor and three-band filter bank © The WebRTC project authors
  (BSD-3-Clause and the WebRTC patent grant), including Takuya Ooura's FFT.
- [RNNoise](https://gitlab.xiph.org/xiph/rnnoise) © Jean-Marc Valin, Mozilla, Amazon and contributors (BSD-3-Clause), model `0a8755f8`.
- [DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) © 2021 Hendrik Schröter (MIT OR Apache-2.0, used here under MIT, see `ThirdParty/deepfilternet/LICENSE-MIT`),
  DeepFilterNet3 model. H. Schröter, T. Rosenkranz, A. N. Escalante-B., A. Maier, "DeepFilterNet: Perceptually Motivated
  Real-Time Speech Enhancement", INTERSPEECH 2023, arXiv:2305.08227. The pretrained weights have no separate licence file;
  they are redistributed under the repository's MIT OR Apache-2.0 licence, which upstream applies to them itself by
  embedding the same model in its MIT/Apache-2.0 `libDF` library (details in `ThirdParty/deepfilternet/README.md`).
  Upstream issues #697 and #700 asking for an explicit statement are still open (as of 2026-10-18).
- Built with [JUCE](https://juce.com), used under the GNU Affero General Public License v3 (AGPLv3); the plugin binaries
  combine GPLv3 and AGPLv3 code, so the AGPLv3 applies to them as a whole. VST3 SDK © Steinberg Media Technologies GmbH (MIT).

This code is released under the GPLv3 license. See the `LICENSE` file for details; the third-party
license files are listed there and in `ThirdParty/UPSTREAM.md`.

VST is a registered trademark of Steinberg Media Technologies GmbH.

## VST2 Support Disclaimer

Due to licensing restrictions from Steinberg, the VST2 SDK cannot be distributed with this project.
Building VST2 versions of this plugin is only possible if you already have the VST2 SDK from a previous license agreement with Steinberg.

No VST2 SDK files are included in this repository, and I cannot provide them.
VST2 builds are for personal use only and must not be redistributed: the VST2 SDK license is not
compatible with the GPL. Published binaries are VST3 only.
VST3 build is fully supported and open source.

## Tools
This project was made with the help from ChatGPT 4.1 by OpenAI.
The 2.0 multi-engine rewrite was made with the help of Claude by Anthropic.
