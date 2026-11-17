#!/usr/bin/env python3
"""Bounded offline ROM+retained-owner evidence. Battery observations are separate."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('persistent_base',HERE/'persistent-base.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
SCHEMAS={'RESULT':('NORD2 WIFI ROM:','status stage policy placed cpu_attempt cpu_release done halt error'),
'OWNER':('NORD2 WIFI ROM OWNER:','ap wf sem sem_hw pending clock power protection asleep balanced'),
'RECEIPT':('NORD2 WIFI ROM RECEIPT:','phase accepted issued raw_valid raw0 raw1 backup writes reads barriers')}
EXPECT={'RESULT':[0,16,1,1,1,1,0x1d1e,0,0],'OWNER':[0,0,0,0,0,0,1,1,1,1],'RECEIPT':[7,2,2,3,0,0,1,88,176,2]}
NUM=r'(0x[0-9a-fA-F]+|[0-9]+)'
def number(s):return int(s,16 if s.startswith('0x') else 10)
def parse(data):
 if len(data)>8*1024*1024:return {'complete':False,'issues':['input exceeds8MiB bound'],'network_ready':False}
 issues=[];records={};kept=[];arms=[];cpu=[];battery=[];obs=[];prep=[]
 try:text=data.decode('utf8')
 except UnicodeDecodeError:text=data.decode('utf8',errors='replace');issues.append('invalid UTF8')
 for n,line in enumerate(text.splitlines(keepends=True),1):
  if 'NORD2 WIFI WFSYS' in line or 'NORD2 WIFI WAKE' in line:issues.append('conflicting old diagnostic')
  if 'NORD2 BAT ' in line or 'NORD2 BATTERY:' in line:battery.append({'line':n,'raw':line.rstrip()})
  if 'NORD2 WIFI ROM' not in line:kept.append(line);continue
  body=line[line.index('NORD2 WIFI ROM'):].rstrip('\r\n')
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:issues.append('truncated/interleaved ROM line')
  found=False
  for k,(prefix,fields) in SCHEMAS.items():
   v=base.base.nums(body,prefix,fields)
   if v is None:continue
   found=True
   if k in records:issues.append('duplicate '+k)
   if list(v.values())!=EXPECT[k]:issues.append('incomplete/contradictory '+k)
   records[k]={'line':n,'value':v};break
  if found:continue
  v=base.base.nums(body,'NORD2 WIFI ROM PREP:','status prepared supply_error claim_source')
  if v is not None:
   if list(v.values())!=[0,1,0,3]:issues.append('ROM supply/currentclaim preparation failed')
   prep.append(n);continue
  m=re.fullmatch(r'NORD2 WIFI ROM SMC ARM: group='+NUM+r' claim='+NUM+'/'+NUM+r' x0='+NUM+r' x1='+NUM+r' x2='+NUM+r' x3='+NUM+r' x4='+NUM,body)
  if m:
   v=list(map(number,m.groups()));idx=len(arms);expect=[1-idx,0x8fb00000,0x450000,0xc200050b,0,0x1b008fb0,0x01008ff4 if idx==0 else 0x8ff4,0xb6db6d if idx==0 else 0xb6d028]
   if idx>=2 or v!=expect:issues.append('unexpected SET request')
   arms.append(n);continue
  m=re.fullmatch(r'NORD2 WIFI ROM CPU ARM: stage='+NUM+' attempted='+NUM+' placed='+NUM+' policy='+NUM+'/'+NUM+' raw='+NUM+'/'+NUM+' writes='+NUM+' reads='+NUM+' barriers='+NUM,body)
  if m:
   if list(map(number,m.groups()))!=[12,1,1,2,2,0,0,88,176,2]:issues.append('invalid CPU release receipt')
   cpu.append(n);continue
  m=re.fullmatch(r'NORD2 WIFI ROM OBS: conn='+NUM+' local='+NUM+' clock='+NUM+' irq='+NUM+' cold='+NUM+' pwr='+NUM+' polls='+NUM+'/'+NUM+'/'+NUM+'/'+NUM,body)
  if m:
   v=list(map(number,m.groups()))
   if v[0]!=0x20010101 or v[1]!=0x20010000 or v[2]&6!=6 or v[3]&1!=1 or v[4]!=0x1f or v[5]&4!=4 or not 1<=v[-1]<=102:issues.append('invalid ROM observation')
   obs.append(n);continue
  issues.append('unknown/refusal ROM line')
 r=base.parse(''.join(kept).encode());issues+=r['issues']
 if set(records)!=set(SCHEMAS) or len(arms)!=2 or len(cpu)!=1 or len(obs)!=1 or len(prep)!=1:issues.append('missing complete ROM evidence')
 else:
  persist=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI PERSIST: retained=' in l),0)
  outer=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI: power probe stage=' in l),0)
  order=prep+[persist]+arms+cpu+[records[k]['line'] for k in SCHEMAS]+obs+[outer]
  if any(a>=b for a,b in zip(order,order[1:])):issues.append('ROM source/result ordering')
 r.update(complete=not issues,issues=issues,rom=records,sha256=hashlib.sha256(data).hexdigest(),network_ready=False,firmware_rom_done_observed=not issues,battery={'records':battery,'first_valid':next((x for x in battery if 'soc_valid=1' in x['raw']),None),'read_error':next((x for x in battery if re.search(r'rc=-[0-9]+',x['raw'])),None),'hardware_values_not_required_for_rom':True})
 return r
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);a=ap.parse_args();r=parse(a.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
