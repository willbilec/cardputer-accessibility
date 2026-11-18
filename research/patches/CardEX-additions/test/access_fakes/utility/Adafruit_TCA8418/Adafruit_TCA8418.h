#pragma once
#include <cstdint>
#include <deque>
struct FakeSerial {template<class...T>void printf(const char*,T...) {}};
inline FakeSerial Serial;
class Adafruit_TCA8418 {
public:
 inline static std::deque<uint8_t> events;
 bool begin(){return true;}
 void matrix(int,int){} void flush(){events.clear();} void enableInterrupts(){}
 uint8_t available(){return events.size();}
 uint8_t getEvent(){auto n=events.front();events.pop_front();return n;}
 void writeRegister8(uint8_t,uint8_t){}
};
