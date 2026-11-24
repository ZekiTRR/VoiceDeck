#include "app/App.hpp"

#include "core/platform/AutoStart.hpp"
#include "core/platform/Elevation.hpp"
#include "core/util/Log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>

namespace vd {

namespace {
std::string toLower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::size_t findByName(const std::vector<DeviceEntry>& devs, const std::string& name) {
    for (std::size_t i = 0; i < devs.size(); ++i)
        if (toLower(devs[i].name) == toLower(name)) return i;
    return SIZE_MAX;
}

std::size_t findBySubstr(const std::vector<DeviceEntry>& devs, const std::string& part) {
    for (std::size_t i = 0; i < devs.size(); ++i)
        if (toLower(devs[i].name).find(toLower(part)) != std::string::npos) return i;
    return SIZE_MAX;
}

std::size_t defaultDevice(const std::vector<DeviceEntry>& devs) {
    for (std::size_t i = 0; i < devs.size(); ++i)
        if (devs[i].isDefault) return i;
    return devs.empty() ? SIZE_MAX : 0;
}

std::string normalizedPath(std::filesystem::path p) {
    std::error_code ec;
    p = std::filesystem::absolute(p, ec);
    std::string s = p.lexically_normal().string();
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct SoundFingerprint {
    std::uintmax_t size = 0;
    std::uint64_t hash = 0;
    bool valid = false;
};

// FNV-1a over the whole file — cheap enough for sound clips and catches
// "same audio, different filename" duplicates.
SoundFingerprint fingerprintOf(const std::filesystem::path& p) {
    SoundFingerprint fp;
    std::error_code ec;
    fp.size = std::filesystem::file_size(p, ec);
    if (ec || fp.size == 0) return fp;
    std::ifstream in(p, std::ios::binary);
    if (!in) return fp;
    std::array<char, 16384> buf{};
    std::uint64_t h = 1469598103934665603ull;
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(buf[static_cast<std::size_t>(i)]);
            h *= 1099511628211ull;
        }
    }
    if (in.bad()) return fp;
    fp.hash = h;
    fp.valid = true;
    return fp;
}
} // namespace

App::App() : bank(Settings::appDataDir() / "sounds") {
    dataDir = Settings::appDataDir();
    settingsFile = dataDir / "settings.json";
    soundsFile = dataDir / "sounds.json";
    charactersFile = dataDir / "characters.json";
}

void App::init() {
    settings.load(settingsFile);
    bypassOn = settings.bypass;
    duckingOn = settings.ducking;

    // Voice characters: built-ins are FACTORY-LOCKED and always regenerated
    // from defaultPresets(); only user-made (custom) characters are restored
    // from characters.json. This guarantees live tweaks can never be saved
    // into a built-in character such as "Male".
    {
        std::vector<VoicePreset> loaded;
        const bool hadFile = loadPresets(charactersFile, loaded);
        presets = defaultPresets();
        if (hadFile) {
            for (const VoicePreset& lp : loaded) {
                if (!lp.custom) {
                    // Keep only the user's hotkey binding for a built-in.
                    for (VoicePreset& p : presets)
                        if (p.name == lp.name) { p.hotkey = lp.hotkey; break; }
                    continue;
                }
                presets.push_back(lp); // custom character — preserved verbatim
            }
        }
        savePresets(charactersFile, presets); // rewrite a clean, normalized file
    }
    engine.setPresets(presets);

    if (!engine.init())
        log::error("failed to initialize audio context (WASAPI)");
    std::string err;
    if (!engine.refreshDevices(err)) log::warn("device enumeration failed: %s", err.c_str());

    // Sound library.
    const auto soundCombos = bank.loadFromJson(soundsFile, AudioEngine::kSampleRate,
                                               AudioEngine::kChannels);
    if (!bank.snapshot().empty())
        log::info("sound library: %zu clip(s) loaded", bank.snapshot().size());
    soundCombos_ = soundCombos; // restored by startHotkeyService()

    // The device period is chosen at start(), so apply the persisted mode
    // BEFORE opening the devices.
    engine.setLatencyMode(settings.latencyMode);
    engine.setVadThresholdDb(settings.vadThresholdDb);

    // Pick devices: saved names first, then auto (virtual cable).
    std::size_t capIdx = SIZE_MAX;
    std::size_t playIdx = SIZE_MAX;
    {
        const auto& caps = engine.captureDevices();
        const auto& plays = engine.playbackDevices();
        if (!caps.empty()) {
            if (!settings.captureDevice.empty()) capIdx = findByName(caps, settings.captureDevice);
            if (capIdx == SIZE_MAX) capIdx = defaultDevice(caps);
        }
        if (!plays.empty()) {
            if (!settings.playbackDevice.empty())
                playIdx = findByName(plays, settings.playbackDevice);
            // Auto-detect a virtual cable to use as the "mic" other apps see:
            // VB-CABLE ("CABLE Input"), Virtual Audio Cable ("Line 1 (...)"),
            // Voicemod ("Dummy Output (Voicemod)").
            if (playIdx == SIZE_MAX) playIdx = findBySubstr(plays, "cable input");
            if (playIdx == SIZE_MAX) playIdx = findBySubstr(plays, "virtual audio cable");
            if (playIdx == SIZE_MAX) playIdx = findBySubstr(plays, "(voicemod)");
            if (playIdx == SIZE_MAX) {
                playIdx = defaultDevice(plays);
                log::warn("no virtual cable found - install VB-CABLE "
                          "(https://vb-audio.com/Cable/) or Virtual Audio Cable, "
                          "then pick the output device in settings");
            }
        }
    }
    if (capIdx != SIZE_MAX && playIdx != SIZE_MAX) {
        if (engine.start(capIdx, playIdx, err)) {
            settings.captureDevice = engine.currentCaptureName();
            settings.playbackDevice = engine.currentPlaybackName();
            if (!settings.monitorDevice.empty()) {
                const std::size_t mIdx = findByName(engine.playbackDevices(),
                                                    settings.monitorDevice);
// Don't auto-start the monitor: the user controls it via the UI.
            // Only capture/playback devices are auto-restored.
            }
            settings.save(settingsFile);
        } else {
            log::warn("failed to start audio: %s - pick devices manually", err.c_str());
        }
    }

    // Initial engine state.
    {
        Command c;
        c.type = Command::Type::SetBypass;
        c.value = bypassOn ? 1.f : 0.f;
        engine.submit(c);
    }
    {
        Command c;
        c.type = Command::Type::SetDucking;
        c.value = duckingOn ? 1.f : 0.f;
        engine.submit(c);
    }
    if (!presets.empty()) {
        Command c;
        c.type = Command::Type::SetPreset;
        c.soundId = 0;
        engine.submit(c);
    }
    applyAiSettings();
}

