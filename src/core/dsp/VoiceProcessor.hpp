#pragma once

#include "core/dsp/Preset.hpp"

#include "Iir.h"
#include "Effects/decimator.h"
#include "Effects/pitchshifter.h"
#include "Utility/delayline.h"
#include "signalsmith-basics/crunch.h"
#include "signalsmith-basics/limiter.h"
#include "signalsmith-basics/reverb.h"
#include "signalsmith-stretch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vd {

enum class Space { None, Room, Hall, Cave, Helmet, Radio, Megaphone };
enum class Effect { None, Robot, Alien, Atari, Clone, Silence };
// Existing numeric values (None..Eq) are kept stable; new modules are appended.
enum class LabModule {
    None,
    Pitch,
    Formant,
    Vocoder,
    Bitcrusher,
    Ringmod,
    Distortion,
    Chorus,
    Reverb,
    Delay,
    Eq,
    Tremolo,
    Flanger,
    Phaser,
    Wahwah,
    Stutter,
    Stereowidth,
    Compressor,
    Octaver
};

constexpr std::size_t kLabModuleCount = 19;

// One element in the effect chain.
struct ChainItem {
    enum class Kind { Space, Effect, VoiceLab };
    Kind kind;
    Space space;
    Effect effect;
    LabModule module;
};

// One full voice-processing chain. Audio-thread only.
//
// Input arrives already enhanced (HPF/gate/comp at engine level, before the
// AI stage). The chain handles the character part:
//
// enhanced in -> Character (Stretch: transpose/formant/LFO)
//            -> ordered legacy/VoiceLab modules
//            -> Limiter -> dry/wet(strength) -> out
class VoiceChain {
public:
    static constexpr std::size_t kMaxBlock = 2048;

    explicit VoiceChain(double sampleRate);

    void configure(const VoicePreset& p);
    void process(const float* in, float* out, std::size_t frames, float transposeOv,
                 float formantOv);
    // Reconfigure the Stretch STFT for the given latency mode (0/1/2).
    void setStretchConfig(int blockSamples, int intervalSamples) {
        stretch_.configure(2, blockSamples, intervalSamples, /*splitComputation=*/true);
        stretch_.setFormantBase(float(150.0 / sr_));
        stretch_.reset();
        updateLatency();
    }
    void applySpace(Space space, const float* srcL, const float* srcR,
                    float* dstL, float* dstR, std::size_t frames);
    void applyEffect(Effect effect, float* dstL, float* dstR, std::size_t frames);
    void resetTail();

    double latencyMs() const { return latencyMs_; }
    double tailSeconds() const { return tailSeconds_; }

private:
    struct PresetParams {
        float transpose = 0.0f;
        float formant = 0.0f;
        float strength = 1.0f;
        float lfoRate = 0.0f;
        float lfoDepth = 0.0f;
        float eq[8] = {};
    };

    struct PeakingBand {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        void setup(double sampleRate, double centerHz, double gainDb);
        float process(float input);
        void reset();
    };

    void updateLatency();
    void configureSpace(Space space);
    void configureVoiceLabReverb(const VoiceLabParams& params);
    void configureVoiceLabVocoder(const VoiceLabParams& params);
    void processStretch(const float* srcL, const float* srcR,
                        float* dstL, float* dstR, std::size_t frames,
                        float transpose, float formant,
                        float formantBaseHz = 150.0f);
    void applyVoiceLab(LabModule module, const float* srcL, const float* srcR,
                       float* dstL, float* dstR, std::size_t frames,
                       float transpose, float formant);
    void resetClone();
    void resetOctaver();
    void resetStutter();

