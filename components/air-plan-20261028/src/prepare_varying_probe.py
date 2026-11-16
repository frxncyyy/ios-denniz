#!/usr/bin/env python3
"""Package host compiled pair and native descriptors for explicit GPU tests."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

class Attribute(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('location','buffer_index','format','offset','stride','input_rate','divisor')]
class Buffer(C.Structure):
    _fields_ = [('address', C.c_uint64), ('length', C.c_uint64)]
class Draw(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('first_vertex','vertex_count','instance_count','base_instance')]
class State(C.Structure):
    _fields_ = [('resource_table', C.c_uint64),('buffer_mask', C.c_uint32),('reserved', C.c_uint32),('descriptors', C.c_ubyte*4096)]

def sha(data):return hashlib.sha256(data).hexdigest()

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pair',type=Path,required=True,help='Directory with pair_metadata.json and compiled binaries')
p.add_argument('--out',type=Path,required=True)
p.add_argument('--fetch',type=Path,help='Actual linked AIR vertex-fetch plan; must match the explicit test layout')
p.add_argument('--push',type=Path,help='Actual reflected AIR push bytes/spans, required for push-using shaders')
p.add_argument('--oracle',choices=('constant','barycentric'),required=True)
a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False)
m=json.loads((a.pair/'pair_metadata.json').read_text())
for name, artifact in (('vs','vs_full'),('fs','fs')):
    data=(a.pair/(artifact+'.bin')).read_bytes()
    assert sha(data)==m['sha256'][artifact]==m[name]['sha256']
    (a.out/(name+'.mali')).write_bytes(data)
positions=((-.75,-.75,.5,1.),(.75,-.75,.5,1.),(-.75,.75,.5,1.))
colors=((.2,.4,.8,.6),)*3 if a.oracle=='constant' else ((1.,0.,0.,.6),(0.,1.,0.,.6),(0.,0.,1.,.6))
vertices=b''.join(struct.pack('<8f',*pos,*color) for pos,color in zip(positions,colors))
attributes=(Attribute*2)(Attribute(0,1,109,0,32,0,1),Attribute(1,1,109,16,32,0,1))
if a.fetch:
    plan=json.loads(a.fetch.read_text())
    expected=dict(version=1,mask=3,count=2,reserved=0,attributes=[
        dict(location=i,buffer_index=1,format=109,offset=i*16,stride=32,input_rate=0,divisor=1) for i in range(2)])
    # Bridge versions may omit reserved in the serialized diagnostic plan.
    plan.setdefault('reserved',0)
    if plan!=expected:p.error('AIR fetch layout differs from the explicit hardware fixture')
buffers=(Buffer*31)();buffers[1]=Buffer(0x05051000,len(vertices));draw=Draw(0,3,1,0);state=State()
source=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    libpath=Path(tmp)/'vertex.so'
    subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',str(source/'vertex_fetch.c'),'-o',str(libpath)],check=True)
    builder=C.CDLL(str(libpath)).tegu_vertex_fetch_build
    builder.argtypes=[C.c_uint32,C.POINTER(Attribute),C.c_size_t,C.POINTER(Buffer),C.POINTER(Draw),C.c_uint64,C.POINTER(State)]
    builder.restype=C.c_int
    assert not builder(m['pair']['vs_attribute_mask'],attributes,2,buffers,C.byref(draw),0x05050000,C.byref(state))
assert state.resource_table==0x05050004 and state.buffer_mask==2
raw=bytes(state.descriptors)
(a.out/'vertex-descriptors.bin').write_bytes(raw);(a.out/'vertices.bin').write_bytes(vertices)
report=dict(schema=1,oracle=a.oracle,vs=m['vs'],fs=m['fs'],pair=m['pair'],vertices_sha256=sha(vertices),
            descriptors_sha256=sha(raw),gpu_executed=False,source_scope=m['evidence'])
if a.push: report['push']=json.loads(a.push.read_text())
(a.out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(a.out/'result.json')
