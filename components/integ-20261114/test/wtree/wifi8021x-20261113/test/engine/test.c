/* 0450 host test: the 802.1X instance of the WPA2 engine (wifi_8021x_wpa2.c, generated from wifi_wpa2.c) against the synthetic authenticator for AKM 1 (802.1X, version 2),
 * AKM 5 (802.1X SHA-256, version 3) and AKM 2 (PSK, unchanged). */
#include <stdio.h>
#include "ap.h"
#include "wifi_8021x_rename.h" /* the 802.1X instance: nwp_* are e8_nwp_* here */
#include "wifi_wpa2.h"
#include "wifi_8021x.h"
unsigned niwj_8021x=0x11;
static unsigned rng_calls;
static int rng(void*c,uint8_t*out,size_t n){(void)c;rng_calls++;memset(out,(int)(0x50+rng_calls),n);return 0;}
static void authenticator(struct ap*a,unsigned akm,unsigned caps){
 memset(a,0,sizeof *a);a->akm=akm;for(unsigned i=0;i<32;i++){a->pmk[i]=(uint8_t)(0xa0+i);a->anonce[i]=(uint8_t)(3*i+akm);}for(unsigned i=0;i<16;i++)a->gtk[i]=(uint8_t)(0x30+i);
 static const uint8_t aa[6]={0x00,0x11,0x22,0x33,0x44,0x55},spa[6]={0x02,0x66,0x77,0x88,0x99,0xaa};memcpy(a->aa,aa,6);memcpy(a->spa,spa,6);
 n8x_station_rsn(akm,a->rsn);a->rsn[20]=(uint8_t)caps;a->rsn_bytes=22;
}
static void handshake(unsigned akm,unsigned caps){
 struct ap a;static struct nwp w;struct nwp_result r;uint8_t f[700],station[22];unsigned n;
 authenticator(&a,akm,caps);n8x_station_rsn(akm,station);
 assert(!nwp_begin_ap(&w,7,a.pmk,a.spa,a.aa,station,a.rsn,a.rsn_bytes,rng,0)&&w.state==NWP_M1&&!nwp_copy_ap_rsnxe(&w,0,0));
 /* message 1 of the other descriptor version is refused before any key exists */
 {static struct nwp x;assert(!nwp_begin_ap(&x,7,a.pmk,a.spa,a.aa,station,a.rsn,a.rsn_bytes,rng,0));struct ap b=a;b.akm=akm==5?1:5;n=ap_m1(&b,f);assert(nwp_receive(&x,f,n,&r)==NWP_FAIL&&r.failure==NWP_F_KEYINFO_STATE);}
 n=ap_m1(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX&&r.bytes==121);ap_m2(&a,r.frame,r.bytes,station);
 assert(!memcmp(w.kck,a.ptk,16)&&!memcmp(w.kek,a.ptk+16,16)&&!memcmp(w.tk,a.ptk+32,16)); /* same PTK as the independent derivation */
 uint64_t t2=r.ticket;assert(nwp_security_submitted(&w,7,t2,1)==NWP_DROP);
 n=ap_m3(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX&&r.bytes==99);ap_m4(&a,r.frame,r.bytes);assert(!memcmp(w.gtk,a.gtk,16)&&w.gtk_index==1);
 uint64_t t4=r.ticket;assert(nwp_security_submitted(&w,7,t4,1)==NWP_INSTALL&&w.state==NWP_KEYS);
 assert(nwp_keys_ready(&w,7,t4,1,1)==NWP_DROP&&nwp_keys_ready(&w,7,t4,2,1)==NWP_DROP&&nwp_keys_ready(&w,7,t4,4,1)==NWP_AUTH&&w.state==NWP_AUTHORIZED);
 /* group key renewal with the same descriptor version */
 uint8_t next[16];memset(next,0x77,16);n=ap_group1(&a,f,next,2);assert(nwp_receive(&w,f,n,&r)==NWP_INSTALL);uint64_t g=r.ticket;
 assert(nwp_group_ready(&w,7,g,1,&r)==NWP_TX&&r.bytes==99);ap_group2(&a,r.frame,r.bytes);assert(nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP&&!memcmp(w.gtk,next,16)&&w.gtk_index==2&&w.state==NWP_AUTHORIZED);
 /* ---- 0452: PTK rekey on the authorized link (kern.n2wifi_8021x bit 2) ---- */
 if(akm!=2){
  uint8_t old[48],kck[16];memcpy(old,a.ptk,48);memcpy(kck,w.kck,16);niwj_8021x=0x13;
  /* a message 1 nobody follows up (forged, stale): answered under a candidate key set, the link keeps its keys */
  {struct ap b=a;memset(b.anonce,0xee,32);n=ap_m1(&b,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX&&r.bytes==121&&nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP);a.replay=b.replay;
   assert(w.state==NWP_AUTHORIZED&&!memcmp(w.kck,kck,16)&&!memcmp(w.tk,old+32,16)&&!nwp_rekeying(&w));
   uint8_t g2[16];memset(g2,0x78,16);n=ap_group1(&a,f,g2,1);assert(nwp_receive(&w,f,n,&r)==NWP_INSTALL);uint64_t g=r.ticket;assert(nwp_group_ready(&w,7,g,1,&r)==NWP_TX);ap_group2(&a,r.frame,r.bytes);assert(nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP);memcpy(a.gtk,g2,16);}
  /* message 3 that does not verify (the AP holds another PMK): dropped, nothing changes */
  for(unsigned i=0;i<32;i++)a.anonce[i]^=0x55;n=ap_m1(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX);ap_m2(&a,r.frame,r.bytes,station);assert(nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP);
  {struct ap b=a;b.ptk[3]^=1;n=ap_m3(&b,f);a.replay=b.replay;f[130]^=0;assert(nwp_receive(&w,f,n,&r)==NWP_DROP&&w.state==NWP_AUTHORIZED&&!memcmp(w.tk,old+32,16));}
  /* the real rekey, same PMK: message 1 repeated (same SNonce), 2, 3, 4, install */
  n=ap_m1(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX);uint8_t sn[32];memcpy(sn,r.frame+17,32);assert(nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP);
  n=ap_m1(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX&&!memcmp(sn,r.frame+17,32));ap_m2(&a,r.frame,r.bytes,station);assert(nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP&&memcmp(a.ptk,old,48)&&!memcmp(w.tk,old+32,16));
  n=ap_m3(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX&&r.bytes==99);ap_m4(&a,r.frame,r.bytes);assert(!memcmp(w.kck,a.ptk,16)&&!memcmp(w.tk,a.ptk+32,16)&&!memcmp(w.gtk,a.gtk,16)&&!nwp_rekeying(&w));
  uint64_t t=r.ticket;assert(nwp_security_submitted(&w,7,t,1)==NWP_INSTALL&&nwp_rekeying(&w)&&nwp_keys_ready(&w,7,t,1,1)==NWP_DROP&&nwp_keys_ready(&w,7,t,2,1)==NWP_DROP&&nwp_keys_ready(&w,7,t,4,1)==NWP_AUTH);nwp_rekey_done();assert(!nwp_rekeying(&w)&&w.state==NWP_AUTHORIZED);
  /* message 3 repeated by the AP after the install: message 4 again, no second install */
  n=ap_m3(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX);ap_m4(&a,r.frame,r.bytes);assert(nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP&&w.state==NWP_AUTHORIZED);
  /* re-authentication: a new PMK, then the AP's handshake with it */
  uint8_t fresh[32];memset(fresh,0x3c,32);nwp_rekey_pmk(fresh);memcpy(a.pmk,fresh,32);for(unsigned i=0;i<32;i++)a.anonce[i]^=0x99;
  n=ap_m1(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX);ap_m2(&a,r.frame,r.bytes,station);assert(nwp_security_submitted(&w,7,r.ticket,1)==NWP_DROP);
  n=ap_m3(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_TX);ap_m4(&a,r.frame,r.bytes);t=r.ticket;assert(!memcmp(w.pmk,fresh,32)&&!memcmp(w.tk,a.ptk+32,16)&&nwp_security_submitted(&w,7,t,1)==NWP_INSTALL);
  assert(nwp_keys_ready(&w,7,t,7,1)==NWP_AUTH);nwp_rekey_done();
  /* bit 2 clear: a message 1 on the authorized link ends the handshake state as before 0452 */
  niwj_8021x=0x11;for(unsigned i=0;i<32;i++)a.anonce[i]^=0x21;n=ap_m1(&a,f);assert(nwp_receive(&w,f,n,&r)==NWP_FAIL&&w.state==NWP_OFF);
 }
 /* a wrong PMK (EAP gave another key) fails at the MIC of message 3 */
 static struct nwp bad;uint8_t other[32];memset(other,9,32);authenticator(&a,akm,caps);assert(!nwp_begin_ap(&bad,8,other,a.spa,a.aa,station,a.rsn,a.rsn_bytes,rng,0));
 n=ap_m1(&a,f);assert(nwp_receive(&bad,f,n,&r)==NWP_TX);memcpy(a.snonce,r.frame+17,32);ap_ptk(&a);n=ap_m3(&a,f);assert(nwp_receive(&bad,f,n,&r)==NWP_FAIL&&r.failure==NWP_F_MIC&&bad.state==NWP_OFF);
}
int main(void){
 uint8_t station[22],e[32];static struct nwp w;struct ap a;
 handshake(1,0);handshake(1,0x8c);handshake(5,0);handshake(5,0x80);handshake(2,0);
 /* the AP element must offer the AKM of the station element; MFP required and non-CCMP stay refused */
 authenticator(&a,1,0);n8x_station_rsn(1,station);
 n8x_station_rsn(2,e);assert(nwp_begin_ap(&w,1,a.pmk,a.spa,a.aa,station,e,22,rng,0)<0&&nwp_ap_rsn_akm(e,22,1)<0&&!nwp_ap_rsn_akm(e,22,2)&&!nwp_ap_rsn(e,22));
 n8x_station_rsn(5,e);assert(nwp_begin_ap(&w,1,a.pmk,a.spa,a.aa,station,e,22,rng,0)<0);
 n8x_station_rsn(1,e);assert(nwp_ap_rsn(e,22)<0&&!nwp_ap_rsn_akm(e,22,1));e[20]=0x40;assert(nwp_ap_rsn_akm(e,22,1)<0);e[20]=0;e[13]=2;assert(nwp_ap_rsn_akm(e,22,1)<0);
 n8x_station_rsn(8,station);n8x_station_rsn(8,e);assert(nwp_begin_ap(&w,1,a.pmk,a.spa,a.aa,station,e,22,rng,0)<0); /* SAE is not part of 0450 */
 puts("PASS 0450/0452 engine: 4-way + group handshake against an OpenSSL authenticator for AKM 1 (v2 HMAC-SHA1), AKM 5 (v3 AES-CMAC, KDF-SHA256) and PSK; identical PTK; wrong PMK fails at the M3 MIC; version and AKM mismatches refused; PTK rekey (bit 2): unanswered or unverifiable attempts change nothing, same-PMK rekey, repeated M1/M3, rekey with a new PMK, bit clear = old behaviour");return 0;}
