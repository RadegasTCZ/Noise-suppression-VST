#include "EngineHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nsp
{
void EngineHost::addEngine (std::unique_ptr<Engine> e)
{
    jassert (e != nullptr);
    auto slot = std::make_unique<Slot>();
    slot->engine = std::move (e);
    slots.push_back (std::move (slot));
}

int EngineHost::numEngines() const noexcept
{
    return (int) slots.size();
}

Engine& EngineHost::getEngine (int index) noexcept
{
    jassert (juce::isPositiveAndBelow (index, numEngines()));
    return *slots[(size_t) index]->engine;
}

int EngineHost::maxExtraLatency() const noexcept
{
    int m = 0;

    for (const auto& s : slots)
        m = juce::jmax (m, s->engine->extraLatencySamples());

    return m;
}

void EngineHost::prepare (int numChannels, int maxSamplesPerCall)
{
    jassert (numEngines() > 0);
    jassert (numChannels >= 1 && numChannels <= kMaxChannels);

    preparedChannels = juce::jlimit (1, kMaxChannels, numChannels);
    maxExtra = maxExtraLatency();
    preRollFrames = (maxExtra + kFrame - 1) / kFrame;
    const int maxIn = juce::jmax (1, maxSamplesPerCall);

    for (auto& s : slots)
    {
        s->engine->prepare (preparedChannels);
        const int pad = maxExtra - s->engine->extraLatencySamples();

        for (size_t ch = 0; ch < (size_t) kMaxChannels; ++ch)
        {
            s->wetPad[ch].prepare (juce::jmax (1, pad));
            s->wetPad[ch].setDelay (pad);
            s->resPad[ch].prepare (juce::jmax (1, pad));
            s->resPad[ch].setDelay (pad);
        }
    }

    for (size_t ch = 0; ch < (size_t) kMaxChannels; ++ch)
    {
        inRing[ch].allocate (maxIn + kFrame);          // leftover (< 480) + one call
        outRing[ch].allocate (maxIn + 3 * kFrame);     // one call's frames + headroom if not pulled
        dryDelay[ch].prepare (juce::jmax (1, maxExtra));
        dryDelay[ch].setDelay (maxExtra);
        history[ch].assign ((size_t) (preRollFrames * kFrame), 0.0f);
    }

    for (int i = 0; i < kFrame; ++i)
    {
        const double t = juce::MathConstants<double>::halfPi * ((double) i + 0.5) / (double) kFrame;
        fadeOut[(size_t) i] = (float) std::cos (t);
        fadeIn[(size_t) i]  = (float) std::sin (t);
    }

    reset();
}

void EngineHost::reset() noexcept
{
    for (size_t ch = 0; ch < (size_t) kMaxChannels; ++ch)
    {
        inRing[ch].clear();
        outRing[ch].clear();
        dryDelay[ch].reset();
        std::fill (history[ch].begin(), history[ch].end(), 0.0f);
    }

    for (auto& s : slots)
    {
        for (size_t ch = 0; ch < (size_t) kMaxChannels; ++ch)
        {
            s->wetPad[ch].reset();
            s->resPad[ch].reset();
        }
    }

    active = requested;
    lastRun = 1;

    if (! slots.empty())
        slots[(size_t) active]->engine->reset();
}

void EngineHost::setTarget (int engineIndex, bool monitorResidual) noexcept
{
    requested = slots.empty() ? 0 : juce::jlimit (0, numEngines() - 1, engineIndex);
    residualSelected = monitorResidual;
}

void EngineHost::updateParameters (const juce::AudioProcessorValueTreeState& s) noexcept
{
    for (auto& slot : slots)
        slot->engine->updateParameters (s);
}

void EngineHost::process (const float* const* in, int numChannels, int n) noexcept
{
    if (preparedChannels == 0 || n <= 0)
        return;

    jassert (numChannels >= 1 && numChannels <= preparedChannels);
    const int numCh = juce::jlimit (1, preparedChannels, numChannels);

    int offset = 0;

    while (offset < n)
    {
        const int take = juce::jmin (n - offset, inRing[0].freeSpace());
        jassert (take > 0);

        if (take <= 0)
            return;

        for (int ch = 0; ch < numCh; ++ch)
        {
            [[maybe_unused]] const bool ok = inRing[(size_t) ch].push (in[ch] + offset, take);
            jassert (ok);
        }

        offset += take;

        while (inRing[0].size() >= kFrame)
            processFrame (numCh);
    }
}

int EngineHost::available() const noexcept
{
    return preparedChannels > 0 ? outRing[0].size() : 0;
}

void EngineHost::pull (float* const* out, int numChannels, int n) noexcept
{
    jassert (n <= available());

    for (int ch = 0; ch < juce::jmin (numChannels, preparedChannels); ++ch)
    {
        [[maybe_unused]] const bool ok = outRing[(size_t) ch].pop (out[ch], n);
        jassert (ok);
    }
}

void EngineHost::processFrame (int numCh) noexcept
{
    const bool switching = requested != active;
    const int incoming = requested;

    if (switching)
        beginSwitch (incoming, numCh);

    Slot& a = *slots[(size_t) active];

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto c = (size_t) ch;
        inRing[c].pop (frameIn.data(), kFrame);
        dryDelay[c].process (frameIn.data(), dry.data(), kFrame);

        runEngine (a, ch, frameIn.data(), wetA.data(), resA.data());
        select (a, wetA.data(), resA.data(), dry.data(), outA.data());

        if (switching)
        {
            Slot& b = *slots[(size_t) incoming];
            runEngine (b, ch, frameIn.data(), wetB.data(), resB.data());
            select (b, wetB.data(), resB.data(), dry.data(), outB.data());

            for (size_t i = 0; i < (size_t) kFrame; ++i)
                outA[i] = outA[i] * fadeOut[i] + outB[i] * fadeIn[i];
        }

        [[maybe_unused]] const bool ok = outRing[c].push (outA.data(), kFrame);
        jassert (ok); // caller must pull regularly

        if (preRollFrames > 0)
        {
            auto& h = history[c];
            std::memmove (h.data(), h.data() + kFrame, sizeof (float) * (h.size() - (size_t) kFrame));
            std::memcpy (h.data() + h.size() - (size_t) kFrame, frameIn.data(), sizeof (float) * (size_t) kFrame);
        }
    }

    if (switching)
        active = incoming;

    lastRun = switching ? 2 : 1;
}

