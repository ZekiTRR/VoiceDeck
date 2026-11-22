#pragma once

#include "core/ai/AiVoiceStage.hpp"
#include "core/dsp/VoiceProcessor.hpp"
#include "core/engine/CommandQueue.hpp"
#include "core/engine/DriftPath.hpp"
#include "core/engine/SpscQueue.hpp"
#include "core/soundboard/SoundBank.hpp"
#include "core/soundboard/VoiceMixer.hpp"

#include "Iir.h"
#include "signalsmith-basics/dynamics.h"

#include "miniaudio.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vd {

struct DeviceEntry {
    ma_device_id id{};
    std::string name;
    bool isDefault = false;
};

// Stage-1 engine: mic passthrough + soundboard mixing into a virtual cable
// (VB-CABLE "CABLE Input"), with an optional monitor output for headphones.
//
// Threading model (no locks / allocations / disk I/O in audio callbacks):
//
//   [WASAPI capture cb]  --SPSC ring-->  [WASAPI playback cb]  --SPSC ring-->  [monitor cb]
//                                        ^ command ring (SPSC)
//                                        |
//                          [dispatcher] <- inbox (mutex, CLI/hotkey threads)
//
// The playback callback is the single consumer of commands and the single
// writer of voice state. Clock drift between the physical mic and the
// virtual cable is compensated by DriftPath (adaptive ~1x resample).
//
// Stage-2 seam: the block between DriftPath (micBlock_) and the output mix
// is where the DSP chain (Signalsmith Stretch, filters, spaces) and later
// the AI stage (RVC: resample 48k->16k with ma_resampler on a worker thread,
// inference, 40/48k->48k back) will be inserted. Tensor dims will be read
// from the model config at load time (see docs/README "Stage 2 notes").
class AudioEngine {
public:
    static constexpr std::uint32_t kSampleRate = 48000;
    static constexpr std::uint32_t kChannels = 2;
    // Latency presets: (periodFrames, stretchBlock, stretchInterval).
    static constexpr std::uint32_t kPeriodFrames[3] = {240, 480, 960};
    static constexpr std::uint32_t kPeriodFramesDefault = 480; // 10 ms
    static constexpr int kStretchBlock[3] = {768, 1536, 2048};
    static constexpr int kStretchInterval[3] = {192, 384, 512};
    static constexpr std::size_t kMaxBlockFrames = 2048;
    static constexpr std::size_t kMicRingSamples = std::size_t(1) << 15; // ~341 ms
    static constexpr std::size_t kMicTargetFillFrames = 2048;            // ~43 ms
    static constexpr std::size_t kMonitorRingSamples = std::size_t(1) << 14;
    static constexpr std::size_t kMonitorTargetFillFrames = 1024;

    explicit AudioEngine(const SoundBank& bank);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init();
    void shutdown();

    bool refreshDevices(std::string& err);
    const std::vector<DeviceEntry>& captureDevices() const { return captureDevices_; }
    const std::vector<DeviceEntry>& playbackDevices() const { return playbackDevices_; }

    bool start(std::size_t captureIdx, std::size_t playbackIdx, std::string& err);
    void stopDevices();
    bool startMonitor(std::size_t playbackIdx, std::string& err);
    void stopMonitor();
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }
    bool monitorRunning() const { return monRunning_; }

    // Control-thread API (thread-safe command transport).
    void submit(const Command& c) { queue_.submit(c); }
    const VoiceMixer& mixer() const { return mixer_; }
    bool bypassEnabled() const { return bypass_.load(std::memory_order_relaxed); }
    bool duckingOn() const { return ducking_.load(std::memory_order_relaxed); }

    // Voice presets: set once from the control side before audio starts.
    // Entries are immutable afterwards; the audio thread only reads them via
    // SetPreset commands.
    void setPresets(std::vector<VoicePreset> presets) { presets_ = std::move(presets); }
    const std::vector<VoicePreset>& presets() const { return presets_; }

    // Latency mode (0=low, 1=normal, 2=hq): changes the WASAPI period and
    // the Stretch STFT block size. Restarts devices if running.
    void setLatencyMode(int mode);
    int latencyMode() const { return latencyMode_.load(std::memory_order_relaxed); }
    std::uint32_t currentPeriodFrames() const {
        return kPeriodFrames[latencyMode_.load(std::memory_order_relaxed)];
    }
    bool dspOn() const { return dspOn_.load(std::memory_order_relaxed); }
    int voicePresetIndex() const { return voice_.activePresetIndex(); }
    float voiceLatencyMs() const { return voice_.latencyMs(); }

    // AI voice (Stage 2b). The library/service are used from control threads
    // (blocking model loads); the stage itself runs its own worker.
    VoiceLibrary& aiLibrary() { return aiLib_; }
    const VoiceLibrary& aiLibrary() const { return aiLib_; }
    AiVoiceService& aiService() { return aiService_; }
    const AiVoiceService& aiService() const { return aiService_; }
    // Live tuning knobs (lock-free atomics; CLI writes, threads read).
    AiVoiceStage::Controls& aiControls() { return aiControls_; }
    const AiVoiceStage::Controls& aiControls() const { return aiControls_; }
    bool aiEnabled() const { return aiControls_.enabled.load(std::memory_order_relaxed); }
    int aiUnderruns() const { return aiStage_.underruns(); }
    double aiLatencyMs() const { return aiStage_.latencyMs(); }

    // Control threads: wait until all submitted commands have been applied
    // by the playback callback (queue empty + one audio block passed).
    void drainCommands(int timeoutMs = 200);

    // Lock-free status meters (any thread).
    float micLevel() const { return micLevel_.load(std::memory_order_relaxed); }
    std::uint64_t micUnderruns() const { return micUnderruns_.load(std::memory_order_relaxed); }
    std::uint64_t captureOverflows() const { return capOverflows_.load(std::memory_order_relaxed); }
    std::uint64_t monitorUnderruns() const { return monUnderruns_.load(std::memory_order_relaxed); }

    std::size_t currentCaptureIdx() const { return curCaptureIdx_; }
    std::size_t currentPlaybackIdx() const { return curPlaybackIdx_; }
    std::size_t currentMonitorIdx() const { return curMonitorIdx_; }
    std::string currentCaptureName() const;
    std::string currentPlaybackName() const;

