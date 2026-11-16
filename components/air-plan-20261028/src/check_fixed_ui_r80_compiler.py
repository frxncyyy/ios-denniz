#!/usr/bin/env python3
"""Host-check the reconstructed R82 fixed-UI Mali compiler policy."""
import argparse
import ctypes as C
import subprocess
import tempfile
from pathlib import Path

U32=C.c_uint32
class Graphics(C.Structure):
    _fields_=[('code',C.c_void_p),('size',C.c_size_t)]+[(n,U32) for n in
      ('stage','registers','tls','fau_words','sysval_mask','push_bytes')]+[('preload',C.c_uint64)]
class Slot(C.Structure):
    _fields_=[(n,U32) for n in ('location','offset','components','alu_type')]
class Pair(C.Structure):
    _fields_=[(n,U32) for n in ('version','flags','position_offset','position_size',
      'point_position_offset','point_position_size','secondary_offset','secondary_size',
      'secondary_registers','generic_size_B')]+[('secondary_preload',C.c_uint64)]+[
      (n,U32) for n in ('vs_attribute_mask','vs_varying_mask','fs_varying_mask',
      'fs_uses_ld_var','noperspective_mask','needs_extended_fifo','point_size_section',
      'point_size_offset','point_size_alu_type','varying_count')]+[('varyings',Slot*4)]
class Ubo(C.Structure):
    _fields_=[(n,U32) for n in ('stage','descriptor_set','binding','table','index','declared_bytes')]
class Plan(C.Structure):
    _fields_=[(n,U32) for n in ('version','table','resource_count','texture_count',
      'constant_bytes','constant_access_bytes')]+[('ubos',Ubo*3)]

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--library',type=Path,required=True)
args=parser.parse_args()
lib=C.CDLL(str(args.library.resolve()))
compile_pair=lib.tegu_mali_compile_fixed_ui_pair_r80
compile_pair.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Graphics),
 C.POINTER(Graphics),C.POINTER(Pair),C.POINTER(Plan),C.c_void_p,C.c_size_t]
compile_pair.restype=C.c_int
free=lib.tegu_mali_graphics_free
free.argtypes=[C.POINTER(Graphics)]

VS='''#version 450
layout(set=0,binding=0,std140) uniform VertexData { vec4 data[20]; } u;
layout(location=0) in vec4 position; layout(location=0) out vec4 color;
void main(){ gl_Position=position+vec4(u.data[0].xy,0.0,0.0); color=u.data[19]; }
'''
FS='''#version 450
#extension GL_EXT_scalar_block_layout : require
layout(set=0,binding=1,scalar) uniform FragmentWords { uint words[108]; } u;
layout(set=0,binding=1,scalar) uniform FragmentVectors { uvec4 vectors[27]; } v;
layout(set=0,binding=2,std430) readonly buffer ConstantBlob { uint words[]; } c;
layout(location=0) in vec4 color; layout(location=0) out vec4 outColor;
void main(){
  uvec4 bits=v.vectors[26]+uvec4(u.words[0],u.words[107],c.words[0],c.words[83]);
  outColor=color+uintBitsToFloat(bits);
}
'''

def spirv(source,stage):
    with tempfile.TemporaryDirectory() as td:
        src=Path(td)/('fixture.'+stage); out=Path(td)/'fixture.spv';src.write_text(source)
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.2','-S',stage,
          str(src),'-o',str(out)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout',
          str(out)],check=True,capture_output=True)
        return out.read_bytes()

def invoke(vb,fb):
    vi=C.create_string_buffer(vb);fi=C.create_string_buffer(fb)
    vs,fs,pair,plan,err=Graphics(),Graphics(),Pair(),Plan(),C.create_string_buffer(512)
    rc=compile_pair(vi,len(vb),fi,len(fb),C.byref(vs),C.byref(fs),C.byref(pair),
      C.byref(plan),err,len(err))
    result=(rc,err.value.decode(errors='replace'),vs,fs,pair,plan)
    return result

vb,fb=spirv(VS,'vert'),spirv(FS,'frag')
rc,msg,vs,fs,pair,plan=invoke(vb,fb)
assert rc==0,msg
assert vs.code and fs.code and not vs.tls and not fs.tls and pair.version==2
assert (plan.version,plan.table,plan.resource_count,plan.texture_count,
  plan.constant_bytes,plan.constant_access_bytes)==(1,1,3,0,8,336)
assert [(u.stage,u.descriptor_set,u.binding,u.table,u.index,u.declared_bytes) for u in plan.ubos]==[
 (1,0,0,1,0,320),(2,0,1,1,1,432),(2,0,2,1,2,0)]
free(C.byref(vs));free(C.byref(fs))

bad_cases=[
 (VS.replace('vec4 data[20]','vec4 data[19]').replace('u.data[19]','u.data[18]'),FS,'wrong byte extent'),
 (VS,FS.replace('binding=1','binding=3'),'exact stage binding'),
 (VS,FS.replace('layout(location=0) in vec4 color;',
   'layout(set=0,binding=4) uniform sampler2D image; layout(location=0) in vec4 color;').replace(
   'outColor=color+uintBitsToFloat(bits);',
   'outColor=color+uintBitsToFloat(bits)+texture(image,vec2(0.5));'),
   'exact UBO/constant shape'),
]
for bad_vs,bad_fs,needle in bad_cases:
    rc,msg,vs,fs,pair,plan=invoke(spirv(bad_vs,'vert'),spirv(bad_fs,'frag'))
    assert rc!=0 and needle in msg,(rc,msg,needle)
    assert not vs.code and not fs.code and not pair.version and not plan.version

print('fixed-ui-r80: R82 alias UBO and bounded constant lowering PASS')
