#include <arm64/sweet_early.h>
#if defined(NORD2)
#include "wifi_ios_policy.h"
#include "wifi_ios_rates.h"
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int niwi_scan_envelope(unsigned selector,const void*v,size_t n,const char name[16]){
 const uint8_t*p=v;size_t cap;if(!p||!name)return -1;
 if(selector==0&&n==15408)cap=15360;else if(selector==6&&n==245808)cap=245760;else return -1;
 if(u32(p)!=0x802869c8||u32(p+4)!=5456||u32(p+8+cap+16)!=10||u32(p+8+cap+20))return -1;
 for(unsigned i=0;i<16;i++)if(p[8+cap+i]!=(uint8_t)name[i])return -1;return 0;
}
int niwi_set_shape(unsigned selector,const void*v,size_t n,const char name[16],uint32_t*out){
 const uint8_t*p=v;size_t cap;if(!p||!name||!out)return -1;
 if(selector==0&&n==15408)cap=15360;else if(selector==6&&n==245808)cap=245760;else return -1;
 if(u32(p)!=0x802869c8||u32(p+4)>cap)return -1;
 for(unsigned i=0;i<16;i++)if(p[8+cap+i]!=(uint8_t)name[i])return -1;
 *out=u32(p+8+cap+16);return 0;
}
/* Explicit passive discovery compatibility: BSS2 infrastructure or BSS3 any;
 * raw1 ACTIVE is served passively, raw2 is PASSIVE, raw3 FAST permits but does
 * not require cache reuse. We execute a fresh scan for all three. No probes.
 * Apple110ms rounds up to108 source V2 TU (110592us); firmware default0 stays0.
 * Dwell is a command parameter, not a measured RF duration guarantee.
 * Directed SSID/BSSID, rest and unknown timing/options remain refused.
 * DFS is admitted only from the authenticated owner list, always passively.
 * info must be the authoritative native owner snapshot. Scheduling hints are
 * best effort; no priority, preemption or completion latency is promised. */
