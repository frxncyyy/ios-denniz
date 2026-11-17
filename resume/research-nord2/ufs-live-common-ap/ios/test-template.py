#!/usr/bin/env python3
from pathlib import Path
import importlib.util,json,subprocess,re
P=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('v',P/'verify-image.py');v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
s=importlib.util.spec_from_file_location('p',P/'parse-evidence.py');p=importlib.util.module_from_spec(s);s.loader.exec_module(p)
assert all(x is None for k,x in json.loads((P/'identity-required.json').read_text()).items() if k!='parent_verified')
r=subprocess.run(['python3',str(P/'verify-image.py'),'--preflight'],capture_output=True,text=True);assert r.returncode and 'BLOCKED: parent' in r.stderr
assert not (P/'manifest.json').exists() and not (P/'payload.bin').exists()
meta=v.request_metadata();assert not meta['automatic_rescue_fallback_allowed'] and not meta['negotiated_x1_restoration_required'];assert meta['root_allowed_only_after']==dict(phase=5,commands=60,sets=18,reads=2,verified=1,rollback=0,original=0)
tokens=v.COMMANDLINE.split();keys=[x.split('=')[0] for x in tokens];assert len(keys)==len(set(keys)) and len(v.COMMANDLINE)<608
assert 'nord2ufs2live=1' in tokens and 'nord2ufs2lane' not in keys and 'nord2wifiemi=0' in tokens and 'nord2wificommon=1' in tokens
shell=(P/'build-image.sh').read_text();assert v.COMMANDLINE in shell
subprocess.run(['sh','-n',str(P/'build-image.sh')],check=True)
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
# All success-shaped evidence here is SYNTHETIC parser testing, not hardware.
assert p.parse((LIVE+CLEAN).encode())['observed_combined_success']
assert p.parse((LIVE+SKIP).encode())['complete'] and not p.parse((LIVE+SKIP).encode())['observed_combined_success']
assert p.parse((LIVE+DIRTY).encode())['complete'] and not p.parse((LIVE+DIRTY).encode())['observed_combined_success']
for old,new in [('phase=5','phase=4'),('commands=60','commands=59'),('sets=18','sets=36'),('reads=2','reads=3'),('rollback=0','rollback=1'),('original=0','original=1'),('handoff_allowed=0','handoff_allowed=1'),('return_blocked=1','return_blocked=0'),('active=1','active=0')]:assert not p.parse((LIVE.replace(old,new)+CLEAN).encode())['complete'],(old,new)
for bad in [SKIP.replace('valid=1','valid=3'),SKIP.replace('pwr=0x12','pwr=0xd'),SKIP.replace('ack0=0','ack0=2'),SKIP.replace('owner_ok=0x1ff','owner_ok=0xff'),SKIP.replace('failed=0','failed=7'),SKIP.replace('owner_fail=0','owner_fail=2'),SKIP.replace('NORD2 WIFI COMMON AP RAW:','BAD RAW:'),SKIP+SKIP,SKIP.rstrip('\n')]:assert not p.parse((LIVE+bad).encode())['complete']
owner=SKIP.replace('owner_ok=0x1ff owner_fail=0 valid=1 failed=0','owner_ok=1 owner_fail=2 valid=0 failed=7').replace('pwr=0x12','pwr=0');assert p.parse((LIVE+owner).encode())['complete']
assert not p.parse((LIVE+CLEAN+SKIP).encode())['complete']
assert not p.parse((LIVE+'BSD root: ufsroot0\n').encode().replace(b'phase=5',b'phase=4'))['complete']
assert not p.parse(('BSD root: ufsroot0\n'+LIVE).encode())['complete']
ref='NORD2 UFS X2 LIVE POLICY: armed=1 invalid=1 return_blocked=1 reason=manual-reset-required\nNORD2 UFS X2 LIVE REFUSED: error=22 root_allowed=0 return_blocked=1 manual_reset=1\n';assert p.parse(ref.encode())['complete']
runtime='NORD2 UFS X2 LIVE: runtime I/O failed; retain DMA/ownership; manual reset required\n';assert p.parse((LIVE+CLEAN+runtime).encode())['complete'] and not p.parse((LIVE+CLEAN+runtime).encode())['observed_combined_success']

