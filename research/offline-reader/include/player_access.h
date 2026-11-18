#pragma once
#include "reader_core.h"
#include "firmware_layout.h"
#include "speech_service.h"

static reader::Observer readerObserver;
static bool readerQuitPending = false;
static bool readerForce = true;
static bool readerPlaybackReady = false;
static int readerInputLength = -1;
static int readerInputTarget = -1;

static void readerNotice(const char *message) {
    speechSay(message);
    // No credentials, filenames or synthesized text are written to serial.
    Serial.println("READER event");
}
static String readerSongName(int index) {
    struct Entry { int index = -1; String name; };
    static Entry entries[2];
    static String cachedPlaylist;
    static unsigned generation = ~0u;
    static unsigned next = 0;
    if (index < 0 || index >= (int)audioApp.songOffsets.size()) return "No tracks";
    if (cachedPlaylist != g_activePlaylist || generation != readerLibraryGeneration) {
        cachedPlaylist = g_activePlaylist;
        generation = readerLibraryGeneration;
        for (auto &entry : entries) entry.index = -1;
    }
    for (auto &entry : entries) if (entry.index == index) return entry.name;
    auto &entry = entries[next++ % 2];
    entry.index = index;
    entry.name = audioApp.getSongPath(index);
    int slash = entry.name.lastIndexOf('/');
    if (slash >= 0) entry.name = entry.name.substring(slash + 1);
    return entry.name;
}
static String readerPosition(int index, int count) {
    return String(", ") + String(index + 1) + " of " + String(count);
}
static String readerTransport() {
    String state = !audioApp.decoder ? "Stopped" : audioApp.isPaused ? "Paused" : "Playing";
    state += ". " + (audioApp.currentTitle.length() ? audioApp.currentTitle : readerSongName(audioApp.currentIndex));
    if (audioApp.currentArtist.length()) state += ", " + audioApp.currentArtist;
    state += audioApp.isShuffle ? ". Shuffle on" : ". Shuffle off";
    state += ". Loop "; state += audioApp.loopMode == NO_LOOP ? "off" : audioApp.loopMode == LOOP_ALL ? "all" : "one";
    state += ". Music volume " + String(readerMusicVolume);
    return state;
}
static reader::Snapshot readerSnapshot() {
    String screen, focus;
    switch (currentState) {
    case UI_PLAYER:
        screen = "Music";
        focus = audioApp.songOffsets.empty() ? "No music found. Escape opens settings" :
            readerSongName(audioApp.browserIndex) + readerPosition(audioApp.browserIndex, audioApp.songOffsets.size());
        break;
    case UI_SETTINGS:
        screen = "Settings";
#ifdef READER_HEADLESS
        focus = readerSettingsLabel(UIManager::settingsCursor) + readerPosition(headlessSettingPosition(UIManager::settingsCursor), headlessSettingCount);
#else
        focus = readerSettingsLabel(UIManager::settingsCursor) + readerPosition(UIManager::settingsCursor, numSettings);
#endif
        break;
    case UI_HELP:
        screen = "Help";
        if (I18N::help.empty()) I18N::loadHelp(userSettings.languageCode);
        if (UIManager::helpScrollOffset < (int)I18N::help.size()) focus = I18N::help[UIManager::helpScrollOffset];
        break;
    case UI_FOLDER_SELECT:
        screen = "Folders";
        if (UIManager::folderList.empty()) focus = "No folders found";
        else if (UIManager::folderCursor < (int)UIManager::folderList.size()) {
            focus = UIManager::folderList[UIManager::folderCursor];
            if (!focus.length()) focus = "All Music";
            focus += readerPosition(UIManager::folderCursor, UIManager::folderList.size());
        }
        break;
    case UI_SEARCH:
        screen = "Search";
        focus = "Query " + g_searchQuery + ". " + String(g_searchResults.size()) + " results";
        if (!g_searchResults.empty() && g_searchCursor < (int)g_searchResults.size())
            focus += ". " + readerSongName(g_searchResults[g_searchCursor]) + readerPosition(g_searchCursor, g_searchResults.size());
        break;
    case UI_WIFI_SCAN:
        screen = "Wi-Fi networks";
        focus = UIManager::wifiNetworkCount ? WiFi.SSID(UIManager::wifiCursor) +
            readerPosition(UIManager::wifiCursor, UIManager::wifiNetworkCount) : "No networks found";
        break;
    case UI_TEXT_INPUT:
        screen = textInputTarget == 1 ? "Access point name" : "Password. Input protected";
        // Never copy a password into the reader model, history, or serial log.
        focus = textInputTarget == 1 ? enteredText : "Enter to save. Escape to cancel";
        break;
    }
    String status = readerTransport();
    status += ". Wi-Fi " + String((int)WiFi.status());
    return {screen.c_str(), focus.c_str(), status.c_str()};
}
static void readerPoll() {
    M5Cardputer.Speaker.setChannelVolume(0, M5Cardputer.Speaker.isPlaying(1) ? readerMusicVolume / 4 : readerMusicVolume);
    if (!readerPlaybackReady) return;
    static uint32_t lastPoll = 0;
    if (!readerForce && millis() - lastPoll < 50) return;
    lastPoll = millis();
    auto snapshot = readerSnapshot();
    auto utterance = readerObserver.update(snapshot, readerForce);
    readerForce = false;
    if (currentState == UI_TEXT_INPUT && textInputTarget != 1) {
        int length = enteredText.length();
        if (textInputTarget == readerInputTarget && readerInputLength >= 0 && length != readerInputLength)
            utterance = length < readerInputLength ? "Deleted" : "Character";
        readerInputLength = length;
        readerInputTarget = textInputTarget;
    } else { readerInputLength = -1; readerInputTarget = -1; }
    if (!utterance.empty() && !readerQuitPending) speechSay(utterance.c_str());
}
static bool readerHandleKeys() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return false;
    const auto &keys = M5Cardputer.Keyboard.keysState();
    if (readerQuitPending) {
        if (keys.enter) {
            speechSayAndWait("Returning to launcher", 2500);
            if (!readerReturnToLauncher()) readerNotice("Paired launcher unavailable. Staying in player");
        } else readerNotice("Return cancelled");
        readerQuitPending = false;
        return true;
    }
    if (!keys.ctrl) return false;
    if (keys.space) speechStop();
    else if (M5Cardputer.Keyboard.isKeyPressed('r')) speechRepeat();
    else if (M5Cardputer.Keyboard.isKeyPressed('f')) readerForce = true;
    else if (M5Cardputer.Keyboard.isKeyPressed('t')) speechSay(readerTransport().c_str());
    else if (M5Cardputer.Keyboard.isKeyPressed('h')) readerNotice("Reader: Control F focus, T playback, R repeat, Space stop, Q launcher. Up and down are semicolon and period");
    else if (M5Cardputer.Keyboard.isKeyPressed('q')) {
        readerQuitPending = true;
        readerNotice("Return to test launcher? Enter confirms. Any other key cancels");
    }
    return true;
}
static void readerFatal(const char *message) {
    readerNotice(message);
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println(message);
    while (true) {
        M5Cardputer.update();
        readerHandleKeys();
        delay(5);
    }
}
