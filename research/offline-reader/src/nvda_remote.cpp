#include "nvda_remote.h"
#include "nvda_frames.h"
#include "nvda_keys.h"
#include "nvda_tls.h"
#include "nvda_wake.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <atomic>
#include <memory>
#include <new>
#include <lwip/sockets.h>
#include <bearssl/bearssl.h>
#include <esp_heap_caps.h>

namespace nvdaRemote {
namespace {
constexpr size_t frameLimit=8192;
constexpr size_t textLimit=640;
constexpr const char* profilePath="/nvda-remote.ini";
constexpr char profileTemplate[]=
 "# Use the same relay address, port and session key as NVDA Remote Access.\r\n"
 "host=\r\n"
 "port=6837\r\n"
 "key=\r\n"
 "# Optional SHA-256 certificate fingerprint for a self-signed or pinned server.\r\n"
 "certificate_sha256=\r\n"
 "# Optional spoken name for this computer in recent connections.\r\n"
 "name=\r\n"
 "# Optional MAC of the computer's LAN adapter, for example AA:BB:CC:DD:EE:FF.\r\n"
 "wake_mac=\r\n";
using Report=std::array<uint8_t,8>;
enum class State { Idle, Connecting, Waiting, Ready, Offline, Unreachable, TLS, Trust, Join, Lost, Overload, Stalled,
                   Waking, WakeSent, WakeFailed };
enum class Screen { Menu, Edit, Review, Session, Forget, WakeMenu };
struct Speech {
 uint32_t queuedAt;
 unsigned revision;
 unsigned navigation;
 unsigned sequence;
 char text[textLimit]{};
};
struct Input {
 Report report;
 unsigned revision;
 uint32_t queuedAt;
};
struct Profile {
 String host,port="6837",key,pin,label,wakeMac;
};
constexpr unsigned recentLimit=5;
std::array<Profile,recentLimit> recent;
unsigned recentCount=0;
struct Session {
 Profile profile;
 bool wakeOnly=false;
 char fingerprint[65]{};
 int tlsError=0;
 QueueHandle_t reports=nullptr;
 QueueHandle_t speech=nullptr;
 std::atomic<State> state{State::Connecting};
 std::atomic<bool> closing{false},done{false},overload{false};
 std::atomic<unsigned> peersChanged{0};
 std::atomic<unsigned> speechCanceled{0};
 std::atomic<unsigned> navigation{0};
 std::atomic<unsigned> latestRapidSpeech{0};
 std::atomic<bool> speechPaused{false};
 ~Session(){if(reports)vQueueDelete(reports);if(speech)vQueueDelete(speech);}
};

std::unique_ptr<Session> session;
fs::FS* storage;
Say announce;
bool (*startWifi)();
void (*stopAudio)();
void (*enableSpeechBoost)(bool);
bool speechBoostHeld=false;
Profile profile;
Screen screen=Screen::Menu;
State lastState=State::Idle;
unsigned item=0,field=0,peerRevision=0,cancelRevision=0,spokenNavigation=0,spokenSequence=0;
std::array<unsigned,recentLimit> wakeChoices{};
unsigned wakeCount=0,wakeItem=0;
bool hasSD=false,forwarding=false,suppressed=true,alternate=defaultAlternateLayer;
bool recentConnection=false,armOnReady=false;
bool altTap=false,optionTap=false,optionTapPending=false,paused=false,remoteSpeaking=false;
String currentSpeech;
String sessionPin;
#ifdef UNIFIED_DIAGNOSTICS
std::atomic<int> diagnosticState{int(State::Idle)},diagnosticTls{0};
std::atomic<int> networkCore{-1};
std::atomic<unsigned> speechReceived{0},speechStarted{0},speechDropped{0},stalledInputs{0};
std::atomic<unsigned> frameDrops{0};
std::atomic<uint32_t> speechWaitMs{0},keySendMs{0};
#endif
Report lastReport{};
RemoteKeyboard remoteKeyboard;
constexpr const char* fields[]={"Server host","Port","Session key","Certificate fingerprint, optional","Connection name, optional",
                                "Wake on LAN MAC address, optional"};
constexpr const char* help="Semicolon and period move in the menu. Enter selects. Alt Backspace forgets a recent connection. Alt Q returns Home. Alt Enter switches between computer and local control. During control, semicolon, comma, period and slash are up, left, down and right by default. Tap Alt to switch layers. Function N opens the NVDA menu. Function with 1 through Equals sends F1 through F12. Hold Function and physical Alt for NVDA number shortcuts. Release Alt while holding Function for NVDA F key shortcuts. Control plus Option sends Left Alt. In local mode press Alt D to disconnect, or Enter to retry after an error.";

String& value(Profile& p,unsigned index) {
 switch(index){case 0:return p.host;case 1:return p.port;case 2:return p.key;case 3:return p.pin;case 4:return p.label;default:return p.wakeMac;}
}
size_t limit(unsigned index) {
 constexpr size_t limits[]={253,5,256,95,64,17};
 return limits[index];
}
bool valid(Profile& p) {
 p.host.trim();p.port.trim();p.pin.trim();p.label.trim();p.wakeMac.trim();
 if(!p.host.length()||!p.key.length())return false;
 for(unsigned i=0;i<std::size(fields);++i)if(value(p,i).length()>limit(i))return false;
 for(unsigned i=0;i<p.host.length();++i){
  char c=p.host[i];
  if(!isalnum(static_cast<unsigned char>(c))&&c!='.'&&c!='-'&&c!=':')return false;
 }
 if(!p.port.length())p.port="6837";
 for(unsigned i=0;i<p.port.length();++i)if(p.port[i]<'0'||p.port[i]>'9')return false;
 if(p.port.toInt()<1||p.port.toInt()>65535)return false;
 p.pin.replace(":","");p.pin.toLowerCase();
 if(p.pin.length()&&p.pin.length()!=64)return false;
 for(unsigned i=0;i<p.pin.length();++i)if(!isxdigit(static_cast<unsigned char>(p.pin[i])))return false;
 WakeMac mac{};
 if(p.wakeMac.length()&&!parseWakeMac(p.wakeMac.c_str(),mac))return false;
 return true;
}
bool readProfile(Profile& parsed,bool& wakeOnly) {
 auto file=storage->open(profilePath,FILE_READ);
 if(!file||file.isDirectory()||file.size()>1024)return false;
 unsigned seen=0;
 bool first=true;
 while(file.available()){
  String line=file.readStringUntil('\n');
  if(first&&line.startsWith("\xef\xbb\xbf"))line.remove(0,3);
  first=false;
  if(line.endsWith("\r"))line.remove(line.length()-1);
  String name=line;
  name.trim();
  if(!name.length()||name[0]=='#'||name[0]==';')continue;
  int equals=line.indexOf('=');
  if(equals<0)return false;
  name=line.substring(0,equals);name.trim();
  unsigned index;
  if(name=="host")index=0;
  else if(name=="port")index=1;
  else if(name=="key")index=2;
  else if(name=="certificate_sha256")index=3;
  else if(name=="name")index=4;
  else if(name=="wake_mac")index=5;
  else return false;
  if(seen&(1u<<index)||line.length()-equals-1>limit(index))return false;
  seen|=1u<<index;
  value(parsed,index)=line.substring(equals+1);
 }
 if(valid(parsed))return true;
 if(seen!=((1u<<0)|(1u<<5))&&seen!=((1u<<0)|(1u<<5)|(1u<<1)))return false;
 parsed.host.trim();parsed.port.trim();parsed.wakeMac.trim();
 if(!parsed.port.length())parsed.port="6837";
 WakeMac mac{};
 if(!parseWakeMac(parsed.wakeMac.c_str(),mac))return false;
 const Profile* match=nullptr;
 for(unsigned i=0;i<recentCount;++i){
  const auto& existing=recent[i];
  if(!existing.host.equalsIgnoreCase(parsed.host)||existing.port!=parsed.port)continue;
  if(match)return false;
  match=&existing;
 }
 if(!match)return false;
 const String address=parsed.wakeMac;
 parsed=*match;
 parsed.wakeMac=address;
 wakeOnly=true;
 return true;
}
bool createProfileTemplate(){
 if(storage->exists(profilePath))return false;
 auto file=storage->open(profilePath,FILE_WRITE);
 if(!file)return false;
 const bool written=file.print(profileTemplate)==sizeof(profileTemplate)-1;
 file.flush();file.close();
 if(!written)storage->remove(profilePath);
 return written;
}
void speak(const String& message){announce(message,false,true);}
String description(const Profile& entry){
 return entry.label.length()?entry.label:entry.host+", port "+entry.port;
}
String menuItem(){
 if(item<recentCount)return "Recent. "+description(recent[item]);
 if(item==recentCount)return "Load from SD";
 return item==recentCount+1?"Enter manually":"Wake a saved computer";
}
void speakMenu(const String& prefix=""){
 speak(prefix+menuItem()+", "+String(item+1)+" of "+String(recentCount+3));
}
void loadRecent(){
 recent={};recentCount=0;
 Preferences store;
 if(!store.begin("nvdaRemote",true))return;
 JsonDocument data;
 if(deserializeJson(data,store.getString("recent")))return;
 for(JsonObjectConst entry:data.as<JsonArrayConst>()){
  Profile saved;
  saved.host=entry["host"]|"";saved.port=entry["port"]|"6837";
  saved.key=entry["key"]|"";saved.pin=entry["pin"]|"";saved.label=entry["name"]|"";
  saved.wakeMac=entry["wake_mac"]|"";
  if(valid(saved))recent[recentCount++]=saved;
  if(recentCount==recentLimit)break;
 }
}
bool storeRecent(const std::array<Profile,recentLimit>& entries,unsigned count){
 JsonDocument data;
 auto list=data.to<JsonArray>();
 for(unsigned i=0;i<count;++i){
  const auto& entry=entries[i];
  auto obj=list.add<JsonObject>();
  obj["host"]=entry.host;obj["port"]=entry.port;obj["key"]=entry.key;
  obj["pin"]=entry.pin;obj["name"]=entry.label;
  if(entry.wakeMac.length())obj["wake_mac"]=entry.wakeMac;
 }
 String encoded;
 if(data.overflowed()||!serializeJson(data,encoded))return false;
 Preferences store;
 if(!store.begin("nvdaRemote",false)||store.putString("recent",encoded)!=encoded.length())return false;
 recent=entries;recentCount=count;
 return true;
}
bool remember(){
 auto same=[](const Profile& a,const Profile& b){return a.host==b.host&&a.port==b.port&&a.key==b.key;};
 if(recentCount&&same(recent[0],profile)&&recent[0].pin==profile.pin&&recent[0].label==profile.label&&
    recent[0].wakeMac==profile.wakeMac)return true;
 std::array<Profile,recentLimit> entries;
 entries[0]=profile;
 unsigned count=1;
 for(unsigned i=0;i<recentCount&&count<recentLimit;++i)
  if(!same(recent[i],profile))entries[count++]=recent[i];
 return storeRecent(entries,count);
}
void useSavedPin(Profile& target){
 if(target.pin.length())return;
 for(unsigned i=0;i<recentCount;++i){
  if(recent[i].host.equalsIgnoreCase(target.host)&&recent[i].port.toInt()==target.port.toInt()&&recent[i].pin.length()){
   target.pin=recent[i].pin;
   return;
  }
 }
}
void clearSpeech() {
 if(remoteSpeaking)stopAudio();
 remoteSpeaking=paused=false;currentSpeech="";
}
void focusField() {
 String label=fields[field];
 if((field<2||field>=4)&&value(profile,field).length())label+=", "+value(profile,field);
 if(field==1&&!profile.port.length())label+=". Leave blank for port 6837";
 if(field==2)label+=". Entry is silent";
 speak(label+". Enter continues. Function backtick goes back.");
}
void review(const String& prefix="") {
 screen=Screen::Review;
 speak(prefix+"Server "+profile.host+", port "+profile.port+
       (profile.pin.length()?". Using certificate fingerprint":". Using public certificate validation")+
       ". Enter connects. Function backtick returns to the menu.");
}
bool send(TlsClient& client,JsonDocument& message) {
 String line;
 serializeJson(message,line);
 if(!line.concat('\n'))return false;
 return client.write(reinterpret_cast<const uint8_t*>(line.c_str()),line.length())==line.length();
}
bool sendReport(TlsClient& client,Report& last,const Report& next,uint32_t timeoutMs=150,
                bool* navigated=nullptr) {
 if(next[2]==1)return true;
 const uint32_t started=millis();
 char batch[1024];
 size_t used=0;
 auto flush=[&](){
  if(!used)return true;
  const uint32_t elapsed=millis()-started;
  const bool written=elapsed<timeoutMs&&
      client.write(reinterpret_cast<const uint8_t*>(batch),used,timeoutMs-elapsed)==used;
  used=0;
  return written;
 };
 bool ok=true;
 transitions(last,next,[&](uint8_t usage,bool pressed){
  const auto key=windowsKey(usage);
  if(!ok||!key.vk)return;
  constexpr size_t keyLine=112;
  if(sizeof(batch)-used<keyLine&&!flush()){ok=false;return;}
  const int size=snprintf(batch+used,keyLine,
   "{\"type\":\"key\",\"vk_code\":%u,\"scan_code\":%u,\"extended\":%s,\"pressed\":%s}\n",
   unsigned(key.vk),unsigned(key.scan),key.extended?"true":"false",pressed?"true":"false");
  if(size<=0||size>=int(keyLine)){ok=false;return;}
  used+=size;
  if(navigated&&pressed&&(usage==0x28||usage==0x2b||(usage>=0x4a&&usage<=0x52&&usage!=0x4c)))
   *navigated=true;
 });
 if(ok)ok=flush();
 if(ok)last=next;
 return ok;
}
class Receiver {
 Session& app;
 static constexpr uint32_t rapidWindowMs=240;
 uint32_t lastNavigation=0;
 unsigned sequence=0;
 bool hasNavigation=false,rapidNavigation=false;
 std::array<int,16> peers{};
 bool joined=false;
 void cancel() {
  xQueueReset(app.speech);
  app.speechPaused=false;
  ++app.speechCanceled;
 }
 void speech(Speech& event) {
#ifdef UNIFIED_DIAGNOSTICS
  event.queuedAt=millis();
#endif
  event.revision=app.speechCanceled;
  event.navigation=app.navigation;
  if(xQueueSend(app.speech,&event,0)!=pdPASS){
   Speech oldest{};
#ifdef UNIFIED_DIAGNOSTICS
   if(xQueueReceive(app.speech,&oldest,0)==pdPASS)++speechDropped;
#else
   xQueueReceive(app.speech,&oldest,0);
#endif
   xQueueSend(app.speech,&event,0);
  }
 }
 bool contains(int id) const {return id>0&&std::find(peers.begin(),peers.end(),id)!=peers.end();}
 void add(JsonObjectConst peer) {
  const int id=peer["id"]|0;
  if(id<=0||contains(id)||strcmp(peer["connection_type"]|"","slave"))return;
  auto slot=std::find(peers.begin(),peers.end(),0);
  if(slot!=peers.end())*slot=id;
  else tooMany=true;
 }
 bool tooMany=false;
 bool singleton() const {return !tooMany&&std::count_if(peers.begin(),peers.end(),[](int id){return id>0;})==1;}
public:
 explicit Receiver(Session& session):app(session){}
 void navigated(){
  const uint32_t now=millis();
  rapidNavigation=hasNavigation&&now-lastNavigation<=rapidWindowMs;
  lastNavigation=now;
  hasNavigation=true;
  ++app.navigation;
 }
 bool ready() const{return joined&&singleton();}
 bool isJoined() const{return joined;}
 bool consume(const char* line) {
  JsonDocument data;
  const auto error=deserializeJson(data,line,DeserializationOption::NestingLimit(12));
  if(error==DeserializationError::NoMemory){app.overload=true;return false;}
  if(error)return true;
  const char* type=data["type"]|"";
  const auto previous=peers;
  if(!strcmp(type,"channel_joined")){
   joined=true;tooMany=false;peers.fill(0);
   for(auto peer:data["clients"].as<JsonArrayConst>())add(peer.as<JsonObjectConst>());
  }else if(!strcmp(type,"client_joined")&&joined)add(data["client"].as<JsonObjectConst>());
  else if(!strcmp(type,"client_left")&&joined){
   const int id=data["user_id"]|0;
   if(id>0)std::replace(peers.begin(),peers.end(),id,0);
  }else if(!strcmp(type,"version_mismatch")||!strcmp(type,"error"))return false;
  if(previous!=peers){
   ++app.peersChanged;
   xQueueReset(app.reports);
   cancel();
  }
  if(joined){
   const State state=ready()?State::Ready:State::Waiting;
   app.state=state;
#ifdef UNIFIED_DIAGNOSTICS
   diagnosticState=int(state);
#endif
  }
  const int origin=data["origin"]|0;
  if(!ready()||(origin&&!contains(origin)))return true;
  if(!strcmp(type,"cancel"))cancel();
  else if(!strcmp(type,"pause_speech"))app.speechPaused=data["switch"]|false;
  else if(!strcmp(type,"speak")){
#ifdef UNIFIED_DIAGNOSTICS
   ++speechReceived;
#endif
   const char* priority=data["priority"]|"";
   if((data["priority"]|-1)==2||!strcmp(priority,"NOW"))cancel();
   Speech event{};
   event.sequence=++sequence;
   const bool preferLatest=rapidNavigation&&millis()-lastNavigation<=rapidWindowMs;
   bool published=false;
   size_t used=0;
   auto emit=[&](){
    if(!used)return;
    event.text[used]='\0';
    // Replace whole announcements, preserving the order of each one's chunks.
    if(preferLatest&&!published){app.latestRapidSpeech=event.sequence;published=true;}
    speech(event);
    used=0;
   };
   for(JsonVariantConst entry:data["sequence"].as<JsonArrayConst>()){
    const char* text=entry.as<const char*>();
    if(!text||!*text)continue;
    if(used+1<textLimit&&used)event.text[used++]=' ';
    while(*text){
     size_t length=1;
     while(length<4&&(uint8_t(text[length])&0xc0)==0x80)++length;
     if(used+length>=textLimit)emit();
     memcpy(event.text+used,text,length);
     used+=length;
     text+=length;
    }
   }
   emit();
  }
  return true;
 }
};

bool sendWake(const String& target){
 WakeMac mac{};
 if(!parseWakeMac(target.c_str(),mac))return false;
 const auto packet=magicPacket(mac);
 const auto ip=WiFi.localIP();
 const auto mask=WiFi.subnetMask();
 const IPAddress broadcast(ip[0]|uint8_t(~mask[0]),ip[1]|uint8_t(~mask[1]),
                           ip[2]|uint8_t(~mask[2]),ip[3]|uint8_t(~mask[3]));
 const int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
 if(fd<0)return false;
 const int allowed=1;
 sockaddr_in address{};
 address.sin_family=AF_INET;
 address.sin_port=htons(9);
 address.sin_addr.s_addr=uint32_t(broadcast);
 const bool sent=setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&allowed,sizeof(allowed))==0&&
     sendto(fd,packet.data(),packet.size(),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&address),sizeof(address))==int(packet.size());
 ::close(fd);
 return sent;
}