for i in range(7):
 masks=[31,2,2,1,0x6000,4,0x20];wants=[13,2,2,1,0,0,0]
 vals=wants[:i]+[0 if wants[i] else masks[i]]+[0]*(6-i)
 a=f'NORD2 WIFI COMMON: skipped=1 reason=owner-or-AP\nNORD2 WIFI COMMON AP: owner_ok=0x1ff owner_fail=0 valid={(1<<(i+1))-1} failed={i}\nNORD2 WIFI COMMON AP RAW: '+' '.join(f'{k}={v}' for k,v in zip('pwr ack0 ack1 gals prot rx host'.split(),vals))+'\nNORD2 WIFI: power probe stage=3 id=0x20010101 cfg=3 success=1\nNORD2 WIFI: power probe restored=1\n'
 assert p.parse((LIVE+a).encode())['complete'],i
for i in range(1,10):
 a=SKIP.replace('owner_ok=0x1ff owner_fail=0 valid=1 failed=0',f'owner_ok={(1<<(i-1))-1} owner_fail={i} valid=0 failed=7').replace('pwr=0x12','pwr=0')
 assert p.parse((LIVE+a).encode())['complete'],i
for uncertain,rollback,original,phase,error in [(0,1,1,4,-8),(1,0,0,2,-6)]:
 failure=f'NORD2 UFS X2 LIVE POLICY: armed=1 invalid=0 return_blocked=1 reason=manual-reset-required\nNORD2 UFS X2 LIVE: rc=-1 phase={phase} error={error} active=0 uncertain={uncertain} root_allowed=0 handoff_allowed=0\nNORD2 UFS X2 LIVE PROOF: commands=61 sets=18 reads=2 verified=0 rollback={rollback} original={original}\nNORD2 UFS X2 LIVE PROFILE: link_fields=pre-transition-baseline return_policy=manual-reset-only\nNORD2 UFS X2 STOP: rc=-1 phase={phase} error={error} commands=61 sets=18 reads=2\nNORD2 UFS X2 STATE: uncertain={uncertain} rollback={rollback} original={original} engine_root=0 engine_handoff=0\nNORD2 UFS X2 LAST: attr=0 result=0 is=0 hcs=0 root_published=0 return_blocked=1\n'
 assert p.parse(failure.encode())['complete'] and not p.parse(failure.encode())['observed_combined_success']
 assert not p.parse((failure+'BSD root: ufsroot0\n').encode())['complete']
 assert not p.parse(failure.replace('root_published=0','root_published=1').encode())['complete']


# Frozen source can panic during initial TUR before phase5/proof/publication.
for msg in [runtime,'NORD2 UFS X2 LIVE: uncertain before I/O; retain DMA/ownership; manual reset required\n']:
 pre=LIVE.splitlines(keepends=True)[0]+msg
 rr=p.parse(pre.encode());assert rr['complete'] and rr['live']['classification']=='pre_root_io_failure_manual_reset' and not rr['observed_combined_success']
 assert not p.parse(msg.encode())['complete']
 assert not p.parse(pre.replace('invalid=0','invalid=1').encode())['complete']
 assert not p.parse((pre+'BSD root: ufsroot0\n').encode())['complete']
 assert not p.parse((pre+CLEAN).encode())['complete']
 assert not p.parse((pre+LIVE.splitlines(keepends=True)[2]).encode())['complete']
 assert not p.parse((pre+msg).encode())['complete']
 post=p.parse((LIVE+CLEAN+msg).encode());assert post['complete'] and post['live']['classification']=='runtime_failure_manual_reset' and not post['observed_combined_success']
product=v.ROOT/v.PRODUCT_ADT;assert product.is_file() and v.sha(product.read_bytes())==v.PRODUCT_ADT_SHA256

# Real old hardware confirms only skip/outer restoration; no fabricated AP/live success.
real=P.parents[1]/'ufs-linereset-common/ios/hardware-live/xnu-live.log'
assert real.exists();e=p.parse(real.read_bytes());assert not e['observed_combined_success'] and not e['complete']
print('PASS offline placeholders hardfail; exactargs/metadata; synthetic clean/skipped/dirty/live/pre-root+post-proof IOpanic contradictions; real oldskip remains insufficient; no imagebuild or observedsuccess claim')
