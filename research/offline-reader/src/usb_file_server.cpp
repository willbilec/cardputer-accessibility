#include "usb_sd.h"
#include <Arduino.h>
#include <SD.h>
#include <memory>
#include <new>
#ifdef UNIFIED_WIFI_SD
#include <WiFi.h>
#include <lwip/sockets.h>
#endif

namespace {
constexpr size_t chunkSize=256;
constexpr size_t maxLine=4*chunkSize+32;
enum class Operation { Idle, List, Read, Write, StreamRead, StreamWrite };
Operation operation=Operation::Idle;
bool canWrite=false, overflow=false, streamFailed=false, replacing=false;
bool (*setClock)(int64_t)=nullptr;
File file;
String destination, temporary, reply;
char input[maxLine+1];
size_t inputSize=0, sent=0;
constexpr size_t streamCapacity=4096;
std::unique_ptr<uint8_t[]> streamBuffer;
size_t streamRemaining=0, streamSize=0, streamSent=0;
uint32_t lastCommand=0;
#ifdef UNIFIED_BUILDER_HELP_UPLOAD
bool helpOnly=false, helpFinished=false;

bool helpFile(const String &path){
 if(!path.startsWith("/Help/")||!path.endsWith(".txt")||path.length()>80||path.length()<=10)return false;
 for(size_t i=6;i<path.length()-4;++i){
  const char ch=path[i];
  if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'))return false;
 }
 return true;
}
#endif
#ifdef UNIFIED_WIFI_SD
constexpr uint16_t sharePort=47631;
constexpr char discoverRequest[]="CARDFILES DISCOVER 1";
WiFiServer server(sharePort, 1);
WiFiClient client;
WiFiUDP discovery;
bool networkMode=false, authenticated=false, rejected=false;
String accessCode;

bool sharedFolder(String name){
 return name.equalsIgnoreCase("Music")||name.equalsIgnoreCase("Recordings")||
        name.equalsIgnoreCase("Transfers");
}
#endif

void cancel(){
 if(file)file.close();
 if(!temporary.isEmpty())SD.remove(temporary);
 temporary="";destination="";
 streamBuffer.reset();
 streamRemaining=streamSize=streamSent=0;streamFailed=replacing=false;
 operation=Operation::Idle;
}

void respond(const String &message){
 reply=message;
 reply+='\n';
 sent=0;
}

int digit(char c){
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 return -1;
}

bool decode(const char *hex,uint8_t *bytes,size_t &size,size_t maximum){
 size_t length=strlen(hex);
 if(length%2||length/2>maximum)return false;
 size=length/2;
 for(size_t i=0;i<size;++i){
  int high=digit(hex[2*i]),low=digit(hex[2*i+1]);
  if(high<0||low<0)return false;
  bytes[i]=(high<<4)|low;
 }
 return true;
}

String encode(const uint8_t *bytes,size_t size){
 const char hex[]="0123456789abcdef";
 String result;
 if(!result.reserve(size*2))return "";
 for(size_t i=0;i<size;++i){result+=hex[bytes[i]>>4];result+=hex[bytes[i]&15];}
 return result;
}

bool readPath(const char *hex,String &path){
 uint8_t bytes[chunkSize+1];
 size_t size;
 if(!decode(hex,bytes,size,chunkSize)||!size||bytes[0]!='/')return false;
 for(size_t i=0;i<size;++i)if(bytes[i]<32||bytes[i]=='\\')return false;
 bytes[size]=0;
 path=reinterpret_cast<const char *>(bytes);
 if(path=="/")return true;
 if(path.endsWith("/")||path.indexOf("//")>=0)return false;
#ifdef UNIFIED_WIFI_SD
 int firstSlash=path.indexOf('/',1);
 if(networkMode&&!sharedFolder(firstSlash<0?path.substring(1):path.substring(1,firstSlash)))return false;
#endif
 for(int start=1;start<int(path.length());){
  int slash=path.indexOf('/',start);
  String part=slash<0?path.substring(start):path.substring(start,slash);
  String lower=part;
  lower.toLowerCase();
  if(part=="."||part==".."||lower.startsWith(".cardputer-transfer-"))return false;
#ifdef UNIFIED_WIFI_SD
  if(networkMode&&(part.indexOf(':')>=0||part.startsWith(" ")||part.endsWith(".")||part.endsWith(" ")))return false;
#endif
  if(slash<0)break;
  start=slash+1;
 }
 return true;
}

void next(){
 if(operation==Operation::List){
  for(File entry=file.openNextFile();entry;entry=file.openNextFile()){
   String name=entry.name();
   int slash=name.lastIndexOf('/');
   if(slash>=0)name=name.substring(slash+1);
   if(name.startsWith(".cardputer-transfer-"))continue;
#ifdef UNIFIED_WIFI_SD
   if(networkMode&&destination=="/"&&(!entry.isDirectory()||!sharedFolder(name)))continue;
#endif
   String hex=encode(reinterpret_cast<const uint8_t *>(name.c_str()),name.length());
   respond(entry.isDirectory()?"DIR "+hex:"FILE "+String(entry.size())+" "+hex);
   return;
  }
  cancel();respond("END");return;
 }
 if(operation==Operation::Read){
  if(file.position()>=file.size()){cancel();respond("END");return;}
   uint8_t bytes[chunkSize];
   int count=file.read(bytes,sizeof(bytes));
  if(count<=0){cancel();respond("ERR SD read failed");return;}
  respond("DATA "+encode(bytes,count));return;
 }
 respond("ERR No read or list operation");
}

void commit(){
 file.flush();file.close();
 const bool exists=SD.exists(destination);
 if(exists&&replacing){
  File original=SD.open(destination,FILE_READ);
  if(!original||original.isDirectory()){cancel();respond("ERR Destination is not a file");return;}
  original.close();
  const String backup=temporary+".bak";
  if(SD.exists(backup)||!SD.rename(destination,backup)){
   cancel();respond("ERR Could not stage the original file");return;
  }
  if(!SD.rename(temporary,destination)){
   bool restored=SD.rename(backup,destination);
   cancel();
   respond(restored?"ERR Could not replace file. Original restored":
                    "ERR Could not restore original file. It remains on SD as "+backup);
   return;
  }
  temporary="";
  bool removed=SD.remove(backup);
  cancel();respond(removed?"OK":"OK Backup remains on SD as "+backup);return;
 }
 bool ok=!exists&&!replacing&&SD.rename(temporary,destination);
 if(ok)temporary="";
 cancel();respond(ok?"OK":"ERR Could not save destination");
}

template<class Read,class Write>
bool pollStream(Read receive,Write sendBytes,bool usb){
 if(operation==Operation::StreamRead){
  if(streamSent==streamSize){
   if(!streamRemaining){cancel();return true;}
   int count=file.read(streamBuffer.get(),std::min(streamCapacity,streamRemaining));
   if(count<=0){cancel();return true;}
   streamSize=count;streamSent=0;streamRemaining-=count;
  }
  int count=sendBytes(streamBuffer.get()+streamSent,streamSize-streamSent);
  if(count>0){streamSent+=count;lastCommand=millis();}
  return true;
 }
 if(operation!=Operation::StreamWrite)return false;
 if(streamRemaining){
  const size_t capacity=usb?usbSDBurstSize:streamCapacity;
  const size_t needed=std::min(capacity-streamSize,streamRemaining);
  int count=receive(streamBuffer.get()+streamSize,needed);
  if(count<=0)return true;
  streamSize+=count;
  streamRemaining-=count;lastCommand=millis();
  if(streamSize==capacity||!streamRemaining){
   if(!streamFailed&&file.write(streamBuffer.get(),streamSize)!=streamSize)streamFailed=true;
   streamSize=0;
   if(usb&&streamRemaining){
    if(streamFailed){cancel();respond("ERR SD write failed");}
    else respond("OK");
   }
  }
 }
 if(!streamRemaining){
  if(streamFailed){cancel();respond("ERR SD write failed");}
  else commit();
 }
 return true;
}

void handle(const char *line){
#ifdef UNIFIED_WIFI_SD
 if(networkMode&&!authenticated){
  authenticated=!strncmp(line,"AUTH ",5)&&accessCode==line+5;
  rejected=!authenticated;
  respond(authenticated?"OK":"ERR Incorrect sharing code");
  return;
 }
#endif
#ifdef UNIFIED_BUILDER_HELP_UPLOAD
 if(helpOnly&&!strcmp(line,"DONE")){cancel();helpFinished=true;respond("OK");return;}
#endif
 if(!strcmp(line,"HELLO")){cancel();respond("CARDFILES 1");return;}
 if(!strcmp(line,"CANCEL")){cancel();respond("OK");return;}
 if(!strcmp(line,"NEXT")){next();return;}
 if(operation==Operation::Write){
  if(!strcmp(line,"COMMIT")){
   commit();
  }else if(!strncmp(line,"DATA ",5)){
   uint8_t bytes[chunkSize];size_t size;
   if(decode(line+5,bytes,size,sizeof(bytes))&&size&&file.write(bytes,size)==size)respond("OK");
   else{cancel();respond("ERR SD write failed or invalid data");}
  }else respond("ERR Finish or cancel the upload");
  return;
 }
 if(operation!=Operation::Idle){respond("ERR Finish or cancel the current operation");return;}
 if(!strncmp(line,"TIME ",5)){
#ifdef UNIFIED_BUILDER_HELP_UPLOAD
  if(helpOnly){respond("ERR Help transfer only");return;}
#endif
#ifdef UNIFIED_WIFI_SD
  if(networkMode){respond("ERR Clock changes require USB");return;}
#endif
  char *end=nullptr;
  int64_t epoch=strtoll(line+5,&end,10);
  respond(end!=line+5&&!*end&&setClock&&setClock(epoch)?"OK":"ERR Invalid date or time");
  return;
 }
 const char *arg=strchr(line,' ');
 const bool renaming=!strncmp(line,"RENAME ",7);
 const bool replaceUpload=!strncmp(line,"REPLACE ",8);
 const bool streamUpload=replaceUpload||!strncmp(line,"PUT ",4);
 size_t uploadSize=0;
 if(streamUpload){
  char *end=nullptr;
  const char* start=arg+1;
  unsigned long long size=strtoull(start,&end,10);
  if(*start<'0'||*start>'9'||*end!=' '||size>UINT32_MAX){
   respond("ERR Invalid upload size");return;
  }
  uploadSize=size;arg=end;
 }
 const char *newPath=renaming&&arg?strchr(arg+1,' '):nullptr;
 String source;
 if(newPath)source=String(arg+1).substring(0,newPath-arg-1);
 String path;
 if(!arg||!readPath(newPath?source.c_str():arg+1,path)){respond("ERR Invalid command or path");return;}
#ifdef UNIFIED_BUILDER_HELP_UPLOAD
 if(helpOnly){
  if(!strcmp(line,"MKDIR 2f48656c70")){
   File folder=SD.open("/Help",FILE_READ);
   const bool ok=folder?folder.isDirectory():SD.mkdir("/Help");
   respond(ok?"OK":"ERR Could not open or create the SD Help folder");return;
  }
  if(!helpFile(path)||(!streamUpload&&strncmp(line,"READ ",5))||
     (streamUpload&&(!uploadSize||uploadSize>65536))){
   respond("ERR Expected a Help text file of at most 64 KiB");return;
  }
 }
#endif
 const bool streamDownload=!strncmp(line,"GET ",4);
 if(!strncmp(line,"LIST ",5)||!strncmp(line,"READ ",5)||streamDownload){
  bool listing=line[0]=='L';
  file=SD.open(path,FILE_READ);
  if(!file||file.isDirectory()!=listing){cancel();respond(listing?"ERR Folder unavailable":"ERR File unavailable");return;}
  operation=listing?Operation::List:streamDownload?Operation::StreamRead:Operation::Read;
  if(streamDownload){
   streamBuffer.reset(new(std::nothrow) uint8_t[streamCapacity]);
   if(!streamBuffer){cancel();respond("ERR Not enough memory for SD transfer");return;}
   streamRemaining=file.size();
  }
#ifdef UNIFIED_WIFI_SD
  if(networkMode&&listing)destination=path;
#endif
  respond(listing?"OK":"OK "+String(file.size()));return;
 }
 if(!canWrite){respond("ERR Read-only session");return;}
 if(renaming){
  String target;
  if(path=="/"||!newPath||!readPath(newPath+1,target)||target=="/"||SD.exists(target)){
   respond("ERR Invalid rename or destination exists");return;
  }
  if(path.substring(0,path.lastIndexOf('/'))!=target.substring(0,target.lastIndexOf('/'))){
   respond("ERR Rename must stay within the same folder");return;
  }
  respond(SD.rename(path,target)?"OK":"ERR Could not rename item");return;
 }
 if(!strncmp(line,"DELETE ",7)){
  if(path=="/"){respond("ERR Cannot delete SD root");return;}
  File target=SD.open(path,FILE_READ);
  if(!target){respond("ERR Item unavailable");return;}
  bool folder=target.isDirectory();
  target.close();
  respond((folder?SD.rmdir(path):SD.remove(path))?"OK":"ERR Could not delete item. Folders must be empty");
  return;
 }
#ifdef UNIFIED_WIFI_SD
 if(networkMode&&path=="/"){respond("ERR Open a shared folder before uploading");return;}
#endif
 if(!strncmp(line,"MKDIR ",6)){
  respond(!SD.exists(path)&&SD.mkdir(path)?"OK":"ERR Could not create folder");return;
 }
 if(!strncmp(line,"WRITE ",6)||streamUpload){
  if(path=="/"){respond("ERR Cannot upload to SD root as a file");return;}
  const bool exists=SD.exists(path);
  if(replaceUpload){
   if(!exists){respond("ERR Destination no longer exists");return;}
   File original=SD.open(path,FILE_READ);
   if(!original||original.isDirectory()){respond("ERR Destination is not a file");return;}
  }else if(exists){respond("ERR Destination exists");return;}
  int slash=path.lastIndexOf('/');
  String name=path.substring(0,slash+1)+".cardputer-transfer-"+String(millis(),HEX)+".tmp";
  if(SD.exists(name)){respond("ERR Temporary name is busy");return;}
  file=SD.open(name,FILE_WRITE);
  if(!file){respond("ERR Cannot open destination folder");return;}
  temporary=name;destination=path;
  if(streamUpload){
   streamBuffer.reset(new(std::nothrow) uint8_t[streamCapacity]);
   if(!streamBuffer){cancel();respond("ERR Not enough memory for SD transfer");return;}
  }
  replacing=replaceUpload;
  operation=streamUpload?Operation::StreamWrite:Operation::Write;
  if(streamUpload)streamRemaining=uploadSize;
  respond("OK");return;
 }
 respond("ERR Invalid command");
}
}

