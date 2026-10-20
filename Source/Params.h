#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace nsp
{
namespace ids
{
    inline constexpr const char* engine = "engine", *bypass = "bypass",
        *monitorResidual = "monitorResidual", *classicSuppress = "classicSuppress", *snsSuppress = "snsSuppress",
        *rnnModel = "rnnModel", *webrtcLevel = "webrtcLevel", *dfnStrength = "dfnStrength",
        *limiter = "limiter";
}

inline constexpr int kParamVersion = 2, kStateVersion = 2;
inline constexpr const char* kStateType = "Parameters";              // APVTS root tag (same as v1.1.2)
inline constexpr const char* kStateVersionProperty = "stateVersion"; // root property; absent in v1.1.2

/** Engine parameter indices. Appended only; never reorder (saved sessions store the index). */
enum EngineIndex { kEngineClassic = 0, kEngineSns = 1, kEngineWebRtc = 2, kEngineRnnoise = 3, kEngineDfn = 4 };

juce::StringArray engineNames();       // {"Classic Speex", "SNS v2", "WebRTC NS", "RNNoise", "DeepFilterNet"}
juce::StringArray webrtcLevelNames();  // {"6 dB", "12 dB", "18 dB", "21 dB"} = WebRtcEngine::Level
juce::StringArray rnnModelNames();     // {"Standard", "Light"} = RnnoiseEngine::Model
juce::StringArray dfnStrengthNames();  // {"Gentle", "Normal", "Strong"} = DfnEngine::Strength

/** Fresh parameter objects in layout order (used by createParameterLayout and state migration). */
std::vector<std::unique_ptr<juce::RangedAudioParameter>> createParameters();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace nsp
