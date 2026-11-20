#include "nvda_tls.h"
#include <Arduino.h>
#include <NetworkClient.h>
#include <BearSSLTrustAnchors.h>
#include <bearssl/bearssl.h>
#include <esp_random.h>
#include <lwip/sockets.h>
#include <new>

namespace nvdaRemote {
namespace {
// A TLS probe records the leaf in DER form. The Remote client compares its
// digest before calling write(), which is the only application-data path.
struct Leaf {
 const br_x509_class* vtable;
 br_x509_decoder_context decoder;
 br_sha256_context hash;
 bool seen=false;
};

Leaf& leaf(const br_x509_class** ctx){return *reinterpret_cast<Leaf*>(ctx);}
const br_x509_class leafVerifier={
 sizeof(Leaf),
 [](const br_x509_class** ctx,const char*){
  auto& state=leaf(ctx);
  state.seen=false;
  br_sha256_init(&state.hash);
  br_x509_decoder_init(&state.decoder,nullptr,nullptr);
 },
 [](const br_x509_class**,uint32_t){},
 [](const br_x509_class** ctx,const unsigned char* data,size_t size){
  auto& state=leaf(ctx);
  if(!state.seen){
   br_sha256_update(&state.hash,data,size);
   br_x509_decoder_push(&state.decoder,data,size);
  }
 },
 [](const br_x509_class** ctx){leaf(ctx).seen=true;},
 [](const br_x509_class** ctx)->unsigned{
  auto& state=leaf(ctx);
  return state.seen&&br_x509_decoder_get_pkey(&state.decoder)?0:BR_ERR_X509_INVALID_VALUE;
 },
 [](const br_x509_class* const* ctx,unsigned* usages)->const br_x509_pkey*{
  auto& state=*reinterpret_cast<const Leaf*>(ctx);
  if(usages)*usages=BR_KEYTYPE_SIGN|BR_KEYTYPE_KEYX;
  return state.seen?&state.decoder.pkey:nullptr;
 }
};
}

struct TlsClient::Connection {
 NetworkClient tcp;
 br_ssl_client_context ssl{};
 // Accept full-size records. This client sends short JSON messages and
 // BearSSL splits larger writes across records sized for its output buffer.
 uint8_t input[BR_SSL_BUFSIZE_INPUT];
 uint8_t output[1536];

 bool pump(bool receive){
  size_t size=0;
  if(const auto* bytes=br_ssl_engine_sendrec_buf(&ssl.eng,&size)){
   const int count=::send(tcp.fd(),bytes,size,MSG_DONTWAIT);
   if(count>0)br_ssl_engine_sendrec_ack(&ssl.eng,count);
   else if(!count||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=ENOBUFS))return false;
  }else if(receive){
   if(auto* bytes=br_ssl_engine_recvrec_buf(&ssl.eng,&size)){
    const int count=::recv(tcp.fd(),bytes,size,MSG_DONTWAIT);
    if(count>0)br_ssl_engine_recvrec_ack(&ssl.eng,count);
    else if(!count||(errno!=EAGAIN&&errno!=EWOULDBLOCK))return false;
   }
  }
  return !(br_ssl_engine_current_state(&ssl.eng)&BR_SSL_CLOSED);
 }
};

