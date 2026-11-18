// Work in progress. This image is not yet suitable for installation.
#include <Arduino.h>
#include <Wire.h>
#include <Network.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <BackgroundAudioSpeech.h>
#include <BackgroundAudioWAV.h>
#include <BackgroundAudioMP3.h>
#include <BackgroundAudioMixer.h>
#include "duplex_audio.h"
#include "wav_header.h"
#include <libespeak-ng/voice/en.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <esp_vfs_fat.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <new>
#include <memory>
#include <vector>
#include <algorithm>
#include "headless_board.h"
#include "key_learning.h"
#include "global_shortcuts.h"
#include "recording_gain.h"
#include "unified_hid.h"
#include "usb_sd.h"
#include "settings_store.h"
#include "synth.h"
#ifdef UNIFIED_PLAYPALACE
#include "playpalace.h"
#include "playpalace_sound.h"
#endif

namespace {
fs::SDFS &card=SD;
DuplexAudio output(41,43,42);
// Four 128-frame DMA blocks bound queued audio to about 12 ms at 44.1 kHz.
BackgroundAudioMixer<128> mixer(output,44100);
BackgroundAudioSpeech reader;
BackgroundAudioWAV *player=nullptr;
BackgroundAudioMP3 *mp3=nullptr;
SemaphoreHandle_t speechMutex=nullptr;
struct SpeechLock {
 SpeechLock(){if(speechMutex)xSemaphoreTake(speechMutex,portMAX_DELAY);}
 ~SpeechLock(){if(speechMutex)xSemaphoreGive(speechMutex);}
};
class MixerLeg : public AudioOutputBase {
public:
 BackgroundAudioMixerInput *leg=nullptr;
 void discard(){if(leg)leg->discardBufferedAudio();}
 bool setBuffers(size_t,size_t w,int32_t s=0) override {return started||leg->setBuffers(2,w,s);}
 bool setBitsPerSample(int n) override{return leg->setBitsPerSample(n);}
 bool setFrequency(int n) override{return leg->setFrequency(n);}
 bool setStereo(bool v=true) override{return leg->setStereo(v);}
 bool begin() override{return started||(started=leg->begin());}
 bool end() override{callback=nullptr;context=nullptr;return true;}
 bool getUnderflow() override{return leg->getUnderflow();}
 void onTransmit(void(*cb)(void*),void*p) override{callback=cb;context=p;leg->onTransmit(dispatch,this);}
 int availableForWrite() override{return leg->availableForWrite();}
 size_t write(uint8_t b) override{return leg->write(&b,1);}
 size_t write(const uint8_t*b,size_t n) override{return leg->write(b,n);}
 void cue(bool high){tonePeriod=high?22:44;toneLeft=440;}
private:
 bool started=false;
 unsigned tonePeriod=0,toneLeft=0;
 void (*callback)(void*)=nullptr;void *context=nullptr;
 static void dispatch(void *p){
  auto *self=static_cast<MixerLeg*>(p);SpeechLock lock;
  if(self->toneLeft){
   while(self->toneLeft&&self->availableForWrite()>=4){
    unsigned i=440-self->toneLeft--;
    int ramp=std::min({i,439-i,44u});
    int16_t sample=(i%self->tonePeriod<self->tonePeriod/2?1:-1)*ramp*35;
    int16_t frame[]={sample,sample};
    self->write(reinterpret_cast<uint8_t *>(frame),sizeof(frame));
   }
  }
  if(self->callback)self->callback(self->context);
 }
} speechLeg,wavLeg,mp3Leg;
MixerLeg synthLeg;
Synth synth;
constexpr const char *synthPresets[]={"Piano","Chiptune","Drums","Bass","Glass bells","Grand piano","NES chip","Analog brass","Electric piano","Drawbar organ"};
constexpr const char *synthHelp="Alt 1 piano, 2 chiptune, 3 drums, 4 bass, 5 glass bells, 6 grand piano, 7 NES chip, 8 analog brass, 9 electric piano, 0 drawbar organ. Alt R reads the preset. Hold Control for sustain, Shift flanger, Function tremolo, or Option arpeggio. Alt brackets adjust quiet sound boost. Alt Q goes home.";
int synthBoost=2;
bool synthSustain=false,synthFlange=false,synthTremolo=false,synthArpeggio=false;
float synthGain=0;
int synthRenderBoost=2;
void resetSynth(Synth::Preset preset){
 SpeechLock lock;
 synth.reset();
 synth.preset=preset;
 if(synthLeg.leg)synthLeg.discard();
}
void renderSynth(void *){
 // MixerLeg dispatch holds the audio lock. The mixer task owns the PCM queue.
 if(!synth.active())return;
 int16_t pcm[Synth::blockFrames*2];
 if(synthLeg.availableForWrite()<int(sizeof(pcm)))return;
 synth.render(pcm,synthGain,synthSustain,synthFlange,synthTremolo,synthRenderBoost,synthArpeggio);
 synthLeg.write(reinterpret_cast<const uint8_t*>(pcm),sizeof(pcm));
}
#ifdef UNIFIED_PLAYPALACE
MixerLeg effectLeg;
void discardPlayPalaceAudio(){
 SpeechLock lock;
 effectLeg.discard();
}
size_t playPalaceAudio(unsigned rate,const uint8_t* pcm,size_t bytes){
 SpeechLock lock;
 if(!effectLeg.leg||!effectLeg.begin()||effectLeg.leg->discardPending())return 0;
 if(!effectLeg.setFrequency(rate))return 0;
 const size_t available=std::max(0,effectLeg.availableForWrite());
 return effectLeg.write(pcm,std::min(bytes,available)&~size_t(3));
}
#endif
SettingsStore settings(card);
enum class Page { Home, Learn, Record, Files, FileActions, FileName, ConfirmDelete, Copying, Play, Settings, WifiScan, WifiList, Password, TimeZone, ManualTime, TimePicker, SpeechRate, SpeechVolume, MicGain, MediaVolume, Hid, UsbSDMode, UsbSD, PlayPalace, SettingsCategories, Synth };
Page page=Page::Home;
bool ready=false,speechLoaded=false,sdReady=false,speaking=false,playing=false,paused=false,isMp3=false;
bool lastSensitive=false;
bool usbReady=false,hidUsb=false,hidSuppressed=false,hidMouse=false,lastConnected=false;
bool usbWritable=false,usbHadHost=false;
int hidSlot=0,hidChord=0,pendingHidSlot=0;
uint32_t hidChordStarted=0;
bool hidChordHandled=false;
const char *slotKeys[]={"hidV2Peer1","hidV2Peer2","hidV2Peer3"};
int startupStage=0,startupError=0;bool keyboardInitialized=false;
int rate=175,volume=65,mediaVolume=75,micGainDb=0,selection=0,timezoneIndex=0;
int timeFields[5]={2026,1,1,12,0},timeField=0;
time_t lastKnownTime=0;
std::atomic<bool> networkTimeReceived{false};
bool wifiConnected=false,networkTimeAnnounce=false;
uint32_t networkTimeStarted=0,timeConnectStarted=0;
String directory="/",password,ssid,lastSpoken,timeInput;
String pendingSSID,pendingPassword;
uint32_t wifiSaveStarted=0;
String fileTarget,fileInput,copySource;
bool renaming=false;
File audio;
FILE *recordingFile=nullptr;
File copyIn,copyOut;
String copyDestination;
size_t copyTotal=0,copyDone=0;
String recordingPath,lastRecordingPath;
String playbackPath;
constexpr char previewPath[]="/Recordings/.cardputer-preview.wav";
constexpr uint32_t recordingBytesPerSecond=44100*4;
constexpr uint32_t previewLimit=10*recordingBytesPerSecond;
uint32_t recordedBytes=0,lastCapture=0,lastCheckpoint=0;
uint32_t captureSkip=0;
uint32_t recordedNonzero=0,recordedClipped=0;int recordedPeak=0;
bool recordingActive=false,recordingPaused=false,recordingPreview=false;
bool previewPlaying=false,recordingPlayback=false,confirmRecordingDelete=false,previewKeyArmed=false;
constexpr size_t scratchBytes=4096;
std::unique_ptr<int16_t[]> captureBuffer;
size_t captureUsed=0;
float captureGain=1;
uint32_t wavDataStart=0,wavDataSize=0;
bool wavSeekable=false;
struct Entry { String name; bool dir; };
std::vector<Entry> entries;
std::vector<String> networks;
const char *home[]={"Keyboard learning","Voice recorder","File manager","Bluetooth and USB keyboard","Settings",wifiSDEnabled?"Browse SD card over USB or Wi-Fi":"Browse SD card over USB"
#ifdef UNIFIED_PLAYPALACE
 ,"PlayPalace"
#endif
 ,"Synthesizer"
};
constexpr int homeCount=sizeof(home)/sizeof(home[0]);
constexpr int synthHomeIndex=homeCount-1;
const char *options[]={"Wi-Fi networks","Disconnect Wi-Fi","Set time from network","Time zone","Type date and time","Choose date and time","Speech rate","Speech volume","Media volume","Microphone gain","Internal flash space","SD card space"};
constexpr struct SettingsCategory { const char *name; int first, count; } categories[]={
 {"Network",0,2},{"Date and time",2,4},{"Audio",6,3},{"Recording",9,1},{"Storage",10,2}
};
constexpr int categoryCount=sizeof(categories)/sizeof(categories[0]);
int category=0;
const char *fileActions[]={"Rename selected","Delete selected","Copy selected","Paste copied file","Create folder","Refresh SD card","Cancel"};
const char *zoneNames[]={"Alberta Time","Pacific time","Central time","Eastern time","UTC","Mountain time, United States"};
const char *zoneValues[]={"ABT6","PST8PDT,M3.2.0,M11.1.0","CST6CDT,M3.2.0,M11.1.0","EST5EDT,M3.2.0,M11.1.0","UTC0","MST7MDT,M3.2.0,M11.1.0"};
constexpr int zoneCount=sizeof(zoneNames)/sizeof(zoneNames[0]);
void say(const String &message,bool sensitive=false,bool interrupt=true);
void applyTimezone(){setenv("TZ",zoneValues[timezoneIndex],1);tzset();}
bool validTime(int64_t when){return when>=1577836800LL&&when<4102444800LL;}
void rememberTime(time_t when){
 if(!validTime(when))return;
 lastKnownTime=when;
 settings.putString("lastEpoch",String(static_cast<unsigned long>(when)));
}
bool setClock(int64_t epoch){
 if(!validTime(epoch))return false;
 timeval tv{static_cast<time_t>(epoch),0};
 if(settimeofday(&tv,nullptr)!=0)return false;
 rememberTime(tv.tv_sec);
 return true;
}
void startTimeSync(bool announce){
 if(WiFi.status()!=WL_CONNECTED){
  if(!announce)return;
  String ssid=settings.getString("wifiSsid");
  if(ssid.isEmpty()||!settings.getBool("wifiOn",true)){say("Choose a Wi-Fi network first");return;}
  WiFi.persistent(false);
  if(!WiFi.mode(WIFI_STA)){say("Wi-Fi radio could not start");return;}
  WiFi.begin(ssid.c_str(),settings.getString("wifiPass").c_str());
  timeConnectStarted=millis()|1;
  say("Connecting to the saved Wi-Fi network for time");
  return;
 }
 networkTimeReceived=false;
 networkTimeAnnounce=announce;
 networkTimeStarted=millis()|1;
 sntp_set_time_sync_notification_cb([](timeval *){networkTimeReceived=true;});
 configTzTime(zoneValues[timezoneIndex],"pool.ntp.org","time.cloudflare.com","time.nist.gov");
 if(announce)say("Synchronizing time");
}
void stopWifi(){
 timeConnectStarted=networkTimeStarted=0;
 networkTimeAnnounce=wifiConnected=false;
 networkTimeReceived=false;
 if(WiFi.getMode()==WIFI_OFF)return;
 esp_sntp_stop();
 wifiSaveStarted=0;pendingSSID="";pendingPassword="";
 if(page==Page::WifiScan){esp_wifi_scan_stop();WiFi.scanDelete();}
 WiFi.disconnect();
 WiFi.mode(WIFI_OFF);
}
int daysInMonth(int year,int month){static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};bool leap=year%4==0&&(year%100!=0||year%400==0);return days[month-1]+(month==2&&leap?1:0);}

