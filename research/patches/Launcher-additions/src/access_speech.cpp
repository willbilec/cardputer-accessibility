#ifdef LAUNCHER_ACCESS_SPEECH
#include "access_speech.h"
#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <atomic>
#include <espeak.h>

extern bool UseTCA8418;
namespace {
struct Message {
    char text[160];
    uint32_t posted;
    unsigned generation;
};
QueueHandle_t queue;
std::atomic<unsigned> generation{0};
std::atomic<unsigned> completedGeneration{0};
unsigned currentGeneration;
I2SClass i2s;

class SpeechOutput : public Print {
public:
    size_t write(uint8_t data) override { return write(&data, 1); }
    size_t write(const uint8_t *data, size_t size) override {
        if (currentGeneration != generation.load()) return size;
        return i2s.write(data, size);
    }
};
SpeechOutput output;
ESpeak synth(output);

void worker(void *) {
    synth.setUsePSRAM(false);
    if (!synth.begin(20)) {
        Serial.println("ACCESS: eSpeak initialization failed");
        vTaskDelete(nullptr);
        return;
    }
    synth.setRate(210);
    Serial.printf("ACCESS: ready free_heap=%u\n", ESP.getFreeHeap());
    Message message;
    while (xQueueReceive(queue, &message, portMAX_DELAY)) {
        currentGeneration = message.generation;
        Serial.printf(
            "ACCESS: start delay=%lu free_heap=%u\n",
            (unsigned long)(millis() - message.posted),
            ESP.getFreeHeap()
        );
        synth.say(message.text);
        completedGeneration.store(message.generation);
    }
}

bool codecWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(0x18);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}
} // namespace

bool accessSpeechBegin() {
    if (!UseTCA8418) return false;
    // Register sequence follows M5Unified's Cardputer ADV ES8311 driver.
    const uint8_t settings[][2] = {
        {0x00, 0x80},
        {0x01, 0xB5},
        {0x02, 0x18},
        {0x0D, 0x01},
        {0x12, 0x00},
        {0x13, 0x10},
        {0x32, 0xBF},
        {0x37, 0x08}
    };
    for (auto &setting : settings)
        if (!codecWrite(setting[0], setting[1])) return false;
    i2s.setPins(41, 43, 42);
    // Match M5Unified's ADV mono configuration: 16-bit I2S with both slots
    // enabled. eSpeak produces mono 22,050 Hz PCM.
    if (!i2s.begin(I2S_MODE_STD, 22050, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH))
        return false;
    queue = xQueueCreate(1, sizeof(Message));
    if (!queue) return false;
    if (xTaskCreatePinnedToCore(worker, "access-speech", 24576, nullptr, 1, nullptr, 0) != pdPASS) {
        vQueueDelete(queue);
        queue = nullptr;
        return false;
    }
    return true;
}

void accessSpeak(const char *text) {
    if (!queue || !text) return;
    Message msg{};
    strlcpy(msg.text, text, sizeof(msg.text));
    msg.generation = ++generation;
    msg.posted = millis();
    xQueueOverwrite(queue, &msg);
}

void accessSpeakAndWait(const char *text, unsigned timeoutMs) {
    if (!queue) return;
    accessSpeak(text);
    unsigned target = generation.load();
    uint32_t start = millis();
    while (completedGeneration.load() != target && millis() - start < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Allow the final samples already handed to the I2S driver to drain.
    vTaskDelay(pdMS_TO_TICKS(100));
}
#endif
