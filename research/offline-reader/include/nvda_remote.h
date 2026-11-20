#pragma once
#include <Arduino.h>
#include <FS.h>
#include "headless_board.h"
#include "keyboard_map.h"

namespace nvdaRemote {
using Say = void (*)(const String&,bool,bool);
void open(fs::FS& fs,bool sdReady,Say say,bool (*wifi)(),void (*stop)(),void (*speechBoost)(bool));
void close();
bool busy();
void status();
void performance();
int taskCore();
void localShortcut();
void key(const HeadlessKeyboard::Event&,const HeadlessKeyboard::State&);
void keyboard(const keyboardMap::Map&);
void tick(bool speaking);
}
