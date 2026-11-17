#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "crypto_provider.h"
static const uint8_t pmk[32]={0xf4,0x2c,0x6f,0xc5,0x2d,0xf0,0xeb,0xef,0x9e,0xbb,0x4b,0x90,0xb3,0x8a,0x5f,0x90,0x2e,0x83,0xfe,0x1b,0x13,0x5a,0x70,0xe2,0x3a,0xed,0x76,0x2e,0x97,0x10,0xa1,0x2e};
static const uint8_t wrap[24]={0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47,0xae,0xf3,0x4b,0xd8,0xfb,0x5a,0x7b,0x82,0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5};
int main(void){uint8_t out[640],saved[640],key[16],w[24],expected[16];size_t used;unsigned checks=0;
 assert(!nwpc_pmk((const uint8_t*)"password",8,(const uint8_t*)"IEEE",4,out)&&!memcmp(out,pmk,32));checks++;
 for(unsigned i=0;i<16;i++){key[i]=i;expected[i]=(uint8_t)(i*17);}
 assert(!nwpc_unwrap(key,wrap,24,out,sizeof out,&used)&&used==16&&!memcmp(out,expected,16));checks++;
 for(unsigned i=0;i<24;i++)for(unsigned bit=0;bit<8;bit++){memcpy(w,wrap,24);w[i]^=1u<<bit;memset(out,0xa5,sizeof out);memcpy(saved,out,sizeof out);assert(nwpc_unwrap(key,w,24,out,sizeof out,&used)<0&&!used&&!memcmp(out,saved,sizeof out));checks++;}
 for(unsigned n=0;n<24;n++){assert(nwpc_unwrap(key,wrap,n,out,sizeof out,&used)<0&&!used);checks++;}
 for(unsigned cap=0;cap<16;cap++){assert(nwpc_unwrap(key,wrap,24,out,cap,&used)<0&&!used);checks++;}
 uint8_t hkey[20];memset(hkey,0x0b,20);const uint8_t*p[1]={(const uint8_t*)"Hi There"};size_t ns[1]={8};const uint8_t hmac[20]={0xb6,0x17,0x31,0x86,0x55,0x05,0x72,0x64,0xe2,0x8b,0xc0,0xb6,0xfb,0x37,0x8c,0x8e,0xf1,0x46,0xbe,0x00};assert(!nwpc_hmac(hkey,20,p,ns,1,out)&&!memcmp(out,hmac,20));checks++;
 printf("PASS %u existing-BearSSL provider checks (public vectors only)\n",checks);return 0;}
