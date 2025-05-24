#include "PluginProcessor.h"
#include "PluginEditor.h"

SpeexDSPNoiseSuppressorAudioProcessor::SpeexDSPNoiseSuppressorAudioProcessor()
: juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
    .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
  apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (int ch = 0; ch < 2; ++ch)
        speexStates[ch] = nullptr;
}

SpeexDSPNoiseSuppressorAudioProcessor::~SpeexDSPNoiseSuppressorAudioProcessor()
{
    for (int ch = 0; ch < 2; ++ch)
        if (speexStates[ch])
            speex_preprocess_state_destroy(speexStates[ch]);
}

juce::AudioProcessorValueTreeState::ParameterLayout SpeexDSPNoiseSuppressorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noisesuppress", "Noise Suppress (dB)", juce::NormalisableRange<float>(-40.0f, -5.0f, 1.0f, 1.0f, true), -30.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "probstart", "Noise Learn Aggressiveness", juce::NormalisableRange<float>(0.5f, 1.0f, 0.01f, 1.0f, true), 0.60f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "probcontinue", "Noise Adaptation Rate", juce::NormalisableRange<float>(0.5f, 1.0f, 0.01f, 1.0f, true), 0.95f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "enableDenoise", "Enable Noise Suppression", true));
    return { params.begin(), params.end() };
}

float SpeexDSPNoiseSuppressorAudioProcessor::getNoiseSuppress() const {
    return apvts.getRawParameterValue("noisesuppress")->load();
}
float SpeexDSPNoiseSuppressorAudioProcessor::getProbStart() const {
    return apvts.getRawParameterValue("probstart")->load();
}
float SpeexDSPNoiseSuppressorAudioProcessor::getProbContinue() const {
    return apvts.getRawParameterValue("probcontinue")->load();
}

void SpeexDSPNoiseSuppressorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const double frameDurationMs = 10.0; // You can adjust this (Speex default is 10 ms)
    speexFrameSize = static_cast<int>(sampleRate * frameDurationMs / 1000.0);

    for (int ch = 0; ch < 2; ++ch)
    {
        channelInputBuffers[ch].clear();
        channelOutputFIFOs[ch].clear();
        if (speexStates[ch])
            speex_preprocess_state_destroy(speexStates[ch]);
        speexStates[ch] = speex_preprocess_state_init(speexFrameSize, (int)sampleRate);

        float noiseSuppress = getNoiseSuppress();
        float probStart = getProbStart();
        float probContinue = getProbContinue();
        int noiseSuppressInt = static_cast<int>(noiseSuppress);
        int enableDenoise = getEnableDenoise() ? 1 : 0;

        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppressInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_START, &probStart);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_CONTINUE, &probContinue);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_DENOISE, &enableDenoise);
    }
}

void SpeexDSPNoiseSuppressorAudioProcessor::releaseResources()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        channelInputBuffers[ch].clear();
        channelOutputFIFOs[ch].clear();
        if (speexStates[ch])
            speex_preprocess_state_destroy(speexStates[ch]);
        speexStates[ch] = nullptr;
    }
}

void SpeexDSPNoiseSuppressorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float noiseSuppress = getNoiseSuppress();
        float probStart = getProbStart();
        float probContinue = getProbContinue();
        int noiseSuppressInt = static_cast<int>(noiseSuppress);
        int enableDenoise = getEnableDenoise() ? 1 : 0;

        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppressInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_START, &probStart);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_CONTINUE, &probContinue);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_DENOISE, &enableDenoise);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* channelData = buffer.getWritePointer(ch);
        const float* inputData = buffer.getReadPointer(ch);

        // 1. Append all input samples to the input buffer
        channelInputBuffers[ch].insert(channelInputBuffers[ch].end(), inputData, inputData + numSamples);

        // 2. While we have enough input for a full frame, process it
        while (channelInputBuffers[ch].size() >= (size_t)speexFrameSize)
        {
            std::vector<spx_int16_t> frame(speexFrameSize);
            for (int j = 0; j < speexFrameSize; ++j)
                frame[j] = static_cast<spx_int16_t>(juce::jlimit(-32768, 32767,
                    static_cast<int>(channelInputBuffers[ch][j] * 32768.0f)));

            speex_preprocess_run(speexStates[ch], frame.data());

            // Append processed frame to the output FIFO
            for (int j = 0; j < speexFrameSize; ++j)
                channelOutputFIFOs[ch].push_back(frame[j] / 32768.0f);

            // Remove processed samples from input buffer
            channelInputBuffers[ch].erase(channelInputBuffers[ch].begin(), channelInputBuffers[ch].begin() + speexFrameSize);
        }

        // 3. Now, drain exactly numSamples from the output FIFO to channelData
        int samplesWritten = 0;
        while (samplesWritten < numSamples && !channelOutputFIFOs[ch].empty())
        {
            channelData[samplesWritten++] = channelOutputFIFOs[ch].front();
            channelOutputFIFOs[ch].erase(channelOutputFIFOs[ch].begin());
        }

        // 4. If we run out of processed samples, output DRY input for the remainder of this block
        while (samplesWritten < numSamples)
            channelData[samplesWritten++] = inputData[samplesWritten - 1];
    }
}

juce::AudioProcessorEditor* SpeexDSPNoiseSuppressorAudioProcessor::createEditor()
{
    return new SpeexDSPNoiseSuppressorAudioProcessorEditor(*this);
}

bool SpeexDSPNoiseSuppressorAudioProcessor::hasEditor() const { return true; }
const juce::String SpeexDSPNoiseSuppressorAudioProcessor::getName() const { return "SpeexDSP Noise Suppressor"; }

void SpeexDSPNoiseSuppressorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpeexDSPNoiseSuppressorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpeexDSPNoiseSuppressorAudioProcessor();
}