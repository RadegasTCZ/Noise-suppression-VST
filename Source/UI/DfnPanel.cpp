// Source/UI/DfnPanel.cpp
#include "DfnPanel.h"
#include "../Params.h"

namespace nsp::ui
{
DfnPanel::DfnPanel (juce::AudioProcessorValueTreeState& apvts, std::function<bool()> modelFailed)
    : failed (std::move (modelFailed)),
      strength (*apvts.getParameter (nsp::ids::dfnStrength), nsp::dfnStrengthNames(), 1, kPillWidth, kPillHeight, kGap)
{
    setComponentID ("panel.dfn");
    strength.setComponentID (nsp::ids::dfnStrength);
    addAndMakeVisible (strength);
    setUpCaption (caption, "Strength");
    addAndMakeVisible (caption);
}

juce::String DfnPanel::readout() const
{
    return modelHasFailed() ? juce::String ("Model failed to load") : juce::String();
}

bool DfnPanel::readoutIsWarning() const
{
    return modelHasFailed();
}

void DfnPanel::resized()
{
    strength.setBounds (strength.gridBounds().withPosition (0, kSelectorTop));   // 0, 2, 62, 54
    caption.setBounds (getLocalBounds().withTrimmedTop (kKnobHeight));          // 0, 58, 62, 12
}
} // namespace nsp::ui
