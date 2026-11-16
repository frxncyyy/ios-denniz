#!/usr/bin/env python3
"""One-process ABI probe. Nonzero status is a refusal only if process exits normally."""
import argparse
import ctypes as C
import json
import hashlib
from pathlib import Path
import sys

class Graphics(C.Structure):
    _fields_=[('code',C.c_void_p),('size',C.c_size_t)]+[(n,C.c_uint32) for n in
      ('stage','registers','tls','fau_words','sysval_mask','push_bytes')]+[('preload',C.c_uint64)]
class Slot(C.Structure): _fields_=[(n,C.c_uint32) for n in ('location','offset','components','alu_type')]
class Pair(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('version','flags','position_offset','position_size','point_position_offset',
      'point_position_size','secondary_offset','secondary_size','secondary_registers','generic_size_B')]+[
      ('secondary_preload',C.c_uint64)]+[(n,C.c_uint32) for n in ('vs_attribute_mask','vs_varying_mask',
      'fs_varying_mask','fs_uses_ld_var','noperspective_mask','needs_extended_fifo','point_size_section',
      'point_size_offset','point_size_alu_type','varying_count')]+[('varyings',Slot*4)]
class Ubo(C.Structure): _fields_=[(n,C.c_uint32) for n in ('stage','descriptor_set','binding','table','index','declared_bytes')]
class Plan(C.Structure): _fields_=[(n,C.c_uint32) for n in ('version','table','texture_index','sampler_index',
      'normalized_coordinates','sample_count')]+[('ubos',Ubo*4)]
class Texture(C.Structure): _fields_=[(n,C.c_uint32) for n in ('version','table','texture_index','sampler_index',
      'normalized_coordinates','descriptor_set','binding')]

p=argparse.ArgumentParser()
p.add_argument('--library',type=Path,required=True);p.add_argument('--vertex',type=Path,required=True)
p.add_argument('--fragment',type=Path,required=True);p.add_argument('--mode',choices=['resources','pair2','texture','alias','sentinel','plan-sentinel'],required=True)
p.add_argument('--output-dir',type=Path)
p.add_argument('--expect-refusal',action='store_true')
a=p.parse_args();lib=C.CDLL(str(a.library.resolve()))
vb,fb=a.vertex.read_bytes(),a.fragment.read_bytes();v=C.create_string_buffer(vb);f=C.create_string_buffer(fb)
vs,fs,pair,plan,texture=Graphics(),Graphics(),Pair(),Plan(),Texture();err=C.create_string_buffer(512)
input_before=(C.string_at(C.addressof(v),len(vb)),C.string_at(C.addressof(f),len(fb)))
def raw(x): return C.string_at(C.addressof(x),C.sizeof(x))
def fill_sentinel(x,version=None):
    C.memset(C.addressof(x),0xA5,C.sizeof(x))
    if version is not None: C.cast(C.addressof(x),C.POINTER(C.c_uint32))[0]=version
if a.mode=='resources':
    fn=lib.tegu_mali_compile_graphics_pair_resources;fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,
      C.POINTER(Graphics),C.POINTER(Graphics),C.POINTER(Pair),C.POINTER(Plan),C.c_void_p,C.c_size_t]
    rc=fn(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),C.byref(plan),err,len(err))
elif a.mode=='pair2':
    fn=lib.tegu_mali_compile_graphics_pair2;fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,
      C.POINTER(Graphics),C.POINTER(Graphics),C.POINTER(Pair),C.c_void_p,C.c_size_t]
    rc=fn(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),err,len(err))
elif a.mode=='texture':
    fn=lib.tegu_mali_compile_graphics_pair_texture;fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,
      C.POINTER(Graphics),C.POINTER(Graphics),C.POINTER(Pair),C.POINTER(Texture),C.c_void_p,C.c_size_t]
    rc=fn(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),C.byref(texture),err,len(err))
