#pragma once

#include "core/dsp/Preset.hpp"
#include "core/engine/AudioEngine.hpp"
#include "core/platform/HotkeyManager.hpp"
#include "core/settings/Settings.hpp"
#include "core/soundboard/SoundBank.hpp"

#include "nlohmann/json.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vd {

// The application core shared by both front-ends (CLI and the WebView2 UI):
// owns the engine, sound bank, presets, AI service and hotkeys, and exposes
// operations as plain methods returning JSON-able values.
struct App {
    App();

    // Loads settings/presets/sound library, initializes the engine and
    // picks devices. Call once at startup.
    void init();

    // --- hotkeys ------------------------------------------------------------
    // Wires the hotkey/tray thread callbacks; restores persisted bindings
    // (sounds, presets, voice/ai toggles).
    void startHotkeyService();
    // Optional hook fired when something changes via hotkeys/tray so a UI
    // can refresh (event ids: "dspChanged", "aiChanged", "presetChanged",
    // "soundPlayed", "stopAll", "bypassChanged").
    std::function<void(const char* event)> onHotkeyEvent;
    // Fired when the tray menu's Exit is clicked.
    std::function<void()> onExitRequested;

    // --- soundboard ----------------------------------------------------------
    std::uint64_t addSound(const std::filesystem::path& path, const std::string& name,
                           std::string& err);
    std::string removeSound(std::uint64_t id); // "" ok; stops voices first
    void playSound(std::uint64_t id);
    void stopSound(std::uint64_t id);
    void stopAllSounds();
    void setSoundVolume(std::uint64_t id, int pct);
    void setSoundLoop(std::uint64_t id, bool on);
    // Voice-activity gate threshold (-60..-20 dBFS); persists to settings.
    std::string setVadThresholdDb(float db);
    void setLanguage(const std::string& language);
    // Global VAD-gated ambience master and soundboard FX Send.
    std::string setAmbientEnabled(bool on);
    std::string setSoundFxSend(bool on);
    // Persists ambience selection/gain for any character, including factory
    // characters (their voice parameters remain factory-locked).
    std::string setPresetAmbient(int idx, const nlohmann::json& def);
    // Copies/decodes a custom loop into data\ambiences and assigns it to idx.
    std::string importAmbient(int idx, const std::filesystem::path& path);

    // --- tags ---------------------------------------------------------------
    // `tags` is a JSON array of strings. Persists to sounds.json immediately.
    std::string setSoundTags(std::uint64_t id, const nlohmann::json& tags);
    std::string addSoundTag(std::uint64_t id, const std::string& tag);
    std::string removeSoundTag(std::uint64_t id, const std::string& tag);
    // Binds/clears a sound hotkey and keeps the built-in "hotkey" tag in sync.
    std::string bindSoundHotkey(std::uint64_t id, const std::string& combo);

    // --- in-app audio editor -------------------------------------------------
    // def: {"trimStart":s,"trimEnd":s,"gain":float,"regions":[{"start","end","gain"}]}.
    // Stops the clip first (refuses while it is still playing), then rebuilds
    // the PCM from the pristine source and saves the metadata to sounds.json.
    std::string editSound(std::uint64_t id, const nlohmann::json& def);

    // --- Windows integration -------------------------------------------------
    bool autoStartEnabled() const;
    std::string setAutoStart(bool on);
    // Returns "" on success. Sets `relaunching` when an elevated copy was
    // spawned (the caller should then exit this process).
    std::string setRunAsAdmin(bool on, bool& relaunching);
    // Call at startup before init(). Returns true when an elevated copy was
    // launched and this process should exit immediately.
    bool maybeElevate(const std::string& args);

    // --- DSP characters -------------------------------------------------------
    bool applyPreset(int idx); // false when out of range
    void setDsp(bool on);
    void setBypass(bool on);
    void setDucking(bool on);
    void setPitchOverride(float semi, bool on);
    void setFormantOverride(float semi, bool on);

