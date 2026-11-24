#include "core/soundboard/Sound.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>

namespace vd {

bool Sound::loadFromFile(Sound& out, std::uint64_t id, const std::filesystem::path& path,
                         std::uint32_t sampleRate, std::uint32_t channels, std::string& err,
                          double maxDurationSec, std::uint64_t maxFileBytes) {
    // ma_decoder_config_init(format, channels, outputSampleRate): the decoder
    // converts everything on the fly - format, channel count and sample rate
    // (via its internal ma_resampler, linear algorithm). What we read out is
    // exactly engine-format float32 PCM.
    if (maxFileBytes > 0) {
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(path, ec);
        if (!ec && bytes > maxFileBytes) {
            err = "audio file is too large";
            return false;
        }
    }
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
    if (maxDurationSec > 0.0 && sampleRate > 0 &&
        total > ma_uint64(maxDurationSec * double(sampleRate))) {
        ma_decoder_uninit(&dec);
        err = "audio is longer than " + std::to_string(int(maxDurationSec)) + " seconds";
        return false;
    }
    if (total > std::numeric_limits<std::size_t>::max() / std::max(1u, channels)) {
        ma_decoder_uninit(&dec);
        err = "audio is too large";
        return false;
    }
    try {
        if (total > 0) out.pcm.reserve(static_cast<std::size_t>(total) * channels);
        constexpr ma_uint64 kChunkFrames = 4096;
        std::vector<float> chunk(static_cast<std::size_t>(kChunkFrames) * channels);
        for (;;) {
            ma_uint64 read = 0;
            if (ma_decoder_read_pcm_frames(&dec, chunk.data(), kChunkFrames, &read) != MA_SUCCESS ||
                read == 0)
                break;
            if (maxDurationSec > 0.0 && sampleRate > 0 &&
                out.pcm.size() / std::max(1u, channels) + read >
                    ma_uint64(maxDurationSec * double(sampleRate))) {
                ma_decoder_uninit(&dec);
                out.pcm.clear();
                err = "audio is longer than " + std::to_string(int(maxDurationSec)) + " seconds";
                return false;
            }
            out.pcm.insert(out.pcm.end(), chunk.begin(),
                           chunk.begin() + static_cast<std::ptrdiff_t>(read) * channels);
        }
    } catch (const std::bad_alloc&) {
        ma_decoder_uninit(&dec);
        out.pcm.clear();
        err = "not enough memory to decode audio";
        return false;
    } catch (const std::length_error&) {
        ma_decoder_uninit(&dec);
        out.pcm.clear();
        err = "audio is too large";
        return false;
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
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.durationSec = double(out.frames) / double(sampleRate);

    // Fresh load: no edits, no pristine copy until applyEdits() is called.
    out.tags.clear();
    out.trimStart = 0.0;
    out.trimEnd = 0.0;
    out.editGain = 1.0f;
    out.regions.clear();
    out.sourcePcm.clear();
    return true;
}

void Sound::applyEdits() {
    if (sampleRate == 0 || channels == 0) return;
    // Capture the decoded original exactly once. Later edits always start
    // from this copy, so trim/gain changes never stack or lose quality.
    if (sourcePcm.empty()) sourcePcm = pcm;

    const std::size_t ch = static_cast<std::size_t>(channels);
    const std::uint64_t srcFrames = sourcePcm.size() / ch;
    if (srcFrames == 0) return;
    const double total = double(srcFrames) / double(sampleRate);

    // Normalize trim (0 means "use the whole source").
    trimStart = std::max(0.0, std::min(trimStart, total));
    double end = (trimEnd <= 0.0) ? total : std::min(trimEnd, total);
    if (end - trimStart < 0.01) { // degenerate range -> full clip
        trimStart = 0.0;
        end = total;
    }
    trimEnd = end;

    std::uint64_t from = static_cast<std::uint64_t>(std::floor(trimStart * double(sampleRate)));
    std::uint64_t to = static_cast<std::uint64_t>(std::ceil(trimEnd * double(sampleRate)));
    from = std::min(from, srcFrames);
    to = std::min(std::max(to, from), srcFrames);

    // Short linear ramps at region borders avoid clicks when a region's gain
    // differs from its surroundings.
    auto regionFactor = [&](const SoundRegion& r, double t) {
        if (r.gain == 1.0f || r.endSec <= r.startSec) return 1.0f;
        if (t < r.startSec || t >= r.endSec) return 1.0f;
        double f = 1.0;
        const double fade = std::min(0.005, (r.endSec - r.startSec) / 4.0);
        if (fade > 0.0) {
            f = std::min(f, (t - r.startSec) / fade);
            f = std::min(f, (r.endSec - t) / fade);
        }
        return float(1.0 + (double(r.gain) - 1.0) * std::max(0.0, std::min(1.0, f)));
    };

    const std::size_t nFrames = static_cast<std::size_t>(to - from);
    std::vector<float> out;
    out.resize(nFrames * ch);
    for (std::size_t i = 0; i < nFrames; ++i) {
        const std::size_t src = static_cast<std::size_t>(from + i) * ch;
        const double t = double(from + i) / double(sampleRate); // time on the source timeline
        float g = editGain;
        for (const SoundRegion& r : regions) g *= regionFactor(r, t);
        float* dst = out.data() + i * ch;
        for (std::size_t c = 0; c < ch; ++c) dst[c] = sourcePcm[src + c] * g;
    }

    pcm = std::move(out);
    frames = nFrames;
    durationSec = double(frames) / double(sampleRate);
}

} // namespace vd
