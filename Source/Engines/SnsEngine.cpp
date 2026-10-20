#include "SnsEngine.h"
#include <algorithm>
#include <cstring>

namespace nsp
{
static_assert (HD_FRAME_SIZE == Engine::kFrameSize, "SNS frame size must equal the engine frame size");

SnsEngine::~SnsEngine()
{
    destroyStates();
}

void SnsEngine::destroyStates() noexcept
{
    for (auto& st : states)
        hybrid_denoise_cleanup (&st);   // frees the kissfft configs; no-op when the pointers are null
    numChannels = 0;
}

void SnsEngine::prepare (int channels)
{
    jassert (channels >= 1 && channels <= kMaxChannels);
    destroyStates();
    numChannels = juce::jlimit (1, kMaxChannels, channels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        hybrid_denoise_init (&states[ch]);     // allocates the two kissfft configs (message thread)
        states[ch].suppress_db = suppressDb;
        states[ch].residual_mode = 0;
    }
    for (auto& r : residual)
        r.fill (0.0f);
    noiseFloor.store (kInitialFloorDb, std::memory_order_relaxed);
}

/* Allocation-free equivalent of hybrid_denoise_init() on an initialised state.
   Mirrors HybridDenoise.cpp hybrid_denoise_init(): memset(0) of the whole struct
   (analysis_mem, synthesis_mem, S, Smin, Stmp, noise, ps, prior, post, gain, lastg,
   last_ps, Pframe, nb_adapt, min_count, display_noise_db, residual_synthesis_mem),
   then noise[i] = 1e-4f and lastg[i] = 1.0f. The kissfft configs are kept (init would
   allocate new ones); suppress_db and residual_mode are parameters and are kept. */
void SnsEngine::resetState (HybridDenoiseState& st) noexcept
{
    const kiss_fftr_cfg fwd = st.fft_fwd;
    const kiss_fftr_cfg inv = st.fft_inv;
    const float suppress = st.suppress_db;
    const int residualMode = st.residual_mode;

    std::memset (&st, 0, sizeof (st));

    st.fft_fwd = fwd;
    st.fft_inv = inv;
    st.suppress_db = suppress;
    st.residual_mode = residualMode;
    for (int i = 0; i < HD_NB_BANDS; ++i)
    {
        st.noise[i] = 1e-4f;
        st.lastg[i] = 1.0f;
    }
}

void SnsEngine::reset()
{
    for (int ch = 0; ch < numChannels; ++ch)
        resetState (states[ch]);
    for (auto& r : residual)
        r.fill (0.0f);
    noiseFloor.store (kInitialFloorDb, std::memory_order_relaxed);
}

void SnsEngine::processFrame (int channel, const float* in, float* out)
{
    jassert (channel >= 0 && channel < numChannels);
    if (channel < 0 || channel >= numChannels)
    {
        std::fill (out, out + kFrameSize, 0.0f);
        return;
    }

    auto& st = states[channel];
    st.suppress_db = suppressDb;
    hybrid_denoise_process_frame_dual (&st, out, residual[(size_t) channel].data(), in);

    if (channel == 0)
        noiseFloor.store (st.display_noise_db, std::memory_order_relaxed);
}

void SnsEngine::getResidual (int channel, float* residualOut)
{
    jassert (channel >= 0 && channel < numChannels);
    if (channel < 0 || channel >= numChannels)
    {
        std::fill (residualOut, residualOut + kFrameSize, 0.0f);
        return;
    }
    const auto& r = residual[(size_t) channel];
    std::copy (r.begin(), r.end(), residualOut);
}

void SnsEngine::updateParameters (const juce::AudioProcessorValueTreeState& state)
{
    // String literal: the same id as nsp::ids::snsSuppress (Params.h).
    if (auto* p = state.getRawParameterValue ("snsSuppress"))
        setSuppressDb (p->load (std::memory_order_relaxed));
}
} // namespace nsp
