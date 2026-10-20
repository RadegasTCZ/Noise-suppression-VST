#include <JuceHeader.h>
#include "rnnoise.h"
#include "rnnoise_reference/RnnoiseRef.h"
#include "TestPaths.h"
#include "TestSignals.h"
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
constexpr int F = 480;
constexpr int R = 48000;

/** Embedded model blob + the facts recorded in ThirdParty/UPSTREAM.md (RNNoise section). */
struct EmbeddedBlob
{
    const char* data;
    int size;
    int expectedSize;
    const char* sha256;
};

EmbeddedBlob standardBlob()
{
    return { BinaryData::rnnoise_standard_bin, BinaryData::rnnoise_standard_binSize, 3544320,
             "1ad07b428be34c74d9678d05b3c387b73ccf0e6203a1a5f9ce58689b30b303c8" };
}

EmbeddedBlob lightBlob()
{
    return { BinaryData::rnnoise_light_bin, BinaryData::rnnoise_light_binSize, 1553664,
             "69fbd33c10d90e294673427f71fc4a592398d847efb2db19c99484f88b9343e4" };
}

/** A DenoiseState in owned memory (rnnoise_get_size() bytes). */
struct State
{
    std::vector<unsigned char> mem = std::vector<unsigned char> ((size_t) rnnoise_get_size(), 0);
    DenoiseState* get() { return reinterpret_cast<DenoiseState*> (mem.data()); }
};

/** Owned, operator-new-aligned copy of a blob wrapped as an RNNModel. Freed with std::free:
    rnnoise_model_from_buffer leaves RNNModel::file uninitialised, so rnnoise_model_free would fclose garbage. */
struct Model
{
    Model (const void* data, int size)
        : bytes ((const unsigned char*) data, (const unsigned char*) data + size),
          model (rnnoise_model_from_buffer (bytes.data(), (int) bytes.size())) {}
    ~Model() { std::free (model); }
    Model (const Model&) = delete;
    Model& operator= (const Model&) = delete;

    std::vector<unsigned char> bytes;
    RNNModel* model;
};

/** Frames of x through st, scaled to the int16 range and back as the engine does. */
std::vector<float> run (DenoiseState* st, const std::vector<float>& x)
{
    std::vector<float> y (x.size(), 0.0f);
    float a[F], b[F];

    for (size_t pos = 0; pos + (size_t) F <= x.size(); pos += (size_t) F)
    {
        for (int i = 0; i < F; ++i)
            a[i] = x[pos + (size_t) i] * 32768.0f;

        rnnoise_process_frame (st, b, a);

        for (int i = 0; i < F; ++i)
            y[pos + (size_t) i] = b[i] * (1.0f / 32768.0f);
    }

    return y;
}

/** 0.5 s of silence (exercises the "no audio" path), then 5.5 s speech-like signal with noise. */
std::vector<float> testInput()
{
    auto x = nsp::test::speechLike (6 * R, 61);
    std::fill (x.begin(), x.begin() + R / 2, 0.0f);
    return x;
}

int mismatches (const std::vector<float>& a, const std::vector<float>& b)
{
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp (&a[i], &b[i], sizeof (float)) != 0)
            ++n;
    return n;
}

class RnnoiseLibraryTests final : public juce::UnitTest
{
public:
    RnnoiseLibraryTests() : juce::UnitTest ("RNNoise library (vendored)", "RnnoiseLib") {}

