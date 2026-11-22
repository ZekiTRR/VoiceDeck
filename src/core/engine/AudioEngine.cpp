#include "core/engine/AudioEngine.hpp"
#include "core/settings/Settings.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>

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
    if (mode < 0 || mode > 2) mode = 1;
    latencyMode_.store(mode, std::memory_order_relaxed);
}

AudioEngine::AudioEngine(const SoundBank& bank)
    : bank_(bank),
      aiLib_(Settings::appDataDir() / "AiVoice"),
      aiService_(aiLib_),
      aiStage_(int(kSampleRate), int(kMaxBlockFrames), aiControls_, aiService_) {
    // Fixed enhancement: 85 Hz rumble cut. Shelves/dynamics follow the
    // active preset (configureEnhancement).
    for (int c = 0; c < 2; ++c) enhHpf_[c].setup(double(kSampleRate), 85.0, 0.707);
    enhDyn_.configure(double(kSampleRate), kMaxBlockFrames, 2);
}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
    if (ctxOk_) return true;
    if (ma_context_init(NULL, 0, NULL, &ctx_) != MA_SUCCESS) return false;
    ctxOk_ = true;
    queue_.start();
    return true;
}

void AudioEngine::shutdown() {
    stopDevices();
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

    ma_device_config capCfg = ma_device_config_init(ma_device_type_capture);
    capCfg.capture.format = ma_format_f32;
    capCfg.capture.channels = kChannels;
    capCfg.capture.pDeviceID = &activeCaptureId_;
    capCfg.sampleRate = kSampleRate;
    capCfg.periodSizeInFrames = currentPeriodFrames();
    capCfg.dataCallback = &AudioEngine::captureDataCb;
    capCfg.pUserData = this;
    if (ma_device_init(&ctx_, &capCfg, &capDev_) != MA_SUCCESS) {
        err = "failed to open capture device: " + captureDevices_[captureIdx].name;
        return false;
    }

    ma_device_config playCfg = ma_device_config_init(ma_device_type_playback);
    playCfg.playback.format = ma_format_f32;
    playCfg.playback.channels = kChannels;
    playCfg.playback.pDeviceID = &activePlaybackId_;
    playCfg.sampleRate = kSampleRate;
    playCfg.periodSizeInFrames = currentPeriodFrames();
    playCfg.dataCallback = &AudioEngine::playbackDataCb;
    playCfg.pUserData = this;
    if (ma_device_init(&ctx_, &playCfg, &playDev_) != MA_SUCCESS) {
        err = "failed to open playback device: " + playbackDevices_[playbackIdx].name;
        ma_device_uninit(&capDev_);
        return false;
    }

    micRing_.reset();
    micPath_.reset();
    monitorRing_.reset();
    monPath_.reset();
    duck_ = 1.0f;
    capOverflows_.store(0, std::memory_order_relaxed);
    micUnderruns_.store(0, std::memory_order_relaxed);

    if (ma_device_start(&capDev_) != MA_SUCCESS || ma_device_start(&playDev_) != MA_SUCCESS) {
        err = "failed to start devices";
        ma_device_uninit(&playDev_);
        ma_device_uninit(&capDev_);
        return false;
    }
    capRunning_ = true;
    playRunning_ = true;
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
    cfg.dataCallback = &AudioEngine::monitorDataCb;
    cfg.pUserData = this;
    if (ma_device_init(&ctx_, &cfg, &monDev_) != MA_SUCCESS) {
        err = "failed to open monitor device: " + playbackDevices_[playbackIdx].name;
        return false;
    }

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
    curMonitorIdx_ = playbackIdx;
    return true;
}

void AudioEngine::stopMonitor() {
    monitorActive_.store(false, std::memory_order_relaxed);
    if (monRunning_) {
        ma_device_uninit(&monDev_);
        monRunning_ = false;
    }
    curMonitorIdx_ = std::size_t(-1);
}