    // --- AI voice --------------------------------------------------------------
    std::string loadAiVoice(const std::string& name); // "" ok (blocking)
    // Imports a user voice pair (x.onnx + x.json) into the voices folder.
    // "" on success, else an error message.
    std::string importAiVoice(const std::filesystem::path& onnxPath);
    void setAi(bool on);
    void setAiPitch(float semi);
    void setAiMargin(int ms);
    void setAiFade(int ms);
    void setAiAutolimit(bool on, int flapLimit);
    std::string setAiProvider(const std::string& name); // "" ok, reloads voice

    // --- user presets ------------------------------------------------------------
    // Creates a character from a JSON definition {"name","transpose",...} with
    // clamped/validated values. Returns the new index (>=0) or -1 + err.
    int addPreset(const nlohmann::json& def, std::string& err);
    // Removes a character (built-in "None" and index 0 are protected).
    std::string removePreset(int idx);
    // Live EQ for the given character (5 bands in dB); persists to
    // characters.json and re-applies the preset (20 ms crossfade).
    std::string setPresetEq(int idx, const float eq[8]);
    // Renames a sound clip (display name + the file in data\sounds).
    std::string renameSound(std::uint64_t id, const std::string& name);
    std::string previewSound(std::uint64_t id);
    void stopPreview();
    // Scans data\sounds, adds new audio files and drops library entries whose
    // backing file in that folder has been deleted. Returns {added, removed}.
    std::pair<std::size_t, std::size_t> refreshSounds();
    std::string setLatencyMode(int mode);
    // Live-edits the given preset's parameters and re-applies it (A/B crossfade).
    // The edit is TEMPORARY: App::presets / characters.json are never touched.
    // Only addPreset() ("Save as New") makes a change persistent.
    void liveUpdatePreset(int idx, const nlohmann::json& def);
    // Pushes a temporary copy into the audio engine without persisting it.
    void applyLivePreset(int idx, const VoicePreset& modified);
    // Discard a temporary editor override and return the active character to
    // its stored values (built-in presets stay factory-clean).
    bool resetLivePreset();

    // --- routing ---------------------------------------------------------------
    void selectDevices(size_t captureIdx, size_t playbackIdx);
    bool setMonitor(bool on, size_t playbackIdx = SIZE_MAX); // SIZE_MAX = default

    // --- state -----------------------------------------------------------------
    nlohmann::json statusJson() const;      // full snapshot for the UI
    nlohmann::json metersJson() const;      // light ~15 Hz refresh
    void saveAll();

    void applyAiSettings();

    // --- fields -----------------------------------------------------------------
    Settings settings;
    SoundBank bank;
    AudioEngine engine{bank};
    HotkeyManager hotkeys;
    std::filesystem::path dataDir;
    std::filesystem::path settingsFile;
    std::filesystem::path soundsFile;
    std::filesystem::path charactersFile;
    // Set by main() so the elevation relaunch preserves CLI/GUI mode.
    bool cliMode = false;
    std::vector<VoicePreset> presets;
    bool bypassOn = true;
    bool duckingOn = false;
    // True when previewSound() had to start the monitor itself; used to
    // restore the pre-preview state when the preview voice is stopped.
    bool monitorStartedForPreview_ = false;
    // Index of the preset currently carrying a temporary live override, or -1.
    int livePresetIndex_ = -1;

private:
    std::shared_ptr<const Sound> loadAmbientClip(const std::string& key, std::string& err);
    bool rebuildEngineAmbients();
    // Sound hotkey combos loaded from sounds.json (restored once the hotkey
    // service is up).
    std::map<std::uint64_t, std::string> soundCombos_;
    // Portable custom ambience PCM. Keys are "ambiences/<filename>"; entries
    // stay alive for the engine's shared_ptr snapshot even after a file swap.
    std::map<std::string, std::shared_ptr<const Sound>> ambientCache_;
};

} // namespace vd
