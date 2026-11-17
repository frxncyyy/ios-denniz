#!/usr/bin/env python3
"""Small pure synthetic decoder tests; no parser repetition/device."""
import copy,importlib.util
from pathlib import Path
p=Path(__file__).resolve().parent;s=importlib.util.spec_from_file_location('decode',p/'decode-raw-regions.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture(value=0):
 return dict(complete=True,issues=[],sha256='a'*64,legacy=dict(complete=True,issues=[],classification='all128_raw_values',all128_values=True,end=dict(collector=0,issued=128,valid=[0xffffffff]*4),records=[dict(index=i,offset=(i%4+1)*0x100+4*(i//4),status=1,error=0,raw=[value,1,2,3]) for i in range(128)]))
r=m.decode(fixture());assert r['raw_pattern']=='constant_zero_uninformative' and r['regions'][0]['candidate_last_inclusive']==0x4000ffff
assert m.decode(fixture(2))['raw_pattern']=='constant_two_uninformative'
f=fixture();rows=f['legacy']['records'];rows[27*4]['raw'][0]=0x4fb0;rows[27*4+1]['raw'][0]=0x4ff4;rows[27*4+2]['raw'][0]=7|(7<<6)|0x80000000
r=m.decode(f);assert r['intersections']['CONN']==[27] and not r['effective_permissions_proven']
assert r['regions'][27]['domains'][0]['source_label']=='unknown_reserved' and r['regions'][27]['domains'][2]['source_label']=='unknown_reserved'
assert r['regions'][27]['apc_uninterpreted_high_bits'][0]==0x80000000
for mutate in [lambda f:f.update(complete=False),lambda f:f['legacy'].update(all128_values=False),lambda f:f['legacy']['records'].pop(),lambda f:f['legacy']['records'][0].update(status=0),lambda f:f['legacy']['end'].update(valid=[0]*4),lambda f:f['legacy']['records'][0].update(raw=[0x20000,1,2,3]),lambda f:f['legacy']['records'][0].update(raw=[1,1,2,3]),lambda f:f['legacy']['records'][0].update(raw=[0xfffffffd,1,2,3])]:
 f=fixture();mutate(f)
 try:m.decode(f)
 except ValueError:pass
 else:raise AssertionError('invalid/partial fixture accepted')
print('PASS: synthetic interval bounds/CONN intersection/raw preservation/domain7/partial refusal/zero+two patterns')
