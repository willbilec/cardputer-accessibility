#pragma once
#include <cstdint>

constexpr unsigned usbSDBurstSize=2048;

void startUsbSD(bool writable, bool (*setTime)(int64_t));
void stopUsbSD();
void pollUsbSD();

#ifdef UNIFIED_WIFI_SD
constexpr bool wifiSDEnabled=true;
bool startWifiSD(const char *code);
void stopWifiSD();
void pollWifiSD();
#else
constexpr bool wifiSDEnabled=false;
inline bool startWifiSD(const char *){return false;}
inline void stopWifiSD(){}
inline void pollWifiSD(){}
#endif
