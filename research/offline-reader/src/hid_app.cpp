// Accessible front end for geo-tp's USB/BLE keyboard/mouse transports.
#include <Arduino.h>
#include "headless_board.h"
#include "hid_transport.h"
#include "hid_ui.h"
#include "speech_service.h"
#include "firmware_layout.h"

static hidUi::Controller ui;
static bool initialized=false, ignoreUntilRelease=true;
static uint8_t oldMouseButtons=0;
static void releaseReports() { if(initialized) releaseHidReports(ui.usb); ignoreUntilRelease=true; }
static void respond(const hidUi::Reply &reply) {
 if(reply.action!=hidUi::Action::None) releaseReports();
 switch(reply.action) {
  case hidUi::Action::StartUsb:
   initialized=initUsbHid();
   if(!initialized){ui.page=hidUi::Page::Connection;ui.selection=0;speechSay("USB initialization failed. Choose a connection");return;}
   break;
  case hidUi::Action::StartBluetooth:
   initBluetooth();initialized=bluetoothTransportReady();
   if(!initialized){ui.page=hidUi::Page::Connection;ui.selection=1;speechSay("Bluetooth initialization failed. Choose a connection");return;}
   break;
  case hidUi::Action::Restart:speechSayAndWait(reply.speech.c_str(),4000);ESP.restart();return;
  case hidUi::Action::Return:
   speechSayAndWait(reply.speech.c_str(),4000);
   if(!readerReturnToLauncher()) speechSay("Paired launcher unavailable. Open menu to resume");
   return;
  default:break;
 }
 if(!reply.speech.empty()) speechSay(reply.speech.c_str());
}
void setup() {
 Serial.begin(115200);M5Cardputer.begin();M5Cardputer.Speaker.setVolume(128);speechServiceBegin();
 speechSay("Keyboard and mouse. Choose connection. Semicolon and period move; Enter selects. USB, 1 of 4");
}
void loop() {
 M5Cardputer.update();auto &keyboard=M5Cardputer.Keyboard;const auto &state=keyboard.keysState();
 if(M5Cardputer.BtnA.wasDecideClickCount()) { M5Cardputer.BtnA.getClickCount();respond(ui.handle(hidUi::Key::Go)); }
 bool keyPress=false;for(auto event:keyboard.events()) keyPress|=event.down;
 if(keyPress) {
  if(ui.page==hidUi::Page::Confirm) { respond(ui.handle(state.enter?hidUi::Key::Enter:hidUi::Key::Other));delay(5);return; }
  if(state.fn && state.opt) {
   releaseReports();
   if(keyboard.keyDown(3,9)) respond(ui.handle(hidUi::Key::Menu));
   else if(keyboard.keyDown(2,7)) respond(ui.handle(hidUi::Key::Help));
   else if(keyboard.keyDown(1,1)) respond(ui.handle(hidUi::Key::Quit));
   else if(keyboard.keyDown(1,4)) speechRepeat();
   else if(keyboard.keyDown(3,13)) speechStop();
   delay(5);return;
  }
  if(ui.page!=hidUi::Page::Active) {
   auto key=state.enter?hidUi::Key::Enter:state.esc?hidUi::Key::Back:
    keyboard.isKeyPressed(';')?hidUi::Key::Up:keyboard.isKeyPressed('.')?hidUi::Key::Down:hidUi::Key::Other;
   respond(ui.handle(key));delay(5);return;
  }
 }
 if(ui.page!=hidUi::Page::Active || !initialized) { delay(5);return; }
 bool connected=ui.usb?usbHostConnected():getBluetoothStatus();
 auto status=ui.connection(connected);if(!status.empty()) speechSay(status.c_str());
 if(ignoreUntilRelease) { if(!keyboard.isPressed()) ignoreUntilRelease=false;else {delay(5);return;} }
 if(connected) {
  if(ui.mouse && keyboard.isChange()) {
   uint8_t buttons=(state.enter?1:0)|(keyboard.isKeyPressed('\\')?2:0);
   if(buttons!=oldMouseButtons) speechSay(buttons==3?"Both mouse buttons pressed":buttons==1?"Left mouse button pressed":buttons==2?"Right mouse button pressed":"Mouse buttons released");
   else if(keyboard.isPressed()) {
    if(keyboard.isKeyPressed(';')) speechSay("Mouse up");
    else if(keyboard.isKeyPressed('.')) speechSay("Mouse down");
    else if(keyboard.isKeyPressed(',')) speechSay("Mouse left");
    else if(keyboard.isKeyPressed('/')) speechSay("Mouse right");
   }
   oldMouseButtons=buttons;
  }
  if(ui.usb) handleUsbMode(ui.mouse);else handleBluetoothMode(ui.mouse);
 }
 delay(5);
}
