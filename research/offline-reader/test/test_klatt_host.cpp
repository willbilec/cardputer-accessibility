#include <tts/tts.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include "../include/klatt_render.h"

static void u16(std::ofstream &out, uint16_t n) { out.put(n & 255); out.put(n >> 8); }
static void u32(std::ofstream &out, uint32_t n) { u16(out, n & 65535); u16(out, n >> 16); }
int main(int argc, char **argv) {
    assert(argc == 2);
    alignas(float) uint8_t arena[32768];
    auto voice = tts::DefaultVoiceParams();
    tts::StreamSynth synth(voice, arena, sizeof(arena));
    tts::StreamOptions options; options.speed = 1.2f;
    std::ofstream wav(argv[1], std::ios::binary);
    assert(wav.good());
    wav.write("RIFF", 4); u32(wav, 0); wav.write("WAVEfmt ", 8); u32(wav, 16);
    u16(wav, 1); u16(wav, 1); u32(wav, 22050); u32(wav, 44100); u16(wav, 2); u16(wav, 16);
    wav.write("data", 4); u32(wav, 0);
    uint32_t samples = 0; double energy = 0;
    for (const char *text : {"Music.", "Down selects next song.", "Enter plays or pauses.", "Wi-Fi connected."}) {
        int result = renderKlattText(synth, text, options, [&](const float *pcm, int n) {
            for (int i = 0; i < n; ++i) {
                assert(std::isfinite(pcm[i]));
                auto v = int16_t(std::clamp(pcm[i], -1.0f, 1.0f) * 32767);
                u16(wav, uint16_t(v)); energy += double(v) * v; ++samples;
            }
            return true;
        }, []() { return true; });
        assert(result == 0);
        assert(synth.done());
    }
    assert(samples > 22050 && energy > 0);
    assert(renderKlattText(synth, "Cancel this request", options,
        [](const float *, int) { return true; }, []() { return false; }) == 1);
    assert(renderKlattText(synth, "Output failure", options,
        [](const float *, int) { return false; }, []() { return true; }) == -4);
    wav.seekp(4); u32(wav, 36 + samples * 2); wav.seekp(40); u32(wav, samples * 2);
    std::cout << "Actual Klatt streaming synthesis passed: " << samples << " samples, "
              << double(samples) / 22050 << " seconds; 32768-byte arena\n";
}
