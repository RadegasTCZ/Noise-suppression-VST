#pragma once

#include <JuceHeader.h>

namespace nsp
{
/** A noise-suppression engine running at 48 kHz on 480-sample frames.
    prepare() runs on the message thread (may allocate); everything else runs on the
    audio thread and must not allocate, lock or log. */
class Engine
{
public:
    virtual ~Engine() = default;

    static constexpr int kSampleRate = 48000;
    static constexpr int kFrameSize  = 480;

    virtual const char* id() const = 0;                     // "classic", "sns", ...
    virtual void prepare (int numChannels) = 0;             // allocate everything; called from prepareToPlay
    virtual void reset() = 0;                               // clear adaptive state; no allocation
    virtual void processFrame (int channel, const float* in, float* out) = 0;  // kFrameSize samples
    virtual int  extraLatencySamples() const = 0;           // delay beyond the shared 480-frame buffering, at 48 kHz
    virtual bool providesResidual() const { return false; } // true -> engine fills residual exactly
    virtual void getResidual (int channel, float* residualOut) { juce::ignoreUnused (channel, residualOut); }
    virtual void updateParameters (const juce::AudioProcessorValueTreeState&) = 0; // once per block
};
} // namespace nsp