void App::applyAiSettings() {
    auto& c = engine.aiControls();
    c.primeMarginMs.store(std::max(0, settings.aiPrimeMarginMs));
    c.fadeMs.store(std::max(1, settings.aiFadeMs));
    c.autoLimit.store(settings.aiAutoLimit);
    c.flapLimit.store(std::max(1, settings.aiFlapLimit));
    AiProvider p;
    if (!ParseAiProvider(settings.aiProvider, p)) p = AiProvider::Auto;
    engine.aiService().setProviderHint(p);
}

void App::startHotkeyService() {
    hotkeys.start(
        [this](const HotkeyAction& a) {
            Command c;
            switch (a.kind) {
                case HotkeyAction::Kind::Sound:
                    c.type = Command::Type::PlaySound;
                    c.soundId = a.id;
                    engine.submit(c);
                    if (onHotkeyEvent) onHotkeyEvent("soundPlayed");
                    break;
                case HotkeyAction::Kind::Preset:
                    c.type = Command::Type::SetPreset;
                    c.soundId = a.id;
                    engine.submit(c);
                    if (onHotkeyEvent) onHotkeyEvent("presetChanged");
                    break;
                case HotkeyAction::Kind::VoiceToggle:
                    c.type = Command::Type::SetDsp;
                    c.value = engine.dspOn() ? 0.f : 1.f;
                    engine.submit(c);
                    log::info("voice DSP: %s (hotkey)", !engine.dspOn() ? "ON" : "OFF");
                    if (onHotkeyEvent) onHotkeyEvent("dspChanged");
                    break;
                case HotkeyAction::Kind::AiToggle:
                    if (!engine.aiService().isReady()) {
                        log::warn("AI hotkey: no voice loaded");
                        break;
                    }
                    engine.aiControls().enabled.store(!engine.aiEnabled());
                    log::info("AI voice: %s (hotkey)", engine.aiEnabled() ? "ON" : "OFF");
                    if (onHotkeyEvent) onHotkeyEvent("aiChanged");
                    break;
                default:
                    break;
            }
        },
        [this]() {
            Command c;
            c.type = Command::Type::StopAll;
            engine.submit(c);
            if (onHotkeyEvent) onHotkeyEvent("stopAll");
        },
        [this]() {
            setBypass(!bypassOn);
            log::info("bypass: %s (tray)", engine.bypassEnabled() ? "ON" : "OFF");
            if (onHotkeyEvent) onHotkeyEvent("bypassChanged");
        },
        [this]() {
            if (onExitRequested) onExitRequested();
        });

    hotkeys.setBypassLabel(bypassOn);

    // Restore persisted bindings.
    for (const auto& [id, combo] : soundCombos_) {
        const std::string berr =
            (id == 0) ? hotkeys.bindStopAll(combo) : hotkeys.bindSound(id, combo);
        if (!berr.empty()) log::warn("hotkey '%s' not restored: %s", combo.c_str(), berr.c_str());
        // Older libraries (saved before tags existed) still get the built-in
        // marker for every sound that actually has a hotkey.
        if (id != 0 && berr.empty() && !combo.empty()) bank.addTag(id, kTagHotkey);
    }
    for (std::size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].hotkey.empty()) continue;
        const std::string berr = hotkeys.bindPreset(i, presets[i].hotkey);
        if (!berr.empty())
            log::warn("hotkey '%s' (character '%s') not restored: %s",
                      presets[i].hotkey.c_str(), presets[i].name.c_str(), berr.c_str());
    }
    if (!settings.voiceHotkey.empty()) {
        const std::string berr = hotkeys.bindVoiceToggle(settings.voiceHotkey);
        if (!berr.empty()) log::warn("voice hotkey not restored: %s", berr.c_str());
    }
    if (!settings.aiHotkey.empty()) {
        const std::string berr = hotkeys.bindAiToggle(settings.aiHotkey);
        if (!berr.empty()) log::warn("AI hotkey not restored: %s", berr.c_str());
    }
}

// ---------------------------------------------------------------- soundboard

std::uint64_t App::addSound(const std::filesystem::path& path, const std::string& name,
                             std::string& err) {
    // Duplicate guard for every entry point (dialog, drop, CLI). Reject both
    // the same file path and identical audio bytes under a different name.
    const std::string key = normalizedPath(path);
    const SoundFingerprint fp = fingerprintOf(path);
    for (const Sound* s : bank.snapshot()) {
        if (normalizedPath(s->path) == key) {
            err = "уже в библиотеке: " + s->name;
            return 0;
        }
        if (fp.valid) {
            std::error_code sec;
            if (std::filesystem::file_size(s->path, sec) != fp.size) continue;
            const SoundFingerprint other = fingerprintOf(s->path);
            if (other.valid && other.hash == fp.hash) {
                err = "уже в библиотеке (одинаковый файл): " + s->name;
                return 0;
            }
        }
    }
    return bank.add(path, name, AudioEngine::kSampleRate, AudioEngine::kChannels, err);
}

