#include "core/settings/Settings.hpp"

#include "nlohmann/json.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>
#include <windows.h>

namespace vd {

std::filesystem::path Settings::appDataDir() {
    // Portable layout: "<exe dir>/data". Nothing is written to %APPDATA%.
    char exePath[MAX_PATH] = {};
    std::filesystem::path base = ".";
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
        base = std::filesystem::path(exePath).parent_path();
    std::filesystem::path dir = base / "data";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool Settings::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    try {
        nlohmann::json j;
        in >> j;
        captureDevice = j.value("captureDevice", "");
        playbackDevice = j.value("playbackDevice", "");
        monitorDevice = j.value("monitorDevice", "");
        bypass = j.value("bypass", true);
        ducking = j.value("ducking", false);
        aiProvider = j.value("aiProvider", "auto");
        aiPrimeMarginMs = j.value("aiPrimeMarginMs", 100);
        aiFadeMs = j.value("aiFadeMs", 30);
        aiAutoLimit = j.value("aiAutoLimit", true);
        aiFlapLimit = j.value("aiFlapLimit", 8);
        latencyMode = j.value("latencyMode", 1);
        voiceHotkey = j.value("voiceHotkey", "");
        aiHotkey = j.value("aiHotkey", "");
    } catch (...) {
        return false;
    }
    return true;
}

bool Settings::save(const std::filesystem::path& file) const {
    nlohmann::json j;
    j["captureDevice"] = captureDevice;
    j["playbackDevice"] = playbackDevice;
    j["monitorDevice"] = monitorDevice;
    j["bypass"] = bypass;
    j["ducking"] = ducking;
    j["aiProvider"] = aiProvider;
    j["aiPrimeMarginMs"] = aiPrimeMarginMs;
    j["aiFadeMs"] = aiFadeMs;
    j["aiAutoLimit"] = aiAutoLimit;
    j["aiFlapLimit"] = aiFlapLimit;
    j["latencyMode"] = latencyMode;
    j["voiceHotkey"] = voiceHotkey;
    j["aiHotkey"] = aiHotkey;

    std::ofstream out(file);
    if (!out) return false;
    out << j.dump(2);
    return out.good();
}

} // namespace vd
