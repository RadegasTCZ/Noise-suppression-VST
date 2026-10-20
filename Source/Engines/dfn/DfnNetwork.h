// Ported from DeepFilterNet libDF, commit d375b2d. Portions Copyright (c) 2021 Hendrik Schröter, used under the
// MIT licence (ThirdParty/deepfilternet/LICENSE-MIT); the port and its changes are part of this project (GPLv3).
#pragma once

#include <memory>

namespace nsp::dfn
{
class Model;

/** DeepFilterNet3's three networks (encoder, ERB decoder, DF decoder), run one hop (10 ms) at a time.
    The weights are shared and read-only after init(); each channel has its own State. The run methods
    are allocation-free. */
class Network
{
public:
    struct State                                        // one per channel; no heap members
    {
        float featErbHist[3][32];  float featSpecHist[3][2][96];   // encoder time history (oldest first)
        float hEnc[256], hErb1[256], hErb2[256], hDf1[256], hDf2[256];
        float c0Hist[5][64 * 96];                                 // DF-decoder pathway history (oldest first)
        float e0[64 * 32], e1[64 * 16], e2[64 * 8], e3[64 * 8], c0[64 * 96];
        float emb[512], lsnr;                                     // encoder outputs (lsnr in dB)
        float gains[32];                                          // ERB decoder output (sigmoid)
        float coefs[96 * 10];                                     // DF decoder output: [bin][tap k][re, im]
        void clear() noexcept;                                    // == value-initialised
    };

    Network();
    ~Network();
    Network (const Network&) = delete;
    Network& operator= (const Network&) = delete;

    /** Copies (and transposes) the weights out of a loaded model; not for the audio thread.
        False if the model is not loaded. */
    bool init (const Model&);

    /** Shifts the encoder's time history and runs the encoder. featErb32: 32 ERB features;
        featSpec2x96: [re, im][96] (the first 96 bins). */
    void encode (State&, const float* featErb32, const float* featSpec2x96) const noexcept;
    void decodeErb (State&) const noexcept;
    /** Pushes this hop's c0 into c0Hist, then runs the DF decoder. Upstream runs it as a separate pulsed
        model: its time-conv history advances only on hops where the DF stage runs, so skipped hops must
        not call this. coefs[f * 10 + k * 2 + 0/1] = re/im of tap k (0 = oldest frame) of bin f. */
    void decodeDf (State&) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace nsp::dfn
