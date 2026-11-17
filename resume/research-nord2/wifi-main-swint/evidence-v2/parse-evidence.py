#!/usr/bin/env python3
"""Offline strict captured diagnostic successor; executed stage remains immutable."""
from pathlib import Path
import importlib.util,json,argparse,re,hashlib
P=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('executed_stage',P.parent/'ios/parse-evidence-observed.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
SCHEMA={
'PRESPAN':'reason same sync valid native_before span_native',
'PREORDER':'index events finish coex prepared',
'GUARD':'reason source stage error valid addr raw mask expected',
'TXDESC':'addr used didx valid mismatch wait_ddone consumed_zero',
'TXWB':'constructed observed ddone consumed_zero software_used',
'TXSUBMIT':'raw','TXOBSERVED':'raw',
'RXDESC':'addr buffer didx valid reason','RXWORDS':'raw',
'RXHEADER':'expected type event status cid valid'}
PARTS={'native_before':2,'raw':4,'expected':2,'event':2}
def regex(k):
 parts=[]
 for f in SCHEMA[k].split():
  n=PARTS.get(f,1) if k in ('PRESPAN','TXSUBMIT','TXOBSERVED','RXWORDS','RXHEADER') else 1
  if k=='PRESPAN' and f!='native_before':n=1
  parts.append(re.escape(f)+'='+ '/'.join([r'(0x[0-9a-fA-F]+|[0-9]+)']*n))
 return re.compile('NORD2 WIFI MAIN '+k+': '+ ' '.join(parts)+'$')
RX={k:regex(k) for k in SCHEMA}
def parse(data):
 issues=[];rows={};kept=[];order=[]
 try:data.decode('utf8')
 except UnicodeDecodeError:issues.append('invalid UTF8')
 if len(data)>8*1024*1024:return {'complete':False,'issues':['input>8MiB'],'fw_ready':False}
 for i,line in enumerate(data.decode('utf8',errors='replace').splitlines(keepends=True),1):
  matches=[k for k in SCHEMA if 'NORD2 WIFI MAIN '+k+':' in line]
  if not matches:kept.append(line);continue
  k=matches[0];body=line[line.index('NORD2 WIFI MAIN '):].rstrip('\r\n');m=RX[k].fullmatch(body)
  if not m or not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:
   issues.append('malformed/truncated diagnostic '+k);continue
  if k in rows:issues.append('duplicate diagnostic '+k)
  row=tuple(int(x,16 if x.startswith('0x') else 10) if len(x)<23 else 1<<65 for x in m.groups())
  if any(x>=1<<32 for x in row):issues.append('numeric width '+k)
  rows[k]=row;order.append((i,k))
 result=base.parse(''.join(kept).encode())
 issues=result['issues']+issues
 if set(rows)!=set(SCHEMA):issues.append('missing diagnostic set')
 if set(rows)==set(SCHEMA):
  def check(ok,why):
   if not ok:issues.append(why)
  check(rows['PRESPAN']==(0,0,1,1,0,0,0),'pre-span guard contradiction')
  check(rows['PREORDER']==(6,3,2,1,1),'pre-order contradiction')
  check(rows['GUARD']==(0,)*9,'nonzero first guard/error')
  check(rows['TXDESC']==(0x90000020,72,3,3,0,0,1),'TX descriptor receipt contradiction')
  check(rows['TXWB']==(1,1,1,1,72),'TX software/DDONE receipt contradiction')
  check(rows['TXSUBMIT']==(0x90005000,0x40480000,0,0),'constructed TX tuple contradiction')
  check(rows['TXOBSERVED']==(0x90005000,0x80000000,0,0),'consumed TX tuple contradiction')
  check(rows['RXDESC']==(0x90004020,0x90008000,3,31,0),'RX descriptor receipt contradiction')
  check(rows['RXWORDS']==(0x90008000,0xc0240000,0,0),'RX raw tuple contradiction')
  h=rows['RXHEADER'];check(h[:2]==(1,3) and h[2] in (7,0xe000) and h[3:6]==(1,3,0) and h[6]<=255 and h[7]==31,'actual INIT ACK contradiction')
  check([k for _,k in order]==list(SCHEMA),'diagnostic source order')
  # Preserve diagnostics in their exact current coordinator neighborhoods.
  positions={k:i for i,k in order};lines=data.decode('utf8',errors='replace').splitlines()
  def at(token):return next((i for i,l in enumerate(lines,1) if token in l),0)
  check(at('NORD2 WIFI MAIN OWNER:')<positions['PRESPAN']<positions['GUARD']<at('NORD2 WIFI MAIN NATIVE:'),'guard neighborhood')
  check(at('NORD2 WIFI MAIN TRANSPORT:')<positions['TXDESC']<positions['RXHEADER']<at('NORD2 WIFI MAIN DMASTATUS:'),'descriptor neighborhood')
 result.update(complete=not issues,issues=issues,fw_ready=not issues,classification='main_fw_ready' if not issues else 'incomplete_or_conflicting',diagnostic_records=rows,sha256=hashlib.sha256(data).hexdigest(),dma_enabled=True if not issues else None,rf_calibration_command_executed=True if not issues else None)
 return result
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('log',type=Path);x=a.parse_args();r=parse(x.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
