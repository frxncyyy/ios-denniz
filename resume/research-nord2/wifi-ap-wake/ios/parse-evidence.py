#!/usr/bin/env python3
"""Offline phase10 plus balanced AP-wake evidence; no connectivity inference."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('persistent_base',HERE/'persistent-base.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
def parse(data):
 text=data.decode('utf8',errors='replace');issues=[];wake={};kept=[]
 schemas={'RESULT':('NORD2 WIFI WAKE:','status phase balanced ref halt'),
 'ID':('NORD2 WIFI WAKE ID:','id clock irq ack_polls clock_polls id_polls'),
 'COLD':('NORD2 WIFI WAKE COLD:','valid cpu top power bgfcpu bgftop')}
 for n,line in enumerate(text.splitlines(keepends=True),1):
  if 'NORD2 WIFI WAKE' not in line:kept.append(line);continue
  body=line[line.index('NORD2 WIFI WAKE'):].rstrip('\r\n')
  if not line.endswith('\n') or len(body.encode())>=255 or line.count('NORD2 WIFI')!=1:issues.append('malformed/truncated/interleaved wake line')
  found=False
  for kind,(prefix,fields) in schemas.items():
   value=base.base.nums(body,prefix,fields)
   if value is None:continue
   found=True
   if kind in wake:issues.append('duplicate wake '+kind)
   if any(not(-(2**31)<=v<2**31 if k=='status' else 0<=v<2**32) for k,v in value.items()):issues.append('wake numeric bounds')
   wake[kind]=dict(value=value,line=n,raw=line);break
  if not found:issues.append('wake refusal/fatal/unknown record')
 r=base.parse(''.join(kept).encode());issues+=r['issues']
 try:data.decode('utf8')
 except UnicodeDecodeError:issues.append('invalid UTF8 source')
 if set(wake)!={'RESULT','ID','COLD'}:issues.append('missing complete wake result/ID/cold')
 else:
  a=wake['RESULT']['value'];i=wake['ID']['value'];c=wake['COLD']['value']
  if a!={'status':0,'phase':7,'balanced':1,'ref':0,'halt':0}:issues.append('wake did not complete balanced sleep/ref0')
  if i['id']!=0x20010101 or i['clock']&6!=6 or i['irq']&1!=1:issues.append('wake ID/clock/IRQ contradiction')
  if not(0<=i['ack_polls']<=11 and 1<=i['clock_polls']<=1600 and 1<=i['id_polls']<=10):issues.append('wake poll bounds')
  if c['valid']!=0x1f or any(c[k]&m for k,m in zip(('cpu','top','power','bgfcpu','bgftop'),(1,0x80,4,1,0x80))):issues.append('wake cold proof failed/incomplete')
  p_line=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI PERSIST: retained=' in l),0)
  outer_line=next((n for n,l in enumerate(text.splitlines(),1) if 'NORD2 WIFI: power probe stage=' in l),0)
  if not(p_line<wake['RESULT']['line']<wake['ID']['line']<wake['COLD']['line']<outer_line):issues.append('wake not between retained phase10 and outer summary')
 root=bool(re.search(r'^BSD root: ufsroot0\b',text,re.M));launch=bool(re.search(r'^exec launchd\s*$',text,re.M))
 input_ready=bool(re.search(r'NORD2 INPUT: FT3518 id=54:52 ready=1 powered=1 idle=1 stage=0 irq=[01]; TOPSTATUS=0x[0-9a-f]+ ok=1',text))
 springboard='SpringBoard' in text
 indicators=dict(ufs_root_log=root,launchd_exec_log=launch,input_ready_log=input_ready,springboard_process_log=springboard,human_visible_gui_confirmed=None,human_controls_confirmed=None)
 if not all((root,launch,input_ready,springboard)):issues.append('baseline root/launchd/input/SpringBoard indicators missing')
 r.update(complete=not issues,issues=issues,sha256=hashlib.sha256(data).hexdigest(),wake=wake,health_indicators=indicators,classification='persistent_phase10_and_balanced_AP_wake' if not issues else 'incomplete_or_conflicting',observed_balanced_wake=not issues,observed_combined_success=not issues,network_ready=False)
 r['limits']+=['Balanced wake releases only new AP vote, not retained common power/ownership','Software process/input logs do not prove visible GUI/physical controls; human confirmation remains separate','No new ROM, firmware CPU execution, RF/network or DRAM/MPU permission is inferred']
 return r
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);args=ap.parse_args();r=parse(args.log.read_bytes());print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
