// ADV-only board glue. Pin/remap/codec settings follow the inspected M5Stack
// Cardputer keyboard and M5Unified ADV speaker sources (MIT licensed).
#include "headless_board.h"
#include <Wire.h>
#include <driver/i2s_std.h>
#include <algorithm>
#include "key_learning.h"

HeadlessBoard M5Cardputer;
namespace {
bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address); Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
}
int readRegister(uint8_t address, uint8_t reg) {
    Wire.beginTransmission(address); Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, uint8_t(1)) != 1) return -1;
    return Wire.read();
}
using keys::plain;
using keys::shifted;
struct Packet { uint16_t count; unsigned rate, generation; bool stereo; int16_t pcm[2048]; };
Packet staging[2]; // One producer per channel; no 4 KiB temporary on the UI/speech stack.
QueueHandle_t queues[2];
std::atomic<unsigned> pending[2]{{0}, {0}}, stopGeneration[2]{{0}, {0}};
portMUX_TYPE audioCounts = portMUX_INITIALIZER_UNLOCKED;
i2s_chan_handle_t tx;
}

bool HeadlessKeyboard::begin() {
    state.word.reserve(56); state.hid_keys.reserve(56); keyEvents.reserve(10);
    for (uint8_t reg = 0x23; reg <= 0x25; ++reg)
        if (!writeRegister(0x34, reg, 0)) return false;
    if (!writeRegister(0x34, 0x1d, 0x7f) || !writeRegister(0x34, 0x1e, 0xff) ||
        !writeRegister(0x34, 0x1f, 0) || !writeRegister(0x34, 1, 3)) return false;
    for (int n = 0; n < 10 && (readRegister(0x34, 3) & 15); ++n) readRegister(0x34, 4);
    writeRegister(0x34, 2, 0xff);
    return ready = true;
}
void HeadlessKeyboard::update() {
    changed = false;
    quitChord = false;
    keyEvents.clear();
    if (!ready) return;
    int available = readRegister(0x34, 3);
    if (available < 0) return;
    for (int n = 0; n < (available & 15) && n < 10; ++n) {
        int raw = readRegister(0x34, 4), event = (raw & 0x7f) - 1;
        if (raw < 0 || event < 0) continue;
        int originalRow = event / 10, originalCol = event % 10;
        int row = (originalCol + 4) % 4, col = originalRow * 2 + (originalCol > 3);
        if (row < 4 && col < 14) {
            bool down = raw & 0x80;
            if (held[row][col] != down) { changed = true; keyEvents.push_back({uint8_t(row),uint8_t(col),down}); }
            held[row][col] = down;
            if (down && ((row == 1 && col == 1) || (row == 3 && col == 2)) &&
                held[1][1] && held[3][2] && !held[3][0] && !held[3][1] && !held[2][0]) {
                quitChord = true;
            }
        }
    }
    writeRegister(0x34, 2, 0xff);
    if (!changed) return;
    state.word.clear(); state.hid_keys.clear();
    state.enter=held[2][13]; state.space=held[3][13];
    state.backspace=state.del=state.esc=false;
    state.shift = held[2][1]; state.ctrl = held[3][0]; state.fn = held[2][0];
    state.opt=held[3][1]; state.alt=held[3][2];
    state.modifiers=(state.ctrl?1:0)|(state.shift?2:0)|(state.alt?4:0);
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 14; ++col) if (held[row][col]) {
        uint8_t hid = keys::hidUsage(row,col,state.fn);
        if (hid) state.hid_keys.push_back(hid);
        uint8_t c = state.shift ? shifted[row][col] : plain[row][col];
        if (state.fn) { if(hid==0x29) state.esc=true; if(hid==0x4c) state.del=true; continue; }
        if (c == 13) state.enter = true;
        else if (c == 8) state.backspace = state.del = true;
        else if (state.fn && row == 0 && col == 0) state.esc = true;
        else if (c >= 32) { state.word.push_back(c); if (c == ' ') state.space = true; }
    }
}
bool HeadlessKeyboard::isPressed() const {
    for (const auto &row : held) for (bool value : row) if (value) return true;
    return false;
}
bool HeadlessKeyboard::isKeyPressed(char c) const {
    if (c == KEY_ENTER) return state.enter;
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 14; ++col)
        if (held[row][col] && (state.shift ? shifted[row][col] : plain[row][col]) == uint8_t(c)) return true;
    return false;
}

void HeadlessButton::update() {
    bool now = digitalRead(0) == LOW;
    if (now != down && millis() - last >= 25) {
        down = now; last = millis(); if (!now) ++clicks;
    }
    if (!down && clicks && millis() - last > 350) { decided = clicks; clicks = 0; }
}
bool HeadlessButton::wasDecideClickCount() {
    bool result = decided > 0; return result;
}

