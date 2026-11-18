#pragma once
#include <cstdint>

// Call these from the application's UI task, not an interrupt or another task.
// begin() follows M5Cardputer.begin(). Text is copied into a bounded queue and
// a new request interrupts earlier speech. Successful enqueue is not a promise
// that the codec or synthesizer has produced sound.
bool speechServiceBegin();
bool speechSay(const char *utf8);
void speechStop();
bool speechRepeat();
bool speechSayAndWait(const char *utf8, uint32_t timeoutMs);
