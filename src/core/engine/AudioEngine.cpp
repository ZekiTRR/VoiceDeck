#include "core/engine/AudioEngine.hpp"
#include "core/engine/SoftLimiter.hpp"
#include "core/platform/ThreadAffinity.hpp"
#include "core/settings/Settings.hpp"
#include "core/util/Log.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <thread>

namespace vd {

namespace {
// Auto-ducking: when the mic RMS rises above the threshold, soundboard
// voices are smoothly pulled down to kDuckGain.
constexpr float kDuckThreshold = 0.02f;
constexpr float kDuckGain = 0.25f;
// DSP engage/disengage ramp (~20 ms @ 48 kHz).
constexpr std::size_t kFadeRampFrames = 960;
} // namespace

void AudioEngine::setLatencyMode(int mode) {
    if (mode < 0 || mode > 2) mode = 0;
    latencyMode_.store(mode, std::memory_order_relaxed);
    voice_.setLatencyMode(mode);
    fxVoice_.setLatencyMode(mode);
}

void AudioEngine::setVadThresholdDb(float db) {
    db = std::max(-60.0f, std::min(-20.0f, db));
    vadThresholdLinear_.store(std::pow(10.0f, db / 20.0f), std::memory_order_relaxed);
}

AudioEngine::AudioEngine(const SoundBank& bank)
    : bank_(bank),
      aiLib_(Settings::appDataDir() / "AiVoice"),
      aiService_(aiLib_),
      aiStage_(int(kSampleRate), int(kMaxBlockFrames), aiControls_, aiService_),
      fxAiStage_(int(kSampleRate), int(kMaxBlockFrames), fxAiControls_, aiService_, true) {
    // Fixed enhancement: 85 Hz rumble cut. Shelves/dynamics follow the
    // active preset (configureEnhancement).
    for (int c = 0; c < 2; ++c) {
        enhHpf_[c].setup(double(kSampleRate), 85.0, 0.707);
        fxEnhHpf_[c].setup(double(kSampleRate), 55.0, 0.707);
    }
    enhDyn_.configure(double(kSampleRate), kMaxBlockFrames, 2);
    fxEnhDyn_.configure(double(kSampleRate), kMaxBlockFrames, 2);
    fxEnhDyn_.gate.limitDb = -80.0;
    fxAiControls_.autoLimit.store(false, std::memory_order_relaxed);
}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
    if (ctxOk_) return true;
    if (ma_context_init(NULL, 0, NULL, &ctx_) != MA_SUCCESS) return false;
    ctxOk_ = true;
    queue_.start();
    captureManager_.initialize();
    return true;
}

void AudioEngine::shutdown() {
    stopDevices();
    captureManager_.shutdown();
    queue_.stop();
    if (ctxOk_) {
        ma_context_uninit(&ctx_);
        ctxOk_ = false;
    }
}

bool AudioEngine::refreshDevices(std::string& err) {
    if (!ctxOk_) {
        err = "audio context is not initialized";
        return false;
    }
    ma_device_info* pCap = nullptr;
    ma_uint32 nCap = 0;
    ma_device_info* pPlay = nullptr;
    ma_uint32 nPlay = 0;
    if (ma_context_get_devices(&ctx_, &pPlay, &nPlay, &pCap, &nCap) != MA_SUCCESS) {
        err = "failed to enumerate audio devices";
        return false;
    }
    captureDevices_.clear();
    captureDevices_.reserve(nCap);
    for (ma_uint32 i = 0; i < nCap; ++i)
        captureDevices_.push_back({pCap[i].id, pCap[i].name, pCap[i].isDefault != 0});
    playbackDevices_.clear();
    playbackDevices_.reserve(nPlay);
    for (ma_uint32 i = 0; i < nPlay; ++i)
        playbackDevices_.push_back({pPlay[i].id, pPlay[i].name, pPlay[i].isDefault != 0});
    return true;
}

