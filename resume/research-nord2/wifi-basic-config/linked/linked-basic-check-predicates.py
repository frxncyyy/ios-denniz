#!/usr/bin/env python3
"""Reproduce a few bounded linked predicates after the authorized extraction.
This checks exact instructions/constants; it cannot replace the manual review.
"""
from pathlib import Path
import json,re,struct,hashlib
P=Path(__file__).resolve().parent;R=P.parents[3]
d=json.loads((P/'linked-basic-data.json').read_text());b=(R/d['kernel']).read_bytes()
assert hashlib.sha256(b).hexdigest()==d['kernel_sha256']
segments=[];p=32
for _ in range(struct.unpack_from('<I',b,16)[0]):
 c,n=struct.unpack_from('<II',b,p)
 if c==0x19:
  va,vs,off,fs=struct.unpack_from('<QQQQ',b,p+24);segments.append((va,off,fs))
 p+=n
def read(a,n):
 for va,off,fs in segments:
  if va<=a and a+n<=va+fs:return b[off+a-va:off+a-va+n]
 raise ValueError(hex(a))
constants={
 'native_window_expected_4x_u32':list(struct.unpack('<4I',read(0xfffffe000700bf80,16))),
 'resource_tc_pse_ple_wire':list(struct.unpack('<4I',read(0xfffffe000700c330,16))),
 'basic_wire_prefix_8B':read(0xfffffe000700c340,8).hex(),
 'ena_guard_base_mask':hex(2100822015),'ena_guard_leased_mask':hex(1563951103),
 'ddone_software_control':hex((-1068761088)&0xffffffff)}
assert constants['native_window_expected_4x_u32']==[1,1,1,0]
assert constants['resource_tc_pse_ple_wire']==[4,1,0,76]
assert constants['basic_wire_prefix_8B']=='4c00000100000100'
assert 2100822015^1563951103==1<<29
text=(P/'linked-basic-core-disassembly.txt').read_text()+'\n'+(P/'linked-basic-helper-disassembly.txt').read_text()
inst={int(m[1],16):m[2] for line in text.splitlines() if (m:=re.match(r'^([0-9a-f]{16}):\s+(.*)$',line))}
expected={
 0xfffffe00074ffc20:'cmp\tw8, #4',
 0xfffffe00074ffc28:'bl\t0xfffffe0007504544 <_ncd_normal_owner_window>',
 0xfffffe00074ffda8:'tbnz\tw8, #29, 0xfffffe00074ffdc0 <_nwfn_guard+0x174c>',
 0xfffffe0007509eec:'str\twzr, [sp, #44]',
 0xfffffe0007509ef8:'cmp\tw8, #1',
 0xfffffe0007509efc:'b.ne\t0xfffffe0007509fa0 <_ncd_worker+0x15c>',
 0xfffffe000750bd18:'cmp\tw8, #4',
 0xfffffe000750c228:'mov\tw2, #76',
 0xfffffe000750c268:'mov\tw5, #76',
 0xfffffe000750c2e4:'bl\t0xfffffe000750d1f4 <_nnr_reserve_once>',
 0xfffffe000750c340:'bl\t0xfffffe000750c9c0 <_rw>',
 0xfffffe000750c460:'bl\t0xfffffe000750ca98 <_drain>',
 0xfffffe000750c504:'bl\t0xfffffe000750c63c <_coherent>',
 0xfffffe000750c534:'str\tw9, [x25, #1920]',
 0xfffffe000750c53c:'str\tw8, [x19, #60]',
 0xfffffe000750c540:'str\tw9, [x19, #36]',
 0xfffffe000750d258:'sub\tw9, w9, #1',
 0xfffffe000750d260:'stp\tw10, w9, [x8, #44]'}
for a,s in expected.items():assert inst[a]==s,(hex(a),inst[a],s)
terminal=[s for a,s in inst.items() if 0xfffffe000750c50c<=a<=0xfffffe000750c544]
assert not any(re.match(r'b(l|lr)\s',s) for s in terminal)
report=text[text.index('fffffe000750a708 <_nbc_report>:'):text.index('fffffe000750baac <_nbc_prepare>:')]
calls=sorted(set(re.findall(r'\bbl\s+0x[0-9a-f]+ <_([^>]+)>',report)))
assert calls==['current_thread','nbc_lease_window','printf'],calls
out={'status':'PASS_BOUNDED_INSTRUCTION_AND_CONSTANT_PREDICATES','kernel_sha256':d['kernel_sha256'],'constants':constants,'instruction_assertions':{hex(a):s for a,s in expected.items()},'terminal_after_last_io_calls':[],'nbc_report_direct_call_targets':calls,'state_allocation':{'nbc_native':d['state_symbols']['nbc_native'],'ncd_native':d['state_symbols']['ncd_native'],'nbc_allocation_to_next_cap_object_bytes':int(d['state_symbols']['ncd_native'],16)-int(d['state_symbols']['nbc_native'],16)},'limits':'Only listed actual instructions/constants checked mechanically; full targeted branch trace reviewed manually in verdict.'}
(P/'linked-basic-predicates.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS bounded linked predicates')
