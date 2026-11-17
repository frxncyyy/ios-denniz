#!/usr/bin/env python3
"""Offline evidence parser; no monitor/device operations or permission inference."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('adie_base',HERE/'adie-base.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
PREFIX='NORD2 WIFI EMI LEGACY'
BEGIN=PREFIX+' begin queries_max=128 deadline=none effective=unknown'
INDEX=re.compile(PREFIX+r' index=([0-9]+) offset=(0x[0-9a-f]+|0) status=([0-9]+) error=(-?[0-9]+) raw=([0-9a-f]+)/([0-9a-f]+)/([0-9a-f]+)/([0-9a-f]+)')
END=re.compile(PREFIX+r' end collector=(-?[0-9]+) issued=([0-9]+) valid=(0x[0-9a-f]+|0)/(0x[0-9a-f]+|0)/(0x[0-9a-f]+|0)/(0x[0-9a-f]+|0) elapsed_ticks=([0-9]+) effective=unknown')
def offset(i):return (i%4+1)*0x100+4*(i//4)
def legacy(data):
 issues=[];kept=[];records=[];begin=None;end=None;all_lines=[]
 for n,line in enumerate(data.decode('utf8',errors='replace').splitlines(keepends=True),1):
  if PREFIX not in line:kept.append(line);continue
  body=line[line.index(PREFIX):].rstrip('\r\n');all_lines.append(n)
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:issues.append('malformed/truncated/interleaved legacy line')
  if body==BEGIN:
   if begin is not None:issues.append('duplicate begin')
   if records or end is not None:issues.append('begin after entries/end')
   begin=n;continue
  m=INDEX.fullmatch(body)
  if m:
   i,off,status,error=map(lambda x:int(x,0) if x.startswith('0x') else int(x),m.groups()[:4]);raw=[int(x,16) for x in m.groups()[4:]]
   if i!=len(records) or i>=128:issues.append('duplicate/missing/out-of-order index')
   if begin is None or end is not None:issues.append('index outside begin/end')
   if off!=offset(i) or status>4 or any(x>=2**64 for x in raw) or not(-(2**31)<=error<2**31):issues.append('index offset/status/numeric bounds')
   records.append(dict(index=i,offset=off,status=status,error=error,raw=raw,line=n));continue
  m=END.fullmatch(body)
  if m:
   if end is not None:issues.append('duplicate end')
   vals=m.groups();end=dict(collector=int(vals[0]),issued=int(vals[1]),valid=[int(x,0) for x in vals[2:6]],elapsed_ticks=int(vals[6]),line=n)
   if begin is None or len(records)!=128:issues.append('end lacks begin/all128reports')
   continue
  issues.append('malformed/unknown legacy line')
 classification='not_observed';pattern=None;values=[]
 if all_lines:
  if begin is None or end is None or len(records)!=128:issues.append('incomplete legacy report')
  failed=None;valid=[0]*4
  for i,r in enumerate(records):
   s=r['status'];raw=r['raw'];err=r['error'];v=raw[0]
   known=next((-e for e in range(1,5) if v in (2**64-e,2**32-e)),0)
   if s==1:
    if failed is not None:issues.append('value after first failure')
    if v>0xffffffff or known or err:issues.append('invalid VALUE encoding')
    if i<128:valid[i//32]|=1<<(i%32)
    values.append(v)
   elif s==0:
    if failed is None:issues.append('NOT_CALLED before first failure')
    if any(raw) or err:issues.append('NOT_CALLED contains claimed raw/error')
   else:
    if failed is not None:issues.append('multiple failure results instead of NOT_CALLED tail')
    failed=i
    if s==2 and (not known or err!=known):issues.append('known error encoding mismatch')
    if s==3 and (v<=0xffffffff or known or err):issues.append('invalid upper32 error encoding')
    if s==4 and (any(raw) or err):issues.append('transport failure claims returned raw')
  if end:
   expected=128 if failed is None else failed+1
   if end['issued']!=expected or end['collector']!=(0 if failed is None else 1):issues.append('issued/collector contradict first failure')
   if end['valid']!=valid or any(x>0xffffffff for x in end['valid']) or end['elapsed_ticks']>=2**64:issues.append('summary mask/ticks mismatch')
   classification='all128_raw_values' if failed is None else 'partial_stopped_first_failure'
  if values and len(set(values))==1 and values[0] in (0,2):pattern='constant_zero_uninformative' if values[0]==0 else 'constant_two_uninformative'
 return dict(complete=not issues,issues=issues,classification=classification if not issues else 'incomplete_or_conflicting',records=records,end=end,pattern=pattern,all128_values=not issues and classification=='all128_raw_values',effective_permissions_proven=False,limits=['Raw values are observations only; neither zero nor constant2 proves ABI correctness or permission','Missing/NOT_CALLED never means observed zero','Current executing monitor identity remains unproven from backups; stalled EL3 call has no interruptible software deadline']),''.join(kept).encode()
def parse(data):
 l,filtered=legacy(data);b=base.parse(filtered);issues=['legacy: '+x for x in l['issues']]+b['issues']
 try:data.decode('utf8')
 except UnicodeDecodeError:issues.append('invalid UTF8 source')
 if b['observed_combined_success'] and l['classification']=='not_observed':issues.append('legacy report not observed')
 if l['classification']!='not_observed' and not b['live']['success']:issues.append('legacy observation without verified live UFS')
 b.update(legacy=l,issues=issues,complete=not issues,sha256=hashlib.sha256(data).hexdigest(),observed_combined_success=not issues and b['observed_combined_success'] and l['all128_values'],effective_permissions_proven=False)
 return b
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);args=ap.parse_args();r=parse(args.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