void say(const String &message,bool sensitive,bool interrupt) {
 if(!ready || !message.length())return;
 {
  SpeechLock lock;
  if(speechLoaded){
   if(interrupt){
    if(speaking)reader.flush();
    speechLeg.discard();
   }
   lastSpoken=message;lastSensitive=sensitive;reader.speak(message.c_str()); speaking=true;
  }else speechLeg.cue(false);
 }
 if(page!=Page::UsbSD){
  if(sensitive)Serial.println("UF_SAY sensitive character");else Serial.printf("UF_SAY %s\n",message.c_str());
 }
}
void suspendSpeech(){
 SpeechLock lock;
 if(!speechLoaded)return;
 reader.end();
 espeak_Terminate();
 speechLoaded=speaking=false;
}
bool resumeSpeech(){
 SpeechLock lock;
 if(speechLoaded)return true;
 speechLoaded=reader.begin();
 if(speechLoaded){
  espeak_SetParameter(espeakVOLUME,65,0);
  reader.setRate(rate);
  reader.setGain(volume*0.007f);
 }
 return speechLoaded;
}
void stopMedia(){
 if(playing||audio||player||mp3)captureBuffer.reset();
 playing=paused=false;
 if(audio)audio.close();
 SpeechLock lock;
 if(player){player->end();delete player;player=nullptr;}
 if(mp3){mp3->end();delete mp3;mp3=nullptr;}
 if(previewPlaying){card.remove(previewPath);previewPlaying=false;}
 recordingPlayback=false;
}
void setMediaGain(){
 {
  SpeechLock lock;
  if(player)player->setGain(mediaVolume/100.0f);
  if(mp3)mp3->setGain(mediaVolume/100.0f);
 }
#ifdef UNIFIED_PLAYPALACE
 playpalace::setMediaVolume(mediaVolume);
#endif
}
void hidTone(bool high);
void stopSpeech();
bool startPlayback(const String &path);

bool recorderCardReady(){
 // A mounted SD object can outlive removal of the physical card.
 uint8_t sector[512];
 if(sdReady&&card.readRAW(sector,0))return true;
 lastRecordingPath="";
 card.end();
 sdReady=card.begin(12,SPI,25000000);
 settings.attachSD(sdReady);
 return sdReady;
}

