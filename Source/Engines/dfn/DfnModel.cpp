// Ported from DeepFilterNet libDF, commit d375b2d. Portions Copyright (c) 2021 Hendrik Schröter, used under the
// MIT licence (ThirdParty/deepfilternet/LICENSE-MIT); the port and its changes are part of this project (GPLv3).
#include "DfnModel.h"

#include <JuceHeader.h>

#include <cstdint>
#include <cstring>

namespace nsp::dfn
{
namespace
{
struct Shape
{
    int nd;
    int d[4];
};

constexpr Config kExpectedConfig { 48000, 960, 480, 32, 96, 5, 2, 2, 2, 1.0f };

// Every tensor's shape, in blob order (enc, erb_dec, df_dec).
constexpr Shape kShapes[Model::kNumTensors] = {
    // enc (25)
    { 4, { 64, 1, 3, 3 } }, { 1, { 64 } },
    { 4, { 64, 1, 1, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 4, { 64, 1, 1, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 4, { 64, 1, 1, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 4, { 64, 1, 3, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 4, { 64, 1, 1, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 3, { 32, 96, 16 } },
    { 3, { 16, 32, 16 } },
    { 3, { 1, 768, 256 } }, { 3, { 1, 768, 256 } }, { 2, { 1, 1536 } },
    { 3, { 16, 16, 32 } },
    { 2, { 512, 1 } }, { 1, { 1 } },
    // erb_dec (27)
    { 3, { 16, 32, 16 } },
    { 3, { 1, 768, 256 } }, { 3, { 1, 768, 256 } }, { 2, { 1, 1536 } },
    { 3, { 1, 768, 256 } }, { 3, { 1, 768, 256 } }, { 2, { 1, 1536 } },
    { 3, { 16, 16, 32 } },
    { 4, { 64, 1, 1, 1 } }, { 1, { 64 } }, { 4, { 64, 1, 1, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 4, { 64, 1, 1, 1 } }, { 1, { 64 } }, { 4, { 64, 1, 1, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 4, { 64, 1, 1, 1 } }, { 1, { 64 } }, { 4, { 64, 1, 1, 3 } }, { 4, { 64, 64, 1, 1 } }, { 1, { 64 } },
    { 4, { 64, 1, 1, 1 } }, { 1, { 64 } },
    { 4, { 1, 64, 1, 3 } }, { 1, { 1 } },
    // df_dec (14)
    { 3, { 8, 64, 32 } },
    { 3, { 1, 768, 256 } }, { 3, { 1, 768, 256 } }, { 2, { 1, 1536 } },
    { 3, { 1, 768, 256 } }, { 3, { 1, 768, 256 } }, { 2, { 1, 1536 } },
    { 3, { 16, 32, 16 } },
    { 4, { 10, 32, 5, 1 } }, { 4, { 10, 10, 1, 1 } }, { 1, { 10 } },
    { 2, { 256, 1 } }, { 1, { 1 } },
    { 3, { 16, 16, 60 } },
};

/** Bounds-checked little-endian reader over the blob. */
class Reader
{
public:
    Reader (const unsigned char* d, size_t n) : p (d), end (d + n) {}

    bool u32 (std::uint32_t& v) { return take (&v, 4); }
    bool i32 (std::int32_t& v) { return take (&v, 4); }
    bool f32 (float& v) { return take (&v, 4); }
    bool bytes (void* dst, size_t n) { return take (dst, n); }
    bool atEnd() const { return p == end; }

private:
    bool take (void* dst, size_t n)
    {
        if ((size_t) (end - p) < n)
            return false;
        std::memcpy (dst, p, n);
        p += n;
        return true;
    }

    const unsigned char* p;
    const unsigned char* end;
};
} // namespace

bool Model::load (const void* data, size_t size)
{
    loaded = false;
    cfg = {};
    tensors.clear();

    if (data == nullptr)
        return false;

    Reader r (static_cast<const unsigned char*> (data), size);
    char magic[4];
    std::uint32_t version = 0;
    if (! r.bytes (magic, 4) || std::memcmp (magic, "DFN3", 4) != 0 || ! r.u32 (version) || version != 1)
        return false;

    Config c {};
    std::int32_t* ints[] = { &c.sr, &c.fftSize, &c.hopSize, &c.nbErb, &c.nbDf, &c.dfOrder, &c.dfLookahead, &c.convLookahead, &c.minNbErbFreqs };
    for (auto* i : ints)
        if (! r.i32 (*i))
            return false;
    std::uint32_t numTensors = 0;
    if (! r.f32 (c.normTau) || ! r.u32 (numTensors) || numTensors != (std::uint32_t) kNumTensors)
        return false;

    const auto& e = kExpectedConfig;
    if (c.sr != e.sr || c.fftSize != e.fftSize || c.hopSize != e.hopSize || c.nbErb != e.nbErb || c.nbDf != e.nbDf
        || c.dfOrder != e.dfOrder || c.dfLookahead != e.dfLookahead || c.convLookahead != e.convLookahead
        || c.minNbErbFreqs != e.minNbErbFreqs || c.normTau != e.normTau)
        return false;

    std::vector<std::vector<float>> t;
    t.reserve ((size_t) kNumTensors);
    for (const auto& shape : kShapes)
    {
        std::uint32_t nd = 0;
        if (! r.u32 (nd) || nd != (std::uint32_t) shape.nd)
            return false;

        size_t count = 1;
        for (int i = 0; i < shape.nd; ++i)
        {
            std::uint32_t dim = 0;
            if (! r.u32 (dim) || dim != (std::uint32_t) shape.d[i])
                return false;
            count *= (size_t) dim;
        }

        std::vector<float> w (count);
        if (! r.bytes (w.data(), count * sizeof (float)))
            return false;
        t.push_back (std::move (w));
    }

    if (! r.atEnd())
        return false;

    cfg = c;
    tensors = std::move (t);
    loaded = true;
    return true;
}

const std::vector<float>& Model::tensor (int index) const
{
    jassert (index >= 0 && (size_t) index < tensors.size());
    return tensors[(size_t) index];
}
} // namespace nsp::dfn
