#pragma once
#include <JuceHeader.h>

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
    float getNoiseSuppress() const;
    float getProbStart() const;
    float getProbContinue() const;
    bool getEnableDenoise() const { return apvts.getRawParameterValue("enableDenoise")->load() > 0.5f; }

    juce::AudioProcessorValueTreeState apvts;
    
    SpeexPreprocessState* speexStates[2];

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int speexFrameSize = 480; // default value; will be set in prepareToPlay
    std::vector<float> channelInputBuffers[2];  // For input buffering
    std::vector<float> channelOutputFIFOs[2];   // For output draining
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpeexDSPNoiseSuppressorAudioProcessor)
};
