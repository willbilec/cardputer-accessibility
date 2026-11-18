#include <cassert>
#include <iostream>
#include "../include/reader_core.h"
#include "../include/headless_settings.h"

int main() {
    assert(headlessNextSetting(4, -1) == 15 && headlessNextSetting(15, 1) == 4);
    for (int id : headlessSettings) {
        assert(id != 0 && id != 1 && id != 3 && id != 8 && id != 9 && id != 16 && id != 18);
        assert(headlessNextSetting(headlessNextSetting(id, 1), -1) == id);
    }
    reader::Observer observer;
    reader::Snapshot state{"Music", "First song, 1 of 2", "Paused"};
    assert(observer.update(state) == "Music. First song, 1 of 2");
    for (int i = 0; i < 1000; ++i) assert(observer.update(state).empty());
    state.focus = "Second song, 2 of 2";
    assert(observer.update(state) == state.focus);
    state.status = "Playing";
    assert(observer.update(state) == "Playing");
    assert(observer.update(state, true) == "Music. Second song, 2 of 2");
    state.screen = "Settings";
    state.focus = "Wi-Fi power: off";
    assert(observer.update(state) == "Settings. Wi-Fi power: off");
    state.focus = "Wi-Fi power: on";
    assert(observer.update(state) == state.focus);
    state = {"Password. Input protected", "Enter to save. Escape to cancel", ""};
    assert(observer.update(state).find("Input protected") != std::string::npos);
    assert(observer.update(state).empty());
    assert(reader::validLayout(0x10000, 0x280000, 0x290000, 0x560000, 4000000));
    assert(!reader::validLayout(0x10000, 0x300000, 0x310000, 0x560000, 4000000));
    assert(!reader::validLayout(0x10000, 0x280000, 0x290000, 0x560000, 0));
    assert(!reader::validLayout(0x10000, 0x280000, 0x290000, 0x560000, 0x560001));
    assert(!reader::validLayout(0x10000, 0x280000, 0x10000, 0x560000, 2000000));
    std::cout << "Reader change detection and exact-layout guards passed\n";
}