unsigned niwi_scan_relax=1;
int printf(const char*,...) __attribute__((format(printf,1,2)));
#if defined(KERNEL)
extern int PE_parse_boot_argn(const char*,void*,int);
static void relax_once(void){static unsigned done;if(!done){unsigned m=0;done=1;if(PE_parse_boot_argn("n2wsr",&m,sizeof(m))&&m<=1)niwi_scan_relax=m;}}
#else
static void relax_once(void){}
#endif
static unsigned admit_logs;
static int admit(const uint8_t*p,size_t n,const struct nss_info*info,struct nss_request*out,unsigned relax,unsigned*relaxed){
 if(!p||n!=5456||!info||!out)return NIWI_ADMIT_ARGUMENT;
 if(!info->count||info->count>NSS_CHANNELS||(info->dfs_mask>>info->count))return NIWI_ADMIT_OWNER;
 uint32_t mode=u32(p+52),count=u32(p+68),options=u32(p+4872);
 if(u32(p)!=1||(u32(p+4)!=2&&u32(p+4)!=3)||u32(p+56)!=1||count>NSS_CHANNELS||
    (mode!=1&&mode!=2&&mode!=3))return NIWI_ADMIT_PROFILE;
 for(unsigned j=0;j<info->count;j++){
  if((info->channels[j].band!=1&&info->channels[j].band!=2)||!info->channels[j].channel||
     ((info->dfs_mask&(UINT64_C(1)<<j))&&info->channels[j].band!=2))return NIWI_ADMIT_OWNER;
  for(unsigned k=0;k<j;k++)if(info->channels[j].band==info->channels[k].band&&
      info->channels[j].channel==info->channels[k].channel)return NIWI_ADMIT_OWNER;
 }
 /* Compare semantic fields, not padding or inactive array storage. The actual
  * framework dictionary builder initializes RSSI threshold to INT32_MIN.
  * Zero is retained for the existing null/default request profile. Explicit
  * thresholds, other timing profiles, named/short SSIDs are not implemented. */
 unsigned dwell=p[60]|(unsigned)p[61]<<8,soft=0;
 if(u32(p+16)){if(u32(p+16)>32)return NIWI_ADMIT_OPTION;soft|=1;}
 if(dwell!=0&&dwell!=110)soft|=2;
 if(u32(p+64))soft|=4;
 if(u32(p+4880)!=0&&u32(p+4880)!=0x80000000u)soft|=8;
 if(u32(p+4884))soft|=16;
 if(u32(p+5448))soft|=32;
 if(soft&&!relax)return NIWI_ADMIT_OPTION;
 /* Exact builder0x1d3ebb970..9bc maps true LOW_PRIORITY to2 and LOW_LATENCY
  * to0x20. Neither changes RF/filter semantics. The dedicated scan-only worker
  * wakes on enqueue and serializes FIFO without artificial throttling. Admit
  * either advisory hint in that scheduling policy; do not claim preemption,
  * priority reordering or a latency bound. Future shared-radio work must review
  * arbitration. Mixed priority/latency, filters, overrides and6GHz bits refuse. */
 if(u32(p+4876))return NIWI_ADMIT_FLAGS;
 if(options&&options!=NIWI_SCAN_LOW_PRIORITY&&options!=NIWI_SCAN_LOW_LATENCY){
  if(!relax||(options&~(NIWI_SCAN_LOW_PRIORITY|NIWI_SCAN_LOW_LATENCY)))return NIWI_ADMIT_FLAGS;
  soft|=64;
 }
 unsigned zero=1,broadcast=1;
 for(unsigned i=8;i<14;i++){if(p[i])zero=0;if(p[i]!=255)broadcast=0;}
 if(!zero&&!broadcast){if(!relax)return NIWI_ADMIT_PEER;soft|=128;} /* legacy: neither directed peer nor fabricated filter */
 struct nss_request result={0};
 for(unsigned j=0;j<count;j++){
  const uint8_t*c=p+72+12*j;uint32_t channel=u32(c+4),flags=u32(c+8);unsigned band=0;
  if(u32(c)!=1||!channel||channel>255)return NIWI_ADMIT_CHANNEL;
  /* ACTIVE0x80 grants channel permission; it does not require probe TX.
   * We remain passive, including DFS. Other widths/unknown attributes refuse. */
  unsigned dfs=(flags&0x100u)!=0;flags&=~(128u|0x100u);
  if(flags==8||flags==10)band=1;
  else if(flags==16||flags==18)band=2;
  else if(flags!=0&&flags!=2)return NIWI_ADMIT_CHANNEL_FLAGS;
  unsigned found=info->count;
  for(unsigned k=0;k<info->count;k++)if(info->channels[k].channel==channel&&
      (!band||info->channels[k].band==band)){
   if(found!=info->count)return NIWI_ADMIT_ALLOWLIST;found=k;
  }
  if(found==info->count)return NIWI_ADMIT_ALLOWLIST;
  if(dfs&&!(info->dfs_mask&(UINT64_C(1)<<found)))return NIWI_ADMIT_CHANNEL_FLAGS;
  for(unsigned k=0;k<j;k++)if(result.channels[k].band==info->channels[found].band&&
      result.channels[k].channel==info->channels[found].channel)return NIWI_ADMIT_ALLOWLIST;
  result.channels[j]=info->channels[found];
 }
 result.count=count;result.dwell_tu=dwell?108:0;*out=result;*relaxed=soft;return 0;
}
int niwi_scan_admit(const uint8_t*p,size_t n,const struct nss_info*info,struct nss_request*out){
 relax_once();unsigned soft=0,relax=niwi_scan_relax;int r=admit(p,n,info,out,relax,&soft);
 /* Not capped per client like SCANREQ: every relaxed or refused request of
  * the first 512, then one in 32. Numbers only, never names or addresses. */
 if(relax&&(r||soft)&&p&&n==5456){
  if(admit_logs<512||!(admit_logs&31u))printf("NORD2 WIFI IOS SCANADMIT: status=%d relaxed=%#x type=%u bss=%u channels=%u ssid_length=%u names=%u dwell=%u options=%#x seen=%u\n",r,soft,(unsigned)u32(p+52),(unsigned)u32(p+4),(unsigned)u32(p+68),(unsigned)u32(p+16),(unsigned)u32(p+4884),(unsigned)(p[60]|(unsigned)p[61]<<8),(unsigned)u32(p+4872),admit_logs);
  admit_logs++;
 }
 return r;
}
const struct niwp_rate_map*niwi_rate_map(size_t*n){return niwr_production_map(n);}

int niwi_power_on_admit(const void *v,size_t n){
 const uint8_t*p=v;
 return p&&n==24&&u32(p)==1&&u32(p+4)==1&&u32(p+8)==1?0:-1;
}

#endif /* NORD2 */
