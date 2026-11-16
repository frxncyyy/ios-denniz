#!/usr/bin/env python3
import argparse, ctypes as C,json
from pathlib import Path
p=argparse.ArgumentParser(description="Validate retained pipeline and translator dispatch binding plans")
for key in ("bridge","air","profile","output"):p.add_argument("--"+key,type=Path,required=True)
p.add_argument("--mali-library",type=Path)
a=p.parse_args()
class Blob(C.Structure):_fields_=[('bytes',C.c_void_p),('length',C.c_size_t)]
class Binding(C.Structure):_fields_=[('index',C.c_uint32),('reserved',C.c_uint32),('length',C.c_uint64),('offset',C.c_uint64)]
class NativeBinding(C.Structure):_fields_=[('index',C.c_uint32),('reserved',C.c_uint32),('offset',C.c_uint64),('length',C.c_uint64)]
class Constant(C.Structure):_fields_=[('id',C.c_uint32),('size',C.c_uint32),('data',C.c_uint8*8)]
class NativeDispatch(C.Structure):_fields_=[('version',C.c_uint32),('mask',C.c_uint32),('groups',C.c_uint32*3),('count',C.c_uint32),('bindings',NativeBinding*8),('push',C.c_uint8*128),('constants',Constant*64)]
assert C.sizeof(NativeDispatch)==1368
l=C.CDLL(str(a.bridge.resolve()))
l.tegu_air_library_open.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t];l.tegu_air_library_open.restype=C.c_void_p
l.tegu_air_library_close.argtypes=[C.c_void_p]
l.tegu_air_pipeline_create.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Blob),C.POINTER(Blob),C.c_void_p,C.c_size_t];l.tegu_air_pipeline_create.restype=C.c_void_p
l.tegu_air_pipeline_close.argtypes=[C.c_void_p]
l.tegu_air_pipeline_plan.argtypes=[C.c_void_p,C.POINTER(Binding),C.c_size_t,C.POINTER(C.c_uint32),C.POINTER(C.c_uint32),C.POINTER(Blob),C.c_void_p,C.c_size_t]
native=l.tegu_air_pipeline_native_dispatch
native.argtypes=[C.c_void_p,C.POINTER(Binding),C.c_size_t,C.POINTER(C.c_uint32),C.POINTER(C.c_uint32),C.POINTER(NativeDispatch),C.c_void_p,C.c_size_t]
native.restype=C.c_int
l.tegu_air_blob_free.argtypes=[C.POINTER(Blob)]
e=C.create_string_buffer(2048)
air=a.air.read_bytes();profile=a.profile.read_bytes()
h=l.tegu_air_library_open(air,len(air),e,len(e));assert h,e.value
sp,meta=Blob(),Blob();name=b'scale_and_offset'
x=l.tegu_air_pipeline_create(h,name,len(name),profile,len(profile),C.byref(sp),C.byref(meta),e,len(e));assert x,e.value
l.tegu_air_library_close(h)
b=(Binding*3)(Binding(0,0,1152,128),Binding(1,0,1152,128),Binding(2,0,4,0));g=(C.c_uint32*3)(256,1,1);local=(C.c_uint32*3)(1,1,1);out=Blob()
assert l.tegu_air_pipeline_plan(x,b,3,g,local,C.byref(out),e,len(e))==0,e.value
j=json.loads(C.string_at(out.bytes,out.length));a.output.write_text(json.dumps(j,indent=2))
cmd=j['commands'];region=cmd['regions'][0]
assert len(cmd['regions'])==1 and region['group_count']==[256,1,1]
assert [r['range'] for r in cmd['descriptor_ranges']]==[dict(offset=0,range=1152),dict(offset=0,range=1152),dict(offset=0,range=16)]
assert [d['source']['Buffer']['slot']['index'] for d in j['plan']['descriptors']]==[0,1,2]
push=bytes(region['push_constants'][0]['bytes'])
assert int.from_bytes(push[32:36],'little')==128 and int.from_bytes(push[36:40],'little')==128
assert not cmd['push_holes'] and cmd['root_table'] is None
l.tegu_air_blob_free(C.byref(out))
# Dispatch specialization must reflect requested local size, without recompiling AIR.
local[0]=32;g[0]=8
assert l.tegu_air_pipeline_plan(x,b,3,g,local,C.byref(out),e,len(e))==0,e.value
j2=json.loads(C.string_at(out.bytes,out.length));r=j2['commands']['regions'][0]
assert r['group_count']==[8,1,1]
assert int.from_bytes(bytes(r['specialization']['data'][:4]),'little')==32
l.tegu_air_blob_free(C.byref(out))
packet=NativeDispatch()
assert native(x,b,3,g,local,C.byref(packet),e,len(e))==0,e.value
assert packet.version==1 and packet.mask==7 and list(packet.groups)==[8,1,1]
assert [(q.index,q.offset,q.length) for q in packet.bindings[:3]]==[(0,0,1152),(1,0,1152),(2,0,16)]
assert bytes(packet.push[:len(push)])==bytes(r['push_constants'][0]['bytes'])
assert packet.count==4 and int.from_bytes(bytes(packet.constants[0].data[:4]),'little')==32
if a.mali_library:
    mali=C.CDLL(str(a.mali_library.resolve()))
    compile=mali.tegu_mali_compile_specialized
    compile.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Constant),C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
    mali.tegu_mali_free.argtypes=[C.POINTER(Blob)]
    result=Blob()
    assert compile(sp.bytes,sp.length,packet.constants,packet.count,C.byref(result),e,len(e))==0,e.value
    import struct
    code=C.string_at(result.bytes,result.length)
    assert struct.unpack('<16I',code[:64])[9:12]==(32,1,1)
    a.output.with_suffix('.tgp').write_bytes(code)
    mali.tegu_mali_free(C.byref(result))
# Aligned descriptor base changes independently from the Metal bind offset.
b[0].offset=384;b[1].offset=384
assert native(x,b,3,g,local,C.byref(packet),e,len(e))==0,e.value
assert packet.bindings[0].offset==256 and packet.bindings[0].length==896
assert int.from_bytes(bytes(packet.push[32:36]),'little')==128
# Single-page native ABI must refuse an overlarge descriptor without stale output.
b[0].length=8192
assert native(x,b,3,g,local,C.byref(packet),e,len(e))==-1 and e.value
assert bytes(packet)==bytes(C.sizeof(packet))
b[0].length=1152
b[1].index=0;assert l.tegu_air_pipeline_plan(x,b,3,g,local,C.byref(out),e,len(e))==-1 and e.value and not out.bytes
for z in (sp,meta):l.tegu_air_blob_free(C.byref(z))
l.tegu_air_pipeline_close(x)

print("PASS retained pipeline, aligned descriptor ranges, bind-offset push bytes, dispatch specialization, native ABI conversion, page-bound refusal, duplicate refusal")
