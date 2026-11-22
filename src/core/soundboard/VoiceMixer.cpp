#include "core/soundboard/VoiceMixer.hpp"

#include "core/soundboard/SoundBank.hpp"

#include <algorithm>
#include <cstdint>

namespace vd {

void VoiceMixer::setPeakingCoefs(float* c, double freqHz, double q, double gainDb) {
    // RBJ audio-EQ-cookbook peaking filter.
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979323846 * freqHz / 48000.0;
    const double cw = std::cos(w);
    const double sw = std::sin(w);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    c[0] = float((1.0 + alpha * A) / a0);
    c[1] = float((-2.0 * cw) / a0);
    c[2] = float((1.0 - alpha * A) / a0);
    c[3] = float((-2.0 * cw) / a0);
    c[4] = float((1.0 - alpha / A) / a0);
}

void VoiceMixer::setVoiceEq(Voice& v, const Sound& s) {
    static constexpr double kFreq[5] = {100.0, 500.0, 2000.0, 6000.0, 12000.0};
    for (int b = 0; b < 5; ++b) {
        setPeakingCoefs(v.eqCoef_[b], kFreq[b], 0.9,
                        double(s.eq[b].load(std::memory_order_relaxed)));
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 3; ++k) {
                v.eqX_[b][ch][k] = 0.0f;
                v.eqY_[b][ch][k] = 0.0f;
            }
    }
}

