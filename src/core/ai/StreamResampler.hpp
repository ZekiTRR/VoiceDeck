#pragma once

// Streaming mono resampler: a polyphase windowed-sinc FIR bank (24 taps,
// 256 phases) evaluated at a fractional input/output rate. It replaces the
// old 4-point Catmull-Rom interpolator: the anti-alias cutoff follows the
// conversion ratio (48k->16k gets a proper low-pass instead of aliasing),
// and the phase table is rebuilt only when the ratio changes.
//
// All allocations happen in the constructor / configure step on the worker
// thread; process() is fixed-allocation and safe for the inference worker.

#include <algorithm>
#include <cmath>
#include <vector>

namespace vd {

class StreamResampler {
public:
    static constexpr int kTaps = 24;
    static constexpr int kPhases = 256;

    explicit StreamResampler(int maxInputBlock)
        : work_(std::size_t(std::max(1, maxInputBlock)) + kTaps) {}

    int maxInputBlock() const { return int(work_.size()) - kTaps; }

    // Upper bound on output samples for `inputLength` input samples.
    static int MaxOutputLength(int inputLength, double step) {
        return int(std::ceil(double(inputLength) / step)) + 2;
    }

    // step = input samples consumed per output sample (inputRate/outputRate).
    // Returns the number of samples written to `output` (capacity assumed
    // >= MaxOutputLength(inputLength, step)).
    int process(const float* input, int inputLength, float* output, int outputCapacity,
                double step) {
        if (step <= 0.0) return 0;
        rebuildIfNeeded(step);
        int written = 0;
        int pos = 0;
        while (pos < inputLength) {
            const int chunk = std::min(inputLength - pos, maxInputBlock());
            written += processChunk(input + pos, chunk, output + written,
                                    outputCapacity - written, step);
            pos += chunk;
        }
        return written;
    }

    void reset() {
        std::fill(work_.begin(), work_.end(), 0.0f);
        position_ = double(kTaps - (kTaps / 2 - 1));
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void rebuildIfNeeded(double step) {
        if (std::fabs(step - lastStep_) < 1e-9) return;
        lastStep_ = step;
        coeff_.resize(std::size_t(kPhases * kTaps));
        // Keep the passband below the lower of the two Nyquist limits.
        const double cutoff = std::min(1.0, 0.95 / std::max(step, 1e-6));
        for (int p = 0; p < kPhases; ++p) {
            const double frac = double(p) / double(kPhases);
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k) {
                const double x = double(k - (kTaps / 2 - 1)) - frac;
                const double sinc =
                    std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * cutoff * x) / (kPi * x);
                // Hann window over the tap span.
                const double w =
                    0.5 - 0.5 * std::cos(2.0 * kPi * (double(k) + 0.5) / double(kTaps));
                const double c = cutoff * sinc * w;
                coeff_[std::size_t(p * kTaps + k)] = float(c);
                sum += c;
            }
            // Normalize each phase to unity DC gain.
            if (std::fabs(sum) > 1e-12) {
                const float inv = float(1.0 / sum);
                for (int k = 0; k < kTaps; ++k)
                    coeff_[std::size_t(p * kTaps + k)] *= inv;
            }
        }
    }

    int processChunk(const float* chunk, int chunkLength, float* output, int outputCapacity,
                     double step) {
        if (chunkLength <= 0 || outputCapacity <= 0) return 0;
        std::copy(chunk, chunk + chunkLength, work_.begin() + kTaps);
        const int length = kTaps + chunkLength;

        double p = position_;
        int o = 0;
        constexpr int half = kTaps / 2;
        while (o < outputCapacity) {
            const int i = int(p);
            const int first = i - (half - 1);
            if (first < 0 || i + half >= length) break;
            double frac = p - double(i);
            int phase = int(frac * double(kPhases));
            if (phase < 0) phase = 0;
            if (phase >= kPhases) phase = kPhases - 1;
            const float* c = coeff_.data() + std::size_t(phase * kTaps);
            const float* x = work_.data() + first;
            float sum = 0.0f;
            for (int k = 0; k < kTaps; ++k) sum += c[k] * x[k];
            output[o++] = sum;
            p += step;
        }

        // Retain the last kTaps samples as history for the next chunk.
        std::copy(work_.begin() + (length - kTaps), work_.begin() + length, work_.begin());
        position_ = p - double(length - kTaps);
        if (position_ < double(half - 1)) position_ = double(half - 1);
        return o;
    }

    std::vector<float> work_;
    std::vector<float> coeff_;
    double position_ = double(kTaps - (kTaps / 2 - 1));
    double lastStep_ = -1.0;
};

} // namespace vd
