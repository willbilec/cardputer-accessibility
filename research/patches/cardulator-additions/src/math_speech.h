#pragma once
#include <string>
#include <string_view>
#include <cctype>
namespace MathSpeech {
inline std::string describe(std::string_view input){
 std::string out;out.reserve(input.size()*2);
 for(size_t i=0;i<input.size();++i){
  char c=input[i];
  // Read scientific notation as an exponent only inside a numeric token,
  // never inside identifiers such as sensor2e3.
  bool numberStart=std::isdigit(static_cast<unsigned char>(c))||(c=='.'&&i+1<input.size()&&std::isdigit(static_cast<unsigned char>(input[i+1])));
  bool boundary=i==0||(!std::isalnum(static_cast<unsigned char>(input[i-1]))&&input[i-1]!='_'&&input[i-1]!='.');
  if(numberStart&&boundary){
   size_t end=i;while(end<input.size()&&(std::isdigit(static_cast<unsigned char>(input[end]))||input[end]=='.'))++end;
   out.append(input.substr(i,end-i));
   if(end<input.size()&&(input[end]=='e'||input[end]=='E')){
    size_t exponent=end+1;if(exponent<input.size()&&(input[exponent]=='+'||input[exponent]=='-'))++exponent;
    if(exponent<input.size()&&std::isdigit(static_cast<unsigned char>(input[exponent]))){
     out+=" times ten to the power ";if(input[end+1]=='-')out+="minus ";
     size_t tail=exponent;while(tail<input.size()&&std::isdigit(static_cast<unsigned char>(input[tail])))++tail;
     out.append(input.substr(exponent,tail-exponent));end=tail;
    }
   }
   i=end-1;continue;
  }
  if(i+1<input.size()){
   auto pair=input.substr(i,2);const char*word=nullptr;
   if(pair=="<=")word=" less than or equal to ";else if(pair==">=")word=" greater than or equal to ";
   else if(pair=="!=")word=" not equal to ";else if(pair=="==")word=" equals ";
   else if(pair=="&&")word=" and ";else if(pair=="||")word=" or ";
   if(word){out+=word;++i;continue;}
  }
  switch(c){
   case '+':out+=" plus ";break;case '-':out+=" minus ";break;
   case '*':out+=" times ";break;case '/':out+=" divided by ";break;
   case '^':out+=" to the power ";break;case '=':out+=" equals ";break;
   case '(':out+=" open parenthesis ";break;case ')':out+=" close parenthesis ";break;
   case '[':out+=" open bracket ";break;case ']':out+=" close bracket ";break;
   case '{':out+=" open brace ";break;case '}':out+=" close brace ";break;
   case '<':out+=" less than ";break;case '>':out+=" greater than ";break;
   case '%':out+=" modulo ";break;case ',':out+=" comma ";break;
   case '_':out+=" underscore ";break;case ';':out+=" semicolon ";break;
   case '"':out+=" quote ";break;case '\n':out+=". New line. ";break;
   default:out+=c;
  }
 }
 return out;
}
}
