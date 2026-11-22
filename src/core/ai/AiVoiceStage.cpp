#include "core/ai/AiVoiceStage.hpp"

#include "core/util/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vd {

AiVoiceStage::AiVoiceStage(int sampleRate, int maxBlockFrames, Controls& controls,
                           AiVoiceService& service)
    : rate_(sampleRate), controls_(controls), service_(service) {
    wet_.resize(std::size_t(maxBlockFrames));
    monoIn_.resize(std::size_t(maxBlockFrames));
    monoDry_.resize(std::size_t(maxBlockFrames));
    state_ = std::make_unique<WorkerState>(rate_);
    worker_ = std::thread([this] { workerLoop(); });
}

AiVoiceStage::~AiVoiceStage() {
    running_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(sigMtx_);
        sigCv_.notify_all();
    }
    if (worker_.joinable()) worker_.join();
}

// ---------------------------------------------------------------------------
// Audio-thread side
// ---------------------------------------------------------------------------

void AiVoiceStage::processStereo(const float* in, float* out, std::size_t frames) {
    for (std::size_t i = 0; i < frames; ++i)
        monoIn_[i] = (in[i * 2] + in[i * 2 + 1]) * 0.5f;
    std::memcpy(monoDry_.data(), monoIn_.data(), frames * sizeof(float));

    process(monoIn_.data(), frames);

    // Expand the mono delta back to both channels.
    for (std::size_t i = 0; i < frames; ++i) {
        const float d = monoIn_[i] - monoDry_[i];
        out[i * 2] = in[i * 2] + d;
        out[i * 2 + 1] = in[i * 2 + 1] + d;
    }
}

void AiVoiceStage::process(float* mono, std::size_t frames) {
    const bool want = controls_.enabled.load(std::memory_order_relaxed) && service_.isReady();
    if (!want && !active_ && wetGain_ <= 0.0f) return;

    if (want && !active_) {
        active_ = true;
        primed_ = false;
        flapCount_.store(0, std::memory_order_relaxed);
        flapWindowStart_ = clock_;
        output_.discard(output_.available());
        resetRequested_.store(true, std::memory_order_relaxed);
    }

    if (active_) {
        input_.write(mono, frames);
        sigCv_.notify_one(); // lock-free nudge; 50 ms poll catches the rest
        quietSamples_ = isQuiet(mono, frames) ? quietSamples_ + std::int64_t(frames) : 0;
    }

    std::fill(wet_.begin(), wet_.begin() + std::ptrdiff_t(frames), 0.0f);
    if (active_) {
        std::size_t available = output_.available();
        if (clock_ < holdUntil_) {
            // Recovering from an underrun: stay on the dry voice, discard stale output.
            output_.discard(available);
        } else {
            const int prime = primeSamples_.load(std::memory_order_relaxed);
            const int maxBuf = maxBuffered_.load(std::memory_order_relaxed);
            if (!primed_ && prime != std::numeric_limits<int>::max() && available >= std::size_t(prime)) {
                // Start exactly at the prime level so the buffered delay is the
                // minimum that still absorbs inference jitter.
                output_.discard(available - std::size_t(prime));
                primed_ = true;
                available = std::size_t(prime);
            }
            if (primed_) {
                if (available > std::size_t(maxBuf) && maxBuf > 0)
                    output_.discard(available - std::size_t(maxBuf > prime ? prime : maxBuf));
                const std::size_t read = output_.read(wet_.data(), frames);
                if (read < frames) onUnderrun();
            }
        }
    }
    clock_ += std::int64_t(frames);

    // Wet/dry crossfade, speed from the live fadeMs setting.
    const int fadeMs = std::max(1, controls_.fadeMs.load(std::memory_order_relaxed));
    const float gainStep = 1.0f / float(double(rate_) * double(fadeMs) * 0.001);
    const float target = (want && primed_) ? 1.0f : 0.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        wetGain_ = (target > wetGain_) ? std::min(target, wetGain_ + gainStep)
                                       : std::max(target, wetGain_ - gainStep);
        mono[i] += wetGain_ * (wet_[i] - mono[i]);
    }

    if (!want && wetGain_ <= 0.0f && active_) {
        active_ = false;
        primed_ = false;
    }
}

