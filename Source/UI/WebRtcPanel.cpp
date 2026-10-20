// Source/UI/WebRtcPanel.cpp
#include "WebRtcPanel.h"
#include "../Params.h"

namespace nsp::ui
{
WebRtcPanel::WebRtcPanel (juce::AudioProcessorValueTreeState& apvts)
    : levels (*apvts.getParameter (nsp::ids::webrtcLevel), { "6", "12", "18", "21" }, 2, kPillWidth, kPillHeight, kGap)
{
    setComponentID ("panel.webrtc");
    levels.setComponentID (nsp::ids::webrtcLevel);
    addAndMakeVisible (levels);
    setUpCaption (caption, "Level (dB)");
    addAndMakeVisible (caption);
}

void WebRtcPanel::resized()
{
    levels.setBounds (levels.gridBounds().withPosition (0, kSelectorTop));    // 0, 7, 62, 44
    caption.setBounds (getLocalBounds().withTrimmedTop (kKnobHeight));       // 0, 58, 62, 12 (as the knob panels)
}
} // namespace nsp::ui
