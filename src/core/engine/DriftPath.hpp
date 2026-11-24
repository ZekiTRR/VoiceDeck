#pragma once

#include "core/engine/SpscQueue.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vd {

// Pulls a continuous stream of interleaved stereo float frames out of a ring
// with a tiny adaptive resample factor that compensates clock drift between
// two audio devices (the physical mic and the virtual cable run off
// independent hardware clocks and drift by tens of ppm).
//
// The control loop keeps the total backlog (ring + circular staging) near a
// target fill and adjusts the input-advance step in [0.997 .. 1.003].
// Staging is a true circular buffer: no memmove of the remaining samples on
// every callback. Audio-thread only except for the atomic getters/setters.
class DriftPath {
public:
    static constexpr std::size_t kChannels = 2;

    DriftPath(std::size_t maxBlockFrames, std::size_t targetFillFrames)
        : capFrames_(maxBlockFrames * 2 + 16),
          stage_(capFrames_ * kChannels),
          targetFrames_(double(std::max<std::size_t>(1, targetFillFrames))) {}

    void reset() {
        head_ = 0;
        count_ = 0;
        pos_ = 0.0;
        avgFill_.store(0.0, std::memory_order_relaxed);
        minFill_.store(1e9, std::memory_order_relaxed);
        maxFill_.store(0.0, std::memory_order_relaxed);
        feedForward_.store(1.0, std::memory_order_relaxed);
        step_.store(1.0, std::memory_order_relaxed);
        framesSinceUnderrun_ = 0;
    }

    // Control thread, while the audio device is stopped. The engine picks
    // 1.5..2.0x of max(capture period, playback period) after opening the
    // devices, so capture batches and playback pulls cannot starve each other.
    void setTargetFrames(double frames) {
        const double base = std::max(1.0, frames);
        baseTarget_.store(base, std::memory_order_relaxed);
        // Allow the jitter buffer to grow if this driver delivers callbacks in
        // bursts; 4x the base (up to ~85 ms) is the safety ceiling.
        maxTarget_.store(std::min(4096.0, std::max(2048.0, base * 2.0)),
                         std::memory_order_relaxed);
        targetFrames_.store(base, std::memory_order_relaxed);
        framesSinceUnderrun_ = 0;
    }
    double targetFrames() const { return targetFrames_.load(std::memory_order_relaxed); }

    // Audio thread: feed-forward clock ratio (input frames per output frame)
    // measured from the capture/playback callback counters. This learns the
    // virtual cable's clock offset in a fraction of a second instead of
    // waiting for the fill error to build up.
    void setFeedForward(double inputPerOutput) {
        feedForward_.store(std::max(0.85, std::min(1.15, inputPerOutput)),
                           std::memory_order_relaxed);
    }

    // Control/UI threads: smoothed backlog (ring + staging) in frames.
    double averageFillFrames() const { return avgFill_.load(std::memory_order_relaxed); }
    // Debug/diagnostics: backlog measured at the start of the last pull.
    double lastFillFrames() const { return lastFill_.load(std::memory_order_relaxed); }
    double minFillFrames() const { return minFill_.load(std::memory_order_relaxed); }
    double maxFillFrames() const { return maxFill_.load(std::memory_order_relaxed); }
    double step() const { return step_.load(std::memory_order_relaxed); }

