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
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "enableDenoise", "Enable Noise Suppression", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "enableVAD", "Enable Noise Suppression", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noisesuppress", "Noise Suppress (dB)", juce::NormalisableRange<float>(-40.0f, -5.0f, 1.0f, 1.0f, true), -20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "probstart", "Noise Learn Aggressiveness", juce::NormalisableRange<float>(0.5f, 1.0f, 0.01f, 1.0f, true), 0.70f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "probcontinue", "Noise Adaptation Rate", juce::NormalisableRange<float>(0.5f, 1.0f, 0.01f, 1.0f, true), 0.7f));
    return { params.begin(), params.end() };
}

bool SpeexDSPNoiseSuppressorAudioProcessor::getEnableDenoise() const {
    return apvts.getRawParameterValue("enableDenoise")->load() > 0.5f;
}
bool SpeexDSPNoiseSuppressorAudioProcessor::getEnableVAD() const {
    return apvts.getRawParameterValue("enableVAD")->load() > 0.5f;
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
	const double frameDurationMs = 20.0; // Set your desired frame duration in milliseconds, 20ms is the default for Speex.
    speexFrameSize = (int)(sampleRate * frameDurationMs / 1000.0);

    // Gate smoothing: 2 ms attack, 80 ms release
    const float attackMs = 2.0f;
    const float releaseMs = 80.0f;
    gateAttack = 1.0f - std::exp(-1.0f / (attackMs * 0.001f * (float)sampleRate));
    gateRelease = 1.0f - std::exp(-1.0f / (releaseMs * 0.001f * (float)sampleRate));

    for (int ch = 0; ch < 2; ++ch)
    {
        channelInputBuffers[ch].clear();
        channelOutputFIFOs[ch].clear();
        if (speexStates[ch]) speex_preprocess_state_destroy(speexStates[ch]);
        speexStates[ch] = speex_preprocess_state_init(speexFrameSize, (int)sampleRate);

        // fetch input params
        int enableVAD = getEnableVAD() ? 1 : 0;
        int enableDenoise = getEnableDenoise() ? 1 : 0;
        float noiseSuppress = getNoiseSuppress();
        float probStart     = getProbStart();
        float probContinue  = getProbContinue();
        int noiseSuppressInt = (int)noiseSuppress;

        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_DENOISE, &enableDenoise);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_VAD, &enableVAD);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppressInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_START, &probStart);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_CONTINUE, &probContinue);


        // pre-fill output FIFO with one frame of zeros (pre-buffer)
        channelOutputFIFOs[ch].assign((size_t)speexFrameSize, 0.0f);

        // allocate reusable int16 frame
        frameI16[ch].assign((size_t)speexFrameSize, 0);

        gateGain[ch] = 1.0f; // reset
    }
    // Tell the host about the exact processing latency (1 frame = 20 ms)
    setLatencySamples(speexFrameSize);
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
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        int enableVAD = getEnableVAD() ? 1 : 0;
        int enableDenoise = getEnableDenoise() ? 1 : 0;
        float noiseSuppress = getNoiseSuppress();
        float probStart = getProbStart();
        float probContinue = getProbContinue();
        int noiseSuppressInt = (int)noiseSuppress;

        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_DENOISE, &enableDenoise);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_VAD, &enableVAD);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppressInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_START, &probStart);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_CONTINUE, &probContinue);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* channelData = buffer.getWritePointer(ch);
        const float* inputData = buffer.getReadPointer(ch);

        // 1) Append input to deque
        for (int j = 0; j < numSamples; ++j)
            channelInputBuffers[ch].push_back(inputData[j]);

        // 2) While we have a full frame, process it
        while ((int)channelInputBuffers[ch].size() >= speexFrameSize)
        {
            // Reuse preallocated frameI16
            auto& frm = frameI16[ch]; // vector<spx_int16_t>

            // Convert float -> int16
            for (int j = 0; j < speexFrameSize; ++j)
            {
                const float s = channelInputBuffers[ch][(size_t)j];
                frm[(size_t)j] = (spx_int16_t)juce::jlimit(-32768, 32767, (int)std::lrintf(s * 32768.0f));
            }

            int isSpeech = speex_preprocess_run(speexStates[ch], frm.data());

            // RMS level check safety
            float rms = 0.0f;
            for (int j = 0; j < speexFrameSize; ++j) {
                float v = (float)frm[(size_t)j] / 32768.0f;
                rms += v * v;
            }
            rms = std::sqrt(rms / (float)speexFrameSize);
            if (!isSpeech && rms > 0.01f)  // set RMS level to -40 dBFS
                isSpeech = 1;

            // VAD-driven soft gate
            float target = isSpeech ? 1.0f : gateFloor;
            float& g = gateGain[ch];
            float  coef = (target > g) ? gateAttack : gateRelease;

            for (int j = 0; j < speexFrameSize; ++j)
            {
                g += coef * (target - g);
                float f = (float)frm[(size_t)j] / 32768.0f;
                channelOutputFIFOs[ch].push_back(f * g);
            }

            // Remove processed samples from input deque (fast)
            for (int j = 0; j < speexFrameSize; ++j)
            channelInputBuffers[ch].pop_front();
        }

        // 3) Drain exactly numSamples to output
        int samplesWritten = 0;
        while (samplesWritten < numSamples && !channelOutputFIFOs[ch].empty())
        {
            channelData[samplesWritten++] = channelOutputFIFOs[ch].front();
            channelOutputFIFOs[ch].pop_front();
        }

        // 4) Zero-pad remainder (should be rare)
        while (samplesWritten < numSamples)
            channelData[samplesWritten++] = 0.0f;
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