#pragma once

#include "Engine.h"
#include <array>

struct SpeexPreprocessState_;   // SpeexDSP opaque state (speex/speex_preprocess.h)

namespace nsp
{
/** speex_preprocess at 48 kHz / 480 samples, float -> int16 -> float like v1.1.2.
    Denoise on; VAD, AGC and dereverb off; NOISE_SUPPRESS = classicSuppress. */
class ClassicSpeexEngine final : public Engine
{
public:
    ClassicSpeexEngine() = default;
    ~ClassicSpeexEngine() override;

    const char* id() const override { return "classic"; }
    void prepare (int numChannels) override;
    void reset() override;
    void processFrame (int channel, const float* in, float* out) override;
    int  extraLatencySamples() const override { return kFrameSize; }   // speex_preprocess's overlap-add delays by one frame; test-verified
    void updateParameters (const juce::AudioProcessorValueTreeState& state) override;

    /** Noise suppression in dB (negative, e.g. -20). Applied to live states immediately. */
    void setSuppressDb (int db) noexcept;

private:
    static constexpr int kMaxChannels = 2;

    void destroyStates() noexcept;

    std::array<::SpeexPreprocessState_*, kMaxChannels> states {};
    std::array<short, kFrameSize> frame16 {};   // spx_int16_t scratch, shared (frames run sequentially)
    int numChannels = 0;
    int suppressDb = -20;

    JUCE_DECLARE_NON_COPYABLE (ClassicSpeexEngine)
};
} // namespace nsp