std::string App::removeSound(std::uint64_t id) {
    const Sound* victim = bank.find(id);
    if (!victim) return "no sound with id " + std::to_string(id);

    auto normKey = [](std::filesystem::path p) {
        std::error_code e;
        p = std::filesystem::absolute(p, e);
        std::string s = p.lexically_normal().string();
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::filesystem::path stored = victim->path; // copy — pointer dies below
    const std::string key = normKey(stored);
    bool shared = false;
    for (const Sound* s : bank.snapshot()) {
        if (s->id != id && normKey(s->path) == key) { shared = true; break; }
    }

    Command stop;
    stop.type = Command::Type::StopSound;
    stop.soundId = id;
    engine.submit(stop);
    engine.drainCommands();
    for (const auto& v : engine.mixer().snapshot())
        if (v.soundId == id) return "clip is playing - try again in a second";
    std::string err = bank.remove(id);
    if (!err.empty()) return err;

    // Delete the backing file too — but only when it lives in our portable
    // data\sounds folder and no other clip references it. Files added from
    // elsewhere (fallback when the copy failed) are left untouched.
    const std::string dirKey = normKey(dataDir / "sounds");
    if (!shared && key.size() > dirKey.size() &&
        key.compare(0, dirKey.size(), dirKey) == 0 &&
        (key[dirKey.size()] == '\\' || key[dirKey.size()] == '/')) {
        std::error_code ec;
        std::filesystem::remove(stored, ec);
        if (ec)
            log::warn("could not delete sound file '%s': %s", stored.string().c_str(),
                      ec.message().c_str());
        else
            log::info("removed sound file: %s", stored.string().c_str());
    }
    bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    return "";
}

void App::playSound(std::uint64_t id) {
    Command c;
    c.type = Command::Type::PlaySound;
    c.soundId = id;
    engine.submit(c);
}

void App::stopSound(std::uint64_t id) {
    Command c;
    c.type = Command::Type::StopSound;
    c.soundId = id;
    engine.submit(c);
}

void App::stopAllSounds() {
    Command c;
    c.type = Command::Type::StopAll;
    engine.submit(c);
}

void App::setSoundVolume(std::uint64_t id, int pct) {
    pct = std::max(0, std::min(200, pct));
    Command c;
    c.type = Command::Type::SetSoundVolume;
    c.soundId = id;
    c.value = float(pct) / 100.f;
    engine.submit(c);
}

void App::setSoundLoop(std::uint64_t id, bool on) {
    Command c;
    c.type = Command::Type::SetSoundLoop;
    c.soundId = id;
    c.value = on ? 1.f : 0.f;
    engine.submit(c);
}

std::string App::setVadThresholdDb(float db) {
    db = std::max(-60.0f, std::min(-20.0f, db));
    settings.vadThresholdDb = db;
    engine.setVadThresholdDb(db);
    settings.save(settingsFile);
    return "";
}

// ---------------------------------------------------------------- tags

std::string App::setSoundTags(std::uint64_t id, const nlohmann::json& tags) {
    std::vector<std::string> parsed;
    if (tags.is_array()) {
        for (const auto& t : tags)
            if (t.is_string()) parsed.push_back(t.get<std::string>());
    }
    // The built-in tag cannot be faked by the tag editor: it is derived from
    // the actual hotkey binding below.
    const bool hasHotkey = bank.hasTag(id, kTagHotkey);
    std::vector<std::string> user;
    for (const std::string& t : parsed) {
        if (toLower(t) == kTagHotkey) continue;
        user.push_back(t);
    }
    if (hasHotkey) user.emplace_back(kTagHotkey);
    const std::string err = bank.setTags(id, user);
    if (!err.empty()) return err;
    bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    return "";
}

std::string App::addSoundTag(std::uint64_t id, const std::string& tag) {
    if (toLower(tag) == kTagHotkey) return "встроенный тег — назначь хоткей";
    const std::string err = bank.addTag(id, tag);
    if (!err.empty()) return err;
    bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    return "";
}

std::string App::removeSoundTag(std::uint64_t id, const std::string& tag) {
    if (toLower(tag) == kTagHotkey) return "встроенный тег — сними хоткей";
    const std::string err = bank.removeTag(id, tag);
    if (!err.empty()) return err;
    bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    return "";
}

std::string App::bindSoundHotkey(std::uint64_t id, const std::string& combo) {
    const Sound* s = bank.find(id);
    if (!s) return "no sound with id " + std::to_string(id);
    std::string err;
    if (combo.empty()) {
        hotkeys.unbindSound(id);
        bank.removeTag(id, kTagHotkey);
    } else {
        err = hotkeys.bindSound(id, combo);
        if (err.empty()) bank.addTag(id, kTagHotkey);
    }
    saveAll();
    return err;
}

// ------------------------------------------------------------ audio editor

std::string App::editSound(std::uint64_t id, const nlohmann::json& def) {
    const Sound* victim = bank.find(id);
    if (!victim) return "no sound with id " + std::to_string(id);

    // Never mutate PCM while the audio callback may be reading it.
    Command stop;
    stop.type = Command::Type::StopSound;
    stop.soundId = id;
    engine.submit(stop);
    engine.drainCommands();
    for (const auto& v : engine.mixer().snapshot())
        if (v.soundId == id) return "звук ещё играет — останови его и повтори";

    auto getNum = [&](const char* key, double fallback) {
        if (!def.contains(key) || !def.at(key).is_number()) return fallback;
        return def.at(key).get<double>();
    };
    double trimStart = std::max(0.0, getNum("trimStart", 0.0));
    double trimEnd = getNum("trimEnd", 0.0); // 0 = full length
    const float gain = float(std::max(0.0, std::min(4.0, getNum("gain", 1.0))));

    std::vector<SoundRegion> regions;
    if (def.contains("regions") && def.at("regions").is_array()) {
        for (const auto& r : def.at("regions")) {
            if (!r.is_object()) continue;
            SoundRegion reg;
            reg.startSec = std::max(0.0, r.value("start", 0.0));
            reg.endSec = std::max(0.0, r.value("end", 0.0));
            reg.gain = float(std::max(0.0, std::min(4.0, r.value("gain", 1.0))));
            if (reg.endSec - reg.startSec > 0.01) regions.push_back(reg);
        }
        std::sort(regions.begin(), regions.end(),
                  [](const SoundRegion& a, const SoundRegion& b) { return a.startSec < b.startSec; });
    }

    const std::string err = bank.applyEdits(id, trimStart, trimEnd, gain, regions);
    if (!err.empty()) return err;
    bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    return "";
}

// -------------------------------------------------------- Windows integration

bool App::autoStartEnabled() const {
    return isAutoStartEnabled();
}

std::string App::setAutoStart(bool on) {
    return vd::setAutoStart(on);
}

std::string App::setRunAsAdmin(bool on, bool& relaunching) {
    relaunching = false;
    if (!on) {
        settings.runAsAdmin = false;
        settings.save(settingsFile);
        return "";
    }
    if (isElevated()) {
        settings.runAsAdmin = true;
        settings.save(settingsFile);
        return "";
    }

    // Persist first, then ask Windows to relaunch us elevated. If the user
    // declines the UAC prompt, keep the setting off so they are not locked
    // out of the toggle on the next launch.
    settings.runAsAdmin = true;
    settings.save(settingsFile);
    const ElevateResult r = relaunchElevated(cliMode ? "--cli" : "");
    if (r == ElevateResult::Launched) {
        relaunching = true;
        return "";
    }
    settings.runAsAdmin = false;
    settings.save(settingsFile);
    if (r == ElevateResult::Cancelled) return "Отменено — запуск от имени администратора не включён";
    return "Не удалось запустить приложение от имени администратора";
}

bool App::maybeElevate(const std::string& args) {
    // Guard against an elevation loop if the token check ever misreports.
    if (args.find("--elevated") != std::string::npos) return false;
    Settings s;
    if (!s.load(settingsFile) || !s.runAsAdmin || isElevated()) return false;
    const ElevateResult r = relaunchElevated(args);
    if (r == ElevateResult::Launched) {
        log::info("relaunching elevated (runAsAdmin)");
        return true;
    }
    log::warn("elevation was not started (%s) - continuing normally",
              r == ElevateResult::Cancelled ? "cancelled by user" : "failed");
    // Do not silently disable the preference on a cancelled startup prompt:
    // the user can still turn it off in Settings.
    return false;
}

// ---------------------------------------------------------------- characters

bool App::applyPreset(int idx) {
    if (idx < 0 || std::size_t(idx) >= presets.size()) return false;
    // Selecting a different character drops any temporary live override so
    // factory values are heard again when that character is picked later.
    if (livePresetIndex_ >= 0 && livePresetIndex_ != idx) {
        engine.drainCommands();
        engine.setPresets(presets);
        livePresetIndex_ = -1;
    }
    Command c;
    c.type = Command::Type::SetPreset;
    c.soundId = static_cast<std::uint64_t>(idx);
    engine.submit(c);
    return true;
}

void App::setDsp(bool on) {
    Command c;
    c.type = Command::Type::SetDsp;
    c.value = on ? 1.f : 0.f;
    engine.submit(c);
}

void App::setBypass(bool on) {
    bypassOn = on;
    Command c;
    c.type = Command::Type::SetBypass;
    c.value = on ? 1.f : 0.f;
    engine.submit(c);
    hotkeys.setBypassLabel(on);
    settings.bypass = on;
    settings.save(settingsFile);
}

void App::setDucking(bool on) {
    duckingOn = on;
    Command c;
    c.type = Command::Type::SetDucking;
    c.value = on ? 1.f : 0.f;
    engine.submit(c);
    settings.ducking = on;
    settings.save(settingsFile);
}

void App::setPitchOverride(float semi, bool on) {
    Command c;
    c.type = Command::Type::SetTransposeOverride;
    c.soundId = on ? 1 : 0;
    c.value = std::max(-24.f, std::min(24.f, semi));
    engine.submit(c);
}

void App::setFormantOverride(float semi, bool on) {
    Command c;
    c.type = Command::Type::SetFormantOverride;
    c.soundId = on ? 1 : 0;
    c.value = std::max(-24.f, std::min(24.f, semi));
    engine.submit(c);
}

// ---------------------------------------------------------------- AI voice

std::string App::loadAiVoice(const std::string& name) {
    if (name.empty()) return "empty voice name";
    std::string wanted = name;
    const auto voices = engine.aiLibrary().scanVoices();
    const AiVoiceInfo* found = nullptr;
    for (const auto& v : voices) {
        if (toLower(v.name) == toLower(wanted)) {
            found = &v;
            break;
        }
    }
    if (!found) {
        if (voices.empty()) return "no AI voices installed (see the AI tab)";
        return "no AI voice '" + name + "'";
    }
    AiVoiceInfo v = *found;
    log::info("loading '%s' (a few seconds, GPU warm-up)...", v.name.c_str());
    const std::string err = engine.aiService().loadVoice(v);
    if (!err.empty()) return err;
    engine.notifyAiWorker(); // new shape/session is ready
    Command p;
    p.type = Command::Type::SetAiPitch;
    p.value = float(v.defaultPitchShift);
    engine.submit(p);
    return "";
}

std::string App::importAiVoice(const std::filesystem::path& onnxPath) {
    AiVoiceInfo out;
    const std::string err = engine.aiLibrary().import(onnxPath, out);
    if (!err.empty()) return err;
    log::info("imported AI voice '%s' (%s)", out.name.c_str(), out.modelPath.string().c_str());
    return "";
}

void App::setAi(bool on) {
    if (on && !engine.aiService().isReady()) {
        // The front-end checks readiness; still guard here.
        log::warn("AI: no voice loaded");
        return;
    }
    Command c;
    c.type = Command::Type::SetAi;
    c.value = on ? 1.f : 0.f;
    engine.submit(c);
}

void App::setAiPitch(float semi) {
    Command c;
    c.type = Command::Type::SetAiPitch;
    c.value = std::max(-24.f, std::min(24.f, semi));
    engine.submit(c);
}

void App::setAiMargin(int ms) {
    engine.aiControls().primeMarginMs.store(std::max(0, std::min(1000, ms)));
    settings.aiPrimeMarginMs = engine.aiControls().primeMarginMs.load();
    settings.save(settingsFile);
}

void App::setAiFade(int ms) {
    engine.aiControls().fadeMs.store(std::max(1, std::min(1000, ms)));
    settings.aiFadeMs = engine.aiControls().fadeMs.load();
    settings.save(settingsFile);
}

void App::setAiAutolimit(bool on, int flapLimit) {
    engine.aiControls().autoLimit.store(on);
    if (on) engine.aiControls().flapLimit.store(std::max(1, std::min(100, flapLimit)));
    settings.aiAutoLimit = on;
    settings.aiFlapLimit = engine.aiControls().flapLimit.load();
    settings.save(settingsFile);
}

std::string App::setAiProvider(const std::string& name) {
    AiProvider p;
    if (!ParseAiProvider(name, p)) return "unknown provider (use: auto|cuda|dml|cpu)";
    engine.aiService().setProviderHint(p);
    engine.notifyAiWorker();
    settings.aiProvider = AiProviderName(p);
    settings.save(settingsFile);
    return "";
}

// ---------------------------------------------------------------- routing

void App::selectDevices(std::size_t captureIdx, std::size_t playbackIdx) {
    std::string err;
    if (engine.start(captureIdx, playbackIdx, err)) {
        settings.captureDevice = engine.captureDevices()[captureIdx].name;
        settings.playbackDevice = engine.playbackDevices()[playbackIdx].name;
        settings.monitorDevice.clear();
        settings.save(settingsFile);
    } else {
        log::error("select failed: %s", err.c_str());
    }
}

bool App::setMonitor(bool on, std::size_t playbackIdx) {
    if (!on) {
        engine.stopMonitor();
        // Keep the chosen device so toggling monitoring back on restores the
        // SAME output instead of falling back to the system default (which is
        // often the virtual cable and therefore silent to the user).
        return false;
    }
    if (!engine.isRunning()) return false;
    engine.monitorPreviewOnly_.store(false); // user explicitly wants full monitoring
    std::size_t idx = playbackIdx;
    if (idx == SIZE_MAX) {
        const auto& plays = engine.playbackDevices();
        if (!settings.monitorDevice.empty()) {
            for (std::size_t i = 0; i < plays.size(); ++i)
                if (plays[i].name == settings.monitorDevice) {
                    idx = i;
                    break;
                }
        }
        if (idx == SIZE_MAX) {
            for (std::size_t i = 0; i < plays.size(); ++i)
                if (plays[i].isDefault) {
                    idx = i;
                    break;
                }
        }
        if (idx == SIZE_MAX && !plays.empty()) idx = 0;
    }
    if (idx == SIZE_MAX || idx >= engine.playbackDevices().size()) return false;
    std::string err;
    if (!engine.startMonitor(idx, err)) return false;
    settings.monitorDevice = engine.playbackDevices()[idx].name;
    settings.save(settingsFile);
    return true;
}

// ---------------------------------------------------------------- user presets

int App::addPreset(const nlohmann::json& def, std::string& err) {
    VoicePreset p;
    p.name = def.value("name", std::string());
    for (char& c : p.name) {
        if (static_cast<unsigned char>(c) < ' ') c = ' ';
    }
    while (!p.name.empty() && p.name.front() == ' ') p.name.erase(p.name.begin());
    while (!p.name.empty() && p.name.back() == ' ') p.name.pop_back();
    if (p.name.empty()) {
        err = "имя персонажа обязательно";
        return -1;
    }
    for (const VoicePreset& ex : presets) {
        std::string a = p.name, b = ex.name;
        for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : b) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (a == b) {
            err = "персонаж '" + p.name + "' уже существует";
            return -1;
        }
    }

    auto clampf = [](double v, double lo, double hi) {
        return float(std::max(lo, std::min(hi, v)));
    };
    p.transpose = clampf(def.value("transpose", 0.0), -24.0, 24.0);
    p.formant = clampf(def.value("formant", 0.0), -24.0, 24.0);
    p.timbre = clampf(def.value("timbre", 0.0), -1.0, 1.0);
    p.strength = clampf(def.value("strength", 1.0), 0.0, 1.0);
    p.lfoRate = clampf(def.value("lfoRate", 0.0), 0.0, 10.0);
    p.lfoDepth = clampf(def.value("lfoDepth", 0.0), 0.0, 12.0);

    static const char* kSpaces[] = {"none", "room", "hall", "cave", "helmet", "radio",
                                    "megaphone"};
    static const char* kEffects[] = {"none", "robot", "alien", "atari", "clone", "silence"};
    auto pick = [&](const char* key, const char* const* allowed, size_t n, const char* dflt) {
        std::string v = def.value(key, std::string(dflt));
        for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (size_t i = 0; i < n; ++i)
            if (v == allowed[i]) return v;
        return std::string(dflt);
    };
    p.space = pick("space", kSpaces, sizeof(kSpaces) / sizeof(kSpaces[0]), "none");
    p.effect = pick("effect", kEffects, sizeof(kEffects) / sizeof(kEffects[0]), "none");
    p.gate = def.value("gate", true);
    p.comp = def.value("comp", true);
    if (def.contains("eq") && def["eq"].is_array()) {
        for (int i = 0; i < 8 && std::size_t(i) < def["eq"].size(); ++i)
            p.eq[i] = clampf(def["eq"][std::size_t(i)].get<double>(), -12, 12);
    }
    if (def.contains("chain") && def["chain"].is_array()) {
        for (const auto& c : def["chain"])
            if (c.is_string()) p.chain.push_back(c.get<std::string>());
    }
    p.custom = true;

    // Mutation protocol: no preset command may be in flight while the audio
    // thread reads the vector; drain first, then swap the copy in.
    engine.drainCommands();
    livePresetIndex_ = -1; // a live override must not leak into the new list
    presets.push_back(p);
    engine.setPresets(presets);
    savePresets(charactersFile, presets);
    log::info("preset added: '%s' (index %zu)", p.name.c_str(), presets.size() - 1);
    return int(presets.size() - 1);
}

