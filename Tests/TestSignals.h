#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <limits>
#include <vector>

namespace nsp::test
{
    /** Uniform white noise with the given RMS (uniform in [-a, a), a = rms * sqrt(3)). Deterministic per seed. */
    inline std::vector<float> whiteNoise (int n, float rms, juce::uint32 seed)
    {
        juce::Random random ((juce::int64) seed);
        const float a = rms * std::sqrt (3.0f);
        std::vector<float> v ((size_t) juce::jmax (0, n));

        for (auto& x : v)
            x = a * (2.0f * random.nextFloat() - 1.0f);

        return v;
    }

    /** Pink-ish noise: Paul Kellet's economy 3-pole filter on whiteNoise (n, 1, seed), scaled to the
        given RMS over the whole signal. Deterministic per seed. */
    inline std::vector<float> pinkNoise (int n, float rms, juce::uint32 seed)
    {
        const auto w = whiteNoise (n, 1.0f, seed);
        std::vector<float> v (w.size());
        double b0 = 0.0, b1 = 0.0, b2 = 0.0, sumSq = 0.0;

        for (size_t i = 0; i < w.size(); ++i)
        {
            const double x = w[i];
            b0 = 0.99765 * b0 + x * 0.0990460;
            b1 = 0.96300 * b1 + x * 0.2965164;
            b2 = 0.57000 * b2 + x * 1.0526913;
            const double y = b0 + b1 + b2 + x * 0.1848;
            v[i] = (float) y;
            sumSq += y * y;
        }

        const double gain = sumSq > 0.0 ? (double) rms / std::sqrt (sumSq / (double) v.size()) : 0.0;

        for (auto& x : v)
            x = (float) (x * gain);

        return v;
    }

    /** amp * sin(2*pi*freq*i/rate), i = 0..n-1, computed in double. */
    inline std::vector<float> sine (int n, double freq, double rate, float amp)
    {
        std::vector<float> v ((size_t) juce::jmax (0, n));
        const double w = juce::MathConstants<double>::twoPi * freq / rate;

        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) ((double) amp * std::sin (w * (double) i));

        return v;
    }

    /** n zeros with amp at index pos (pos outside [0, n) gives all zeros). */
    inline std::vector<float> impulse (int n, int pos, float amp = 1.0f)
    {
        std::vector<float> v ((size_t) juce::jmax (0, n), 0.0f);

        if (pos >= 0 && pos < n)
            v[(size_t) pos] = amp;

        return v;
    }

    /** The lag in [0, maxLag] maximising sum_i ref[i] * sig[i + lag] (first maximum wins). */
    inline int xcorrLag (const std::vector<float>& ref, const std::vector<float>& sig, int maxLag)
    {
        int bestLag = 0;
        double bestSum = -std::numeric_limits<double>::infinity();

        for (int lag = 0; lag <= maxLag; ++lag)
        {
            const int count = juce::jmin ((int) ref.size(), (int) sig.size() - lag);
            double sum = 0.0;

            for (int i = 0; i < count; ++i)
                sum += (double) ref[(size_t) i] * (double) sig[(size_t) (i + lag)];

            if (sum > bestSum)
            {
                bestSum = sum;
                bestLag = lag;
            }
        }

        return bestLag;
    }

    /** max |x[i] - x[i-1]| over i in [1, n); 0 if n < 2. */
    inline float maxAbsStep (const float* x, int n)
    {
        float best = 0.0f;

        for (int i = 1; i < n; ++i)
            best = juce::jmax (best, std::abs (x[i] - x[i - 1]));

        return best;
    }

    /** 20*log10(rms of x[0..n)); -200 if the rms is 0 or n <= 0. */
    inline float rmsDb (const float* x, int n)
    {
        if (n <= 0)
            return -200.0f;

        double sumSq = 0.0;

        for (int i = 0; i < n; ++i)
            sumSq += (double) x[i] * (double) x[i];

        if (sumSq <= 0.0)
            return -200.0f;

        return (float) (10.0 * std::log10 (sumSq / (double) n));
    }

    /** 20*log10(max |a[i] - b[i]|); -200 if the signals are identical. */
    inline float maxAbsDiffDb (const float* a, const float* b, int n)
    {
        double worst = 0.0;

        for (int i = 0; i < n; ++i)
            worst = juce::jmax (worst, std::abs ((double) a[i] - (double) b[i]));

        if (worst <= 0.0)
            return -200.0f;

        return (float) (20.0 * std::log10 (worst));
    }

    /** Speech-like signal at 48 kHz: 12-harmonic 150 Hz voice (1/k amplitudes) under a
        |sin(2*pi*2*t)| syllable envelope (4 per second), peak 0.3, plus white noise at -40 dBFS rms.
        (Moved here from SnsEngineTests.cpp, unchanged, so the WebRTC NS / RNNoise engine tests can use it.) */
    inline std::vector<float> speechLike (int n, juce::uint32 seed)
    {
        constexpr double rate = 48000.0;
        const double twoPi = juce::MathConstants<double>::twoPi;
        std::vector<double> voice ((size_t) juce::jmax (0, n), 0.0);
        double peak = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / rate;
            double v = 0.0;

            for (int k = 1; k <= 12; ++k)
                v += std::sin (twoPi * 150.0 * k * t) / k;

            voice[(size_t) i] = v;
            peak = juce::jmax (peak, std::abs (v));
        }

        auto x = whiteNoise (n, 0.01f, seed);

        for (int i = 0; i < n; ++i)
        {
            const double env = std::abs (std::sin (twoPi * 2.0 * (double) i / rate));
            x[(size_t) i] += (float) (0.3 * env * voice[(size_t) i] / peak);
        }

        return x;
    }
}
