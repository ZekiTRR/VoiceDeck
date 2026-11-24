#include "core/dsp/VoiceProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <string_view>

namespace vd {

// ---------------------------------------------------------------------------
// VoiceChain
// ---------------------------------------------------------------------------

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kTwoPi = float(2.0 * kPi);

// Comb / ring-mod settings for the built-in effects.
constexpr float kRobotCombDelaySec = 0.0018f; // ~1.8 ms comb
constexpr float kRobotCombFeedback = 0.72f;
constexpr float kRobotRingHz = 55.0f;
constexpr float kRobotRingMix = 0.65f;
constexpr float kAlienRingHz = 85.0f;
constexpr float kAlienRingMix = 0.35f;
constexpr float kCloneTwinMix = 0.45f;
constexpr float kAtariOutGain = 1.3f;

// VoiceLab output levels are fixed; each module's strength is applied by the
// common dry/wet crossfade in applyVoiceLab().
constexpr float kVoiceLabBitGain = 1.15f;
constexpr float kChorusDry = 0.75f;
constexpr float kChorusWet = 0.45f;
constexpr float kFeedbackDelayWet = 0.35f;

constexpr std::array<std::string_view, 11> kLabModuleNames = {
    "none", "pitch", "formant", "vocoder", "bitcrusher", "ringmod",
    "distortion", "chorus", "reverb", "delay", "eq"
};

// Match the existing engine-level peaking EQ (Q = 0.9).
constexpr double kEqFrequency[8] = {200.0, 400.0, 800.0, 1600.0,
                                   3200.0, 6400.0, 10000.0, 12000.0};

Space parseSpace(const std::string& s) {
    if (s == "room") return Space::Room;
    if (s == "hall") return Space::Hall;
    if (s == "cave") return Space::Cave;
    if (s == "helmet") return Space::Helmet;
    if (s == "radio") return Space::Radio;
    if (s == "megaphone") return Space::Megaphone;
    return Space::None;
}

Effect parseEffect(const std::string& s) {
    if (s == "robot") return Effect::Robot;
    if (s == "alien") return Effect::Alien;
    if (s == "atari") return Effect::Atari;
    if (s == "clone") return Effect::Clone;
    if (s == "silence") return Effect::Silence;
    return Effect::None;
}

LabModule parseLabModule(const std::string& s) {
    if (s == "pitch") return LabModule::Pitch;
    if (s == "formant") return LabModule::Formant;
    if (s == "vocoder") return LabModule::Vocoder;
    if (s == "bitcrusher") return LabModule::Bitcrusher;
    if (s == "ringmod") return LabModule::Ringmod;
    if (s == "distortion") return LabModule::Distortion;
    if (s == "chorus") return LabModule::Chorus;
    if (s == "reverb") return LabModule::Reverb;
    if (s == "delay") return LabModule::Delay;
    if (s == "eq") return LabModule::Eq;
    return LabModule::None;
}

std::array<VoiceLabParams, 11> makeDefaultLabParams() {
    std::array<VoiceLabParams, 11> params{};
    for (std::size_t i = 0; i < params.size(); ++i)
        params[i] = defaultVoiceLabParams(kLabModuleNames[i]);
    return params;
}

inline float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

// Live character controls retain their original absolute-override semantics.
inline float voiceLabPitchSemitones(const VoiceLabParams& params, float transposeOv) {
    return std::clamp(std::isnan(transposeOv) ? params.value : transposeOv, -24.0f, 24.0f);
}

inline float voiceLabFormantSemitones(const VoiceLabParams& params, float formantOv) {
    return std::clamp(std::isnan(formantOv) ? params.value : formantOv, -24.0f, 24.0f);
}

inline bool voiceLabPitchActive(const VoiceLabParams& params, float transposeOv) {
    return voiceLabPitchSemitones(params, transposeOv) != 0.0f ||
           (params.value2 != 0.0f && params.value3 != 0.0f);
}

// Keep a hard stability ceiling even if a future caller bypasses preset clamps.
inline float delayFeedbackCoefficient(float feedback) {
    return std::clamp(feedback, 0.0f, 0.95f);
}

inline double voiceLabDelayTailSeconds(const VoiceLabParams& params) {
    const double delay = double(params.value) * 0.001;
    const double feedback = double(delayFeedbackCoefficient(params.value2));
    if (feedback <= 0.0) return delay;
    return delay + delay * std::log(0.001) / std::log(feedback);
}

inline float wrapPhase(float phase, float period) {
    // All per-sample oscillators advance by much less than one period.
    if (phase >= period) phase -= period;
    if (phase < 0.0f) phase += period;
    return phase;
}

inline float followEnvelope(float input, float& state, float attack, float release) {
    const float value = std::fabs(input);
    const float coefficient = value > state ? attack : release;
    state += coefficient * (value - state);
    return state;
}

// PolyBLEP-corrected sawtooth: a cheap, harmonic-rich vocoder carrier without
// the strong aliasing of a naive saw.
inline float polyBlep(float phase, float phaseStep) {
    if (phase < phaseStep) {
        const float t = phase / phaseStep;
        return t + t - t * t - 1.0f;
    }
    if (phase > 1.0f - phaseStep) {
        const float t = (phase - 1.0f) / phaseStep;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
} // namespace

VoiceChain::VoiceChain(double sampleRate)
    : sr_(sampleRate),
      stretch_{},
      bp_{},
      lpf_{},
      comb_{},
      twin_{},
      crush_{} {
    labParams_ = makeDefaultLabParams();
    const std::size_t n = kMaxBlock;
    inL_.resize(n); inR_.resize(n);
    cL_.resize(n);  cR_.resize(n);
    xL_.resize(n);  xR_.resize(n);
    spL_.resize(n); spR_.resize(n);
    fL_.resize(n);  fR_.resize(n);

    // One-shot module configuration (runs before audio starts, so it may allocate).
    // Stretch block size follows the engine's latency mode:
    //   0 = low (768/192, ~20ms), 1 = normal (1536/384, ~40ms), 2 = hq (2048/512, ~53ms)
    stretch_.configure(2, 1536, 384, /*splitComputation=*/true);
    stretch_.setFormantBase(float(150.0 / sr_));
    reverb_.configure(sr_, int(kMaxBlock), 2);
    crunch_.configure(sr_, int(kMaxBlock), 2);
    limiter_.configure(sr_, int(kMaxBlock), 2);
    limiter_.outputLimit = 0.708f; // ~-3 dBFS
    limiter_.attackMs = 5.0;
    limiter_.releaseMs = 60.0;

    resetClone();
    const std::size_t combDelay = std::max<std::size_t>(
        2, static_cast<std::size_t>(kRobotCombDelaySec * sr_));
    const float feedbackDelay = std::clamp(
        float(labParams_[std::size_t(LabModule::Delay)].value * 0.001 * sr_),
        1.0f, float(kFeedbackDelayCapacity - 1));
    for (int c = 0; c < 2; ++c) {
        comb_[c].Init();
        comb_[c].SetDelay(combDelay);
        crush_[c].Init();
        feedbackDelay_[c].Reset();
        feedbackDelay_[c].SetDelay(feedbackDelay);
        chorusDelay_[c].Reset();
        bp_[c].setup(sr_, 1000.0, 500.0);
        lpf_[c].setup(sr_, 2800.0);
    }

    configureVoiceLabVocoder(labParams_[std::size_t(LabModule::Vocoder)]);
    updateLatency();
}

void VoiceChain::updateLatency() {
    if (fastPath_) { // neutral preset: soft clipper only, zero lookahead
        latencyMs_ = 0.0;
        return;
    }
    double samples = double(limiter_.latencySamples());
    if (stretchStages_ > 0) {
        samples += double(stretchStages_) *
                   double(stretch_.inputLatency() + stretch_.outputLatency());
    }
    latencyMs_ = samples * 1000.0 / sr_;
}

// Rational tanh approximation: transparent at normal levels, never hard-clips.
static inline float softClip(float x) {
    const float ax = std::fabs(x);
    if (ax >= 3.0f) return x > 0.0f ? 1.0f : -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void VoiceChain::PeakingBand::setup(double sampleRate, double centerHz, double gainDb) {
    if (!std::isfinite(gainDb)) gainDb = 0.0;
    gainDb = std::clamp(gainDb, -12.0, 12.0);
    centerHz = std::clamp(centerHz, 1.0, sampleRate * 0.49);
    const double amplitude = std::pow(10.0, gainDb / 40.0);
    const double omega = 2.0 * kPi * centerHz / sampleRate;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double alpha = sine / (2.0 * 0.9);
    const double a0 = 1.0 + alpha / amplitude;
    b0 = float((1.0 + alpha * amplitude) / a0);
    b1 = float(-2.0 * cosine / a0);
    b2 = float((1.0 - alpha * amplitude) / a0);
    a1 = float(-2.0 * cosine / a0);
    a2 = float((1.0 - alpha / amplitude) / a0);
    reset();
}

float VoiceChain::PeakingBand::process(float input) {
    const float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = input;
    y2 = y1; y1 = output;
    return output;
}

void VoiceChain::PeakingBand::reset() {
    x1 = x2 = y1 = y2 = 0.0f;
}

void VoiceChain::configureSpace(Space space) {
    configuredSpace_ = space;
    voiceLabReverbActive_ = false;
    reverb_.dry = 1.0;
    reverb_.early = 1.5;
    reverb_.detune = 2.0;
    reverb_.lowDampRate = 1.5;
    reverb_.highDampRate = 2.5;
    switch (space) {
        case Space::Room:
            reverb_.wet = 0.25; reverb_.roomMs = 40; reverb_.rt20 = 0.8;
            reverb_.lowCutHz = 120; reverb_.highCutHz = 12000;
            break;
        case Space::Hall:
            reverb_.wet = 0.35; reverb_.roomMs = 120; reverb_.rt20 = 2.0;
            reverb_.lowCutHz = 100; reverb_.highCutHz = 9000;
            break;
        case Space::Cave:
            reverb_.wet = 0.45; reverb_.roomMs = 250; reverb_.rt20 = 3.2;
            reverb_.lowCutHz = 150; reverb_.highCutHz = 6000;
            break;
        case Space::Helmet:
            reverb_.wet = 0.12; reverb_.roomMs = 15; reverb_.rt20 = 0.3;
            reverb_.lowCutHz = 80; reverb_.highCutHz = 12000;
            for (int c = 0; c < 2; ++c) lpf_[c].setup(sr_, 2800.0);
            break;
        case Space::Radio:
            reverb_.wet = 0.08; reverb_.roomMs = 8; reverb_.rt20 = 0.2;
            reverb_.lowCutHz = 120; reverb_.highCutHz = 12000;
            for (int c = 0; c < 2; ++c) bp_[c].setup(sr_, 1200.0, 1500.0);
            crunch_.drive = 6.0; crunch_.fuzz = 0.05; crunch_.toneHz = 2800.0;
            crunch_.outGain = 0.8;
            break;
        case Space::Megaphone:
            reverb_.wet = 0.15; reverb_.roomMs = 12; reverb_.rt20 = 0.25;
            reverb_.lowCutHz = 120; reverb_.highCutHz = 12000;
            for (int c = 0; c < 2; ++c) bp_[c].setup(sr_, 900.0, 1400.0);
            crunch_.drive = 10.0; crunch_.fuzz = 0.15; crunch_.toneHz = 2400.0;
            crunch_.outGain = 0.55;
            break;
        case Space::None:
        default:
            break;
    }
}

void VoiceChain::configureVoiceLabReverb(const VoiceLabParams& params) {
    voiceLabReverbActive_ = true;
    reverb_.dry = 1.0;
    reverb_.wet = 0.32;
    // Size 0..1 maps to 0..200 ms; the default 0.55 retains the old 110 ms room.
    reverb_.roomMs = std::clamp(double(params.value2) * 200.0, 10.0, 400.0);
    reverb_.rt20 = params.value;
    reverb_.early = 1.25;
    reverb_.detune = 2.0;
    reverb_.lowCutHz = 90;
    reverb_.highCutHz = 10000;
    reverb_.lowDampRate = 1.0 + params.value3;
    reverb_.highDampRate = 2.0 + params.value3;
}

void VoiceChain::configureVoiceLabVocoder(const VoiceLabParams& params) {
    for (std::size_t b = 0; b < kVocoderBands; ++b) {
        const double center = std::min(double(params.value) * std::pow(2.0, double(b)),
                                       sr_ * 0.42);
        const double width = std::max(20.0, center * 0.65);
        vocoderAnalysis_[b].setup(sr_, center, width);
        vocoderCarrier_[b].setup(sr_, center, width);
    }
    const double attackSeconds = double(params.value2) * 0.001;
    const double releaseSeconds = double(params.value3) * 0.001;
    vocoderAttack_ = float(1.0 - std::exp(-1.0 / (attackSeconds * sr_)));
    vocoderRelease_ = float(1.0 - std::exp(-1.0 / (releaseSeconds * sr_)));
}

void VoiceChain::resetClone() {
    for (int c = 0; c < 2; ++c) {
        // PitchShifter::Init() does not initialise every scalar. Recreate it
        // over zeroed storage so both startup and tail-reset are deterministic.
        twin_[c].~PitchShifter();
        std::memset(&twin_[c], 0, sizeof(twin_[c]));
        new (static_cast<void*>(&twin_[c])) daisysp::PitchShifter();
        twin_[c].Init(float(sr_), /*quantize_semitones=*/true);
        twin_[c].SetDelSize(1440); // ~30 ms internal window
        twin_[c].SetTransposition(12.0f);
    }
}

void VoiceChain::configure(const VoicePreset& p) {
    // Copy only scalar/EQ values. The module map is read by reference and
    // flattened into a fixed array: no preset strings, map nodes, or heap storage
    // are copied on the audio thread.
    params_.transpose = finiteOr(p.transpose, 0.0f);
    params_.formant = finiteOr(p.formant, 0.0f);
    params_.strength = finiteOr(p.strength, 1.0f);
    params_.lfoRate = finiteOr(p.lfoRate, 0.0f);
    params_.lfoDepth = finiteOr(p.lfoDepth, 0.0f);
    for (int b = 0; b < 8; ++b) params_.eq[b] = finiteOr(p.eq[b], 0.0f);

    auto labParams = makeDefaultLabParams();
    bool hasPitchParams = false;
    bool hasFormantParams = false;
    for (const auto& entry : p.moduleParams) {
        const std::string_view name(entry.first);
        for (std::size_t i = 1; i < labParams.size(); ++i) {
            if (name != kLabModuleNames[i]) continue;
            VoiceLabParams& dst = labParams[i];
            const VoiceLabParams& src = entry.second;
            dst.mix = finiteOr(src.mix, dst.mix);
            dst.value = finiteOr(src.value, dst.value);
            dst.value2 = finiteOr(src.value2, dst.value2);
            dst.value3 = finiteOr(src.value3, dst.value3);
            if (i == std::size_t(LabModule::Pitch)) hasPitchParams = true;
            if (i == std::size_t(LabModule::Formant)) hasFormantParams = true;
            break;
        }
    }
    if (!hasPitchParams) {
        VoiceLabParams& pitch = labParams[std::size_t(LabModule::Pitch)];
        pitch.value = params_.transpose;
        pitch.value2 = params_.lfoDepth;
        pitch.value3 = params_.lfoRate;
    }
    if (!hasFormantParams)
        labParams[std::size_t(LabModule::Formant)].value = params_.formant;

    for (std::size_t i = 1; i < labParams.size(); ++i) {
        VoiceLabParams& value = labParams[i];
        value.mix = std::clamp(value.mix, 0.0f, 1.0f);
        switch (LabModule(i)) {
            case LabModule::Pitch:
                value.value = std::clamp(value.value, -24.0f, 24.0f);
                value.value2 = std::clamp(value.value2, 0.0f, 12.0f);
                value.value3 = std::clamp(value.value3, 0.0f, 10.0f);
                break;
            case LabModule::Formant:
                value.value = std::clamp(value.value, -24.0f, 24.0f);
                value.value2 = std::clamp(value.value2, 70.0f, 250.0f);
                break;
            case LabModule::Vocoder:
                value.value = std::clamp(value.value, 30.0f, 500.0f);
                value.value2 = std::clamp(value.value2, 1.0f, 50.0f);
                value.value3 = std::clamp(value.value3, 10.0f, 500.0f);
                break;
            case LabModule::Bitcrusher:
                value.value = std::clamp(value.value, 2.0f, 12.0f);
                value.value2 = std::clamp(value.value2, 0.1f, 1.0f);
                break;
            case LabModule::Ringmod:
                value.value = std::clamp(value.value, 20.0f, 1200.0f);
                value.value2 = std::clamp(value.value2, 0.0f, 1.0f);
                break;
            case LabModule::Distortion:
                value.value = std::clamp(value.value, 0.0f, 1.0f);
                break;
            case LabModule::Chorus:
                value.value = std::clamp(value.value, 0.05f, 5.0f);
                value.value2 = std::clamp(value.value2, 0.0f, 20.0f);
                value.value3 = std::clamp(value.value3, 8.0f, 50.0f);
                break;
            case LabModule::Reverb:
                value.value = std::clamp(value.value, 0.1f, 6.0f);
                value.value2 = std::clamp(value.value2, 0.1f, 1.0f);
                value.value3 = std::clamp(value.value3, 0.0f, 1.0f);
                break;
            case LabModule::Delay:
                value.value = std::clamp(value.value, 20.0f, 800.0f);
                value.value2 = std::clamp(value.value2, 0.0f, 0.9f);
                break;
            case LabModule::None:
            case LabModule::Eq:
            default:
                break;
        }
    }
    labParams_ = labParams;

    voiceLabChain_ = usesExplicitChain(p);
    hasActiveChain_ = false;
    configuredSpace_ = Space::None;
    voiceLabReverbActive_ = false;
    chainSize_ = 0;
    const auto append = [this](ChainItem item) {
        if (chainSize_ < kMaxChainItems) chain_[chainSize_++] = item;
    };

    // Parse directly from the caller's chain. If non-empty, it overrides
    // space/effect. VoiceLab tokens suppress the automatic pitch/formant stage;
    // only their explicit position in this list can invoke those processors.
    if (!p.chain.empty()) {
        for (const auto& name : p.chain) {
            if (chainSize_ == kMaxChainItems) break;
            if (isVoiceLabModule(name)) {
                const LabModule module = parseLabModule(name);
                append({ChainItem::Kind::VoiceLab, Space::None, Effect::None, module});
                continue;
            }
            const Space space = parseSpace(name);
            const Effect effect = parseEffect(name);
            if (space != Space::None) {
                append({ChainItem::Kind::Space, space, Effect::None, LabModule::None});
            } else if (effect != Effect::None) {
                append({ChainItem::Kind::Effect, Space::None, effect, LabModule::None});
            }
        }
    } else if (!voiceLabChain_) {
        // Fallback to single space + effect only for legacy presets. An
        // explicit empty VoiceLab chain is a true direct/bypass chain.
        const Space space = parseSpace(p.space);
        const Effect effect = parseEffect(p.effect);
        if (space != Space::None)
            append({ChainItem::Kind::Space, space, Effect::None, LabModule::None});
        if (effect != Effect::None)
            append({ChainItem::Kind::Effect, Space::None, effect, LabModule::None});
    }

    usesReverb_ = false;
    usesClone_ = false;
    usesDelay_ = false;
    tailSeconds_ = 0.10;
    const auto addTail = [this](double seconds) {
        if (std::isfinite(seconds)) tailSeconds_ = std::max(tailSeconds_, seconds);
    };
    for (std::size_t i = 0; i < chainSize_; ++i) {
        const ChainItem& item = chain_[i];
        if (item.kind == ChainItem::Kind::Space) {
            hasActiveChain_ = true;
            switch (item.space) {
                case Space::Room: usesReverb_ = true; addTail(2.4); break;
                case Space::Hall: usesReverb_ = true; addTail(6.0); break;
                case Space::Cave: usesReverb_ = true; addTail(9.6); break;
                case Space::Helmet: usesReverb_ = true; addTail(0.9); break;
                case Space::Radio: addTail(0.6); break;
                case Space::Megaphone: addTail(0.75); break;
                case Space::None: default: break;
            }
        }
        if (item.kind == ChainItem::Kind::VoiceLab) {
            const VoiceLabParams& moduleParams =
                labParams_[std::size_t(item.module)];
            if (moduleParams.mix <= 0.0f) continue;
            hasActiveChain_ = true;
            if (item.module == LabModule::Reverb) {
                usesReverb_ = true;
                addTail(double(moduleParams.value) * 3.0); // RT60, matching the reverb
            } else if (item.module == LabModule::Delay) {
                usesDelay_ = true;
                addTail(voiceLabDelayTailSeconds(moduleParams));
            } else if (item.module == LabModule::Chorus) {
                addTail(std::max(0.3, double(moduleParams.value2 + moduleParams.value3) * 0.001));
            }
        }
        if (item.kind == ChainItem::Kind::Effect) {
            hasActiveChain_ = true;
            if (item.effect == Effect::Clone) usesClone_ = true;
            if (item.effect == Effect::Robot) addTail(0.2);
        }
    }

    // Prepare the first reverb/space settings now, so its first block does not
    // fade in from an unrelated preset's space. Later occurrences update their
    // own parameters in applySpace()/applyVoiceLab().
    for (std::size_t i = 0; i < chainSize_; ++i) {
        const ChainItem& item = chain_[i];
        if (item.kind == ChainItem::Kind::Space) {
            configureSpace(item.space);
            break;
        }
        if (item.kind == ChainItem::Kind::VoiceLab && item.module == LabModule::Reverb &&
            labParams_[std::size_t(item.module)].mix > 0.0f) {
            configureVoiceLabReverb(labParams_[std::size_t(item.module)]);
            break;
        }
    }

    const std::size_t combDelay = std::max<std::size_t>(
        2, static_cast<std::size_t>(kRobotCombDelaySec * sr_));
    const float feedbackDelay = std::clamp(
        float(labParams_[std::size_t(LabModule::Delay)].value * 0.001 * sr_),
        1.0f, float(kFeedbackDelayCapacity - 1));
    for (int c = 0; c < 2; ++c) {
        comb_[c].Init();
        comb_[c].SetDelay(combDelay);
        feedbackDelay_[c].SetDelay(feedbackDelay);
    }

    for (int b = 0; b < 8; ++b) {
        const float gain = std::clamp(params_.eq[b], -12.0f, 12.0f);
        eqActive_[b] = gain != 0.0f;
        for (int c = 0; c < 2; ++c) eq_[b][c].setup(sr_, kEqFrequency[b], gain);
    }
    configureVoiceLabVocoder(labParams_[std::size_t(LabModule::Vocoder)]);

    stretchStages_ = 0;
    if (voiceLabChain_) {
        for (std::size_t i = 0; i < chainSize_; ++i) {
            const ChainItem& item = chain_[i];
            if (item.kind != ChainItem::Kind::VoiceLab) continue;
            const VoiceLabParams& moduleParams =
                labParams_[std::size_t(item.module)];
            if (moduleParams.mix <= 0.0f) continue;
            if (item.module == LabModule::Pitch &&
                voiceLabPitchActive(moduleParams, std::nanf(""))) {
                ++stretchStages_;
            } else if (item.module == LabModule::Formant &&
                       voiceLabFormantSemitones(moduleParams, std::nanf("")) != 0.0f) {
                ++stretchStages_;
            }
        }
    } else if (params_.transpose != 0.0f || params_.formant != 0.0f ||
               params_.lfoDepth != 0.0f) {
        stretchStages_ = 1;
    }
    fastPath_ = !hasActiveChain_ && stretchStages_ == 0 && params_.strength >= 0.999f;
    ringPhase_ = 0.0f;
    lfoPhase_ = 0.0f;
    pitchLfoPhase_ = 0.0f;
    updateLatency();
}

void VoiceChain::processStretch(const float* srcL, const float* srcR,
                                float* dstL, float* dstR, std::size_t frames,
                                float transpose, float formant, float formantBaseHz) {
    stretch_.setTransposeSemitones(transpose);
    stretch_.setFormantFactor(float(std::pow(2.0, double(formant) / 12.0)), false);
    stretch_.setFormantBase(float(formantBaseHz / sr_));
    const float* input[2] = {srcL, srcR};
    float* output[2] = {dstL, dstR};
    stretch_.process(input, int(frames), output, int(frames));
}

void VoiceChain::applySpace(Space space, const float* srcL, const float* srcR,
                            float* dstL, float* dstR, std::size_t frames) {
    if (space != Space::None &&
        (voiceLabReverbActive_ || configuredSpace_ != space)) {
        configureSpace(space);
    }
    switch (space) {
        case Space::Room:
        case Space::Hall:
        case Space::Cave:
            {
                float* input[2] = {const_cast<float*>(srcL), const_cast<float*>(srcR)};
                float* output[2] = {dstL, dstR};
                reverb_.process(input, output, frames);
            }
            break;
        case Space::Helmet:
            for (std::size_t i = 0; i < frames; ++i) {
                xL_[i] = float(lpf_[0].filter(double(srcL[i])));
                xR_[i] = float(lpf_[1].filter(double(srcR[i])));
            }
            {
                float* input[2] = {xL_.data(), xR_.data()};
                float* output[2] = {dstL, dstR};
                reverb_.process(input, output, frames);
            }
            break;
        case Space::Radio:
        case Space::Megaphone:
            for (std::size_t i = 0; i < frames; ++i) {
                xL_[i] = float(bp_[0].filter(double(srcL[i])));
                xR_[i] = float(bp_[1].filter(double(srcR[i])));
            }
            {
                float* input[2] = {xL_.data(), xR_.data()};
                float* output[2] = {dstL, dstR};
                crunch_.process(input, output, frames);
            }
            break;
        case Space::None:
        default:
            if (dstL != srcL)
                std::memcpy(dstL, srcL, frames * sizeof(float));
            if (dstR != srcR)
                std::memcpy(dstR, srcR, frames * sizeof(float));
            break;
    }
}

void VoiceChain::applyEffect(Effect effect, float* dstL, float* dstR, std::size_t frames) {
    switch (effect) {
        case Effect::Robot: {
            const float ringStep = float(2.0 * kPi * kRobotRingHz / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                for (int c = 0; c < 2; ++c) {
                    float* sample = (c == 0) ? &dstL[i] : &dstR[i];
                    const float input = *sample;
                    const float delayed = comb_[c].Read();
                    comb_[c].Write(input + kRobotCombFeedback * delayed);
                    const float carrier = float(std::sin(double(ringPhase_)));
                    const float voice = input + 0.45f * delayed;
                    *sample = voice * (1.0f - kRobotRingMix) + voice * carrier * kRobotRingMix;
                }
                ringPhase_ = wrapPhase(ringPhase_ + ringStep, kTwoPi);
            }
            break;
        }
        case Effect::Alien: {
            const float ringStep = float(2.0 * kPi * kAlienRingHz / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                const float carrier = float(std::sin(double(ringPhase_)));
                dstL[i] = dstL[i] * (1.0f - kAlienRingMix) +
                          dstL[i] * carrier * kAlienRingMix;
                dstR[i] = dstR[i] * (1.0f - kAlienRingMix) +
                          dstR[i] * carrier * kAlienRingMix;
                ringPhase_ = wrapPhase(ringPhase_ + ringStep, kTwoPi);
            }
            break;
        }
        case Effect::Atari:
            for (int c = 0; c < 2; ++c) {
                crush_[c].SetDownsampleFactor(0.75f);
                crush_[c].SetBitsToCrush(4);
            }
            for (std::size_t i = 0; i < frames; ++i) {
                dstL[i] = crush_[0].Process(dstL[i]) * kAtariOutGain;
                dstR[i] = crush_[1].Process(dstR[i]) * kAtariOutGain;
            }
            break;
        case Effect::Clone:
            for (int c = 0; c < 2; ++c) twin_[c].SetTransposition(12.0f);
            for (std::size_t i = 0; i < frames; ++i) {
                dstL[i] = dstL[i] + kCloneTwinMix * twin_[0].Process(dstL[i]);
                dstR[i] = dstR[i] + kCloneTwinMix * twin_[1].Process(dstR[i]);
            }
            break;
        case Effect::Silence:
            std::fill(dstL, dstL + frames, 0.0f);
            std::fill(dstR, dstR + frames, 0.0f);
            break;
        case Effect::None:
        default:
            break;
    }
}

void VoiceChain::applyVoiceLab(LabModule module, const float* srcL, const float* srcR,
                               float* dstL, float* dstR, std::size_t frames,
                               float transposeOv, float formantOv) {
    const VoiceLabParams& moduleParams = labParams_[std::size_t(module)];
    const float mix = moduleParams.mix;
    if (mix <= 0.0f) {
        std::memcpy(dstL, srcL, frames * sizeof(float));
        std::memcpy(dstR, srcR, frames * sizeof(float));
        return;
    }

    switch (module) {
        case LabModule::Pitch: {
            float pitch = voiceLabPitchSemitones(moduleParams, transposeOv);
            if (moduleParams.value2 != 0.0f && moduleParams.value3 != 0.0f) {
                pitchLfoPhase_ += float(2.0 * kPi * double(moduleParams.value3) *
                                        double(frames) / sr_);
                pitchLfoPhase_ = float(std::fmod(double(pitchLfoPhase_), double(kTwoPi)));
                if (pitchLfoPhase_ < 0.0f) pitchLfoPhase_ += kTwoPi;
                pitch += moduleParams.value2 * float(std::sin(double(pitchLfoPhase_)));
            }
            pitch = std::clamp(pitch, -24.0f, 24.0f);
            if (voiceLabPitchActive(moduleParams, transposeOv)) {
                processStretch(srcL, srcR, dstL, dstR, frames, pitch, 0.0f);
            } else {
                std::memcpy(dstL, srcL, frames * sizeof(float));
                std::memcpy(dstR, srcR, frames * sizeof(float));
            }
            break;
        }
        case LabModule::Formant: {
            const float formant = voiceLabFormantSemitones(moduleParams, formantOv);
            if (formant != 0.0f) {
                processStretch(srcL, srcR, dstL, dstR, frames, 0.0f, formant,
                               moduleParams.value2);
            } else {
                std::memcpy(dstL, srcL, frames * sizeof(float));
                std::memcpy(dstR, srcR, frames * sizeof(float));
            }
            break;
        }
        case LabModule::Vocoder: {
            const float phaseStep = float(moduleParams.value / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                float carrier = 2.0f * vocoderPhase_ - 1.0f;
                carrier -= polyBlep(vocoderPhase_, phaseStep);
                const float modulator = 0.5f * (srcL[i] + srcR[i]);
                float bandSum = 0.0f;
                for (std::size_t b = 0; b < kVocoderBands; ++b) {
                    const float analysis = float(vocoderAnalysis_[b].filter(double(modulator)));
                    const float carrierBand = float(vocoderCarrier_[b].filter(double(carrier)));
                    const float modulatorEnvelope = followEnvelope(
                        analysis, vocoderEnvelope_[b], vocoderAttack_, vocoderRelease_);
                    const float carrierEnvelope = followEnvelope(
                        carrierBand, vocoderCarrierEnvelope_[b], vocoderAttack_, vocoderRelease_);
                    const float gain = std::clamp(
                        modulatorEnvelope / std::max(carrierEnvelope, 0.003f), 0.0f, 4.0f);
                    bandSum += carrierBand * gain;
                }
                // A little direct speech keeps consonants intelligible; the
                // harmonically rich carrier supplies the robotic formants.
                dstL[i] = srcL[i] * 0.12f + bandSum * 0.65f;
                dstR[i] = srcR[i] * 0.12f + bandSum * 0.65f;
                vocoderPhase_ = wrapPhase(vocoderPhase_ + phaseStep, 1.0f);
            }
            break;
        }
        case LabModule::Bitcrusher: {
            // DaisySP's argument is the number of bits shifted off, while the
            // UI exposes the resulting bit depth. 6-bit depth => shift 10.
            const auto bits = static_cast<std::uint8_t>(
                16 - std::lround(moduleParams.value));
            for (int c = 0; c < 2; ++c) {
                crush_[c].SetDownsampleFactor(moduleParams.value2);
                crush_[c].SetBitsToCrush(bits);
            }
            for (std::size_t i = 0; i < frames; ++i) {
                dstL[i] = crush_[0].Process(srcL[i]) * kVoiceLabBitGain;
                dstR[i] = crush_[1].Process(srcR[i]) * kVoiceLabBitGain;
            }
            break;
        }
        case LabModule::Ringmod: {
            const float ringStep = float(2.0 * kPi * moduleParams.value / sr_);
            const float depth = moduleParams.value2;
            for (std::size_t i = 0; i < frames; ++i) {
                const float carrier = float(std::sin(double(ringPhase_)));
                const float modulator = (1.0f - depth) + depth * carrier;
                dstL[i] = srcL[i] * modulator;
                dstR[i] = srcR[i] * modulator;
                ringPhase_ = wrapPhase(ringPhase_ + ringStep, kTwoPi);
            }
            break;
        }
        case LabModule::Distortion: {
            const float drive = moduleParams.value;
            if (drive <= 0.0f) {
                std::memcpy(dstL, srcL, frames * sizeof(float));
                std::memcpy(dstR, srcR, frames * sizeof(float));
                break;
            }
            // The default 0.55 reproduces the previous 2.4/1.8 drive stages.
            const float softGain = 0.2f + 4.0f * drive;
            const float hardGain = 0.75f * softGain;
            const float hardMix = 0.15f + (0.30f / 0.55f) * drive;
            for (std::size_t i = 0; i < frames; ++i) {
                const float leftSoft = softClip(srcL[i] * softGain);
                const float rightSoft = softClip(srcR[i] * softGain);
                const float leftHard = std::clamp(srcL[i] * hardGain, -1.0f, 1.0f);
                const float rightHard = std::clamp(srcR[i] * hardGain, -1.0f, 1.0f);
                dstL[i] = leftSoft * (1.0f - hardMix) + leftHard * hardMix;
                dstR[i] = rightSoft * (1.0f - hardMix) + rightHard * hardMix;
            }
            break;
        }
        case LabModule::Chorus: {
            const float maxDelay = float(kChorusDelayCapacity - 2);
            const float base = std::min(float(moduleParams.value3 * 0.001 * sr_), maxDelay);
            const float depth = std::min(float(moduleParams.value2 * 0.001 * sr_),
                                         maxDelay - base);
            const float phaseStep = float(moduleParams.value / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                const double angle = 2.0 * kPi * double(chorusPhase_);
                const float leftMod = float(std::sin(angle));
                const float rightMod = float(std::cos(angle)); // quarter-cycle offset
                const float leftDelayed = chorusDelay_[0].ReadHermite(base + depth * leftMod);
                const float rightDelayed = chorusDelay_[1].ReadHermite(base + depth * rightMod);
                chorusDelay_[0].Write(srcL[i]);
                chorusDelay_[1].Write(srcR[i]);
                dstL[i] = srcL[i] * kChorusDry + leftDelayed * kChorusWet;
                dstR[i] = srcR[i] * kChorusDry + rightDelayed * kChorusWet;
                chorusPhase_ = wrapPhase(chorusPhase_ + phaseStep, 1.0f);
            }
            break;
        }
        case LabModule::Reverb:
            if (!voiceLabReverbActive_) configureVoiceLabReverb(moduleParams);
            {
                float* input[2] = {const_cast<float*>(srcL), const_cast<float*>(srcR)};
                float* output[2] = {dstL, dstR};
                reverb_.process(input, output, frames);
            }
            break;
        case LabModule::Delay: {
            const float feedback = delayFeedbackCoefficient(moduleParams.value2);
            for (std::size_t i = 0; i < frames; ++i) {
                const float leftDelayed = feedbackDelay_[0].Read();
                const float rightDelayed = feedbackDelay_[1].Read();
                feedbackDelay_[0].Write(srcL[i] + leftDelayed * feedback);
                feedbackDelay_[1].Write(srcR[i] + rightDelayed * feedback);
                dstL[i] = srcL[i] + leftDelayed * kFeedbackDelayWet;
                dstR[i] = srcR[i] + rightDelayed * kFeedbackDelayWet;
            }
            break;
        }
        case LabModule::Eq:
            // params_.eq retains the preset's eight band values; the common
            // crossfade below applies Eq.mix at this module's chain position.
            for (int c = 0; c < 2; ++c) {
                const float* input = (c == 0) ? srcL : srcR;
                float* output = (c == 0) ? dstL : dstR;
                for (std::size_t i = 0; i < frames; ++i) {
                    float sample = input[i];
                    for (int b = 0; b < 8; ++b) {
                        if (eqActive_[b]) sample = eq_[b][c].process(sample);
                    }
                    output[i] = sample;
                }
            }
            break;
        case LabModule::None:
        default:
            std::memcpy(dstL, srcL, frames * sizeof(float));
            std::memcpy(dstR, srcR, frames * sizeof(float));
            break;
    }

    if (mix < 1.0f) {
        for (std::size_t i = 0; i < frames; ++i) {
            dstL[i] = srcL[i] + (dstL[i] - srcL[i]) * mix;
            dstR[i] = srcR[i] + (dstR[i] - srcR[i]) * mix;
        }
    }
}

void VoiceChain::resetTail() {
    stretch_.reset();
    if (usesReverb_) reverb_.reset();
    crunch_.reset();
    limiter_.reset();
    if (usesClone_) resetClone();

    const std::size_t combDelay = std::max<std::size_t>(
        2, static_cast<std::size_t>(kRobotCombDelaySec * sr_));
    const float feedbackDelay = std::clamp(
        float(labParams_[std::size_t(LabModule::Delay)].value * 0.001 * sr_),
        1.0f, float(kFeedbackDelayCapacity - 1));
    for (int c = 0; c < 2; ++c) {
        bp_[c].reset();
        lpf_[c].reset();
        comb_[c].Reset();
        comb_[c].SetDelay(combDelay);
        crush_[c].Init();
        if (usesDelay_) {
            feedbackDelay_[c].Reset();
            feedbackDelay_[c].SetDelay(feedbackDelay);
        }
        chorusDelay_[c].Reset();
    }
    for (std::size_t b = 0; b < kVocoderBands; ++b) {
        vocoderAnalysis_[b].reset();
        vocoderCarrier_[b].reset();
        vocoderEnvelope_[b] = 0.0f;
        vocoderCarrierEnvelope_[b] = 0.0f;
    }
    for (int b = 0; b < 8; ++b) {
        for (int c = 0; c < 2; ++c) eq_[b][c].reset();
    }

    std::fill(inL_.begin(), inL_.end(), 0.0f);
    std::fill(inR_.begin(), inR_.end(), 0.0f);
    std::fill(cL_.begin(), cL_.end(), 0.0f);
    std::fill(cR_.begin(), cR_.end(), 0.0f);
    std::fill(xL_.begin(), xL_.end(), 0.0f);
    std::fill(xR_.begin(), xR_.end(), 0.0f);
    std::fill(spL_.begin(), spL_.end(), 0.0f);
    std::fill(spR_.begin(), spR_.end(), 0.0f);
    std::fill(fL_.begin(), fL_.end(), 0.0f);
    std::fill(fR_.begin(), fR_.end(), 0.0f);
    lfoPhase_ = 0.0f;
    pitchLfoPhase_ = 0.0f;
    ringPhase_ = 0.0f;
    vocoderPhase_ = 0.0f;
    chorusPhase_ = 0.0f;
}

void VoiceChain::process(const float* in, float* out, std::size_t frames, float transposeOv,
                         float formantOv) {
    if (frames == 0) return;
    if (frames > kMaxBlock) {
        std::memcpy(out, in, frames * 2 * sizeof(float));
        return;
    }

    const float transpose = finiteOr(transposeOv, params_.transpose);
    const float formant = finiteOr(formantOv, params_.formant);

    // Count only Stretch invocations that will actually run. A chain can place
    // pitch and formant independently (and can repeat either), so each active
    // occurrence contributes its own STFT latency.
    int stretchStages = 0;
    if (voiceLabChain_) {
        for (std::size_t i = 0; i < chainSize_; ++i) {
            const ChainItem& item = chain_[i];
            if (item.kind != ChainItem::Kind::VoiceLab) continue;
            const VoiceLabParams& moduleParams =
                labParams_[std::size_t(item.module)];
            if (moduleParams.mix <= 0.0f) continue;
            if (item.module == LabModule::Pitch &&
                voiceLabPitchActive(moduleParams, transposeOv)) {
                ++stretchStages;
            } else if (item.module == LabModule::Formant &&
                       voiceLabFormantSemitones(moduleParams, formantOv) != 0.0f) {
                ++stretchStages;
            }
        }
    } else if (transpose != 0.0f || formant != 0.0f || params_.lfoDepth != 0.0f) {
        stretchStages = 1;
    }

    // A neutral preset (no pitch/formant/LFO, empty effect chain, full strength)
    // takes a zero-latency fast path with a soft clipper instead of the
    // lookahead limiter. Live overrides can move it on/off this path.
    const bool fast = stretchStages == 0 && !hasActiveChain_ && params_.strength >= 0.999f;
    if (stretchStages != stretchStages_ || fast != fastPath_) {
        stretchStages_ = stretchStages;
        fastPath_ = fast;
        updateLatency();
    }
    if (fast) {
        for (std::size_t i = 0; i < frames * 2; ++i) out[i] = softClip(in[i]);
        return;
    }

    // 0. De-interleave (input is already enhanced at engine level).
    for (std::size_t i = 0; i < frames; ++i) {
        inL_[i] = in[i * 2];
        inR_[i] = in[i * 2 + 1];
    }

    float transposeNow = transpose;
    if (!voiceLabChain_ && params_.lfoDepth != 0.0f) {
        lfoPhase_ += float(2.0 * kPi * double(params_.lfoRate) * double(frames) / sr_);
        lfoPhase_ = float(std::fmod(double(lfoPhase_), double(kTwoPi)));
        if (lfoPhase_ < 0.0f) lfoPhase_ += kTwoPi;
        transposeNow += params_.lfoDepth * float(std::sin(double(lfoPhase_)));
    }

    const float* srcL = inL_.data();
    const float* srcR = inR_.data();
    if (!voiceLabChain_ && stretchStages > 0) {
        processStretch(inL_.data(), inR_.data(), cL_.data(), cR_.data(), frames,
                       transposeNow, formant);
        srcL = cL_.data();
        srcR = cR_.data();
    }

    // Ordered module chain. Every stage reads cur and writes dst; the pointers
    // then swap. VoiceLab pitch/formant are deliberately not run before this
    // loop when hasVoiceLabChain() selected explicit ordering.
    {
        float* curL = const_cast<float*>(srcL);
        float* curR = const_cast<float*>(srcR);
        float* dstL = xL_.data();
        float* dstR = xR_.data();
        for (std::size_t i = 0; i < chainSize_; ++i) {
            const ChainItem& item = chain_[i];
            if (item.kind == ChainItem::Kind::Space) {
                applySpace(item.space, curL, curR, dstL, dstR, frames);
            } else if (item.kind == ChainItem::Kind::Effect) {
                std::memcpy(dstL, curL, frames * sizeof(float));
                std::memcpy(dstR, curR, frames * sizeof(float));
                applyEffect(item.effect, dstL, dstR, frames);
            } else {
                if (labParams_[std::size_t(item.module)].mix <= 0.0f) continue;
                applyVoiceLab(item.module, curL, curR, dstL, dstR, frames,
                              transposeOv, formantOv);
            }
            std::swap(curL, dstL);
            std::swap(curR, dstR);
        }
        std::memcpy(spL_.data(), curL, frames * sizeof(float));
        std::memcpy(spR_.data(), curR, frames * sizeof(float));
    }

    // NaN/inf scrubber: one bad sample must not poison the limiter or mix.
    for (std::size_t i = 0; i < frames; ++i) {
        if (std::isnan(spL_[i]) || std::isinf(spL_[i])) spL_[i] = 0.0f;
        if (std::isnan(spR_[i]) || std::isinf(spR_[i])) spR_[i] = 0.0f;
    }

    {
        float* input[2] = {spL_.data(), spR_.data()};
        float* output[2] = {fL_.data(), fR_.data()};
        limiter_.process(input, output, frames);
    }

    // Dry/wet (strength) + interleave.
    const float strength = params_.strength;
    if (strength >= 0.999f) {
        for (std::size_t i = 0; i < frames; ++i) {
            out[i * 2] = fL_[i];
            out[i * 2 + 1] = fR_[i];
        }
    } else if (strength <= 0.001f) {
        std::memcpy(out, in, frames * 2 * sizeof(float));
    } else {
        const float dry = 1.0f - strength;
        for (std::size_t i = 0; i < frames; ++i) {
            out[i * 2] = in[i * 2] * dry + fL_[i] * strength;
            out[i * 2 + 1] = in[i * 2 + 1] * dry + fR_[i] * strength;
        }
    }
}

// ---------------------------------------------------------------------------
// VoiceProcessor (A/B crossfade)
// ---------------------------------------------------------------------------

VoiceProcessor::VoiceProcessor(double sampleRate)
    : chains_{VoiceChain(sampleRate), VoiceChain(sampleRate)}, sr_(sampleRate) {
    outA_.resize(VoiceChain::kMaxBlock * 2);
    outB_.resize(VoiceChain::kMaxBlock * 2);
}

void VoiceProcessor::applyPreset(int index, const VoicePreset& p) {
    if (pending_ >= 0) { // a fade is already running: commit it, then start fresh
        active_ = pending_;
        curIdx_.store(pendingIdx_, std::memory_order_relaxed);
        pending_ = -1;
        fadePos_ = 0;
    }
    const int next = 1 - active_;
    chains_[next].configure(p);
    chains_[next].resetTail();
    pending_ = next;
    pendingIdx_ = index;
    fadePos_ = 0;
}

void VoiceProcessor::setTransposeOverride(float semitones, bool enable) {
    tOv_ = enable ? semitones : std::nanf("");
}

void VoiceProcessor::setFormantOverride(float semitones, bool enable) {
    fOv_ = enable ? semitones : std::nanf("");
}

void VoiceProcessor::setLatencyMode(int mode) {
    static constexpr int kBlock[3] = {768, 1536, 2048};
    static constexpr int kInterval[3] = {192, 384, 512};
    if (mode < 0 || mode > 2) mode = 1;
    for (auto& chain : chains_) {
        chain.setStretchConfig(kBlock[mode], kInterval[mode]);
    }
}

void VoiceProcessor::resetTail() {
    chains_[0].resetTail();
    chains_[1].resetTail();
}

void VoiceProcessor::process(const float* in, float* out, std::size_t frames) {
    if (frames == 0) return;
    if (frames > kMaxBlock) {
        std::memcpy(out, in, frames * 2 * sizeof(float));
        return;
    }
    if (pending_ < 0) {
        chains_[active_].process(in, out, frames, tOv_, fOv_);
        latencyMs_.store(float(chains_[active_].latencyMs()), std::memory_order_relaxed);
        return;
    }

    chains_[active_].process(in, outA_.data(), frames, tOv_, fOv_);
    chains_[pending_].process(in, outB_.data(), frames, tOv_, fOv_);

    const double step = 1.0 / double(kFadeFrames);
    double gain = double(fadePos_) * step;
    const float* a = outA_.data();
    const float* b = outB_.data();
    const std::size_t samples = frames * 2;
    for (std::size_t i = 0; i < samples; ++i, gain += step) {
        const double g = (gain < 1.0) ? gain : 1.0;
        out[i] = float(a[i] * (1.0 - g) + b[i] * g);
    }
    fadePos_ += frames;
    if (fadePos_ >= kFadeFrames) {
        chains_[active_].resetTail();
        active_ = pending_;
        curIdx_.store(pendingIdx_, std::memory_order_relaxed);
        pending_ = -1;
        fadePos_ = 0;
        latencyMs_.store(float(chains_[active_].latencyMs()), std::memory_order_relaxed);
    }
}

} // namespace vd
