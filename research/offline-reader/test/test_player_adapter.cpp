// Runs the real player_access.h adapter with model/keyboard/audio/SDK fakes.
// This deliberately does not emulate the player decoder, UI drawing or hardware.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

struct String {
    std::string data;
    String() = default;
    String(const char *s): data(s) {}
    String(std::string s): data(std::move(s)) {}
    String(int n): data(std::to_string(n)) {}
    String(unsigned n): data(std::to_string(n)) {}
    String(size_t n): data(std::to_string(n)) {}
    size_t length() const { return data.size(); }
    const char *c_str() const { return data.c_str(); }
    int lastIndexOf(char c) const { auto i = data.rfind(c); return i == std::string::npos ? -1 : int(i); }
    String substring(size_t i) const { return data.substr(i); }
    String &operator+=(const String &other) { data += other.data; return *this; }
    friend String operator+(String a, const String &b) { return a += b; }
    friend bool operator!=(const String &a, const String &b) { return a.data != b.data; }
};
enum UIState { UI_PLAYER, UI_SETTINGS, UI_HELP, UI_WIFI_SCAN, UI_TEXT_INPUT, UI_FOLDER_SELECT, UI_SEARCH };
enum LoopState { NO_LOOP, LOOP_ALL, LOOP_ONE };
UIState currentState = UI_PLAYER;
unsigned readerLibraryGeneration = 0;
int readerMusicVolume = 128;
String g_activePlaylist = "/reader-playlist.txt", g_searchQuery, enteredText;
std::vector<int> g_searchResults;
int g_searchCursor = 0, textInputTarget = 0, numSettings = 19;
struct { String languageCode = "en"; } userSettings;
struct AudioModel {
    int currentIndex = 0, browserIndex = 0;
    bool isPaused = true, isShuffle = false;
    LoopState loopMode = NO_LOOP;
    void *decoder = this;
    String currentTitle, currentArtist;
    std::vector<int> songOffsets{0, 10};
    int pathReads = 0;
    String getSongPath(int index) { ++pathReads; return index == 0 ? "/First full song.mp3" : "/Second full song.mp3"; }
} audioApp;
struct UIManager {
    static inline int settingsCursor = 0, helpScrollOffset = 0, folderCursor = 0, wifiNetworkCount = 0, wifiCursor = 0;
    static inline std::vector<String> folderList;
};
struct I18N {
    static inline std::vector<String> help;
    static void loadHelp(const String &) { help = {"Enter plays or pauses", "Semicolon moves up"}; }
};
String readerSettingsLabel(int index) { return index == 5 ? "Wi-Fi Power: OFF" : "Brightness: 100"; }
struct Keys { bool ctrl = false, enter = false, space = false; };
struct Keyboard {
    bool changed = false;
    char key = 0;
    Keys keys;
    bool isChange() { return changed; }
    bool isPressed() { return changed; }
    bool isKeyPressed(char c) { return key == c; }
    Keys &keysState() { return keys; }
};
struct Speaker {
    int getVolume() { return 128; }
    bool isPlaying(int) { return false; }
    void setChannelVolume(int, int) {}
};
struct Display {
    void fillScreen(int) {}
    void setCursor(int, int) {}
    void println(const char *) {}
};
struct Device { struct Keyboard Keyboard; struct Speaker Speaker; struct Display Display; void update() {} } M5Cardputer;
struct { void println(const char *) {} } Serial;
struct { int status() { return 0; } String SSID(int) { return "Test network"; } } WiFi;
struct { bool restarted = false; void restart() { restarted = true; } } ESP;
constexpr int BLACK = 0;
uint32_t millis() { static uint32_t time = 0; time += 50; return time; }
void delay(int) {}
std::vector<std::string> spoken;
bool speechSay(const char *s) { spoken.emplace_back(s); return true; }
bool speechSayAndWait(const char *s, uint32_t) { return speechSay(s); }
void speechStop() { spoken.emplace_back("[stop]"); }
bool speechRepeat() { spoken.emplace_back("[repeat]"); return true; }
#include "../include/player_access.h"

void poll() { readerPoll(); if (!spoken.empty()) std::cout << spoken.back() << '\n'; }
int main() {
    // Link/exercise fatal handler's referenced commands without entering its infinite wait.
    auto fatal = &readerFatal; (void)fatal;
    readerPlaybackReady = true;
    poll();
    assert(spoken.back() == "Music. First full song.mp3, 1 of 2");
    size_t count = spoken.size();
    for (int i = 0; i < 100; ++i) readerPoll();
    assert(spoken.size() == count && audioApp.pathReads == 1);
    audioApp.browserIndex = 1; poll();
    assert(spoken.back() == "Second full song.mp3, 2 of 2");
    int reads = audioApp.pathReads;
    for (int i = 0; i < 100; ++i) readerPoll();
    assert(audioApp.pathReads == reads); // selected and playing tracks do not thrash SD cache
    audioApp.isPaused = false; poll(); assert(spoken.back().find("Playing") == 0);
    currentState = UI_SETTINGS; UIManager::settingsCursor = 5; poll();
    assert(spoken.back().find("Wi-Fi Power: OFF") != std::string::npos);
    currentState = UI_TEXT_INPUT; textInputTarget = 2; enteredText = "private-password"; poll();
    enteredText += "X"; poll(); assert(spoken.back() == "Character");
    enteredText = "short"; poll(); assert(spoken.back() == "Deleted");
    for (const auto &s : spoken) assert(s.find("private-password") == std::string::npos && s.find("short") == std::string::npos);
    currentState = UI_TEXT_INPUT; textInputTarget = 1; enteredText = "My AP"; poll();
    assert(spoken.back().find("My AP") != std::string::npos);
    currentState = UI_SEARCH; g_searchQuery = "Second"; g_searchResults = {1}; poll();
    assert(spoken.back().find("Second full song.mp3") != std::string::npos);
    currentState = UI_FOLDER_SELECT; UIManager::folderList = {"/Music/Long full folder"}; poll();
    assert(spoken.back().find("Long full folder") != std::string::npos);
    currentState = UI_HELP; poll(); assert(spoken.back().find("Enter plays or pauses") != std::string::npos);
    currentState = UI_WIFI_SCAN; UIManager::wifiNetworkCount = 1; poll();
    assert(spoken.back().find("Test network") != std::string::npos);
    M5Cardputer.Keyboard.changed = true; M5Cardputer.Keyboard.keys.ctrl = true;
    M5Cardputer.Keyboard.key = 'q'; assert(readerHandleKeys()); assert(readerQuitPending && !ESP.restarted);
    M5Cardputer.Keyboard.keys.ctrl = false; M5Cardputer.Keyboard.key = 'x';
    assert(readerHandleKeys()); assert(!readerQuitPending && !ESP.restarted);
    M5Cardputer.Keyboard.keys.ctrl = true; M5Cardputer.Keyboard.key = 'q'; readerHandleKeys();
    M5Cardputer.Keyboard.keys.enter = true; readerHandleKeys(); assert(ESP.restarted);
    fakeHasFactory = false; assert(!readerReturnToLauncher());
    std::cout << "Actual player adapter: seven UI states, privacy, cache and return controls passed\n";
}
