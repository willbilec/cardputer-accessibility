#include <cassert>
#include <iostream>
#include <string>
#include "../src/headless_board.cpp"
static std::vector<std::string> spoken;
bool speechServiceBegin(){return true;}
bool speechSay(const char *s){spoken.emplace_back(s);return true;}
bool speechSayAndWait(const char *s,uint32_t){return speechSay(s);}
bool speechRepeat(){return true;}
void speechStop(){}
#include "../src/key_learning_app.cpp"
static void event(int row,int col,bool down){int raw=(col/2)*10+row+(col%2?4:0)+1;Wire.events.push_back(raw|(down?128:0));}
static void press(int row,int col){event(row,col,true);loop();}
static void release(int row,int col){event(row,col,false);loop();}
int main(){
 setup();press(2,13);assert(learner.page==learningUi::Page::Explore && !ESP.restarted);
 assert(spoken.back().find("Enter")!=std::string::npos);release(2,13);
 press(2,0);press(0,1);assert(spoken.back().find("F 1")!=std::string::npos);
 auto count=spoken.size();release(0,1);release(2,0);assert(spoken.size()==count);
 press(2,1);press(3,3);assert(spoken.back().find("Shift Letter zee")!=std::string::npos);
 release(3,3);release(2,1);auto explored=learner.lastKey;
 fakePins[0]=LOW;fakeTime=1000;loop();fakePins[0]=HIGH;fakeTime=1050;loop();fakeTime=1500;loop();
 assert(learner.page==learningUi::Page::Menu);
 press(3,11);release(3,11);press(2,13);release(2,13);assert(spoken.back()==explored);
 event(2,0,true);event(3,1,true);event(1,1,true);loop();assert(learner.page==learningUi::Page::Confirm);
 release(1,1);assert(learner.page==learningUi::Page::Confirm);
 event(2,0,false);event(3,1,false);loop();press(2,13);assert(ESP.restarted);
 std::cout<<"Actual learning event loop: names/layers, no Enter activation, release silence, Go menu, repeat and confirmed return passed; no HID transport linked\n";
}
