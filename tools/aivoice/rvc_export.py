"""Export helpers for Voice CTX AI Voice: RVC v2 content encoder and voice generators -> ONNX.

The generator is exported in a *streaming* form: it encodes a fixed context window but decodes only
the newest frames. RVC's relative attention bakes the sequence length into the graph, so every voice
is exported for one fixed streaming configuration (see BLOCK_MS / CONTEXT_MS).
"""
from __future__ import annotations

import hashlib
import json
import pickle
import subprocess
import sys
import urllib.request
from pathlib import Path

import torch

# Streaming configuration. Format 2 is the low-latency default (50 ms hop,
# 400 ms context, 10 ms crossfade); format 1 remains available for models that
# must stay compatible with older Voice CTX builds.
DEFAULT_BLOCK_MS = 50
DEFAULT_CONTEXT_MS = 400
DEFAULT_MARGIN_FRAMES = 1
FORMAT_VERSION_V1 = 1
FORMAT_VERSION_V2 = 2
DEFAULT_FORMAT = FORMAT_VERSION_V2

RVC_REPO = "https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI.git"
RVC_COMMIT = "81eed5e8f68b6bed1789f682fe78cdd324495afc"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def download(url: str, dest: Path, expected_sha256: str) -> Path:
    if dest.exists() and sha256(dest) == expected_sha256:
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    print(f"downloading {url}")
    urllib.request.urlretrieve(url, tmp)
    actual = sha256(tmp)
    if actual != expected_sha256:
        tmp.unlink()
        raise RuntimeError(f"SHA-256 mismatch for {url}: expected {expected_sha256}, got {actual}")
    tmp.replace(dest)
    return dest


def ensure_rvc_source(cache: Path) -> Path:
    """Fetches the RVC model definitions (MIT) at a pinned commit and puts them on sys.path."""
    src = cache / f"rvc-{RVC_COMMIT[:10]}"
    if not (src / "infer" / "module" / "models.py").exists():
        src.mkdir(parents=True, exist_ok=True)
        for args in (["init", "-q"], ["fetch", "-q", "--depth", "1", RVC_REPO, RVC_COMMIT], ["checkout", "-q", "FETCH_HEAD"]):
            subprocess.run(["git", "-C", str(src), *args], check=True)
    if str(src) not in sys.path:
        sys.path.insert(0, str(src))
    return src


class _Stub:
    def __init__(self, *a, **k): pass
    def __setstate__(self, state): pass


class _FairseqUnpickler(pickle.Unpickler):
    """Reads a fairseq checkpoint without fairseq: config objects become inert stubs."""
    def find_class(self, module, name):
        if module.startswith(("fairseq", "omegaconf", "argparse")):
            return _Stub
        if module.startswith(("torch", "collections", "numpy", "_codecs", "builtins")):
            return super().find_class(module, name)
        raise pickle.UnpicklingError(f"refusing to load {module}.{name}")


