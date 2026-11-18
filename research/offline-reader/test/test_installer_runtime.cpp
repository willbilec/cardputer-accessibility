#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../src/headless_board.cpp"
#include <esp_ota_ops.h>
static std::vector<std::string> spoken;
bool speechServiceBegin(){return true;}bool speechSay(const char*s){spoken.emplace_back(s);return true;}
bool speechSayAndWait(const char*s,uint32_t){return speechSay(s);}bool speechRepeat(){return true;}void speechStop(){}
// Override only boot selection in the existing base fake, using a macro after
// its header was included; source calls below are counted and can fail.
static int measuredSetBoot(const esp_partition_t*p){assert(p==&player);++otaBoots;return failBoot?-1:0;}
#define esp_ota_set_boot_partition measuredSetBoot
#include "../src/catalogue_launcher.cpp"
#undef esp_ota_set_boot_partition

static unsigned cases=0;
static void reset(){
 ++cases;fakeSd.resize(3201);for(size_t i=0;i<fakeSd.size();++i)fakeSd[i]=i%251;
 mbedtls_sha256_context h;mbedtls_sha256_init(&h);mbedtls_sha256_update(&h,fakeSd.data(),fakeSd.size());mbedtls_sha256_finish(&h,testDigest);
 fakeFlash.assign(3201,0xcc);fakeSdAvailable=fakeFileAvailable=true;fakeMutateOnRewind=fakeTruncateOnRewind=false;fakeReadLimit=177;fakeReadFailAt=size_t(-1);fakeSeekFail=-1;fakeSeeks=0;
 otaBegins=otaWrites=otaAborts=otaEnds=otaBoots=0;failWrite=powerCutWrite=-1;failBegin=failEnd=failBoot=failPartitionRead=false;fakeRunningFactory=true;fakeHasFactory=true;factory.address=0x10000;ESP.restarted=false;spoken.clear();
}
static void refused(){assert(otaBoots==0&&!ESP.restarted);}
int main(){
 reset();install(programs[0]);assert(fakeFlash==fakeSd&&otaBoots==1&&ESP.restarted);
 reset();fakeSdAvailable=false;install(programs[0]);assert(!otaBegins);refused();
 reset();fakeFileAvailable=false;install(programs[0]);assert(!otaBegins);refused();
 reset();fakeSd[17]^=1;install(programs[0]);assert(!otaBegins);refused();
 reset();fakeSd.pop_back();install(programs[0]);assert(!otaBegins);refused();
 reset();fakeSd.push_back(1);install(programs[0]);assert(!otaBegins);refused();
 reset();programs[0].size=0x560001;install(programs[0]);assert(!otaBegins);refused();programs[0].size=3201;
 reset();programs[0].size=0;install(programs[0]);assert(!otaBegins);refused();programs[0].size=3201;
 reset();fakeReadFailAt=500;install(programs[0]);assert(!otaBegins);refused();
 for(int seek=1;seek<=2;++seek){reset();fakeSeekFail=seek;install(programs[0]);assert(!otaBegins);refused();}
 reset();fakeHasFactory=false;install(programs[0]);assert(!otaBegins);refused();
 reset();factory.address=0x20000;install(programs[0]);assert(!otaBegins);refused();
 reset();fakeRunningFactory=false;install(programs[0]);assert(!otaBegins);refused();
 reset();failBegin=true;install(programs[0]);assert(!otaWrites);refused();
 for(int w=1;w<=19;++w){reset();failWrite=w;install(programs[0]);assert(otaAborts==1&&!otaEnds);refused();}
 for(int w=1;w<=19;++w){reset();powerCutWrite=w;bool cut=false;try{install(programs[0]);}catch(const std::runtime_error&){cut=true;}assert(cut);refused();assert(factory.address==0x10000);}
 reset();failEnd=true;install(programs[0]);refused();
 reset();fakeMutateOnRewind=true;install(programs[0]);assert(otaEnds==1);refused();
 reset();fakeTruncateOnRewind=true;install(programs[0]);assert(otaWrites==1&&otaAborts==1&&!otaEnds);refused();
 reset();failBoot=true;install(programs[0]);assert(otaBoots==1&&!ESP.restarted);
 reset();failPartitionRead=true;run(programs[0]);refused();
 std::cout<<"Actual catalogue installer: "<<cases<<" success/fault cases passed (fake SD/flash/OTA and digest; no physical recovery claim)\n";
}
