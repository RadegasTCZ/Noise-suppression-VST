#include "PluginEditor.h"

SpeexDSPNoiseSuppressorAudioProcessorEditor::SpeexDSPNoiseSuppressorAudioProcessorEditor (SpeexDSPNoiseSuppressorAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible(enableDenoiseButton);
    enableDenoiseButton.setButtonText("Enable Noise Suppression");

    addAndMakeVisible(enableVADButton);
    enableVADButton.setButtonText("Enable Voice Activation Detection");

    addAndMakeVisible(enableLevelGateButton);
    enableLevelGateButton.setButtonText("Enable Level Noise Gate");

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
    probStartSlider.setRange(50, 100, 1);
    probStartSlider.setTextValueSuffix("%");
    probStartSlider.setNumDecimalPlacesToDisplay(0);
    probStartSlider.setTooltip("How much to trust the start as noise");

    addAndMakeVisible(adoptionLabel);
    adoptionLabel.setText("Adoption aggressiveness:", juce::dontSendNotification);
    adoptionLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(probContinueSlider);
    probContinueSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    probContinueSlider.setRange(50, 100, 1);
    probContinueSlider.setTextValueSuffix("%");
    probContinueSlider.setNumDecimalPlacesToDisplay(0);
    probContinueSlider.setTooltip("How quickly noise adapts to changes");

    addAndMakeVisible(vadThresholdLabel);
    vadThresholdLabel.setText("Noise Gate RMS Threshold level:", juce::dontSendNotification);
    vadThresholdLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(vadThresholdSlider);
    vadThresholdSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    vadThresholdSlider.setRange(-60, -15, 1.0);
    vadThresholdSlider.setTextValueSuffix(" dBFS");
    vadThresholdSlider.setNumDecimalPlacesToDisplay(0);
    vadThresholdSlider.setTooltip("Set the gate threshold level to clamp down on residual noise");

    addAndMakeVisible(gateFloorLabel);
    gateFloorLabel.setText("Gate floor:", juce::dontSendNotification);
    gateFloorLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(gateFloorSlider);
    gateFloorSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gateFloorSlider.setRange(-60.0, -5.0, 1.0);
    gateFloorSlider.setTextValueSuffix(" dB");
    gateFloorSlider.setNumDecimalPlacesToDisplay(0);
    gateFloorSlider.setTooltip("Reduction applied when no speech is detected");

    addAndMakeVisible(gateAttackLabel);
    gateAttackLabel.setText("Gate attack (ms):", juce::dontSendNotification);
    gateAttackLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(gateAttackSlider);
    gateAttackSlider.setSliderStyle(juce::Slider::IncDecButtons);
    gateAttackSlider.setRange(1.0, 10.0, 0.5);
    gateAttackSlider.setNumDecimalPlacesToDisplay(1);
    gateAttackSlider.setTextValueSuffix(" ms");
    gateAttackSlider.setTooltip("Smoothing attack (1–10 ms)");
    gateAttackSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 28);
    gateAttackSlider.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_AutoDirection);

    addAndMakeVisible(gateReleaseLabel);
    gateReleaseLabel.setText("Gate release (ms):", juce::dontSendNotification);
    gateReleaseLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(gateReleaseSlider);
    gateReleaseSlider.setSliderStyle(juce::Slider::IncDecButtons);
    gateReleaseSlider.setRange(10.0, 1000.0, 5.0);
    gateReleaseSlider.setNumDecimalPlacesToDisplay(0);
    gateReleaseSlider.setTextValueSuffix(" ms");
    gateReleaseSlider.setTooltip("Smoothing release (10–1000 ms)");
    gateReleaseSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 28);
    gateReleaseSlider.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_AutoDirection);

    addAndMakeVisible(versionLabel);
    versionLabel.setText("Version: " JucePlugin_VersionString, juce::dontSendNotification);

    enableDenoiseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "enableDenoise", enableDenoiseButton);
    enableVADAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "enableVAD", enableVADButton);
    enableLevelGateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "enableLevelGate", enableLevelGateButton);
    noiseSuppressAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "noisesuppress", noiseSuppressSlider);
    probStartAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "probstart", probStartSlider);
    probContinueAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "probcontinue", probContinueSlider);
    vadThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "vadThreshold", vadThresholdSlider);
    gateFloorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "gateFloor", gateFloorSlider);
    gateAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "gateAttackMs", gateAttackSlider);
    gateReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "gateReleaseMs", gateReleaseSlider);

    const int margin = 16;
    const int buttonHeight = 16;
    const int labelHeight = 18;
    const int sliderHeight = 32;
    const int sliderSpacing = 10;
    const int numButtons = 3;
    const int numSliderGroups = 7;
    const int totalHeight = margin + (numButtons * (buttonHeight)) + margin + (numSliderGroups * (labelHeight + sliderHeight + sliderSpacing)) + margin;
    setSize(400, totalHeight);
}

SpeexDSPNoiseSuppressorAudioProcessorEditor::~SpeexDSPNoiseSuppressorAudioProcessorEditor() {}

void SpeexDSPNoiseSuppressorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff020b1a));
}

void SpeexDSPNoiseSuppressorAudioProcessorEditor::resized()
{
    int margin = 16;
    int labelHeight = 18;
    int sliderHeight = 32;
    int buttonHeight = 24;
    int sliderSpacing = 10;
    int halfWidth = (getWidth() - 3 * margin) / 2;
    int y = margin;

    enableDenoiseButton.setBounds(margin, y, getWidth() - 2 * margin, buttonHeight);
    y += buttonHeight + sliderSpacing;
    enableVADButton.setBounds(margin, y, getWidth() - 2 * margin, buttonHeight);
    y += buttonHeight + sliderSpacing;
    enableLevelGateButton.setBounds(margin, y, getWidth() - 2 * margin, buttonHeight);
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
    y += sliderHeight + sliderSpacing;
    vadThresholdLabel.setBounds(margin, y, getWidth() - 2 * margin, labelHeight);
    y += labelHeight + 2;
    vadThresholdSlider.setBounds(margin, y, getWidth() - 2 * margin, sliderHeight);
    y += sliderHeight + sliderSpacing;
    gateFloorLabel.setBounds(margin, y, getWidth() - 2 * margin, labelHeight);
    y += labelHeight + 2;
    gateFloorSlider.setBounds(margin, y, getWidth() - 2 * margin, sliderHeight);
    y += sliderHeight + sliderSpacing;
    gateAttackLabel.setBounds(margin, y, halfWidth, labelHeight);
    gateReleaseLabel.setBounds(margin + halfWidth + margin, y, halfWidth, labelHeight);
    y += labelHeight + 4;
    gateAttackSlider.setBounds(margin, y, halfWidth, sliderHeight);
    gateReleaseSlider.setBounds(margin + halfWidth + margin, y, halfWidth, sliderHeight);
    y += labelHeight + 2 + margin;
    versionLabel.setBounds(10, getHeight() - 24, 120, 20);
}