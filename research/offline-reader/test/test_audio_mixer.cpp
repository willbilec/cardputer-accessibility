#include <cassert>
#include <iostream>
#include <vector>
#include "../src/headless_board.cpp"
struct MixerIdle{};
static std::vector<int16_t> captured;
static bool cancelAfterBlock=false;
static void output(const int16_t*p,size_t n){captured.insert(captured.end(),p,p+n);if(cancelAfterBlock){M5Cardputer.Speaker.stop(1);cancelAfterBlock=false;}}
static void render(){
 fakeOnAudioWrite=output;fakeOnTaskDelay=[](unsigned){throw MixerIdle{};};
 try{headlessAudioWorker(nullptr);}catch(const MixerIdle&){}
 fakeOnTaskDelay=nullptr;fakeOnAudioWrite=nullptr;
}
int main(){
 assert(M5Cardputer.begin());M5Cardputer.Speaker.setVolume(255);
 int16_t musicPcm[256],speechPcm[256];std::fill_n(musicPcm,256,1000);std::fill_n(speechPcm,256,2000);
 assert(M5Cardputer.Speaker.playRaw(musicPcm,256,44100,false,1,0));
 assert(M5Cardputer.Speaker.playRaw(speechPcm,256,22050,false,1,1));render();
 assert(captured.size()==512);for(size_t i=0;i<512;++i)assert(captured[i]==(i<256?3000:2000));
 assert(!pending[0]&&!pending[1]);
 captured.clear();std::fill_n(musicPcm,256,30000);std::fill_n(speechPcm,256,30000);
 assert(M5Cardputer.Speaker.playRaw(musicPcm,256,44100,false,1,0));assert(M5Cardputer.Speaker.playRaw(speechPcm,256,44100,false,1,1));render();
 assert(captured.size()==256);for(auto s:captured)assert(s==32767);
 captured.clear();std::fill_n(musicPcm,256,-30000);std::fill_n(speechPcm,256,-30000);
 assert(M5Cardputer.Speaker.playRaw(musicPcm,256,44100,false,1,0));assert(M5Cardputer.Speaker.playRaw(speechPcm,256,44100,false,1,1));render();
 for(auto s:captured)assert(s==-32768);
 captured.clear();for(unsigned i=0;i<256;i+=2){musicPcm[i]=1000;musicPcm[i+1]=-500;}
 assert(M5Cardputer.Speaker.playRaw(musicPcm,256,44100,true,1,0));render();
 for(size_t i=0;i<256;++i)assert(captured[i]==(i<128?250:0));
 captured.clear();std::fill_n(speechPcm,256,2000);cancelAfterBlock=true;
 assert(M5Cardputer.Speaker.playRaw(speechPcm,256,22050,false,1,1));render();
 assert(captured.size()==256&&!pending[1]&&queues[1]->items.empty());
 captured.clear();assert(M5Cardputer.Speaker.playRaw(speechPcm,256,22050,false,1,1));render();
 assert(captured.size()==512&&!pending[1]); // new generation resumes after stop
 std::cout<<"Actual audio mixer: dual rates/channels, stereo averaging, positive/negative clipping, active cancellation and restart passed under sanitizers with fake I2S\n";
}
