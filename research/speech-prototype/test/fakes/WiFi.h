#pragma once
#include "Arduino.h"
constexpr int WL_CONNECTED = 3;
constexpr int WIFI_STA = 1;
struct FakeWiFi {
    int state = 0;
    std::string ssid, password;
    bool persistence = true;
    int status() const { return state; }
    void persistent(bool value) { persistence = value; }
    void mode(int) {}
    void begin(const char *name, const char *pw) { ssid = name; password = pw; state = WL_CONNECTED; }
};
inline FakeWiFi WiFi;
inline uint32_t fakeBytes = 0;
struct WiFiClient {
    void setTimeout(int) {}
    bool connect(const char *, uint16_t, int) { return true; }
    bool connected() { return true; }
    int availableForWrite() { return 512; }
    size_t write(const uint8_t *, size_t n) { fakeBytes += n; return n; }
    void stop() {}
};
