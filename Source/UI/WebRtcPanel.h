// Source/UI/WebRtcPanel.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EnginePanel.h"
#include "SegmentedSelector.h"

namespace nsp::ui
{
/** WebRTC NS: 2 x 2 grid of 29 x 20 pills "6 12 / 18 21" bound to webrtcLevel,
    caption "Level (dB)". No readout. */
class WebRtcPanel final : public EnginePanel
{
public:
    static constexpr int kPillWidth = 29, kPillHeight = 20, kGap = 4, kSelectorTop = 7;

    explicit WebRtcPanel (juce::AudioProcessorValueTreeState& apvts);
    void resized() override;

private:
    SegmentedSelector levels;
    juce::Label caption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebRtcPanel)
};
} // namespace nsp::ui
