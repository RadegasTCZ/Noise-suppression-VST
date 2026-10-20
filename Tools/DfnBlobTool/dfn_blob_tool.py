r"""DeepFilterNet3 blob tool. Run by hand (not part of the build), in a venv, from the repo root:

    python -m venv Tools\DfnBlobTool\.venv && Tools\DfnBlobTool\.venv\Scripts\pip install -r Tools\DfnBlobTool\requirements.txt
    Tools\DfnBlobTool\.venv\Scripts\python Tools\DfnBlobTool\dfn_blob_tool.py <sub-command> ...

Sub-commands:
  blob <DeepFilterNet3_onnx.tar.gz> <out.bin>
        Writes the weight blob read by Source/Engines/dfn/DfnModel.cpp; prints SHA-256 of tarball and blob.
  stage-refs <DeepFilterNet3_onnx.tar.gz> <outdir>
        Writes stage_in.bin / stage_out.bin: synthetic features and the onnxruntime outputs for them
        (encoder, ERB decoder, DF decoder), the reference for Tests/DfnNetworkTests.cpp.
  cli-refs <deep-filter.exe> <audio-dir> <outdir>
        Runs the official CLI (-D) on every 48 kHz mono *.wav in audio-dir (local-only end-to-end references).

Blob format (little-endian):
  char[4] "DFN3"; uint32 version = 1
  int32 sr, fftSize, hopSize, nbErb, nbDf, dfOrder, dfLookahead, convLookahead, minNbErbFreqs
  float32 normTau
  uint32 numTensors
  repeat numTensors: uint32 ndim; uint32 dims[ndim]; float32 data[prod(dims)]
Tensors are in ONNX node order: enc, erb_dec, df_dec; per node of type Conv / ConvTranspose / Einsum /
GRU / MatMul / Add the inputs that are initializers, plus the Constant inputs of GRU nodes.
"""
import configparser
import hashlib
import struct
import subprocess
import sys
import tarfile
import wave
from pathlib import Path

import numpy as np
import onnx
from onnx import numpy_helper

NAMES = ("enc", "erb_dec", "df_dec")
NODE_TYPES = ("Conv", "ConvTranspose", "Einsum", "GRU", "MatMul", "Add")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_tarball(path):
    """Returns (config.ini text, {name: onnx bytes})."""
    ini, models = None, {}
    with tarfile.open(path) as t:
        for m in t.getmembers():
            base = Path(m.name).name
            if base == "config.ini":
                ini = t.extractfile(m).read().decode()
            elif base.endswith(".onnx") and base[:-5] in NAMES:
                models[base[:-5]] = t.extractfile(m).read()
    if ini is None or set(models) != set(NAMES):
        sys.exit("tarball lacks config.ini or one of enc/erb_dec/df_dec.onnx")
    return ini, models


def weights_in_node_order(onnx_bytes):
    g = onnx.load_from_string(onnx_bytes).graph
    inits = {t.name: numpy_helper.to_array(t) for t in g.initializer}
    consts = {n.output[0]: numpy_helper.to_array(n.attribute[0].t) for n in g.node if n.op_type == "Constant"}
    ws = []
    for n in g.node:
        if n.op_type not in NODE_TYPES:
            continue
        for i in n.input:
            if i in inits:
                ws.append(inits[i].astype(np.float32))
            elif n.op_type == "GRU" and i in consts:
                ws.append(consts[i].astype(np.float32))
    return ws


