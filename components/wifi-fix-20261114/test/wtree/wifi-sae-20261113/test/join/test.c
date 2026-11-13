/* 0434 host test: IOUC join decoder for the iOS 16.7 SAE / PSK-SHA256 / MFP request shapes (as produced by
 * IO80211 __performAssociation once the card reports capability bits 75 and 52), the secret side channel,
 * BSS selection with SAE-only / PMF-required / H2E-only access points, nwq_negotiate, capability bits. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_ios_join.h"
#include "wifi_ios_card.h"
#include "wifi_ios_control.h"
#include "wifi_wpa3.h"
#include "wifi_station_transaction.h"
static const uint8_t owner[6]={0x00,0x0c,0xe7,0x10,0x20,0x30};
static unsigned hook_calls;
static int hook(const uint8_t*pass,size_t n,const uint8_t*ssid,size_t z,uint8_t out[32]){(void)pass;(void)ssid;hook_calls++;if(n<8||n>63||!z||z>32)return -1;memset(out,0x77,32);return 0;}
static size_t rsn(uint8_t*o,const uint8_t*akms,unsigned count,unsigned caps,int management){
 static const uint8_t head[14]={48,0,1,0,0,15,172,4,1,0,0,15,172,4};size_t n=14;memcpy(o,head,14);o[n++]=(uint8_t)count;o[n++]=0;
 for(unsigned i=0;i<count;i++){o[n++]=0;o[n++]=15;o[n++]=172;o[n++]=akms[i];}
 o[n++]=(uint8_t)caps;o[n++]=(uint8_t)(caps>>8);if(management){o[n++]=0;o[n++]=0;o[n++]=0;o[n++]=15;o[n++]=172;o[n++]=(uint8_t)management;}
 o[1]=(uint8_t)(n-2);return n;
}
/* personal join in the bit-75 layout: passphrase kind 10 at +72, PBKDF2 PMK at +472 */
static void join(uint8_t a[592],unsigned upper,unsigned flags,unsigned akm,unsigned caps,int management){
 memset(a,0,592);a[0]=1;a[4]=2;a[6]=1;a[8]=(uint8_t)upper;a[9]=(uint8_t)(upper>>8);a[12]=4;memcpy(a+16,"home",4);
 a[56]=1;a[60]=12;a[64]=10;memcpy(a+72,"password1234",12);a[468]=32;memset(a+472,0x5a,32);
 a[504]=(uint8_t)flags;a[505]=(uint8_t)(flags>>8);a[507]=(uint8_t)(flags>>24);
 uint8_t k=(uint8_t)akm;a[204]=(uint8_t)rsn(a+206,&k,1,caps,management);
}
static int dec(const uint8_t a[592],struct niwj_request*q){struct niwj_diagnostic d;return niwj_decode_identity(a,592,owner,q,&d);}
extern unsigned niwc_wpa3;
static void take(const struct niwj_request*q,struct stc_secret*s){memset(s,0,sizeof *s);(void)niwj_secret_take(q->ssid,q->ssid_length,s);}
static struct nsb_scan scan;static struct nss_info info;
static void bss(unsigned k,unsigned last,const uint8_t*ap_rsn,size_t z,int rsnxe,int h2e_only,int rssi){
 struct nsb_record*r=&scan.records[k];memset(r,0,sizeof *r);r->bssid[0]=0;r->bssid[5]=(uint8_t)(k+1);r->bssid[1]=0xaa;
 memcpy(r->ssid,"home",4);r->ssid_len=4;r->ssid_present=1;r->subtype=8;r->hw_channel=r->channel=6;r->group_valid=4;r->rxv_valid=1;r->rxv[4]=r->rxv[5]=(uint8_t)(2*(rssi+110));
 r->rsn_present=1;r->privacy=1;r->interval=100;r->capability=0x11;r->observed_us=1000;
 size_t n=0;r->ies[n++]=1;r->ies[n++]=(uint8_t)(h2e_only?5:4);r->ies[n++]=0x82;r->ies[n++]=0x84;r->ies[n++]=0x8b;r->ies[n++]=0x96;if(h2e_only)r->ies[n++]=0xfb;
 memcpy(r->ies+n,ap_rsn,z);n+=z;if(rsnxe){r->ies[n++]=244;r->ies[n++]=1;r->ies[n++]=0x20;}
 r->ie_bytes=r->ie_total=(uint16_t)n;if(last){scan.count=k+1;scan.frames=k+1;}
}
int main(void){
 uint8_t a[592],ie[64];struct niwj_request q;struct stc_secret s;struct niwj_target t;struct nwp_profile f;
 static const uint8_t k8[1]={8},k28[2]={2,8},k2[1]={2},k6[1]={6},k26[2]={2,6};
 niwj_pmk_hook=hook;niwj_privmac_mode=1;niwj_sae_mode=1;
 /* 1. WPA3-only SAE request: upper 0x1000, flags 0x100, element AKM 8 + MFPC|MFPR + BIP */
 join(a,0x1000,0x100,8,0xc0,6);hook_calls=0;assert(!dec(a,&q)&&q.security==NIWJ_WPA2_PSK_CCMP&&q.pmk[0]==0x5a&&!hook_calls&&!q.native_flags);
 take(&q,&s);assert(s.allow==(NIWJ_ALLOW_SAE|NIWJ_SAE_REQUESTED)&&s.bytes==12&&!memcmp(s.password,"password1234",12));
 take(&q,&s);assert(!s.allow&&!s.bytes); /* taken once */
 /* 2. transition: upper 0x1000, flags 0x40000004 -> SAE requested, WPA2 alternatives allowed */
 join(a,0x1000,0x40000004,8,0x80,6);assert(!dec(a,&q)&&q.native_flags==4);take(&q,&s);assert(s.allow==(NIWJ_ALLOW_LEGACY|NIWJ_ALLOW_PMF|NIWJ_ALLOW_SAE|NIWJ_SAE_REQUESTED));
 /* another SSID does not receive the record; two SSIDs wait side by side; a new decode of an SSID replaces its record; PMF flags travel */
 join(a,0x1000,0x100,8,0xc0,6);assert(!dec(a,&q));{struct niwj_request o=q;o.ssid[0]='x';take(&o,&s);assert(!s.bytes);
  uint8_t b[592];struct niwj_request q2;join(b,8,0x40000004,2,0x80,6);memcpy(b+16,"work",4);niwj_pmf_flags=3;assert(!dec(b,&q2));niwj_pmf_flags=0;
  join(a,0x1000,0x100,8,0xc0,6);memcpy(a+72,"PASSWORD5678",12);assert(!dec(a,&q));
  take(&q2,&s);assert(s.allow==7&&s.pmf_flags==3&&s.bytes==12);take(&q,&s);assert(s.allow==12&&!s.pmf_flags&&!memcmp(s.password,"PASSWORD5678",12));take(&q,&s);assert(!s.bytes);}
 /* 3. transition disabled (+584 bit 0, flags 0x40000100): SAE only */
 join(a,0x1000,0x40000100,8,0x80,6);a[584]=1;assert(!dec(a,&q));take(&q,&s);assert(s.allow==(NIWJ_ALLOW_SAE|NIWJ_SAE_REQUESTED));
 /* 4. WPA2-PSK in the bit-75 layout (kind 10 + PMK@472, MFPC in the element): SAE possible but not requested */
 join(a,8,0x40000004,2,0x80,6);assert(!dec(a,&q)&&q.pmk[31]==0x5a);take(&q,&s);assert(s.allow==(NIWJ_ALLOW_LEGACY|NIWJ_ALLOW_PMF|NIWJ_ALLOW_SAE)&&s.bytes==12);
 /* 5. PMF required by the AP (element MFPR): no legacy profile; PSK-SHA256 request */
 join(a,8,0x40000004,2,0xc0,6);assert(!dec(a,&q));take(&q,&s);assert(s.allow==(NIWJ_ALLOW_PMF|NIWJ_ALLOW_SAE));
 join(a,0x400,0x40000004,6,0x80,6);assert(!dec(a,&q));take(&q,&s);assert(s.allow==(NIWJ_ALLOW_LEGACY|NIWJ_ALLOW_PMF|NIWJ_ALLOW_SAE));
 /* 6. old layout (kind 6 PMK at +72, flags 4): no passphrase, so never SAE */
 join(a,8,4,2,0,0);a[60]=32;a[64]=6;a[468]=0;memset(a+72,0x11,32);assert(!dec(a,&q)&&q.pmk[0]==0x11);take(&q,&s);assert(s.allow==(NIWJ_ALLOW_LEGACY|NIWJ_ALLOW_PMF)&&!s.bytes);
 /* 7. refusals: SAE without passphrase, 802.1X shapes (upper 4 / 0x800 / 0x4000), bad cached length, unknown flag bit, short password */
 join(a,0x1000,0x100,8,0xc0,6);a[60]=32;a[64]=6;a[468]=0;assert(dec(a,&q)==NIWJ_SECURITY);
 join(a,4,0,1,0,0);a[56]=a[60]=a[64]=a[468]=0;assert(dec(a,&q)==NIWJ_PROFILE);join(a,0x800,0,5,0x80,6);assert(dec(a,&q)==NIWJ_PROFILE);join(a,0x4000,0,12,0xc0,12);assert(dec(a,&q)==NIWJ_PROFILE);
 join(a,0x1000,0x100,8,0xc0,6);a[468]=16;assert(dec(a,&q)==NIWJ_CREDENTIAL);join(a,0x1000,0x108,8,0xc0,6);assert(dec(a,&q)==NIWJ_OPTIONS);
 join(a,0x1000,0x100,8,0xc0,6);a[60]=7;assert(dec(a,&q)==NIWJ_CREDENTIAL);
 take(&q,&s);assert(!s.allow&&!s.bytes); /* a refusal leaves nothing pending */
 /* 8. kern.n2wifi_sae=0: WPA3-only refused, transition served as WPA2-PSK with the PMK, no secret recorded */
 niwj_sae_mode=0;join(a,0x1000,0x100,8,0xc0,6);assert(dec(a,&q)==NIWJ_SECURITY);
 join(a,0x1000,0x40000004,8,0x80,6);assert(!dec(a,&q)&&q.pmk[0]==0x5a);take(&q,&s);assert(!s.allow&&!s.bytes);
 join(a,8,0x40000004,2,0xc0,6);assert(dec(a,&q)==NIWJ_SECURITY);niwj_sae_mode=1;
 /* ---- selection and negotiation */
 scan.entered=scan.done=1;scan.completion.state=7;scan.sequence=scan.completion.sequence=3;scan.started=10;scan.last=20;scan.deadline=30;
 info.count=1;info.channels[0]=(struct ncs_channel){1,6};
 join(a,0x1000,0x40000004,8,0x80,6);assert(!dec(a,&q));
 size_t z=rsn(ie,k8,1,0xc0,6);bss(0,1,ie,z,1,1,-50);                                   /* WPA3-only, H2E-only AP */
 niwj_select_allow=NIWJ_ALLOW_LEGACY;assert(niwj_select(&q,&scan,&info,2000,&t)==NIWJ_TARGET);   /* legacy selection never takes it */
 niwj_select_allow=0;assert(!niwj_select(&q,&scan,&info,2000,&t)&&t.ap_rsn_bytes==z&&t.rate_count==4); /* allowed by the waiting record of this SSID */
 take(&q,&s);assert(s.allow==15);assert(niwj_select(&q,&scan,&info,2000,&t)==NIWJ_TARGET);            /* record consumed: legacy selection again */
 niwj_select_allow=7;assert(!niwj_select(&q,&scan,&info,2000,&t));
 assert(!nwq_negotiate(t.ap_rsn,t.ap_rsn_bytes,t.record.ies,t.record.ie_bytes,7,&f)&&f.akm==8&&f.pmf&&f.pmf_required&&f.h2e);
 assert(nwq_negotiate(t.ap_rsn,t.ap_rsn_bytes,t.record.ies,t.record.ie_bytes,3,&f)<0&&!f.akm);
 z=rsn(ie,k28,2,0x80,6);bss(0,1,ie,z,0,0,-50);                                          /* transition AP */
 niwj_select_allow=NIWJ_ALLOW_LEGACY;assert(!niwj_select(&q,&scan,&info,2000,&t));
 assert(!nwq_negotiate(t.ap_rsn,t.ap_rsn_bytes,t.record.ies,t.record.ie_bytes,7,&f)&&f.akm==8&&f.pmf&&!f.pmf_required&&!f.h2e);
 assert(!nwq_negotiate(t.ap_rsn,t.ap_rsn_bytes,t.record.ies,t.record.ie_bytes,3,&f)&&f.akm==2&&!f.pmf);   /* the pre-0432 profile */
 z=rsn(ie,k2,1,0xc0,6);bss(0,1,ie,z,0,0,-50);                                           /* WPA2-PSK, PMF required */
 niwj_select_allow=NIWJ_ALLOW_LEGACY;assert(niwj_select(&q,&scan,&info,2000,&t)==NIWJ_TARGET);
 niwj_select_allow=3;assert(!niwj_select(&q,&scan,&info,2000,&t));
 assert(!nwq_negotiate(t.ap_rsn,t.ap_rsn_bytes,t.record.ies,t.record.ie_bytes,3,&f)&&f.akm==2&&f.pmf&&f.pmf_required);
 z=rsn(ie,k6,1,0x80,6);bss(0,1,ie,z,0,0,-50);niwj_select_allow=3;assert(!niwj_select(&q,&scan,&info,2000,&t));          /* PSK-SHA256 only */
 assert(!nwq_negotiate(t.ap_rsn,t.ap_rsn_bytes,t.record.ies,t.record.ie_bytes,3,&f)&&f.akm==6&&f.pmf&&!f.pmf_required);
 z=rsn(ie,k26,2,0x80,6);bss(0,1,ie,z,0,0,-50);assert(!niwj_select(&q,&scan,&info,2000,&t));
 assert(!nwq_negotiate(t.ap_rsn,t.ap_rsn_bytes,t.record.ies,t.record.ie_bytes,3,&f)&&f.akm==2&&!f.pmf);                  /* mixed 2+6 stays on the reviewed path */
 z=rsn(ie,k8,1,0xc0,12);bss(0,1,ie,z,0,0,-50);niwj_select_allow=7;assert(niwj_select(&q,&scan,&info,2000,&t)==NIWJ_TARGET); /* BIP-GMAC-256 only: unsupported */
 {uint8_t o[28];size_t u;struct nwp_profile g={8,1,1,0};assert(!nwq_station_rsn(&g,o,&u)&&u==28&&o[1]==26&&o[19]==8&&o[20]==0xc0&&o[27]==6);g.pmf=g.pmf_required=0;g.akm=2;assert(!nwq_station_rsn(&g,o,&u)&&u==22&&o[19]==2&&!o[20]);}
 niwj_select_allow=0;
 /* ---- capability bits 75 (SAE) and 52 (MFP) */
 {static uint8_t carrier[NIW_SMALL_BYTES];uint8_t raw[13];size_t used=0;struct niw_request rq;unsigned found=0;
  const struct niwc_manifest m={2,{{NIWC_CCMP,NIWC_END_TO_END},{NIWC_RSN_IE,NIWC_END_TO_END}}},empty={0};
  {uint32_t get=NIW_GET;memcpy(carrier,&get,4);carrier[4]=13;memcpy(carrier+8+NIW_SMALL_PAYLOAD,"en0",3);carrier[8+NIW_SMALL_PAYLOAD+16]=12;
   found=!niw_decode(0,carrier,sizeof carrier,&rq)&&rq.request==12&&rq.payload_bytes==13&&!rq.value;assert(found);}
  if(found){
   niwc_wpa3=0;assert(!niwc_card_reply(carrier,sizeof carrier,&m,raw,sizeof raw,&used)&&used==13&&raw[0]==8&&raw[1]==0x40&&!raw[6]&&!raw[9]);
   niwc_wpa3=1;assert(!niwc_card_reply(carrier,sizeof carrier,&m,raw,sizeof raw,&used)&&raw[0]==8&&raw[1]==0x40&&raw[6]==0x10&&raw[9]==0x08);
   assert(!niwc_card_reply(carrier,sizeof carrier,&empty,raw,sizeof raw,&used)&&!raw[6]&&!raw[9]);
   puts("PASS 0434 card capabilities: bits 3/14 unchanged, bits 52+75 only with kern.n2wifi_sae and the CCMP+RSN inventory");
  }
 }
 puts("PASS 0434 decoder/selection: SAE WPA3-only/transition/transition-disabled, bit-75 WPA2 layout, PSK-SHA256, MFPR, refusals, n2wifi_sae=0, secret side channel, SAE-only/H2E-only/PMF-required/mixed APs, negotiation");
 return 0;
}
