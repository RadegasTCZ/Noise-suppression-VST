// Source/UI/RnnoisePanel.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EnginePanel.h"
#include "SegmentedSelector.h"
#include <atomic>
#include <functional>

namespace nsp::ui
{
/** RNNoise: two stacked 62 x 20 pills "Standard" / "Light" bound to rnnModel,
    caption "Model". Readout: "Model failed to load" (a warning) while the selected model's blob
    failed to load (the editor passes processor.isRnnoiseModelFailed; tests pass a stub), else empty. */
class RnnoisePanel final : public EnginePanel
{
public:
    static constexpr int kPillWidth = 62, kPillHeight = 20, kGap = 4, kSelectorTop = 7;

    RnnoisePanel (juce::AudioProcessorValueTreeState& apvts, std::function<bool (int model)> modelFailed);
    juce::String readout() const override;
    bool readoutIsWarning() const override;
    void resized() override;

private:
    bool selectedModelFailed() const;

    std::function<bool (int)> failed;
    std::atomic<float>* modelParam = nullptr;
    SegmentedSelector models;
    juce::Label caption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RnnoisePanel)
};
} // namespace nsp::ui
