#include "Render.h"
#include "TestPaths.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Params.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <climits>
#include <cstdio>

namespace nsp::test
{
namespace
{
/** One render target: engine, output folder (Engine::id(), plus a suffix for a non-default variant),
    and an optional variant parameter value. WebRTC renders at its default level (12 dB). */
struct RenderEngine { int index; const char* folder; const char* variantParam; float variantValue; };
const RenderEngine kEngines[] = { { nsp::kEngineClassic, "classic", nullptr, 0.0f },
                                  { nsp::kEngineSns, "sns", nullptr, 0.0f },
                                  { nsp::kEngineWebRtc, "webrtc", nullptr, 0.0f },
                                  { nsp::kEngineRnnoise, "rnnoise", nsp::ids::rnnModel, 0.0f },
                                  { nsp::kEngineRnnoise, "rnnoise-light", nsp::ids::rnnModel, 1.0f },
                                  { nsp::kEngineDfn, "dfn-gentle", nsp::ids::dfnStrength, 0.0f },
                                  { nsp::kEngineDfn, "dfn-normal", nsp::ids::dfnStrength, 1.0f },
                                  { nsp::kEngineDfn, "dfn-strong", nsp::ids::dfnStrength, 2.0f } };

void setParam (NoiseSuppressionProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

bool writeWav24 (const juce::File& target, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    target.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream> (target);
    if (! fileStream->openedOk())
        return false;
    std::unique_ptr<juce::OutputStream> stream (std::move (fileStream));
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (buffer.getNumChannels())
                                                   .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}
} // namespace

bool renderBuffer (const juce::AudioBuffer<float>& input, double sampleRate, int engineIndex,
                   juce::AudioBuffer<float>& output,
                   const char* variantParamId, float variantValue)
{
    const int ch = input.getNumChannels(), len = input.getNumSamples();
    if (ch < 1 || ch > 2)
        return false;

    NoiseSuppressionProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    const auto set = ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    if (! p.setBusesLayout (layout))
        return false;

    setParam (p, nsp::ids::engine, (float) engineIndex);
    if (variantParamId != nullptr)
        setParam (p, variantParamId, variantValue);
    p.setRateAndBufferSizeDetails (sampleRate, kRenderBlock);
    p.prepareToPlay (sampleRate, kRenderBlock);
    const int latency = p.getLatencySamples();

    output.setSize (ch, len);
    output.clear();
    juce::AudioBuffer<float> block (ch, kRenderBlock);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < len + latency; pos += kRenderBlock)
    {
        const int n = juce::jmin (kRenderBlock, len + latency - pos);
        block.setSize (ch, n, false, false, true);
        block.clear();
        const int fromInput = juce::jlimit (0, n, len - pos);          // zeros once the input is used up
        for (int c = 0; c < ch && fromInput > 0; ++c)
            block.copyFrom (c, 0, input, c, pos, fromInput);
        p.processBlock (block, midi);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i)
            {
                const int dst = pos + i - latency;                      // output sample k is input sample k - latency
                if (dst >= 0 && dst < len)
                    output.setSample (c, dst, block.getSample (c, i));
            }
    }
    p.releaseResources();
    return true;
}

int runRenders()
{
    juce::ScopedJuceInitialiser_GUI gui;   // APVTS needs the message manager; nested initialisers are ref-counted
    const auto inDir = testAudioDir();
    const auto inPath = inDir.getFullPathName();
    if (! inDir.isDirectory())
    {
        std::printf ("render: input folder not found: %s (set NSP_TEST_AUDIO_DIR or use --audio-dir)\n", inPath.toRawUTF8());
        return 1;
    }
    const auto outRoot = testsDir().getChildFile ("render");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto files = inDir.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();
    std::printf ("render: %d file(s) from %s -> %s\n", files.size(), inPath.toRawUTF8(), outRoot.getFullPathName().toRawUTF8());

    bool ok = true;
    for (const auto& file : files)
    {
        const auto name = file.getFileName();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            std::printf ("  skip %s: unreadable\n", name.toRawUTF8());
            continue;
        }
        const int ch = (int) reader->numChannels;
        if (ch < 1 || ch > 2)
        {
            std::printf ("  skip %s: %d channels (only mono and stereo)\n", name.toRawUTF8(), ch);
            continue;
        }
        if (reader->lengthInSamples > INT_MAX)
        {
            std::printf ("  skip %s: too long\n", name.toRawUTF8());
            continue;
        }
        const int len = (int) reader->lengthInSamples;
        const double fs = reader->sampleRate;
        juce::AudioBuffer<float> input (ch, len);
        reader->read (&input, 0, len, 0, true, ch > 1);

        for (const auto& engine : kEngines)
        {
            juce::AudioBuffer<float> rendered;
            const auto dir = outRoot.getChildFile (engine.folder);
            dir.createDirectory();
            const auto target = dir.getChildFile (name);
            const bool done = renderBuffer (input, fs, engine.index, rendered, engine.variantParam, engine.variantValue)
                              && writeWav24 (target, rendered, fs);
            ok = ok && done;
            std::printf ("  %-13s %-60s %d ch %6.0f Hz %8.2f s  %s\n", engine.folder, name.toRawUTF8(), ch, fs,
                         (double) len / fs, done ? "ok" : "FAILED");
        }
    }
    return ok ? 0 : 1;
}
} // namespace nsp::test
