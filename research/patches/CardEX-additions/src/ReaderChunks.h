#pragma once
#include <algorithm>
#include <cstddef>
#include <string_view>
namespace ReaderChunks {
struct Chunk {size_t start,end,next;};
inline bool space(char c){return c==' '||c=='\n'||c=='\r'||c=='\t';}
inline Chunk take(std::string_view text,size_t start,size_t limit){
 start=std::min(start,text.size());while(start<text.size()&&space(text[start]))++start;
 size_t end=std::min(text.size(),start+std::max(size_t(1),limit));
 if(end<text.size()&&!space(text[end])){
  size_t boundary=end;while(boundary>start&&!space(text[boundary-1]))--boundary;
  if(boundary>start)end=boundary;
 }
 while(end>start&&space(text[end-1]))--end;
 size_t next=end;while(next<text.size()&&space(text[next]))++next;
 return {start,end,next};
}
}
