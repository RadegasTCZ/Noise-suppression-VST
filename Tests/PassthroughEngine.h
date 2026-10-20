#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstring>
#include "../Source/Engines/Engine.h"
#include "../Source/Host/DelayLine.h"

namespace nsp::test
{
/** Test engine. Output = input delayed by `extraLatency` samples (its extraLatencySamples()).
    With withResidual = true it also provides a residual equal to 0.5 * its (unpadded) output,
    so tests can tell engine residual from the host's difference residual. */
class PassthroughEngine final : public nsp::Engine
{
public:
    explicit PassthroughEngine (int extraLatency, bool withResidual = false)
        : extra (extraLatency), residual (withResidual) {}

    const char* id() const override { return "pass"; }

    void prepare (int numChannels) override
    {
        jassert (numChannels >= 1 && numChannels <= kMaxCh);
        juce::ignoreUnused (numChannels);

        for (auto& d : delays)
        {
            d.prepare (juce::jmax (1, extra));
            d.setDelay (extra);
        }

        reset();
    }

    void reset() override
    {
        for (auto& d : delays)
            d.reset();

        for (auto& frame : lastOut)
            frame.fill (0.0f);
    }

    void processFrame (int channel, const float* in, float* out) override
    {
        delays[(size_t) channel].process (in, out, kFrameSize);
        std::memcpy (lastOut[(size_t) channel].data(), out, sizeof (float) * (size_t) kFrameSize);
    }

    int extraLatencySamples() const override { return extra; }

    bool providesResidual() const override { return residual; }

    void getResidual (int channel, float* residualOut) override
    {
        const auto& frame = lastOut[(size_t) channel];

        for (int i = 0; i < kFrameSize; ++i)
            residualOut[i] = 0.5f * frame[(size_t) i];
    }

    void updateParameters (const juce::AudioProcessorValueTreeState&) override {}

private:
    static constexpr int kMaxCh = 2;
    const int extra;
    const bool residual;
    std::array<nsp::DelayLine, kMaxCh> delays;
    std::array<std::array<float, kFrameSize>, kMaxCh> lastOut {};
};
} // namespace nsp::test
