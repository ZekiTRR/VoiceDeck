// VoiceDeck UI localization. Russian strings in index.html/app.js are the
// source strings; English translations below are written for UI context, not
// word-for-word. A MutationObserver translates dynamically created nodes too.
"use strict";

const VD_I18N = {
  ru: {},
  en: {
    // Shell
    "Свернуть": "Minimize",
    "Развернуть": "Maximize",
    "Закрыть": "Close",
    "Закрыть параметры": "Close parameters",
    "Голос": "Voice",
    "Звуки": "Sounds",
    "AI-голос": "AI Voice",
    "Устройства": "Devices",
    "Захват": "Capture",
    "Захват приложений": "Application Capture",
    "Обновить список приложений": "Refresh application list",
    "Приложения": "Applications",
    "Звук выбранного приложения будет добавлен в виртуальный кабель вместе с голосом и саундбордом. Список обновляется вручную.": "The selected application's audio is added to the virtual cable together with your voice and soundboard. Refresh the list manually.",
    "Добавить выбранные": "Add selected",
    "звук": "audio",
    "Активные захваты": "Active captures",
    "Нет активных захватов": "No active captures",
    "Нажми «Обновить список приложений».": "Click “Refresh application list”.",
    "Запуск захвата": "Starting capture",
    "Захват активен": "Capture active",
    "Нет сигнала": "No signal",
    "Остановка захвата": "Stopping capture",
    "Ошибка захвата": "Capture error",
    "Процесс закрыт": "Process closed",
    "Захват приложений недоступен в этой версии Windows.": "Application capture is unavailable in this Windows version.",
    "Process Loopback требует Windows 10 build 20348+ или Windows 11": "Process Loopback requires Windows 10 build 20348+ or Windows 11",
    "WASAPI Process Loopback недоступен в этой версии Windows": "WASAPI Process Loopback is unavailable in this Windows version",
    "Голос вкл/выкл": "Toggle voice DSP",
    "AI вкл/выкл": "Toggle AI voice",
    "Фоновые эмбиенты пресетов звучат только во время речи (VAD)": "Preset ambiences play only while you speak (VAD)",
    "Эмбиент": "Ambience",
    "Фоновые эмбиенты вкл/выкл": "Toggle background ambiences",
    "Слышать свой голос с эффектами на оригинальном выходе": "Hear your processed voice on the original output",
    "Предпросмотр": "Preview",
    "Предпрослушивание голоса": "Voice monitoring",
    "Голосовые эффекты": "Voice Effects",
    "Саундборд": "Soundboard",

    // Voice tab
    "Питч": "Pitch",
    "Форманта": "Formant",
    "сброс": "Reset",
    "Ниже этого порога голосовая ветка засыпает; звуки продолжают играть": "Below this level the voice branch sleeps; sounds keep playing",
    "Тишина": "Silence",
    "Порог тишины (VAD)": "Silence threshold (VAD)",
    "+ Персонаж": "+ Character",
    "Эквалайзер персонажа": "Character EQ",
    "Редактор персонажа": "Character Editor",
    "Имя": "Name",
    "Тембр": "Timbre",
    "Сила": "Strength",
    "Пространство": "Space",
    "нет": "None",
    "комната": "Room",
    "зал": "Hall",
    "пещера": "Cave",
    "шлем": "Helmet",
    "радио": "Radio",
    "мегафон": "Megaphone",
    "Эффект": "Effect",
    "робот": "Robot",
    "пришелец": "Alien",
    "клон": "Clone",
    "тишина": "Silence",
    "Фоновый эмбиент · VAD": "Background Ambience · VAD",
    "Луп": "Loop",
    "радиошум + писк": "Radio static + chirp",
    "гул кабины": "Cockpit hum",
    "ветер пещеры": "Cave wind",
    "Уровень": "Level",
    "Загрузить свой эмбиент…": "Load custom ambience…",
    "Луп и писк конца фразы слышны только пока VAD слышит тебя. Общий выключатель — в левой панели.": "The loop and end-of-phrase chirp are audible only while VAD hears you. The master switch is in the left panel.",
    "VoiceLab · цепочка эффектов": "VoiceLab · Effects Chain",
    "Перетаскивай блоки за ручку. Стрелки — альтернатива для клавиатуры, шестерёнка — параметры блока.": "Drag blocks by the handle. Arrows work with a keyboard; the gear opens module settings.",
    "Цепочка эффектов": "Effects Chain",
    "Добавить модуль": "Add Module",
    "Параметры": "Parameters",
    "Эквалайзер пресета (legacy)": "Character EQ (legacy)",
    "Сохранить как новый ★": "Save as New ★",
    "Ползунки меняют звук временно. «Сохранить» создаёт нового персонажа — встроенные пресеты остаются статическими.": "Sliders change the sound temporarily. Save as New creates a separate character; built-ins stay untouched.",
    "Редактировать": "Edit",
    "Мои эмбиенты": "My ambiences",
    "Нет модулей — сигнал идёт напрямую": "No modules — the signal passes through",
    "Перетащить": "Drag",
    "Выше": "Move up",
    "Ниже": "Move down",
    "Убрать": "Remove",
    "Добавь Pitch в VoiceLab": "Add Pitch in VoiceLab",
    "Добавь Formant в VoiceLab": "Add Formant in VoiceLab",
    "Добавь 8-band EQ в VoiceLab": "Add 8-band EQ in VoiceLab",
    "EQ этого модуля": "This Module’s EQ",

    // Module descriptions
    "транспозиция": "transposition",
    "форма резонанса": "resonance shape",
    "8 полос": "8 bands",
    "несущая 110 Hz": "110 Hz carrier",
    "сатурация": "saturation",
    "модулированная задержка": "modulated delay",
    "пространство": "space",
    "feedback echo": "feedback echo",
    "8 полос эквалайзера": "8-band EQ",
    "амплитудное вибрато": "amplitude vibrato",
    "короткая модулированная задержка": "short modulated delay",
    "сканирующие all-pass фильтры": "swept all-pass filters",
    " sweeper резонанса": " resonance sweep",
    "повтор фрагмента": "slice repeat",
    "расширение стерео": "stereo widening",
    "компрессия динамики": "dynamic compression",
    "октава вниз": "octave down",
    "реверберация": "reverb",
    "пещера": "cave",
    "фильтр + реверберация": "filter + reverb",
    "полосовой фильтр + сатурация": "band-pass + saturation",
    "мегафон": "megaphone",
    "comb + ring mod": "comb + ring mod",
    "ring mod": "ring mod",
    "bitcrush": "bitcrush",
    "двойный голос": "twin voice",
    "совместимый модуль": "compatible module",

    // Module parameter labels
    "Полутона": "Semitones",
    "Глубина вибрато": "Vibrato Depth",
    "Скорость вибрато": "Vibrato Rate",
    "База форманты": "Formant Base",
    "Несущая": "Carrier",
    "Атака": "Attack",
    "Отпускание": "Release",
    "Битность": "Bit Depth",
    "Даунсэмплинг": "Downsample",
    "Частота": "Frequency",
    "Глубина": "Depth",
    "Базовая задержка": "Base Delay",
    "Время": "Time",
    "Центр": "Center",
    "Диапазон": "Range",
    "Фрагмент": "Slice",
    "Повторы": "Repeats",
    "Сила / Mix": "Strength / Mix",

    // Soundboard tab
    "имя или тег...": "name or tag...",
    "Пропускать саундборд через текущий пресет голоса и RVC": "Route the soundboard through the current voice preset and RVC",
    "Через пресет": "Through preset",
    "Пропускать саундборд через пресет": "Route soundboard through preset",
    "Слышать звуки": "Hear sounds",
    "Слышать основной саундборд": "Hear the main soundboard",
    "Слышать на оригинальном выходе те же звуки, что уходят в виртуальный кабель": "Hear the same sounds on the original output that are sent to the virtual cable",
    "Основной саундборд: слышно на оригинальном выходе": "Main soundboard: audible on the original output",
    "Основной саундборд: только в канал": "Main soundboard: cable only",
    "Карточки": "Cards",
    "Список": "List",
    "Подхватить новые и удалённые файлы из папки без перезапуска": "Pick up new and deleted files without restarting",
    "⟳ Обновить": "⟳ Refresh",
    "Стоп все": "Stop All",
    "+ Добавить": "+ Add",
    "Перетащи аудиофайлы в окно — они сразу скопируются в data\\sounds, можно сразу несколько. Файлы, добавленные в папку вручную, подхватываются кнопкой ⟳ Обновить без перезапуска. Тег hotkey ставится автоматически, когда звуку назначена горячая клавиша.": "Drop audio files into the window to copy them into data\\sounds — multiple files are fine. Files added to the folder manually appear after clicking ⟳ Refresh; no restart needed. The hotkey tag is added automatically when a sound gets a shortcut.",
    "Библиотека пуста.": "Library is empty.",
    "Добавить звук": "Add Sound",
    "Стоп": "Stop",
    "Повтор": "Repeat",
    "Предпрослушать на динамики": "Preview on speakers",
    "Предпрослушать": "Preview",
    "Остановить предпрослушивание": "Stop preview",
    "Воспроизвести": "Play",
    "Пауза": "Pause",
    "ЛКМ — играть, ПКМ — предпрослушать": "Left-click to play, right-click to preview",
    "Клик — перемотать": "Click to seek",
    "Изменить теги": "Edit tags",
    "Назначить хоткей": "Assign hotkey",
    "Хоткей": "Hotkey",
    "снять хоткей": "Clear hotkey",
    "удалить": "Remove",
    "тегов нет": "No tags",
    "все": "All",
    "Громкость: ": "Volume: ",
    "Редактор: ": "Editor: ",

    // Sound editor
    "Редактор звука": "Audio Editor",
    "Перетащи границы — обрезать. Выдели участок на волне — добавить изменения громкости.": "Drag the edges to trim. Select a region on the waveform to change its volume.",
    "Спектрограмма": "Spectrogram",
    "Начало, с": "Start, s",
    "Конец, с": "End, s",
    "Общая громкость": "Global Gain",
    "Участки громкости": "Volume Regions",
    "+ Добавить выделенный участок": "+ Add Selected Region",
    "нет участков": "No regions",
    "Сбросить правки": "Reset Edits",
    "Применить": "Apply",

    // AI tab
    "Модели не установлены.": "No models installed.",
    "Конвертировать .pth": "Convert .pth",
    "Импорт голоса": "Import Voice",
    "Где взять голоса?": "Where to get voices?",
    "Питч AI": "AI Pitch",
    "Провайдер": "Provider",
    "Запас": "Safety Margin",
    "Кроссфейд": "Crossfade",
    "Авто-лимит": "Auto Limit",
    "устройство": "Device",
    "латентность": "Latency",
    "выпадения": "Dropouts",
    "загружен": "loaded",
    "клик": "click",
    "[сис]": "[sys]",
    "Загружаю…": "Loading…",
    "Конвертация RVC v2 (.pth)": "RVC v2 Conversion (.pth)",
    "Имя голоса": "Voice Name",
    "Пол": "Gender",
    "другой": "Other",
    "мужской": "Male",
    "женский": "Female",
    "Сдвиг питча": "Pitch Shift",
    "FP16 (только GPU)": "FP16 (GPU only)",
    "Конвертировать": "Convert",
    "My voice": "My Voice",
    "Конвертация запущена — это может занять несколько минут": "Conversion started — it may take a few minutes",
    "Голос сконвертирован и добавлен в библиотеку": "Voice converted and added to the library",
    "Ошибка конвертации: ": "Conversion failed: ",
    "неизвестная ошибка": "unknown error",
    "AI загружен": "AI loaded",
    "Ошибка: ": "Error: ",
    "Провайдер: ": "Provider: ",

    // Settings
    "Маршрут": "Routing",
    "Микрофон": "Microphone",
    "Выход (вирт. кабель)": "Output (virtual cable)",
    "Применить": "Apply",
    "Маршрут применён": "Routing applied",
    "Оригинальный выход": "Original Output",
    "Устройство": "Device",
    "Включить": "Enable",
    "Голос вкл (bypass)": "Voice On (bypass)",
    "Приглушать при речи": "Duck While Speaking",
    "Задержка": "Latency",
    "Режим": "Mode",
    "Низкая (256 · 5.3 мс)": "Low (256 · 5.3 ms)",
    "Средняя (480 · 10 мс)": "Normal (480 · 10 ms)",
    "Высокая (960 · 20 мс)": "High (960 · 20 ms)",
    "Меньше = быстрее отклик, больше = стабильнее. Для Bluetooth-гарнитур и слабых драйверов выбирайте «Средняя» или «Высокая».": "Lower means faster response, higher means more stability. For Bluetooth headsets or weak drivers choose Normal or High.",
    "Задержка изменена": "Latency changed",
    "Запуск": "Startup",
    "Автозапуск с Windows": "Launch with Windows",
    "Всегда от имени админа": "Always run as administrator",
    "Скрывать консоль при запуске": "Hide console on startup",
    "Консоль скрыта при обычном запуске. Режим `--cli` всегда показывает вывод.": "The console is hidden for normal launches. `--cli` mode always keeps its output.",
    "При включении «от имени админа» приложение перезапустится с запросом UAC. Настройка действует при каждом запуске.": "When enabled, the app restarts with a UAC prompt. The setting is applied on every launch.",
    "Приложение сейчас запущено от имени администратора.": "The app is currently running as administrator.",
    "не запущено": "not running",
    "Автозапуск включён": "Auto-start enabled",
    "Автозапуск выключен": "Auto-start disabled",
    "Запуск от имени администратора включён": "Run as administrator enabled",
    "Запуск от имени администратора выключен": "Run as administrator disabled",
    "Хоткеи": "Hotkeys",
    "Хоткей: голос": "Voice hotkey",
    "Хоткей: AI": "AI hotkey",
    "Хоткей: стоп звуков": "Stop sounds hotkey",
    "— клик —": "— click —",
    "Хоткей: ": "Hotkey: ",
    "Интерфейс": "Interface",
    "Язык": "Language",
    "Русский": "Russian",
    "Английский": "English",
    "Язык интерфейса сохраняется в data\\settings.json.": "The interface language is saved in data\\settings.json.",

    // Dialogs
    "Нажми комбинацию…": "Press a shortcut…",
    "Нажми новую комбинацию или Delete/Backspace, чтобы удалить хоткей.": "Press a new shortcut, or Delete/Backspace to remove the hotkey.",
    "Удалить хоткей": "Remove hotkey",
    "Хоткей удалён": "Hotkey removed",
    "отмена": "cancel",
    "Отмена": "Cancel",
    "Удалить?": "Delete?",
    "Удалить": "Delete",
    "Теги звука": "Sound Tags",
    "Новый тег": "New Tag",
    "например: мемы": "e.g. memes",
    "Готово": "Done",
    "Тег hotkey назначается автоматически": "The hotkey tag is added automatically",
    "Такой тег уже есть": "That tag already exists",
    "Хоткей снят — тег hotkey удалён": "Hotkey cleared — hotkey tag removed",

    // Toasts / status
    "Тракт ": "Path ",
    " мс": " ms",
    " DSP ": " DSP ",
    " DSP off": " DSP off",
    " AI ": " AI ",
    "≈": "~",
    " dB": " dB",
    "ur ": "dropouts ",
    "Максимум 24 модуля": "Maximum 24 modules",
    "Имя обязательно": "A name is required",
    "Сохранено как «": "Saved as “",
    "Предпрослушивание остановлено": "Preview stopped",
    "Предпрослушивание: ": "Preview: ",
    "Предпрослушивание голоса: ВКЛ": "Voice monitoring: ON",
    "Предпрослушивание голоса: ВЫКЛ": "Voice monitoring: OFF",
    "Удалить «": "Delete “",
    "Правки сброшены": "Edits reset",
    "Звук отредактирован": "Sound edited",
    "Загрузи AI-голос": "Load an AI voice first",
    "Саундборд идёт через текущий пресет": "Soundboard is routed through the current preset",
    " и RVC": " and RVC",
    "Задай новое имя и нажми «Сохранить как новый»": "Enter a new name, then click Save as New",
    "off": "off",
    "Обновляю библиотеку из data\\sounds…": "Refreshing library from data\\sounds…",
    "Добавляю звуков: ": "Adding sounds: ",
    "добавлено: ": "added: ",
    "удалено: ": "removed: ",
    "Библиотека обновлена — ": "Library updated — ",
    "Отпусти файлы — добавим в библиотеку": "Drop files to add them to the library",

    // Host notices / errors
    "уже в библиотеке": "already in the library",
    "Пропущено дубликатов: ": "Duplicates skipped: ",
    "Не удалось добавить файлы": "Could not add files",
    "Не удалось добавить звук": "Could not add the sound",
    "Не удалось открыть диалог выбора эмбиента": "Could not open the ambience picker",
    "Эмбиент назначен персонажу": "Ambience assigned to the character",
    "Не удалось открыть диалог выбора модели": "Could not open the model picker",
    "Не выбран файл модели": "No model file selected",
    "Файл модели не найден": "Model file not found",
    "Конвертер не найден: tools\\aivoice\\voicedeck_convert.py": "Converter not found: tools\\aivoice\\voicedeck_convert.py",
    "Конвертация запущена. При первом запуске установка зависимостей займёт несколько минут.": "Conversion started. The first run installs dependencies and may take a few minutes.",
    "Аудиодвижок занят — повтори выход из редактора": "Audio engine is busy — close the editor again",
    "аудиодвижок занят — повтори попытку": "Audio engine is busy — try again",
    "звук ещё играет — останови его и повтори": "The sound is still playing — stop it and try again",
    "встроенный тег — назначь хоткей": "Built-in tag — assign a hotkey first",
    "встроенный тег — сними хоткей": "Built-in tag — clear the hotkey first",
    "имя персонажа обязательно": "A character name is required",
    "уже существует": "already exists",
    "этот персонаж нельзя удалить": "This character cannot be deleted",
    "встроенные персонажи защищены": "Built-in characters are protected",
    "эмбиент не найден": "Ambience not found",
    "недостаточно памяти для эмбиента": "Not enough memory for the ambience",
    "нет такого персонажа": "No such character",
    "не удалось сохранить персонажа": "Could not save the character",
    "файл эмбиента не найден": "Ambience file not found",
    "аудиофайл слишком большой": "Audio file is too large",
    "включи оригинальный выход в настройках": "Enable the original output in Settings",
    "Отменено — запуск от имени администратора не включён": "Cancelled — administrator mode was not enabled",
    "Не удалось запустить приложение от имени администратора": "Could not restart as administrator",
    "имя пустое": "Name is empty",
    "empty voice name": "Voice name is empty",
    "no AI voices installed (see the AI tab)": "No AI voices installed (see the AI tab)",
    "unknown provider (use: auto|cuda|dml|cpu)": "Unknown provider (use auto|cuda|dml|cpu)",
  },
};

