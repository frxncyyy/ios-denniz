#!/usr/bin/env python3
from pathlib import Path
import importlib.util,json,subprocess,tempfile,shutil,re
P=Path(__file__).resolve().parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,P/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
p=load('parser','parse-evidence.py');v=load('verifier','verify-image.py')
previous=json.loads((P.parents[1]/'wifi-ap-wake/ios/manifest.json').read_text());expected=previous['commandline'].replace(' nord2wifiwake=1',' nord2wifiwfsys=1')
assert v.COMMANDLINE==expected and expected in (P/'build-image.sh').read_text()
(P/'template.json').write_text(json.dumps(dict(planned_image=str(v.IMAGE.relative_to(v.ROOT)),commandline=expected,commandline_bytes=len(expected),metadata=v.request_metadata()),indent=2)+'\n')
with tempfile.TemporaryDirectory() as d:
 q=Path(d)/'a'/'b'/'c'/'ios';q.mkdir(parents=True)
 for file in ['verify-image.py','run-approved-boot.py']:shutil.copyfile(P/file,q/file)
 (q/'identity-required.json').write_text(json.dumps(dict(parent_verified=False)))
 for args in [('verify-image.py','--preflight'),('run-approved-boot.py','--authorized-wifi-wfsys-boot')]:
  result=subprocess.run(['python3',str(q/args[0]),args[1]],capture_output=True,text=True)
  assert result.returncode and 'BLOCKED: parent' in result.stderr
LIVE='''NORD2 UFS X2 LIVE POLICY: armed=1 invalid=0 return_blocked=1 reason=manual-reset-required
NORD2 UFS X2 LIVE: rc=0 phase=5 error=0 active=1 uncertain=0 root_allowed=1 handoff_allowed=0
NORD2 UFS X2 LIVE PROOF: commands=60 sets=18 reads=2 verified=1 rollback=0 original=0
NORD2 UFS X2 LIVE PROFILE: link_fields=pre-transition-baseline return_policy=manual-reset-only
BSD root: ufsroot0, major 1, minor 0
exec launchd
'''
WFSYS='''NORD2 WIFI WFSYS: status=0 stage=11 local_id=1 cpu_held=1 balanced=1 halt=0 dirty=0 unverified=0 error=0
NORD2 WIFI WFSYS OWNER: ap=0 wf=0 sem=0 sem_hw=0 sem_pending=0 clock=0 power=0 protection=0 asleep=1
NORD2 WIFI WFSYS ID: conn=0x20010101 local=0x20010000 clock=0x4006 irq=0x101 initial_afe=0x80000000 final_power=0x582fb00
NORD2 WIFI WFSYS COLD: valid=0x1f cpu=0 top=0x12 power=0x582fb00 bgfcpu=0 bgftop=0x12
NORD2 WIFI WFSYS POLLS: ap=3 wf=1 cfg=2 power=1 protection=4 id=1 off=1 sem=1 sem_off=1 clock=6
'''
PERSIST='NORD2 WIFI PERSIST: retained=1 phase=10 id=0x66358a11 handoff=2\n'+WFSYS+'NORD2 WIFI: power probe stage=3 id=0x20010101 cfg=0x3 success=1\nNORD2 WIFI: retained=1 restored=0 persistent source stage10\n'
FW=''.join(f'NORD2 WIFI FW verified name={name} bytes={size}\n' for name,size in p.base.FW.items())+'NORD2 WIFI FW probe status=0 elapsed_ticks=123\n'
HEALTH='NORD2 INPUT: FT3518 id=54:52 ready=1 powered=1 idle=1 stage=0 irq=0; TOPSTATUS=0xe ok=1\nSWEET AKS: SpringBoard synthetic process indicator\n'
log=LIVE+PERSIST+FW+HEALTH
r=p.parse(log.encode());assert r['complete'] and r['observed_cpu_held_wfsys_cycle'],r['issues'];assert r['health_indicators']['human_visible_gui_confirmed'] is None
cases=[('stage=11','stage=10'),('local_id=1','local_id=0'),('cpu_held=1','cpu_held=0'),('balanced=1','balanced=0'),('halt=0','halt=1'),('dirty=0','dirty=1'),('unverified=0','unverified=1'),('error=0','error=-7'),('sem_pending=0','sem_pending=1'),('asleep=1','asleep=0'),('local=0x20010000','local=0x20010101'),('clock=0x4006','clock=0x4002'),('irq=0x101','irq=0x100'),('initial_afe=0x80000000','initial_afe=0x80100000'),('final_power=0x582fb00','final_power=0x582fb04'),('valid=0x1f','valid=0xf'),('cpu=0 top','cpu=1 top'),('bgftop=0x12','bgftop=0x92'),('ap=3 wf=1','ap=13 wf=1'),('cfg=2 power=1','cfg=1 power=1'),('protection=4 id=1','protection=409 id=1'),('sem_off=1 clock=6','sem_off=1001 clock=6'),('clock=6\n','clock=9601\n'),('commands=60','commands=59'),('handoff=2','handoff=1')]
for before,after in cases:assert not p.parse(log.replace(before,after).encode())['complete'],(before,after)
for bad in [log.replace(WFSYS,''),log+WFSYS,log.replace(WFSYS,'\n'.join(reversed(WFSYS.strip().splitlines()))+'\n'),WFSYS+log.replace(WFSYS,''),log.replace(HEALTH,''),log+'NORD2 WIFI WFSYS: uncertain stage7; manual reset\n',log+'panic(cpu 0 caller): synthetic\n',log+WFSYS.rstrip('\n'),log+'NORD2 WIFI WAKE: status=0 phase=7 balanced=1 ref=0 halt=0\n']:
 assert not p.parse(bad.encode())['complete']
assert not p.parse(log.encode()+b'\xff')['complete']
# Native log format maximum uint32/int32 values stay below console truncation.
bind=(v.ROOT/'shim/nord2-xnu/wifi_wfsys_bind.inc').read_text()
for fmt in re.findall(r'printf\("(NORD2 WIFI WFSYS[^"\\]*(?:\\.[^"\\]*)*)"',bind):
 if '%' not in fmt:continue
 worst=re.sub(r'%#x','0xffffffff',fmt);worst=re.sub(r'%u','4294967295',worst);worst=re.sub(r'%d','-2147483648',worst)
 assert len(worst.replace('\\n','\n').encode())<255,(len(worst),worst)
subprocess.run(['sh','-n',str(P/'build-image.sh')],check=True)
print('PASS exact49args replacing only diagnostic flag; false identity gates; WFSYS result/owner/ID/cold/poll/order/health/fatal/malformed tests; bounded native log lines')
