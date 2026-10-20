#include <JuceHeader.h>
#include "modules/audio_processing/audio_buffer.h"            // ThirdParty/webrtc/shim (stand-in)
#include "modules/audio_processing/ns/noise_suppressor.h"
#include "modules/audio_processing/three_band_filter_bank.h"
#include "TestSignals.h"
#include "AllocCounter.h"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace
{
constexpr int F = 480;
constexpr int R = 48000;
constexpr int kBand = 160;

/** One channel of the WebRTC NS chain exactly as WebRtcEngine runs it (filter bank + NoiseSuppressor). */
struct Chain
{
    explicit Chain (webrtc::NsConfig::SuppressionLevel level, bool withNs = true) : useNs (withNs)
    {
        webrtc::NsConfig config;
        config.target_level = level;
        ns = std::make_unique<webrtc::NoiseSuppressor> (config, 48000, 1);
        for (size_t b = 0; b < 3; ++b)
        {
            ptrs[b] = bands[b].data();
            views[b] = std::span<float> (bands[b]);
        }
    }

    void frame (const float* in, float* out)
    {
        std::array<float, F> scaled {}, synth {};
        for (int i = 0; i < F; ++i)
            scaled[(size_t) i] = in[i] * 32768.0f;
        bank.Analysis (scaled, views);
        if (useNs)
        {
            webrtc::AudioBuffer buffer (ptrs.data(), 3, kBand);
            ns->Analyze (buffer);
            ns->Process (&buffer);
        }
        bank.Synthesis (views, synth);
        for (int i = 0; i < F; ++i)
            out[i] = synth[(size_t) i] * (1.0f / 32768.0f);
    }

    std::vector<float> run (const std::vector<float>& x)
    {
        std::vector<float> y (x.size(), 0.0f);
        for (size_t pos = 0; pos + (size_t) F <= x.size(); pos += (size_t) F)
            frame (x.data() + pos, y.data() + pos);
        return y;
    }

    bool useNs;
    webrtc::ThreeBandFilterBank bank;
    std::unique_ptr<webrtc::NoiseSuppressor> ns;
    std::array<std::array<float, kBand>, 3> bands {};
    std::array<float*, 3> ptrs {};
    std::array<std::span<float>, 3> views;
};

bool bitEqual (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

class WebRtcLibraryTests final : public juce::UnitTest
{
public:
    WebRtcLibraryTests() : juce::UnitTest ("WebRTC NS library (vendored)", "WebRtcLib") {}

    void runTest() override
    {
        beginTest ("AudioBuffer stand-in: one channel, the engine's band pointers, upstream's shape queries");
        {
            std::array<std::array<float, kBand>, 3> bands {};
            std::array<float*, 3> ptrs { bands[0].data(), bands[1].data(), bands[2].data() };
            webrtc::AudioBuffer buffer (ptrs.data(), 3, kBand);
            expectEquals ((int) buffer.num_channels(), 1);
            expectEquals ((int) buffer.num_bands(), 3);
            expectEquals ((int) buffer.num_frames_per_band(), kBand);
            expect (buffer.split_bands (0)[2] == bands[2].data());
            expect (static_cast<const webrtc::AudioBuffer&> (buffer).split_bands_const (0)[1] == bands[1].data());
        }

        beginTest ("three-band filter bank: analysis -> synthesis lags 46 samples, SNR >= 9.5 dB (upstream: ~9.5 dB)");
        {
            Chain bankOnly (webrtc::NsConfig::SuppressionLevel::k12dB, false);
            const auto x = nsp::test::whiteNoise (2 * R, 0.1f, 5);
            const auto y = bankOnly.run (x);
            const int lag = nsp::test::xcorrLag (x, y, 2000);
            double signal = 0.0, error = 0.0;
            for (int i = R / 4; i + lag < 2 * R; ++i)
            {
                const double d = (double) y[(size_t) (i + lag)] - (double) x[(size_t) i];
                error += d * d;
                signal += (double) x[(size_t) i] * (double) x[(size_t) i];
            }
            const double snr = 10.0 * std::log10 (signal / error);
            logMessage ("WebRTC filter bank: lag " + juce::String (lag) + ", reconstruction SNR " + juce::String (snr, 2) + " dB");
            expectEquals (lag, 46);
            expectGreaterOrEqual (snr, 9.5);
        }

        beginTest ("NoiseSuppressor::Reset (local patch): bit-exact with a freshly constructed suppressor, no allocation");
        {
            for (auto level : { webrtc::NsConfig::SuppressionLevel::k6dB, webrtc::NsConfig::SuppressionLevel::k21dB })
            {
                Chain used (level), fresh (level);
                used.run (nsp::test::speechLike (2 * R, 81));       // adapt the state
                const auto x = nsp::test::pinkNoise (2 * R, 0.05f, 82);

                int allocs = -1;
                {
                    nsp::test::ScopedAllocCheck check;
                    used.ns->Reset();
                    used.bank = webrtc::ThreeBandFilterBank();   // filter-bank reset: assignment, no allocation
                    allocs = check.count();
                }
                expectEquals (allocs, 0);
                expect (bitEqual (used.run (x), fresh.run (x)), "level " + juce::String ((int) level));
            }
        }

        beginTest ("NoiseSuppressor::Analyze/Process and the filter bank do not allocate");
        {
            Chain c (webrtc::NsConfig::SuppressionLevel::k18dB);
            const auto x = nsp::test::whiteNoise (20 * F, 0.1f, 83);
            std::vector<float> out ((size_t) F);
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                for (int f = 0; f < 20; ++f)
                    c.frame (x.data() + f * F, out.data());
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }
    }
};

static WebRtcLibraryTests webRtcLibraryTests;
} // namespace
