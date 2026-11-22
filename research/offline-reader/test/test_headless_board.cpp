#include <cassert>
#include <iostream>
#include "global_shortcuts.h"
#include "../src/headless_board.cpp"

static void queueEvent(int row, int col, bool down) {
    int raw = (col / 2) * 10 + row + ((col % 2) ? 4 : 0) + 1;
    Wire.events.push_back(raw | (down ? 0x80 : 0));
}
static void event(int row, int col, bool down) {
    queueEvent(row, col, down);
    M5Cardputer.Keyboard.update();
}
int main() {
    assert(M5Cardputer.begin());
    assert(Wire.sda==8 && Wire.scl==9 && fakePins[38]==LOW && fakePins[37]==HIGH);
    assert(fakeI2SConfig.gpio_cfg.bclk==41 && fakeI2SConfig.gpio_cfg.ws==43 && fakeI2SConfig.gpio_cfg.dout==42);
    assert(fakeI2SConfig.clk_cfg.rate==44100 && fakeI2SConfig.slot_cfg.slot_mask==3);
    assert(fakeI2SChannel.auto_clear); // DMA must become silence during idle/underrun.
    assert(Wire.registers[(0x18<<8)|1]==0xb5 && fakeMixerStack==12288);
    for(int row=0; row<4; ++row) for(int col=0; col<14; ++col) if(plain[row][col]>=32) {
        event(row,col,true);
        assert(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed());
        assert(M5Cardputer.Keyboard.isKeyPressed(plain[row][col]));
        assert(M5Cardputer.Keyboard.keysState().word.size()==1);
        M5Cardputer.Keyboard.update(); assert(!M5Cardputer.Keyboard.isChange());
        event(row,col,false); assert(!M5Cardputer.Keyboard.isPressed());
    }
    event(3,0,true); event(1,4,true);
    assert(M5Cardputer.Keyboard.keysState().ctrl && M5Cardputer.Keyboard.isKeyPressed('r'));
    event(1,4,false); event(3,0,false);
    event(2,1,true); event(1,1,true); assert(M5Cardputer.Keyboard.isKeyPressed('Q'));
    assert(M5Cardputer.Keyboard.keysState().word.size()==1 && M5Cardputer.Keyboard.keysState().word[0]=='Q');
    assert(M5Cardputer.Keyboard.keysState().modifiers & 2);
    event(1,1,false); event(2,1,false);
    event(3,2,true); event(1,1,true); assert(M5Cardputer.Keyboard.quitRequested());
    M5Cardputer.Keyboard.update(); assert(!M5Cardputer.Keyboard.quitRequested());
    event(1,1,false); event(3,2,false);
    event(1,1,true); event(3,2,true); assert(M5Cardputer.Keyboard.quitRequested());
    event(3,2,false); event(1,1,false);
    // All four transitions can arrive in one TCA8418 FIFO read.
    Wire.events.push_back(0x80 | 14); // Alt
    Wire.events.push_back(0x80 | 6);  // Q
    Wire.events.push_back(6);
    Wire.events.push_back(14);
    M5Cardputer.Keyboard.update();
    assert(M5Cardputer.Keyboard.quitRequested() && !M5Cardputer.Keyboard.isPressed());
    // Preserve the modifier on the letter event even if the chord has ended
    // by the time the launcher gets the full batch of events.
    for (int modifier : {1, 2}) {
        queueEvent(3, modifier, true);
        queueEvent(1, 5, true);
        queueEvent(1, 5, false);
        queueEvent(3, modifier, false);
        M5Cardputer.Keyboard.update();
        const auto &events = M5Cardputer.Keyboard.events();
        assert(events.size()==4 && !M5Cardputer.Keyboard.isPressed());
        const auto &letter=events[1];
        const auto &m=letter.heldModifiers;
        using globalShortcuts::Command;
        assert(globalShortcuts::route({letter.row,letter.col,letter.down,m.opt,m.fn,m.alt,m.ctrl,m.shift})==
               (modifier==2 ? Command::Time : Command::None));
    }
    event(2,13,true); assert(M5Cardputer.Keyboard.keysState().enter); event(2,13,false);
    int16_t pcm[16]; std::fill_n(pcm,16,int16_t(123));
    assert(M5Cardputer.Speaker.playRaw(pcm,16,22050,false,1,1));
    pcm[0]=0; Packet packet{}; memcpy(&packet,queues[1]->items.front().data(),sizeof(packet));
    assert(packet.pcm[0]==123 && packet.count==16 && M5Cardputer.Speaker.isPlaying(1));
    assert(M5Cardputer.Speaker.playRaw(pcm,16,22050,false,1,1));
    assert(!M5Cardputer.Speaker.playRaw(pcm,16,22050,false,1,1));
    assert(pending[1]==2);
    assert(!M5Cardputer.Speaker.playRaw(pcm,16,0,false,1,0));
    assert(!M5Cardputer.Speaker.playRaw(pcm,15,44100,true,1,0));
    M5Cardputer.Speaker.stop(1); assert(!M5Cardputer.Speaker.isPlaying(1) && queues[1]->items.empty());
    fakePins[0]=LOW; fakeTime=100; M5Cardputer.BtnA.update();
    fakePins[0]=HIGH; fakeTime=150; M5Cardputer.BtnA.update();
    fakeTime=600; M5Cardputer.BtnA.update();
    assert(M5Cardputer.BtnA.wasDecideClickCount() && M5Cardputer.BtnA.getClickCount()==1);
    assert(!M5Cardputer.BtnA.wasDecideClickCount());
    std::cout << "Actual headless board glue: key remap/modifiers, codec configuration, bounded audio queue, stop and button passed with fake hardware\n";
}