std::string App::removePreset(int idx) {
    if (idx <= 0 || std::size_t(idx) >= presets.size())
        return "этот персонаж нельзя удалить";
    if (!presets[std::size_t(idx)].custom) return "встроенные персонажи защищены";
    engine.drainCommands();
    livePresetIndex_ = -1;

    // Capture hotkeys by old index, unbind the stale tail, rebind shifted.
    const auto combos = hotkeys.allPresetCombos();
    const std::size_t oldSize = presets.size();
    std::vector<VoicePreset> next;
    next.reserve(oldSize - 1);
    for (std::size_t i = 0; i < oldSize; ++i)
        if (int(i) != idx) next.push_back(presets[i]);
    for (std::size_t i = next.size(); i < oldSize; ++i)
        hotkeys.unbindPreset(i); // stale bindings beyond the shrunken list
    for (const auto& [oldIdx, combo] : combos) {
        if (int(oldIdx) == idx) continue;
        const std::size_t ni = (oldIdx > std::size_t(idx)) ? oldIdx - 1 : oldIdx;
        hotkeys.bindPreset(ni, combo);
    }
    presets = std::move(next);
    engine.setPresets(presets);
    savePresets(charactersFile, presets);
    // If the removed character was active, fall back to "None".
    if (engine.voicePresetIndex() == idx) applyPreset(0);
    log::info("preset removed (was index %d)", idx);
    return "";
}

