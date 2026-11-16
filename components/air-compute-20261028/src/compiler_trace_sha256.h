/* Small allocation-free SHA-256 for bounded compiler-trace artifacts only. */
#ifndef TEGU_COMPILER_TRACE_SHA256_H
#define TEGU_COMPILER_TRACE_SHA256_H
#include <stddef.h>
#include <stdint.h>
#define TEGU_COMPILER_TRACE_HASH_MAX 32768u
static const uint32_t tegu_trace_sha_k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t tegu_trace_rotr(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void tegu_trace_sha_block(uint32_t h[8],const unsigned char *p){
 uint32_t w[64],a,b,c,d,e,f,g,z;
 for(unsigned i=0;i<16;i++)w[i]=(uint32_t)p[i*4]<<24|(uint32_t)p[i*4+1]<<16|(uint32_t)p[i*4+2]<<8|p[i*4+3];
 for(unsigned i=16;i<64;i++){uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(tegu_trace_rotr(x,7)^tegu_trace_rotr(x,18)^(x>>3))+w[i-7]+(tegu_trace_rotr(y,17)^tegu_trace_rotr(y,19)^(y>>10));}
 a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];f=h[5];g=h[6];z=h[7];
 for(unsigned i=0;i<64;i++){uint32_t t1=z+(tegu_trace_rotr(e,6)^tegu_trace_rotr(e,11)^tegu_trace_rotr(e,25))+((e&f)^(~e&g))+tegu_trace_sha_k[i]+w[i],t2=(tegu_trace_rotr(a,2)^tegu_trace_rotr(a,13)^tegu_trace_rotr(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
 h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
}
static __attribute__((unused)) int tegu_compiler_trace_sha256(const void *data,size_t n,char out[65]){
 if(!data||!out||!n||n>TEGU_COMPILER_TRACE_HASH_MAX)return -1;
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 const unsigned char *p=(const unsigned char *)data;size_t full=n/64;
 for(size_t i=0;i<full;i++)tegu_trace_sha_block(h,p+i*64);
 unsigned char tail[128]={0};size_t rem=n%64;for(size_t i=0;i<rem;i++)tail[i]=p[full*64+i];tail[rem]=0x80;
 size_t blocks=rem<56?1:2;uint64_t bits=(uint64_t)n*8;
 for(unsigned i=0;i<8;i++)tail[blocks*64-1-i]=(unsigned char)(bits>>(8*i));
 for(size_t i=0;i<blocks;i++)tegu_trace_sha_block(h,tail+i*64);
 static const char hex[]="0123456789abcdef";
 for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++){unsigned char q=(unsigned char)(h[i]>>(24-8*j));unsigned k=(i*4+j)*2;out[k]=hex[q>>4];out[k+1]=hex[q&15];}
 out[64]=0;return 0;
}
/* Pipeline identity hashes include metallibs larger than the diagnostic trace
 * cap.  Keep this separate so trace callers retain their 32 KiB bound. */
static __attribute__((unused)) int tegu_compiler_identity_sha256(const void *data,size_t n,char out[65]){
 if(!data||!out||!n)return -1;
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 const unsigned char *p=(const unsigned char *)data;size_t full=n/64;
 for(size_t i=0;i<full;i++)tegu_trace_sha_block(h,p+i*64);
 unsigned char tail[128]={0};size_t rem=n%64;for(size_t i=0;i<rem;i++)tail[i]=p[full*64+i];tail[rem]=0x80;
 size_t blocks=rem<56?1:2;uint64_t bits=(uint64_t)n*8;
 for(unsigned i=0;i<8;i++)tail[blocks*64-1-i]=(unsigned char)(bits>>(8*i));
 for(size_t i=0;i<blocks;i++)tegu_trace_sha_block(h,tail+i*64);
 static const char hex[]="0123456789abcdef";
 for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++){unsigned char q=(unsigned char)(h[i]>>(24-8*j));unsigned k=(i*4+j)*2;out[k]=hex[q>>4];out[k+1]=hex[q&15];}
 out[64]=0;return 0;
}
#endif
