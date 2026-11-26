#pragma once

#include "core/engine/DriftPath.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

struct IAudioClient;
struct IAudioCaptureClient;

namespace vd {

enum class CaptureState : std::uint32_t {
    Idle = 0,
    Starting = 1,
    Active = 2,
    Silent = 3,
    Stopping = 4,
    Error = 5,
};

enum class CaptureError : std::uint32_t {
    None = 0,
    ProcessClosed = 1,
    ActivationFailed = 2,
    DeviceInvalidated = 3,
    ReadFailed = 4,
};

// One process-loopback source with a stable address for the lifetime of the
// manager. The audio callback only touches atomics and the SPSC ring; all
// lifecycle operations stay on the UI/control thread.
class ProcessAudioCapture {
public:
    static constexpr std::size_t kMaxFrames = 2048;
    static constexpr std::size_t kRingSamples = std::size_t(1) << 15; // ~341 ms stereo

    ProcessAudioCapture();
    ~ProcessAudioCapture();

    ProcessAudioCapture(const ProcessAudioCapture&) = delete;
    ProcessAudioCapture& operator=(const ProcessAudioCapture&) = delete;

    bool start(std::uint32_t pid, std::string& err);
    void requestStop();
    void setInactive();
    void stopAndJoin();
    bool audioBusy() const { return audioBusy_.load(std::memory_order_acquire); }

    void setGain(float gain);
    float gain() const { return targetGain_.load(std::memory_order_relaxed); }
    CaptureState state() const {
        return static_cast<CaptureState>(state_.load(std::memory_order_relaxed));
    }
    CaptureError error() const {
        return static_cast<CaptureError>(error_.load(std::memory_order_relaxed));
    }
    std::uint32_t pid() const { return pid_.load(std::memory_order_relaxed); }
    float level() const { return level_.load(std::memory_order_relaxed); }
    std::uint64_t capturedFrames() const {
        return capturedFrames_.load(std::memory_order_relaxed);
    }
    std::uint64_t silentFrames() const {
        return silentFrames_.load(std::memory_order_relaxed);
    }

    // Audio thread. Lock-free: atomics + per-source SPSC/DriftPath only.
    void mixInto(float* out, std::size_t frames);

private:
    void threadMain();
    void releaseCom();

    static constexpr std::size_t kChannels = 2;
    static constexpr std::size_t kHighWaterFrames = 9600; // 200 ms

    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    HANDLE wasapiEvent_ = nullptr;
    HANDLE processHandle_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioCaptureClient* captureClient_ = nullptr;

    RingBuffer ring_{kRingSamples};
    DriftPath drift_{kMaxFrames, 1200}; // ~25 ms target watermark
    float block_[kMaxFrames * kChannels] = {};

    std::atomic<std::uint32_t> pid_{0};
    std::atomic<std::uint32_t> state_{static_cast<std::uint32_t>(CaptureState::Idle)};
    std::atomic<std::uint32_t> error_{static_cast<std::uint32_t>(CaptureError::None)};
    std::atomic<bool> active_{false};
    std::atomic<bool> audioBusy_{false};
    std::atomic<float> targetGain_{1.0f};
    std::atomic<float> level_{0.0f};
    std::atomic<std::uint64_t> capturedFrames_{0};
    std::atomic<std::uint64_t> silentFrames_{0};

    // Audio-thread-only smoothing state.
    float smoothedGain_ = 0.0f;
    float edgeFade_ = 0.0f;
};

} // namespace vd
