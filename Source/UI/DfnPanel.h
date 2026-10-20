// Source/UI/DfnPanel.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EnginePanel.h"
#include "SegmentedSelector.h"
#include <functional>

namespace nsp::ui
{
/** DeepFilterNet: three stacked 62 x 16 pills "Gentle" / "Normal" / "Strong" bound to dfnStrength,
    caption "Strength". Readout: "Model failed to load" (a warning) while the model blob failed to load
    (the editor passes processor.isDfnModelFailed; tests pass a stub), else empty. */
class DfnPanel final : public EnginePanel
{
public:
    static constexpr int kPillWidth = 62, kPillHeight = 16, kGap = 3, kSelectorTop = 2;   // 3 x 16 + 2 x 3 = 54: rows 2..56, caption at 58

    DfnPanel (juce::AudioProcessorValueTreeState& apvts, std::function<bool()> modelFailed);
    juce::String readout() const override;
    bool readoutIsWarning() const override;
    void resized() override;

private:
    bool modelHasFailed() const { return failed != nullptr && failed(); }

    std::function<bool()> failed;
    SegmentedSelector strength;
    juce::Label caption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DfnPanel)
};
} // namespace nsp::ui
