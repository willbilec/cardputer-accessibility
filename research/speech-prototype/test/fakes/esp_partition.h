#pragma once
#include <cstring>
struct esp_partition_t {};
constexpr int ESP_PARTITION_TYPE_APP = 0;
constexpr int ESP_PARTITION_SUBTYPE_APP_TEST = 0x20;
inline bool fakeLauncherPartition = true;
inline const esp_partition_t *esp_partition_find_first(int, int, const char *label) {
    static esp_partition_t partition;
    return fakeLauncherPartition && label && std::strcmp(label, "app0") == 0 ? &partition : nullptr;
}
