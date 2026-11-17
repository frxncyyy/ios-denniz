#!/usr/bin/env python3
from pathlib import Path
import importlib.util
P=Path(__file__).resolve().parent;ROOT=P.parents[3]
s=importlib.util.spec_from_file_location('dma_parse',P/'parse-evidence.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
base=(ROOT/'research/nord2/wifi-rom/ios/hardware-live/xnu-live.log').read_text()
# Explicit synthetic poolcases over historical ROM-success log, not newhardware.
def fixture(inherited=False,unstable=False,reserved=False):
 first='1/0x18024208/0x1/0xf' if inherited else '0/0/0/0'
 lines=['NORD2 WIFI DMA PREP: status=0 claim=1 base=0x90000000 bytes=0xf20000 layout=0x20000','NORD2 WIFI DMA ARM: stage=5 action=preflight base=0x18024000 rings=27']
 if not inherited:lines+=['NORD2 WIFI DMA SMC ARM: group=1 base=0x90000000 bytes=0xf20000 x0=0xc200050b x1=0 x2=0x12009000 x3=0x10090f1 x4=0xb6db6d','NORD2 WIFI DMA SMC ARM: group=0 base=0x90000000 bytes=0xf20000 x0=0xc200050b x1=0 x2=0x12009000 x3=0x90f1 x4=0x80b6da28','NORD2 WIFI DMA ARM: stage=8 action=map base=0x90000000 bytes=0xf20000','NORD2 WIFI DMA ARM: stage=9 action=zero base=0x90000000 bytes=0x20000']
 lines+=[f'NORD2 WIFI DMA: status={int(inherited)} stage=12 inherited={int(inherited)} preflight=1 idle={int(not inherited)} stable={int(not unstable)} reads=112 halt=0 error=0', 'NORD2 WIFI DMA SET: attempted=0 accepted=0 raw_valid=0 raw1=0 raw0=0 mapped=0 zero=0 verified=0 words=0/0' if inherited else 'NORD2 WIFI DMA SET: attempted=3 accepted=2 raw_valid=3 raw1=0 raw0=0 mapped=1 zero=1 verified=1 words=32768/32768']
 if not inherited:lines+=['NORD2 WIFI DMA RAW: group=1 x=0/0x100/0/0','NORD2 WIFI DMA RAW: group=0 x=0/0x100/0/0']
 lines +=[f'NORD2 WIFI DMA OWNER: ap=0 wf=0 asleep=1 balanced=1 possible_lock={int(not inherited)} live=0x1f cfg=0x20010101 local=0x20010000 done=0x1d1e first={first}','NORD2 WIFI DMA LIVE: cpu=1 top=0x80 power=4 protection=0 dma_protection=0',f'NORD2 WIFI DMA CFG: before={int(inherited)}/0 after={int(inherited and not unstable)}/0 valid=0xf']
 for i,a in enumerate(m.RINGS):lines.append(f'NORD2 WIFI DMA RING: n={i} addr={a:#x} raw=0/{"0x8000f000" if reserved else "0"}/{"0xfffff000" if reserved else "0"}/0 valid=0xf')
 block='\n'.join(lines)+'\n';idx=base.index('NORD2 WIFI: power probe stage=');return (base[:idx]+block+base[idx:]).encode()
good=fixture();r=m.parse(good);assert r['complete'] and r['pool_zero_verified'] and not r['dma_enabled'],r['issues']
for args in [(True,False,False),(True,True,False),(False,False,True)]:
 r=m.parse(fixture(*args));assert r['complete'],r['issues'];assert r['pool_zero_verified']==(not args[0])
for a,b in [('stage=12 inherited','stage=11 inherited'),('words=32768/32768','words=32767/32768'),('raw_valid=3','raw_valid=2'),('raw1=0 raw0=0','raw1=0x200c raw0=0'),('possible_lock=1','possible_lock=0'),('raw=0/0/0/0 valid=0xf','raw=0/1/0/0 valid=0xf'),('reads=112','reads=111'),('group=1 base','group=0 base'),('n=26 addr','n=25 addr')]:assert not m.parse(good.replace(a.encode(),b.encode(),1))['complete'],a
assert not m.parse(good+good)['complete']
assert not m.parse(good.replace(b'raw1=0 raw0=0',b'raw1=0 raw0='+b'9'*5000,1))['complete']
assert not m.parse(good.replace(b'NORD2 WIFI DMA RING: n=26',b'NORD2 WIFI DMA RING: n=260',1))['complete']
assert not m.parse(good.replace(b'raw1=0 raw0=0',b'raw1=0 raw0=18446744073709551616',1))['complete']
assert not m.parse(good[:-1]+b'\xff')['complete']
assert not m.parse(b'x'*(8*1024*1024+1))['complete']
refusal=fixture(True);assert not m.parse(refusal.replace(b'attempted=0',b'attempted=1',1))['complete']
assert not m.parse(refusal.replace(b'first=1/0x18024208/0x1/0xf',b'first=0/0/0/0'))['complete']
print('PASS syntheticpoolsuccess/knowninherited+unstablerefusal/reservedbitsignored/full27arrays/recomputedfirstmismatch/partialSET/zero-readbackcounts/order/overflow/UTF8/bounds; no hardware')
