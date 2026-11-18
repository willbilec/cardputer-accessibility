#pragma once
#include "Arduino.h"
inline bool fakeSynthBegin = true;
inline std::string fakeSynthLast;
struct ESpeak {
    explicit ESpeak(Print &p) : output(p) {}
    void setUsePSRAM(bool) {}
    bool begin(int) { return fakeSynthBegin; }
    void setRate(int) {}
    void say(const char *text) {
        fakeSynthLast = text;
        uint8_t samples[2048]{};
        output.write(samples, sizeof(samples));
    }
    Print &output;
};
