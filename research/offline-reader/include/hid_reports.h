#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace hidReports {
inline std::array<uint8_t,8> keyboard(uint8_t modifiers,const std::vector<uint8_t> &usages) {
 std::array<uint8_t,8> result{}; result[0]=modifiers;
 unsigned count=0;
 for(uint8_t code:usages) {
  if(!code) continue;
  bool duplicate=false; for(unsigned i=0;i<count;++i) if(result[2+i]==code) duplicate=true;
  if(duplicate) continue;
  if(count==6) { for(unsigned i=2;i<8;++i) result[i]=1; return result; }
  result[2+count++]=code;
 }
 return result;
}
inline std::array<uint8_t,4> mouse(bool left,bool right,bool up,bool down,bool west,bool east) {
 return {uint8_t((left?1:0)|(right?2:0)),uint8_t(int(east)-int(west)),uint8_t(int(down)-int(up)),0};
}
}
