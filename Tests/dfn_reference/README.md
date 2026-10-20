# DeepFilterNet3 stage references

Synthetic features (no audio) and the outputs of the three official ONNX networks (encoder, ERB decoder, DF decoder) for them, run with onnxruntime over the whole 64-hop sequence. `Tests/DfnNetworkTests.cpp` compares the C++ network against them hop by hop. All float32 little-endian.

- `stage_in.bin` (57,344 bytes): `[64][32]` feat_erb, then `[64][2][96]` feat_spec.
- `stage_out.bin` (385,280 bytes): `[64]` lsnr (dB), `[64][512]` emb, `[64][32]` ERB gains, `[64][960]` DF coefficients (`[bin 96][tap 5][re, im]`).

Regenerate (from the repository root, with the blob tool's venv, see `Tools/DfnBlobTool/dfn_blob_tool.py`):

    Tools\DfnBlobTool\.venv\Scripts\python Tools\DfnBlobTool\dfn_blob_tool.py stage-refs ..\_upstream\deepfilternet\models\DeepFilterNet3_onnx.tar.gz Tests\dfn_reference

The output must be byte-identical (SHA-256 `c71a9335...` / `0a495e37...`) with the pinned package versions.
