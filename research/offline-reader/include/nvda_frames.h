#pragma once
#include <cstddef>

namespace nvdaRemote {
// A stream read can contain a partial message, multiple messages, or both.
class Frames {
 char* const buffer;
 const size_t capacity;
 size_t used=0;
 bool discard=false;
 unsigned rejected=0;
public:
 Frames(char* buffer,size_t capacity):buffer(buffer),capacity(capacity){}
 const char* push(char byte) {
  if(byte=='\n'){
   const bool complete=!discard&&used!=0;
   if(complete)buffer[used]='\0';
   used=0;discard=false;
   return complete?buffer:nullptr;
  }
  if(!discard){
   if(byte&&used+1<capacity)buffer[used++]=byte;
   else{discard=true;++rejected;}
  }
  return nullptr;
 }
 unsigned dropped() const{return rejected;}
};
}