void EngineHost::beginSwitch (int incoming, int numCh) noexcept
{
    Slot& b = *slots[(size_t) incoming];
    b.engine->reset();

    for (size_t ch = 0; ch < (size_t) kMaxChannels; ++ch)
    {
        b.wetPad[ch].reset();
        b.resPad[ch].reset();
    }

    // Pre-roll over the frames before the current one so the incoming engine's padded output
    // is valid (not reset zeros) from the first crossfade sample.
    for (int f = 0; f < preRollFrames; ++f)
        for (int ch = 0; ch < numCh; ++ch)
            runEngine (b, ch, history[(size_t) ch].data() + f * kFrame, wetB.data(), resB.data());
}

void EngineHost::runEngine (Slot& s, int ch, const float* in, float* wetOut, float* resOut) noexcept
{
    const auto c = (size_t) ch;
    s.engine->processFrame (ch, in, wetOut);
    s.wetPad[c].process (wetOut, wetOut, kFrame);

    if (s.engine->providesResidual())
    {
        // always run, so the residual padding history is valid when monitoring is switched on
        s.engine->getResidual (ch, resOut);
        s.resPad[c].process (resOut, resOut, kFrame);
    }
}

void EngineHost::select (const Slot& s, const float* wet, const float* res, const float* dryIn, float* out) const noexcept
{
    if (! residualSelected)
        std::memcpy (out, wet, sizeof (float) * (size_t) kFrame);
    else if (s.engine->providesResidual())
        std::memcpy (out, res, sizeof (float) * (size_t) kFrame);
    else
        for (int i = 0; i < kFrame; ++i)
            out[i] = dryIn[i] - wet[i];
}
} // namespace nsp
