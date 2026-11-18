#pragma once
#ifdef CARDEX_EMULATOR_DEMO
#include <utility/Keyboard/KeyboardReader/KeyboardReader.h>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418.h>
#include <algorithm>
// Same public matrix mapping as M5Stack's MIT-licensed TCA8418 reader.
// Poll one event per frame: consuming press+release together would hide taps
// from the application's edge detector. No GPIO interrupt is required.
class SimulatorKeyboardReader:public KeyboardReader {
 Adafruit_TCA8418 controller;
 bool ready=false;
public:
 void begin()override{
  ready=controller.begin();
  if(ready){controller.matrix(7,8);controller.flush();controller.enableInterrupts();}
  Serial.printf("CARDEX_POLLING_KEYBOARD ok=%d\n",ready);
 }
 void update()override{
  if(!ready||!controller.available())return;
  uint8_t raw=controller.getEvent();uint8_t number=raw&0x7f;
  controller.writeRegister8(2,1);
  if(number==0||number>68)return;
  unsigned index=number-1,r=index/10,c=index%10;
  if(r>=7||c>=8)return;
  Point2D_t point;point.x=r*2+(c>3);point.y=c%4;
  auto found=std::find(_key_list.begin(),_key_list.end(),point);
  if(raw&0x80){if(found==_key_list.end())_key_list.push_back(point);}
  else if(found!=_key_list.end())_key_list.erase(found);
 }
};
#endif
