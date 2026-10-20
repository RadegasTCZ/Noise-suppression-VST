#pragma once

#include "Engine.h"
#include "sns/HybridDenoise.h"
#include <array>
#include <atomic>
#include <cmath>

namespace nsp
{
/** SNS v2.4.0 HybridDenoise at 48 kHz / 480 with an exact residual ((1 - gain) * X). */
class SnsEngine final : public Engine
{
public:
    SnsEngine() = default;
    ~SnsEngine() override;

    const char* id() const override { return "sns"; }
    void prepare (int numChannels) override;
    void reset() override;
    void processFrame (int channel, const float* in, float* out) override;
    int  extraLatencySamples() const override { return kFrameSize; }   // 960-sample window overlap-add; test-verified
    bool providesResidual() const override { return true; }
    void getResidual (int channel, float* residualOut) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& state) override;

    void  setSuppressDb (float db) noexcept { suppressDb = std::isfinite (db) ? db : -20.0f; }   // applied on the next processFrame
    float noiseFloorDb() const noexcept     { return noiseFloor.load (std::memory_order_relaxed); }

private:
    static constexpr int kMaxChannels = 2;
    static constexpr float kInitialFloorDb = -90.0f;

    void destroyStates() noexcept;
    static void resetState (HybridDenoiseState& st) noexcept;

    HybridDenoiseState states[kMaxChannels] = {};
    std::array<std::array<float, kFrameSize>, kMaxChannels> residual {};
    int numChannels = 0;
    float suppressDb = -20.0f;
    std::atomic<float> noiseFloor { kInitialFloorDb };

    JUCE_DECLARE_NON_COPYABLE (SnsEngine)
};
} // namespace nsp
