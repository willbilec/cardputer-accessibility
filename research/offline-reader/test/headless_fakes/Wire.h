#pragma once
#include "Arduino.h"
struct FakeWire {
    uint8_t address=0, reg=0;
    std::vector<uint8_t> outgoing;
    std::map<unsigned,uint8_t> registers;
    std::deque<uint8_t> events;
    int sda=-1,scl=-1;
    void begin(int a, int b) { sda=a; scl=b; }
    void beginTransmission(uint8_t a) { address=a; outgoing.clear(); }
    void write(uint8_t n) { outgoing.push_back(n); }
    int endTransmission(bool = true) {
        if (!outgoing.empty()) reg=outgoing[0];
        if (outgoing.size()==2) registers[(unsigned(address)<<8)|reg]=outgoing[1];
        return 0;
    }
    int requestFrom(uint8_t a, uint8_t) { address=a; return 1; }
    int read() {
        if (address==0x34 && reg==3) return events.size();
        if (address==0x34 && reg==4) {
            if(events.empty()) return 0;
            int event=events.front(); events.pop_front(); return event;
        }
        return registers[(unsigned(address)<<8)|reg];
    }
} inline Wire;
