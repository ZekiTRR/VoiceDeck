// VoiceDeck entry point. Default front-end: the Voicemod-style WebView2 UI;
// pass --cli for the headless console mode.

#include "app/App.hpp"
#include "core/platform/Console.hpp"
#include "core/util/Log.hpp"
#include "ui/UiApp.hpp"

#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using vd::App;
using vd::Command;

std::atomic<bool> g_exit{false};

// ------------------------------------------------------------- CLI helpers

std::string toLower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Quote-aware tokenizer: add "C:\my sounds\a.mp3" "My Sound"
std::vector<std::string> tokenize(const std::string& line) {
    // Tolerate BOMs from piped input (UTF-8 EF BB BF bytes / U+FEFF pairs).
    std::string_view lv = line;
    while (lv.size() >= 3 && lv.substr(0, 3) == "\xEF\xBB\xBF") lv.remove_prefix(3);
    while (lv.size() >= 2 && lv[0] == '\xEF' && lv[1] == '\xBF') lv.remove_prefix(2);
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    bool has = false;
    for (char c : lv) {
        if (c == '"') {
            inQuote = !inQuote;
            has = true;
        } else if ((c == ' ' || c == '\t') && !inQuote) {
            if (has) out.push_back(cur);
            cur.clear();
            has = false;
        } else {
            cur.push_back(c);
            has = true;
        }
    }
    if (has) out.push_back(cur);
    return out;
}

std::string levelBar(float v) {
    const int width = 24;
    const float x = std::sqrt(std::max(0.0f, std::min(1.0f, v * 6.0f)));
    const int n = int(x * width);
    std::string bar(width, '-');
    for (int i = 0; i < n && i < width; ++i) bar[i] = '#';
    return bar;
}

std::uint64_t parseId(const std::string& tok) {
    return std::strtoull(tok.c_str(), nullptr, 10);
}

void printHelp() {
    vd::log::info("commands:");
    vd::log::info("  devices                      list capture/playback devices");
    vd::log::info("  select <cN> <pN>             route mic -> chosen output, e.g. select c0 p1");
    vd::log::info("  monitor on [pN] | off       hear the final mix on headphones");
    vd::log::info("  add <file> [name]            load a wav/mp3/flac clip into the library");
    vd::log::info("  sounds                       list clips | remove <id> (deletes file too)");
    vd::log::info("  refresh                      pick up new files in data\\sounds (no restart)");
    vd::log::info("  play <id> | stop <id> | stopall");
    vd::log::info("  volume <id> <0..200>         per-clip volume, percent");
    vd::log::info("  loop <id> on|off             loop the clip on next plays");
    vd::log::info("  hotkey <id|stopall|preset|voice|ai> ... <combo>");
    vd::log::info("  tags                         list all tags (adds 'hotkey' automatically)");
    vd::log::info("  tags <tag>                   list clips carrying this tag");
    vd::log::info("  tag <id> <tag...>            replace a clip's tags");
    vd::log::info("  tagadd|tagrm <id> <tag>      add/remove one tag");
    vd::log::info("  edit <id> <json>             e.g. edit 1 {'trimStart':1,'trimEnd':5,'gain':1.5,'regions':[{'start':0,'end':1,'gain':0.5}]}");
    vd::log::info("  autostart on|off             launch VoiceDeck with Windows");
    vd::log::info("  admin on|off                 always run as administrator (UAC relaunch)");
    vd::log::info("  bypass on|off                voice path on/off (clip output continues)");
    vd::log::info("  ducking on|off               auto-lower clips while you speak");
    vd::log::info("  ambient on|off               VAD-gated preset background ambiences");
    vd::log::info("  ambient <idx> <none|radio|pilot|cave|file>");
    vd::log::info("  fxsend on|off                route soundboard through current voice/RVC");
    vd::log::info("  vad <-60..-20>               voice-branch silence threshold (dBFS)");
    vd::log::info("  presets                      list voice characters");
    vd::log::info("  preset <name|index>          apply a voice character");
    vd::log::info("  voice on|off                 voice DSP chain on/off");
    vd::log::info("  pitch <semitones>|off        live pitch override (-24..+24)");
    vd::log::info("  formant <semitones>|off      live formant override (-24..+24)");
    vd::log::info("  aivoices                     list installed AI voices + model folder");
    vd::log::info("  aivoice <name>               load an AI voice (blocking, seconds)");
    vd::log::info("  ai on|off                    AI voice on/off");
    vd::log::info("  ai settings                  detailed AI tuning overview");
    vd::log::info("  ai margin|fade <ms>          latency and crossfade tuning");
    vd::log::info("  ai autolimit on|off [n]      auto-off for slow GPUs (n dropouts/30s)");
    vd::log::info("  ai provider auto|cuda|dml|cpu  inference backend choice");
    vd::log::info("  aipitch <semitones>           pitch for the AI voice (-24..+24)");
    vd::log::info("  status                       engine meters and now-playing");
    vd::log::info("  save | quit");
}

