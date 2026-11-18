#pragma once
#include "../fakes/esp_ota_ops.h"
#include <vector>
#include <stdexcept>
#include <cstring>
using esp_ota_handle_t=int;
inline std::vector<uint8_t> fakeFlash;
inline unsigned otaBegins=0,otaWrites=0,otaAborts=0,otaEnds=0,otaBoots=0;
inline int failWrite=-1,powerCutWrite=-1;
inline bool failBegin=false,failEnd=false,failBoot=false,failPartitionRead=false,fakeRunningFactory=true;
inline const esp_partition_t* esp_ota_get_running_partition(){return fakeRunningFactory?&factory:&player;}
inline int esp_partition_read(const esp_partition_t*,size_t offset,void*p,size_t n){if(failPartitionRead||offset+n>fakeFlash.size())return -1;memcpy(p,fakeFlash.data()+offset,n);return 0;}
inline int esp_ota_begin(const esp_partition_t*,size_t size,esp_ota_handle_t*h){++otaBegins;if(failBegin)return -1;fakeFlash.clear();fakeFlash.reserve(size);*h=1;return 0;}
inline int esp_ota_write(int,const void*p,size_t n){++otaWrites;if(int(otaWrites)==powerCutWrite)throw std::runtime_error("power loss");if(int(otaWrites)==failWrite)return -1;auto*b=static_cast<const uint8_t*>(p);fakeFlash.insert(fakeFlash.end(),b,b+n);return 0;}
inline int esp_ota_end(int){++otaEnds;return failEnd?-1:0;}
inline void esp_ota_abort(int){++otaAborts;}
