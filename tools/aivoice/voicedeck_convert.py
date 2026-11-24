r"""VoiceDeck wrapper: safely converts an RVC v2 .pth and installs it into data\AiVoice\voices.

Run with Python 3.10-3.12 (e.g. `py -3.12 voicedeck_convert.py <data_dir> <pth> <name> <gender> <pitch>`).
The .pth is loaded by convert_voice.py with torch.load(weights_only=True): no code from the
checkpoint is ever executed. VoiceDeck launches this from the UI and shows progress in-app.
"""
from __future__ import annotations

import subprocess
import sys
import venv
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 6:
        print("usage: voicedeck_convert.py <data_dir> <pth> <name> <gender> <pitch> [fp16]", flush=True)
        return 2
    data = Path(sys.argv[1])
    pth = Path(sys.argv[2])
    name, gender, pitch = sys.argv[3], sys.argv[4], sys.argv[5]
    fp16 = len(sys.argv) >= 7 and sys.argv[6] in ("1", "true", "fp16")
    tools = Path(__file__).resolve().parent
    ai = data / "aivoice"
    ai.mkdir(parents=True, exist_ok=True)
    venv_dir = ai / "venv"
    py = venv_dir / "Scripts" / "python.exe"

    if not py.exists():
        print("VD:creating Python environment", flush=True)
        venv.create(venv_dir, with_pip=True)

    marker = ai / ".deps_ok_v2"
    if not marker.exists():
        print("VD:installing converter dependencies (first run, may take several minutes)", flush=True)
        try:
            subprocess.check_call([str(py), "-m", "pip", "install", "-r", str(tools / "requirements.txt")])
        except Exception as exc:
            print(f"VD:dependency install failed: {exc}", flush=True)
            return 3
        marker.write_text("ok", encoding="utf-8")

    print("VD:converting (.pth loaded weights-only; RVC v2 + pitch guidance only)", flush=True)
    cmd = [str(py), str(tools / "convert_voice.py"), str(pth),
           "--name", name, "--gender", gender, "--pitch-shift", str(pitch),
           "--format", "2",
           "--out", str(data / "AiVoice" / "voices"),
           "--cache", str(ai / "cache")]
    if fp16:
        cmd.append("--fp16")
    try:
        code = subprocess.call(cmd)
    except Exception as exc:
        print(f"VD:conversion failed: {exc}", flush=True)
        return 4
    print(f"VD:exit {code}", flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