std::string App::previewSound(std::uint64_t id) {
    if (!bank.find(id)) return "no sound";
    if (!engine.monitorRunning()) {
        // Preview starts the monitor in PREVIEW-ONLY mode (no self-monitoring).
        std::size_t idx = SIZE_MAX;
        if (!settings.monitorDevice.empty()) {
            const auto& plays = engine.playbackDevices();
            for (std::size_t i = 0; i < plays.size(); ++i)
                if (plays[i].name == settings.monitorDevice) { idx = i; break; }
        }
        if (!setMonitor(true, idx))
            return "включи оригинальный выход в настройках";
        engine.monitorPreviewOnly_.store(true);
        monitorStartedForPreview_ = true;
    }
    Command c;
    c.type = Command::Type::PreviewSound;
    c.soundId = id;
    engine.submit(c);
    return "";
}

void App::stopPreview() {
    Command c;
    c.type = Command::Type::StopPreview;
    engine.submit(c);
    // Preview-only mode suppresses the voice in the monitor. Restore the
    // previous state as soon as the preview voice is stopped, otherwise
    // self-monitoring ("voice preview") silently stops working.
    if (monitorStartedForPreview_) {
        monitorStartedForPreview_ = false;
        engine.stopMonitor(); // do NOT clear settings.monitorDevice
    } else if (engine.monitorRunning() && engine.monitorPreviewOnly_.load()) {
        engine.monitorPreviewOnly_.store(false);
    }
}

