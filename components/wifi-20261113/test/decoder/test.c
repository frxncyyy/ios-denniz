/* 0430+0436 host test: relaxed IOUC join decoder + relaxed scan admission (pure C, ASan/UBSan). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_ios_join.h"
#include "wifi_ios_policy.h"
static const uint8_t rsn22[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
static const uint8_t owner[6]={0x00,0x0c,0xe7,0x10,0x20,0x30},laa[6]={0x06,0xaa,0xbb,0xcc,0xdd,0xee};
static unsigned hook_calls;
static int hook(const uint8_t*pass,size_t n,const uint8_t*ssid,size_t z,uint8_t out[32]){hook_calls++;if(n<8||n>63||!z||z>32)return -1;for(unsigned i=0;i<32;i++)out[i]=(uint8_t)(pass[i%n]^ssid[i%z]^i);return 0;}
static void base(uint8_t a[592],unsigned upper){memset(a,0,592);a[0]=1;a[4]=2;a[6]=1;a[8]=(uint8_t)upper;a[9]=(uint8_t)(upper>>8);a[12]=4;memcpy(a+16,"home",4);a[464]=0x40;a[536]=3;memcpy(a+540,"abc",3);}
static void psk(uint8_t a[592],unsigned upper){base(a,upper);a[56]=1;a[60]=32;a[64]=6;for(unsigned i=0;i<32;i++)a[72+i]=(uint8_t)(i+1);a[204]=22;memcpy(a+206,rsn22,22);}
static int dec(const uint8_t a[592],struct niwj_request*q,struct niwj_diagnostic*d){return niwj_decode_identity(a,592,owner,q,d);}
int main(void){
 uint8_t a[592],self[6];struct niwj_request q;struct niwj_diagnostic d;
 niwj_pmk_hook=hook;
 /* 1. WPA2-PSK with PMK, no private address: accepted in both modes. */
 for(unsigned mode=0;mode<=1;mode++){niwj_privmac_mode=mode;psk(a,8);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_PSK_CCMP&&q.ssid_length==4&&q.pmk[0]==1&&!memcmp(q.station_rsn,rsn22,22));}
 /* 2. Private address different from the station address (JOINIDENT private_present=1 same_identity=0):
  *    legacy refuses with reason 5; relaxed accepts the join (the address itself is ignored by this image). */
 psk(a,8);a[464]|=8;memcpy(a+508,laa,6);
 niwj_privmac_mode=0;assert(dec(a,&q,&d)==NIWJ_OPTIONS&&d.reason==5&&d.private_address_present&&!d.private_address_matches);
 for(unsigned mode=1;mode<=2;mode++){niwj_privmac_mode=mode;assert(!dec(a,&q,&d)&&!d.reason&&d.private_address_present&&!d.private_address_matches&&q.security==NIWJ_WPA2_PSK_CCMP);
  assert(niwj_take_self(&q,self)==1&&!memcmp(self,laa,6)&&!niwj_take_self(&q,self));}                       /* handed over exactly once */
 niwj_privmac_mode=3;assert(!dec(a,&q,&d)&&!niwj_take_self(&q,self));                                          /* kill switch: accepted, never handed over */
 niwj_privmac_mode=1;
 assert(!dec(a,&q,&d));{struct niwj_request other=q;other.ssid[0]^=1;assert(!niwj_take_self(&other,self)&&!niwj_take_self(&q,self));}   /* belongs to its SSID */
 assert(!dec(a,&q,&d));{uint8_t b[592];psk(b,8);struct niwj_request q2;assert(!dec(b,&q2,&d)&&!niwj_take_self(&q,self));}              /* any later decode drops it */
 /* multicast, universally administered or zero private address: refused */
 a[508]=0x07;assert(dec(a,&q,&d)==NIWJ_OPTIONS);a[508]=0x04;assert(dec(a,&q,&d)==NIWJ_OPTIONS);memset(a+508,0,6);assert(dec(a,&q,&d)==NIWJ_OPTIONS);
 /* 3. OPEN with a private address (items 2+4). */
 base(a,0);a[464]|=8;memcpy(a+508,laa,6);assert(!dec(a,&q,&d)&&q.security==NIWJ_OPEN&&niwj_take_self(&q,self)==1);
 a[60]=5;assert(dec(a,&q,&d)==NIWJ_CREDENTIAL);a[60]=0;a[204]=22;assert(dec(a,&q,&d)==NIWJ_SECURITY);
 /* 4. The request of wifi1/live.log: auth_upper 4, no key, RSN 22, flags 0x4808 -> profile refusal (reason 2). */
 base(a,4);a[204]=22;memcpy(a+206,rsn22,22);a[463]=0x08;a[464]=0x48;memcpy(a+508,laa,6);assert(dec(a,&q,&d)==NIWJ_PROFILE&&d.reason==2&&d.auth_upper==4&&!d.key_bytes&&d.association_flags==0x4808);
 psk(a,0x200);assert(dec(a,&q,&d)==NIWJ_PROFILE);psk(a,0x208);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_PSK_CCMP);
 /* 5. Passphrase key (kind 10): PMK through the hook; too short/long refused; without hook refused. */
 psk(a,8);a[64]=10;a[60]=10;memcpy(a+72,"correcthor",10);hook_calls=0;assert(!dec(a,&q,&d)&&hook_calls==1&&q.pmk[0]==(uint8_t)('c'^'h'));
 a[60]=7;assert(dec(a,&q,&d)==NIWJ_CREDENTIAL);a[60]=64;assert(dec(a,&q,&d)==NIWJ_CREDENTIAL);a[60]=10;niwj_pmk_hook=0;assert(dec(a,&q,&d)==NIWJ_CREDENTIAL);niwj_pmk_hook=hook;
 /* 6. Framework RSN with PSK+SAE and MFP capable: accepted (our own 22-byte element is used); MFP required or no PSK: refused. */
 {static const uint8_t t[]={48,24,1,0,0,15,172,4,1,0,0,15,172,4,2,0,0,15,172,2,0,15,172,8,0x80,0};psk(a,8);a[204]=sizeof t;memset(a+206,0,64);memcpy(a+206,t,sizeof t);assert(!dec(a,&q,&d)&&!memcmp(q.station_rsn,rsn22,22));
  a[206+24]=0xc0;assert(dec(a,&q,&d)==NIWJ_SECURITY);a[206+24]=0x80;a[206+19]=8;assert(dec(a,&q,&d)==NIWJ_SECURITY);a[206+19]=2;a[206+1]=30;assert(dec(a,&q,&d)==NIWJ_SECURITY);}
 /* 7. Unknown flag bits tolerated, transition-disable refuses PSK, band preference ignored, blacklist refused. */
 psk(a,8);a[463]=0x08;a[464]|=0x10;a[588]=1;assert(!dec(a,&q,&d));a[584]=1;assert(dec(a,&q,&d)==NIWJ_SECURITY);a[584]=0;a[514]=1;assert(dec(a,&q,&d)==NIWJ_OPTIONS);
 niwj_privmac_mode=0;psk(a,8);a[463]=0x08;assert(dec(a,&q,&d)==NIWJ_OPTIONS);psk(a,8);a[588]=1;assert(dec(a,&q,&d)==NIWJ_OPTIONS);niwj_privmac_mode=1;
 psk(a,8);a[463]=0x08;assert(niwj_decode(a,592,&q,&d)==NIWJ_OPTIONS); /* pure entry (no owner) stays legacy */
 /* ---- scan admission */
 static uint8_t s[5456];struct nss_info info={0};struct nss_request out;info.count=3;info.channels[0]=(struct ncs_channel){1,1};info.channels[1]=(struct ncs_channel){1,6};info.channels[2]=(struct ncs_channel){2,36};info.band5=1;
 memset(s,0,sizeof s);s[0]=1;s[4]=2;s[52]=1;s[56]=1;s[68]=1;s[72]=1;s[76]=6;s[80]=10;s[4883]=0x80;
 /* plain request (the SCANREQ shape of the logs: scan_type 1, flags 32): identical result with and without relax */
 s[4872]=32;for(unsigned relax=0;relax<=1;relax++){niwi_scan_relax=relax;memset(&out,0xa5,sizeof out);assert(!niwi_scan_admit(s,sizeof s,&info,&out)&&out.count==1&&out.channels[0].band==1&&out.channels[0].channel==6&&!out.dwell_tu);}
 s[68]=0;assert(!niwi_scan_admit(s,sizeof s,&info,&out)&&out.count==0&&!out.dwell_tu);s[68]=1;s[4872]=0;
 s[16]=4;memcpy(s+20,"home",4);                               /* directed SSID */
 niwi_scan_relax=0;assert(niwi_scan_admit(s,sizeof s,&info,&out)==NIWI_ADMIT_OPTION);
 niwi_scan_relax=1;assert(!niwi_scan_admit(s,sizeof s,&info,&out)&&out.count==1&&out.channels[0].band==1&&out.channels[0].channel==6);
 s[16]=33;assert(niwi_scan_admit(s,sizeof s,&info,&out)==NIWI_ADMIT_OPTION);s[16]=4;
 s[60]=40;assert(!niwi_scan_admit(s,sizeof s,&info,&out)&&out.dwell_tu==108);s[60]=0;
 s[4884]=2;s[4872]=0x22;s[8]=0x10;s[9]=1;assert(!niwi_scan_admit(s,sizeof s,&info,&out));
 niwi_scan_relax=0;assert(niwi_scan_admit(s,sizeof s,&info,&out)<0);niwi_scan_relax=1;
 s[4872]=0x40;assert(niwi_scan_admit(s,sizeof s,&info,&out)==NIWI_ADMIT_FLAGS);s[4872]=0;
 s[76]=11;assert(niwi_scan_admit(s,sizeof s,&info,&out)==NIWI_ADMIT_ALLOWLIST);
 puts("PASS 0430+0436 decoder: private address handed over once in modes 1/2, ignored in 3, refused in 0, OPEN, PMK/passphrase PSK, wifi1 802.1X-shaped request refused, lenient station RSN, flags; scan admission: plain request unchanged, directed/list/dwell/hints served, legacy refusals with relax=0");
 return 0;
}
