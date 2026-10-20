#include <juce_audio_processors/juce_audio_processors.h>
#include "Render.h"
#include "../Source/Params.h"
#include "TestSignals.h"
#include <cmath>

namespace nsp
{
class RenderTests final : public juce::UnitTest
{
public:
    RenderTests() : juce::UnitTest ("Listening render alignment", "Render") {}

    void runTest() override
    {
        for (double fs : { 48000.0, 44100.0 })
            for (int ch : { 1, 2 })
            {
                beginTest ("bypass render equals the input sample-for-sample (" + juce::String (fs, 0) + " Hz, "
                           + juce::String (ch) + " ch): latency is compensated");
                juce::AudioBuffer<float> in (ch, (int) fs);              // 1 s of noise
                juce::Random r (42);
                for (int c = 0; c < ch; ++c)
                    for (int i = 0; i < in.getNumSamples(); ++i)
                        in.setSample (c, i, 0.25f * (r.nextFloat() * 2.0f - 1.0f));
                juce::AudioBuffer<float> out;
                expect (test::renderBuffer (in, fs, kEngineSns, out, nsp::ids::bypass, 1.0f));
                expectEquals (out.getNumChannels(), ch);
                expectEquals (out.getNumSamples(), in.getNumSamples());
                float maxErr = 0.0f;
                for (int c = 0; c < ch; ++c)
                    for (int i = 480; i < in.getNumSamples(); ++i)     // past the 480-sample bypass ramp
                        maxErr = juce::jmax (maxErr, std::abs (out.getSample (c, i) - in.getSample (c, i)));
                expectLessThan (maxErr, 1.0e-6f);
            }

        beginTest ("full-wet render has the input's length and is not silent");
        {
            juce::AudioBuffer<float> in (1, 48000);
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (0, i, 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / 48000.0f));
            juce::AudioBuffer<float> out;
            expect (test::renderBuffer (in, 48000.0, kEngineClassic, out));
            expectEquals (out.getNumSamples(), in.getNumSamples());
            expectGreaterThan (out.getMagnitude (0, 24000, 24000), 0.01f);
        }

        beginTest ("WebRTC NS / RNNoise targets: WebRTC and RNNoise Standard / Light render aligned, non-silent; Light differs from Standard");
        {
            const auto speech = test::speechLike (48000, 111);
            juce::AudioBuffer<float> in (1, 48000);
            in.copyFrom (0, 0, speech.data(), 48000);
            juce::AudioBuffer<float> webrtc, standard, light;
            expect (test::renderBuffer (in, 48000.0, kEngineWebRtc, webrtc));
            expect (test::renderBuffer (in, 48000.0, kEngineRnnoise, standard, ids::rnnModel, 0.0f));
            expect (test::renderBuffer (in, 48000.0, kEngineRnnoise, light, ids::rnnModel, 1.0f));
            for (auto* out : { &webrtc, &standard, &light })
            {
                expectEquals (out->getNumSamples(), 48000);
                expectGreaterThan (out->getMagnitude (0, 24000, 24000), 0.01f);
            }
            float diff = 0.0f;
            for (int i = 0; i < 48000; ++i)
                diff = juce::jmax (diff, std::abs (standard.getSample (0, i) - light.getSample (0, i)));
            expectGreaterThan (diff, 1.0e-3f);
        }

        beginTest ("DeepFilterNet Gentle / Normal / Strong render aligned and non-silent; Strong differs from Gentle");
        {
            const auto speech = test::speechLike (48000, 111);
            juce::AudioBuffer<float> in (1, 48000);
            in.copyFrom (0, 0, speech.data(), 48000);
            juce::AudioBuffer<float> gentle, normal, strong;
            expect (test::renderBuffer (in, 48000.0, kEngineDfn, gentle, ids::dfnStrength, 0.0f));
            expect (test::renderBuffer (in, 48000.0, kEngineDfn, normal, ids::dfnStrength, 1.0f));
            expect (test::renderBuffer (in, 48000.0, kEngineDfn, strong, ids::dfnStrength, 2.0f));
            for (auto* out : { &gentle, &normal, &strong })
            {
                expectEquals (out->getNumSamples(), 48000);
                expectGreaterThan (out->getMagnitude (0, 24000, 24000), 0.01f);
            }
            float diff = 0.0f;
            for (int i = 0; i < 48000; ++i)
                diff = juce::jmax (diff, std::abs (strong.getSample (0, i) - gentle.getSample (0, i)));
            expectGreaterThan (diff, 1.0e-3f);
        }

        beginTest ("more than two channels is refused");
        {
            juce::AudioBuffer<float> in (3, 480), out;
            in.clear();
            expect (! test::renderBuffer (in, 48000.0, kEngineClassic, out));
        }
    }
};

static RenderTests renderTests;
} // namespace nsp
