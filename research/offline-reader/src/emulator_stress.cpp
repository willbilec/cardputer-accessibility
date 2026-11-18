// Reuse the real decoder, paced sinks and metrics of the original workload.
#define BENCH_EXTERNAL_MAIN
#include "emulator_workload.cpp"

static uint32_t stressStart, nextAction, nextCheckpoint;
static unsigned actions, stops, repeats, stressPhase, warmHeap, warmLargest;
static bool stressReady=false, finished=false;

void setup() {
    Serial.begin(115200); setCpuFrequencyMhz(240); Serial.setTimeout(100);
    Serial.println("STRESS_CONFIG"); delay(200);
    unsigned reserve=Serial.available()?Serial.readStringUntil('\n').toInt():0;
    if(reserve>131072){Serial.println("STRESS_FAIL reserve_bound");return;}
    if(reserve){extraReserve=(uint8_t*)malloc(reserve);if(!extraReserve){Serial.println("STRESS_FAIL reserve");return;}memset(extraReserve,0x5a,reserve);}
    pcmReserve=(uint8_t*)malloc(12288);
    if(!pcmReserve || !speechServiceBegin() || !musicBegin()){Serial.println("STRESS_FAIL setup");return;}
    Serial.printf("STRESS_BEGIN reserve=%u psram=%u\n",reserve,ESP.getPsramSize());
    stressStart=millis();nextAction=stressStart;nextCheckpoint=stressStart+10000;stressReady=true;
    memory("stress_start");
}
void loop() {
    if(!stressReady || finished){delay(10);return;}
    uint32_t elapsed=millis()-stressStart;
    if(!decoder->loop() && !musicBegin()){Serial.println("STRESS_FAIL decoder_restart");finished=true;return;}
    unsigned p=elapsed/60000;
    if(p!=stressPhase){
        stressPhase=p;
        if(p==1){WiFi.begin("myssid","mypassword");Serial.println("STRESS_WIFI on");}
        if(p==2){WiFi.disconnect(true);WiFi.mode(WIFI_OFF);Serial.println("STRESS_WIFI off");}
    }
    if(elapsed<180000 && int32_t(millis()-nextAction)>=0){
        nextAction=millis()+200;++actions;
        if(actions%7==0){speechStop();++stops;}
        else if(actions%11==0){speechRepeat();++repeats;}
        // The original sequential benchmark's request counters are not safe
        // for overlapping requests. Use the service's own per-request timing.
        else {speechSay(actions%3==0?"Selected track. A long announcement replaced by the next navigation event.":actions%3==1?"Letter bee.":"Keyboard mode. Bluetooth disconnected. Function Option M opens the menu.");}
    }
    if(int32_t(millis()-nextCheckpoint)>=0){
        nextCheckpoint=millis()+10000;memory("stress_checkpoint");
        Serial.printf("STRESS_PROGRESS elapsed_ms=%lu actions=%u stops=%u repeats=%u wifi=%d music=%u completions=%u\n",
            (unsigned long)elapsed,actions,stops,repeats,(int)WiFi.status(),music.totalFrames,completions.load());
        if(elapsed>=30000 && !warmHeap){warmHeap=ESP.getFreeHeap();warmLargest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);}
    }
    if(elapsed>=180000 && !finished){
        speechStop();memory("stress_end");
        Serial.printf("STRESS_COMPLETE actions=%u stops=%u repeats=%u samples=%u music=%u warm_heap=%u warm_largest=%u end_heap=%u end_largest=%u\n",
            actions,stops,repeats,speechSamples.load(),music.totalFrames,warmHeap,warmLargest,
            ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));finished=true;
    }
    delay(1);
}
