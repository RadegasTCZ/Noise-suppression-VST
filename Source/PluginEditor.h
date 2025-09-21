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
    juce::Slider noiseSuppressSlider;
    juce::Slider probStartSlider;
    juce::Slider probContinueSlider;
    juce::Label suppressionLabel;
    juce::Label learningLabel;
    juce::Label adoptionLabel;
    juce::Label versionLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableDenoiseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableVADAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseSuppressAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> probStartAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> probContinueAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeexDSPNoiseSuppressorAudioProcessorEditor)
};