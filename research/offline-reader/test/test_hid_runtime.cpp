#include <cassert>
#include <iostream>
#include <string>
#include "../src/headless_board.cpp"
#include "hid_transport.h"
static bool fakeBluetoothConnected=false;
void initBluetooth(){}
bool bluetoothTransportReady(){return true;}
bool getBluetoothStatus(){return fakeBluetoothConnected;}
void sendEmptyReports(){}
void handleBluetoothMode(bool){}
#include "usb_transport_generated.h"
static std::vector<std::string> spoken;
bool speechServiceBegin(){return true;}
bool speechSay(const char *s){spoken.emplace_back(s);return true;}
bool speechSayAndWait(const char *s,uint32_t){return speechSay(s);}
bool speechRepeat(){return true;}
void speechStop(){}
#include "../src/hid_app.cpp"

static void event(int row,int col,bool down){int raw=(col/2)*10+row+(col%2?4:0)+1;Wire.events.push_back(raw|(down?128:0));}
static void press(int row,int col){event(row,col,true);loop();}
static void release(int row,int col){event(row,col,false);loop();}
int main(){
 setup();assert(ui.page==hidUi::Page::Connection);
 fakeUsbInitOk=false;press(2,13);assert(ui.page==hidUi::Page::Connection && !initialized);
 release(2,13);fakeUsbInitOk=true;press(2,13);assert(ui.page==hidUi::Page::Active && initialized);
 assert(ignoreUntilRelease);release(2,13);assert(!ignoreUntilRelease);
 auto count=spoken.size();press(2,2);assert(keyboard.last.keys[0]==4 && spoken.size()==count);release(2,2);
 assert(keyboard.last.keys[0]==0); // real adapted USB keyboard releases on key-up
 fakePins[0]=LOW;fakeTime=1000;loop();fakePins[0]=HIGH;fakeTime=1050;loop();fakeTime=1500;loop();
 assert(ui.mouse && keyboard.last.keys[0]==0);
 press(2,11);press(2,13);assert(mouse.buttons==1 && mouse.y==-1);
 release(2,13);assert(mouse.buttons==0 && mouse.y==-1); // regression: another key still held
 press(1,13);press(2,13);assert(mouse.buttons==3);release(1,13);assert(mouse.buttons==1);
 release(2,13);assert(mouse.buttons==0);
 unsigned moves=mouse.moves;
 event(2,0,true);event(3,1,true);event(3,9,true);loop();
 assert(ui.page==hidUi::Page::Menu && mouse.moves==moves && mouse.buttons==0);
 event(2,0,false);event(3,1,false);event(3,9,false);event(2,11,false);loop();
 press(2,11);release(2,11);assert(ui.selection==4);
 press(2,13);assert(ui.page==hidUi::Page::Confirm);release(2,13);
 assert(ui.page==hidUi::Page::Confirm); // releases alone do not cancel
 press(3,4);assert(ui.page==hidUi::Page::Menu && keyboard.last.keys[0]==0);release(3,4);
 press(2,13);release(2,13);press(2,13);
 assert(ESP.restarted && mouse.buttons==0 && keyboard.last.keys[0]==0);
 std::cout<<"Actual HID event loop and adapted USB transport: selection/error, silent typing, releases, menu isolation and confirmation passed\n";
 return 0;
}
