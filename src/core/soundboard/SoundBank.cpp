#include "core/soundboard/SoundBank.hpp"

#include "core/util/Log.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>

namespace vd {
namespace {

std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.c_str()), u.size());
}

std::filesystem::path utf8ToPath(const std::string& s) {
    return std::filesystem::path(reinterpret_cast<const char8_t*>(s.c_str()));
}

} // namespace

std::uint64_t SoundBank::add(const std::filesystem::path& path, const std::string& name,
                             std::uint32_t sampleRate, std::uint32_t channels, std::string& err) {
    if (owned_.size() >= kMaxSounds) {
        err = "sound library is full (64 clips)";
        return 0;
    }

    // Copy the clip into the project's sound folder so the library is fully
    // self-contained (skipped if it already lives there).
    std::error_code ec;
    std::filesystem::path stored = path;
    bool inside = false;
    {
        std::error_code e;
        const std::filesystem::path rel = std::filesystem::relative(path, clipDir_, e);
        inside = !e && rel.native().find(L"..") != 0 && !rel.empty();
    }
    if (!inside) {
        std::filesystem::create_directories(clipDir_, ec);
        std::filesystem::path dst = clipDir_ / path.filename();
        // Avoid collisions: append " (2)", " (3)", ...
        int n = 2;
        while (std::filesystem::exists(dst, ec)) {
            dst = clipDir_ / (path.stem().string() + " (" + std::to_string(n++) + ")" +
                              path.extension().string());
        }
        std::filesystem::copy_file(path, dst, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) {
            err = "cannot copy into the sound folder: " + ec.message();
            // Not fatal: fall through and keep the original path.
        } else {
            stored = dst;
        }
    }

    auto s = std::make_unique<Sound>();
    if (!Sound::loadFromFile(*s, 0, stored, sampleRate, channels, err)) return 0;
    s->id = nextId_.fetch_add(1, std::memory_order_relaxed);
    if (!name.empty()) s->name = name;
    Sound* raw = s.get();
    owned_.push_back(std::move(s));
    for (std::size_t i = 0; i < kMaxSounds; ++i) {
        if (slots_[i].load(std::memory_order_acquire) == nullptr) {
            slots_[i].store(raw, std::memory_order_release);
            break;
        }
    }
    return raw->id;
}

std::string SoundBank::remove(std::uint64_t id) {
    Sound* s = find(id);
    if (!s) return "no sound with id " + std::to_string(id);
    // Block new plays, then unpublish the slot. Active voices have already
    // been stopped by the caller (commands drained), so nothing reads the
    // pointer anymore.
    s->removing.store(true, std::memory_order_relaxed);
    for (std::size_t i = 0; i < kMaxSounds; ++i) {
        if (slots_[i].load(std::memory_order_acquire) == s) {
            slots_[i].store(nullptr, std::memory_order_release);
            break;
        }
    }
    auto it = std::find_if(owned_.begin(), owned_.end(),
                           [&](const std::unique_ptr<Sound>& p) { return p.get() == s; });
    if (it != owned_.end()) owned_.erase(it); // frees the PCM
    return "";
}

std::string SoundBank::rename(std::uint64_t id, const std::string& newName) {
    Sound* s = find(id);
    if (!s) return "no sound with id " + std::to_string(id);

    // Clean display name.
    std::string clean;
    for (char c : newName) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= ' ') clean.push_back(c);
    }
    while (!clean.empty() && clean.front() == ' ') clean.erase(clean.begin());
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    if (clean.empty()) return "имя пустое";
    if (clean.size() > 64) clean.resize(64);
    s->name = clean;

    // Rename the stored file when it lives inside the project's sound folder.
    std::error_code ec;
    const std::filesystem::path old = s->path;
    const std::filesystem::path rel = std::filesystem::relative(old, clipDir_, ec);
    if (!ec && !rel.empty() && rel.native().find(L"..") != 0) {
        std::string file = clean;
        for (char& c : file) {
            if (std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
        }
        if (!file.empty() && file != ".") {
            const std::filesystem::path next = clipDir_ / (file + old.extension().string());
            if (next != old) {
                std::filesystem::rename(old, next, ec);
                if (!ec) s->path = next;
                // A rename failure (locked file / collision) is not fatal:
                // the display name still changed.
            }
        }
    }
    return "";
}

