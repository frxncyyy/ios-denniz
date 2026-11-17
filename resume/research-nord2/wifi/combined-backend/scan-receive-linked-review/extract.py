#!/usr/bin/env python3
"""Private bounded linked evidence extractor. Static evidence, not a PASS claim."""
from pathlib import Path
import argparse,hashlib,json,subprocess
p=argparse.ArgumentParser();p.add_argument('kernel',type=Path);p.add_argument('--artifact-ready',action='store_true',required=True);a=p.parse_args()
R=Path(__file__).resolve()
while R.name!='ios-sweet':R=R.parent
T=R.parent/'toolchains/proton-clang/bin';out=Path(__file__).resolve().parent/'artifact';out.mkdir(exist_ok=True)
b=a.kernel.read_bytes();raw=subprocess.check_output([str(T/'llvm-nm'),'-n',str(a.kernel)],text=True)
syms=[]
for l in raw.splitlines():
 q=l.split()
 if len(q)==3:
  try:syms.append((int(q[0],16),q[1],q[2]))
  except ValueError:pass
wanted=['_nrd_capture','_ncr_pump','_ncr_memory_range','_ncb_report','_ncd_worker','_nct_run','_nct_lease_window','_ncd_normal_owner_window','_nwfn_guard','_nwm_failure_drain','_ncs_build']
selected={}
for name in wanted:
 hit=[x for x in syms if x[2]==name]
 if len(hit)!=1:raise RuntimeError('missing/duplicate '+name)
 start,kind,_=hit[0];ends=[x[0] for x in syms if x[0]>start and x[1] in 'sStT'];end=min(ends) if ends else start+16384
 if end-start>16384:raise RuntimeError('function extent bound '+name)
 text=subprocess.check_output([str(T/'llvm-objdump'),'-d','--no-show-raw-insn','--start-address='+hex(start),'--stop-address='+hex(end),str(a.kernel)],text=True)
 f=out/(name[1:]+'.txt');f.write_text(text);selected[name]={'address':hex(start),'end':hex(end),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
(out/'extraction.json').write_text(json.dumps({'source_expected':'035793eb6971be129d8ab0514c1b64e4157df7db','kernel':str(a.kernel.resolve()),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'functions':selected,'status':'EXTRACTED_NOT_YET_REVIEWED'},indent=2)+'\n')
