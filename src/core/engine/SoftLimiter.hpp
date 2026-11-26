#pragma once

#include <cmath>

namespace vd {

// Transparent below the knee, then an exponential C1-continuous knee towards
// 1.0. This avoids the audible coloration of tanh() on ordinary voice levels.
inline float softLimit(float sample) {
    if (!std::isfinite(sample)) return 0.0f;
    constexpr float knee = 0.85f;
    constexpr float range = 1.0f - knee;
    const float magnitude = std::fabs(sample);
    if (magnitude <= knee) return sample;
    const float excess = magnitude - knee;
    const float limited = knee + range * (1.0f - std::exp(-excess / range));
    return std::copysign(limited, sample);
}

} // namespace vd