std::vector<const Sound*> SoundBank::snapshot() const {
    std::vector<const Sound*> out;
    out.reserve(owned_.size());
    for (std::size_t i = 0; i < kMaxSounds; ++i) {
        const Sound* s = slots_[i].load(std::memory_order_acquire);
        if (s) out.push_back(s);
    }
    return out;
}

std::map<std::uint64_t, std::string> SoundBank::loadFromJson(const std::filesystem::path& file,
                                                             std::uint32_t sampleRate,
                                                             std::uint32_t channels) {
    std::map<std::uint64_t, std::string> combos;
    std::ifstream in(file);
    if (!in) return combos;
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        log::warn("sound library file is corrupt: %s", file.string().c_str());
        return combos;
    }
    if (!j.contains("sounds") || !j["sounds"].is_array()) return combos;

    std::uint64_t maxId = 0;
    for (const auto& e : j["sounds"]) {
        std::filesystem::path p = utf8ToPath(e.value("path", std::string()));
        // Relative paths resolve against the project's sound folder.
        if (p.is_relative()) p = clipDir_ / p;
        if (p.empty()) continue;
        std::string err;
        const std::uint64_t id = add(p, e.value("name", std::string()), sampleRate, channels, err);
        if (id == 0) {
            log::warn("skipping '%s': %s", p.string().c_str(), err.c_str());
            continue;
        }
        if (Sound* s = find(id)) {
            s->gain.store(float(e.value("gain", 1.0)));
            s->loop.store(e.value("loop", false));
            auto eb = e.find("eq");
            if (eb != e.end() && eb->is_array()) {
                for (int i = 0; i < 5 && std::size_t(i) < eb->size(); ++i)
                    s->eq[i].store(float((*eb)[std::size_t(i)].get<double>()));
            }
        }
        if (e.contains("hotkey")) combos[id] = e.value("hotkey", std::string());
        maxId = std::max(maxId, id);
    }
    if (j.contains("stopAllHotkey")) combos[0] = j.value("stopAllHotkey", std::string());
    if (nextId_.load(std::memory_order_relaxed) <= maxId)
        nextId_.store(maxId + 1, std::memory_order_relaxed);
    return combos;
}

bool SoundBank::saveToJson(const std::filesystem::path& file,
                           const std::map<std::uint64_t, std::string>& hotkeys) const {
    nlohmann::json j;
    j["sounds"] = nlohmann::json::array();
    for (const Sound* s : snapshot()) {
        nlohmann::json e;
        e["id"] = s->id;
        e["name"] = s->name;
        e["path"] = pathToUtf8(s->path);
        e["gain"] = double(s->gain.load(std::memory_order_relaxed));
        e["loop"] = bool(s->loop.load(std::memory_order_relaxed));
        e["eq"] = nlohmann::json::array({
            double(s->eq[0].load(std::memory_order_relaxed)),
            double(s->eq[1].load(std::memory_order_relaxed)),
            double(s->eq[2].load(std::memory_order_relaxed)),
            double(s->eq[3].load(std::memory_order_relaxed)),
            double(s->eq[4].load(std::memory_order_relaxed)),
        });
        auto it = hotkeys.find(s->id);
        if (it != hotkeys.end()) e["hotkey"] = it->second;
        j["sounds"].push_back(std::move(e));
    }
    auto stop = hotkeys.find(0);
    if (stop != hotkeys.end() && !stop->second.empty()) j["stopAllHotkey"] = stop->second;

    std::ofstream out(file);
    if (!out) return false;
    out << j.dump(2);
    return out.good();
}

} // namespace vd
