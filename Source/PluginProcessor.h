#pragma once
#include <JuceHeader.h>
#include <deque>

extern "C" {
    #include "../ThirdParty/speexdsp/speex/speex_preprocess.h"
}

class SpeexDSPNoiseSuppressorAudioProcessor : public juce::AudioProcessor
{
public:
    SpeexDSPNoiseSuppressorAudioProcessor();
    ~SpeexDSPNoiseSuppressorAudioProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // Required overrides for JUCE
    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Parameter accessors
    bool getEnableDenoise() const;
    bool getEnableVAD() const;
    float getNoiseSuppress() const;
    float getProbStart() const;
    float getProbContinue() const;

    // ---- Soft gate state ----
    float gateGain[2] = { 1.0f, 1.0f }; // per-channel smoothed gain
    float gateFloor = 0.1f; // drop audio to -20 dB when VAD says "no speech"
    float gateAttack = 0.0f; // computed from sample rate
    float gateRelease = 0.0f; // computed from sample rate

    juce::AudioProcessorValueTreeState apvts;
    SpeexPreprocessState* speexStates[2];

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int speexFrameSize = 480; // default value; will be set in prepareToPlay

    // Input/Output buffers (deque for O(1) pop_front)
    std::deque<float> channelInputBuffers[2];
    std::deque<float> channelOutputFIFOs[2];

    // Reusable per-channel int16 frame (avoid per-frame allocations)
    std::vector<spx_int16_t> frameI16[2];
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpeexDSPNoiseSuppressorAudioProcessor)
};
