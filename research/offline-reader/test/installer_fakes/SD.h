#pragma once
#include <memory>
#include <vector>
#include <cstring>
constexpr int FILE_READ=0;
inline std::vector<uint8_t> fakeSd;
inline bool fakeSdAvailable=true,fakeFileAvailable=true,fakeMutateOnRewind=false,fakeTruncateOnRewind=false;
inline size_t fakeReadLimit=177,fakeReadFailAt=size_t(-1);
inline int fakeSeekFail=-1,fakeSeeks=0;
class File {
 std::shared_ptr<size_t> cursor=std::make_shared<size_t>(0);
public:
 explicit operator bool()const{return fakeFileAvailable;}
 size_t size()const{return fakeSd.size();}
 bool seek(size_t pos){if(++fakeSeeks==fakeSeekFail)return false;*cursor=pos;if(fakeMutateOnRewind&&fakeSeeks==2&&!fakeSd.empty())fakeSd[0]^=1;if(fakeTruncateOnRewind&&fakeSeeks==2)fakeSd.resize(100);return true;}
 int read(uint8_t*p,size_t n){if(*cursor>=fakeReadFailAt)return -1;n=std::min({n,fakeReadLimit,fakeSd.size()-*cursor});memcpy(p,fakeSd.data()+*cursor,n);*cursor+=n;return int(n);}
 void close(){}
};
struct {template<class...A> bool begin(A...){return fakeSdAvailable;}File open(const char*,int){return {};}} inline SD;