let vdLocale = "ru";
try {
  const savedLocale = localStorage.getItem("voicedeck-language");
  if (savedLocale === "en" || savedLocale === "ru") vdLocale = savedLocale;
} catch (_) { /* file:// may restrict storage */ }
let vdI18nApplying = false;
const vdOriginalText = new WeakMap();
const vdAppliedText = new WeakMap();
const vdOriginalAttrs = new WeakMap();
const vdAppliedAttrs = new WeakMap();
const vdI18nAttrs = ["title", "aria-label", "placeholder", "aria-valuetext"];

function vdTranslateString(source) {
  if (vdLocale === "ru") return source;
  const dict = VD_I18N.en;
  if (Object.prototype.hasOwnProperty.call(dict, source)) return dict[source];
  const rules = [
    [/ · параметры/g, " · parameters"],
    [/Добавь блок /g, "Add "],
    [/ в VoiceLab/g, " in VoiceLab"],
    [/Добавить /g, "Add "],
    [/ уже в цепочке/g, " already in chain"],
    [/Выше: /g, "Move up: "],
    [/Ниже: /g, "Move down: "],
    [/Параметры: /g, "Parameters: "],
    [/Убрать: /g, "Remove: "],
    [/персонаж '/g, "Character '"],
    [/не удалось создать папку ambiences: /g, "Could not create the ambiences folder: "],
    [/не удалось скопировать эмбиент: /g, "Could not copy the ambience: "],
    [/\[сис\]/g, "[sys]"],
    [/(\d[\d.,]*)\sс/g, "$1 s"],
    [/Тракт /g, "Path "],
    [/Язык интерфейса сохраняется в /g, "The interface language is saved in "],
    [/Перетащи аудиофайлы в окно — они сразу скопируются в /g, "Drop audio files into the window to copy them into "],
    [/, можно сразу несколько\. Файлы, добавленные в папку вручную, подхватываются кнопкой /g, ", multiple files are fine. Files added manually appear after clicking "],
    [/ без перезапуска\. Тег /g, " without restarting. The "],
    [/ ставится автоматически, когда звуку назначена горячая клавиша\./g, " is added automatically when a sound gets a shortcut."],
    [/Удалить «/g, "Delete “"],
    [/Сохранено как «/g, "Saved as “"],
    [/Удалить «/g, "Delete “"],
    [/Предпрослушивание: /g, "Preview: "],
    [/Хоткей: /g, "Hotkey: "],
    [/Провайдер: /g, "Provider: "],
    [/Ошибка конвертации: /g, "Conversion failed: "],
    [/Ошибка: /g, "Error: "],
    [/Максимум 24 модуля/g, "Maximum 24 modules"],
    [/Добавляю звуков: /g, "Adding sounds: "],
    [/добавлено: /g, "added: "],
    [/удалено: /g, "removed: "],
    [/Библиотека обновлена — /g, "Library updated — "],
    [/Пропущено дубликатов: /g, "Duplicates skipped: "],
    [/уже в библиотеке/g, "already in the library"],
    [/одинаковый файл/g, "identical file"],
    [/уже существует/g, "already exists"],
    [/ · DSP /g, " · DSP "],
    [/ · AI /g, " · AI "],
    [/ мс/g, " ms"],
    [/≈/g, "~"],
    [/«/g, "“"],
    [/»/g, "”"],
  ];
  let out = source;
  for (const [re, replacement] of rules) out = out.replace(re, replacement);
  return out;
}

function vdApplyI18n(root) {
  const target = root || document;
  const walker = document.createTreeWalker(target, NodeFilter.SHOW_TEXT, null, false);
  const nodes = [];
  while (walker.nextNode()) nodes.push(walker.currentNode);
  for (const node of nodes) {
    const parent = node.parentElement;
    if (!parent || parent.tagName === "SCRIPT" || parent.tagName === "STYLE") continue;
    // If app code replaced the text after our last pass, treat the new value
    // as the source string instead of pinning the first value forever.
    if (!vdOriginalText.has(node) ||
        (vdAppliedText.has(node) && node.nodeValue !== vdAppliedText.get(node)))
        vdOriginalText.set(node, node.nodeValue);
    const original = vdOriginalText.get(node);
    const next = vdTranslateString(original);
    if (node.nodeValue !== next) node.nodeValue = next;
    vdAppliedText.set(node, next);
  }
  const elements = target.querySelectorAll ? Array.from(target.querySelectorAll("*")) : [];
  if (target.nodeType === 1) elements.unshift(target);
  for (const el of elements) {
    if (el.tagName === "SCRIPT" || el.tagName === "STYLE") continue;
    let originals = vdOriginalAttrs.get(el);
    if (!originals) { originals = {}; vdOriginalAttrs.set(el, originals); }
    let applied = vdAppliedAttrs.get(el);
    if (!applied) { applied = {}; vdAppliedAttrs.set(el, applied); }
    for (const attr of vdI18nAttrs) {
      if (!el.hasAttribute(attr)) continue;
      const current = el.getAttribute(attr);
      if (!Object.prototype.hasOwnProperty.call(originals, attr) ||
          (Object.prototype.hasOwnProperty.call(applied, attr) && current !== applied[attr]))
        originals[attr] = current;
      const next = vdTranslateString(originals[attr]);
      if (current !== next) el.setAttribute(attr, next);
      applied[attr] = next;
    }
  }
}

function vdSetLocale(lang) {
  const next = (lang === "en") ? "en" : "ru";
  const changed = next !== vdLocale;
  vdLocale = next;
  document.documentElement.lang = next;
  try { localStorage.setItem("voicedeck-language", next); } catch (_) {}
  vdI18nApplying = true;
  vdApplyI18n(document);
  vdI18nApplying = false;
  return changed;
}

function vdT(source) { return vdTranslateString(source); }

function vdWatchI18n() {
  const observer = new MutationObserver(() => {
    if (vdI18nApplying) return;
    vdI18nApplying = true;
    vdApplyI18n(document);
    vdI18nApplying = false;
  });
  observer.observe(document.body, {subtree: true, childList: true, characterData: true, attributes: true,
    attributeFilter: vdI18nAttrs});
}
