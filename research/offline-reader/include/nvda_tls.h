#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace nvdaRemote {
class TlsClient {
 struct Connection;
 std::unique_ptr<Connection> connection;
 int error=0;
public:
 static constexpr int unreachable=-1, noMemory=-2, timedOut=-3, transportClosed=-4;
 TlsClient();
 ~TlsClient();
 bool connect(const char* host,uint16_t port,bool verify,char (&fingerprint)[65]);
 bool connected();
 int available();
 int fd() const;
 int lastError() const{return error;}
 int read(uint8_t* data,size_t size);
 size_t write(const uint8_t* data,size_t size,uint32_t timeoutMs=5000);
};
}
