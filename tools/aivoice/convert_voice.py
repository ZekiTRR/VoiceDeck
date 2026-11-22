"""Converts an RVC v2 voice (.pth) into a Voice CTX AI Voice model (.onnx + .json).

The checkpoint is loaded with torch.load(weights_only=True), so a malicious .pth cannot run code.
Only convert voices you have the rights to use.
"""
from __future__ import annotations

import argparse
import os
import re
from pathlib import Path

import rvc_export as rx


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("pth", type=Path)
    ap.add_argument("--name", required=True)
    ap.add_argument("--gender", choices=["male", "female", "other"], default="other")
    ap.add_argument("--pitch-shift", type=int, default=0)
    ap.add_argument("--credits", default="Imported by the user.")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--cache", type=Path)
    a = ap.parse_args()

    out = a.out or (Path(os.environ.get("APPDATA", Path.home())) / "VoiceCtx" / "AiVoice" / "voices")
    cache = a.cache or (Path.home() / ".cache" / "voicectx-aivoice")
    rx.ensure_rvc_source(cache)
    voice_id = "user-" + (re.sub(r"[^a-z0-9]+", "-", a.name.lower()).strip("-") or "voice")
    path = rx.export_voice(a.pth, out, voice_id, {
        "name": a.name, "gender": a.gender, "defaultPitchShift": a.pitch_shift,
        "credits": a.credits, "license": "user supplied", "builtIn": False,
    })
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
