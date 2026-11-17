#!/usr/bin/env python3
"""Decode an already strict-parsed report offline; never infer effective policy."""
import argparse,hashlib,json,re
from pathlib import Path
BASE=0x40000000
LIMIT=0x23fffffff
LABELS=['unrestricted','secure_RW','secure_RW_nonsecure_R','secure_RW_nonsecure_W','secure_R_nonsecure_R','forbidden','secure_R_nonsecure_RW','unknown_reserved']
def require(condition,message):
 if not condition:raise ValueError(message)
def decode(report):
 require(type(report) is dict and report.get('complete') is True and report.get('issues')==[], 'strict complete report required')
 digest=report.get('sha256');require(isinstance(digest,str) and re.fullmatch('[0-9a-f]{64}',digest),'capture SHA256 missing')
 l=report.get('legacy',{})
 require(l.get('complete') is True and l.get('issues')==[] and l.get('classification')=='all128_raw_values' and l.get('all128_values') is True,'all128 validated raw returns required')
 rows=l.get('records');require(isinstance(rows,list) and len(rows)==128,'exact128 records required')
 summary=l.get('end',{});require(summary.get('collector')==0 and summary.get('issued')==128 and summary.get('valid')==[0xffffffff]*4,'exact all128 summary required')
 for i,row in enumerate(rows):
  require(row.get('index')==i and row.get('offset')==(i%4+1)*0x100+4*(i//4) and row.get('status')==1 and row.get('error')==0,'record order/offset/status/error invalid')
  raw=row.get('raw');require(isinstance(raw,list) and len(raw)==4 and all(type(x) is int and 0<=x<2**64 for x in raw),'full64 raw registers required')
  require(raw[0]<2**32 and raw[0] not in range(0xfffffffc,0x100000000),'invalid/error raw x0')
 regions=[];conn_first=0x8fb00000;conn_last=conn_first+0x450000-1;mcif=0x8a000000
 for r in range(32):
  group=rows[r*4:r*4+4];sa,ea,apc0,apc1=[row['raw'][0] for row in group]
  require(sa<=ea,'inverted SA/EA has no proven disabled encoding')
  first=BASE+(sa<<16);last=BASE+(ea<<16)+0xffff
  require(BASE<=first<=last<=LIMIT and last<2**64,'decoded interval outside saved DRAM bounds')
  domains=[]
  for d in range(16):
   code=([apc0,apc1][d//8]>>(3*(d%8)))&7
   domains.append(dict(domain=d,source_name={0:'AP',2:'CONN',3:'SCP'}.get(d),code=code,source_label=LABELS[code],effective_permission='unknown'))
  regions.append(dict(region=r,raw_records=group,candidate_first=first,candidate_last_inclusive=last,domains=domains,
   apc_uninterpreted_high_bits=[apc0&0xff000000,apc1&0xff000000],
   intersects_CONN=first<=conn_last and last>=conn_first,contains_MCIF_base=first<=mcif<=last))
 values=[x['raw'][0] for x in rows];pattern=None
 if len(set(values))==1 and values[0] in (0,2):pattern='constant_zero_uninformative' if values[0]==0 else 'constant_two_uninformative'
 return dict(capture_sha256=digest,source_report_legacy_pattern=l.get('pattern'),raw_pattern=pattern,
  formula=dict(base=BASE,shift=16,dram_last_inclusive=LIMIT),regions=regions,
  targets=dict(CONN=dict(first=conn_first,last_inclusive=conn_last),MCIF=dict(base_only=mcif,size_unknown=True)),
  intersections=dict(CONN=[r['region'] for r in regions if r['intersects_CONN']],MCIF_base=[r['region'] for r in regions if r['contains_MCIF_base']]),
  effective_permissions_proven=False,limits=['Sequential raw observation, not atomic snapshot','Intervals are source-formula candidates; no enabled/disabled encoding or overlap priority inferred','APC high bits preserved without lock claims; domain7 unknown/reserved','AP/CONN/SCP names are selected source assignments, not current master-routing proof','MCIF intersection tests its base address only; shared allocation size is not proven','Raw zeros/constant2 never establish ABI correctness, ownership or permission'])
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('strict_report',type=Path);args=ap.parse_args();raw=args.strict_report.read_bytes()
 try:r=decode(json.loads(raw));r['strict_report_sha256']=hashlib.sha256(raw).hexdigest()
 except (ValueError,TypeError,KeyError) as e:raise SystemExit('REFUSED: '+str(e))
 print(json.dumps(r,indent=2))
if __name__=='__main__':main()
