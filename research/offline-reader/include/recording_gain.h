#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace recording {
// Preserve the gain adjustment around a louder reference level.
inline float captureGain(int gainDb) {
    return std::pow(10.0f, (12 + gainDb) / 20.0f);
}

inline int16_t applyCaptureGain(int16_t sample, float gain) {
    const float scaled = sample * gain;
    return static_cast<int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
}
}
