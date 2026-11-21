#pragma once
#include <Arduino.h>
#include "headless_board.h"
#include "keyboard_map.h"

class KeyMapManager {
 using Map = keyboardMap::Map;
 using Speak = void (*)(const String &, bool, bool);
 enum Category { Letters, Digits, Modifiers, Functions, Navigation, Typing, Keypad, Other };
 struct Range { Category category; uint8_t first, last; };
 static constexpr Range ranges[]{
  {Letters,4,0x1d},{Digits,0x1e,0x27},{Modifiers,0xe0,0xe7},{Functions,0x3a,0x45},
  {Navigation,0x49,0x52},{Typing,0x28,0x39},{Keypad,0x53,0x63},
  {Other,0x46,0x48},{Other,0x64,0x65},{Other,0,0},{Other,0xff,0xff}
 };
 static constexpr const char *categories[]{
  "Letters","Numbers","Modifiers","Function keys","Navigation","Typing keys","Keypad","Other"
 };

 Map draft;
 bool alternate = false, dirty = false, choosing = false;
 int target = -1, held = -1, category = Letters, selection = 0;
 uint32_t pressedAt = 0;
 String query;
 std::vector<uint8_t> results;

 static String name(uint8_t code, bool spoken = true) {
  if (code == keyboardMap::inherit) return "Default assignment";
  if (!code) return "Disabled";
  if (code >= 4 && code <= 0x1d) {
   const char letter = 'a' + code - 4;
   return spoken ? String(keys::name(letter,true).c_str()) : String(letter);
  }
  if (code >= 0x1e && code <= 0x27) return String((code - 0x1d) % 10);
  if (code >= 0x3a && code <= 0x45) return String("F") + String(code - 0x39);
  if (code >= 0xe0 && code <= 0xe7) {
   constexpr const char *names[]{"Left Control","Left Shift","Left Alt","Left Windows GUI",
                                 "Right Control","Right Shift","Right Alt","Right Windows GUI"};
   return names[code-0xe0];
  }
  constexpr const char *special[]{
   "Enter","Escape","Backspace","Tab","Space","Minus","Equals","Left bracket","Right bracket",
   "Backslash","Non US hash","Semicolon","Apostrophe","Backtick","Comma","Period","Slash","Caps lock"
  };
  if (code >= 0x28 && code <= 0x39) return special[code-0x28];
  constexpr const char *extended[]{
   "Print screen","Scroll lock","Pause","Insert","Home","Page up","Delete","End","Page down",
   "Right arrow","Left arrow","Down arrow","Up arrow","Num lock","Keypad divide","Keypad multiply",
   "Keypad minus","Keypad plus","Keypad enter","Keypad 1","Keypad 2","Keypad 3","Keypad 4",
   "Keypad 5","Keypad 6","Keypad 7","Keypad 8","Keypad 9","Keypad 0","Keypad period",
   "Non US backslash","Application menu"
  };
  return code >= 0x46 && code <= 0x65 ? extended[code-0x46] : "Unknown";
 }

 void refresh() {
  results.clear();
  selection = 0;
  for (const auto &range : ranges) {
   if (query.isEmpty() && category != range.category) continue;
   for (int code = range.first; code <= range.last; ++code) {
    String label = name(code,false);
    label.toLowerCase();
    if (query.isEmpty() || label.indexOf(query) >= 0) results.push_back(code);
   }
  }
 }

 String result() const {
  if (results.empty()) return "No matches";
  return name(results[selection]) + ", " + String(selection+1) + " of " + String(results.size());
 }

 String assignment() const {
  return String(keys::name(keys::plain[target/14][target%14],true).c_str())
      + ", mapped to " + name(draft.usage(alternate,target));
 }

 void openPicker(Speak say) {
  held = -1;
  choosing = true;
  query = "";
  category = Letters;
  refresh();
  say(assignment() + ". Type to search, or use Function with arrows to browse. " + result(),false,true);
 }

public:
 void open(const Map &map) {
  draft = map;
  alternate = dirty = choosing = false;
  target = held = -1;
  query = "";
  results.clear();
 }

 bool changed() const { return dirty; }
#ifdef UNIFIED_SCREEN
 int choiceCount() const { return choosing ? results.size() : 0; }
 int selectedChoice() const { return selection; }
 String choiceLabel(int index) const { return name(results[index],false); }
#endif
 const Map &map() const { return draft; }
 void saved() { dirty = false; }
 void cancelHold() { held = -1; }

 void tick(const HeadlessKeyboard &keyboard, uint32_t now, Speak say) {
  if (held >= 0 && keyboard.keyDown(held/14,held%14) && now-pressedAt >= 550) openPicker(say);
 }

 void toggle(Speak say) {
  alternate = !alternate;
  choosing = false;
  target = held = -1;
  say(alternate ? "Alternate layer" : "Regular layer",false,true);
 }

 void key(const HeadlessKeyboard::Event &event, const HeadlessKeyboard::State &state,
          uint32_t now, Speak say) {
  const int key = event.row*14+event.col;
  const auto base = keys::plain[event.row][event.col];
  if (!event.down) {
   if (held == key) {
    if (now-pressedAt >= 550) openPicker(say);
    held = -1;
   }
   return;
  }
  if (state.alt || (state.opt && (choosing || key != keyboardMap::optionKey))) return;
  if (!choosing) {
   if (state.fn && base == ' ' && target >= 0) openPicker(say);
   else if (!state.fn && keyboardMap::assignable(key)) {
    target = held = key;
    pressedAt = now;
    say(assignment(),false,true);
   }
   return;
  }
  if (state.fn) {
   switch (keys::hidUsage(event.row,event.col,true)) {
   case 0x29:
    choosing = false;
    say("Assignment canceled. " + assignment(),false,true);
    return;
   case 0x50: case 0x4f:
    category = (category + (base==',' ? 7 : 1)) % 8;
    query = "";
    refresh();
    say(String(categories[category]) + ". " + result(),false,true);
    return;
   case 0x52: case 0x51:
    if (!results.empty()) {
     const int count = results.size();
     selection = (selection+count+(base==';' ? -1 : 1)) % count;
    }
    say(result(),false,true);
    return;
   default: return;
   }
  }
  if (base == 13) {
   if (results.empty()) return;
   draft.set(alternate,target,results[selection]);
   dirty = true;
   choosing = false;
   say(assignment(),false,true);
   return;
  }
  if (base == 8) {
   if (!query.isEmpty()) query.remove(query.length()-1);
  } else if (base >= 32 && query.length() < 32) query += char(base);
  else return;
  refresh();
  say((query.isEmpty() ? String(categories[category]) : "Search " + query) + ". " + result(),false,true);
 }
};