void startUsbSD(bool writable,bool (*setTime)(int64_t)){
 stopUsbSD();
 canWrite=writable;
 setClock=setTime;
 lastCommand=millis();
}

void stopUsbSD(){
 cancel();reply="";inputSize=sent=0;overflow=false;
 setClock=nullptr;
#ifdef UNIFIED_BUILDER_HELP_UPLOAD
 helpOnly=helpFinished=false;
#endif
}

void pollUsbSD(){
 if(operation!=Operation::Idle&&millis()-lastCommand>15000)cancel();
 if(sent<reply.length()){
  int room=Serial.availableForWrite();
  if(room>0)sent+=Serial.write(reinterpret_cast<const uint8_t *>(reply.c_str())+sent,
                              std::min<size_t>(room,reply.length()-sent));
  return;
 }
 if(pollStream(
  [](uint8_t *out,size_t count){return Serial.available()?Serial.read(out,count):0;},
  [](const uint8_t *data,size_t count){
   if(!Serial.availableForWrite())return size_t(0);
   Serial.setTxTimeoutMs(3);
   size_t written=Serial.write(data,count);
   Serial.setTxTimeoutMs(0);
   return written;
  },true))return;
 for(size_t read=0;read<maxLine && Serial.available();++read){
  int c=Serial.read();
  if(c=='\n'){
   input[inputSize]=0;
   lastCommand=millis();
   if(overflow)respond("ERR Command too long");else handle(input);
   inputSize=0;overflow=false;
   return;
  }
  if(c!='\r'){
   if(inputSize<maxLine)input[inputSize++]=char(c);
   else overflow=true;
  }
 }
}

