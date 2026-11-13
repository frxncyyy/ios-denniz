#!/usr/bin/env python3
"""0450 edits on work/b (copy of the q10jgw stage): engine, transaction, wire, join decoder, station view."""
from pathlib import Path
B=Path(__file__).resolve().parent/'b/bsd/dev'
def sub(name,old,new,count=1):
    f=B/name;t=f.read_text();assert t.count(old)==count,(name,old[:80],t.count(old));f.write_text(t.replace(old,new))
# ---------------- wifi_wpa2: AKM 1 / AKM 5 (key descriptor version 3)
sub('wifi_wpa2.h','int nwp_ap_rsn(const uint8_t*,size_t);','''int nwp_ap_rsn(const uint8_t*,size_t);
/* 0450: the AP element must offer CCMP and this AKM (00-0F-AC:2 PSK, :1 802.1X,
 * :5 802.1X SHA-256). The station element selects the AKM: byte 19 of rsn[22]. */
int nwp_ap_rsn_akm(const uint8_t*,size_t,unsigned akm);''')
sub('wifi_wpa2.c','#include "wifi_wpa_crypto.h"\n','#include "wifi_wpa_crypto.h"\n#include "wifi_8021x.h"\n')
sub('wifi_wpa2.c',''' return equal(p,rsn,sizeof rsn); /* exactly one CCMP pairwise/group, PSK; no PMF */
}''',''' if(equal(p,rsn,sizeof rsn))return 1; /* exactly one CCMP pairwise/group, PSK; no PMF */
 return n8x_eap_akm(n8x_rsn_akm(p)); /* 0450: the same element with AKM 802.1X or 802.1X SHA-256 */
}
/* 0450: AKM of this handshake and its EAPOL-Key descriptor version (2 HMAC-SHA1, 3 AES-128-CMAC). */
static unsigned akm_of(const struct nwp*w){return w->rsn[19];}
static unsigned version_of(const struct nwp*w){return akm_of(w)==N8X_AKM_8021X_SHA256?3u:2u;}''')
sub('wifi_wpa2.c','''int nwp_ap_rsn(const uint8_t*p,size_t n){
 if(!p||n<20''','''int nwp_ap_rsn(const uint8_t*p,size_t n){return nwp_ap_rsn_akm(p,n,N8X_AKM_PSK);}
int nwp_ap_rsn_akm(const uint8_t*p,size_t n,unsigned akm){
 if(!p||n<20''')
sub('wifi_wpa2.c',' for(unsigned i=0;i<count;i++,at+=4)psk|=suite(p+at,2);\n if(!ccmp||!psk)return -1;',' for(unsigned i=0;i<count;i++,at+=4)psk|=suite(p+at,akm);\n if(!ccmp||!psk)return -1;')
sub('wifi_wpa2.c','!rsn_valid(rsn)||nwp_ap_rsn(ap,ap_bytes)||','!rsn_valid(rsn)||nwp_ap_rsn_akm(ap,ap_bytes,rsn[19])||')
sub('wifi_wpa2.c',''' prf(w->pmk,32,"Pairwise key expansion",data,76,keys,48);''',''' if(akm_of(w)==N8X_AKM_8021X_SHA256)(void)n8x_ptk_sha256(w->pmk,data,keys);
 else prf(w->pmk,32,"Pairwise key expansion",data,76,keys,48);''')
sub('wifi_wpa2.c',''' const uint8_t z[16]={0},*pieces[3]={p,z,p+97};unsigned sizes[3]={81,16,n-97};uint8_t d[20];
''',''' const uint8_t z[16]={0},*pieces[3]={p,z,p+97};unsigned sizes[3]={81,16,n-97};uint8_t d[20];
 if(version_of(w)==3){size_t wide[3]={81,16,n-97};(void)n8x_cmac(w->kck,pieces,wide,3,out);return;}
''')
sub('wifi_wpa2.c','put16(r->frame+5,kind==1?0x010a:kind==4?0x0302:0x030a);','put16(r->frame+5,(kind==1?0x0108u:kind==4?0x0300u:0x0308u)|version_of(w));')
sub('wifi_wpa2.c',''' unsigned info=be16(p+5);
 if(info==0x1382)return group_receive(w,p,(unsigned)n,r);''',''' unsigned info=be16(p+5),version=version_of(w);
 if(info==(0x1380u|version))return group_receive(w,p,(unsigned)n,r);''')