std::pair<std::size_t, std::size_t> App::refreshSounds() {
    std::size_t added = 0, removed = 0;
    const std::filesystem::path dir = dataDir / "sounds";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return {0, 0};

    auto normKey = [](std::filesystem::path p) {
        std::error_code e;
        p = std::filesystem::absolute(p, e);
        std::string s = p.lexically_normal().string();
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    static const char* kExts[] = {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".opus"};
    auto isAudio = [](const std::filesystem::path& p) {
        std::string ext = p.extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const char* x : kExts) if (ext == x) return true;
        return false;
    };

    // Files currently present in the folder.
    std::vector<std::string> present;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        std::error_code fileEc;
        if (entry.is_regular_file(fileEc) && isAudio(entry.path()))
            present.push_back(normKey(entry.path()));
    }

    const std::string dirKey = normKey(dir);
    auto insideFolder = [&](const std::string& key) {
        if (key.size() <= dirKey.size() || key.compare(0, dirKey.size(), dirKey) != 0)
            return false;
        const char sep = key[dirKey.size()];
        return sep == '\\' || sep == '/';
    };

    // 1) Drop library entries whose backing file was deleted from data\sounds.
    struct Stale { std::uint64_t id; std::string name; };
    std::vector<Stale> stale;
    for (const Sound* s : bank.snapshot()) {
        const std::string key = normKey(s->path);
        if (!insideFolder(key)) continue; // clips stored elsewhere are left alone
        if (std::find(present.begin(), present.end(), key) == present.end())
            stale.push_back({s->id, s->name});
    }
    if (!stale.empty()) {
        Command stopPreview;
        stopPreview.type = Command::Type::StopPreview;
        engine.submit(stopPreview);
    }
    for (const Stale& s : stale) {
        Command stop;
        stop.type = Command::Type::StopSound;
        stop.soundId = s.id;
        engine.submit(stop);
        engine.drainCommands();
        bool playing = false;
        for (const auto& v : engine.mixer().snapshot())
            if (v.soundId == s.id) { playing = true; break; }
        if (playing) continue; // retry on the next refresh
        if (bank.remove(s.id).empty()) {
            ++removed;
            log::info("refresh: removed '%s' (file gone)", s.name.c_str());
        }
    }

    // 2) Add files that appeared in the folder.
    std::vector<std::string> known;
    for (const Sound* s : bank.snapshot()) known.push_back(normKey(s->path));
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc) || !isAudio(entry.path())) continue;
        const std::string key = normKey(entry.path());
        if (std::find(known.begin(), known.end(), key) != known.end()) continue;
        std::string err;
        if (bank.add(entry.path(), "", AudioEngine::kSampleRate, AudioEngine::kChannels, err) != 0) {
            ++added;
            known.push_back(key);
            log::info("refresh: added '%s'", entry.path().filename().string().c_str());
        } else {
            log::warn("refresh: skipped '%s': %s", entry.path().filename().string().c_str(),
                      err.c_str());
        }
    }
    if (added || removed) bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    log::info("refresh: %zu added, %zu removed", added, removed);
    return {added, removed};
}

std::string App::setLatencyMode(int mode) {
    if (mode < 0 || mode > 2) mode = 0;
    engine.setLatencyMode(mode);
    settings.latencyMode = mode;
    settings.save(settingsFile);
    // Restart devices with the new WASAPI period if the engine is running.
    if (engine.isRunning()) {
        const std::size_t cap = engine.currentCaptureIdx();
        const std::size_t play = engine.currentPlaybackIdx();
        if (cap != std::size_t(-1) && play != std::size_t(-1)) {
            std::string err;
            engine.stopMonitor();
            engine.start(cap, play, err);
        }
    }
    return "";
}

void App::applyLivePreset(int idx, const VoicePreset& modified) {
    if (idx < 0 || std::size_t(idx) >= presets.size()) return;
    // Work on a COPY: App::presets stays factory-clean, so saveAll(),
    // addPreset() and removePreset() can never persist a live tweak.
    auto live = presets;
    live[std::size_t(idx)] = modified;
    engine.drainCommands();
    engine.setPresets(std::move(live));
    livePresetIndex_ = idx;
    applyPreset(idx);
}

