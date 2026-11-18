#pragma once
#include <cstdint>
#include <cstring>
namespace recording {
inline void put16(uint8_t *p,uint16_t n){p[0]=uint8_t(n);p[1]=uint8_t(n>>8);}
inline void put32(uint8_t *p,uint32_t n){put16(p,uint16_t(n));put16(p+2,uint16_t(n>>16));}
inline void wavHeader(uint8_t *h,uint32_t dataBytes,uint32_t sampleRate=44100){
 std::memcpy(h,"RIFF",4);put32(h+4,dataBytes+36);std::memcpy(h+8,"WAVEfmt ",8);
 put32(h+16,16);put16(h+20,1);put16(h+22,2);put32(h+24,sampleRate);
 put32(h+28,sampleRate*4);put16(h+32,4);put16(h+34,16);
 std::memcpy(h+36,"data",4);put32(h+40,dataBytes);
}
}