void VoiceMixer::applyCommand(const Command& c, const SoundBank& bank) {
    switch (c.type) {
        case Command::Type::PlaySound: {
            Sound* s = bank.find(c.soundId);
            if (!s || s->removing.load(std::memory_order_relaxed)) return;
            for (Voice& v : voices_) {
                if (v.active.load(std::memory_order_relaxed)) continue;
                v.sound.store(s, std::memory_order_relaxed);
                v.gain = s->gain.load(std::memory_order_relaxed);
                v.loop = s->loop.load(std::memory_order_relaxed);
                v.paused.store(false, std::memory_order_relaxed);
                v.posFrames.store(0, std::memory_order_relaxed);
                setVoiceEq(v, *s);
                v.soundId.store(s->id, std::memory_order_relaxed);
                v.voiceId.store(nextVoiceId_.fetch_add(1, std::memory_order_relaxed),
                                std::memory_order_relaxed);
                v.active.store(true, std::memory_order_relaxed);
                return;
            }
            rejected_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        case Command::Type::PauseSound:
            for (Voice& v : voices_) {
                if (v.active.load(std::memory_order_relaxed) &&
                    v.soundId.load(std::memory_order_relaxed) == c.soundId) {
                    const bool was = v.paused.load(std::memory_order_relaxed);
                    v.paused.store(!was, std::memory_order_relaxed);
                }
            }
            return;

        case Command::Type::SeekSound:
            for (Voice& v : voices_) {
                if (!v.active.load(std::memory_order_relaxed)) continue;
                if (v.soundId.load(std::memory_order_relaxed) != c.soundId) continue;
                const Sound* s = v.sound.load(std::memory_order_relaxed);
                if (!s || s->frames == 0) continue;
                float frac = c.value;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                v.posFrames.store(std::uint64_t(double(s->frames) * double(frac)),
                                   std::memory_order_relaxed);
            }
            return;

        case Command::Type::SetSoundEq: {
            Sound* s = bank.find(c.soundId);
            if (!s) return;
            for (int i = 0; i < 5; ++i) {
                float gain = 0.0f;
                if (i == 0) gain = c.value;
                else if (i == 1) gain = c.valueB;
                else if (i == 2) gain = c.valueC;
                else if (i == 3) gain = c.valueD;
                s->eq[i].store(gain, std::memory_order_relaxed);
            }
            for (Voice& v : voices_) {
                if (v.active.load(std::memory_order_relaxed) &&
                    v.soundId.load(std::memory_order_relaxed) == c.soundId) {
                    const Sound* cur = v.sound.load(std::memory_order_relaxed);
                    if (cur) setVoiceEq(v, *cur);
                }
            }
            return;
        }

        case Command::Type::StopSound:
            for (Voice& v : voices_) {
                if (v.active.load(std::memory_order_relaxed) &&
                    v.soundId.load(std::memory_order_relaxed) == c.soundId) {
                    v.active.store(false, std::memory_order_relaxed);
                }
            }
            return;

        case Command::Type::StopAll:
            for (Voice& v : voices_) v.active.store(false, std::memory_order_relaxed);
            return;

        case Command::Type::SetSoundVolume: {
            Sound* s = bank.find(c.soundId);
            if (!s) return;
            s->gain.store(c.value, std::memory_order_relaxed);
            for (Voice& v : voices_) {
                if (v.active.load(std::memory_order_relaxed) &&
                    v.soundId.load(std::memory_order_relaxed) == c.soundId) {
                    v.gain = c.value;
                }
            }
            return;
        }

        case Command::Type::SetSoundLoop: {
            Sound* s = bank.find(c.soundId);
            if (s) s->loop.store(c.value != 0.0f, std::memory_order_relaxed);
            return;
        }

        default:
            return;
    }
}

void VoiceMixer::mix(float* out, std::size_t frames, std::uint32_t channels, float duckGain) {
    for (Voice& v : voices_) {
        if (!v.active.load(std::memory_order_relaxed)) continue;
        if (v.paused.load(std::memory_order_relaxed)) continue;
        const Sound* s = v.sound.load(std::memory_order_relaxed);
        if (!s || s->frames == 0) {
            v.active.store(false, std::memory_order_relaxed);
            continue;
        }
        const float g = v.gain * duckGain;
        const bool eqOn = v.eqCoef_[0][0] != 1.0f || v.eqCoef_[0][3] != 0.0f ||
                          v.eqCoef_[1][0] != 1.0f || v.eqCoef_[1][3] != 0.0f ||
                          v.eqCoef_[2][0] != 1.0f || v.eqCoef_[2][3] != 0.0f ||
                          v.eqCoef_[3][0] != 1.0f || v.eqCoef_[3][3] != 0.0f ||
                          v.eqCoef_[4][0] != 1.0f || v.eqCoef_[4][3] != 0.0f;
        std::size_t done = 0;
        float* cursor = out;
        while (done < frames) {
            std::uint64_t pos = v.posFrames.load(std::memory_order_relaxed);
            std::uint64_t avail = s->frames - pos;
            if (avail == 0) {
                if (v.loop) {
                    v.posFrames.store(0, std::memory_order_relaxed);
                    continue;
                }
                v.active.store(false, std::memory_order_relaxed);
                break;
            }
            const std::size_t n =
                static_cast<std::size_t>(std::min<std::uint64_t>(avail, frames - done));
            const float* src = s->pcm.data() + static_cast<std::size_t>(pos) * channels;
            for (std::size_t i = 0; i < n; ++i) {
                for (std::uint32_t ch = 0; ch < channels && ch < 2; ++ch) {
                    float x = src[i * channels + ch] * g;
                    if (eqOn) {
                        // 5-band peaking EQ per channel.
                        for (int b = 0; b < 5; ++b) {
                            const float* c = v.eqCoef_[b];
                            float* xh = v.eqX_[b][ch];
                            float* yh = v.eqY_[b][ch];
                            const float y = c[0] * x + c[1] * xh[0] + c[2] * xh[1] -
                                            c[3] * yh[0] - c[4] * yh[1];
                            xh[1] = xh[0];
                            xh[0] = x;
                            yh[1] = yh[0];
                            yh[0] = y;
                            x = y;
                        }
                    }
                    cursor[i * channels + ch] += x;
                }
            }
            v.posFrames.store(pos + n, std::memory_order_relaxed);
            cursor += n * channels;
            done += n;
        }
    }
}

std::vector<VoiceInfo> VoiceMixer::snapshot() const {
    std::vector<VoiceInfo> r;
    for (const Voice& v : voices_) {
        if (!v.active.load(std::memory_order_relaxed)) continue;
        VoiceInfo info;
        info.voiceId = v.voiceId.load(std::memory_order_relaxed);
        info.soundId = v.soundId.load(std::memory_order_relaxed);
        info.posFrames = v.posFrames.load(std::memory_order_relaxed);
        info.paused = v.paused.load(std::memory_order_relaxed);
        if (const Sound* s = v.sound.load(std::memory_order_relaxed)) info.totalFrames = s->frames;
        r.push_back(info);
    }
    return r;
}

} // namespace vd