#ifdef UNIFIED_BUILDER_HELP_UPLOAD
void startUsbHelp(bool cardReady){
 startUsbSD(true,nullptr);
 helpOnly=true;
 helpFinished=!cardReady;
 respond(cardReady?"CARDFILES HELP 1":"ERR No microSD card is mounted");
}

bool pollUsbHelp(){
 if(!helpOnly)return false;
 if((helpFinished&&sent==reply.length())||millis()-lastCommand>15000){
  stopUsbSD();return false;
 }
 pollUsbSD();
 return true;
}

void stopUsbHelp(){
 if(helpOnly)stopUsbSD();
}
#endif

#ifdef UNIFIED_WIFI_SD
bool startWifiSD(const char *code){
 startUsbSD(true,nullptr);
 if(!SD.exists("/Transfers"))SD.mkdir("/Transfers");
 accessCode=code;
 server.begin();
 server.setNoDelay(true);
 if(!server||!discovery.begin(sharePort)){
  stopWifiSD();
  return false;
 }
 networkMode=true;
 return true;
}
void stopWifiSD(){
 client.stop();
 server.end();
 discovery.stop();
 networkMode=authenticated=rejected=false;
 accessCode="";
 stopUsbSD();
}

void pollWifiSD(){
 if(int size=discovery.parsePacket()){
  char request[sizeof(discoverRequest)];
  if(size==sizeof(discoverRequest)-1&&discovery.read(request,size)==size&&
     !memcmp(request,discoverRequest,size)){
   if(discovery.beginPacket(discovery.remoteIP(),discovery.remotePort())){
    discovery.print("CARDFILES WIFI 1");
    discovery.endPacket();
   }
  }
  discovery.clear();
 }
 if(!client.connected()||millis()-lastCommand>15000||(rejected&&sent>=reply.length())){
  client.stop();
  cancel();reply="";inputSize=sent=0;overflow=authenticated=rejected=false;
  client=server.accept();
  if(!client)return;
  lastCommand=millis();
 }
 if(sent<reply.length()){
  int count=send(client.fd(),reply.c_str()+sent,reply.length()-sent,MSG_DONTWAIT);
  if(count>0)sent+=count;
  return;
 }
 if(pollStream(
  [](uint8_t *out,size_t count){return client.read(out,count);},
  [](const uint8_t *data,size_t count){return send(client.fd(),data,count,MSG_DONTWAIT);}
 ,false))return;
 for(size_t read=0;read<maxLine&&client.available();++read){
  int c=client.read();
  if(c=='\n'){
   input[inputSize]=0;
   lastCommand=millis();
   if(overflow)respond("ERR Command too long");else handle(input);
   inputSize=0;overflow=false;
   return;
  }
  if(c>=0&&c!='\r'){
   if(inputSize<maxLine)input[inputSize++]=char(c);
   else overflow=true;
  }
 }
}
#endif