def export_content_encoder(hubert_pt: Path, out: Path) -> None:
    from transformers import HubertConfig, HubertModel

    pm = type(sys)("fairseq_safe_pickle")
    pm.Unpickler = _FairseqUnpickler
    pm.load = lambda f, **kw: _FairseqUnpickler(f, **kw).load()
    sd = torch.load(hubert_pt, map_location="cpu", pickle_module=pm, weights_only=False)["model"]

    cfg = HubertConfig(hidden_size=768, num_hidden_layers=12, num_attention_heads=12, intermediate_size=3072,
                       feat_extract_norm="group", conv_bias=False, do_stable_layer_norm=False,
                       feat_proj_layer_norm=True, apply_spec_augment=False)
    model = HubertModel(cfg).eval()
    m = {}
    for i in range(7):
        m[f"feature_extractor.conv_layers.{i}.conv.weight"] = sd[f"feature_extractor.conv_layers.{i}.0.weight"]
    m["feature_extractor.conv_layers.0.layer_norm.weight"] = sd["feature_extractor.conv_layers.0.2.weight"]
    m["feature_extractor.conv_layers.0.layer_norm.bias"] = sd["feature_extractor.conv_layers.0.2.bias"]
    m["feature_projection.layer_norm.weight"] = sd["layer_norm.weight"]
    m["feature_projection.layer_norm.bias"] = sd["layer_norm.bias"]
    m["feature_projection.projection.weight"] = sd["post_extract_proj.weight"]
    m["feature_projection.projection.bias"] = sd["post_extract_proj.bias"]
    keys = set(model.state_dict())
    m[next(k for k in keys if k.startswith("encoder.pos_conv_embed.conv") and ("weight_g" in k or "original0" in k))] = sd["encoder.pos_conv.0.weight_g"]
    m[next(k for k in keys if k.startswith("encoder.pos_conv_embed.conv") and ("weight_v" in k or "original1" in k))] = sd["encoder.pos_conv.0.weight_v"]
    m["encoder.pos_conv_embed.conv.bias"] = sd["encoder.pos_conv.0.bias"]
    m["encoder.layer_norm.weight"] = sd["encoder.layer_norm.weight"]
    m["encoder.layer_norm.bias"] = sd["encoder.layer_norm.bias"]
    for i in range(12):
        p = f"encoder.layers.{i}."
        for proj in ("q_proj", "k_proj", "v_proj", "out_proj"):
            for t in ("weight", "bias"):
                m[f"{p}attention.{proj}.{t}"] = sd[f"{p}self_attn.{proj}.{t}"]
        for t in ("weight", "bias"):
            m[f"{p}layer_norm.{t}"] = sd[f"{p}self_attn_layer_norm.{t}"]
            m[f"{p}feed_forward.intermediate_dense.{t}"] = sd[f"{p}fc1.{t}"]
            m[f"{p}feed_forward.output_dense.{t}"] = sd[f"{p}fc2.{t}"]
            m[f"{p}final_layer_norm.{t}"] = sd[f"{p}final_layer_norm.{t}"]
    missing, unexpected = model.load_state_dict(m, strict=False)
    assert [k for k in missing if k != "masked_spec_embed"] == [] and not unexpected, (missing, unexpected)

    class Encoder(torch.nn.Module):
        def __init__(self, inner): super().__init__(); self.inner = inner
        def forward(self, wav16k): return self.inner(wav16k).last_hidden_state

    x = torch.randn(1, (DEFAULT_BLOCK_MS + DEFAULT_CONTEXT_MS) * 16)
    torch.onnx.export(Encoder(model).eval(), (x,), str(out), input_names=["wav16k"], output_names=["features"],
                      dynamic_axes={"wav16k": {1: "n"}, "features": {1: "t"}}, opset_version=17, do_constant_folding=True)


def load_voice_checkpoint(pth: Path):
    """Loads an RVC inference checkpoint with weights_only=True (no arbitrary code execution)."""
    ckpt = torch.load(pth, map_location="cpu", weights_only=True)
    if "weight" not in ckpt or "config" not in ckpt:
        raise ValueError(f"{pth.name} is not an RVC inference checkpoint (expected 'weight' and 'config')")
    if ckpt.get("version") != "v2":
        raise ValueError(f"{pth.name}: only RVC v2 voices are supported (got {ckpt.get('version')!r})")
    if int(ckpt.get("f0", 1)) != 1:
        raise ValueError(f"{pth.name}: voices without pitch guidance are not supported")
    return ckpt


