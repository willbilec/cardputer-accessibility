#pragma once
#include "Arduino.h"
struct FakeSpeaker {
    int calls = 0;
    int lastChannel = -1;
    void (*onPlay)() = nullptr;
    bool playRaw(const int16_t *, size_t, int, bool, int, int channel) {
        ++calls;
        lastChannel = channel;
        if (onPlay) { auto callback = onPlay; onPlay = nullptr; callback(); }
        return true;
    }
    bool isPlaying(int) { return false; }
};
struct FakeCardputer { FakeSpeaker Speaker; };
inline FakeCardputer M5Cardputer;