void App::liveUpdatePreset(int idx, const nlohmann::json& def) {
    if (idx < 0 || std::size_t(idx) >= presets.size()) return;
    // Start from the factory value, or from the current override if this is
    // the same character — so successive slider moves accumulate.
    VoicePreset p = (livePresetIndex_ == idx)
                        ? engine.presets()[std::size_t(idx)]
                        : presets[std::size_t(idx)];
    auto clampf = [](double v, double lo, double hi) {
        return float(std::max(lo, std::min(hi, v)));
    };
    if (def.contains("transpose")) p.transpose = clampf(def["transpose"].get<double>(), -24, 24);
    if (def.contains("formant")) p.formant = clampf(def["formant"].get<double>(), -24, 24);
    if (def.contains("timbre")) p.timbre = clampf(def["timbre"].get<double>(), -1, 1);
    if (def.contains("strength")) p.strength = clampf(def["strength"].get<double>(), 0, 1);
    if (def.contains("space")) p.space = def["space"].get<std::string>();
    if (def.contains("effect")) p.effect = def["effect"].get<std::string>();
    if (def.contains("chain") && def["chain"].is_array()) {
        p.chain.clear();
        for (const auto& c : def["chain"])
            if (c.is_string()) p.chain.push_back(c.get<std::string>());
    }
    if (def.contains("eq") && def["eq"].is_array()) {
        for (int i = 0; i < 8 && std::size_t(i) < def["eq"].size(); ++i)
            p.eq[i] = clampf(def["eq"][std::size_t(i)].get<double>(), -12, 12);
    }
    applyLivePreset(idx, p);
}

// ---------------------------------------------------------------- state

std::string App::setPresetEq(int idx, const float eq[8]) {
    if (idx < 0 || std::size_t(idx) >= presets.size()) return "нет такого персонажа";
    VoicePreset p = (livePresetIndex_ == idx)
                        ? engine.presets()[std::size_t(idx)]
                        : presets[std::size_t(idx)];
    for (int i = 0; i < 8; ++i) p.eq[i] = std::max(-12.0f, std::min(12.0f, eq[i]));
    // Temporary only — never written to characters.json.
    applyLivePreset(idx, p);
    return "";
}

std::string App::renameSound(std::uint64_t id, const std::string& name) {
    const std::string err = bank.rename(id, name);
    if (!err.empty()) return err;
    bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    return "";
}

void App::saveAll() {
    engine.drainCommands();
    settings.voiceHotkey = hotkeys.voiceToggleCombo();
    settings.aiHotkey = hotkeys.aiToggleCombo();
    settings.save(settingsFile);
    bank.saveToJson(soundsFile, hotkeys.allSoundCombos());
    // Reflect currently-bound combos into the preset list, then persist.
    auto presetCombos = hotkeys.allPresetCombos();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        auto it = presetCombos.find(i);
        presets[i].hotkey = (it == presetCombos.end()) ? std::string() : it->second;
    }
    savePresets(charactersFile, presets);
}

nlohmann::json App::metersJson() const {
    nlohmann::json j;
    j["type"] = "meters";
    j["mic"] = engine.micLevel();
    j["dspOn"] = engine.dspOn();
    j["bypassOn"] = engine.bypassEnabled();
    j["preset"] = engine.voicePresetIndex();
    j["voiceLatency"] = engine.voiceLatencyMs();
    j["e2eLatency"] = engine.endToEndLatencyMs();
    j["micUnderruns"] = double(engine.micUnderruns());
    j["vadSleeping"] = engine.vadSleeping();
    j["vadThresholdDb"] = double(engine.vadThresholdDb());
    j["aiOn"] = engine.aiEnabled();
    j["aiReady"] = engine.aiService().isReady();
    j["aiLatency"] = engine.aiLatencyMs();
    j["aiUnderruns"] = engine.aiUnderruns();
    nlohmann::json playing = nlohmann::json::array();
    for (const auto& v : engine.mixer().snapshot())
        playing.push_back({{"id", v.soundId},
                           {"pos", v.totalFrames ? double(v.posFrames) / double(v.totalFrames) : 0.0},
                           {"paused", v.paused}});
    j["playing"] = std::move(playing);
    return j;
}

