#include <arm64/sweet_early.h>
#if defined(NORD2)
/* New bounded RFC2104/8018/3394 and WPA PRF glue. No Redwood arithmetic copied.
 * BearSSL dependencies retain their own Thomas Pornin MIT notices. */
#include "wifi_wpa_crypto.h"
#if defined(KERNEL) || defined(_KERNEL)
/* Canonical placement is xnu/bsd/dev; reuse the already staged MIT library. */
#include "../../libkern/crypto/sweet_bearssl/inc/bearssl_hash.h"
#include "../../libkern/crypto/sweet_bearssl/inc/bearssl_block.h"
#else
#include "bearssl_hash.h"
#include "bearssl_block.h"
#endif
#if defined(KERNEL) || defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif
static void clear(void*v,size_t n){volatile uint8_t*p=v;while(n--)*p++=0;}
int nwpc_hmac(const uint8_t*k,size_t kn,const uint8_t*const*p,const size_t*n,size_t count,uint8_t out[20]){
 if(!out)return -1;
 if(!k||kn>64||!p||!n||count>4){clear(out,20);return -1;}
 for(size_t i=0;i<count;i++)if(n[i]>640||(!p[i]&&n[i])){clear(out,20);return -1;}
 uint8_t pad[64],digest[20];br_sha1_context c;
 for(unsigned i=0;i<64;i++)pad[i]=(i<kn?k[i]:0)^0x36;
 br_sha1_init(&c);br_sha1_update(&c,pad,64);
 for(size_t i=0;i<count;i++)if(n[i])br_sha1_update(&c,p[i],n[i]);
 br_sha1_out(&c,digest);
 for(unsigned i=0;i<64;i++)pad[i]^=0x36^0x5c;
 br_sha1_init(&c);br_sha1_update(&c,pad,64);br_sha1_update(&c,digest,20);br_sha1_out(&c,out);
 clear(&c,sizeof c);clear(pad,64);clear(digest,20);return 0;
}
int nwpc_pmk(const uint8_t*p,size_t pn,const uint8_t*s,size_t sn,uint8_t out[32]){
 if(!out)return -1;
 if(!p||pn<8||pn>63||!s||!sn||sn>32){clear(out,32);return -1;}
 uint8_t u[20],x[20];
 for(unsigned block=1;block<=2;block++){
  uint8_t counter[4]={0,0,0,(uint8_t)block};const uint8_t*parts[2]={s,counter};size_t ns[2]={sn,4};
  (void)nwpc_hmac(p,pn,parts,ns,2,u);memcpy(x,u,20);
  for(unsigned round=1;round<4096;round++){const uint8_t*q[1]={u};size_t size[1]={20};(void)nwpc_hmac(p,pn,q,size,1,u);for(unsigned i=0;i<20;i++)x[i]^=u[i];}
  memcpy(out+(block-1)*20,x,block==1?20:12);
 }
 clear(u,20);clear(x,20);return 0;
}
int nwpc_ptk(const uint8_t key[32],const uint8_t context[76],uint8_t out[48]){
 if(!out)return -1;
 if(!key||!context){clear(out,48);return -1;}
 static const uint8_t label[]="Pairwise key expansion";uint8_t d[20],counter;
 for(counter=0;counter<3;counter++){
  const uint8_t*p[3]={label,context,&counter};size_t ns[3]={sizeof label,76,1};
  (void)nwpc_hmac(key,32,p,ns,3,d);memcpy(out+20*counter,d,counter==2?8:20);
 }
 clear(d,20);return 0;
}
int nwpc_unwrap(const uint8_t key[16],const uint8_t*in,size_t n,uint8_t*out,size_t cap,size_t*used){
 if(used)*used=0;
 if(!key||!in||!out||!used||n<24||n>640||(n&7)||cap<n-8)return -1;
 uint8_t plain[632],a[8],block[16],iv[16];br_aes_ct_cbcdec_keys ctx;
 br_aes_ct_cbcdec_init(&ctx,key,16);memcpy(a,in,8);memcpy(plain,in+8,n-8);size_t blocks=n/8-1;
 for(int j=5;j>=0;j--)for(size_t i=blocks;i;i--){
  memcpy(block,a,8);uint64_t t=blocks*(unsigned)j+i;for(unsigned k=0;k<8;k++)block[7-k]^=(uint8_t)(t>>(k*8));
  memcpy(block+8,plain+8*(i-1),8);memset(iv,0,16);br_aes_ct_cbcdec_run(&ctx,iv,block,16);
  memcpy(a,block,8);memcpy(plain+8*(i-1),block+8,8);
 }
 unsigned bad=0;for(unsigned i=0;i<8;i++)bad|=a[i]^0xa6;
 if(!bad){memcpy(out,plain,n-8);*used=n-8;}
 clear(&ctx,sizeof ctx);clear(plain,sizeof plain);clear(a,8);clear(block,16);clear(iv,16);return bad?-1:0;
}
/* ---- 0432: SHA-256 family (FIPS 180-4, RFC 2104, RFC 4493, IEEE 802.11-2020 12.7.1.6.2) ---- */
static uint32_t ror(uint32_t x,unsigned n){return x>>n|x<<(32-n);}
static void sha256_block(uint32_t h[8],const uint8_t*p){
 static const uint32_t k[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64],s[8];
 for(unsigned i=0;i<16;i++)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
 for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10));
 for(unsigned i=0;i<8;i++)s[i]=h[i];
 for(unsigned i=0;i<64;i++){
  uint32_t a=s[0],e=s[4];
  uint32_t t1=s[7]+(ror(e,6)^ror(e,11)^ror(e,25))+((e&s[5])^(~e&s[6]))+k[i]+w[i];
  uint32_t t2=(ror(a,2)^ror(a,13)^ror(a,22))+((a&s[1])^(a&s[2])^(s[1]&s[2]));
  for(unsigned j=7;j;j--)s[j]=s[j-1];
  s[4]+=t1;s[0]=t1+t2;
 }
 for(unsigned i=0;i<8;i++)h[i]+=s[i];
 clear(w,sizeof w);clear(s,sizeof s);
}
void nwpc_sha256_init(struct nwpc_sha256*c){
 static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 for(unsigned i=0;i<8;i++)c->h[i]=iv[i];c->bytes=0;
}
void nwpc_sha256_update(struct nwpc_sha256*c,const void*v,size_t n){
 const uint8_t*p=v;
 while(n--){c->block[c->bytes++&63]=*p++;if(!(c->bytes&63))sha256_block(c->h,c->block);}
}
void nwpc_sha256_final(struct nwpc_sha256*c,uint8_t out[32]){
 uint64_t bits=c->bytes*8;uint8_t pad=0x80,z=0,len[8];
 nwpc_sha256_update(c,&pad,1);while((c->bytes&63)!=56)nwpc_sha256_update(c,&z,1);
 for(unsigned i=0;i<8;i++)len[i]=(uint8_t)(bits>>(56-8*i));
 nwpc_sha256_update(c,len,8);
 for(unsigned i=0;i<8;i++){out[4*i]=(uint8_t)(c->h[i]>>24);out[4*i+1]=(uint8_t)(c->h[i]>>16);out[4*i+2]=(uint8_t)(c->h[i]>>8);out[4*i+3]=(uint8_t)c->h[i];}
 clear(c,sizeof *c);
}
int nwpc_hmac256(const uint8_t*k,size_t kn,const uint8_t*const*p,const size_t*n,size_t count,uint8_t out[32]){
 if(!out)return -1;
 if((!k&&kn)||kn>64||(count&&(!p||!n))||count>6){clear(out,32);return -1;}
 for(size_t i=0;i<count;i++)if(n[i]>1024||(!p[i]&&n[i])){clear(out,32);return -1;}
 uint8_t pad[64],digest[32];struct nwpc_sha256 c;
 for(unsigned i=0;i<64;i++)pad[i]=(i<kn?k[i]:0)^0x36;
 nwpc_sha256_init(&c);nwpc_sha256_update(&c,pad,64);
 for(size_t i=0;i<count;i++)nwpc_sha256_update(&c,p[i],n[i]);
 nwpc_sha256_final(&c,digest);
 for(unsigned i=0;i<64;i++)pad[i]^=0x36^0x5c;
 nwpc_sha256_init(&c);nwpc_sha256_update(&c,pad,64);nwpc_sha256_update(&c,digest,32);nwpc_sha256_final(&c,out);
 clear(pad,64);clear(digest,32);return 0;
}
int nwpc_kdf256(const uint8_t*k,size_t kn,const char*label,const uint8_t*context,size_t cn,uint8_t*out,size_t bytes){
 if(!out||!bytes||bytes>64)return -1;
 size_t ln=0;if(label)while(label[ln])ln++;
 if(!k||!label||!ln||ln>64||(!context&&cn)||cn>1024){clear(out,bytes);return -1;}
 uint8_t d[32],bits[2]={(uint8_t)(bytes*8),(uint8_t)(bytes*8>>8)};
 for(size_t done=0,i=1;done<bytes;i++){
  uint8_t counter[2]={(uint8_t)i,0};const uint8_t*p[4]={counter,(const uint8_t*)label,context,bits};size_t ns[4]={2,ln,cn,2};
  if(nwpc_hmac256(k,kn,p,ns,4,d)){clear(out,bytes);return -1;}
  size_t take=bytes-done<32?bytes-done:32;memcpy(out+done,d,take);done+=take;
 }
 clear(d,32);return 0;
}
int nwpc_ptk256(const uint8_t key[32],const uint8_t context[76],uint8_t out[48]){
 if(!out)return -1;
 if(!key||!context){clear(out,48);return -1;}
 return nwpc_kdf256(key,32,"Pairwise key expansion",context,76,out,48);
}
static void cmac_double(uint8_t b[16]){unsigned carry=b[0]>>7;for(unsigned i=0;i<15;i++)b[i]=(uint8_t)(b[i]<<1|b[i+1]>>7);b[15]=(uint8_t)(b[15]<<1)^(carry?0x87:0);}
int nwpc_cmac(const uint8_t key[16],const uint8_t*const*p,const size_t*n,size_t count,uint8_t out[16]){
 if(!out)return -1;
 if(!key||(count&&(!p||!n))||count>4){clear(out,16);return -1;}
 for(size_t i=0;i<count;i++)if(n[i]>1024||(!p[i]&&n[i])){clear(out,16);return -1;}
 br_aes_ct_cbcenc_keys ctx;uint8_t x[16]={0},sub[16]={0},block[16],iv[16]={0};unsigned fill=0;
 br_aes_ct_cbcenc_init(&ctx,key,16);
 br_aes_ct_cbcenc_run(&ctx,iv,sub,16);cmac_double(sub); /* K1 */
 for(size_t i=0;i<count;i++)for(size_t j=0;j<n[i];j++){
  if(fill==16){br_aes_ct_cbcenc_run(&ctx,x,block,16);fill=0;} /* x = E(x ^ block) */
  block[fill++]=p[i][j];
 }
 if(fill!=16){cmac_double(sub);block[fill++]=0x80;while(fill<16)block[fill++]=0;} /* K2 */
 for(unsigned i=0;i<16;i++)block[i]^=sub[i];
 br_aes_ct_cbcenc_run(&ctx,x,block,16);memcpy(out,x,16);
 clear(&ctx,sizeof ctx);clear(x,16);clear(sub,16);clear(block,16);clear(iv,16);return 0;
}

#endif /* NORD2 */
