#ifndef NWP_CRYPTO_PROVIDER_H
#define NWP_CRYPTO_PROVIDER_H
#include <stddef.h>
#include <stdint.h>
/* Original project glue over existing MIT-licensed BearSSL primitives.
 * Separate nonaliasing bounded input/output, exclusive caller ownership. */
int nwpc_hmac(const uint8_t*,size_t,const uint8_t*const*,const size_t*,size_t,uint8_t out[20]);
int nwpc_pmk(const uint8_t*,size_t,const uint8_t*,size_t,uint8_t out[32]);
int nwpc_ptk(const uint8_t pmk[32],const uint8_t sorted_context[76],uint8_t out[48]);
int nwpc_unwrap(const uint8_t kek[16],const uint8_t*,size_t,uint8_t*,size_t,size_t*);
/* 0432 (WPA3-SAE, AKM 00-0F-AC:6/8): SHA-256, HMAC-SHA256, the IEEE 802.11
 * KDF-SHA256-Length, AES-128-CMAC and the SHA-256 pairwise key expansion.
 * Same ownership rules as above; outputs are wiped on refusal. */
struct nwpc_sha256 {uint32_t h[8];uint64_t bytes;uint8_t block[64];};
void nwpc_sha256_init(struct nwpc_sha256*);
void nwpc_sha256_update(struct nwpc_sha256*,const void*,size_t);
void nwpc_sha256_final(struct nwpc_sha256*,uint8_t out[32]);
int nwpc_hmac256(const uint8_t*,size_t,const uint8_t*const*,const size_t*,size_t,uint8_t out[32]);
/* out = L(HMAC-SHA256(key, i_le16 | label | context | bits_le16), i=1.., 0, 8*bytes); bytes 1..64. */
int nwpc_kdf256(const uint8_t*,size_t,const char*label,const uint8_t*context,size_t,uint8_t*out,size_t bytes);
int nwpc_cmac(const uint8_t key[16],const uint8_t*const*,const size_t*,size_t,uint8_t out[16]);
int nwpc_ptk256(const uint8_t pmk[32],const uint8_t sorted_context[76],uint8_t out[48]);
#endif
