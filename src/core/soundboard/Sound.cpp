#include "core/soundboard/Sound.hpp"

#include "miniaudio.h"

#include <cmath>
#include <cstddef>

namespace vd {

bool Sound::loadFromFile(Sound& out, std::uint64_t id, const std::filesystem::path& path,
                         std::uint32_t sampleRate, std::uint32_t channels, std::string& err) {
    // ma_decoder_config_init(format, channels, outputSampleRate): the decoder
    // converts everything on the fly - format, channel count and sample rate
    // (via its internal ma_resampler, linear algorithm). What we read out is
    // exactly engine-format float32 PCM.
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, channels, sampleRate);

    ma_decoder dec;
    // miniaudio expects UTF-8 file paths on Windows.
    const std::u8string u8path = path.u8string();
    if (ma_decoder_init_file(reinterpret_cast<const char*>(u8path.c_str()), &cfg, &dec) != MA_SUCCESS) {
        err = "cannot open or decode file (supported: WAV, MP3, FLAC)";
        return false;
    }

    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &total);
    if (total > 0) out.pcm.reserve(static_cast<std::size_t>(total) * channels);

    constexpr ma_uint64 kChunkFrames = 4096;
    std::vector<float> chunk(static_cast<std::size_t>(kChunkFrames) * channels);
    for (;;) {
        ma_uint64 read = 0;
        if (ma_decoder_read_pcm_frames(&dec, chunk.data(), kChunkFrames, &read) != MA_SUCCESS || read == 0)
            break;
        out.pcm.insert(out.pcm.end(), chunk.begin(),
                       chunk.begin() + static_cast<std::ptrdiff_t>(read) * channels);
    }
    ma_decoder_uninit(&dec);

    out.frames = out.pcm.size() / channels;
    if (out.frames == 0) {
        err = "file contains no audio";
        out.pcm.clear();
        return false;
    }
    out.id = id;
    out.path = path;
    out.name = path.stem().string();
    if (out.name.empty()) out.name = "sound";
    out.durationSec = double(out.frames) / double(sampleRate);
    return true;
}

} // namespace vd