def export_voice(pth: Path, out_dir: Path, voice_id: str, meta: dict,
                 block_ms: int = DEFAULT_BLOCK_MS,
                 context_ms: int = DEFAULT_CONTEXT_MS,
                 margin_frames: int = DEFAULT_MARGIN_FRAMES,
                 format_version: int = DEFAULT_FORMAT,
                 fp16: bool = False) -> Path:
    from infer.module.models import SynthesizerTrnMs768NSFsid

    if format_version == FORMAT_VERSION_V1:
        block_ms, context_ms, margin_frames = 100, 600, 4
    elif format_version != FORMAT_VERSION_V2:
        raise ValueError(f"unsupported format version {format_version}")

    block_ms = int(block_ms)
    context_ms = int(context_ms)
    margin_frames = int(margin_frames)
    if block_ms <= 0 or context_ms <= 0 or margin_frames < 0:
        raise ValueError("invalid streaming configuration (block/context/margin)")

    n16k = (block_ms + context_ms) * 16
    frames = ((n16k - 400) // 320 + 1) * 2
    tail = block_ms // 10 + margin_frames

    ckpt = load_voice_checkpoint(pth)
    cfg = list(ckpt["config"])
    weights = {k: v.float() for k, v in ckpt["weight"].items()}
    cfg[-3] = weights["emb_g.weight"].shape[0]
    sample_rate = int(cfg[-1])
    net = SynthesizerTrnMs768NSFsid(*cfg, is_half=False)
    net.load_state_dict(weights, strict=False)
    net.eval().remove_weight_norm()
    del net.enc_q

    class Streaming(torch.nn.Module):
        def __init__(self, n): super().__init__(); self.n = n
        def forward(self, phone, pitch, nsff0, sid, noise_scale):
            lengths = torch.full((1,), phone.shape[1], dtype=torch.long)
            g = self.n.emb_g(sid).unsqueeze(-1)
            m_p, logs_p, x_mask = self.n.enc_p(phone, pitch, lengths)
            z_p = (m_p + torch.exp(logs_p) * torch.randn_like(m_p) * noise_scale) * x_mask
            z = self.n.flow(z_p, x_mask, g=g, reverse=True)[:, :, -nsff0.shape[1]:]
            return self.n.dec(z, nsff0, g=g).squeeze(1)

    args = (torch.randn(1, frames, 768), torch.randint(1, 255, (1, frames)),
            torch.rand(1, tail) * 200 + 100, torch.zeros(1, dtype=torch.long),
            torch.tensor(0.66666))
    with torch.no_grad():
        y = Streaming(net)(*args)
    assert torch.isfinite(y).all() and y.shape[-1] == tail * sample_rate // 100

    out_dir.mkdir(parents=True, exist_ok=True)
    onnx_path = out_dir / f"{voice_id}.onnx"
    torch.onnx.export(Streaming(net).eval(), args, str(onnx_path),
                      input_names=["phone", "pitch", "nsff0", "sid", "noise_scale"], output_names=["audio"],
                      opset_version=17, do_constant_folding=True)

    precision = "fp32"
    if fp16:
        try:
            import onnx
            from onnxconverter_common import float16
            model = onnx.load(str(onnx_path))
            # The stochastic source module mixes RandomUniform with casts;
            # keep that small subgraph FP32 to avoid type-inference failures.
            keep_nodes = [n.name for n in model.graph.node
                          if "l_sin_gen" in n.name or "m_source" in n.name]
            model = float16.convert_float_to_float16(
                model, keep_io_types=True,
                # Precision-sensitive nodes stay FP32; everything else uses
                # tensor cores / FP16 on the GPU provider.
                op_block_list=["LayerNormalization", "Softmax", "ReduceMean",
                               "RandomUniform", "RandomNormal",
                               "RandomUniformLike", "RandomNormalLike"],
                node_block_list=keep_nodes)
            onnx.save(model, str(onnx_path))
            precision = "fp16"
        except Exception as exc:  # noqa: BLE001 - fallback is intentional
            print(f"FP16 conversion failed, keeping FP32: {exc}")

    info = {
        "format": format_version, "id": voice_id, "sampleRate": sample_rate,
        "blockMs": block_ms, "contextMs": context_ms, "frames": frames,
        "tail": tail, "marginFrames": margin_frames, "speakers": int(cfg[-3]),
        "precision": precision, **meta,
    }
    (out_dir / f"{voice_id}.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    return onnx_path
