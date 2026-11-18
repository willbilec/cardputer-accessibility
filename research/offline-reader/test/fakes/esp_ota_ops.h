#pragma once
struct esp_partition_t { unsigned address, size; };
constexpr int ESP_PARTITION_TYPE_APP = 0;
constexpr int ESP_PARTITION_SUBTYPE_APP_FACTORY = 0;
constexpr int ESP_PARTITION_SUBTYPE_APP_OTA_0 = 16;
#ifndef FAKE_ESP_OK_DEFINED
#define FAKE_ESP_OK_DEFINED
constexpr int ESP_OK = 0;
#endif
inline esp_partition_t factory{0x10000, 0x280000}, player{0x290000, 0x560000};
inline bool fakeHasFactory = true;
inline const esp_partition_t *esp_partition_find_first(int, int subtype, const char *) {
    return subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ? (fakeHasFactory ? &factory : nullptr) : &player;
}
inline int esp_ota_set_boot_partition(const esp_partition_t *) { return ESP_OK; }
