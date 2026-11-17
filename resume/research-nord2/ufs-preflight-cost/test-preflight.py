#!/usr/bin/env python3
import ast,hashlib,importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('preflight',HERE/'parse-preflight.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
tree=ast.parse((HERE.parent/'ufs-two-lane/hardware-candidate/test-parser.py').read_text())
raw=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='raw' for t in n.targets))
def report(ef=0,of=0):
 ev=[1,1,0x10f,0x10f]+[0]*10
 ov=[1,1,0,1,0,0,0,2,0,0,0,0xfffffe0000001000,0xfffffe0010000000,0x145000000,3153920,28082168,0,1,0,0,0x45000000,1,1]
 em=(1<<14)-1;om=(1<<23)-1
 if ef:
  em=(1<<ef)-1
  if ef==1:ev[0]=0
  elif ef==2:ev[1]=0
  elif ef==3:ev[2]=0
  elif ef==4:ev[3]=0x30
  else:ev[ef-1]=1
  ev[ef:]=[0]*(14-ef)
 if of:
  assert ef==1
  om=(1<<of)-1
  i=of-1
  if i in (0,1,3,11,17,22):ov[i]=0
  elif i==7:ov[i]=1
  elif i in (12,13,14,15):ov[i]+=1
  elif i in (20,21):ov[i]^=1
  else:ov[i]=1
  ov[of:]=[0]*(23-of)
 extra=f'NORD2 UFS X2 PREFLIGHT: valid={em:#x} fail={ef} owned_valid={om:#x} owned_fail={of}\n'
 for start in range(0,14,4):extra+='NORD2 UFS X2 GATE '+str(start)+': '+' '.join(f'v{i}={ev[i]:#x}' for i in range(start,min(start+4,14)))+'\n'
 for start in range(0,23,3):extra+='NORD2 UFS X2 OWN '+str(start)+': '+' '.join(f'v{i}={ov[i]:#x}' for i in range(start,min(start+3,23)))+'\n'
 base=raw
 if ef:base=base.replace('rc=0 phase=4 error=0 commands=115 sets=37 reads=3','rc=-1 phase=1 error=-2 commands=0 sets=0 reads=0').replace('rollback=1 original=1 engine_root=1 engine_handoff=1','rollback=0 original=0 engine_root=0 engine_handoff=0')
 return base+extra
source=(HERE.parents[2]/'shim/nord2-xnu/ufs_two_lane_adapter.inc').read_text()
for label in m.EXTRA:assert 'NORD2 UFS X2 '+label+':' in source
success=report();r=m.parse(success.encode());assert r['complete'] and r['success'],r['issues']
assert r['source_sha256']==hashlib.sha256(success.encode()).hexdigest() and r['raw_text']==success
assert m.parse(success.replace('\n','\r\n').encode())['complete']
for ef in range(2,15):
 r=m.parse(report(ef).encode());assert r['complete'] and not r['success'],r['issues'];assert r['failure']['engine_index']==ef-1
 assert r['observations']['engine'][ef-1]['state']=='observed'
 if ef<14:assert r['observations']['engine'][ef]['state']=='UNREAD' and r['observations']['engine'][ef]['value'] is None
for of in range(1,24):
 r=m.parse(report(1,of).encode());assert r['complete'] and not r['success'],(of,r['issues'])
 assert r['failure']['ownership_predicate']==m.OWN[of-1]
lines=success.splitlines(keepends=True)
for i in range(20):
 for bad in [''.join(lines[:i]+lines[i+1:]),''.join(lines[:i]+[lines[i]]+lines[i:]),''.join(lines[:i]+[lines[i][:30]+'\n']+lines[i+1:])]:
  assert not m.parse(bad.encode())['complete'],(i,bad)
for bad in [success[:-1],success[:-2],success+'NORD2 UFS X2 UNKNOWN: a=1\n',success.replace('owned_valid=0x7fffff','owned_valid=0x7ffffe'),success.replace('valid=0x3fff fail=0','valid=0x7 fail=0'),success.replace('v1=0x1','v1=0x0',1),success.replace('v0=0xffff','v0=0xffff',1).replace('v20=0x45000000','v20=0x45000001'),success.replace('v22=0x1','v22=18446744073709551616'),report(2).replace('v3=0x0','v3=0x1'),report(1,5).replace('owned_fail=5','owned_fail=4'),report(2).replace('commands=0','commands=1'),success.replace('GATE 0:','GATE 0: NORD2 UFS X2 OWN 0:')]:
 assert not m.parse(bad.encode())['complete'],bad
# Preflight-only cannot borrow success from absent seven-line engine report.
assert not m.parse(''.join(lines[7:]).encode())['success']
print('PASS:20-line strict parser, all14engine/23ownership outcomes, observed-vs-unread, masks/firstfailure, raw/SHA, success delegated, missing/duplicate/truncation/interleaving/overflow')

assert not m.parse(success.encode()+b'\xff')['complete']
# Policy is explicit and persisted; historical register data never selects it.
import base64,json,subprocess,sys,tempfile
historical_path=HERE/'diagnostic/report-snapshot1.json'
if historical_path.exists():
 historical=json.loads(historical_path.read_text());capture=base64.b64decode(historical['raw_base64'])
 h=m.parse(capture);assert h['complete'] and not h['success'] and h['failure']['engine_index']==8,h['issues']
 assert h['source_sha256']==historical['source_sha256']
 assert h['source_policy']['name']=='original-8702715-zero-only'
 assert not m.parse(capture,allow_line_reset=True)['complete']
 live=(HERE/'diagnostic/hardware-live/xnu-live.log').read_bytes()
 # Growing raw log may contain additional material; decode it independently.
 live_old=m.parse(live);assert live_old['complete'] and live_old['failure']['engine_index']==8,live_old['issues']
 assert not m.parse(live,allow_line_reset=True)['complete']
future=success.replace('GATE 8: v8=0x0','GATE 8: v8=0x80000010')
assert not m.parse(future.encode())['complete']
f=m.parse(future.encode(),allow_line_reset=True)
assert f['complete'] and f['success'] and f['source_policy']['allow_line_reset'],f['issues']
assert f['observations']['engine'][8]['value']==0x80000010
assert 'LINERESET' in f['observations']['engine'][8]['predicate']
for v in ('0x10','0x80000000','0x80000011','0x80000030','0xffffffff'):
 assert not m.parse(future.replace('v8=0x80000010','v8='+v).encode(),allow_line_reset=True)['complete']
# New policy never allows bit4 in other error registers or unknown status.
assert not m.parse(future.replace('v9=0x0','v9=0x80000010').encode(),allow_line_reset=True)['complete']
with tempfile.TemporaryDirectory() as td:
 path=Path(td)/'future.log';path.write_text(future)
 for flag,code in (([],2),(['--allow-line-reset'],0)):
  cli=subprocess.run([sys.executable,str(HERE/'parse-preflight.py'),*flag,str(path)],capture_output=True,text=True)
  assert cli.returncode==code
  output=json.loads(cli.stdout);assert output['source_policy']['allow_line_reset']==bool(flag)
print('PASS explicit policy: captured8702715 valid only old; futurepassedLINERESET only flag; CLI/outputpolicy recorded; no historical result overwritten')