bool writeCapturedFrames(){
 size_t frames=captureUsed/4;
 for(size_t i=0;i<frames*2;++i){
  int16_t sample=recording::applyCaptureGain(captureBuffer[i],captureGain);
  captureBuffer[i]=sample;
  int magnitude=abs(int(sample));
  recordedNonzero+=magnitude!=0;
  recordedClipped+=magnitude>=32760;
  recordedPeak=std::max(recordedPeak,magnitude);
 }
 const size_t bytes=frames*4;
 captureUsed=0;
 if(!bytes)return true;
 const size_t written=fwrite(captureBuffer.get(),1,bytes,recordingFile);
 recordedBytes+=written-written%4;
 return written==bytes;
}
bool checkpointRecording(){
 // Sync the payload and its file length before publishing the new WAV length.
 const int fd=fileno(recordingFile);
 if(fflush(recordingFile)!=0||fsync(fd)!=0)return false;
 uint8_t header[44];recording::wavHeader(header,recordedBytes);
 if(fseek(recordingFile,0,SEEK_SET)!=0||
    fwrite(header,1,sizeof(header),recordingFile)!=sizeof(header)||
    fflush(recordingFile)!=0||fsync(fd)!=0)return false;
 return fseek(recordingFile,44L+recordedBytes,SEEK_SET)==0;
}
void finishRecording(const char *error=nullptr,bool playPreview=true,bool discard=false){
 if(!recordingActive)return;
 if(!recordingPaused)output.endCapture();
 recordingActive=recordingPaused=false;
 bool ok=true;
 if(!discard){
  if(captureUsed&&!writeCapturedFrames())error="SD write failed";
  ok=checkpointRecording();
 }
 if(fclose(recordingFile)!=0)ok=false;
 recordingFile=nullptr;
 captureBuffer.reset();
 if(discard){
  say(card.remove(recordingPath)?"Recording deleted":"Could not delete recording");
  return;
 }
 if(recordingPreview){
  if(ok&&!error&&playPreview&&recordedBytes&&startPlayback(previewPath)){
   previewPlaying=recordingPlayback=true;
   hidTone(true);
   return;
  }
  card.remove(previewPath);
  if(error)say(error);
  else if(!ok)say("Could not finish preview");
  else if(playPreview&&!recordedBytes)hidTone(false);
  return;
 }
 lastRecordingPath=recordingPath;
 Serial.printf("UF_RECORD stopped bytes=%u nonzero=%u clipped=%u peak=%d header_ok=%d\n",recordedBytes,recordedNonzero,recordedClipped,recordedPeak,ok);
 if(!ok) say("Could not finalize the WAV header. File "+recordingPath);
 else if(error) say(String("Recording stopped. ")+error+". File "+recordingPath);
 else say("Recording saved. "+recordingPath+". "+String(recordedBytes/recordingBytesPerSecond)+" seconds");
}
void startRecording(bool preview){
 if(!recorderCardReady()){say("Insert an SD card before recording");return;}
 if(!card.exists("/Recordings")&&!card.mkdir("/Recordings")){say("Cannot create recordings folder");return;}
 // Clear any preview left behind by a reset.
 card.remove(previewPath);
 recordingPath=preview?previewPath:"";
 for(unsigned i=1;!preview&&i<100000;++i){
  char path[48];snprintf(path,sizeof(path),"/Recordings/recording-%05u.wav",i);
  if(!card.exists(path)){recordingPath=path;break;}
 }
 if(recordingPath.isEmpty()){say("No available recording filename");return;}
 std::unique_ptr<int16_t[]> buffer(new(std::nothrow) int16_t[scratchBytes/sizeof(int16_t)]);
 if(!buffer){say("Not enough memory to record");return;}
 const String mountedPath=String(card.mountpoint())+recordingPath;
 recordingFile=fopen(mountedPath.c_str(),preview?"wb":"wbx");
 if(!recordingFile){say("Cannot create recording");return;}
 recordedBytes=0;
 auto failStart=[](const char *message){
  fclose(recordingFile);recordingFile=nullptr;card.remove(recordingPath);say(message);
 };
 if(!checkpointRecording()){failStart("Cannot write recording header");return;}
 // Keep the DAC clocks enabled while also enabling both ADC clocks.
 // Speaker-only 0xB5 leaves the ADC clocks off; microphone-only 0xBA
 // disables DAC clocks. 0xBF enables both paths on the shared ES8311.
 const uint8_t regs[][2]={{0x01,0xbf},{0x0e,0x02},{0x14,0x10},{0x17,0xbf},{0x1c,0x6a}};
 for(auto &r:regs){Wire.beginTransmission(0x18);Wire.write(r[0]);Wire.write(r[1]);
  if(Wire.endTransmission()!=0){failStart("Microphone codec unavailable");return;}}
 stopSpeech();
 if(!output.beginCapture()){failStart("Microphone could not start");return;}
 captureBuffer=std::move(buffer);
 captureUsed=0;captureGain=recording::captureGain(micGainDb);
 recordedNonzero=recordedClipped=0;recordedPeak=0;
 lastCheckpoint=lastCapture=millis();
 recordingActive=true;recordingPreview=preview;
 captureSkip=recordingBytesPerSecond/10;
 hidTone(true);
 Serial.printf("UF_RECORD started path=%s\n",recordingPath.c_str());
}
void toggleRecordingPause(bool announceCue=true){
 if(recordingPaused){
  if(!output.beginCapture()){finishRecording("Microphone could not resume",false);return;}
  recordingPaused=false;lastCapture=millis();
  captureSkip=recordingBytesPerSecond/10;
 }else{
  output.endCapture();recordingPaused=true;
  if((captureUsed&&!writeCapturedFrames())||!checkpointRecording()){
   finishRecording("SD write failed",false);return;
  }
  lastCheckpoint=millis();
 }
 if(announceCue)hidTone(!recordingPaused);
}
int menuCount(){
 switch(page){
 case Page::Home:return homeCount;
 case Page::SettingsCategories:return categoryCount;
 case Page::Settings:return categories[category].count;
 case Page::TimeZone:return zoneCount;
 case Page::FileActions:return sizeof(fileActions)/sizeof(fileActions[0]);
 case Page::Files:return int(entries.size()+1);
 case Page::WifiList:return int(networks.size());
 default:return 0;
 }
}
void focus(const String &prefix="");
void moveMenu(char key,bool repeated=false){
 const int count=menuCount();
 if(!count){focus();return;}
 const int first=page==Page::Settings?categories[category].first:0;
 const int last=first+count-1;
 const int previous=selection;
 if(key==',')selection=first;
 else if(key=='/')selection=last;
 else if(key==';')selection=selection==first&&!repeated?last:std::max(first,selection-1);
 else if(key=='.')selection=selection==last&&!repeated?first:std::min(last,selection+1);
 if(!repeated||selection!=previous)focus();
}
void focus(const String &prefix) {
 switch(page){
 case Page::Home:say(String(home[selection])+", "+String(selection+1)+" of "+String(homeCount));break;
 case Page::SettingsCategories:say(prefix+categories[selection].name+", "+String(selection+1)+" of "+String(categoryCount));break;
 case Page::Settings:say(String(options[selection])+", "+String(selection-categories[category].first+1)+" of "+String(categories[category].count));break;
 case Page::TimeZone:say(String(zoneNames[selection])+", "+String(selection+1)+" of "+String(zoneCount));break;
 case Page::FileActions:say(String(fileActions[selection])+", "+String(selection+1)+" of 7");break;
 case Page::Files:
  if(selection==0)say(prefix+"Bluetooth keyboard and mouse, application, 1 of "+String(entries.size()+1));
  else if(selection<=int(entries.size())) {auto &e=entries[selection-1];say(e.name+(e.dir?", folder, ":", file, ")+String(selection+1)+" of "+String(entries.size()+1));}
  break;
 case Page::WifiList:
  if(networks.empty())say("No networks found");
  else{
   String network=networks[selection];
   String state=WiFi.status()==WL_CONNECTED&&WiFi.SSID()==network?", connected":settings.getString("wifiSsid")==network?", saved":"";
   say(network+state+", "+String(selection+1)+" of "+String(networks.size()));
  }
  break;
 default:break;
 }
}
void suppressHid(){if(lastConnected)sendUnifiedHid(hidUsb,hidMouse,true);hidSuppressed=true;}
void closeHid(){
 if(page!=Page::Hid)return;
 suppressHid();stopKeyboardSlot();hidChord=pendingHidSlot=0;lastConnected=false;
 if(!keyboardRadioRunning())resumeSpeech();
}
void cancelCopy(){if(copyIn)copyIn.close();if(copyOut)copyOut.close();if(!copyDestination.isEmpty())card.remove(copyDestination);copyDestination="";captureBuffer.reset();}
void homePage(){
 if(page==Page::Synth)resetSynth(synth.preset);
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace)playpalace::close();
#endif
 closeHid();stopWifi();settings.flushDeferred();stopMedia();
 const bool saved=recordingActive&&!recordingPreview;
 if(recordingActive)finishRecording(nullptr,false);
 String notice=saved?lastSpoken+". ":"";
 if(page==Page::Copying)cancelCopy();
 std::vector<Entry>().swap(entries);std::vector<String>().swap(networks);
 confirmRecordingDelete=previewKeyArmed=false;
 password="";page=Page::Home;selection=0;say(notice+"Home. Keyboard learning, 1 of "+String(homeCount));
}
void leaveUsbSD(){
 if(wifiSDEnabled)stopWifiSD();else stopUsbSD();
 page=Page::Home;selection=5;
 focus("SD sharing ended. ");
}
bool wifiSD=false, wifiSDListening=false;
uint32_t wifiSDStarted=0;
char wifiSDCode[9]{};
void announceWifiSD(){
 if(!wifiSDListening){say("Connecting to Wi-Fi. Alt Q cancels");return;}
 String message="Wi-Fi SD sharing. IP ";
 const String address=WiFi.localIP().toString();
 for(char c:address)message+=c=='.'?" dot ":String(c);
 message+=". Sharing code ";
 for(const char *p=wifiSDCode;*p;++p){message+=*p;message+=' ';}
 say(message+". Alt Q stops sharing");
}
void openWifiSD(){
 if(!sdReady){say("SD card unavailable");return;}
 if(WiFi.status()!=WL_CONNECTED){
  const String ssid=settings.getString("wifiSsid");
  if(ssid.isEmpty()||!settings.getBool("wifiOn",true)){say("Connect to Wi-Fi in Settings first");return;}
  WiFi.persistent(false);
  if(!WiFi.mode(WIFI_STA)){say("Wi-Fi radio could not start");return;}
  WiFi.begin(ssid.c_str(),settings.getString("wifiPass").c_str());
 }
 wifiSD=true;wifiSDListening=false;wifiSDStarted=millis();
 page=Page::UsbSD;
 announceWifiSD();
}
void openUsbSD(bool writable){
 if(!usbReady){say("USB unavailable");return;}
 if(!sdReady){say("SD card unavailable");return;}
 startUsbSD(writable,setClock);
 wifiSD=false;
 page=Page::UsbSD;
 usbWritable=writable;usbHadHost=unifiedUSBConnected();
 say(writable?"USB file access, read and write. Run USB SD browser on the computer. Alt Q stops sharing":"USB file access, read only. Run USB SD browser on the computer. Alt Q stops sharing");
}
void hidTone(bool high){
 SpeechLock lock;speechLeg.cue(high);
}
void selectHid(int slot,bool pair){
 suppressHid();stopKeyboardSlot();lastConnected=false;
 pendingHidSlot=0;
 if(slot==4){
  if(!usbReady){say("USB keyboard unavailable");return;}
  hidUsb=true;
  if(!keyboardRadioRunning())resumeSpeech();
  say(unifiedUSBConnected()?"USB keyboard selected":"USB keyboard selected. Connect a USB data cable");
  return;
 }
 hidUsb=false;hidSlot=slot;
 const String peer=settings.getString(slotKeys[slot-1]);
 if(!pair&&peer.isEmpty()){say("Slot "+String(slot)+" is empty. Hold Alt and "+String(slot)+" to pair");return;}
 if(pair){
  // Delay advertising until the spoken instructions have finished.
  say("Pair slot "+String(slot)+". Choose "+keyboardDeviceName(slot)+" on your device");
 }else say("Switching to Bluetooth slot "+String(slot));
 // A positive request is a switch, a negative request opens pairing.
 pendingHidSlot=pair?-slot:slot;
}
void loadDirectory(){
 entries.clear();selection=0;
 if(!sdReady){say("SD card unavailable. Bluetooth keyboard is available");return;}
 File dir=card.open(directory);if(!dir||!dir.isDirectory()){say("Cannot open folder");return;}
 for(File f=dir.openNextFile();f;f=dir.openNextFile()){
  if(entries.size()>=512){f.close();break;}
  String name=f.name();int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);
  if(directory=="/Recordings"&&name==".cardputer-preview.wav")continue;
  if(name.length())entries.push_back({name,f.isDirectory()});f.close();
 }
 dir.close();
 std::sort(entries.begin(),entries.end(),[](const Entry&a,const Entry&b){return a.dir!=b.dir?a.dir>b.dir:a.name.compareTo(b.name)<0;});
 focus(directory=="/"?String("SD card root. "):"Folder "+directory.substring(directory.lastIndexOf('/')+1)+". ");
}
String pathFor(const String &name){return directory=="/"?"/"+name:directory+"/"+name;}
uint32_t little32(const uint8_t *b){return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
void inspectWav(){
 wavSeekable=false;wavDataStart=wavDataSize=0;uint8_t b[20];
 if(!audio.seek(0)||audio.read(b,12)!=12||memcmp(b,"RIFF",4)||memcmp(b+8,"WAVE",4)){audio.seek(0);return;}
 bool formatOk=false;
 while(audio.position()+8<=audio.size()){
  if(audio.read(b,8)!=8)break;uint32_t len=little32(b+4);uint32_t start=audio.position();
  if(uint64_t(start)+len>audio.size())break;
  if(!memcmp(b,"fmt ",4)&&len>=16){if(audio.read(b,16)!=16)break;formatOk=b[0]==1&&b[1]==0&&b[2]==2&&b[3]==0&&little32(b+4)==44100&&b[14]==16&&b[15]==0;}
  if(!memcmp(b,"data",4)){wavDataStart=start;wavDataSize=len;wavSeekable=formatOk&&len>=4;break;}
  if(!audio.seek(start+len+(len&1)))break;
 }
 audio.seek(0);
}
void seekAudio(bool forward){
 if(!audio){say("Seeking is unavailable at the end of the file");return;}
 if(isMp3){
  size_t pos=audio.position(),step=std::max(size_t(1),size_t(audio.size()/20));
  size_t target=forward?std::min(size_t(audio.size()),pos+step):pos>step?pos-step:0;
  {SpeechLock lock;mp3->flush();}if(!audio.seek(target)){say("Seek failed");return;}say(forward?"Skipped forward approximately five percent":"Skipped back approximately five percent");return;
 }
 if(!wavSeekable){say("Seeking is supported for 44.1 kilohertz stereo 16 bit PCM WAV files");return;}
 size_t position=audio.position();size_t offset=position>wavDataStart?std::min(size_t(wavDataSize),position-size_t(wavDataStart)):0;
 const size_t step=5*44100*4;
 size_t target=forward?std::min(size_t(wavDataSize),offset+step):offset>step?offset-step:0;target-=target%4;
 if(!audio.seek(wavDataStart+target)){say("Seek failed");return;}
 uint8_t header[44];recording::wavHeader(header,wavDataSize-target);
 size_t accepted;
 {SpeechLock lock;player->flush();accepted=player->write(header,sizeof(header));}
 if(accepted!=sizeof(header)){say("Seek failed");return;}
 say(forward?"Forward approximately five seconds":"Back approximately five seconds");
}
int readBatteryPercent(int &mv){
 int samples[15];
 for(auto &sample:samples)sample=analogReadMilliVolts(10)*2;
 std::sort(std::begin(samples),std::end(samples));
 mv=samples[7];
 if(mv<2500||mv>5000)return -1;
 // Approximate 1S lithium-ion curve. Voltage rises under charge, so the
 // board's voltage-only reading cannot resolve exact state of charge.
 constexpr int volts[]={3300,3650,3750,3820,3900,3970,4050,4120,4200};
 constexpr int levels[]={0,8,25,45,62,75,86,94,100};
 int percent=100;
 if(mv<=volts[0])percent=0;
 else for(size_t i=1;i<sizeof(volts)/sizeof(*volts);++i){
  if(mv<=volts[i]){
   percent=levels[i-1]+((mv-volts[i-1])*(levels[i]-levels[i-1])+(volts[i]-volts[i-1])/2)/(volts[i]-volts[i-1]);
   break;
  }
 }
 return percent;
}
void announceBattery(){
 int mv;
 int percent=readBatteryPercent(mv);
 if(percent<0){say("Battery level unavailable");return;}
 if(unifiedUSBConnected()){
  say("USB connected. Unplug for a battery estimate. Voltage "+String(mv)+" millivolts");
  return;
 }
 // USB wall chargers cannot be detected reliably. Avoid calling a high
 // terminal voltage a full battery and round away small ADC variations.
 if(percent>=98){
  say("Battery voltage is high, "+String(mv)+" millivolts. Unplug for a reliable estimate");
  return;
 }
 say("Battery approximately "+String(((percent+2)/5)*5)+" percent. External power can raise this estimate");
}
String storageSize(uint64_t bytes){
 const double mib=bytes/1048576.0;
 if(mib>=1024)return String(mib/1024,2)+" gibibytes";
 if(mib>=1)return String(mib,2)+" mebibytes";
 if(bytes>=1024)return String(bytes/1024.0,2)+" kibibytes";
 return String(static_cast<unsigned>(bytes))+" bytes";
}
void announceFlashSpace(){
 const auto *partition=esp_ota_get_running_partition();
 if(!partition){say("Internal flash space unavailable");return;}
 const esp_partition_pos_t position{partition->address,partition->size};
 esp_image_metadata_t image{};
 if(esp_image_verify(ESP_IMAGE_VERIFY,&position,&image)!=ESP_OK||image.image_len>partition->size){
  say("Internal flash totals "+storageSize(ESP.getFlashChipSize())+". Firmware space unavailable");
  return;
 }
 say("Internal flash totals "+storageSize(ESP.getFlashChipSize())+
     ". Firmware uses "+storageSize(image.image_len)+". Available for firmware "+
     storageSize(partition->size-image.image_len)+". User files are stored on the SD card");
}
void announceSDSpace(){
 uint64_t total=0,available=0;
 if(!sdReady||esp_vfs_fat_info(card.mountpoint(),&total,&available)!=ESP_OK||!total||available>total){
  say("SD card space unavailable");
  return;
 }
 say("SD card totals "+storageSize(total)+". Used "+storageSize(total-available)+
     ". Available "+storageSize(available));
}
void announceTime(){
 time_t now=time(nullptr);
 bool current=validTime(now);
 if(!current)now=lastKnownTime;
 if(!validTime(now)){say("Time is not set");return;}
 tm info{};
 localtime_r(&now,&info);
 char text[72];
 strftime(text,sizeof(text),"%A %B %d, %I:%M %p",&info);
 say(current?String(text):String("Time is not set. Last saved ")+text);
}
void announceTimeField(){static const char *names[]={"Year","Month","Day","Hour, 24 hour clock","Minute"};say(String(names[timeField])+" "+String(timeFields[timeField])+". "+String(timeField+1)+" of 5");}
void announceNetwork(){say(WiFi.status()==WL_CONNECTED?String("Wi-Fi connected to ")+WiFi.SSID():String("Wi-Fi not connected"));}
#ifdef UNIFIED_PLAYPALACE
bool connectPlayPalaceWifi(){
 if(WiFi.status()==WL_CONNECTED)return true;
 const String ssid=settings.getString("wifiSsid");
 if(ssid.isEmpty()||!settings.getBool("wifiOn",true))return false;
 WiFi.persistent(false);
 if(!WiFi.mode(WIFI_STA))return false;
 WiFi.setAutoReconnect(true);
 WiFi.begin(ssid.c_str(),settings.getString("wifiPass").c_str());
 return true;
}
#endif
void stopSpeech(){
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace)playpalace::stopAnnouncements();
#endif
 SpeechLock lock;
 if(speechLoaded&&speaking)reader.flush();
 speechLeg.discard();
 speaking=false;
}
bool global(const HeadlessKeyboard::Event &e,const HeadlessKeyboard::State&s){
 using namespace globalShortcuts;
 Command c=route({e.row,e.col,e.down,s.opt,s.fn,s.alt,s.ctrl,s.shift});
 if(page==Page::Synth&&(c==Command::Help||c==Command::Repeat))return false;
 if(c==Command::None)return false;
 if(page==Page::Hid&&c!=Command::Home)suppressHid();
 switch(c){
 case Command::Home:homePage();break;
 case Command::Battery:announceBattery();break;
 case Command::Time:announceTime();break;
 case Command::Network:announceNetwork();break;
 case Command::Stop:stopSpeech();break;
 case Command::Repeat:{String previous=lastSpoken;bool sensitive=lastSensitive;say(previous,sensitive);break;}
 case Command::Help:
  if(menuCount()||page==Page::WifiList)say("Semicolon moves up, period moves down. Hold to repeat. Comma first, slash last. Space reads the selection. Enter opens. Function backtick goes back. Control stops speech. Alt Q goes home");
  else say("Alt comma and slash change speech rate. Alt semicolon and period change speech and media volume. Option space stops speech. Option R repeats. Alt Q goes home. Alt B battery. Alt T time. Alt N network");
  break;
 case Command::RateDown:case Command::RateUp:
  rate=constrain(rate+(c==Command::RateUp?10:-10),100,espeakRATE_MAXIMUM);{SpeechLock lock;if(speechLoaded)reader.setRate(rate);}settings.putInt("rate",rate,page==Page::PlayPalace);say("Speech rate "+String(rate));break;
 case Command::VolumeUp:case Command::VolumeDown:
  volume=constrain(volume+(c==Command::VolumeUp?5:-5),5,100);{SpeechLock lock;if(speechLoaded)reader.setGain(volume*0.007f);}settings.putInt("volume",volume,page==Page::PlayPalace);say("Speech volume "+String(volume)+" percent");break;
 case Command::CombinedVolumeUp:case Command::CombinedVolumeDown:{
  const int delta=c==Command::CombinedVolumeUp?5:-5;
  volume=constrain(volume+delta,5,100);
  mediaVolume=constrain(mediaVolume+delta,0,100);
  {SpeechLock lock;if(speechLoaded)reader.setGain(volume*0.007f);}
  setMediaGain();
  settings.putVolumes(volume,mediaVolume,page==Page::PlayPalace);
  say("Speech "+String(volume)+" percent. Media "+String(mediaVolume)+" percent");
  break;
 }
 default:break;
 }
 return true;
}
void openSelected(){
 if(selection==0){
  page=Page::Hid;hidMouse=false;hidSuppressed=true;lastConnected=false;
  std::vector<Entry>().swap(entries);
  std::vector<String>().swap(networks);
  hidUsb=usbReady&&unifiedUSBConnected();
  say(String(hidUsb?"USB keyboard. ":"Keyboard. ")+"Tap Alt 1 through 3 to switch. Hold the number to pair. Alt 4 selects USB. Option M switches mouse mode. Alt Q returns home");
  if(!hidUsb){
   hidSlot=constrain(settings.getInt("hidSlot",1),1,3);
   if(!settings.getString(slotKeys[hidSlot-1]).isEmpty())pendingHidSlot=hidSlot;
  }
  return;
 }
 Entry e=entries[selection-1];String path=pathFor(e.name);
 if(e.dir){directory=path;loadDirectory();return;}
 if(!startPlayback(path))return;
 page=Page::Play;
 recordingPlayback=path.startsWith("/Recordings/");
 if(recordingPlayback){stopSpeech();hidTone(true);}
 else say("Playing "+e.name+". Space pauses. Semicolon and period adjust media volume. Backspace returns to files");
}
bool startPlayback(const String &path){
 String lower=path;lower.toLowerCase();
 if(!lower.endsWith(".wav")&&!lower.endsWith(".mp3")){say("Unsupported file type. Supported audio: WAV and MP3");return false;}
 audio=card.open(path,FILE_READ);if(!audio){say("Could not open file");return false;}
 isMp3=lower.endsWith(".mp3");if(!isMp3)inspectWav();
 size_t decoderSize=isMp3?sizeof(BackgroundAudioMP3):sizeof(BackgroundAudioWAV);
 size_t reserve=isMp3?40000:20000;
 if(ESP.getFreeHeap()<decoderSize+reserve||heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<decoderSize){
  audio.close();say("Not enough memory for playback. Turn off Wi-Fi and try again");return false;
 }
 std::unique_ptr<int16_t[]> buffer(new(std::nothrow) int16_t[scratchBytes/sizeof(int16_t)]);
 if(!buffer){audio.close();say("Not enough memory for playback");return false;}
 bool started=false;
 {
  SpeechLock lock;
  if(isMp3){
   mp3=new(std::nothrow) BackgroundAudioMP3;
   if(mp3){mp3->setDevice(&mp3Leg);mp3->setGain(mediaVolume/100.0f);started=mp3->begin();}
  }else{
   player=new(std::nothrow) BackgroundAudioWAV;
   if(player){player->setDevice(&wavLeg);player->setGain(mediaVolume/100.0f);started=player->begin();}
  }
 }
 if(!started){stopMedia();say("Could not start playback");return false;}
 captureBuffer=std::move(buffer);
 playbackPath=path;
 playing=true;paused=false;
 return true;
}
void togglePlaybackPause(){
 paused=!paused;
 {SpeechLock lock;if(isMp3){if(paused)mp3->pause();else mp3->unpause();}else{if(paused)player->pause();else player->unpause();}}
 if(recordingPlayback)hidTone(!paused);
 else say(paused?"Paused":"Playing");
}
void scanWifi(){
 std::vector<Entry>().swap(entries);
 networks.clear();
 WiFi.persistent(false);
 if(!WiFi.mode(WIFI_STA)){
  Serial.printf("UF_WIFI start failed heap=%u largest=%u\n",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  say("Wi-Fi radio could not start. "+String(ESP.getFreeHeap()/1024)+" kilobytes free");return;
 }
 page=Page::WifiScan;say("Scanning Wi-Fi networks");
 if(WiFi.scanNetworks(true)==WIFI_SCAN_FAILED){page=Page::Settings;selection=0;say("Wi-Fi scan could not start");}
}
void handleSynth(const HeadlessKeyboard::Event &e,const HeadlessKeyboard::State &s){
 const unsigned key=e.row*14+e.col;
 if(!e.down){SpeechLock lock;synth.noteOff(key);return;}
 if(s.alt){
  if(s.opt)return;
  if(e.row==0&&e.col>=1&&e.col<=std::size(synthPresets)){
   resetSynth(Synth::Preset(e.col-1));
   say(synthPresets[e.col-1]);
  }else if(e.row==1&&(e.col==11||e.col==12)){
   synthBoost=constrain(synthBoost+(e.col==12?1:-1),0,Synth::maxBoost);
   settings.putInt("synthBoost",synthBoost,true);
   say(String("Quiet sound boost, ")+String(synthBoost)+" of "+String(Synth::maxBoost));
  }else if(e.row==2&&e.col==7){
   say(String(synthPresets[int(synth.preset)])+". "+synthHelp);
  }else if(e.row==1&&e.col==4){
   say(synthPresets[int(synth.preset)]);
  }
  return;
 }
 if(!keys::usage[e.row][e.col])return;
 // Contiguous chromatic rows, from Z and Space up to the digit row.
 constexpr int rowNotes[]={75,61,49,38};
 const int firstColumn=e.row==3?3:e.row==2?2:0;
 SpeechLock lock;
 synth.noteOn(key,rowNotes[e.row]+e.col-firstColumn);
}
void handle(const HeadlessKeyboard::Event &e,const HeadlessKeyboard::State &s){
 if(page==Page::Record){
  if(e.row==3&&e.col==0){
   if(e.down)previewKeyArmed=!s.alt&&!s.fn&&!s.opt&&!s.shift;
   else if(previewKeyArmed&&!confirmRecordingDelete){
    if(recordingActive&&recordingPreview)finishRecording();
    else if(!recordingActive){stopMedia();startRecording(true);}
   }
   if(!e.down)previewKeyArmed=false;
   return;
  }
  if(e.down)previewKeyArmed=false;
 }
 if(page==Page::Synth){
  if(!global(e,s))handleSynth(e,s);
  return;
 }
 if(page==Page::Hid&&e.row==0&&e.col>=1&&e.col<=4){
  if(e.down&&s.alt){
   suppressHid();hidChord=e.col;hidChordStarted=millis();hidChordHandled=false;
   return;
  }
  if(!e.down&&hidChord==e.col){
   if(!hidChordHandled)selectHid(hidChord,hidChord<4&&millis()-hidChordStarted>=650);
   hidChord=0;return;
  }
 }
 if(!e.down)return;
 if(page==Page::UsbSD){
  if((s.alt&&e.row==1&&e.col==1)||(s.fn&&e.row==0&&e.col==0)||(!s.alt&&!s.fn&&e.row==0&&e.col==13)){
   leaveUsbSD();
  }else if(e.row==3&&e.col==13){
   if(wifiSDEnabled&&wifiSD)announceWifiSD();
   else say(!unifiedUSBConnected()?"Connect a USB data cable":usbWritable?"SD card shared with write access":"SD card shared read only");
  }else if(e.row==3&&e.col==0)stopSpeech();
  return;
 }
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace&&e.row==3&&e.col==0){stopSpeech();return;}
#endif
 if(global(e,s))return;
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace){
  if(s.opt)return;
  const char base=keys::plain[e.row][e.col];
  const char character=s.shift?keys::shifted[e.row][e.col]:base;
  const int function=s.fn&&e.row==0&&e.col>=1&&e.col<=12?e.col:0;
  playpalace::key({base,character,function,s.fn,s.shift,s.ctrl,s.alt});
  return;
 }
 if(page==Page::Home&&selection==6&&e.row==2&&e.col==13&&!s.fn){
  page=Page::PlayPalace;
  playpalace::open(card,sdReady,say,connectPlayPalaceWifi,homePage);
  return;
 }