bool AudioEngine::start(std::size_t captureIdx, std::size_t playbackIdx, std::string& err) {
    if (!ctxOk_) {
        err = "audio context is not initialized";
        return false;
    }
    if (captureIdx >= captureDevices_.size() || playbackIdx >= playbackDevices_.size()) {
        err = "device index out of range";
        return false;
    }
    stopDevices();

    activeCaptureId_ = captureDevices_[captureIdx].id;
    activePlaybackId_ = playbackDevices_[playbackIdx].id;

    // Shared-mode low latency: miniaudio uses IAudioClient3 when the driver
    // supports it, clamps the period to the engine's fundamental period and
    // falls back to IAudioClient automatically. Requesting a period is only a
    // hint - the actual values are read back below and exposed to the UI.
    ma_device_config capCfg = ma_device_config_init(ma_device_type_capture);
    capCfg.capture.format = ma_format_f32;
    capCfg.capture.channels = kChannels;
    capCfg.capture.pDeviceID = &activeCaptureId_;
    capCfg.sampleRate = kSampleRate;
    capCfg.periodSizeInFrames = currentPeriodFrames();
    capCfg.periods = 2;
    capCfg.performanceProfile = ma_performance_profile_low_latency;
    capCfg.wasapi.usage = ma_wasapi_usage_pro_audio; // MMCSS "Pro Audio" thread
    capCfg.dataCallback = &AudioEngine::captureDataCb;
    capCfg.pUserData = this;
    if (ma_device_init(&ctx_, &capCfg, &capDev_) != MA_SUCCESS) {
        err = "failed to open capture device: " + captureDevices_[captureIdx].name;
        return false;
    }
    {
        const auto& c = capDev_.capture;
        capPeriodFrames_.store(c.internalPeriodSizeInFrames, std::memory_order_relaxed);
        capBufferFrames_.store(
            capDev_.wasapi.actualBufferSizeInFramesCapture > 0
                ? capDev_.wasapi.actualBufferSizeInFramesCapture
                : c.internalPeriodSizeInFrames * std::max<ma_uint32>(1, c.internalPeriods),
            std::memory_order_relaxed);
        capSampleRate_.store(c.internalSampleRate, std::memory_order_relaxed);
        log::info("capture: '%s' %u Hz, %u ch, fmt %d, period %u frames (%.2f ms), "
                  "buffer %u frames, shared=%d",
                  c.name, c.internalSampleRate, c.internalChannels, int(c.internalFormat),
                  c.internalPeriodSizeInFrames,
                  1000.0 * double(c.internalPeriodSizeInFrames) / double(c.internalSampleRate),
                  capDev_.wasapi.actualBufferSizeInFramesCapture, int(c.shareMode));
    }

    ma_device_config playCfg = ma_device_config_init(ma_device_type_playback);
    playCfg.playback.format = ma_format_f32;
    playCfg.playback.channels = kChannels;
    playCfg.playback.pDeviceID = &activePlaybackId_;
    playCfg.sampleRate = kSampleRate;
    playCfg.periodSizeInFrames = currentPeriodFrames();
    playCfg.periods = 4;
    playCfg.performanceProfile = ma_performance_profile_low_latency;
    playCfg.wasapi.usage = ma_wasapi_usage_pro_audio; // MMCSS "Pro Audio" thread
    playCfg.dataCallback = &AudioEngine::playbackDataCb;
    playCfg.pUserData = this;
    if (ma_device_init(&ctx_, &playCfg, &playDev_) != MA_SUCCESS) {
        err = "failed to open playback device: " + playbackDevices_[playbackIdx].name;
        ma_device_uninit(&capDev_);
        return false;
    }
    {
        const auto& p = playDev_.playback;
        playPeriodFrames_.store(p.internalPeriodSizeInFrames, std::memory_order_relaxed);
        playBufferFrames_.store(
            playDev_.wasapi.actualBufferSizeInFramesPlayback > 0
                ? playDev_.wasapi.actualBufferSizeInFramesPlayback
                : p.internalPeriodSizeInFrames * std::max<ma_uint32>(1, p.internalPeriods),
            std::memory_order_relaxed);
        playSampleRate_.store(p.internalSampleRate, std::memory_order_relaxed);
        log::info("playback: '%s' %u Hz, %u ch, fmt %d, period %u frames (%.2f ms), "
                  "buffer %u frames, shared=%d",
                  p.name, p.internalSampleRate, p.internalChannels, int(p.internalFormat),
                  p.internalPeriodSizeInFrames,
                  1000.0 * double(p.internalPeriodSizeInFrames) / double(p.internalSampleRate),
                  playDev_.wasapi.actualBufferSizeInFramesPlayback, int(p.shareMode));
    }

    // Target backlog = 1.75x the LARGER of the two callback periods. Using
    // only the playback period would let a 480-frame capture batch starve a
    // 256-frame playback callback. This is inside the 1.5..2.0x window.
    const std::uint32_t capPeriod = capPeriodFrames_.load(std::memory_order_relaxed);
    const std::uint32_t playPeriod = playPeriodFrames_.load(std::memory_order_relaxed);
    const std::uint32_t quantum = std::max(capPeriod, playPeriod);
    micTargetFrames_ = double(std::max<std::uint32_t>(1, quantum)) * 2.0;
    micPath_.setTargetFrames(micTargetFrames_);
    log::info("mic ring target: %.0f frames (%.2f ms, quantum %u)",
              micTargetFrames_, 1000.0 * micTargetFrames_ / double(kSampleRate), quantum);

    micRing_.reset();
    micPath_.reset();
    monitorRing_.reset();
    monPath_.reset();
    duck_ = 1.0f;
    vadQuietFrames_ = 0;
    vadSleepState_ = false;
    vadPhraseActive_ = false;
    vadWakeGain_ = 0.0f;
    vadSleeping_.store(false, std::memory_order_relaxed);
    aiControls_.sleeping.store(false, std::memory_order_relaxed);
    fxAiControls_.sleeping.store(true, std::memory_order_relaxed);
    fxSoundTailFrames_ = 0;
    fxAiTailFrames_ = 0;
    fxSendGain_ = 0.0f;
    ambient_.reset();
    capOverflows_.store(0, std::memory_order_relaxed);
    micUnderruns_.store(0, std::memory_order_relaxed);
    playbackMaxFrames_.store(0, std::memory_order_relaxed);
    lastUnderrunFrames_.store(0, std::memory_order_relaxed);
    lastUnderrunFill_.store(0.0, std::memory_order_relaxed);
    capTicks_.store(0, std::memory_order_relaxed);
    playTicks_.store(0, std::memory_order_relaxed);
    capFramesTotal_.store(0, std::memory_order_relaxed);
    playFramesTotal_.store(0, std::memory_order_relaxed);
    lastClockCap_ = 0;
    lastClockPlay_ = 0;

    // Start capture first and wait for the prefill, so the first playback
    // callback already has a full target backlog (zero startup underruns).
    if (ma_device_start(&capDev_) != MA_SUCCESS) {
        err = "failed to start capture device";
        ma_device_uninit(&playDev_);
        ma_device_uninit(&capDev_);
        return false;
    }
    // Prefill 2x the steady-state target: the extra headroom absorbs the
    // virtual cable's clock offset while the controller is still pulling in.
    const std::size_t wantSamples =
        std::size_t(micTargetFrames_ * 4.0) * std::size_t(kChannels);
    for (int i = 0; i < 300 && micRing_.available() < wantSamples; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (micRing_.available() < wantSamples)
        log::warn("mic prefill timed out (have %zu of %zu samples)",
                  micRing_.available(), wantSamples);
    if (ma_device_start(&playDev_) != MA_SUCCESS) {
        err = "failed to start playback device";
        ma_device_uninit(&playDev_);
        ma_device_uninit(&capDev_);
        return false;
    }
    capRunning_ = true;
    playRunning_ = true;
    // Soft P-core pinning after the device threads exist (one syscall each).
    pinThreadToPerformanceCores(capDev_.thread);
    pinThreadToPerformanceCores(playDev_.thread);
    curCaptureIdx_ = captureIdx;
    curPlaybackIdx_ = playbackIdx;
    running_.store(true, std::memory_order_relaxed);
    return true;
}

void AudioEngine::stopDevices() {
    stopMonitor();
    running_.store(false, std::memory_order_relaxed);
    if (playRunning_) {
        ma_device_uninit(&playDev_);
        playRunning_ = false;
    }
    if (capRunning_) {
        ma_device_uninit(&capDev_);
        capRunning_ = false;
    }
    curCaptureIdx_ = std::size_t(-1);
    curPlaybackIdx_ = std::size_t(-1);
    capPeriodFrames_.store(0, std::memory_order_relaxed);
    playPeriodFrames_.store(0, std::memory_order_relaxed);
    capBufferFrames_.store(0, std::memory_order_relaxed);
    playBufferFrames_.store(0, std::memory_order_relaxed);
    capSampleRate_.store(0, std::memory_order_relaxed);
    playSampleRate_.store(0, std::memory_order_relaxed);
}

bool AudioEngine::startMonitor(std::size_t playbackIdx, std::string& err) {
    if (!ctxOk_) {
        err = "audio context is not initialized";
        return false;
    }
    if (playbackIdx >= playbackDevices_.size()) {
        err = "playback index out of range";
        return false;
    }
    stopMonitor();

    activeMonitorId_ = playbackDevices_[playbackIdx].id;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.playback.pDeviceID = &activeMonitorId_;
    cfg.sampleRate = kSampleRate;
    cfg.periodSizeInFrames = currentPeriodFrames();
    cfg.periods = 2;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.wasapi.usage = ma_wasapi_usage_pro_audio;
    cfg.dataCallback = &AudioEngine::monitorDataCb;
    cfg.pUserData = this;
    if (ma_device_init(&ctx_, &cfg, &monDev_) != MA_SUCCESS) {
        err = "failed to open monitor device: " + playbackDevices_[playbackIdx].name;
        return false;
    }

    const std::uint32_t monPeriod = monDev_.playback.internalPeriodSizeInFrames;
    const std::uint32_t playPeriod = playPeriodFrames_.load(std::memory_order_relaxed);
    monPath_.setTargetFrames(double(std::max(monPeriod, playPeriod)) * 2.0);
    monitorRing_.reset();
    monPath_.reset();
    // Start feeding before the consumer starts so the path prefills.
    monitorActive_.store(true, std::memory_order_relaxed);
    if (ma_device_start(&monDev_) != MA_SUCCESS) {
        err = "failed to start monitor device";
        monitorActive_.store(false, std::memory_order_relaxed);
        ma_device_uninit(&monDev_);
        return false;
    }
    monRunning_ = true;
    pinThreadToPerformanceCores(monDev_.thread);
    curMonitorIdx_ = playbackIdx;
    return true;
}

void AudioEngine::stopMonitor() {
    monitorActive_.store(false, std::memory_order_relaxed);
    monitorVoice_.store(false, std::memory_order_relaxed);
    if (monRunning_) {
        ma_device_uninit(&monDev_);
        monRunning_ = false;
    }
    curMonitorIdx_ = std::size_t(-1);
}

bool AudioEngine::drainCommands(int timeoutMs) {
    std::lock_guard<std::recursive_mutex> lock(controlMtx_);
    if (!isRunning()) return true; // no callback can hold a preset reference
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto remaining = [&]() {
        return std::chrono::steady_clock::now() < deadline;
    };
    // 1. Wait until the dispatcher moved everything into the audio ring.
    while (!queue_.idle()) {
        if (!remaining()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // 2. Wait for one more audio block so the playback callback consumed it.
    const std::uint64_t ticks = playTicks_.load(std::memory_order_relaxed);
    while (playTicks_.load(std::memory_order_relaxed) == ticks) {
        if (!remaining()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

std::string AudioEngine::currentCaptureName() const {
    if (curCaptureIdx_ == std::size_t(-1) || curCaptureIdx_ >= captureDevices_.size()) return "-";
    return captureDevices_[curCaptureIdx_].name;
}

std::string AudioEngine::currentPlaybackName() const {
    if (curPlaybackIdx_ == std::size_t(-1) || curPlaybackIdx_ >= playbackDevices_.size()) return "-";
    return playbackDevices_[curPlaybackIdx_].name;
}

double AudioEngine::endToEndLatencyMs() const {
    if (!isRunning()) return 0.0;
    const double rate = double(kSampleRate);
    // Capture buffer + playback buffer + smoothed mic-ring backlog.
    double ms = (double(capBufferFrames_.load(std::memory_order_relaxed)) +
                 double(playBufferFrames_.load(std::memory_order_relaxed)) +
                 micPath_.averageFillFrames()) *
                1000.0 / rate;
    // The character chain (and AI, when active) sits between capture and mix.
    // While the VAD sleeps the voice branch is skipped, so it adds no delay.
    if (bypassEnabled() && dspOn() && !vadSleeping()) {
        ms += double(voiceLatencyMs());
        if (aiEnabled() && aiService_.isReady()) ms += aiLatencyMs();
    }
    return ms;
}

// --------------------------------------------------------------------------
// WASAPI callbacks. No allocations, no locks, no logging here.
// --------------------------------------------------------------------------

void AudioEngine::captureDataCb(ma_device* pDevice, void* /*pOutput*/, const void* pInput,
                                ma_uint32 frameCount) {
    auto* self = static_cast<AudioEngine*>(pDevice->pUserData);
    if (pInput == nullptr || frameCount == 0) return;
    self->capTicks_.fetch_add(1, std::memory_order_relaxed);
    self->capFramesTotal_.fetch_add(frameCount, std::memory_order_relaxed);
    const float* in = static_cast<const float*>(pInput);
    const std::size_t n = std::size_t(frameCount) * kChannels;
    if (self->micRing_.write(in, n) < n)
        self->capOverflows_.fetch_add(1, std::memory_order_relaxed);

    // RMS level for meters + auto-ducking.
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) sum += double(in[i]) * double(in[i]);
    const double rms = std::sqrt(sum / double(n));
    self->micLevel_.store(float(rms), std::memory_order_relaxed);
}

void AudioEngine::playbackDataCb(ma_device* pDevice, void* pOutput, const void* /*pInput*/,
                                 ma_uint32 frameCount) {
    auto* self = static_cast<AudioEngine*>(pDevice->pUserData);
    float* out = static_cast<float*>(pOutput);
    const std::size_t frames = frameCount;
    self->playTicks_.fetch_add(1, std::memory_order_relaxed);
    self->playFramesTotal_.fetch_add(frameCount, std::memory_order_relaxed);
    // Feed-forward clock ratio: every 64 callbacks (~0.34 s) compare how many
    // frames capture produced vs playback consumed. Virtual cables can be
    // several percent off; this converges in one window instead of slowly
    // draining the ring. The first sample only seeds the counters.
    if ((self->playTicks_.load(std::memory_order_relaxed) & 63) == 0) {
        const std::uint64_t capFrames =
            self->capFramesTotal_.load(std::memory_order_relaxed);
        const std::uint64_t playFrames =
            self->playFramesTotal_.load(std::memory_order_relaxed);
        if (self->lastClockCap_ != 0 && self->lastClockPlay_ != 0 &&
            capFrames > self->lastClockCap_ && playFrames > self->lastClockPlay_) {
            const std::uint64_t dCap = capFrames - self->lastClockCap_;
            const std::uint64_t dPlay = playFrames - self->lastClockPlay_;
            self->micPath_.setFeedForward(double(dCap) / double(dPlay));
        }
        self->lastClockCap_ = capFrames;
        self->lastClockPlay_ = playFrames;
    }

    if (frames > kMaxBlockFrames) { // cannot happen with our period config, just in case
        std::memset(out, 0, frames * kChannels * sizeof(float));
        return;
    }

    // 1. Apply pending commands (lock-free ring).
    Command cmd;
    while (self->queue_.popAudio(cmd)) {
        switch (cmd.type) {
            case Command::Type::SetBypass:
                self->bypass_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetDucking:
                self->ducking_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetAmbient:
                self->ambientOn_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetAmbientPreset:
                if (cmd.soundId < self->presets_.size() && self->voice_.matchesPreset(int(cmd.soundId))) {
                    std::shared_ptr<const Sound> clip;
                    if (cmd.soundId < self->presetAmbients_.size())
                        clip = self->presetAmbients_[cmd.soundId];
                    self->ambient_.configure(int(cmd.value), cmd.valueB, std::move(clip));
                }
                break;
            case Command::Type::SetSoundboardMonitor:
                self->soundboardMonitor_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetMonitorVoice:
                self->monitorVoice_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetSoundFx:
                self->soundFxSend_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                if (cmd.value == 0.0f) {
                    self->fxAiControls_.sleeping.store(true, std::memory_order_relaxed);
                    self->fxAiStage_.notifyWorker();
                    self->fxSoundTailFrames_ = 0;
                    self->fxAiTailFrames_ = 0;
                    self->fxVoice_.resetTail(); // do not resume stale FX tails on re-enable
                }
                break;
            case Command::Type::SetPreset:
                if (cmd.soundId < self->presets_.size()) {
                    const VoicePreset& p = self->presets_[cmd.soundId];
                    self->configureEnhancement(p);
                    self->configureFxEnhancement(p);
                    self->voice_.applyPreset(int(cmd.soundId), p);
                    self->fxVoice_.applyPreset(int(cmd.soundId), p);
                    std::shared_ptr<const Sound> clip;
                    if (cmd.soundId < self->presetAmbients_.size())
                        clip = self->presetAmbients_[cmd.soundId];
                    self->ambient_.configure(p, std::move(clip));
                }
                break;
            case Command::Type::SetDsp:
                self->dspOn_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetTransposeOverride:
                self->voice_.setTransposeOverride(cmd.value, cmd.soundId != 0);
                self->fxVoice_.setTransposeOverride(cmd.value, cmd.soundId != 0);
                break;
            case Command::Type::SetFormantOverride:
                self->voice_.setFormantOverride(cmd.value, cmd.soundId != 0);
                self->fxVoice_.setFormantOverride(cmd.value, cmd.soundId != 0);
                break;
            case Command::Type::SetAi:
                self->aiControls_.enabled.store(cmd.value != 0.0f, std::memory_order_relaxed);
                self->fxAiControls_.enabled.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetAiPitch:
                self->aiControls_.pitchShift.store(cmd.value, std::memory_order_relaxed);
                self->fxAiControls_.pitchShift.store(cmd.value, std::memory_order_relaxed);
                break;
            case Command::Type::PreviewSound: {
                // VoiceMixer only understands PlaySound - convert.
                Command play = cmd;
                play.type = Command::Type::PlaySound;
                self->previewMixer_.applyCommand(play, self->bank_);
                break;
            }
            case Command::Type::StopPreview: {
                Command stopAll;
                stopAll.type = Command::Type::StopAll;
                self->previewMixer_.applyCommand(stopAll, self->bank_);
                break;
            }
            default:
                self->mixer_.applyCommand(cmd, self->bank_);
                break;
        }
    }

    // 2. Mic path with drift correction. Always pulled - even when bypassed -
    //    so the ring keeps draining and meters/ducking stay alive.
    if (frameCount > self->playbackMaxFrames_.load(std::memory_order_relaxed))
        self->playbackMaxFrames_.store(frameCount, std::memory_order_relaxed);
    const std::size_t produced = self->micPath_.pull(self->micRing_, self->micBlock_, frames);
    if (produced < frames) {
        std::memset(self->micBlock_ + produced * kChannels, 0,
                    (frames - produced) * kChannels * sizeof(float));
        self->micUnderruns_.fetch_add(1, std::memory_order_relaxed);
        self->lastUnderrunFrames_.store(frameCount, std::memory_order_relaxed);
        self->lastUnderrunFill_.store(self->micPath_.lastFillFrames(),
                                      std::memory_order_relaxed);
    }

    // 2b. Voice-activity gate (RMS with hysteresis). After ~300 ms below the
    //     configured threshold the voice branch (enhance + AI + character)
    //     sleeps; the soundboard and preview keep running. The AI stage keeps
    //     a pre-roll history while asleep.
    {
        const float thr = self->vadThresholdLinear_.load(std::memory_order_relaxed);
        double sum = 0.0;
        const std::size_t n = frames * kChannels;
        for (std::size_t i = 0; i < n; ++i) sum += double(self->micBlock_[i]) * double(self->micBlock_[i]);
        const double rms = std::sqrt(sum / double(n));
        const float wakeThr = thr * 1.58489319f; // +4 dB hysteresis
        if ((!self->vadSleepState_ && rms >= double(thr)) ||
            (self->vadSleepState_ && rms > double(wakeThr)))
            self->vadPhraseActive_ = true;
        bool sleep = self->vadSleepState_;
        if (rms < double(thr)) {
            self->vadQuietFrames_ += frames;
            if (!sleep && self->vadQuietFrames_ >= std::uint64_t(kSampleRate) * 3 / 10)
                sleep = true;
        } else {
            self->vadQuietFrames_ = 0;
            if (sleep && rms > double(wakeThr)) sleep = false;
        }
        if (sleep != self->vadSleepState_) {
            self->vadSleepState_ = sleep;
            self->vadWakeGain_ = 0.0f;
            self->vadSleeping_.store(sleep, std::memory_order_relaxed);
            self->aiControls_.sleeping.store(sleep, std::memory_order_relaxed);
            if (sleep) {
                self->vadPhraseActive_ = false;
                self->voice_.resetTail(); // drop stale reverb/delay tails
                self->aiStage_.notifyWorker(); // worker drains input/output and resets
            }
        }
    }

    // 3. Voice path. Enhancement (rumble cut, gate, compressor, timbre) is
    //    engine-level and always applies - same as Voice CTX's studio chain.
    //    The AI stage (optional) and the character chain sit after it.
    if (!self->bypassEnabled() || self->vadSleepState_) {
        std::memset(out, 0, frames * kChannels * sizeof(float));
        if (self->vadSleepState_) self->aiStage_.updatePreRoll(self->micBlock_, frames);
    } else {
        self->processEnhance(self->micBlock_, self->enhBlock_, frames);

        const float target = self->dspOn_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        const float k =
            (frames >= kFadeRampFrames) ? 1.0f : (float(frames) / float(kFadeRampFrames));
        self->dspFade_ += (target - self->dspFade_) * k;
        if (self->dspFade_ < 0.001f && target == 0.0f) self->dspFade_ = 0.0f;

        if (self->dspFade_ > 0.001f) {
            // Stage-2b core: enhanced -> AI (48k -> 16k -> model -> back)
            //            -> character chain.
            self->aiStage_.processStereo(self->enhBlock_, self->aiBlock_, frames);
            if (!self->aiControls_.enabled.load(std::memory_order_relaxed) &&
                self->fxAiControls_.enabled.load(std::memory_order_relaxed))
                self->fxAiControls_.enabled.store(false, std::memory_order_relaxed);
            self->voice_.process(self->aiBlock_, self->dspBlock_, frames);
            const float f = self->dspFade_;
            const float d = 1.0f - f;
            const float* enh = self->enhBlock_;
            const float* dsp = self->dspBlock_;
            for (std::size_t i = 0; i < frames * kChannels; ++i) out[i] = enh[i] * d + dsp[i] * f;
        } else {
            std::memcpy(out, self->enhBlock_, frames * kChannels * sizeof(float));
        }
    }

    // 3b. 10 ms fade-in after a VAD wake (avoids a click on the first block).
    if (!self->vadSleepState_ && self->vadWakeGain_ < 1.0f) {
        const float step = 1.0f / (0.010f * float(kSampleRate));
        for (std::size_t i = 0; i < frames; ++i) {
            self->vadWakeGain_ = std::min(1.0f, self->vadWakeGain_ + step);
            out[i * 2] *= self->vadWakeGain_;
            out[i * 2 + 1] *= self->vadWakeGain_;
        }
    }

    // 4. Soundboard, ducked under speech if enabled. Render into its own block
    //    first: FX Send must never share Stretch/reverb/AI state with the mic.
    for (std::size_t i = 0; i < frames * kChannels; ++i) {
        if (std::isnan(out[i]) || std::isinf(out[i])) out[i] = 0.0f;
    }
    std::memcpy(self->voiceBlock_, out, frames * kChannels * sizeof(float));
    const float duck = self->duckingStep(frames);
    std::memset(self->soundBlock_, 0, frames * kChannels * sizeof(float));
    self->mixer_.mix(self->soundBlock_, frames, kChannels, 1.0f);

    const float fxTarget = self->soundFxSend_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    // Time-based ramp works for 256/480/960-frame callbacks alike; a single
    // HQ block must not snap the send fully on or off.
    const float fxStep = 1.0f - std::exp(-float(frames) / (0.010f * float(kSampleRate)));
    self->fxSendGain_ += (fxTarget - self->fxSendGain_) * fxStep;
    if (self->fxSendGain_ < 0.001f && fxTarget == 0.0f) self->fxSendGain_ = 0.0f;

    if (self->fxSendGain_ > 0.001f) {
        float peak = 0.0f;
        for (std::size_t i = 0; i < frames * kChannels; ++i) {
            const float a = std::fabs(self->soundBlock_[i]);
            if (a > peak) peak = a;
            if (std::isnan(self->soundBlock_[i]) || std::isinf(self->soundBlock_[i]))
                self->soundBlock_[i] = 0.0f;
        }
        // Let long cave/reverb/delay tails finish after a clip, but put the
        // extra RVC worker back to sleep much sooner to avoid idle GPU work.
        const std::uint64_t kVoiceTailFrames = std::max<std::uint64_t>(
            std::uint64_t(kSampleRate) / 5,
            std::uint64_t(self->fxVoice_.tailSeconds() * double(kSampleRate)) +
                std::uint64_t(kSampleRate) / 4);
        constexpr std::uint64_t kAiTailFrames = std::uint64_t(kSampleRate) * 3 / 10;
        const bool hasSignal = peak > 0.00001f;
        self->fxSoundTailFrames_ = hasSignal
                                       ? kVoiceTailFrames
                                       : (self->fxSoundTailFrames_ > frames
                                              ? self->fxSoundTailFrames_ - frames
                                              : 0);
        self->fxAiTailFrames_ = hasSignal
                                    ? kAiTailFrames
                                    : (self->fxAiTailFrames_ > frames
                                           ? self->fxAiTailFrames_ - frames
                                           : 0);
        const bool fxActive = self->fxSoundTailFrames_ > 0;
        const bool fxAiSleeping = self->fxAiTailFrames_ == 0;
        if (self->fxAiControls_.sleeping.load(std::memory_order_relaxed) != fxAiSleeping) {
            self->fxAiControls_.sleeping.store(fxAiSleeping, std::memory_order_relaxed);
            self->fxAiStage_.notifyWorker();
        }
        if (fxActive) {
            self->processFxEnhancement(self->soundBlock_, self->fxEnhBlock_, frames);
            self->fxAiStage_.processStereo(self->fxEnhBlock_, self->fxAiBlock_, frames);
            self->fxVoice_.process(self->fxAiBlock_, self->fxDspBlock_, frames);
        } else {
            std::memset(self->fxDspBlock_, 0, frames * kChannels * sizeof(float));
        }
        const float wet = self->fxSendGain_;
        const float dry = 1.0f - wet;
        for (std::size_t i = 0; i < frames * kChannels; ++i) {
            self->soundboardBlock_[i] =
                (self->soundBlock_[i] * dry + self->fxDspBlock_[i] * wet) * duck;
            out[i] += self->soundboardBlock_[i];
        }
    } else {
        if (!self->fxAiControls_.sleeping.load(std::memory_order_relaxed)) {
            self->fxAiControls_.sleeping.store(true, std::memory_order_relaxed);
            self->fxAiStage_.notifyWorker();
        }
        for (std::size_t i = 0; i < frames * kChannels; ++i) {
            self->soundboardBlock_[i] = self->soundBlock_[i] * duck;
            out[i] += self->soundboardBlock_[i];
        }
    }

    // 4b. Preset ambience follows the existing mic VAD, never the soundboard.
    //     It is therefore silent in the channel between phrases.
    {
        std::memset(self->ambientBlock_, 0, frames * kChannels * sizeof(float));
        const bool speechActive = self->vadPhraseActive_ && !self->vadSleepState_;
        const bool ambientSystemOn = self->ambientOn_.load(std::memory_order_relaxed) &&
                                      self->bypassEnabled() &&
                                      self->dspOn_.load(std::memory_order_relaxed);
        self->ambient_.process(self->ambientBlock_, frames, speechActive, ambientSystemOn);
        for (std::size_t i = 0; i < frames * kChannels; ++i) {
            out[i] += self->ambientBlock_[i];
            self->voiceBlock_[i] += self->ambientBlock_[i];
        }
    }
    // Final guard after soundboard FX and custom ambience joins. The monitor
    // uses the isolated voice/soundboard sends, so sanitize those as well as
    // the main cable mix.
    for (std::size_t i = 0; i < frames * kChannels; ++i) {
        if (std::isnan(out[i]) || std::isinf(out[i])) out[i] = 0.0f;
        if (std::isnan(self->voiceBlock_[i]) || std::isinf(self->voiceBlock_[i]))
            self->voiceBlock_[i] = 0.0f;
        if (std::isnan(self->soundboardBlock_[i]) || std::isinf(self->soundboardBlock_[i]))
            self->soundboardBlock_[i] = 0.0f;
    }

    // 5. Preview sounds (monitor/original output only, not the main cable).
    std::memset(self->previewBlock_, 0, frames * kChannels * sizeof(float));
    self->previewMixer_.mix(self->previewBlock_, frames, kChannels, 1.0f);

    // 6. Feed the monitor path (SPSC -> monitor callback).
    if (self->monitorActive_.load(std::memory_order_relaxed)) {
        if (self->monitorPreviewOnly_.load(std::memory_order_relaxed)) {
            // Preview-only mode: just the preview sounds, NO voice mix.
            if (self->monitorRing_.write(self->previewBlock_, frames * kChannels) <
                frames * kChannels)
                self->monOverflows_.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Independent monitor sends: voice preview and main soundboard can
            // be enabled separately, while explicit previews are always added.
            if (self->monitorVoice_.load(std::memory_order_relaxed)) {
                for (std::size_t i = 0; i < frames * kChannels; ++i)
                    self->previewBlock_[i] += self->voiceBlock_[i];
            }
            if (self->soundboardMonitor_.load(std::memory_order_relaxed)) {
                for (std::size_t i = 0; i < frames * kChannels; ++i)
                    self->previewBlock_[i] += self->soundboardBlock_[i];
            }
            if (self->monitorRing_.write(self->previewBlock_, frames * kChannels) <
                frames * kChannels)
                self->monOverflows_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 7. Captured application audio goes to the virtual cable only. This runs
    //    after the monitor copy so the original application audio is not
    //    monitored twice. The final limiter is transparent below 0.85.
    self->captureManager_.mixInto(out, frames);
    for (std::size_t i = 0; i < frames * kChannels; ++i) out[i] = softLimit(out[i]);
}

void AudioEngine::monitorDataCb(ma_device* pDevice, void* pOutput, const void* /*pInput*/,
                                ma_uint32 frameCount) {
    auto* self = static_cast<AudioEngine*>(pDevice->pUserData);
    float* out = static_cast<float*>(pOutput);
    if (frameCount > kMaxBlockFrames) {
        std::memset(out, 0, std::size_t(frameCount) * kChannels * sizeof(float));
        return;
    }
    const std::size_t produced = self->monPath_.pull(self->monitorRing_, self->monBlock_, frameCount);
    if (produced < frameCount) {
        std::memset(self->monBlock_ + produced * kChannels, 0,
                    (frameCount - produced) * kChannels * sizeof(float));
        self->monUnderruns_.fetch_add(1, std::memory_order_relaxed);
    }
    std::memcpy(out, self->monBlock_, std::size_t(frameCount) * kChannels * sizeof(float));
}

// Engine-level enhancement: rumble cut + timbre shelves (per sample) +
// gate/compressor (block). Audio thread only.
void AudioEngine::configureEnhancement(const VoicePreset& p) {
    static constexpr double kEqFreq[8] = {200.0, 400.0, 800.0, 1600.0,
                                          3200.0, 6400.0, 10000.0, 12000.0};
    const bool engineEq = !usesExplicitChain(p);
    for (int c = 0; c < 2; ++c) {
        enhShelfHigh_[c].setup(double(kSampleRate), 3500.0, 4.0 * double(p.timbre));
        enhShelfLow_[c].setup(double(kSampleRate), 250.0, -2.0 * double(p.timbre));
    }
    // Hand-rolled RBJ peaking biquads (same formula as VoiceMixer - stable).
    for (int b = 0; b < 8; ++b) {
        const float gain = std::max(-12.0f, std::min(12.0f, p.eq[b]));
        eqGains_[b] = engineEq ? gain : 0.0f;
        for (int c = 0; c < 2; ++c) {
            const double A = std::pow(10.0, gain / 40.0);
            const double w = 2.0 * 3.14159265358979 * kEqFreq[b] / double(kSampleRate);
            const double cw = std::cos(w);
            const double sw = std::sin(w);
            const double alpha = sw / (2.0 * 0.9);
            const double a0 = 1.0 + alpha / A;
            eqCoef_[b][c][0] = float((1.0 + alpha * A) / a0);
            eqCoef_[b][c][1] = float((-2.0 * cw) / a0);
            eqCoef_[b][c][2] = float((1.0 - alpha * A) / a0);
            eqCoef_[b][c][3] = float((-2.0 * cw) / a0);
            eqCoef_[b][c][4] = float((1.0 - alpha / A) / a0);
            for (int k = 0; k < 3; ++k) {
                eqX_[b][c][k] = 0.0f;
                eqY_[b][c][k] = 0.0f;
            }
        }
    }
    if (p.gate) {
        enhDyn_.gate.limitDb = -48.0;
        enhDyn_.gate.hysteresis = 6.0;
    } else {
        enhDyn_.gate.limitDb = -80.0; // effectively open
    }
    if (p.comp) {
        enhDyn_.compressor.limitDb = -18.0;
        enhDyn_.compressor.invRatio = 0.5; // 2:1
        enhDyn_.compressor.autoGain = 1;
    } else {
        enhDyn_.compressor.invRatio = 1.0; // 1:1 = off
    }
}

void AudioEngine::configureFxEnhancement(const VoicePreset& p) {
    static constexpr double kEqFreq[8] = {200.0, 400.0, 800.0, 1600.0,
                                          3200.0, 6400.0, 10000.0, 12000.0};
    const bool engineEq = !usesExplicitChain(p);
    for (int c = 0; c < 2; ++c) {
        fxEnhShelfHigh_[c].setup(double(kSampleRate), 3500.0, 4.0 * double(p.timbre));
        fxEnhShelfLow_[c].setup(double(kSampleRate), 250.0, -2.0 * double(p.timbre));
    }
    for (int b = 0; b < 8; ++b) {
        const float gain = std::max(-12.0f, std::min(12.0f, p.eq[b]));
        fxEqGains_[b] = engineEq ? gain : 0.0f;
        for (int c = 0; c < 2; ++c) {
            const double A = std::pow(10.0, gain / 40.0);
            const double w = 2.0 * 3.14159265358979 * kEqFreq[b] / double(kSampleRate);
            const double cw = std::cos(w);
            const double sw = std::sin(w);
            const double alpha = sw / (2.0 * 0.9);
            const double a0 = 1.0 + alpha / A;
            fxEqCoef_[b][c][0] = float((1.0 + alpha * A) / a0);
            fxEqCoef_[b][c][1] = float((-2.0 * cw) / a0);
            fxEqCoef_[b][c][2] = float((1.0 - alpha * A) / a0);
            fxEqCoef_[b][c][3] = float((-2.0 * cw) / a0);
            fxEqCoef_[b][c][4] = float((1.0 - alpha / A) / a0);
            for (int k = 0; k < 3; ++k) {
                fxEqX_[b][c][k] = 0.0f;
                fxEqY_[b][c][k] = 0.0f;
            }
        }
    }
    // Music/memes must not be cut by the microphone noise gate.
    fxEnhDyn_.gate.limitDb = -80.0;
    fxEnhDyn_.compressor.limitDb = -18.0;
    fxEnhDyn_.compressor.invRatio = p.comp ? 0.5f : 1.0f;
    fxEnhDyn_.compressor.autoGain = p.comp ? 1 : 0;
}

void AudioEngine::processFxEnhancement(const float* in, float* out, std::size_t frames) {
    if (frames > kMaxBlockFrames) {
        std::memcpy(out, in, frames * kChannels * sizeof(float));
        return;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        double l = fxEnhHpf_[0].filter(double(in[i * 2]));
        l = fxEnhShelfLow_[0].filter(l);
        l = fxEnhShelfHigh_[0].filter(l);
        for (int b = 0; b < 8; ++b) {
            if (fxEqGains_[b] == 0.0f) continue;
            const float* c = fxEqCoef_[b][0];
            float* xh = fxEqX_[b][0];
            float* yh = fxEqY_[b][0];
            const float y = c[0] * float(l) + c[1] * xh[0] + c[2] * xh[1] -
                            c[3] * yh[0] - c[4] * yh[1];
            xh[1] = xh[0]; xh[0] = float(l);
            yh[1] = yh[0]; yh[0] = y;
            l = y;
        }
        enhL_[i] = float(std::isnan(l) || std::isinf(l) ? 0.0 : l);

        double r = fxEnhHpf_[1].filter(double(in[i * 2 + 1]));
        r = fxEnhShelfLow_[1].filter(r);
        r = fxEnhShelfHigh_[1].filter(r);
        for (int b = 0; b < 8; ++b) {
            if (fxEqGains_[b] == 0.0f) continue;
            const float* c = fxEqCoef_[b][1];
            float* xh = fxEqX_[b][1];
            float* yh = fxEqY_[b][1];
            const float y = c[0] * float(r) + c[1] * xh[0] + c[2] * xh[1] -
                            c[3] * yh[0] - c[4] * yh[1];
            xh[1] = xh[0]; xh[0] = float(r);
            yh[1] = yh[0]; yh[0] = y;
            r = y;
        }
        enhR_[i] = float(std::isnan(r) || std::isinf(r) ? 0.0 : r);
    }
    float* inBufs[2] = {enhL_, enhR_};
    float* outBufs[2] = {enhOutL_, enhOutR_};
    fxEnhDyn_.process(inBufs, outBufs, frames);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i * 2] = enhOutL_[i];
        out[i * 2 + 1] = enhOutR_[i];
    }
}

void AudioEngine::processEnhance(const float* in, float* out, std::size_t frames) {
    if (frames > kMaxBlockFrames) {
        std::memcpy(out, in, frames * kChannels * sizeof(float));
        return;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        double l = enhHpf_[0].filter(double(in[i * 2]));
        l = enhShelfLow_[0].filter(l);
        l = enhShelfHigh_[0].filter(l);
        // Hand-rolled peaking biquads (skip zero-gain bands).
        for (int b = 0; b < 8; ++b) {
            if (eqGains_[b] == 0.0f) continue;
            const float* c = eqCoef_[b][0];
            float* xh = eqX_[b][0];
            float* yh = eqY_[b][0];
            const float y = c[0] * float(l) + c[1] * xh[0] + c[2] * xh[1] -
                            c[3] * yh[0] - c[4] * yh[1];
            xh[1] = xh[0]; xh[0] = float(l);
            yh[1] = yh[0]; yh[0] = y;
            l = y;
        }
        // NaN/inf scrubber: one bad sample must not poison the pipeline.
        if (std::isnan(l) || std::isinf(l)) l = 0.0;
        enhL_[i] = float(l);

        double r = enhHpf_[1].filter(double(in[i * 2 + 1]));
        r = enhShelfLow_[1].filter(r);
        r = enhShelfHigh_[1].filter(r);
        for (int b = 0; b < 8; ++b) {
            if (eqGains_[b] == 0.0f) continue;
            const float* c = eqCoef_[b][1];
            float* xh = eqX_[b][1];
            float* yh = eqY_[b][1];
            const float y = c[0] * float(r) + c[1] * xh[0] + c[2] * xh[1] -
                            c[3] * yh[0] - c[4] * yh[1];
            xh[1] = xh[0]; xh[0] = float(r);
            yh[1] = yh[0]; yh[0] = y;
            r = y;
        }
        if (std::isnan(r) || std::isinf(r)) r = 0.0;
        enhR_[i] = float(r);
    }
    float* inBufs[2] = {enhL_, enhR_};
    float* outBufs[2] = {enhOutL_, enhOutR_};
    enhDyn_.process(inBufs, outBufs, frames);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i * 2] = enhOutL_[i];
        out[i * 2 + 1] = enhOutR_[i];
    }
}

float AudioEngine::duckingStep(std::size_t /*frames*/) {    if (!duckingOn()) {
        duck_ = 1.0f;
        return 1.0f;
    }
    const bool speaking = micLevel_.load(std::memory_order_relaxed) > kDuckThreshold;
    const float target = speaking ? kDuckGain : 1.0f;
    // Per-block smoothing (~10 ms blocks) - click-free transitions.
    duck_ += (target - duck_) * 0.25f;
    return duck_;
}

} // namespace vd
