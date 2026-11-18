#include "wav_header.h"
#include "recording_gain.h"
#include <cassert>
#include <cstring>
int main(){
 uint8_t h[44]={};recording::wavHeader(h,176400);
 assert(std::memcmp(h,"RIFF",4)==0&&std::memcmp(h+8,"WAVEfmt ",8)==0);
 assert(std::memcmp(h+36,"data",4)==0);
 assert(h[4]==0x34&&h[5]==0xb1&&h[6]==0x02&&h[7]==0);
 assert(h[22]==2&&h[24]==0x44&&h[25]==0xac&&h[28]==0x10&&h[29]==0xb1);
 assert(h[32]==4&&h[34]==16&&h[40]==0x10&&h[41]==0xb1&&h[42]==0x02);
 const float defaultGain=recording::captureGain(0);
 assert(defaultGain>3.9f&&defaultGain<4.1f);
 assert(recording::applyCaptureGain(1000,defaultGain)>3900);
 assert(recording::applyCaptureGain(10000,defaultGain)==32767);
 assert(recording::applyCaptureGain(-10000,defaultGain)==-32768);
 assert(recording::applyCaptureGain(1000,recording::captureGain(-12))==1000);
 assert(recording::applyCaptureGain(0,recording::captureGain(24))==0);
}