def cmd_blob(tar, out):
    ini, models = read_tarball(tar)
    cp = configparser.ConfigParser()
    cp.read_string(ini)
    df, dn = cp["df"], cp["deepfilternet"]
    ints = [int(df["sr"]), int(df["fft_size"]), int(df["hop_size"]), int(df["nb_erb"]), int(df["nb_df"]),
            int(df["df_order"]), int(df["df_lookahead"]), int(dn["conv_lookahead"]), int(df["min_nb_erb_freqs"])]
    tensors = [w for name in NAMES for w in weights_in_node_order(models[name])]
    with open(out, "wb") as f:
        f.write(b"DFN3" + struct.pack("<I", 1))
        f.write(struct.pack("<9i", *ints))
        f.write(struct.pack("<f", float(df["norm_tau"])))
        f.write(struct.pack("<I", len(tensors)))
        for w in tensors:
            f.write(struct.pack("<I", w.ndim) + struct.pack(f"<{w.ndim}I", *w.shape))
            f.write(np.ascontiguousarray(w, dtype="<f4").tobytes())
    print(f"{len(tensors)} tensors, {sum(w.size for w in tensors)} floats, config {ints} normTau {df['norm_tau']}")
    print(f"tarball {sha256(tar)}")
    print(f"blob    {sha256(out)}  {Path(out).stat().st_size} bytes")


def cmd_stage_refs(tar, outdir):
    import onnxruntime as ort
    _, models = read_tarball(tar)
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    T = 64
    rng = np.random.default_rng(1234)
    t = np.arange(T)[:, None]
    feat_erb = (np.sin(0.3 * t + np.arange(32)[None, :] * 0.2) * 0.8 + rng.normal(0, 0.3, (T, 32))).astype(np.float32)
    feat_spec = (rng.normal(0, 0.5, (2, T, 96)) * (1 + 0.5 * np.sin(0.2 * t))[None]).astype(np.float32)
    feat_erb = feat_erb[None, None]  # [1,1,T,32]
    feat_spec = feat_spec[None]      # [1,2,T,96]

    so = ort.SessionOptions()
    so.intra_op_num_threads = 1
    enc = ort.InferenceSession(models["enc"], so)
    e0, e1, e2, e3, emb, c0, lsnr = enc.run(None, {"feat_erb": feat_erb, "feat_spec": feat_spec})
    erb = ort.InferenceSession(models["erb_dec"], so)
    (m,) = erb.run(None, {"emb": emb, "e3": e3, "e2": e2, "e1": e1, "e0": e0})
    dfd = ort.InferenceSession(models["df_dec"], so)
    coefs, _alpha = dfd.run(None, {"emb": emb, "c0": c0})

    def dump(name, *arrs):
        with open(outdir / name, "wb") as f:
            for a in arrs:
                f.write(np.ascontiguousarray(a, dtype="<f4").tobytes())

    dump("stage_in.bin", feat_erb[0, 0], feat_spec[0].transpose(1, 0, 2))   # [T][32], [T][2][96]
    dump("stage_out.bin", lsnr[0, :, 0], emb[0], m[0, 0], coefs[0])         # [T], [T][512], [T][32], [T][960]
    print("lsnr range", lsnr.min(), lsnr.max(), "gains range", m.min(), m.max(), "coefs abs max", np.abs(coefs).max())
    for n in ("stage_in.bin", "stage_out.bin"):
        print(f"{n} {(outdir / n).stat().st_size} bytes  {sha256(outdir / n)}")


def cmd_cli_refs(exe, audio_dir, outdir):
    Path(outdir).mkdir(parents=True, exist_ok=True)
    for wav in sorted(Path(audio_dir).glob("*.wav")):
        with wave.open(str(wav)) as w:
            if w.getframerate() != 48000 or w.getnchannels() != 1:
                print("skip (not 48 kHz mono):", wav.name)
                continue
        subprocess.run([exe, "-D", "-o", str(outdir), str(wav)], check=True)
        print("ok", wav.name)


def main(argv):
    cmds = {"blob": (cmd_blob, 2), "stage-refs": (cmd_stage_refs, 2), "cli-refs": (cmd_cli_refs, 3)}
    if len(argv) < 2 or argv[1] not in cmds or len(argv) - 2 != cmds[argv[1]][1]:
        sys.exit(__doc__)
    cmds[argv[1]][0](*argv[2:])


if __name__ == "__main__":
    main(sys.argv)
