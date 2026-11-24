#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

namespace vd {

// Per-module VoiceLab parameters. `mix` is the module's own strength/depth;
// value/value2/value3 are module-specific (semitones, Hz, ms, feedback…).
// The UI prevents duplicate VoiceLab modules, so the module name is a stable key.
struct VoiceLabParams {
    float mix = 1.0f;
    float value = 0.0f;
    float value2 = 0.0f;
    float value3 = 0.0f;
};

// A voice "character" - the full parameter set of the DSP chain.
//
//   mic -> [enhancement: HPF, gate, compressor, timbre shelves, 8-band EQ]
//       -> [character: pitch/formant via Signalsmith Stretch, +LFO]
//       -> [ordered legacy or VoiceLab modules]
//       -> limiter -> dry/wet (strength) -> out
// A VoiceLab chain owns the order of Pitch/Formant/EQ and all added effects;
// legacy chains keep the original space/effect behavior for compatibility.
struct VoicePreset {
    std::string name;
    float transpose = 0.0f;   // semitones
    float formant = 0.0f;     // semitones (independent of pitch)
    float timbre = 0.0f;      // -1 dark .. +1 bright (shelving)
    float strength = 1.0f;    // dry/wet
    float lfoRate = 0.0f;     // Hz, pitch LFO (mutation/alien)
    float lfoDepth = 0.0f;    // semitones
    bool gate = true;         // noise gate
    bool comp = true;         // compressor
    // 8-band voice EQ (dB). Fixed center frequencies:
    // 200, 400, 800, 1600, 3200, 6400, 10000, 12000 Hz.
    float eq[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::string space = "none";
    std::string effect = "none";
    // Ordered DSP modules. Legacy names ("radio","robot","cave") remain
    // valid; VoiceLab names ("pitch","formant","vocoder",...) take full
    // control of module order, including the character EQ and stretch stages.
    std::vector<std::string> chain;
    // When true, the chain is authoritative even when empty. This lets the
    // visual constructor remove every module without reviving legacy
    // space/effect/pitch/EQ behavior.
    bool chainExplicit = false;
    std::map<std::string, VoiceLabParams> moduleParams;
    bool custom = false;       // user-made (deletable in the UI)
    std::string hotkey;       // persistence only (bound by HotkeyManager)

    // VAD-gated background ambience. Built-ins are synthesised in real time;
    // custom clips use a portable path relative to data\ ("ambiences\foo.wav").
    // The global master switch lives in Settings; "none" disables this preset.
    std::string ambient = "none";
    float ambientGain = 0.18f;
    bool ambientExplicit = false; // migration flag: old files keep factory beds
};

inline bool isVoiceLabModule(std::string_view name) {
    return name == "pitch" || name == "formant" || name == "vocoder" ||
           name == "bitcrusher" || name == "ringmod" || name == "distortion" ||
           name == "chorus" || name == "reverb" || name == "delay" || name == "eq";
}

inline VoiceLabParams defaultVoiceLabParams(std::string_view name) {
    VoiceLabParams p;
    if (name == "formant") p.value2 = 150.0f;
    else if (name == "vocoder") { p.value = 100.0f; p.value2 = 5.0f; p.value3 = 80.0f; }
    else if (name == "bitcrusher") { p.value = 6.0f; p.value2 = 0.55f; }
    else if (name == "ringmod") { p.value = 110.0f; p.value2 = 0.85f; }
    else if (name == "distortion") p.value = 0.55f;
    else if (name == "chorus") { p.value = 0.33f; p.value2 = 3.5f; p.value3 = 18.0f; }
    else if (name == "reverb") { p.value = 1.8f; p.value2 = 0.55f; p.value3 = 0.5f; }
    else if (name == "delay") { p.value = 280.0f; p.value2 = 0.34f; }
    return p;
}

inline VoiceLabParams readVoiceLabParams(const nlohmann::json& j, std::string_view name) {
    VoiceLabParams p = defaultVoiceLabParams(name);
    if (!j.is_object()) return p;
    auto finite = [&](const char* key, float& out) {
        if (!j.contains(key) || !j[key].is_number()) return;
        const double v = j[key].get<double>();
        if (std::isfinite(v)) out = float(v);
    };
    finite("mix", p.mix);
    finite("value", p.value);
    finite("value2", p.value2);
    finite("value3", p.value3);
    p.mix = std::clamp(p.mix, 0.0f, 1.0f);
    if (name == "pitch") {
        p.value = std::clamp(p.value, -24.0f, 24.0f);
        p.value2 = std::clamp(p.value2, 0.0f, 12.0f);
        p.value3 = std::clamp(p.value3, 0.0f, 10.0f);
    } else if (name == "formant") {
        p.value = std::clamp(p.value, -24.0f, 24.0f);
        p.value2 = std::clamp(p.value2, 70.0f, 250.0f);
    } else if (name == "vocoder") {
        p.value = std::clamp(p.value, 30.0f, 500.0f);
        p.value2 = std::clamp(p.value2, 1.0f, 50.0f);
        p.value3 = std::clamp(p.value3, 10.0f, 500.0f);
    } else if (name == "bitcrusher") {
        p.value = std::clamp(p.value, 2.0f, 12.0f);
        p.value2 = std::clamp(p.value2, 0.1f, 1.0f);
    } else if (name == "ringmod") {
        p.value = std::clamp(p.value, 20.0f, 1200.0f);
        p.value2 = std::clamp(p.value2, 0.0f, 1.0f);
    } else if (name == "distortion") {
        p.value = std::clamp(p.value, 0.0f, 1.0f);
    } else if (name == "chorus") {
        p.value = std::clamp(p.value, 0.05f, 5.0f);
        p.value2 = std::clamp(p.value2, 0.0f, 20.0f);
        p.value3 = std::clamp(p.value3, 8.0f, 50.0f);
    } else if (name == "reverb") {
        p.value = std::clamp(p.value, 0.1f, 6.0f);
        p.value2 = std::clamp(p.value2, 0.1f, 1.0f);
        p.value3 = std::clamp(p.value3, 0.0f, 1.0f);
    } else if (name == "delay") {
        p.value = std::clamp(p.value, 20.0f, 800.0f);
        p.value2 = std::clamp(p.value2, 0.0f, 0.9f);
    }
    return p;
}

inline nlohmann::json writeVoiceLabParams(const VoiceLabParams& p) {
    return {{"mix", double(p.mix)}, {"value", double(p.value)},
            {"value2", double(p.value2)}, {"value3", double(p.value3)}};
}

inline bool isChainModule(std::string_view name) {
    if (isVoiceLabModule(name)) return true;
    return name == "none" || name == "room" || name == "hall" || name == "cave" ||
           name == "helmet" || name == "radio" || name == "megaphone" ||
           name == "robot" || name == "alien" || name == "atari" ||
           name == "clone" || name == "silence";
}

inline bool hasVoiceLabChain(const std::vector<std::string>& chain) {
    for (const auto& item : chain)
        if (isVoiceLabModule(item)) return true;
    return false;
}

inline bool usesExplicitChain(const VoicePreset& preset) {
    return preset.chainExplicit || hasVoiceLabChain(preset.chain);
}

inline int ambientKindCode(std::string_view ambient) {
    if (ambient == "radio") return 1;
    if (ambient == "pilot") return 2;
    if (ambient == "cave") return 3;
    if (ambient.rfind("ambiences/", 0) == 0) return 4;
    return 0;
}

// Built-in characters - inspired by Voicemod's catalog + creative extras.
inline std::vector<VoicePreset> defaultPresets() {
    std::vector<VoicePreset> v;
    auto add = [&](const char* name, float tr, float fo, float ti, float lr, float ld,
                   const char* space, const char* fx) {
        VoicePreset p;
        p.name = name;
        p.transpose = tr;
        p.formant = fo;
        p.timbre = ti;
        p.lfoRate = lr;
        p.lfoDepth = ld;
        p.space = space;
        p.effect = fx;
        v.push_back(std::move(p));
    };
    auto addEq = [&](const char* name, float tr, float fo, float ti, float lr, float ld,
                     const char* space, const char* fx, std::initializer_list<float> eq) {
        VoicePreset p;
        p.name = name;
        p.transpose = tr;
        p.formant = fo;
        p.timbre = ti;
        p.lfoRate = lr;
        p.lfoDepth = ld;
        p.space = space;
        p.effect = fx;
        int i = 0;
        for (float g : eq) {
            if (i < 8) p.eq[i++] = g;
        }
        v.push_back(std::move(p));
    };

    // --- Neutral ---
    add("None", 0, 0, 0, 0, 0, "none", "none");

    // --- Classic transformations ---
    add("Male", -3, -1, -0.3f, 0, 0, "none", "none");
    add("Female", 4, 3, 0.3f, 0, 0, "none", "none");
    add("Baby", 8, 5, 0.2f, 0, 0, "none", "none");
    add("Helium", 12, 2, 0, 0, 0, "none", "none");
    add("Chipmunk", 16, 8, 0.4f, 0, 0, "none", "none");
    add("Kong", -12, -4, -0.5f, 0, 0, "cave", "none");
    add("Deep", -8, -3, -0.4f, 0, 0, "none", "none");

    // --- Voicemod-style situational ---
    add("Robot", 0, 0, 0, 0, 0, "none", "robot");
    add("Alien", 8, 0, 0, 5.5f, 0.7f, "none", "alien");
    add("Atari", 0, 0, 0, 0, 0, "none", "atari");
    add("Clone", 0, 0, 0, 0, 0, "none", "clone");
    add("Radio", 0, 0, -0.2f, 0, 0, "radio", "none");
    add("Megaphone", 0, 0, 0.3f, 0, 0, "megaphone", "none");
    add("Phone", 0, 0, 0, 0, 0, "radio", "none");
    add("Walkie", -2, 0, -0.3f, 0, 0, "radio", "none");
    add("Underwater", -5, -2, -0.6f, 0.5f, 0.3f, "hall", "none");
    add("Helmet", 0, 0, -0.5f, 0, 0, "helmet", "none");
    add("Cave", 0, 0, -0.2f, 0, 0, "cave", "none");
    add("Hall", 0, 0, 0, 0, 0, "hall", "none");
    add("Cathedral", -2, 0, -0.3f, 0, 0, "hall", "none");
    add("Ghost", -5, 0, -0.4f, 0.3f, 0.5f, "hall", "none");
    add("Demon", -10, -5, -0.6f, 0, 0, "cave", "none");
    add("Dark Lord", -12, -6, -0.7f, 0, 0, "hall", "none");

    // --- Voicemod-style fun ---
    add("Mutation", 0, 0, 0, 0.25f, 3.5f, "none", "none");
    add("Drunk", 0, 0, -0.2f, 0.8f, 2.0f, "none", "none");
    add("Crazy", 3, 0, 0, 2.5f, 5.0f, "none", "none");
    add("Party", 5, 2, 0.4f, 3.0f, 2.0f, "hall", "none");
    add("Sleepy", -4, -2, -0.4f, 0.2f, 0.5f, "room", "none");
    add("Possessed", -7, 0, -0.5f, 4.0f, 4.0f, "hall", "alien");
    add("Romantic", -2, 1, 0.2f, 0, 0, "room", "none");
    add("Ogre", -14, -8, -0.8f, 0, 0, "cave", "none");
    add("Santa", -4, 0, 0.3f, 0, 0, "hall", "none");

    // --- EQ-based creative (Voicemod Voicelab style) ---
    addEq("Warm Radio Host", -2, 0, -0.3f, 0, 0, "room", "none",
          {2, 3, 0, -1, -2, -3, -4, -5});
    addEq("Podcast Pro", 0, 0, 0.2f, 0, 0, "none", "none",
          {0, 2, 1, 0, 1, 2, 0, 0});
    addEq("Bass Boost", 0, 0, -0.5f, 0, 0, "none", "none",
          {6, 5, 3, 0, 0, 0, 0, 0});
    addEq("Treble Cut", 0, 0, -0.3f, 0, 0, "none", "none",
          {0, 0, 0, 0, -2, -4, -6, -8});
    addEq("Telephone EQ", 0, 0, 0, 0, 0, "none", "none",
          {-8, -5, 0, 3, 4, 0, -8, -12});
    addEq("Vintage 50s", -2, 0, -0.4f, 0, 0, "radio", "none",
          {4, 3, 0, 2, 0, -3, -6, -10});
    addEq("Anime Girl", 7, 5, 0.5f, 0, 0, "none", "none",
          {0, 0, 0, 0, 2, 4, 5, 3});
    addEq("Villain", -6, -4, -0.6f, 0, 0, "cave", "none",
          {5, 4, 2, 0, -1, -2, -3, -4});
    add("Pilot", -2, -1, -0.2f, 0, 0, "helmet", "none");
    addEq("Space Marine", -5, -2, 0, 0, 0, "helmet", "none",
          {3, 2, 0, 1, 2, 3, 1, 0});
    addEq("Wizard", -3, 0, -0.2f, 0.3f, 0.5f, "hall", "none",
          {2, 1, 0, 0, 1, 2, 3, 2});

    add("Silence", 0, 0, 0, 0, 0, "none", "silence");

    // A small, phrase-gated bed is part of these characters. It is silent
    // whenever the existing VAD sleeps and can be disabled globally or per
    // character from the voice editor.
    for (VoicePreset& p : v) {
        if (p.name == "Radio" || p.name == "Phone" || p.name == "Walkie" ||
            p.name == "Warm Radio Host" || p.name == "Vintage 50s") {
            p.ambient = "radio";
            p.ambientGain = 0.20f;
        } else if (p.name == "Pilot" || p.name == "Space Marine") {
            p.ambient = "pilot";
            p.ambientGain = 0.16f;
        } else if (p.name == "Cave" || p.name == "Demon" || p.name == "Ogre" ||
                   p.name == "Villain") {
            p.ambient = "cave";
            p.ambientGain = 0.14f;
        }
    }
    return v;
}

inline bool loadPresets(const std::filesystem::path& file, std::vector<VoicePreset>& out) {
    std::ifstream in(file);
    if (!in) return false;
    try {
        nlohmann::json j;
        in >> j;
        if (!j.contains("characters") || !j["characters"].is_array()) return false;
        std::vector<VoicePreset> loaded;
        for (const auto& e : j["characters"]) {
            VoicePreset p;
            p.name = e.value("name", std::string());
            if (p.name.empty()) continue;
            p.transpose = float(e.value("transpose", 0.0));
            p.formant = float(e.value("formant", 0.0));
            p.timbre = float(e.value("timbre", 0.0));
            p.strength = float(e.value("strength", 1.0));
            p.lfoRate = float(e.value("lfoRate", 0.0));
            p.lfoDepth = float(e.value("lfoDepth", 0.0));
            p.gate = e.value("gate", true);
            p.comp = e.value("comp", true);
            if (e.contains("eq") && e["eq"].is_array()) {
                for (int i = 0; i < 8; ++i)
                    p.eq[i] = (e["eq"].size() > std::size_t(i))
                                  ? float(e["eq"][std::size_t(i)].get<double>())
                                  : 0.0f;
            }
p.space = e.value("space", "none");
            p.effect = e.value("effect", "none");
            if (e.contains("chain") && e["chain"].is_array()) {
                for (const auto& ce : e["chain"]) {
                    if (!ce.is_string()) continue;
                    std::string item = ce.get<std::string>();
                    if (isChainModule(item) && p.chain.size() < 16) p.chain.push_back(std::move(item));
                }
            }
            p.chainExplicit = e.value("chainExplicit", false) || hasVoiceLabChain(p.chain);
            if (e.contains("moduleParams") && e["moduleParams"].is_object()) {
                for (auto it = e["moduleParams"].begin(); it != e["moduleParams"].end(); ++it) {
                    if (!isVoiceLabModule(it.key())) continue;
                    p.moduleParams[it.key()] = readVoiceLabParams(it.value(), it.key());
                }
            }
            p.custom = e.value("custom", false);
            p.hotkey = e.value("hotkey", std::string());
            p.ambientExplicit = e.contains("ambient");
            p.ambient = e.value("ambient", std::string("none"));
            for (char& c : p.ambient) if (c == '\\') c = '/';
            if (p.ambient.size() > 260 || p.ambient.find("..") != std::string::npos)
                p.ambient = "none";
            if (p.ambient != "none" && p.ambient != "radio" && p.ambient != "pilot" &&
                p.ambient != "cave" && p.ambient.rfind("ambiences/", 0) != 0)
                p.ambient = "none";
            p.ambientGain = float(e.value("ambientGain", 0.18));
            p.ambientGain = std::max(0.0f, std::min(1.0f, p.ambientGain));
            loaded.push_back(std::move(p));
        }
        if (loaded.empty()) return false;
        out = std::move(loaded);
        return true;
    } catch (...) {
        return false;
    }
}

inline bool savePresets(const std::filesystem::path& file, const std::vector<VoicePreset>& presets) {
    nlohmann::json j;
    j["characters"] = nlohmann::json::array();
    for (const VoicePreset& p : presets) {
        nlohmann::json e;
        e["name"] = p.name;
        e["transpose"] = double(p.transpose);
        e["formant"] = double(p.formant);
        e["timbre"] = double(p.timbre);
        e["strength"] = double(p.strength);
        e["lfoRate"] = double(p.lfoRate);
        e["lfoDepth"] = double(p.lfoDepth);
        e["gate"] = p.gate;
        e["comp"] = p.comp;
        nlohmann::json eqArr = nlohmann::json::array();
        for (int i = 0; i < 8; ++i) eqArr.push_back(double(p.eq[i]));
        e["eq"] = std::move(eqArr);
        e["space"] = p.space;
        e["effect"] = p.effect;
        nlohmann::json chainArr = nlohmann::json::array();
        for (const auto& c : p.chain) chainArr.push_back(c);
        e["chain"] = std::move(chainArr);
        e["chainExplicit"] = p.chainExplicit;
        nlohmann::json moduleParams = nlohmann::json::object();
        for (const auto& [name, params] : p.moduleParams)
            if (isVoiceLabModule(name)) moduleParams[name] = writeVoiceLabParams(params);
        e["moduleParams"] = std::move(moduleParams);
        e["custom"] = p.custom;
        e["hotkey"] = p.hotkey;
        // Presence is the migration marker for factory ambience overrides.
        if (p.ambientExplicit || p.custom) {
            e["ambient"] = p.ambient;
            e["ambientGain"] = double(p.ambientGain);
        }
        j["characters"].push_back(std::move(e));
    }
    std::ofstream out(file);
    if (!out) return false;
    out << j.dump(2);
    return out.good();
}

} // namespace vd
