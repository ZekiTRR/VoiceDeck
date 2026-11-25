# VoiceDeck 1.5.0

## Highlights

- **VoiceLab**: a visual effect-chain builder with Pitch, Formant, Vocoder, Bitcrusher, Ring Modulator, Distortion, Chorus, Reverb, Delay and 8-band EQ. Add, remove and drag modules; every module has independent Strength/Mix and effect-specific controls.
- **Background ambiences**: per-preset, VAD-gated radio static with an end-of-phrase chirp, cockpit hum and cave wind. Toggle them globally or import a custom seamless loop for any character.
- **Soundboard FX Send**: route songs and memes through the current voice preset and loaded RVC voice. Microphone and soundboard use independent DSP/AI paths, with microphone priority during simultaneous RVC work.
- **45 voice characters**, including the new Pilot preset. Legacy chains such as `radio → robot → cave` remain compatible.

## Audio engine and AI

- New low-latency 256-frame mode, MMCSS Pro Audio threads and ONNX Runtime CPU-thread tuning.
- Adaptive jitter buffer with virtual-cable clock-drift compensation and honest end-to-end latency reporting.
- VAD sleep for the voice branch with AI pre-roll; the soundboard keeps playing while the voice path is idle.
- Zero-latency neutral soft clipper and improved real-time snapshot/AI queue handling.
- RVC model format v2: 50 ms blocks, 400 ms context and 10 ms crossfade for lower AI latency.
- Optional FP16 GPU models with automatic FP32 fallback.
- Polyphase resampling, performance-core affinity and WebView2 suspension while minimized.

## UI and CLI

- Live VoiceLab parameter editing with per-module bypass via `Mix = 0`.
- Custom ambience library stored in `data\ambiences`.
- FX Send toggle and active-preset label on the Soundboard tab.
- New CLI commands: `ambient on|off`, `ambient <idx> <none|radio|pilot|cave|file>` and `fxsend on|off`.

## Compatibility

- Existing `data\settings.json`, `characters.json`, sound library and v1 RVC models continue to work.
- Built-in voice parameters remain factory-locked; use **Save as New** to persist VoiceLab edits as a new character.

---

# VoiceDeck 1.5.0 — Русский

## Главное

- **VoiceLab**: визуальный конструктор цепочки из Pitch, Formant, Vocoder, Bitcrusher, Ring Modulator, Distortion, Chorus, Reverb, Delay и 8-band EQ. Блоки можно добавлять, убирать и перетаскивать; у каждого модуля свои Strength/Mix и параметры эффекта.
- **Фоновые эмбиенты**: радиошум с писком конца фразы, гул кабины и ветер пещеры включаются только во время речи по VAD. Их можно выключить общим переключателем или загрузить свой бесшовный луп для любого персонажа.
- **FX Send саундборда**: песни и мемы проходят через текущий пресет и загруженный RVC-голос. Микрофон и саундборд используют независимые DSP/AI-тракты; при одновременной работе RVC приоритет получает микрофон.
- **45 голосовых персонажей**, включая новый пресет Pilot. Старые цепочки вроде `radio → robot → cave` остаются совместимыми.

## Аудиодвижок и AI

- Новый низколатентный режим с буфером 256 кадров, потоки MMCSS Pro Audio и настройка CPU-потоков ONNX Runtime.
- Адаптивный jitter-буфер с компенсацией дрейфа виртуального кабеля и честным отображением сквозной задержки.
- VAD-сон голосовой ветки с AI pre-roll: саундборд продолжает играть, пока голосовой тракт простаивает.
- Zero-latency soft clipper для нейтрального пресета и улучшенная реальновременная обработка очередей и снимков AI.
- Формат RVC v2: блоки 50 мс, контекст 400 мс и кроссфейд 10 мс для меньшей задержки AI.
- Опциональные FP16 GPU-модели с автоматическим откатом на FP32.
- Полифазный ресемплинг, привязка к производительным ядрам и приостановка WebView2 в свёрнутом окне.

## Интерфейс и CLI

- Живая настройка параметров VoiceLab; `Mix = 0` полностью обходит отдельный модуль.
- Библиотека пользовательских эмбиентов в `data\ambiences`.
- Переключатель FX Send и название активного пресета во вкладке «Звуки».
- Новые CLI-команды: `ambient on|off`, `ambient <idx> <none|radio|pilot|cave|file>` и `fxsend on|off`.

## Совместимость

- Существующие `data\settings.json`, `characters.json`, библиотека звуков и RVC-модели v1 продолжают работать.
- Параметры встроенных голосов остаются защищёнными; для сохранения изменений VoiceLab используйте **Save as New**.
