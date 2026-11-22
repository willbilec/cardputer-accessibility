#pragma once

#include <Arduino.h>
#include "headless_board.h"

namespace launcher {

struct Services {
    void (*speak)(const String &message, bool sensitive, bool interrupt);
    void (*home)();
    bool (*connectWifi)();
    bool (*speechBusy)();
    void (*setSpeechBoostEnabled)(bool);
    void (*syncTime)();
    int64_t (*savedEpoch)();
};

struct App {
    bool (*open)(const Services &services);
    void (*key)(const HeadlessKeyboard::Event &, const HeadlessKeyboard::State &);
    void (*tick)();
    void (*close)();
};

}