#endif
 if(page==Page::Home&&selection==synthHomeIndex&&e.row==2&&e.col==13&&!s.fn){
  if(!synthLeg.leg){say("Synthesizer audio unavailable");return;}
  resetSynth(synth.preset);
  page=Page::Synth;
  say(String("Synthesizer. ")+synthPresets[int(synth.preset)]+". Play the keys. Alt with a digit selects a preset. Alt H reads controls.");
  return;
 }
 bool escape=s.fn&&e.row==0&&e.col==0;
 bool backspace=!s.fn&&!s.opt&&!s.alt&&!s.ctrl&&e.row==0&&e.col==13;
 if(page==Page::UsbSDMode){
  if(escape||backspace){page=Page::Home;selection=5;focus();}
  else if(e.col==11&&(e.row==2||e.row==3)){
   selection=wifiSDEnabled?(selection+(e.row==2?2:1))%3:1-selection;
   say(selection==0?"USB read only":selection==1?"USB read and write":"Wi-Fi read and write");
  }else if(e.row==2&&e.col==13){
   if(wifiSDEnabled&&selection==2)openWifiSD();else openUsbSD(selection==1);
  }
  return;
 }
 if((escape||backspace)&&page==Page::Settings){page=Page::SettingsCategories;selection=category;focus();return;}
 if((escape||backspace)&&page==Page::SettingsCategories){page=Page::Home;selection=4;focus();return;}
 int setting=-1;
 switch(page){
 case Page::WifiList:case Page::WifiScan:setting=0;break;
 case Page::TimeZone:setting=3;break;
 case Page::SpeechRate:setting=6;break;
 case Page::SpeechVolume:setting=7;break;
 case Page::MediaVolume:setting=8;break;
 case Page::MicGain:setting=9;break;
 default:break;
 }
 if((escape||backspace)&&setting>=0){page=Page::Settings;selection=setting;focus();return;}
 if(escape){
  if(page==Page::Password){password="";page=Page::WifiList;selection=0;focus();return;}
  if(page==Page::ManualTime||page==Page::TimePicker){timeInput="";bool typed=page==Page::ManualTime;page=Page::Settings;selection=typed?4:5;focus();return;}
  if(page==Page::FileName||page==Page::ConfirmDelete||page==Page::FileActions){page=Page::Files;loadDirectory();return;}
  homePage();return;
 }
 if(backspace&&page==Page::FileActions){page=Page::Files;loadDirectory();return;}
 if(page==Page::Hid){
  if(s.opt && e.row==3 && e.col==9){suppressHid();hidMouse=!hidMouse;say(hidMouse?"Mouse mode":"Keyboard mode");}
  return;
 }
 if(page==Page::Learn){say(keys::describe(e.row,e.col,s.shift,s.fn,s.ctrl,s.alt).c_str());return;}
 if(page==Page::Password){
  if(e.row==2&&e.col==13){
   pendingSSID=ssid;pendingPassword=password;password="";
   wifiSaveStarted=millis()|1;
   WiFi.persistent(false);WiFi.begin(pendingSSID.c_str(),pendingPassword.c_str());
   page=Page::Settings;selection=0;say("Connecting. Use Alt N for network status");
  }
  else if(e.row==0&&e.col==13){if(password.length())password.remove(password.length()-1);say("Deleted");}
  else if(!s.fn&&!s.ctrl&&!s.alt&&!s.opt){char ch=s.shift?keys::shifted[e.row][e.col]:keys::plain[e.row][e.col];if(ch>=32&&password.length()<63){password+=ch;String spoken=keys::name(ch).c_str();if(ch>='A'&&ch<='Z')spoken="Capital "+spoken;say(spoken,true);}}
  return;
 }
 if(page==Page::ManualTime){
  if(e.row==2&&e.col==13){
   if(timeInput.length()!=12){say("Enter 12 digits: year month day hour minute");return;}
   tm value{};value.tm_year=timeInput.substring(0,4).toInt()-1900;value.tm_mon=timeInput.substring(4,6).toInt()-1;value.tm_mday=timeInput.substring(6,8).toInt();value.tm_hour=timeInput.substring(8,10).toInt();value.tm_min=timeInput.substring(10,12).toInt();value.tm_isdst=-1;
   tm original=value;time_t t=mktime(&value);
   if(t<0||value.tm_year!=original.tm_year||value.tm_mon!=original.tm_mon||value.tm_mday!=original.tm_mday||value.tm_hour!=original.tm_hour||value.tm_min!=original.tm_min){say("Invalid date or time. Enter 12 digits again");timeInput="";return;}
   if(!setClock(t)){say("Enter a date from 2020 through 2099");return;}
   page=Page::Settings;selection=4;say("Time set");
  } else if(e.row==0&&e.col==13){if(timeInput.length())timeInput.remove(timeInput.length()-1);say("Deleted");}
  else {char ch=keys::plain[e.row][e.col];if(ch>='0'&&ch<='9'&&timeInput.length()<12){timeInput+=ch;say(String(ch));}}
  return;
 }
 if(page==Page::TimePicker){
  if(e.row==2&&e.col==13){
   if(timeField<4){++timeField;announceTimeField();return;}
   tm value{};value.tm_year=timeFields[0]-1900;value.tm_mon=timeFields[1]-1;value.tm_mday=timeFields[2];value.tm_hour=timeFields[3];value.tm_min=timeFields[4];value.tm_isdst=-1;
   tm original=value;time_t t=mktime(&value);
   if(t<0||value.tm_year!=original.tm_year||value.tm_mon!=original.tm_mon||value.tm_mday!=original.tm_mday||value.tm_hour!=original.tm_hour||value.tm_min!=original.tm_min){say("Invalid local time, choose a different hour or minute");timeField=3;return;}
   if(!setClock(t)){say("Could not set time");return;}
   page=Page::Settings;selection=5;say("Time set");return;
  }
  if(backspace){if(timeField>0){--timeField;announceTimeField();}else{page=Page::Settings;selection=5;focus();}return;}
  if(e.col==11&&(e.row==2||e.row==3)){
   int delta=e.row==2?1:-1;int lo=timeField==0?2020:timeField==1||timeField==2?1:0;
   int hi=timeField==0?2099:timeField==1?12:timeField==2?daysInMonth(timeFields[0],timeFields[1]):timeField==3?23:59;
   timeFields[timeField]+=delta;if(timeFields[timeField]<lo)timeFields[timeField]=hi;else if(timeFields[timeField]>hi)timeFields[timeField]=lo;
   timeFields[2]=std::min(timeFields[2],daysInMonth(timeFields[0],timeFields[1]));announceTimeField();
  }return;
 }
 if(page==Page::FileName){
  if(e.row==2&&e.col==13){
   if(fileInput.isEmpty()||fileInput=="."||fileInput==".."||fileInput.indexOf('/')>=0||fileInput.indexOf('\\')>=0){say("Invalid name");return;}
   String destination=pathFor(fileInput);
   if(card.exists(destination)){say("A file or folder already has that name");return;}
   bool ok=renaming?card.rename(fileTarget,destination):card.mkdir(destination);
   page=Page::Files;if(ok){loadDirectory();say(renaming?"Renamed":"Folder created");}else say(renaming?"Rename failed":"Could not create folder");
  }else if(e.row==0&&e.col==13){if(fileInput.length())fileInput.remove(fileInput.length()-1);say("Deleted");}
  else if(s.fn&&e.row==0&&e.col==0){page=Page::Files;loadDirectory();}
  else if(!s.fn&&!s.ctrl&&!s.alt&&!s.opt){char ch=s.shift?keys::shifted[e.row][e.col]:keys::plain[e.row][e.col];if(ch>=32&&fileInput.length()<120){fileInput+=ch;String spoken=keys::name(ch).c_str();if(ch>='A'&&ch<='Z')spoken="Capital "+spoken;say(spoken);}}
  return;
 }
 if(page==Page::ConfirmDelete){
  if(e.row==2&&e.col==13){File f=card.open(fileTarget);bool isDirectory=f&&f.isDirectory();f.close();bool ok=isDirectory?card.rmdir(fileTarget):card.remove(fileTarget);page=Page::Files;if(ok){loadDirectory();say("Deleted");}else say("Delete failed. Folders must be empty");}
  else {page=Page::Files;loadDirectory();say("Cancelled");}return;
 }
 if(page==Page::Copying){if(e.row==3&&e.col==13){unsigned percent=copyTotal?unsigned((uint64_t(copyDone)*100)/copyTotal):100;say("Copy "+String(percent)+" percent. Alt Q cancels");}return;}
 if(page==Page::SpeechRate||page==Page::SpeechVolume||page==Page::MicGain||page==Page::MediaVolume){
  if(e.col==11&&(e.row==2||e.row==3)){
   int delta=e.row==2?1:-1;
   if(page==Page::SpeechRate){rate=constrain(rate+delta*10,100,espeakRATE_MAXIMUM);{SpeechLock lock;reader.setRate(rate);}settings.putInt("rate",rate);say("Speech rate "+String(rate));}
   else if(page==Page::SpeechVolume){volume=constrain(volume+delta*5,5,100);{SpeechLock lock;reader.setGain(volume*0.007f);}settings.putInt("volume",volume);say("Speech volume "+String(volume)+" percent");}
   else if(page==Page::MicGain){micGainDb=constrain(micGainDb+delta*3,-24,24);settings.putInt("micGain",micGainDb);say("Microphone gain "+String(micGainDb)+" decibels");}
   else {mediaVolume=constrain(mediaVolume+delta*5,0,100);setMediaGain();settings.putInt("mediaVol",mediaVolume);say("Media volume "+String(mediaVolume)+" percent");}
  }else if(e.row==2&&e.col==13){int selected=page==Page::SpeechRate?6:page==Page::SpeechVolume?7:page==Page::MicGain?9:8;page=Page::Settings;selection=selected;focus();}
  return;
 }
 if(page==Page::Play){
  if(e.row==3&&e.col==13){
   togglePlaybackPause();
  }
  else if((e.row==2||e.row==3)&&e.col==11&&!s.fn){mediaVolume=constrain(mediaVolume+(e.row==2?5:-5),0,100);setMediaGain();settings.putInt("mediaVol",mediaVolume);say("Media volume "+String(mediaVolume)+" percent");}
  else if(e.row==3&&(e.col==10||e.col==12))seekAudio(e.col==12);
  else if(backspace){
   const bool deleteRecording=recordingPlayback;
   stopMedia();
   if(deleteRecording){fileTarget=playbackPath;page=Page::ConfirmDelete;say("Delete "+fileTarget+"? Press Enter to confirm or Backspace to cancel");}
   else{page=Page::Files;focus();}
  }return;
 }
 if(page==Page::Record){
  if(s.fn||s.ctrl||s.alt||s.opt||s.shift)return;
  const bool enter=e.row==2&&e.col==13;
  if(confirmRecordingDelete){
   if(!enter&&!backspace)return;
   confirmRecordingDelete=false;
   if(backspace)say(recordingActive?"Cancelled. Recording paused. Space resumes":"Cancelled");
   else if(recordingActive)finishRecording(nullptr,false,true);
   else if(recorderCardReady()&&!lastRecordingPath.isEmpty()&&card.remove(lastRecordingPath)){lastRecordingPath="";say("Recording deleted");}
   else say("Could not delete recording");
  }else if(enter){
   if(recordingActive)finishRecording();
   else{stopMedia();startRecording(false);}
  }else if(backspace){
   if(previewPlaying){stopMedia();hidTone(false);return;}
   if(!recordingActive&&lastRecordingPath.isEmpty()){say("No recording to delete");return;}
   const String target=recordingActive?recordingPath:lastRecordingPath;
   if(recordingActive&&!recordingPaused){
    toggleRecordingPause(false);
    if(!recordingActive)return;
   }
   confirmRecordingDelete=true;
   say("Delete "+target+"? Press Enter to confirm or Backspace to cancel");
  } else if((e.row==2||e.row==3)&&e.col==11&&!s.fn){
   if(recordingActive)say("Stop recording before changing microphone gain");
   else {micGainDb=constrain(micGainDb+(e.row==2?3:-3),-24,24);settings.putInt("micGain",micGainDb);say("Microphone gain "+String(micGainDb)+" decibels");}
  } else if(e.row==3&&e.col==13){
   if(recordingActive){stopSpeech();toggleRecordingPause();}
   else if(previewPlaying)togglePlaybackPause();
   else say("Enter records. Tap Control to record a preview. Alt Q returns home");
  }
  return;
 }
 if(menuCount()||page==Page::WifiList){
  const char key=keys::plain[e.row][e.col];
  if(key==3){stopSpeech();return;}
  if(!s.ctrl&&!s.alt&&!s.opt&&!s.shift){
   if(key==';'||key=='.'||key==','||key=='/'){moveMenu(key);return;}
   if(key==' '&&!s.fn){focus();return;}
  }
 }
 if(page==Page::Files&&e.row==0&&e.col==13){if(directory!="/"){int p=directory.lastIndexOf('/');directory=p<=0?"/":directory.substring(0,p);loadDirectory();}else homePage();return;}
 if(page==Page::Files&&!s.fn&&!s.ctrl&&!s.alt&&!s.opt&&e.row==3&&e.col==9){fileTarget=selection>0?pathFor(entries[selection-1].name):"";page=Page::FileActions;selection=0;focus();return;}
 if(e.row!=2||e.col!=13)return;
 if(page==Page::Home){switch(selection){case 0:page=Page::Learn;say("Keyboard learning. Press keys to hear them. Shift is to the right of Function. Alt Q returns home");break;case 1:page=Page::Record;say("Voice recorder. Enter starts or saves. Space pauses or resumes. Tap Control to start or play back a preview. Backspace asks to delete. Semicolon and period change microphone gain. Alt Q returns home");break;case 2:page=Page::Files;directory="/";loadDirectory();break;case 3:selection=0;openSelected();break;case 4:page=Page::SettingsCategories;selection=0;focus("Settings. ");break;case 5:page=Page::UsbSDMode;selection=0;say(wifiSDEnabled?"SD card sharing. USB read only. Press period for USB read and write, then Wi-Fi read and write. Enter starts sharing":"USB SD card. Read only. Period switches to read and write. Enter starts sharing");break;}return;}
 if(page==Page::Files){openSelected();return;}
 if(page==Page::FileActions){
  int action=selection;
  if(action==6){page=Page::Files;loadDirectory();return;}
  if(action==5){card.end();sdReady=card.begin(12,SPI,25000000);settings.attachSD(sdReady);directory="/";page=Page::Files;if(sdReady){loadDirectory();say("SD card ready. Restart to apply settings from the card");}else say("SD card unavailable");return;}
  if(action==4){renaming=false;fileInput="";page=Page::FileName;say("New folder name. Type name, Enter creates, Function backtick cancels");return;}
  if(action==3){
   if(copySource.isEmpty()){say("No file copied");return;}
   int slash=copySource.lastIndexOf('/');String dest=pathFor(copySource.substring(slash+1));
   if(card.exists(dest)){say("Destination already exists");return;}
   std::unique_ptr<int16_t[]> buffer(new(std::nothrow) int16_t[scratchBytes/sizeof(int16_t)]);
   if(!buffer){say("Not enough memory to copy the file");return;}
   copyIn=card.open(copySource,FILE_READ);if(!copyIn||copyIn.isDirectory()){if(copyIn)copyIn.close();say("Source file unavailable");return;}
   copyOut=card.open(dest,FILE_WRITE);if(!copyOut){copyIn.close();say("Cannot create destination");return;}
   captureBuffer=std::move(buffer);
   copyDestination=dest;copyTotal=copyIn.size();copyDone=0;page=Page::Copying;say("Copying file. Space announces progress. Alt Q cancels");return;
  }
  if(fileTarget.isEmpty()){say("Select a file or folder first");return;}
  if(action==0){renaming=true;fileInput="";page=Page::FileName;say("New name. Type name, Enter renames, Function backtick cancels");return;}
  if(action==1){page=Page::ConfirmDelete;say("Delete selected item? Enter confirms, any other key cancels");return;}
  File source=card.open(fileTarget);bool isDirectory=source&&source.isDirectory();if(source)source.close();
  if(isDirectory){say("Copying folders is not supported");return;}copySource=fileTarget;say("File copied to clipboard. Open destination and choose paste");return;
 }
 if(page==Page::WifiList&&!networks.empty()){
  ssid=networks[selection];
  if(!s.fn&&WiFi.status()==WL_CONNECTED&&WiFi.SSID()==ssid){
   page=Page::Settings;selection=0;say("Already connected to "+ssid+". Function Enter changes the password in the network list");return;
  }
  if(!s.fn&&settings.getString("wifiSsid")==ssid){
   WiFi.persistent(false);WiFi.begin(ssid.c_str(),settings.getString("wifiPass").c_str());
   settings.putBool("wifiOn",true);page=Page::Settings;selection=0;
   say("Connecting to saved network "+ssid+". Function Enter changes the password in the network list");return;
  }
  password="";page=Page::Password;
  say("Password. Characters are spoken aloud. Hold Shift, next to Function, for capitals. Enter connects");return;
 }
 if(page==Page::TimeZone){timezoneIndex=selection;settings.putInt("timezone",timezoneIndex);applyTimezone();page=Page::Settings;selection=3;say(String("Time zone set to ")+zoneNames[timezoneIndex]);return;}
 if(page==Page::SettingsCategories){
  category=selection;page=Page::Settings;selection=categories[category].first;
  say(String(categories[category].name)+". "+options[selection]+", 1 of "+String(categories[category].count));
  return;
 }
 if(page==Page::Settings){switch(selection){case 0:scanWifi();break;case 1:settings.putBool("wifiOn",false);stopWifi();say("Wi-Fi disconnected");break;case 2:startTimeSync(true);break;case 3:page=Page::TimeZone;selection=timezoneIndex;focus();break;case 4:page=Page::ManualTime;timeInput="";say("Enter year, month, day, hour and minute as twelve digits, then Enter");break;case 5:{tm current{};time_t seed=time(nullptr);if(!validTime(seed))seed=lastKnownTime;if(validTime(seed)&&localtime_r(&seed,&current)){timeFields[0]=current.tm_year+1900;timeFields[1]=current.tm_mon+1;timeFields[2]=current.tm_mday;timeFields[3]=current.tm_hour;timeFields[4]=current.tm_min;}timeField=0;page=Page::TimePicker;say("Choose year, month, day, hour and minute. Semicolon increases, period decreases. Enter selects. "+String("Year ")+String(timeFields[0]));break;}case 6:page=Page::SpeechRate;say("Speech rate "+String(rate)+". Semicolon increases, period decreases. Enter returns");break;case 7:page=Page::SpeechVolume;say("Speech volume "+String(volume)+" percent. Semicolon increases, period decreases. Enter returns");break;case 9:page=Page::MicGain;say("Microphone gain "+String(micGainDb)+" decibels. Semicolon increases, period decreases. Enter returns");break;case 8:page=Page::MediaVolume;say("Media volume "+String(mediaVolume)+" percent. Semicolon increases, period decreases. Enter returns");break;case 10:announceFlashSpace();break;case 11:announceSDSpace();break;}return;}
}
}
void setup(){
 Serial.setRxBufferSize(usbSDBurstSize);Serial.begin(115200);Serial.setTxTimeoutMs(0);setCpuFrequencyMhz(240);
 usbReady=startUnifiedUSB();
 startupStage=1;
 pinMode(38,OUTPUT);digitalWrite(38,LOW);pinMode(37,OUTPUT);digitalWrite(37,HIGH);Wire.begin(8,9);
 const uint8_t regs[][2]={{0,0x80},{1,0xb5},{2,0x18},{0x0d,1},{0x12,0},{0x13,0x10},{0x32,0xbf},{0x37,8}};
 for(auto&r:regs){Wire.beginTransmission(0x18);Wire.write(r[0]);Wire.write(r[1]);int result=Wire.endTransmission();if(result!=0){startupError=(int(r[0])<<8)|result;Serial.printf("UF_FAIL codec reg=%u error=%d\n",r[0],result);return;}}
 startupStage=2;keyboardInitialized=M5Cardputer.Keyboard.begin();SPI.begin(40,39,14,12);sdReady=card.begin(12,SPI,25000000);
 startupStage=3;
 speechMutex=xSemaphoreCreateMutex();if(!speechMutex){startupError=ESP_ERR_NO_MEM;Serial.println("UF_FAIL speech lock");return;}
 speechLeg.leg=mixer.add();wavLeg.leg=mixer.add();mp3Leg.leg=mixer.add();if(!speechLeg.leg||!wavLeg.leg||!mp3Leg.leg)return;
#ifdef UNIFIED_PLAYPALACE
 effectLeg.leg=mixer.add();
 if(effectLeg.leg){
  effectLeg.setBuffers(2,256);effectLeg.setStereo(true);
  if(sdReady&&card.exists("/PlayPalace/effects.ppfx"))effectLeg.begin();
 }
#endif
 synthLeg.leg=mixer.add();
 if(synthLeg.leg){
  synthLeg.setBuffers(2,Synth::blockFrames);
  synthLeg.begin();
  synthLeg.onTransmit(renderSynth,nullptr);
 }
 reader.setDevice(&speechLeg);reader.setVoice(voice_en);
 ready=mixer.begin()&&output.active();
 if(ready)ready=resumeSpeech();
 if(!ready){startupError=output.failureCode;Serial.printf("UF_FAIL audio stage=%d code=%d\n",output.failureStage,output.failureCode);return;}
 startupStage=4;
 settings.begin(sdReady);rate=constrain(settings.getInt("rate",175),100,espeakRATE_MAXIMUM);volume=constrain(settings.getInt("volume",65),5,100);mediaVolume=constrain(settings.getInt("mediaVol",75),0,100);micGainDb=constrain(settings.getInt("micGain",0),-24,24);timezoneIndex=constrain(settings.getInt("timezone",0),0,zoneCount-1);applyTimezone();lastKnownTime=strtoll(settings.getString("lastEpoch").c_str(),nullptr,10);
 synthBoost=constrain(settings.getInt("synthBoost",2),0,Synth::maxBoost);
 setMediaGain();
 {SpeechLock lock;
  // Reduce level before eSpeak generates PCM, where clipping can already
  // occur. The separate reader volume still controls the mixer input.
  espeak_SetParameter(espeakVOLUME,65,0);
  reader.setRate(rate);reader.setGain(volume*0.007f);
 }
 analogRead(10);analogSetPinAttenuation(10,ADC_11db);
 startupStage=5;Serial.printf("UF_READY keyboard=%d sd=%d heap=%u reset=%d\n",keyboardInitialized,sdReady,ESP.getFreeHeap(),int(esp_reset_reason()));
 say("Development firmware. Home. Keyboard learning, 1 of "+String(homeCount));
}
void loop(){
 M5Cardputer.Keyboard.update();auto&s=M5Cardputer.Keyboard.keysState();
 if(M5Cardputer.Keyboard.quitRequested()){
  if(page==Page::UsbSD)leaveUsbSD();else homePage();
 }else for(auto e:M5Cardputer.Keyboard.events())handle(e,s);
 if(page==Page::Synth){
  SpeechLock lock;
  synthSustain=s.ctrl;synthFlange=s.shift;synthTremolo=s.fn;
  synthArpeggio=s.opt&&!s.alt;
  synthGain=mediaVolume/100.0f;synthRenderBoost=synthBoost;
 }
 static char heldArrow=0;
 static Page heldArrowPage=Page::Home;
 static uint32_t nextArrowRepeat=0;
 char arrow=0;
 if(!s.ctrl&&!s.alt&&!s.opt&&!s.shift){
  if(menuCount()>1){
   if(M5Cardputer.Keyboard.isKeyPressed(';'))arrow=';';
   else if(M5Cardputer.Keyboard.isKeyPressed('.'))arrow='.';
  }
#ifdef UNIFIED_PLAYPALACE
  else if(page==Page::PlayPalace&&playpalace::canRepeatMenuArrow()){
   for(char direction : {';', '.', ',', '/'})
    if(M5Cardputer.Keyboard.isKeyPressed(direction)){arrow=direction;break;}
  }
#endif
 }
 if(arrow!=heldArrow||page!=heldArrowPage){
  heldArrow=arrow;
  heldArrowPage=page;
  nextArrowRepeat=millis()+230;
 }else if(arrow&&int32_t(millis()-nextArrowRepeat)>=0){
  nextArrowRepeat=millis()+85;
#ifdef UNIFIED_PLAYPALACE
  if(page==Page::PlayPalace)playpalace::key({arrow,arrow,0,s.fn,false,false,false},true);
  else
#endif
   moveMenu(arrow,true);
 }
#if defined(UNIFIED_DIAGNOSTICS) || defined(UNIFIED_PLAYPALACE)
 static char diagnostic[48];static size_t used=0;
 while(page!=Page::UsbSD&&Serial.available()){
  int ch=Serial.read();
  if(ch=='\n'){
   diagnostic[used]=0;
#ifdef UNIFIED_DIAGNOSTICS
   // Development builds allow USB key injection without logging the value.
   unsigned row,col,flags;
   if(sscanf(diagnostic,"K %u %u %u",&row,&col,&flags)==3&&row<4&&col<14){
    HeadlessKeyboard::State state{};state.opt=flags&1;state.fn=flags&2;state.alt=flags&4;state.ctrl=flags&8;state.shift=flags&16;
    handle({uint8_t(row),uint8_t(col),true},state);
   }else
#endif
   if(!strcmp(diagnostic,"STATUS"))Serial.printf("UF_STATUS page=%d ready=%d sd=%d keyboard=%d startup=%d error=%d audio_stage=%d audio_error=%d reset=%d heap=%u min=%u largest=%u recording=%d bytes=%u wifi=%d\n",int(page),ready,sdReady,keyboardInitialized,startupStage,startupError,output.failureStage,output.failureCode,int(esp_reset_reason()),ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),recordingActive,recordedBytes,WiFi.status());
#ifdef UNIFIED_PLAYPALACE
   else if(!strcmp(diagnostic,"PPSTATUS"))playpalace::status();
   else if(!strcmp(diagnostic,"PPMEM"))playpalace::memoryStatus();
   else if(!strcmp(diagnostic,"FXSTATUS"))playpalace::sound::status();
#endif
   memset(diagnostic,0,sizeof(diagnostic));used=0;
  }else if(ch!='\r'&&used+1<sizeof(diagnostic))diagnostic[used++]=char(ch);
 }
