#pragma once

#include <Arduino.h>
#include <FS.h>

namespace playpalace::sound {
using Output = size_t (*)(unsigned rate, const uint8_t* pcm, size_t bytes);
void request(fs::FS& storage, const String& base, const String& name, unsigned volume);
void localCue(fs::FS& storage, const String& name, unsigned volume);
void cancel();
bool busy();
void changeVolume(unsigned previous, unsigned current);
void tick(Output output, void (*discard)());
void status();
}
