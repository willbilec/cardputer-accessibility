#include "synth.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr float phaseScale = 4294967296.0f;
constexpr double pi = 3.14159265358979323846;
constexpr auto makeSineTable() {
    std::array<float, 256> table{};
    for (unsigned i = 0; i < table.size(); ++i) {
        const double angle = (i <= 128 ? int(i) : int(i) - 256) * pi / 128;
        double term = angle;
        double sine = angle;
        for (unsigned n = 1; n <= 11; ++n) {
            term *= -angle * angle / ((2 * n) * (2 * n + 1));
            sine += term;
        }
        table[i] = float(sine);
    }
    return table;
}
constexpr auto sineTable = makeSineTable();

float decayOver(float seconds) {
    return std::exp(-1.0f / (seconds * Synth::sampleRate));
}
}

void Synth::reset() {
    for (auto &voice : voices) voice = {};
    std::fill_n(history, 512, 0);
    cursor = modulation = 0;
    flangeMix = tremoloMix = peak = arpeggioMix = 0;
}

float Synth::wave(uint32_t phase) {
    unsigned index = phase >> 24;
    float fraction = ((phase >> 16) & 255) / 256.0f;
    return sineTable[index] + fraction * (sineTable[(index + 1) & 255] - sineTable[index]);
}

void Synth::noteOn(unsigned key, unsigned note) {
    auto *voice = std::min_element(voices, voices + 8, [](const Voice &a, const Voice &b) {
        return a.level < b.level || (a.level == b.level && a.age > b.age);
    });
    for (auto &existing : voices) {
        if (existing.key == key && existing.level > 0) {
            voice = &existing;
            break;
        }
    }
    *voice = {};
    voice->key = key;
    voice->held = true;
    voice->level = 1;
    voice->release = decayOver(0.035f);
    voice->lowNote = std::clamp((60 - int(note)) / 22.0f, 0.0f, 1.0f);
    float frequency = 440 * std::exp2((int(note) - 69) / 12.0f);
    float ratio = 2;
    switch (preset) {
    case Preset::Piano:
        voice->decay = decayOver(0.55f);
        ratio = 2.006f;
        break;
    case Preset::Chiptune:
        break;
    case Preset::Drums: {
        constexpr float frequencies[] = {52, 165, 310, 410, 95, 630, 235, 710, 145, 890, 460, 1820};
        constexpr float lengths[] = {0.115f, 0.08f, 0.019f, 0.15f, 0.16f, 0.085f, 0.095f, 0.12f,
                                     0.10f, 0.055f, 0.035f, 0.24f};
        voice->drum = (note - 38u) % 12;
        frequency = frequencies[voice->drum];
        voice->decay = decayOver(lengths[voice->drum]);
        ratio = 1.47f;
        break;
    }
    case Preset::Bass:
        frequency *= 0.5f;
        voice->decay = decayOver(1.25f);
        break;
    case Preset::Bells:
        ratio = 2.71f;
        voice->decay = decayOver(0.9f);
        voice->release = decayOver(0.24f);
        break;
    case Preset::GrandPiano:
        ratio = 1.003f;
        voice->decay = decayOver(0.9f);
        voice->release = decayOver(0.065f);
        break;
    case Preset::Nes:
        // The triangle channel takes the low keys. Upper keys use a 25% pulse.
        voice->decay = decayOver(0.85f);
        break;
    case Preset::AnalogBrass:
        ratio = 1.008f;
        voice->release = decayOver(0.10f);
        break;
    case Preset::ElectricPiano:
        ratio = 3.5f;
        voice->decay = decayOver(0.75f);
        voice->release = decayOver(0.13f);
        break;
    case Preset::DrawbarOrgan:
        break;
    }
    voice->step = uint32_t(frequency * phaseScale / sampleRate);
    voice->overtoneStep = uint32_t(frequency * ratio * phaseScale / sampleRate);
}

void Synth::noteOff(unsigned key) {
    for (auto &voice : voices) if (voice.key == key) voice.held = false;
}

