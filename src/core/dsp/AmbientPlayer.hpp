#pragma once

#include "core/dsp/Preset.hpp"
#include "core/soundboard/Sound.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace vd {

// Real-time, allocation-free background bed. The caller passes the existing
// VAD decision: the loop is mixed only while speech is active. Radio also
// emits a short release chirp at the end of a phrase; custom clips loop for as
// long as the VAD is awake.
class AmbientPlayer {
public:
    void configure(const VoicePreset& preset, std::shared_ptr<const Sound> clip);
    // Lightweight live update: kind uses ambientKindCode(), without touching
    // any voice DSP state. Safe to call from the audio callback.
    void configure(int kind, float gain, std::shared_ptr<const Sound> clip);
    void process(float* out, std::size_t frames, bool speechActive, bool enabled);
    void reset();

private:
    enum class Kind { None, Radio, Pilot, Cave, Custom };

    Kind kind_ = Kind::None;
    std::shared_ptr<const Sound> clip_;
    float gain_ = 0.0f;
    float envelope_ = 0.0f;
    bool wasActive_ = false;
    std::uint64_t clipPos_ = 0;

    // Synth state. Phases keep advancing while muted so loop boundaries do not
    // click and every phrase begins at a different point of the bed.
    std::uint32_t noiseState_ = 0x9e3779b9u;
    float noiseLp_ = 0.0f;
    float rumblePhase_ = 0.0f;
    float harmonicPhase_ = 0.0f;
    float windLp_ = 0.0f;

    // One-shot phrase-release chirp.
    std::uint64_t chirpPos_ = 0;
    std::uint64_t chirpFrames_ = 0;
    float chirpPhase_ = 0.0f;
    float chirpGain_ = 0.0f;
};

} // namespace vd