sub('wifi_wpa2.c',' if(info==0x008a){',' if(info==(0x0088u|version)){')
sub('wifi_wpa2.c',' if(info!=0x13ca||w->state<NWP_M3)return failed(w,r,7);',' if(info!=(0x13c8u|version)||w->state<NWP_M3)return failed(w,r,7);')
# ---------------- wifi_station_wire: the association request carries the RSN of this attempt
sub('wifi_station_wire.h','int sw_assoc(uint8_t *, size_t, size_t *, const struct sw_assoc *);','''int sw_assoc(uint8_t *, size_t, size_t *, const struct sw_assoc *);
/* 0450: 22-byte station RSN element of the running attempt when it is not the
 * WPA2-PSK one (802.1X AKM); 0 = PSK. Set and cleared by stc_begin only. */
extern const uint8_t *sw_assoc_rsn;''')
sub('wifi_station_wire.c','int sw_assoc(uint8_t *out,size_t cap,size_t *used,const struct sw_assoc *p) {','const uint8_t *sw_assoc_rsn;\nint sw_assoc(uint8_t *out,size_t cap,size_t *used,const struct sw_assoc *p) {')
sub('wifi_station_wire.c','if(p->security!=SW_SECURITY_OPEN){memcpy(b+n,rsn,sizeof(rsn));n+=sizeof(rsn);}','if(p->security!=SW_SECURITY_OPEN){memcpy(b+n,sw_assoc_rsn?sw_assoc_rsn:rsn,sizeof(rsn));n+=sizeof(rsn);}')
# ---------------- wifi_station_transaction: the engine starts when the PMK arrives
sub('wifi_station_transaction.h','int stc_tick(struct stc*,uint64_t now_ms);','''int stc_tick(struct stc*,uint64_t now_ms);
/* 0450 (802.1X): stc_begin with pmk==0 and a station RSN whose AKM is 802.1X
 * associates without starting the WPA engine; EAPOL-Key frames wait (newest
 * kept) until the PMK of the EAP exchange is supplied here in phase STC_EAPOL.
 * 1 engine started (a waiting message 1 is answered), 0 not applicable now
 * (keep the PMK and retry), <0 the transaction stopped. stc_eap says whether
 * this generation is such an attempt, stc_eap_waiting whether it still waits. */
int stc_pmk(struct stc*,uint64_t generation,const uint8_t pmk[32],uint64_t now_ms);
int stc_eap(const struct stc*);
int stc_eap_waiting(const struct stc*);''')
sub('wifi_station_transaction.c','#include "wifi_wpa_diag.h"\n','#include "wifi_wpa_diag.h"\n#include "wifi_8021x.h"\n')
sub('wifi_station_transaction.c','static unsigned le16(const uint8_t*p){return p[0]|(unsigned)p[1]<<8;}\n','''static unsigned le16(const uint8_t*p){return p[0]|(unsigned)p[1]<<8;}
/* 0450: inputs of the deferred engine start of the one 802.1X attempt. No
 * secret: addresses are in every frame, the elements are public. struct stc
 * keeps its layout (wifi_wpa_owner.o / wifi_wpa_nst.o are not rebuilt). */
static struct {uint64_t generation;unsigned started,ap_rsn_bytes,ap_rsnxe_bytes;uint8_t own[6],peer[6],rsn[22],ap_rsn[257],ap_rsnxe[257];nwp_rng rng;void*rng_context;} eap;
static void eap_clear(void){erase(&eap,sizeof eap);sw_assoc_rsn=0;}
static int eap_save(const struct stc_config*c){
 size_t n=c->ap_rsn_bytes?c->ap_rsn_bytes:22;
 if(!c->station_rsn||!c->ap_rsn||!c->rng||n>sizeof eap.ap_rsn||nwp_ap_rsn_akm(c->ap_rsn,n,c->station_rsn[19])||c->ap_ies_bytes>1024||(!c->ap_ies&&c->ap_ies_bytes))return -1;
 for(size_t at=0;at<c->ap_ies_bytes;){ /* the one RSNXE (244) of the captured elements, as nwp_copy_ap_rsnxe selects it */
  if(c->ap_ies_bytes-at<2||c->ap_ies[at+1]>c->ap_ies_bytes-at-2)return -1;
  size_t bytes=c->ap_ies[at+1]+2u;
  if(c->ap_ies[at]==244){if(eap.ap_rsnxe_bytes)return -1;memcpy(eap.ap_rsnxe,c->ap_ies+at,bytes);eap.ap_rsnxe_bytes=(unsigned)bytes;}
  at+=bytes;
 }
 eap.generation=c->generation;memcpy(eap.own,c->assoc.peer.self,6);memcpy(eap.peer,c->assoc.peer.ap,6);memcpy(eap.rsn,c->station_rsn,22);memcpy(eap.ap_rsn,c->ap_rsn,n);eap.ap_rsn_bytes=(unsigned)n;eap.rng=c->rng;eap.rng_context=c->rng_context;
 sw_assoc_rsn=eap.rsn;return 0;
}
int stc_eap(const struct stc*s){return s&&s->attempted&&s->generation&&eap.generation==s->generation;}
int stc_eap_waiting(const struct stc*s){return stc_eap(s)&&!eap.started&&s->phase>=STC_PREPARE&&s->phase<STC_DEAUTH;}
''')
sub('wifi_station_transaction.c',''' memset(s,0,sizeof *s);s->attempted=1;s->generation=c->generation;s->last_ms=now;s->deadline=c->deadline_ms;s->cleanup_timeout_ms=c->cleanup_timeout_ms;
 s->security=c->security;
 if(c->security==SW_SECURITY_OPEN){if(c->pmk||c->station_rsn||c->ap_rsn||c->ap_rsn_bytes||c->rng){erase(s,sizeof *s);s->attempted=1;return -1;}}
 else if(''',''' memset(s,0,sizeof *s);s->attempted=1;s->generation=c->generation;s->last_ms=now;s->deadline=c->deadline_ms;s->cleanup_timeout_ms=c->cleanup_timeout_ms;
 s->security=c->security;eap_clear();
 if(c->security==SW_SECURITY_OPEN){if(c->pmk||c->station_rsn||c->ap_rsn||c->ap_rsn_bytes||c->rng){erase(s,sizeof *s);s->attempted=1;return -1;}}
 else if(!c->pmk&&c->station_rsn&&n8x_eap_akm(n8x_rsn_akm(c->station_rsn))){if(eap_save(c)){eap_clear();erase(s,sizeof *s);s->attempted=1;return -1;}} /* 0450: 802.1X, engine starts in stc_pmk */
 else if(''')
