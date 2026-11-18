#pragma once
#include "nvs.h"
inline esp_err_t fakeWakeResult = ESP_OK;
inline uint64_t fakeWakeMicros = 0;
inline bool fakeSleepStarted = false;
inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t micros) { fakeWakeMicros = micros; return fakeWakeResult; }
inline void esp_deep_sleep_start() { fakeSleepStarted = true; }
