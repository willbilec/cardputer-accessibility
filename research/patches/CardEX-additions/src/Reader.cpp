#include "Reader.h"
#ifdef CARDEX_SPEECH
#include "ReaderChunks.h"
#include <M5Cardputer.h>
#include <BackgroundAudioSpeech.h>
#include <ESP32I2SAudio.h>
#include <libespeak-ng/voice/en.h>
#include <esp_heap_caps.h>
#include <algorithm>
namespace {
class BufferedOutput:public ESP32I2SAudio {
public:
 BufferedOutput():ESP32I2SAudio(41,43,42){}
 bool initialized=false;
 bool begin()override{initialized=ESP32I2SAudio::begin();return initialized;}
 bool setBuffers(size_t n,size_t words,int32_t silence=0)override{
  return ESP32I2SAudio::setBuffers(std::max(size_t(2),n),words,silence);
 }
};
BufferedOutput audio;
BackgroundAudioSpeech speech(audio);
bool ready=false,critical=false;
String focusId,readText,pendingFocus,criticalText;
size_t readOffset=0,readEnd=0,criticalOffset=0;
constexpr size_t chunkSize=280;
size_t speak(const String &text,size_t start=0,bool interrupt=true,bool moreHint=false){
 auto range=ReaderChunks::take(std::string_view(text.c_str(),text.length()),start,chunkSize);
 if(!ready)return range.next;
 if(interrupt)speech.flush();String chunk=text.substring(range.start,range.end);
 if(moreHint&&range.next<text.length())chunk+=". More text. Control Shift Space continues.";
 speech.speak(chunk.c_str());Serial.printf("READER_SAY %s\n",chunk.c_str());
 return range.next;
}
}
namespace Reader {
void begin(){
 const uint8_t regs[][2]={{0,0x80},{1,0xb5},{2,0x18},{0x0d,1},{0x12,0},{0x13,0x10},{0x32,0xbf},{0x37,8}};
 for(auto&r:regs){if(!M5.In_I2C.writeRegister8(0x18,r[0],r[1],400000)){Serial.println("READER_FAIL codec");return;}}
 speech.setVoice(voice_en);ready=speech.begin()&&audio.initialized;speech.setRate(175);speech.setGain(.6f);
 Serial.printf("READER_CONFIG requested_buffers=2 cpu=%u\n",getCpuFrequencyMhz());
 Serial.printf("READER_READY ok=%d heap=%u largest=%u\n",ready,ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
void context(const String &text){if(readText!=text){readText=text;readOffset=0;readEnd=0;}}
void event(const String &text){focusId="";context(text);readOffset=0;pendingFocus="";critical=true;criticalText=text;criticalOffset=speak(criticalText);readEnd=criticalOffset;}
void focus(const String &id,const String &text){
 if(id==focusId)return;focusId=id;context(text);
 if(critical&&ready){pendingFocus=text;return;}
 critical=false;readEnd=speak(text,0,true,true);
}
void repeat(){critical=false;criticalText="";pendingFocus="";readEnd=speak(readText,readOffset,true,true);}
void next(){
 readOffset=readEnd<readText.length()?readEnd:0;
 repeat();
}
void stop(){if(ready)speech.flush();critical=false;criticalText="";pendingFocus="";focusId="";Serial.println("READER_STOP");}
void poll(){
 static uint32_t lastStats=0;
 if(ready&&millis()-lastStats>=5000){lastStats=millis();Serial.printf("READER_AUDIO_STATS time_ms=%u frames=%u underruns=%u heap=%u minimum=%u\n",millis(),audio.frames(),audio.underflows(),ESP.getFreeHeap(),ESP.getMinFreeHeap());}
 if(critical&&ready&&speech.done()){
  if(criticalOffset<criticalText.length()){
   criticalOffset=speak(criticalText,criticalOffset,false);
   if(readText==criticalText)readEnd=criticalOffset;
  }else{
   critical=false;criticalText="";
   if(pendingFocus.length()){String text=pendingFocus;pendingFocus="";readEnd=speak(text,0,false,true);}
  }
 }
}
void progress(int percent,const String &text){
 static int last=-1;static String operation;
 int step=percent/25;if(text!=operation||step!=last){operation=text;last=step;event(text+". "+String(percent)+" percent.");}
}
}
#endif
