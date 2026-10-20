// Source/UI/ClassicPanel.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EnginePanel.h"
#include "SuppressKnob.h"
#include <memory>

namespace nsp::ui
{
/** Classic Speex: one flipped Suppress knob bound to classicSuppress. No readout. */
class ClassicPanel final : public EnginePanel
{
public:
    explicit ClassicPanel (juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    SuppressKnob knob;
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;   // after knob: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClassicPanel)
};
} // namespace nsp::ui
