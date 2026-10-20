#include <JuceHeader.h>
#include "../Source/Engines/dfn/DfnModel.h"
#include "../Source/Engines/dfn/DfnNetwork.h"
#include "AllocCounter.h"
#include "TestPaths.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
constexpr int T = 64;
constexpr size_t kDfnBlobSize = 8539604;
const char* const kDfnBlobSha256 = "c2d1567a952a97f7fbb227a92f004ca7111baef0b478c54c7dd23ec7564c9fae";

/** The reference hops written by Tools/DfnBlobTool (stage-refs): onnxruntime on synthetic features. */
struct StageRefs
{
    std::vector<float> in, out;   // stage_in.bin: [T][32] + [T][2][96]; stage_out.bin: [T] + [T][512] + [T][32] + [T][960]
    const float* erb (int t) const { return in.data() + t * 32; }
    const float* spec (int t) const { return in.data() + T * 32 + t * 192; }
    const float* lsnr() const { return out.data(); }
    const float* emb() const { return out.data() + T; }
    const float* gains() const { return emb() + T * 512; }
    const float* coefs() const { return gains() + T * 32; }
};

bool loadFloats (const juce::File& f, std::vector<float>& v, size_t expectedFloats)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb) || mb.getSize() != expectedFloats * sizeof (float))
        return false;
    v.resize (expectedFloats);
    std::memcpy (v.data(), mb.getData(), mb.getSize());
    return true;
}

bool loadRefs (StageRefs& r)
{
    const auto dir = nsp::test::testsDir().getChildFile ("dfn_reference");
    return loadFloats (dir.getChildFile ("stage_in.bin"), r.in, (size_t) T * (32 + 192))
        && loadFloats (dir.getChildFile ("stage_out.bin"), r.out, (size_t) T * (1 + 512 + 32 + 960));
}

/** One channel's outputs for all T hops, in the stage_out.bin layout. */
struct Outputs
{
    std::vector<float> lsnr = std::vector<float> (T), emb = std::vector<float> (T * 512),
                       gains = std::vector<float> (T * 32), coefs = std::vector<float> (T * 960);
};

void hop (const nsp::dfn::Network& net, nsp::dfn::Network::State& s, const float* erb, const float* spec)
{
    net.encode (s, erb, spec);
    net.decodeErb (s);
    net.decodeDf (s);
}

void runAll (const nsp::dfn::Network& net, nsp::dfn::Network::State& s, const StageRefs& r, Outputs& o)
{
    for (int t = 0; t < T; ++t)
    {
        hop (net, s, r.erb (t), r.spec (t));
        o.lsnr[(size_t) t] = s.lsnr;
        std::memcpy (&o.emb[(size_t) t * 512], s.emb, sizeof s.emb);
        std::memcpy (&o.gains[(size_t) t * 32], s.gains, sizeof s.gains);
        std::memcpy (&o.coefs[(size_t) t * 960], s.coefs, sizeof s.coefs);
    }
}

float maxAbsErr (const std::vector<float>& a, const float* ref)
{
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        m = std::fmax (m, std::fabs (a[i] - ref[i]));
    return m;
}

