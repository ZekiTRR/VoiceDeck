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

#include <cmath>
#include <cstddef>
#include <vector>

namespace vd {

enum class Space { None, Room, Hall, Cave, Helmet, Radio, Megaphone };
enum class Effect { None, Robot, Alien, Atari, Clone, Silence };

// One element in the effect chain.
struct ChainItem {
    enum class Kind { Space, Effect };
    Kind kind;
    Space space;
    Effect effect;
};

// One full voice-processing chain. Audio-thread only.
//
// Input arrives already enhanced (HPF/gate/comp at engine level, before the
// AI stage). The chain handles the character part:
//
// enhanced in -> Character (Stretch: transpose/formant/LFO)
//            -> Space (reverb / band-pass+crunch / low-pass)
//            -> Effect (robot comb+ringmod, alien shimmer, atari crush,
//                       clone twin, silence)
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
        stretch_.setFormantBase(150.0 / sr_);
        stretch_.reset();
        updateLatency();
    }
    void applySpace(Space space, const float* srcL, const float* srcR,
                    float* dstL, float* dstR, std::size_t frames);
    void applyEffect(Effect effect, float* dstL, float* dstR, std::size_t frames);
    void resetTail();

    double latencyMs() const { return latencyMs_; }

private:
    void updateLatency();
    void processEffect(std::size_t frames);

    double sr_ = 48000.0;
    VoicePreset p_{};
    Space space_ = Space::None;
    Effect effect_ = Effect::None;
    bool useStretch_ = false;
    double latencyMs_ = 0.0;
    float lfoPhase_ = 0.0f;

    // --- stage buffers (allocated once in ctor; process never allocates)
    std::vector<float> inL_, inR_;  // de-interleaved (already enhanced) input
    std::vector<float> cL_, cR_;   // post character (stretch output or copy of in)
    std::vector<float> xL_, xR_;   // scratch (band-pass / low-pass)
    std::vector<float> spL_, spR_; // post space
    std::vector<float> fL_, fR_;   // post limiter

    // --- character (pitch/formant, Signalsmith Stretch)
    signalsmith::stretch::SignalsmithStretch<float> stretch_;
    std::vector<ChainItem> chain_;  // ordered effect chain

    // --- space
    signalsmith::basics::ReverbFloat reverb_;
    signalsmith::basics::CrunchFloat crunch_;
    Iir::Butterworth::BandPass<2> bp_[2];
    Iir::Butterworth::LowPass<2> lpf_[2];

    // --- effect
    daisysp::DelayLine<float, 512> comb_[2]; // robot comb (up to ~10 ms)
    daisysp::PitchShifter twin_[2];          // clone: +12 semitones twin
    daisysp::Decimator crush_[2];            // atari bit-crush
    float ringPhase_ = 0.0f;

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

    // Any thread.
    int activePresetIndex() const { return curIdx_.load(std::memory_order_relaxed); }
    float latencyMs() const { return latencyMs_.load(std::memory_order_relaxed); }

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
    std::atomic<float> latencyMs_{0.0f};
};

} // namespace vd
