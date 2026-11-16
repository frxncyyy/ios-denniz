#!/usr/bin/env python3
"""Check bounded compositor descriptors against Mesa and an optional raw packet.

This executes host packing/refusal tests. It does not submit GPU work or
qualify a native Metal pipeline.
"""
import argparse
import ctypes
import hashlib
import json
import struct
import subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mesa-source',type=Path,required=True)
p.add_argument('--mesa-build',type=Path,required=True)
p.add_argument('--producer-texture',type=Path,required=True)
p.add_argument('--producer-plane',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--cc',default='cc')
p.add_argument('--candidate',type=Path)
p.add_argument('--candidate-manifest',type=Path)
a=p.parse_args()
if bool(a.candidate)!=bool(a.candidate_manifest):
 p.error('--candidate and --candidate-manifest must be supplied together')
a.work.mkdir(parents=True,exist_ok=False)
assert hashlib.sha256((a.mesa_build/'src/panfrost/genxml/v10_pack.h').read_bytes()).hexdigest()==\
 'b73468a372af9bfcaaa3e9f29cfdf36d3e79fbdb839dc27a96d4fa160160e2d1'
d=Path(__file__).resolve().parent
common=[a.cc,'-std=c11','-Wall','-Wextra','-Werror']
mesa_inc=['-DUTIL_ARCH_LITTLE_ENDIAN=1','-DUTIL_ARCH_BIG_ENDIAN=0','-D__ANDROID__=1','-Wno-unused-parameter',
 '-I'+str(a.mesa_source/'src/panfrost'),'-I'+str(a.mesa_build/'src/panfrost'),
 '-I'+str(a.mesa_source/'src'),'-I'+str(a.mesa_source/'src/compiler'),
 '-I'+str(a.mesa_source/'src/util'),'-I'+str(a.mesa_source/'include')]
commands=[common+mesa_inc+[str(d/'compositor_resource_mesa_golden.c'),'-lm','-o',str(a.work/'gen_v10_golden')],
 common+['-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'compositor_resource_state.c'),
 str(d/'compositor_resource_state_host_test.c'),'-o',str(a.work/'test_resource_state')]]
with (a.work/'commands.txt').open('w') as f:
 for c in commands:f.write(' '.join(c)+'\n')
for c in commands:subprocess.run(c,check=True)
subprocess.run([str(a.work/'gen_v10_golden'),str(a.work/'v10-golden.bin')],check=True)
r=subprocess.run([str(a.work/'test_resource_state'),str(a.work/'v10-golden.bin'),
 str(a.producer_texture),str(a.producer_plane)],text=True,capture_output=True)
if r.returncode:
 raise RuntimeError(f'resource state test failed ({r.returncode}):\n{r.stdout}\n{r.stderr}')
(a.work/'result.txt').write_text(r.stdout)
print(r.stdout,end='')
result={'passed':True,'gpu_executed':False,'mesa_descriptor_parity':True,
        'producer_texture_plane_parity':True,'sanitized':True,'candidate_parity':False}
if a.candidate:
 class Span(ctypes.Structure):
  _fields_=[('address',ctypes.c_uint64),('bytes',ctypes.c_uint64)]
 class Input(ctypes.Structure):
  _fields_=[('version',ctypes.c_uint32),('table0',Span),('resource_table',Span),
            ('descriptors',Span),('plane',Span),('texels',Span),('ubos',Span*4)]
 class State(ctypes.Structure):
  _fields_=[('resource_table_handle',ctypes.c_uint64),('resource_table',ctypes.c_uint8*64),
            ('descriptors',ctypes.c_uint8*192),('plane',ctypes.c_uint8*32)]
 library=a.work/'resource-state.so'
 subprocess.run(common+['-shared','-fPIC',str(d/'compositor_resource_state.c'),'-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library.resolve()))
 fn=lib.tegu_compositor_resource_state_build
 fn.argtypes=[ctypes.POINTER(Input),ctypes.POINTER(State)];fn.restype=ctypes.c_int
 packet=a.candidate.read_bytes();m=json.loads(a.candidate_manifest.read_text());va=m['va']
 inp=Input();inp.version=1;inp.table0=Span(va+0x50000,576)
 inp.resource_table=Span(va+0x53000,64);inp.descriptors=Span(va+0x53100,192)
 inp.plane=Span(va+0x53200,32);inp.texels=Span(va+0x54000,1024)
 inp.ubos[:]=[Span(va+offset,length) for offset,length in
              ((0x53300,64),(0x53340,16),(0x53350,80),(0x533a0,16))]
 state=State();assert fn(ctypes.byref(inp),ctypes.byref(state))==0
 for offset,expected in ((0x57000,bytes(state.resource_table)),
                         (0x57100,bytes(state.descriptors)),(0x57200,bytes(state.plane))):
  assert packet[offset:offset+len(expected)]==expected,hex(offset)
 assert struct.unpack_from('<Q',packet,0x57010)[0]&((1<<56)-1)==va+0x53100
 result.update(candidate_parity=True,candidate_sha256=hashlib.sha256(packet).hexdigest())
 print('PASS canonical descriptors match supplied raw packet; no GPU submission')
(a.work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
