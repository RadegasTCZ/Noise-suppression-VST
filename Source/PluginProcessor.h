#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "Params.h"
#include "Host/DelayLine.h"
#include "Host/EngineHost.h"
#include "Host/PeakMeter.h"
#include "Host/Resampler.h"
#include "Host/RingBuffer.h"
#include "Host/SafetyLimiter.h"

namespace nsp { class SnsEngine; class RnnoiseEngine; class DfnEngine; }

/** Host layer: host-rate resampling, dry delay, bypass, meters. The 48 kHz frame pipeline
    lives in nsp::EngineHost. Latency (set in prepareToPlay only):
      48 kHz : 480 (output FIFO prefill) + maxExtraLatency (1440, DeepFilterNet) = 1920
      other  : in.inputLatency + P + out.outputLatency + maxExtra * host / 48000,
               P = ceil (480 * host / 48000) + 2 (one sample of underrun margin) */
class NoiseSuppressionProcessor : public juce::AudioProcessor
{
public:
    NoiseSuppressionProcessor();
    ~NoiseSuppressionProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                 { return true; }

    const juce::String getName() const override                     { return "Noise Suppression"; }
    bool acceptsMidi() const override                               { return false; }
    bool producesMidi() const override                              { return false; }
    bool isMidiEffect() const override                              { return false; }
    double getTailLengthSeconds() const override                    { return 0.0; }

    int getNumPrograms() override                                   { return 1; }
    int getCurrentProgram() override                                { return 0; }
    void setCurrentProgram (int) override                           {}
    const juce::String getProgramName (int) override                { return {}; }
    void changeProgramName (int, const juce::String&) override      {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState apvts;
    nsp::PeakHold inputPeak, outputPeak;

    float getSnsNoiseFloorDb() const noexcept;
    /** True if the RNNoise engine tried to load `model` (RnnoiseEngine::Model) and failed. Any thread. */
    bool  isRnnoiseModelFailed (int model) const noexcept;
    /** True if the DeepFilterNet engine tried to load its model and failed. Any thread. */
    bool  isDfnModelFailed() const noexcept;
    bool  isSampleRateSupported() const noexcept   { return rateSupported.load (std::memory_order_relaxed); }
    nsp::EngineHost& getEngineHost() noexcept      { return host; }
    int   getUnderrunCount() const noexcept        { return underruns.load (std::memory_order_relaxed); }

private:
    static constexpr int kMaxChannels = nsp::EngineHost::kMaxChannels;
    static constexpr int kBypassRampSamples = 480;

    void processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept;
    void processChunk (float* const* io, int numCh, int n, bool bypass, bool limit) noexcept;

    nsp::EngineHost host;
    nsp::SafetyLimiter limiter;          // on the processed path only (wet or residual), never on bypassed dry
    nsp::SnsEngine* sns = nullptr;          // owned by host
    nsp::RnnoiseEngine* rnnoise = nullptr;  // owned by host
    nsp::DfnEngine* dfn = nullptr;          // owned by host

    std::array<nsp::Resampler, kMaxChannels> inResampler, outResampler;
    std::array<nsp::RingBuffer, kMaxChannels> outRing;   // host-rate wet/residual FIFO
    std::array<nsp::DelayLine, kMaxChannels> dryDelay;   // host-rate dry, exactly `latency`
    std::array<std::vector<float>, kMaxChannels> dryBuf, wetBuf, in48, out48, hostOut;
    std::array<std::vector<float>, kMaxChannels> spareIo; // stands in for channels the host buffer lacks
    std::vector<float> bypassRamp;

    int bypassCount = 0;
    int preparedBlock = 0, hostRate = 48000, latency = 0;
    int hostChannels = kMaxChannels; // EngineHost/resampler channel count; every chunk feeds exactly this many

    std::atomic<bool> rateSupported { true };
    std::atomic<int> underruns { 0 };

    std::atomic<float>* engineParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* monitorParam = nullptr;
    std::atomic<float>* limiterParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseSuppressionProcessor)
};