void printDevices(App& app) {
    std::string err;
    if (!app.engine.refreshDevices(err)) {
        vd::log::warn("failed to enumerate devices: %s", err.c_str());
        return;
    }
    const auto& caps = app.engine.captureDevices();
    const auto& plays = app.engine.playbackDevices();
    const std::size_t curCap = app.engine.currentCaptureIdx();
    const std::size_t curPlay = app.engine.currentPlaybackIdx();
    const std::size_t curMon = app.engine.currentMonitorIdx();

    vd::log::info("Capture devices:");
    for (std::size_t i = 0; i < caps.size(); ++i)
        vd::log::info("  c%zu: %s%s%s", i, caps[i].name.c_str(),
                      caps[i].isDefault ? "   [system default]" : "",
                      i == curCap ? "   <== in use" : "");
    vd::log::info("Playback devices:");
    for (std::size_t i = 0; i < plays.size(); ++i)
        vd::log::info("  p%zu: %s%s%s%s", i, plays[i].name.c_str(),
                      toLower(plays[i].name).find("cable") != std::string::npos
                          ? "   [virtual cable]"
                          : "",
                      i == curPlay ? "   <== output" : "",
                      i == curMon ? "   <== monitor" : "");
    vd::log::info("use: select <cN> <pN>   |   monitor on [pN]");
}

void printStatus(App& app) {
    if (app.engine.isRunning())
        vd::log::info("engine    : RUNNING  '%s' -> '%s'  (%u cap / %u play frames, %u Hz, %u ch)",
                      app.engine.currentCaptureName().c_str(),
                      app.engine.currentPlaybackName().c_str(),
                      app.engine.actualCapturePeriodFrames(),
                      app.engine.actualPlaybackPeriodFrames(), vd::AudioEngine::kSampleRate,
                      vd::AudioEngine::kChannels);
    else
        vd::log::info("engine    : stopped (run 'devices' and 'select cN pN')");
    vd::log::info("mic level : [%s] %.3f", levelBar(app.engine.micLevel()).c_str(),
                  app.engine.micLevel());
    const int pIdx = app.engine.voicePresetIndex();
    const char* pName = (pIdx >= 0 && std::size_t(pIdx) < app.presets.size())
                            ? app.presets[std::size_t(pIdx)].name.c_str()
                            : "?";
    vd::log::info("voice     : DSP %s, preset '%s', chain ~%.0f ms, end-to-end ~%.0f ms",
                  app.engine.dspOn() ? "ON" : "OFF", pName,
                  double(app.engine.voiceLatencyMs()), app.engine.endToEndLatencyMs());
    {
        auto& svc = app.engine.aiService();
        std::string aiLine = app.engine.aiEnabled() ? "ON" : "OFF";
        if (svc.isReady()) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s, '%s' on %s, ~%.0f ms, underruns %d", aiLine.c_str(),
                          svc.voiceName().c_str(), svc.deviceName().c_str(),
                          app.engine.aiLatencyMs(), app.engine.aiUnderruns());
            vd::log::info("ai voice  : %s", buf);
        } else {
            vd::log::info("ai voice  : %s (%s)", aiLine.c_str(), svc.statusMessage().c_str());
        }
    }
    vd::log::info("bypass    : %s   ducking: %s   monitor: %s",
                  app.engine.bypassEnabled() ? "ON" : "OFF",
                  app.engine.duckingOn() ? "ON" : "OFF",
                  app.engine.monitorRunning() ? "on" : "off");
    vd::log::info("ambient   : %s   soundboard FX Send: %s",
                  app.engine.ambientEnabled() ? "ON" : "OFF",
                  app.engine.soundFxSend() ? "ON" : "OFF");
    vd::log::info("VAD       : %.0f dBFS, voice branch %s", double(app.engine.vadThresholdDb()),
                  app.engine.vadSleeping() ? "SLEEPING" : "awake");
    const auto clips = app.bank.snapshot();
    vd::log::info("library   : %zu clip(s)", clips.size());
    const auto voices = app.engine.mixer().snapshot();
    if (!voices.empty()) vd::log::info("playing now:");
    for (const auto& v : voices) {
        const vd::Sound* s = app.bank.find(v.soundId);
        const double dur = double(v.totalFrames) / double(vd::AudioEngine::kSampleRate);
        const double pos = double(v.posFrames) / double(vd::AudioEngine::kSampleRate);
        vd::log::info("  #%llu  %-24s %5.1fs / %5.1fs", static_cast<unsigned long long>(v.soundId),
                      s ? s->name.c_str() : "?", pos, dur);
    }
    vd::log::info("glitches  : mic underruns %llu | capture overflow %llu | monitor underruns %llu | "
                  "rejected plays %llu",
                  static_cast<unsigned long long>(app.engine.micUnderruns()),
                  static_cast<unsigned long long>(app.engine.captureOverflows()),
                  static_cast<unsigned long long>(app.engine.monitorUnderruns()),
                  static_cast<unsigned long long>(app.engine.mixer().rejectedPlays()));
    vd::log::info("playback  : max callback %u frames, last underrun %u at fill %.0f",
                  app.engine.playbackMaxFrames(), app.engine.lastUnderrunFrames(),
                  app.engine.lastUnderrunFill());
    vd::log::info("pipeline  : mic fill min/max %.0f/%.0f, target %.0f, drift step %.4f",
                  app.engine.micFillMin(), app.engine.micFillMax(),
                  app.engine.micTargetFrames(), app.engine.micDriftStep());
    vd::log::info("callbacks : capture %llu / playback %llu (ratio %.3f)",
                  static_cast<unsigned long long>(app.engine.capTicks()),
                  static_cast<unsigned long long>(app.engine.playTicks()),
                  app.engine.capTicks() ? double(app.engine.playTicks()) /
                                              double(app.engine.capTicks())
                                        : 0.0);
}

