#pragma once

#include "Engine.h"
#include "VariantCrossfade.h"
#include "../Host/DelayLine.h"
#include <array>
#include <atomic>
#include <vector>

struct DenoiseState;   // RNNoise opaque types (rnnoise.h)
struct RNNModel;

namespace nsp
{
/** RNNoise (xiph, commit 70f1d25, model 0a8755f8) at 48 kHz / 480: Standard and Light models
    Both models are loaded once from their blobs in the first prepare(); one
    DenoiseState per (model x channel); only the selected model runs. A model change crossfades
    inside the engine (VariantCrossfade). Input is scaled x32768 (RNNoise expects the int16
    range), output x1/32768; the voice probability is ignored. No residual output.
    A model whose blob fails to load passes audio through, delayed by the engine latency. */
class RnnoiseEngine final : public Engine
{
public:
    enum Model { kStandard = 0, kLight = 1, kNumModels = 2 };

    /** rnnoise_process_frame applies its gains to the previous frame's spectrum (delayed_X) on top of
        the 960-sample window overlap-add: two frames. Test-verified ("latency"). */
    static constexpr int kLatency = 2 * kFrameSize;

    struct Blob
    {
        const void* data = nullptr;
        int size = 0;
    };

    RnnoiseEngine();                          // the embedded blobs (BinaryData)
    RnnoiseEngine (Blob standard, Blob light);  // tests: e.g. a corrupt blob; copied in prepare()
    ~RnnoiseEngine() override;

    const char* id() const override { return "rnnoise"; }
    void prepare (int numChannels) override;
    void reset() override;
    void processFrame (int channel, const float* in, float* out) override;
    int  extraLatencySamples() const override { return kLatency; }
    void updateParameters (const juce::AudioProcessorValueTreeState& state) override;

    /** Model index (kStandard / kLight, clamped). Takes effect at the next frame start, crossfaded. */
    void setModel (int model) noexcept;
    int  activeModel() const noexcept        { return xfade.activeVariant(); }
    int  modelsRunLastFrame() const noexcept { return xfade.variantsRunLastFrame(); }
    /** True only after prepare() tried to load `model` and failed (corrupt blob). Any thread. */
    bool modelFailed (int model) const noexcept;

    /** Tests only: the safety floor is always on in the engine (loadModels() enables it on the pristine
        snapshots, so every reset carries it); this switches it on all states and the snapshots so a test can
        compare against the raw output. Call after prepare(). */
    void setSafetyFloorForTest (bool on) noexcept;

private:
    static constexpr int kMaxChannels = VariantCrossfade::kMaxChannels;
    static constexpr int kPreRollFrames = (kLatency + kFrameSize - 1) / kFrameSize;

    void loadModels();
    void resetModel (int model) noexcept;
    void runModel (int model, int channel, const float* in, float* out) noexcept;
    DenoiseState* stateOf (int model, int channel) noexcept;

    std::array<Blob, kNumModels> source;
    std::array<std::vector<unsigned char>, kNumModels> blob;      // owned copy (operator new: 16-byte aligned)
    std::array<RNNModel*, kNumModels> model {};
    std::array<bool, kNumModels> loaded {};
    std::array<std::atomic<bool>, kNumModels> failed {};
    std::array<std::vector<unsigned char>, kNumModels> pristine;  // DenoiseState right after rnnoise_init
    std::array<std::array<std::vector<unsigned char>, kMaxChannels>, kNumModels> state;
    std::array<std::array<DelayLine, kMaxChannels>, kNumModels> passthrough;   // failed model: delayed dry
    bool attempted = false;

    VariantCrossfade xfade;
    std::array<float, kFrameSize> scaledIn {}, scaledOut {};
    int numChannels = 0;

    JUCE_DECLARE_NON_COPYABLE (RnnoiseEngine)
};
} // namespace nsp
