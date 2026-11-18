#include <array>
#include <cstdint>
#include <vector>
#include <cassert>
#include <iostream>
#include <string>
#include "../src/math_speech.h"
// Fake raw-key transport. The implementation under test is accessible_keys.h.
constexpr uint8_t KEY_FN=255,KEY_OPT=254,KEY_LEFT_CTRL=224,KEY_LEFT_SHIFT=225,KEY_LEFT_ALT=226;
constexpr uint8_t KEY_ENTER=40,KEY_BACKSPACE=42,KEY_TAB=43;
struct Point2D_t{int x=0,y=0;};
struct Value{char value_first=0,value_second=0;};
struct State{
 bool fn=false,ctrl=false,shift=false,alt=false,opt=false,enter=false,backspace=false,del=false,tab=false,up=false,down=false,left=false,right=false,esc=false,space=false;
 std::vector<char>word;
};
struct Keyboard{
 std::vector<Point2D_t>keys;std::array<Value,56>values{};bool caps=false;
 const auto&keyList(){return keys;}
 Value getKeyValue(Point2D_t p){assert(p.x>=0&&p.x<14&&p.y>=0&&p.y<4);return values[p.y*14+p.x];}
 bool capslocked(){return caps;}
 Keyboard(){
  values[0]={'1','!'};values[1]={'.','>'};values[2]={'=','+'};values[3]={'o','O'};
  values[4]={char(KEY_LEFT_CTRL),0};values[5]={char(KEY_LEFT_SHIFT),0};values[6]={char(KEY_FN),0};
  values[7]={char(KEY_ENTER),0};values[8]={char(KEY_BACKSPACE),0};values[9]={char(KEY_TAB),0};
  values[10]={char(KEY_OPT),0};values[11]={char(KEY_LEFT_ALT),0};values[12]={'v','V'};values[13]={';',':'};
 }
 void hold(std::initializer_list<int>indices){keys.clear();for(int i:indices)keys.push_back({i%14,i/14});}
};
#include "../src/accessible_keys.h"
int main(){
 using MathSpeech::describe;
 assert(describe("2+2")=="2 plus 2");
 assert(describe("a<=b && b!=c").find("less than or equal to")!=std::string::npos);
 assert(describe("a<=b && b!=c").find("not equal to")!=std::string::npos);
 assert(describe("sqrt(9)").find("open parenthesis")!=std::string::npos);
 assert(describe("x_1^2")=="x underscore 1 to the power 2");
 assert(describe("1.25e-3")=="1.25 times ten to the power minus 3");
 assert(describe("2E+10")=="2 times ten to the power 10");
 assert(describe("sensor2e3")=="sensor2e3");
 assert(describe("conv(10,\"m/s\",\"km/hr\")").find("divided by")!=std::string::npos);
 {
  Keyboard k;AccessibleKeys input;State s;k.hold({0});assert(input.poll(k,s,0)&&s.word==std::vector<char>{'1'});
  s={};assert(!input.poll(k,s,499));s={};assert(input.poll(k,s,500));s={};assert(!input.poll(k,s,599));s={};assert(input.poll(k,s,600));
  k.hold({});s={};assert(!input.poll(k,s,601));k.hold({0});s={};assert(input.poll(k,s,602));
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.hold({4,3});assert(input.poll(k,s,0)&&s.ctrl&&!s.fn&&s.word.front()=='o');
  s={};assert(!input.poll(k,s,800)); // Command chords never autorepeat.
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.hold({6,12});assert(input.poll(k,s,0)&&s.fn&&s.word.front()=='v');
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.hold({4,1});assert(input.poll(k,s,0)&&s.down&&!s.ctrl&&s.word.empty());
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.hold({4,5,1});assert(input.poll(k,s,0)&&s.down&&s.ctrl&&s.word.empty());
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.hold({7});assert(input.poll(k,s,0)&&s.enter&&s.word.empty());
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.values[0]={'8','*'};k.hold({0,5});assert(input.poll(k,s,0)&&s.word.front()=='*'&&!s.backspace);
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.caps=true;k.hold({12});assert(input.poll(k,s,0)&&s.word.front()=='V');
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.caps=true;k.hold({0});assert(input.poll(k,s,0)&&s.word.front()=='1');
 }
 {
  Keyboard k;AccessibleKeys input;State s;k.keys={{-1,0},{14,0},{0,4}};assert(!input.poll(k,s,0));
 }
 std::cout<<"Actual math speech and raw-key adapter tests passed\n";
}
