#pragma once
#include <algorithm>
#include <array>
#include "key_learning.h"
#include "settings_codec.h"

namespace keyboardMap {
inline constexpr int positions = 56;
inline constexpr int functionKey = 28, shiftKey = 29, controlKey = 42, optionKey = 43, altKey = 44;
inline constexpr uint8_t inherit = 0xff;

inline bool assignable(int key) {
 return key >= 0 && key < positions && key != functionKey && key != altKey;
}

class Map {
 std::array<std::array<uint8_t, positions>, 2> layers;
public:
 Map() { for (auto &layer : layers) layer.fill(inherit); }

 uint8_t usage(bool alternate, int key, bool function = false, bool backslashAsGui = true) const {
  if (!assignable(key)) return 0;
  if (function && key != shiftKey && key != controlKey) return keys::hidUsage(key/14, key%14, true);
  const uint8_t custom = layers[alternate][key];
  if (custom != inherit) return custom;
  if (key == shiftKey) return 0xe1;
  if (key == controlKey) return 0xe0;
  if (key == optionKey) return 0xe3;
  if (alternate) {
   switch (key) {
   case 0: return 0x29; // Backtick / tilde becomes Escape.
   case 27: if (backslashAsGui) return 0xe3; break;
   case 39: return 0x52; // Semicolon, up.
   case 52: return 0x50; // Comma, left.
   case 53: return 0x51; // Period, down.
   case 54: return 0x4f; // Slash, right.
   }
  }
  return keys::usage[key/14][key%14];
 }

 void set(bool alternate, int key, uint8_t code) {
  if (assignable(key) && valid(code)) layers[alternate][key] = code;
 }

 std::string encode() const {
  constexpr char hex[] = "0123456789abcdef";
  std::string value;
  value.reserve(2 * 2 * positions);
  for (const auto &layer : layers) for (uint8_t code : layer) {
   value += hex[code >> 4];
   value += hex[code & 15];
  }
  return value;
 }

 bool decode(const std::string &value) {
  if (value.size() != 2 * 2 * positions) return false;
  Map parsed;
  size_t index = 0;
  for (auto &layer : parsed.layers) for (auto &code : layer) {
   const int hi = settingsCodec::digit(value[index++]);
   const int lo = settingsCodec::digit(value[index++]);
   if (hi < 0 || lo < 0 || !valid(hi * 16 + lo)) return false;
   code = hi * 16 + lo;
  }
  *this = parsed;
  return true;
 }
private:
 static bool valid(uint8_t code) {
  return code == inherit || code == 0 || (code >= 4 && code <= 0x65)
         || (code >= 0xe0 && code <= 0xe7);
 }
};

template<typename KeyDown, typename Usage>
std::array<uint8_t,8> reportWithUsage(bool optionTap, KeyDown down, Usage usage) {
 std::array<uint8_t,8> bytes{};
 std::array<bool,0x66> seen{};
 unsigned count = 0;
 bool optionChord = false;
 for (int key=0; key<positions; ++key)
  if (key!=optionKey && down(key)) optionChord = true;
 for (int key=0; key<positions; ++key) {
  if (key==optionKey ? !(optionTap || (optionChord && down(key))) : !down(key)) continue;
  const uint8_t code = usage(key);
  if (code>=0xe0 && code<=0xe7) bytes[0] |= 1u << (code-0xe0);
  else if (code && !seen[code]) {
   seen[code] = true;
   if (count<6) bytes[2+count] = code;
   ++count;
  }
 }
 if (count>6) std::fill(bytes.begin()+2,bytes.end(),1);
 if (down(altKey) && count) bytes[0] |= 0x04;
 return bytes;
}

template<typename KeyDown>
std::array<uint8_t,8> report(const Map &map, bool alternate, bool function, bool optionTap, KeyDown down) {
 return reportWithUsage(optionTap, down, [&](int key){ return map.usage(alternate,key,function); });
}
}
