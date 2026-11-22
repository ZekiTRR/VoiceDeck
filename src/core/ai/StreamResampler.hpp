#pragma once

// Port of Voice CTX's StreamResampler.cs (MIT): streaming mono resampler
// using 4-point Catmull-Rom interpolation. Fixed allocation; the ratio can
// change on every call.

#include <algorithm>
#include <cmath>
#include <vector>

namespace vd {

class StreamResampler {
public:
    explicit StreamResampler(int maxInputBlock)
        : work_(std::size_t(maxInputBlock) + kHistory) {}

    int maxInputBlock() const { return int(work_.size()) - kHistory; }

    // Upper bound on output samples for `inputLength` input samples.
    static int MaxOutputLength(int inputLength, double step) {
        return int(std::ceil(double(inputLength) / step)) + 2;
    }

    // step = input samples consumed per output sample (inputRate/outputRate).
    // Returns the number of samples written to `output` (capacity assumed
    // >= MaxOutputLength(inputLength, step)).
    int process(const float* input, int inputLength, float* output, int outputCapacity, double step) {
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
        position_ = 1.0;
    }

private:
    static constexpr int kHistory = 3;

    int processChunk(const float* chunk, int chunkLength, float* output, int outputCapacity,
                     double step) {
        std::copy(chunk, chunk + chunkLength, work_.begin() + kHistory);
        const int length = kHistory + chunkLength;
        double p = position_;
        int o = 0;
        while (o < outputCapacity) {
            const int i = int(p);
            if (i + 2 >= length) break;
            const float t = float(p - i);
            output[o++] = catmullRom(work_[std::size_t(i - 1)], work_[std::size_t(i)],
                                     work_[std::size_t(i + 1)], work_[std::size_t(i + 2)], t);
            p += step;
        }
        std::copy(work_.begin() + (length - kHistory), work_.begin() + length, work_.begin());
        // Clamp only matters if the caller's output was too small.
        position_ = std::max(1.0, p - (length - kHistory));
        return o;
    }

    static float catmullRom(float y0, float y1, float y2, float y3, float t) {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    std::vector<float> work_;
    double position_ = 1.0;
};

} // namespace vd