    // Produces up to `frames` frames into dst; returns the count produced
    // (less than requested on underrun - callers fill the rest with silence).
    std::size_t pull(RingBuffer& src, float* dst, std::size_t frames) {
        topUp(src);

        // Keep the startup headroom: the controller drains it smoothly while
        // the feed-forward ratio is still converging.
        double fill = double(src.available() / kChannels + count_);
        const double target = targetFrames_.load(std::memory_order_relaxed);
        lastFill_.store(fill, std::memory_order_relaxed);
        if (fill < minFill_.load(std::memory_order_relaxed))
            minFill_.store(fill, std::memory_order_relaxed);
        if (fill > maxFill_.load(std::memory_order_relaxed))
            maxFill_.store(fill, std::memory_order_relaxed);
        const double avg = avgFill_.load(std::memory_order_relaxed);
        const double nextAvg = avg + (fill - avg) * 0.05;
        avgFill_.store(nextAvg, std::memory_order_relaxed);
        const double err = (nextAvg - target) / target;

        // Feed-forward clock ratio + proportional trim. The interpolation
        // step IS the clock-rate ratio: when the virtual cable runs ~5% fast,
        // the counters supply step~0.95 within ~0.3 s and the final pitch is
        // correct. The proportional part only absorbs jitter/backlog error.
        const double ff = feedForward_.load(std::memory_order_relaxed);
        // Asymmetric limits: refilling can be fast (0.90), but draining the
        // startup headroom must stay gentle (<=1.02) or the controller
        // overshoots into an underrun before the feed-forward ratio settles.
        step_.store(std::max(0.90, std::min(1.02, ff + err * 0.05)),
                    std::memory_order_relaxed);

        // Produce output, linearly interpolated at the adaptive rate.
        std::size_t out = 0;
        while (out < frames) {
            const std::size_t i0 = std::size_t(pos_);
            if (i0 + 1 >= count_) break;
            const float* a = framePtr(i0);
            const float* b = framePtr(i0 + 1);
            const float f = float(pos_ - double(i0));
            float* o = dst + out * kChannels;
            o[0] = a[0] + (b[0] - a[0]) * f;
            o[1] = a[1] + (b[1] - a[1]) * f;
            pos_ += step_;
            ++out;
        }

        // Retire consumed input frames by advancing the ring head.
        const std::size_t consumed = std::min<std::size_t>(std::size_t(pos_), count_);
        if (consumed > 0) {
            head_ += consumed;
            if (head_ >= capFrames_) head_ %= capFrames_;
            count_ -= consumed;
            pos_ -= double(consumed);
        }

        // Adaptive jitter buffer: a bursty virtual-cable driver can drain the
        // ring faster than any clock controller can react. Grow on an
        // underrun, then shrink slowly after 10 s of stability.
        if (out < frames) {
            framesSinceUnderrun_ = 0;
            // A bursty driver needs a larger jitter buffer. Grow on any
            // underrun, then shrink after 5 s of stability.
            const double t = targetFrames_.load(std::memory_order_relaxed);
            targetFrames_.store(std::min(maxTarget_.load(std::memory_order_relaxed),
                                         t * 1.5),
                                std::memory_order_relaxed);
        } else {
            framesSinceUnderrun_ += frames;
            if (framesSinceUnderrun_ > 48000 * 5) {
                framesSinceUnderrun_ = 0;
                const double t = targetFrames_.load(std::memory_order_relaxed);
                const double base = baseTarget_.load(std::memory_order_relaxed);
                if (t > base)
                    targetFrames_.store(std::max(base, t * 0.95),
                                        std::memory_order_relaxed);
            }
        }
        return out;
    }

private:
    // Fill the circular staging buffer from the SPSC ring without moving
    // existing samples. The free region may wrap around the end.
    void topUp(RingBuffer& src) {
        if (count_ >= capFrames_) return;
        std::size_t freeFrames = capFrames_ - count_;
        std::size_t tail = head_ + count_;
        if (tail >= capFrames_) tail -= capFrames_;
        const std::size_t first = std::min(freeFrames, capFrames_ - tail);
        std::size_t got = src.read(stage_.data() + tail * kChannels, first * kChannels) / kChannels;
        count_ += got;
        if (got < first) return;
        const std::size_t second = freeFrames - first;
        if (second > 0) {
            got = src.read(stage_.data(), second * kChannels) / kChannels;
            count_ += got;
        }
    }

    const float* framePtr(std::size_t index) const {
        std::size_t p = head_ + index;
        if (p >= capFrames_) p -= capFrames_;
        return stage_.data() + p * kChannels;
    }

    const std::size_t capFrames_;
    std::vector<float> stage_;
    std::size_t head_ = 0;  // oldest frame in the circular staging buffer
    std::size_t count_ = 0; // frames currently staged
    double pos_ = 0.0;      // fractional read offset from head_
    std::atomic<double> avgFill_{0.0};
    std::atomic<double> lastFill_{0.0};
    std::atomic<double> minFill_{1e9};
    std::atomic<double> maxFill_{0.0};
    std::atomic<double> targetFrames_;
    std::atomic<double> baseTarget_{1.0};
    std::atomic<double> maxTarget_{4096.0};
    std::atomic<double> feedForward_{1.0};
    std::atomic<double> step_{1.0};
    std::uint64_t framesSinceUnderrun_ = 0;
};

} // namespace vd
