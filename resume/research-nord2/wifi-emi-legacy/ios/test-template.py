#!/usr/bin/env python3
"""Synthetic offline evidence tests only; no hardware claims."""
from pathlib import Path
import ast,importlib.util,json,subprocess
P=Path(__file__).resolve().parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,P/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
p=load('parser','parse-evidence.py');v=load('verifier','verify-image.py')
previous=json.loads((P.parents[1]/'wifi-adie-id/ios/manifest.json').read_text())
assert v.COMMANDLINE==previous['commandline']+' nord2wifiemilegacy=1'
assert v.COMMANDLINE in (P/'build-image.sh').read_text()
assert v.COMMANDLINE.split().count('nord2wifiemi=0')==1
assert len(v.COMMANDLINE.encode())<608
assert json.loads((P/'template.json').read_text())['commandline']==v.COMMANDLINE
for args in [('verify-image.py','--preflight'),('run-approved-boot.py','--authorized-wifi-emi-legacy-boot')]:
 r=subprocess.run(['python3',str(P/args[0]),args[1]],capture_output=True,text=True)
 assert r.returncode and 'BLOCKED: parent' in r.stderr
assert not (P/'manifest.json').exists() and not (P/'payload.bin').exists()
fixtures={}
for node in ast.parse((P.parents[1]/'wifi-adie-id/ios/test-template.py').read_text()).body:
 if isinstance(node,ast.Assign) and isinstance(node.targets[0],ast.Name) and node.targets[0].id in ('LIVE','CLEAN','ADIE'):
  fixtures[node.targets[0].id]=ast.literal_eval(node.value)
BASE=fixtures['LIVE']+fixtures['CLEAN'].replace('NORD2 WIFI COMMON: status=',fixtures['ADIE']+'NORD2 WIFI COMMON: status=')
def evidence(fail=None,status=2,raw=None,error=-3,value=0):
 lines=[p.BEGIN];valid=[0]*4
 for i in range(128):
  s=1 if fail is None or i<fail else status if i==fail else 0
  registers=[value,0x1234567887654321,2,3] if s==1 else (raw or [2**64-3,1,2,3]) if s in (2,3) else [0]*4
  e=error if s==2 else 0
  if s==1:valid[i//32]|=1<<(i%32)
  lines.append(f'{p.PREFIX} index={i} offset={p.offset(i):#x} status={s} error={e} raw='+ '/'.join(f'{x:x}' for x in registers))
 lines.append(f'{p.PREFIX} end collector={0 if fail is None else 1} issued={128 if fail is None else fail+1} valid='+ '/'.join(f'{x:#x}' if x else '0' for x in valid)+' elapsed_ticks=123 effective=unknown')
 return '\n'.join(lines)+'\n'
for value in (0,2,0xffffffff-4):
 log=evidence(value=value);r=p.parse((BASE+log).encode());assert r['observed_combined_success'],r['issues']
 if value in (0,2):assert r['legacy']['pattern']
for i in range(128):
 for e in range(1,5):
  for raw in (2**64-e,2**32-e):
   r=p.parse((BASE+evidence(i,2,[raw,1,2,3],-e)).encode());assert r['complete'] and not r['observed_combined_success'],r['issues']
 for status,raw in [(3,[2**32,1,2,3]),(4,None)]:
  r=p.parse((BASE+evidence(i,status,raw)).encode());assert r['complete'] and not r['observed_combined_success'],r['issues']
log=evidence();lines=log.splitlines(keepends=True)
for bad in [log+lines[-1],log+lines[1],''.join(lines[:20]+lines[21:]),''.join(lines[1:]),log.rstrip('\n'),log.replace('offset=0x100','offset=0x104',1),log.replace('valid=0xffffffff','valid=0xfffffffe',1),log.replace('issued=128','issued=127'),log.replace('status=1','status=0',1),log.replace('error=0','error=-3',1),log.replace('raw=0/','raw=100000000/',1)]:
 assert not p.parse((BASE+bad).encode())['complete']
assert not p.parse(BASE.encode())['complete']
assert not p.parse((BASE+evidence(3).replace('index=4 offset=0x104 status=0','index=4 offset=0x104 status=1')).encode())['complete']
print('PASS: exact args/null identity blocked; synthetic all128/constant patterns/1280first failures; malformed/duplicate/missing/mask/firstfail contradictions')
