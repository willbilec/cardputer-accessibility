#pragma once
#include <algorithm>
#include <cstdint>
// Include after the M5Cardputer interface. Read raw edges, not key-count changes
// or keysState().word (which is empty for some Fn combinations).
class AccessibleKeys {
 bool held[56]{};
 uint32_t pressedAt[56]{},repeatedAt[56]{};
public:
 template<class Keyboard,class State>
 bool poll(Keyboard&keyboard,State&state,uint32_t now){
  bool current[56]{};
  for(const auto&p:keyboard.keyList())if(p.x>=0&&p.x<14&&p.y>=0&&p.y<4)current[p.y*14+p.x]=true;
  for(int i=0;i<56;++i)if(current[i]){
   Point2D_t p;p.x=i%14;p.y=i/14;uint8_t code=uint8_t(keyboard.getKeyValue(p).value_first);
   if(code==KEY_FN)state.fn=true;
   if(code==KEY_LEFT_CTRL)state.ctrl=true;
   if(code==KEY_LEFT_SHIFT)state.shift=true;
   if(code==KEY_LEFT_ALT)state.alt=true;
   if(code==KEY_OPT)state.opt=true;
  }
  bool emitted=false;
  for(int i=0;i<56;++i){
   if(!current[i]){held[i]=false;continue;}
   Point2D_t p;p.x=i%14;p.y=i/14;auto value=keyboard.getKeyValue(p);uint8_t code=uint8_t(value.value_first);
   if(code==KEY_FN||code==KEY_LEFT_CTRL||code==KEY_LEFT_SHIFT||code==KEY_LEFT_ALT||code==KEY_OPT){held[i]=true;continue;}
   bool repeatable=code==KEY_BACKSPACE||(!state.ctrl&&!state.alt&&!state.opt&&!state.fn)||(state.ctrl&&(code==';'||code=='.'||code==','||code=='/'));
   bool emit=!held[i]||(repeatable&&now-pressedAt[i]>=500&&now-repeatedAt[i]>=100);
   if(!held[i])pressedAt[i]=now;
   held[i]=true;if(!emit)continue;repeatedAt[i]=now;emitted=true;
   if(code==KEY_ENTER){state.enter=true;continue;}
   if(code==KEY_BACKSPACE){if(state.fn)state.del=true;else state.backspace=true;continue;}
   if(code==KEY_TAB){state.tab=true;continue;}
   if(state.ctrl&&(code==';'||code=='.'||code==','||code=='/')){
    state.up=code==';';state.down=code=='.';state.left=code==',';state.right=code=='/';
    state.ctrl=state.shift;state.shift=false;state.fn=false;continue;
   }
   if(state.ctrl&&code=='`'){state.esc=true;state.ctrl=false;state.fn=false;continue;}
   bool letter=code>='a'&&code<='z';bool shifted=letter?(state.shift!=keyboard.capslocked()):state.shift;
   char ch=state.fn?char(code):(shifted?value.value_second:value.value_first);
   if(uint8_t(ch)>=32&&uint8_t(ch)<127){state.word.push_back(ch);if(ch==' ')state.space=true;}
  }
  return emitted;
 }
};