State run(Session& app) {
 const uint32_t started=millis();
 while(!app.closing&&WiFi.status()!=WL_CONNECTED&&millis()-started<20000)vTaskDelay(pdMS_TO_TICKS(50));
 if(app.closing||WiFi.status()!=WL_CONNECTED)return State::Offline;
 if(app.wakeOnly)return sendWake(app.profile.wakeMac)?State::WakeSent:State::WakeFailed;
 struct AwakeWifi {
  wifi_ps_type_t previous=WiFi.getSleep();
  AwakeWifi(){WiFi.setSleep(false);}
  ~AwakeWifi(){WiFi.setSleep(previous);}
 } awake;
 const bool usePin=app.profile.pin.length();
 while(!usePin&&!app.closing&&time(nullptr)<1700000000&&millis()-started<10000)vTaskDelay(pdMS_TO_TICKS(50));
 if(app.closing)return State::Idle;

 TlsClient client;
 const char* host=app.profile.host.c_str();
 const uint16_t port=app.profile.port.toInt();
 const bool haveClock=time(nullptr)>=1700000000;
 bool wakeAttempted=false;
 bool wakeFailed=false;
 auto connectTls=[&](bool verify){
  if(client.connect(host,port,verify,app.fingerprint))return true;
  app.tlsError=client.lastError();
  const bool canWake=app.tlsError==TlsClient::unreachable||app.tlsError==TlsClient::timedOut||
                     app.tlsError==TlsClient::transportClosed;
  if(!canWake||wakeAttempted||app.closing||!app.profile.wakeMac.length())return false;
  wakeAttempted=true;
  if(!sendWake(app.profile.wakeMac)){wakeFailed=true;return false;}
  app.state=State::Waking;
  const uint32_t sent=millis();
  while(!app.closing&&WiFi.status()==WL_CONNECTED&&millis()-sent<8000)vTaskDelay(pdMS_TO_TICKS(50));
  if(app.closing||WiFi.status()!=WL_CONNECTED)return false;
  app.state=State::Connecting;
  if(client.connect(host,port,verify,app.fingerprint))return true;
  app.tlsError=client.lastError();
  return false;
 };
 auto connectionFailure=[&](){
  if(app.closing)return State::Idle;
  if(wakeFailed)return State::WakeFailed;
  return app.tlsError==TlsClient::unreachable?State::Unreachable:State::TLS;
 };
 bool verified=false;
 if(!usePin&&haveClock){
  verified=connectTls(true);
  if(!verified&&(app.tlsError<BR_ERR_X509_INVALID_VALUE||app.tlsError>BR_ERR_X509_NOT_TRUSTED))
   return connectionFailure();
 }
 if(!verified){
  if(app.closing)return State::Idle;
  // A probe can obtain the certificate, but cannot transmit credentials.
  if(!connectTls(false))return connectionFailure();
  if(app.profile.pin!=app.fingerprint)return State::Trust;
 }
 if(app.closing)return State::Idle;
 app.tlsError=0;
 app.reports=xQueueCreate(24,sizeof(Input));
 app.speech=xQueueCreate(5,sizeof(Speech));
 if(!app.reports||!app.speech)return State::Overload;
 // The frame and ArduinoJson's allocation must coexist. Prefer the smaller
 // frame before exhausting the last contiguous block.
 size_t capacity=ESP.getFreeHeap()<frameLimit+8192?frameLimit/2:frameLimit;
 std::unique_ptr<char[]> line(new(std::nothrow) char[capacity]);
 if(!line&&capacity==frameLimit){
  capacity=frameLimit/2;
  line.reset(new(std::nothrow) char[capacity]);
 }
 if(!line)return State::Overload;
 Frames frames(line.get(),capacity);
 int keepAlive=1,idle=15,interval=5,probes=3;
 setsockopt(client.fd(),SOL_SOCKET,SO_KEEPALIVE,&keepAlive,sizeof(keepAlive));
 setsockopt(client.fd(),IPPROTO_TCP,TCP_KEEPIDLE,&idle,sizeof(idle));
 setsockopt(client.fd(),IPPROTO_TCP,TCP_KEEPINTVL,&interval,sizeof(interval));
 setsockopt(client.fd(),IPPROTO_TCP,TCP_KEEPCNT,&probes,sizeof(probes));
 {
  JsonDocument message;
  message["type"]="protocol_version";message["version"]=2;
  if(!send(client,message))return State::Join;
  message.clear();
  message["type"]="join";message["channel"]=app.profile.key;message["connection_type"]="master";
  if(!send(client,message))return State::Join;
 }
 Report sent{};
 Receiver receiver(app);
 const uint32_t joinStarted=millis();
 while(client.connected()&&WiFi.status()==WL_CONNECTED&&!app.overload){
  if(app.closing){
   if(receiver.ready())sendReport(client,sent,{});
   return State::Idle;
  }
  Input next{};
  for(unsigned i=0;i<4&&!app.closing&&xQueueReceive(app.reports,&next,0)==pdPASS;++i){
   if(!receiver.ready()||next.revision!=app.peersChanged)continue;
   constexpr uint32_t maxAgeMs=250;
   const uint32_t age=millis()-next.queuedAt;
   if(age>=maxAgeMs){
#ifdef UNIFIED_DIAGNOSTICS
    ++stalledInputs;
#endif
    sendReport(client,sent,{});
    return State::Stalled;
   }
   bool navigated=false;
   if(!sendReport(client,sent,next.report,std::min<uint32_t>(150,maxAgeMs-age),&navigated)){
#ifdef UNIFIED_DIAGNOSTICS
    ++stalledInputs;
#endif
    return State::Stalled;
   }
#ifdef UNIFIED_DIAGNOSTICS
   keySendMs=millis()-next.queuedAt;
#endif
   if(navigated)receiver.navigated();
  }
  if(!receiver.isJoined()&&millis()-joinStarted>10000)return State::Join;
  uint8_t bytes[512];
  int count=client.available()>0?client.read(bytes,sizeof(bytes)):0;
#ifdef UNIFIED_DIAGNOSTICS
  const unsigned dropped=frames.dropped();
#endif
  for(int i=0;i<count;++i){
   if(const char* frame=frames.push(bytes[i])){
    unsigned revision=app.peersChanged;
    if(!receiver.consume(frame))return app.overload?State::Overload:State::Join;
    if(revision!=app.peersChanged&&!sendReport(client,sent,{}))return State::Lost;
   }
  }
#ifdef UNIFIED_DIAGNOSTICS
  if(frames.dropped()!=dropped)frameDrops+=frames.dropped()-dropped;
#endif
  vTaskDelay(1);
 }
 if(app.overload&&client.connected()&&receiver.ready())sendReport(client,sent,{});
 return app.overload?State::Overload:State::Lost;
}
void worker(void* context){
 auto& app=*static_cast<Session*>(context);
#ifdef UNIFIED_DIAGNOSTICS
 networkCore=xPortGetCoreID();
#endif
 const auto result=run(app);
#ifdef UNIFIED_DIAGNOSTICS
 diagnosticTls=app.tlsError;
#endif
#ifdef UNIFIED_DIAGNOSTICS
 diagnosticState=int(result);
#endif
 app.state=result;
 app.done=true;
 vTaskDelete(nullptr);
}
void holdSpeechBoost(bool hold){
 if(speechBoostHeld==hold)return;
 speechBoostHeld=hold;
 enableSpeechBoost(!hold);
}
void reap(bool restoreBoost=true){
 if(session&&session->done){
  session.reset();
  if(restoreBoost)holdSpeechBoost(false);
 }
}
void connect(bool wakeOnly=false) {
 reap(false);
 if(session){speak("Previous connection is closing. Press Enter to retry.");return;}
 holdSpeechBoost(!wakeOnly);
 if(!startWifi()){
  holdSpeechBoost(false);speak("Wi-Fi is unavailable. Enable and save a network in Settings.");return;
 }
 session.reset(new(std::nothrow) Session);
 if(!session){
  holdSpeechBoost(false);speak("Not enough memory to start NVDA Remote.");return;
 }
 session->profile=profile;
 session->wakeOnly=wakeOnly;
 if(sessionPin.length())session->profile.pin=sessionPin;
 if(xTaskCreatePinnedToCore(worker,"nvda-remote",8192,session.get(),2,nullptr,0)!=pdPASS){
  session.reset();holdSpeechBoost(false);speak("Could not start NVDA Remote.");return;
 }
 screen=Screen::Session;lastState=State::Connecting;peerRevision=cancelRevision=0;
 armOnReady=!wakeOnly&&recentConnection;
 speak(wakeOnly?"Waking "+description(profile)+".":"Connecting to "+profile.host+".");
}
void speakWakeChoice(const String& prefix=""){
 speak(prefix+description(recent[wakeChoices[wakeItem]])+", "+String(wakeItem+1)+" of "+String(wakeCount)+". Enter sends wake. Backspace goes back.");
}
void openWakeMenu(){
 wakeItem=wakeCount=0;
 for(unsigned i=0;i<recentCount;++i)if(recent[i].wakeMac.length())wakeChoices[wakeCount++]=i;
 if(!wakeCount){speak("No recent computers have a Wake on LAN MAC address. Add wake MAC using Load from SD or Enter manually.");return;}
 screen=Screen::WakeMenu;
 speakWakeChoice("Wake a saved computer. ");
}
void report(const Report& next) {
 if(next==lastReport||next[2]==1||!session||session->state!=State::Ready)return;
 const Input input{next,peerRevision,millis()};
 if(xQueueSend(session->reports,&input,0)!=pdPASS)session->overload=true;
 else lastReport=next;
}
}

