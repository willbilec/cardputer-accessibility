#pragma once

#include <algorithm>
#include <cstdint>

class MusicDynamics {
public:
    void reset(unsigned rate, bool enabled) {
        peak = 0;
        boost = enabled ? maxBoost : 0.0f;
        release = 1.0f - 10.0f / rate; // Roughly 100 ms.
    }

    int32_t process(int32_t sample, bool enabled, bool limitPeaks) {
        const int32_t magnitude = sample < 0 ? -sample : sample;
        peak = std::max(float(magnitude), peak * release);
        const float target = enabled ? maxBoost : 0.0f;
        boost += std::clamp(target - boost, -0.001f, 0.001f);

        // Lift quieter passages and back off immediately on loud transients.
        constexpr float quietPeak = 4096.0f;
        constexpr float loudPeak = 24576.0f;
        const float lift = std::clamp((loudPeak - peak) / (loudPeak - quietPeak), 0.0f, 1.0f);
        int32_t output = int32_t(magnitude * (1.0f + boost * lift));
        constexpr int32_t knee = 30000;
        constexpr int32_t headroom = 32767 - knee;
        if (output > knee && (limitPeaks || boost > 0.0f)) {
            const int32_t excess = output - knee;
            output = knee + headroom * excess / (headroom + excess);
        }
        return sample < 0 ? -output : output;
    }

private:
    static constexpr float maxBoost = 0.35f;
    float peak = 0, boost = 0, release = 0;
};