private:
    static void captureDataCb(ma_device* pDevice, void* pOutput, const void* pInput,
                              ma_uint32 frameCount);
    static void playbackDataCb(ma_device* pDevice, void* pOutput, const void* pInput,
                               ma_uint32 frameCount);
    static void monitorDataCb(ma_device* pDevice, void* pOutput, const void* pInput,
                              ma_uint32 frameCount);
    float duckingStep(std::size_t frames);
    // Audio thread: enhancement params follow the active preset.
    void configureEnhancement(const VoicePreset& p);
    void processEnhance(const float* in, float* out, std::size_t frames);

    const SoundBank& bank_;
    ma_context ctx_{};
    bool ctxOk_ = false;

    ma_device capDev_{};
    ma_device playDev_{};
    ma_device monDev_{};
    bool capRunning_ = false;
    bool playRunning_ = false;
    bool monRunning_ = false;

    // Stable storage for the ids of the devices currently opened
    // (pDeviceID points here for the device's lifetime).
    ma_device_id activeCaptureId_{};
    ma_device_id activePlaybackId_{};
    ma_device_id activeMonitorId_{};

    std::vector<DeviceEntry> captureDevices_;
    std::vector<DeviceEntry> playbackDevices_;
    std::size_t curCaptureIdx_ = std::size_t(-1);
    std::size_t curPlaybackIdx_ = std::size_t(-1);
    std::size_t curMonitorIdx_ = std::size_t(-1);

    // Audio plumbing.
    CommandQueue queue_;
    RingBuffer micRing_{kMicRingSamples};
    RingBuffer monitorRing_{kMonitorRingSamples};
    DriftPath micPath_{kMaxBlockFrames, kMicTargetFillFrames};
    DriftPath monPath_{kMaxBlockFrames, kMonitorTargetFillFrames};
    VoiceMixer mixer_;

    std::atomic<bool> running_{false};
    std::atomic<bool> monitorActive_{false};
    std::atomic<bool> bypass_{true};
    std::atomic<int> latencyMode_{1};
    std::atomic<bool> ducking_{false};
    std::atomic<bool> dspOn_{true};
    std::atomic<float> micLevel_{0.0f};
    std::atomic<std::uint64_t> capOverflows_{0};
    std::atomic<std::uint64_t> micUnderruns_{0};
    std::atomic<std::uint64_t> monUnderruns_{0};
    std::atomic<std::uint64_t> monOverflows_{0};
    std::atomic<std::uint64_t> playTicks_{0};

    // Preview mixer: sounds played only to the original output (monitor).
    VoiceMixer previewMixer_;
    float previewBlock_[kMaxBlockFrames * kChannels] = {};
public:
    std::atomic<bool> monitorPreviewOnly_{false};
private:

    // Audio-thread scratch (fixed size - the callback never allocates).
    float micBlock_[kMaxBlockFrames * kChannels] = {};
    float dspBlock_[kMaxBlockFrames * kChannels] = {};
    float monBlock_[kMaxBlockFrames * kChannels] = {};
    float duck_ = 1.0f;  // audio-thread only
    float dspFade_ = 0.0f; // audio-thread only

    // Voice chain (audio-thread only) + preset storage (immutable at runtime).
    VoiceProcessor voice_{double(kSampleRate)};
    std::vector<VoicePreset> presets_;

    // AI voice: shared models + the lock-free worker bridge.
    VoiceLibrary aiLib_;
    AiVoiceService aiService_;
    AiVoiceStage::Controls aiControls_{};
    AiVoiceStage aiStage_;

    // Engine-level enhancement, applied before the AI stage and the character
    // chain (gate + compressor + rumble cut + timbre shelves + 8-band EQ).
    Iir::RBJ::HighPass enhHpf_[2];
    Iir::RBJ::LowShelf enhShelfLow_[2];
    Iir::RBJ::HighShelf enhShelfHigh_[2];
    // Hand-rolled peaking biquads: [band][channel][b0..a2] and state.
    float eqCoef_[8][2][5] = {};  // b0, b1, b2, a1, a2
    float eqX_[8][2][3] = {};     // input history
    float eqY_[8][2][3] = {};     // output history
    float eqGains_[8] = {};       // cached per-band gains (skip zero-gain)
    signalsmith::basics::DynamicsFloat enhDyn_;
    float enhBlock_[kMaxBlockFrames * kChannels] = {};
    float aiBlock_[kMaxBlockFrames * kChannels] = {};
    float enhL_[kMaxBlockFrames] = {};
    float enhR_[kMaxBlockFrames] = {};
    float enhOutL_[kMaxBlockFrames] = {};
    float enhOutR_[kMaxBlockFrames] = {};
};

} // namespace vd
