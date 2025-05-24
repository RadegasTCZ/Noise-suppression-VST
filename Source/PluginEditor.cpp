#include "PluginEditor.h"

SpeexDSPNoiseSuppressorAudioProcessorEditor::SpeexDSPNoiseSuppressorAudioProcessorEditor (SpeexDSPNoiseSuppressorAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible(enableDenoiseButton);
    enableDenoiseButton.setButtonText("Enable noise suppression");

    addAndMakeVisible(suppressionLabel);
    suppressionLabel.setText("Noise suppression level:", juce::dontSendNotification);
    suppressionLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(noiseSuppressSlider);
    noiseSuppressSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    noiseSuppressSlider.setRange(-40.0, -5.0, 1.0);
    noiseSuppressSlider.setTextValueSuffix(" dB");
    noiseSuppressSlider.setNumDecimalPlacesToDisplay(0);
    noiseSuppressSlider.setTooltip("Noise suppression level");

    addAndMakeVisible(learningLabel);
    learningLabel.setText("Learning aggressiveness:", juce::dontSendNotification);
    learningLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(probStartSlider);
    probStartSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    probStartSlider.setRange(0.5, 1.0, 0.01);
    probStartSlider.setNumDecimalPlacesToDisplay(2);
    probStartSlider.setTooltip("How much to trust the start as noise");

    addAndMakeVisible(adoptionLabel);
    adoptionLabel.setText("Adoption aggressiveness:", juce::dontSendNotification);
    adoptionLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(probContinueSlider);
    probContinueSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    probContinueSlider.setRange(0.5, 1.0, 0.01);
    probContinueSlider.setNumDecimalPlacesToDisplay(2);
    probContinueSlider.setTooltip("How quickly noise adapts to changes");

    addAndMakeVisible(versionLabel);
    versionLabel.setText("Version: " JucePlugin_VersionString, juce::dontSendNotification);

    noiseSuppressAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "noisesuppress", noiseSuppressSlider);
    probStartAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "probstart", probStartSlider);
    probContinueAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "probcontinue", probContinueSlider);
    enableDenoiseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "enableDenoise", enableDenoiseButton);

    const int margin = 16;
    const int labelHeight = 18;
    const int sliderHeight = 32;
    const int sliderSpacing = 10;

    const int numSliderGroups = 4;
    const int totalHeight = margin +
        numSliderGroups * (labelHeight + sliderHeight + sliderSpacing) +
        margin;

    setSize(400, totalHeight);

}

SpeexDSPNoiseSuppressorAudioProcessorEditor::~SpeexDSPNoiseSuppressorAudioProcessorEditor() {}

void SpeexDSPNoiseSuppressorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

void SpeexDSPNoiseSuppressorAudioProcessorEditor::resized()
{

    int margin = 16;
    int labelHeight = 18;
    int sliderHeight = 32;
    int buttonHeight = 24;
    int sliderSpacing = 10;
    int y = margin;

    enableDenoiseButton.setBounds(margin, y, getWidth() - 2 * margin, buttonHeight);
    y += buttonHeight + sliderSpacing;
    suppressionLabel.setBounds(margin, y, getWidth() - 2 * margin, labelHeight);
    y += labelHeight + 2;
    noiseSuppressSlider.setBounds(margin, y, getWidth() - 2 * margin, sliderHeight);
    y += sliderHeight + sliderSpacing;
    learningLabel.setBounds(margin, y, getWidth() - 2 * margin, labelHeight);
    y += labelHeight + 2;
    probStartSlider.setBounds(margin, y, getWidth() - 2 * margin, sliderHeight);
    y += sliderHeight + sliderSpacing;
    adoptionLabel.setBounds(margin, y, getWidth() - 2 * margin, labelHeight);
    y += labelHeight + 2;
    probContinueSlider.setBounds(margin, y, getWidth() - 2 * margin, sliderHeight);
    y += labelHeight + 2;
    versionLabel.setBounds(10, getHeight() - 24, 120, 20);
}