void AudioEngine::drainCommands(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto remaining = [&]() {
        return std::chrono::steady_clock::now() < deadline;
    };
    // 1. Wait until the dispatcher moved everything into the audio ring.
    while (!queue_.idle()) {
        if (!remaining()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // 2. Wait for one more audio block so the playback callback consumed it.
    const std::uint64_t ticks = playTicks_.load(std::memory_order_relaxed);
    while (playTicks_.load(std::memory_order_relaxed) == ticks) {
        if (!remaining()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string AudioEngine::currentCaptureName() const {
    if (curCaptureIdx_ == std::size_t(-1) || curCaptureIdx_ >= captureDevices_.size()) return "-";
    return captureDevices_[curCaptureIdx_].name;
}

std::string AudioEngine::currentPlaybackName() const {
    if (curPlaybackIdx_ == std::size_t(-1) || curPlaybackIdx_ >= playbackDevices_.size()) return "-";
    return playbackDevices_[curPlaybackIdx_].name;
}

// --------------------------------------------------------------------------
// WASAPI callbacks. No allocations, no locks, no logging here.
// --------------------------------------------------------------------------

void AudioEngine::captureDataCb(ma_device* pDevice, void* /*pOutput*/, const void* pInput,
                                ma_uint32 frameCount) {
    auto* self = static_cast<AudioEngine*>(pDevice->pUserData);
    if (pInput == nullptr || frameCount == 0) return;
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
            case Command::Type::SetPreset:
                if (cmd.soundId < self->presets_.size()) {
                    self->configureEnhancement(self->presets_[cmd.soundId]);
                    self->voice_.applyPreset(int(cmd.soundId), self->presets_[cmd.soundId]);
                }
                break;
            case Command::Type::SetDsp:
                self->dspOn_.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetTransposeOverride:
                self->voice_.setTransposeOverride(cmd.value, cmd.soundId != 0);
                break;
            case Command::Type::SetFormantOverride:
                self->voice_.setFormantOverride(cmd.value, cmd.soundId != 0);
                break;
            case Command::Type::SetAi:
                self->aiControls_.enabled.store(cmd.value != 0.0f, std::memory_order_relaxed);
                break;
            case Command::Type::SetAiPitch:
                self->aiControls_.pitchShift.store(cmd.value, std::memory_order_relaxed);
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
    const std::size_t produced = self->micPath_.pull(self->micRing_, self->micBlock_, frames);
    if (produced < frames) {
        std::memset(self->micBlock_ + produced * kChannels, 0,
                    (frames - produced) * kChannels * sizeof(float));
        self->micUnderruns_.fetch_add(1, std::memory_order_relaxed);
    }

    // 3. Voice path. Enhancement (rumble cut, gate, compressor, timbre) is
    //    engine-level and always applies - same as Voice CTX's studio chain.
    //    The AI stage (optional) and the character chain sit after it.
    if (!self->bypassEnabled()) {
        std::memset(out, 0, frames * kChannels * sizeof(float));
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

    // 4. Soundboard, ducked under speech if enabled.
    //    NaN scrubber: zero any bad sample before it poisons the mix.
    for (std::size_t i = 0; i < frames * kChannels; ++i) {
        if (std::isnan(out[i]) || std::isinf(out[i])) out[i] = 0.0f;
    }
    const float duck = self->duckingStep(frames);
    self->mixer_.mix(out, frames, kChannels, duck);

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
            // User-enabled self-monitoring: full mix + preview sounds.
            for (std::size_t i = 0; i < frames * kChannels; ++i)
                self->previewBlock_[i] += out[i];
            if (self->monitorRing_.write(self->previewBlock_, frames * kChannels) <
                frames * kChannels)
                self->monOverflows_.fetch_add(1, std::memory_order_relaxed);
        }
    }
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
    for (int c = 0; c < 2; ++c) {
        enhShelfHigh_[c].setup(double(kSampleRate), 3500.0, 4.0 * double(p.timbre));
        enhShelfLow_[c].setup(double(kSampleRate), 250.0, -2.0 * double(p.timbre));
    }
    // Hand-rolled RBJ peaking biquads (same formula as VoiceMixer - stable).
    for (int b = 0; b < 8; ++b) {
        const float gain = std::max(-12.0f, std::min(12.0f, p.eq[b]));
        eqGains_[b] = gain;
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