bool Synth::active() const {
    for (const auto &voice : voices) if (voice.level > 0) return true;
    return false;
}

float Synth::sample(Voice &v) {
    const float fundamental = wave(v.phase);
    const float overtone = wave(v.overtone);
    switch (preset) {
    case Preset::Piano:
        v.attack *= 0.99975f;
        return (0.68f - 0.19f * v.lowNote) * fundamental +
               (0.23f * v.attack + 0.29f * v.lowNote) * overtone +
               (0.09f * v.attack + 0.16f * v.lowNote) * wave(v.phase * 3);
    case Preset::Chiptune:
        return v.phase < 0x60000000u ? 0.8f : -0.48f;
    case Preset::Drums: {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        const float white = float(noise >> 16) / 32768 - 1;
        const float bright = (white - v.previousNoise) * 0.5f;
        v.previousNoise = white;
        v.pitch *= 0.99935f;
        v.phase += uint32_t(v.step * v.pitch * 2.8f);
        switch (v.drum) {
        case 0: return fundamental;
        case 1: return 0.74f * white + 0.26f * fundamental;
        case 2: return 1.45f * bright;
        case 3: return bright + 0.35f * fundamental * overtone;
        case 4: return 0.8f * fundamental + 0.2f * overtone;
        case 5: return (v.age < 2400 && (v.age / 580) % 2 ? 0.18f : 1.0f) * white;
        case 6: return 0.62f * overtone + 0.38f * fundamental;
        case 7: return 0.78f * bright + 0.7f * fundamental * overtone;
        case 8: return 0.74f * fundamental + 0.26f * wave(v.phase * 3);
        case 9: return fundamental * (0.53f + 0.46f * wave(v.overtone * 3));
        case 10: return 0.73f * fundamental + 0.27f * white;
        default: return 0.55f * bright + 0.52f * fundamental * wave(v.overtone * 2);
        }
    }
    case Preset::Bass:
        v.attack *= 0.99993f;
        return 0.72f * fundamental + v.attack * (0.19f * overtone + 0.09f * wave(v.phase * 5));
    case Preset::Bells:
        v.attack *= 0.99994f;
        return 0.50f * fundamental + v.attack * (0.35f * overtone + 0.15f * wave(v.overtone * 2));
    case Preset::GrandPiano: {
        v.attack *= 0.99972f;
        const float strings = 0.35f * fundamental + 0.28f * overtone +
            (0.16f + 0.16f * v.lowNote) * wave(v.overtone * 2);
        const float hammer = 0.16f * wave(v.phase * 3) +
            0.10f * wave(v.overtone * 4) + 0.07f * wave(v.phase * 7);
        return strings + v.attack * hammer;
    }
    case Preset::Nes: {
        if (v.lowNote > 0.5f) {
            const int step = v.phase >> 27;
            return (step < 16 ? step : 31 - step) / 7.5f - 1;
        }
        const unsigned envelope = 15 - std::min<uint32_t>(v.age / 2800, 10);
        return (v.phase < 0x40000000u ? 0.9f : -0.3f) * (envelope / 15.0f);
    }
    case Preset::AnalogBrass: {
        // Two detuned saws with a filter-like attack on the upper harmonics.
        const float bloom = std::min(v.age / 760.0f, 1.0f) *
                            (0.52f + 0.48f * v.attack);
        v.attack *= 0.9999f;
        auto sawOvertones = [](uint32_t phase) {
            return -0.50f * wave(phase * 2) + 0.33f * wave(phase * 3) -
                    0.25f * wave(phase * 4) + 0.20f * wave(phase * 5);
        };
        return 0.38f * (fundamental + overtone + bloom *
                       (sawOvertones(v.phase) + sawOvertones(v.overtone)));
    }
    case Preset::ElectricPiano: {
        v.attack *= 0.99982f;
        const int32_t modulation = int32_t(v.attack * overtone * 530000000.0f);
        return 0.76f * wave(v.phase + uint32_t(modulation)) +
               0.19f * v.attack * wave(v.overtone * 2);
    }
    case Preset::DrawbarOrgan:
        return 0.59f * fundamental + 0.22f * overtone +
               0.13f * wave(v.phase * 3) + 0.08f * wave(v.phase * 4);
    }
    return 0;
}

