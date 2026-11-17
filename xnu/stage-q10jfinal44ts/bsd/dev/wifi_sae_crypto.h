#ifndef WIFI_SAE_CRYPTO_H
#define WIFI_SAE_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
/* 0432 (WPA3-SAE, AKM 00-0F-AC:6/8): HMAC-SHA256, the IEEE 802.11
 * KDF-SHA256-Length, AES-128-CMAC and the SHA-256 pairwise key expansion.
 * A new object beside the CI member wifi_wpa_crypto.o, which stays as built.
 * SHA-256 is the kernel's libkern implementation; AES is the BearSSL
 * constant-time code already in the kernel archive. Separate nonaliasing
 * bounded input/output; outputs are wiped on refusal. */
int nsc_hmac256(const uint8_t*key,size_t,const uint8_t*const*parts,const size_t*sizes,size_t count,uint8_t out[32]);
/* out = L(HMAC-SHA256(key, i_le16 | label | context | bits_le16), i=1.., 0, 8*bytes); bytes 1..64. */
int nsc_kdf256(const uint8_t*key,size_t,const char*label,const uint8_t*context,size_t,uint8_t*out,size_t bytes);
int nsc_cmac(const uint8_t key[16],const uint8_t*const*parts,const size_t*sizes,size_t count,uint8_t out[16]);
int nsc_ptk256(const uint8_t pmk[32],const uint8_t sorted_context[76],uint8_t out[48]);
#endif
