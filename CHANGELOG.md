# VoiceDeck 2.0.0

## Highlights

- **Application audio capture**: add up to eight applications to the virtual cable alongside your voice and soundboard. Capture uses WASAPI Process Loopback, follows the target process tree, lists active audio sessions across all render endpoints plus visible windows, and includes search by process name, window title or PID.
- **Per-source controls**: independent volume with smoothing and click-free fades, live activity meters, process status, safe removal, and automatic cleanup when a captured process closes.
- **Sound editor upgrades**: equal-power fade-in, fade-out or both, persisted as non-destructive edit metadata, plus a live 192×32 spectrogram that follows trim, gain, regions and fades.
- **Cleaner startup**: normal GUI launches no longer create a console window. The new “Hide console on startup” option is persisted, while `--cli` mode always keeps console input/output available for automation.
- **Output safety**: captured audio is mixed into the virtual cable after the local monitor copy, so it is never monitored twice, and a transparent threshold soft limiter leaves levels below 0.85 untouched.

## Compatibility

- Existing `data\settings.json`, `characters.json`, sound libraries and RVC models continue to work.
- Process capture requires Windows 10 build 20348+ or Windows 11. Protected or elevated applications may require running VoiceDeck as administrator.

---

# VoiceDeck 2.0.0 — Русский

## Главное

- **Захват звука приложений**: до восьми приложений можно добавить в виртуальный кабель вместе с голосом и саундбордом. Используется WASAPI Process Loopback с обходом дерева процессов, списком активных аудиосессий со всех render-устройств и видимых окон, а также поиском по имени процесса, заголовку окна или PID.
- **Управление источниками**: отдельная громкость с плавным сглаживанием и фейдами без щелчков, индикаторы активности, статус процесса, безопасное удаление и автоматическая очистка при закрытии захваченного приложения.
- **Обновлённый редактор звука**: equal-power fade-in, fade-out или оба сразу с сохранением в неразрушающих метаданных, а также динамическая спектрограмма 192×32, которая следует за обрезкой, громкостью, участками и фейдами.
- **Чистый запуск**: обычный GUI-запуск больше не создаёт окно консоли. Новая опция «Скрывать консоль при запуске» сохраняется, а режим `--cli` всегда оставляет ввод/вывод консоли для автоматизации.
- **Защита выхода**: захваченный звук добавляется в виртуальный кабель после копии для локального монитора, поэтому не дублируется, а прозрачный threshold soft limiter не трогает уровни ниже 0.85.

## Совместимость

- Существующие `data\settings.json`, `characters.json`, библиотеки звуков и RVC-модели продолжают работать.
- Для захвата процессов нужны Windows 10 build 20348+ или Windows 11. Для защищённых приложений и приложений, запущенных от администратора, может потребоваться запуск VoiceDeck от имени администратора.

---

# VoiceDeck 1.7.0

## Highlights

- **RU/EN localization**: switch the interface language on the Devices tab; the choice is saved in `data\settings.json`.
- **Expanded VoiceLab**: 18 modules with per-module 8-band EQ. New effects include Tremolo, Flanger, Phaser, Wah Wah, Stutter, Stereo Width, Compressor and Octaver.
- **Removable hotkeys**: clear any sound, preset, voice, AI or stop-all shortcut from the capture dialog or with Delete/Backspace. Removals are unregistered, persisted and reflected in tags and badges.
- **Main soundboard monitoring**: the new “Hear sounds” toggle sends the same soundboard mix to your selected original output while keeping the virtual-cable send unchanged. Voice monitoring and preview-only playback remain independent.
- **Faster preset workflow**: one-click character switching, draggable titlebar and an embedded VoiceDeck application icon. Built-in character edits stay temporary; “Save as New” persists them.

## Compatibility

- Existing `data\settings.json`, `characters.json`, sound libraries and RVC models continue to work.
- The new language and soundboard-monitor settings default to Russian and off when absent.

---

# VoiceDeck 1.7.0 — Русский

## Главное

- **Локализация RU/EN**: переключение языка интерфейса на вкладке «Устройства»; выбор сохраняется в `data\settings.json`.
- **Расширенный VoiceLab**: 18 модулей и собственный 8-полосный EQ у каждого модуля. Добавлены Tremolo, Flanger, Phaser, Wah Wah, Stutter, Stereo Width, Compressor и Octaver.
- **Удаление хоткеев**: хоткей звука, персонажа, голоса, AI или остановки всех звуков можно снять кнопкой или Delete/Backspace. Регистрация снимается, изменение сохраняется, теги и badges обновляются.
- **Мониторинг саундборда**: тумблер «Слышать звуки» выводит тот же основной микс на выбранный оригинальный выход, не меняя отправку в виртуальный кабель. Мониторинг голоса и preview-only режим остаются независимыми.
- **Ускоренная работа с пресетами**: переключение персонажей одним кликом, перетаскивание окна за заголовок и встроенная иконка VoiceDeck. Изменения встроенных персонажей остаются временными; «Save as New» сохраняет их.

## Совместимость

- Существующие `data\settings.json`, `characters.json`, библиотеки звуков и RVC-модели продолжают работать.
- Если новые настройки языка и мониторинга отсутствуют, используются русский язык и выключенный мониторинг.

---

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
