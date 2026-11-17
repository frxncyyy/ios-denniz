#ifndef WIFI_8021X_H
#define WIFI_8021X_H
/* 0450: WPA2-Enterprise (IEEE 802.1X) pieces shared by the join decoder, the
 * 802.1X instance of the WPA2 engine (wifi_8021x_wpa2.c) and its glue. Header
 * only.
 *  - AKM 00-0F-AC:1 (802.1X): same PRF-SHA1 / HMAC-SHA1 key descriptor 2 as PSK;
 *  - AKM 00-0F-AC:5 (802.1X SHA-256): KDF-SHA256 PTK and AES-128-CMAC MIC,
 *    key descriptor version 3 (IEEE 802.11-2012 11.6.1.7.2, 11.6.2).
 * SHA-256 (FIPS 180-4), HMAC (RFC 2104) and AES-128-CMAC (RFC 4493) are
 * written here; the AES S-box is computed (no table indexed by secret data).
 * No PMF, no FT, no Suite B, no TKIP: the callers refuse those profiles. */
#include <stddef.h>
#include <stdint.h>
#define N8X_AKM_PSK 2u
#define N8X_AKM_8021X 1u
#define N8X_AKM_8021X_SHA256 5u
/* The larger primitives are one out-of-line copy per translation unit that uses them (kernel image size). */
#if defined(__clang__)
#define N8X_FN static __attribute__((noinline,unused,minsize))
#else
#define N8X_FN static __attribute__((noinline,unused))
#endif
/* The only station RSN elements this driver sends: CCMP group and pairwise,
 * one AKM, capabilities 0. Byte 19 is the AKM suite type. */
