#!/usr/bin/env python3
"""Compare shared C draw state against native-qualified compiled triangle oracle."""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gpu_compiled_triangle_image import prepare

class Addresses(C.Structure):
    _fields_ = [(name,C.c_uint64) for name in
                ('descriptors','tiler','geometry','chunk','target','vertex_code','fragment_code')] + [('clear_rgba',C.c_uint32)]
class State(C.Structure):
    _fields_ = [('descriptors',C.c_ubyte*4096),('tiler',C.c_ubyte*256),
                ('commands',C.c_uint64*128),('command_count',C.c_uint32)]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--graphics',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False)
image,q=prepare(0x880100000,a.graphics)
manifest=json.loads((a.graphics/'result.json').read_text())
stages={s['stage']:s for s in manifest['fixtures'] if s['format']==70}
vs=(a.graphics/'70-vertex.mali').read_bytes();fs=(a.graphics/'70-fragment.mali').read_bytes()
fetch=manifest.get('vertex_fetch')
code_offset=1280 if fetch else 384
fs_offset=code_offset+((len(vs)+63)&~63)
package=bytearray(fs_offset+len(fs))
header=[0x31524754,2 if fetch else 1,0xb8a21030,70,len(vs),len(fs),0,68]+[0]*24
for i,name in enumerate(('vertex','fragment')):
    row=stages[name];preload=int(row['preload'],16)
    header[8+8*i:16+8*i]=[row['registers'],row['fau_word_count'],row.get('sysval_mask',7 if i==0 else 8),
                          row['tls'],preload&0xffffffff,preload>>32,0 if i==0 else 68,0]
if fetch:
    plan=fetch['plan'];header[24:26]=[plan['mask'],plan['count']]
    for i,attribute in enumerate(plan['attributes']):
        struct.pack_into('<7I',package,384+i*28,*[attribute[name] for name in
            ('location','buffer_index','format','offset','stride','input_rate','divisor')])
struct.pack_into('<32I',package,0,*header)
package[code_offset:code_offset+len(vs)]=vs;package[fs_offset:]=fs
struct.pack_into('<I',package,320,0x2222)
addresses=Addresses(q['descriptor_va'],q['tiler_context_va'],q['tiler_geometry_va'],
                    q['tiler_chunk_va'],q['va']+0x3000,q['vertex_shader_va'],q['shader_va'],0xff3366cc)
source=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    library=Path(tmp)/'render.so'
    subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
                    str(source/'render_state.c'),'-o',str(library)],check=True)
    lib=C.CDLL(str(library));entry=lib.tegu_render_build_vertex
    entry.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Addresses),C.c_uint64,C.POINTER(State)]
    entry.restype=C.c_int
    resource_table=fetch['resource_table'] if fetch else 0
    def build(binary,size,addresses,state):
        return entry(binary,size,addresses,resource_table,state)
    state=State();binary=bytes(package)
    assert build(binary,len(binary),C.byref(addresses),C.byref(state))==0
    # Unused pre-frame draw and handwritten-vertex fixture payloads intentionally
    # omitted; compare every descriptor consumed by this IDVS pass.
    for start,end in ((0,0x200),(0x400,0x620),(0xa00,0xc00)):
        assert bytes(state.descriptors)[start:end]==image[0x20000+start:0x20000+end],hex(start)
    assert bytes(state.tiler)==image[0x21000:0x21100]
    commands=bytes(state.commands)[:state.command_count*8]
    assert commands==image[0x4000:0x4000+q['command_bytes']]
    (a.out/'descriptors.bin').write_bytes(bytes(state.descriptors))
    (a.out/'tiler.bin').write_bytes(bytes(state.tiler))
    (a.out/'commands.bin').write_bytes(commands)
    (a.out/'pipeline.tgr').write_bytes(binary)
    baseline=bytes(state)
    # Refuse bad mappings and formats without corrupting a previous state.
    for name,_ in Addresses._fields_[:-1]:
        original=getattr(addresses,name)
        for value in (0,original+1,(1<<48),addresses.geometry):
            if value==original:continue
            setattr(addresses,name,value)
            assert build(binary,len(binary),C.byref(addresses),C.byref(state))!=0,(name,value)
            assert bytes(state)==baseline
        setattr(addresses,name,original)
    bad=bytearray(binary);struct.pack_into('<I',bad,12,80)
    assert build(bytes(bad),len(bad),C.byref(addresses),C.byref(state))!=0
    assert build(binary,len(binary)-1,C.byref(addresses),C.byref(state))!=0
    assert build(binary,len(binary),None,C.byref(state))!=0
    assert build(binary,len(binary),C.byref(addresses),None)!=0
    assert bytes(state)==baseline
    # Clear color and aliases come from bindings, not the qualification fixture.
    addresses.clear_rgba=0x11223344;addresses.target+=0x100000
    addresses.vertex_code+=0x200000;addresses.fragment_code+=0x200000
    assert build(binary,len(binary),C.byref(addresses),C.byref(state))==0
    d=bytes(state.descriptors)
    assert struct.unpack_from('<Q',d,160)[0]==addresses.target
    assert struct.unpack_from('<4I',d,176)==(0x11223344,)*4
    assert struct.unpack_from('<Q',d,0x608)[0]==addresses.vertex_code
    assert struct.unpack_from('<Q',d,0x488)[0]==addresses.fragment_code
    sanitized=Path(tmp)/'render-sanitized'
    subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined',str(source/'render_state.c'),
                    str(source/'render_state_host_test.c'),'-o',str(sanitized)],check=True)
    subprocess.run([str(sanitized),str(a.out/'pipeline.tgr'),str(resource_table)],check=True)
result=dict(scope='Host C render state vs native-qualified Python oracle; no new GPU execution',
            command_count=len(commands)//8,command_bytes=len(commands),
            descriptors_match=True,tiler_match=True,commands_match=True,
            refusal_preserves_output=True,relocated_bindings=True)
(a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS shared C render state: native-qualified descriptors/tiler/commands identical, refusal, relocated bindings')
