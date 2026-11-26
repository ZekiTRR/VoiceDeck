#include "core/soundboard/SoundSpectrum.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vd {
namespace {

constexpr int kFftSize = 512;
constexpr double kMinHz = 40.0;
constexpr double kMaxHz = 16000.0;

void fft(std::vector<float>& re, std::vector<float>& im) {
    const std::size_t n = re.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    constexpr double kPi = 3.14159265358979323846;
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / double(len);
        const float wRe = float(std::cos(angle));
        const float wIm = float(std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            float curRe = 1.0f;
            float curIm = 0.0f;
            for (std::size_t j = 0; j < len / 2; ++j) {
                const float aRe = re[i + j];
                const float aIm = im[i + j];
                const float bRe = re[i + j + len / 2] * curRe - im[i + j + len / 2] * curIm;
                const float bIm = re[i + j + len / 2] * curIm + im[i + j + len / 2] * curRe;
                re[i + j] = aRe + bRe;
                im[i + j] = aIm + bIm;
                re[i + j + len / 2] = aRe - bRe;
                im[i + j + len / 2] = aIm - bIm;
                const float nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }
}

} // namespace

SoundSpectrum computeSoundSpectrum(const float* pcm, std::uint64_t frames,
                                   std::uint32_t channels, std::uint32_t sampleRate,
                                   int cols, int bins) {
    SoundSpectrum out;
    if (!pcm || frames == 0 || channels == 0 || sampleRate == 0) return out;
    cols = std::max(1, std::min(cols, 512));
    bins = std::max(1, std::min(bins, 64));
    out.cols = cols;
    out.bins = bins;
    out.data.assign(std::size_t(cols) * std::size_t(bins), 0);

    std::vector<float> window(kFftSize);
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < kFftSize; ++i)
        window[std::size_t(i)] =
            float(0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(kFftSize - 1)));

    std::vector<float> re(kFftSize);
    std::vector<float> im(kFftSize);
    const double fMax = std::min(kMaxHz, double(sampleRate) * 0.5);
    const double logSpan = std::log(fMax / kMinHz);

    for (int col = 0; col < cols; ++col) {
        const double center = (double(col) + 0.5) * double(frames) / double(cols);
        const std::int64_t start = std::int64_t(std::llround(center)) - kFftSize / 2;
        for (int i = 0; i < kFftSize; ++i) {
            const std::int64_t at = start + i;
            float sample = 0.0f;
            if (at >= 0 && std::uint64_t(at) < frames) {
                const std::size_t base = std::size_t(at) * channels;
                for (std::uint32_t c = 0; c < channels; ++c) sample += pcm[base + c];
                sample /= float(channels);
            }
            re[std::size_t(i)] = sample * window[std::size_t(i)];
            im[std::size_t(i)] = 0.0f;
        }
        fft(re, im);

        for (int b = 0; b < bins; ++b) {
            const double loHz =
                kMinHz * std::exp(logSpan * double(b) / double(bins));
            const double hiHz =
                kMinHz * std::exp(logSpan * double(b + 1) / double(bins));
            int lo = int(std::floor(loHz * kFftSize / double(sampleRate)));
            int hi = int(std::ceil(hiHz * kFftSize / double(sampleRate)));
            lo = std::max(1, std::min(lo, kFftSize / 2 - 1));
            hi = std::max(lo + 1, std::min(hi, kFftSize / 2));
            float peak = 0.0f;
            for (int k = lo; k < hi; ++k) {
                const float mag = std::sqrt(re[std::size_t(k)] * re[std::size_t(k)] +
                                            im[std::size_t(k)] * im[std::size_t(k)]);
                peak = std::max(peak, mag);
            }
            // Hann sum = N/2; a full-scale sine peak gives |X| ~= N/4.
            const float amplitude = 4.0f * peak / float(kFftSize);
            const float db = 20.0f * std::log10(std::max(amplitude, 1e-9f));
            const int mapped = int(std::lround((db + 80.0f) / 80.0f * 127.0f));
            out.data[std::size_t(col) * bins + b] =
                std::uint8_t(std::max(0, std::min(127, mapped)));
        }
    }
    return out;
}

} // namespace vd