bool identical (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

/** True if the blob was accepted. A rejected blob must also leave the model unloaded. */
bool accepts (const std::vector<unsigned char>& blob, bool& leftLoaded)
{
    nsp::dfn::Model m;
    const bool ok = m.load (blob.data(), blob.size());
    leftLoaded = ! ok && m.isLoaded();
    return ok;
}

class DfnNetworkTests final : public juce::UnitTest
{
public:
    DfnNetworkTests() : juce::UnitTest ("DeepFilterNet network", "DfnNetwork") {}

    void runTest() override
    {
        const auto* blobData = (const unsigned char*) BinaryData::dfn3_standard_bin;
        const auto blobSize = (size_t) BinaryData::dfn3_standard_binSize;

        beginTest ("embedded blob loads: 66 tensors, config 48000/960/480/32/96/5/2/2/2/1.0");
        {
            nsp::dfn::Model m;
            expect (m.load (BinaryData::dfn3_standard_bin, blobSize));
            expect (m.isLoaded());
            expectEquals (nsp::dfn::Model::kNumTensors, 66);
            const auto& c = m.config();
            expectEquals (c.sr, 48000);
            expectEquals (c.fftSize, 960);
            expectEquals (c.hopSize, 480);
            expectEquals (c.nbErb, 32);
            expectEquals (c.nbDf, 96);
            expectEquals (c.dfOrder, 5);
            expectEquals (c.dfLookahead, 2);
            expectEquals (c.convLookahead, 2);
            expectEquals (c.minNbErbFreqs, 2);
            expectEquals (c.normTau, 1.0f);
            expectEquals ((int) m.tensor (0).size(), 64 * 1 * 3 * 3);
        }

        beginTest ("embedded blob: size and SHA-256 as recorded in ThirdParty/UPSTREAM.md");
        {
            expectEquals ((juce::int64) blobSize, (juce::int64) kDfnBlobSize);
            const auto hash = juce::SHA256 (blobData, blobSize).toHexString();
            logMessage ("    blob " + juce::String ((juce::int64) blobSize) + " bytes, SHA-256 " + hash);
            expectEquals (hash, juce::String (kDfnBlobSha256));

            const auto upstreamMd = nsp::test::testsDir().getParentDirectory().getChildFile ("ThirdParty").getChildFile ("UPSTREAM.md");
            expect (upstreamMd.existsAsFile(), upstreamMd.getFullPathName());
            expect (upstreamMd.loadFileAsString().contains (kDfnBlobSha256), juce::String ("UPSTREAM.md must record ") + kDfnBlobSha256);
        }

        beginTest ("corrupt blobs are rejected without crashing: truncated, wrong magic, wrong version, wrong config, wrong shape");
        {
            const std::vector<unsigned char> good (blobData, blobData + blobSize);
            auto truncated = good;   truncated.resize (blobSize / 2);
            auto magic = good;       magic[0] = 'X';
            auto version = good;     version[4] = 2;
            auto config = good;      { const std::int32_t sr = 44100; std::memcpy (&config[8], &sr, 4); }
            auto shape = good;       { const std::uint32_t d = 63; std::memcpy (&shape[56], &d, 4); }   // first tensor, dims[0]
            auto trailing = good;    trailing.push_back (0);

            bool left = false;
            expect (accepts (good, left), "unmodified copy loads");
            for (const auto& [label, blob] : { std::pair<const char*, const std::vector<unsigned char>*> { "truncated", &truncated },
                                              { "magic", &magic }, { "version", &version }, { "config", &config },
                                              { "shape", &shape }, { "trailing bytes", &trailing } })
            {
                expect (! accepts (*blob, left), juce::String (label) + " must be rejected");
                expect (! left, juce::String (label) + " must leave the model unloaded");
            }

            nsp::dfn::Model m;
            expect (! m.load (nullptr, 0));
            expect (! m.isLoaded());
        }

        StageRefs refs;
        const bool haveRefs = loadRefs (refs);

        nsp::dfn::Model model;
        expect (model.load (BinaryData::dfn3_standard_bin, blobSize));
        nsp::dfn::Network net;
        expect (net.init (model));

        beginTest ("all three networks match onnxruntime over 64 synthetic hops");
        {
            expect (haveRefs, "Tests/dfn_reference/stage_in.bin / stage_out.bin must be present");
            if (haveRefs)
            {
                auto s = std::make_unique<nsp::dfn::Network::State>();
                Outputs o;
                runAll (net, *s, refs, o);

                const float eL = maxAbsErr (o.lsnr, refs.lsnr());
                const float eE = maxAbsErr (o.emb, refs.emb());
                const float eG = maxAbsErr (o.gains, refs.gains());
                const float eC = maxAbsErr (o.coefs, refs.coefs());
                logMessage ("    max abs error vs onnxruntime: lsnr " + juce::String (eL, 8) + " dB, emb " + juce::String (eE, 8)
                            + ", gains " + juce::String (eG, 8) + ", coefs " + juce::String (eC, 8));
                expectLessOrEqual (eL, 1.0e-3f);
                expectLessOrEqual (eE, 1.0e-4f);
                expectLessOrEqual (eG, 1.0e-4f);
                expectLessOrEqual (eC, 1.0e-4f);
            }
        }

        beginTest ("State::clear() makes the networks behave exactly like a fresh state");
        if (haveRefs)
        {
            auto s = std::make_unique<nsp::dfn::Network::State>();
            Outputs first, second;
            runAll (net, *s, refs, first);
            s->clear();
            runAll (net, *s, refs, second);
            expect (identical (first.lsnr, second.lsnr));
            expect (identical (first.emb, second.emb));
            expect (identical (first.gains, second.gains));
            expect (identical (first.coefs, second.coefs));
        }

        beginTest ("encode, decodeErb and decodeDf do not allocate");
        if (haveRefs)
        {
            auto s = std::make_unique<nsp::dfn::Network::State>();
            int allocs = -1;
            {
                nsp::test::ScopedAllocCheck check;
                for (int t = 0; t < 8; ++t)
                    hop (net, *s, refs.erb (t), refs.spec (t));
                allocs = check.count();
            }
            expectEquals (allocs, 0);
        }

        beginTest ("CPU: networks, stereo (two states), all stages every hop, at least 5x real time in Release (SSE2)");
       #if JUCE_DEBUG
        logMessage ("    skipped in Debug");
       #else
        if (haveRefs)
        {
            auto s0 = std::make_unique<nsp::dfn::Network::State>();
            auto s1 = std::make_unique<nsp::dfn::Network::State>();
            nsp::dfn::Network::State* st[2] = { s0.get(), s1.get() };
            constexpr int hops = 6000;   // 60 s of audio per channel at 10 ms hops

            double best = 1.0e9;
            for (int rep = 0; rep < 3; ++rep)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (int n = 0; n < hops; ++n)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const int t = (n + ch * 7) % T;
                        hop (net, *st[ch], refs.erb (t), refs.spec (t));
                    }
                best = std::fmin (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
            }
            const double rtf = 60.0 / best;
            logMessage ("    stereo networks: " + juce::String (best, 3) + " s for 60 s of audio, " + juce::String (rtf, 1) + "x real time");
            expectGreaterThan (rtf, 5.0);
        }
       #endif
    }
};

static DfnNetworkTests dfnNetworkTests;
} // namespace
