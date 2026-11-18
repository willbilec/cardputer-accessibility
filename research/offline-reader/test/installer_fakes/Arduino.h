#pragma once
#include "../headless_fakes/Arduino.h"
#include <string>
class String {
 std::string value;
public:
 String(const char *s=""):value(s){} String(std::string s):value(s){} String(unsigned n):value(std::to_string(n)){} String(int n):value(std::to_string(n)){} String(unsigned long n):value(std::to_string(n)){}
 const char *c_str()const{return value.c_str();}
 String& operator+=(const char *s){value+=s;return *this;}
 friend String operator+(const String&a,const String&b){return String(a.value+b.value);}
};
