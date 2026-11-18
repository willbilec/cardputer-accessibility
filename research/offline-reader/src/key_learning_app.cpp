#include <Arduino.h>
#include "headless_board.h"
#include "speech_service.h"
#include "firmware_layout.h"
#include "learning_ui.h"

static learningUi::Controller learner;
static void respond(const learningUi::Reply &reply) {
 if(reply.leave) {
  speechSayAndWait(reply.speech.c_str(),4000);
  if(!readerReturnToLauncher()) speechSay("Paired launcher unavailable. Staying in keyboard learning");
 } else if(!reply.speech.empty()) speechSay(reply.speech.c_str());
}
void setup() {
 Serial.begin(115200); M5Cardputer.begin(); M5Cardputer.Speaker.setVolume(128);
 speechServiceBegin(); speechSay(learner.introduction().c_str());
}
void loop() {
 M5Cardputer.update(); auto &keyboard=M5Cardputer.Keyboard; const auto &state=keyboard.keysState();
 if(M5Cardputer.BtnA.wasDecideClickCount()) { M5Cardputer.BtnA.getClickCount();respond(learner.handle(learningUi::Key::Go)); }
 bool keyPress=false; for(auto event:keyboard.events()) keyPress|=event.down;
 if(!keyPress) { delay(5);return; }
 if(learner.page==learningUi::Page::Confirm) { respond(learner.handle(state.enter?learningUi::Key::Enter:learningUi::Key::Other));return; }
 if(state.fn && state.opt) {
  if(keyboard.keyDown(1,1)) respond(learner.handle(learningUi::Key::Quit));
  else if(keyboard.keyDown(3,9)) respond(learner.handle(learningUi::Key::Menu));
  else if(keyboard.keyDown(2,7)) respond(learner.handle(learningUi::Key::Help));
  else if(keyboard.keyDown(1,4)) speechRepeat();
  else if(keyboard.keyDown(3,13)) speechStop();
  return;
 }
 if(learner.page==learningUi::Page::Menu) {
  auto key=state.enter?learningUi::Key::Enter:state.esc?learningUi::Key::Back:
    keyboard.isKeyPressed(';')?learningUi::Key::Up:keyboard.isKeyPressed('.')?learningUi::Key::Down:learningUi::Key::Other;
  respond(learner.handle(key));return;
 }
 std::string message;
 for(auto event:keyboard.events()) if(event.down) {
  auto description=learner.pressed(event.row,event.col,state.shift,state.fn,state.ctrl,state.alt);
  // Latest key/chord wins, rather than truncating a batch of long descriptions.
  message=description;
 }
 if(!message.empty()) speechSay(message.c_str());
 delay(5);
}
