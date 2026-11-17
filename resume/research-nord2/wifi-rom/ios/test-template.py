#!/usr/bin/env python3
from pathlib import Path
import importlib.util,json,sys
P=Path(__file__).resolve().parent;ROOT=P.parents[3]
def load(name):
 s=importlib.util.spec_from_file_location(name,P/(name+'.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
v=load('verify-image');p=load('parse-evidence')
sys.path.insert(0,str(ROOT/'tools'));import adt
c=load('rom_claim_static_check');r=adt.Node();ch=adt.Node();ch.props=[('name',0,b'chosen\0'),('nord2-conn-claim',0,bytes(24))];r.children=[ch];c.verify(adt.serialize(r));ch.props[-1]=('nord2-conn-claim',0,b'\1'+bytes(23))
try:c.verify(adt.serialize(r));assert False
except AssertionError:pass
old=json.loads((ROOT/'research/nord2/wifi-wfsys/ios/template.json').read_text())
assert v.COMMANDLINE==old['commandline'].replace(' nord2wifiwfsys=1',' nord2wifirom=1 nord2battery=1')
for f,k in [('identity-required.json','parent_verified'),('prepared-image-required.json','parent_verified'),('authorization-required.json','human_exact_image_authorized')]:assert json.loads((P/f).read_text())[k] is False
try:v.load_identity();assert False
except AssertionError:pass
src=(ROOT/'research/nord2/wifi-wfsys/ios/hardware-live/xnu-live.log').read_text()
lines=[l+'\n' for l in src.splitlines() if 'NORD2 WIFI WFSYS' not in l]
rom='''NORD2 WIFI ROM SMC ARM: group=1 claim=0x8fb00000/0x450000 x0=0xc200050b x1=0 x2=0x1b008fb0 x3=0x1008ff4 x4=0xb6db6d
NORD2 WIFI ROM SMC ARM: group=0 claim=0x8fb00000/0x450000 x0=0xc200050b x1=0 x2=0x1b008fb0 x3=0x8ff4 x4=0xb6d028
NORD2 WIFI ROM CPU ARM: stage=12 attempted=1 placed=1 policy=2/2 raw=0/0 writes=88 reads=176 barriers=2
NORD2 WIFI ROM: status=0 stage=16 policy=1 placed=1 cpu_attempt=1 cpu_release=1 done=0x1d1e halt=0 error=0
NORD2 WIFI ROM OWNER: ap=0 wf=0 sem=0 sem_hw=0 pending=0 clock=0 power=1 protection=1 asleep=1 balanced=1
NORD2 WIFI ROM RECEIPT: phase=7 accepted=2 issued=2 raw_valid=3 raw0=0 raw1=0 backup=1 writes=88 reads=176 barriers=2
NORD2 WIFI ROM OBS: conn=0x20010101 local=0x20010000 clock=0x4006 irq=1 cold=0x1f pwr=0x582fb04 polls=1/1/1/1
'''
pi=next(i for i,l in enumerate(lines) if 'NORD2 WIFI PERSIST: retained=' in l);lines.insert(pi,'NORD2 WIFI ROM PREP: status=0 prepared=1 supply_error=0 claim_source=3\n')
i=next(i for i,l in enumerate(lines) if 'NORD2 WIFI: power probe stage=' in l);lines.insert(i,rom);good=''.join(lines).encode();assert p.parse(good)['complete'],p.parse(good)['issues']
for a,b in [('stage=16','stage=15'),('cpu_release=1','cpu_release=0'),('writes=88','writes=87'),('raw0=0','raw0=0x200c'),('power=1 protection=1','power=0 protection=1'),('polls=1/1/1/1','polls=1/1/1/103')]:assert not p.parse(good.replace(a.encode(),b.encode()))['complete'],a
assert not p.parse(good+rom.encode())['complete']
assert not p.parse(good.replace(b'group=1',b'group=0',1))['complete']
assert not p.parse(good[:-1]+b'\xff')['complete']
assert not p.parse(b'x'*(8*1024*1024+1))['complete']
assert p.parse(good+b'NORD2 BAT RAW: rc=-1 soc_valid=0\n')['battery']['read_error']
assert p.parse(good+b'NORD2 BAT RAW: rc=0 soc_valid=1\n')['battery']['first_valid']
print('PASS private template exact args/false gates, synthetic ROM success, bad receipt/release/owner/polls/duplicate/order/UTF8/size rejection, separate battery evidence; no runner/build/hardware')
