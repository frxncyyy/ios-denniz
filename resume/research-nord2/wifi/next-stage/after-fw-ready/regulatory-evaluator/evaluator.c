/* SPDX-License-Identifier: GPL-2.0-only
 * Source-derived local tables and Linux regulatory mapping; see LICENSES.md. */
#include "evaluator.h"
struct nre_rule {uint32_t start,end,bw,flags,gain,power,cac;};
struct nre_domain {uint32_t key;unsigned dfs,count;struct nre_rule rule[NRE_MAX_RULES];};
#include "source-tables.inc"
static unsigned fits(const struct nre_rule*r,uint32_t center,uint32_t bw){
 return center>=bw/2&&center-bw/2>=r->start&&center<=UINT32_MAX-bw/2&&center+bw/2<=r->end;
}
static uint32_t distance(uint32_t a,uint32_t b){return a>b?a-b:b-a;}
static int rule_at(const struct nre_domain*d,uint32_t center){
 unsigned band=0;
 for(unsigned k=0;k<d->count;k++){
  const struct nre_rule*r=&d->rule[k];
  if(!band)band=distance(center,r->start)<=2000000u||distance(center,r->end)<=2000000u;
  /* handle_band_custom requires at least20MHz; no 10/5MHz fallback. */
  if(band&&fits(r,center,20000))return (int)k;
 }
 return -1;
}
static uint32_t max_bw(const struct nre_domain*d,unsigned k){
 const struct nre_rule*r=&d->rule[k];uint32_t bw=r->bw;
 if(r->flags&(1u<<11)){
  unsigned lo=k,hi=k;
  while(lo&&d->rule[lo-1].end>=d->rule[lo].start)lo--;
  while(hi+1<d->count&&d->rule[hi+1].start<=d->rule[hi].end)hi++;
  bw=d->rule[hi].end-d->rule[lo].start;
  if((r->flags&(1u<<16))&&bw>80000)bw=80000;
  if((r->flags&(1u<<15))&&bw>40000)bw=40000;
  if((r->flags&(3u<<13))==(3u<<13)&&bw>20000)bw=20000;
 }
 return bw;
}
static uint32_t rule_flags(uint32_t f){
 uint32_t out=0;
 if(f&((1u<<7)|(1u<<8)))out|=2;
 if(f&(1u<<4))out|=8;
 if(f&1u)out|=64;
 if(f&(1u<<3))out|=512;
 if(f&(1u<<12))out|=1024;
 if(f&(1u<<13))out|=32;
 if(f&(1u<<14))out|=16;
 if(f&(1u<<15))out|=128;
 if(f&(1u<<16))out|=256;
 return out;
}
static uint32_t bandwidth_flags(const struct nre_domain*d,unsigned k,uint32_t center){
 const struct nre_rule*r=&d->rule[k];uint32_t bw=max_bw(d,k),f=0;
 if(!fits(r,center,10000)||bw<10000)f|=4096;
 if(!fits(r,center,20000)||bw<20000)f|=2048;
 if(bw<40000)f|=48;
 if(bw<80000)f|=128;
 if(bw<160000)f|=256;
 return f;
}
static int valid_domain(const struct nre_domain*d){
 if(!d||!d->count||d->count>NRE_MAX_RULES)return 0;
 for(unsigned k=0;k<d->count;k++){
  const struct nre_rule*r=&d->rule[k];
  if(!r->start||r->start>=r->end||!r->bw||r->end>10000000u)return 0;
  if(k&&r->start<d->rule[k-1].start)return 0;
 }
 return 1;
}
int nre_evaluate(const struct nrp_caps*c,const struct nrp_board*b,struct nre_result*out){
 struct nre_result r={0};const struct nre_domain*d=&nre_world;
 if(!c||!b||!out||c->sku_present!=1||c->phy_present!=1||!c->nss)return -1;
 r.country=c->efuse_valid?c->efuse_country:0x3030u;
 r.origin=c->efuse_valid?NRE_TAG3_EFUSE:NRE_RESET_WORLD;
 for(unsigned k=0;k<sizeof(nre_local)/sizeof(nre_local[0]);k++)if(nre_local[k].key==r.country){d=&nre_local[k];r.local_match=1;break;}
 if(!valid_domain(d))return -1;
 r.world_fallback=!r.local_match;r.table_key=d->key;r.dfs_region=d->dfs;r.rule_count=d->count;r.channels=NRE_CHANNELS;
 static const uint8_t five[25]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165};
 for(unsigned k=0;k<NRE_CHANNELS;k++){
  struct nre_channel*x=&r.channel[k];x->band=k>=14;x->channel=(uint16_t)(k<14?k+1:five[k-14]);
  x->frequency_mhz=x->band?5000+5*x->channel:x->channel==14?2484:2407+5*x->channel;
  x->hardware_flags=x->band&&x->channel>=52&&x->channel<=144?8:0;
  x->rule_index=255;int ri=rule_at(d,1000u*x->frequency_mhz);
  /* mtk_apply_custom_regulatory resets current flags to zero. The non-self-
   * managed kernel path maps a matching rule; absent rules restore orig flags
   * plus DISABLED. Hardware DFS is additionally retained below for passive
   * eligibility, explicitly narrower than broad source world fallback. */
  if(ri<0){x->flags=x->hardware_flags|1u;continue;}
  const struct nre_rule*q=&d->rule[ri];x->rule_index=(uint8_t)ri;
  x->rule_flags=q->flags;x->max_bandwidth_khz=max_bw(d,(unsigned)ri);
  x->flags=rule_flags(q->flags)|bandwidth_flags(d,(unsigned)ri,1000u*x->frequency_mhz);
  x->antenna_gain_mbi=q->gain;x->max_eirp_mbm=q->power;
  if(x->flags&8)x->dfs_cac_ms=q->cac?q->cac:60000;
  if(x->band&&(!c->band5||!b->support5||!b->enable5))x->flags|=1;
  x->allowed20=!(x->flags&(1u|2048u));
  x->passive20=x->allowed20&&!((x->flags|x->hardware_flags)&(8u|512u));
  if(x->allowed20)r.allowed[r.allowed_count++]=(uint8_t)k;
  if(x->passive20)r.passive[r.passive_count++]=(uint8_t)k;
 }
 *out=r;return 0;
}
