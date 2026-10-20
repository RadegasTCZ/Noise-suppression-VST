// Source/UI/SnsPanel.cpp
#include "SnsPanel.h"
#include "../Params.h"

namespace nsp::ui
{
SnsPanel::SnsPanel (juce::AudioProcessorValueTreeState& apvts, std::function<float()> noiseFloorDb)
    : floorDb (std::move (noiseFloorDb))
{
    setComponentID ("panel.sns");
    knob.setComponentID (nsp::ids::snsSuppress);
    addAndMakeVisible (knob);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, nsp::ids::snsSuppress, knob);
    setUpCaption (caption, "Suppress");
    addAndMakeVisible (caption);
}

juce::String SnsPanel::readout() const
{
    return "Floor " + juce::String (floorDb != nullptr ? floorDb() : -90.0f, 1);
}

void SnsPanel::resized()
{
    auto r = getLocalBounds();
    knob.setBounds (r.removeFromTop (kKnobHeight));
    caption.setBounds (r);
}
} // namespace nsp::ui
