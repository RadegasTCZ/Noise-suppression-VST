#pragma once
#include <juce_core/juce_core.h>

namespace nsp::test
{
/** The Tests folder (the one that holds NoiseSuppressionTests.jucer), found by walking up from the
    executable (Tests/Builds/VisualStudio2026/x64/<Config>/ConsoleApp/). Falls back to the CWD. */
inline juce::File testsDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (int i = 0; i < 8; ++i, d = d.getParentDirectory())
        if (d.getChildFile ("NoiseSuppressionTests.jucer").existsAsFile())
            return d;
    return juce::File::getCurrentWorkingDirectory();
}

/** Set by the `--audio-dir <folder>` command-line option; takes precedence over NSP_TEST_AUDIO_DIR. */
inline juce::String& testAudioDirOverride()
{
    static juce::String dir;
    return dir;
}

/** Folder with the test recordings (*.wav, e.g. sample.wav), which are not in the repository:
    `--audio-dir <folder>` if given, else the environment variable NSP_TEST_AUDIO_DIR if set, else
    `_test-audio` next to the repository folder (<repo>/../_test-audio). */
inline juce::File testAudioDir()
{
    if (testAudioDirOverride().isNotEmpty())
        return juce::File::getCurrentWorkingDirectory().getChildFile (testAudioDirOverride());
    const auto env = juce::SystemStats::getEnvironmentVariable ("NSP_TEST_AUDIO_DIR", {});
    if (env.isNotEmpty())
        return juce::File::getCurrentWorkingDirectory().getChildFile (env);
    return testsDir().getParentDirectory().getSiblingFile ("_test-audio");
}
} // namespace nsp::test