void printPresets(App& app) {
    const int active = app.engine.voicePresetIndex();
    vd::log::info("voice characters (current: %s):",
                  (active >= 0 && std::size_t(active) < app.presets.size())
                      ? app.presets[std::size_t(active)].name.c_str()
                      : "-");
    for (std::size_t i = 0; i < app.presets.size(); ++i) {
        const vd::VoicePreset& p = app.presets[i];
        char params[128];
        std::snprintf(params, sizeof(params), "pitch %+.0f  formant %+.0f  %s%s", double(p.transpose),
                      double(p.formant), p.space != "none" ? (p.space + " ").c_str() : "",
                      p.effect != "none" ? p.effect.c_str() : "");
        vd::log::info("  %2zu  %-12s %s%s", i, p.name.c_str(), params,
                      p.hotkey.empty() ? "" : ("   [" + p.hotkey + "]").c_str());
    }
    vd::log::info("use: preset <name|index>   |   hotkey preset <n> <combo>");
}

// ------------------------------------------------------------- CLI commands

bool handleCommand(App& app, const std::string& raw, bool& quit) {
    const std::vector<std::string> t = tokenize(raw);
    if (t.empty()) return true;
    const std::string c = toLower(t[0]);

    if (c == "quit" || c == "exit" || c == "q") {
        quit = true;
        return false;
    }
    if (c == "help" || c == "?") {
        printHelp();
        return true;
    }
    if (c == "devices") {
        printDevices(app);
        return true;
    }
    if (c == "status") {
        app.engine.drainCommands();
        printStatus(app);
        return true;
    }
    if (c == "presets" || c == "characters") {
        app.engine.drainCommands();
        printPresets(app);
        return true;
    }

    if (c == "refresh") {
        const auto [added, removed] = app.refreshSounds();
        vd::log::info("refresh: %zu added, %zu removed", added, removed);
        return true;
    }

    if (c == "add") {
        if (t.size() < 2) {
            vd::log::warn("usage: add <file.wav|mp3|flac> [name]");
            return true;
        }
        const fs::path p(t[1]);
        if (!fs::exists(p)) {
            vd::log::error("file not found: %s", t[1].c_str());
            return true;
        }
        std::string err;
        const std::uint64_t id = app.addSound(p, t.size() > 2 ? t[2] : std::string(), err);
        if (id == 0) {
            vd::log::error("add failed: %s", err.c_str());
            return true;
        }
        const vd::Sound* s = app.bank.find(id);
        vd::log::info("sound #%llu '%s' (%.1f s)  -  use: play %llu",
                      static_cast<unsigned long long>(id), s ? s->name.c_str() : "?",
                      s ? s->durationSec : 0.0, static_cast<unsigned long long>(id));
        app.saveAll();
        return true;
    }

    if (c == "sounds" || c == "clips") {
        const auto clips = app.bank.snapshot();
        const auto combos = app.hotkeys.allSoundCombos();
        if (clips.empty()) {
            vd::log::info("sound library is empty - use: add <file>");
            return true;
        }
        vd::log::info("sound library (%zu):", clips.size());
        for (const vd::Sound* s : clips) {
            const auto hk = combos.find(s->id);
            vd::log::info("  #%llu  %-24s %5.1fs  vol %3d%%  %s%s",
                          static_cast<unsigned long long>(s->id), s->name.c_str(), s->durationSec,
                          int(std::round(double(s->gain.load()) * 100.0)),
                          s->loop.load() ? "[loop] " : "",
                          hk != combos.end() ? ("[" + hk->second + "]").c_str() : "");
        }
        return true;
    }

    if (c == "remove" || c == "rm") {
        if (t.size() < 2) {
            vd::log::warn("usage: remove <soundId>");
            return true;
        }
        const std::string err = app.removeSound(parseId(t[1]));
        if (!err.empty()) vd::log::error("remove failed: %s", err.c_str());
        else
            vd::log::info("removed sound #%llu (file deleted from data\\sounds)",
                          static_cast<unsigned long long>(parseId(t[1])));
        return true;
    }

    if (c == "play") {
        if (t.size() < 2) {
            vd::log::warn("usage: play <soundId>");
            return true;
        }
        if (!app.bank.find(parseId(t[1]))) {
            vd::log::error("no sound with id %s", t[1].c_str());
            return true;
        }
        app.playSound(parseId(t[1]));
        return true;
    }

    if (c == "stop") {
        if (t.size() < 2) {
            vd::log::warn("usage: stop <soundId>   or: stopall");
            return true;
        }
        if (toLower(t[1]) == "all") app.stopAllSounds();
        else app.stopSound(parseId(t[1]));
        return true;
    }
    if (c == "stopall") {
        app.stopAllSounds();
        return true;
    }

    if (c == "volume") {
        if (t.size() < 3) {
            vd::log::warn("usage: volume <soundId> <0..200>");
            return true;
        }
        app.setSoundVolume(parseId(t[1]), std::atoi(t[2].c_str()));
        vd::log::info("sound #%s volume: %s%%", t[1].c_str(), t[2].c_str());
        return true;
    }

    if (c == "loop") {
        if (t.size() < 3) {
            vd::log::warn("usage: loop <soundId> on|off");
            return true;
        }
        const bool on = toLower(t[2]) == "on" || t[2] == "1" || toLower(t[2]) == "true";
        app.setSoundLoop(parseId(t[1]), on);
        vd::log::info("sound #%s loop: %s", t[1].c_str(), on ? "ON" : "OFF");
        return true;
    }

    if (c == "hotkey") {
        if (t.size() < 3) {
            vd::log::warn("usage: hotkey <soundId|stopall|preset|voice|ai> ... <combo>");
            return true;
        }
        std::string err;
        if (toLower(t[1]) == "voice") {
            err = app.hotkeys.bindVoiceToggle(t[2]);
            if (err.empty()) {
                app.settings.voiceHotkey = t[2];
                app.settings.save(app.settingsFile);
                vd::log::info("hotkey %s -> voice DSP on/off", t[2].c_str());
            }
        } else if (toLower(t[1]) == "ai") {
            err = app.hotkeys.bindAiToggle(t[2]);
            if (err.empty()) {
                app.settings.aiHotkey = t[2];
                app.settings.save(app.settingsFile);
                vd::log::info("hotkey %s -> AI voice on/off", t[2].c_str());
            }
        } else if (toLower(t[1]) == "preset") {
            if (t.size() < 4) {
                vd::log::warn("usage: hotkey preset <n> <combo>");
                return true;
            }
            const std::uint64_t idx = parseId(t[2]);
            if (idx >= app.presets.size()) {
                vd::log::error("no character #%llu", static_cast<unsigned long long>(idx));
                return true;
            }
            err = app.hotkeys.bindPreset(idx, t[3]);
            if (err.empty()) {
                app.presets[std::size_t(idx)].hotkey = t[3];
                vd::savePresets(app.charactersFile, app.presets);
                vd::log::info("hotkey %s -> character '%s'", t[3].c_str(),
                              app.presets[std::size_t(idx)].name.c_str());
            }
        } else {
            const bool stopAll = toLower(t[1]) == "stopall";
            const std::uint64_t id = stopAll ? 0 : parseId(t[1]);
            if (!stopAll && !app.bank.find(id)) {
                vd::log::error("no sound with id %s", t[1].c_str());
                return true;
            }
            err = stopAll ? app.hotkeys.bindStopAll(t[2]) : app.bindSoundHotkey(id, t[2]);
            if (err.empty())
                vd::log::info("hotkey %s -> %s", t[2].c_str(), stopAll ? "stop all" : t[1].c_str());
        }
        if (!err.empty()) vd::log::error("hotkey: %s", err.c_str());
        return true;
    }

    if (c == "tags") {
        const auto all = app.bank.allTags();
        const bool filter = t.size() >= 2;
        if (!filter) {
            vd::log::info("tags (%zu):", all.size());
            for (const auto& tag : all) vd::log::info("  %s", tag.c_str());
        }
        const std::string want = toLower(filter ? t[1] : std::string());
        int shown = 0;
        for (const vd::Sound* s : app.bank.snapshot()) {
            bool match = !filter;
            for (const auto& tag : s->tags)
                if (toLower(tag) == want) match = true;
            if (!match) continue;
            std::string list;
            for (const auto& tag : s->tags) list += (list.empty() ? "" : ", ") + tag;
            vd::log::info("  #%llu  %-24s [%s]", static_cast<unsigned long long>(s->id),
                          s->name.c_str(), list.c_str());
            ++shown;
        }
        if (filter && shown == 0) vd::log::info("no clips with tag '%s'", t[1].c_str());
        return true;
    }

    if (c == "tag" || c == "tagadd" || c == "tagrm") {
        if (t.size() < 3) {
            vd::log::warn("usage: tag <id> <tag...> | tagadd|tagrm <id> <tag>");
            return true;
        }
        const std::uint64_t id = parseId(t[1]);
        std::string err;
        if (c == "tag") {
            std::vector<std::string> tags(t.begin() + 2, t.end());
            err = app.setSoundTags(id, nlohmann::json(tags));
        } else if (c == "tagadd") {
            err = app.addSoundTag(id, t[2]);
        } else {
            err = app.removeSoundTag(id, t[2]);
        }
        if (err.empty()) app.saveAll();
        else vd::log::error("tags: %s", err.c_str());
        return true;
    }

    if (c == "edit") {
        if (t.size() < 3) {
            vd::log::warn("usage: edit <id> <json>   (use ' instead of \", e.g. edit 1 {'gain':1.5})");
            return true;
        }
        // The shell tokenizer eats double quotes, so the CLI dialect uses
        // single quotes for JSON strings. Rejoin all remaining tokens to
        // allow spaces inside the object.
        std::string raw;
        for (std::size_t i = 2; i < t.size(); ++i) {
            if (i > 2) raw += ' ';
            raw += t[i];
        }
        for (char& ch : raw)
            if (ch == '\'') ch = '"';
        auto def = nlohmann::json::parse(raw, nullptr, false);
        if (def.is_discarded() || !def.is_object()) {
            vd::log::error("edit: invalid JSON (use single quotes for keys)");
            return true;
        }
        const std::string err = app.editSound(parseId(t[1]), def);
        if (err.empty()) vd::log::info("sound #%s edited (non-destructively)", t[1].c_str());
        else vd::log::error("edit: %s", err.c_str());
        return true;
    }

    if (c == "autostart" || c == "admin") {
        const bool on = t.size() >= 2 ? (toLower(t[1]) == "on")
                                      : !(c == "autostart" ? app.autoStartEnabled()
                                                           : app.settings.runAsAdmin);
        std::string err;
        if (c == "autostart") err = app.setAutoStart(on);
        else {
            bool relaunching = false;
            err = app.setRunAsAdmin(on, relaunching);
            if (relaunching) {
                vd::log::info("restarting elevated...");
                app.hotkeys.stop();
                app.engine.shutdown();
                std::exit(0);
            }
        }
        if (err.empty()) vd::log::info("%s: %s", c.c_str(), on ? "ON" : "OFF");
        else vd::log::error("%s: %s", c.c_str(), err.c_str());
        return true;
    }

    if (c == "bypass") {
        const bool on = (t.size() >= 2) ? (toLower(t[1]) == "on") : !app.bypassOn;
        app.setBypass(on);
        vd::log::info("bypass: %s", app.engine.bypassEnabled() ? "ON" : "OFF");
        return true;
    }
    if (c == "ducking") {
        const bool on = (t.size() >= 2) ? (toLower(t[1]) == "on") : !app.duckingOn;
        app.setDucking(on);
        vd::log::info("ducking: %s", app.engine.duckingOn() ? "ON" : "OFF");
        return true;
    }
    if (c == "ambient") {
        if (t.size() == 2 && (toLower(t[1]) == "on" || toLower(t[1]) == "off")) {
            const bool on = toLower(t[1]) == "on";
            const std::string err = app.setAmbientEnabled(on);
            if (!err.empty()) vd::log::error("ambient: %s", err.c_str());
            else vd::log::info("ambient master: %s", on ? "ON" : "OFF");
            return true;
        }
        if (t.size() < 3) {
            vd::log::warn("usage: ambient on|off | ambient <idx> <none|radio|pilot|cave|file>");
            return true;
        }
        const int idx = int(parseId(t[1]));
        if (idx < 0 || std::size_t(idx) >= app.presets.size()) {
            vd::log::error("ambient: bad character index");
            return true;
        }
        const std::string kind = toLower(t[2]);
        std::string err;
        if (kind == "none" || kind == "radio" || kind == "pilot" || kind == "cave")
            err = app.setPresetAmbient(idx, {{"ambient", kind}});
        else
            err = app.importAmbient(idx, fs::path(t[2]));
        if (!err.empty()) vd::log::error("ambient: %s", err.c_str());
        else vd::log::info("ambient for '%s': %s", app.presets[std::size_t(idx)].name.c_str(),
                           app.presets[std::size_t(idx)].ambient.c_str());
        return true;
    }
    if (c == "fxsend") {
        const bool on = (t.size() >= 2) ? (toLower(t[1]) == "on") : !app.settings.soundFxSend;
        const std::string err = app.setSoundFxSend(on);
        if (!err.empty()) vd::log::error("fxsend: %s", err.c_str());
        else vd::log::info("soundboard FX Send: %s", on ? "ON" : "OFF");
        return true;
    }
    if (c == "vad") {
        if (t.size() < 2) {
            vd::log::warn("usage: vad <-60..-20>   (dBFS, default -48)");
            return true;
        }
        const std::string err = app.setVadThresholdDb(float(std::atof(t[1].c_str())));
        if (!err.empty()) vd::log::error("vad: %s", err.c_str());
        else vd::log::info("VAD threshold: %.0f dBFS", double(app.engine.vadThresholdDb()));
        return true;
    }

    if (c == "preset") {
        if (t.size() < 2) {
            vd::log::warn("usage: preset <name|index>   (see 'presets')");
            return true;
        }
        int idx = -1;
        if (std::isdigit(static_cast<unsigned char>(t[1][0]))) {
            idx = int(parseId(t[1]));
        } else {
            for (std::size_t i = 0; i < app.presets.size(); ++i)
                if (toLower(app.presets[i].name) == toLower(t[1])) {
                    idx = int(i);
                    break;
                }
        }
        if (!app.applyPreset(idx)) {
            vd::log::error("no character '%s' - see 'presets'", t[1].c_str());
            return true;
        }
        vd::log::info("voice: '%s'", app.presets[std::size_t(idx)].name.c_str());
        return true;
    }

    if (c == "voice") {
        const bool on = (t.size() >= 2) ? (toLower(t[1]) == "on") : !app.engine.dspOn();
        app.setDsp(on);
        vd::log::info("voice DSP: %s", on ? "ON" : "OFF");
        return true;
    }

    if (c == "pitch" || c == "formant") {
        const bool isPitch = (c == "pitch");
        if (t.size() < 2 || toLower(t[1]) == "off") {
            if (isPitch) app.setPitchOverride(0, false);
            else app.setFormantOverride(0, false);
            vd::log::info("%s override: off", isPitch ? "pitch" : "formant");
            return true;
        }
        const float semi = float(std::atof(t[1].c_str()));
        if (semi < -24.f || semi > 24.f) {
            vd::log::warn("semitones must be within -24..+24");
            return true;
        }
        if (isPitch) app.setPitchOverride(semi, true);
        else app.setFormantOverride(semi, true);
        vd::log::info("%s override: %+.1f semitones", isPitch ? "pitch" : "formant", double(semi));
        return true;
    }

    if (c == "aivoices") {
        auto& lib = app.engine.aiLibrary();
        if (!lib.baseModelsInstalled()) {
            vd::log::warn("AI Voice models are not installed");
            vd::log::info("download: https://github.com/bhoirpushpak/voice-ctx/releases/latest/"
                          "download/VoiceCtx-AiVoice-Models-v1.zip");
            vd::log::info("unzip into: %s", lib.root().string().c_str());
            return true;
        }
        const auto voices = lib.scanVoices();
        const std::string cur = app.engine.aiService().voiceName();
        if (voices.empty()) {
            vd::log::warn("no voices found in %s", lib.voicesFolder().string().c_str());
            return true;
        }
        vd::log::info("AI voices (models: %s):", lib.root().string().c_str());
        for (const auto& v : voices)
            vd::log::info("  %-24s %-7s default pitch %+d  %s", v.name.c_str(), v.gender.c_str(),
                          v.defaultPitchShift,
                          toLower(v.name) == toLower(cur) ? "   <== loaded" : "");
        vd::log::info("use: aivoice <name> | import: aiimport <file.onnx> (+ .json рядом)");
        return true;
    }

    if (c == "aiimport") {
        if (t.size() < 2) {
            vd::log::warn("usage: aiimport <file.onnx>   (.json с конфигом - рядом)");
            return true;
        }
        const fs::path p(t[1]);
        if (!fs::exists(p)) {
            vd::log::error("file not found: %s", t[1].c_str());
            return true;
        }
        const std::string err = app.importAiVoice(p);
        if (!err.empty()) vd::log::error("import failed: %s", err.c_str());
        else vd::log::info("imported - see 'aivoices'");
        return true;
    }

    if (c == "mkpreset") {
        // mkpreset <name> [pitch] [formant] [space] [effect]
        if (t.size() < 2) {
            vd::log::warn("usage: mkpreset <name> [pitch] [formant] [space] [effect]");
            return true;
        }
        nlohmann::json def;
        def["name"] = t[1];
        if (t.size() > 2) def["transpose"] = std::atof(t[2].c_str());
        if (t.size() > 3) def["formant"] = std::atof(t[3].c_str());
        if (t.size() > 4) def["space"] = t[4];
        if (t.size() > 5) def["effect"] = t[5];
        std::string err;
        const int idx = app.addPreset(def, err);
        if (idx < 0) vd::log::error("mkpreset: %s", err.c_str());
        else vd::log::info("created '%s' (index %d) - use: preset %d", t[1].c_str(), idx, idx);
        return true;
    }

    if (c == "rmpreset") {
        if (t.size() < 2) {
            vd::log::warn("usage: rmpreset <index>   (встроенные защищены)");
            return true;
        }
        const std::string err = app.removePreset(int(parseId(t[1])));
        if (!err.empty()) vd::log::error("rmpreset: %s", err.c_str());
        else vd::log::info("removed");
        return true;
    }

    if (c == "aivoice") {
        if (t.size() < 2) {
            vd::log::warn("usage: aivoice <name>   (see 'aivoices')");
            return true;
        }
        std::string wanted = t[1];
        for (std::size_t i = 2; i < t.size(); ++i) wanted += " " + t[i];
        const std::string err = app.loadAiVoice(wanted);
        if (!err.empty()) {
            vd::log::error("aivoice failed: %s", err.c_str());
            return true;
        }
        vd::log::info("AI voice ready: '%s' on %s", app.engine.aiService().voiceName().c_str(),
                      app.engine.aiService().deviceName().c_str());
        vd::log::info("turn it on with: ai on");
        return true;
    }

    if (c == "ai") {
        if (t.size() >= 2 && toLower(t[1]) == "settings") {
            auto& c2 = app.engine.aiControls();
            vd::log::info("AI settings (live, persisted in settings.json):");
            vd::log::info("  enabled   : %s", app.engine.aiEnabled() ? "ON" : "OFF");
            vd::log::info("  provider  : %s%s", vd::AiProviderName(app.engine.aiService().providerHint()),
                          app.engine.aiService().isReady()
                              ? ("  [active: " + app.engine.aiService().deviceName() + "]").c_str()
                              : "");
            vd::log::info("  pitch     : %+.1f", double(c2.pitchShift.load()));
            vd::log::info("  margin    : %d ms", c2.primeMarginMs.load());
            vd::log::info("  fade      : %d ms", c2.fadeMs.load());
            vd::log::info("  autolimit : %s  (after %d dropouts/30s)",
                          c2.autoLimit.load() ? "ON" : "OFF", c2.flapLimit.load());
            vd::log::info("latency ~%.0f ms, underruns %d", app.engine.aiLatencyMs(),
                          app.engine.aiUnderruns());
            return true;
        }
        if (t.size() >= 3 && toLower(t[1]) == "margin") {
            app.setAiMargin(std::atoi(t[2].c_str()));
            vd::log::info("AI margin: %d ms", app.engine.aiControls().primeMarginMs.load());
            return true;
        }
        if (t.size() >= 3 && toLower(t[1]) == "fade") {
            app.setAiFade(std::atoi(t[2].c_str()));
            vd::log::info("AI fade: %d ms", app.engine.aiControls().fadeMs.load());
            return true;
        }
        if (t.size() >= 3 && toLower(t[1]) == "autolimit") {
            const bool on = toLower(t[2]) == "on";
            int n = app.engine.aiControls().flapLimit.load();
            if (on && t.size() >= 4) n = std::atoi(t[3].c_str());
            app.setAiAutolimit(on, n);
            if (on)
                vd::log::info("AI autolimit: ON (flap limit %d dropouts/30s)",
                              app.engine.aiControls().flapLimit.load());
            else vd::log::info("AI autolimit: OFF");
            return true;
        }
        if (t.size() >= 3 && toLower(t[1]) == "provider") {
            const std::string err = app.setAiProvider(t[2]);
            if (!err.empty()) vd::log::error("provider: %s", err.c_str());
            else vd::log::info("AI provider: %s", t[2].c_str());
            return true;
        }
        const bool on = (t.size() >= 2) ? (toLower(t[1]) == "on") : !app.engine.aiEnabled();
        if (on && !app.engine.aiService().isReady()) {
            vd::log::warn("no AI voice loaded - run 'aivoices' and 'aivoice <name>' first");
            return true;
        }
        app.setAi(on);
        app.engine.drainCommands();
        vd::log::info("AI voice: %s (engine=%d)", on ? "ON" : "OFF",
                      app.engine.aiEnabled() ? 1 : 0);
        return true;
    }

    if (c == "aipitch") {
        if (t.size() < 2) {
            vd::log::warn("usage: aipitch <semitones>   e.g. aipitch +12");
            return true;
        }
        app.setAiPitch(float(std::atof(t[1].c_str())));
        vd::log::info("AI voice pitch: %s semitones", t[1].c_str());
        return true;
    }

    if (c == "select") {
        if (t.size() < 3) {
            vd::log::warn("usage: select <cN> <pN>   (see 'devices')");
            return true;
        }
        auto parseIdx = [](const std::string& tok, char kind) -> std::size_t {
            if (tok.size() < 2 || char(std::tolower(static_cast<unsigned char>(tok[0]))) != kind)
                return SIZE_MAX;
            std::size_t v = 0;
            for (char ch : tok.substr(1)) {
                if (!std::isdigit(static_cast<unsigned char>(ch))) return SIZE_MAX;
                v = v * 10 + std::size_t(ch - '0');
            }
            return v;
        };
        const std::size_t capIdx = parseIdx(t[1], 'c');
        const std::size_t playIdx = parseIdx(t[2], 'p');
        if (capIdx == SIZE_MAX || playIdx == SIZE_MAX) {
            vd::log::warn("usage: select <cN> <pN>");
            return true;
        }
        app.selectDevices(capIdx, playIdx);
        vd::log::info("audio restarted: '%s' -> '%s'",
                      app.settings.captureDevice.c_str(), app.settings.playbackDevice.c_str());
        return true;
    }

    if (c == "monitor") {
        if (t.size() < 2 || (toLower(t[1]) != "on" && toLower(t[1]) != "off")) {
            vd::log::warn("usage: monitor on [pN] | monitor off");
            return true;
        }
        const bool on = toLower(t[1]) == "on";
        std::size_t idx = SIZE_MAX;
        if (on && t.size() >= 3) {
            if (t[2].size() >= 2 && char(std::tolower(static_cast<unsigned char>(t[2][0]))) == 'p')
                idx = std::strtoul(t[2].c_str() + 1, nullptr, 10);
        }
        if (app.setMonitor(on, idx)) {
            if (on) vd::log::info("monitor: '%s'", app.settings.monitorDevice.c_str());
        } else if (on) vd::log::warn("monitor failed (device or engine stopped?)");
        if (!on) vd::log::info("monitor: off");
        return true;
    }

    if (c == "save") {
        app.saveAll();
        vd::log::info("settings + sound library + characters saved");
        return true;
    }

    vd::log::warn("unknown command '%s' - try 'help'", c.c_str());
    return true;
}

