#include <arm64/sweet_early.h>
#if defined(NORD2)
#include "wifi_ios_join.h"
#include "wifi_station_transaction.h"
#include "wifi_8021x.h"
static uint16_t j16(const uint8_t*p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t j32(const uint8_t*p){return j16(p)|(uint32_t)j16(p+2)<<16;}
static int same(const uint8_t*a,const uint8_t*b,size_t n){unsigned d=0;while(n--)d|=*a++^*b++;return !d;}
static int nz(const uint8_t*p,size_t n){unsigned d=0;while(n--)d|=*p++;return d!=0;}
static void copy(void*v,const void*s,size_t n){uint8_t*d=v;const uint8_t*p=s;while(n--)*d++=*p++;}
void niwj_clear(void*v,size_t n){volatile uint8_t*p=v;if(p)while(n--)*p++=0;}
static const uint8_t station_rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
/* Selected producer 1d3ec138c..15c0 emits optional PMKID0/BIP trailer
 * even with station caps0. We negotiate no PMF: native association/M2 retain
 * exactly station_rsn22, never adopt AP-required capabilities or PMKIDs. */
static int station_rsn_ok(const uint8_t*p,unsigned n){
 if(n==22)return same(p,station_rsn,22);
 if((n!=24&&n!=28)||p[0]!=48||p[1]!=n-2||!same(p+2,station_rsn+2,20)||p[22]||p[23])return 0;
 return n==24||(!p[24]&&p[25]==15&&p[26]==172&&(p[27]==6||p[27]==11||p[27]==12||p[27]==13));
}
static int refuse(struct niwj_diagnostic*d,int reason){if(d)d->reason=(uint32_t)-reason;return reason;}
/* 0430: relaxed IOUC join decoder. Still WPA2-PSK/CCMP or OPEN only: SAE-only,
 * 802.1X, WEP, WPA1/TKIP and a station RSN that demands MFP stay refused. */
unsigned niwj_privmac_mode=1;
unsigned niwj_8021x=1,niwj_ap_mfpr;
int printf(const char*,...) __attribute__((format(printf,1,2)));
unsigned niwj_sae_mode=1,niwj_pmf_flags=0,niwj_select_allow=0;
#if defined(KERNEL)
/* Kernel: PBKDF2 of the existing WPA2 engine; mode from the boot-arg once. */
#include <sys/sysctl.h>
extern int PE_parse_boot_argn(const char*,void*,int);
int (*niwj_pmk_hook)(const uint8_t*,size_t,const uint8_t*,size_t,uint8_t out[32])=nwp_pmk;
static void mode_once(void){static unsigned done;if(!done){unsigned m=0;done=1;if(PE_parse_boot_argn("n2wpm",&m,sizeof(m))&&m<=3)niwj_privmac_mode=m;
 if(PE_parse_boot_argn("n2sae",&m,sizeof(m))&&m<=2)niwj_sae_mode=m;if(PE_parse_boot_argn("n2pmf",&m,sizeof(m))&&m<=3)niwj_pmf_flags=m;}}
SYSCTL_UINT(_kern, OID_AUTO, n2wifi_sae, CTLFLAG_RW | CTLFLAG_LOCKED, &niwj_sae_mode, 0, "0434: WPA3-Personal (SAE), PSK-SHA256, PMF-required joins: 0 off, 1 on (SAE when requested), 2 SAE whenever possible; capability bits follow the value at wifid start (boot-arg n2sae)");
SYSCTL_UINT(_kern, OID_AUTO, n2wifi_pmf, CTLFLAG_RW | CTLFLAG_LOCKED, &niwj_pmf_flags, 0, "0434: PMF experiments (not hardware verified): 1 ucMgmtProtection in the pairwise key command, 2 ignore unprotected deauth/disassoc after key install");
#else
int (*niwj_pmk_hook)(const uint8_t*,size_t,const uint8_t*,size_t,uint8_t out[32]);
static void mode_once(void){}
#endif
static unsigned join_logs;
unsigned niwj_sae_enabled(void){mode_once();return niwj_sae_mode!=0;}
/* 0434: secret records of decoded requests, by SSID; see wifi_ios_join.h. */
static struct {uint8_t valid,ssid_length,ssid[32];struct stc_secret secret;} pending[2];
static unsigned pending_next;
static int pending_find(const uint8_t*ssid,size_t n){
 for(int k=0;k<2;k++)if(pending[k].valid&&n&&n<=32&&pending[k].ssid_length==n&&same(pending[k].ssid,ssid,n))return k;
 return -1;
}
int niwj_secret_take(const uint8_t*ssid,size_t n,struct stc_secret*out){
 int k=ssid&&out?pending_find(ssid,n):-1;
 if(k<0)return 0;
 *out=pending[k].secret;niwj_clear(&pending[k],sizeof pending[k]);return 1;
}
/* Tolerant view of an RSN element: AKM bitmap (bit n = 00-0F-AC:n), capabilities, group management cipher; 0 when malformed. */
static int rsn_view(const uint8_t*p,size_t n,unsigned*akms,unsigned*caps,unsigned*management){
 size_t at=8;unsigned count,ccmp=0;*akms=*caps=*management=0;
 if(n<20||n>257||p[0]!=48||(size_t)p[1]+2!=n||j16(p+2)!=1)return 0;
 count=j16(p+at);at+=2;if(!count||count>(n-at)/4)return 0;
 while(count--){ccmp|=(unsigned)(!p[at]&&p[at+1]==15&&p[at+2]==172&&p[at+3]==4);at+=4;}
 if(!ccmp||n-at<2)return 0;
 count=j16(p+at);at+=2;if(!count||count>(n-at)/4)return 0;
 while(count--){if(!p[at]&&p[at+1]==15&&p[at+2]==172&&p[at+3]<32)*akms|=1u<<p[at+3];at+=4;}
 if(n-at>=2){*caps=j16(p+at);at+=2;}
 if(n-at>=2){count=j16(p+at);at+=2;if(count>(n-at)/16)return 0;at+=16*(size_t)count;}
 if(n-at>=4&&!p[at]&&p[at+1]==15&&p[at+2]==172)*management=p[at+3];
 return 1;
}
static struct {uint8_t valid,ssid_length,ssid[32],self[6];} pending_self;
int niwj_take_self(const struct niwj_request*q,uint8_t out[6]){
 int r=0;
 if(q&&out&&pending_self.valid&&q->ssid_length&&q->ssid_length<=32&&pending_self.ssid_length==q->ssid_length&&same(pending_self.ssid,q->ssid,q->ssid_length)){copy(out,pending_self.self,6);r=1;}
 niwj_clear(&pending_self,sizeof pending_self);return r;
}
static int suite0(const uint8_t*p,unsigned type){return !p[0]&&p[1]==15&&p[2]==172&&p[3]==type;}
/* The framework's own RSN element only has to offer CCMP and PSK and must
 * not require MFP; association and M2 always carry station_rsn22. */
static int station_rsn_psk(const uint8_t*p,unsigned n){
 unsigned at=8,count,ccmp=0,psk=0;
 if(n<20||n>64||p[0]!=48||p[1]!=n-2||j16(p+2)!=1||!suite0(p+4,4))return 0;
 count=j16(p+at);at+=2;if(!count||count>(n-at)/4)return 0;
 while(count--){ccmp|=(unsigned)suite0(p+at,4);at+=4;}
 if(!ccmp||n-at<2)return 0;
 count=j16(p+at);at+=2;if(!count||count>(n-at)/4)return 0;
 while(count--){psk|=(unsigned)suite0(p+at,2);at+=4;}
 if(!psk)return 0;
 return n-at<2||!(j16(p+at)&64u);
}
/* 0450: AKM for an 802.1X request. The framework element (when present) must
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
/* class: 1 open, 2 WPA2-PSK with PMK, 3 WPA2-PSK with passphrase, 4 802.1X
 * style, 5 anything else, 6 SAE (WPA3-Personal), 7 PSK-SHA256.
 * 0434, from the IO80211 producer (__performAssociation, iOS 16.7): auth_lower
 * is 1 for every RSN join; auth_upper 0x8 WPA2-PSK, 0x400 PSK-SHA256, 0x1000
 * SAE (0x1/0x4/0x80/0x800/0x4000/0x8000 are the 802.1X family). Once the card
 * reports SAE (capability bit 75) every personal join carries the passphrase
 * as key kind 10 at +72 and the PBKDF2 PMK at +472 (+468 = 32); +504 is 0x4
 * or 0x40000004 when WPA2-PSK is an allowed alternative and 0x100 /
 * 0x40000100 for WPA3-only / transition disabled. The framework RSN element
 * lists only the AKM it selected; the native side negotiates its own. */
static int relaxed(const uint8_t*p,struct niwj_request*out,unsigned*klass,unsigned*ignored,unsigned*akms_out,unsigned*allow_out){
 unsigned upper=j16(p+8),open=upper==0,pass=0,flags=j32(p+504),cached=j32(p+468);
 unsigned akms=0,caps=0,management=0,rsn_bytes=j16(p+204);
 unsigned viewed=rsn_bytes>=20&&rsn_bytes<=64&&rsn_view(p+206,rsn_bytes,&akms,&caps,&management);
 unsigned shape_sae=(upper&0x1000u)!=0,personal=(upper&0x1408u)!=0,alternative=(flags&4u)!=0&&!(j32(p+584)&1u);
 unsigned sae=niwj_sae_mode&&shape_sae;
 *klass=open?1:shape_sae?6:(upper&0x400u)?7:(upper&8u)?2:(upper&0xc885u)?4:5;*ignored=0;*akms_out=(akms&0xffffu)|caps<<16;*allow_out=0;
 if(j32(p)!=1||!j32(p+12)||j32(p+12)>32||(nz(p+48,6)&&(p[48]&1)))return NIWJ_SHAPE;
 /* 0450: WPA2 (4) and/or SHA-256 802.1X (0x80), optionally with the WPA1 bit: no key in the request. */
 unsigned eap=niwj_8021x&&!(upper&8u)&&(upper&0x84u)&&!(upper&~0x85u);
 if(j16(p+4)!=2||j16(p+6)!=1||(!open&&!personal&&!eap))return NIWJ_PROFILE;
 if(eap){
  if(j32(p+60)||j16(p+68)||j16(p+70))return NIWJ_CREDENTIAL;
  eap=eap_akm(p+206,j16(p+204),upper);if(!eap)return NIWJ_SECURITY;
  *klass=4;
 }else if(open){
  if(j32(p+56)||j32(p+60)||j32(p+64)||j16(p+68)||j16(p+70)||cached)return NIWJ_CREDENTIAL;
  if(j16(p+204))return NIWJ_SECURITY;
 }else{
  if(j32(p+56)!=1||j16(p+68)||j16(p+70)||(cached&&cached!=32))return NIWJ_CREDENTIAL;
  if(j32(p+64)==6&&j32(p+60)==32&&!cached)pass=0;
  else if(j32(p+64)==10&&j32(p+60)>=8&&j32(p+60)<=63&&(cached||niwj_pmk_hook)){pass=1;if(*klass==2)*klass=3;}
  else return NIWJ_CREDENTIAL;
  if(!station_rsn_ok(p+206,rsn_bytes)&&!station_rsn_psk(p+206,rsn_bytes)&&!(viewed&&(akms&0x144u)&&(niwj_sae_mode||shape_sae||(upper&0x400u))))return NIWJ_SECURITY;
  /* Without SAE (kern.n2wifi_sae=0) an SAE request is served only where WPA2-PSK is an allowed alternative. */
  if(shape_sae&&(!pass||(!sae&&!alternative)))return NIWJ_SECURITY;
  if((j32(p+584)&1u)&&!sae)return NIWJ_SECURITY; /* WPA3 transition disabled: no PSK fallback */
 }
 /* Unknown association flag bits are reported, not refused; band preference
  * is a hint. Blacklists and conditional options stay unimplemented. */
 *ignored=(p[463]&~0x20u)|((unsigned)(p[464]&~0x48u)<<8);
 if(p[465]>2||((p[463]&0x20u)&&!nz(p+48,6))||(flags&&(open||(flags&~0x40000104u)))||p[514]||j32(p+580))return NIWJ_OPTIONS;
 if((p[464]&8u)&&(!nz(p+508,6)||(p[508]&3u)!=2u))return NIWJ_OPTIONS;
 unsigned scope=j32(p+536);
 if(p[464]&0x40u){
  if(!scope||scope>36||p[540+scope])return NIWJ_OPTIONS;
  for(unsigned i=0;i<scope;i++)if(!p[540+i])return NIWJ_OPTIONS;
 }else if(scope)return NIWJ_OPTIONS;
 out->ssid_length=(uint8_t)j32(p+12);copy(out->ssid,p+16,out->ssid_length);
 out->has_target=(uint8_t)nz(p+48,6);copy(out->target,p+48,6);
 if(eap){n8x_marker(eap,out->pmk);copy(out->station_rsn,station_rsn,22);} /* PSK-shaped for the unchanged binder */
 else if(!open){
  if(pass&&cached)copy(out->pmk,p+472,32);
  else if(pass){if(niwj_pmk_hook(p+72,j32(p+60),p+16,j32(p+12),out->pmk))return NIWJ_CREDENTIAL;}
  else copy(out->pmk,p+72,32);
  copy(out->station_rsn,station_rsn,22);
 }
 out->association_reason=p[465];out->security=open?NIWJ_OPEN:NIWJ_WPA2_PSK_CCMP;out->native_flags=flags&4u;
 if((p[464]&8u)&&(niwj_privmac_mode==1||niwj_privmac_mode==2)){pending_self.ssid_length=out->ssid_length;copy(pending_self.ssid,out->ssid,out->ssid_length);copy(pending_self.self,p+508,6);pending_self.valid=1;}
 {int k=pending_find(out->ssid,out->ssid_length);if(k>=0)niwj_clear(&pending[k],sizeof pending[k]);}
 if(!open&&!eap&&niwj_sae_mode){
  /* Which AKM families the station transaction may negotiate for this
   * request. The WPA2-PSK profile without PMF is excluded when the framework
   * element requires MFP and when the request is WPA3-only / transition disabled. */
  unsigned only=(shape_sae&&!alternative)||(j32(p+584)&1u),k=!pending[0].valid?0u:!pending[1].valid?1u:(pending_next++&1u);
  unsigned allow=only?0u:(NIWJ_ALLOW_PMF|((viewed&&(caps&64u))?0u:NIWJ_ALLOW_LEGACY));
  niwj_clear(&pending[k],sizeof pending[k]);
  if(pass){allow|=NIWJ_ALLOW_SAE;if(shape_sae||niwj_sae_mode>=2)allow|=NIWJ_SAE_REQUESTED;
   pending[k].secret.bytes=j32(p+60);copy(pending[k].secret.password,p+72,j32(p+60));}
  pending[k].secret.allow=allow;pending[k].secret.pmf_flags=niwj_pmf_flags;
  pending[k].ssid_length=out->ssid_length;copy(pending[k].ssid,out->ssid,out->ssid_length);pending[k].valid=1;*allow_out=allow;
 }
 return 0;
}
int niwj_decode_identity(const void*v,size_t n,const uint8_t owner_mac[6],struct niwj_request*out,struct niwj_diagnostic*d){
 const uint8_t*p=v;if(d)niwj_clear(d,sizeof(*d));if(out)niwj_clear(out,sizeof(*out));
 if(!out||!p||n!=NIWJ_ASSOC_BYTES)return refuse(d,NIWJ_SHAPE);
 if(d){d->mode=j16(p+4);d->auth_lower=j16(p+6);d->auth_upper=j16(p+8);d->rsn_bytes=j16(p+204);d->key_kind=j32(p+64);d->key_bytes=j32(p+60);d->native_flags=j32(p+504);d->association_flags=j16(p+463);d->association_reason=p[465];
  d->cached_pmk_bytes=j32(p+468);d->blacklist_count=p[514];d->scope_id=j32(p+536);d->conditional_option=j32(p+580);d->transition_disable=j32(p+584);d->band_preference=j32(p+588);
  d->private_address_present=(p[464]&8u)!=0;
  d->owner_address_valid=owner_mac&&nz(owner_mac,6)&&!(owner_mac[0]&1u);
  d->private_address_matches=d->private_address_present&&d->owner_address_valid&&same(p+508,owner_mac,6);}
 mode_once();
 if(niwj_privmac_mode&&owner_mac){
  unsigned klass=0,ignored=0,akms=0,allow=0;niwj_clear(&pending_self,sizeof pending_self);
  int r=relaxed(p,out,&klass,&ignored,&akms,&allow);if(r)niwj_clear(out,sizeof(*out));
  if(join_logs<4096){join_logs++;printf("NORD2 WIFI IOS JOINSEC: class=%u upper=%#x key_kind=%u key_bytes=%u rsn_bytes=%u flags=%#x ignored=%#x private=%u mode=%u refusal=%d\n",klass,(unsigned)j16(p+8),(unsigned)j32(p+64),(unsigned)j32(p+60),(unsigned)j16(p+204),(unsigned)j16(p+463),ignored,(unsigned)((p[464]&8u)!=0),niwj_privmac_mode,-r);
   /* 0434: numbers only (bit n of akms = AKM 00-0F-AC:n of the framework RSN element). */
   printf("NORD2 WIFI IOS JOINSAE: sae=%u upper=%#x akms=%#x rsn_caps=%#x native=%#x transition=%#x cached_pmk=%u allow=%#x\n",niwj_sae_mode,(unsigned)j16(p+8),akms&0xffffu,akms>>16,(unsigned)j32(p+504),(unsigned)j32(p+584),(unsigned)j32(p+468),allow);}
  return r?refuse(d,r):0;
 }
 if(j32(p)!=1||!j32(p+12)||j32(p+12)>32||(nz(p+48,6)&&(p[48]&1)))return refuse(d,NIWJ_SHAPE);
 unsigned open=j16(p+8)==0;
 if(j16(p+4)!=2||j16(p+6)!=1||(!open&&j16(p+8)!=8))return refuse(d,NIWJ_PROFILE);
 if(open){
  /* Selected no-security producer zeros key record56..199 before RawSet;
   * key storage is inactive, not a zero PMK. No WEP/shared-key/OWE admission. */
  if(j32(p+56)||j32(p+60)||j32(p+64)||j16(p+68)||j16(p+70)||j32(p+468))return refuse(d,NIWJ_CREDENTIAL);
  if(j16(p+204))return refuse(d,NIWJ_SECURITY);
 }else{
  if(j32(p+56)!=1||j32(p+60)!=32||j32(p+64)!=6||j16(p+68)||j16(p+70)||j32(p+468))return refuse(d,NIWJ_CREDENTIAL);
  if(!station_rsn_ok(p+206,j16(p+204)))return refuse(d,NIWJ_SECURITY);
 }
 /* Port policy: producer's PMK6 WPA2 profile flags0/4 are handled by our
  * host WPA2 engine. No claim of undocumented vendor offload semantics. */
 /* Exact producer copies a valid LAA to508 and sets bit08; it is not a
  * permission to ignore a different requested self address. Existing native
  * BSS/TX/ifnet identity is immutable, so require actual retained equality. */
 if((p[463]&~0x20u)||(p[464]&~0x48u)||p[465]>2||((p[463]&0x20u)&&!nz(p+48,6))||
    (j32(p+504)!=0&&(open||j32(p+504)!=4))||p[514]||j32(p+580)||j32(p+584)||j32(p+588))return refuse(d,NIWJ_OPTIONS);
 if((p[464]&8u)&&(!owner_mac||!nz(owner_mac,6)||(owner_mac[0]&1u)||
    !nz(p+508,6)||(p[508]&3u)!=2u||!same(p+508,owner_mac,6)))return refuse(d,NIWJ_OPTIONS);
 /* Producer CFStringGetCString(...,37,UTF8), strlen, then stores length536.
  * Match the first terminator exactly; unused bytes after it are inactive.
  * Absence of bit40 leaves this storage inactive but length must be zero. */
 unsigned scope=j32(p+536);
 if(p[464]&0x40u){
  if(!scope||scope>36||p[540+scope])return refuse(d,NIWJ_OPTIONS);
  for(unsigned i=0;i<scope;i++)if(!p[540+i])return refuse(d,NIWJ_OPTIONS);
 }else if(scope)return refuse(d,NIWJ_OPTIONS);
 out->ssid_length=(uint8_t)j32(p+12);copy(out->ssid,p+16,out->ssid_length);
 out->has_target=(uint8_t)nz(p+48,6);copy(out->target,p+48,6);if(!open){copy(out->pmk,p+72,32);copy(out->station_rsn,station_rsn,22);}
 out->association_reason=p[465];out->security=open?NIWJ_OPEN:NIWJ_WPA2_PSK_CCMP;out->native_flags=j32(p+504);return 0;
}
int niwj_decode(const void*v,size_t n,struct niwj_request*out,struct niwj_diagnostic*d){
 return niwj_decode_identity(v,n,0,out,d);
}
/* AP offer may contain SAE in addition to PSK. We negotiate PSK/CCMP only,
 * never SAE/PMF. Unknown optional suites do not become our capabilities. */
static int suite(const uint8_t*p,unsigned type){return !p[0]&&p[1]==15&&p[2]==172&&p[3]==type;}
static int rsn_ok(const uint8_t*p,size_t z,unsigned akm){
 size_t at=2;unsigned count,psk=0,ccmp=0;
 if(z<22||z>NIWJ_AP_RSN_BYTES||p[0]!=48||(size_t)p[1]+2!=z||j16(p+at)!=1)return 0;at+=2;
 if(!suite(p+at,4))return 0;at+=4;
 count=j16(p+at);at+=2;if(!count||count>(z-at)/4)return 0;
 while(count--){ccmp|=suite(p+at,4);at+=4;}if(!ccmp||z-at<2)return 0;
 count=j16(p+at);at+=2;if(!count||count>(z-at)/4)return 0;
 while(count--){psk|=suite(p+at,akm);at+=4;}if(!psk)return 0;
 if(z-at<2)return 0;unsigned caps=j16(p+at);at+=2;if(caps&(64u|2u)){if(caps&64u)niwj_ap_mfpr++;return 0;} /*MFPR or NoPairwise*/
 /* AP ExtendedKeyID support is optional; station caps0 negotiates key ID0. */
 if(at==z)return 1;
 if(z-at<2)return 0;count=j16(p+at);at+=2;if(count>(z-at)/16)return 0;at+=16*count;
 if(at==z)return 1;
 /* Group-management cipher may be advertised with optional PMF; never used. */
 return z-at==4&&(caps&128u)!=0&&!p[at]&&p[at+1]==15&&p[at+2]==172;
}
/* 0434: AP offers that only the SAE / PSK-SHA256 / PMF profiles can use; the
 * station transaction repeats the exact choice (nwq_negotiate). BIP-CMAC-128 only. */
static int rsn_wide(const uint8_t*p,size_t z,unsigned allow){
 unsigned akms,caps,management;
 if(!(allow&(NIWJ_ALLOW_SAE|NIWJ_ALLOW_PMF))||z<22||z>NIWJ_AP_RSN_BYTES||!suite(p+4,4)||!rsn_view(p,z,&akms,&caps,&management)||(caps&2u))return 0;
 if((caps&128u)&&management&&management!=6)return 0;
 if((allow&NIWJ_ALLOW_SAE)&&(akms&0x100u))return 1;
 if(!(allow&NIWJ_ALLOW_PMF))return 0;
 return ((akms&0x40u)!=0)||((akms&4u)&&(caps&192u)==192u);
}
static int legacy_rate(unsigned v){return v==2||v==4||v==11||v==22||v==12||v==18||v==24||v==36||v==48||v==72||v==96||v==108;}
struct parsed {unsigned rsn_at,rsn_bytes,count;uint8_t rates[12];int16_t rssi;};
static unsigned wanted_akm; /* 0450: AKM the running niwj_select requires of the AP element (2 PSK, 1 / 5 802.1X) */
static unsigned select_logs;
static int candidate(const struct nsb_record*r,const struct niwj_request*q,const struct nss_info*info,uint64_t now,struct parsed*a,unsigned allow){
 unsigned seen=0;niwj_clear(a,sizeof(*a));
 if(!r->ssid_present||r->hidden||r->ssid_len!=q->ssid_length||!same(r->ssid,q->ssid,r->ssid_len)||
  !nz(r->bssid,6)||(r->bssid[0]&1)||(q->has_target&&!same(q->target,r->bssid,6))||
  (q->security==NIWJ_OPEN?(r->privacy||r->rsn_present||(r->capability&16)):(!r->privacy||!r->rsn_present))||!(r->capability&1)||(r->capability&2)||r->channel_mismatch||!r->channel||r->channel!=r->hw_channel||
  (r->subtype!=8&&r->subtype!=5)||r->ies_truncated||r->ie_bytes!=r->ie_total||r->ie_bytes>NSB_IE_BYTES||
  !r->observed_us||r->observed_us>now||!r->rxv_valid||!(r->group_valid&4))return 0;
 unsigned band=r->hw_channel<=14?1:2,allowed=0;
 for(unsigned i=0;i<info->count;i++)if(info->channels[i].band==band&&info->channels[i].channel==r->channel)allowed=1;
 if(!allowed)return 0;
 unsigned x=r->rxv[4],y=r->rxv[5];if(x==255&&y==255)return 0;if(x==255)x=0;if(y==255)y=0;x=x>y?x:y;if(x>220)x=220;a->rssi=(int16_t)((int)x/2-110);
 for(size_t i=0;i<r->ie_bytes;){if(r->ie_bytes-i<2)return 0;unsigned id=r->ies[i],z=r->ies[i+1];size_t body=i+2;if(z>r->ie_bytes-body)return 0;
  if(q->security==NIWJ_OPEN&&(id==48||(id==221&&z>=4&&!r->ies[body]&&r->ies[body+1]==80&&r->ies[body+2]==242&&r->ies[body+3]==1)))return 0;
  if(id==48){if(seen&4||(!rsn_ok(r->ies+i,z+2,wanted_akm)&&(wanted_akm!=N8X_AKM_PSK||!rsn_wide(r->ies+i,z+2,allow))))return 0;seen|=4;a->rsn_at=(unsigned)i;a->rsn_bytes=z+2;}
  if(id==1||id==50){unsigned flag=id==1?1:2;if((seen&flag)||!z||(id==1&&z>8))return 0;seen|=flag;
   for(unsigned j=0;j<z;j++){unsigned raw=r->ies[body+j],rate=raw&127;if(raw==0xfbu&&(allow&NIWJ_ALLOW_SAE))continue; /* SAE hash-to-element only */
    if(!legacy_rate(rate)){if(raw&128)return 0;continue;}
    if(band==2&&(rate==2||rate==4||rate==11||rate==22)){if(raw&128)return 0;continue;}
    unsigned k=0;while(k<a->count&&(a->rates[k]&127)!=rate)k++;if(k==a->count){if(k==12)return 0;a->rates[a->count++]=(uint8_t)raw;}else a->rates[k]|=(uint8_t)(raw&128);}
  }i=body+z;
 }
 return (q->security==NIWJ_OPEN?(seen&1)==1:(seen&5)==5)&&a->count!=0;
}
int niwj_select(const struct niwj_request*q,const struct nsb_scan*s,const struct nss_info*info,uint64_t now,struct niwj_target*out){
 const struct nsb_record*best=0;struct parsed a,b={0};
 if(!q||!s||!info||!out||(q->security!=NIWJ_WPA2_PSK_CCMP&&q->security!=NIWJ_OPEN)||!q->ssid_length||q->ssid_length>32||q->association_reason>2||
  (q->has_target!=0&&q->has_target!=1)||(q->has_target&&(!nz(q->target,6)||(q->target[0]&1)))||
  (q->security==NIWJ_OPEN?(nz(q->station_rsn,22)||nz(q->pmk,32)||q->native_flags):!same(q->station_rsn,station_rsn,22))||!niwj_scan_usable(s)||(q->native_flags!=0&&q->native_flags!=4)||!now||!info->count||info->count>NSS_CHANNELS||info->band5>1)return NIWJ_TARGET;
 for(unsigned i=0;i<info->count;i++){if((info->channels[i].band!=1&&info->channels[i].band!=2)||!info->channels[i].channel||
   (info->channels[i].band==1&&info->channels[i].channel>14)||
   (info->channels[i].band==2&&(!info->band5||info->channels[i].channel<=14||info->channels[i].channel>196)))return NIWJ_TARGET;
  for(unsigned j=0;j<i;j++)if(info->channels[j].band==info->channels[i].band&&info->channels[j].channel==info->channels[i].channel)return NIWJ_TARGET;}
 /* 0450: a WPA2 request whose PMK is the 802.1X marker wants an AP with that AKM instead of PSK. */
 unsigned eap=q->security==NIWJ_WPA2_PSK_CCMP?n8x_marker_akm(q->pmk):0;
 if(eap&&!niwj_8021x)return NIWJ_TARGET;
 wanted_akm=eap?eap:N8X_AKM_PSK;niwj_ap_mfpr=0;
 /* 0434: families the waiting secret record of this SSID allows (a password is needed for SAE). */
 unsigned allow=NIWJ_ALLOW_LEGACY;
 if(niwj_select_allow)allow=niwj_select_allow;
 else if(niwj_sae_mode){int k=pending_find(q->ssid,q->ssid_length);if(k>=0){allow=pending[k].secret.allow&7u;if(!pending[k].secret.bytes)allow&=~NIWJ_ALLOW_SAE;}}
 for(unsigned i=0;i<s->count;i++)if(candidate(&s->records[i],q,info,now,&a,allow)&&(!best||a.rssi>b.rssi)){best=&s->records[i];b=a;}
 if(eap&&select_logs<64){select_logs++;printf("NORD2 WIFI 8021X: select akm=%u ap_found=%u ap_requires_pmf=%u%s\n",eap,(unsigned)(best!=0),niwj_ap_mfpr,!best&&niwj_ap_mfpr?" (PMF/MFPR is not supported)":"");}
 if(!best)return NIWJ_TARGET;
 niwj_clear(out,sizeof(*out));out->security=q->security;out->record=*best;out->band=best->hw_channel<=14?1:2;out->channel=best->channel;out->rssi_dbm=b.rssi;
 out->rate_count=(uint8_t)b.count;copy(out->rates,b.rates,b.count);out->ap_rsn_bytes=(uint16_t)b.rsn_bytes;copy(out->ap_rsn,best->ies+b.rsn_at,b.rsn_bytes);return 0;
}

#endif /* NORD2 */
