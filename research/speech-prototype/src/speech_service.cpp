#include "speech_service.h"
#include <Arduino.h>
#ifndef SPEECH_EMULATOR_OUTPUT
#include <M5Cardputer.h>
#else
// Test-only sink: real eSpeak/queue/task, no emulated codec/I2S dependency.
extern void speechEmulatorOutput(const int16_t *, size_t, unsigned);
extern void speechEmulatorCompleted(unsigned);
#endif
#include <esp_heap_caps.h>
#include <espeak.h>
#include <atomic>

#ifndef SPEECH_OUTPUT_CHANNEL
#define SPEECH_OUTPUT_CHANNEL 0
#endif

namespace {
struct Utterance { char text[160]; unsigned generation; uint32_t requested; };
std::atomic<unsigned> generation{0};
std::atomic<unsigned> completedGeneration{0};
std::atomic<bool> failed{false};
unsigned speakingGeneration;
uint32_t currentRequestTime;
bool firstAudio;
QueueHandle_t requests;
char lastUtterance[160];

class SpeakerOutput : public Print {
public:
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t *data, size_t length) override {
        size_t offset = 0;
        while (offset + 1 < length) {
            size_t bytes = min(length - offset, sizeof(buffer)) & ~size_t(1);
            if (generation.load() != speakingGeneration) return length;
            if (firstAudio) {
                firstAudio = false;
                Serial.printf("FIRST_PCM latency_ms=%lu\n", (unsigned long)(millis() - currentRequestTime));
            }
            memcpy(buffer, data + offset, bytes);
#ifdef SPEECH_EMULATOR_OUTPUT
            speechEmulatorOutput(buffer, bytes / 2, 22050);
#else
            if (!M5Cardputer.Speaker.playRaw(buffer, bytes / 2, 22050, false, 1, SPEECH_OUTPUT_CHANNEL)) return length;
            while (M5Cardputer.Speaker.isPlaying(SPEECH_OUTPUT_CHANNEL)) vTaskDelay(1);
#endif
            offset += bytes;
        }
        return length;
    }
private:
    int16_t buffer[512];
};
SpeakerOutput output;
ESpeak synth(output);

unsigned enqueue(const char *text) {
    if (!requests || failed.load() || !text || !*text) return 0;
    Utterance utterance{};
    strlcpy(utterance.text, text, sizeof(utterance.text));
    strlcpy(lastUtterance, utterance.text, sizeof(lastUtterance));
    utterance.generation = ++generation;
    utterance.requested = millis();
    xQueueOverwrite(requests, &utterance);
    return utterance.generation;
}

void worker(void *) {
    synth.setUsePSRAM(false);
    if (!synth.begin(20)) {
        failed.store(true);
        Serial.println("ERROR: eSpeak initialization failed");
        vTaskDelete(nullptr);
        return;
    }
    synth.setRate(210);
    Serial.printf("SPEECH_READY heap=%u largest=%u\n", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Utterance utterance;
    while (xQueueReceive(requests, &utterance, portMAX_DELAY)) {
        speakingGeneration = utterance.generation;
        currentRequestTime = utterance.requested;
        firstAudio = true;
        uint32_t start = millis();
        Serial.printf("SPEAK queue_ms=%lu heap=%u\n", (unsigned long)(start - utterance.requested), ESP.getFreeHeap());
        synth.say(utterance.text);
        completedGeneration.store(utterance.generation);
#ifdef SPEECH_EMULATOR_OUTPUT
        speechEmulatorCompleted(utterance.generation);
#endif
        Serial.printf("SPEAK_DONE duration_ms=%lu min_heap=%u\n", (unsigned long)(millis() - start), ESP.getMinFreeHeap());
    }
}
} // namespace

bool speechServiceBegin() {
    requests = xQueueCreate(1, sizeof(Utterance));
    if (!requests) return false;
    if (xTaskCreatePinnedToCore(worker, "speech", 24576, nullptr, 1, nullptr, 0) != pdPASS) {
        vQueueDelete(requests);
        requests = nullptr;
        return false;
    }
    return true;
}

bool speechSay(const char *text) { return enqueue(text) != 0; }
void speechStop() {
    ++generation;
    if (requests) xQueueReset(requests);
}
bool speechRepeat() {
    char copy[sizeof(lastUtterance)];
    strlcpy(copy, lastUtterance, sizeof(copy));
    return enqueue(copy) != 0;
}
bool speechSayAndWait(const char *text, uint32_t timeoutMs) {
    unsigned target = enqueue(text);
    if (!target) return false;
    uint32_t start = millis();
    while (completedGeneration.load() != target && !failed.load() && millis() - start < timeoutMs) delay(10);
    if (completedGeneration.load() != target) return false;
    delay(100);
    return true;
}
