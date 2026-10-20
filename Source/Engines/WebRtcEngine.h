#pragma once

#include "Engine.h"
#include "VariantCrossfade.h"
#include <array>
#include <memory>
#include <span>

namespace nsp
{
/** WebRTC NS (revision b58dcf8fec, vendored in ThirdParty/webrtc) at 48 kHz / 480, as WebRTC's
    audio processing module runs it: per channel and frame, ThreeBandFilterBank analysis -> 3 x 160
    samples at 16 kHz -> NoiseSuppressor::Analyze + Process (low band analysed, gain applied to all
    bands) -> synthesis. Data is handed over in the int16 range (x32768 / x1/32768), like APM's float
    path. Channels are independent (one suppressor per channel, num_channels = 1).
    The suppression level is fixed per NoiseSuppressor, so prepare() builds one chain per
    (level x channel); only the selected level runs and a level change crossfades inside the engine
    (VariantCrossfade). No residual output. */
class WebRtcEngine final : public Engine
{
public:
    enum Level { k6dB = 0, k12dB = 1, k18dB = 2, k21dB = 3, kNumLevels = 4 };   // = webrtcLevel choice index

    /** Filter bank (46: analysis + synthesis prototype delay) + NS overlap (96 samples at 16 kHz = 288).
        Test-verified ("latency"). */
    static constexpr int kLatency = 46 + 3 * 96;

    WebRtcEngine();
    ~WebRtcEngine() override;

    const char* id() const override { return "webrtc"; }
    void prepare (int numChannels) override;
    void reset() override;
    void processFrame (int channel, const float* in, float* out) override;
    int  extraLatencySamples() const override { return kLatency; }
    void updateParameters (const juce::AudioProcessorValueTreeState& state) override;

    /** Level index (k6dB..k21dB, clamped). Takes effect at the next frame start, crossfaded. */
    void setLevel (int level) noexcept;
    int  activeLevel() const noexcept        { return xfade.activeVariant(); }
    int  levelsRunLastFrame() const noexcept { return xfade.variantsRunLastFrame(); }

private:
    static constexpr int kMaxChannels = VariantCrossfade::kMaxChannels;
    static constexpr int kBands = 3, kBandSize = 160;
    static constexpr int kPreRollFrames = (kLatency + kFrameSize - 1) / kFrameSize;

    struct Chain;   // ThreeBandFilterBank + NoiseSuppressor (WebRtcEngine.cpp)

    void resetLevel (int level) noexcept;
    void runLevel (int level, int channel, const float* in, float* out) noexcept;

    std::array<std::array<std::unique_ptr<Chain>, kMaxChannels>, kNumLevels> chains;
    VariantCrossfade xfade;
    std::array<std::array<float, kBandSize>, kBands> bands {};
    std::array<float*, kBands> bandPtrs {};
    std::array<std::span<float>, kBands> bandViews;
    std::array<float, kFrameSize> scaled {}, synthesis {};
    int numChannels = 0;

    JUCE_DECLARE_NON_COPYABLE (WebRtcEngine)
};
} // namespace nsp
