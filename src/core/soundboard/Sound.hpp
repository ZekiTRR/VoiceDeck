#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vd {

// A sound clip decoded once into RAM as float32 PCM at the engine's output
// rate. PCM is immutable after loading; gain/loop are atomic so the audio
// callback can read them without locks.
struct Sound {
    std::uint64_t id = 0;
    std::string name;
    std::filesystem::path path;
    std::vector<float> pcm; // interleaved float32, `channels` samples per frame
    std::uint64_t frames = 0;
    double durationSec = 0.0;
    std::atomic<float> gain{1.0f};
    std::atomic<bool> loop{false};
    // Per-clip 5-band EQ. Fixed frequencies: 100, 500, 2000, 6000, 12000 Hz.
    std::atomic<float> eq[5] = {{0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f}};
    // Set by remove(): blocks new plays while the clip is being retired.
    std::atomic<bool> removing{false};

    // Decodes `path` (WAV/MP3/FLAC via miniaudio) straight to float32 PCM at
    // `sampleRate`/`channels`. Rate and channel conversion happen inside
    // ma_decoder's built-in data converter, whose resampling stage is
    // ma_resampler (linear) - the "decode once, resample to the output
    // device rate, cache in RAM" pipeline for the soundboard. The audio
    // callback never touches decoders or resamplers.
    static bool loadFromFile(Sound& out, std::uint64_t id, const std::filesystem::path& path,
                             std::uint32_t sampleRate, std::uint32_t channels, std::string& err);
};

} // namespace vd
