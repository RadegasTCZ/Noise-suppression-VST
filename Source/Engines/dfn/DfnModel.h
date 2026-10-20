// Ported from DeepFilterNet libDF, commit d375b2d. Portions Copyright (c) 2021 Hendrik Schröter, used under the
// MIT licence (ThirdParty/deepfilternet/LICENSE-MIT); the port and its changes are part of this project (GPLv3).
#pragma once

#include <cstddef>
#include <vector>

namespace nsp::dfn
{
/** DeepFilterNet3 parameters from the model's config.ini ([df] and [deepfilternet]). */
struct Config
{
    int sr, fftSize, hopSize, nbErb, nbDf, dfOrder, dfLookahead, convLookahead, minNbErbFreqs;
    float normTau;
};

/** The DeepFilterNet3 weights, parsed from the blob written by Tools/DfnBlobTool (format: see its docstring).
    Not for the audio thread (load() allocates). */
class Model
{
public:
    static constexpr int kNumTensors = 66;

    /** Parses and validates the blob (magic, version, config, every tensor's shape, exact size). Copies the
        weights, so `data` need not be aligned and may go away. Returns false and leaves the model unloaded on
        any mismatch. */
    bool load (const void* data, size_t size);
    bool isLoaded() const noexcept { return loaded; }

    /** Valid when loaded. */
    const Config& config() const noexcept { return cfg; }

    /** Tensor `index` (0 .. kNumTensors - 1) in ONNX node order: encoder, ERB decoder, DF decoder. */
    const std::vector<float>& tensor (int index) const;

private:
    bool loaded = false;
    Config cfg {};
    std::vector<std::vector<float>> tensors;
};
} // namespace nsp::dfn
