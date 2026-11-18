#include "synth.h"
#include <algorithm>
#include <array>
#include <cassert>

int main() {
    Synth synth;
    int16_t pcm[Synth::blockFrames * 2];
    std::array<std::array<int16_t, Synth::blockFrames * 2>, 10> presets{};
    for (unsigned i = 0; i < presets.size(); ++i) {
        synth.reset();
        synth.preset = Synth::Preset(i);
        synth.noteOn(42, 60);
        synth.render(pcm, 1, false, false, false);
        assert(std::any_of(std::begin(pcm), std::end(pcm), [](int16_t x) { return x != 0; }));
        std::copy(std::begin(pcm), std::end(pcm), presets[i].begin());
        for (unsigned j = 0; j < i; ++j) assert(presets[i] != presets[j]);
    }
    std::array<std::array<int16_t, Synth::blockFrames * 2>, 12> drums{};
    synth.preset = Synth::Preset::Drums;
    for (unsigned i = 0; i < drums.size(); ++i) {
        synth.reset();
        synth.noteOn(i, 38 + i);
        synth.render(pcm, 1, false, false, false);
        assert(std::any_of(std::begin(pcm), std::end(pcm), [](int16_t x) { return x != 0; }));
        std::copy(std::begin(pcm), std::end(pcm), drums[i].begin());
        for (unsigned j = 0; j < i; ++j) assert(drums[i] != drums[j]);
    }

    // A released chip note keeps sounding only while the sustain pedal is down.
    synth.reset();
    synth.preset = Synth::Preset::Chiptune;
    synth.noteOn(0, 60);
    synth.noteOff(0);
    for (int i = 0; i < 100; ++i) synth.render(pcm, 1, true, false, false);
    assert(synth.active());
    for (int i = 0; i < 100; ++i) synth.render(pcm, 1, false, false, false);
    assert(!synth.active());

    for (bool useFlanger : {true, false}) {
        synth.reset();
        synth.noteOn(0, 60);
        synth.render(pcm, 1, false, useFlanger, !useFlanger);
        assert(!std::equal(std::begin(pcm), std::end(pcm), presets[1].begin()));
    }
    synth.reset();
    synth.noteOn(0, 60);
    synth.render(pcm, 1, false, false, false, 2, true);
    assert(!std::equal(std::begin(pcm), std::end(pcm), presets[1].begin()));
    auto crossings = [&] {
        int count = 0;
        for (unsigned i = 2; i < Synth::blockFrames * 2; i += 2)
            if (pcm[i - 2] <= 0 && pcm[i] > 0) ++count;
        return count;
    };
    int rootCrossings = 0, octaveCrossings = 0;
    for (int block = 1; block <= 25; ++block) {
        synth.render(pcm, 1, false, false, false, 2, true);
        if (block >= 2 && block <= 12) rootCrossings += crossings();
        if (block >= 15) octaveCrossings += crossings();
    }
    assert(rootCrossings > 0 && octaveCrossings > rootCrossings * 1.7);
    auto quietPeak = [&](unsigned boost) {
        synth.reset();
        synth.preset = Synth::Preset::Piano;
        synth.noteOn(0, 38);
        for (int i = 0; i < 180; ++i) synth.render(pcm, 1, false, false, false, boost);
        return *std::max_element(std::begin(pcm), std::end(pcm));
    };
    assert(quietPeak(4) > quietPeak(0));
    assert(quietPeak(Synth::maxBoost) > quietPeak(4));

    synth.reset();
    synth.preset = Synth::Preset::GrandPiano;
    for (unsigned key = 0; key < 8; ++key) synth.noteOn(key, 38 + key * 7);
    for (unsigned block = 0; block < 320; ++block) {
        synth.render(pcm, 1, true, block % 2 == 0, true, Synth::maxBoost);
        for (auto value : pcm) assert(value > -32000 && value < 32000);
    }
    synth.reset();
    assert(!synth.active());
    synth.render(pcm, 1, false, true, true);
    assert(std::all_of(std::begin(pcm), std::end(pcm), [](int16_t x) { return x == 0; }));
}
