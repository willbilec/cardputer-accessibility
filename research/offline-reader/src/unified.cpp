// Work in progress. This image is not yet suitable for installation.
#include <Arduino.h>
#ifdef UNIFIED_SCREEN
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include "unified_screen.h"
#endif
#ifdef UNIFIED_CUSTOM_BUILD
#include "builder_identity.h"
#include "launcher_app.h"
#define UNIFIED_ADD_APP(entry, label) extern const launcher::App entry;
#include "builder_apps.inc"
#undef UNIFIED_ADD_APP
#endif
#include <Wire.h>
#include <Network.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <BackgroundAudioSpeech.h>
#include <BackgroundAudioWAV.h>
#include "bible_catalog.h"
#include <libespeak-ng/sonic.h>
extern "C" void cardputer_sonic_discard(sonicStream stream);
#include "media_decoder.h"
#include "file_playback_formats.h"
#include "music_dynamics.h"
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
#include "global_shortcuts.h"
#include "recording_gain.h"
#include "unified_hid.h"
#include "key_map_manager.h"
#include "usb_sd.h"
#include "settings_store.h"
#include "synth.h"
#include "voice_effects.h"
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_VOICE_CHANGER)
#define CARDPUTER_VOICE_CHANGER
#endif
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_BIBLE_APP)
#define CARDPUTER_BIBLE_APP
#endif
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_SYNTH_APP)
#define CARDPUTER_SYNTH_APP
#endif
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_SD_SHARING)
#define CARDPUTER_SD_SHARING
#endif
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_KEY_LEARNING)
#define CARDPUTER_KEY_LEARNING
#endif
#ifdef CARDPUTER_KEY_LEARNING
#include "key_learning.h"
#endif
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_HELP_MENU)
#define CARDPUTER_HELP_MENU
#endif
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_COMPRESSED_MEDIA)
#define CARDPUTER_COMPRESSED_MEDIA
#endif
#ifdef UNIFIED_NVDA_REMOTE
#include "nvda_remote.h"
extern "C" bool espeak_SetPlaybackTempo(float);
extern "C" float espeak_GetPlaybackTempo();
#endif
#ifdef UNIFIED_PLAYPALACE
#include "playpalace.h"
#include "playpalace_sound.h"
#endif

#ifdef UNIFIED_SCREEN
namespace unifiedScreen {
namespace {
// Use the LCD on SPI3. The SD card uses the Arduino SPI bus on SPI2.
lgfx::LGFX_Device screen;
lgfx::Bus_SPI screenBus;
lgfx::Panel_ST7789 screenPanel;
bool screenReady=false, backlightOn=false;
uint32_t lastInput=0;
int timeoutSeconds=0;
String pendingText;
const char *pendingHelp=nullptr;
constexpr int headerHeight=22, rowHeight=16, footerHeight=15;
constexpr uint32_t background=0x101820, header=0x22364a, textColor=0xf0f6ff;
constexpr uint32_t highlight=0x16639c, accent=0x83d8ff;

void drawIcon(Icon icon,int x,int y,uint32_t color){
 switch(icon){
 case Icon::Folder:
  screen.fillRect(x,y+1,5,3,color);
  screen.drawRoundRect(x,y+3,13,9,2,color);break;
 case Icon::Settings:
  screen.drawCircle(x+6,y+6,4,color);
  screen.drawFastHLine(x,y+6,13,color);
  screen.drawFastVLine(x+6,y,13,color);break;
 case Icon::Radio:
  screen.drawLine(x,y+3,x+6,y+9,color);
  screen.drawLine(x+6,y+9,x+12,y+3,color);
  screen.fillCircle(x+6,y+11,1,color);break;
 case Icon::Book:
  screen.drawRect(x,y+1,13,11,color);
  screen.drawFastVLine(x+6,y+1,11,color);break;
 case Icon::Controls:
  screen.drawRoundRect(x,y+1,13,11,2,color);
  screen.drawFastHLine(x+3,y+8,7,color);break;
 case Icon::Menu:
  for(int i=0;i<3;++i)screen.drawFastHLine(x,y+2+i*4,12,color);
  break;
 }
}

void beginFrame(const char *title,Icon icon){
 screen.startWrite();
 screen.fillScreen(background);
 screen.fillRect(0,0,screen.width(),headerHeight,header);
 drawIcon(icon,7,5,accent);
 screen.setTextColor(textColor);
 screen.setTextWrap(false);
 screen.setCursor(27,3);
 screen.print(title);
}

void drawFooter(const char *help){
 if(!help)return;
 const int y=screen.height()-footerHeight;
 screen.fillRect(0,y,screen.width(),footerHeight,header);
 screen.setFont(&lgfx::fonts::Font0);
 screen.setTextColor(accent);
 screen.setTextWrap(false);
 screen.setCursor(5,y+4);
 screen.print(help);
 screen.setFont(&lgfx::fonts::Font2);
}

void showScreenText(const char *title,const String &message,const char *help=nullptr){
 if(!screenReady)return;
 beginFrame(title,Icon::Controls);
 screen.setClipRect(6,headerHeight+4,screen.width()-12,
                    screen.height()-headerHeight-4-(help?footerHeight:0));
 screen.setTextWrap(true);
 screen.setCursor(6,headerHeight+4);
 screen.print(message);
 screen.clearClipRect();
 drawFooter(help);
 screen.endWrite();
}
}

void menu(const char *title,Icon icon,int count,int selected,MenuLabel label,const char *help){
 if(!screenReady||count<=0||selected<0||selected>=count)return;
 pendingText="";
 pendingHelp=nullptr;
 beginFrame(title,icon);
 const int rows=(screen.height()-headerHeight-(help?footerHeight:0))/rowHeight;
 const int first=std::min(selected/rows*rows,std::max(0,count-rows));
 const int last=std::min(first+rows,count);
 for(int i=first;i<last;++i){
  const int y=headerHeight+(i-first)*rowHeight;
  if(i==selected){
   screen.fillRoundRect(3,y,screen.width()-9,rowHeight,4,highlight);
   screen.fillTriangle(8,y+4,8,y+12,13,y+8,accent);
  }
  screen.setTextColor(textColor);
  screen.setClipRect(18,y,screen.width()-29,rowHeight);
  screen.setCursor(18,y);
  screen.print(label(i));
  screen.clearClipRect();
 }
 if(count>rows){
  const int track=rows*rowHeight-4;
  const int thumb=std::max(5,track*rows/count);
  screen.fillRect(screen.width()-4,headerHeight+2,2,track,header);
  screen.fillRect(screen.width()-4,headerHeight+2+(track-thumb)*first/(count-rows),2,thumb,accent);
 }
 drawFooter(help);
 screen.endWrite();
}

void input(){
 lastInput=millis();
 if(screenReady&&!backlightOn){digitalWrite(38,HIGH);backlightOn=true;}
}

void update(const char *title){
 if(!screenReady)return;
 if(pendingText.length()){
  showScreenText(title,pendingText,pendingHelp);
  pendingText="";
  pendingHelp=nullptr;
 }
 if(backlightOn&&timeoutSeconds&&millis()-lastInput>=uint32_t(timeoutSeconds)*1000){
  digitalWrite(38,LOW);
  backlightOn=false;
 }
}

void beginScreen(){
 auto bus=screenBus.config();
 bus.spi_host=SPI3_HOST;
 bus.spi_mode=0;
 bus.freq_write=40000000;
 bus.pin_sclk=36;
 bus.pin_mosi=35;
 bus.pin_miso=-1;
 bus.pin_dc=34;
 screenBus.config(bus);
 screenPanel.setBus(&screenBus);
 auto panel=screenPanel.config();
 panel.pin_cs=37;
 panel.pin_rst=33;
 panel.panel_width=135;
 panel.panel_height=240;
 panel.offset_x=52;
 panel.offset_y=40;
 panel.readable=false;
 panel.invert=true;
 screenPanel.config(panel);
 screen.setPanel(&screenPanel);
 screenReady=screen.init();
 if(!screenReady)return;
 screen.setRotation(1);
 screen.setFont(&lgfx::fonts::Font2);
 showScreenText("Cardputer ADV","Starting...");
 input();
}
}
#endif
namespace {
fs::SDFS &card=SD;
DuplexAudio output(41,43,42);
// Four 128-frame DMA blocks bound queued audio to about 12 ms at 44.1 kHz.
BackgroundAudioMixer<128> mixer(output,44100);
BackgroundAudioSpeech reader;
// Keep synthesis below its clipping point, then apply the saved volume to PCM.
constexpr int speechSynthesisVolume=65;
constexpr float speechGainPerPercent=0.01f;
#ifdef CARDPUTER_COMPRESSED_MEDIA
constexpr int compressedPeakPercent=60;
#endif
#ifdef UNIFIED_NVDA_REMOTE
constexpr int maxSpeechRate=650;
bool speechBoostEnabled=true;
#else
constexpr int maxSpeechRate=espeakRATE_MAXIMUM;
#endif
int configureSpeechRate(int requested){
 reader.setRate(std::min(requested,espeakRATE_MAXIMUM));
#ifdef UNIFIED_NVDA_REMOTE
 const float tempo=speechBoostEnabled?float(std::max(requested,espeakRATE_MAXIMUM))/espeakRATE_MAXIMUM:1.0f;
 if(!espeak_SetPlaybackTempo(tempo))return espeakRATE_MAXIMUM;
#endif
 return requested;
}
BackgroundAudioWAV *player=nullptr;
#ifdef CARDPUTER_COMPRESSED_MEDIA
std::unique_ptr<MediaDecoder> compressedAudio;
MediaDecoder::Block compressedBlock{};
unsigned compressedRate=0;
size_t compressedRemainder=0;
bool compressedEnded=false,compressedFailed=false;
bool mediaBassCut=true;
bool mediaQuietBoost=true;
MusicDynamics musicDynamics;
int32_t mediaBass=0;
int32_t mediaBassAlpha=0;
int mediaBassMix=128;
bool isMp3=false;
#endif
bool compressedPlaying(){
#ifdef CARDPUTER_COMPRESSED_MEDIA
 return bool(compressedAudio);
#else
 return false;
#endif
}
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
 void cue(bool high,unsigned frames=440){tonePeriod=high?22:44;toneLength=toneLeft=frames;}
private:
 bool started=false;
 unsigned tonePeriod=0,toneLength=0,toneLeft=0;
 void (*callback)(void*)=nullptr;void *context=nullptr;
 static void dispatch(void *p){
  auto *self=static_cast<MixerLeg*>(p);SpeechLock lock;
  if(self->toneLeft){
   while(self->toneLeft&&self->availableForWrite()>=4){
    unsigned i=self->toneLength-self->toneLeft--;
    int ramp=std::min({i,self->toneLeft,44u});
    int16_t sample=(i%self->tonePeriod<self->tonePeriod/2?1:-1)*ramp*35;
    int16_t frame[]={sample,sample};
    self->write(reinterpret_cast<uint8_t *>(frame),sizeof(frame));
   }
  }
  if(self->callback)self->callback(self->context);
 }
} speechLeg,wavLeg;
#ifdef CARDPUTER_COMPRESSED_MEDIA
MixerLeg compressedLeg;
#endif
#if defined(CARDPUTER_SYNTH_APP) || defined(CARDPUTER_VOICE_CHANGER)
MixerLeg synthLeg;
#endif
#ifdef CARDPUTER_SYNTH_APP
struct SynthSession {
 Synth live;
 SynthLoop loop;
};
std::unique_ptr<SynthSession> synth;
Synth::Preset synthPreset=Synth::Preset::Piano;
constexpr const char *synthPresets[]={"Piano","Chiptune","Drums","Bass","Glass bells","Grand piano","NES chip","Analog brass","Electric piano","Drawbar organ"};
constexpr const char *synthHelp="Option 1 piano, 2 chiptune, 3 drums, 4 bass, 5 glass bells, 6 grand piano, 7 NES chip, 8 analog brass, 9 electric piano, 0 drawbar organ. Option Z, X, C, V, or M starts a backing loop for the preset. Option Backspace or Tilde stops it. Play any notes over the loop. Option Function R reads the preset. Hold Control for sustain, Shift flanger, Function tremolo, or Option arpeggio. Option brackets adjust quiet sound boost. Alt Space stops speech. Alt minus and equals adjust synth volume. Alt brackets adjust speech volume. Alt Q goes home.";
int synthBoost=2;
bool synthSustain=false,synthFlange=false,synthTremolo=false,synthArpeggio=false;
float synthGain=0;
int synthRenderBoost=2;
void resetSynth(Synth::Preset preset){
 SpeechLock lock;
 synth->live.reset();
 synth->loop.stop();
 synth->live.preset=synthPreset=preset;
 if(synthLeg.leg)synthLeg.discard();
}
void renderSynth(void *){
 // MixerLeg dispatch holds the audio lock. The mixer task owns the PCM queue.
 if(!synth||(!synth->live.active()&&!synth->loop.active()))return;
 int16_t pcm[Synth::blockFrames*2];
 if(synthLeg.availableForWrite()<int(sizeof(pcm)))return;
 if(synth->live.active())
  synth->live.render(pcm,synthGain,synthSustain,synthFlange,synthTremolo,synthRenderBoost,synthArpeggio);
 else
  std::fill_n(pcm,Synth::blockFrames*2,0);
 synth->loop.mix(pcm,synthGain);
 synthLeg.write(reinterpret_cast<const uint8_t*>(pcm),sizeof(pcm));
}
#endif
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
enum class Page { Home
#ifdef CARDPUTER_KEY_LEARNING
 ,Learn
#endif
 ,Record, Files, FileActions, FileName, ConfirmDelete, Copying, Play, Settings, WifiScan, WifiList, Password, TimeZone, ManualTime, TimePicker, SpeechRate, SpeechVolume, MicGain, MediaVolume, Hid, UsbSDMode, UsbSD, PlayPalace, SettingsCategories, ShortcutPrograms, ShortcutKey, Synth, KeyMap, NvdaRemote, Text, VoiceChanger, Bible, RegisteredApp
#ifdef CARDPUTER_HELP_MENU
 ,Help
#endif
#ifdef UNIFIED_SCREEN
 ,BacklightTimeout
#endif
};
Page page=Page::Home;
bool ready=false,speechLoaded=false,sdReady=false,speaking=false,playing=false,paused=false;
File textFile;
String textChunk;
Page textReturnPage=Page::Files;
#ifdef CARDPUTER_KEY_LEARNING
bool learningFromHelp=false;
#endif
size_t textStart=0,textHistory[64],textHistoryCount=0;
bool lastSensitive=false;
bool usbReady=false,hidUsb=false,hidSuppressed=false,hidMouse=false,lastConnected=false;
bool usbWritable=false,usbHadHost=false;
int hidSlot=0,hidChord=0,pendingHidSlot=0;
uint32_t hidChordStarted=0;
bool hidChordHandled=false;
const char *slotKeys[]={"hidV2Peer1","hidV2Peer2","hidV2Peer3"};
const char *hidModeKeys[]={"hidMode1","hidMode2","hidMode3","hidModeUSB"};
keyboardMap::Map remoteKeyMap;
KeyMapManager keyMapManager;
bool hidAlternate=false, altTapArmed=false, optionTapArmed=false, optionTapPending=false;
bool hidReportPending=false;
constexpr const char *keyMapHelp="Tap a key to hear its assignment. Hold it, or tap it then press Function Space, to change it. Type to search choices. Function comma and slash change category. Function semicolon and period browse results. Enter assigns. Function backtick cancels a choice. Tap Alt to switch layers. Alt Q saves and quits.";
#ifdef UNIFIED_DIAGNOSTICS
int startupStage=0,startupError=0;bool keyboardInitialized=false;
#endif
int rate=175,volume=65,mediaVolume=75,micGainDb=0,selection=0,timezoneIndex=0;
#ifdef UNIFIED_NVDA_REMOTE
void setSpeechBoostEnabled(bool enabled){
 SpeechLock lock;
 speechBoostEnabled=enabled;
 if(speechLoaded&&rate>espeakRATE_MAXIMUM)configureSpeechRate(rate);
}
#endif
String speechRateLabel(){
#ifdef UNIFIED_NVDA_REMOTE
 if(!speechBoostEnabled&&rate>espeakRATE_MAXIMUM)
  return "Speech rate 450 during the network session. Saved rate "+String(rate)+".";
#endif
 return "Speech rate "+String(rate);
}
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
#ifdef UNIFIED_DIAGNOSTICS
uint32_t recordedNonzero=0,recordedClipped=0;int recordedPeak=0;
#endif
bool recordingActive=false,recordingPaused=false,recordingPreview=false;
bool previewPlaying=false,recordingPlayback=false,confirmRecordingDelete=false,previewKeyArmed=false;
constexpr size_t scratchBytes=4096;
constexpr char textHelp[]="Period or Enter reads the next section. Semicolon goes back. Space rereads. Comma starts over. Backspace returns to files.";
#ifdef CARDPUTER_HELP_MENU
constexpr char helpTextHelp[]="Period or Enter reads the next section. Semicolon goes back. Space rereads. Comma starts over. Backspace returns to the Help menu.";
#endif
#ifdef CARDPUTER_COMPRESSED_MEDIA
constexpr size_t compressedMixerFrames=512;
constexpr size_t compressedMixerBuffers=4;
constexpr size_t compressedQueueBytes=compressedMixerBuffers*compressedMixerFrames*4;
#endif
std::unique_ptr<int16_t[]> captureBuffer;
size_t captureUsed=0;
float captureGain=1;
uint32_t wavDataStart=0,wavDataSize=0;
bool wavSeekable=false;
struct Entry { String name; bool dir; };
std::vector<Entry> entries;
#ifdef CARDPUTER_HELP_MENU
std::vector<Entry> helpDocuments;
#endif
std::vector<String> networks;
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_VOICE_RECORDER)
#define CARDPUTER_VOICE_RECORDER
#endif
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(UNIFIED_KEYBOARD_APP)
#define CARDPUTER_KEYBOARD_APP
#endif
const char *home[]={
#ifdef CARDPUTER_VOICE_RECORDER
 "Voice recorder",
#endif
 "File manager"
#ifdef CARDPUTER_KEYBOARD_APP
 ,"Bluetooth and USB keyboard"
#endif
 ,"Settings"
#ifdef CARDPUTER_SD_SHARING
 ,wifiSDEnabled?"Browse SD card over USB or Wi-Fi":"Browse SD card over USB"
#endif
#ifdef UNIFIED_PLAYPALACE
 ,"PlayPalace"
#endif
#ifdef UNIFIED_NVDA_REMOTE
 ,"NVDA Remote"
#endif
#ifdef CARDPUTER_VOICE_CHANGER
 ,"Voice changer"
#endif
#ifdef CARDPUTER_SYNTH_APP
 ,"Synthesizer"
#endif
#ifdef CARDPUTER_BIBLE_APP
 ,"Bible"
#endif
#ifdef CARDPUTER_HELP_MENU
 ,"Help"
#endif
#ifdef UNIFIED_CUSTOM_BUILD
#define UNIFIED_ADD_APP(entry, label) ,label
#include "builder_apps.inc"
#undef UNIFIED_ADD_APP
#endif
};
constexpr int homeCount=sizeof(home)/sizeof(home[0]);
constexpr int recorderHomeIndex=0;
constexpr int filesHomeIndex=0
#ifdef CARDPUTER_VOICE_RECORDER
+1
#endif
;
constexpr int keyboardHomeIndex=filesHomeIndex+1;
constexpr int settingsHomeIndex=keyboardHomeIndex
#ifdef CARDPUTER_KEYBOARD_APP
 +1
