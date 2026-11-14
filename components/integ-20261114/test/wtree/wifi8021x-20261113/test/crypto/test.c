/* 0450 host test: wifi_8021x.h primitives against OpenSSL and published vectors. */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations" /* CMAC_* is the simplest independent reference */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/cmac.h>
#include "wifi_8021x.h"
static void ossl_cmac(const uint8_t*k,const uint8_t*p,size_t n,uint8_t out[16]){size_t z=0;CMAC_CTX*c=CMAC_CTX_new();assert(c&&CMAC_Init(c,k,16,EVP_aes_128_cbc(),0)&&CMAC_Update(c,p,n)&&CMAC_Final(c,out,&z)&&z==16);CMAC_CTX_free(c);}
int main(void){
 /* RFC 4493 examples */
 static const uint8_t key[16]={0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
 static const uint8_t empty[16]={0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46};
 static const uint8_t m16[16]={0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a};
 static const uint8_t t16[16]={0x07,0x0a,0x16,0xb4,0x6b,0x4d,0x41,0x44,0xf7,0x9b,0xdd,0x9d,0xd0,0x4a,0x28,0x7c};
 uint8_t out[32],ref[32];const uint8_t*one[1]={m16};size_t z[1]={0};
 assert(!n8x_cmac(key,one,z,1,out)&&!memcmp(out,empty,16));z[0]=16;assert(!n8x_cmac(key,one,z,1,out)&&!memcmp(out,t16,16));
 /* FIPS 197 C.1 through the key schedule and one block */
 {uint8_t k[16],rk[176],b[16];static const uint8_t c[16]={0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
  for(unsigned i=0;i<16;i++){k[i]=(uint8_t)i;b[i]=(uint8_t)(i*17);}n8x_aes128_keys(k,rk);n8x_aes128_encrypt(rk,b);assert(!memcmp(b,c,16));}
 srand(20261113);unsigned cases=0;
 for(unsigned round=0;round<4000;round++){
  uint8_t k[64],m[700];size_t kn=1+(size_t)rand()%64,n=(size_t)rand()%700,a=n?(size_t)rand()%(n+1):0,b=a+(n-a?(size_t)rand()%(n-a+1):0);
  for(size_t i=0;i<64;i++)k[i]=(uint8_t)rand();for(size_t i=0;i<n;i++)m[i]=(uint8_t)rand();
  const uint8_t*parts[3]={m,m+a,m+b};size_t sizes[3]={a,b-a,n-b};
  assert(!n8x_cmac(k,parts,sizes,3,out));ossl_cmac(k,m,n,ref);assert(!memcmp(out,ref,16));
  unsigned len=0;assert(!n8x_hmac_sha256(k,kn,parts,sizes,3,out)&&HMAC(EVP_sha256(),k,(int)kn,m,n,ref,&len)&&len==32&&!memcmp(out,ref,32));
  struct n8x_sha256 s;n8x_sha256_init(&s);n8x_sha256_update(&s,m,a);n8x_sha256_update(&s,m+a,n-a);n8x_sha256_out(&s,out);assert(EVP_Digest(m,n,ref,&len,EVP_sha256(),0)&&!memcmp(out,ref,32));
  /* KDF-SHA256-384 written out independently */
  uint8_t ctx[76],ptk[48],want[64],in[102];for(unsigned i=0;i<76;i++)ctx[i]=(uint8_t)rand();
  for(unsigned i=1;i<=2;i++){in[0]=(uint8_t)i;in[1]=0;memcpy(in+2,"Pairwise key expansion",22);memcpy(in+24,ctx,76);in[100]=0x80;in[101]=0x01;assert(HMAC(EVP_sha256(),k,32,in,102,want+32*(i-1),&len));}
  assert(!n8x_ptk_sha256(k,ctx,ptk)&&!memcmp(ptk,want,48));cases++;
 }
 uint8_t r[22];n8x_station_rsn(5,r);assert(n8x_rsn_akm(r)==5&&r[1]==20&&r[19]==5);n8x_station_rsn(1,r);assert(n8x_rsn_akm(r)==1&&n8x_eap_akm(1)&&n8x_eap_akm(5)&&!n8x_eap_akm(2));n8x_station_rsn(2,r);assert(n8x_rsn_akm(r)==2);r[19]=8;assert(!n8x_rsn_akm(r));r[19]=1;r[20]=0x80;assert(!n8x_rsn_akm(r));
 assert(n8x_cmac(0,one,z,1,out)<0&&n8x_hmac_sha256(key,65,one,z,1,out)<0);
 printf("PASS 0450 crypto: RFC 4493 / FIPS 197 vectors, %u random cases of AES-CMAC, HMAC-SHA256, SHA-256 and KDF-SHA256-384 equal to OpenSSL\n",cases);return 0;}