void Synth::render(int16_t (&pcm)[blockFrames * 2], float gain,
                   bool sustain, bool flange, bool tremolo, unsigned boost, bool arpeggio) {
    constexpr float quietThreshold = 0.7f;
    const float maxLift = 1 + 0.35f * boost;
    for (unsigned i = 0; i < blockFrames; ++i) {
        float dry = 0;
        arpeggioMix += ((arpeggio ? 0.85f : 0.0f) - arpeggioMix) * 0.005f;
        for (auto &voice : voices) {
            if (voice.level <= 0) continue;
            const float fadeIn = std::min(voice.age / 90.0f, 1.0f);
            float tone = sample(voice);
            if (arpeggioMix > 0.001f) {
                // Root, octave, fifth above the octave, then octave again.
                // Fade the stepped tone at each boundary to avoid clicks.
                constexpr unsigned ratios[] = {1, 2, 3, 2};
                constexpr unsigned stepFrames = sampleRate / 12;
                const unsigned position = voice.age % stepFrames;
                const float edge = std::min({1.0f, position / 80.0f,
                                             (stepFrames - position) / 120.0f});
                const unsigned ratio = ratios[(voice.age / stepFrames) % 4];
                const float stepped = wave(voice.phase * ratio);
                const float toneLevel = 0.92f - 0.42f * position / stepFrames;
                tone += arpeggioMix * (stepped * edge * toneLevel - tone);
            }
            dry += tone * voice.level * fadeIn;
            voice.phase += voice.step;
            voice.overtone += voice.overtoneStep;
            ++voice.age;
            voice.level *= voice.decay;
            if (!voice.held && !sustain && preset != Preset::Drums) voice.level *= voice.release;
            if (voice.level < 0.0005f) voice.level = 0;
        }
        modulation += uint32_t(phaseScale / sampleRate * 0.36f);
        flangeMix += ((flange ? 0.52f : 0.0f) - flangeMix) * 0.005f;
        tremoloMix += ((tremolo ? 0.72f : 0.0f) - tremoloMix) * 0.005f;
        unsigned delay = 112 + int(92 * wave(modulation));
        float delayed = history[(cursor + 512 - delay) & 511];
        history[cursor] = dry + flangeMix * 0.42f * delayed;
        cursor = (cursor + 1) & 511;
        const float amplitude = 1 - tremoloMix * (0.5f + 0.5f * wave(modulation * 16));
        const float wet = dry + flangeMix * delayed;
        // Follow short peaks so the lift preserves waveform shape and key attacks.
        peak = std::max(std::abs(wet), peak * 0.9994f);
        const float lift = 1 + (maxLift - 1) * std::max(0.0f, 1 - peak / quietThreshold);
        const float mixed = wet * lift * amplitude;
        const float limited = mixed / (1 + 0.36f * std::abs(mixed));
        pcm[2 * i] = pcm[2 * i + 1] = int16_t(limited * gain * 11500);
    }
}

