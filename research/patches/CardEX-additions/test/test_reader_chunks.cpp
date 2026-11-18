#include "../src/ReaderChunks.h"
#include <cassert>
#include <iostream>
#include <string>
int main(){
 std::string text="File manager help. Semicolon up, period down. Any other key closes help.";
 std::string rebuilt;size_t at=0;unsigned count=0;
 while(at<text.size()){
  auto part=ReaderChunks::take(text,at,24);assert(part.next>at);assert(part.end-part.start<=24);
  if(!rebuilt.empty())rebuilt+=' ';
  rebuilt+=text.substr(part.start,part.end-part.start);at=part.next;++count;
  if(part.end<text.size())assert(ReaderChunks::space(text[part.end]));
 }
 assert(count>1&&rebuilt==text);
 assert(rebuilt.substr(rebuilt.size()-24)=="y other key closes help.");
 auto blank=ReaderChunks::take(" \n\t",0,280);assert(blank.start==3&&blank.end==3&&blank.next==3);
 auto end=ReaderChunks::take("done",99,280);assert(end.next==4);
 std::string identifier(700,'x');at=0;size_t copied=0;
 while(at<identifier.size()){auto part=ReaderChunks::take(identifier,at,280);assert(part.next>at);copied+=part.end-part.start;at=part.next;}
 assert(copied==identifier.size());
 std::cout<<"Actual speech chunker: complete help, word boundaries, blanks, bounds and long identifiers passed\n";
}
