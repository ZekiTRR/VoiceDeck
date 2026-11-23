#pragma once

#include "core/soundboard/Sound.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vd {

// Lock-free, append-only registry of sounds.
//
// The CLI thread is the only writer: a Sound is fully constructed first and
// then published into a slot with a release store. The audio callback and
// the hotkey thread look sounds up with acquire loads - no locks, no ABA
// (entries are only removed through remove(), which first blocks new plays
// and waits for active voices to stop).
class SoundBank {
public:
    static constexpr std::size_t kMaxSounds = 64;

    // clipDir: where `add` copies new clips (data/sounds).
    explicit SoundBank(std::filesystem::path clipDir) : clipDir_(std::move(clipDir)) {}

    // Thread-safe (lock-free scan).
    Sound* find(std::uint64_t id) const {
        for (std::size_t i = 0; i < kMaxSounds; ++i) {
            Sound* s = slots_[i].load(std::memory_order_acquire);
            if (s && s->id == id) return s;
        }
        return nullptr;
    }

    // CLI thread only. Copies the file into the project's sound folder and
    // decodes it. Returns the new sound id, or 0 on failure.
    std::uint64_t add(const std::filesystem::path& path, const std::string& name,
                      std::uint32_t sampleRate, std::uint32_t channels, std::string& err);

    // CLI thread only: retire a clip. Blocks new plays, stops active voices
    // (stopAllVoices must be called + commands drained by the caller), then
    // frees the memory. Returns "" on success.
    std::string remove(std::uint64_t id);

    // CLI thread only: rename a clip (display name + the stored file when it
    // lives in the project's sound folder). Returns "" on success.
    std::string rename(std::uint64_t id, const std::string& newName);

    // CLI thread only (listings / persistence). Sorted by slot order.
    std::vector<const Sound*> snapshot() const;

    // --- tags ---------------------------------------------------------------
    // CLI thread only. Tags are trimmed, de-duplicated case-insensitively and
    // capped at 16 entries of 32 characters each. Returns "" on success.
    std::string setTags(std::uint64_t id, const std::vector<std::string>& tags);
    std::string addTag(std::uint64_t id, const std::string& tag);
    std::string removeTag(std::uint64_t id, const std::string& tag);
    bool hasTag(std::uint64_t id, const std::string& tag) const;
    // Sorted, unique list of every tag in the library (for filter chips).
    std::vector<std::string> allTags() const;
    static std::vector<std::string> normalizeTags(const std::vector<std::string>& tags);

    // --- in-app audio editor -------------------------------------------------
    // CLI thread only. Rebuilds the clip PCM from the pristine source. No
    // files are modified. Returns "" on success.
    std::string applyEdits(std::uint64_t id, double trimStart, double trimEnd, float gain,
                           const std::vector<SoundRegion>& regions);

    // CLI thread only. Loads a library file; returns soundId -> hotkey combo
    // pairs (including a 0 -> combo entry for the global "stop all" hotkey).
    // Relative paths resolve against the sound folder.
    std::map<std::uint64_t, std::string> loadFromJson(const std::filesystem::path& file,
                                                      std::uint32_t sampleRate,
                                                      std::uint32_t channels);

    // CLI thread only.
    bool saveToJson(const std::filesystem::path& file,
                    const std::map<std::uint64_t, std::string>& hotkeys) const;

private:
    std::filesystem::path clipDir_;
    mutable std::array<std::atomic<Sound*>, kMaxSounds> slots_{};
    std::atomic<std::uint64_t> nextId_{1};
    std::vector<std::unique_ptr<Sound>> owned_; // CLI-thread bookkeeping
};

} // namespace vd
