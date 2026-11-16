#!/usr/bin/env python3
"""Focused host check for the exact R85 sampled fixed-UI Mali compiler."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path

class Graphics(C.Structure):
    _fields_=[('code',C.c_void_p),('size',C.c_size_t)]+[(n,C.c_uint32) for n in
      ('stage','registers','tls','fau_words','sysval_mask','push_bytes')]+[('preload',C.c_uint64)]
class Slot(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('location','offset','components','alu_type')]
class Pair(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('version','flags','position_offset','position_size','point_position_offset',
      'point_position_size','secondary_offset','secondary_size','secondary_registers','generic_size_B')]+[
      ('secondary_preload',C.c_uint64)]+[(n,C.c_uint32) for n in ('vs_attribute_mask','vs_varying_mask',
      'fs_varying_mask','fs_uses_ld_var','noperspective_mask','needs_extended_fifo','point_size_section',
      'point_size_offset','point_size_alu_type','varying_count')]+[('varyings',Slot*4)]
class Buffer(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('stage','descriptor_set','binding','table','index','declared_bytes')]
class Plan(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('version','table','resource_count','texture_index','sampler_index',
      'normalized_coordinates','sample_count','implicit_lod','has_bias','constant_bytes','constant_access_bytes')]+[
      ('buffers',Buffer*5)]

def raw(value):return C.string_at(C.addressof(value),C.sizeof(value))
def graphics(g):
    return {n:getattr(g,n) for n,_ in Graphics._fields_ if n!='code'}|{
      'sha256':hashlib.sha256(C.string_at(g.code,g.size)).hexdigest()}

p=argparse.ArgumentParser()
p.add_argument('--library',type=Path,required=True)
p.add_argument('--vertex',type=Path,required=True)
p.add_argument('--fragment',type=Path,required=True)
a=p.parse_args();lib=C.CDLL(str(a.library.resolve()))
compile_r85=lib.tegu_mali_compile_fixed_ui_texture_pair_r85
compile_r85.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Graphics),C.POINTER(Graphics),
 C.POINTER(Pair),C.POINTER(Plan),C.c_void_p,C.c_size_t]
vb,fb=a.vertex.read_bytes(),a.fragment.read_bytes();v=C.create_string_buffer(vb);f=C.create_string_buffer(fb)
vs,fs,pair,plan=Graphics(),Graphics(),Pair(),Plan();error=C.create_string_buffer(512)
status=compile_r85(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),C.byref(plan),error,len(error))
if status:
    raise AssertionError((status,error.value.decode(errors='replace')))
assert (plan.version,plan.table,plan.resource_count,plan.texture_index,plan.sampler_index,
 plan.normalized_coordinates,plan.sample_count,plan.implicit_lod,plan.has_bias,plan.constant_bytes)==(1,1,5,5,6,1,1,1,1,8)
assert [(x.stage,x.descriptor_set,x.binding,x.table,x.index) for x in plan.buffers]==[
 (1,0,0,1,0),(1,0,1,1,1),(2,0,2,1,2),(2,0,3,1,3),(2,0,4,1,4)]
result={'vs':graphics(vs),'fs':graphics(fs),'pair_version':pair.version,
 'plan_size':C.sizeof(Plan),'constant_access_bytes':plan.constant_access_bytes,
 'declared_bytes':[x.declared_bytes for x in plan.buffers]}
assert result['vs']=={'size':4096,'stage':1,'registers':32,'tls':0,'fau_words':34,
 'sysval_mask':3,'push_bytes':8,'preload':0x3800000000000000,
 'sha256':'d59d911486fabc4c18ccbed471ef5f0b89101e63f5a49eee04355955f32262fb'}
assert result['fs']=={'size':1152,'stage':2,'registers':32,'tls':0,'fau_words':51,
 'sysval_mask':8,'push_bytes':76,'preload':0x3000000000000000,
 'sha256':'85e7d601f71120499cf64ee49a4c324d0c87868180947008bd708bcec873bb84'}
assert (pair.flags,pair.position_offset,pair.position_size,pair.point_position_size,
 pair.secondary_offset,pair.secondary_size,pair.secondary_registers,pair.generic_size_B,
 pair.vs_attribute_mask,pair.vs_varying_mask,pair.fs_varying_mask,pair.varying_count)==(
 1,1792,1792,1792,3584,512,32,8,7,1,1,1)
assert (pair.varyings[0].location,pair.varyings[0].offset,pair.varyings[0].components,
 pair.varyings[0].alu_type)==(0,0,2,160)
assert result['declared_bytes']==[320,272,432,272,0] and plan.constant_access_bytes==0
print(json.dumps(result,indent=2))
lib.tegu_mali_graphics_free(C.byref(vs));lib.tegu_mali_graphics_free(C.byref(fs))

# Caller-owned nonempty output state is rejected before compilation.
sentinel=Plan()
before=raw(sentinel)
pair=Pair();pair.version=1
status=compile_r85(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),C.byref(sentinel),error,len(error))
assert status==1 and b'Output must be empty' in error.value and raw(sentinel)==before
assert not vs.code and not fs.code and pair.version==1

# Aliasing is rejected before caller-owned output bytes are changed.
shared=(C.c_ubyte*max(C.sizeof(Pair),C.sizeof(Plan)))()
C.memset(shared,0x5A,C.sizeof(shared));before=bytes(shared)
status=compile_r85(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),
 C.cast(shared,C.POINTER(Pair)),C.cast(shared,C.POINTER(Plan)),error,len(error))
assert status==1 and bytes(shared)==before
assert C.string_at(v,len(vb))==vb and C.string_at(f,len(fb))==fb
print('R85 sampled fixed-UI compiler: positive and two bounded refusals passed')
