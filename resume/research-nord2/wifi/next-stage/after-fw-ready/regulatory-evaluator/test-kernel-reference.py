#!/usr/bin/env python3
"""Compare pure mapping to selected kernel function bodies, extracted verbatim."""
from pathlib import Path
import re,tempfile,subprocess
P=Path(__file__).resolve().parent;R=P.parents[5].parent/'nord2-current-kernel'
reg=(R/'net/wireless/reg.c').read_text();util=(R/'net/wireless/util.c').read_text()
def function(src,name):
 pattern=r'(?:static\s+)?(?:const struct ieee80211_reg_rule\s*\*|unsigned int|uint32_t|u32|bool)\s*'+name+r'\s*\('
 match=re.search(pattern,src);assert match,name
 begin=src.index('{',match.end());depth=1;end=begin+1
 while depth:
  depth+=(src[end]=='{')-(src[end]=='}');end+=1
 return src[match.start():end]+'\n'
flags={ 'NO_OFDM':0,'NO_OUTDOOR':3,'DFS':4,'NO_IR':7,'IR_CONCURRENT':12,'NO_HT40MINUS':13,'NO_HT40PLUS':14,'NO_80MHZ':15,'NO_160MHZ':16 }
channel={'DISABLED':0,'NO_IR':1,'RADAR':3,'NO_HT40PLUS':4,'NO_HT40MINUS':5,'NO_OFDM':6,'NO_80MHZ':7,'NO_160MHZ':8,'INDOOR_ONLY':9,'IR_CONCURRENT':10,'NO_20MHZ':11,'NO_10MHZ':12}
code=reg[:reg.index('\n\n\n')]+'''\n#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t u32;
static int kernel_abs(uint32_t a){int32_t v=(int32_t)a;return v<0?-v:v;}
#undef abs
#define abs(x) kernel_abs(x)
#define MHZ_TO_KHZ(x) ((x)*1000u)
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define EINVAL 22
#define ERANGE 34
#define NL80211_RRF_AUTO_BW (1u<<11)
#define NL80211_RRF_NO_IR_ALL ((1u<<7)|(1u<<8))
#define IEEE80211_CHAN_NO_HT40 ((1u<<4)|(1u<<5))
struct ieee80211_freq_range {u32 start_freq_khz,end_freq_khz,max_bandwidth_khz;};
struct ieee80211_power_rule {u32 max_antenna_gain,max_eirp;};
struct ieee80211_reg_rule {struct ieee80211_freq_range freq_range;struct ieee80211_power_rule power_rule;u32 flags,dfs_cac_ms;};
struct ieee80211_regdomain {unsigned n_reg_rules;struct ieee80211_reg_rule reg_rules[7];};
struct ieee80211_channel {u32 center_freq;};
'''
for n,b in flags.items():code+=f'#define NL80211_RRF_{n} (1u<<{b})\n'
for n,b in channel.items():code+=f'#define IEEE80211_CHAN_{n} (1u<<{b})\n'
code+=function(util,'cfg80211_does_bw_fit_range')
for name in ['reg_get_max_bandwidth_from_range','reg_get_max_bandwidth','freq_in_rule_band','map_regdom_flags','freq_reg_info_regd','reg_rule_to_chan_bw_flags']:code+=function(reg,name)
code+='#include "'+str(P/'evaluator.c')+'"\n'
code+=r'''
int main(void){unsigned checked=0;
 for(unsigned f=0;f<131072;f++)assert(rule_flags(f)==map_regdom_flags(f));
 for(unsigned z=0;z<8;z++){
  const struct nre_domain*d=z<7?&nre_local[z]:&nre_world;
  struct ieee80211_regdomain rd={0};rd.n_reg_rules=d->count;
  for(unsigned k=0;k<d->count;k++){
   struct ieee80211_reg_rule*r=&rd.reg_rules[k];const struct nre_rule*n=&d->rule[k];
   r->freq_range=(struct ieee80211_freq_range){n->start,n->end,n->bw};r->flags=n->flags;

  }
  for(unsigned k=0;k<d->count;k++){const struct ieee80211_reg_rule*r=&rd.reg_rules[k];assert(max_bw(d,k)==((r->flags&NL80211_RRF_AUTO_BW)?reg_get_max_bandwidth(&rd,r):d->rule[k].bw));}
  for(unsigned center=2390000;center<=5860000;center+=1000){
   int actual=rule_at(d,center);const struct ieee80211_reg_rule*kernel=freq_reg_info_regd(center,&rd,20000);
   assert((actual<0)==((intptr_t)kernel<0));
   if(actual>=0){assert(kernel==&rd.reg_rules[actual]);struct ieee80211_channel ch={center/1000};assert(bandwidth_flags(d,(unsigned)actual,center)==reg_rule_to_chan_bw_flags(&rd,kernel,&ch));}
   checked++;
  }
 }
 printf("PASS selected kernel verbatim mapping oracle:131072 rule flags and%u table/frequency evaluations; AUTO_BW and20MHz edges match\n",checked);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='nre-kernel-reference-') as t:
 src=Path(t)/'reference.c';src.write_text(code);out=Path(t)/'reference'
 subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(src),'-o',str(out)],check=True)
 q=subprocess.run([str(out)],capture_output=True,text=True);(P/'kernel-reference.log').write_text(q.stdout+q.stderr);print(q.stdout);q.check_returncode()
