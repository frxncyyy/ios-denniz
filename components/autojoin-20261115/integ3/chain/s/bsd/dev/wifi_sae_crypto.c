#include <arm64/sweet_early.h>
#if defined(NORD2)
/* 0432: SHA-256 family glue (RFC 2104, RFC 4493, IEEE 802.11-2020 12.7.1.6.2). */
#include "wifi_sae_crypto.h"
#if defined(KERNEL) || defined(_KERNEL)
#include "../../libkern/crypto/sweet_bearssl/inc/bearssl_block.h"
#include <sys/systm.h>
#include <libkern/crypto/sha2.h>
typedef SHA256_CTX hash;
static void sc_hash_init(hash*c){SHA256_Init(c);}
static void sc_hash_update(hash*c,const void*p,size_t n){SHA256_Update(c,p,n);}
static void sc_hash_final(hash*c,uint8_t out[32]){SHA256_Final(out,c);}
#else
#include "bearssl_block.h"
#include <string.h>
/* Host tests: FIPS 180-4 SHA-256 standing in for libkern's SHA256_*. */
typedef struct {uint32_t h[8];uint64_t bytes;uint8_t block[64];} hash;
static uint32_t sc_ror(uint32_t x,unsigned n){return x>>n|x<<(32-n);}
static void sc_block(uint32_t h[8],const uint8_t*p){
 static const uint32_t k[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64],s[8];
 for(unsigned i=0;i<16;i++)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
 for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(sc_ror(w[i-15],7)^sc_ror(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(sc_ror(w[i-2],17)^sc_ror(w[i-2],19)^(w[i-2]>>10));
 for(unsigned i=0;i<8;i++)s[i]=h[i];
 for(unsigned i=0;i<64;i++){
  uint32_t a=s[0],e=s[4];
  uint32_t t1=s[7]+(sc_ror(e,6)^sc_ror(e,11)^sc_ror(e,25))+((e&s[5])^(~e&s[6]))+k[i]+w[i];
  uint32_t t2=(sc_ror(a,2)^sc_ror(a,13)^sc_ror(a,22))+((a&s[1])^(a&s[2])^(s[1]&s[2]));
  for(unsigned j=7;j;j--)s[j]=s[j-1];
  s[4]+=t1;s[0]=t1+t2;
 }
 for(unsigned i=0;i<8;i++)h[i]+=s[i];
}
static void sc_hash_init(hash*c){
 static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 for(unsigned i=0;i<8;i++)c->h[i]=iv[i];c->bytes=0;
}
static void sc_hash_update(hash*c,const void*v,size_t n){
 const uint8_t*p=v;
 while(n--){c->block[c->bytes++&63]=*p++;if(!(c->bytes&63))sc_block(c->h,c->block);}
}
static void sc_hash_final(hash*c,uint8_t out[32]){
 uint64_t bits=c->bytes*8;uint8_t pad=0x80,z=0,len[8];
 sc_hash_update(c,&pad,1);while((c->bytes&63)!=56)sc_hash_update(c,&z,1);
 for(unsigned i=0;i<8;i++)len[i]=(uint8_t)(bits>>(56-8*i));
 sc_hash_update(c,len,8);
 for(unsigned i=0;i<8;i++){out[4*i]=(uint8_t)(c->h[i]>>24);out[4*i+1]=(uint8_t)(c->h[i]>>16);out[4*i+2]=(uint8_t)(c->h[i]>>8);out[4*i+3]=(uint8_t)c->h[i];}
}
#endif
static void sc_clear(void*v,size_t n){volatile uint8_t*p=v;while(n--)*p++=0;}
int nsc_hmac256(const uint8_t*k,size_t kn,const uint8_t*const*p,const size_t*n,size_t count,uint8_t out[32]){
 if(!out)return -1;
 if((!k&&kn)||kn>64||(count&&(!p||!n))||count>6){sc_clear(out,32);return -1;}
 for(size_t i=0;i<count;i++)if(n[i]>1024||(!p[i]&&n[i])){sc_clear(out,32);return -1;}
 uint8_t pad[64],digest[32];hash c;
 for(unsigned i=0;i<64;i++)pad[i]=(i<kn?k[i]:0)^0x36;
 sc_hash_init(&c);sc_hash_update(&c,pad,64);
 for(size_t i=0;i<count;i++)if(n[i])sc_hash_update(&c,p[i],n[i]);
 sc_hash_final(&c,digest);
 for(unsigned i=0;i<64;i++)pad[i]^=0x36^0x5c;
 sc_hash_init(&c);sc_hash_update(&c,pad,64);sc_hash_update(&c,digest,32);sc_hash_final(&c,out);
 sc_clear(&c,sizeof c);sc_clear(pad,64);sc_clear(digest,32);return 0;
}
int nsc_kdf256(const uint8_t*k,size_t kn,const char*label,const uint8_t*context,size_t cn,uint8_t*out,size_t bytes){
 if(!out||!bytes||bytes>64)return -1;
 size_t ln=0;if(label)while(label[ln])ln++;
 if(!k||!label||!ln||ln>64||(!context&&cn)||cn>1024){sc_clear(out,bytes);return -1;}
 uint8_t d[32],bits[2]={(uint8_t)(bytes*8),(uint8_t)(bytes*8>>8)};
 for(size_t done=0,i=1;done<bytes;i++){
  uint8_t counter[2]={(uint8_t)i,0};const uint8_t*p[4]={counter,(const uint8_t*)label,context,bits};size_t ns[4]={2,ln,cn,2};
  if(nsc_hmac256(k,kn,p,ns,4,d)){sc_clear(out,bytes);return -1;}
  size_t take=bytes-done<32?bytes-done:32;memcpy(out+done,d,take);done+=take;
 }
 sc_clear(d,32);return 0;
}
int nsc_ptk256(const uint8_t key[32],const uint8_t context[76],uint8_t out[48]){
 if(!out)return -1;
 if(!key||!context){sc_clear(out,48);return -1;}
 return nsc_kdf256(key,32,"Pairwise key expansion",context,76,out,48);
}
static void sc_cmac_double(uint8_t b[16]){unsigned carry=b[0]>>7;for(unsigned i=0;i<15;i++)b[i]=(uint8_t)(b[i]<<1|b[i+1]>>7);b[15]=(uint8_t)((b[15]<<1)^(carry?0x87:0));}
int nsc_cmac(const uint8_t key[16],const uint8_t*const*p,const size_t*n,size_t count,uint8_t out[16]){
 if(!out)return -1;
 if(!key||(count&&(!p||!n))||count>4){sc_clear(out,16);return -1;}
 for(size_t i=0;i<count;i++)if(n[i]>1024||(!p[i]&&n[i])){sc_clear(out,16);return -1;}
 br_aes_ct_cbcenc_keys ctx;uint8_t x[16]={0},sub[16]={0},block[16],iv[16]={0};unsigned fill=0;
 br_aes_ct_cbcenc_init(&ctx,key,16);
 br_aes_ct_cbcenc_run(&ctx,iv,sub,16);sc_cmac_double(sub); /* K1 */
 for(size_t i=0;i<count;i++)for(size_t j=0;j<n[i];j++){
  if(fill==16){br_aes_ct_cbcenc_run(&ctx,x,block,16);fill=0;} /* x = E(x ^ block) */
  block[fill++]=p[i][j];
 }
 if(fill!=16){sc_cmac_double(sub);block[fill++]=0x80;while(fill<16)block[fill++]=0;} /* K2 */
 for(unsigned i=0;i<16;i++)block[i]^=sub[i];
 br_aes_ct_cbcenc_run(&ctx,x,block,16);memcpy(out,x,16);
 sc_clear(&ctx,sizeof ctx);sc_clear(x,16);sc_clear(sub,16);sc_clear(block,16);sc_clear(iv,16);return 0;
}

#endif /* NORD2 */
