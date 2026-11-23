# VoiceDeck

**Soundpad + Voicemod in one app.** A hotkey soundboard, DSP voice changer and live AI voice conversion (RVC) written in C++20 for Windows. All data lives in `data\` next to the executable — fully portable, nothing is written to system folders.

[Русская версия](README.md)

![Voice tab](docs/screenshots/voice.png)

## Features

- **Soundboard** — card/list views, waveform with seeking, volume, loop, hotkeys, preview to your speakers. Drag & drop adds a whole batch at once and copies files into `data\sounds`.
- **Tags & search** — custom tags per clip, search by name or tag, tag filter chips. The built-in `hotkey` tag is added automatically whenever a sound gets a hotkey.
- **Audio editor** — quick trim, global gain and per-region gain changes right in the app. Edits are non-destructive: the source file stays untouched, parameters live in `sounds.json`.
- **Windows startup** — launch with Windows and an "always run as administrator" mode (UAC relaunch).
- **45 voice characters** — pitch, formant, timbre, 8-band EQ, effect chains (radio → robot → cave), live editing with "Save as New".
- **AI voice** — RVC v2 via ONNX Runtime (DirectML / CUDA / CPU), ~45–290 ms depending on model and GPU. Built-in `.pth` converter right from the UI.
- **Devices** — mic and virtual cable selection, monitoring, latency mode, global hotkeys.
- **No allocations or locks in the audio callback** — lock-free SPSC rings and atomics.

![Soundboard](docs/screenshots/sounds.png)

## Quick start

```powershell
git clone https://github.com/ZekiTRR/VoiceDeck.git
cd VoiceDeck
cmake -S . -B build
cmake --build build --config Release
build\Release\voicedeck.exe
```

Or run `voicedeck.exe --cli` for the headless console mode with the same feature set.

**First launch:** open the *Devices* tab, pick your microphone and virtual cable (VB-CABLE / VAC). Enable *Preview* if you want to hear yourself.

> In Discord/games, select the **virtual cable output** (`CABLE Output` / `Microphone (Line 2)`) as the microphone — otherwise apps will hear your clean voice.

## `data\` layout

```
data\
  settings.json     devices, latency, AI tuning, hotkeys
  sounds.json       sound library: tags, hotkeys and audio-editor settings (trimStart/trimEnd/gain/regions)
  characters.json   voice characters
  sounds\           clips (added files are copied here)
  AiVoice\          content_encoder.onnx, rmvpe.onnx, voices\*.onnx + *.json
```

## AI voice

![AI tab](docs/screenshots/ai.png)

A voice is a `name.onnx` + `name.json` pair (Voice CTX format 1) in `data\AiVoice\voices\`.

- **"Where to get voices?"** opens the official Voice CTX model pack (base models + two English VCTK voices).
- **"Import voice"** adds an existing `.onnx` + `.json` pair.
- **"Convert .pth"** converts an RVC v2 checkpoint inside the app. Checkpoints are loaded with `torch.load(weights_only=True)` — no code from the file is executed. The first run creates an isolated Python environment in `data\aivoice\venv` (Python 3.10–3.12 and internet required).

For Russian or other languages you need a voice trained on that language — VCTK is English only.

## Devices

![Settings](docs/screenshots/settings.png)

WASAPI devices on Windows 10/11. Setting 48 kHz on your mic and cable is recommended.

## Building

Requires Windows 10/11 x64, MSVC (C++20), CMake 3.20+. WebView2 Runtime ships with Windows 10/11. All dependencies (miniaudio, nlohmann/json, Signalsmith, DaisySP, iir1, ONNX Runtime, WebView2 SDK) are vendored in `third_party\`.

## License and credits

MIT. Built on RVC, HuBERT, RMVPE, ONNX Runtime, miniaudio, Signalsmith and DaisySP. Built-in VCTK voices: CC BY 4.0 (data) / Apache-2.0 (models). Only use AI voices you have the rights to.