sub('wifi_station_transaction.c',''' unsigned resume=s->phase,security_state=s->wpa.state;struct nwp_result r;int result=nwp_receive(&s->wpa,p,n,&r);''',''' if(stc_eap_waiting(s)){memcpy(s->queued_eapol,p,n);s->eapol_bytes=(unsigned)n;s->eapol_pending=1;return 1;} /* 0450: EAPOL-Key before the PMK, newest kept */
 unsigned resume=s->phase,security_state=s->wpa.state;struct nwp_result r;int result=nwp_receive(&s->wpa,p,n,&r);''')
sub('wifi_station_transaction.c','''int stc_channel_released(struct stc*s,uint64_t gen,uint8_t token,uint64_t now){''','''int stc_pmk(struct stc*s,uint64_t gen,const uint8_t pmk[32],uint64_t now){
 if(!pmk||!current(s,gen,now)||!stc_eap_waiting(s)||s->phase!=STC_EAPOL||s->wpa.state!=NWP_OFF)return 0;
 eap.started=1;
 if(nwp_begin_ap(&s->wpa,gen,pmk,eap.own,eap.peer,eap.rsn,eap.ap_rsn,eap.ap_rsn_bytes,eap.rng,eap.rng_context)||nwp_copy_ap_rsnxe(&s->wpa,eap.ap_rsnxe,eap.ap_rsnxe_bytes))return stop(s,STC_FAILED);
 int r=consume_eapol(s,now);return r<0?r:1;
}
int stc_channel_released(struct stc*s,uint64_t gen,uint8_t token,uint64_t now){''')
# ---------------- wifi_ios_join: 802.1X profile decode and AP selection
sub('wifi_ios_join.h','#define NIWJ_OPEN 2u\n','''#define NIWJ_OPEN 2u
/* 0450: WPA2-Enterprise, CCMP, AKM 00-0F-AC:1 / :5 (SHA-256). Like WPA2-PSK
 * for association, keys and data; the PMK comes later from eapolclient. */
#define NIWJ_WPA2_8021X 3u
#define NIWJ_WPA2_8021X_SHA256 4u
#define NIWJ_SECURITY_MAX NIWJ_WPA2_8021X_SHA256
static inline int niwj_eap(unsigned security){return security==NIWJ_WPA2_8021X||security==NIWJ_WPA2_8021X_SHA256;}
''')
sub('wifi_ios_join.h','extern unsigned niwj_privmac_mode;\n','''extern unsigned niwj_privmac_mode;
/* 0450, kern.n2wifi_8021x (default 1): 0 refuses 802.1X profiles as before
 * (NIWJ_PROFILE), 1 decodes auth_upper WPA2 (4) / SHA-256 802.1X (0x80)
 * without a key into NIWJ_WPA2_8021X[_SHA256]: station RSN with that AKM and
 * a zero PMK. niwj_ap_mfpr counts scanned APs of the wanted SSID refused only
 * because they require management frame protection. */
extern unsigned niwj_8021x,niwj_ap_mfpr;
''')
sub('wifi_ios_join.c','#include "wifi_ios_join.h"\n','#include "wifi_ios_join.h"\n#include "wifi_8021x.h"\n')
sub('wifi_ios_join.c','unsigned niwj_privmac_mode=1;\n','unsigned niwj_privmac_mode=1;\nunsigned niwj_8021x=1,niwj_ap_mfpr;\n')
sub('wifi_ios_join.c','''/* class: 1 open,''','''/* 0450: AKM for an 802.1X request. The framework element (when present) must
 * be RSN v1 with CCMP group and pairwise and name 802.1X (preferred) or
 * 802.1X SHA-256, without requiring MFP. No element: WPA2 bit -> AKM 1,
 * SHA-256 bit alone -> AKM 5. 0 = not a profile this driver can join. */
static unsigned eap_akm(const uint8_t*p,unsigned n,unsigned upper){
 unsigned at=8,count,ccmp=0,plain=0,sha256=0;
 if(!n)return (upper&4u)?N8X_AKM_8021X:N8X_AKM_8021X_SHA256;
 if(n<20||n>64||p[0]!=48||p[1]!=n-2||j16(p+2)!=1||!suite0(p+4,4))return 0;
 count=j16(p+at);at+=2;if(!count||count>(n-at)/4)return 0;
 while(count--){ccmp|=(unsigned)suite0(p+at,4);at+=4;}
 if(!ccmp||n-at<2)return 0;
 count=j16(p+at);at+=2;if(!count||count>(n-at)/4)return 0;
 while(count--){plain|=(unsigned)suite0(p+at,1);sha256|=(unsigned)suite0(p+at,5);at+=4;}
 if(n-at>=2&&(j16(p+at)&64u))return 0;
 return plain?N8X_AKM_8021X:sha256?N8X_AKM_8021X_SHA256:0;
}
/* class: 1 open,''')
sub('wifi_ios_join.c',''' *klass=open?1:(upper&8u)?2:(upper&0x125u)?4:5;*ignored=0;
 if(j32(p)!=1||!j32(p+12)||j32(p+12)>32||(nz(p+48,6)&&(p[48]&1)))return NIWJ_SHAPE;
 if(j16(p+4)!=2||j16(p+6)!=1||(!open&&!(upper&8u)))return NIWJ_PROFILE;
 if(open){''',''' *klass=open?1:(upper&8u)?2:(upper&0x125u)?4:5;*ignored=0;
 if(j32(p)!=1||!j32(p+12)||j32(p+12)>32||(nz(p+48,6)&&(p[48]&1)))return NIWJ_SHAPE;
 /* 0450: WPA2 (4) and/or SHA-256 802.1X (0x80), optionally with the WPA1 bit: no key in the request. */
 unsigned eap=niwj_8021x&&!(upper&8u)&&(upper&0x84u)&&!(upper&~0x85u);
 if(j16(p+4)!=2||j16(p+6)!=1||(!open&&!(upper&8u)&&!eap))return NIWJ_PROFILE;
 if(eap){
  if(j32(p+60)||j16(p+68)||j16(p+70))return NIWJ_CREDENTIAL;
  eap=eap_akm(p+206,j16(p+204),upper);if(!eap)return NIWJ_SECURITY;
  *klass=4;
 }else if(open){''')
