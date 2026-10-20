#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace nsp::test
{
inline constexpr int kRenderBlock = 512;

/** Runs `input` through a fresh NoiseSuppressionProcessor (engine `engineIndex`,
    optionally parameter `variantParamId` = `variantValue` (e.g. rnnModel = 1), every other parameter
    at its default) at `sampleRate` with 512-sample blocks, discards the first getLatencySamples()
    output samples and feeds that many zeros at the end, so `output` (resized to the input's size) is
    time-aligned with `input`. False for 0 or > 2 channels or a refused layout. */
bool renderBuffer (const juce::AudioBuffer<float>& input, double sampleRate, int engineIndex,
                   juce::AudioBuffer<float>& output,
                   const char* variantParamId = nullptr, float variantValue = 0.0f);

/** `--render`: every *.wav in testAudioDir() (TestPaths.h; non-recursive), through every
    engine (RNNoise with both models), to Tests/render/<folder>/<file name>.wav (24-bit), folders
    classic, sns, webrtc, rnnoise (Standard), rnnoise-light. One line per file and target.
    Returns 0, or 1 if the input folder is missing or a file could not be written. */
int runRenders();
} // namespace nsp::test
