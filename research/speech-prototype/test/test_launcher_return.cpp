#include <cassert>
#include "../src/launcher_return.cpp"

int main() {
    fakeLauncherPartition = false;
    assert(launcherReturnCheck() == LauncherReturn::NoLauncher);
    fakeLauncherPartition = true;
    fakeNvsOpen = ESP_ERR_NVS_NOT_FOUND;
    assert(launcherReturnCheck() == LauncherReturn::Ready);
    fakeNvsOpen = 99;
    assert(launcherReturnCheck() == LauncherReturn::SettingsUnavailable);
    fakeNvsOpen = ESP_OK;
    fakeDdlb = 1;
    assert(launcherReturnCheck() == LauncherReturn::Disabled);
    fakeDdlb = 0; fakeGpio = 7;
    assert(launcherReturnCheck() == LauncherReturn::Disabled);
    fakeGpio = -1; fakeGpioResult = 99;
    assert(launcherReturnCheck() == LauncherReturn::SettingsUnavailable);
    fakeGpioResult = ESP_OK;
    assert(launcherReturnCheck() == LauncherReturn::Ready);
    fakeWakeResult = 99;
    assert(!launcherReturnWake() && !fakeSleepStarted);
    fakeWakeResult = ESP_OK;
    assert(!launcherReturnWake() && fakeSleepStarted && fakeWakeMicros == 100000);
}
