#pragma once
#include <Arduino.h>
#include <atomic>
#include <vector>

// Minimal compatibility surface for the existing player. No graphics includes,
// fonts, framebuffer, LCD bus initialization or sprite allocation.
constexpr int BLACK = 0, TFT_BLACK = 0, TFT_RED = 0xf800, TFT_WHITE = 0xffff;
constexpr int TFT_ORANGE = 0xfd20, TFT_YELLOW = 0xffe0, TFT_GREEN = 0x7e0;
constexpr char KEY_ENTER = 13;
struct HeadlessDisplay : Print {
    using Print::write;
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *, size_t n) override { return n; }
    template<class... A> void fillScreen(A...) {}
    template<class... A> void setCursor(A...) {}
    template<class... A> void setTextColor(A...) {}
    template<class... A> void setRotation(A...) {}
    template<class... A> void setBrightness(A...) {}
    void sleep() {} void wakeup() {}
    int width() const { return 240; }
    int height() const { return 135; }
};
struct HeadlessSprite {
    explicit HeadlessSprite(HeadlessDisplay *) {}
    void setColorDepth(int) {}
    void *createSprite(int, int) { return nullptr; }
};
class HeadlessKeyboard {
public:
    struct State {
        bool ctrl = false, shift = false, fn = false, opt = false, alt = false, enter = false, space = false;
        bool backspace = false, del = false, esc = false;
        uint8_t modifiers = 0;
        std::vector<char> word;
        std::vector<uint8_t> hid_keys;
    };
    using KeysState = State;
    struct Event { uint8_t row, col; bool down; };
    bool begin();
    void update();
    bool isChange() const { return changed; }
    bool isPressed() const;
    bool isKeyPressed(char c) const;
    const State &keysState() const { return state; }
    const std::vector<Event> &events() const { return keyEvents; }
    bool quitRequested() const { return quitChord; }
    bool keyDown(unsigned row, unsigned col) const { return row<4 && col<14 && held[row][col]; }
private:
    bool held[4][14]{};
    bool changed = false, ready = false, quitChord = false;
    State state;
    std::vector<Event> keyEvents;
};
using Keyboard_Class = HeadlessKeyboard;
class HeadlessSpeaker {
public:
    struct Config { unsigned sample_rate = 44100; int task_pinned_core = 1, dma_buf_count = 6, dma_buf_len = 256, task_priority = 3; };
    bool begin();
    Config config() const { return {}; }
    void config(const Config &) {} // Output remains fixed at 44.1 kHz in this test.
    void setVolume(uint8_t volume) { master.store(volume); }
    uint8_t getVolume() const { return master.load(); }
    void setChannelVolume(unsigned channel, uint8_t volume);
    bool playRaw(const int16_t *, size_t, unsigned, bool, int, unsigned);
    bool isPlaying(unsigned) const;
    void stop(unsigned);
private:
    std::atomic<uint8_t> master{128};
    std::atomic<uint8_t> gain[2]{{255}, {255}};
    friend void headlessAudioWorker(void *);
};
namespace m5 { using Speaker_Class = HeadlessSpeaker; }
struct HeadlessButton {
    bool wasDecideClickCount();
    int getClickCount() { int result = decided; decided = 0; return result; }
    void update();
    bool down = false;
    uint32_t last = 0;
    int clicks = 0, decided = 0;
};
struct HeadlessBoard {
    HeadlessDisplay Display;
    HeadlessKeyboard Keyboard;
    HeadlessSpeaker Speaker;
    HeadlessButton BtnA;
    bool begin();
    void update() { Keyboard.update(); BtnA.update(); }
};
extern HeadlessBoard M5Cardputer;
