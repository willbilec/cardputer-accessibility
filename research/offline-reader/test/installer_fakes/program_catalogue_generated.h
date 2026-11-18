#pragma once
#include "program_catalogue.h"
inline uint8_t testDigest[32]{};
inline ProgramRecord programs[]={{"test","Test program","/accessible/test.bin",3201,testDigest}};
inline constexpr unsigned kProgramCount=1;