void AiVoiceStage::onUnderrun() {
    underruns_.fetch_add(1, std::memory_order_relaxed);
    primed_ = false;
    // Nothing audible is being converted: just re-prime so the next line
    // spoken isn't left in the dry voice.
    if (quietSamples_ >= std::int64_t(double(rate_) * kQuietSeconds)) return;
    const bool recent = (clock_ - lastUnderrun_) < std::int64_t(double(rate_) * 5.0);
    lastUnderrun_ = clock_;
    holdUntil_ = clock_ + std::int64_t(double(rate_) * (recent ? kLongHoldSeconds : kShortHoldSeconds));

    // Auto-off: count audible dropouts (the mic was live when the stage ran
    // dry). If they flap often, the GPU can't sustain this voice - stop it
    // instead of an audible wet/dry stutter. The worker thread logs it once.
    if (clock_ - flapWindowStart_ > std::int64_t(double(rate_) * 30.0)) {
        flapWindowStart_ = clock_;
        flapCount_.store(0, std::memory_order_relaxed);
    }
    if (controls_.autoLimit.load(std::memory_order_relaxed) &&
        flapCount_.fetch_add(1, std::memory_order_relaxed) + 1 >=
            std::max(1, controls_.flapLimit.load(std::memory_order_relaxed))) {
        controls_.enabled.store(false, std::memory_order_relaxed);
        limitedByDropouts_.store(true, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void AiVoiceStage::workerLoop() {
    std::vector<float> chunk(std::size_t(rate_ / 10));
    while (running_.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lk(sigMtx_);
            sigCv_.wait_for(lk, std::chrono::milliseconds(50),
                            [this] { return !running_.load(std::memory_order_relaxed); });
        }
        if (!running_.load(std::memory_order_relaxed)) break;

        if (resetRequested_.exchange(false, std::memory_order_relaxed)) {
            input_.discard(input_.available());
            state_->reset();
        }

        ConverterShape shape;
        if (!service_.currentShape(shape)) {
            input_.discard(input_.available());
            continue;
        }
        if (state_->configure(shape)) {
            const int blockDeviceSamples =
                int((std::int64_t(shape.blockSamples16k) * rate_) / kWorkerSampleRate);
            marginMs_.store(float(1000.0 * shape.marginSamples / shape.outputSampleRate),
                            std::memory_order_relaxed);
            // Prime one block plus a live-tunable jitter margin; cap at three
            // blocks total to bound the delay.
            const int margin =
                std::max(0, controls_.primeMarginMs.load(std::memory_order_relaxed));
            const int prime =
                blockDeviceSamples + int(double(rate_) * double(margin) * 0.001);
            primeSamples_.store(prime, std::memory_order_relaxed);
            maxBuffered_.store(prime + blockDeviceSamples * 2, std::memory_order_relaxed);
        }

        std::size_t read;
        while ((read = input_.read(chunk.data(), chunk.size())) > 0) {
            state_->pushInput(chunk.data(), int(read));
            while (state_->hasBlock()) {
                const float shift = controls_.pitchShift.load(std::memory_order_relaxed);
                const auto t0 = std::chrono::steady_clock::now();
                if (!state_->convertBlock(service_, shift)) break;
                const double dt =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                        .count();
                convMs_ = (convCount_ == 0) ? dt : convMs_ * 0.9 + dt * 0.1;
                ++convCount_;
                if (convCount_ % 50 == 0)
                    log::info("AI inference: %.0f ms avg per 100 ms block", convMs_);
                output_.write(state_->deviceOutput(), std::size_t(state_->deviceCount()));

                // The audio thread flipped the auto-off latch after audible
                // dropouts; report it once from this (non-audio) thread.
                if (limitedByDropouts_.exchange(false, std::memory_order_relaxed)) {
                    log::warn("AI voice switched off: repeated dropouts while speaking "
                              "(GPU can't sustain %.0f ms per 100 ms block) - your clean "
                              "voice continues; try 'ai margin +%d' or a faster GPU",
                              convMs_,
                              controls_.primeMarginMs.load(std::memory_order_relaxed) + 100);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// WorkerState
// ---------------------------------------------------------------------------

AiVoiceStage::WorkerState::WorkerState(int deviceRate) : deviceRate_(deviceRate) {}

bool AiVoiceStage::WorkerState::configure(const ConverterShape& shape) {
    if (shape == shape_) return false;
    shape_ = shape;
    const int maxIn = deviceRate_ / 10;
    toWorker_ = std::make_unique<StreamResampler>(maxIn);
    toDevice_ = std::make_unique<StreamResampler>(shape.blockOutputSamples());
    const int resampledLen = StreamResampler::MaxOutputLength(maxIn, double(deviceRate_) / kWorkerSampleRate) + 8;
    resampled_.assign(std::size_t(resampledLen), 0.0f);
    context_.assign(std::size_t(shape.contextSamples16k), 0.0f);
    pending_.assign(std::size_t(shape.blockSamples16k) * 4 + std::size_t(resampledLen), 0.0f);
    converted_.assign(std::size_t(shape.outputSamples), 0.0f);
    block_.assign(std::size_t(shape.blockOutputSamples()), 0.0f);
    tailMemory_.assign(std::size_t(shape.marginSamples), 0.0f);
    fadeIn_.assign(std::size_t(shape.marginSamples), 0.0f);
    for (std::size_t i = 0; i < fadeIn_.size(); ++i) {
        const double x = std::sin(3.14159265358979323846 / 2 * double(i) /
                                  double(std::max<std::size_t>(1, fadeIn_.size() - 1)));
        fadeIn_[i] = float(x * x);
    }
    const int deviceOutLen =
        StreamResampler::MaxOutputLength(shape.blockOutputSamples(),
                                         double(shape.outputSampleRate) / deviceRate_) + 8;
    deviceOut_.assign(std::size_t(deviceOutLen), 0.0f);
    reset();
    return true;
}

void AiVoiceStage::WorkerState::reset() {
    if (toWorker_) toWorker_->reset();
    if (toDevice_) toDevice_->reset();
    std::fill(context_.begin(), context_.end(), 0.0f);
    std::fill(tailMemory_.begin(), tailMemory_.end(), 0.0f);
    pendingCount_ = 0;
    hasPrevious_ = false;
}

void AiVoiceStage::WorkerState::pushInput(const float* samples, int count) {
    if (!toWorker_) return;
    const int produced = toWorker_->process(samples, count, resampled_.data(),
                                             int(resampled_.size()),
                                             double(deviceRate_) / kWorkerSampleRate);
    const int room = int(pending_.size()) - pendingCount_;
    if (produced > room) {
        // Inference fell far behind: drop the oldest audio rather than grow latency.
        const int drop = produced - room;
        std::memmove(pending_.data(), pending_.data() + drop,
                     std::size_t(pendingCount_ - drop) * sizeof(float));
        pendingCount_ -= drop;
    }
    std::memcpy(pending_.data() + pendingCount_, resampled_.data(), std::size_t(produced) * sizeof(float));
    pendingCount_ += produced;
}

bool AiVoiceStage::WorkerState::convertBlock(AiVoiceService& service, float pitchShift) {
    const int block = shape_.blockSamples16k;
    // Shift the context window: drop the oldest `block`, append the newest.
    std::memmove(context_.data(), context_.data() + block,
                 (context_.size() - std::size_t(block)) * sizeof(float));
    std::memcpy(context_.data() + context_.size() - std::size_t(block), pending_.data(),
                 std::size_t(block) * sizeof(float));
    std::memmove(pending_.data(), pending_.data() + block,
                 std::size_t(pendingCount_ - block) * sizeof(float));
    pendingCount_ -= block;

    ConverterShape got;
    if (!service.tryConvert(context_.data(), pitchShift, converted_.data(), got) ||
        !(got == shape_)) {
        deviceCount_ = 0;
        return false;
    }

    // Crossfade the overlap with the previous block's tail, then emit one block.
    const int margin = shape_.marginSamples;
    int emitted = 0;
    for (int i = 0; i < margin; ++i) {
        const float fresh = converted_[std::size_t(i)];
        block_[std::size_t(emitted++)] =
            hasPrevious_ ? tailMemory_[std::size_t(i)] +
                               fadeIn_[std::size_t(i)] * (fresh - tailMemory_[std::size_t(i)])
                         : fresh * fadeIn_[std::size_t(i)];
    }
    const int body = shape_.blockOutputSamples() - margin;
    std::memcpy(block_.data() + emitted, converted_.data() + margin, std::size_t(body) * sizeof(float));
    std::memcpy(tailMemory_.data(), converted_.data() + (shape_.outputSamples - margin),
                std::size_t(margin) * sizeof(float));
    hasPrevious_ = true;

    deviceCount_ = toDevice_->process(block_.data(), shape_.blockOutputSamples(), deviceOut_.data(),
                                      int(deviceOut_.size()),
                                      double(shape_.outputSampleRate) / deviceRate_);
    return true;
}

} // namespace vd
