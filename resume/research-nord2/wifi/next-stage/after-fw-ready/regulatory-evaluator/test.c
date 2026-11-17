/* SPDX-License-Identifier: GPL-2.0-only */
#include "evaluator.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t key(const char*s){uint32_t v=0;for(unsigned i=0;i<4&&s[i];i++)v|=(uint32_t)(uint8_t)s[i]<<(8*i);return v;}
int main(void){
 struct nrp_caps c={.sku_present=1,.phy_present=1,.efuse_valid=1,.band5=1,.nss=2};
 struct nrp_board b={{0,0},1,1};struct nre_result r,before;
 const char*keys[]={"US01","US","CN","NL","CZ","JP","TR"};
 const unsigned allowed[]={32,36,26,32,32,33,32},passive[]={20,20,22,17,17,18,17};
 for(unsigned k=0;k<7;k++){
  c.efuse_country=key(keys[k]);assert(!nre_evaluate(&c,&b,&r));
  assert(r.origin==NRE_TAG3_EFUSE&&r.local_match&&!r.world_fallback&&r.country==c.efuse_country&&r.table_key==c.efuse_country);
  assert(r.channels==39&&r.allowed_count==allowed[k]&&r.passive_count==passive[k]&&!r.tx_authorized&&!r.power_path_resolved);
  for(unsigned n=0;n<39;n++){
   const struct nre_channel*x=&r.channel[n];assert(x->channel&&x->frequency_mhz&&x->band==(n>=14));
   if(x->passive20)assert(x->allowed20&&!((x->flags|x->hardware_flags)&(8|512|1|2048)));
   if(x->allowed20)assert(x->rule_index<r.rule_count&&!x->max_eirp_mbm&&!x->antenna_gain_mbi);
  }
 }
 c.efuse_country=key("JP");assert(!nre_evaluate(&c,&b,&r));assert(r.channel[13].allowed20&&(r.channel[13].flags&64));
 c.efuse_country=key("US");assert(!nre_evaluate(&c,&b,&r));assert(r.channel[14].max_bandwidth_khz==160000&&!(r.channel[14].flags&256));assert(r.channel[18].flags&8);assert(r.channel[18].dfs_cac_ms==60000);
 c.efuse_country=key("US01");assert(!nre_evaluate(&c,&b,&r));assert(r.channel[14].max_bandwidth_khz==40000&&(r.channel[14].flags&128));
 for(unsigned k=0;k<65536;k++){
  c.efuse_country=(uint32_t)k|0xa5a50000u;assert(!nre_evaluate(&c,&b,&r)&&r.world_fallback&&!r.local_match&&r.origin==NRE_TAG3_EFUSE&&r.country==c.efuse_country&&r.table_key==key("99")&&r.allowed_count==39&&r.passive_count==23);
 }
 c.efuse_valid=0;c.efuse_country=key("US01");b.country[0]='U';b.country[1]='S';assert(!nre_evaluate(&c,&b,&r)&&r.country==0x3030&&r.origin==NRE_RESET_WORLD&&r.world_fallback&&r.allowed_count==39&&r.passive_count==23);
 /* Source clears current flags; conservative passive eligibility separately
  * preserves source hardware DFS markers even under broad world rules. */
 assert(r.channel[18].hardware_flags==8&&!r.channel[18].passive20);
 /* World max BW80 maps NO160, not RADAR. */
 assert((r.channel[18].flags&256)&&!(r.channel[18].flags&8));
 for(unsigned k=0;k<3;k++){
  c.band5=b.support5=b.enable5=1;if(k==0)c.band5=0;if(k==1)b.support5=0;if(k==2)b.enable5=0;
  assert(!nre_evaluate(&c,&b,&r)&&r.allowed_count==14&&r.passive_count==14);
  for(unsigned n=14;n<39;n++)assert(r.channel[n].flags&1);
 }
 c.band5=b.support5=b.enable5=1;
 memset(&r,0xa5,sizeof r);before=r;
 assert(nre_evaluate(0,&b,&r)<0&&!memcmp(&r,&before,sizeof r));assert(nre_evaluate(&c,0,&r)<0&&!memcmp(&r,&before,sizeof r));assert(nre_evaluate(&c,&b,0)<0);
 c.sku_present=0;assert(nre_evaluate(&c,&b,&r)<0&&!memcmp(&r,&before,sizeof r));c.sku_present=1;c.phy_present=0;assert(nre_evaluate(&c,&b,&r)<0&&!memcmp(&r,&before,sizeof r));c.phy_present=1;c.nss=0;assert(nre_evaluate(&c,&b,&r)<0&&!memcmp(&r,&before,sizeof r));
 /* Exercise exact kernel mapping not present in selected local tables. */
 assert(rule_flags((1u<<7)|(1u<<8))==2);
 assert(rule_flags((1u<<4)|(1u<<3)|1u|(1u<<12)|(3u<<13)|(3u<<15))==(8u|512u|64u|1024u|48u|384u));
 struct nre_domain d={0,0,1,{{2402000,2417000,15000,0,0,0,0}}};
 assert(rule_at(&d,2412000)<0); /* Fits10MHz but no source20MHz fallback. */
 d.rule[0]=(struct nre_rule){2402000,2422000,20000,0,0,0,0};assert(rule_at(&d,2412000)==0);assert(rule_at(&d,2412001)<0);assert(bandwidth_flags(&d,0,2412000)==(48|128|256));
 d.count=2;d.rule[0]=(struct nre_rule){2400000,2440000,40000,1u<<11,0,0,0};d.rule[1]=(struct nre_rule){2440000,2480000,40000,0,0,0,0};assert(max_bw(&d,0)==80000);d.rule[1].start++;assert(max_bw(&d,0)==40000);d.rule[1].start--;
 d.rule[0].flags|=(3u<<13);assert(max_bw(&d,0)==20000);
 puts("PASS all7 local keys,65536 four-byte fallback cases, complete39-channel counts/power/flags, source AUTO_BW/20MHz edges, conservative DFS filter,3band guards and atomic refusals; synthetic only");return 0;
}
