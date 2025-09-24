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
        "enableVAD", "Enable Voice Activation Detection", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noisesuppress", "Noise Suppress (dB)", juce::NormalisableRange<float>(-40.0f, -5.0f, 1.0f, 1.0f, true), -20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "probstart", "Noise Learn Aggressiveness", juce::NormalisableRange<float>(50.0f, 100.0f, 1.0f, 1.0f, true), 70.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "probcontinue", "Noise Adaptation Rate", juce::NormalisableRange<float>(50.0f, 100.0f, 1.0f, 1.0f, true), 70.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "vadThreshold", "VAD RMS Threshold (dBFS)", juce::NormalisableRange<float>(-60.0f, -15.0f, 1.0f, 1.0f, true), -40.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateFloor", "Gate Floor (dB)", juce::NormalisableRange<float>(-60.0f, -5.0f, 1.0f, 1.0f, true), -20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateAttackMs", "Gate Attack (ms)", juce::NormalisableRange<float>(1.0f, 10.0f, 0.5f, 1.0f, true), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateReleaseMs", "Gate Release (ms)", juce::NormalisableRange<float>(10.0f, 1000.0f, 5.0f, 1.0f, true), 80.0f));
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
float SpeexDSPNoiseSuppressorAudioProcessor::getVadThresholdDb() const {
    return apvts.getRawParameterValue("vadThreshold")->load();
}
float SpeexDSPNoiseSuppressorAudioProcessor::getGateFloorDb() const {
    return apvts.getRawParameterValue("gateFloor")->load();
}
float SpeexDSPNoiseSuppressorAudioProcessor::getGateAttackMs() const {
    return apvts.getRawParameterValue("gateAttackMs")->load();
}
float SpeexDSPNoiseSuppressorAudioProcessor::getGateReleaseMs() const {
    return apvts.getRawParameterValue("gateReleaseMs")->load();
}

void SpeexDSPNoiseSuppressorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	const double frameDurationMs = 20.0; // Set your desired frame duration in milliseconds, 20ms is the default for Speex.
    speexFrameSize = (int)(sampleRate * frameDurationMs / 1000.0);

    const float attackMs = getGateAttackMs();
    const float releaseMs = getGateReleaseMs();
    gateAttack = 1.0f - std::exp(-1.0f / (attackMs * 0.001f * (float)sampleRate));
    gateRelease = 1.0f - std::exp(-1.0f / (releaseMs * 0.001f * (float)sampleRate));
    prevAttackMs = getGateAttackMs();
    prevReleaseMs = getGateReleaseMs();

    for (int ch = 0; ch < 2; ++ch)
    {
        channelInputBuffers[ch].clear();
        channelOutputFIFOs[ch].clear();
        if (speexStates[ch]) speex_preprocess_state_destroy(speexStates[ch]);
        speexStates[ch] = speex_preprocess_state_init(speexFrameSize, (int)sampleRate);

        int enableVAD = getEnableVAD() ? 1 : 0;
        int enableDenoise = getEnableDenoise() ? 1 : 0;
        float noiseSuppress = getNoiseSuppress();
        int noiseSuppressInt = (int)noiseSuppress;
        float probStart = getProbStart();
        int probStartInt = (int)probStart;
        float probContinue = getProbContinue();
        int probContinueInt = (int)probContinue;

        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_DENOISE, &enableDenoise);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_VAD, &enableVAD);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppressInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_START, &probStartInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_CONTINUE, &probContinueInt);

        // pre-fill output FIFO with one frame of zeros (pre-buffer)
        channelOutputFIFOs[ch].assign((size_t)speexFrameSize, 0.0f);

        // allocate reusable int16 frame
        frameI16[ch].assign((size_t)speexFrameSize, 0);

        gateGain[ch] = 1.0f; // reset
        juce::ignoreUnused(samplesPerBlock);
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
    const float attackMsNow = getGateAttackMs();
    const float releaseMsNow = getGateReleaseMs();
    if (attackMsNow != prevAttackMs || releaseMsNow != prevReleaseMs)
    {
        const float sr = (float)getSampleRate();
        gateAttack = 1.0f - std::exp(-1.0f / (attackMsNow * 0.001f * sr));
        gateRelease = 1.0f - std::exp(-1.0f / (releaseMsNow * 0.001f * sr));
        prevAttackMs = attackMsNow;
        prevReleaseMs = releaseMsNow;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        int enableVAD = getEnableVAD() ? 1 : 0;
        int enableDenoise = getEnableDenoise() ? 1 : 0;
        float noiseSuppress = getNoiseSuppress();
        int noiseSuppressInt = (int)noiseSuppress;
        float probStart = getProbStart();
        int probStartInt = (int)probStart;
        float probContinue = getProbContinue();
        int probContinueInt = (int)probContinue;

        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_DENOISE, &enableDenoise);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_VAD, &enableVAD);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppressInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_START, &probStartInt);
        speex_preprocess_ctl(speexStates[ch], SPEEX_PREPROCESS_SET_PROB_CONTINUE, &probContinueInt);
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

            // VAD RMS level check safety
            float rms = 0.0f;
            for (int j = 0; j < speexFrameSize; ++j) {
                float v = (float)frm[(size_t)j] / 32768.0f;
                rms += v * v;
            }
            rms = std::sqrt(rms / (float)speexFrameSize);
            float vadThresholdLinear = std::pow(10.0f, getVadThresholdDb() / 20.0f); // read the VAD threshold
            if (!isSpeech && rms > vadThresholdLinear) 
                isSpeech = 1;

            // VAD-driven soft gate
            float target = isSpeech ? 1.0f : std::pow(10.0f, getGateFloorDb() / 20.0f); // reduce non-speech audio by user-defined value
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