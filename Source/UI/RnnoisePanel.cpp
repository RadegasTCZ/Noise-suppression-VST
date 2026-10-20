// Source/UI/RnnoisePanel.cpp
#include "RnnoisePanel.h"
#include "../Params.h"

namespace nsp::ui
{
RnnoisePanel::RnnoisePanel (juce::AudioProcessorValueTreeState& apvts, std::function<bool (int)> modelFailed)
    : failed (std::move (modelFailed)),
      modelParam (apvts.getRawParameterValue (nsp::ids::rnnModel)),
      models (*apvts.getParameter (nsp::ids::rnnModel), nsp::rnnModelNames(), 1, kPillWidth, kPillHeight, kGap)
{
    jassert (modelParam != nullptr);
    setComponentID ("panel.rnnoise");
    models.setComponentID (nsp::ids::rnnModel);
    addAndMakeVisible (models);
    setUpCaption (caption, "Model");
    addAndMakeVisible (caption);
}

bool RnnoisePanel::selectedModelFailed() const
{
    const int model = modelParam != nullptr ? juce::roundToInt (modelParam->load (std::memory_order_relaxed)) : 0;
    return failed != nullptr && failed (model);
}

juce::String RnnoisePanel::readout() const
{
    return selectedModelFailed() ? juce::String ("Model failed to load") : juce::String();
}

bool RnnoisePanel::readoutIsWarning() const
{
    return selectedModelFailed();
}

void RnnoisePanel::resized()
{
    models.setBounds (models.gridBounds().withPosition (0, kSelectorTop));   // 0, 7, 62, 44
    caption.setBounds (getLocalBounds().withTrimmedTop (kKnobHeight));      // 0, 58, 62, 12
}
} // namespace nsp::ui
