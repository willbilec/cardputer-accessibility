#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace voiceClipCodec {
// Logarithmic 8-bit PCM preserves quiet speech while doubling the RAM clip length.
inline uint8_t encode(int16_t sample) {
    const unsigned magnitude = std::min(std::abs(int(sample)), 32635) + 132;
    unsigned segment = 0;
    for (unsigned threshold = 256; magnitude >= threshold; threshold <<= 1) ++segment;
    return (sample < 0 ? 0x80 : 0) | (segment << 4) | ((magnitude >> (segment + 3)) & 15);
}

inline int16_t decode(uint8_t code) {
    const int magnitude = (((code & 15) * 8 + 132) << ((code >> 4) & 7)) - 132;
    return code & 0x80 ? -magnitude : magnitude;
}
}

class VoiceEffect {
public:
    enum Preset { Robot, Flanger, Deep, High, Echo, Tremolo, Radio, Fuzz, Chorus, Whisper };
    static constexpr const char *names[] = {
        "Robot", "Flanger", "Deep voice", "High voice", "Echo", "Tremolo",
        "Radio", "Fuzz", "Chorus", "Whisper"
    };
    static constexpr unsigned blockFrames = 256;
    static constexpr unsigned rate = 44100;

    void render(Preset preset, int16_t *stereo, float gain) {
        for (unsigned i = 0; i < blockFrames; ++i) {
            const float input = (int(stereo[2*i]) + stereo[2*i+1]) / 2.0f;
            // Remove capture DC before driving modulated delays or distortion.
            dc += 0.002f * (input - dc);
            const float dry = input - dc;
            delay[head] = pcm(dry);
            float angle = 0;
            float speed = 0;
            float wet = dry;
            switch (preset) {
            case Robot:
                wet = dry * (0.12f + 0.88f * sine(phase));
                speed = 76;
                break;
            case Flanger:
                angle = 0.5f + 0.5f * sine(phase);
                wet = 0.58f * dry + 0.42f * tap(18 + 240 * angle);
                speed = 0.45f;
                break;
            case Deep:
            case High: {
                constexpr float window = 1700;
                const float second = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
                const float weight = 1.0f - std::abs(2 * phase - 1);
                wet = tap(32 + phase * window) * weight +
                      tap(32 + second * window) * (1 - weight);
                speed = (preset == Deep ? 0.28f : -0.51f) * rate / window;
                break;
            }
            case Echo: {
                const float echo = tap(7497);
                delay[head] = pcm(dry + 0.32f * echo);
                wet = 0.75f * dry + 0.36f * echo;
                break;
            }
            case Tremolo:
                wet = dry * (0.6f + 0.4f * sine(phase));
                speed = 7;
                break;
            case Radio:
                low += 0.05f * (dry - low);
                band += 0.32f * (dry - low - band);
                wet = std::clamp(2.8f * band, -8500.0f, 8500.0f);
                break;
            case Fuzz:
                wet = 0.22f * dry + std::clamp(4.0f * dry, -11000.0f, 11000.0f);
                break;
            case Chorus:
                wet = 0.58f * dry + 0.23f * tap(850 + 155 * sine(phase)) +
                      0.19f * tap(1230 - 190 * sine(phase));
                speed = 0.85f;
                break;
            case Whisper: {
                envelope += (std::abs(dry) > envelope ? 0.04f : 0.003f) *
                            (std::abs(dry) - envelope);
                noise ^= noise << 13;
                noise ^= noise >> 17;
                noise ^= noise << 5;
                const float hiss = float(noise & 0xffff) / 32768.0f - 1;
                low += 0.19f * (dry - low);
                wet = 0.36f * (dry - low) + hiss * envelope * 0.9f;
                break;
            }
            }
            phase += speed / rate;
            if (phase >= 1) phase -= 1;
            if (phase < 0) phase += 1;
            head = (head + 1) & mask;
            stereo[2*i] = stereo[2*i+1] = pcm(wet * gain);
        }
    }

private:
    static constexpr unsigned mask = 8191;
    int16_t delay[mask + 1]{};
    unsigned head = 0;
    uint32_t noise = 0x6abaf551;
    float phase = 0, dc = 0, low = 0, band = 0, envelope = 0;

    static int16_t pcm(float value) {
        return static_cast<int16_t>(std::clamp(value, -32768.0f, 32767.0f));
    }
    static float sine(float cycle) { return std::sin(cycle * 6.283185307f); }
    float tap(float frames) const {
        const unsigned whole = static_cast<unsigned>(frames);
        const float fraction = frames - whole;
        const float first = delay[(head - whole) & mask];
        return first + (delay[(head - whole - 1) & mask] - first) * fraction;
    }
};
