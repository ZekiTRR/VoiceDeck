#pragma once

#include "core/engine/SpscQueue.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

namespace vd {

struct Command {
    enum class Type : std::uint8_t {
        PlaySound,
        StopSound,
        StopAll,
        SetSoundVolume, // value = linear gain (1.0 = 100%)
        SetSoundLoop,   // value != 0 -> loop next plays
        SetBypass,      // value != 0 -> mic passthrough on
        SetDucking,     // value != 0 -> duck soundboard under speech
        SetPreset,      // soundId = voice preset index
        SetDsp,         // value != 0 -> voice DSP chain on
        SetTransposeOverride, // value = semitones, soundId != 0 -> enable
        SetFormantOverride,   // value = semitones, soundId != 0 -> enable
        SetAi,          // value != 0 -> AI voice stage on
        SetAiPitch,     // value = semitones for the AI converter
        PauseSound,     // toggle pause for voices of soundId
        SeekSound,      // value = position fraction 0..1 for soundId
        SetSoundEq,     // value/valueB/valueC = bands 1..3 dB for soundId
        PreviewSound,   // play soundId to the monitor (original output) only
        StopPreview,    // stop all preview voices
    };
    Type type = Type::StopAll;
    std::uint64_t soundId = 0;
    float value = 0.0f;
    float valueB = 0.0f;
    float valueC = 0.0f;
    float valueD = 0.0f;
};

// Thread-safe command transport into the audio engine.
//
// Producers: CLI thread, hotkey/tray thread (submit() - short mutex on the
// inbox only). A dispatcher thread is the single producer of a lock-free SPSC
// ring; the WASAPI playback callback is its single consumer (popAudio()) and
// applies commands at block boundaries - no locks, no allocations inside the
// audio callback.
class CommandQueue {
public:
    ~CommandQueue() { stop(); }

    void start() {
        quit_.store(false, std::memory_order_relaxed);
        dispatcher_ = std::thread([this] { dispatchLoop(); });
    }

    void stop() {
        if (!dispatcher_.joinable()) return;
        quit_.store(true, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            cv_.notify_all();
        }
        dispatcher_.join();
    }

    // Any control thread.
    void submit(const Command& c) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            inbox_.push_back(c);
        }
        cv_.notify_one();
    }

    // Audio thread only.
    bool popAudio(Command& out) { return ring_.pop(out); }

    // True when every submitted command has been consumed by the audio
    // callback (call drainCommands() on the engine for the full guarantee).
    bool idle() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return inbox_.empty() && ring_.available() == 0;
    }

private:
    void dispatchLoop() {
        while (!quit_.load(std::memory_order_relaxed)) {
            Command c;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(20), [this] {
                    return quit_.load(std::memory_order_relaxed) || !inbox_.empty();
                });
                if (quit_.load(std::memory_order_relaxed)) return;
                if (inbox_.empty()) continue;
                c = inbox_.front();
                inbox_.pop_front();
            }
            while (!ring_.push(c)) {
                if (quit_.load(std::memory_order_relaxed)) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Command> inbox_;    // many producers -> dispatcher
    SpscQueue<Command> ring_{256}; // dispatcher -> audio callback
    std::thread dispatcher_;
    std::atomic<bool> quit_{false};
};

} // namespace vd
