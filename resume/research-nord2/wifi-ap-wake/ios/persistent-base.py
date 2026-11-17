#!/usr/bin/env python3
"""Offline persistent source-phase10 evidence; no networking inference."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('live_base',HERE/'live-base.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
FW={'WIFI_RAM_CODE_soc3_0_1a_1.bin':973648,'soc3_0_patch_wmmcu_1a_1_hdr.bin':5726,'soc3_0_ram_wifi_1a_1_hdr.bin':296916,'soc3_0_ram_wmmcu_1a_1_hdr.bin':49868}
RETAIN='NORD2 WIFI: retained=1 restored=0 persistent source stage10'
def parse(data):
 issues=[];records={};firmware={};evidence=[];text=data.decode('utf8',errors='replace')
 try:data.decode('utf8')
 except UnicodeDecodeError:issues.append('invalid UTF8 source')
 live=base.live(data);issues+=['live: '+i for i in live['issues']]
 if not live['success']:issues.append('verified retained live UFS success missing')
 def save(kind,value,n,raw):
  if kind in records:issues.append('duplicate '+kind)
  records[kind]=dict(value=value,line=n);evidence.append(dict(kind=kind,line=n,raw=raw))
 for n,line in enumerate(text.splitlines(keepends=True),1):
  if re.search(r'panic\(cpu|\bpanic:|Kernel panic|Debugger called|NORD2 WIFI PERSIST: (?:fatal|reentry|refuse reentry|incomplete)',line,re.I):issues.append('panic/fatal retained failure observed')
  if any(x in line for x in ('NORD2 WIFI COMMON','NORD2 WIFI ADIE','NORD2 WIFI EMI','NORD2 WIFI: power probe restored=')):issues.append('restoring diagnostic/EMI/outer cleanup conflicts with persistent mode')
  if not any(x in line for x in ('NORD2 WIFI PERSIST','NORD2 WIFI: retained=','NORD2 WIFI: power probe stage=','NORD2 WIFI FW')):continue
  body=line[line.index('NORD2 WIFI'):].rstrip('\r\n')
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:issues.append('malformed/truncated/interleaved report')
  value=base.nums(body,'NORD2 WIFI PERSIST:','retained phase id handoff')
  if value is not None:
   save('PERSIST',value,n,line)
   if value!={'retained':1,'phase':10,'id':0x66358a11,'handoff':2}:issues.append('persistent result lacks exact phase10/knownID/terminal handoff2')
   continue
  value=base.nums(body,'NORD2 WIFI: power probe','stage id cfg success')
  if value is not None:
   save('OUTER',value,n,line)
   if value!={'stage':3,'id':0x20010101,'cfg':3,'success':1}:issues.append('outer result lacks exact source window success')
   continue
  if body==RETAIN:save('RETAIN',dict(retained=1,restored=0),n,line);continue
  m=re.fullmatch(r'NORD2 WIFI FW verified name=([^ ]+) bytes=([0-9]+)',body)
  if m:
   name,size=m[1],int(m[2]);
   if name in firmware:issues.append('duplicate firmware blob')
   if name not in FW or FW.get(name)!=size:issues.append('unexpected firmware blob identity/size')
   firmware[name]=dict(bytes=size,line=n,raw=line);continue
  value=base.nums(body,'NORD2 WIFI FW probe','status elapsed_ticks')
  if value is not None:
   save('FW',value,n,line)
   if value['status']!=0 or not 0<=value['elapsed_ticks']<2**64:issues.append('firmware RAM verification failed/invalid ticks')
   continue
  issues.append('malformed/unknown persistent/firmware report')
 if set(records)!={'PERSIST','OUTER','RETAIN','FW'}:issues.append('missing persistent/outer/retained/firmware result')
 if set(firmware)!=set(FW):issues.append('missing exact four verified firmware blobs')
 if 'FW' in records and any(r['line']>=records['FW']['line'] for r in firmware.values()):issues.append('firmware final before blob verification')
 if all(k in records for k in ('PERSIST','OUTER','RETAIN')):
  if not records['PERSIST']['line']<records['OUTER']['line']<records['RETAIN']['line']:issues.append('persistent/outer/retain report order')
 proofline=next((r['line'] for r in live['raw_evidence'] if r['kind']=='PROOF'),10**9)
 if any(r['line']<=proofline for r in records.values()):issues.append('Wi-Fi result precedes verified live UFS proof')
 return dict(complete=not issues,issues=issues,classification='persistent_source_phase10_retained' if not issues else 'incomplete_or_conflicting',live=live,persistent=records,firmware=firmware,raw_evidence=evidence,sha256=hashlib.sha256(data).hexdigest(),observed_persistent_source_success=not issues,observed_combined_success=not issues,network_ready=False,effective_permissions_proven=False,limits=['Phase10 is selected source initialization/HW-control handoff only; no networking/RF/firmware CPU execution or later MMIO readability inferred','Successful persistent mode retains power and ownership; no restoring common/ID diagnostic is expected','Firmware RAM verification establishes blob integrity only, not execution','Manual physical reset to fastboot for conditional recorded recovery; no automatic Linux fallback'])
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);args=ap.parse_args();r=parse(args.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
