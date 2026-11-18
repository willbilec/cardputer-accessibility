#include "launcher_return.h"
#include <esp_partition.h>
#include <esp_sleep.h>
#include <nvs.h>

LauncherReturn launcherReturnCheck() {
    // The paired experimental Launcher layout names its test app "app0".
    // Avoid treating an unrelated test partition as our launcher.
    const auto *launcher = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, "app0");
    if (!launcher) return LauncherReturn::NoLauncher;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("launcher", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return launcherReturnPolicy(true, true, false, -1);
    if (err != ESP_OK) return LauncherReturn::SettingsUnavailable;
    uint8_t ddlb = 0;
    int32_t gpio = -1;
    esp_err_t ddlbErr = nvs_get_u8(handle, "DDLB", &ddlb);
    esp_err_t gpioErr = nvs_get_i32(handle, "LauncherOnKey", &gpio);
    nvs_close(handle);
    bool ok = (ddlbErr == ESP_OK || ddlbErr == ESP_ERR_NVS_NOT_FOUND) &&
              (gpioErr == ESP_OK || gpioErr == ESP_ERR_NVS_NOT_FOUND);
    return launcherReturnPolicy(true, ok, ddlb != 0, gpio);
}

bool launcherReturnWake() {
    if (esp_sleep_enable_timer_wakeup(100000) != ESP_OK) return false;
    esp_deep_sleep_start();
    return false; // Only reached if deep sleep fails unexpectedly.
}
