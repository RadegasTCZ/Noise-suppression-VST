// Source/UI/ClassicPanel.cpp
#include "ClassicPanel.h"
#include "../Params.h"

namespace nsp::ui
{
ClassicPanel::ClassicPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setComponentID ("panel.classic");
    knob.setComponentID (nsp::ids::classicSuppress);
    addAndMakeVisible (knob);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, nsp::ids::classicSuppress, knob);
    setUpCaption (caption, "Suppress");
    addAndMakeVisible (caption);
}

void ClassicPanel::resized()
{
    auto r = getLocalBounds();
    knob.setBounds (r.removeFromTop (kKnobHeight));
    caption.setBounds (r);
}
} // namespace nsp::ui
