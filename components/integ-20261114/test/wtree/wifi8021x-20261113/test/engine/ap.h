/* Synthetic authenticator for the 0450/0451 host tests: an independent (OpenSSL) implementation of the RSN 4-way and
 * group handshakes for AKM 1/2 (PRF-SHA1, HMAC-SHA1-128, key descriptor 2) and AKM 5 (KDF-SHA256, AES-128-CMAC, 3). */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations" /* CMAC_* is the simplest independent reference */
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/cmac.h>
#define AP_FN static __attribute__((unused))
struct ap {unsigned akm;uint8_t pmk[32],aa[6],spa[6],anonce[32],snonce[32],ptk[48],gtk[16],replay;uint8_t rsn[64];unsigned rsn_bytes;};
AP_FN unsigned ap_version(const struct ap*a){return a->akm==5?3u:2u;}
AP_FN void ap_ptk(struct ap*a){
 uint8_t ctx[76];const uint8_t*lo=a->aa,*hi=a->spa;if(memcmp(lo,hi,6)>0){lo=a->spa;hi=a->aa;}memcpy(ctx,lo,6);memcpy(ctx+6,hi,6);
 lo=a->anonce;hi=a->snonce;if(memcmp(lo,hi,32)>0){lo=a->snonce;hi=a->anonce;}memcpy(ctx+12,lo,32);memcpy(ctx+44,hi,32);
 unsigned len=0;uint8_t in[128],d[64];
 if(a->akm==5){for(unsigned i=1;i<=2;i++){in[0]=(uint8_t)i;in[1]=0;memcpy(in+2,"Pairwise key expansion",22);memcpy(in+24,ctx,76);in[100]=0x80;in[101]=1;assert(HMAC(EVP_sha256(),a->pmk,32,in,102,d,&len));memcpy(a->ptk+32*(i-1),d,i==1?32:16);}}
 else for(unsigned i=0;i<3;i++){memcpy(in,"Pairwise key expansion",23);memcpy(in+23,ctx,76);in[99]=(uint8_t)i;assert(HMAC(EVP_sha1(),a->pmk,32,in,100,d,&len));memcpy(a->ptk+20*i,d,i==2?8:20);}
}
AP_FN void ap_mic(const struct ap*a,const uint8_t*frame,unsigned n,uint8_t out[16]){
 uint8_t copy[700],d[32];assert(n<=sizeof copy&&n>=97);memcpy(copy,frame,n);memset(copy+81,0,16);
 if(a->akm==5){size_t z=0;CMAC_CTX*c=CMAC_CTX_new();assert(c&&CMAC_Init(c,a->ptk,16,EVP_aes_128_cbc(),0)&&CMAC_Update(c,copy,n)&&CMAC_Final(c,d,&z)&&z==16);CMAC_CTX_free(c);}
 else{unsigned len=0;assert(HMAC(EVP_sha1(),a->ptk,16,copy,n,d,&len));}
 memcpy(out,d,16);
}
/* EAPOL-Key frame with this key information (version bits added), key data (wrapped with the KEK when asked) and MIC. */
AP_FN unsigned ap_key(struct ap*a,uint8_t*f,unsigned info,const uint8_t*nonce,const uint8_t*data,unsigned bytes,int wrap,int mic){
 uint8_t body[256];unsigned z=bytes;
 if(wrap){uint8_t plain[256];memcpy(plain,data,bytes);if(bytes<16||(bytes&7)){plain[bytes++]=0xdd;while(bytes<16||(bytes&7))plain[bytes++]=0;}
  EVP_CIPHER_CTX*c=EVP_CIPHER_CTX_new();assert(c);EVP_CIPHER_CTX_set_flags(c,EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);int n=0,end=0;assert(EVP_EncryptInit_ex(c,EVP_aes_128_wrap(),0,a->ptk+16,0)&&EVP_EncryptUpdate(c,body,&n,plain,(int)bytes)&&EVP_EncryptFinal_ex(c,body+n,&end));EVP_CIPHER_CTX_free(c);z=(unsigned)(n+end);assert(z==bytes+8);}
 else if(bytes)memcpy(body,data,bytes);
 unsigned n=99+z;memset(f,0,n);f[0]=2;f[1]=3;f[2]=(uint8_t)((n-4)>>8);f[3]=(uint8_t)(n-4);f[4]=2;info|=ap_version(a);f[5]=(uint8_t)(info>>8);f[6]=(uint8_t)info;f[8]=16;f[16]=++a->replay;
 if(nonce)memcpy(f+17,nonce,32);f[97]=(uint8_t)(z>>8);f[98]=(uint8_t)z;memcpy(f+99,body,z);if(mic)ap_mic(a,f,n,f+81);return n;
}
AP_FN unsigned ap_m1(struct ap*a,uint8_t*f){return ap_key(a,f,0x0088,a->anonce,0,0,0,0);}
/* Checks message 2 of the station (key information, replay, RSN element, MIC) and takes its nonce. */
AP_FN void ap_m2(struct ap*a,const uint8_t*f,unsigned n,const uint8_t station_rsn[22]){
 uint8_t mic[16];assert(n==121&&f[1]==3&&f[4]==2&&(((unsigned)f[5]<<8)|f[6])==(0x0108u|ap_version(a))&&f[16]==a->replay&&f[98]==22&&!memcmp(f+99,station_rsn,22));
 memcpy(a->snonce,f+17,32);ap_ptk(a);ap_mic(a,f,n,mic);assert(!memcmp(mic,f+81,16));
}
AP_FN unsigned ap_m3(struct ap*a,uint8_t*f){
 uint8_t kde[128];unsigned z=0;memcpy(kde,a->rsn,a->rsn_bytes);z=a->rsn_bytes;
 kde[z++]=0xdd;kde[z++]=22;kde[z++]=0;kde[z++]=0x0f;kde[z++]=0xac;kde[z++]=1;kde[z++]=1;kde[z++]=0;memcpy(kde+z,a->gtk,16);z+=16;
 return ap_key(a,f,0x13c8,a->anonce,kde,z,1,1);
}
AP_FN void ap_m4(const struct ap*a,const uint8_t*f,unsigned n){uint8_t mic[16];assert(n==99&&(((unsigned)f[5]<<8)|f[6])==(0x0308u|ap_version(a))&&f[16]==a->replay&&!f[98]);ap_mic(a,f,n,mic);assert(!memcmp(mic,f+81,16));}
AP_FN unsigned ap_group1(struct ap*a,uint8_t*f,const uint8_t gtk[16],unsigned index){
 uint8_t kde[32];unsigned z=0;kde[z++]=0xdd;kde[z++]=22;kde[z++]=0;kde[z++]=0x0f;kde[z++]=0xac;kde[z++]=1;kde[z++]=(uint8_t)index;kde[z++]=0;memcpy(kde+z,gtk,16);z+=16;
 return ap_key(a,f,0x1380,0,kde,z,1,1);
}
AP_FN void ap_group2(const struct ap*a,const uint8_t*f,unsigned n){uint8_t mic[16];assert(n==99&&(((unsigned)f[5]<<8)|f[6])==(0x0300u|ap_version(a))&&f[16]==a->replay);ap_mic(a,f,n,mic);assert(!memcmp(mic,f+81,16));}