nlohmann::json App::statusJson() const {
    nlohmann::json j;
    j["type"] = "state";
    j["dspOn"] = engine.dspOn();
    j["bypassOn"] = engine.bypassEnabled();
    j["duckingOn"] = engine.duckingOn();
    j["monitorOn"] = engine.monitorRunning();
    j["preset"] = engine.voicePresetIndex();

    nlohmann::json presetsArr = nlohmann::json::array();
    const auto presetCombos = hotkeys.allPresetCombos();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        nlohmann::json p;
        p["idx"] = i;
        p["name"] = presets[i].name;
        p["transpose"] = double(presets[i].transpose);
        p["formant"] = double(presets[i].formant);
        p["timbre"] = double(presets[i].timbre);
        p["strength"] = double(presets[i].strength);
        p["lfoRate"] = double(presets[i].lfoRate);
        p["lfoDepth"] = double(presets[i].lfoDepth);
        p["space"] = presets[i].space;
        p["effect"] = presets[i].effect;
        p["custom"] = presets[i].custom;
        nlohmann::json eqArr = nlohmann::json::array();
        for (int b = 0; b < 8; ++b) eqArr.push_back(double(presets[i].eq[b]));
        p["eq"] = std::move(eqArr);
        nlohmann::json chainArr = nlohmann::json::array();
        for (const auto& c : presets[i].chain) chainArr.push_back(c);
        p["chain"] = std::move(chainArr);
        p["hotkey"] = (presetCombos.count(i)) ? presetCombos.at(i) : presets[i].hotkey;
        presetsArr.push_back(std::move(p));
    }
    j["presets"] = std::move(presetsArr);

    nlohmann::json soundsArr = nlohmann::json::array();
    const auto soundCombos = hotkeys.allSoundCombos();
    for (const Sound* s : bank.snapshot()) {
        nlohmann::json e;
        e["id"] = s->id;
        e["name"] = s->name;
        e["dur"] = s->durationSec;
        e["gain"] = double(s->gain.load());
        e["loop"] = bool(s->loop.load());
        e["eq"] = nlohmann::json::array({double(s->eq[0].load()), double(s->eq[1].load()),
                                         double(s->eq[2].load()), double(s->eq[3].load()),
                                         double(s->eq[4].load())});
        auto hk = soundCombos.find(s->id);
        e["hotkey"] = (hk == soundCombos.end()) ? std::string() : hk->second;
        nlohmann::json tags = nlohmann::json::array();
        for (const std::string& t : s->tags) tags.push_back(t);
        e["tags"] = std::move(tags);
        nlohmann::json regions = nlohmann::json::array();
        for (const SoundRegion& r : s->regions)
            regions.push_back({{"start", r.startSec}, {"end", r.endSec}, {"gain", double(r.gain)}});
        e["edits"] = {{"trimStart", s->trimStart},
                      {"trimEnd", s->trimEnd},
                      {"gain", double(s->editGain)},
                      {"regions", std::move(regions)}};

        // Waveform peaks: 256 buckets of max amplitude, normalized to 0..1.
        // `peaks` is the (possibly trimmed/edited) playback waveform;
        // `rawPeaks` is the full source used by the audio editor.
        constexpr std::size_t kPeaks = 256;
        auto peaksOf = [&](const std::vector<float>& pcm) {
            nlohmann::json peaks = nlohmann::json::array();
            if (!pcm.empty()) {
                std::vector<float> bucket(kPeaks, 0.0f);
                const std::size_t total = pcm.size();
                const std::size_t bucketSize = total / kPeaks + 1;
                float maxPeak = 1e-9f;
                for (std::size_t b = 0; b < kPeaks; ++b) {
                    const std::size_t from = b * bucketSize;
                    if (from >= total) break;
                    const std::size_t to = std::min(total, from + bucketSize);
                    float m = 0.0f;
                    for (std::size_t i = from; i < to; i += 2) {
                        const float a = std::fabs(pcm[i]);
                        if (a > m) m = a;
                    }
                    bucket[b] = m;
                    if (m > maxPeak) maxPeak = m;
                }
                for (std::size_t b = 0; b < kPeaks; ++b) peaks.push_back(bucket[b] / maxPeak);
            } else {
                for (std::size_t b = 0; b < kPeaks; ++b) peaks.push_back(0.0f);
            }
            return peaks;
        };
        e["peaks"] = peaksOf(s->pcm);
        e["rawPeaks"] = peaksOf(s->sourcePcm.empty() ? s->pcm : s->sourcePcm);
        e["sourceDur"] = (s->sourcePcm.empty() ? s->durationSec
                                               : double(s->sourcePcm.size() /
                                                        std::max(1u, s->channels)) /
                                                     std::max(1u, s->sampleRate));
        soundsArr.push_back(std::move(e));
    }
    j["sounds"] = std::move(soundsArr);

    nlohmann::json ai;
    ai["enabled"] = engine.aiEnabled();
    ai["ready"] = engine.aiService().isReady();
    ai["voice"] = engine.aiService().voiceName();
    ai["device"] = engine.aiService().deviceName();
    ai["usesGpu"] = engine.aiService().usesGpu();
    ai["provider"] = AiProviderName(engine.aiService().providerHint());
    ai["modelsInstalled"] = engine.aiLibrary().baseModelsInstalled();
    ai["latency"] = engine.aiLatencyMs();
    ai["underruns"] = engine.aiUnderruns();
    auto& c = engine.aiControls();
    ai["pitch"] = double(c.pitchShift.load());
    ai["margin"] = c.primeMarginMs.load();
    ai["fade"] = c.fadeMs.load();
    ai["autolimit"] = bool(c.autoLimit.load());
    ai["flapLimit"] = c.flapLimit.load();
    nlohmann::json voicesArr = nlohmann::json::array();
    for (const AiVoiceInfo& v : engine.aiLibrary().scanVoices()) {
        nlohmann::json e;
        e["name"] = v.name;
        e["gender"] = v.gender;
        e["pitch"] = v.defaultPitchShift;
        e["credits"] = v.credits;
        voicesArr.push_back(std::move(e));
    }
    ai["voices"] = std::move(voicesArr);
    j["ai"] = std::move(ai);

    nlohmann::json devices;
    nlohmann::json capsArr = nlohmann::json::array();
    const auto& caps = engine.captureDevices();
    for (std::size_t i = 0; i < caps.size(); ++i)
        capsArr.push_back({{"idx", i}, {"name", caps[i].name}, {"def", caps[i].isDefault}});
    nlohmann::json playsArr = nlohmann::json::array();
    const auto& plays = engine.playbackDevices();
    for (std::size_t i = 0; i < plays.size(); ++i)
        playsArr.push_back({{"idx", i}, {"name", plays[i].name}, {"def", plays[i].isDefault}});
    devices["capture"] = std::move(capsArr);
    devices["playback"] = std::move(playsArr);
    devices["curCapture"] = engine.currentCaptureIdx();
    devices["curPlayback"] = engine.currentPlaybackIdx();
    // currentMonitorIdx() is size_t(-1) when the monitor is off — serializing
    // that as a huge unsigned value makes the UI checkbox look "on".
    devices["curMonitor"] = engine.monitorRunning() ? int(engine.currentMonitorIdx()) : -1;
    devices["running"] = engine.isRunning();
    j["devices"] = std::move(devices);

    j["mic"] = engine.micLevel();
    j["voiceLatency"] = engine.voiceLatencyMs();
    j["e2eLatency"] = engine.endToEndLatencyMs();
    j["micUnderruns"] = double(engine.micUnderruns());
    j["vadSleeping"] = engine.vadSleeping();
    j["vadThresholdDb"] = double(engine.vadThresholdDb());
    j["latencyMode"] = engine.latencyMode();
    j["capturePeriod"] = engine.actualCapturePeriodFrames();
    j["playbackPeriod"] = engine.actualPlaybackPeriodFrames();
    j["captureRate"] = engine.actualCaptureSampleRate();
    j["playbackRate"] = engine.actualPlaybackSampleRate();
    j["hotkeys"] = {{"voice", hotkeys.voiceToggleCombo()}, {"ai", hotkeys.aiToggleCombo()}};
    nlohmann::json allTags = nlohmann::json::array();
    for (const std::string& t : bank.allTags()) allTags.push_back(t);
    j["allTags"] = std::move(allTags);
    j["autoStart"] = isAutoStartEnabled();
    j["runAsAdmin"] = settings.runAsAdmin;
    j["elevated"] = isElevated();
    return j;
}

} // namespace vd
