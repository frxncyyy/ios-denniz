#!/usr/bin/env python3
"""Reproduce all selected local rules; no runtime country or board data read."""
from pathlib import Path
import ast,re,argparse
P=Path(__file__).resolve().parent
ROOT=P.parents[5]
S=ROOT.parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity/wlan/core/gen4m'
VALUES={'KAL_RRF_DFS':16,'KAL_RRF_AUTO_BW':2048,'KAL_RRF_NO_OFDM':1,'BW_5G':80}
def expression(text):
 node=ast.parse(text.strip(),mode='eval').body
 def walk(n):
  if isinstance(n,ast.Constant) and type(n.value)==int:return n.value
  if isinstance(n,ast.Name):return VALUES[n.id]
  if isinstance(n,ast.BinOp):
   a,b=walk(n.left),walk(n.right)
   if isinstance(n.op,ast.Add):return a+b
   if isinstance(n.op,ast.Sub):return a-b
   if isinstance(n.op,ast.BitOr):return a|b
  raise ValueError('unexpected source expression')
 return walk(node)
def table(text,name):
 block=re.search(r'const struct ieee80211_regdomain '+name+r' = \{(.*?)\n\};',text,re.S).group(1)
 rules=[]
 for args in re.findall(r'REG_RULE_LIGHT\(([^)]*)\)',block):
  a,b,bw,flags=map(expression,args.split(','));rules.append((a*1000,b*1000,bw*1000,flags,0,0,0))
 declared=int(re.search(r'\.n_reg_rules = (\d+)',block).group(1));assert len(rules)==declared
 dfs=1 if 'NL80211_DFS_FCC' in block else 2 if 'NL80211_DFS_ETSI' in block else 3 if 'NL80211_DFS_JP' in block else 0
 return dfs,rules
src=(S/'mgmt/reg_rule.c').read_text();world=(S/'mgmt/rlm_domain.c').read_text();world=world[world.index('#else\nconst struct ieee80211_regdomain default_regdom_ww'):]
keys=[('US01','regdom_us01'),('US','regdom_us'),('CN','regdom_cn'),('NL','regdom_cz_nl'),('CZ','regdom_cz_nl'),('JP','regdom_jp'),('TR','regdom_tr')]
def emit(key,name,text):
 dfs,rules=table(text,name);number=int.from_bytes(key.encode().ljust(4,b'\0'),'little')
 return ' {0x%xu,%du,%du,{\n%s\n }}'%(number,dfs,len(rules),',\n'.join('  {'+','.join(str(v)+'u' for v in r)+'}' for r in rules))
result='/* SPDX-License-Identifier: GPL-2.0-only\n * Generated selected Android local DB; REG_RULE_LIGHT gain/EIRP=0.\n * SOC3 selected HE80, CFG_SUPPORT_BW160 default0, WiFi6G=0. */\n'
result+='static const struct nre_domain nre_world =\n'+emit('99','default_regdom_ww',world).lstrip()+';\n'
result+='static const struct nre_domain nre_local[] = {\n'+',\n'.join(emit(k,n,src) for k,n in keys)+'\n};\n'
a=argparse.ArgumentParser();a.add_argument('--check',action='store_true');args=a.parse_args()
if args.check:assert (P/'source-tables.inc').read_text()==result
else:(P/'source-tables.inc').write_text(result)
print('PASS full selected local DB extraction (7 keys), explicit world fallback; no runtime data')
