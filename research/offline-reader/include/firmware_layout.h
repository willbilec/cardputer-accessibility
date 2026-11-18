#pragma once
#include <esp_ota_ops.h>
#include "reader_core.h"

inline const esp_partition_t *readerFactory() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "reader");
}
inline const esp_partition_t *readerPlayer() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "player");
}
inline bool readerLayoutValid(unsigned imageSize = 1) {
    const auto *factory = readerFactory();
    const auto *player = readerPlayer();
    return factory && player && reader::validLayout(factory->address, factory->size,
        player->address, player->size, imageSize);
}
inline bool readerReturnToLauncher() {
    if (!readerLayoutValid()) return false;
    if (esp_ota_set_boot_partition(readerFactory()) != ESP_OK) return false;
    ESP.restart();
    return true;
}
