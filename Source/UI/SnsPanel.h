// Source/UI/SnsPanel.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EnginePanel.h"
#include "SuppressKnob.h"
#include <functional>
#include <memory>

namespace nsp::ui
{
/** SNS v2: one flipped Suppress knob bound to snsSuppress; readout "Floor -62.4" (dBFS) from
    noiseFloorDb (the editor passes processor.getSnsNoiseFloorDb(); tests pass a stub). */
class SnsPanel final : public EnginePanel
{
public:
    SnsPanel (juce::AudioProcessorValueTreeState& apvts, std::function<float()> noiseFloorDb);
    juce::String readout() const override;
    void resized() override;

private:
    std::function<float()> floorDb;
    SuppressKnob knob;
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnsPanel)
};
} // namespace nsp::ui