    void runTest() override
    {
        beginTest ("frame size 480, state size > 0");
        expectEquals (rnnoise_get_frame_size(), F);
        expectGreaterThan (rnnoise_get_size(), 0);

        beginTest ("embedded blobs: size and SHA-256 as recorded in ThirdParty/UPSTREAM.md");
        {
            const auto upstreamMd = nsp::test::testsDir().getParentDirectory().getChildFile ("ThirdParty").getChildFile ("UPSTREAM.md");
            expect (upstreamMd.existsAsFile(), upstreamMd.getFullPathName());
            const auto recorded = upstreamMd.loadFileAsString();

            for (const auto& blob : { standardBlob(), lightBlob() })
            {
                expectEquals (blob.size, blob.expectedSize);
                const auto hash = juce::SHA256 (blob.data, (size_t) blob.size).toHexString();
                logMessage ("    blob " + juce::String (blob.size) + " bytes, SHA-256 " + hash);
                expectEquals (hash, juce::String (blob.sha256));
                expect (recorded.contains (blob.sha256), juce::String ("UPSTREAM.md must record ") + blob.sha256);
            }
        }

        beginTest ("both embedded models load (rnnoise_init == 0)");
        {
            const auto s = standardBlob();
            const auto l = lightBlob();
            Model standard (s.data, s.size), light (l.data, l.size);
            State a, b;
            expectEquals (rnnoise_init (a.get(), standard.model), 0);
            expectEquals (rnnoise_init (b.get(), light.model), 0);
        }

        const auto x = testInput();

        beginTest ("blob-loaded Standard is bit-exact with upstream's compiled-in rnnoise_data.c");
        std::vector<float> standardOut;
        if (nsp_ref_rnnoise_standard_available() == 0)
        {
            logMessage ("    SKIPPED: upstream rnnoise_data.c not found on the header search path "
                        "(optional reference sources, see README 'Tests')");
        }
        else
        {
            const auto s = standardBlob();
            Model model (s.data, s.size);
            State blobState, refState;
            expectEquals (rnnoise_init (blobState.get(), model.model), 0);
            expectEquals (nsp_ref_rnnoise_init_standard (refState.get()), 0);
            standardOut = run (blobState.get(), x);
            const auto ref = run (refState.get(), x);
            expectEquals (mismatches (standardOut, ref), 0);
            expectGreaterThan (nsp::test::rmsDb (standardOut.data(), (int) standardOut.size()), -60.0f);
        }

        beginTest ("blob-loaded Light is bit-exact with upstream's compiled-in rnnoise_data_little.c");
        if (nsp_ref_rnnoise_light_available() == 0 || standardOut.empty())
        {
            logMessage ("    SKIPPED: upstream rnnoise_data_little.c / rnnoise_data.c not found on the header search path "
                        "(optional reference sources, see README 'Tests')");
        }
        else
        {
            const auto l = lightBlob();
            Model model (l.data, l.size);
            State blobState, refState;
            expectEquals (rnnoise_init (blobState.get(), model.model), 0);
            expectEquals (nsp_ref_rnnoise_init_light (refState.get()), 0);
            const auto lightOut = run (blobState.get(), x);
            const auto ref = run (refState.get(), x);
            expectEquals (mismatches (lightOut, ref), 0);
            expectGreaterThan (mismatches (lightOut, standardOut), (int) x.size() / 2, "Light must differ from Standard");
        }

        beginTest ("rnnoise_set_safety_floor off (default) leaves output bit-identical to upstream behaviour");
        {
            // x has pauses with -40 dBFS noise, which the floor fills when on: a state with the floor on must differ.
            const auto s = standardBlob();
            Model model (s.data, s.size);
            State byDefault, explicitOff, toggled, on;
            for (auto* st : { &byDefault, &explicitOff, &toggled, &on })
                expectEquals (rnnoise_init (st->get(), model.model), 0);
            rnnoise_set_safety_floor (explicitOff.get(), 0);
            rnnoise_set_safety_floor (toggled.get(), 1);
            rnnoise_set_safety_floor (toggled.get(), 0);
            rnnoise_set_safety_floor (on.get(), 1);
            const auto outDefault = run (byDefault.get(), x);
            expectEquals (mismatches (run (explicitOff.get(), x), outDefault), 0, "explicitly off");
            expectEquals (mismatches (run (toggled.get(), x), outDefault), 0, "switched on and off again");
            expectGreaterThan (mismatches (run (on.get(), x), outDefault), 0, "the floor changes this output when on");

            if (nsp_ref_rnnoise_standard_available() == 0)
            {
                logMessage ("    upstream comparison SKIPPED: upstream rnnoise_data.c not found (optional reference sources)");
            }
            else
            {
                State refState;
                expectEquals (nsp_ref_rnnoise_init_standard (refState.get()), 0);
                expectEquals (mismatches (outDefault, run (refState.get(), x)), 0, "== upstream compiled-in Standard");
            }
        }

        beginTest ("truncated, junk and empty blobs fail to load (rnnoise_init == -1)");
        {
            const auto s = standardBlob();
            const std::vector<unsigned char> truncated ((const unsigned char*) s.data, (const unsigned char*) s.data + s.size / 3);
            const std::vector<unsigned char> junk (4096, 0x5a);
            const unsigned char none[1] = { 0 };

            Model t (truncated.data(), (int) truncated.size()), j (junk.data(), (int) junk.size()), e (none, 0);
            State a, b, c;
            expectEquals (rnnoise_init (a.get(), t.model), -1);
            expectEquals (rnnoise_init (b.get(), j.model), -1);
            expectEquals (rnnoise_init (c.get(), e.model), -1);
        }
    }
};

static RnnoiseLibraryTests rnnoiseLibraryTests;
} // namespace
