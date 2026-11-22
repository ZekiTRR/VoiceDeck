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
    int aiPrimeMarginMs = 100;       // extra buffered delay beyond one block
    int aiFadeMs = 30;               // wet/dry crossfade on (de)activation
    bool aiAutoLimit = true;         // auto-disable on audible dropouts
    int aiFlapLimit = 8;             // ...after N dropouts per 30 s

    // Latency mode: 0 = low (~5ms WASAPI + 20ms Stretch), 1 = normal (~10ms + 40ms),
    // 2 = high quality (~20ms + 53ms). Trade CPU load for delay.
    int latencyMode = 1;

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