namespace {
struct LoopBank {
    unsigned root;
    unsigned bpm;
    const char *patterns[SynthLoop::count];
};

// Two bars of eighth notes. Hex digits are semitones above the root,
// or percussion voices for Drums. A dot leaves space for the player.
constexpr LoopBank loopBanks[] = {
    {60,  94, {"047.c740259.9752", "037a7530158c8531", "0.4.7.c.2.5.9.5.", "7420.2479532.359", "0.7c.74.5.9e.95."}}, // Piano
    {62, 144, {"070c707a292e9297", "00c.77a355d.99c7", "03690369a369a963", "0.0c7a7.5.5c9c9.", "c7cde9e7a5acfca5"}}, // Chiptune
    {38, 112, {"0212021202120212", "0b1a20b10b19214a", "0281408262816032", "0.1.00120.1.6022", "072a9701276a9871"}}, // Drums
    {52, 104, {"0.007.3.5.559.7.", "0035.7a75579.c97", "0..70.3.2..92.5.", "00c.0a.75.c.579a", "07530357a.753.57"}}, // Bass
    {65,  76, {"0..7..c.9..5..2.", "c...7.9.e...9.5.", "05a..fc..e9..75.", "7.9.c...5.7.a...", "e..c.9..7..5.2.."}}, // Glass bells
    {57,  86, {"037ce.c7537ac.a7", "0.4.7c74.2579752", "07c7e7c757a7c7a7", "c.975.32a.753.20", "0.37.c.75.9c.e.c"}}, // Grand piano
    {55, 152, {"00c7a07755d9c599", "c.e.fc7.e.c.a75.", "037c70a7059e9259", "0c0c7c7c5d5d9d9d", "cba9.a7.579a.c7."}}, // NES chip
    {58, 108, {"0.04.77.5.59.cc.", "7..a7.30a..ca.53", "03.5.7.7a.75.30.", "0..57..9a..c7..5", "c.c97.757.a53.30"}}, // Analog brass
    {60,  92, {"0.47.a7.2.59.c9.", "03.c.7a.15.d.8c.", "7.9a.c..5.79.a..", "0..47.c.2..59.e.", "c.9.74..a.7.53.."}}, // Electric piano
    {55, 118, {"0.4.70c.5.9.95e.", "037.37a.15a.58c.", "0.77.c7.5.99.e9.", "c7.a7.40e9.c9.52", "0..c.77.5..e.99."}}, // Drawbar organ
};

constexpr int tempoOffsets[] = {0, 8, -8, 16, -4};
constexpr unsigned chordRoots[][4] = {
    {0, 0, 5, 7}, {0, 3, 5, 7}, {0, 7, 5, 0}, {0, 2, 5, 7}, {0, 5, 3, 7},
};
}

void SynthLoop::start(Synth::Preset preset, unsigned selected) {
    if (selected >= count) return;
    stop();
    backing.preset = preset;
    variation = selected;
    const auto &bank = loopBanks[unsigned(preset)];
    framesPerStep = (Synth::sampleRate * 30) / (int(bank.bpm) + tempoOffsets[variation]);
}

void SynthLoop::stop() {
    framesPerStep = framesUntilStep = 0;
    step = 0;
    backing.reset();
}

void SynthLoop::mix(int16_t (&pcm)[Synth::blockFrames * 2], float gain) {
    if (!active()) return;
    if (framesUntilStep <= 0) {
        constexpr unsigned melody = 0, bass = 1, harmony = 2;
        const auto &bank = loopBanks[unsigned(backing.preset)];
        const char note = bank.patterns[variation][step];
        backing.noteOff(melody);
        const bool drums = backing.preset == Synth::Preset::Drums;
        if (note != '.') {
            const unsigned interval = note <= '9' ? note - '0' : note - 'a' + 10;
            backing.noteOn(drums ? 3 + interval : melody, bank.root + interval);
        }
        if (drums && step % 2 == 1 && note != '2')
            backing.noteOn(5, bank.root + 2);
        if (step % 4 == 0) {
            backing.noteOff(bass);
            backing.noteOff(harmony);
            if (!drums) {
                const unsigned root = bank.root - 12 + chordRoots[variation][step / 4];
                backing.noteOn(bass, root);
                if (backing.preset != Synth::Preset::Bass && variation != 2)
                    backing.noteOn(harmony, root + 7);
            }
        }
        step = (step + 1) % 16;
        framesUntilStep += framesPerStep;
    }
    framesUntilStep -= Synth::blockFrames;
    int16_t accompaniment[Synth::blockFrames * 2];
    backing.render(accompaniment, gain * 0.32f, false, false, false, 0);
    for (unsigned i = 0; i < Synth::blockFrames * 2; ++i)
        pcm[i] = int16_t(std::clamp(int(pcm[i]) + accompaniment[i], -32768, 32767));
}
