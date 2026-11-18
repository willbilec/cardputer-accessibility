#pragma once
#include "key_learning.h"
namespace learningUi {
enum class Page { Explore, Menu, Confirm };
enum class Key { Up, Down, Enter, Back, Go, Menu, Help, Quit, Other };
struct Reply { bool leave=false; std::string speech; };
class Controller {
public:
 Page page=Page::Explore;
 int selection=0;
 std::string lastKey;
 std::string introduction() const { return "Keyboard learning. Press a key to hear its name and use. Nothing is sent to a computer. Go opens menu"; }
 std::string focus() const {
  static const char *items[]={"Resume learning","Repeat last key","Instructions","Return to launcher"};
  return std::string(items[selection])+", "+std::to_string(selection+1)+" of 4";
 }
 std::string pressed(int row,int col,bool shift,bool fn,bool ctrl,bool alt) {
  if(page!=Page::Explore) return {};
  lastKey=keys::describe(row,col,shift,fn,ctrl,alt); return lastKey;
 }
 Reply handle(Key key) {
  if(page==Page::Confirm) {
   if(key==Key::Enter) return {true,"Returning to launcher"};
   page=Page::Menu;return {false,"Cancelled. "+focus()};
  }
  if(key==Key::Quit) { page=Page::Confirm;return {false,"Return to launcher? Enter confirms. Any other key cancels"}; }
  if(key==Key::Help) return {false,"Press any key to learn it. Function changes the key layer. Go opens menu. Function and Option with Q requests the launcher"};
  if(page==Page::Explore) {
   if(key==Key::Go || key==Key::Menu) { page=Page::Menu;selection=0;return {false,"Go button. Learning menu. "+focus()}; }
   return {};
  }
  if(key==Key::Back || key==Key::Go) { page=Page::Explore;return {false,introduction()}; }
  if(key==Key::Up || key==Key::Down) { selection=(selection+(key==Key::Up?3:1))%4;return {false,focus()}; }
  if(key!=Key::Enter) return {};
  switch(selection) {
   case 0:page=Page::Explore;return {false,introduction()};
   case 1:return {false,lastKey.empty()?"No key explored yet":lastKey};
   case 2:return handle(Key::Help);
   default:return handle(Key::Quit);
  }
 }
};
}
