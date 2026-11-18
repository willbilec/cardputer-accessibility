// Bounded, streaming speech backend. Uses the same UI API as the eSpeak service.
#include <Arduino.h>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <tts/tts.h>
#include <esp_heap_caps.h>
#include "speech_service.h"
#include "klatt_render.h"

#ifdef SPEECH_EMULATOR_OUTPUT
extern void speechEmulatorOutput(const int16_t *, size_t, unsigned);
extern void speechEmulatorCompleted(unsigned);
#else
#include "headless_board.h"
#endif

namespace {
struct Utterance { char text[160]; unsigned generation; uint32_t requested; };
alignas(float) uint8_t arena[32768];
std::atomic<unsigned> generation{0}, completed{0}, failedGeneration{0};
QueueHandle_t requests;
char lastUtterance[160];
tts::VoiceParams voice = tts::DefaultVoiceParams();

unsigned enqueue(const char *text) {
    if (!requests || !text || !*text) return 0;
    Utterance request{};
    strlcpy(request.text, text, sizeof(request.text));
    strlcpy(lastUtterance, request.text, sizeof(lastUtterance));
    request.requested = millis(); request.generation = ++generation;
    xQueueOverwrite(requests, &request);
    return request.generation;
}

void worker(void *) {
    tts::StreamSynth synth(voice, arena, sizeof(arena));
    tts::StreamOptions options;
    options.sample_rate = 22050;
    options.speed = 1.2f;
    int16_t pcm[256];
    Serial.printf("KLATT_READY heap=%u largest=%u arena=%u\n", ESP.getFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), unsigned(sizeof(arena)));
    Utterance utterance;
    while (xQueueReceive(requests, &utterance, portMAX_DELAY)) {
#if defined(READER_PIPELINED_AUDIO) && !defined(SPEECH_EMULATOR_OUTPUT)
        M5Cardputer.Speaker.stop(1);
#endif
        bool first = true;
        int result = renderKlattText(synth, utterance.text, options,
            [&](const float *samples, int n) {
                for (int i = 0; i < n; ++i)
                    pcm[i] = int16_t(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f);
                if (first) {
                    first = false;
                    Serial.printf("FIRST_PCM latency_ms=%lu\n", (unsigned long)(millis() - utterance.requested));
                }
#ifdef SPEECH_EMULATOR_OUTPUT
                speechEmulatorOutput(pcm, n, 22050);
#else
                if (!M5Cardputer.Speaker.playRaw(pcm, n, 22050, false, 1, 1)) return false;
#ifndef READER_PIPELINED_AUDIO
                while (M5Cardputer.Speaker.isPlaying(1)) vTaskDelay(1);
#endif
#endif
                return true;
            }, [&]() { return generation.load() == utterance.generation; });
#if defined(READER_PIPELINED_AUDIO) && !defined(SPEECH_EMULATOR_OUTPUT)
        // Diagnostic alternative: compute ahead into the existing bounded
        // queue, draining once at utterance end instead of after each chunk.
        while (generation.load()==utterance.generation && M5Cardputer.Speaker.isPlaying(1)) vTaskDelay(1);
        if (generation.load()!=utterance.generation) M5Cardputer.Speaker.stop(1);
#endif
        if (result < 0) {
            failedGeneration.store(utterance.generation);
            Serial.printf("KLATT_ERROR status=%d\n", result);
#ifdef SPEECH_EMULATOR_OUTPUT
            Serial.printf("BENCH_FAIL klatt_status=%d\n", result);
#endif
        }
        completed.store(utterance.generation);
#ifdef SPEECH_EMULATOR_OUTPUT
        speechEmulatorCompleted(utterance.generation);
#endif
    }
}
}

bool speechServiceBegin() {
    if (requests) return true;
    requests = xQueueCreate(1, sizeof(Utterance));
    if (!requests) return false;
    if (xTaskCreatePinnedToCore(worker, "klatt-reader", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        vQueueDelete(requests); requests = nullptr; return false;
    }
    return true;
}
bool speechSay(const char *text) { return enqueue(text) != 0; }
void speechStop() { ++generation; if (requests) xQueueReset(requests); }
bool speechRepeat() { char copy[160]; strlcpy(copy, lastUtterance, sizeof(copy)); return enqueue(copy) != 0; }
bool speechSayAndWait(const char *text, uint32_t timeoutMs) {
    unsigned target = enqueue(text);
    if (!target) return false;
    uint32_t start = millis();
    while (completed.load() != target && millis() - start < timeoutMs) delay(5);
    return completed.load() == target && failedGeneration.load() != target;
}
