#define ESP32 1
#include "../generated/backgroundaudio-trial/src/BackgroundAudioMixer.h"
#include <iostream>
class Sink:public AudioOutputBase {
public:
 void(*cb)(void*)=nullptr;void*ctx=nullptr;int available=0;uint32_t captured[4]={};
 bool setBuffers(size_t,size_t,int32_t=0)override{return true;}
 bool setBitsPerSample(int)override{return true;}
 bool setFrequency(int)override{return true;}
 bool setStereo(bool=true)override{return true;}
 bool begin()override{return true;}
 bool end()override{return true;}
 bool getUnderflow()override{return false;}
 void onTransmit(void(*f)(void*),void*p)override{cb=f;ctx=p;}
 int availableForWrite()override{return available;}
 size_t write(uint8_t)override{return 0;}
 size_t write(const uint8_t*p,size_t n)override{assert(n==sizeof(captured));memcpy(captured,p,n);available-=n;return n;}
};
int main(){
 BackgroundAudioMixerInput input(44100,512);
 assert(input.setBuffers(1,1324));
 assert(input.begin());
 assert(input.availableForWrite()==1324*4);
 uint32_t partial[16]={};
 assert(input.write(reinterpret_cast<uint8_t*>(partial),sizeof(partial))==sizeof(partial));
 assert(input.availableForWrite()==(1324-16)*4);
 uint32_t rest[1324]={};
 assert(input.write(reinterpret_cast<uint8_t*>(rest),sizeof(rest))==(1324-16)*4);
 assert(input.availableForWrite()==0);
 assert(input.write(reinterpret_cast<uint8_t*>(partial),sizeof(partial))==0);
 Sink sink;BackgroundAudioMixer<4> mixer(sink,44100);auto*leg=mixer.add();auto*effect=mixer.add();
 leg->setBuffers(2,4);leg->setFrequency(22050);assert(leg->begin());
 effect->setBuffers(1,4);assert(effect->begin());assert(mixer.begin());
 uint32_t ramp[]={0,0x00010001,0x00020002,0x00030003};
 assert(leg->write(reinterpret_cast<uint8_t*>(ramp),sizeof(ramp))==sizeof(ramp));
 sink.available=16;sink.cb(sink.ctx);
 // A 22,050-Hz source at 44,100-Hz output must repeat each sample twice.
 assert(sink.captured[0]==0&&sink.captured[1]==0);
 assert(sink.captured[2]==0x00010001&&sink.captured[3]==0x00010001);
 leg->discardBufferedAudio();
 uint32_t cue[]={0x00090009,0x00090009,0x00090009,0x00090009};
 assert(effect->write(reinterpret_cast<uint8_t*>(cue),sizeof(cue))==sizeof(cue));
 sink.available=16;sink.cb(sink.ctx);
 for (auto sample : sink.captured) assert(sample == 0x00090009);
 assert(leg->availableForWrite()==2*4*4);
 delete leg;delete effect;
 std::cout<<"Actual mixer byte capacity, partial write and full-buffer bounds passed\n";
 std::cout<<"Configured source rate survives begin; 2x resampling preserves pitch\n";
}
