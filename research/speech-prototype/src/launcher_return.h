#pragma once
#include <cstdint>

// Policy derived from the upstream launcher_keyboot bootloader. A positive
// configured GPIO overrides both power-on and deep-sleep behavior.
enum class LauncherReturn { Ready, NoLauncher, SettingsUnavailable, Disabled };
inline LauncherReturn launcherReturnPolicy(bool hasLauncher, bool settingsReadable, bool disableDeepSleep,
                                           int32_t forceGpio) {
    if (!hasLauncher) return LauncherReturn::NoLauncher;
    if (!settingsReadable) return LauncherReturn::SettingsUnavailable;
    if (disableDeepSleep || forceGpio > 0) return LauncherReturn::Disabled;
    return LauncherReturn::Ready;
}

LauncherReturn launcherReturnCheck();
bool launcherReturnWake();
