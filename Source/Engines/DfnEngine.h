// Uses the DeepFilterNet3 port in Source/Engines/dfn (libDF, Hendrik Schroeter, MIT OR Apache-2.0).
#pragma once

#include "Engine.h"
#include "VariantCrossfade.h"
#include "dfn/DfnDenoiser.h"
#include "dfn/DfnModel.h"
#include "dfn/DfnNetwork.h"
#include "../Host/DelayLine.h"
#include <array>
#include <atomic>
#include <memory>

namespace nsp
{
/** DeepFilterNet3 (native C++ port) at 48 kHz / 480: one Denoiser per channel sharing one Model + Network,
    loaded once in the first prepare(). Strength presets map to the attenuation limit and post-filter
    beta; a change ramps over Denoiser::kRampHops hops inside the denoiser (no crossfade needed).
    If the model fails to load, audio passes through delayed by the engine latency. No residual output. */
class DfnEngine final : public Engine
{
public:
    enum Strength { kGentle = 0, kNormal = 1, kStrong = 2, kNumStrengths = 3 };
    struct Preset { float attenLimDb, postFilterBeta; };
    static constexpr Preset kPresets[kNumStrengths] { { 12.0f, 0.0f }, { 24.0f, 0.0f }, { 80.0f, 0.02f } };
    static constexpr int kLatency = dfn::Denoiser::kLatency;   // 1440

    struct Blob
    {
        const void* data = nullptr;
        int size = 0;
    };

    DfnEngine();                  // the embedded blob (BinaryData::dfn3_standard_bin)
    explicit DfnEngine (Blob);    // tests: e.g. a corrupt blob; copied into the model in prepare()

    const char* id() const override { return "dfn"; }
    void prepare (int numChannels) override;
    void reset() override;
    void processFrame (int channel, const float* in, float* out) override;
    int  extraLatencySamples() const override { return kLatency; }
    void updateParameters (const juce::AudioProcessorValueTreeState& state) override;

    /** Strength index (clamped). Ramped over Denoiser::kRampHops hops; reset() adopts it immediately. */
    void setStrength (int strength) noexcept;
    int  strength() const noexcept { return requested; }
    /** True only after prepare() tried to load the model and failed; a failed load is permanent for the
        instance (as RNNoise). Any thread. */
    bool modelFailed() const noexcept { return failed.load (std::memory_order_relaxed); }

    /** Tests only: the safety floor is always on in the engine (prepare() turns it on); this switches it on
        the prepared denoisers so a test can compare against the raw output. Re-enabled by the next prepare(). */
    void setSafetyFloorForTest (bool on) noexcept;

private:
    static constexpr int kMaxChannels = VariantCrossfade::kMaxChannels;

    void loadModel();

    Blob source;
    dfn::Network network;          // must not move once the denoisers are prepared
    bool attempted = false, loaded = false;
    std::atomic<bool> failed { false };
    std::array<std::unique_ptr<dfn::Denoiser>, kMaxChannels> denoiser;
    std::array<DelayLine, kMaxChannels> passthrough;   // failed model: delayed dry
    int requested = kNormal;
    int numChannels = 0;

    JUCE_DECLARE_NON_COPYABLE (DfnEngine)
};
} // namespace nsp