// ------------------------------------------------------------- CLI loop

int runCli(App& app) {
    app.onExitRequested = [] { g_exit.store(true); };
    printHelp();

    std::deque<std::string> lines;
    std::mutex lineMtx;
    std::condition_variable lineCv;
    bool inputEof = false;
    std::thread reader([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            {
                std::lock_guard<std::mutex> lk(lineMtx);
                lines.push_back(line);
            }
            lineCv.notify_one();
        }
        {
            std::lock_guard<std::mutex> lk(lineMtx);
            inputEof = true;
        }
        lineCv.notify_one();
    });
    reader.detach();

    bool quit = false;
    while (!quit && !g_exit.load()) {
        std::unique_lock<std::mutex> lk(lineMtx);
        lineCv.wait_for(lk, std::chrono::milliseconds(200),
                        [&] { return !lines.empty() || inputEof || g_exit.load(); });
        std::deque<std::string> local;
        local.swap(lines);
        const bool eof = inputEof;
        lk.unlock();
        for (const std::string& l : local)
            if (!handleCommand(app, l, quit)) break;
        if (eof && !quit) {
            vd::log::info("stdin closed");
            break;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool cliMode = (argc > 1 && std::strcmp(argv[1], "--cli") == 0);

    // Read this one preference before any logging. The executable uses the
    // Windows GUI subsystem, so no console is created at process startup; CLI
    // mode (or an explicit opt-out) attaches/allocates one on demand.
    vd::Settings bootSettings;
    bootSettings.load(vd::Settings::appDataDir() / "settings.json");
    const bool hideConsole = bootSettings.hideConsole;
    if (cliMode || !hideConsole) vd::console::ensureConsole();
    else vd::console::hide();

    vd::log::info("VoiceDeck 2.0.0 - Soundpad + Voicemod (DSP + AI), C++20");

    // The engine owns several independent, fixed-size DSP chains. Keep the
    // facade off the default 1 MB thread stack (CLI and UI both enter here).
    auto appOwner = std::make_unique<App>();
    App& app = *appOwner;
    app.cliMode = cliMode;
    // When "always run as administrator" is enabled, hand off to an elevated
    // copy before initializing the audio engine (avoids a double init).
    if (app.maybeElevate(cliMode ? "--cli" : "")) return 0;
    if (!cliMode) {
        // The UI thread must be STA. Do this BEFORE the engine init:
        // miniaudio's WASAPI backend tries COINIT_MULTITHREADED here and
        // gracefully tolerates RPC_E_CHANGED_MODE, so first COM init wins.
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }
    app.init();
    app.startHotkeyService();

    int code = 0;
    if (cliMode) {
        code = runCli(app);
    } else {
        code = vd::runUi(app);
    }

    vd::log::info("shutting down...");
    app.saveAll();
    app.hotkeys.stop();
    app.engine.shutdown();
    return code;
}
