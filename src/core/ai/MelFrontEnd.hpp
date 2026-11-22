#pragma once

// Port of Voice CTX's MelFrontEnd.cs + PitchMath.cs (MIT).
//
// Log-mel spectrogram matching RMVPE's training front end (librosa: centered
// reflect-padded STFT magnitude, periodic Hann, HTK mel scale with Slaney
// area normalisation, natural log clamped at 1e-5). Output is laid out
// [mel, frame] with frames zero-padded to a multiple of 32.

#include "core/ai/Fft.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vd {

class MelFrontEnd {
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kFftSize = 1024;
    static constexpr int kHop = 160;
    static constexpr int kMels = 128;
    static constexpr double kFMin = 30.0;
    static constexpr double kFMax = 8000.0;
    static constexpr int kBins = kFftSize / 2 + 1;

    MelFrontEnd() : fft_(kFftSize) {
        window_.resize(kFftSize);
        for (int n = 0; n < kFftSize; ++n)
            window_[std::size_t(n)] = 0.5 - 0.5 * std::cos(2.0 * kPi * n / kFftSize);
        basis_.resize(std::size_t(kMels * kBins));
        basisStart_.resize(std::size_t(kMels));
        basisEnd_.resize(std::size_t(kMels));
        re_.resize(std::size_t(kFftSize));
        im_.resize(std::size_t(kFftSize));
        magnitude_.resize(std::size_t(kBins));
        buildBasis();
    }

    static int frameCount(int samples) { return 1 + samples / kHop; }

    static int paddedFrameCount(int frames) { return 32 * ((frames - 1) / 32 + 1); }

    // audio: 16 kHz mono samples; mel: kMels x paddedFrameCount(frameCount(n)).
    void compute(const float* audio, int audioSamples, float* mel) {
        const int frames = frameCount(audioSamples);
        const int padded = paddedFrameCount(frames);
        std::fill(mel, mel + std::size_t(kMels * padded), 0.0f);

        const int half = kFftSize / 2;
        for (int t = 0; t < frames; ++t) {
            const int origin = t * kHop - half;
            for (int n = 0; n < kFftSize; ++n) {
                re_[std::size_t(n)] = reflect(audio, audioSamples, origin + n) * window_[std::size_t(n)];
                im_[std::size_t(n)] = 0.0;
            }
            fft_.forward(re_.data(), im_.data());
            for (int k = 0; k < kBins; ++k)
                magnitude_[std::size_t(k)] =
                    float(std::sqrt(re_[std::size_t(k)] * re_[std::size_t(k)] +
                                    im_[std::size_t(k)] * im_[std::size_t(k)]));

            for (int m = 0; m < kMels; ++m) {
                double sum = 0.0;
                const int row = m * kBins;
                for (int k = basisStart_[std::size_t(m)]; k < basisEnd_[std::size_t(m)]; ++k)
                    sum += double(basis_[std::size_t(row + k)]) * double(magnitude_[std::size_t(k)]);
                mel[std::size_t(m * padded + t)] = float(std::log(std::max(sum, 1e-5)));
            }
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    static float reflect(const float* x, int n, int i) {
        if (n == 1) return x[0];
        const int period = 2 * (n - 1);
        i %= period;
        if (i < 0) i += period;
        return i < n ? x[i] : x[period - i];
    }

    void buildBasis() {
        auto hzToMel = [](double f) { return 2595.0 * std::log10(1.0 + f / 700.0); };
        auto melToHz = [](double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); };

        std::vector<double> melF(std::size_t(kMels + 2));
        const double lo = hzToMel(kFMin);
        const double hi = hzToMel(kFMax);
        for (int i = 0; i < kMels + 2; ++i)
            melF[std::size_t(i)] = melToHz(lo + (hi - lo) * i / (kMels + 1));

        for (int m = 0; m < kMels; ++m) {
            const double lower = melF[std::size_t(m)];
            const double center = melF[std::size_t(m + 1)];
            const double upper = melF[std::size_t(m + 2)];
            const double norm = 2.0 / (upper - lower);
            int first = kBins;
            int last = 0;
            for (int k = 0; k < kBins; ++k) {
                const double f = double(k) * kSampleRate / kFftSize;
                const double rising = (f - lower) / (center - lower);
                const double falling = (upper - f) / (upper - center);
                const double w = std::max(0.0, std::min(rising, falling)) * norm;
                basis_[std::size_t(m * kBins + k)] = float(w);
                if (w > 0) {
                    first = std::min(first, k);
                    last = k;
                }
            }
            basisStart_[std::size_t(m)] = first;
            basisEnd_[std::size_t(m)] = std::max(first, last + 1);
        }
    }

    Fft fft_;
    std::vector<double> window_;
    std::vector<float> basis_;
    std::vector<int> basisStart_;
    std::vector<int> basisEnd_;
    std::vector<double> re_;
    std::vector<double> im_;
    std::vector<float> magnitude_;
};

// RMVPE's 360-bin pitch salience -> Hz, and Hz -> RVC coarse pitch tokens.
namespace PitchMath {

constexpr int kSalienceBins = 360;
constexpr double kThreshold = 0.03;

// Local weighted average of +/-4 bins around the peak, in cents -> Hz
// (0 when unvoiced).
inline void DecodeSalience(const float* salience, int frames, float* f0) {
    for (int t = 0; t < frames; ++t) {
        const float* row = salience + std::size_t(t) * kSalienceBins;
        int peak = 0;
        float max = row[0];
        for (int c = 1; c < kSalienceBins; ++c)
            if (row[c] > max) {
                max = row[c];
                peak = c;
            }

        if (max <= kThreshold) {
            f0[t] = 0.0f;
            continue;
        }

        double weighted = 0.0;
        double weight = 0.0;
        for (int c = peak - 4; c <= peak + 4; ++c) {
            if (c < 0 || c >= kSalienceBins) continue;
            const double cents = 20.0 * c + 1997.3794084376191;
            weighted += double(row[c]) * cents;
            weight += double(row[c]);
        }
        f0[t] = float(10.0 * std::pow(2.0, weighted / weight / 1200.0));
    }
}

// RVC's 1..255 mel-scaled pitch token (1 = unvoiced).
inline long long Coarse(float f0) {
    // std::log is not constexpr on MSVC - computed per call (cheap).
    const double coarseLow = 1127.0 * std::log(1.0 + 50.0 / 700.0);
    const double coarseHigh = 1127.0 * std::log(1.0 + 1100.0 / 700.0);
    const double mel = 1127.0 * std::log(1.0 + double(f0) / 700.0);
    const double c = mel > 0 ? (mel - coarseLow) * 254.0 / (coarseHigh - coarseLow) + 1 : 1;
    return static_cast<long long>(std::clamp(std::round(c), 1.0, 255.0));
}

} // namespace PitchMath

} // namespace vd
