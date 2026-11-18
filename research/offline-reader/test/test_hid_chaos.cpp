// Execute the existing deterministic scenarios, then stress the real event loop.
#define main baselineMain
#include "test_hid_runtime.cpp"
#undef main
int main(){
 assert(baselineMain()==0);
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<14;++c)if(M5Cardputer.Keyboard.keyDown(r,c)){event(r,c,false);loop();}
 ESP.restarted=false;
 // A real restart ends the process; stop modeled restarts and re-enter USB.
 ui=hidUi::Controller();ui.page=hidUi::Page::Active;ui.usb=true;initialized=true;
 bool held[4][14]{};uint32_t seed=0x51a7d;
 for(unsigned i=0;i<30000;++i){
  seed=seed*1664525u+1013904223u;unsigned row=(seed>>16)%4,col=(seed>>20)%14;
  held[row][col]=!held[row][col];event(row,col,held[row][col]);loop();
  if(ui.page!=hidUi::Page::Active || ignoreUntilRelease){
   for(auto key:keyboard.last.keys)assert(key==0);
   assert(keyboard.last.modifiers==0&&mouse.buttons==0);
  }
  // Do not continue an impossible post-reboot process state.
  if(ESP.restarted){ESP.restarted=false;ui.page=hidUi::Page::Menu;releaseReports();}
  if(i%137==0){
   for(unsigned r=0;r<4;++r)for(unsigned c=0;c<14;++c)if(held[r][c]){event(r,c,false);held[r][c]=false;loop();}
   assert(!M5Cardputer.Keyboard.isPressed());
  }
 }
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<14;++c)if(held[r][c]){event(r,c,false);loop();}
 assert(!M5Cardputer.Keyboard.isPressed());
 releaseReports();for(auto key:keyboard.last.keys)assert(key==0);assert(!mouse.buttons);
 std::cout<<"Actual HID event loop: 30000 deterministic random key transitions plus release sweeps; local-menu report isolation passed\n";
}
