#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>
#include "../Engines/Engine.h"
#include "DelayLine.h"
#include "RingBuffer.h"

namespace nsp
{
/** The 48 kHz frame pipeline shared by all engines (audio thread, except addEngine/prepare).

    - Input is buffered per channel; every completed 480-sample frame is processed at once
      (channels in lockstep, triggered by channel 0).
    - Exactly one engine is active. Each engine's output (and residual) runs through a padding delay
      of maxExtraLatency() - its own extra latency, so the pulled stream is always the input delayed
      by maxExtraLatency().
    - Switching: the target is sampled at the start of each frame. If it differs from the active
      engine, the incoming engine is reset, pre-rolled over the last ceil(maxExtra / 480) input
      frames (output discarded) so its padded output is aligned, and both engines run for that
      frame with an equal-power crossfade (old * cos, new * sin, t = (i + 0.5) / 480). Then the
      incoming engine becomes active. Never more than two engines per frame.
    - Residual (monitorResidual): engine residual if providesResidual(), else dryDelayed - wet,
      where dryDelayed is the input delayed by maxExtraLatency(). */
class EngineHost
{
public:
    static constexpr int kMaxChannels = 2;

    void addEngine (std::unique_ptr<Engine> e);
    int  numEngines() const noexcept;
    Engine& getEngine (int index) noexcept;

    void prepare (int numChannels, int maxSamplesPerCall);
    void reset() noexcept;
    int  maxExtraLatency() const noexcept;

    void setTarget (int engineIndex, bool monitorResidual) noexcept;
    int  activeEngine() const noexcept         { return active; }
    int  enginesRunLastFrame() const noexcept  { return lastRun; }

    void updateParameters (const juce::AudioProcessorValueTreeState& s) noexcept;

    void process (const float* const* in, int numChannels, int n) noexcept;
    int  available() const noexcept;
    void pull (float* const* out, int numChannels, int n) noexcept;

private:
    static constexpr int kFrame = Engine::kFrameSize;

    struct Slot
    {
        std::unique_ptr<Engine> engine;
        std::array<DelayLine, kMaxChannels> wetPad, resPad;
    };

    void processFrame (int numCh) noexcept;
    void beginSwitch (int incoming, int numCh) noexcept;
    void runEngine (Slot& s, int ch, const float* in, float* wetOut, float* resOut) noexcept;
    void select (const Slot& s, const float* wet, const float* res, const float* dry, float* out) const noexcept;

    std::vector<std::unique_ptr<Slot>> slots;
    std::array<RingBuffer, kMaxChannels> inRing, outRing;
    std::array<DelayLine, kMaxChannels> dryDelay;
    std::array<std::vector<float>, kMaxChannels> history; // last preRollFrames input frames, oldest first

    std::array<float, kFrame> frameIn {}, dry {}, wetA {}, resA {}, wetB {}, resB {}, outA {}, outB {};
    std::array<float, kFrame> fadeIn {}, fadeOut {};

    int preparedChannels = 0, maxExtra = 0, preRollFrames = 0;
    int active = 0, requested = 0, lastRun = 1;
    bool residualSelected = false;
};
} // namespace nsp
