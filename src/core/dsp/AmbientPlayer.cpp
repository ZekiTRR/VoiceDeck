#include "core/dsp/AmbientPlayer.hpp"

#include <algorithm>
#include <cmath>

namespace vd {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kTwoPi = float(2.0 * kPi);

float nextNoise(std::uint32_t& state) {
    // xorshift32: deterministic, cheap, and no shared/global RNG state.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(state & 0x7fffffffu) / float(0x7fffffff) * 2.0f - 1.0f;
}
} // namespace

void AmbientPlayer::configure(const VoicePreset& preset,
                              std::shared_ptr<const Sound> clip) {
    configure(ambientKindCode(preset.ambient), preset.ambientGain, std::move(clip));
}

void AmbientPlayer::configure(int kind, float gain, std::shared_ptr<const Sound> clip) {
    Kind next = Kind::None;
    if (kind == 1) next = Kind::Radio;
    else if (kind == 2) next = Kind::Pilot;
    else if (kind == 3) next = Kind::Cave;
    else if (kind == 4 && clip && clip->frames > 0 && !clip->pcm.empty())
        next = Kind::Custom;
    const bool unchanged = next == kind_ && clip.get() == clip_.get();
    clip_ = std::move(clip);
    gain_ = std::max(0.0f, std::min(1.0f, gain));
    if (unchanged) return; // gain-only edits keep loop phase and envelope
    kind_ = next;
    reset();
}

void AmbientPlayer::reset() {
    envelope_ = 0.0f;
    wasActive_ = false;
    clipPos_ = 0;
    noiseState_ = 0x9e3779b9u;
    noiseLp_ = 0.0f;
    rumblePhase_ = 0.0f;
    harmonicPhase_ = 0.0f;
    windLp_ = 0.0f;
    chirpPos_ = 0;
    chirpFrames_ = 0;
    chirpPhase_ = 0.0f;
    chirpGain_ = 0.0f;
}

void AmbientPlayer::process(float* out, std::size_t frames, bool speechActive,
                            bool enabled) {
    if (frames == 0 || kind_ == Kind::None || gain_ <= 0.0001f) return;

    const bool audible = enabled && speechActive;
    if (audible && !wasActive_) {
        chirpPos_ = 0;
        chirpFrames_ = 0;
        chirpGain_ = 0.0f;
    } else if (!audible && wasActive_ && enabled && kind_ == Kind::Radio) {
        chirpFrames_ = std::uint64_t(0.085 * 48000.0);
        chirpPos_ = 0;
        chirpPhase_ = 0.0f;
        chirpGain_ = gain_ * 0.32f;
    }
    wasActive_ = audible;

    const float attack = 1.0f / (0.012f * 48000.0f);
    const float release = 1.0f / (0.055f * 48000.0f);
    const float rumbleStep = kTwoPi * 47.0f / 48000.0f;
    const float harmonicStep = kTwoPi * 93.0f / 48000.0f;

    for (std::size_t i = 0; i < frames; ++i) {
        if (audible) {
            envelope_ += (gain_ - envelope_) * attack;
        } else {
            // Finite release: the channel is truly silent 55 ms after VAD
            // sleep instead of retaining an exponentially decaying bed.
            envelope_ = std::max(0.0f, envelope_ - release);
        }

        const float noise = nextNoise(noiseState_);
        noiseLp_ += (noise - noiseLp_) * 0.055f;
        windLp_ += (noise - windLp_) * 0.008f;
        rumblePhase_ += rumbleStep;
        harmonicPhase_ += harmonicStep;
        if (rumblePhase_ >= kTwoPi) rumblePhase_ -= kTwoPi;
        if (harmonicPhase_ >= kTwoPi) harmonicPhase_ -= kTwoPi;

        float l = 0.0f;
        float r = 0.0f;
        switch (kind_) {
            case Kind::Radio:
                l = r = noiseLp_ * 0.82f + noise * 0.055f;
                break;
            case Kind::Pilot:
                l = r = std::sin(rumblePhase_) * 0.42f +
                      std::sin(harmonicPhase_) * 0.15f + windLp_ * 0.65f;
                break;
            case Kind::Cave:
                l = r = windLp_ * 1.25f + noiseLp_ * 0.16f +
                      std::sin(rumblePhase_ * 0.5f) * 0.08f;
                break;
            case Kind::Custom: {
                if (!clip_ || clip_->frames == 0) break;
                const std::uint64_t frame = clipPos_ % clip_->frames;
                const std::size_t channels = std::max<std::size_t>(1, clip_->channels);
                const float* src = clip_->pcm.data() + std::size_t(frame) * channels;
                l = src[0];
                r = channels > 1 ? src[1] : src[0];
                ++clipPos_;
                break;
            }
            case Kind::None:
            default:
                break;
        }

        float bed = l * envelope_;
        float bedR = r * envelope_;
        if (chirpFrames_ > 0 && chirpPos_ < chirpFrames_) {
            const float t = float(chirpPos_) / float(chirpFrames_);
            const float hz = 1250.0f + 1550.0f * t;
            chirpPhase_ += kTwoPi * hz / 48000.0f;
            if (chirpPhase_ >= kTwoPi) chirpPhase_ -= kTwoPi;
            const float env = std::sin(float(kPi) * t) * chirpGain_;
            bed += std::sin(chirpPhase_) * env;
            bedR += std::sin(chirpPhase_) * env;
            ++chirpPos_;
            if (chirpPos_ >= chirpFrames_) {
                chirpFrames_ = 0;
                chirpGain_ = 0.0f;
            }
        }
        out[i * 2] += bed;
        out[i * 2 + 1] += bedR;
    }
}

} // namespace vd
