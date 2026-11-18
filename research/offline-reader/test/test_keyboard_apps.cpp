#include <cassert>
#include <iostream>
#include "../include/key_learning.h"
#include "../include/hid_reports.h"
#include "../include/hid_ui.h"
#include "../include/learning_ui.h"
#include "../include/catalogue_ui.h"
int main() {
 for(int row=0;row<4;++row)for(int col=0;col<14;++col) {
  for(bool shift:{false,true})for(bool fn:{false,true}) {
   auto text=keys::describe(row,col,shift,fn);
   assert(!text.empty() && text.size()<160);
  }
 }
 assert(keys::hidUsage(0,1,true)==0x3a && keys::hidUsage(0,12,true)==0x45);
 assert(keys::hidUsage(0,0,true)==0x29 && keys::hidUsage(0,13,true)==0x4c);
 assert(keys::hidUsage(2,11,true)==0x52 && keys::hidUsage(3,11,true)==0x51);
 assert(keys::describe(2,2,true,false).find("Shift Letter ay")!=std::string::npos);
 assert(keys::name('A')=="ay");
 assert(keys::name('.')=="Period");
 auto report=hidReports::keyboard(5,{4,4,0,0x2c});
 assert(report[0]==5 && report[2]==4 && report[3]==0x2c && report[4]==0);
 report=hidReports::keyboard(0,{4,5,6,7,8,9,10});for(int i=2;i<8;++i)assert(report[i]==1);
 assert(hidReports::mouse(true,true,true,false,true,false)==(std::array<uint8_t,4>{3,255,255,0}));
 assert(hidReports::mouse(false,false,false,false,false,false)==(std::array<uint8_t,4>{0,0,0,0}));
 hidUi::Controller hid;
 auto step=[&](hidUi::Key key){auto reply=hid.handle(key);assert(reply.speech.size()<160);return reply;};
 assert(step(hidUi::Key::Down).speech.find("Bluetooth")!=std::string::npos);
 assert(step(hidUi::Key::Enter).action==hidUi::Action::StartBluetooth && !hid.usb);
 assert(hid.connection(true).find("connected")!=std::string::npos && hid.connection(true).empty());
 assert(step(hidUi::Key::Go).action==hidUi::Action::Release && hid.mouse);
 assert(step(hidUi::Key::Menu).action==hidUi::Action::Release && hid.page==hidUi::Page::Menu);
 step(hidUi::Key::Down);assert(step(hidUi::Key::Enter).action==hidUi::Action::Release && !hid.mouse);
 step(hidUi::Key::Quit);assert(hid.page==hidUi::Page::Confirm);
 step(hidUi::Key::Other);assert(hid.page==hidUi::Page::Active);
 step(hidUi::Key::Quit);assert(step(hidUi::Key::Enter).action==hidUi::Action::Return);
 learningUi::Controller learning;
 assert(learning.pressed(0,1,false,true,false,false).find("F 1")!=std::string::npos);
 learning.handle(learningUi::Key::Go);assert(learning.page==learningUi::Page::Menu);
 learning.handle(learningUi::Key::Down);
 assert(learning.handle(learningUi::Key::Enter).speech==learning.lastKey);
 learning.handle(learningUi::Key::Quit);assert(!learning.handle(learningUi::Key::Other).leave);
 learning.handle(learningUi::Key::Quit);assert(learning.handle(learningUi::Key::Enter).leave);
 catalogueUi::Controller catalogue(3);
 catalogue.handle(catalogueUi::Key::Up);assert(catalogue.selection==3);
 assert(catalogue.handle(catalogueUi::Key::Enter).action==catalogueUi::Action::Help);
 assert(!catalogue.askInstall(3));assert(catalogue.askInstall(2));
 auto installation=catalogue.handle(catalogueUi::Key::Enter);
 assert(installation.action==catalogueUi::Action::Install && installation.index==2);
 catalogue.askInstall(1);assert(catalogue.handle(catalogueUi::Key::Down).action==catalogueUi::Action::Cancel);
 std::cout<<"Keyboard app models: all key layers, reports/rollover, spoken menus, connection changes, confirmation and catalogue passed\n";
}
