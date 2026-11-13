/* 0450 host test: 802.1X profile decode (the 592-byte ASSOCIATE of autocycle/runs/wifi1/live.log line 8029) and AP selection.
 * cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I<candidate> test.c wifi_ios_join.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_ios_join.h"
#include "wifi_8021x.h"
static const uint8_t owner[6]={0x00,0x0c,0xe7,0x10,0x20,0x30},laa[6]={0x06,0xaa,0xbb,0xcc,0xdd,0xee},zero[32]={0};
static void rsn(uint8_t*p,unsigned akm,unsigned caps){n8x_station_rsn(akm,p);p[20]=(uint8_t)caps;p[21]=(uint8_t)(caps>>8);}
/* auth_upper=4 key_kind=0 key_bytes=0 rsn_bytes=22 assoc_flags=0x4808 private_present=1 scope_bytes=36 */
static void live(uint8_t a[592],unsigned upper,unsigned akm){
 memset(a,0,592);a[0]=1;a[4]=2;a[6]=1;a[8]=(uint8_t)upper;a[9]=(uint8_t)(upper>>8);a[12]=7;memcpy(a+16,"eduroam",7);
 a[204]=22;rsn(a+206,akm,0);a[463]=0x08;a[464]=0x48;memcpy(a+508,laa,6);a[536]=36;memset(a+540,'s',36);
}
static int dec(const uint8_t a[592],struct niwj_request*q,struct niwj_diagnostic*d){return niwj_decode_identity(a,592,owner,q,d);}
static struct nsb_scan scan;static struct nss_info info;
/* One beacon of the wanted SSID with this RSN element. */
static void ap(const uint8_t*element,unsigned bytes){
 memset(&scan,0,sizeof scan);memset(&info,0,sizeof info);
 scan.entered=scan.done=1;scan.count=scan.frames=1;scan.sequence=3;scan.started=1000;scan.last=2000;scan.deadline=9000;scan.completion.state=7;scan.completion.sequence=3;
 struct nsb_record*r=&scan.records[0];static const uint8_t bssid[6]={0x00,0x11,0x22,0x33,0x44,0x55};
 memcpy(r->bssid,bssid,6);memcpy(r->transmitter,bssid,6);memcpy(r->ssid,"eduroam",7);r->ssid_len=7;r->ssid_present=1;r->subtype=8;r->hw_channel=r->channel=r->ds_channel=6;
 r->group_valid=4;r->rxv_valid=1;r->rxv[4]=120;r->rxv[5]=255;r->rsn_present=1;r->privacy=1;r->interval=100;r->capability=0x11;r->observed_us=1500;
 unsigned n=0;r->ies[n++]=1;r->ies[n++]=2;r->ies[n++]=0x82;r->ies[n++]=0x0c;memcpy(r->ies+n,element,bytes);n+=bytes;r->ie_bytes=r->ie_total=(uint16_t)n;
 info.count=1;info.channels[0].band=1;info.channels[0].channel=6;
}
int main(void){
 uint8_t a[592],self[6],want[22],e[64];struct niwj_request q;struct niwj_diagnostic d;struct niwj_target t;
 niwj_privmac_mode=1;
 /* 1. The request of the log: refused as a profile with the switch off (the behaviour before 0450), accepted with it on. */
 niwj_8021x=0;live(a,4,1);assert(dec(a,&q,&d)==NIWJ_PROFILE&&d.reason==2&&d.auth_upper==4&&d.rsn_bytes==22&&!d.key_bytes&&d.association_flags==0x4808&&!memcmp(&q,zero,sizeof zero));
 niwj_8021x=1;assert(!dec(a,&q,&d)&&!d.reason&&q.security==NIWJ_WPA2_8021X&&niwj_eap(q.security)&&q.ssid_length==7&&!memcmp(q.pmk,zero,32));
 n8x_station_rsn(1,want);assert(!memcmp(q.station_rsn,want,22)&&n8x_rsn_akm(q.station_rsn)==1&&niwj_take_self(&q,self)==1&&!memcmp(self,laa,6));
 /* 2. SHA-256 802.1X: the framework element decides, also when both auth bits are set; no element: the bits decide. */
 live(a,0x80,5);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_8021X_SHA256&&n8x_rsn_akm(q.station_rsn)==5);
 live(a,0x84,5);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_8021X_SHA256);
 live(a,4,1);a[204]=0;memset(a+206,0,22);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_8021X);
 live(a,0x80,1);a[204]=0;memset(a+206,0,22);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_8021X_SHA256);
 live(a,5,1);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_8021X); /* WPA/WPA2 mixed enterprise: the RSN side */
 /* an element naming both AKMs: 802.1X preferred */
 live(a,4,1);{uint8_t two[26]={48,24,1,0,0,15,172,4,1,0,0,15,172,4,2,0,0,15,172,5,0,15,172,1,0,0};a[204]=26;memcpy(a+206,two,26);assert(!dec(a,&q,&d)&&q.security==NIWJ_WPA2_8021X&&!memcmp(q.station_rsn,want,22));}
 /* 3. Refusals: WPA1 only (TKIP), a key in an 802.1X request, MFP required, TKIP group, PSK element, PSK bit without key. */
 live(a,1,1);assert(dec(a,&q,&d)==NIWJ_PROFILE);
 live(a,0x200,1);assert(dec(a,&q,&d)==NIWJ_PROFILE);live(a,0x804,1);assert(dec(a,&q,&d)==NIWJ_PROFILE);
 live(a,4,1);a[60]=32;assert(dec(a,&q,&d)==NIWJ_CREDENTIAL&&!memcmp(&q,zero,sizeof zero));
 live(a,4,1);a[226]=0x40;assert(dec(a,&q,&d)==NIWJ_SECURITY);live(a,4,1);a[226]=0x80;assert(!dec(a,&q,&d)); /* MFPC alone is fine */
 live(a,4,1);a[213]=2;assert(dec(a,&q,&d)==NIWJ_SECURITY);
 live(a,4,2);assert(dec(a,&q,&d)==NIWJ_SECURITY);
 live(a,12,1);assert(dec(a,&q,&d)==NIWJ_CREDENTIAL);
 live(a,4,1);a[463]|=0x20;assert(dec(a,&q,&d)==NIWJ_OPTIONS); /* options keep their checks */
 /* 4. Selection: the AP must offer the AKM of the request, with CCMP, without requiring MFP. */
 live(a,4,1);assert(!dec(a,&q,&d));
 rsn(e,1,0x000c);ap(e,22);assert(!niwj_select(&q,&scan,&info,3000,&t)&&t.security==NIWJ_WPA2_8021X&&t.ap_rsn_bytes==22&&t.channel==6&&!niwj_ap_mfpr);
 rsn(e,2,0);ap(e,22);assert(niwj_select(&q,&scan,&info,3000,&t)==NIWJ_TARGET);
 rsn(e,5,0);ap(e,22);assert(niwj_select(&q,&scan,&info,3000,&t)==NIWJ_TARGET&&!niwj_ap_mfpr);
 rsn(e,1,0x00c0);ap(e,22);assert(niwj_select(&q,&scan,&info,3000,&t)==NIWJ_TARGET&&niwj_ap_mfpr==1); /* MFPR */
 rsn(e,1,0x0080);ap(e,22);assert(!niwj_select(&q,&scan,&info,3000,&t)); /* MFPC */
 {uint8_t two[26]={48,24,1,0,0,15,172,4,1,0,0,15,172,4,2,0,0,15,172,5,0,15,172,1,0x80,0};ap(two,26);assert(!niwj_select(&q,&scan,&info,3000,&t)&&t.ap_rsn_bytes==26);
  live(a,0x80,5);struct niwj_request q5;assert(!dec(a,&q5,&d)&&!niwj_select(&q5,&scan,&info,3000,&t)&&t.security==NIWJ_WPA2_8021X_SHA256);
  rsn(e,1,0);ap(e,22);assert(niwj_select(&q5,&scan,&info,3000,&t)==NIWJ_TARGET);}
 /* a request whose RSN and security disagree, or with a PMK, never selects */
 {struct niwj_request bad=q;bad.security=NIWJ_WPA2_8021X_SHA256;rsn(e,1,0);ap(e,22);assert(niwj_select(&bad,&scan,&info,3000,&t)==NIWJ_TARGET);bad=q;bad.pmk[5]=1;assert(niwj_select(&bad,&scan,&info,3000,&t)==NIWJ_TARGET);bad=q;bad.security=5;assert(niwj_select(&bad,&scan,&info,3000,&t)==NIWJ_TARGET);}
 /* 5. WPA2-PSK is untouched: PSK request, PSK AP; an 802.1X-only AP is not a PSK candidate. */
 {uint8_t b[592];memset(b,0,592);b[0]=1;b[4]=2;b[6]=1;b[8]=8;b[12]=7;memcpy(b+16,"eduroam",7);b[56]=1;b[60]=32;b[64]=6;memset(b+72,7,32);b[204]=22;rsn(b+206,2,0);struct niwj_request p;
  assert(!dec(b,&p,&d)&&p.security==NIWJ_WPA2_PSK_CCMP&&p.station_rsn[19]==2);rsn(e,2,0);ap(e,22);assert(!niwj_select(&p,&scan,&info,3000,&t)&&t.security==NIWJ_WPA2_PSK_CCMP);rsn(e,1,0);ap(e,22);assert(niwj_select(&p,&scan,&info,3000,&t)==NIWJ_TARGET);}
 puts("PASS 0450 decoder: live.log 802.1X request refused with kern.n2wifi_8021x=0 and decoded with 1 (AKM 1 / AKM 5, private address, no key), refusals (WPA1, key, MFPR, TKIP, PSK element), AP selection by AKM, MFPR counted, PSK unchanged");return 0;}
