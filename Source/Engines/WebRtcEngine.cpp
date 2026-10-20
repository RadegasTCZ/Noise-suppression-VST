#include "WebRtcEngine.h"
#include <algorithm>
#include <utility>
#include "../Params.h"
#include "modules/audio_processing/audio_buffer.h"          // ThirdParty/webrtc/shim: minimal stand-in
#include "modules/audio_processing/ns/noise_suppressor.h"
#include "modules/audio_processing/three_band_filter_bank.h"

namespace nsp
{
struct WebRtcEngine::Chain
{
    webrtc::ThreeBandFilterBank bank;
    std::unique_ptr<webrtc::NoiseSuppressor> ns;
};

static_assert (webrtc::ThreeBandFilterBank::kFullBandSize == Engine::kFrameSize, "one engine frame = one filter-bank frame");
static_assert (webrtc::ThreeBandFilterBank::kSplitBandSize == 160 && webrtc::kNsFrameSize == 160, "16 kHz bands of 10 ms");

WebRtcEngine::WebRtcEngine()
{
    static_assert (kPreRollFrames == 1, "WebRTC latency is below one frame");
    xfade.setRequested (k12dB);   // the webrtcLevel parameter's default
    for (size_t b = 0; b < (size_t) kBands; ++b)
    {
        bandPtrs[b] = bands[b].data();
        bandViews[b] = std::span<float> (bands[b]);
    }
}

WebRtcEngine::~WebRtcEngine() = default;

void WebRtcEngine::prepare (int channels)
{
    jassert (channels >= 1 && channels <= kMaxChannels);
    numChannels = juce::jlimit (1, kMaxChannels, channels);

    for (int level = 0; level < kNumLevels; ++level)
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            webrtc::NsConfig config;
            config.target_level = static_cast<webrtc::NsConfig::SuppressionLevel> (level);   // same order
            auto chain = std::make_unique<Chain>();
            chain->ns = std::make_unique<webrtc::NoiseSuppressor> (config, (size_t) kSampleRate, (size_t) 1);
            chains[(size_t) level][(size_t) ch] = std::move (chain);
        }

    xfade.prepare (numChannels, kPreRollFrames);
    reset();
}

void WebRtcEngine::resetLevel (int level) noexcept
{
    for (auto& chain : chains[(size_t) level])
    {
        if (chain == nullptr)
            continue;
        chain->bank = webrtc::ThreeBandFilterBank();   // fixed-size arrays: no allocation
        chain->ns->Reset();                             // local patch, no allocation (ThirdParty/UPSTREAM.md)
    }
}

void WebRtcEngine::reset()
{
    xfade.reset();                       // adopts the requested level without a crossfade
    resetLevel (xfade.activeVariant());
}

void WebRtcEngine::runLevel (int level, int ch, const float* in, float* out) noexcept
{
    auto& chain = *chains[(size_t) level][(size_t) ch];

    for (int i = 0; i < kFrameSize; ++i)
        scaled[(size_t) i] = in[i] * 32768.0f;   // APM hands NS data in the int16 range (FloatToFloatS16)

    chain.bank.Analysis (scaled, bandViews);

    webrtc::AudioBuffer buffer (bandPtrs.data(), (size_t) kBands, (size_t) kBandSize);
    chain.ns->Analyze (buffer);
    chain.ns->Process (&buffer);

    chain.bank.Synthesis (bandViews, synthesis);

    for (int i = 0; i < kFrameSize; ++i)
        out[i] = synthesis[(size_t) i] * (1.0f / 32768.0f);
}

void WebRtcEngine::processFrame (int channel, const float* in, float* out)
{
    jassert (channel >= 0 && channel < numChannels);
    if (channel < 0 || channel >= numChannels)
    {
        std::fill (out, out + kFrameSize, 0.0f);
        return;
    }

    xfade.process (channel, in, out,
                   [this] (int level) { resetLevel (level); },
                   [this] (int level, int ch, const float* x, float* y) { runLevel (level, ch, x, y); });
}

void WebRtcEngine::setLevel (int level) noexcept
{
    xfade.setRequested (juce::jlimit (0, kNumLevels - 1, level));
}

void WebRtcEngine::updateParameters (const juce::AudioProcessorValueTreeState& s)
{
    if (auto* p = s.getRawParameterValue (ids::webrtcLevel))   // nullptr if the state has no such parameter
        setLevel (juce::roundToInt (p->load (std::memory_order_relaxed)));
}
} // namespace nsp
