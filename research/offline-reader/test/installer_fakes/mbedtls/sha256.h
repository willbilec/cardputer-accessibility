#pragma once
// Deliberately a digest test double: verifies control flow, not SHA-256 itself.
// Target builds and package checks exercise the real SHA implementation.
#include <cstdint>
#include <cstddef>
#include <cstring>
struct mbedtls_sha256_context{uint8_t digest[32];size_t offset;};
inline void mbedtls_sha256_init(mbedtls_sha256_context*c){memset(c,0,sizeof(*c));}
inline int mbedtls_sha256_starts(mbedtls_sha256_context*,int){return 0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context*c,const uint8_t*p,size_t n){while(n--){c->digest[c->offset++%32]+=*p++;}return 0;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context*c,uint8_t*out){memcpy(out,c->digest,32);return 0;}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
