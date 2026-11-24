#pragma once

#include <filesystem>
#include <string>

namespace vd {

// Persisted engine settings. Everything lives in a portable "data" folder
// next to the executable (nothing is written to the system drive's
// %APPDATA%).
struct Settings {
    std::string captureDevice;  // exact device name, "" = auto
    std::string playbackDevice; // exact device name, "" = auto (CABLE Input preferred)
    std::string monitorDevice; // exact device name, "" = monitor off
    bool bypass = true;
    bool ducking = false;

    // AI voice tuning (detailed settings, persisted).
    std::string aiProvider = "auto"; // auto | cuda | dml | cpu
    int aiPrimeMarginMs = 20;        // extra buffered delay beyond one block
                                     // (v2 50 ms block + 20 ms + 10 ms xfade = ~80 ms)
    int aiFadeMs = 30;               // wet/dry crossfade on (de)activation
    bool aiAutoLimit = true;         // auto-disable on audible dropouts
    int aiFlapLimit = 8;             // ...after N dropouts per 30 s

    // Voice-activity gate: below this level for ~300 ms the DSP/AI branch
    // sleeps (the soundboard keeps playing). -60..-20 dBFS.
    float vadThresholdDb = -48.0f;

    // Latency mode: 0 = low (256 frames + 20ms Stretch), 1 = normal (480 + 40ms),
    // 2 = high quality (960 + 53ms). Trade CPU load for delay.
    int latencyMode = 0;

    // Windows integration. `runAsAdmin` relaunches the app through UAC on
    // startup; `autoStart` lives in HKCU\...\Run (see AutoStart.hpp).
    bool runAsAdmin = false;

    // Extra global hotkeys.
    std::string voiceHotkey; // toggles the DSP chain
    std::string aiHotkey;    // toggles the AI voice

    // Exe-relative portable data folder (created on demand). All configs,
    // the sound library and the AI models live here.
    static std::filesystem::path appDataDir();

    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
};

} // namespace vd