else:
    fn=lib.tegu_mali_compile_graphics_pair_resources;fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,
      C.POINTER(Graphics),C.POINTER(Graphics),C.POINTER(Pair),C.POINTER(Plan),C.c_void_p,C.c_size_t]
    if a.mode=='plan-sentinel':
        # Preflight permits plan.version0 with arbitrary remaining bytes.
        # A late FS output refusal follows successful VS code generation:
        # generated code/pair must be cleaned up, plan must remain untouched.
        fill_sentinel(plan,0);before=raw(plan)
        rc=fn(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),C.byref(plan),err,len(err))
        assert rc==4 and b'Unsupported graphics output' in err.value
        assert raw(plan)==before, 'post-preflight refusal modified plan bytes'
        assert raw(vs)==bytes(C.sizeof(vs)) and raw(fs)==bytes(C.sizeof(fs))
        assert raw(pair)==bytes(C.sizeof(pair))
    elif a.mode=='alias':
        # The new API must refuse overlapping outputs before touching any object.
        for x in (vs,fs,pair): fill_sentinel(x)
        before=tuple(raw(x) for x in (vs,fs,pair))
        rc=fn(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),
          C.cast(C.byref(pair),C.POINTER(Plan)),err,len(err))
        assert tuple(raw(x) for x in (vs,fs,pair))==before, 'alias refusal modified output bytes'
    else:
        # Invalid input must leave sentinel-filled outputs untouched, including
        # all non-version fields, while reporting an ordinary refusal.
        for x in (vs,fs): fill_sentinel(x)
        fill_sentinel(pair,2);fill_sentinel(plan,1)
        before=tuple(raw(x) for x in (vs,fs,pair,plan))
        rc=fn(v,len(vb),f,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),C.byref(plan),err,len(err))
        assert tuple(raw(x) for x in (vs,fs,pair,plan))==before, 'sentinel refusal modified output bytes'
assert (C.string_at(C.addressof(v),len(vb)),C.string_at(C.addressof(f),len(fb)))==input_before, 'compiler modified SPIR-V input'
if rc==0:
    if a.mode=='resources':
        assert vs.code and fs.code and pair.version==2 and plan.version==1
        assert (plan.table,plan.texture_index,plan.sampler_index,plan.normalized_coordinates,plan.sample_count)==(1,4,5,1,4)
        assert [(x.stage,x.descriptor_set,x.binding,x.table,x.index,x.declared_bytes) for x in plan.ubos]==[
          (1,0,0,1,0,80),(1,0,1,1,1,32),(2,0,2,1,2,96),(2,0,3,1,3,32)]
    elif a.mode=='texture':
        assert texture.version==1 and (texture.table,texture.texture_index,texture.sampler_index,
          texture.normalized_coordinates,texture.descriptor_set,texture.binding)==(0,1,0,1,0,0)
    elif a.mode!='alias': assert False,'legacy API unexpectedly accepted'
elif a.mode not in ('alias','sentinel'):
    assert not vs.code and not vs.size and not fs.code and not fs.size and not pair.version
    if a.mode=='resources': assert not plan.version
    if a.mode=='texture': assert not texture.version
if a.expect_refusal and rc==0: raise AssertionError('case expected refusal but compiler accepted it')
if not a.expect_refusal and a.mode in ('resources','texture') and rc!=0:
    raise AssertionError('unexpected compiler refusal: '+err.value.decode(errors='replace'))
result={'mode':a.mode,'status':rc,'error':err.value.decode(errors='replace'),'process':'normal',
 'vs_size':vs.size,'fs_size':fs.size,'plan_version':plan.version,'pair_version':pair.version}
if rc==0:
    result['graphics']={}
    for name,g in (('vs',vs),('fs',fs)):
        result['graphics'][name]={'stage':g.stage,'registers':g.registers,'tls':g.tls,
          'fau_words':g.fau_words,'sysval_mask':g.sysval_mask,'push_bytes':g.push_bytes,'preload':g.preload}
    result['pair']={name:getattr(pair,name) for name,_ in Pair._fields_ if name!='varyings'}
    result['pair']['varyings']=[{n:getattr(x,n) for n,_ in Slot._fields_} for x in pair.varyings[:pair.varying_count]]
    if a.mode=='resources':
        result['plan']={'version':plan.version,'table':plan.table,'texture_index':plan.texture_index,
          'sampler_index':plan.sampler_index,'normalized_coordinates':plan.normalized_coordinates,
          'sample_count':plan.sample_count,'ubos':[{n:getattr(x,n) for n,_ in Ubo._fields_} for x in plan.ubos]}
    if a.mode=='texture':
        result['texture']={n:getattr(texture,n) for n,_ in Texture._fields_}
if rc==0 and a.output_dir:
    a.output_dir.mkdir(parents=True,exist_ok=True)
    for name,g in (('vs',vs),('fs',fs)):
        data=C.string_at(g.code,g.size);(a.output_dir/(name+'.mali')).write_bytes(data)
        result[name+'_sha256']=hashlib.sha256(data).hexdigest()
print(json.dumps(result))
if rc==0:
    lib.tegu_mali_graphics_free.argtypes=[C.POINTER(Graphics)]
    lib.tegu_mali_graphics_free(C.byref(vs));lib.tegu_mali_graphics_free(C.byref(fs))
