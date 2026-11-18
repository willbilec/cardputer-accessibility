#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <map>
#include <vector>
#include <cstdio>
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *, size_t n) { return n; }
};
inline uint32_t fakeTime = 0;
inline std::map<int,int> fakePins;
inline uint32_t millis() { return fakeTime; }
inline int digitalRead(int pin) { return fakePins.count(pin) ? fakePins[pin] : 1; }
inline void digitalWrite(int pin, int value) { fakePins[pin] = value; }
inline void pinMode(int, int) {}
constexpr int LOW=0, HIGH=1, OUTPUT=1, INPUT_PULLUP=2;
struct { void begin(unsigned) {} template<class... A> void printf(const char *, A...) {} } inline Serial;
struct { bool restarted=false;void restart(){restarted=true;} unsigned getFreeHeap() { return 200000; } } inline ESP;
struct FakeQueue { unsigned depth; size_t size; std::deque<std::vector<uint8_t>> items; };
using QueueHandle_t = FakeQueue*;
inline QueueHandle_t xQueueCreate(unsigned n, size_t size) { return new FakeQueue{n,size,{}}; }
inline bool xQueueSend(QueueHandle_t q, const void *p, unsigned) {
    if (q->items.size() == q->depth) return false;
    const auto *data = static_cast<const uint8_t *>(p);
    q->items.emplace_back(data, data + q->size); return true;
}
inline bool xQueueReceive(QueueHandle_t q, void *p, unsigned) {
    if (q->items.empty()) return false;
    memcpy(p, q->items.front().data(), q->size); q->items.pop_front(); return true;
}
inline void xQueueReset(QueueHandle_t q) { q->items.clear(); }
inline void (*fakeOnTaskDelay)(unsigned)=nullptr;
inline void vTaskDelay(unsigned n) { fakeTime += n;if(fakeOnTaskDelay)fakeOnTaskDelay(n); }
inline void delay(unsigned n) { fakeTime += n; }
inline unsigned fakeMixerStack = 0;
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned stack, void *, int, void *, int) {
    fakeMixerStack = stack; return 1;
}
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(n) (n)
constexpr int pdPASS=1, pdTRUE=1;
