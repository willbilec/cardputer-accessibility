#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

namespace settingsCodec {
using Values=std::map<std::string,std::string>;
inline uint32_t checksum(const std::string &s){uint32_t h=2166136261u;for(unsigned char c:s){h^=c;h*=16777619u;}return h;}
inline std::string encode(const Values &values){
 static const char hex[]="0123456789abcdef";std::string out="CARDA11Y1\n";
 for(const auto &entry:values){out+=entry.first;out+='=';for(unsigned char c:entry.second){out+=hex[c>>4];out+=hex[c&15];}out+='\n';}
 char tail[16];std::snprintf(tail,sizeof(tail),"#%08lx\n",(unsigned long)checksum(out));return out+tail;
}
inline int digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;}
inline bool decode(const std::string &data,Values &out){
 if(data.size()<20||data.compare(0,10,"CARDA11Y1\n")!=0)return false;
 size_t footer=data.rfind('#');if(footer==std::string::npos||footer+10!=data.size()||data.back()!='\n')return false;
 uint32_t expected=0;for(size_t i=footer+1;i<footer+9;++i){int v=digit(data[i]);if(v<0)return false;expected=(expected<<4)|unsigned(v);}
 if(checksum(data.substr(0,footer))!=expected)return false;
 Values parsed;size_t start=10;
 while(start<footer){size_t end=data.find('\n',start);if(end==std::string::npos||end>=footer)return false;
  size_t eq=data.find('=',start);if(eq==std::string::npos||eq>=end||eq==start||(end-eq-1)%2)return false;
  std::string key=data.substr(start,eq-start),value;
  for(char c:key)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;
  for(size_t i=eq+1;i<end;i+=2){int hi=digit(data[i]),lo=digit(data[i+1]);if(hi<0||lo<0)return false;value+=char((hi<<4)|lo);}
  if(!parsed.emplace(key,value).second)return false;
  start=end+1;
 }
 out.swap(parsed);return true;
}
}