TlsClient::TlsClient()=default;
TlsClient::~TlsClient()=default;
bool TlsClient::connect(const char* host,uint16_t port,bool verify,char (&fingerprint)[65]){
 connection.reset();
 error=0;
 fingerprint[0]='\0';
 auto next=std::unique_ptr<Connection>(new(std::nothrow) Connection);
 if(!next){error=noMemory;return false;}
 struct Validator {
  br_x509_minimal_context roots{};
  Leaf cert{&leafVerifier,{},{}};
 };
 auto validator=std::unique_ptr<Validator>(new(std::nothrow) Validator);
 if(!validator){error=noMemory;return false;}
 if(!next->tcp.connect(host,port,5000)){error=unreachable;return false;}
 const int noDelay=1;
 setsockopt(next->tcp.fd(),IPPROTO_TCP,TCP_NODELAY,&noDelay,sizeof(noDelay));
 auto& ssl=next->ssl;
 br_ssl_client_init_full(&ssl,&validator->roots,TAs,TAs_NUM);
 br_ssl_engine_set_buffers_bidi(&ssl.eng,next->input,sizeof(next->input),next->output,sizeof(next->output));
 br_ssl_engine_add_flags(&ssl.eng,BR_OPT_NO_RENEGOTIATION);
 if(verify){
  const time_t now=time(nullptr);
  br_x509_minimal_set_time(&validator->roots,now/86400+719528,now%86400);
 }else br_ssl_engine_set_x509(&ssl.eng,&validator->cert.vtable);
 uint8_t entropy[32];
 esp_fill_random(entropy,sizeof(entropy));
 br_ssl_engine_inject_entropy(&ssl.eng,entropy,sizeof(entropy));
 if(!br_ssl_client_reset(&ssl,host,0)){
  error=br_ssl_engine_last_error(&ssl.eng);
  if(!error)error=transportClosed;
  return false;
 }
 const uint32_t start=millis();
 bool transportOpen=true;
 while(!(br_ssl_engine_current_state(&ssl.eng)&BR_SSL_SENDAPP)&&millis()-start<8000){
  if(!(transportOpen=next->pump(true)))break;
  vTaskDelay(1);
 }
 if(!(br_ssl_engine_current_state(&ssl.eng)&BR_SSL_SENDAPP)){
  error=br_ssl_engine_last_error(&ssl.eng);
  if(!error)error=transportOpen?timedOut:transportClosed;
  return false;
 }
 if(!verify){
  uint8_t digest[32];
  br_sha256_out(&validator->cert.hash,digest);
  constexpr char hex[]="0123456789abcdef";
  for(unsigned i=0;i<sizeof(digest);++i){
   fingerprint[2*i]=hex[digest[i]>>4];
   fingerprint[2*i+1]=hex[digest[i]&15];
  }
  fingerprint[64]='\0';
 }
 // Renegotiation is disabled. Only the initial handshake needs validation state.
 br_ssl_engine_set_x509(&ssl.eng,nullptr);
 connection=std::move(next);
 return true;
}
bool TlsClient::connected(){return connection&&connection->tcp.connected()&&
                                   !(br_ssl_engine_current_state(&connection->ssl.eng)&BR_SSL_CLOSED);}
int TlsClient::fd() const{return connection?connection->tcp.fd():-1;}
int TlsClient::available(){
 if(!connection)return 0;
 if(!connection->pump(true)){connection.reset();return 0;}
 size_t size=0;
 br_ssl_engine_recvapp_buf(&connection->ssl.eng,&size);
 return size;
}
int TlsClient::read(uint8_t* data,size_t size){
 if(!connection)return 0;
 auto& eng=connection->ssl.eng;
 size_t available=0;
 auto* bytes=br_ssl_engine_recvapp_buf(&eng,&available);
 const size_t count=std::min(size,available);
 if(!count)return 0;
 memcpy(data,bytes,count);
 br_ssl_engine_recvapp_ack(&eng,count);
 return count;
}
size_t TlsClient::write(const uint8_t* data,size_t size,uint32_t timeoutMs){
 if(!connection)return 0;
 auto& eng=connection->ssl.eng;
 const uint32_t start=millis();
 size_t written=0;
 while(millis()-start<timeoutMs){
  size_t space=0,pending=0;
  br_ssl_engine_sendrec_buf(&eng,&pending);
  if(written==size&&!pending)return written;
  if(auto* out=br_ssl_engine_sendapp_buf(&eng,&space);out&&written<size){
   const size_t count=std::min(size-written,space);
   memcpy(out,data+written,count);
   br_ssl_engine_sendapp_ack(&eng,count);
   br_ssl_engine_flush(&eng,0);
   written+=count;
  }
  if(!connection->pump(false))return 0;
  br_ssl_engine_sendrec_buf(&eng,&pending);
  if(written==size&&!pending)return written;
  vTaskDelay(1);
 }
 return 0;
}
}