static inline void n8x_station_rsn(unsigned akm,uint8_t out[22]){
 static const uint8_t base[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 for(unsigned i=0;i<22;i++)out[i]=base[i];out[19]=(uint8_t)akm;
}
/* 2, 1 or 5 for such an element, 0 for anything else. */
static inline unsigned n8x_rsn_akm(const uint8_t rsn[22]){
 uint8_t want[22];unsigned d=0;if(!rsn)return 0;
 n8x_station_rsn(rsn[19],want);for(unsigned i=0;i<22;i++)d|=(unsigned)(rsn[i]^want[i]);
 return !d&&(rsn[19]==N8X_AKM_PSK||rsn[19]==N8X_AKM_8021X||rsn[19]==N8X_AKM_8021X_SHA256)?rsn[19]:0;
}
static inline int n8x_eap_akm(unsigned akm){return akm==N8X_AKM_8021X||akm==N8X_AKM_8021X_SHA256;}
/* The native station binder (sweet_touch.o) and the IOUC client are not rebuilt:
 * they know WPA2-PSK and OPEN only. An 802.1X join therefore travels through
 * them as a WPA2-PSK request whose "PMK" is this marker (its last byte is the
 * AKM). wifi_ios_join.c writes it, niwj_select and the nwn_begin wrapper of
 * wifi_8021x_glue.c recognise it; it never reaches a key derivation. */
static inline void n8x_marker(unsigned akm,uint8_t out[32]){
 static const uint8_t m[32]={'N','O','R','D','2',' ','8','0','2','.','1','X',' ','n','o',' ','P','M','K',' ','y','e','t',':',' ','E','A','P',0xff,0x00,0xa5,0};
 for(unsigned i=0;i<32;i++)out[i]=m[i];out[31]=(uint8_t)akm;
}
static inline unsigned n8x_marker_akm(const uint8_t pmk[32]){
 uint8_t m[32];unsigned d=0;if(!pmk||!n8x_eap_akm(pmk[31]))return 0;
 n8x_marker(pmk[31],m);for(unsigned i=0;i<32;i++)d|=(unsigned)(pmk[i]^m[i]);return d?0:pmk[31];
}
static inline void n8x_wipe(void*v,size_t n){volatile uint8_t*p=(volatile uint8_t*)v;while(n--)*p++=0;}
/* ---- SHA-256 */
struct n8x_sha256 {uint32_t h[8];uint64_t bytes;uint8_t block[64];};
static inline uint32_t n8x_ror(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
N8X_FN void n8x_sha256_block(uint32_t h[8],const uint8_t*p){
 static const uint32_t k[64]={
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64],a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],x=h[7];
 for(unsigned i=0;i<16;i++)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
 for(unsigned i=16;i<64;i++){uint32_t s0=n8x_ror(w[i-15],7)^n8x_ror(w[i-15],18)^(w[i-15]>>3),s1=n8x_ror(w[i-2],17)^n8x_ror(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
 for(unsigned i=0;i<64;i++){
  uint32_t t1=x+(n8x_ror(e,6)^n8x_ror(e,11)^n8x_ror(e,25))+((e&f)^(~e&g))+k[i]+w[i],t2=(n8x_ror(a,2)^n8x_ror(a,13)^n8x_ror(a,22))+((a&b)^(a&c)^(b&c));
  x=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
 }
 h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=x;n8x_wipe(w,sizeof w);
}
static inline void n8x_sha256_init(struct n8x_sha256*s){
 static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 for(unsigned i=0;i<8;i++)s->h[i]=iv[i];s->bytes=0;
}
N8X_FN void n8x_sha256_update(struct n8x_sha256*s,const uint8_t*p,size_t n){
 while(n--){s->block[s->bytes++&63]=*p++;if(!(s->bytes&63))n8x_sha256_block(s->h,s->block);}
}
N8X_FN void n8x_sha256_out(struct n8x_sha256*s,uint8_t out[32]){
 uint64_t bits=s->bytes*8;uint8_t pad=0x80,zero=0,len[8];
 n8x_sha256_update(s,&pad,1);while((s->bytes&63)!=56)n8x_sha256_update(s,&zero,1);
 for(unsigned i=0;i<8;i++)len[i]=(uint8_t)(bits>>(56-8*i));n8x_sha256_update(s,len,8);
 for(unsigned i=0;i<8;i++){out[4*i]=(uint8_t)(s->h[i]>>24);out[4*i+1]=(uint8_t)(s->h[i]>>16);out[4*i+2]=(uint8_t)(s->h[i]>>8);out[4*i+3]=(uint8_t)s->h[i];}
 n8x_wipe(s,sizeof *s);
}
/* Key up to 64 bytes, at most 4 parts. 0 done, -1 refused (output zeroed). */
N8X_FN int n8x_hmac_sha256(const uint8_t*k,size_t kn,const uint8_t*const*p,const size_t*n,size_t count,uint8_t out[32]){
 if(!out)return -1;
 if(!k||kn>64||!p||!n||count>4){n8x_wipe(out,32);return -1;}
 for(size_t i=0;i<count;i++)if(!p[i]&&n[i]){n8x_wipe(out,32);return -1;}
 uint8_t pad[64],digest[32];struct n8x_sha256 c;
 for(unsigned i=0;i<64;i++)pad[i]=(uint8_t)((i<kn?k[i]:0)^0x36);
 n8x_sha256_init(&c);n8x_sha256_update(&c,pad,64);
 for(size_t i=0;i<count;i++)if(n[i])n8x_sha256_update(&c,p[i],n[i]);
 n8x_sha256_out(&c,digest);
 for(unsigned i=0;i<64;i++)pad[i]^=0x36^0x5c;
 n8x_sha256_init(&c);n8x_sha256_update(&c,pad,64);n8x_sha256_update(&c,digest,32);n8x_sha256_out(&c,out);
 n8x_wipe(pad,64);n8x_wipe(digest,32);return 0;
}
/* KDF-SHA256-384(PMK, "Pairwise key expansion", min(AA,SPA)||max||min(nonces)||max):
 * KCK 16, KEK 16, TK 16. The label has no terminator in this KDF. */
N8X_FN int n8x_ptk_sha256(const uint8_t pmk[32],const uint8_t context[76],uint8_t out[48]){
 if(!out)return -1;
 if(!pmk||!context){n8x_wipe(out,48);return -1;}
 static const uint8_t label[22]={'P','a','i','r','w','i','s','e',' ','k','e','y',' ','e','x','p','a','n','s','i','o','n'};
 static const uint8_t bits[2]={0x80,0x01}; /* 384, little endian */
 uint8_t d[32];
 for(unsigned i=1;i<=2;i++){
  uint8_t counter[2]={(uint8_t)i,0};const uint8_t*parts[4]={counter,label,context,bits};size_t sizes[4]={2,22,76,2};
  (void)n8x_hmac_sha256(pmk,32,parts,sizes,4,d);for(unsigned j=0;j<(i==1?32u:16u);j++)out[32*(i-1)+j]=d[j];
 }
 n8x_wipe(d,32);return 0;
}
/* ---- AES-128 encryption with a computed S-box, then CMAC */
static inline uint8_t n8x_xtime(uint8_t x){return (uint8_t)((x<<1)^((uint8_t)(0u-(unsigned)(x>>7))&0x1b));}
N8X_FN uint8_t n8x_gmul(uint8_t a,uint8_t b){
 uint8_t r=0;for(unsigned i=0;i<8;i++){r^=(uint8_t)((uint8_t)(0u-(unsigned)(b&1))&a);a=n8x_xtime(a);b>>=1;}return r;
}
N8X_FN uint8_t n8x_sbox(uint8_t x){
 /* x^254 in GF(2^8) (0 stays 0), then the affine map. */
 uint8_t x2=n8x_gmul(x,x),x4=n8x_gmul(x2,x2),x8=n8x_gmul(x4,x4),x16=n8x_gmul(x8,x8),x32=n8x_gmul(x16,x16),x64=n8x_gmul(x32,x32),x128=n8x_gmul(x64,x64);
 uint8_t i=n8x_gmul(n8x_gmul(n8x_gmul(x2,x4),n8x_gmul(x8,x16)),n8x_gmul(n8x_gmul(x32,x64),x128));
 return (uint8_t)(i^(uint8_t)(i<<1|i>>7)^(uint8_t)(i<<2|i>>6)^(uint8_t)(i<<3|i>>5)^(uint8_t)(i<<4|i>>4)^0x63);
}
N8X_FN void n8x_aes128_keys(const uint8_t key[16],uint8_t rk[176]){
 uint8_t rcon=1;for(unsigned i=0;i<16;i++)rk[i]=key[i];
 for(unsigned i=16;i<176;i+=4){
  uint8_t t[4]={rk[i-4],rk[i-3],rk[i-2],rk[i-1]};
  if(!(i&15)){uint8_t u=t[0];t[0]=(uint8_t)(n8x_sbox(t[1])^rcon);t[1]=n8x_sbox(t[2]);t[2]=n8x_sbox(t[3]);t[3]=n8x_sbox(u);rcon=n8x_xtime(rcon);}
  for(unsigned j=0;j<4;j++)rk[i+j]=(uint8_t)(rk[i-16+j]^t[j]);
 }
}
N8X_FN void n8x_aes128_encrypt(const uint8_t rk[176],uint8_t s[16]){
 for(unsigned i=0;i<16;i++)s[i]^=rk[i];
 for(unsigned round=1;round<=10;round++){
  uint8_t t[16];
  for(unsigned c=0;c<4;c++)for(unsigned r=0;r<4;r++)t[4*c+r]=n8x_sbox(s[4*((c+r)&3)+r]); /* SubBytes + ShiftRows */
  if(round<10)for(unsigned c=0;c<4;c++){
   uint8_t a=t[4*c],b=t[4*c+1],d=t[4*c+2],e=t[4*c+3],x=(uint8_t)(a^b^d^e);
   t[4*c]=(uint8_t)(a^x^n8x_xtime((uint8_t)(a^b)));t[4*c+1]=(uint8_t)(b^x^n8x_xtime((uint8_t)(b^d)));
   t[4*c+2]=(uint8_t)(d^x^n8x_xtime((uint8_t)(d^e)));t[4*c+3]=(uint8_t)(e^x^n8x_xtime((uint8_t)(e^a)));
  }
  for(unsigned i=0;i<16;i++)s[i]=(uint8_t)(t[i]^rk[16*round+i]);
  n8x_wipe(t,16);
 }
}
static inline void n8x_cmac_double(uint8_t b[16]){
 uint8_t carry=(uint8_t)(b[0]>>7);for(unsigned i=0;i<15;i++)b[i]=(uint8_t)(b[i]<<1|b[i+1]>>7);
 b[15]=(uint8_t)((b[15]<<1)^((uint8_t)(0u-(unsigned)carry)&0x87));
}
/* AES-128-CMAC over at most 4 parts. 0 done, -1 refused (output zeroed). */
N8X_FN int n8x_cmac(const uint8_t key[16],const uint8_t*const*p,const size_t*n,size_t count,uint8_t out[16]){
 if(!out)return -1;
 if(!key||!p||!n||count>4){n8x_wipe(out,16);return -1;}
 size_t total=0;for(size_t i=0;i<count;i++){if(!p[i]&&n[i]){n8x_wipe(out,16);return -1;}total+=n[i];}
 uint8_t rk[176],k1[16]={0},x[16]={0};unsigned fill=0;size_t seen=0;
 n8x_aes128_keys(key,rk);n8x_aes128_encrypt(rk,k1);n8x_cmac_double(k1);
 for(size_t i=0;i<count;i++)for(size_t j=0;j<n[i];j++){
  if(fill==16){n8x_aes128_encrypt(rk,x);fill=0;} /* a full block that is not the last one */
  x[fill++]^=p[i][j];seen++;
 }
 if(fill==16&&seen==total&&total){for(unsigned i=0;i<16;i++)x[i]^=k1[i];}
 else{n8x_cmac_double(k1);x[fill]^=0x80;for(unsigned i=0;i<16;i++)x[i]^=k1[i];} /* K2 with 10* padding */
 n8x_aes128_encrypt(rk,x);for(unsigned i=0;i<16;i++)out[i]=x[i];
 n8x_wipe(rk,sizeof rk);n8x_wipe(k1,16);n8x_wipe(x,16);return 0;
}
#endif
