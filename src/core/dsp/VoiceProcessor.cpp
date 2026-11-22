#include "core/dsp/VoiceProcessor.hpp"

#include <algorithm>
#include <cstring>

namespace vd {

// ---------------------------------------------------------------------------
// VoiceChain
// ---------------------------------------------------------------------------

namespace {
constexpr double kPi = 3.14159265358979323846;

// Comb / ring-mod settings for the built-in effects.
constexpr float kRobotCombDelaySec = 0.0018f; // ~1.8 ms comb
constexpr float kRobotCombFeedback = 0.72f;
constexpr float kRobotRingHz = 55.0f;
constexpr float kRobotRingMix = 0.65f;
constexpr float kAlienRingHz = 85.0f;
constexpr float kAlienRingMix = 0.35f;
constexpr float kCloneTwinMix = 0.45f;
constexpr float kAtariOutGain = 1.3f;

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
} // namespace

VoiceChain::VoiceChain(double sampleRate)
    : sr_(sampleRate),
      stretch_{},
      bp_{},
      lpf_{},
      comb_{},
      twin_{},
      crush_{} {
    const std::size_t n = kMaxBlock;
    inL_.resize(n); inR_.resize(n);
    cL_.resize(n);  cR_.resize(n);
    xL_.resize(n);  xR_.resize(n);
    spL_.resize(n); spR_.resize(n);
    fL_.resize(n);  fR_.resize(n);

    // One-shot module configuration (runs before audio starts, so it may allocate).
    // Stretch block size follows the engine's latency mode:
    //   0 = low (768/192, ~20ms), 1 = normal (1536/384, ~40ms), 2 = hq (2048/512, ~53ms)
    {
        // Read the mode from the engine via a global-free approach:
        // the VoiceProcessor passes it via the first preset's chain creation.
        // For now, use normal (the most common case); setLatency re-creates.
        stretch_.configure(2, 1536, 384, /*splitComputation=*/true);
    }
    stretch_.setFormantBase(150.0 / sr_);
    reverb_.configure(sr_, kMaxBlock, 2);
    crunch_.configure(sr_, kMaxBlock, 2);
    limiter_.configure(sr_, kMaxBlock, 2);
    limiter_.outputLimit = 0.708f; // ~-3 dBFS
    limiter_.attackMs = 5.0;
    limiter_.releaseMs = 60.0;
    for (int c = 0; c < 2; ++c) {
        comb_[c].Init();
        twin_[c].Init(float(sr_), /*quantize_semitones=*/true);
        twin_[c].SetDelSize(1440); // ~30 ms internal window
        twin_[c].SetTransposition(12.0f);
        crush_[c].Init();
        bp_[c].setup(sr_, 1000.0, 500.0);
        lpf_[c].setup(sr_, 2800.0);
    }
    updateLatency();
}

void VoiceChain::updateLatency() {
    double s = double(limiter_.latencySamples());
    if (useStretch_) s += double(stretch_.inputLatency() + stretch_.outputLatency());
    latencyMs_ = s * 1000.0 / sr_;
}

void VoiceChain::configure(const VoicePreset& p) {
    p_ = p;
    // Parse the chain: if non-empty, it overrides space/effect.
    // Each element is either a space or an effect, processed in order.
    chain_.clear();
    if (!p.chain.empty()) {
        for (const auto& name : p.chain) {
            const Space s = parseSpace(name);
            const Effect e = parseEffect(name);
            if (s != Space::None) chain_.push_back({ChainItem::Kind::Space, s, e});
            else if (e != Effect::None) chain_.push_back({ChainItem::Kind::Effect, s, e});
        }
    } else {
        // Fallback to single space + effect.
        const Space s = parseSpace(p.space);
        const Effect e = parseEffect(p.effect);
        if (s != Space::None) chain_.push_back({ChainItem::Kind::Space, s, Effect::None});
        if (e != Effect::None) chain_.push_back({ChainItem::Kind::Effect, Space::None, e});
    }
    space_ = parseSpace(p.space);
    effect_ = parseEffect(p.effect);

    // Spaces.
    reverb_.dry = 1.0;
    switch (space_) {
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
            for (int c = 0; c < 2; ++c) lpf_[c].setup(sr_, 2800.0);
            break;
        case Space::Radio:
            reverb_.wet = 0.08; reverb_.roomMs = 8; reverb_.rt20 = 0.2;
            for (int c = 0; c < 2; ++c) bp_[c].setup(sr_, 1200.0, 1500.0);
            crunch_.drive = 6.0; crunch_.fuzz = 0.05; crunch_.toneHz = 2800.0;
            crunch_.outGain = 0.8;
            break;
        case Space::Megaphone:
            reverb_.wet = 0.15; reverb_.roomMs = 12; reverb_.rt20 = 0.25;
            for (int c = 0; c < 2; ++c) bp_[c].setup(sr_, 900.0, 1400.0);
            crunch_.drive = 10.0; crunch_.fuzz = 0.15; crunch_.toneHz = 2400.0;
            crunch_.outGain = 0.55;
            break;
        case Space::None:
        default:
            break;
    }

    // Effects.
    const size_t combDelay = std::max<size_t>(2, size_t(kRobotCombDelaySec * sr_));
    for (int c = 0; c < 2; ++c) {
        comb_[c].Init();
        comb_[c].SetDelay(combDelay);
        if (effect_ == Effect::Atari) {
            crush_[c].SetDownsampleFactor(0.75f);
            crush_[c].SetBitsToCrush(4);
        }
        if (effect_ == Effect::Clone) twin_[c].SetTransposition(12.0f);
    }
    ringPhase_ = 0.0f;
    lfoPhase_ = 0.0f;
    useStretch_ = (p_.transpose != 0 || p_.formant != 0 || p_.lfoDepth != 0);
    updateLatency();
}

void VoiceChain::applySpace(Space space, const float* srcL, const float* srcR,
                             float* dstL, float* dstR, std::size_t frames) {
    switch (space) {
        case Space::Room:
        case Space::Hall:
        case Space::Cave: {
            float* inBufs[2] = {const_cast<float*>(srcL), const_cast<float*>(srcR)};
            float* outBufs[2] = {dstL, dstR};
            reverb_.process(inBufs, outBufs, frames);
            break;
        }
        case Space::Helmet: {
            for (std::size_t i = 0; i < frames; ++i) {
                xL_[i] = float(lpf_[0].filter(double(srcL[i])));
                xR_[i] = float(lpf_[1].filter(double(srcR[i])));
            }
            float* inBufs[2] = {xL_.data(), xR_.data()};
            float* outBufs[2] = {dstL, dstR};
            reverb_.process(inBufs, outBufs, frames);
            break;
        }
        case Space::Radio:
        case Space::Megaphone: {
            for (std::size_t i = 0; i < frames; ++i) {
                xL_[i] = float(bp_[0].filter(double(srcL[i])));
                xR_[i] = float(bp_[1].filter(double(srcR[i])));
            }
            float* inBufs[2] = {xL_.data(), xR_.data()};
            float* outBufs[2] = {dstL, dstR};
            crunch_.process(inBufs, outBufs, frames);
            break;
        }
        case Space::None:
        default:
            if (dstL != srcL) {
                std::memcpy(dstL, srcL, frames * sizeof(float));
                std::memcpy(dstR, srcR, frames * sizeof(float));
            }
            break;
    }
}

void VoiceChain::applyEffect(Effect effect, float* dstL, float* dstR, std::size_t frames) {
    switch (effect) {
        case Effect::Robot: {
            const float ringStep = float(2.0 * 3.14159265358979323846 * 55.0 / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                for (int c = 0; c < 2; ++c) {
                    float* s = (c == 0) ? &dstL[i] : &dstR[i];
                    const float in = *s;
                    const float dl = comb_[c].Read();
                    comb_[c].Write(in + 0.72f * dl);
                    const float carrier = float(std::sin(double(ringPhase_)));
                    *s = (in + 0.45f * dl) * (1.0f - 0.65f) + (in + 0.45f * dl) * carrier * 0.65f;
                }
                ringPhase_ += ringStep;
                if (ringPhase_ > float(2.0 * 3.14159265358979323846))
                    ringPhase_ -= float(2.0 * 3.14159265358979323846);
            }
            break;
        }
        case Effect::Alien: {
            const float ringStep = float(2.0 * 3.14159265358979323846 * 85.0 / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                const float carrier = float(std::sin(double(ringPhase_)));
                dstL[i] = dstL[i] * 0.65f + dstL[i] * carrier * 0.35f;
                dstR[i] = dstR[i] * 0.65f + dstR[i] * carrier * 0.35f;
                ringPhase_ += ringStep;
                if (ringPhase_ > float(2.0 * 3.14159265358979323846))
                    ringPhase_ -= float(2.0 * 3.14159265358979323846);
            }
            break;
        }
        case Effect::Atari: {
            for (std::size_t i = 0; i < frames; ++i) {
                dstL[i] = crush_[0].Process(dstL[i]) * 1.3f;
                dstR[i] = crush_[1].Process(dstR[i]) * 1.3f;
            }
            break;
        }
        case Effect::Clone: {
            for (std::size_t i = 0; i < frames; ++i) {
                dstL[i] = dstL[i] + 0.45f * twin_[0].Process(dstL[i]);
                dstR[i] = dstR[i] + 0.45f * twin_[1].Process(dstR[i]);
            }
            break;
        }
        case Effect::Silence: {
            std::fill(dstL, dstL + frames, 0.0f);
            std::fill(dstR, dstR + frames, 0.0f);
            break;
        }
        case Effect::None:
        default:
            break;
    }
}

void VoiceChain::resetTail() {
    stretch_.reset();
    reverb_.reset();
    crunch_.reset();
    limiter_.reset();
    for (int c = 0; c < 2; ++c) comb_[c].Reset();
    std::fill(xL_.begin(), xL_.end(), 0.0f);
    std::fill(xR_.begin(), xR_.end(), 0.0f);
    std::fill(spL_.begin(), spL_.end(), 0.0f);
    std::fill(spR_.begin(), spR_.end(), 0.0f);
    lfoPhase_ = 0.0f;
    ringPhase_ = 0.0f;
}

void VoiceChain::processEffect(std::size_t frames) {
    switch (effect_) {
        case Effect::Robot: {
            const float ringStep = float(2.0 * kPi * kRobotRingHz / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                for (int c = 0; c < 2; ++c) {
                    float* s = (c == 0) ? &spL_[i] : &spR_[i];
                    const float in = *s;
                    const float dl = comb_[c].Read();
                    comb_[c].Write(in + kRobotCombFeedback * dl);
                    float v = in + 0.45f * dl;
                    const float carrier = float(std::sin(double(ringPhase_)));
                    *s = v * (1.0f - kRobotRingMix) + v * carrier * kRobotRingMix;
                }
                ringPhase_ += ringStep;
                if (ringPhase_ > float(2.0 * kPi)) ringPhase_ -= float(2.0 * kPi);
            }
            break;
        }
        case Effect::Alien: {
            const float ringStep = float(2.0 * kPi * kAlienRingHz / sr_);
            for (std::size_t i = 0; i < frames; ++i) {
                const float carrier = float(std::sin(double(ringPhase_)));
                spL_[i] = spL_[i] * (1.0f - kAlienRingMix) +
                          spL_[i] * carrier * kAlienRingMix;
                spR_[i] = spR_[i] * (1.0f - kAlienRingMix) +
                          spR_[i] * carrier * kAlienRingMix;
                ringPhase_ += ringStep;
                if (ringPhase_ > float(2.0 * kPi)) ringPhase_ -= float(2.0 * kPi);
            }
            break;
        }
        case Effect::Atari: {
            for (std::size_t i = 0; i < frames; ++i) {
                spL_[i] = crush_[0].Process(spL_[i]) * kAtariOutGain;
                spR_[i] = crush_[1].Process(spR_[i]) * kAtariOutGain;
            }
            break;
        }
        case Effect::Clone: {
            for (std::size_t i = 0; i < frames; ++i) {
                spL_[i] = spL_[i] + kCloneTwinMix * twin_[0].Process(spL_[i]);
                spR_[i] = spR_[i] + kCloneTwinMix * twin_[1].Process(spR_[i]);
            }
            break;
        }
        case Effect::Silence: {
            std::fill(spL_.begin(), spL_.begin() + frames, 0.0f);
            std::fill(spR_.begin(), spR_.begin() + frames, 0.0f);
            break;
        }
        case Effect::None:
        default:
            break;
    }
}

void VoiceChain::process(const float* in, float* out, std::size_t frames, float transposeOv,
                         float formantOv) {
    if (frames == 0) return;
    if (frames > kMaxBlock) {
        std::memcpy(out, in, frames * 2 * sizeof(float));
        return;
    }

    // 0. De-interleave (input is already enhanced at engine level).
    for (std::size_t i = 0; i < frames; ++i) {
        inL_[i] = in[i * 2];
        inR_[i] = in[i * 2 + 1];
    }

    // 1. Character: pitch/formant via Signalsmith Stretch.
    const float transpose = std::isnan(transposeOv) ? p_.transpose : transposeOv;
    const float formant = std::isnan(formantOv) ? p_.formant : formantOv;
    const bool wantStretch = (transpose != 0 || formant != 0 || p_.lfoDepth != 0);
    if (wantStretch != useStretch_) {
        useStretch_ = wantStretch;
        updateLatency();
    }
    const float* srcL = inL_.data();
    const float* srcR = inR_.data();
    if (useStretch_) {
        float tNow = transpose;
        if (p_.lfoDepth != 0) {
            lfoPhase_ += float(2.0 * kPi * double(p_.lfoRate) * double(frames) / sr_);
            if (lfoPhase_ > float(2.0 * kPi)) lfoPhase_ -= float(2.0 * kPi);
            tNow += p_.lfoDepth * float(std::sin(double(lfoPhase_)));
        }
        stretch_.setTransposeSemitones(float(tNow));
        stretch_.setFormantFactor(float(std::pow(2.0, double(formant) / 12.0)), false);
        const float* inCh[2] = {inL_.data(), inR_.data()};
        float* outCh[2] = {cL_.data(), cR_.data()};
        stretch_.process(inCh, int(frames), outCh, int(frames));
        srcL = cL_.data();
        srcR = cR_.data();
    }

    // 4. Chain processing (spaces and effects in order).
    //    src points to the character output; each chain element reads from
    //    src and writes to dst, then we swap for the next element.
    {
        float* curL = const_cast<float*>(srcL);
        float* curR = const_cast<float*>(srcR);
        float* dstL = xL_.data();
        float* dstR = xR_.data();
        for (const auto& item : chain_) {
            if (item.kind == ChainItem::Kind::Space) {
                applySpace(item.space, curL, curR, dstL, dstR, frames);
            } else {
                // Effects operate in-place on dst.
                if (curL != dstL) {
                    std::memcpy(dstL, curL, frames * sizeof(float));
                    std::memcpy(dstR, curR, frames * sizeof(float));
                }
                applyEffect(item.effect, dstL, dstR, frames);
            }
            std::swap(curL, dstL);
            std::swap(curR, dstR);
        }
        std::memcpy(spL_.data(), curL, frames * sizeof(float));
        std::memcpy(spR_.data(), curR, frames * sizeof(float));
    }

    // 5. Copy the chain output to spL_/spR_ for the limiter.
    {
        float* dstL = spL_.data();
        float* dstR = spR_.data();
        for (std::size_t i = 0; i < frames; ++i) {
            if (std::isnan(spL_[i]) || std::isinf(spL_[i])) spL_[i] = 0.0f;
            if (std::isnan(spR_[i]) || std::isinf(spR_[i])) spR_[i] = 0.0f;
        }
    }

    // 6. Limiter.
    {
        float* inBufs[2] = {spL_.data(), spR_.data()};
        float* outBufs[2] = {fL_.data(), fR_.data()};
        limiter_.process(inBufs, outBufs, frames);
    }

    // 7. Dry/wet (strength) + interleave.
    const float s = p_.strength;
    if (s >= 0.999f) {
        for (std::size_t i = 0; i < frames; ++i) {
            out[i * 2] = fL_[i];
            out[i * 2 + 1] = fR_[i];
        }
    } else if (s <= 0.001f) {
        std::memcpy(out, in, frames * 2 * sizeof(float));
    } else {
        const float d = 1.0f - s;
        for (std::size_t i = 0; i < frames; ++i) {
            out[i * 2] = in[i * 2] * d + fL_[i] * s;
            out[i * 2 + 1] = in[i * 2 + 1] * d + fR_[i] * s;
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

void VoiceProcessor::process(const float* in, float* out, std::size_t frames) {
    if (pending_ < 0) {
        chains_[active_].process(in, out, frames, tOv_, fOv_);
        latencyMs_.store(float(chains_[active_].latencyMs()), std::memory_order_relaxed);
        return;
    }

    chains_[active_].process(in, outA_.data(), frames, tOv_, fOv_);
    chains_[pending_].process(in, outB_.data(), frames, tOv_, fOv_);

    const double step = 1.0 / double(kFadeFrames);
    double g = double(fadePos_) * step;
    const float* a = outA_.data();
    const float* b = outB_.data();
    const std::size_t n = frames * 2;
    for (std::size_t i = 0; i < n; ++i, g += step) {
        const double gg = (g < 1.0) ? g : 1.0;
        out[i] = float(a[i] * (1.0 - gg) + b[i] * gg);
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
