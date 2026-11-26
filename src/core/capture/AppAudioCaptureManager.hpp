#pragma once

#include "core/capture/ProcessAudioCapture.hpp"

#include "nlohmann/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace vd {

struct CaptureProcessInfo {
    std::uint32_t pid = 0;
    std::string name;
    std::string title;
    bool audible = false;
};

// Owns the fixed pool of process-loopback sources and publishes UI snapshots.
// Lifecycle methods are called from the UI thread. mixInto() is audio-thread
// only and never takes metaMutex_ or any other lock.
class AppAudioCaptureManager {
public:
    static constexpr std::size_t kMaxSources = 8;

    AppAudioCaptureManager() = default;
    ~AppAudioCaptureManager();

    AppAudioCaptureManager(const AppAudioCaptureManager&) = delete;
    AppAudioCaptureManager& operator=(const AppAudioCaptureManager&) = delete;

    void initialize();
    void shutdown();

    bool supported() const { return supported_; }
    const std::string& unsupportedReason() const { return unsupportedReason_; }

    void refreshProcesses();
    const std::vector<CaptureProcessInfo>& processes() const { return processes_; }
    nlohmann::json processesJson() const;

    bool add(std::uint32_t pid, std::string& err);
    void remove(std::size_t slot);
    bool reapFinished();
    void setGain(std::size_t slot, float gain);

    nlohmann::json sourcesJson() const;
    nlohmann::json metersJson() const;

    // Audio callback. No allocations, no locks, no COM.
    void mixInto(float* out, std::size_t frames);

private:
    struct SlotMeta {
        std::uint32_t pid = 0;
        std::string name;
        std::string title;
    };

    std::size_t findFreeSlot() const;
    static void enumerateProcesses(std::vector<CaptureProcessInfo>& out);

    std::array<ProcessAudioCapture, kMaxSources> slots_;
    std::array<SlotMeta, kMaxSources> meta_;
    mutable std::mutex metaMutex_;

    std::vector<CaptureProcessInfo> processes_;
    bool supported_ = false;
    std::string unsupportedReason_;
};

} // namespace vd
