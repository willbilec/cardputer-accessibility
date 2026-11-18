#pragma once
#include <cstdint>
struct KeyReport { uint8_t modifiers=0,reserved=0,keys[6]{}; };
struct USBHIDKeyboard {
 KeyReport last{};unsigned reports=0;
 void begin(){}
 bool sendReport(KeyReport *report){last=*report;++reports;return true;}
 void releaseAll(){last={};++reports;}
};
