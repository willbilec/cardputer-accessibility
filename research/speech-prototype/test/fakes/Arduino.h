#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using std::min;
inline uint32_t fakeMillis = 0;
inline void (*fakeOnDelay)() = nullptr;
inline uint32_t millis() { return fakeMillis; }
inline void vTaskDelay(uint32_t ms) { fakeMillis += ms; }
inline void delay(uint32_t ms) { fakeMillis += ms; if (fakeOnDelay) fakeOnDelay(); }
inline void vTaskDelete(void *) {}
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
struct Print {
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *, size_t) = 0;
};
struct FakeQueue { std::vector<uint8_t> bytes; bool present = false; };
using QueueHandle_t = FakeQueue *;
inline QueueHandle_t xQueueCreate(int, size_t size) { auto *q = new FakeQueue; q->bytes.resize(size); return q; }
inline void xQueueOverwrite(QueueHandle_t q, const void *value) { memcpy(q->bytes.data(), value, q->bytes.size()); q->present = true; }
inline int xQueueReceive(QueueHandle_t q, void *value, uint32_t) {
    if (!q->present) return 0;
    memcpy(value, q->bytes.data(), q->bytes.size()); q->present = false; return 1;
}
inline void xQueueReset(QueueHandle_t q) { q->present = false; }
inline void vQueueDelete(QueueHandle_t q) { delete q; }
inline void (*fakeTask)(void *) = nullptr;
inline int xTaskCreatePinnedToCore(void (*fn)(void *), const char *, int, void *, int, void *, int) {
    fakeTask = fn;
    return pdPASS;
}
inline size_t strlcpy(char *dst, const char *src, size_t n) {
    size_t len = strlen(src);
    if (n) { size_t count = min(len, n - 1); memcpy(dst, src, count); dst[count] = 0; }
    return len;
}
struct FakeSerial {
    std::string input, output;
    size_t offset = 0;
    size_t available() const { return input.size() - offset; }
    int read() { return input[offset++]; }
    void println(const char *s) { output += s; output += '\n'; }
    template<typename... Args> void printf(const char *format, Args... args) {
        char buf[256]; snprintf(buf, sizeof(buf), format, args...); output += buf;
    }
    void feed(const std::string &s) { input = s; offset = 0; }
};
inline FakeSerial Serial;
struct FakeESP { unsigned getFreeHeap() { return 100000; } unsigned getMinFreeHeap() { return 90000; } };
inline FakeESP ESP;
