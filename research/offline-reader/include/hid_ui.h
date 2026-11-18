#pragma once
#include <string>
namespace hidUi {
enum class Page { Connection, Active, Menu, Confirm };
enum class Key { Up, Down, Enter, Back, Menu, Help, Quit, Go, Other };
enum class Action { None, StartUsb, StartBluetooth, Release, Restart, Return };
struct Reply { Action action=Action::None; std::string speech; };
class Controller {
 Page beforeConfirm=Page::Connection;
 bool restart=false, connected=false;
public:
 Page page=Page::Connection;
 int selection=0;
 bool usb=true, mouse=false;
 std::string mode() const { return mouse?"Mouse mode":"Keyboard mode"; }
 std::string help() const {
  return mouse ? "Mouse: semicolon up, period down, comma left, slash right. Enter left click, backslash right click. Go switches mode. Function Option M opens menu" :
   "Keyboard sends keys silently. Go switches to mouse. Function and Option with M opens menu, H help, R repeat, Space stops speech, Q launcher";
 }
 std::string focus() const {
  static const char *connections[]={"USB","Bluetooth","Instructions","Return to launcher"};
  static const char *menu[]={"Resume","Switch keyboard or mouse mode","Choose connection again","Instructions","Return to launcher"};
  return std::string(page==Page::Connection?connections[selection]:menu[selection])+", "+std::to_string(selection+1)+" of "+std::to_string(page==Page::Connection?4:5);
 }
 Reply confirm(bool boot) { beforeConfirm=page; page=Page::Confirm; restart=boot;
  return {Action::Release,boot?"Restart to choose USB or Bluetooth? Enter confirms. Any other key cancels":"Return to launcher? Enter confirms. Any other key cancels"}; }
 Reply handle(Key key) {
  if(page==Page::Confirm) {
   if(key==Key::Enter) return {restart?Action::Restart:Action::Return,restart?"Restarting connection selection":"Returning to launcher"};
   page=beforeConfirm; return {Action::Release,"Cancelled. "+(page==Page::Active?mode():focus())};
  }
  if(key==Key::Quit) return confirm(false);
  if(key==Key::Help) return {Action::None,help()};
  if(page==Page::Active) {
   if(key==Key::Menu) { page=Page::Menu; selection=0; return {Action::Release,"Menu. "+focus()}; }
   if(key==Key::Go) { mouse=!mouse; return {Action::Release,mode()+". "+help()}; }
   return {};
  }
  if(page==Page::Menu && (key==Key::Back || key==Key::Go)) { page=Page::Active; return {Action::Release,mode()}; }
  int count=page==Page::Connection?4:5;
  if(key==Key::Up || key==Key::Down) { selection=(selection+(key==Key::Up?count-1:1))%count; return {Action::None,focus()}; }
  if(key!=Key::Enter) return {};
  if(page==Page::Connection) {
   if(selection==2) return {Action::None,help()};
   if(selection==3) return confirm(false);
   usb=selection==0; connected=false; page=Page::Active;
   return {usb?Action::StartUsb:Action::StartBluetooth,(usb?"USB selected. Waiting for host. ":"Bluetooth selected. Pair with M5 Keyboard Mouse. ")+mode()+". Go changes mode. Function Option M opens menu"};
  }
  switch(selection) {
   case 0:page=Page::Active;return {Action::Release,mode()};
   case 1:mouse=!mouse;page=Page::Active;return {Action::Release,mode()+". "+help()};
   case 2:return confirm(true);
   case 3:return {Action::None,help()};
   default:return confirm(false);
  }
 }
 std::string connection(bool ready) {
  if(ready==connected) return {};
  connected=ready;
  return std::string(usb?"USB ":"Bluetooth ")+(ready?"connected. ":"disconnected. ")+mode()+". Function Option M opens menu";
 }
};
}
