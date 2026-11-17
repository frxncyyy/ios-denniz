#include <arm64/sweet_early.h>
#if defined(NORD2)
/* WPA2 pairwise handshake and bounded group-key renewal. The BearSSL-backed
 * provider supplies SHA1/HMAC/PBKDF2/PRF/AES-CT unwrap with MIT provenance. */
#include "wifi_wpa2.h"
#include "wifi_wpa_crypto.h"
#if defined(KERNEL) || defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif
static void wipe(void*v,size_t n){volatile uint8_t*p=v;while(n--)*p++=0;}
static void raw_pmk(const uint8_t*p,unsigned pn,const uint8_t*s,unsigned sn,uint8_t out[32]){(void)nwpc_pmk(p,pn,s,sn,out);}
static void hmac_sha1(const uint8_t*k,unsigned kn,const uint8_t*const*p,const unsigned*n,unsigned count,uint8_t out[20]){
 size_t sizes[4];if(count>4){wipe(out,20);return;}for(unsigned i=0;i<count;i++)sizes[i]=n[i];(void)nwpc_hmac(k,kn,p,sizes,count,out);
}
static void prf(const uint8_t*k,unsigned kn,const char*label,const uint8_t*p,unsigned n,uint8_t*out,unsigned on){
 if(kn!=32||n!=76||on!=48||strcmp(label,"Pairwise key expansion")){wipe(out,on);return;}(void)nwpc_ptk(k,p,out);
}
static void mic_cmac(const uint8_t*k,const uint8_t*const*p,const unsigned*n,unsigned count,uint8_t out[16]){
 size_t sizes[4];if(count>4){wipe(out,16);return;}for(unsigned i=0;i<count;i++)sizes[i]=n[i];(void)nwpc_cmac(k,p,sizes,count,out);
}
static int aes_unwrap(const uint8_t*k,const uint8_t*p,unsigned n,uint8_t*out){size_t used=0;return nwpc_unwrap(k,p,n,out,NWP_FRAME,&used);}
/* ---- strict WPA2-PSK/CCMP transaction and bounded GTK-only rekey, no PTK rekey/roam/PMF ---- */
static unsigned be16(const uint8_t*p){return (unsigned)p[0]<<8|p[1];}
static void put16(uint8_t*p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static int equal(const uint8_t*a,const uint8_t*b,size_t n){unsigned d=0;for(size_t i=0;i<n;i++)d|=a[i]^b[i];return !d;}
static int nonzero(const uint8_t*a,size_t n){unsigned d=0;for(size_t i=0;i<n;i++)d|=a[i];return !!d;}
static int rsn_valid(const uint8_t*p){
 static const uint8_t rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 return equal(p,rsn,sizeof rsn); /* exactly one CCMP pairwise/group, PSK; no PMF */
}
/* Bounded WPA2 selection from a full AP RSN IE. This driver selects PSK/CCMP,
 * never SAE or PMF. MFPC is an AP capability, MFPR requires unsupported PMF. */
static unsigned le16(const uint8_t*p){return p[0]|(unsigned)p[1]<<8;}
static int suite(const uint8_t*p,unsigned type){return p[0]==0&&p[1]==15&&p[2]==172&&p[3]==type;}
int nwp_ap_rsn(const uint8_t*p,size_t n){
 if(!p||n<20||n>257||p[0]!=48||p[1]+2u!=n||le16(p+2)!=1||!suite(p+4,4))return -1;
 size_t at=8;unsigned count=le16(p+at),ccmp=0,psk=0;at+=2;
 if(!count||count>(n-at)/4)return -1;
 for(unsigned i=0;i<count;i++,at+=4)ccmp|=suite(p+at,4);
 if(n-at<2)return -1;count=le16(p+at);at+=2;
 if(!count||count>(n-at)/4)return -1;
 for(unsigned i=0;i<count;i++,at+=4)psk|=suite(p+at,2);
 if(!ccmp||!psk)return -1;
 if(at==n)return 0;
 if(n-at<2)return -1;unsigned caps=le16(p+at);at+=2;if(caps&(64u|2u))return -1;
 if(at==n)return 0;
 if(n-at<2)return -1;count=le16(p+at);at+=2;
 if(count>(n-at)/16)return -1;at+=(size_t)count*16;
 /* Optional group-management cipher is metadata only for non-PMF selection. */
 if(n-at==4){if(!(caps&128u)||p[at]||p[at+1]!=15||p[at+2]!=172)return -1;at+=4;}
 return at==n?0:-1;
}
/* Captured RSNXE must match authenticated M3. No SAE/PMF negotiation added. */
int nwp_copy_ap_rsnxe(struct nwp*w,const uint8_t*p,size_t n){
 if(!w||w->state!=NWP_M1||n>1024||(!p&&n))return -1;
 unsigned found=0;const uint8_t*e=0;size_t z=0;
 for(size_t at=0;at<n;){if(n-at<2||p[at+1]>n-at-2)return -1;
  size_t bytes=p[at+1]+2u;
  if(p[at]==244){if(found++||bytes<3||((p[at+2]&15u)+1u)!=p[at+1])return -1;e=p+at;z=bytes;}
  at+=bytes;
 }
 wipe(w->ap_rsnxe,sizeof w->ap_rsnxe);w->ap_rsnxe_bytes=(unsigned)z;if(z)memcpy(w->ap_rsnxe,e,z);return 0;
}
/* M1 may carry a single plaintext PMKID KDE; no PMKSA cache or trust grant. */
static int m1_data(const uint8_t*p,unsigned n){
 unsigned seen=0;
 for(unsigned at=0;at<n;){
  if(p[at]==221&&(at+1==n||p[at+1]==0)){for(unsigned j=at+1;j<n;j++)if(p[j])return -1;return 0;}
  if(n-at<22||seen++||p[at]!=221||p[at+1]!=20||!suite(p+at+2,4))return -1;
  at+=22;
 }return 0;
}
void nwp_cancel(struct nwp*w){if(w)wipe(w,sizeof(*w));}
static int fail(struct nwp*w,struct nwp_result*r){nwp_cancel(w);if(r){wipe(r,sizeof(*r));r->failure=NWP_F_INTERNAL;}return NWP_FAIL;}
static int failed(struct nwp*w,struct nwp_result*r,unsigned why){int v=fail(w,r);if(r)r->failure=why;return v;}
int nwp_pmk(const uint8_t*p,size_t pn,const uint8_t*s,size_t sn,uint8_t out[32]){
 if(!out)return -1;
 if(!p||!s||pn<8||pn>63||sn<1||sn>32){wipe(out,32);return -1;}
 raw_pmk(p,(unsigned)pn,s,(unsigned)sn,out);return 0;
}
int nwp_begin_ap(struct nwp*w,uint64_t gen,const uint8_t pmk[32],const uint8_t own[6],const uint8_t peer[6],const uint8_t rsn[22],const uint8_t *ap,size_t ap_bytes,nwp_rng rng,void*ctx){
 if(!w)return -1;
 nwp_cancel(w);
 if(!gen||!pmk||!own||!peer||!rsn||!ap||!rng||!rsn_valid(rsn)||nwp_ap_rsn(ap,ap_bytes)||
    !nonzero(own,6)||!nonzero(peer,6)||(own[0]&1)||(peer[0]&1)||equal(own,peer,6))return -1;
 w->generation=gen;memcpy(w->pmk,pmk,32);memcpy(w->own,own,6);memcpy(w->peer,peer,6);memcpy(w->rsn,rsn,22);memcpy(w->ap_rsn,ap,ap_bytes);w->ap_rsn_bytes=(unsigned)ap_bytes;
 w->akm=NWP_AKM_PSK;w->version=2;w->pmk_ready=1;memcpy(w->own_rsn,rsn,22);w->own_rsn_bytes=22;
 if(rng(ctx,w->snonce,32)||!nonzero(w->snonce,32)){nwp_cancel(w);return -1;}
 w->state=NWP_M1;return 0;
}
/* ---- 0433: AKM 00-0F-AC:2 with PMF, :6 (PSK-SHA256) and :8 (SAE) ---- */
struct rsn_view {unsigned akms,caps,group_management;};
/* Full AP RSN element: CCMP group and pairwise offered, well formed tail. */
static int rsn_view(const uint8_t*p,size_t n,struct rsn_view*v){
 if(!p||n<20||n>257||p[0]!=48||p[1]+2u!=n||le16(p+2)!=1||!suite(p+4,4))return -1;
 size_t at=8;unsigned count=le16(p+at),ccmp=0;at+=2;v->akms=v->caps=v->group_management=0;
 if(!count||count>(n-at)/4)return -1;
 for(unsigned i=0;i<count;i++,at+=4)ccmp|=suite(p+at,4);
 if(!ccmp||n-at<2)return -1;count=le16(p+at);at+=2;
 if(!count||count>(n-at)/4)return -1;
 for(unsigned i=0;i<count;i++,at+=4)if(p[at]==0&&p[at+1]==15&&p[at+2]==172&&p[at+3]<32)v->akms|=1u<<p[at+3];
 if(at==n)return 0;
 if(n-at<2)return -1;v->caps=le16(p+at);at+=2;if(v->caps&2u)return -1;
 if(at==n)return 0;
 if(n-at<2)return -1;count=le16(p+at);at+=2;
 if(count>(n-at)/16)return -1;at+=(size_t)count*16;
 if(at==n)return 0;
 if(n-at!=4||p[at]||p[at+1]!=15||p[at+2]!=172)return -1;
 v->group_management=p[at+3];return 0;
}
static int profile_fits(const struct nwp_profile*f,const struct rsn_view*v){
 if(!f||(f->akm!=NWP_AKM_PSK&&f->akm!=NWP_AKM_PSK_SHA256&&f->akm!=NWP_AKM_SAE)||f->pmf>1||f->pmf_required>1||f->h2e>1||
    (f->pmf_required&&!f->pmf)||(f->h2e&&f->akm!=NWP_AKM_SAE)||!(v->akms&(1u<<f->akm)))return 0;
 if(f->pmf)return (v->caps&128u)&&(!v->group_management||v->group_management==6)&&f->pmf_required==((v->caps&64u)!=0);
 return !(v->caps&64u);
}
int nwp_negotiate(const uint8_t*ap,size_t n,const uint8_t*ies,size_t ies_bytes,unsigned allow,struct nwp_profile*out){
 struct rsn_view v;struct nwp_profile f={0};unsigned h2e=0;
 if(!out)return -1;
 wipe(out,sizeof *out);
 if(rsn_view(ap,n,&v)||ies_bytes>1024||(!ies&&ies_bytes))return -1;
 for(size_t at=0;at<ies_bytes;){if(ies_bytes-at<2||ies[at+1]>ies_bytes-at-2)return -1;
  if(ies[at]==244&&ies[at+1]>=1&&(ies[at+2]&32u))h2e=1;
  at+=2u+ies[at+1];}
 f.pmf=(v.caps&128u)!=0;f.pmf_required=(v.caps&64u)!=0;
 if((allow&NWP_ALLOW_SAE)&&(v.akms&(1u<<NWP_AKM_SAE))){f.akm=NWP_AKM_SAE;f.h2e=h2e;}
 else if((allow&NWP_ALLOW_LEGACY)&&(v.akms&(1u<<NWP_AKM_PSK))&&!f.pmf_required){f.akm=NWP_AKM_PSK;f.pmf=0;}
 else if((allow&NWP_ALLOW_PMF)&&(v.akms&(1u<<NWP_AKM_PSK_SHA256)))f.akm=NWP_AKM_PSK_SHA256;
 else if((allow&NWP_ALLOW_PMF)&&(v.akms&(1u<<NWP_AKM_PSK))&&f.pmf_required)f.akm=NWP_AKM_PSK;
 else return -1;
 if(f.pmf&&v.group_management&&v.group_management!=6)return -1; /* only BIP-CMAC-128 */
 if(!profile_fits(&f,&v))return -1;
 *out=f;return 0;
}
int nwp_station_rsn(const struct nwp_profile*f,uint8_t out[28],size_t*used){
 static const uint8_t base[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 if(used)*used=0;
 if(!f||!out||!used||(f->akm!=NWP_AKM_PSK&&f->akm!=NWP_AKM_PSK_SHA256&&f->akm!=NWP_AKM_SAE)||f->pmf>1||f->pmf_required>f->pmf)return -1;
 memcpy(out,base,22);out[19]=(uint8_t)f->akm;
 if(!f->pmf){*used=22;return 0;}
 out[1]=26;out[20]=(uint8_t)(128u|(f->pmf_required?64u:0u));out[22]=out[23]=0;out[24]=0;out[25]=15;out[26]=172;out[27]=6;*used=28;return 0;
}
int nwp_begin_profile(struct nwp*w,uint64_t gen,const uint8_t*pmk,const uint8_t own[6],const uint8_t peer[6],const struct nwp_profile*f,const uint8_t*ap,size_t ap_bytes,nwp_rng rng,void*ctx){
 struct rsn_view v;size_t used=0;
 if(!w)return -1;
 nwp_cancel(w);
 if(!gen||!own||!peer||!f||!ap||!rng||rsn_view(ap,ap_bytes,&v)||!profile_fits(f,&v)||(!pmk&&f->akm!=NWP_AKM_SAE)||
    !nonzero(own,6)||!nonzero(peer,6)||(own[0]&1)||(peer[0]&1)||equal(own,peer,6)||nwp_station_rsn(f,w->own_rsn,&used))return -1;
 w->generation=gen;memcpy(w->own,own,6);memcpy(w->peer,peer,6);memcpy(w->ap_rsn,ap,ap_bytes);w->ap_rsn_bytes=(unsigned)ap_bytes;
 w->own_rsn_bytes=(unsigned)used;memcpy(w->rsn,w->own_rsn,22);
 w->akm=f->akm;w->pmf=f->pmf;w->akm_profile=1;w->version=f->akm==NWP_AKM_PSK?2:f->akm==NWP_AKM_PSK_SHA256?3:0;
 if(f->h2e){w->own_rsnxe[0]=244;w->own_rsnxe[1]=1;w->own_rsnxe[2]=32;w->own_rsnxe_bytes=3;}
 if(pmk){memcpy(w->pmk,pmk,32);w->pmk_ready=1;}
 if(rng(ctx,w->snonce,32)||!nonzero(w->snonce,32)){nwp_cancel(w);return -1;}
 w->state=NWP_M1;return 0;
}
int nwp_set_pmk(struct nwp*w,uint64_t gen,const uint8_t pmk[32]){
 if(!w||!pmk||gen!=w->generation||w->state!=NWP_M1||w->pmk_ready||w->akm!=NWP_AKM_SAE||!nonzero(pmk,32))return -1;
 memcpy(w->pmk,pmk,32);w->pmk_ready=1;return 0;
}
int nwp_begin(struct nwp*w,uint64_t g,const uint8_t p[32],const uint8_t o[6],const uint8_t a[6],const uint8_t r[22],const uint8_t ar[22],nwp_rng rng,void*c){return nwp_begin_ap(w,g,p,o,a,r,ar,22,rng,c);}
static void derive_keys(struct nwp*w){
 uint8_t data[76],keys[48];int m=memcmp(w->own,w->peer,6)<0,n=memcmp(w->snonce,w->anonce,32)<0;
 memcpy(data,m?w->own:w->peer,6);memcpy(data+6,m?w->peer:w->own,6);
 memcpy(data+12,n?w->snonce:w->anonce,32);memcpy(data+44,n?w->anonce:w->snonce,32);
 if(w->akm==NWP_AKM_PSK)prf(w->pmk,32,"Pairwise key expansion",data,76,keys,48);else (void)nwpc_ptk256(w->pmk,data,keys);
 memcpy(w->kck,keys,16);memcpy(w->kek,keys+16,16);memcpy(w->tk,keys+32,16);wipe(keys,48);wipe(data,76);
}
static void mic(const struct nwp*w,const uint8_t*p,unsigned n,uint8_t*out){
 const uint8_t z[16]={0},*pieces[3]={p,z,p+97};unsigned sizes[3]={81,16,n-97};uint8_t d[20];
 if(w->akm!=NWP_AKM_PSK){mic_cmac(w->kck,pieces,sizes,3,out);return;}
 hmac_sha1(w->kck,16,pieces,sizes,3,d);memcpy(out,d,16);wipe(d,20);
}
static int reply(struct nwp*w,const uint8_t*p,unsigned kind,struct nwp_result*r){
 if(w->serial==UINT64_MAX)return fail(w,r);
 unsigned data=w->own_rsn_bytes+w->own_rsnxe_bytes,n=kind==1?99+data:99;memset(r,0,sizeof(*r));r->frame[0]=p[0];r->frame[1]=3;put16(r->frame+2,n-4);r->frame[4]=2;
 put16(r->frame+5,(kind==1?0x0108u:kind==4?0x0300u:0x0308u)|w->version);memcpy(r->frame+9,p+9,8);
 if(kind==1){memcpy(r->frame+17,w->snonce,32);put16(r->frame+97,data);memcpy(r->frame+99,w->own_rsn,w->own_rsn_bytes);memcpy(r->frame+99+w->own_rsn_bytes,w->own_rsnxe,w->own_rsnxe_bytes);}
 mic(w,r->frame,n,r->frame+81);r->action=NWP_TX;r->bytes=n;r->ticket=++w->serial;w->pending=r->ticket;w->tx_kind=kind;return NWP_TX;
}
static int kde(struct nwp*w,const uint8_t*p,unsigned n,const uint8_t*rsc){
 unsigned at=0,have_rsn=0,have_gtk=0,have_rsnxe=0,have_igtk=0;
 while(at<n){
  if(p[at]==0xdd&&(at+1==n||p[at+1]==0)){
   for(unsigned j=at+1;j<n;j++)if(p[j])return -1;
   break;
  }
  if(n-at<2||p[at+1]>n-at-2)return -1;
  unsigned len=p[at+1]+2;const uint8_t*e=p+at;
  if(e[0]==48){if(have_rsn++||len!=w->ap_rsn_bytes||!equal(e,w->ap_rsn,len))return -1;}
  else if(e[0]==244){if(have_rsnxe++||len!=w->ap_rsnxe_bytes||len<3||!equal(e,w->ap_rsnxe,len))return -1;}
  else if(e[0]==221&&len>=6&&!(e[2]==0&&e[3]==15&&e[4]==172)){/* Bounded unknown vendor IE: no security state. */}
  else if(e[0]==221&&w->pmf&&len>=6&&e[5]==9){ /* IGTK KDE: key ID 4/5, IPN, BIP-CMAC-128 key */
   if(have_igtk++||len!=30||(e[6]!=4&&e[6]!=5)||e[7])return -1;
   w->igtk_index=e[6];memcpy(w->ipn,e+8,6);memcpy(w->igtk,e+14,16);w->igtk_ready=1;
  }
  else if(e[0]==221&&w->akm_profile&&len>=6&&e[5]!=1&&e[5]!=9){/* 0433: OCI/BIGTK and other RSN KDEs carry no state here. */}
  else if(e[0]==221){
   static const uint8_t oui[4]={0,15,172,1};
   if(have_gtk++||len!=24||!equal(e+2,oui,4)||(e[6]&~7u)||e[7])return -1;
   w->gtk_index=e[6]&3;memcpy(w->gtk,e+8,16);memcpy(w->rsc,rsc,8);
  }else return -1;
  at+=len;
 }
 return have_rsn==1&&have_gtk==1&&have_igtk==(w->pmf?1u:0u)&&have_rsnxe==(w->ap_rsnxe_bytes?1u:0u)?0:-1;
}
/* RSN group key data does not require M3's RSN/RSNXE repetition. Optional
 * copies must still equal the authenticated association selection; PMF KDEs
 * are not admitted. Ignore only bounded non-RSN vendor metadata. */
static int group_kde(struct nwp*w,const uint8_t*p,unsigned n,uint8_t key[16],unsigned*index){
 unsigned at=0,gtk=0,rsn=0,xe=0,igtk=0;
 while(at<n){
  if(p[at]==221&&(at+1==n||p[at+1]==0)){for(unsigned j=at+1;j<n;j++)if(p[j])return -1;break;}
  if(n-at<2||p[at+1]>n-at-2)return -1;
  unsigned z=p[at+1]+2u;const uint8_t*e=p+at;
  if(e[0]==48){if(rsn++||z!=w->ap_rsn_bytes||!equal(e,w->ap_rsn,z))return -1;}
  else if(e[0]==244){if(xe++||z!=w->ap_rsnxe_bytes||!equal(e,w->ap_rsnxe,z))return -1;}
  else if(e[0]==221&&z>=6&&!(e[2]==0&&e[3]==15&&e[4]==172)){}
  else if(e[0]==221&&w->pmf&&z>=6&&e[5]==9){
   if(igtk++||z!=30||(e[6]!=4&&e[6]!=5)||e[7])return -1;
   if(!equal(e+14,w->igtk,16)||e[6]!=w->igtk_index){w->igtk_index=e[6];memcpy(w->ipn,e+8,6);memcpy(w->igtk,e+14,16);w->igtk_fresh=1;}
  }
  else if(e[0]==221&&w->akm_profile&&z>=6&&e[5]!=1&&e[5]!=9){}
  else if(e[0]==221){if(gtk++||z!=24||!suite(e+2,1)||(e[6]&~7u)||e[7])return -1;*index=e[6]&3;memcpy(key,e+8,16);}
  else return -1;
  at+=z;
 }
 return gtk==1?0:-1;
}
static int group_receive(struct nwp*w,const uint8_t*p,unsigned n,struct nwp_result*r){
 /* KeyLength is unused for RSN Group1; KDE validates the actual CCMP length.
  * GNonce is not the original pairwise ANonce. Never compare them. */
 if(w->state!=NWP_AUTHORIZED||!w->installed||w->ready_mask!=7)return failed(w,r,NWP_F_KEYINFO_STATE);
 uint8_t sig[16];mic(w,p,n,sig);int valid=equal(sig,p+81,16);wipe(sig,sizeof sig);
 if(!valid)return failed(w,r,NWP_F_MIC);
 int replay=memcmp(p+9,w->rx_replay,8);
 if(replay<0)return failed(w,r,NWP_F_M3_STATE_REPLAY);
 if(!replay){
  if(!w->group_bytes||n!=w->group_bytes||!equal(p,w->group,n))return failed(w,r,NWP_F_M3_CONTENT);
  if(w->pending||w->group_phase)return NWP_BUSY;
  w->group_phase=2;return reply(w,p,4,r); /* ACK retry, no installation or PN reset. */
 }
 if(w->pending||w->group_phase)return NWP_BUSY;
 unsigned z=be16(p+97),index=0;uint8_t plain[NWP_FRAME],key[16];
 if(p[71]||p[72]||aes_unwrap(w->kek,p+99,z,plain)){wipe(plain,sizeof plain);return failed(w,r,NWP_F_UNWRAP);}
 if(group_kde(w,plain,z-8,key,&index)){wipe(key,sizeof key);wipe(plain,sizeof plain);return failed(w,r,NWP_F_KDE);}
 wipe(plain,sizeof plain);
 int same=equal(key,w->gtk,16);
 if(same&&index!=w->gtk_index){wipe(key,sizeof key);return failed(w,r,NWP_F_KDE);}
 if(!same){
  for(unsigned k=0;k<w->gtk_history_count;k++)if(equal(key,w->gtk_history[k],16)){wipe(key,sizeof key);return failed(w,r,NWP_F_KDE);}
  if(w->gtk_history_count>=32||w->serial==UINT64_MAX){wipe(key,sizeof key);return fail(w,r);}
  memcpy(w->gtk_history[w->gtk_history_count++],key,16);
 }
 memcpy(w->rx_replay,p+9,8);memcpy(w->group,p,n);w->group_bytes=n;
 if(same){wipe(key,sizeof key);w->group_phase=2;return reply(w,p,4,r);}
 memcpy(w->gtk,key,16);wipe(key,sizeof key);w->gtk_index=index;memcpy(w->rsc,p+65,8);
 w->group_phase=1;w->group_ticket=++w->serial;w->pending=w->group_ticket;
 r->ticket=w->group_ticket;r->action=NWP_INSTALL;return NWP_INSTALL;
}
int nwp_group_ready(struct nwp*w,uint64_t gen,uint64_t ticket,int success,struct nwp_result*r){
 if(!r)return NWP_FAIL;memset(r,0,sizeof *r);
 if(!w||gen!=w->generation||!ticket||ticket!=w->group_ticket||ticket!=w->pending||w->group_phase!=1||w->state!=NWP_AUTHORIZED)return NWP_DROP;
 if(success!=1)return fail(w,r);w->pending=0;w->group_ticket=0;w->group_phase=2;
 return reply(w,w->group,4,r);
}
int nwp_receive(struct nwp*w,const uint8_t*p,size_t n,struct nwp_result*r){
 if(!r)return NWP_FAIL;
 memset(r,0,sizeof(*r));
 if(!w){r->failure=NWP_F_INTERNAL;return NWP_FAIL;}
 if(w->state==NWP_OFF){r->failure=NWP_F_OFF;return NWP_FAIL;}
 if(!p||n<99||n>NWP_FRAME||p[0]<1||p[0]>2||p[1]!=3||be16(p+2)!=n-4||p[4]!=2||be16(p+97)!=n-99)return failed(w,r,3);
 unsigned info=be16(p+5);
 if(!w->pmk_ready)return failed(w,r,NWP_F_OFF);
 if(info==(0x1380u|w->version))return group_receive(w,p,(unsigned)n,r);
 if(be16(p+7)!=16)return failed(w,r,4);
 if(w->group_bytes)return failed(w,r,NWP_F_KEYINFO_STATE);
 if(info==(0x0088u|w->version)){
  if(!nonzero(p+17,32)||m1_data(p+99,be16(p+97)))return failed(w,r,5);
  if(w->state!=NWP_M1){
   if(w->state!=NWP_M3||memcmp(p+9,w->m1_replay,8)<0||!equal(p+17,w->anonce,32))return failed(w,r,6);
   if(w->pending)return NWP_BUSY;
   memcpy(w->m1_replay,p+9,8);
   return reply(w,p,1,r); /* Same ANonce, same SNonce/PTK; monotonic retransmit counter. */
  }
  if(w->pending)return NWP_BUSY;
  memcpy(w->anonce,p+17,32);memcpy(w->m1_replay,p+9,8);derive_keys(w);w->state=NWP_M3;
  return reply(w,p,1,r);
 }
 if(info!=(0x13c8u|w->version)||w->state<NWP_M3)return failed(w,r,7);
 uint8_t signature[16];mic(w,p,(unsigned)n,signature);int ok=equal(signature,p+81,16);wipe(signature,16);
 if(!ok)return failed(w,r,8);
 if(!equal(p+17,w->anonce,32))return failed(w,r,13);
 if(w->m3_bytes){
  /* AP retries may use a higher replay counter. All authenticated key/RSN/RSC
   * contents must remain identical; never reinstall/reset any packet number. */
  if(n!=w->m3_bytes||memcmp(p+9,w->m3+9,8)<0)return failed(w,r,9);
  for(unsigned i=0;i<n;i++)if(!((i>=9&&i<17)||(i>=81&&i<97))&&p[i]!=w->m3[i])return failed(w,r,10);
  if(w->pending)return NWP_BUSY;
  memcpy(w->m3,p,n);memcpy(w->rx_replay,p+9,8);
  return reply(w,p,3,r);
 }
 if(w->state!=NWP_M3||memcmp(p+9,w->m1_replay,8)<=0)return failed(w,r,11);
 if(w->pending)return NWP_BUSY;
 uint8_t plain[NWP_FRAME];unsigned wrapped=be16(p+97);
 if(aes_unwrap(w->kek,p+99,wrapped,plain)){wipe(plain,sizeof plain);return failed(w,r,12);}
 if(kde(w,plain,wrapped-8,p+65)){wipe(plain,sizeof plain);return failed(w,r,14);}
 wipe(plain,sizeof plain);memcpy(w->m3,p,n);w->m3_bytes=(unsigned)n;memcpy(w->rx_replay,p+9,8);memcpy(w->gtk_history[0],w->gtk,16);w->gtk_history_count=1;w->state=NWP_M4_TX;
 return reply(w,p,2,r);
}
int nwp_security_submitted(struct nwp*w,uint64_t gen,uint64_t ticket,int success){
 if(!w||!ticket||gen!=w->generation||ticket!=w->pending)return NWP_DROP;
 unsigned kind=w->tx_kind;w->pending=0;w->tx_kind=0;
 if(!success){nwp_cancel(w);return NWP_FAIL;}
 if(kind==4){if(w->state!=NWP_AUTHORIZED||w->group_phase!=2){nwp_cancel(w);return NWP_FAIL;}w->group_phase=0;return NWP_DROP;}
 if((kind==2||kind==3)&&w->state==NWP_M4_TX&&!w->installed){w->installed=1;w->install_ticket=ticket;w->state=NWP_KEYS;return NWP_INSTALL;}
 return NWP_DROP;
}
int nwp_keys_ready(struct nwp*w,uint64_t gen,uint64_t ticket,unsigned mask,int success){
 if(!w||gen!=w->generation||!ticket||ticket!=w->install_ticket||w->state!=NWP_KEYS||!w->installed)return NWP_DROP;
 if(!mask||(mask&~7u)||success!=1){nwp_cancel(w);return NWP_FAIL;}
 w->ready_mask|=mask;
 if(w->ready_mask!=7)return NWP_DROP;
 w->state=NWP_AUTHORIZED;return NWP_AUTH;
}

#endif /* NORD2 */
