#include "network_test.h"
#include <Arduino.h>
#include <Network.h>
#include <WiFi.h>
#include <atomic>
#include <cstring>

namespace {
std::atomic<bool> running{false};
std::atomic<bool> stopRequested{false};
struct LoadTarget { char host[64]; uint16_t port; };
LoadTarget target;

void loadWorker(void *) {
    WiFiClient client;
    uint8_t data[512];
    memset(data, 0x5a, sizeof(data));
    uint32_t bytes = 0;
    uint32_t started = millis();
    client.setTimeout(1000);
    if (!client.connect(target.host, target.port, 3000)) {
        Serial.println("LOAD connection failed");
    } else {
        Serial.println("LOAD started for up to 30 seconds");
        while (!stopRequested.load() && WiFi.status() == WL_CONNECTED && client.connected() &&
               millis() - started < 30000) {
            int available = client.availableForWrite();
            if (available > 0) {
                size_t n = min(static_cast<size_t>(available), sizeof(data));
                bytes += client.write(data, n);
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        client.stop();
        Serial.printf("LOAD done bytes=%lu elapsed_ms=%lu free_heap=%u min_heap=%u\n",
                      (unsigned long)bytes, (unsigned long)(millis() - started), ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }
    running.store(false);
    vTaskDelete(nullptr);
}
} // namespace

bool networkTestRunning() { return running.load(); }

bool networkTestPoll() {
    static char buffer[192];
    static size_t used = 0;
    static bool overflow = false;
    bool wifiRequested = false;
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (used + 1 < sizeof(buffer)) buffer[used++] = c;
            else overflow = true;
            continue;
        }
        buffer[used] = '\0';
        if (overflow) {
            Serial.println("Command too long");
        } else if (strncmp(buffer, "WIFI\t", 5) == 0) {
            char *ssid = buffer + 5;
            char *password = strchr(ssid, '\t');
            if (password) *password++ = '\0';
            if (!password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 63) {
                Serial.println("WIFI requires SSID and password separated by tabs");
            } else {
                WiFi.persistent(false);
                WiFi.mode(WIFI_STA);
                WiFi.begin(ssid, password);
                wifiRequested = true;
                Serial.println("WIFI connection requested");
            }
        } else if (strncmp(buffer, "LOAD\t", 5) == 0) {
            char *host = buffer + 5;
            char *portText = strchr(host, '\t');
            if (portText) *portText++ = '\0';
            char *end = nullptr;
            unsigned long port = portText ? strtoul(portText, &end, 10) : 0;
            if (!portText || !host[0] || strlen(host) >= sizeof(target.host) || !*portText || *end ||
                port == 0 || port > 65535) {
                Serial.println("LOAD requires host and port separated by tabs");
            } else if (WiFi.status() != WL_CONNECTED || running.load()) {
                Serial.println("LOAD requires connected Wi-Fi and no active load");
            } else {
                strlcpy(target.host, host, sizeof(target.host));
                target.port = static_cast<uint16_t>(port);
                stopRequested.store(false);
                running.store(true);
                if (xTaskCreatePinnedToCore(loadWorker, "network-test", 6144, nullptr, 1, nullptr, 1) != pdPASS) {
                    running.store(false);
                    Serial.println("LOAD task creation failed");
                }
            }
        } else if (strcmp(buffer, "STOP") == 0) {
            stopRequested.store(true);
        } else if (used) {
            Serial.println("Unknown command");
        }
        // Clear the entire buffer after every line so credentials are not retained here.
        memset(buffer, 0, sizeof(buffer));
        used = 0;
        overflow = false;
    }
    return wifiRequested;
}
