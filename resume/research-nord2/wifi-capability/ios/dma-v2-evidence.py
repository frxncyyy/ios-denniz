#!/usr/bin/env python3
"""Offline reviewed pool evidence with source-backed count0/observed512 policy."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
P=Path(__file__).resolve().parent
# P is wifi-dma/ios; siblingwifi-rom lies below nord2.
sp=importlib.util.spec_from_file_location('rom_base',P.parent.parent/'wifi-rom/ios/parse-evidence.py');base=importlib.util.module_from_spec(sp);sp.loader.exec_module(base)
N=r'(0x[0-9a-fA-F]+|[0-9]+)';S=r'(-?(?:0x[0-9a-fA-F]+|[0-9]+))'
def num(s):return 2**65 if len(s)>22 else int(s,16 if s.lstrip('-').startswith('0x') else 10)
def fields(body,prefix,keys):
 m=re.fullmatch(re.escape(prefix)+' '+' '.join(re.escape(k)+'='+S for k in keys.split()),body)
 return dict(zip(keys.split(),map(num,m.groups()))) if m else None
RINGS=[0x18024500+16*i for i in range(8)]+[0x18025300+16*i for i in list(range(6))+list(range(8,14))+list(range(16,20))]+[0x18025500+16*i for i in range(3)]
SCALAR={'PREP':('NORD2 WIFI DMA PREP:','status claim base bytes layout'),'RESULT':('NORD2 WIFI DMA:','status stage inherited preflight idle stable reads halt error')}
PATTERNS={
 'SET':r'NORD2 WIFI DMA SET: attempted='+N+' accepted='+N+' raw_valid='+N+' raw1='+N+' raw0='+N+' mapped='+N+' zero='+N+' verified='+N+' words='+N+'/'+N,
 'OWNER':r'NORD2 WIFI DMA OWNER: ap='+N+' wf='+N+' asleep='+N+' balanced='+N+' possible_lock='+N+' live='+N+' cfg='+N+' local='+N+' done='+N+' first='+N+'/'+N+'/'+N+'/'+N,
 'LIVE':r'NORD2 WIFI DMA LIVE: cpu='+N+' top='+N+' power='+N+' protection='+N+' dma_protection='+N,
 'CFG':r'NORD2 WIFI DMA CFG: before='+N+'/'+N+' after='+N+'/'+N+' valid='+N}
def parse(data):
 if len(data)>8*1024*1024:return {'complete':False,'issues':['input>8MiB'],'dma_enabled':False,'network_ready':False}
 issues=[];records={};arms=[];raw=[];rows=[];kept=[]
 try:text=data.decode('utf8')
 except UnicodeDecodeError:text=data.decode('utf8',errors='replace');issues.append('invalid UTF8')
 for ln,line in enumerate(text.splitlines(keepends=True),1):
  if 'NORD2 WIFI DMA' not in line:kept.append(line);continue
  body=line[line.index('NORD2 WIFI DMA'):].rstrip('\r\n')
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:
   issues.append('truncated/interleaved DMA report');continue
  found=False
  for kind,(pre,keys) in SCALAR.items():
   v=fields(body,pre,keys)
   if v is not None:
    if kind in records:issues.append('duplicate '+kind)
    records[kind]={'line':ln,'value':v};found=True;break
  if found:continue
  for kind,pat in PATTERNS.items():
   m=re.fullmatch(pat,body)
   if m:
    if kind in records:issues.append('duplicate '+kind)
    records[kind]={'line':ln,'value':list(map(num,m.groups()))};found=True;break
  if found:continue
  m=re.fullmatch(r'NORD2 WIFI DMA ARM: stage='+N+' action=(preflight|map|zero) base='+N+' (rings|bytes)='+N,body)
  if m:arms.append({'line':ln,'value':[num(m[1]),m[2],num(m[3]),m[4],num(m[5])]});continue
  m=re.fullmatch(r'NORD2 WIFI DMA SMC ARM: group='+N+' base='+N+' bytes='+N+' x0='+N+' x1='+N+' x2='+N+' x3='+N+' x4='+N,body)
  if m:arms.append({'line':ln,'value':['smc']+list(map(num,m.groups()))});continue
  m=re.fullmatch(r'NORD2 WIFI DMA RAW: group='+N+' x='+N+'/'+N+'/'+N+'/'+N,body)
  if m:raw.append({'line':ln,'value':list(map(num,m.groups()))});continue
  m=re.fullmatch(r'NORD2 WIFI DMA RING: n='+N+' addr='+N+' raw='+N+'/'+N+'/'+N+'/'+N+' valid='+N,body)
  if m:rows.append({'line':ln,'value':list(map(num,m.groups()))});continue
  issues.append('unknown/refused/fatal DMA record')
 try:r=base.parse(''.join(kept).encode())
 except (ValueError,OverflowError):r={'complete':False,'issues':['malformed numeric evidence'],'limits':[]}
 issues+=['baseline: '+x for x in r['issues']]
 kinds=set(SCALAR)|set(PATTERNS)
 if set(records)!=kinds or len(rows)!=27:issues.append('missing full DMA result/preflight arrays')
 outcome='incomplete_or_conflicting';success=False
 if set(records)==kinds and len(rows)==27:
  prep,res=(records[k]['value'] for k in ('PREP','RESULT'));se,own,live,cfg=(records[k]['value'] for k in ('SET','OWNER','LIVE','CFG'))
  if prep!={'status':0,'claim':1,'base':0x90000000,'bytes':0xf20000,'layout':0x20000}:issues.append('bad currentclaim/preparation')
  if any(v<0 or v>=2**32 for k,v in res.items() if k not in ('status','error')) or any(not -2**31<=res[k]<2**31 for k in ('status','error')):issues.append('numeric bounds')
  if not(live[0]&1 and live[1]&0x80 and live[2]&4) or live[3]&9 or live[4]&9:issues.append('live ROM/reset/power/protection contradiction')
  if own[:4]!=[0,0,1,1] or own[5:9]!=[0x1f,0x20010101,0x20010000,0x1d1e]:issues.append('owner/lifetime/identity contradiction')
  if cfg[4]!=15:issues.append('incomplete global snapshot')
  first=[0,0,0,0];empty=True
  def mismatch(reason,a,v,mask):
   nonlocal first,empty
   empty=False
   if not first[0]:first=[reason,a,v,mask]
  for i in range(2):
   if cfg[i]&15:mismatch(1,0x18024208+0x1000*i,cfg[i],15)
  for i,row in enumerate(rows):
   a=row['value']
   if a[0]!=i or a[1]!=RINGS[i] or a[6]!=15:issues.append('ring order/address/validity')
   for w,mask in enumerate((0xffffffff,0xf0fff,0xfff,0xfff)):
    defined=a[2+w]&mask
    if w==1 and (defined&0xfff)==0x200:defined&=~0xfff
    if defined:mismatch(2,RINGS[i]+4*w,a[2+w],mask)
  stable=True
  for i in range(2):
   if cfg[2+i]&15:mismatch(1,0x18024208+0x1000*i,cfg[2+i],15)
   if (cfg[i]^cfg[2+i])&15:stable=False;mismatch(3,0x18024208+0x1000*i,cfg[2+i],15)
  if own[9:]!=first or res['idle']!=int(empty) or res['stable']!=int(stable):issues.append('first mismatch/summary does not match captured arrays')
  common=dict(stage=12,preflight=1,reads=112,halt=0,error=0,idle=int(empty),stable=int(stable))
  if res==dict(common,status=0,inherited=0) and empty and stable:
   outcome='pool_policy_zero_verified';success=True
   if se!=[3,2,3,0,0,1,1,1,32768,32768] or own[4]!=1:issues.append('incomplete SET/map/zero/readback proof')
   expected=[[5,'preflight',0x18024000,'rings',27],['smc',1,0x90000000,0xf20000,0xc200050b,0,0x12009000,0x10090f1,0xb6db6d],['smc',0,0x90000000,0xf20000,0xc200050b,0,0x12009000,0x90f1,0x80b6da28],[8,'map',0x90000000,'bytes',0xf20000],[9,'zero',0x90000000,'bytes',0x20000]]
   if [a['value'] for a in arms]!=expected or [x['value'][0] for x in raw]!=[1,0] or any(x['value'][1] for x in raw):issues.append('SET request/rawreturn/order contradiction')
  elif res==dict(common,status=1,inherited=1) and not empty:
   outcome='complete_inherited_refusal'
   if se!=[0]*10 or own[4]!=0 or raw or [a['value'] for a in arms]!=[[5,'preflight',0x18024000,'rings',27]]:issues.append('refusal performed SET/map/zero')
  else:issues.append('DMA operation incomplete or contradictory')
  order=[records['PREP']['line']]+[x['line'] for x in arms]+[records['RESULT']['line'],records['SET']['line']]+[x['line'] for x in raw]+[records[k]['line'] for k in ('OWNER','LIVE','CFG')]+[x['line'] for x in rows]
  romobs=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI ROM OBS:' in l),0)
  outer=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI: power probe stage=' in l),0)
  order=[romobs]+order+[outer]
  if any(a>=b for a,b in zip(order,order[1:])):issues.append('DMA native/source evidence ordering')
  allvals=se+own+live+cfg+[v for row in rows for v in row['value']]
  if any(v<0 or v>=2**64 for x in raw for v in x['value']) or any(v>=2**32 for v in se[:3]+se[5:]) or any(v<0 or v>=2**64 for v in allvals) or any(v>=2**32 for v in own+live+cfg+[v for row in rows for v in row['value']]):issues.append('DMA integer overflow')
 r.update(complete=not issues,issues=issues,dma_records=records,dma_rings=rows,classification=outcome if not issues else 'incomplete_or_conflicting',pool_zero_verified=success and not issues,preflight_capture_complete=not issues,dma_enabled=False,network_ready=False,sha256=hashlib.sha256(data).hexdigest())
 return r
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);a=ap.parse_args();r=parse(a.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
