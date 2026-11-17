#!/usr/bin/env python3
"""Strict capability format receipt; actual failed inner header remains unknown.

header_metadata_complete concerns only bounded header rows. Receipt completion
also requires the pinned existing full parser and the selected inner policy.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, re
P=Path(__file__).resolve().parent
ROOT=P.parents[3]
SOURCE_PARITY=True
LIMIT=8*1024*1024
N=r'(0x[0-9a-fA-F]+|[0-9]+)'
HEADER=re.compile(r'NORD2 WIFI CAP EVENTHEADER: n='+N+' valid='+N+' bytes='+N+' inner='+N+' raw='+'/'.join([N]*9)+'$')
def parse(data):
 result={'complete':False,'capability_reply':False,'header_metadata_complete':False,'headers':[], 'issues':[], 'network_ready':False,'scan_ready':False,'sha256':hashlib.sha256(data).hexdigest()}
 issues=result['issues']
 if len(data)>LIMIT:issues.append('input>8MiB');return result
 try:text=data.decode('utf8')
 except UnicodeDecodeError:issues.append('invalid UTF8');return result
 try:
  dep=json.loads((P/'parser-dependency.json').read_text());path=ROOT/dep['path']
  if dep['path']!='research/nord2/wifi-capability/ios/parse-capability.py' or dep['sha256']!='7c5bdf4f2d6b3fafad6fbbb3d59a56e67825cdb4045e6735cc629132a6131232' or hashlib.sha256(path.read_bytes()).hexdigest()!=dep['sha256']:raise ValueError()
  spec=importlib.util.spec_from_file_location('executed_capability',path);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
 except (OSError,ValueError,KeyError,ImportError):issues.append('pinned base parser unavailable');return result
 events=[];headers=[];kept=[];last_cap=None
 for line in text.splitlines(keepends=True):
  is_header='NORD2 WIFI CAP EVENTHEADER' in line
  is_event='NORD2 WIFI CAP EVENT:' in line
  if is_header or is_event:
   body=line[line.index('NORD2 WIFI CAP'):].rstrip('\r\n')
   if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:
    issues.append('malformed/truncated header/event');continue
   match=(HEADER if is_header else base.RX['EVENT']).fullmatch(body)
   if not match:issues.append('unknown/malformed header/event');continue
   values=tuple(int(v,16 if v.startswith('0x') else 10) if len(v)<23 else 1<<65 for v in match.groups())
   if any(v>=1<<32 for v in values):issues.append('numeric width header/event')
   if is_header:
    if last_cap!='EVENT':issues.append('header source neighborhood')
    if len(headers)<16:headers.append(values)
    else:issues.append('header budget exceeded')
   else:
    if len(events)<16:events.append(values)
    else:issues.append('event budget exceeded')
   last_cap='EVENTHEADER' if is_header else 'EVENT'
  elif 'NORD2 WIFI CAP' in line:last_cap='OTHER'
  if not is_header:kept.append(line)
 if not events or len(headers)!=len(events):issues.append('incomplete paired event/header rows')
 for i,(event,header) in enumerate(zip(events,headers)):
  n,valid,size,inner,*words=header
  if n!=i or event[0]!=i or valid!=1 or size!=36 or inner>65535:issues.append('header index/validity/extent contradiction')
  if words[0]&65535!=event[6] or words[0]>>16!=event[2] or words[6]&65535!=event[7] or words[6]>>16!=inner or words[7]&255!=event[3] or (words[7]>>8)&255!=event[4]:issues.append('raw header versus captured event contradiction')
  if event[5]<36 or event[5]>3840:issues.append('event descriptor extent')
  # Source parity changes only capability; known debug handling stays strict.
  if inner!=0xe000 and not(SOURCE_PARITY and event[1]==1 and event[3]==0xec):
   # Error20 events retain their observation without claiming a valid reply.
   if event[1]!=4:issues.append('inner policy contradiction')
 result['header_metadata_complete']=not issues
 if not issues:
  result['headers']=[{'n':h[0],'valid':h[1],'bytes':h[2],'inner':h[3],'words':list(h[4:])} for h in headers]
 receipt=base.parse(''.join(kept).encode())
 if receipt['complete'] and not issues:
  result.update(fw_ready=True,complete=True,capability_reply=True,classification='capability_reply_worker_parked')
 else:
  result['classification']='incomplete_or_conflicting'
  issues.append('base capability receipt incomplete/conflicting' if not receipt['complete'] else 'header policy refused')
 return result
if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('log',type=Path);args=a.parse_args()
 with args.log.open('rb') as f:r=parse(f.read(LIMIT+1))
 print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
