#!/usr/bin/env python3
"""Offline synthetic persistent evidence tests; null checks use fresh copies."""
from pathlib import Path
import ast,importlib.util,json,subprocess,tempfile,shutil
P=Path(__file__).resolve().parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,P/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
p=load('parser','parse-evidence.py');v=load('verifier','verify-image.py')
previous=json.loads((P.parents[1]/'wifi-adie-id/ios/manifest.json').read_text())
expected=' '.join(t for t in previous['commandline'].split() if t not in ('nord2wificommon=1','nord2wifiadieid=1'))+' nord2wifipersist=1'
assert v.COMMANDLINE==expected and expected in (P/'build-image.sh').read_text()
assert json.loads((P/'template.json').read_text())['commandline']==expected
assert not any(t.startswith(('nord2wifiemilegacy=','nord2wificommon=','nord2wifiadieid=')) for t in expected.split())
assert 'nord2wifiemi=0' in expected.split() and len(expected.encode())<608
# Fresh disposable copy only: safe after parent later fills real identity.
with tempfile.TemporaryDirectory() as d:
 q=Path(d)/'a'/'b'/'c'/'ios';q.mkdir(parents=True)
 for file in ['verify-image.py','run-approved-boot.py']:shutil.copyfile(P/file,q/file)
 (q/'identity-required.json').write_text(json.dumps(dict(parent_verified=False)))
 for args in [('verify-image.py','--preflight'),('run-approved-boot.py','--authorized-wifi-persistent-boot')]:
  r=subprocess.run(['python3',str(q/args[0]),args[1]],capture_output=True,text=True)
  assert r.returncode and 'BLOCKED: parent' in r.stderr
source=ast.parse((P.parents[1]/'wifi-adie-id/ios/test-template.py').read_text())
LIVE=next(ast.literal_eval(n.value) for n in source.body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id=='LIVE')
PERSIST='''NORD2 WIFI PERSIST: retained=1 phase=10 id=0x66358a11 handoff=2
NORD2 WIFI: power probe stage=3 id=0x20010101 cfg=0x3 success=1
NORD2 WIFI: retained=1 restored=0 persistent source stage10
'''
FW=''.join(f'NORD2 WIFI FW verified name={name} bytes={size}\n' for name,size in p.FW.items())+'NORD2 WIFI FW probe status=0 elapsed_ticks=123\n'
log=LIVE+PERSIST+FW
assert p.parse(log.encode())['observed_persistent_source_success'],p.parse(log.encode())['issues']
for before,after in [('phase=10','phase=9'),('handoff=2','handoff=1'),('id=0x66358a11','id=0x66358a10'),('cfg=0x3','cfg=0x2'),('retained=1 restored=0','retained=1 restored=1'),('commands=60','commands=59'),('reads=2','reads=1'),('rollback=0','rollback=1'),('status=0 elapsed_ticks','status=-1 elapsed_ticks'),('bytes=973648','bytes=973647')]:
 assert not p.parse(log.replace(before,after).encode())['complete'],(before,after)
for bad in [LIVE+PERSIST,LIVE+FW,PERSIST+LIVE+FW,log+PERSIST,log.rstrip('\n'),log+'panic(cpu 0 caller): synthetic\n',log+'NORD2 WIFI COMMON: status=0 phase=4 snapshots=40 verified=40 error=0 restore_error=0 clean=1\n',log+'NORD2 WIFI: power probe restored=1\n',log+'NORD2 WIFI EMI LEGACY begin queries_max=128 deadline=none effective=unknown\n',log.replace(PERSIST,PERSIST.splitlines()[1]+'\n'+PERSIST.splitlines()[0]+'\n'+PERSIST.splitlines()[2]+'\n')]:
 assert not p.parse(bad.encode())['complete']
assert not p.parse(log.encode()+b'\xff')['complete']
subprocess.run(['sh','-n',str(P/'build-image.sh')],check=True)
print('PASS: exact frozen nonWiFi args/new persistent mode, fresh-copy identity+runner hardfailure, synthetic phase10/outer/FW/UFS/panic/order/diagnostic contradictions')
