#include "ClassicSpeexEngine.h"
#include "speex/speex_preprocess.h"   // has its own extern "C" guard
#include <cmath>

namespace nsp
{
static_assert (sizeof (spx_int16_t) == sizeof (short), "frame16 must match spx_int16_t");

ClassicSpeexEngine::~ClassicSpeexEngine()
{
    destroyStates();
}

void ClassicSpeexEngine::destroyStates() noexcept
{
    for (auto*& st : states)
    {
        if (st != nullptr)
        {
            speex_preprocess_state_destroy (st);
            st = nullptr;
        }
    }
    numChannels = 0;
}

void ClassicSpeexEngine::prepare (int channels)
{
    jassert (channels >= 1 && channels <= kMaxChannels);
    destroyStates();
    numChannels = juce::jlimit (1, kMaxChannels, channels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* st = speex_preprocess_state_init (kFrameSize, kSampleRate);
        jassert (st != nullptr);

        // init already leaves VAD off; SPEEX_PREPROCESS_SET_VAD is deliberately not called
        // because it prints a speex_warning (stderr) on every call.
        spx_int32_t on = 1, off = 0;
        speex_preprocess_ctl (st, SPEEX_PREPROCESS_SET_DENOISE, &on);
        speex_preprocess_ctl (st, SPEEX_PREPROCESS_SET_AGC, &off);
        speex_preprocess_ctl (st, SPEEX_PREPROCESS_SET_DEREVERB, &off);
        spx_int32_t db = suppressDb;
        speex_preprocess_ctl (st, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &db);
        // PROB_START / PROB_CONTINUE: speex defaults (v1.1.2's params were dropped, D3).

        states[(size_t) ch] = st;
    }
}

void ClassicSpeexEngine::reset()
{
    for (int ch = 0; ch < numChannels; ++ch)
        speex_preprocess_state_reset (states[(size_t) ch]);   // local SpeexDSP patch, no allocation
}

void ClassicSpeexEngine::processFrame (int channel, const float* in, float* out)
{
    jassert (channel >= 0 && channel < numChannels);
    if (channel < 0 || channel >= numChannels)
    {
        std::fill (out, out + kFrameSize, 0.0f);
        return;
    }

    // float -> int16 exactly as v1.1.2: x * 32768, round to nearest, clamp to int16 range.
    for (int i = 0; i < kFrameSize; ++i)
    {
        const float s = juce::jlimit (-32768.0f, 32767.0f, in[i] * 32768.0f);
        frame16[(size_t) i] = (short) std::lrintf (s);
    }

    speex_preprocess_run (states[(size_t) channel], frame16.data());

    for (int i = 0; i < kFrameSize; ++i)
        out[i] = (float) frame16[(size_t) i] / 32768.0f;
}

void ClassicSpeexEngine::setSuppressDb (int db) noexcept
{
    if (db == suppressDb)
        return;
    suppressDb = db;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        spx_int32_t v = db;
        speex_preprocess_ctl (states[(size_t) ch], SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &v);
    }
}

void ClassicSpeexEngine::updateParameters (const juce::AudioProcessorValueTreeState& state)
{
    // String literal: the same id as nsp::ids::classicSuppress (Params.h).
    if (auto* p = state.getRawParameterValue ("classicSuppress"))
        setSuppressDb (juce::roundToInt (p->load (std::memory_order_relaxed)));
}
} // namespace nsp
