#pragma once

#include <Arduino.h>
#include <FS.h>

namespace playpalace {
struct Key {
    char base, character;
    int function;
    bool fn, shift, ctrl, alt;
};

using Speak = void (*)(const String&, bool, bool);
using ConnectWifi = bool (*)();
using ReturnHome = void (*)();
void open(fs::FS& card, bool mounted, Speak speak, ConnectWifi connectWifi, ReturnHome returnHome);
void close();
bool busy();
void tick(bool speechBusy);
bool canRepeatMenuArrow();
void key(const Key& key, bool repeated = false);
void readGameView();
void status();
void memoryStatus();
void adjustEffectVolume(int delta);
void setMediaVolume(unsigned percent);
void stopAnnouncements();
bool readingText();
}