    double sr_ = 48000.0;
    PresetParams params_{};
    // Fixed, scalar-only snapshot indexed by LabModule (None=0 .. Octaver=18).
    std::array<VoiceLabParams, kLabModuleCount> labParams_{};
    // Whether a preset supplied an explicit entry for each module. The
    // standalone Eq module falls back to the legacy preset EQ without one.
    std::array<bool, kLabModuleCount> labParamsPresent_{};
    bool voiceLabChain_ = false;
    bool hasActiveChain_ = false;
    Space configuredSpace_ = Space::None;
    bool voiceLabReverbActive_ = false;
    bool usesReverb_ = false;
    bool usesClone_ = false;
    bool usesDelay_ = false;
    int stretchStages_ = 0;
    bool fastPath_ = false; // no stretch, no chain -> soft clipper, 0 ms
    double latencyMs_ = 0.0;
    double tailSeconds_ = 0.10;
    float lfoPhase_ = 0.0f;       // legacy block-rate pitch LFO
    float pitchLfoPhase_ = 0.0f; // explicit Pitch module vibrato

    // --- stage buffers (allocated once in ctor; configure/process never allocate)
    std::vector<float> inL_, inR_;  // de-interleaved (already enhanced) input
    std::vector<float> cL_, cR_;   // post character (stretch output or copy of in)
    std::vector<float> xL_, xR_;   // scratch (band-pass / low-pass)
    std::vector<float> spL_, spR_; // post module chain
    std::vector<float> fL_, fR_;   // post limiter

    // --- character (pitch/formant, Signalsmith Stretch)
    signalsmith::stretch::SignalsmithStretch<float> stretch_;
    static constexpr std::size_t kMaxChainItems = 24;
    std::array<ChainItem, kMaxChainItems> chain_{}; // ordered effect chain
    std::size_t chainSize_ = 0;

    // --- legacy space
    signalsmith::basics::ReverbFloat reverb_{400.0}; // cave needs > 200 ms room size
    signalsmith::basics::CrunchFloat crunch_;
    Iir::Butterworth::BandPass<2> bp_[2];
    Iir::Butterworth::LowPass<2> lpf_[2];

    // --- legacy effect
    daisysp::DelayLine<float, 512> comb_[2]; // robot comb (up to ~10 ms)
    daisysp::PitchShifter twin_[2];          // clone: +12 semitones twin
    daisysp::Decimator crush_[2];            // atari/bitcrusher
    float ringPhase_ = 0.0f;

    // --- VoiceLab modules
    static constexpr std::size_t kVocoderBands = 8;
    Iir::Butterworth::BandPass<2> vocoderAnalysis_[kVocoderBands];
    Iir::Butterworth::BandPass<2> vocoderCarrier_[kVocoderBands];
    float vocoderEnvelope_[kVocoderBands] = {};
    float vocoderCarrierEnvelope_[kVocoderBands] = {};
    float vocoderAttack_ = 0.0f;
    float vocoderRelease_ = 0.0f;
    float vocoderPhase_ = 0.0f;

    static constexpr std::size_t kFeedbackDelayCapacity = 38401; // 800 ms @ 48 kHz
    static constexpr std::size_t kChorusDelayCapacity = 4096;    // ~85 ms @ 48 kHz
    daisysp::DelayLine<float, kFeedbackDelayCapacity> feedbackDelay_[2];
    daisysp::DelayLine<float, kChorusDelayCapacity> chorusDelay_[2];
    float chorusPhase_ = 0.0f;

    // Every module has its own independent 8-band EQ, applied to its input
    // before the effect (the standalone Eq module *is* that EQ).
    static constexpr std::size_t kModuleEqBands = 8;
    PeakingBand moduleEq_[kLabModuleCount][kModuleEqBands][2];
    std::array<std::array<bool, kModuleEqBands>, kLabModuleCount> moduleEqActive_{};
    std::array<bool, kLabModuleCount> moduleEqAny_{};
    std::vector<float> eqScratchL_, eqScratchR_; // module-EQ input scratch

    PeakingBand eq_[8][2]; // legacy preset EQ (kept for compatibility)
    std::array<bool, 8> eqActive_{};