#endif
;
constexpr int sdSharingHomeIndex=settingsHomeIndex+1;
constexpr int firstOnlineHomeIndex=sdSharingHomeIndex
#ifdef CARDPUTER_SD_SHARING
+1
#endif
;
constexpr int launchKeysSetting=13;
int homeShortcuts[homeCount]{};
bool homeShortcutsLoaded=false;
String homeShortcutSettingKey(int homeIndex){
 String identity=home[homeIndex];
#ifdef CARDPUTER_SD_SHARING
 if(homeIndex==sdSharingHomeIndex)identity="SD card sharing";
#endif
 uint32_t hash=2166136261u;
 for(size_t i=0;i<identity.length();++i){hash^=uint8_t(identity[i]);hash*=16777619u;}
 return String("lk")+String(hash,HEX);
}
void loadHomeShortcuts(){
 if(homeShortcutsLoaded)return;
 for(int i=0;i<homeCount;++i){
  const String key=homeShortcutSettingKey(i);
  homeShortcuts[i]=settings.getInt(key.c_str(),0);
 }
 homeShortcutsLoaded=true;
}
int homeShortcut(int homeIndex){
 loadHomeShortcuts();
 return homeShortcuts[homeIndex];
}
#ifdef UNIFIED_CUSTOM_BUILD
constexpr const launcher::App *registeredApps[]={nullptr
#define UNIFIED_ADD_APP(entry, label) ,&entry
#include "builder_apps.inc"
#undef UNIFIED_ADD_APP
};
constexpr int firstRegisteredApp=homeCount+1-sizeof(registeredApps)/sizeof(registeredApps[0]);
const launcher::App *activeRegisteredApp=nullptr;
int registeredSelection=0;
#else
constexpr int firstRegisteredApp=homeCount;
#endif
constexpr int afterSynth=firstRegisteredApp
#ifdef CARDPUTER_HELP_MENU
 -1
#endif
#ifdef CARDPUTER_BIBLE_APP
-1
#endif
;
#ifdef CARDPUTER_BIBLE_APP
constexpr int bibleHomeIndex=firstRegisteredApp-1
#ifdef CARDPUTER_HELP_MENU
 -1
#endif
;
#endif
#ifdef CARDPUTER_HELP_MENU
constexpr int helpHomeIndex=firstRegisteredApp-1;
#endif
#ifdef CARDPUTER_SYNTH_APP
constexpr int synthHomeIndex=afterSynth-1;
#endif
#ifdef CARDPUTER_VOICE_CHANGER
constexpr int voiceHomeIndex=afterSynth-1
#ifdef CARDPUTER_SYNTH_APP
-1
#endif
;
#endif
#ifdef UNIFIED_NVDA_REMOTE
constexpr int nvdaHomeIndex=firstOnlineHomeIndex
#ifdef UNIFIED_PLAYPALACE
+1
#endif
;
bool nvdaWifiCleanup=false;
#ifdef UNIFIED_DIAGNOSTICS
struct SpeechBench {
 bool active=false;
 uint32_t started=0,samples=0,empty=0,underruns=0;
 int rate=0;
} speechBench;
#endif
#endif
const char *options[]={"Wi-Fi networks","Disconnect Wi-Fi","Set time from network","Time zone","Type date and time","Choose date and time","Speech rate","Speech volume","Media volume","Microphone gain","Internal flash space","SD card space","Key Map Manager","Program launch keys"
#ifdef UNIFIED_SCREEN
 ,"Backlight timeout"
#endif
};
constexpr struct SettingsCategory { const char *name; int first, count; } categories[]={
 {"Network",0,2},{"Date and time",2,4},{"Audio",6,3},{"Recording",9,1},{"Storage",10,2},{"Keyboard",12,2}
#ifdef UNIFIED_SCREEN
 ,{"Display",14,1}
#endif
};
constexpr int categoryCount=sizeof(categories)/sizeof(categories[0]);
int category=0;
#ifdef UNIFIED_SCREEN
constexpr int backlightSetting=14;
constexpr struct BacklightChoice { int seconds; const char *label; } backlightChoices[]={
 {0,"Always on"},{15,"15 seconds"},{30,"30 seconds"},{60,"1 minute"},{120,"2 minutes"},{300,"5 minutes"}
};
const char *screenTitle(){
 switch(page){
 case Page::Home:return "Home";
#ifdef CARDPUTER_KEY_LEARNING
 case Page::Learn:return "Keyboard learning";
#endif
 case Page::Record:return "Voice recorder";
 case Page::Files:case Page::FileName:case Page::Copying:case Page::ConfirmDelete:return "File manager";
 case Page::FileActions:return "File actions";
 case Page::Play:return "Media player";
 case Page::Text:return "Text reader";
 case Page::SettingsCategories:return "Settings";
 case Page::Settings:return categories[category].name;
 case Page::ShortcutPrograms:return "Program launch keys";
 case Page::ShortcutKey:return "Assign launch key";
 case Page::WifiScan:case Page::WifiList:case Page::Password:return "Wi-Fi";
 case Page::TimeZone:return "Time zone";
 case Page::ManualTime:case Page::TimePicker:return "Set date and time";
 case Page::SpeechRate:return "Speech rate";
 case Page::SpeechVolume:return "Speech volume";
 case Page::MediaVolume:return "Media volume";
 case Page::MicGain:return "Microphone gain";
 case Page::Hid:return "Bluetooth and USB keyboard";
 case Page::UsbSDMode:case Page::UsbSD:return "SD card sharing";
 case Page::PlayPalace:return "PlayPalace";
 case Page::Synth:return "Synthesizer";
 case Page::KeyMap:return "Key Map Manager";
 case Page::NvdaRemote:return "NVDA Remote";
 case Page::VoiceChanger:return "Voice changer";
 case Page::Bible:return "Bible";
#ifdef CARDPUTER_HELP_MENU
  case Page::Help:return "Help";
#endif
 case Page::BacklightTimeout:return "Backlight timeout";
#ifdef UNIFIED_CUSTOM_BUILD
 case Page::RegisteredApp:return home[registeredSelection];
#endif
 default:return "Cardputer ADV";
 }
}
#endif
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
#ifdef UNIFIED_SCREEN
 if(!sensitive&&unifiedScreen::screenReady)unifiedScreen::pendingText=message;
#endif
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
#ifdef UNIFIED_DIAGNOSTICS
 if(page!=Page::UsbSD&&!(page==Page::NvdaRemote&&sensitive)){
  if(sensitive)Serial.println("UF_SAY sensitive character");else Serial.printf("UF_SAY %s\n",message.c_str());
 }
#endif
}
void suspendSpeech(){
 SpeechLock lock;
 if(!speechLoaded)return;
 reader.end();
#ifdef UNIFIED_NVDA_REMOTE
 espeak_SetPlaybackTempo(1.0f);
#endif
 espeak_Terminate();
 speechLoaded=speaking=false;
}
bool resumeSpeech(){
 SpeechLock lock;
 if(speechLoaded)return true;
 speechLoaded=reader.begin();
 if(speechLoaded){
  espeak_SetParameter(espeakVOLUME,speechSynthesisVolume,0);
  rate=configureSpeechRate(rate);
  reader.setGain(volume*speechGainPerPercent);
 }
 return speechLoaded;
}
#ifdef CARDPUTER_BIBLE_APP
sonicStream bibleTempo=nullptr;
bool bibleTempoFlushed=false;
void clearBibleTempo(){
 if(bibleTempo){sonicDestroyStream(bibleTempo);bibleTempo=nullptr;}
 bibleTempoFlushed=false;
}
#endif
void stopMedia(){
#ifdef CARDPUTER_BIBLE_APP
 clearBibleTempo();
#endif
 if(player)captureBuffer.reset();
 playing=paused=false;
#ifdef CARDPUTER_COMPRESSED_MEDIA
 if(compressedAudio){
  compressedAudio.reset();
  {SpeechLock lock;compressedLeg.discard();}
  settings.flushDeferred();
 }
#endif
 if(audio)audio.close();
 SpeechLock lock;
 if(player){player->end();delete player;player=nullptr;}
 if(previewPlaying){card.remove(previewPath);previewPlaying=false;}
 recordingPlayback=false;
}
void setMediaGain(){
 {
  SpeechLock lock;
  if(player)player->setGain(mediaVolume/100.0f);
 }
#ifdef UNIFIED_PLAYPALACE
 playpalace::setMediaVolume(mediaVolume);
#endif
}
void adjustMediaVolume(int delta){
 mediaVolume=constrain(mediaVolume+delta,0,100);
 setMediaGain();
 settings.putInt("mediaVol",mediaVolume,page==Page::PlayPalace||page==Page::UsbSD);
 say("Media volume "+String(mediaVolume)+" percent");
}
void hidTone(bool high);
void stopSpeech();
bool startPlayback(const String &path,size_t offset=0);
#ifdef CARDPUTER_BIBLE_APP
bool closeBible();
constexpr const char* bibleHelp="Control locks or unlocks the keyboard. Minus and Equals adjust media volume. B opens the book list. C opens the chapter list. Hold Up or Down to skim a list. In books, press the first letter or number. In chapters, press a digit to jump, such as 5 for chapter 50. Enter selects. M saves a bookmark. Option M plays the bookmark. V chooses a version. Space plays or pauses. In the player, Up goes to the next chapter and Down goes to the previous chapter. Left and right seek near a quiet gap. Brackets change speed. Backspace goes back. Alt Q goes home.";
#endif

#ifdef CARDPUTER_VOICE_CHANGER
constexpr char voiceHelp[]="Numbers select voices. 1 robot, 2 flanger, 3 deep voice, 4 high voice, 5 echo, 6 tremolo, 7 radio, 8 fuzz, 9 chorus, 0 whisper. Enter starts recording. Speak after the beep. Press any key to stop and play. Space or apostrophe replays in the selected voice. M toggles live monitoring for headphones. Left and right brackets change microphone gain. Option Backspace stops audio. Backspace returns home.";
enum class VoiceMode { Idle, MonitorStarting, Monitoring, RecordStarting, Recording, Playback, Draining };
struct VoiceSession {
 VoiceEffect effect;
 int16_t stereo[VoiceEffect::blockFrames*2]{};
 size_t position=0,captured=0;
 int32_t captureSum=0;
 unsigned captureFrames=0,tail=0;
 uint32_t lastCapture=0,cueStarted=0,drainUntil=0;
 float inputGain=1;
};
VoiceMode voiceMode=VoiceMode::Idle;
VoiceEffect::Preset voicePreset=VoiceEffect::Robot;
std::unique_ptr<VoiceSession> voiceSession;
std::unique_ptr<uint8_t[]> voiceClip;
size_t voiceCapacity=0,voiceLength=0;
constexpr size_t voiceClipDecimation=6;
constexpr size_t voiceClipRate=VoiceEffect::rate/voiceClipDecimation;
#endif

void adjustMicGain(int delta){
 micGainDb=constrain(micGainDb+delta,-24,24);
 settings.putInt("micGain",micGainDb);
 say("Microphone gain "+String(micGainDb)+" decibels");
}

