#pragma once

#include "core/engine/CommandQueue.hpp"
#include "core/soundboard/Sound.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vd {

class SoundBank;

// Preallocated playback voices - zero heap traffic on the audio thread.
// The audio callback is the only writer of playback state; control threads
// only read the atomic fields for status display.
struct Voice {
    std::atomic<bool> active{false};
    std::atomic<bool> paused{false};
    std::atomic<std::uint64_t> voiceId{0};
    std::atomic<std::uint64_t> soundId{0};
    std::atomic<std::uint64_t> posFrames{0};
    std::atomic<const Sound*> sound{nullptr};
    // Audio-thread-only:
    float gain = 1.0f;
    bool loop = false;
    // Per-voice 5-band EQ, two channels of biquad state.
    // Coeffs: [band][0..4] = b0,b1,b2,a1,a2; history: [band][ch][0..2].
    float eqCoef_[5][5] = {};
    float eqX_[5][2][3] = {};
    float eqY_[5][2][3] = {};
};

struct VoiceInfo {
    std::uint64_t voiceId = 0;
    std::uint64_t soundId = 0;
    std::uint64_t posFrames = 0;
    std::uint64_t totalFrames = 0;
    bool paused = false;
};

class VoiceMixer {
public:
    static constexpr std::size_t kMaxVoices = 32;

    // Audio thread only.
    void applyCommand(const Command& c, const SoundBank& bank);
    void mix(float* out, std::size_t frames, std::uint32_t channels, float duckGain);

    // Any thread (reads atomics).
    std::vector<VoiceInfo> snapshot() const;
    std::uint64_t rejectedPlays() const { return rejected_.load(std::memory_order_relaxed); }

private:
    void setVoiceEq(Voice& v, const Sound& s);
    static void setPeakingCoefs(float* c, double freqHz, double q, double gainDb);

private:
    std::array<Voice, kMaxVoices> voices_{};
    std::atomic<std::uint64_t> nextVoiceId_{1};
    std::atomic<std::uint64_t> rejected_{0};
};

} // namespace vd
