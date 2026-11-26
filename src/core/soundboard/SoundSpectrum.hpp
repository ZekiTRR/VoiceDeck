#pragma once

#include <cstdint>
#include <vector>

namespace vd {

// Compact magnitude spectrogram for the sound editor. Data is laid out as
// column-major time (cols) × log-frequency bins, each value mapped to 0..127.
struct SoundSpectrum {
    int cols = 0;
    int bins = 0;
    int dbMin = -80;
    std::vector<std::uint8_t> data;
};

SoundSpectrum computeSoundSpectrum(const float* pcm, std::uint64_t frames,
                                   std::uint32_t channels, std::uint32_t sampleRate,
                                   int cols = 192, int bins = 32);

} // namespace vd
