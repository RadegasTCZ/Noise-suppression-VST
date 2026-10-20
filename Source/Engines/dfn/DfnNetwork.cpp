// Ported from DeepFilterNet libDF, commit d375b2d. Portions Copyright (c) 2021 Hendrik Schröter, used under the
// MIT licence (ThirdParty/deepfilternet/LICENSE-MIT); the port and its changes are part of this project (GPLv3).
#include "DfnNetwork.h"
#include "DfnModel.h"

#include <JuceHeader.h>
#include <emmintrin.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace nsp::dfn
{
namespace
{
using Vec = std::vector<float>;

/** y += a * x, n % 4 == 0 (SSE2, unaligned). */
void axpy (float a, const float* x, float* y, int n) noexcept
{
    const __m128 va = _mm_set1_ps (a);
    for (int i = 0; i < n; i += 4)
        _mm_storeu_ps (y + i, _mm_add_ps (_mm_loadu_ps (y + i), _mm_mul_ps (va, _mm_loadu_ps (x + i))));
}

inline float sigm (float x) noexcept { return 1.0f / (1.0f + std::exp (-x)); }

void relu (float* x, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        x[i] = x[i] > 0 ? x[i] : 0;
}

/** [rows][cols] -> [cols][rows]. */
Vec transpose (const Vec& w, int rows, int cols)
{
    Vec t (w.size());
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            t[(size_t) (c * rows + r)] = w[(size_t) (r * cols + c)];
    return t;
}

/** Pointwise conv: in [cin][F] -> out [cout][F]. Weights [cout][cin][1][1]. */
struct PW
{
    Vec w, b;
    int cin = 0, cout = 0;

    void init (const Vec& weights, const Vec& bias, int ci, int co)
    {
        jassert ((int) weights.size() == ci * co && (int) bias.size() == co);
        w = weights;
        b = bias;
        cin = ci;
        cout = co;
    }

    void run (const float* in, float* out, int F) const noexcept
    {
        for (int o = 0; o < cout; ++o)
        {
            float* y = out + o * F;
            for (int f = 0; f < F; ++f)
                y[f] = b[(size_t) o];
            const float* wo = w.data() + o * cin;
            for (int i = 0; i < cin; ++i)
                axpy (wo[i], in + i * F, y, F);
        }
    }
};

/** Depthwise 1x3 conv along frequency, pad 1. Weights [64][1][1][3]. */
struct DW3
{
    Vec w;

    void init (const Vec& weights) { jassert (weights.size() == 64 * 3); w = weights; }

    void run (const float* in, float* out, int Fin, int stride) const noexcept
    {
        const int Fout = (Fin + 2 - 3) / stride + 1;
        for (int c = 0; c < 64; ++c)
            for (int o = 0; o < Fout; ++o)
            {
                float s = 0;
                for (int k = 0; k < 3; ++k)
                {
                    const int i = stride * o - 1 + k;
                    if (i >= 0 && i < Fin)
                        s += w[(size_t) (c * 3 + k)] * in[c * Fin + i];
                }
                out[c * Fout + o] = s;
            }
    }
};

/** Transposed depthwise conv, kernel 3, stride 2, pad 1, output padding 1: Fin -> 2 Fin. */
struct DWT3
{
    Vec w;

    void init (const Vec& weights) { jassert (weights.size() == 64 * 3); w = weights; }

    void run (const float* in, float* out, int Fin) const noexcept
    {
        const int Fout = 2 * Fin;
        std::memset (out, 0, sizeof (float) * 64 * (size_t) Fout);
        for (int c = 0; c < 64; ++c)
            for (int i = 0; i < Fin; ++i)
                for (int k = 0; k < 3; ++k)
                {
                    const int o = 2 * i - 1 + k;
                    if (o >= 0 && o < Fout)
                        out[c * Fout + o] += w[(size_t) (c * 3 + k)] * in[c * Fin + i];
                }
    }
};

/** Grouped linear: [G][I] -> [G][H]. Weights [G][I][H]. */
struct GLin
{
    Vec w;
    int G = 0, I = 0, H = 0;

    void init (const Vec& weights, int g, int i, int h)
    {
        jassert ((int) weights.size() == g * i * h);
        w = weights;
        G = g;
        I = i;
        H = h;
    }

    void run (const float* x, float* y) const noexcept
    {
        std::memset (y, 0, sizeof (float) * (size_t) (G * H));
        for (int g = 0; g < G; ++g)
            for (int i = 0; i < I; ++i)
                axpy (x[g * I + i], w.data() + (g * I + i) * H, y + g * H, H);
    }
};

/** ONNX GRU: gates z, r, h; linear_before_reset = 1; hidden 256, input 256. Weights stored transposed. */
struct GRU
{
    Vec Wt, Rt, B;

    void init (const Vec& W, const Vec& R, const Vec& bias)
    {
        jassert (W.size() == 768 * 256 && R.size() == 768 * 256 && bias.size() == 1536);
        Wt = transpose (W, 768, 256);
        Rt = transpose (R, 768, 256);
        B = bias;
    }

    void step (const float* x, float* h) const noexcept   // h updated in place
    {
        alignas (16) float xw[768], hr[768];
        std::memcpy (xw, B.data(), sizeof xw);
        std::memcpy (hr, B.data() + 768, sizeof hr);
        for (int i = 0; i < 256; ++i)
        {
            axpy (x[i], Wt.data() + i * 768, xw, 768);
            axpy (h[i], Rt.data() + i * 768, hr, 768);
        }
        for (int j = 0; j < 256; ++j)
        {
            const float z = sigm (xw[j] + hr[j]);
            const float rr = sigm (xw[256 + j] + hr[256 + j]);
            const float hh = std::tanh (xw[512 + j] + rr * hr[512 + j]);
            h[j] = (1 - z) * hh + z * h[j];
        }
    }
};

void scaleBiasRelu (const float* in, float* out, const Vec& s, const Vec& b, int F) noexcept
{
    for (int c = 0; c < 64; ++c)
        for (int f = 0; f < F; ++f)
        {
            const float v = s[(size_t) c] * in[c * F + f] + b[(size_t) c];
            out[c * F + f] = v > 0 ? v : 0;
        }
}
} // namespace

struct Network::Impl
{
    // Encoder
    Vec c0w, c0b, d0w, lsnrW;
    float lsnrB = 0;
    DW3 e1dw, e2dw, e3dw, d1dw;
    PW e1pw, e2pw, e3pw, d0pw, d1pw;
    GLin dfFcEmb, encIn, encOut;
    GRU encGru;
    // ERB decoder
    GLin erbIn, erbOut;
    GRU erbG1, erbG2;
    Vec p3s, p3b, p2s, p2b, p1s, p1b, p0s, p0b, outW;
    float outB = 0;
    DW3 ct3dw;
    PW ct3pw, ct2pw, ct1pw;
    DWT3 ct2, ct1;
    // DF decoder
    GLin dfIn, dfSkip, dfOut;
    GRU dfG1, dfG2;
    Vec cpW;
    PW cpPw;

    void load (const Model& m)
    {
        auto T = [&m] (int i) -> const Vec& { return m.tensor (i); };
        // Encoder (tensors 0..24)
        c0w = T (0);  c0b = T (1);
        e1dw.init (T (2));  e1pw.init (T (3), T (4), 64, 64);
        e2dw.init (T (5));  e2pw.init (T (6), T (7), 64, 64);
        e3dw.init (T (8));  e3pw.init (T (9), T (10), 64, 64);
        d0w = T (11);  d0pw.init (T (12), T (13), 64, 64);
        d1dw.init (T (14));  d1pw.init (T (15), T (16), 64, 64);
        dfFcEmb.init (T (17), 32, 96, 16);
        encIn.init (T (18), 16, 32, 16);
        encGru.init (T (19), T (20), T (21));
        encOut.init (T (22), 16, 16, 32);
        lsnrW = T (23);  lsnrB = T (24)[0];
        // ERB decoder (25..51)
        erbIn.init (T (25), 16, 32, 16);
        erbG1.init (T (26), T (27), T (28));
        erbG2.init (T (29), T (30), T (31));
        erbOut.init (T (32), 16, 16, 32);
        p3s = T (33);  p3b = T (34);  ct3dw.init (T (35));  ct3pw.init (T (36), T (37), 64, 64);
        p2s = T (38);  p2b = T (39);  ct2.init (T (40));  ct2pw.init (T (41), T (42), 64, 64);
        p1s = T (43);  p1b = T (44);  ct1.init (T (45));  ct1pw.init (T (46), T (47), 64, 64);
        p0s = T (48);  p0b = T (49);  outW = T (50);  outB = T (51)[0];
        // DF decoder (52..65); tensors 63, 64 (alpha head) are not used
        dfIn.init (T (52), 8, 64, 32);
        dfG1.init (T (53), T (54), T (55));
        dfG2.init (T (56), T (57), T (58));
        dfSkip.init (T (59), 16, 32, 16);
        cpW = T (60);  cpPw.init (T (61), T (62), 10, 10);
        dfOut.init (T (65), 16, 16, 60);
        jassert (c0w.size() == 64 * 9 && d0w.size() == 64 * 9 && lsnrW.size() == 512 && outW.size() == 64 * 3 && cpW.size() == 10 * 32 * 5);
    }
};

void Network::State::clear() noexcept
{
    std::memset (this, 0, sizeof (State));
}

Network::Network() = default;
Network::~Network() = default;

bool Network::init (const Model& m)
{
    if (! m.isLoaded())
        return false;
    impl = std::make_unique<Impl>();
    impl->load (m);
    return true;
}

void Network::encode (State& S, const float* featErb, const float* featSpec) const noexcept
{
    jassert (impl != nullptr);
    const Impl& M = *impl;
    alignas (16) float tmp[64 * 96], c1[64 * 48], flat[3072], v512[512], v256[256];

    // history shift
    std::memmove (S.featErbHist[0], S.featErbHist[1], sizeof (float) * 64);
    std::memcpy (S.featErbHist[2], featErb, sizeof (float) * 32);
    std::memmove (S.featSpecHist[0], S.featSpecHist[1], sizeof (float) * 2 * 2 * 96);
    std::memcpy (S.featSpecHist[2], featSpec, sizeof (float) * 2 * 96);

    // ERB pathway: conv 3x3 then three depthwise-separable convs
    for (int c = 0; c < 64; ++c)
        for (int f = 0; f < 32; ++f)
        {
            float s = M.c0b[(size_t) c];
            for (int kt = 0; kt < 3; ++kt)
                for (int kf = 0; kf < 3; ++kf)
                {
                    const int i = f - 1 + kf;
                    if (i >= 0 && i < 32)
                        s += M.c0w[(size_t) ((c * 3 + kt) * 3 + kf)] * S.featErbHist[kt][i];
                }
            S.e0[c * 32 + f] = s > 0 ? s : 0;
        }
    M.e1dw.run (S.e0, tmp, 32, 2);  M.e1pw.run (tmp, S.e1, 16);  relu (S.e1, 64 * 16);
    M.e2dw.run (S.e1, tmp, 16, 2);  M.e2pw.run (tmp, S.e2, 8);   relu (S.e2, 64 * 8);
    M.e3dw.run (S.e2, tmp, 8, 1);   M.e3pw.run (tmp, S.e3, 8);   relu (S.e3, 64 * 8);

    // DF pathway: grouped (2 x 32 channels) 3x3 conv over the spec history, then pointwise
    for (int c = 0; c < 64; ++c)
    {
        const int g = c / 32;
        for (int f = 0; f < 96; ++f)
        {
            float s = 0;
            for (int kt = 0; kt < 3; ++kt)
                for (int kf = 0; kf < 3; ++kf)
                {
                    const int i = f - 1 + kf;
                    if (i >= 0 && i < 96)
                        s += M.d0w[(size_t) ((c * 3 + kt) * 3 + kf)] * S.featSpecHist[kt][g][i];
                }
            tmp[c * 96 + f] = s;
        }
    }
    M.d0pw.run (tmp, S.c0, 96);
    relu (S.c0, 64 * 96);
    M.d1dw.run (S.c0, tmp, 96, 2);  M.d1pw.run (tmp, c1, 48);  relu (c1, 64 * 48);

    // embedding
    for (int f = 0; f < 48; ++f)
        for (int c = 0; c < 64; ++c)
            flat[f * 64 + c] = c1[c * 48 + f];
    M.dfFcEmb.run (flat, v512);
    relu (v512, 512);
    for (int f = 0; f < 8; ++f)
        for (int c = 0; c < 64; ++c)
            v512[f * 64 + c] += S.e3[c * 8 + f];
    M.encIn.run (v512, v256);
    relu (v256, 256);
    M.encGru.step (v256, S.hEnc);
    M.encOut.run (S.hEnc, S.emb);
    relu (S.emb, 512);

    float l = M.lsnrB;
    for (int i = 0; i < 512; ++i)
        l += S.emb[i] * M.lsnrW[(size_t) i];
    S.lsnr = sigm (l) * 50.f - 15.f;
}

void Network::decodeErb (State& S) const noexcept
{
    jassert (impl != nullptr);
    const Impl& M = *impl;
    alignas (16) float tmp[64 * 32], p[64 * 32], x8[64 * 8], y8[64 * 8], y16[64 * 16], y32[64 * 32], v256[256], v512[512];

    M.erbIn.run (S.emb, v256);
    relu (v256, 256);
    M.erbG1.step (v256, S.hErb1);
    M.erbG2.step (S.hErb1, S.hErb2);
    M.erbOut.run (S.hErb2, v512);
    relu (v512, 512);

    scaleBiasRelu (S.e3, p, M.p3s, M.p3b, 8);
    for (int c = 0; c < 64; ++c)
        for (int f = 0; f < 8; ++f)
            x8[c * 8 + f] = v512[f * 64 + c] + p[c * 8 + f];
    M.ct3dw.run (x8, tmp, 8, 1);  M.ct3pw.run (tmp, y8, 8);  relu (y8, 64 * 8);

    scaleBiasRelu (S.e2, p, M.p2s, M.p2b, 8);
    for (int i = 0; i < 64 * 8; ++i)
        y8[i] += p[i];
    M.ct2.run (y8, tmp, 8);  M.ct2pw.run (tmp, y16, 16);  relu (y16, 64 * 16);

    scaleBiasRelu (S.e1, p, M.p1s, M.p1b, 16);
    for (int i = 0; i < 64 * 16; ++i)
        y16[i] += p[i];
    M.ct1.run (y16, tmp, 16);  M.ct1pw.run (tmp, y32, 32);  relu (y32, 64 * 32);

    scaleBiasRelu (S.e0, p, M.p0s, M.p0b, 32);
    for (int i = 0; i < 64 * 32; ++i)
        y32[i] += p[i];

    for (int f = 0; f < 32; ++f)
    {
        float s = M.outB;
        for (int c = 0; c < 64; ++c)
            for (int k = 0; k < 3; ++k)
            {
                const int i = f - 1 + k;
                if (i >= 0 && i < 32)
                    s += M.outW[(size_t) (c * 3 + k)] * y32[c * 32 + i];
            }
        S.gains[f] = sigm (s);
    }
}

void Network::decodeDf (State& S) const noexcept
{
    jassert (impl != nullptr);
    const Impl& M = *impl;
    alignas (16) float v256[256], v256b[256], cp[10 * 96], cp2[10 * 96], o960[960];

    std::memmove (S.c0Hist[0], S.c0Hist[1], sizeof (float) * 4 * 64 * 96);
    std::memcpy (S.c0Hist[4], S.c0, sizeof (float) * 64 * 96);

    M.dfIn.run (S.emb, v256);
    relu (v256, 256);
    M.dfG1.step (v256, S.hDf1);
    M.dfG2.step (S.hDf1, S.hDf2);
    M.dfSkip.run (S.emb, v256b);
    for (int i = 0; i < 256; ++i)
        v256b[i] += S.hDf2[i];

    // DF pathway: (5 x 1) time conv over c0Hist, 5 output channels per group of 32 input channels
    for (int o = 0; o < 10; ++o)
    {
        const int g = o / 5;
        float* y = cp + o * 96;
        for (int f = 0; f < 96; ++f)
            y[f] = 0;
        for (int i = 0; i < 32; ++i)
            for (int kt = 0; kt < 5; ++kt)
                axpy (M.cpW[(size_t) ((o * 32 + i) * 5 + kt)], S.c0Hist[kt] + (g * 32 + i) * 96, y, 96);
    }
    M.cpPw.run (cp, cp2, 96);
    relu (cp2, 10 * 96);

    M.dfOut.run (v256b, o960);
    for (int f = 0; f < 96; ++f)
        for (int k = 0; k < 10; ++k)
            S.coefs[f * 10 + k] = std::tanh (o960[f * 10 + k]) + cp2[k * 96 + f];
}
} // namespace nsp::dfn
