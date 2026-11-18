// ESP32-S3 binary running real synthesis and the player's MP3 decoder.
// LCD, keyboard, SD hardware and I2S output are deliberately substituted.
#include <Arduino.h>
#include <WiFi.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceBuffer.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "speech_service.h"
#include "benchmark_tone.h"

#ifndef BENCH_EXTRA_RESERVE
#define BENCH_EXTRA_RESERVE 0
#endif
static std::atomic<unsigned> completions{0}, speechSamples{0};
static TaskHandle_t speechTaskHandle;
static uint32_t requestTime, firstSampleMs;
static unsigned currentSampleCount;
static volatile int32_t speechChecksum;
static bool firstSample;

static void memory(const char *stage) {
    Serial.printf("BENCH_MEM stage=%s free=%u minimum=%u largest=%u loop_stack=%u\n", stage,
        ESP.getFreeHeap(), ESP.getMinFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

void speechEmulatorOutput(const int16_t *pcm, size_t count, unsigned rate) {
    speechTaskHandle = xTaskGetCurrentTaskHandle();
    if (firstSample) {
        firstSample = false;
        firstSampleMs = millis() - requestTime;
        Serial.printf("BENCH_FIRST_PCM emulated_ms=%lu samples=%u\n", (unsigned long)firstSampleMs, (unsigned)count);
        memory("first_pcm");
    }
    // Consume actual generated samples, then apply a modeled playback duration.
    int32_t sum = speechChecksum;
    for (size_t i = 0; i < count; ++i) sum = (sum ^ pcm[i]) & 0x7fffffff;
    speechChecksum = sum;
    currentSampleCount += count;
    speechSamples += count;
    vTaskDelay(pdMS_TO_TICKS((count * 1000 + rate - 1) / rate));
}
void speechEmulatorCompleted(unsigned generation) {
    Serial.printf("BENCH_SPEECH_DONE generation=%u samples=%u emulated_elapsed_ms=%lu first_pcm_ms=%lu speech_stack=%u checksum=%ld\n",
        generation, currentSampleCount, (unsigned long)(millis() - requestTime),
        (unsigned long)firstSampleMs, (unsigned)uxTaskGetStackHighWaterMark(speechTaskHandle), (long)speechChecksum);
    memory("speech_done");
    ++completions;
}

class TimedMusicOutput : public AudioOutput {
    unsigned frames = 0;
    unsigned firstFrame = 0;
    uint32_t start = 0;
public:
    unsigned totalFrames = 0;
    int32_t checksum = 0;
    bool begin() override { start = millis(); frames = 0; firstFrame = totalFrames; return true; }
    bool ConsumeSample(int16_t sample[2]) override {
        if (frames >= 512) {
            uint32_t due = start + uint64_t(totalFrames - firstFrame) * 1000 / hertz;
            if ((int32_t)(millis() - due) < 0) return false;
            frames = 0;
        }
        checksum = (checksum + abs(int(sample[0]))) % 65521;
        ++frames; ++totalFrames;
        return true;
    }
    bool stop() override { return true; }
};
static TimedMusicOutput music;
static AudioFileSourcePROGMEM *source;
static AudioFileSourceBuffer *buffered;
static AudioGeneratorMP3 *decoder;
static uint8_t *extraReserve, *pcmReserve;
static int phase = 0;
static uint32_t phaseStarted;
static bool allFirst = false;

static void request(const char *text) {
    requestTime = millis(); firstSample = true; currentSampleCount = 0;
    speechSay(text);
}
static bool musicBegin() {
    if (decoder) { decoder->stop(); delete decoder; }
    if (buffered) { buffered->close(); delete buffered; }
    if (source) delete source;
    source = new AudioFileSourcePROGMEM(benchmarkTone, sizeof(benchmarkTone));
    buffered = new AudioFileSourceBuffer(source, 16384); // same size as player
    decoder = new AudioGeneratorMP3(); // same libmad decoder as player
    return decoder->begin(buffered, &music);
}
#ifndef BENCH_EXTERNAL_MAIN
void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(240);
    Serial.setTimeout(100);
    Serial.println("BENCH_CONFIG");
    delay(200);
    unsigned reserveBytes = BENCH_EXTRA_RESERVE;
    if (Serial.available()) {
        String configuration = Serial.readStringUntil('\n');
        reserveBytes = configuration.toInt();
        allFirst = configuration.indexOf("all-first") >= 0;
    }
    if (reserveBytes > 196608) { Serial.println("BENCH_FAIL invalid_reserve"); return; }
    Serial.printf("BENCH_BEGIN extra_reserve=%u psram=%u fixture_bytes=%u cpu_mhz=%u\n",
        reserveBytes, ESP.getPsramSize(), unsigned(sizeof(benchmarkTone)), getCpuFrequencyMhz());
    memory("baseline");
    Serial.printf("BENCH_ORDER all_first=%d\n", allFirst);
    if (reserveBytes) {
        extraReserve = (uint8_t *)malloc(reserveBytes);
        if (!extraReserve) { Serial.println("BENCH_FAIL extra_reserve"); return; }
        memset(extraReserve, 0x5a, reserveBytes);
    }
    // Preserve the player's 3 x 2048 x int16_t staging allocation, although the
    // output sink itself has no physical mixer, DMA, codec or speaker task.
    pcmReserve = (uint8_t *)malloc(12288);
    if (!pcmReserve) { Serial.println("BENCH_FAIL pcm_reserve"); return; }
    if (allFirst) {
        WiFi.begin("myssid", "mypassword");
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) delay(10);
        Serial.printf("BENCH_WIFI_PRELOAD status=%d\n", (int)WiFi.status());
        if (!musicBegin()) { Serial.println("BENCH_FAIL preload_mp3"); return; }
        memory("wifi_and_mp3_preloaded");
    }
    if (!speechServiceBegin()) { Serial.println("BENCH_FAIL service_start"); return; }
    memory("service_start");
    phaseStarted = millis();
}
void loop() {
    if (!pcmReserve) { delay(10); return; }
    if (phase == 0 && millis() - phaseStarted >= 1000) {
        phase = 1;
        memory(allFirst ? "preloaded_combined_request" : "speech_only_request");
        request("Music. First full song, one of twenty seven.");
    } else if (phase == 1 && completions.load() >= 1) {
        if (!decoder && !musicBegin()) { Serial.println("BENCH_FAIL mp3_begin"); phase = 99; return; }
        phase = 2;
        memory("music_and_speech_request");
        request("Playing music. Wi-Fi off. Speech and music together.");
    } else if (phase == 2 && completions.load() >= 2) {
        phase = 3; phaseStarted = millis();
        memory("wifi_start");
        if (WiFi.status() != WL_CONNECTED) WiFi.begin("myssid", "mypassword");
    } else if (phase == 3 && (WiFi.status() == WL_CONNECTED || millis() - phaseStarted >= 8000)) {
        Serial.printf("BENCH_WIFI status=%d\n", (int)WiFi.status());
        memory("wifi_and_music_speech_request");
        phase = 4;
        request(WiFi.status() == WL_CONNECTED ?
            "Playing music with Wi-Fi connected. Reading the selected song." :
            "Wi-Fi did not connect. Reading while music plays and the network is active.");
    } else if (phase == 4 && completions.load() >= 3) {
        phase = 5;
        memory("complete");
        Serial.printf("BENCH_COMPLETE speech_samples=%u music_frames=%u music_checksum=%ld wifi_status=%d\n",
            speechSamples.load(), music.totalFrames, (long)music.checksum, (int)WiFi.status());
    }
    if (decoder && phase <= 4) {
        if (!decoder->loop() && !musicBegin()) { Serial.println("BENCH_FAIL mp3_restart"); phase = 99; }
    }
    delay(1);
}
#endif
