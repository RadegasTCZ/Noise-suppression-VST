#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/MeterView.h"
#include "UI/EnginePanel.h"
#include <memory>
#include <vector>

/** Fixed 440 x 140 strip (no resizing). setSize is called once, in the
    constructor. A 30 Hz timer ticks the meters, switches the visible engine panel and updates
    the readout and title, repainting only what changed. */
class NoiseSuppressionEditor final : public juce::AudioProcessorEditor,
                                     public juce::Timer
{
public:
    static constexpr int kWidth = 440, kHeight = 140;

    explicit NoiseSuppressionEditor (NoiseSuppressionProcessor&);
    ~NoiseSuppressionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    juce::String titleText() const;
    juce::String readoutText() const { return readout; }
    bool readoutIsWarning() const     { return readoutWarning; }

private:
    static constexpr int kPad = 8, kHeaderHeight = 28, kGapY = 8, kGapX = 10, kTitleWidth = 140, kBypassWidth = 76,
                         kComboHeight = 24, kBoxPadX = 10, kBoxPadY = 8, kMonitorWidth = 130;

    NoiseSuppressionProcessor& proc;
    std::atomic<float>* engineParam = nullptr;
    nsp::ui::Theme theme;                         // declared before every child: destroyed after them

    juce::ComboBox engineBox;
    juce::ToggleButton bypassButton { "Bypass" }, monitorButton { "Monitor residual" }, limiterButton { "Limiter" };
    std::vector<std::unique_ptr<nsp::ui::EnginePanel>> panels;   // index = engine parameter index
    nsp::ui::MeterView meters;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, monitorAttachment, limiterAttachment;

    juce::Rectangle<int> titleArea, boxArea, separatorArea, readoutArea;
    int shownEngine = -1;
    bool rateSupported = true;
    juce::String readout;
    bool readoutWarning = false;
    double lastTickSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseSuppressionEditor)
};
