#pragma once

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace vd {

// A voice "character" - the full parameter set of the DSP chain.
//
//   mic -> [enhancement: HPF, gate, compressor, timbre shelves, 8-band EQ]
//       -> [character: pitch/formant via Signalsmith Stretch, +LFO]
//       -> [space: room/hall/cave/helmet/radio/megaphone]
//       -> [effect: robot/alien/atari/clone/silence]
//       -> limiter -> dry/wet (strength) -> out
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
    // 80, 200, 500, 1200, 3000, 6000, 10000, 14000 Hz.
    float eq[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::string space = "none";
    std::string effect = "none";
    // Chain: ordered list of space/effect names processed in sequence.
    // If non-empty, it overrides space/effect. E.g.: ["radio","robot","cave"]
    std::vector<std::string> chain;
    bool custom = false;       // user-made (deletable in the UI)
    std::string hotkey;       // persistence only (bound by HotkeyManager)
};

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
    addEq("Space Marine", -5, -2, 0, 0, 0, "helmet", "none",
          {3, 2, 0, 1, 2, 3, 1, 0});
    addEq("Wizard", -3, 0, -0.2f, 0.3f, 0.5f, "hall", "none",
          {2, 1, 0, 0, 1, 2, 3, 2});

    add("Silence", 0, 0, 0, 0, 0, "none", "silence");
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
                for (const auto& ce : e["chain"])
                    if (ce.is_string()) p.chain.push_back(ce.get<std::string>());
            }
            p.custom = e.value("custom", false);
            p.hotkey = e.value("hotkey", std::string());
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
        e["custom"] = p.custom;
        e["hotkey"] = p.hotkey;
        j["characters"].push_back(std::move(e));
    }
    std::ofstream out(file);
    if (!out) return false;
    out << j.dump(2);
    return out.good();
}

} // namespace vd
