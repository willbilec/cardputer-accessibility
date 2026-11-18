// Actual BackgroundAudio mixer, live English synthesis, ROM WAV and connected Wi-Fi.
#include <Arduino.h>
#include <Wire.h>
#include <Network.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <mbedtls/sha256.h>
#include <BackgroundAudioSpeech.h>
#include <BackgroundAudioWAV.h>
#include <BackgroundAudioMixer.h>
#include <ESP32I2SAudio.h>
#include <libespeak-ng/voice/en.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "coexist_music.h"

static ESP32I2SAudio output(41,43,42);
static BackgroundAudioMixer<512> mixer(output,44100);
static std::atomic<bool> waitingPcm{false};
static std::atomic<uint32_t> requestedAt{0},speechBytes{0};
static std::atomic<uint32_t> firstPcmMs{0};
static std::atomic<bool> firstPcmReport{false};
class SpeechTap:public AudioOutputBase {
public:
 AudioOutputBase *leg=nullptr;
 // Mixer legs publish only complete frames. Two buffers let the priming tail
 // coexist with a complete frame; a single software buffer deadlocks startup.
 bool setBuffers(size_t n,size_t words,int32_t silence=0)override{return leg->setBuffers(std::max(size_t(2),n),words,silence);}
 bool setBitsPerSample(int n)override{return leg->setBitsPerSample(n);}
 bool setFrequency(int n)override{return leg->setFrequency(n);}
 bool setStereo(bool v=true)override{return leg->setStereo(v);}
 bool begin()override{return leg->begin();}
 bool end()override{return leg->end();}
 bool getUnderflow()override{return leg->getUnderflow();}
 void onTransmit(void(*cb)(void*),void*p)override{leg->onTransmit(cb,p);}
 int availableForWrite()override{return leg->availableForWrite();}
 size_t write(uint8_t b)override{return write(&b,1);}
 size_t write(const uint8_t*b,size_t n)override{
  if(waitingPcm.load())for(size_t i=0;i+1<n;i+=2){int16_t s=(int16_t)(b[i]|(b[i+1]<<8));if(s>16||s< -16){
   if(waitingPcm.exchange(false)){firstPcmMs=millis()-requestedAt.load();firstPcmReport=true;}break;}}
  size_t written=leg->write(b,n);speechBytes.fetch_add(written);return written;
 }
};
static SpeechTap tap;
static BackgroundAudioSpeech speech;
static BackgroundAudioWAV music;
static File musicFile;
static bool sdReady=false;
static bool fileSource=false;
static uint32_t musicStart=0,musicUnderflowsStart=0;
static size_t builtinOffset=0;
static const char *testWav="/codex-audio-test.wav";
static bool ready=false,speaking=false,musicEnabled=false,connecting=false;
static uint32_t utteranceStart=0,underflowStart=0,lastStats=0,connectionStart=0,musicLoops=0;
static std::atomic<bool> networkRun{false};
static std::atomic<uint32_t> networkRequests{0},networkBytes{0},networkFailures{0};
static char commandLine[192];
static size_t commandLength=0;
static bool commandOverflow=false;
static bool keyboardReady=false;
static int menuSelection=0;
static const char *menuItems[]={"Speech demonstration", "Toggle music", "Wi-Fi status"};
static bool keyWrite(uint8_t reg,uint8_t value){Wire.beginTransmission(0x34);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;}
static int keyRead(uint8_t reg){Wire.beginTransmission(0x34);Wire.write(reg);if(Wire.endTransmission(false)!=0||Wire.requestFrom(uint8_t(0x34),uint8_t(1))!=1)return -1;return Wire.read();}
static bool keyboardBegin(){
 for(uint8_t reg=0x23;reg<=0x25;++reg)if(!keyWrite(reg,0))return false;
 if(!keyWrite(0x1d,0x7f)||!keyWrite(0x1e,0xff)||!keyWrite(0x1f,0)||!keyWrite(1,3))return false;
 for(int n=0;n<10 && (keyRead(3)&15);++n)keyRead(4);
 return keyWrite(2,0xff);
}
static void memory(const char *stage){Serial.printf("CO_MEM stage=%s free=%u min=%u largest=%u\n",stage,ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));}
static void say(const char*text){
 if(!ready)return;if(speaking)speech.flush();
 requestedAt=millis();utteranceStart=millis();waitingPcm=true;underflowStart=output.underflows();
 Serial.printf("CO_SAY text=%s\n",text);speech.speak(text);speaking=true;
}
static void toggleMusic(){
 musicEnabled=!musicEnabled;music.flush();if(musicFile)musicFile.close();builtinOffset=0;fileSource=false;musicStart=millis();musicUnderflowsStart=music.underflows();
 if(musicEnabled && sdReady && SD.exists(testWav)){
  musicFile=SD.open(testWav,FILE_READ);
  if(!musicFile){musicEnabled=false;Serial.println("CO_MUSIC file_open_failed");return;}
  fileSource=true;Serial.printf("CO_MUSIC source=sd bytes=%u\n",unsigned(musicFile.size()));
 }else if(musicEnabled)Serial.println("CO_MUSIC source=builtin");
 Serial.printf("CO_MUSIC enabled=%d\n",musicEnabled);
}
static void uploadWav(size_t size){
 if(!sdReady || !size || size>10000000 || SD.exists(testWav)){Serial.printf("CO_UPLOAD rejected sd=%d size=%u exists=%d\n",sdReady,unsigned(size),sdReady && SD.exists(testWav));return;}
 File f=SD.open(testWav,FILE_WRITE);if(!f){Serial.println("CO_UPLOAD open_failed");return;}
 Serial.setTxTimeoutMs(100);
 mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);mbedtls_sha256_starts(&hash,0);
 uint8_t buffer[128];size_t total=0;bool ok=true;Serial.println("CO_UPLOAD ready");
 while(total<size){
  size_t want=std::min(sizeof(buffer),size-total),got=0;uint32_t start=millis();
  while(got<want && millis()-start<5000){int n=Serial.read(buffer+got,want-got);if(n>0){got+=n;start=millis();}else delay(1);}
  if(got!=want || f.write(buffer,got)!=got){ok=false;break;}
  mbedtls_sha256_update(&hash,buffer,got);total+=got;Serial.println("CO_UPLOAD ack");
 }
 f.close();uint8_t digest[32];mbedtls_sha256_finish(&hash,digest);mbedtls_sha256_free(&hash);
 if(!ok){SD.remove(testWav);Serial.println("CO_UPLOAD failed");Serial.setTxTimeoutMs(0);return;}
 Serial.printf("CO_UPLOAD done bytes=%u sha256=",unsigned(total));for(auto b:digest)Serial.printf("%02x",b);Serial.println();
 f=SD.open(testWav,FILE_READ);if(!f){Serial.println("CO_UPLOAD verify_open_failed");Serial.setTxTimeoutMs(0);return;}
 mbedtls_sha256_init(&hash);mbedtls_sha256_starts(&hash,0);size_t readTotal=0;
 while(f.available()){int n=f.read(buffer,sizeof(buffer));if(n<=0)break;mbedtls_sha256_update(&hash,buffer,n);readTotal+=n;}
 f.close();uint8_t actual[32];mbedtls_sha256_finish(&hash,actual);mbedtls_sha256_free(&hash);
 Serial.printf("CO_UPLOAD verified=%d bytes=%u\n",readTotal==size && memcmp(digest,actual,32)==0,unsigned(readTotal));
 Serial.setTxTimeoutMs(0);
}
static void menuSpeak(){char text[100];snprintf(text,sizeof(text),"%s. %d of 3",menuItems[menuSelection],menuSelection+1);Serial.printf("CO_MENU selection=%d\n",menuSelection+1);say(text);}
static void keyboardPoll(){
 if(!keyboardReady)return;
 int count=keyRead(3);if(count<0)return;
 for(int n=0;n<(count&15)&&n<10;++n){
  int raw=keyRead(4);if(raw<0 || !(raw&0x80))continue;
  int event=(raw&0x7f)-1;if(event<0)continue;
  int originalRow=event/10,originalCol=event%10;
  int row=(originalCol+4)%4,col=originalRow*2+(originalCol>3);
  if(row==2 && col==11){menuSelection=(menuSelection+2)%3;menuSpeak();}
  else if(row==3 && col==11){menuSelection=(menuSelection+1)%3;menuSpeak();}
  else if(row==3 && col==13)menuSpeak();
  else if(row==2 && col==13){
   if(menuSelection==0)say("Speech demonstration. The quick brown fox jumps over the lazy dog.");
   else if(menuSelection==1){toggleMusic();say(musicEnabled?"Music on":"Music off");}
   else say(WiFi.status()==WL_CONNECTED?"Wi-Fi connected":"Wi-Fi is not connected");
  }
 }
 keyWrite(2,0xff);
}
static void networkWorker(void*){
 for(;;){
  if(networkRun&&WiFi.status()==WL_CONNECTED){
   WiFiClient client;client.setTimeout(2000);uint32_t bytes=0;bool ok=false;
   if(client.connect("example.com",80,2000)){
    client.print("GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n");
    String status=client.readStringUntil('\n');ok=status.startsWith("HTTP/1.")&&status.indexOf(" 200 ")>=0;
    uint32_t start=millis();uint8_t buffer[256];
    while(networkRun&&(client.connected()||client.available())&&millis()-start<5000&&bytes<8192){
     int n=client.read(buffer,sizeof(buffer));if(n>0)bytes+=n;else vTaskDelay(pdMS_TO_TICKS(5));
    }
    Serial.printf("CO_HTTP status=%s bytes=%u\n",status.c_str(),bytes);
   }else Serial.println("CO_HTTP connect_failed");
   client.stop();networkRequests++;networkBytes+=bytes;if(!ok)networkFailures++;
  }
  vTaskDelay(pdMS_TO_TICKS(2000));
 }
}
void setup(){
 Serial.begin(115200);Serial.setTxTimeoutMs(0);setCpuFrequencyMhz(240);delay(200);
 Serial.println("CO_CONFIG engine=BackgroundAudio1.4.4 mixer=44100 preserve_input_rate=1 speech=live music=original_ROM_WAV network=real_station_client");
 pinMode(38,OUTPUT);digitalWrite(38,LOW);pinMode(37,OUTPUT);digitalWrite(37,HIGH);
 Wire.begin(8,9);const uint8_t regs[][2]={{0,0x80},{1,0xb5},{2,0x18},{0x0d,1},{0x12,0},{0x13,0x10},{0x32,0xbf},{0x37,8}};
 for(auto&r:regs){Wire.beginTransmission(0x18);Wire.write(r[0]);Wire.write(r[1]);if(Wire.endTransmission()!=0){Serial.println("CO_FAIL codec");return;}}
 keyboardReady=keyboardBegin();Serial.printf("CO_KEYBOARD ready=%d\n",keyboardReady);
 SPI.begin(40,39,14,12);sdReady=SD.begin(12,SPI,25000000);Serial.printf("CO_SD ready=%d\n",sdReady);
 memory("before_mixer");tap.leg=mixer.add();auto*musicLeg=mixer.add();
 if(!tap.leg||!musicLeg){Serial.println("CO_FAIL legs");return;}
 speech.setDevice(&tap);speech.setVoice(voice_en);music.setDevice(musicLeg);
 bool mixed=mixer.begin(),spoken=speech.begin(),played=music.begin();ready=mixed&&spoken&&played;
 Serial.printf("CO_READY ok=%d mixer=%d speech=%d wav=%d\n",ready,mixed,spoken,played);
 if(!ready)return;speech.setRate(175);speech.setGain(.65f);music.setGain(.18f);
 TaskHandle_t task=nullptr;BaseType_t taskOk=xTaskCreatePinnedToCore(networkWorker,"http-load",6144,nullptr,1,&task,0);
 Serial.printf("CO_NETWORK_TASK ok=%d\n",taskOk==pdPASS);memory("ready");
 Serial.println("Commands: m=music on/off; c=connect demo WiFi; n=HTTP load on/off; 1=short speech; 4=long speech; s=stop speech; x=network and WiFi off.");
 say("Audio test ready. Semicolon and period move through the menu. Enter selects. Space repeats. Speech demonstration. 1 of 3.");
}
void loop(){
 keyboardPoll();
 if(firstPcmReport.exchange(false))Serial.printf("CO_FIRST_PCM ms=%u\n",firstPcmMs.load());
 if(ready&&Serial.available()){
  int input=Serial.read();
  if(commandLength || input=='C' || input=='U' || commandOverflow){
   if(input=='\n'){
    commandLine[commandLength]='\0';
    char *ssid=commandLine+2;
    char *password=strchr(ssid,'\t');
    if(!commandOverflow && commandLength>2 && commandLine[0]=='U' && commandLine[1]=='\t')uploadWav(strtoul(ssid,nullptr,10));
    else if(!commandOverflow && commandLength>2 && commandLine[0]=='C' && commandLine[1]=='\t' && password){
     *password++='\0'; WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.begin(ssid,password);
     connecting=true;connectionStart=millis();Serial.println("CO_WIFI connecting");
    }else Serial.println("CO_WIFI invalid_command");
    memset(commandLine,0,sizeof(commandLine));commandLength=0;commandOverflow=false;
   }else if(input!='\r'){
    if(commandLength+1<sizeof(commandLine))commandLine[commandLength++]=(char)input;
    else commandOverflow=true;
   }
  }else switch(input){
   case '?':Serial.printf("CO_SD ready=%d exists=%d\n",sdReady,sdReady && SD.exists(testWav));break;
   case 'm':toggleMusic();break;
   case 'c':WiFi.mode(WIFI_STA);WiFi.begin("Velxio-GUEST");connecting=true;connectionStart=millis();Serial.println("CO_WIFI connecting");break;
   case 'n':networkRun=!networkRun;Serial.printf("CO_NETWORK enabled=%d\n",networkRun.load());break;
   case '1':say("Music and speech are playing together. Keyboard learning selected.");break;
   case '4':say("File report_2026.txt, folder Research. Battery seventy three percent. Calculation: 127.5 divided by 3 equals 42.5. This is live speech while a separate music stream is decoded and mixed. Wi-Fi is connected only if the serial status confirms an address. The quick brown fox jumps over the lazy dog.");break;
   case 's':speech.flush();waitingPcm=false;speaking=false;Serial.println("CO_STOP speech_only");break;
   case 'x':networkRun=false;WiFi.disconnect();WiFi.mode(WIFI_OFF);connecting=false;Serial.println("CO_WIFI off");memory("wifi_off");break;
  }
 }
 if(ready&&musicEnabled){
  size_t space=music.availableForWrite();
  if(musicFile && space){static uint8_t block[4096];size_t want=std::min(space,sizeof(block));int got=musicFile.read(block,want);if(got>0)music.write(block,got);else {musicFile.close();Serial.println("CO_MUSIC file_eof");}}
  else if(!fileSource && builtinOffset<sizeof(coexistMusic) && space){size_t n=std::min(space,sizeof(coexistMusic)-builtinOffset);builtinOffset+=music.write(coexistMusic+builtinOffset,n);}
  else if(!musicFile && music.done()){
   if(builtinOffset){builtinOffset=0;++musicLoops;}
   else {musicEnabled=false;Serial.printf("CO_MUSIC file_done elapsed_ms=%u input_underflows=%u\n",millis()-musicStart,music.underflows()-musicUnderflowsStart);}
  }
 }
 if(connecting){
  if(WiFi.status()==WL_CONNECTED){connecting=false;Serial.printf("CO_WIFI connected=1 ip=%s\n",WiFi.localIP().toString().c_str());memory("connected");}
  else if(millis()-connectionStart>20000){connecting=false;Serial.printf("CO_WIFI connection_timeout status=%d\n",WiFi.status());}
 }
 if(speaking&&speech.done()){speaking=false;waitingPcm=false;Serial.printf("CO_DONE emulated_ms=%u underruns=%u music=%d wifi=%d\n",millis()-utteranceStart,output.underflows()-underflowStart,musicEnabled,WiFi.status());memory("speech_done");}
 if(ready&&millis()-lastStats>=5000){lastStats=millis();Serial.printf("CO_STATS uptime_ms=%u music_loops=%u speech_bytes=%u http=%u received=%u failures=%u wifi=%d underruns=%u wav_underflows=%u wav_errors=%u wav_frames=%u\n",millis(),musicLoops,speechBytes.load(),networkRequests.load(),networkBytes.load(),networkFailures.load(),WiFi.status(),output.underflows(),music.underflows(),music.errors(),music.frames());memory("periodic");}
 delay(1);
}
