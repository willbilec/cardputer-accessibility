// Actual Arduino BLE/NimBLE and Klatt, substituted audio; no physical peer.
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "speech_service.h"
static std::atomic<unsigned> samples{0},done{0};
static BLEHIDDevice *hid;
static void memory(const char *s){Serial.printf("BLE_MEM stage=%s free=%u minimum=%u largest=%u stack=%u\n",s,ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(nullptr));}
void speechEmulatorOutput(const int16_t*,size_t n,unsigned rate){samples+=n;vTaskDelay(pdMS_TO_TICKS((n*1000+rate-1)/rate));}
void speechEmulatorCompleted(unsigned){++done;}
void setup(){
 Serial.begin(115200);delay(200);memory("baseline");
 if(!speechServiceBegin()){Serial.println("BLE_FAIL speech_init");return;}
 memory("speech_init");Serial.println("BLE_CHECKPOINT device_init");
 if(!BLEDevice::init("M5-Keyboard-Mouse")){Serial.println("BLE_FAIL device_init");return;}
 memory("device_initialized");
 auto *server=BLEDevice::createServer();if(!server){Serial.println("BLE_FAIL server");return;}
 hid=new BLEHIDDevice(server);hid->inputReport(1);hid->inputReport(2);hid->outputReport(2);
 hid->manufacturer()->setValue("M5Stack");hid->pnp(2,0xe502,0xa111,0x0210);hid->hidInfo(0,1);
 // Keyboard + mouse shape; physical protocol acceptance is outside this probe.
 static uint8_t descriptor[]={0x05,1,0x09,6,0xa1,1,0x85,2,0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x75,8,0x95,7,0x81,1,0xc0};
 hid->reportMap(descriptor,sizeof(descriptor));hid->startServices();
 auto *advertising=server->getAdvertising();advertising->setAppearance(0x3c1);advertising->addServiceUUID(hid->hidService()->getUUID());advertising->start();
 memory("advertising");
 for(unsigned i=0;i<12;++i){
  if(!speechSayAndWait(i%2?"Keyboard mode. Function Option M opens the menu.":"Bluetooth keyboard ready. Waiting for connection.",15000)){Serial.println("BLE_FAIL speech");return;}
  memory("speech_done");
 }
 Serial.printf("BLE_COMPLETE utterances=%u samples=%u\n",done.load(),samples.load());
}
void loop(){delay(10);}
