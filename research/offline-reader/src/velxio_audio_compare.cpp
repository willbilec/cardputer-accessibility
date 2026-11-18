// Diagnostic-only comparison: identical audio path, prerecorded vs live synthesis.
#include <Arduino.h>
#include "headless_board.h"
#include "speech_service.h"
#include "audio_compare_fixture.h"
static bool ready;
static bool drain(uint32_t limit=30000){uint32_t start=millis();while(M5Cardputer.Speaker.isPlaying(1)){if(millis()-start>limit)return false;delay(1);}return true;}
static void prerecorded(unsigned chunk,bool waitEvery){
 uint32_t start=millis();unsigned blocks=0;
 Serial.printf("COMPARE_BEGIN chunk=%u wait_every=%d samples=%u expected_ms=%u\n",chunk,waitEvery,unsigned(kAudioSamples),unsigned(kAudioSamples*1000/22050));
 for(size_t offset=0;offset<kAudioSamples;offset+=chunk){
  size_t n=std::min(size_t(chunk),kAudioSamples-offset);
  if(!M5Cardputer.Speaker.playRaw(kAudio+offset,n,22050,false,1,1) || (waitEvery&&!drain())){Serial.println("COMPARE_FAIL output");return;}
  ++blocks;
 }
 bool ok=drain();
 Serial.printf("COMPARE_END ok=%d blocks=%u emulated_ms=%lu heap=%u\n",ok,blocks,(unsigned long)(millis()-start),ESP.getFreeHeap());
}
void setup(){
 Serial.begin(115200);ready=M5Cardputer.begin()&&speechServiceBegin();M5Cardputer.Speaker.setVolume(128);
 Serial.printf("COMPARE_READY ok=%d heap=%u\n",ready,ESP.getFreeHeap());
 Serial.println("Serial commands: a=prerecorded 2048/drain; b=prerecorded 256/drain; d=prerecorded 256/queued; c=live Keyboard ready. No automatic sound.");
}
void loop(){
 if(ready&&Serial.available()){
  char command=Serial.read();
  if(command=='a')prerecorded(2048,true);
  else if(command=='b')prerecorded(256,true);
  else if(command=='d')prerecorded(256,false);
  else if(command=='c'){
   uint32_t start=millis();Serial.println("COMPARE_LIVE_BEGIN Keyboard ready");
   bool ok=speechSayAndWait("Keyboard ready.",30000);
   Serial.printf("COMPARE_LIVE_END ok=%d emulated_ms=%lu\n",ok,(unsigned long)(millis()-start));
  }
 }
 delay(5);
}
