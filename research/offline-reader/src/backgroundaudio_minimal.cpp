// Isolated English eSpeak-NG/BackgroundAudio trial. Optional Wi-Fi scan load.
#include <Arduino.h>
#include <Wire.h>
#include <Network.h>
#include <WiFi.h>
#include <BackgroundAudioSpeech.h>
#include <ESP32I2SAudio.h>
#include <libespeak-ng/voice/en.h>
#include <esp_heap_caps.h>
#include <atomic>

static std::atomic<bool> waitingForPcm{false};
static std::atomic<uint32_t> requestedAt{0};
class MeasuredOutput:public ESP32I2SAudio {
public:
 MeasuredOutput():ESP32I2SAudio(41,43,42){}
 bool initialized=false;
 bool begin()override{initialized=ESP32I2SAudio::begin();Serial.printf("BG_I2S_BEGIN ok=%d\n",initialized);return initialized;}
 using ESP32I2SAudio::write;
 size_t write(const uint8_t*bytes,size_t n)override{
  if(waitingForPcm.load()){
   const int16_t *pcm=reinterpret_cast<const int16_t*>(bytes);
   for(size_t i=0;i<n/2;++i)if(pcm[i]>16||pcm[i]<-16){
    if(waitingForPcm.exchange(false))Serial.printf("BG_FIRST_NONZERO_PCM emulated_ms=%lu\n",(unsigned long)(millis()-requestedAt.load()));
    break;
   }
  }
  return ESP32I2SAudio::write(bytes,n);
 }
};
static MeasuredOutput output;
static BackgroundAudioSpeech speech(output);
static bool ready=false,active=false;
static uint32_t started=0,underflowsAtStart=0;
static uint8_t burstRemaining=0;
static uint32_t nextBurst=0;
static bool scanPending=false;
static void memory(const char*stage){Serial.printf("BG_MEM stage=%s free=%u minimum=%u largest=%u stack=%u\n",stage,ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(nullptr));}
static bool codecBegin(){
 const uint8_t settings[][2]={{0,0x80},{1,0xb5},{2,0x18},{0x0d,1},{0x12,0},{0x13,0x10},{0x32,0xbf},{0x37,8}};
 for(auto &r:settings){Wire.beginTransmission(0x18);Wire.write(r[0]);Wire.write(r[1]);if(Wire.endTransmission()!=0)return false;}
 return true;
}
static void say(const char *text){
 if(!ready)return;
 if(active)speech.flush();
 started=millis();requestedAt=started;waitingForPcm=true;underflowsAtStart=output.underflows();
 Serial.printf("BG_SAY text=%s\n",text);speech.speak(text);active=true;
}
void setup(){
 Serial.begin(115200);setCpuFrequencyMhz(240);delay(200);
 Serial.printf("BG_CONFIG version=1.4.4 buffers=1 phonemes=250 optimization=O3 cpu=%u psram=%u\n",getCpuFrequencyMhz(),ESP.getPsramSize());
 pinMode(38,OUTPUT);digitalWrite(38,LOW);pinMode(37,OUTPUT);digitalWrite(37,HIGH);
 Wire.begin(8,9);
 if(!codecBegin()){Serial.println("BG_FAIL codec_init");return;}
 memory("before_speech");speech.setVoice(voice_en);
 bool initialized=speech.begin();ready=initialized&&output.initialized;
 Serial.printf("BG_READY ok=%d\n",ready);if(!ready)return;
 speech.setRate(175);speech.setGain(.5f);memory("ready");
 Serial.println("Commands: 1=ready; 2=menu; 3=keys; 4=complex text; 5=interrupt burst; w=WiFi scan; x=WiFi off; s=stop.");
 delay(500);say("Keyboard ready.");
}
void loop(){
 if(ready&&Serial.available()){
  char c=Serial.read();
  if(c=='1')say("Keyboard ready.");
  else if(c=='2')say("Accessible programs. Media player. Keyboard and mouse. Keyboard learning.");
  else if(c=='3')say("Letter bee. Enter selects. Backspace deletes.");
  else if(c=='4')say("File report_2026.txt, folder Research. Battery: seventy three percent. Timer: one hour, twenty three minutes, forty five seconds. Calculation: 127.5 divided by 3 equals 42.5. Warning! Wi-Fi disconnected. Press Enter to retry, or Escape to cancel. The quick brown fox jumps over the lazy dog.");
  else if(c=='5'){burstRemaining=8;nextBurst=millis();Serial.println("BG_BURST begin count=8 interval_ms=300");}
  else if(c=='w'){
   WiFi.mode(WIFI_STA);WiFi.disconnect();
   int result=WiFi.scanNetworks(true);scanPending=result==WIFI_SCAN_RUNNING;
   Serial.printf("BG_WIFI mode=station scan_result=%d\n",result);memory("wifi_on");
  }
  else if(c=='x'){WiFi.scanDelete();WiFi.mode(WIFI_OFF);scanPending=false;Serial.println("BG_WIFI mode=off");memory("wifi_off");}
  else if(c=='s'){burstRemaining=0;speech.flush();waitingForPcm=false;active=false;Serial.println("BG_STOP");}
 }
 if(burstRemaining&&(int32_t)(millis()-nextBurst)>=0){
  --burstRemaining;nextBurst=millis()+300;
  say(burstRemaining?"Selection changed. This sentence should be interrupted.":"Final selection. Keyboard learning. Interruption test complete.");
 }
 if(scanPending){int n=WiFi.scanComplete();if(n!=WIFI_SCAN_RUNNING){scanPending=false;Serial.printf("BG_WIFI scan_complete=%d\n",n);WiFi.scanDelete();memory("scan_done");}}
 if(active&&speech.done()){
  Serial.printf("BG_DONE emulated_ms=%lu underflows=%lu first_pcm_seen=%d frames=%lu\n",(unsigned long)(millis()-started),(unsigned long)(output.underflows()-underflowsAtStart),!waitingForPcm.load(),(unsigned long)output.frames());
  memory("done");active=false;waitingForPcm=false;
 }
 delay(5);
}