sub('wifi_ios_join.c',''' if(!open){
  if(pass){if(niwj_pmk_hook(p+72,j32(p+60),p+16,j32(p+12),out->pmk))return NIWJ_CREDENTIAL;}
  else copy(out->pmk,p+72,32);
  copy(out->station_rsn,station_rsn,22);
 }
 out->association_reason=p[465];out->security=open?NIWJ_OPEN:NIWJ_WPA2_PSK_CCMP;out->native_flags=j32(p+504);''',''' if(eap)n8x_station_rsn(eap,out->station_rsn);
 else if(!open){
  if(pass){if(niwj_pmk_hook(p+72,j32(p+60),p+16,j32(p+12),out->pmk))return NIWJ_CREDENTIAL;}
  else copy(out->pmk,p+72,32);
  copy(out->station_rsn,station_rsn,22);
 }
 out->association_reason=p[465];out->security=eap==N8X_AKM_8021X?NIWJ_WPA2_8021X:eap?NIWJ_WPA2_8021X_SHA256:open?NIWJ_OPEN:NIWJ_WPA2_PSK_CCMP;out->native_flags=j32(p+504);''')
sub('wifi_ios_join.c','''static int rsn_ok(const uint8_t*p,size_t z){
 size_t at=2;unsigned count,psk=0,ccmp=0;''','''static int rsn_ok(const uint8_t*p,size_t z,unsigned akm){
 size_t at=2;unsigned count,psk=0,ccmp=0;''')