bool enableCaptureCodec(){
 // Enable the ADC and DAC together on the shared ES8311.
 const uint8_t registers[][2]={{0x01,0xbf},{0x0e,0x02},{0x14,0x10},{0x17,0xbf},{0x1c,0x6a}};
 for(const auto &reg:registers){
  Wire.beginTransmission(0x18);Wire.write(reg[0]);Wire.write(reg[1]);
  if(Wire.endTransmission()!=0)return false;
 }
 return true;
}

#ifdef CARDPUTER_VOICE_CHANGER
void stopVoice(){
 if(voiceMode==VoiceMode::Recording||voiceMode==VoiceMode::Monitoring)output.endCapture();
 if(voiceMode==VoiceMode::Recording||voiceMode==VoiceMode::RecordStarting){
  voiceClip.reset();voiceLength=voiceCapacity=0;
 }
 if(voiceSession){
  SpeechLock lock;
  synthLeg.discard();
 }
 voiceSession.reset();
 voiceMode=VoiceMode::Idle;
}

bool prepareVoiceOutput(){
 SpeechLock lock;
 return synthLeg.leg&&synthLeg.begin();
}

void startVoiceCapture(bool monitor){
 stopVoice();
 if(!prepareVoiceOutput()){say("Voice changer audio unavailable");return;}
 voiceSession.reset(new(std::nothrow) VoiceSession);
 if(!voiceSession){stopVoice();say("Not enough memory for the voice changer");return;}
 voiceSession->inputGain=recording::captureGain(micGainDb);
 if(!enableCaptureCodec()){stopVoice();say("Microphone codec unavailable");return;}
 if(monitor){
  voiceMode=VoiceMode::MonitorStarting;
  say(String("Live monitoring. ")+VoiceEffect::names[voicePreset]);
  return;
 }
 voiceMode=VoiceMode::RecordStarting;
 voiceClip.reset();voiceLength=0;
 // Budget the clip after allocating the playback effect and mixer buffers.
 constexpr size_t reserve=16*1024;
 const size_t freeBytes=ESP.getFreeHeap();
 const size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
 const size_t budget=std::min(largest,freeBytes>reserve?freeBytes-reserve:0);
 voiceCapacity=std::min<size_t>(12,budget/voiceClipRate)*voiceClipRate;
 while(voiceCapacity>=4*voiceClipRate){
  voiceClip.reset(new(std::nothrow) uint8_t[voiceCapacity]);
  if(voiceClip)break;
  voiceCapacity-=voiceClipRate;
 }
 if(!voiceClip){stopVoice();say("Not enough memory to record four seconds");return;}
 stopSpeech();
 hidTone(true);
 voiceSession->cueStarted=millis();
}

void beginVoicePlayback(){
 if(!voiceLength){stopVoice();voiceClip.reset();say("No audio recorded");return;}
 stopSpeech();
 voiceMode=VoiceMode::Playback;
 voiceSession->tail=voicePreset==VoiceEffect::Echo?VoiceEffect::rate/2:VoiceEffect::rate/20;
}

void playVoice(){
 if(voiceMode==VoiceMode::RecordStarting){stopVoice();say("No audio recorded");return;}
 output.endCapture();
 voiceMode=VoiceMode::Playback;
 beginVoicePlayback();
}

void replayVoice(){
 if(voiceMode==VoiceMode::Recording||voiceMode==VoiceMode::RecordStarting||!voiceLength)return;
 stopVoice();
 if(!prepareVoiceOutput()){say("Voice changer audio unavailable");return;}
 voiceSession.reset(new(std::nothrow) VoiceSession);
 if(!voiceSession){say("Not enough memory to replay");return;}
 beginVoicePlayback();
}

void updateVoice(){
 if(!voiceSession)return;
 auto &session=*voiceSession;
 const uint32_t now=millis();
 if(voiceMode==VoiceMode::MonitorStarting||voiceMode==VoiceMode::RecordStarting){
  const bool monitor=voiceMode==VoiceMode::MonitorStarting;
  if(monitor){
   if(speaking){session.cueStarted=now;return;}
   if(!session.cueStarted)session.cueStarted=now;
  }
  if(now-session.cueStarted<40)return;
  if(!output.beginCapture()){
   stopVoice();say("Microphone could not start");return;
  }
  voiceMode=monitor?VoiceMode::Monitoring:VoiceMode::Recording;
  session.lastCapture=now;
 }
 if(voiceMode==VoiceMode::Draining){
  if(int32_t(now-session.drainUntil)>=0){stopVoice();say("Playback finished");}
  return;
 }
 const bool monitor=voiceMode==VoiceMode::Monitoring;
 const bool record=voiceMode==VoiceMode::Recording;
 if(!record&&(!monitor||!speaking)){
  SpeechLock lock;
  if(synthLeg.leg->discardPending()||synthLeg.availableForWrite()<int(sizeof(session.stereo)))return;
 }
 if(record||monitor){
  size_t count=0;
  auto *bytes=reinterpret_cast<uint8_t *>(session.stereo);
  const esp_err_t result=output.readCapture(bytes+session.captured,sizeof(session.stereo)-session.captured,count,1);
  if(count)session.lastCapture=now;
  if((result!=ESP_OK&&result!=ESP_ERR_TIMEOUT)||now-session.lastCapture>1000){
   stopVoice();say("Microphone stopped delivering audio");return;
  }
  if(monitor&&speaking)session.cueStarted=now;
  if(monitor&&(speaking||now-session.cueStarted<40)){session.captured=0;return;}
  session.captured+=count;
  if(session.captured<sizeof(session.stereo))return;
  session.captured=0;
  for(auto &sample:session.stereo)sample=recording::applyCaptureGain(sample,session.inputGain);
  if(record){
   for(size_t i=0;i<VoiceEffect::blockFrames&&voiceLength<voiceCapacity;++i){
    session.captureSum+=(int(session.stereo[2*i])+session.stereo[2*i+1])/2;
    if(++session.captureFrames==voiceClipDecimation){
     voiceClip[voiceLength++]=voiceClipCodec::encode(session.captureSum/int(voiceClipDecimation));
     session.captureFrames=0;session.captureSum=0;
    }
   }
   if(voiceLength==voiceCapacity)playVoice();
   return;
  }
 }else{
  for(size_t i=0;i<VoiceEffect::blockFrames;++i){
   const size_t index=session.position/voiceClipDecimation;
   int sample=0;
   if(index<voiceLength){
    const int first=voiceClipCodec::decode(voiceClip[index]);
    const int next=voiceClipCodec::decode(voiceClip[std::min(index+1,voiceLength-1)]);
    sample=first+(next-first)*int(session.position%voiceClipDecimation)/int(voiceClipDecimation);
   }else if(session.tail)--session.tail;
   session.stereo[2*i]=session.stereo[2*i+1]=sample;
   ++session.position;
  }
 }
 session.effect.render(voicePreset,session.stereo,mediaVolume*(monitor?0.0035f:0.008f));
 {
  SpeechLock lock;
  synthLeg.write(reinterpret_cast<const uint8_t *>(session.stereo),sizeof(session.stereo));
 }
 if(!monitor&&!session.tail){
  voiceMode=VoiceMode::Draining;
  session.drainUntil=now+45;
 }
}

#endif
#ifdef CARDPUTER_VOICE_RECORDER
bool recorderCardReady(){
 // A mounted SD object can outlive removal of the physical card.
 uint8_t sector[512];
 if(sdReady&&card.readRAW(sector,0))return true;
 lastRecordingPath="";
 card.end();
 sdReady=card.begin(12,SPI,25000000);
 settings.attachSD(sdReady);
 homeShortcutsLoaded=false;
 return sdReady;
}