void headlessAudioWorker(void *) {
    Packet packet[2]{};
    size_t index[2]{}; unsigned fraction[2]{};
    bool active[2]{};
    unsigned generation[2]{};
    int16_t output[256];
    while (true) {
        bool any = false;
        for (int frame = 0; frame < 256; ++frame) {
            int32_t sum = 0;
            for (unsigned channel = 0; channel < 2; ++channel) {
                if (generation[channel] != stopGeneration[channel].load()) {
                    generation[channel] = stopGeneration[channel].load(); active[channel] = false;
                }
                if (!active[channel] && xQueueReceive(queues[channel], &packet[channel], 0)) {
                    active[channel] = packet[channel].generation == stopGeneration[channel].load();
                    index[channel] = 0; fraction[channel] = 0;
                }
                if (!active[channel]) continue;
                any = true;
                auto &p = packet[channel];
                int32_t value = p.pcm[index[channel]];
                if (p.stereo) value = (value + p.pcm[index[channel] + 1]) / 2;
                sum += value * M5Cardputer.Speaker.gain[channel].load() / 255;
                fraction[channel] += p.rate;
                while (fraction[channel] >= 44100) {
                    fraction[channel] -= 44100; index[channel] += p.stereo ? 2 : 1;
                }
                if (index[channel] >= p.count) {
                    active[channel] = false;
                    portENTER_CRITICAL(&audioCounts);
                    if (p.generation == stopGeneration[channel].load() && pending[channel].load()) --pending[channel];
                    portEXIT_CRITICAL(&audioCounts);
                }
            }
            sum = sum * M5Cardputer.Speaker.master.load() / 255;
            output[frame] = std::clamp(sum, int32_t(-32768), int32_t(32767));
        }
        if (any) {
            size_t written = 0;
            int result = i2s_channel_write(tx, output, sizeof(output), &written, 100);
            static bool reportedFault = false;
            if ((result != ESP_OK || written != sizeof(output)) && !reportedFault) {
                reportedFault = true;
                Serial.printf("HEADLESS_AUDIO write_failed code=%d bytes=%u\n", result, unsigned(written));
            }
        }
        else vTaskDelay(1);
    }
}
bool HeadlessSpeaker::begin() {
#ifdef HEADLESS_STARTUP_TRACE
    Serial.println("BOARD_AUDIO before_codec_registers");
#endif
    const uint8_t settings[][2] = {{0,0x80},{1,0xb5},{2,0x18},{0x0d,1},{0x12,0},{0x13,0x10},{0x32,0xbf},{0x37,8}};
    for (auto &setting : settings) if (!writeRegister(0x18, setting[0], setting[1])) return false;
#ifdef HEADLESS_STARTUP_TRACE
    Serial.println("BOARD_AUDIO before_i2s_new_channel");
#endif
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6; channel.dma_frame_num = 256;
    // Match M5Stack: drain to silence when the producer has no new PCM.
    channel.auto_clear = true;
    if (i2s_new_channel(&channel, &tx, nullptr) != ESP_OK) return false;
#ifdef HEADLESS_STARTUP_TRACE
    Serial.println("BOARD_AUDIO after_i2s_new_channel");
#endif
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    config.gpio_cfg.mclk = I2S_GPIO_UNUSED; config.gpio_cfg.bclk = GPIO_NUM_41;
    config.gpio_cfg.ws = GPIO_NUM_43; config.gpio_cfg.dout = GPIO_NUM_42; config.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(tx, &config) != ESP_OK) return false;
#ifdef HEADLESS_STARTUP_TRACE
    Serial.println("BOARD_AUDIO after_i2s_std_mode");
#endif
    if (i2s_channel_enable(tx) != ESP_OK) return false;
#ifdef HEADLESS_STARTUP_TRACE
    Serial.println("BOARD_AUDIO after_i2s_enable");
#endif
    for (auto &queue : queues) { queue = xQueueCreate(2, sizeof(Packet)); if (!queue) return false; }
    // Packet staging is on this worker's stack; reserve enough for both packets.
    return xTaskCreatePinnedToCore(headlessAudioWorker, "reader-mixer", 12288, nullptr, 3, nullptr, 1) == pdPASS;
}
bool HeadlessSpeaker::playRaw(const int16_t *pcm, size_t count, unsigned rate, bool stereo, int repeat, unsigned channel) {
    if (channel > 1 || !queues[channel] || !pcm || !count || count > 2048 || (stereo && count % 2) || repeat != 1 || rate < 8000 || rate > 192000) return false;
    auto &packet = staging[channel]; packet.count = count; packet.rate = rate; packet.stereo = stereo;
    packet.generation = stopGeneration[channel].load();
    memcpy(packet.pcm, pcm, count * sizeof(int16_t));
    ++pending[channel];
    if (xQueueSend(queues[channel], &packet, pdMS_TO_TICKS(100)) != pdTRUE) { --pending[channel]; return false; }
    return true;
}
bool HeadlessSpeaker::isPlaying(unsigned channel) const { return channel < 2 && pending[channel].load() != 0; }
void HeadlessSpeaker::stop(unsigned channel) {
    // Stop is called by the same producer that submits this channel's audio.
    if (channel < 2 && queues[channel]) {
        portENTER_CRITICAL(&audioCounts);
        ++stopGeneration[channel]; pending[channel] = 0;
        portEXIT_CRITICAL(&audioCounts);
        xQueueReset(queues[channel]);
    }
}
void HeadlessSpeaker::setChannelVolume(unsigned channel, uint8_t volume) { if (channel < 2) gain[channel] = volume; }
bool HeadlessBoard::begin() {
    pinMode(38, OUTPUT); digitalWrite(38, LOW); // Backlight off; no LCD driver loaded.
    pinMode(37, OUTPUT); digitalWrite(37, HIGH);
    pinMode(0, INPUT_PULLUP);
    Wire.begin(8, 9);
    bool keyboard = Keyboard.begin(), audio = Speaker.begin();
    Serial.printf("HEADLESS_BOARD keyboard=%d audio=%d heap=%u\n", keyboard, audio, ESP.getFreeHeap());
    return keyboard && audio;
}
