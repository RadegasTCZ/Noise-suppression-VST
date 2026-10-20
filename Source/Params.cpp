#include "Params.h"

namespace nsp
{
juce::StringArray engineNames()
{
    return { "Classic Speex", "SNS v2", "WebRTC NS", "RNNoise", "DeepFilterNet" };
}

juce::StringArray webrtcLevelNames()
{
    return { "6 dB", "12 dB", "18 dB", "21 dB" };
}

juce::StringArray rnnModelNames()
{
    return { "Standard", "Light" };
}

juce::StringArray dfnStrengthNames()
{
    return { "Gentle", "Normal", "Strong" };
}

std::vector<std::unique_ptr<juce::RangedAudioParameter>> createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    const juce::NormalisableRange<float> suppressRange (-40.0f, -5.0f, 1.0f);
    const auto dB = juce::AudioParameterFloatAttributes().withLabel ("dB");

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::engine, kParamVersion }, "Engine", engineNames(), (int) kEngineSns));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::bypass, kParamVersion }, "Bypass", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::monitorResidual, kParamVersion }, "Monitor Residual", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::classicSuppress, kParamVersion }, "Classic Suppress", suppressRange, -20.0f, dB));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::snsSuppress, kParamVersion }, "SNS Suppress", suppressRange, -20.0f, dB));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::webrtcLevel, kParamVersion }, "WebRTC Level", webrtcLevelNames(), 1));   // 12 dB
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::rnnModel, kParamVersion }, "RNNoise Model", rnnModelNames(), 0));        // Standard
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::dfnStrength, kParamVersion }, "DeepFilterNet Strength", dfnStrengthNames(), 1)); // Normal
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::limiter, kParamVersion }, "Output Limiter", true));   // -1 dBFS safety net

    return params;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    auto params = createParameters();
    return { params.begin(), params.end() };
}
} // namespace nsp