    // --- VoiceLab: tremolo / flanger / phaser
    float tremoloPhase_ = 0.0f;

    static constexpr std::size_t kFlangerDelayCapacity = 2048; // > 15 ms @ 48 kHz
    daisysp::DelayLine<float, kFlangerDelayCapacity> flangerDelay_[2];
    float flangerPhase_ = 0.0f;

    static constexpr std::size_t kPhaserStages = 6;
    float phaserX_[2][kPhaserStages] = {};
    float phaserY_[2][kPhaserStages] = {};
    float phaserPhase_ = 0.0f;

    // --- VoiceLab: wahwah
    Iir::Butterworth::BandPass<2> wahwahBand_[2];
    float wahwahPhase_ = 0.0f;

    // --- VoiceLab: stutter (fixed capture buffers, allocated in the ctor)
    std::vector<float> stutterBufL_, stutterBufR_;
    std::size_t stutterSlice_ = 0;     // current slice length in samples
    std::size_t stutterCapture_ = 0;   // samples captured so far
    std::size_t stutterPlayback_ = 0;  // position inside the captured slice
    int stutterRepeatsLeft_ = 0;
    int stutterRepeatsTotal_ = 1;

    // --- VoiceLab: compressor
    float compEnv_[2] = {};
    float compAttack_ = 0.0f;
    float compRelease_ = 0.0f;

    // --- VoiceLab: octaver (dedicated pitch shifters, not the Clone twin)
    daisysp::PitchShifter octave_[2];

    // --- master
    signalsmith::basics::LimiterFloat limiter_{10.0}; // max 10 ms lookahead
};

// A/B chains with a 20 ms crossfade on preset switch - the same trick Voice
// CTX uses for click-free effect switching. Audio-thread only.
class VoiceProcessor {
public:
    static constexpr std::size_t kMaxBlock = VoiceChain::kMaxBlock;
    static constexpr std::size_t kFadeFrames = 960; // 20 ms @ 48 kHz

    explicit VoiceProcessor(double sampleRate);

    // Audio thread: configure the inactive chain and crossfade to it.
    void applyPreset(int index, const VoicePreset& p);
    void setTransposeOverride(float semitones, bool enable);
    void setFormantOverride(float semitones, bool enable);
    // Reconfigures the Stretch STFT for the latency mode (0/1/2).
    // Audio thread only; the A/B crossfade absorbs the brief reset.
    void setLatencyMode(int mode);

    void process(const float* in, float* out, std::size_t frames);

    // Audio thread: drop reverb/delay/stretch tails (VAD sleep transition).
    void resetTail();

    // Any thread. `target` changes as soon as SetPreset is consumed, while
    // `active` changes only after the A/B crossfade completes.
    int activePresetIndex() const { return curIdx_.load(std::memory_order_relaxed); }
    int targetPresetIndex() const { return targetIdx_.load(std::memory_order_relaxed); }
    bool matchesPreset(int index) const {
        return targetIdx_.load(std::memory_order_relaxed) == index;
    }
    float latencyMs() const { return latencyMs_.load(std::memory_order_relaxed); }
    // Audio thread: longest tail among active and crossfade-target chains.
    double tailSeconds() const {
        return std::max(chains_[active_].tailSeconds(),
                        pending_ >= 0 ? chains_[pending_].tailSeconds() : 0.0);
    }

private:
    VoiceChain chains_[2];
    int active_ = 0;
    int pending_ = -1;
    int pendingIdx_ = -1;
    std::size_t fadePos_ = 0;
    double sr_ = 48000.0;
    float tOv_ = std::nanf("");
    float fOv_ = std::nanf("");
    std::vector<float> outA_, outB_; // crossfade scratch

    std::atomic<int> curIdx_{-1};
    std::atomic<int> targetIdx_{-1};
    std::atomic<float> latencyMs_{0.0f};
};

} // namespace vd