void open(fs::FS& fs,bool sdReady,Say say,bool (*wifi)(),void (*stop)(),void (*speechBoost)(bool)) {
 storage=&fs;hasSD=sdReady;announce=say;startWifi=wifi;stopAudio=stop;enableSpeechBoost=speechBoost;
 profile=Profile{};sessionPin="";screen=Screen::Menu;item=field=0;
 recentConnection=armOnReady=false;
 alternate=defaultAlternateLayer;
 forwarding=altTap=optionTap=optionTapPending=false;suppressed=true;lastReport={};
 remoteKeyboard={};
 loadRecent();
 speakMenu("NVDA Remote. Semicolon and period move. ");
}
void close() {
 localShortcut();
 if(session)session->closing=true;
 profile=Profile{};
 sessionPin="";
}
bool busy(){reap();return bool(session);}
#ifdef UNIFIED_DIAGNOSTICS
void status(){
 Serial.printf("NVDA_STATUS state=%d tls=%d free=%u largest=%u\n",int(diagnosticState),int(diagnosticTls),
               ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
}
void performance(){
 Serial.printf("NVDA_PERF packets=%u submitted=%u dropped=%u wait_ms=%u key_ms=%u stalls=%u frames=%u\n",
               unsigned(speechReceived),unsigned(speechStarted),unsigned(speechDropped),
               unsigned(speechWaitMs),unsigned(keySendMs),unsigned(stalledInputs),unsigned(frameDrops));
}
int taskCore(){return networkCore;}
#endif
void localShortcut() {
 report({});suppressed=true;altTap=optionTap=optionTapPending=false;
 remoteKeyboard={};
 clearSpeech();
 if(session&&session->state==State::Ready){
  xQueueReset(session->speech);
  ++session->speechCanceled;
 }
}
void key(const HeadlessKeyboard::Event& e,const HeadlessKeyboard::State& s) {
 const char base=keys::plain[e.row][e.col];
 const bool enter=e.row==2&&e.col==13;
 const bool escape=s.fn&&e.row==0&&e.col==0;
 if(screen==Screen::Session&&session&&session->state==State::Trust){
  if(!e.down)return;
  if(escape){
   sessionPin="";screen=Screen::Menu;item=0;speakMenu("Certificate declined. ");
  }else if(s.alt&&base=='f'){
   String reading="SHA 256 fingerprint. ";
   for(unsigned i=0;i<64;++i){
    reading+=session->fingerprint[i];reading+=(i%8==7?". ":" ");
   }
   speak(reading);
  }else if(s.alt&&(base=='y'||base=='o')){
   if(!session->done)return;
   sessionPin=session->fingerprint;
   if(base=='y'){
    const String previous=profile.pin;
    profile.pin=sessionPin;
    if(!remember()){
     profile.pin=previous;
     speak("Could not save trust. Alt O connects once or Alt Y retries saving.");return;
    }
   }
   connect();
  }
  return;
 }
 if(e.down&&s.alt&&!s.fn&&!s.ctrl&&!s.opt&&enter&&screen==Screen::Session){
  localShortcut();
  if(!session||session->state!=State::Ready){forwarding=false;speak("Waiting for one controlled computer.");return;}
  forwarding=!forwarding;
  speak(forwarding?"Controlling remote computer":"Local mode");
  return;
 }
 if(forwarding){
  const int position=e.row*14+e.col;
  auto& k=M5Cardputer.Keyboard;
  if(position==keyboardMap::altKey||position==keyboardMap::optionKey){
   bool& tap=position==keyboardMap::altKey?altTap:optionTap;
   if(e.down){
    tap=!suppressed;
    for(int i=0;i<keyboardMap::positions;++i)if(i!=position&&k.keyDown(i/14,i%14))tap=false;
   }else if(tap){
    tap=false;
    if(position==keyboardMap::altKey){
     localShortcut();alternate=!alternate;speak(alternate?"Alternate layer":"Regular layer");
    }else optionTapPending=true;
   }
  }else if(e.down)altTap=optionTap=false;
  return;
 }
 if(!e.down)return;
 if(s.alt&&base=='h'){speak(help);return;}
 const bool backspace=e.row==0&&e.col==13&&!s.fn;
 const bool menuKey=!s.alt&&!s.ctrl&&!s.opt;
 const bool moveMenu=menuKey&&(base==';'||base=='.'||base==9);
 const bool previous=base==';'||(base==9&&s.shift);
 if(screen==Screen::Menu){
  if(moveMenu){
   const unsigned count=recentCount+3;
   item=(item+(previous?count-1:1))%count;
   speakMenu();
  }else if(s.alt&&backspace&&item<recentCount){
   screen=Screen::Forget;
   speak("Forget "+description(recent[item])+"? Enter confirms. Function backtick cancels.");
  }else if(menuKey&&enter){
   sessionPin="";
   recentConnection=item<recentCount;
   if(item<recentCount){
    profile=recent[item];connect();
   }else if(item==recentCount+2)openWakeMenu();
   else if(item==recentCount+1){
    profile=Profile{};profile.port="";field=0;screen=Screen::Edit;focusField();
   }else{
    if(!hasSD){speak("No SD card is available.");return;}
    if(!storage->exists(profilePath)){
     speak(createProfileTemplate()?
       "Created NVDA Remote dot I N I at the root of the SD card. Fill in your connection details, then choose Load from SD again.":
       "Could not create the NVDA Remote template on the SD card.");
     return;
    }
    Profile parsed;
    bool wakeOnly=false;
    if(!readProfile(parsed,wakeOnly)){speak("Check the connection details in NVDA Remote dot I N I on the SD card.");return;}
    profile=parsed;
    useSavedPin(profile);
    const bool saved=remember();
    if(saved)item=0;
    if(wakeOnly)speakMenu(saved?"Wake on LAN address saved. ":"Could not save the Wake on LAN address. ");
    else review(saved?"Imported and saved to recent connections. ":"Imported. Could not save to recent connections. ");
   }
  }
 }else if(screen==Screen::WakeMenu){
  if(escape||(menuKey&&backspace)){screen=Screen::Menu;speakMenu();}
  else if(moveMenu){wakeItem=(wakeItem+(previous?wakeCount-1:1))%wakeCount;speakWakeChoice();}
  else if(menuKey&&enter){profile=recent[wakeChoices[wakeItem]];connect(true);}
 }else if(screen==Screen::Forget){
  if(escape){screen=Screen::Menu;speakMenu();}
  else if(menuKey&&enter){
   auto entries=recent;
   for(unsigned i=item+1;i<recentCount;++i)entries[i-1]=entries[i];
   entries[recentCount-1]=Profile{};
   const bool removed=storeRecent(entries,recentCount-1);
   screen=Screen::Menu;speakMenu(removed?"Connection forgotten. ":"Could not forget the connection. ");
  }
 }else if(screen==Screen::Edit){
  String& input=value(profile,field);
  if(escape){
   if(field){--field;focusField();}
   else{screen=Screen::Menu;speakMenu();}
  }else if(enter){
   if(field+1<std::size(fields)){++field;focusField();}
   else if(valid(profile)){useSavedPin(profile);review();}
   else{speak("Check the host, port, session key, fingerprint and wake MAC address. Function backtick moves to the previous field.");}
  }else if(!s.opt&&!s.alt&&!s.ctrl&&!s.fn){
   if(backspace){
    if(input.length())input.remove(input.length()-1);
    if(field!=2)speak("Backspace");
   }else{
    const char c=s.shift?keys::shifted[e.row][e.col]:base;
    if(c>=32&&c<=126&&input.length()<limit(field)){
     input+=c;
     if(field!=2)speak(c==' '?"Space":String(c));
    }
   }
  }
 }else if(screen==Screen::Review){
  if(escape){screen=Screen::Menu;speakMenu();}
  else if(enter)connect();
 }else if(menuKey&&backspace&&session&&session->wakeOnly){
  session->closing=true;
  screen=Screen::WakeMenu;speakWakeChoice();
 }else if((s.alt&&base=='d')||escape){
  localShortcut();
  if(session)session->closing=true;
  sessionPin="";
  screen=Screen::Menu;item=0;speakMenu("Disconnected. ");
 }else if(enter&&(!session||session->done))connect(session&&session->wakeOnly);
}
void keyboard(const keyboardMap::Map& map) {
 auto& k=M5Cardputer.Keyboard;
 if(suppressed){
  if(!k.isPressed())suppressed=false;
  return;
 }
 if(forwarding&&session&&session->state==State::Ready&&peerRevision==session->peersChanged){
  if(optionTapPending){
   optionTapPending=false;
   report(keyboardMap::report(map,alternate,false,true,[](int){return false;}));
  }
  report(remoteKeyboard.report(map,alternate,[&](int i){return k.keyDown(i/14,i%14);}));
 }
}
void tick(bool speaking) {
 if(!session)return;
 if(session->closing){reap();return;}
 if(screen!=Screen::Session)return;
 const State state=session->state;
 const unsigned revision=session->peersChanged;
 if(state!=lastState||revision!=peerRevision){
  lastState=state;peerRevision=revision;
  forwarding=false;suppressed=true;altTap=optionTap=optionTapPending=false;lastReport={};clearSpeech();
  remoteKeyboard={};
  // The TLS buffers remain live for the entire session. Returning the
  // optional speech allocation here can starve protocol and audio work.
  switch(state){
   case State::Waking:speak("Wake request sent. Retrying in eight seconds.");break;
   case State::Connecting:speak("Retrying NVDA Remote.");break;
   case State::WakeSent:
    screen=Screen::WakeMenu;speakWakeChoice("Wake request sent. ");break;
   case State::WakeFailed:speak(session->wakeOnly?"Could not send the wake request. Enter retries. Backspace goes back.":
                                                "Could not send the wake request. Enter retries.");break;
   case State::Waiting:speak("Channel joined. Waiting for exactly one controlled computer.");break;
   case State::Ready:
    forwarding=armOnReady;
    armOnReady=false;
    {
     const bool saved=remember();
     String message=forwarding?"Controlling remote computer.":"Controlled computer available. Press Alt Enter to control.";
     if(!saved)message+=" Recent connection could not be saved.";
     speak(message);
    }
    break;
   case State::Offline:speak("Wi-Fi connection failed. Enter retries.");break;
   case State::Unreachable:speak("Could not reach the NVDA host and port from the Cardputer. Enter retries.");break;
   case State::TLS:
    if(session->tlsError==TlsClient::noMemory)
     speak("Not enough Cardputer memory for the secure connection. Enter retries.");
    else if(session->tlsError==TlsClient::timedOut)
     speak("TLS handshake timed out. Enter retries.");
    else if(session->tlsError==TlsClient::transportClosed)
     speak("Connection closed during the TLS handshake. Enter retries.");
    else speak("TLS handshake failed. Error "+String(session->tlsError)+". Enter retries.");
    break;
   case State::Trust:
    speak(String(session->profile.pin.length()?"The NVDA host certificate changed. ":"The NVDA host certificate is not trusted. ")+
          profile.host+", port "+profile.port+
          ". Alt F reads the fingerprint. Verify it on your computer. Alt Y trusts and saves. Alt O trusts once. Function backtick cancels.");
    break;
   case State::Join:speak("Could not join NVDA Remote. Check the connection details. Enter retries.");break;
   case State::Lost:speak("NVDA Remote connection lost. Enter retries.");break;
   case State::Overload:speak("NVDA Remote stopped because its buffers filled. Enter retries.");break;
   case State::Stalled:speak("Keyboard connection stalled. Queued keys discarded. Enter retries.");break;
   default:break;
  }
  speaking=true;
 }
 if(state!=State::Ready)return;
 if(remoteSpeaking&&!speaking){remoteSpeaking=false;currentSpeech="";}
 const unsigned cancellation=session->speechCanceled;
 if(cancelRevision!=cancellation){
  cancelRevision=cancellation;
  if(remoteSpeaking)speaking=false;
  clearSpeech();
 }
 const bool pauseRequested=session->speechPaused;
 if(pauseRequested&&!paused){
  if(remoteSpeaking){stopAudio();speaking=remoteSpeaking=false;}
 }else if(!pauseRequested&&paused&&!speaking&&currentSpeech.length()){
  announce(currentSpeech,true,true);speaking=remoteSpeaking=true;
 }
 paused=pauseRequested;
 const unsigned navigation=session->navigation;
 if(paused||(speaking&&remoteSpeaking&&spokenNavigation==navigation&&
             spokenSequence>=session->latestRapidSpeech))return;
 Speech event{};
 while(xQueueReceive(session->speech,&event,0)==pdPASS){
  if(event.revision!=session->speechCanceled||event.navigation!=session->navigation||
     event.sequence<session->latestRapidSpeech){
#ifdef UNIFIED_DIAGNOSTICS
   ++speechDropped;
#endif
   continue;
  }
#ifdef UNIFIED_DIAGNOSTICS
  speechWaitMs=millis()-event.queuedAt;
#endif
#ifdef UNIFIED_DIAGNOSTICS
  ++speechStarted;
#endif
  spokenNavigation=event.navigation;
  spokenSequence=event.sequence;
  currentSpeech=event.text;
  announce(currentSpeech,true,speaking);
  remoteSpeaking=true;
  break;
 }
}
}
