#pragma once
#include <cstddef>
#include <cstdint>

class Synth {
public:
    static constexpr unsigned sampleRate = 44100;
    static constexpr unsigned blockFrames = 256;
    static constexpr int maxBoost = 8;
    enum class Preset { Piano, Chiptune, Drums, Bass, Bells, GrandPiano, Nes,
                        AnalogBrass, ElectricPiano, DrawbarOrgan };
    Preset preset = Preset::Piano;

    void reset();
    void noteOn(unsigned key, unsigned note);
    void noteOff(unsigned key);
    bool active() const;
    void render(int16_t (&pcm)[blockFrames * 2], float gain,
                bool sustain, bool flange, bool tremolo, unsigned boost = 2,
                bool arpeggio = false);

private:
    struct Voice {
        unsigned key = 0, drum = 0;
        uint32_t phase = 0, overtone = 0, step = 0, overtoneStep = 0, age = 0;
        float level = 0, decay = 1, release = 1, attack = 1, pitch = 1, previousNoise = 0;
        float lowNote = 0;
        bool held = false;
    };
    Voice voices[8]{};
    float history[512]{};
    unsigned cursor = 0;
    uint32_t modulation = 0, noise = 0x83a4f192;
    float flangeMix = 0, tremoloMix = 0;
    float peak = 0, arpeggioMix = 0;
    static float wave(uint32_t phase);
    float sample(Voice &voice);
};

class SynthLoop {
public:
    static constexpr unsigned count = 5;
    void start(Synth::Preset preset, unsigned variation);
    void stop();
    bool active() const { return framesPerStep != 0; }
    void mix(int16_t (&pcm)[Synth::blockFrames * 2], float gain);

private:
    Synth backing;
    unsigned variation = 0, step = 0;
    int framesPerStep = 0, framesUntilStep = 0;
};
