#pragma once
#include <tts/tts.h>
#include <algorithm>
#include <cstring>

// Bounded fragments avoid scaling the fixed arena with the entire utterance.
// Returns 0 on completion, 1 on requested cancellation, or a negative error.
template<class Consume, class KeepGoing>
int renderKlattText(tts::StreamSynth &synth, const char *text, const tts::StreamOptions &options,
                    Consume consume, KeepGoing keepGoing) {
    size_t offset = 0, length = std::strlen(text);
    float samples[256];
    while (offset < length && keepGoing()) {
        while (offset < length && text[offset] == ' ') ++offset;
        if (offset == length) break;
        size_t count = std::min(size_t(48), length - offset);
        char fragment[49]; int result;
        do {
            size_t boundary = count;
            if (offset + count < length)
                for (size_t i = count; i > count / 2; --i)
                    if (text[offset + i] == ' ') { boundary = i; break; }
            count = boundary;
            std::memcpy(fragment, text + offset, count); fragment[count] = 0;
            result = synth.BeginText(fragment, options);
            if (result == tts::kStreamErrArenaFull && count > 1) count /= 2;
            else break;
        } while (true);
        if (result != tts::kStreamOk) return result;
        int n;
        while (keepGoing() && (n = synth.Read(samples, 256)) > 0)
            if (!consume(samples, n)) return -4;
        offset += count;
    }
    return keepGoing() ? 0 : 1;
}
