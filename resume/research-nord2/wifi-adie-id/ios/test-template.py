#!/usr/bin/env python3
"""Synthetic/offline templates tests; no device or image build."""
from pathlib import Path
import importlib.util,json,subprocess
P=Path(__file__).resolve().parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,P/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
p=load('parser','parse-evidence.py');v=load('verifier','verify-image.py')
old=json.loads((P.parents[1]/'ufs-live-common-ap/ios/template.json').read_text())
assert v.COMMANDLINE==old['commandline']+' nord2wifiadieid=1'
assert v.COMMANDLINE in (P/'build-image.sh').read_text()
assert json.loads((P/'template.json').read_text())['commandline']==v.COMMANDLINE
for args in [['verify-image.py','--preflight'],['run-approved-boot.py','--authorized-wifi-adie-id-boot']]:
 r=subprocess.run(['python3',str(P/args[0]),*args[1:]],capture_output=True,text=True)
 assert r.returncode and 'BLOCKED: parent' in r.stderr
assert not (P/'manifest.json').exists() and not (P/'payload.bin').exists()
LIVE='''NORD2 UFS X2 LIVE POLICY: armed=1 invalid=0 return_blocked=1 reason=manual-reset-required
NORD2 UFS X2 LIVE: rc=0 phase=5 error=0 active=1 uncertain=0 root_allowed=1 handoff_allowed=0
NORD2 UFS X2 LIVE PROOF: commands=60 sets=18 reads=2 verified=1 rollback=0 original=0
NORD2 UFS X2 LIVE PROFILE: link_fields=pre-transition-baseline return_policy=manual-reset-only
'''
SKIP='''NORD2 WIFI COMMON: skipped=1 reason=owner-or-AP
NORD2 WIFI COMMON AP: owner_ok=0x1ff owner_fail=0 valid=1 failed=0
NORD2 WIFI COMMON AP RAW: pwr=0x12 ack0=0 ack1=0 gals=0 prot=0 rx=0 host=0
NORD2 WIFI: power probe stage=3 id=0x20010101 cfg=0x3 success=1
NORD2 WIFI: power probe restored=1
'''
CLEAN='''NORD2 WIFI COMMON COLD: status=0 valid=7 cpu=0 top=0 power=0
NORD2 WIFI COMMON: status=0 phase=4 snapshots=40 verified=40 error=0 restore_error=0 clean=1
NORD2 WIFI COMMON RESTORE: touched=0xffffffffff restored=0xffffffffff valid=0x3fff field=14
NORD2 WIFI COMMON TIME: ticks=100 frequency=13000000
NORD2 WIFI: power probe stage=3 id=0x20010101 cfg=3 success=1
NORD2 WIFI: power probe restored=1
'''
DIRTY='''NORD2 WIFI COMMON COLD: status=0 valid=7 cpu=0 top=0 power=0
NORD2 WIFI COMMON: status=-1 phase=3 snapshots=40 verified=1 error=-4 restore_error=-6 clean=0
NORD2 WIFI COMMON RESTORE: touched=3 restored=1 valid=0x3fff field=14
NORD2 WIFI COMMON TIME: ticks=100 frequency=13000000
NORD2 WIFI COMMON: dirty restoration; retain power/ownership; refuse handoff
'''

ADIE="""NORD2 WIFI ADIE: attempted=1 id=0x66358a10 valid=1 known=1 complete=1 clean=1 error=0 restore_error=0
NORD2 WIFI ADIE RESTORE: strap_touched=0xf restored=0xf clean=1 error=0 sem_owned=0 inner_clean=1
NORD2 WIFI ADIE CLOCK: attempted=1 ok=1 raw=6 irq=1 polls=2
"""
log=(LIVE+CLEAN.replace('NORD2 WIFI COMMON: status=',ADIE+'NORD2 WIFI COMMON: status=')).encode()
assert p.parse(log)['observed_combined_success'],p.parse(log)['issues']
for before,after in [('verified=40','verified=39'),('raw=6','raw=2'),('irq=1','irq=0'),('polls=2','polls=1601'),('sem_owned=0','sem_owned=1'),('known=1','known=0'),('valid=1 known=1','valid=0 known=0'),('commands=60','commands=59'),('rollback=0','rollback=1'),('clean=1 error=0 restore_error=0','clean=1 error=-6 restore_error=0')]:
 assert not p.parse(log.replace(before.encode(),after.encode()))['observed_combined_success'],(before,after)
assert not p.parse((LIVE+CLEAN).encode())['complete']
assert not p.parse(log+ADIE.encode())['complete']
assert not p.parse(log.rstrip(b'\n'))['complete']
# AP field4 bit14 alone no longer fails; bit13 still does.
ap=SKIP.replace('valid=1 failed=0','valid=0x1f failed=4').replace('pwr=0x12 ack0=0 ack1=0 gals=0 prot=0','pwr=0xd ack0=2 ack1=2 gals=1 prot=0x2000')
assert p.parse((LIVE+ap).encode())['complete']
assert not p.parse((LIVE+ap.replace('prot=0x2000','prot=0x4000')).encode())['complete']
assert p.parse((LIVE+DIRTY).encode())['complete']
print('PASS: identity and runner blocked; exact unchanged base args + opt-in; synthetic ADIE/common/UFS/AP cases')
# Ambiguous command: retain sem/strap/common ownership and require dirty panic.
dirty_adie=ADIE.replace('id=0x66358a10 valid=1 known=1 complete=1 clean=1 error=0','id=0 valid=0 known=0 complete=0 clean=0 error=-6').replace('restored=0xf clean=1 error=0 sem_owned=0 inner_clean=1','restored=0 clean=0 error=-6 sem_owned=1 inner_clean=0')
dirty_common=DIRTY.replace('phase=3 snapshots=40 verified=1 error=-4 restore_error=-6','phase=2 snapshots=40 verified=40 error=-10 restore_error=0').replace('touched=3 restored=1','touched=0xffffffffff restored=0')
dirty_log=(LIVE+dirty_common.replace('NORD2 WIFI COMMON: status=',dirty_adie+'NORD2 WIFI COMMON: status=')).encode()
assert p.parse(dirty_log)['complete'],p.parse(dirty_log)['issues']
assert not p.parse(dirty_log)['observed_combined_success']
assert not p.parse(dirty_log+b'NORD2 WIFI: power probe restored=1\n')['complete']
# Preparation refusal occurs before common snapshots; clean outer unwind remains valid.
prepare=CLEAN.replace('status=0 phase=4 snapshots=40 verified=40 error=0','status=-1 phase=0 snapshots=0 verified=0 error=-12').replace('touched=0xffffffffff restored=0xffffffffff','touched=0 restored=0')
prepare=prepare.replace('NORD2 WIFI COMMON: status=','NORD2 WIFI ADIE: skipped=1 prepare_error=-9 snapshots=0\nNORD2 WIFI COMMON: status=')
assert p.parse((LIVE+prepare).encode())['complete'],p.parse((LIVE+prepare).encode())['issues']
print('PASS: synthetic ambiguous retention and preparation refusal')
