#define CARDEX_EMULATOR_DEMO 1
#include "../src/SimulatorKeyboard.h"
#include <cassert>
#include <set>
#include <iostream>
int main(){
 SimulatorKeyboardReader reader;reader.begin();std::set<int> positions;
 for(unsigned r=0;r<7;++r)for(unsigned c=0;c<8;++c){
  uint8_t code=r*10+c+1;
  Adafruit_TCA8418::events.push_back(code|0x80);
  Adafruit_TCA8418::events.push_back(code);
  reader.update();assert(reader.keyList().size()==1); // Release is still queued.
  auto p=reader.keyList().front();assert(p.x==int(r*2+(c>3))&&p.y==int(c%4));
  positions.insert(p.y*14+p.x);
  reader.update();assert(reader.keyList().empty());
 }
 assert(positions.size()==56);
 for(uint8_t bad:{0,9,10,69,127,255}){Adafruit_TCA8418::events.push_back(bad);reader.update();assert(reader.keyList().empty());}
 Adafruit_TCA8418::events={0x81,0x81};reader.update();reader.update();assert(reader.keyList().size()==1);
 Adafruit_TCA8418::events={1};reader.update();assert(reader.keyList().empty());
 std::cout<<"Actual polling reader: 56 mappings, edge preservation, duplicate and malformed events passed\n";
}
