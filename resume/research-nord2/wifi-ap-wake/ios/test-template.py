#!/usr/bin/env python3
"""Pure synthetic evidence/fresh-copy nullidentity checks, no device/build."""
from pathlib import Path
import ast,importlib.util,json,subprocess,tempfile,shutil
P=Path(__file__).resolve().parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,P/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
p=load('parser','parse-evidence.py');v=load('verifier','verify-image.py')
previous=json.loads((P.parents[1]/'wifi-persistent/ios/manifest.json').read_text());expected=previous['commandline']+' nord2wifiwake=1'
assert v.COMMANDLINE==expected and expected in (P/'build-image.sh').read_text()
assert json.loads((P/'template.json').read_text())['commandline']==expected
with tempfile.TemporaryDirectory() as d:
 q=Path(d)/'a'/'b'/'c'/'ios';q.mkdir(parents=True)
 for file in ['verify-image.py','run-approved-boot.py']:shutil.copyfile(P/file,q/file)
 (q/'identity-required.json').write_text(json.dumps(dict(parent_verified=False)))
 for args in [('verify-image.py','--preflight'),('run-approved-boot.py','--authorized-wifi-ap-wake-boot')]:
  r=subprocess.run(['python3',str(q/args[0]),args[1]],capture_output=True,text=True)
  assert r.returncode and 'BLOCKED: parent' in r.stderr
LIVE='''NORD2 UFS X2 LIVE POLICY: armed=1 invalid=0 return_blocked=1 reason=manual-reset-required
NORD2 UFS X2 LIVE: rc=0 phase=5 error=0 active=1 uncertain=0 root_allowed=1 handoff_allowed=0
NORD2 UFS X2 LIVE PROOF: commands=60 sets=18 reads=2 verified=1 rollback=0 original=0
NORD2 UFS X2 LIVE PROFILE: link_fields=pre-transition-baseline return_policy=manual-reset-only
BSD root: ufsroot0, major 1, minor 0
exec launchd
'''
WAKE='''NORD2 WIFI WAKE: status=0 phase=7 balanced=1 ref=0 halt=0
NORD2 WIFI WAKE ID: id=0x20010101 clock=0x4006 irq=0x101 ack_polls=0 clock_polls=1 id_polls=1
NORD2 WIFI WAKE COLD: valid=0x1f cpu=0x10 top=0x20 power=0x8 bgfcpu=0x10 bgftop=0x20
'''
PERSIST='NORD2 WIFI PERSIST: retained=1 phase=10 id=0x66358a11 handoff=2\n'+WAKE+'NORD2 WIFI: power probe stage=3 id=0x20010101 cfg=0x3 success=1\nNORD2 WIFI: retained=1 restored=0 persistent source stage10\n'
FW=''.join(f'NORD2 WIFI FW verified name={name} bytes={size}\n' for name,size in p.base.FW.items())+'NORD2 WIFI FW probe status=0 elapsed_ticks=123\n'
HEALTH='NORD2 INPUT: FT3518 id=54:52 ready=1 powered=1 idle=1 stage=0 irq=0; TOPSTATUS=0xe ok=1\nSWEET AKS: SpringBoard synthetic process indicator\n'
log=LIVE+PERSIST+FW+HEALTH
r=p.parse(log.encode());assert r['complete'] and r['observed_balanced_wake'],r['issues'];assert r['health_indicators']['human_visible_gui_confirmed'] is None
for before,after in [('phase=7','phase=6'),('balanced=1','balanced=0'),('ref=0','ref=1'),('halt=0','halt=1'),('clock=0x4006','clock=0x4002'),('irq=0x101','irq=0x100'),('ack_polls=0','ack_polls=12'),('clock_polls=1','clock_polls=1601'),('id_polls=1','id_polls=11'),('valid=0x1f','valid=0xf'),('cpu=0x10','cpu=0x11'),('bgftop=0x20','bgftop=0xa0'),('commands=60','commands=59'),('handoff=2','handoff=1')]:
 assert not p.parse(log.replace(before,after).encode())['complete'],(before,after)
for bad in [log.replace(WAKE,''),log+WAKE,log.replace(WAKE,WAKE.splitlines()[1]+'\n'+WAKE.splitlines()[0]+'\n'+WAKE.splitlines()[2]+'\n'),WAKE+log.replace(WAKE,''),log.replace(HEALTH,''),log+'NORD2 WIFI WAKE: uncertain phase3; retain wake/common; manual reset\n',log+'panic(cpu 0 caller): synthetic\n',log+WAKE.rstrip('\n')]:
 assert not p.parse(bad.encode())['complete']
assert not p.parse(log.encode()+b'\xff')['complete']
subprocess.run(['sh','-n',str(P/'build-image.sh')],check=True)
print('PASS: exactc6cbargs+wake1/fresh-copyidentityblocked; syntheticphase10/wakebounds/maskedcold/once-order-health/panicpartial evidence')
