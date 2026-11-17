#!/usr/bin/env python3
"""Offline CPU-held WFSYS identity/power-off evidence; no firmware/network claim."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('persistent_base',HERE/'persistent-base.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
SCHEMAS={'RESULT':('NORD2 WIFI WFSYS:','status stage local_id cpu_held balanced halt dirty unverified error'),
 'OWNER':('NORD2 WIFI WFSYS OWNER:','ap wf sem sem_hw sem_pending clock power protection asleep'),
 'ID':('NORD2 WIFI WFSYS ID:','conn local clock irq initial_afe final_power'),
 'COLD':('NORD2 WIFI WFSYS COLD:','valid cpu top power bgfcpu bgftop'),
 'POLLS':('NORD2 WIFI WFSYS POLLS:','ap wf cfg power protection id off sem sem_off clock')}
def parse(data):
 text=data.decode('utf8',errors='replace');issues=[];records={};kept=[]
 for n,line in enumerate(text.splitlines(keepends=True),1):
  if 'NORD2 WIFI WAKE' in line:issues.append('conflicting old AP wake diagnostic')
  if 'NORD2 WIFI WFSYS' not in line:kept.append(line);continue
  body=line[line.index('NORD2 WIFI WFSYS'):].rstrip('\r\n')
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:issues.append('malformed/truncated/interleaved WFSYS line')
  found=False
  for kind,(prefix,fields) in SCHEMAS.items():
   value=base.base.nums(body,prefix,fields)
   if value is None:continue
   found=True
   if kind in records:issues.append('duplicate WFSYS '+kind)
   if any(not(-(2**31)<=v<2**31 if k in ('status','error') else 0<=v<2**32) for k,v in value.items()):issues.append('WFSYS numeric bounds')
   records[kind]=dict(value=value,line=n,raw=line);break
  if not found:issues.append('WFSYS refusal/fatal/unknown record')
 r=base.parse(''.join(kept).encode());issues+=r['issues']
 try:data.decode('utf8')
 except UnicodeDecodeError:issues.append('invalid UTF8 source')
 if set(records)!=set(SCHEMAS):issues.append('missing complete WFSYS result/owner/ID/cold/polls')
 else:
  a,o,i,c,p=(records[k]['value'] for k in SCHEMAS)
  if a!={'status':0,'stage':11,'local_id':1,'cpu_held':1,'balanced':1,'halt':0,'dirty':0,'unverified':0,'error':0}:issues.append('WFSYS CPU-held power cycle incomplete')
  if o!={k:int(k=='asleep') for k in o}:issues.append('WFSYS retained/uncertain resource')
  if i['conn']!=0x20010101 or i['local']!=0x20010000 or i['clock']&6!=6 or i['irq']&1!=1 or i['initial_afe']&0x300000 or i['final_power']&4:issues.append('WFSYS ID/clock/IRQ/AFE/final-power contradiction')
  bounds={'ap':(1,12),'wf':(1,12),'cfg':(2,22),'power':(1,12),'protection':(4,408),'id':(1,12),'off':(1,12),'sem':(1,1000),'sem_off':(1,1000),'clock':(6,9600)}
  if any(not lo<=p[k]<=hi for k,(lo,hi) in bounds.items()):issues.append('WFSYS poll bounds')
  if c['valid']!=0x1f or any(c[k]&m for k,m in zip(('cpu','top','power','bgfcpu','bgftop'),(1,0x80,4,1,0x80))):issues.append('WFSYS cold proof failed/incomplete')
  p_line=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI PERSIST: retained=' in l),0)
  outer_line=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI: power probe stage=' in l),0)
  order=[p_line]+[records[k]['line'] for k in SCHEMAS]+[outer_line]
  if any(x>=y for x,y in zip(order,order[1:])):issues.append('WFSYS not between retained phase10 and outer summary')
 root=bool(re.search(r'^BSD root: ufsroot0\b',text,re.M));launch=bool(re.search(r'^exec launchd\s*$',text,re.M))
 input_ready=bool(re.search(r'NORD2 INPUT: FT3518 id=54:52 ready=1 powered=1 idle=1 stage=0 irq=[01]; TOPSTATUS=0x[0-9a-f]+ ok=1',text))
 springboard='SpringBoard' in text
 indicators=dict(ufs_root_log=root,launchd_exec_log=launch,input_ready_log=input_ready,springboard_process_log=springboard,human_visible_gui_confirmed=None,human_controls_confirmed=None)
 if not all((root,launch,input_ready,springboard)):issues.append('baseline root/launchd/input/SpringBoard indicators missing')
 r.update(complete=not issues,issues=issues,sha256=hashlib.sha256(data).hexdigest(),wfsys=records,health_indicators=indicators,classification='persistent_phase10_and_CPU_held_WFSYS_cycle' if not issues else 'incomplete_or_conflicting',observed_cpu_held_wfsys_cycle=not issues,observed_combined_success=not issues,network_ready=False)
 r['limits']+=['CPU-held source-subset power-off, not byte-exact register restoration; common power retained','Software process/input logs do not prove visible GUI/physical controls; human confirmation separate','No ROM placement, firmware execution, RF/network or DRAM/MPU permission inferred']
 return r
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);args=ap.parse_args();r=parse(args.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
