#pragma once
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int NVS_READONLY = 0;
inline esp_err_t fakeNvsOpen = ESP_OK, fakeDdlbResult = ESP_OK, fakeGpioResult = ESP_OK;
inline uint8_t fakeDdlb = 0;
inline int32_t fakeGpio = -1;
inline esp_err_t nvs_open(const char *, int, nvs_handle_t *h) { *h = 1; return fakeNvsOpen; }
inline esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *value) { *value = fakeDdlb; return fakeDdlbResult; }
inline esp_err_t nvs_get_i32(nvs_handle_t, const char *, int32_t *value) { *value = fakeGpio; return fakeGpioResult; }
inline void nvs_close(nvs_handle_t) {}