sub('wifi_ios_join.c',''' while(count--){psk|=suite(p+at,2);at+=4;}if(!psk)return 0;
 if(z-at<2)return 0;unsigned caps=j16(p+at);at+=2;if(caps&(64u|2u))return 0; /*MFPR or NoPairwise*/''',''' while(count--){psk|=suite(p+at,akm);at+=4;}if(!psk)return 0;
 if(z-at<2)return 0;unsigned caps=j16(p+at);at+=2;if(caps&(64u|2u)){if(caps&64u)niwj_ap_mfpr++;return 0;} /*MFPR or NoPairwise*/''')
sub('wifi_ios_join.c','''  (q->security==NIWJ_OPEN?(r->privacy||r->rsn_present||(r->capability&16)):(!r->privacy||!r->rsn_present))''','''  (q->security==NIWJ_OPEN?(r->privacy||r->rsn_present||(r->capability&16)):(!r->privacy||!r->rsn_present))''')
sub('wifi_ios_join.c','''  if(id==48){if(seen&4||!rsn_ok(r->ies+i,z+2))return 0;''','''  if(id==48){if(seen&4||!rsn_ok(r->ies+i,z+2,q->station_rsn[19]))return 0;''')
sub('wifi_ios_join.c',''' if(!q||!s||!info||!out||(q->security!=NIWJ_WPA2_PSK_CCMP&&q->security!=NIWJ_OPEN)||!q->ssid_length''',''' if(!q||!s||!info||!out||!q->security||q->security>NIWJ_SECURITY_MAX||!q->ssid_length''')
sub('wifi_ios_join.c','''  (q->security==NIWJ_OPEN?(nz(q->station_rsn,22)||nz(q->pmk,32)||q->native_flags):!same(q->station_rsn,station_rsn,22))||''','''  (q->security==NIWJ_OPEN?(nz(q->station_rsn,22)||nz(q->pmk,32)||q->native_flags):
   niwj_eap(q->security)?(n8x_rsn_akm(q->station_rsn)!=(q->security==NIWJ_WPA2_8021X?N8X_AKM_8021X:N8X_AKM_8021X_SHA256)||nz(q->pmk,32)):!same(q->station_rsn,station_rsn,22))||''')
sub('wifi_ios_join.c',''' for(unsigned i=0;i<s->count;i++)if(candidate(&s->records[i],q,info,now,&a)''',''' niwj_ap_mfpr=0;
 for(unsigned i=0;i<s->count;i++)if(candidate(&s->records[i],q,info,now,&a)''')
# ---------------- wifi_ios_station_view: an associated 802.1X link answers state queries while EAP runs
sub('wifi_ios_station_view.c','&&s->security<=NIWJ_OPEN&&','&&s->security<=NIWJ_SECURITY_MAX&&')
sub('wifi_ios_station_view.c','''int niwj_authorized(const struct njs_snapshot*s){return valid(s)&&s->native_generation&&s->ticket&&s->phase==NJS_AUTHORIZED&&s->result==1&&s->associated&&s->data_ready&&(''','''/* 0450: an 802.1X link is reported to its clients from association on (phase
 * AUTHORIZED, result 1 set by the native owner when the BSD link goes up for
 * EAPOL): eapolclient needs SSID/BSSID/AUTH_TYPE before any key exists.
 * data_ready / ptk_ready / gtk_ready still tell the real controlled port. */
int niwj_authorized(const struct njs_snapshot*s){if(valid(s)&&niwj_eap(s->security))return s->native_generation&&s->ticket&&s->phase==NJS_AUTHORIZED&&s->result==1&&s->associated;return valid(s)&&s->native_generation&&s->ticket&&s->phase==NJS_AUTHORIZED&&s->result==1&&s->associated&&s->data_ready&&(''')
sub('wifi_ios_station_view.c','p32(p+8,s->security==NIWJ_OPEN?0:8);','p32(p+8,s->security==NIWJ_OPEN?0:s->security==NIWJ_WPA2_8021X?4:s->security==NIWJ_WPA2_8021X_SHA256?0x80:8);')
