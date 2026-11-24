#pragma once

#include "core/ai/AiVoiceService.hpp"
#include "core/ai/StreamResampler.hpp"
#include "core/engine/SpscQueue.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace vd {

// Port of Voice CTX's AiVoiceStage.cs (MIT). Real-time bridge for the AI
// voice: the audio thread hands its block to a lock-free FIFO and reads
// converted audio from another, delayed by a fixed priming amount; a
// dedicated worker thread does resampling to 16 kHz, model inference, block
// crossfading and resampling back. The audio thread never waits on
// inference: if the worker falls behind, the stage fades back to the dry
// signal and re-primes instead of glitching.
class AiVoiceStage {
public:
    // Detailed, live-tunable settings (all lock-free; CLI writes, audio and
    // worker threads read).
    struct Controls {
        std::atomic<bool> enabled{false};
        std::atomic<float> pitchShift{0.0f}; // semitones
        std::atomic<int> primeMarginMs{100}; // extra buffered delay beyond one block
        std::atomic<int> fadeMs{30};        // wet/dry crossfade on (de)activation
        std::atomic<bool> autoLimit{true};  // auto-disable on audible dropouts
        std::atomic<int> flapLimit{8};     // ...after N dropouts per 30 s
        // VAD sleep: the voice branch is paused; the stage keeps a pre-roll
        // history and re-primes from it on wake.
        std::atomic<bool> sleeping{false};
    };

    AiVoiceStage(int sampleRate, int maxBlockFrames, Controls& controls, AiVoiceService& service,
                 bool lowPriority = false);
    ~AiVoiceStage();
    AiVoiceStage(const AiVoiceStage&) = delete;
    AiVoiceStage& operator=(const AiVoiceStage&) = delete;

    // Audio thread. in/out are interleaved stereo; the AI runs on the mono
    // mix and its delta is applied to both channels.
    void processStereo(const float* in, float* out, std::size_t frames);

    // Audio thread (VAD sleep): keep the pre-roll history fresh while the
    // voice branch is paused, without touching the inference worker.
    void updatePreRoll(const float* stereo, std::size_t frames);

    // Control thread: wake the worker after a model load/provider change.
    void notifyWorker();

    // Any thread.
    int underruns() const { return int(underruns_.load(std::memory_order_relaxed)); }
    bool fallingBehind() const { return active_ && clock_ < holdUntil_; }
    double latencyMs() const {
        const int prime = primeSamples_.load(std::memory_order_relaxed);
        if (prime == std::numeric_limits<int>::max()) return 0.0;
        return 1000.0 * double(prime) / double(rate_) +
               double(marginMs_.load(std::memory_order_relaxed));
    }
private:
    static constexpr int kWorkerSampleRate = 16000;
    static constexpr double kShortHoldSeconds = 2.0, kLongHoldSeconds = 5.0;
    static constexpr double kQuietSeconds = 0.2;
    static constexpr float kQuietPeak = 0.003f; // about -50 dBFS

    static bool isQuiet(const float* buf, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] > kQuietPeak || buf[i] < -kQuietPeak) return false;
        return true;
    }

    // Audio thread core (mono, in-place wet/dry crossfade).
    void process(float* mono, std::size_t frames);
    void onUnderrun();
    void pushPreRollMono(const float* mono, std::size_t frames);
    void seedContextFromPreRoll();

    // Worker thread.
    void workerLoop();

    // Worker-only buffers; reallocated only when the voice's shape changes.
    class WorkerState {
    public:
        explicit WorkerState(int deviceRate);

        bool configure(const ConverterShape& shape);
        void reset();
        void seedContext(const float* samples16k, int count);
        void pushInput(const float* samples, int count);
        bool hasBlock() const { return pendingCount_ >= shape_.blockSamples16k && shape_.blockSamples16k > 0; }
        bool convertBlock(AiVoiceService& service, float pitchShift, bool lowPriority);
        const float* deviceOutput() const { return deviceOut_.data(); }
        int deviceCount() const { return deviceCount_; }

    private:
        int deviceRate_ = 48000;
        ConverterShape shape_{};
        std::unique_ptr<StreamResampler> toWorker_;
        std::unique_ptr<StreamResampler> toDevice_;
        std::vector<float> resampled_;
        std::vector<float> context_;
        std::vector<float> candidateContext_;
        std::vector<float> pending_;
        int pendingCount_ = 0;
        std::vector<float> converted_;
        std::vector<float> block_;
        std::vector<float> fadeIn_;
        std::vector<float> tailMemory_;
        std::vector<float> deviceOut_;
        int deviceCount_ = 0;
        bool hasPrevious_ = false;
    };

    int rate_ = 48000;
    bool lowPriority_ = false;
    Controls& controls_;
    AiVoiceService& service_;

    SpscQueue<float> input_{std::size_t(1) << 17}; // ~2.7 s at 48k
    SpscQueue<float> output_{std::size_t(1) << 17};
    // Wake-up context (16 kHz) handed from the audio thread to the worker so
    // the first inference sees the pre-roll without a 600 ms backlog.
    SpscQueue<float> seed_{std::size_t(1) << 14};
    std::atomic<bool> seedRequested_{false};

    // Audio-thread state.
    float wetGain_ = 0.0f;
    bool active_ = false;
    bool primed_ = false;
    std::int64_t clock_ = 0;
    std::int64_t holdUntil_ = 0;
    std::int64_t lastUnderrun_ = std::numeric_limits<std::int64_t>::min() / 2;
    std::int64_t quietSamples_ = 0;
    std::atomic<int> underruns_{0};

    std::vector<float> wet_;
    std::vector<float> monoIn_;
    std::vector<float> monoDry_;
    // Last ~650 ms of the mono voice, recorded even while the VAD sleeps so
    // the AI can re-prime with the beginning of the first phrase.
    std::vector<float> preRoll_;
    std::size_t preRollPos_ = 0;
    bool preRollFull_ = false;
    bool audioSleeping_ = false;

    // Worker / cross-thread state. The audio thread only stores a flag and
    // notifies; the worker waits on the atomic (WaitOnAddress on Windows).
    std::unique_ptr<WorkerState> state_;
    std::thread worker_;
    std::atomic<bool> running_{true};
    std::atomic<bool> resetRequested_{false};
    std::atomic<int> primeSamples_{std::numeric_limits<int>::max()};
    std::atomic<int> maxBuffered_{0};
    std::atomic<float> marginMs_{0.0f};
    int blockDeviceSamples_ = 0; // worker-only: current voice block at 48 kHz
    int lastPrimeMarginMs_ = -1; // worker-only: force prime recompute on change
    std::atomic<std::uint32_t> wake_{0};

    // Worker diagnostics (EMA of conversion time).
    double convMs_ = 0.0;
    std::uint64_t convCount_ = 0;
    // Auto-off on audible dropouts (audio thread counts, worker logs once).
    std::atomic<int> flapCount_{0};
    std::atomic<bool> limitedByDropouts_{false};
    std::int64_t flapWindowStart_ = 0;
};

} // namespace vd
