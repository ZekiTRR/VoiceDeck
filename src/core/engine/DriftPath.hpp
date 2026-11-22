#pragma once

#include "core/engine/SpscQueue.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace vd {

// Pulls a continuous stream of interleaved stereo float frames out of a ring
// with a tiny adaptive resample factor that compensates clock drift between
// two audio devices (the idea behind Voice CTX's "drift-corrected
// resampling": the physical mic and the virtual cable run off independent
// hardware clocks and drift by tens of ppm).
//
// The control loop keeps the total backlog (ring + staging) near a target
// fill and adjusts the input-advance step in [0.998 .. 1.002]. Audio-thread
// only.
class DriftPath {
public:
    static constexpr std::size_t kChannels = 2;

    DriftPath(std::size_t maxBlockFrames, std::size_t targetFillFrames)
        : stage_((maxBlockFrames + 16) * kChannels), targetFrames_(double(targetFillFrames)) {}

    void reset() {
        staged_ = 0;
        pos_ = 0.0;
        avgFill_ = 0.0;
        step_ = 1.0;
    }

    // Produces up to `frames` frames into dst; returns the count produced
    // (less than requested on underrun - callers fill the rest with silence).
    std::size_t pull(RingBuffer& src, float* dst, std::size_t frames) {
        // Top up staging from the ring.
        const std::size_t stageFrames = stage_.size() / kChannels;
        if (staged_ < stageFrames) {
            const std::size_t got =
                src.read(stage_.data() + staged_ * kChannels, (stageFrames - staged_) * kChannels) / kChannels;
            staged_ += got;
        }

        // Slow control loop: keep total backlog near target. If we consume
        // too fast (backlog falling) step_ drops below 1; too slow -> above 1.
        const double fill = double(src.available() / kChannels + staged_);
        avgFill_ += (fill - avgFill_) * 0.05;
        const double err = (avgFill_ - targetFrames_) / targetFrames_;
        const double clamped = std::max(-1.5, std::min(1.5, err));
        step_ = std::max(0.998, std::min(1.002, 1.0 + clamped * 0.001));

        // Produce output, linearly interpolated at the adaptive rate.
        std::size_t out = 0;
        while (out < frames) {
            const std::size_t i0 = std::size_t(pos_);
            if (i0 + 1 >= staged_) break;
            const double f = pos_ - double(i0);
            const float* a = stage_.data() + i0 * kChannels;
            float* o = dst + out * kChannels;
            o[0] = float(a[0] + (a[2] - a[0]) * f);
            o[1] = float(a[1] + (a[3] - a[1]) * f);
            pos_ += step_;
            ++out;
        }

        // Retire consumed input frames.
        const std::size_t consumed = std::min<std::size_t>(std::size_t(pos_), staged_);
        if (consumed > 0) {
            const std::size_t rest = staged_ - consumed;
            if (rest > 0) {
                std::memmove(stage_.data(), stage_.data() + consumed * kChannels,
                             rest * kChannels * sizeof(float));
            }
            staged_ = rest;
            pos_ -= double(consumed);
        }
        return out;
    }

private:
    std::vector<float> stage_;
    std::size_t staged_ = 0;
    double pos_ = 0.0;
    double avgFill_ = 0.0;
    double step_ = 1.0;
    double targetFrames_ = 1.0;
};

} // namespace vd
