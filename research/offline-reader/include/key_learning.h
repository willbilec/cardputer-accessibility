#pragma once
#include <cstdint>
#include <string>

// Physical positions/layers follow M5Stack's MIT-licensed Cardputer key map.
namespace keys {
inline constexpr uint8_t plain[4][14] = {
 {'`','1','2','3','4','5','6','7','8','9','0','-','=',8},
 {9,'q','w','e','r','t','y','u','i','o','p','[',']','\\'},
 {1,2,'a','s','d','f','g','h','j','k','l',';', '\'',13},
 {3,4,5,'z','x','c','v','b','n','m',',','.','/',' '}};
inline constexpr uint8_t shifted[4][14] = {
 {'~','!','@','#','$','%','^','&','*','(',')','_','+',8},
 {9,'Q','W','E','R','T','Y','U','I','O','P','{','}','|'},
 {1,2,'A','S','D','F','G','H','J','K','L',':','"',13},
 {3,4,5,'Z','X','C','V','B','N','M','<','>','?',' '}};
inline constexpr uint8_t usage[4][14] = {
 {0x35,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2d,0x2e,0x2a},
 {0x2b,0x14,0x1a,8,0x15,0x17,0x1c,0x18,0x0c,0x12,0x13,0x2f,0x30,0x31},
 {0,0,4,0x16,7,9,0x0a,0x0b,0x0d,0x0e,0x0f,0x33,0x34,0x28},
 {0,0,0,0x1d,0x1b,6,0x19,5,0x11,0x10,0x36,0x37,0x38,0x2c}};
inline uint8_t hidUsage(int row, int col, bool fn) {
 if(row<0 || row>=4 || col<0 || col>=14) return 0;
 if(!fn) return usage[row][col];
 if(row==0) return col==0 ? 0x29 : col==13 ? 0x4c : 0x39+col;
 if(row==2 && col==11) return 0x52;
 if(row==3 && col>=10 && col<=12) return col==10 ? 0x50 : col==11 ? 0x51 : 0x4f;
 return 0;
}
inline std::string name(uint8_t c, bool describeType=false) {
 switch(c) {
 case 1:return "Function"; case 2:return "Shift"; case 3:return "Control";
 case 4:return "Option"; case 5:return "Alt"; case 8:return "Backspace";
 case 9:return "Tab"; case 13:return "Enter"; case ' ':return "Space";
 case '`':return "Backtick"; case '~':return "Tilde"; case '!':return "Exclamation mark";
 case '@':return "At sign"; case '#':return "Hash"; case '$':return "Dollar sign";
 case '%':return "Percent"; case '^':return "Caret"; case '&':return "Ampersand";
 case '*':return "Asterisk"; case '(':return "Left parenthesis"; case ')':return "Right parenthesis";
 case '-':return "Minus"; case '_':return "Underscore"; case '=':return "Equals"; case '+':return "Plus";
 case '[':return "Left bracket"; case ']':return "Right bracket"; case '{':return "Left brace";
 case '}':return "Right brace"; case '\\':return "Backslash"; case '|':return "Vertical bar";
 case ';':return "Semicolon"; case ':':return "Colon"; case '\'':return "Apostrophe";
 case '"':return "Quote"; case ',':return "Comma"; case '<':return "Less than";
 case '.':return "Period"; case '>':return "Greater than"; case '/':return "Slash"; case '?':return "Question mark";
 default:
  if((c>='a' && c<='z') || (c>='A' && c<='Z')) {
   static const char *letters[]={"ay","bee","see","dee","ee","ef","gee","aitch","eye","jay","kay","el","em","en","oh","pee","cue","ar","ess","tee","you","vee","double you","ex","why","zee"};
   return std::string(describeType?"Letter ":"")+letters[(c>='a'?c-'a':c-'A')];
  }
  return std::string(1,char(c));
 }
}
inline std::string describe(int row, int col, bool shift, bool fn, bool ctrl=false, bool alt=false) {
 if(row<0 || row>=4 || col<0 || col>=14) return "Unknown key";
 uint8_t base=plain[row][col];
 if(base==1) return "Function. Hold with the number row for F keys, or with punctuation for arrow keys";
 if(base==2) return "Shift. Hold to type capital letters or the upper symbol on a key";
 if(base==3) return "Control. Hold with another key for shortcuts in the connected application";
 if(base==4) return "Option. Local board modifier. Function plus Option with M opens this app's menu";
 if(base==5) return "Alt. Modifier for shortcuts in the connected application";
 std::string result;
 if(ctrl) { result+="Control "; } if(alt) { result+="Alt "; }
 if(fn) {
  uint8_t hid=hidUsage(row,col,true);
  if(hid>=0x3a && hid<=0x45) return result+"Function F "+std::to_string(hid-0x39)+". Its action depends on the connected application";
  if(hid==0x29) return result+"Escape. Cancels or leaves the current action";
  if(hid==0x4c) return result+"Delete. Deletes forward in a text field";
  if(hid>=0x4f && hid<=0x52) {
   const char *arrows[]={"Right","Left","Down","Up"};
   return result+arrows[hid-0x4f]+" arrow. Moves the cursor or selection";
  }
  return result+"Function "+name(base,true)+". No function-layer HID assignment";
 }
 if(shift) { result+="Shift "; }
 result+=name(shift?shifted[row][col]:base,true);
 switch(base) {
 case 8:return result+". Deletes the character before the cursor";
 case 9:return result+". Moves to the next field or control";
 case 13:return result+". Activates a choice or starts a new line";
 case ' ':return result+". Types a space";
 default:return result+((ctrl||alt)?". Shortcut depends on the connected application":". Types this character; the host keyboard layout can change symbols");
 }
}
}
