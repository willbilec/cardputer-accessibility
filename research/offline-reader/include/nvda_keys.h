#pragma once
#include <array>
#include <cstdint>
#include "keyboard_map.h"

namespace nvdaRemote {
inline constexpr bool defaultAlternateLayer = true;
inline constexpr uint8_t nvdaModifierUsage = 0x39; // Caps Lock

class RemoteKeyboard {
 bool nvdaHeld = false;
 bool altChord = false;
public:
 template<class KeyDown>
 std::array<uint8_t,8> report(const keyboardMap::Map& map, bool alternate, KeyDown down) {
  using namespace keyboardMap;
  const bool function = down(functionKey);
  const bool nvdaShortcut = function && down(altKey);
  const bool control = down(controlKey);
  const bool option = down(optionKey);
  const bool remoteAlt = control && option;
  altChord = remoteAlt || (altChord && (control || option));

  bool nvdaChord = nvdaShortcut;
  auto result = reportWithUsage(false, [&](int key) {
   if (key == functionKey || (key == altKey && nvdaShortcut)) return false;
   if (altChord && (key == controlKey || key == optionKey)) return false;
   return down(key);
  }, [&](int key) {
   const bool topRowShortcut = function && !nvdaShortcut && key < 14;
   const uint8_t usage = map.usage(alternate, key, topRowShortcut, false);
   if (!topRowShortcut && key != controlKey && key != shiftKey && key != optionKey
       && usage >= 4 && usage <= 0x65) nvdaChord = true;
   return usage;
  });
  nvdaHeld = function && (nvdaHeld || nvdaChord);
  if (remoteAlt) result[0] |= 0x04; // Left Alt, including when held by itself.
  // The reserved HID byte carries the synthetic modifier in NVDA reports.
  const auto modifier = std::find(result.begin()+2,result.end(),nvdaModifierUsage);
  if (modifier != result.end()) *modifier = 0;
  if (nvdaHeld || modifier != result.end()) result[1] = nvdaModifierUsage;
  return result;
 }
};

struct WindowsKey {
 uint8_t vk=0, scan=0;
 bool extended=false;
};

// USB keyboard usages to Windows virtual keys and Set 1 scan codes.
inline WindowsKey windowsKey(uint8_t usage) {
 constexpr uint8_t letters[]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
 if(usage>=4&&usage<=29) return {uint8_t('A'+usage-4),letters[usage-4]};
 if(usage>=30&&usage<=39) return {uint8_t(usage==39?'0':'1'+usage-30),uint8_t(usage-28)};
 constexpr WindowsKey other[]={
  {0x0d,28},{0x1b,1},{0x08,14},{0x09,15},{0x20,57},
  {0xbd,12},{0xbb,13},{0xdb,26},{0xdd,27},{0xdc,43},{0xe2,86},
  {0xba,39},{0xde,40},{0xc0,41},{0xbc,51},{0xbe,52},{0xbf,53},{0x14,58},
  {0x70,59},{0x71,60},{0x72,61},{0x73,62},{0x74,63},{0x75,64},
  {0x76,65},{0x77,66},{0x78,67},{0x79,68},{0x7a,87},{0x7b,88},
  {0x2c,55,true},{0x91,70},{0x13,69},{0x2d,82,true},{0x24,71,true},
  {0x21,73,true},{0x2e,83,true},{0x23,79,true},{0x22,81,true},
  {0x27,77,true},{0x25,75,true},{0x28,80,true},{0x26,72,true},
  {0x90,69,true},{0x6f,53,true},{0x6a,55},{0x6d,74},{0x6b,78},{0x0d,28,true},
  {0x61,79},{0x62,80},{0x63,81},{0x64,75},{0x65,76},{0x66,77},
  {0x67,71},{0x68,72},{0x69,73},{0x60,82},{0x6e,83},{0xe2,86},{0x5d,93,true}
 };
 static_assert(sizeof(other)/sizeof(*other)==0x65-0x28+1);
 if(usage>=0x28&&usage<=0x65) return other[usage-0x28];
 constexpr WindowsKey modifiers[]={
  {0xa2,29},{0xa0,42},{0xa4,56},{0x5b,91,true},
  {0xa3,29,true},{0xa1,54},{0xa5,56,true},{0x5c,92,true}
 };
 return usage>=0xe0&&usage<=0xe7?modifiers[usage-0xe0]:WindowsKey{};
}

template<class Emit>
void transitions(const std::array<uint8_t,8>& before,const std::array<uint8_t,8>& after,Emit emit) {
 if(after[2]==1) return; // HID rollover. Keep the last valid report.
 auto keys=[&](const auto& from,const auto& to,bool pressed) {
  for(unsigned i=2;i<from.size();++i) {
   const uint8_t usage=from[i];
   if(!usage) continue;
   bool present=false;
   for(unsigned j=2;j<to.size();++j) present|=to[j]==usage;
   if(!present) emit(usage,pressed);
  }
 };
 auto modifiers=[&](uint8_t changed,bool pressed) {
  for(unsigned i=0;i<8;++i) if(changed&(1u<<i)) emit(uint8_t(0xe0+i),pressed);
 };
 keys(before,after,false);
 if(before[1] && before[1]!=after[1]) emit(before[1],false);
 modifiers(before[0]&~after[0],false);
 modifiers(after[0]&~before[0],true);
 if(after[1] && before[1]!=after[1]) emit(after[1],true);
 keys(after,before,true);
}
}
