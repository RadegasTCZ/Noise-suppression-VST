#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpeexDSPNoiseSuppressorAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    SpeexDSPNoiseSuppressorAudioProcessorEditor (SpeexDSPNoiseSuppressorAudioProcessor&);
    ~SpeexDSPNoiseSuppressorAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    SpeexDSPNoiseSuppressorAudioProcessor& processor;

    juce::ToggleButton enableDenoiseButton;
    juce::ToggleButton enableVADButton;
    juce::ToggleButton enableLevelGateButton;
    juce::Slider noiseSuppressSlider;
    juce::Slider probStartSlider;
    juce::Slider probContinueSlider;
    juce::Slider vadThresholdSlider;
    juce::Slider gateFloorSlider;
    juce::Slider gateAttackSlider;
    juce::Slider gateReleaseSlider;
    juce::Label suppressionLabel;
    juce::Label learningLabel;
    juce::Label adoptionLabel;
    juce::Label vadThresholdLabel;
    juce::Label versionLabel;
    juce::Label gateFloorLabel;
    juce::Label gateAttackLabel;
    juce::Label gateReleaseLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableDenoiseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableVADAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableLevelGateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseSuppressAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> probStartAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> probContinueAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> vadThresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateFloorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateReleaseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeexDSPNoiseSuppressorAudioProcessorEditor)
};