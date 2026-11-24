#pragma once

#include "core/ai/RvcConverter.hpp"
#include "core/ai/VoiceLibrary.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace vd {

// Owns the loaded models (GPU memory) independently of the audio pipeline.
// The worker thread borrows the converter under `sync_`; voice switching
// takes the same lock. CLI status strings are guarded by a separate mutex.
class AiVoiceService {
public:
    explicit AiVoiceService(VoiceLibrary& library) : library_(library) {}
    ~AiVoiceService() { unload(); }

    bool isReady() const { return ready_.load(std::memory_order_relaxed); }

    // CLI thread. Chooses the inference backend for the next model load;
    // if a voice is currently loaded, it is reloaded on the new provider.
    void setProviderHint(AiProvider p);
    AiProvider providerHint() const { return hint_; }

    // CLI thread (blocking; may take seconds - model load + GPU warm-up).
    // "" on success, else an error message.
    std::string loadVoice(const AiVoiceInfo& voice);

    void unload();

    // Worker thread only. Returns false when no voice is ready.
    bool tryConvert(const float* context16k, float pitchShift, float* output, ConverterShape& shape,
                    bool lowPriority = false);
    // FX Send yields to queued microphone inference so a continuous soundboard
    // cannot starve the live voice on a shared GPU/converter.
    void setMicrophonePriority(bool active) {
        microphonePriority_.store(active, std::memory_order_release);
    }
    bool microphonePriority() const { return microphonePriority_.load(std::memory_order_acquire); }

    // Worker thread only.
    bool currentShape(ConverterShape& out);

    // CLI status accessors.
    std::string statusMessage() const;
    std::string voiceName() const;
    std::string deviceName() const;
    bool usesGpu() const;

private:
    void setStatus(std::string msg);
    // Must be called with loadMtx_ held. Split out so setProviderHint() can
    // unload+reload atomically without deadlocking on a non-recursive mutex.
    std::string loadVoiceLocked(const AiVoiceInfo& voice);
    void unloadLocked();

    VoiceLibrary& library_;
    std::mutex loadMtx_; // serializes loadVoice/unload/setProviderHint
    std::mutex sync_;    // guards runtime_/converter_ against the worker thread
    std::unique_ptr<RvcRuntime> runtime_;
    std::unique_ptr<RvcConverter> converter_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> microphonePriority_{false};
    AiProvider hint_ = AiProvider::Auto;
    AiVoiceInfo lastVoice_;
    bool hasLastVoice_ = false;

    mutable std::mutex statusMtx_;
    std::string statusMessage_ = "no voice loaded";
    std::string voiceName_;
    std::string deviceName_ = "-";
    bool usesGpu_ = false;
};

} // namespace vd
