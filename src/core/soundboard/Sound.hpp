#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vd {

// Built-in tag assigned automatically to every clip that has a hotkey.
inline constexpr const char* kTagHotkey = "hotkey";

// A volume change applied to a slice of the clip. Times are relative to the
// original (untrimmed) source so they line up with the full waveform shown
// in the editor.
struct SoundRegion {
    double startSec = 0.0;
    double endSec = 0.0;
    float gain = 1.0f;
};

// A sound clip decoded once into RAM as float32 PCM at the engine's output
// rate. PCM is immutable after loading; gain/loop are atomic so the audio
// callback can read them without locks.
//
// Tags and edit parameters are UI-thread-only. `applyEdits()` rebuilds `pcm`
// from a pristine copy captured on the first edit; nothing is ever written
// back to the source file, so all edits are fully non-destructive and
// reversible (saved as metadata in sounds.json).
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

    // --- tags -------------------------------------------------------------
    std::vector<std::string> tags;

    // --- in-app audio editor ---------------------------------------------
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    double trimStart = 0.0; // seconds; 0 = start of source
    double trimEnd = 0.0;   // seconds; 0 (or > source length) = end of source
    float editGain = 1.0f;  // baked-in global gain multiplier
    std::vector<SoundRegion> regions;
    // Equal-power fades on the trimmed clip timeline. 0 disables a fade.
    double fadeInSec = 0.0;
    double fadeOutSec = 0.0;
    // Pristine decoded audio, captured lazily on the first edit. Empty until
    // then, so unedited clips use no extra memory.
    std::vector<float> sourcePcm;

    // Rebuilds `pcm`/`frames`/`durationSec` from the pristine audio using the
    // current trim/gain/regions. Must be called on the UI/CLI thread while no
    // voices are playing this clip (the audio callback reads `pcm` lock-free).
    void applyEdits();

    // Decodes `path` (WAV/MP3/FLAC via miniaudio) straight to float32 PCM at
    // `sampleRate`/`channels`. Rate and channel conversion happen inside
    // ma_decoder's built-in data converter, whose resampling stage is
    // ma_resampler (linear) - the "decode once, resample to the output
    // device rate, cache in RAM" pipeline for the soundboard. The audio
    // callback never touches decoders or resamplers.
    static bool loadFromFile(Sound& out, std::uint64_t id, const std::filesystem::path& path,
                             std::uint32_t sampleRate, std::uint32_t channels, std::string& err,
                             double maxDurationSec = 0.0, std::uint64_t maxFileBytes = 0);
};

} // namespace vd