bool writeCapturedFrames(){
 size_t frames=captureUsed/4;
 for(size_t i=0;i<frames*2;++i){
  int16_t sample=recording::applyCaptureGain(captureBuffer[i],captureGain);
  captureBuffer[i]=sample;
#ifdef UNIFIED_DIAGNOSTICS
  int magnitude=abs(int(sample));
  recordedNonzero+=magnitude!=0;
  recordedClipped+=magnitude>=32760;
  recordedPeak=std::max(recordedPeak,magnitude);
#endif
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
#ifdef UNIFIED_DIAGNOSTICS
 Serial.printf("UF_RECORD stopped bytes=%u nonzero=%u clipped=%u peak=%d header_ok=%d\n",recordedBytes,recordedNonzero,recordedClipped,recordedPeak,ok);
#endif
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
 if(!enableCaptureCodec()){failStart("Microphone codec unavailable");return;}
 stopSpeech();
 if(!output.beginCapture()){failStart("Microphone could not start");return;}
 captureBuffer=std::move(buffer);
 captureUsed=0;captureGain=recording::captureGain(micGainDb);
#ifdef UNIFIED_DIAGNOSTICS
 recordedNonzero=recordedClipped=0;recordedPeak=0;
#endif
 lastCheckpoint=lastCapture=millis();
 recordingActive=true;recordingPreview=preview;
 captureSkip=recordingBytesPerSecond/10;
 hidTone(true);
#ifdef UNIFIED_DIAGNOSTICS
 Serial.printf("UF_RECORD started path=%s\n",recordingPath.c_str());
#endif
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
#endif
int menuCount(){
 switch(page){
 case Page::Home:return homeCount;
 case Page::SettingsCategories:return categoryCount;
 case Page::Settings:return categories[category].count;
 case Page::ShortcutPrograms:return homeCount;
 case Page::TimeZone:return zoneCount;
 case Page::FileActions:return sizeof(fileActions)/sizeof(fileActions[0]);
  case Page::Files:return int(entries.size());
#ifdef CARDPUTER_HELP_MENU
  case Page::Help:return int(helpDocuments.size())
#ifdef CARDPUTER_KEY_LEARNING
   +1
#endif
   ;
#endif
 case Page::WifiList:return int(networks.size());
#ifdef UNIFIED_SCREEN
 case Page::BacklightTimeout:return std::size(backlightChoices);
#endif
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
#ifdef UNIFIED_SCREEN
String menuLabel(int index){
 switch(page){
 case Page::Home:return home[index];
 case Page::SettingsCategories:return categories[index].name;
 case Page::Settings:return options[categories[category].first+index];
 case Page::ShortcutPrograms:{
  const int assigned=homeShortcut(index);
  return String(home[index])+(assigned?String(", key ")+String(char(assigned)):String(", no key assigned"));
 }
 case Page::TimeZone:return zoneNames[index];
 case Page::FileActions:return fileActions[index];
  case Page::Files:
   return entries[index].name+(entries[index].dir?"/":"");
#ifdef CARDPUTER_HELP_MENU
  case Page::Help:
#ifdef CARDPUTER_KEY_LEARNING
   if(index==0)return "Keyboard learning";
   --index;
#endif
   return helpDocuments[index].name;
#endif
 case Page::WifiList:return networks[index];
 case Page::BacklightTimeout:return backlightChoices[index].label;
 default:return "";
 }
}

void drawMenu(const char *help=nullptr){
 using unifiedScreen::Icon;
 Icon icon=Icon::Menu;
 switch(page){
 case Page::SettingsCategories:case Page::Settings:case Page::ShortcutPrograms:case Page::BacklightTimeout:case Page::TimeZone:
  icon=Icon::Settings;break;
 case Page::Files:case Page::FileActions:icon=Icon::Folder;break;
 case Page::WifiList:icon=Icon::Radio;break;
 default:break;
 }
 const int first=page==Page::Settings?categories[category].first:0;
 unifiedScreen::menu(screenTitle(),icon,menuCount(),selection-first,menuLabel,help);
}

const char *appOpeningHelp(Page opened){
 switch(opened){
#ifdef CARDPUTER_KEY_LEARNING
 case Page::Learn:return "Press any key to identify it   Alt+Q Home";
#endif
 case Page::Record:return "Enter Record   Space Pause   Ctrl Preview";
 case Page::Hid:return "Alt Layer   Opt+1-4 Slot   Opt+M Mouse";
 case Page::Play:return "Space Pause   ; . Volume   Backspace Files";
 case Page::Text:return "Enter Next   Space Reread   Alt+H Help";
 case Page::KeyMap:return "Tap a key   Fn+Space Assign   Alt Layer";
 case Page::Synth:return "Play keys   Opt+0-9 Preset   Alt+H Help";
 case Page::VoiceChanger:return "1-0 Voice   Enter Record   Space Replay";
 case Page::Bible:return "B Books   C Chapters   Space Play   Alt+H";
 case Page::PlayPalace:return "; . Move   Enter Select   Alt+Q Home";
 case Page::RegisteredApp:return "Alt+Q Home";
 default:return nullptr;
 }
}

void drawKeyMapChoices(){
 if(unifiedScreen::pendingText.isEmpty())return;
 unifiedScreen::menu("Choose a key assignment",unifiedScreen::Icon::Controls,
   keyMapManager.choiceCount(),keyMapManager.selectedChoice(),
   [](int index)->String{return keyMapManager.choiceLabel(index);},
   "Type to search   Fn+; / Fn+. Move");
}
#endif
void focus(const String &prefix) {
 switch(page){
 case Page::Home:say(String(home[selection])+", "+String(selection+1)+" of "+String(homeCount));break;
 case Page::SettingsCategories:say(prefix+categories[selection].name+", "+String(selection+1)+" of "+String(categoryCount));break;
 case Page::Settings:say(String(options[selection])+", "+String(selection-categories[category].first+1)+" of "+String(categories[category].count));break;
 case Page::ShortcutPrograms:{
  const int assigned=homeShortcut(selection);
  say(String(home[selection])+(assigned?String(", assigned to ")+String(keys::name(assigned).c_str()):String(", no launch key assigned"))+
      ", "+String(selection+1)+" of "+String(homeCount));
  break;
 }
 case Page::TimeZone:say(String(zoneNames[selection])+", "+String(selection+1)+" of "+String(zoneCount));break;
 case Page::FileActions:say(String(fileActions[selection])+", "+String(selection+1)+" of 7");break;
  case Page::Files:
   if(selection<int(entries.size())) {auto &e=entries[selection];say(e.name+(e.dir?", folder, ":", file, ")+String(selection+1)+" of "+String(entries.size()));}
  else say(prefix+"Folder is empty");
  break;
#ifdef CARDPUTER_HELP_MENU
  case Page::Help:
#ifdef CARDPUTER_KEY_LEARNING
   if(selection==0)say("Keyboard learning, 1 of "+String(menuCount()));
   else say(helpDocuments[selection-1].name+", "+String(selection+1)+" of "+String(menuCount()));
#else
   say(helpDocuments[selection].name+", "+String(selection+1)+" of "+String(menuCount()));
#endif
   break;
#endif
 case Page::WifiList:
  if(networks.empty())say("No networks found");
  else{
   String network=networks[selection];
   String state=WiFi.status()==WL_CONNECTED&&WiFi.SSID()==network?", connected":settings.getString("wifiSsid")==network?", saved":"";
   say(network+state+", "+String(selection+1)+" of "+String(networks.size()));
  }
  break;
#ifdef UNIFIED_SCREEN
 case Page::BacklightTimeout:say(backlightChoices[selection].label);break;
#endif
 default:break;
 }
#ifdef UNIFIED_SCREEN
 drawMenu(page==Page::BacklightTimeout?"; . Move  Enter Save  Backspace Back":
          page==Page::ShortcutPrograms?"; . Move  Enter Choose program  Backspace Back":
          page==Page::ShortcutKey?"Press an unmodified key  Backspace Cancel":nullptr);
#endif
}
#ifdef CARDPUTER_KEYBOARD_APP
void suppressHid(){
 if(lastConnected)sendUnifiedHid(hidUsb,hidMouse,remoteKeyMap,hidAlternate,true);
 hidSuppressed=true;optionTapPending=false;hidReportPending=true;
}
void closeHid(){
 if(page!=Page::Hid)return;
 suppressHid();stopKeyboardSlot();hidChord=pendingHidSlot=0;lastConnected=false;
 if(!keyboardRadioRunning())resumeSpeech();
}
#endif
void cancelCopy(){if(copyIn)copyIn.close();if(copyOut)copyOut.close();if(!copyDestination.isEmpty())card.remove(copyDestination);copyDestination="";captureBuffer.reset();}
void homePage(){
 int returnSelection=0;
 bool returnToHelp=false;
#ifdef CARDPUTER_HELP_MENU
 if(page==Page::Help)returnSelection=helpHomeIndex;
#ifdef CARDPUTER_KEY_LEARNING
 if(learningFromHelp){returnSelection=0;returnToHelp=true;learningFromHelp=false;}
#endif
#endif
#ifdef UNIFIED_CUSTOM_BUILD
 if(page==Page::RegisteredApp){
  if(activeRegisteredApp->close)activeRegisteredApp->close();
  activeRegisteredApp=nullptr;
  returnSelection=registeredSelection;
 }
#endif
 bool bibleSaveFailed=false;
#ifdef CARDPUTER_BIBLE_APP
 if(page==Page::Bible){bibleSaveFailed=!closeBible();returnSelection=bibleHomeIndex;}
#endif
#ifdef CARDPUTER_VOICE_CHANGER
 if(page==Page::VoiceChanger){
  stopVoice();voiceClip.reset();voiceLength=voiceCapacity=0;
  returnSelection=voiceHomeIndex;
 }
#endif
#ifdef CARDPUTER_KEYBOARD_APP
 if(page==Page::Hid)returnSelection=keyboardHomeIndex;
#endif
 if(page==Page::Text){textFile.close();textChunk="";}
 if(page==Page::KeyMap&&keyMapManager.changed()){
  if(!settings.putMirroredString("keyboardMap",keyMapManager.map().encode().c_str())){
   say("Key map could not be saved. Press Alt Q to try again.");return;
  }
  remoteKeyMap=keyMapManager.map();
  keyMapManager.saved();
  page=Page::Settings;selection=12;
  say("Key map saved. Keyboard settings.");return;
 }
 if(page==Page::KeyMap){page=Page::Settings;selection=12;focus();return;}
#ifdef CARDPUTER_SYNTH_APP
 if(page==Page::Synth){
  SpeechLock lock;synth.reset();synthLeg.end();synthLeg.discard();returnSelection=synthHomeIndex;
 }
#endif
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace){playpalace::close();returnSelection=firstOnlineHomeIndex;}
#endif
#ifdef UNIFIED_NVDA_REMOTE
 if(page==Page::NvdaRemote){nvdaRemote::close();nvdaWifiCleanup=true;returnSelection=nvdaHomeIndex;}
 else
#endif
 stopWifi();
#ifdef CARDPUTER_KEYBOARD_APP
 closeHid();
#endif
 settings.flushDeferred();stopMedia();
#ifdef CARDPUTER_VOICE_RECORDER
 const bool saved=recordingActive&&!recordingPreview;
 if(recordingActive)finishRecording(nullptr,false);
#else
 constexpr bool saved=false;
#endif
 String notice=bibleSaveFailed?"Could not save Bible position. ":saved?lastSpoken+". ":"";
 if(page==Page::Copying)cancelCopy();
 std::vector<Entry>().swap(entries);std::vector<String>().swap(networks);
 confirmRecordingDelete=previewKeyArmed=false;
 password="";
#ifdef CARDPUTER_HELP_MENU
 page=returnToHelp?Page::Help:Page::Home;
 selection=returnToHelp?0:returnSelection;
 focus(notice+(returnToHelp?"Help. ":"Home. "));
#else
 page=Page::Home;selection=returnSelection;focus(notice+"Home. ");
#endif
}
#ifdef CARDPUTER_SD_SHARING
void leaveUsbSD(){
 if(wifiSDEnabled)stopWifiSD();else stopUsbSD();
 settings.flushDeferred();
 page=Page::Home;selection=sdSharingHomeIndex;
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
#ifdef UNIFIED_SCREEN
void drawSharingMenu(const char *help=nullptr){
 unifiedScreen::menu("SD card sharing",unifiedScreen::Icon::Folder,wifiSDEnabled?3:2,
   selection,[](int index)->String{
    return index==0?"USB read only":index==1?"USB read and write":"Wi-Fi read and write";
   },help);
}
#endif
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
#endif
void hidTone(bool high){
 SpeechLock lock;speechLeg.cue(high);
}
#ifdef CARDPUTER_KEYBOARD_APP
void loadHidMode(int slot){
 const int mode=settings.getInt(hidModeKeys[slot-1],0);
 hidAlternate=mode&1;hidMouse=mode&2;
}
void saveHidMode(){
 settings.putInt(hidModeKeys[(hidUsb?4:hidSlot)-1],int(hidAlternate)|(hidMouse?2:0));
}
void selectHid(int slot,bool pair){
 if(slot==4&&(!usbReady||!unifiedUSBConnected())){say("Connect a USB data host first");return;}
 if(slot!=4&&!pair&&settings.getString(slotKeys[slot-1]).isEmpty()){
  say("Slot "+String(slot)+" is empty. Hold Option and "+String(slot)+" to pair");return;
 }
 suppressHid();stopKeyboardSlot();lastConnected=false;
 loadHidMode(slot);
 pendingHidSlot=0;
 if(slot==4){
  hidUsb=true;
  if(!keyboardRadioRunning())resumeSpeech();
  say(unifiedUSBConnected()?"USB keyboard selected":"USB keyboard selected. Connect a USB data cable");
  return;
 }
 hidUsb=false;hidSlot=slot;
 if(pair){
  // Delay advertising until the spoken instructions have finished.
  say("Pair slot "+String(slot)+". Choose "+keyboardDeviceName(slot)+" on your device");
 }else say("Switching to Bluetooth slot "+String(slot));
 // A positive request is a switch, a negative request opens pairing.
 pendingHidSlot=pair?-slot:slot;
}
#endif
void loadDirectory(){
 entries.clear();selection=0;
 if(!sdReady){say("SD card unavailable");return;}
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
#ifdef CARDPUTER_COMPRESSED_MEDIA
 if(compressedAudio){
  if(!isMp3){say("Seeking is unavailable for Ogg Vorbis");return;}
  size_t pos=audio.position(),step=std::max(size_t(1),size_t(audio.size()/20));
  size_t target=forward?std::min(size_t(audio.size()),pos+step):pos>step?pos-step:0;
  if(target>=audio.size()){say("End of file");return;}
  if(!audio.seek(target)){say("Seek failed");return;}
  compressedAudio->open(audio,MediaDecoder::Format::Mp3);
  {SpeechLock lock;compressedLeg.discard();}
  mediaBass=0;
  musicDynamics.reset(compressedRate,mediaQuietBoost);
  compressedBlock.frames=0;compressedRemainder=0;compressedEnded=compressedFailed=false;
  say(forward?"Skipped forward approximately five percent":"Skipped back approximately five percent");return;
 }
#endif
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
 // Sample across several scheduler ticks so a brief load transient
 // is less likely to affect the entire median window.
 for(size_t i=0;i<std::size(samples);++i){
  if(i)delay(1);
  samples[i]=analogReadMilliVolts(10)*2;
 }
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
  say("USB connected. Battery voltage "+String(mv)+" millivolts");
  return;
 }
 // Above the normal full-cell range, report voltage only. A healthy
 // unplugged cell near 4.2 V can still be reported as approximately full.
 if(mv>4250){
  say("Battery voltage "+String(mv)+" millivolts");
  return;
 }
 String level=percent>0&&percent<3?"under 5":String(((percent+2)/5)*5);
 say("Battery approximately "+level+" percent");
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
bool connectSavedWifi(){
 if(WiFi.status()==WL_CONNECTED)return true;
 const String ssid=settings.getString("wifiSsid");
 if(ssid.isEmpty()||!settings.getBool("wifiOn",true))return false;
 WiFi.persistent(false);
 if(!WiFi.mode(WIFI_STA))return false;
 WiFi.setAutoReconnect(true);
 WiFi.begin(ssid.c_str(),settings.getString("wifiPass").c_str());
 return true;
}
void flushSpeech(){
 SpeechLock lock;
 if(speechLoaded&&speaking)reader.flush();
 speechLeg.discard();
 speaking=false;
}
void stopSpeech(){
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace)playpalace::stopAnnouncements();
#endif
 flushSpeech();
}
bool global(const HeadlessKeyboard::Event &e,const HeadlessKeyboard::State&s){
 using namespace globalShortcuts;
 const Key key{e.row,e.col,e.down,s.opt,s.fn,s.alt,s.ctrl,s.shift};
 Command c=route(key);
 if(c==Command::None)return false;
#ifdef CARDPUTER_KEYBOARD_APP
 if(page==Page::Hid&&c!=Command::Home)suppressHid();
#endif
 switch(c){
 case Command::Home:
#ifdef CARDPUTER_SD_SHARING
  if(page==Page::UsbSD){leaveUsbSD();break;}
#endif
  homePage();break;
 case Command::Battery:announceBattery();break;
 case Command::Time:announceTime();break;
 case Command::Network:announceNetwork();break;
 case Command::Stop:stopSpeech();break;
 case Command::Repeat:{String previous=lastSpoken;bool sensitive=lastSensitive;say(previous,sensitive);break;}
 case Command::Help:
  if(page==Page::Text){
#ifdef CARDPUTER_HELP_MENU
   say(textReturnPage==Page::Help?helpTextHelp:textHelp);
#else
   say(textHelp);
#endif
  }
#ifdef CARDPUTER_SYNTH_APP
  else if(page==Page::Synth)say(synthHelp);
#endif
#ifdef CARDPUTER_BIBLE_APP
  else if(page==Page::Bible)say(bibleHelp);
#endif
#ifdef CARDPUTER_VOICE_CHANGER
  else if(page==Page::VoiceChanger)say(voiceHelp);
#endif
  else if(page==Page::KeyMap)say(keyMapHelp);
  else if(page==Page::ShortcutPrograms)say("Program launch keys. Select any item on the launcher's home screen, including utilities, then press Enter and tap an unmodified printable key. A key can launch one item. Backspace returns");
  else if(page==Page::ShortcutKey)say("Tap an unmodified printable key to assign it. The same key assignment moves from any previous item. Backspace cancels");
  else if(menuCount()||page==Page::WifiList)say("Semicolon moves up, period moves down. Hold to repeat. Comma first, slash last. Space reads the selection. Enter opens. Function backtick goes back. Alt Space stops speech. Alt R repeats. Alt T reads time. Alt Q goes home");
  else say("Alt comma and slash change speech rate. Alt minus and equals change media volume. Alt left and right bracket change speech volume. Alt Space stops speech. Alt R repeats. Alt Q goes home. Alt B battery. Alt T time. Alt N network");
  break;
 case Command::RateDown:case Command::RateUp:
  rate=constrain(rate+(c==Command::RateUp?10:-10),100,maxSpeechRate);{SpeechLock lock;if(speechLoaded)rate=configureSpeechRate(rate);}settings.putInt("rate",rate,page==Page::PlayPalace||page==Page::UsbSD);say(speechRateLabel());break;
 case Command::VolumeUp:case Command::VolumeDown:
  volume=constrain(volume+(c==Command::VolumeUp?5:-5),5,100);{SpeechLock lock;if(speechLoaded)reader.setGain(volume*speechGainPerPercent);}settings.putInt("volume",volume,page==Page::PlayPalace||page==Page::UsbSD);say("Speech volume "+String(volume)+" percent");break;
 case Command::MediaUp:case Command::MediaDown:
  adjustMediaVolume(c==Command::MediaUp?5:-5);break;
 case Command::CombinedVolumeUp:case Command::CombinedVolumeDown:{
  const int delta=c==Command::CombinedVolumeUp?5:-5;
  volume=constrain(volume+delta,5,100);
  mediaVolume=constrain(mediaVolume+delta,0,100);
  {SpeechLock lock;if(speechLoaded)reader.setGain(volume*speechGainPerPercent);}
  setMediaGain();
  settings.putVolumes(volume,mediaVolume,page==Page::PlayPalace||page==Page::UsbSD);
  say("Speech "+String(volume)+" percent. Media "+String(mediaVolume)+" percent");
  break;
 }
 default:break;
 }
 return true;
}
#ifdef CARDPUTER_HELP_MENU
void scanHelpDocuments(){
 helpDocuments.clear();
 File directory=card.open("/Help");
 if(!directory||!directory.isDirectory())return;
 for(File entry=directory.openNextFile();entry;entry=directory.openNextFile()){
  if(entry.isDirectory())continue;
  String name=bible::basename(entry);String lower=name;lower.toLowerCase();
  if(lower.endsWith(".txt"))helpDocuments.push_back({name,false});
 }
 std::sort(helpDocuments.begin(),helpDocuments.end(),[](const Entry&a,const Entry&b){return a.name<b.name;});
}
void openHelpDocument(const String &name){
 const String path=String("/Help/")+name;
 textFile=card.open(path,FILE_READ);
 if(!textFile){say("Could not open help document");return;}
 uint8_t bom[3];
 const bool utf8Bom=textFile.read(bom,sizeof(bom))==3&&!memcmp(bom,"\xef\xbb\xbf",3);
 textStart=utf8Bom?3:0;
 if(!textFile.seek(textStart)){textFile.close();say("Could not read help document");return;}
 textHistoryCount=0;textChunk="";textReturnPage=Page::Help;page=Page::Text;
 say(name+". "+(textFile.available()?String(helpTextHelp):String("Empty help document. Backspace returns to the Help menu.")));
}
#endif
#ifdef CARDPUTER_KEYBOARD_APP
void openKeyboardApp(){
 page=Page::Hid;hidMouse=false;hidSuppressed=true;lastConnected=false;
 std::vector<Entry>().swap(entries);
 std::vector<String>().swap(networks);
 hidUsb=false;altTapArmed=optionTapArmed=optionTapPending=false;hidReportPending=true;
 hidSlot=constrain(settings.getInt("hidSlot",1),1,3);
 loadHidMode(hidSlot);
 const bool paired=!settings.getString(slotKeys[hidSlot-1]).isEmpty();
 if(paired)pendingHidSlot=hidSlot;
 say(String(paired?"Reconnecting Bluetooth slot "+String(hidSlot):String("Bluetooth keyboard"))
     + (hidMouse?". Mouse mode. ":hidAlternate?". Alternate layer. ":". Regular layer. ")
     + "Tap Alt to switch layers. Option 1 through 3 selects a slot. Hold the number to pair. Option 4 selects USB. Option M switches mouse mode. Alt Q returns home");
}
#endif
void openSelected(){
 const int fileIndex=selection;
 if(fileIndex<0||fileIndex>=int(entries.size()))return;
 Entry e=entries[fileIndex];String path=pathFor(e.name);
 if(e.dir){directory=path;loadDirectory();return;}
 String lower=e.name;lower.toLowerCase();
 if(lower.endsWith(".txt")){
  textFile=card.open(path,FILE_READ);
  if(!textFile){say("Could not open text file");return;}
  uint8_t bom[3];
  const bool utf8Bom=textFile.read(bom,sizeof(bom))==3&&!memcmp(bom,"\xef\xbb\xbf",3);
  textStart=utf8Bom?3:0;
  if(!textFile.seek(textStart)){textFile.close();say("Could not read text file");return;}
  textHistoryCount=0;textChunk="";
  textReturnPage=Page::Files;
  page=Page::Text;
  say(e.name+". "+(textFile.available()?String(textHelp):String("Empty text file. Backspace returns to files.")));
  return;
 }
#if defined(UNIFIED_CUSTOM_BUILD) && !defined(UNIFIED_COMPRESSED_FILES)
 if(filePlayback::needsOptionalModule(filePlayback::format(lower.c_str()),false)){
  say("Add the MP3 and Ogg file playback module to play this file.");return;
 }
#endif
 if(!startPlayback(path))return;
 page=Page::Play;
 recordingPlayback=!compressedPlaying()&&path.startsWith("/Recordings/");
 if(recordingPlayback){stopSpeech();hidTone(true);}
 else say("Playing "+e.name+". Space pauses. Semicolon and period adjust media volume."
          +(compressedPlaying()?String(" Option F toggles bass cut. Option L toggles quiet boost."):String())+" Backspace returns to files");
}
bool startPlayback(const String &path,size_t offset){
 if(playing||compressedPlaying()||player)stopMedia();
 String lower=path;lower.toLowerCase();
 const auto fileFormat=filePlayback::format(lower.c_str());
 const bool wav=fileFormat==filePlayback::Format::Wav;
#ifdef CARDPUTER_COMPRESSED_MEDIA
 isMp3=fileFormat==filePlayback::Format::Mp3;
 if(fileFormat==filePlayback::Format::Unknown){
  say("Unsupported file type. Open TXT, WAV, MP3, OGG or AUG");return false;
 }
#ifndef MEDIA_DECODER_VORBIS
 if(!wav&&!isMp3){say("Ogg Vorbis playback is unavailable in this build");return false;}
#endif
#if defined(UNIFIED_CUSTOM_BUILD) && !defined(MEDIA_DECODER_MP3)
 if(isMp3){say("MP3 playback is unavailable in this build");return false;}
#endif
#else
 if(!wav){say("Unsupported file type. Open TXT or WAV");return false;}
#endif
 audio=card.open(path,FILE_READ);if(!audio){say("Could not open file");return false;}
 if(offset && (offset>=audio.size() || !audio.seek(offset))){
  audio.close();say("Could not restore audio position");return false;
 }
 if(wav&&ESP.getFreeHeap()<sizeof(BackgroundAudioWAV)+20000){
  audio.close();say("Not enough memory for playback. Turn off Wi-Fi and try again");return false;
 }
#ifdef CARDPUTER_COMPRESSED_MEDIA
 if(!wav){
  bool started;
  {
   SpeechLock lock;
   started=compressedLeg.begin();
  }
  if(!started){stopMedia();say("Could not start audio output");return false;}
  compressedAudio.reset(new(std::nothrow) MediaDecoder);
  const auto format=isMp3?MediaDecoder::Format::Mp3:MediaDecoder::Format::Vorbis;
  if(!compressedAudio||!compressedAudio->open(audio,format)){
   stopMedia();say("Could not initialize the audio decoder. If Wi-Fi is on, turn it off and try again");return false;
  }
  if(compressedAudio->read(compressedBlock)!=MediaDecoder::Result::Samples){
   stopMedia();say(isMp3?"Could not decode MP3":"Could not decode Ogg Vorbis");return false;
  }
  compressedRate=compressedBlock.rate;compressedRemainder=0;
  compressedEnded=compressedFailed=false;
  mediaBass=0;
  // One-pole low-pass estimate near 180 Hz. Subtract half for a low shelf.
  mediaBassAlpha=1131*256/(compressedRate+1131);
  mediaBassMix=mediaBassCut?128:0;
  musicDynamics.reset(compressedRate,mediaQuietBoost);
  {
   SpeechLock lock;
   compressedLeg.setFrequency(compressedRate);
  }
  playbackPath=path;playing=true;paused=false;return true;
 }
#endif
 inspectWav();
 if(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<sizeof(BackgroundAudioWAV)){
  audio.close();say("Not enough contiguous memory for WAV playback");return false;
 }
 std::unique_ptr<int16_t[]> buffer(new(std::nothrow) int16_t[scratchBytes/sizeof(int16_t)]);
 if(!buffer){audio.close();say("Not enough memory for playback");return false;}
 bool started=false;
 {
  SpeechLock lock;
  player=new(std::nothrow) BackgroundAudioWAV;
  if(player){player->setDevice(&wavLeg);player->setGain(mediaVolume/100.0f);started=player->begin();}
 }
 if(!started){stopMedia();say("Could not start playback");return false;}
 captureBuffer=std::move(buffer);
 playbackPath=path;
 playing=true;paused=false;
 return true;
}
void togglePlaybackPause(){
 paused=!paused;
 if(player){SpeechLock lock;if(paused)player->pause();else player->unpause();}
 if(recordingPlayback)hidTone(!paused);
 else say(paused?"Paused":"Playing");
}
#ifdef CARDPUTER_BIBLE_APP
#include "unified_bible.inc"
#endif

#ifdef CARDPUTER_COMPRESSED_MEDIA
void queueCompressedSamples(const int16_t* samples,size_t frames){
 const size_t bytes=frames*4;
 {SpeechLock lock;compressedLeg.write(reinterpret_cast<const uint8_t*>(samples),bytes);}
 compressedRemainder=(compressedRemainder+bytes)%(compressedMixerFrames*4);
}

bool updateCompressedPlayback(){
 {
  SpeechLock lock;
  if(compressedLeg.leg->discardPending()||
     compressedLeg.availableForWrite()<int(MediaDecoder::blockFrames*4))return false;
 }
#ifdef CARDPUTER_BIBLE_APP
 if(bibleTempo){
  const bool normalSpeed=bibleApp.speed==100;
  if((normalSpeed||(compressedEnded&&!compressedBlock.frames))&&!bibleTempoFlushed){
   bibleTempoFlushed=true;
   if(!sonicFlushStream(bibleTempo))compressedFailed=true;
  }
  int16_t stereo[MediaDecoder::blockFrames*2];
  const int frames=sonicReadShortFromStream(bibleTempo,stereo,MediaDecoder::blockFrames);
  if(frames>0){
   for(int i=frames-1;i>=0;--i)stereo[2*i+1]=stereo[2*i]=stereo[i];
   queueCompressedSamples(stereo,frames);
   return true;
  }
  if(normalSpeed)clearBibleTempo();
 }
#endif
 if(!compressedBlock.frames&&!compressedEnded){
  const auto result=compressedAudio->read(compressedBlock);
  compressedEnded=result!=MediaDecoder::Result::Samples;
  compressedFailed=result==MediaDecoder::Result::Error;
  if(!compressedEnded&&compressedBlock.rate!=compressedRate){
   compressedBlock.frames=0;compressedEnded=compressedFailed=true;
  }
 }
 if(compressedBlock.frames){
#ifdef CARDPUTER_BIBLE_APP
   if(page==Page::Bible && !prepareBibleBlock())return true;
#endif
   // Leave peak headroom for speech and the built-in speaker.
   const int gain=mediaVolume*compressedPeakPercent;
   for(size_t i=0;i<compressedBlock.frames;++i){
    auto* frame=compressedBlock.stereo+2*i;
    int32_t mono=(int32_t(frame[0])+frame[1])/2;
    if(isMp3)mono=mono*100/MediaDecoder::mp3DecodeGainPercent;
    mediaBass+=(mono*256-mediaBass)*mediaBassAlpha/256;
    if(mediaBassMix!=(mediaBassCut?128:0))mediaBassMix+=mediaBassCut?1:-1;
    const int32_t sample=musicDynamics.process(mono-(mediaBass/256)*mediaBassMix/256,
                                               mediaQuietBoost,mediaBassMix||isMp3);
    frame[0]=frame[1]=sample*gain/10000;
   }
#ifdef CARDPUTER_BIBLE_APP
   if(bibleTempo){
    for(size_t i=0;i<compressedBlock.frames;++i)compressedBlock.stereo[i]=compressedBlock.stereo[2*i];
    if(!sonicWriteShortToStream(bibleTempo,compressedBlock.stereo,compressedBlock.frames))
     compressedEnded=compressedFailed=true;
   }else
#endif
   queueCompressedSamples(compressedBlock.stereo,compressedBlock.frames);
   compressedBlock.frames=0;
   return true;
 }
#ifdef CARDPUTER_BIBLE_APP
 if(bibleTempo && !bibleTempoFlushed)return true;
#endif
 {
  SpeechLock lock;
  if(compressedRemainder){
   static const uint8_t silence[compressedMixerFrames*4]{};
   if(compressedLeg.availableForWrite()<int(sizeof(silence)-compressedRemainder))return false;
   compressedLeg.write(silence,sizeof(silence)-compressedRemainder);
   compressedRemainder=0;
  }
  if(compressedLeg.availableForWrite()!=int(compressedQueueBytes))return false;
 }
#ifdef CARDPUTER_BIBLE_APP
 if(page==Page::Bible){finishBibleTrack();return false;}
#endif
 stopMedia();page=Page::Files;
 say(compressedFailed?"Playback stopped because the audio could not be decoded":"Playback finished");
 return false;
}
#endif
void readTextSection(){
 const size_t start=textFile.position();
 if(start>=textFile.size()){say("End of text file");return;}
 textChunk="";
 textChunk.reserve(244);
 unsigned bytes=0;
 while(bytes<240&&textFile.available()){
  int c=textFile.read();
  if(c<0){say("Could not read text file");return;}
  ++bytes;
  if(c=='\r'||c=='\n'){
   if(c=='\r'&&textFile.peek()=='\n')textFile.read();
   break;
  }
  textChunk+=char(c<32||c==127?' ':c);
 }
 // Keep a UTF-8 sequence together when a long line spans sections.
 if(bytes==240){
  for(unsigned i=0;i<3&&(textFile.peek()&0xc0)==0x80;++i)textChunk+=char(textFile.read());
 }
 if(textHistoryCount==std::size(textHistory)){
  memmove(textHistory,textHistory+1,(textHistoryCount-1)*sizeof(textHistory[0]));
  --textHistoryCount;
 }
 textHistory[textHistoryCount++]=start;
 textChunk.trim();
 say(textChunk.isEmpty()?String("Blank line"):textChunk,true);
}
void scanWifi(){
 std::vector<Entry>().swap(entries);
 networks.clear();
 WiFi.persistent(false);
 if(!WiFi.mode(WIFI_STA)){
#ifdef UNIFIED_DIAGNOSTICS
  Serial.printf("UF_WIFI start failed heap=%u largest=%u\n",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
#endif
  say("Wi-Fi radio could not start. "+String(ESP.getFreeHeap()/1024)+" kilobytes free");return;
 }
 page=Page::WifiScan;say("Scanning Wi-Fi networks");
 if(WiFi.scanNetworks(true)==WIFI_SCAN_FAILED){page=Page::Settings;selection=0;say("Wi-Fi scan could not start");}
}
#ifdef CARDPUTER_SYNTH_APP
void handleSynth(const HeadlessKeyboard::Event &e,const HeadlessKeyboard::State &s){
 const unsigned key=e.row*14+e.col;
 if(!e.down){SpeechLock lock;synth->live.noteOff(key);return;}
 if(s.alt)return;
 const bool arrow=(e.row==3&&e.col>=10&&e.col<=12)||(e.row==2&&e.col==11);
 if(s.opt&&!arrow){
  if(s.ctrl||s.shift)return;
  if(s.fn){
   if(e.row==1&&e.col==4)say(synthPresets[int(synthPreset)]);
   return;
  }
  constexpr char loopKeys[]="zxcvm";
  static_assert(sizeof(loopKeys)-1==SynthLoop::count);
  const char *loopKey=std::find(loopKeys,loopKeys+SynthLoop::count,keys::plain[e.row][e.col]);
  if(e.row==0&&(e.col==0||e.col==13)){
   {SpeechLock lock;synth->loop.stop();}
   say("Backing loop stopped.");
   return;
  }
  if(loopKey!=loopKeys+SynthLoop::count){
   const unsigned loop=loopKey-loopKeys;
   {SpeechLock lock;synth->loop.start(synthPreset,loop);}
   say(String(synthPresets[int(synthPreset)])+" backing loop "+String(loop+1));
   return;
  }
  if(e.row==0&&e.col>=1&&e.col<=std::size(synthPresets)){
   resetSynth(Synth::Preset(e.col-1));
   say(synthPresets[e.col-1]);
  }else if(e.row==1&&(e.col==11||e.col==12)){
   synthBoost=constrain(synthBoost+(e.col==12?1:-1),0,Synth::maxBoost);
   settings.putInt("synthBoost",synthBoost,true);
   say(String("Quiet sound boost, ")+String(synthBoost)+" of "+String(Synth::maxBoost));
  }
  return;
 }
 if(!keys::usage[e.row][e.col])return;
 // Contiguous chromatic rows, from Z and Space up to the digit row.
 constexpr int rowNotes[]={75,61,49,38};
 const int firstColumn=e.row==3?3:e.row==2?2:0;
 SpeechLock lock;
 synth->live.noteOn(key,rowNotes[e.row]+e.col-firstColumn);
}
#endif
void handle(const HeadlessKeyboard::Event &e,const HeadlessKeyboard::State &s){
#ifdef UNIFIED_PLAYPALACE
 static bool gameViewTap=false;
 if(page!=Page::PlayPalace)gameViewTap=false;
 else if(e.row*14+e.col==keyboardMap::functionKey){
  if(e.down){
   gameViewTap=true;
   for(int key=0;key<keyboardMap::positions;++key)
    if(key!=keyboardMap::functionKey&&M5Cardputer.Keyboard.keyDown(key/14,key%14))gameViewTap=false;
  }else{
   if(gameViewTap)playpalace::readGameView();
   gameViewTap=false;
  }
  return;
 }else if(e.down)gameViewTap=false;
#endif
#ifdef UNIFIED_CUSTOM_BUILD
 if(page==Page::RegisteredApp){
  if(!global(e,s)&&!s.alt)activeRegisteredApp->key(e,s);
  return;
 }
 if(page==Page::Home&&selection>=firstRegisteredApp&&e.down&&
    e.row==2&&e.col==13&&!s.fn&&!s.opt&&!s.ctrl&&!s.alt){
  static const launcher::Services services{say,homePage,connectSavedWifi,[](){return speaking;},
#ifdef UNIFIED_NVDA_REMOTE
   setSpeechBoostEnabled,
#else
   nullptr,
#endif
   [](){if(!networkTimeStarted)startTimeSync(false);},
   []()->int64_t{return validTime(lastKnownTime)?lastKnownTime:0;}
  };
  activeRegisteredApp=registeredApps[selection-firstRegisteredApp+1];
  registeredSelection=selection;
  page=Page::RegisteredApp;
  if(!activeRegisteredApp->open(services)&&page==Page::RegisteredApp)homePage();
  return;
 }
#endif
#ifdef CARDPUTER_BIBLE_APP
 if(page==Page::Bible){
  if(e.row==3&&e.col==0){
   if(e.down&&!s.alt&&!s.fn&&!s.opt&&!s.shift){
    bibleApp.keyboardLocked=!bibleApp.keyboardLocked;
    say(bibleApp.keyboardLocked?"Keyboard locked":"Keyboard unlocked");
   }
   return;
  }
  if(bibleApp.keyboardLocked)return;
 }
#endif
#ifdef UNIFIED_NVDA_REMOTE
 if(page==Page::NvdaRemote){
  if(globalShortcuts::route({e.row,e.col,e.down,s.opt,s.fn,s.alt,s.ctrl,s.shift})!=globalShortcuts::Command::None){
   nvdaRemote::localShortcut();global(e,s);return;
  }
  nvdaRemote::key(e,s);return;
 }
#endif
 if(page==Page::KeyMap
#ifdef CARDPUTER_KEYBOARD_APP
    ||page==Page::Hid
#endif
 ){
  const int key=e.row*14+e.col;
  if(key==keyboardMap::altKey){
   if(e.down){
    altTapArmed=true;
    for(int other=0;other<keyboardMap::positions;++other)
     if(other!=key&&M5Cardputer.Keyboard.keyDown(other/14,other%14))altTapArmed=false;
   }else if(altTapArmed){
    altTapArmed=false;
    if(page==Page::KeyMap)keyMapManager.toggle(say);
#ifdef CARDPUTER_KEYBOARD_APP
    else{
     suppressHid();hidAlternate=!hidAlternate;saveHidMode();
     if(speechLoaded)say(hidAlternate?"Alternate layer":"Regular layer");
     SpeechLock lock;
     speechLeg.cue(hidAlternate,2200);
    }
#endif
   }
   return;
  }
  if(e.down)altTapArmed=false;
 }
 if(page==Page::KeyMap){
  if(e.down&&global(e,s)){keyMapManager.cancelHold();return;}
  keyMapManager.key(e,s,millis(),say);
#ifdef UNIFIED_SCREEN
  drawKeyMapChoices();
#endif
  return;
 }
#ifdef CARDPUTER_KEYBOARD_APP
 if(page==Page::Hid){
  if(e.row==3&&e.col==1){
   if(e.down){
    optionTapArmed=!hidMouse&&!hidSuppressed&&lastConnected;
    for(int key=0;key<keyboardMap::positions;++key)
     if(key!=keyboardMap::optionKey&&M5Cardputer.Keyboard.keyDown(key/14,key%14))optionTapArmed=false;
   }else{
    optionTapPending=optionTapArmed&&lastConnected&&!hidSuppressed;
    optionTapArmed=false;
   }
   return;
  }
  if(e.down)optionTapArmed=false;
 }
#endif
#ifdef CARDPUTER_VOICE_RECORDER
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
#endif
#ifdef CARDPUTER_SYNTH_APP
 if(page==Page::Synth){
  if(!global(e,s))handleSynth(e,s);
  return;
 }
#endif
#ifdef CARDPUTER_KEYBOARD_APP
 if(page==Page::Hid&&e.row==0&&e.col>=1&&e.col<=4){
  if(e.down&&s.opt&&!s.alt&&!s.fn&&!s.ctrl&&!s.shift){
   suppressHid();hidChord=e.col;hidChordStarted=millis();hidChordHandled=false;
   return;
  }
  if(!e.down&&hidChord==e.col){
   if(!hidChordHandled)selectHid(hidChord,hidChord<4&&millis()-hidChordStarted>=650);
   hidChord=0;return;
  }
 }
#endif
 if(!e.down)return;
 if(global(e,s))return;
 if(s.alt){
#ifdef CARDPUTER_KEYBOARD_APP
  if(page==Page::Hid)suppressHid();
#endif
  return;
 }
#ifdef CARDPUTER_SD_SHARING
 if(page==Page::UsbSD){
  if((s.fn&&e.row==0&&e.col==0)||(!s.fn&&e.row==0&&e.col==13)){
   leaveUsbSD();
  }else if(!s.opt&&e.row==3&&e.col==13){
   if(wifiSDEnabled&&wifiSD)announceWifiSD();
   else say(!unifiedUSBConnected()?"Connect a USB data cable":usbWritable?"SD card shared with write access":"SD card shared read only");
  }else if(e.row==3&&e.col==0)stopSpeech();
  return;
 }
#endif
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace&&e.row==3&&e.col==0){stopSpeech();return;}
#endif
#ifdef CARDPUTER_VOICE_CHANGER
 if(page==Page::VoiceChanger&&(voiceMode==VoiceMode::Recording||voiceMode==VoiceMode::RecordStarting)){
  playVoice();
  return;
 }
#endif
 if(page==Page::ShortcutKey){
  if(!e.down)return;
  if(s.fn&&e.row==0&&e.col==0){page=Page::ShortcutPrograms;focus();return;}
  const int key=keys::plain[e.row][e.col];
  if(!s.fn&&!s.opt&&!s.alt&&!s.ctrl&&!s.shift&&key==8){page=Page::ShortcutPrograms;focus();return;}
  if(!s.fn&&!s.opt&&!s.alt&&!s.ctrl&&!s.shift&&key>=32){
   bool replaced=false;
   for(int i=0;i<homeCount;++i){
    if(i==selection||homeShortcut(i)!=key)continue;
    String oldSetting=homeShortcutSettingKey(i);
    settings.putInt(oldSetting.c_str(),0);
    homeShortcuts[i]=0;
    replaced=true;
   }
   String settingKey=homeShortcutSettingKey(selection);
   settings.putInt(settingKey.c_str(),key);
   homeShortcuts[selection]=key;
   const String keyName=keys::name(key).c_str();
   page=Page::ShortcutPrograms;
   say(String("Launch key ")+keyName+" assigned to "+home[selection]+(replaced?". Any previous assignment for this key was replaced":""));
   focus();
  }
  return;
 }
 if(page==Page::Home&&e.down&&!s.fn&&!s.opt&&!s.alt&&!s.ctrl&&!s.shift){
  const int key=keys::plain[e.row][e.col];
  if(key>=32){
   loadHomeShortcuts();
   for(int i=0;i<homeCount;++i){
    if(homeShortcuts[i]!=key)continue;
    selection=i;
    const HeadlessKeyboard::Event enter{2,13,true};
    const HeadlessKeyboard::State plainState{};
    handle(enter,plainState);
    return;
   }
  }
 }
#ifdef CARDPUTER_BIBLE_APP
 if(page==Page::Bible){handleBible(e,s);return;}
 if(page==Page::Home&&selection==bibleHomeIndex&&e.row==2&&e.col==13&&!s.fn){
  openBible();return;
 }
#endif
#ifdef CARDPUTER_VOICE_CHANGER
 if(page==Page::VoiceChanger){
  const char key=keys::plain[e.row][e.col];
  const bool monitor=voiceMode==VoiceMode::MonitorStarting||voiceMode==VoiceMode::Monitoring;
  if(!s.alt&&!s.fn&&!s.opt&&!s.ctrl&&!s.shift&&(key=='['||key==']')){
   adjustMicGain(key=='['?-3:3);
   if(monitor)voiceSession->inputGain=recording::captureGain(micGainDb);
   return;
  }
  if(s.opt&&!s.fn&&!s.alt&&!s.ctrl&&!s.shift&&key==8){
   stopVoice();say("Stopped");
   return;
  }
  if(s.fn&&e.row==0&&e.col==0){homePage();return;}
  if(s.fn||s.alt||s.opt||s.ctrl||s.shift)return;
  if(key=='h'){say(voiceHelp);return;}
  if(e.row==0&&e.col==13){homePage();return;}
  if(key>='0'&&key<='9'){
   voicePreset=static_cast<VoiceEffect::Preset>(key=='0'?9:key-'1');
   if(monitor)startVoiceCapture(true);
   else {stopVoice();say(VoiceEffect::names[voicePreset]);}
  }else if(key=='m'){
   if(monitor){stopVoice();say("Live monitoring stopped");}
   else startVoiceCapture(true);
  }else if(e.row==2&&e.col==13){
   startVoiceCapture(false);
  }else if(key=='\''||key==' '){
   if(voiceLength)replayVoice();else say("No clip to replay");
  }
  return;
 }
#endif
#ifdef CARDPUTER_KEYBOARD_APP
 if(page==Page::Hid){
  if(s.opt && e.row==3 && e.col==9){
   suppressHid();hidMouse=!hidMouse;saveHidMode();say(hidMouse?"Mouse mode":"Keyboard mode");
  }
  return;
 }
#endif
#ifdef UNIFIED_PLAYPALACE
 if(page==Page::PlayPalace){
  const char base=keys::plain[e.row][e.col];
  const char character=s.shift?keys::shifted[e.row][e.col]:base;
  const int function=s.fn&&e.row==0&&e.col>=1&&e.col<=12?e.col:0;
  if(base>=8&&playpalace::readingText())flushSpeech();
  playpalace::key({base,character,function,s.fn,s.shift,s.ctrl,s.opt});
  return;
 }
 if(page==Page::Home&&selection==firstOnlineHomeIndex&&e.row==2&&e.col==13&&!s.fn){
  if(effectLeg.leg&&sdReady&&card.exists("/PlayPalace/effects.ppfx")){
   SpeechLock lock;effectLeg.begin();
  }
  page=Page::PlayPalace;
  playpalace::open(card,sdReady,say,connectSavedWifi,homePage);
  return;
 }
#endif
#ifdef UNIFIED_NVDA_REMOTE
 if(page==Page::Home&&selection==nvdaHomeIndex&&e.row==2&&e.col==13&&!s.fn){
#ifdef UNIFIED_PLAYPALACE
  if(playpalace::busy()){say("PlayPalace is closing. Press Enter again.");return;}
#endif
  page=Page::NvdaRemote;
  nvdaRemote::open(card,sdReady,say,connectSavedWifi,stopSpeech,setSpeechBoostEnabled);
  return;
 }
#endif
#ifdef CARDPUTER_VOICE_CHANGER
 if(page==Page::Home&&selection==voiceHomeIndex&&e.row==2&&e.col==13&&!s.fn){
  page=Page::VoiceChanger;
  say(String("Voice changer. ")+VoiceEffect::names[voicePreset]+". Numbers select a voice. Enter to record. Space or apostrophe replays. M for headphone monitoring. H for help.");
  return;
 }
#endif
#ifdef CARDPUTER_SYNTH_APP
 if(page==Page::Home&&selection==synthHomeIndex&&e.row==2&&e.col==13&&!s.fn){
  if(!synthLeg.leg){say("Synthesizer audio unavailable");return;}
  {
   SpeechLock lock;
   synth.reset(new(std::nothrow) SynthSession);
   if(synth&&synthLeg.begin()){
    synth->live.preset=synthPreset;
    synthLeg.onTransmit(renderSynth,nullptr);
   }else synth.reset();
  }
  if(!synth){say("Not enough memory for the synthesizer.");return;}
  page=Page::Synth;
  say(String("Synthesizer. ")+synthPresets[int(synthPreset)]+". Play the keys. Option with a digit selects a preset. Option Z starts its first backing loop. Alt H reads controls.");
  return;
 }
#endif
 bool escape=s.fn&&e.row==0&&e.col==0;
 bool backspace=!s.fn&&!s.opt&&!s.alt&&!s.ctrl&&e.row==0&&e.col==13;
#ifdef CARDPUTER_SD_SHARING
 if(page==Page::UsbSDMode){
  if(escape||backspace){page=Page::Home;selection=sdSharingHomeIndex;focus();}
  else if(e.col==11&&(e.row==2||e.row==3)){
   selection=wifiSDEnabled?(selection+(e.row==2?2:1))%3:1-selection;
   say(selection==0?"USB read only":selection==1?"USB read and write":"Wi-Fi read and write");
#ifdef UNIFIED_SCREEN
   drawSharingMenu();
#endif
  }else if(e.row==2&&e.col==13){
   if(wifiSDEnabled&&selection==2)openWifiSD();else openUsbSD(selection==1);
  }
  return;
 }
#endif
 if((escape||backspace)&&page==Page::ShortcutKey){page=Page::ShortcutPrograms;focus();return;}
 if((escape||backspace)&&page==Page::ShortcutPrograms){page=Page::Settings;selection=launchKeysSetting;focus();return;}
 if((escape||backspace)&&page==Page::Settings){page=Page::SettingsCategories;selection=category;focus();return;}
 if((escape||backspace)&&page==Page::SettingsCategories){page=Page::Home;selection=settingsHomeIndex;focus();return;}
#ifdef CARDPUTER_HELP_MENU
 if((escape||backspace)&&page==Page::Help){homePage();return;}
#endif
 int setting=-1;
 switch(page){
 case Page::WifiList:case Page::WifiScan:setting=0;break;
 case Page::TimeZone:setting=3;break;
 case Page::SpeechRate:setting=6;break;
 case Page::SpeechVolume:setting=7;break;
 case Page::MediaVolume:setting=8;break;
 case Page::MicGain:setting=9;break;
#ifdef UNIFIED_SCREEN
 case Page::BacklightTimeout:setting=backlightSetting;break;
#endif
 default:break;
 }
 if((escape||backspace)&&setting>=0){page=Page::Settings;selection=setting;focus();return;}
 if(escape){
   if(page==Page::Text){textFile.close();textChunk="";
#ifdef CARDPUTER_HELP_MENU
    if(textReturnPage==Page::Help){page=Page::Help;focus();return;}
#endif
    page=Page::Files;focus();return;}
  if(page==Page::Password){password="";page=Page::WifiList;selection=0;focus();return;}
  if(page==Page::ManualTime||page==Page::TimePicker){timeInput="";bool typed=page==Page::ManualTime;page=Page::Settings;selection=typed?4:5;focus();return;}
  if(page==Page::FileName||page==Page::ConfirmDelete||page==Page::FileActions){page=Page::Files;loadDirectory();return;}
  homePage();return;
 }
 if(backspace&&page==Page::FileActions){page=Page::Files;loadDirectory();return;}
#ifdef CARDPUTER_KEY_LEARNING
  if(page==Page::Learn){say(keys::describe(e.row,e.col,s.shift,s.fn,s.ctrl,s.alt).c_str());return;}
#endif
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
 if(page==Page::Text){
   if(backspace){textFile.close();textChunk="";page=textReturnPage;focus();return;}
  if(s.fn||s.ctrl||s.alt||s.opt)return;
  const char key=keys::plain[e.row][e.col];
  if(key=='.'||(e.row==2&&e.col==13))readTextSection();
  else if(key==' '){
   if(!textHistoryCount)readTextSection();
   else say(textChunk.isEmpty()?String("Blank line"):textChunk,true);
  }else if(key==';'){
   if(textHistoryCount<2){say("Start of text file");return;}
   if(!textFile.seek(textHistory[textHistoryCount-2])){say("Could not seek in text file");return;}
   textHistoryCount-=2;readTextSection();
  }else if(key==','){
   if(!textFile.seek(textStart)){say("Could not seek in text file");return;}
   textHistoryCount=0;readTextSection();
  }
  return;
 }
 if(page==Page::SpeechRate||page==Page::SpeechVolume||page==Page::MicGain||page==Page::MediaVolume){
  if(e.col==11&&(e.row==2||e.row==3)){
   int delta=e.row==2?1:-1;
   if(page==Page::SpeechRate){rate=constrain(rate+delta*10,100,maxSpeechRate);{SpeechLock lock;rate=configureSpeechRate(rate);}settings.putInt("rate",rate);say("Speech rate "+String(rate));}
   else if(page==Page::SpeechVolume){volume=constrain(volume+delta*5,5,100);{SpeechLock lock;reader.setGain(volume*speechGainPerPercent);}settings.putInt("volume",volume);say("Speech volume "+String(volume)+" percent");}
   else if(page==Page::MicGain)adjustMicGain(delta*3);
   else {mediaVolume=constrain(mediaVolume+delta*5,0,100);setMediaGain();settings.putInt("mediaVol",mediaVolume);say("Media volume "+String(mediaVolume)+" percent");}
  }else if(e.row==2&&e.col==13){int selected=page==Page::SpeechRate?6:page==Page::SpeechVolume?7:page==Page::MicGain?9:8;page=Page::Settings;selection=selected;focus();}
  return;
 }
 if(page==Page::Play){
#ifdef CARDPUTER_COMPRESSED_MEDIA
  if(compressedAudio&&s.opt&&!s.fn&&!s.ctrl&&!s.alt&&!s.shift&&keys::plain[e.row][e.col]=='f'){
   mediaBassCut=!mediaBassCut;
   settings.putBool("mediaBassCut",mediaBassCut);
   say(mediaBassCut?"Music bass cut on":"Music bass cut off");
  }
  else if(compressedAudio&&s.opt&&!s.fn&&!s.ctrl&&!s.alt&&!s.shift&&keys::plain[e.row][e.col]=='l'){
   mediaQuietBoost=!mediaQuietBoost;
   settings.putInt("mediaBoost",mediaQuietBoost,true);
   say(mediaQuietBoost?"Music quiet boost on":"Music quiet boost off");
  }
  else
#endif
  if(e.row==3&&e.col==13){
   togglePlaybackPause();
  }
  else if((e.row==2||e.row==3)&&e.col==11&&!s.fn)adjustMediaVolume(e.row==2?5:-5);
  else if(e.row==3&&(e.col==10||e.col==12))seekAudio(e.col==12);
  else if(backspace){
   const bool deleteRecording=recordingPlayback;
   stopMedia();
   if(deleteRecording){fileTarget=playbackPath;page=Page::ConfirmDelete;say("Delete "+fileTarget+"? Press Enter to confirm or Backspace to cancel");}
   else{page=Page::Files;focus();}
  }return;
 }
#ifdef CARDPUTER_VOICE_RECORDER
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
   else adjustMicGain(e.row==2?3:-3);
  } else if(e.row==3&&e.col==13){
   if(recordingActive){stopSpeech();toggleRecordingPause();}
   else if(previewPlaying)togglePlaybackPause();
   else say("Enter records. Tap Control to record a preview. Alt Q returns home");
  }
  return;
 }
#endif
 if(menuCount()||page==Page::WifiList){
  const char key=keys::plain[e.row][e.col];
  if(key==3){stopSpeech();return;}
  if(!s.ctrl&&!s.alt&&!s.opt&&!s.shift){
   if(key==';'||key=='.'||key==','||key=='/'){moveMenu(key);return;}
   if(key==' '&&!s.fn){focus();return;}
  }
 }
 if(page==Page::Files&&e.row==0&&e.col==13){if(directory!="/"){int p=directory.lastIndexOf('/');directory=p<=0?"/":directory.substring(0,p);loadDirectory();}else homePage();return;}
 if(page==Page::Files&&!s.fn&&!s.ctrl&&!s.alt&&!s.opt&&e.row==3&&e.col==9){
   int index=selection;
  fileTarget=index>=0&&index<int(entries.size())?pathFor(entries[index].name):"";
  page=Page::FileActions;selection=0;focus();return;
 }
 if(e.row!=2||e.col!=13)return;
 if(page==Page::Home){
  switch(selection){
#ifdef CARDPUTER_VOICE_RECORDER
  case recorderHomeIndex:page=Page::Record;say("Voice recorder. Enter starts or saves. Space pauses or resumes. Tap Control to start or play back a preview. Backspace asks to delete. Semicolon and period change microphone gain. Alt Q returns home");break;
#endif
  case filesHomeIndex:page=Page::Files;directory="/";loadDirectory();break;
#ifdef CARDPUTER_KEYBOARD_APP
   case keyboardHomeIndex:openKeyboardApp();break;
#endif
  case settingsHomeIndex:page=Page::SettingsCategories;selection=0;focus("Settings. ");break;
#ifdef CARDPUTER_SD_SHARING
  case sdSharingHomeIndex:
   page=Page::UsbSDMode;selection=0;
   say(wifiSDEnabled?"SD card sharing. USB read only. Press period for USB read and write, then Wi-Fi read and write. Enter starts sharing":"USB SD card. Read only. Period switches to read and write. Enter starts sharing");
#ifdef UNIFIED_SCREEN
   drawSharingMenu("; . Move   Enter Start   Backspace Home");
#endif
   break;
#endif
#ifdef CARDPUTER_HELP_MENU
   case helpHomeIndex:
    scanHelpDocuments();page=Page::Help;selection=0;
    if(menuCount())focus("Help. ");else say("Help documents are not on the SD card. Copy the build sdcard Help folder to the SD card's Help folder");
    break;
#endif
  }
  return;
 }
#ifdef CARDPUTER_HELP_MENU
  if(page==Page::Help){
#ifdef CARDPUTER_KEY_LEARNING
   if(selection==0){learningFromHelp=true;page=Page::Learn;say("Keyboard learning. Press keys to hear their names and functions. Keys are not sent to a computer. Alt Q returns to Help");return;}
   openHelpDocument(helpDocuments[selection-1].name);return;
#else
   if(selection>=0&&selection<int(helpDocuments.size()))openHelpDocument(helpDocuments[selection].name);
   else say("Copy the selected app help files to the SD card Help folder first");
   return;
#endif
  }
#endif
 if(page==Page::Files){openSelected();return;}
 if(page==Page::FileActions){
  int action=selection;
  if(action==6){page=Page::Files;loadDirectory();return;}
  if(action==5){
   card.end();sdReady=card.begin(12,SPI,25000000);settings.attachSD(sdReady);
   homeShortcutsLoaded=false;directory="/";page=Page::Files;
   if(sdReady){loadDirectory();say("SD card ready. Restart to apply settings from the card");}else say("SD card unavailable");
   return;
  }
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
#ifdef UNIFIED_SCREEN
  drawMenu();
#endif
  return;
 }
#ifdef UNIFIED_SCREEN
 if(page==Page::Settings&&selection==backlightSetting){
  page=Page::BacklightTimeout;
  selection=0;
  for(int i=0;i<menuCount();++i)
   if(backlightChoices[i].seconds==unifiedScreen::timeoutSeconds)selection=i;
  focus();
  return;
 }
 if(page==Page::BacklightTimeout){
  const BacklightChoice &choice=backlightChoices[selection];
  unifiedScreen::timeoutSeconds=choice.seconds;
  settings.putInt("backlightSec",choice.seconds);
  page=Page::Settings;
  selection=backlightSetting;
  say(String("Backlight timeout: ")+choice.label);
  drawMenu();
  return;
 }
#endif
 if(page==Page::Settings&&selection==12){
  keyMapManager.open(remoteKeyMap);altTapArmed=false;page=Page::KeyMap;
  say(String("Key Map Manager. Regular layer. ")+keyMapHelp);return;
 }
 if(page==Page::Settings&&selection==launchKeysSetting){
  page=Page::ShortcutPrograms;selection=0;focus("Program launch keys. ");return;
 }
 if(page==Page::ShortcutPrograms){
  page=Page::ShortcutKey;
  say(String("Choose a key to launch ")+home[selection]+". Press any unmodified printable key. Backspace cancels");
#ifdef UNIFIED_SCREEN
  drawMenu("Press a key to assign  Backspace Cancel");
#endif
  return;
 }
 if(page==Page::Settings){switch(selection){case 0:scanWifi();break;case 1:settings.putBool("wifiOn",false);stopWifi();say("Wi-Fi disconnected");break;case 2:startTimeSync(true);break;case 3:page=Page::TimeZone;selection=timezoneIndex;focus();break;case 4:page=Page::ManualTime;timeInput="";say("Enter year, month, day, hour and minute as twelve digits, then Enter");break;case 5:{tm current{};time_t seed=time(nullptr);if(!validTime(seed))seed=lastKnownTime;if(validTime(seed)&&localtime_r(&seed,&current)){timeFields[0]=current.tm_year+1900;timeFields[1]=current.tm_mon+1;timeFields[2]=current.tm_mday;timeFields[3]=current.tm_hour;timeFields[4]=current.tm_min;}timeField=0;page=Page::TimePicker;say("Choose year, month, day, hour and minute. Semicolon increases, period decreases. Enter selects. "+String("Year ")+String(timeFields[0]));break;}case 6:page=Page::SpeechRate;say("Speech rate "+String(rate)+". Semicolon increases, period decreases. Enter returns");break;case 7:page=Page::SpeechVolume;say("Speech volume "+String(volume)+" percent. Semicolon increases, period decreases. Enter returns");break;case 9:page=Page::MicGain;say("Microphone gain "+String(micGainDb)+" decibels. Semicolon increases, period decreases. Enter returns");break;case 8:page=Page::MediaVolume;say("Media volume "+String(mediaVolume)+" percent. Semicolon increases, period decreases. Enter returns");break;case 10:announceFlashSpace();break;case 11:announceSDSpace();break;}return;}
}
}
void setup(){
 Serial.setRxBufferSize(usbSDBurstSize);Serial.begin(115200);Serial.setTxTimeoutMs(0);setCpuFrequencyMhz(240);
 usbReady=startUnifiedUSB();
#ifdef UNIFIED_DIAGNOSTICS
 startupStage=1;
#endif
 pinMode(38,OUTPUT);digitalWrite(38,LOW);pinMode(37,OUTPUT);digitalWrite(37,HIGH);Wire.begin(8,9);
#ifdef UNIFIED_SCREEN
 unifiedScreen::beginScreen();
#endif
 const uint8_t regs[][2]={{0,0x80},{1,0xb5},{2,0x18},{0x0d,1},{0x12,0},{0x13,0x10},{0x32,0xbf},{0x37,8}};
 for(auto&r:regs){
  Wire.beginTransmission(0x18);Wire.write(r[0]);Wire.write(r[1]);
  if(const int result=Wire.endTransmission();result!=0){
#ifdef UNIFIED_DIAGNOSTICS
   startupError=(int(r[0])<<8)|result;
   Serial.printf("UF_FAIL codec reg=%u error=%d\n",r[0],result);
#endif
   return;
  }
 }
#ifdef UNIFIED_DIAGNOSTICS
 startupStage=2;
 keyboardInitialized=M5Cardputer.Keyboard.begin();
#else
 M5Cardputer.Keyboard.begin();
#endif
#ifdef UNIFIED_DIAGNOSTICS
 startupStage=3;
#endif
 speechMutex=xSemaphoreCreateMutex();
 if(!speechMutex){
#ifdef UNIFIED_DIAGNOSTICS
  startupError=ESP_ERR_NO_MEM;Serial.println("UF_FAIL speech lock");
#endif
  return;
 }
 speechLeg.leg=mixer.add();wavLeg.leg=mixer.add();if(!speechLeg.leg||!wavLeg.leg)return;
#ifdef CARDPUTER_COMPRESSED_MEDIA
 compressedLeg.leg=mixer.add();if(!compressedLeg.leg)return;
 compressedLeg.leg->setBuffers(compressedMixerBuffers,compressedMixerFrames);
 compressedLeg.leg->setLinearResampling(true);
#endif
#ifdef UNIFIED_PLAYPALACE
 effectLeg.leg=mixer.add();
 if(effectLeg.leg){
  effectLeg.setBuffers(2,256);effectLeg.setStereo(true);
 }
#endif
#if defined(CARDPUTER_SYNTH_APP) || defined(CARDPUTER_VOICE_CHANGER)
 synthLeg.leg=mixer.add();
 if(synthLeg.leg){
  synthLeg.setBuffers(2,Synth::blockFrames);
 }
#endif
 reader.setDevice(&speechLeg);reader.setVoice(voice_en);
 ready=mixer.begin()&&output.active();
 reader.enablePeakLimiter();
 if(ready)ready=resumeSpeech();
 if(!ready){
#ifdef UNIFIED_DIAGNOSTICS
  startupError=output.failureCode;Serial.printf("UF_FAIL audio stage=%d code=%d\n",output.failureStage,output.failureCode);
#endif
  return;
 }
#ifdef UNIFIED_DIAGNOSTICS
 startupStage=4;
#endif
 // The ES8311 takes its clock from I2S BCLK. Let the audio path settle
 // while the launcher mounts the card and loads the saved speech settings.
 SPI.begin(40,39,14,12);sdReady=card.begin(12,SPI,25000000);
 settings.begin(sdReady);rate=constrain(settings.getInt("rate",175),100,maxSpeechRate);volume=constrain(settings.getInt("volume",65),5,100);mediaVolume=constrain(settings.getInt("mediaVol",75),0,100);micGainDb=constrain(settings.getInt("micGain",0),-24,24);timezoneIndex=constrain(settings.getInt("timezone",0),0,zoneCount-1);applyTimezone();lastKnownTime=strtoll(settings.getString("lastEpoch").c_str(),nullptr,10);
#ifdef UNIFIED_SCREEN
 const int savedTimeout=settings.getInt("backlightSec",0);
 for(const auto &choice:backlightChoices)
  if(choice.seconds==savedTimeout)unifiedScreen::timeoutSeconds=savedTimeout;
 unifiedScreen::input();
#endif
#ifdef CARDPUTER_SYNTH_APP
 synthBoost=constrain(settings.getInt("synthBoost",2),0,Synth::maxBoost);
#endif
#ifdef CARDPUTER_COMPRESSED_MEDIA
 mediaBassCut=settings.getBool("mediaBassCut",true);
 mediaQuietBoost=settings.getInt("mediaBoost",1)!=0;
#endif
 setMediaGain();
 {SpeechLock lock;
  rate=configureSpeechRate(rate);reader.setGain(volume*speechGainPerPercent);
 }
 remoteKeyMap.decode(settings.getString("keyboardMap").c_str());
 analogRead(10);analogSetPinAttenuation(10,ADC_11db);
#ifdef UNIFIED_DIAGNOSTICS
 startupStage=5;Serial.printf("UF_READY keyboard=%d sd=%d heap=%u reset=%d\n",keyboardInitialized,sdReady,ESP.getFreeHeap(),int(esp_reset_reason()));
#endif
 say(String("Cardputer ready. Home. ")+home[0]+", 1 of "+String(homeCount));
#ifdef UNIFIED_SCREEN
 drawMenu();
#endif
}
void loop(){
 static_assert(ARDUINO_RUNNING_CORE == 1, "Keep navigation on the speech mixer core");
 M5Cardputer.Keyboard.update();auto&s=M5Cardputer.Keyboard.keysState();
#ifdef UNIFIED_SCREEN
 if(!M5Cardputer.Keyboard.events().empty())unifiedScreen::input();
#endif
 if(M5Cardputer.Keyboard.quitRequested()
#ifdef CARDPUTER_BIBLE_APP
    &&!(page==Page::Bible&&bibleApp.keyboardLocked)
#endif
 ){
#ifdef UNIFIED_SCREEN
  unifiedScreen::input();
#endif
#ifdef CARDPUTER_SD_SHARING
  if(page==Page::UsbSD)leaveUsbSD();else
#endif
  homePage();
 }else for(auto e:M5Cardputer.Keyboard.events()){
#ifdef UNIFIED_SCREEN
  const Page previous=page;
#endif
  if(e.heldModifiers==s)handle(e,s);
  else{
   // The keyboard controller can deliver both presses and releases at once.
   auto eventState=s;
   static_cast<HeadlessKeyboard::Modifiers &>(eventState)=e.heldModifiers;
   eventState.modifiers=(eventState.ctrl?1:0)|(eventState.shift?2:0)|(eventState.alt?4:0);
   handle(e,eventState);
  }
#ifdef UNIFIED_SCREEN
  if(page!=previous){
   if(previous==Page::Home&&page==Page::Files&&sdReady&&menuCount()>0)
    drawMenu("; . Move   Enter Open   M Actions");
   else {
    if(page==Page::Play&&unifiedScreen::pendingText.isEmpty())
     unifiedScreen::pendingText="Playing recording";
    if(unifiedScreen::pendingText.length())unifiedScreen::pendingHelp=appOpeningHelp(page);
   }
  }
#endif
 }
#ifdef UNIFIED_CUSTOM_BUILD
 if(activeRegisteredApp&&activeRegisteredApp->tick)activeRegisteredApp->tick();
#endif
#ifdef UNIFIED_NVDA_REMOTE
 if(page==Page::NvdaRemote)nvdaRemote::keyboard(remoteKeyMap);
 if(nvdaWifiCleanup&&!nvdaRemote::busy()){
  nvdaWifiCleanup=false;
  if(page==Page::Home)stopWifi();
 }
#endif
 if(page==Page::KeyMap){
#ifdef UNIFIED_SCREEN
  const int choicesBefore=keyMapManager.choiceCount();
#endif
  keyMapManager.tick(M5Cardputer.Keyboard,millis(),say);
#ifdef UNIFIED_SCREEN
  if(!choicesBefore&&keyMapManager.choiceCount())drawKeyMapChoices();
#endif
 }
#ifdef CARDPUTER_SYNTH_APP
 if(page==Page::Synth){
  SpeechLock lock;
  synthSustain=s.ctrl;synthFlange=s.shift;synthTremolo=s.fn;
  synthArpeggio=s.opt&&!s.alt;
  synthGain=mediaVolume/100.0f;synthRenderBoost=synthBoost;
 }
#endif
 static char heldArrow=0;
 static Page heldArrowPage=Page::Home;
 static uint32_t nextArrowRepeat=0;
 char arrow=0;
 if(!s.ctrl&&!s.alt&&!s.opt&&!s.shift){
  if(menuCount()>1
#ifdef CARDPUTER_BIBLE_APP
     ||(page==Page::Bible&&!bibleApp.keyboardLocked&&bibleMenuCount()>1)
#endif
  ){
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
#ifdef CARDPUTER_BIBLE_APP
  if(page==Page::Bible)moveBibleMenu(arrow);
  else
#endif
  moveMenu(arrow,true);
 }
#ifdef UNIFIED_CUSTOM_BUILD
 // The builder identifies the launcher and can hand off to a Help-only session.
 static unsigned identityMatch=0;
 static size_t identitySent=0;
 static bool identityPending=false;
 constexpr char identityCommand[]="UF_ID\n";
 constexpr char identityReply[]="UF_ID " UNIFIED_BUILD_IDENTITY "\n";
#ifdef UNIFIED_BUILDER_HELP_UPLOAD
 static unsigned helpMatch=0;
 constexpr char helpCommand[]="UF_HELP\n";
 if(page!=Page::Home)stopUsbHelp();
 const bool helpTransfer=page==Page::Home&&pollUsbHelp();
#else
 constexpr bool helpTransfer=false;
#endif
 for(unsigned n=0;n<128&&!helpTransfer&&page!=Page::UsbSD&&Serial.available();++n){
  const int ch=Serial.read();
#ifdef UNIFIED_BUILDER_HELP_UPLOAD
  if(ch==helpCommand[helpMatch]){
   if(++helpMatch==sizeof(helpCommand)-1){
    helpMatch=0;
    if(ready&&page==Page::Home){identityMatch=0;identityPending=false;startUsbHelp(sdReady);break;}
   }
  }else helpMatch=ch==helpCommand[0]?1:0;
#endif
  if(ch==identityCommand[identityMatch]){
   if(++identityMatch==sizeof(identityCommand)-1){
    if(ready&&!identityPending){identitySent=0;identityPending=true;}
    identityMatch=0;
   }
  }else identityMatch=ch==identityCommand[0]?1:0;
 }
 if(!helpTransfer&&page!=Page::UsbSD&&identityPending){
  const int space=Serial.availableForWrite();
  if(space>0){
   const size_t bytes=std::min<size_t>(space,sizeof(identityReply)-1-identitySent);
   identitySent+=Serial.write(reinterpret_cast<const uint8_t *>(identityReply+identitySent),bytes);
   if(identitySent==sizeof(identityReply)-1)identityPending=false;
  }
 }
#endif
#ifdef UNIFIED_DIAGNOSTICS
 static char diagnostic[48];static size_t used=0;
 while(page!=Page::UsbSD&&Serial.available()){
  int ch=Serial.read();
  if(ch=='\n'){
   diagnostic[used]=0;
   // Development builds allow USB key injection without logging the value.
   unsigned row,col,flags;
   if(sscanf(diagnostic,"K %u %u %u",&row,&col,&flags)==3&&row<4&&col<14){
    HeadlessKeyboard::State state{};state.opt=flags&1;state.fn=flags&2;state.alt=flags&4;state.ctrl=flags&8;state.shift=flags&16;
    handle({uint8_t(row),uint8_t(col),true},state);
   }else if(!strcmp(diagnostic,"STATUS"))Serial.printf("UF_STATUS page=%d ready=%d sd=%d keyboard=%d startup=%d error=%d audio_stage=%d audio_error=%d reset=%d heap=%u min=%u largest=%u recording=%d bytes=%u wifi=%d\n",int(page),ready,sdReady,keyboardInitialized,startupStage,startupError,output.failureStage,output.failureCode,int(esp_reset_reason()),ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),recordingActive,recordedBytes,WiFi.status());
#ifdef UNIFIED_PLAYPALACE
   else if(!strcmp(diagnostic,"PPSTATUS"))playpalace::status();
   else if(!strcmp(diagnostic,"PPMEM"))playpalace::memoryStatus();
#ifdef UNIFIED_NVDA_REMOTE
   else if(!strcmp(diagnostic,"NVDSTATUS"))nvdaRemote::status();
   else if(!strcmp(diagnostic,"NVDPERF"))nvdaRemote::performance();
   else if(!strcmp(diagnostic,"NVDSCHED"))Serial.printf("NVDA_SCHED network=%d audio=%d controls=%d\n",nvdaRemote::taskCore(),output.taskCore(),xPortGetCoreID());
   else if(!strcmp(diagnostic,"SPEECHRATE")){
    int engineRate=0;
    int tempo=100;
    {SpeechLock lock;if(speechLoaded){engineRate=espeak_GetParameter(espeakRATE,1);tempo=int(espeak_GetPlaybackTempo()*100+0.5f);}}
    Serial.printf("UF_SPEECHRATE setting=%d engine=%d tempo_pct=%d\n",rate,engineRate,tempo);
   }
   else if((!strcmp(diagnostic,"SPEECHBENCH")||!strncmp(diagnostic,"SPEECHBENCH ",12))&&
           page==Page::Home&&speechLoaded&&!speaking){
    const int requested=diagnostic[11]?constrain(atoi(diagnostic+12),100,maxSpeechRate):rate;
    {
     SpeechLock lock;
     speechBench={true,millis(),reader.synthesizedSamples(),reader.emptyFramesWhileSpeaking(),
                  output.underflows(),configureSpeechRate(requested)};
    }
    say("The navigation menu contains several different settings. The speech reader should speak these two sentences quickly and clearly.");
   }
#endif
   else if(!strcmp(diagnostic,"FXSTATUS"))playpalace::sound::status();
#endif
   memset(diagnostic,0,sizeof(diagnostic));used=0;
  }else if(ch!='\r'&&used+1<sizeof(diagnostic))diagnostic[used++]=char(ch);
 }
#endif
#ifdef CARDPUTER_KEYBOARD_APP
 String pairedPeer;
 bool bleConnected=pollKeyboardSlot(pairedPeer);
 if(!speechLoaded&&!keyboardRadioRunning()&&(page==Page::Home||(page==Page::Hid&&hidUsb))){
  if(resumeSpeech()){
   if(page==Page::Home)focus("Home. ");
   else say("USB keyboard selected");
  }
 }
 if(page==Page::Hid){
  if(hidChord>0&&hidChord<4&&!hidChordHandled&&s.opt&&millis()-hidChordStarted>=650){
   hidChordHandled=true;selectHid(hidChord,true);
  }
  if(pendingHidSlot&&!speaking){
   int request=pendingHidSlot;pendingHidSlot=0;
   stopWifi();
   suspendSpeech();
   int batteryMv;
   setHidBatteryLevel(readBatteryPercent(batteryMv));
#ifdef UNIFIED_DIAGNOSTICS
   Serial.printf("UF_BLE start heap=%u largest=%u\n",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
   const int slot=abs(request)-1;
   const String otherPeers[]{settings.getString(slotKeys[(slot+1)%3]),settings.getString(slotKeys[(slot+2)%3])};
   const auto result=startKeyboardSlot(slot+1,settings.getString(slotKeys[slot]),request<0,otherPeers);
#ifdef UNIFIED_DIAGNOSTICS
   Serial.printf("UF_BLE result=%d heap=%u\n",int(result),ESP.getFreeHeap());
#endif
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
   const String previousPeer=settings.getString(slotKeys[hidSlot-1]);
   if(!previousPeer.isEmpty()&&previousPeer!=pairedPeer){
    hidAlternate=hidMouse=false;saveHidMode();
   }
   settings.putString(slotKeys[hidSlot-1],pairedPeer);
  }
  bool connected=hidUsb?unifiedUSBConnected():bleConnected;
  if(connected!=lastConnected){
   lastConnected=connected;hidTone(connected);
   optionTapArmed=optionTapPending=false;
   hidSuppressed=M5Cardputer.Keyboard.isPressed();hidReportPending=true;
   if(connected&&!hidUsb&&settings.getInt("hidSlot",1)!=hidSlot)settings.putInt("hidSlot",hidSlot);
  }
  if(hidSuppressed){if(!M5Cardputer.Keyboard.isPressed())hidSuppressed=false;}
  else if(connected){
   static uint32_t lastMouseReport=0;
   if(hidMouse&&millis()-lastMouseReport>=18){
    lastMouseReport=millis();sendUnifiedHid(hidUsb,true,remoteKeyMap,hidAlternate);
   }else if(!hidMouse){
    hidReportPending|=M5Cardputer.Keyboard.isChange();
    if(optionTapPending){
     if(sendUnifiedHid(hidUsb,false,remoteKeyMap,hidAlternate,false,true)){
      optionTapPending=false;hidReportPending=true;
     }
    }else if(hidReportPending&&sendUnifiedHid(hidUsb,false,remoteKeyMap,hidAlternate))hidReportPending=false;
   }
  }
 }
#else
 if(!speechLoaded&&page==Page::Home&&resumeSpeech())focus("Home. ");
#endif
#ifdef CARDPUTER_SD_SHARING
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
#endif
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
  if((page!=Page::PlayPalace&&page!=Page::NvdaRemote)||announce||!validTime(time(nullptr)))startTimeSync(announce);
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
#ifdef CARDPUTER_COMPRESSED_MEDIA
 if(playing&&!paused&&compressedAudio){
#ifdef CARDPUTER_BIBLE_APP
  if(page==Page::Bible&&bibleApp.seeking){
   // Decoding discarded samples doesn't fill the mixer queue. Use a short
   // time budget instead of waiting a full loop after each batch of samples.
   const uint32_t started=micros();
   while(bibleApp.seeking&&uint32_t(micros()-started)<4000)
    if(!updateCompressedPlayback())break;
  }
#endif
  // Catch up after a slow SD read or a costly codec packet, yielding when full.
  for(size_t queued=0;playing&&!paused&&compressedAudio&&queued<compressedQueueBytes/(MediaDecoder::blockFrames*4);++queued){
   if(!updateCompressedPlayback())break;
  }
 }
 else
#endif
 if(playing&&!paused){
  size_t space;
  bool done;
  {SpeechLock lock;space=player->availableForWrite();done=player->done();}
  if(audio&&space){
   auto *bytes=reinterpret_cast<uint8_t *>(captureBuffer.get());
   int n=audio.read(bytes,std::min(space,scratchBytes));
   if(n>0){
    size_t accepted;
    {SpeechLock lock;accepted=player->write(bytes,n);}
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
 if(page==Page::Home&&ready&&!speaking)loadHomeShortcuts();
#ifdef CARDPUTER_VOICE_CHANGER
 if(page==Page::VoiceChanger)updateVoice();
#endif
#ifdef UNIFIED_NVDA_REMOTE
#ifdef UNIFIED_DIAGNOSTICS
 if(speechBench.active&&page!=Page::Home){
  SpeechLock lock;
  if(speechLoaded)configureSpeechRate(rate);
  speechBench.active=false;
 }
 if(speechBench.active&&!speaking){
  uint32_t samples,empty;
  {
   SpeechLock lock;
   samples=reader.synthesizedSamples()-speechBench.samples;
   empty=reader.emptyFramesWhileSpeaking()-speechBench.empty;
   configureSpeechRate(rate);
  }
  Serial.printf("UF_SPEECHBENCH rate=%d samples=%u ms=%u empty=%u underruns=%u\n",
                speechBench.rate,samples,millis()-speechBench.started,empty,
                output.underflows()-speechBench.underruns);
  speechBench.active=false;
 }
#endif
 nvdaRemote::tick(speaking);
#endif
#ifdef UNIFIED_PLAYPALACE
 if(playpalace::busy()){
  playpalace::tick(speaking);
  if(playpalace::sound::busy())playpalace::sound::tick(playPalaceAudio,discardPlayPalaceAudio);
 }
#endif
#ifdef CARDPUTER_VOICE_RECORDER
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
#ifdef UNIFIED_DIAGNOSTICS
   Serial.printf("UF_RECORD I2S status=%s\n",esp_err_to_name(result));
#endif
   finishRecording("Microphone read failed");
  }else if(captureUsed==scratchBytes&&!writeCapturedFrames())finishRecording("SD write failed");
  else if(millis()-lastCapture>3000)finishRecording("Microphone stopped delivering samples");
  else if(recordingPreview&&recordedBytes>=previewLimit)finishRecording();
  else if(millis()-lastCheckpoint>=1000){
   if(!checkpointRecording())finishRecording("SD sync failed");
   else lastCheckpoint=millis();
  }
 }
#endif
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
#ifdef UNIFIED_SCREEN
 unifiedScreen::update(screenTitle());
#endif
 delay(1);
}