#endif
 String pairedPeer;
 bool bleConnected=pollKeyboardSlot(pairedPeer);
 if(!speechLoaded&&!keyboardRadioRunning()&&(page==Page::Home||(page==Page::Hid&&hidUsb))){
  if(resumeSpeech())say(page==Page::Home?"Home. Keyboard learning, 1 of "+String(homeCount):"USB keyboard selected");
 }
 if(page==Page::Hid){
  if(hidChord>0&&hidChord<4&&!hidChordHandled&&s.alt&&millis()-hidChordStarted>=650){
   hidChordHandled=true;selectHid(hidChord,true);
  }
  if(pendingHidSlot&&!speaking){
   int request=pendingHidSlot;pendingHidSlot=0;
   stopWifi();
   suspendSpeech();
   int batteryMv;
   setHidBatteryLevel(readBatteryPercent(batteryMv));
   Serial.printf("UF_BLE start heap=%u largest=%u\n",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
   const int slot=abs(request)-1;
   const String otherPeers[]{settings.getString(slotKeys[(slot+1)%3]),settings.getString(slotKeys[(slot+2)%3])};
   const auto result=startKeyboardSlot(slot+1,settings.getString(slotKeys[slot]),request<0,otherPeers);
   Serial.printf("UF_BLE result=%d heap=%u\n",int(result),ESP.getFreeHeap());
   if(result!=KeyboardStartup::Ready)resumeSpeech();
   switch(result){
   case KeyboardStartup::Ready:break;
   case KeyboardStartup::InsufficientMemory:
    say(WiFi.getMode()!=WIFI_OFF?"Bluetooth needs more memory. Turn off Wi-Fi in Settings and try again":"Bluetooth needs more memory. "+String(ESP.getFreeHeap()/1024)+" kilobytes free");
    break;
   case KeyboardStartup::Controller:say("Bluetooth controller could not start");break;
   case KeyboardStartup::Services:say("Bluetooth keyboard services could not start");break;
   case KeyboardStartup::Advertising:say("Bluetooth advertising could not start");break;
   }
  }
  if(!pairedPeer.isEmpty()){
   settings.putString(slotKeys[hidSlot-1],pairedPeer);
   settings.putInt("hidSlot",hidSlot);
  }
  bool connected=hidUsb?unifiedUSBConnected():bleConnected;
  if(connected!=lastConnected){lastConnected=connected;hidTone(connected);}
  if(s.opt&&!hidSuppressed)suppressHid();
  if(hidSuppressed){if(!M5Cardputer.Keyboard.isPressed())hidSuppressed=false;}
  else if(connected&&!s.opt){
   static uint32_t lastMouseReport=0;
   if(hidMouse&&millis()-lastMouseReport>=18){lastMouseReport=millis();sendUnifiedHid(hidUsb,true);}
   else if(!hidMouse&&M5Cardputer.Keyboard.isChange()&&(!s.alt||!s.hid_keys.empty()))sendUnifiedHid(hidUsb,false);
  }
 }
 if(wifiSDEnabled&&page==Page::UsbSD&&wifiSD){
  if(WiFi.status()==WL_CONNECTED){
   if(!wifiSDListening){
    snprintf(wifiSDCode,sizeof(wifiSDCode),"%08lu",static_cast<unsigned long>(esp_random()%100000000));
    if(startWifiSD(wifiSDCode)){wifiSDListening=true;announceWifiSD();}
    else{leaveUsbSD();say("Wi-Fi SD sharing could not start");}
   }else pollWifiSD();
  }else if(wifiSDListening||millis()-wifiSDStarted>20000){
   leaveUsbSD();say("Wi-Fi connection unavailable. SD sharing ended");
  }
 }else if(page==Page::UsbSD){
  bool mounted=unifiedUSBConnected();
  if(usbHadHost&&!mounted)leaveUsbSD();
  else if(mounted)usbHadHost=true;
  if(page==Page::UsbSD)pollUsbSD();
 }
 bool connected=WiFi.status()==WL_CONNECTED;
 if(wifiSaveStarted&&connected&&WiFi.SSID()==pendingSSID){
  const bool saved=settings.putWifi(pendingSSID,pendingPassword);
  wifiSaveStarted=0;pendingSSID="";pendingPassword="";
  say(saved?"Wi-Fi connected. Network saved":"Wi-Fi connected. Settings write failed");
 }else if(wifiSaveStarted&&int32_t(millis()-wifiSaveStarted)>20000){
  wifiSaveStarted=0;pendingSSID="";pendingPassword="";
  say("Could not connect to Wi-Fi. Check the network and password");
 }
 if(connected&&!wifiConnected){
  bool announce=timeConnectStarted!=0;
  timeConnectStarted=0;
  if(page!=Page::PlayPalace||announce||!validTime(time(nullptr)))startTimeSync(announce);
 }
 wifiConnected=connected;
 if(page!=Page::UsbSD&&networkTimeReceived.exchange(false)){
  time_t now=time(nullptr);
  if(!validTime(lastKnownTime)||now-lastKnownTime>=6*3600||now<lastKnownTime||networkTimeAnnounce)rememberTime(now);
  if(networkTimeAnnounce)say("Network time synchronized");
  networkTimeStarted=0;networkTimeAnnounce=false;
 }
 if(networkTimeStarted&&int32_t(millis()-networkTimeStarted)>25000){
  if(networkTimeAnnounce)say("Network time did not respond");
  networkTimeStarted=0;networkTimeAnnounce=false;
 }
 if(timeConnectStarted&&int32_t(millis()-timeConnectStarted)>20000){
  timeConnectStarted=0;
  say("Could not connect to the saved Wi-Fi network");
 }
 if(playing&&!paused){
  size_t space;
  bool done;
  {SpeechLock lock;space=isMp3?mp3->availableForWrite():player->availableForWrite();done=isMp3?mp3->done():player->done();}
  if(audio&&space){
   auto *bytes=reinterpret_cast<uint8_t *>(captureBuffer.get());
   int n=audio.read(bytes,std::min(space,scratchBytes));
   if(n>0){
    size_t accepted;
    {SpeechLock lock;accepted=isMp3?mp3->write(bytes,n):player->write(bytes,n);}
    if(accepted!=size_t(n)){const bool preview=previewPlaying;stopMedia();if(!preview)page=Page::Files;say("Playback stopped because audio data could not be queued");}
   }else audio.close();
  }else if(!audio&&done){
   const bool preview=previewPlaying,recorded=recordingPlayback;
   stopMedia();
   if(!preview)page=Page::Files;
   if(recorded)hidTone(false);else say("Playback finished");
  }
 }
 if(speaking){SpeechLock lock;if(reader.done())speaking=false;}
#ifdef UNIFIED_PLAYPALACE
 playpalace::tick(speaking);
 playpalace::sound::tick(playPalaceAudio,discardPlayPalaceAudio);
#endif
 if(recordingActive&&!recordingPaused){
  size_t count=0;
  auto *bytes=reinterpret_cast<uint8_t *>(captureBuffer.get());
  esp_err_t result=output.readCapture(bytes+captureUsed,scratchBytes-captureUsed,count,5);
  if(captureSkip){
   // Drop the first frames after starting or resuming to keep the cue out.
   const size_t skipped=std::min<size_t>(captureSkip,count);
   count-=skipped;captureSkip-=skipped;
   memmove(bytes+captureUsed,bytes+captureUsed+skipped,count);
   if(skipped)lastCapture=millis();
  }
  captureUsed+=count;
  if(count)lastCapture=millis();
  if(recordedBytes>0x7fffffffu-44-scratchBytes){
   captureUsed=0;finishRecording("WAV file size limit reached");
  }else if(result!=ESP_OK&&result!=ESP_ERR_TIMEOUT){
   Serial.printf("UF_RECORD I2S status=%s\n",esp_err_to_name(result));
   finishRecording("Microphone read failed");
  }else if(captureUsed==scratchBytes&&!writeCapturedFrames())finishRecording("SD write failed");
  else if(millis()-lastCapture>3000)finishRecording("Microphone stopped delivering samples");
  else if(recordingPreview&&recordedBytes>=previewLimit)finishRecording();
  else if(millis()-lastCheckpoint>=1000){
   if(!checkpointRecording())finishRecording("SD sync failed");
   else lastCheckpoint=millis();
  }
 }
 if(page==Page::WifiScan){
  int count=WiFi.scanComplete();
  if(count>=0){for(int i=0;i<count;++i){String n=WiFi.SSID(i);if(n.length()&&std::find(networks.begin(),networks.end(),n)==networks.end())networks.push_back(n);}WiFi.scanDelete();page=Page::WifiList;selection=0;focus();}
  else if(count==WIFI_SCAN_FAILED){page=Page::Settings;selection=0;say("Wi-Fi scan failed");}
 }
 if(page==Page::Copying){
  auto *block=reinterpret_cast<uint8_t *>(captureBuffer.get());
  if(copyDone>=copyTotal){copyIn.close();copyOut.close();copyDestination="";captureBuffer.reset();page=Page::Files;loadDirectory();say("File copied");}
  else {size_t want=std::min(scratchBytes,copyTotal-copyDone);int count=copyIn.read(block,want);if(count<=0||copyOut.write(block,count)!=size_t(count)){cancelCopy();page=Page::Files;loadDirectory();say("Copy failed");}else copyDone+=count;}
 }
 delay(1);
}
