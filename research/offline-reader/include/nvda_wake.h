#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include "settings_codec.h"

namespace nvdaRemote {
using WakeMac=std::array<uint8_t,6>;
using WakePacket=std::array<uint8_t,102>;

inline bool parseWakeMac(std::string_view text,WakeMac& mac){
 const bool separated=text.size()==17;
 if(!separated&&text.size()!=12)return false;
 const char separator=separated?text[2]:'\0';
 if(separated&&separator!=':'&&separator!='-')return false;
 const auto digit=[](char c){return settingsCodec::digit(c>='A'&&c<='F'?char(c-'A'+'a'):c);};
 for(unsigned byte=0;byte<mac.size();++byte){
  const size_t index=byte*(separated?3:2);
  if(separated&&byte&&text[index-1]!=separator)return false;
  const int high=digit(text[index]),low=digit(text[index+1]);
  if(high<0||low<0)return false;
  mac[byte]=uint8_t(high*16+low);
 }
 return !(mac[0]&1)&&std::any_of(mac.begin(),mac.end(),[](uint8_t byte){return byte!=0;});
}

inline WakePacket magicPacket(const WakeMac& mac){
 WakePacket packet{};
 std::fill_n(packet.begin(),6,0xff);
 for(unsigned i=6;i<packet.size();i+=mac.size())std::copy(mac.begin(),mac.end(),packet.begin()+i);
 return packet;
}
}
