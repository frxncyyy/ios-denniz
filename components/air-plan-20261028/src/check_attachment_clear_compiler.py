#!/usr/bin/env python3
"""Check the exact attachment-clear compiler class using caller-provided fixtures.

Requires the qualified VS/FS SPIR-V pair and an authored texture fixture; no
shader or OS binaries are embedded. This checks host codegen, not GPU execution.
"""
import argparse
import ctypes as C
import hashlib, json, struct, subprocess, tempfile
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
for option in ('library','vertex','fragment','texture-fragment'):
    parser.add_argument('--'+option,type=Path,required=True)
args=parser.parse_args()
VS,FS,LIB=args.vertex,args.fragment,args.library.resolve()
U32=C.c_uint32
class Graphics(C.Structure):
    _fields_=[('code',C.POINTER(C.c_ubyte)),('size',C.c_size_t),
      ('stage',U32),('registers',U32),('tls',U32),('fau_words',U32),
      ('sysval_mask',U32),('push_bytes',U32),('preload',C.c_uint64)]
class Resource(C.Structure):
    _fields_=[(x,U32) for x in ('role','stage','descriptor_set','binding','source_class',
      'backend_class','table','index','declared_bytes','offset_push_byte','offset_alignment',
      'descriptor_base_alignment','window_bytes','access')]
class Plan(C.Structure):
    _fields_=[(x,U32) for x in ('version','output_count','rt0_write_mask','rt1_write_mask',
      'position_offset','secondary_offset','secondary_enabled','generic_size_B','draw_vertex_count',
      'draw_instance_count','draw_base_instance','draw_indexed','target0_format','target1_format',
      'target0_blend_enabled','target1_blend_enabled','target_descriptors_seen','resource_count')]+[
      ('resources',Resource*2)]
assert C.sizeof(Graphics)==48 and C.sizeof(Resource)==56 and C.sizeof(Plan)==184
lib=C.CDLL(str(LIB))
fn=lib.tegu_mali_compile_attachment_clear_pair_v2
fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,
             C.c_void_p,C.c_void_p,C.c_size_t]
fn.restype=C.c_int
free=lib.tegu_mali_graphics_free;free.argtypes=[C.POINTER(Graphics)]
def compile(vbytes,fbytes):
    vb=C.create_string_buffer(vbytes);fb=C.create_string_buffer(fbytes)
    v,f,p=Graphics(),Graphics(),Plan();err=C.create_string_buffer(512)
    rc=fn(vb,len(vbytes),fb,len(fbytes),C.byref(v),C.byref(f),C.byref(p),err,len(err))
    if rc==0:
        assert p.version==2 and p.output_count==2 and (p.rt0_write_mask,p.rt1_write_mask)==(0,15)
        assert (p.position_offset,p.secondary_offset,p.secondary_enabled,p.generic_size_B)==(896,1792,0,0)
        assert (p.draw_vertex_count,p.draw_instance_count,p.draw_base_instance,p.draw_indexed)==(3,1,0,0)
        assert (p.target0_format,p.target1_format,p.target0_blend_enabled,p.target1_blend_enabled)==(80,80,0,0)
        assert p.target_descriptors_seen==3
        assert p.resource_count==2
        assert [(r.role,r.stage,r.descriptor_set,r.binding,r.source_class,r.backend_class,
                 r.table,r.index,r.declared_bytes,r.offset_push_byte,r.offset_alignment,
                 r.descriptor_base_alignment,r.window_bytes,r.access) for r in p.resources]==[
            (1,1,0,0,1,1,1,0,0,0,4,16,16,1),(2,1,0,1,2,1,1,1,320,4,16,16,320,1)]
        assert (v.size,v.stage,v.registers,v.tls,v.fau_words,v.sysval_mask,v.push_bytes,v.preload)==(
            1792,1,32,0,34,3,8,0x3800000000000000)
        assert (f.size,f.stage,f.registers,f.tls,f.fau_words,f.sysval_mask,f.push_bytes,f.preload)==(
            256,2,32,0,16,8,72,0x3000000000000000)
        vh=C.string_at(v.code,v.size);fh=C.string_at(f.code,f.size)
        assert hashlib.sha256(vh).hexdigest()=='f65b226eb26a30602897f2577b9743916e739df6200b2dab81f45b7e07b7038b'
        assert hashlib.sha256(fh).hexdigest()=='03209c66f0d2181a8b540bcc7d7c3e5e89bc7012f6e856d5c6da426a36dbfab6'
        free(C.byref(v));free(C.byref(f))
    else:
        assert not v.code and not v.size and not f.code and not f.size and p.version==0
    return rc,err.value.decode()
def words(data): return list(struct.unpack('<%dI'%(len(data)//4),data))
def decorate_mutation(data,decoration,old,new):
    w=words(data);i=5;changed=0
    while i<len(w):
        wc=w[i]>>16;op=w[i]&0xffff
        if not wc or i+wc>len(w):raise AssertionError('malformed SPIR-V')
        if op==71 and wc>=4 and w[i+2]==decoration and w[i+3]==old:
            w[i+3]=new;changed+=1
        i+=wc
    assert changed==1,(decoration,changed)
    return struct.pack('<%dI'%len(w),*w)
def decoration_kind_mutation(data,old,new):
    w=words(data);i=5;changed=0
    while i<len(w):
        wc=w[i]>>16;op=w[i]&0xffff
        if not wc or i+wc>len(w):raise AssertionError('malformed SPIR-V')
        if op==71 and wc==3 and w[i+2]==old:
            w[i+2]=new;changed+=1
        i+=wc
    assert changed==1,('decoration kind',changed)
    return struct.pack('<%dI'%len(w),*w)
def remove_decoration(data,decoration):
    w=words(data);i=5;out=w[:5];removed=0
    while i<len(w):
        wc=w[i]>>16;op=w[i]&0xffff
        if not wc or i+wc>len(w):raise AssertionError('malformed SPIR-V')
        ins=w[i:i+wc]
        if op==71 and wc>=3 and ins[2]==decoration:removed+=1
        else:out.extend(ins)
        i+=wc
    assert removed==1,('decoration removal',removed)
    return struct.pack('<%dI'%len(out),*out)
def add_ssbo_write(data,operation):
    with tempfile.TemporaryDirectory() as td:
        binary=Path(td)/'input.spv';assembly=Path(td)/'input.spvasm';output=Path(td)/'output.spv'
        binary.write_bytes(data)
        text=subprocess.check_output(['spirv-dis',str(binary)],text=True)
        marker='               OpDecorate %17 NonWritable\n'
        assert text.count(marker)==1
        text=text.replace(marker,'')
        if operation=='store':
            insertion='               OpStore %79 %80\n'
        elif operation=='atomic':
            insertion='        %209 = OpAtomicIAdd %uint %79 %uint_1 %uint_0 %uint_1\n'
        else: raise AssertionError(operation)
        marker='               OpReturn\n'
        assert text.count(marker)==1
        assembly.write_text(text.replace(marker,insertion+marker))
        subprocess.run(['spirv-as','--preserve-numeric-ids','--target-env','vulkan1.2',
                        str(assembly),'-o',str(output)],check=True)
        result=output.read_bytes()
        subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout',
                        str(output)],check=True)
        return result
def glsl_fragment(source,opengl=False):
    with tempfile.TemporaryDirectory() as td:
        src=Path(td)/'fixture.frag';out=Path(td)/'fixture.spv';src.write_text(source)
        args=['glslangValidator','-G' if opengl else '-V']
        if not opengl:args+=['--target-env','vulkan1.2']
        subprocess.run(args+['-S','frag',str(src),'-o',str(out)],check=True,capture_output=True)
        if not opengl:subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout',str(out)],check=True,capture_output=True)
        return out.read_bytes()
def change_uint_constant(data,old,new):
    with tempfile.TemporaryDirectory() as td:
        binary=Path(td)/'input.spv';assembly=Path(td)/'input.spvasm';output=Path(td)/'output.spv'
        binary.write_bytes(data);text=subprocess.check_output(['spirv-dis',str(binary)],text=True)
        oldline=f'%uint_{old} = OpConstant %uint {old}'
        newline=f'%uint_{old} = OpConstant %uint {new}'
        assert text.count(oldline)==1
        assembly.write_text(text.replace(oldline,newline))
        subprocess.run(['spirv-as','--preserve-numeric-ids','--target-env','vulkan1.2',str(assembly),'-o',str(output)],check=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout',str(output)],check=True)
        return output.read_bytes()
vs=VS.read_bytes();fs=FS.read_bytes()
assert subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout',str(VS)]).returncode==0
assert subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout',str(FS)]).returncode==0
rc,msg=compile(vs,fs);assert rc==0,(rc,msg)
# Fragment output location1 moved to unsupported location2; compiler must refuse cleanly.
fs_bad=decorate_mutation(fs,30,1,2)
assert subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout','-'],input=fs_bad).returncode==0
rc,msg_location=compile(vs,fs_bad);assert rc!=0 and msg_location
# Keep declarations and metadata unchanged while changing the rectangle load
# extent. The API must reject the resulting different machine bytes by digest.
vs_code_mutation=change_uint_constant(vs,8,24)
rc,msg_code=compile(vs_code_mutation,fs);assert rc!=0 and 'code bytes differ' in msg_code
# Path-blit storage block binding0 changed to2; exact resource lowering must refuse.
vs_bad=decorate_mutation(vs,33,0,2) # Decoration Binding=33
assert subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout','-'],input=vs_bad).returncode==0
rc,msg_binding=compile(vs_bad,fs);assert rc!=0 and msg_binding
# Remove NonWritable by changing that decoration to NonReadable (valid SPIR-V, disallowed access).
vs_read_forbidden=decoration_kind_mutation(vs,24,25)
assert subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout','-'],input=vs_read_forbidden).returncode==0
rc,msg_access=compile(vs_read_forbidden,fs);assert rc!=0 and msg_access
# Removing NonWritable makes the declared source block writable. This remains
# valid SPIR-V and must not be accepted by the exact readonly class.
vs_writable=remove_decoration(vs,24)
assert subprocess.run(['spirv-val','--target-env','vulkan1.2','--uniform-buffer-standard-layout','-'],input=vs_writable).returncode==0
rc,msg_writable=compile(vs_writable,fs);assert rc!=0 and msg_writable
vs_store=add_ssbo_write(vs,'store')
rc,msg_store=compile(vs_store,fs);assert rc!=0 and msg_store
vs_atomic=add_ssbo_write(vs,'atomic')
rc,msg_atomic=compile(vs_atomic,fs);assert rc!=0 and msg_atomic
# A valid texture-using fragment fixture exercises the unsupported-resource
# refusal path; it is intentionally not treated as an output-layout fixture.
tex=args.texture_fragment
tex_bytes=tex.read_bytes()
assert subprocess.run(['spirv-val','--target-env','vulkan1.2',str(tex)]).returncode==0
rc,msg_texture=compile(vs,tex_bytes);assert rc!=0 and msg_texture
# Fragment SSBO must not inherit the vertex-only readonly SSBO lowering.
fs_ssbo=glsl_fragment('''#version 450
layout(set=0,binding=0,std430) readonly buffer Bad { uint x; } bad;
layout(location=0) out vec4 c0; layout(location=1) out vec4 c1;
void main(){ c0=vec4(0.0); c1=vec4(float(bad.x)); }
''')
rc,msg_fs_ssbo=compile(vs,fs_ssbo);assert rc!=0 and msg_fs_ssbo
# Exact MRT class requires vec4 float32 and output index0 at both locations.
fs_vec3=glsl_fragment('''#version 450
layout(location=0) out vec4 c0; layout(location=1) out vec3 c1;
void main(){ c0=vec4(0.0); c1=vec3(0.0); }
''')
rc,msg_fs_vec3=compile(vs,fs_vec3);assert rc!=0 and msg_fs_vec3
def add_index1_decoration(data):
    w=words(data);i=5;target=None
    while i<len(w):
        wc=w[i]>>16;op=w[i]&0xffff
        if not wc or i+wc>len(w):raise AssertionError('malformed SPIR-V')
        if op==71 and wc==4 and w[i+2]==30 and w[i+3]==1:target=w[i+1]
        i+=wc
    assert target is not None
    # OpDecorate target Index 1 (Decoration Index=32), immediately after
    # existing annotations. The translator's output-variable index gate must
    # reject this dual-source interface before backend code generation.
    i=5;insert=None
    while i<len(w):
        wc=w[i]>>16;op=w[i]&0xffff
        if op==71 and wc==4 and w[i+1]==target and w[i+2]==30 and w[i+3]==1:
            insert=i+wc;break
        i+=wc
    assert insert is not None
    w[insert:insert]=[(4<<16)|71,target,32,1]
    return struct.pack('<%dI'%len(w),*w)
fs_dual=add_index1_decoration(fs)
rc,msg_fs_dual=compile(vs,fs_dual);assert rc!=0 and msg_fs_dual
# All rejected aliases preserve every byte in every affected region. In
# particular diagnostics must not be initialized until preflight is complete.
def raw_call(vptr,vlen,fptr,flen,vout,fout,pout,eout,elen):
    return fn(vptr,vlen,fptr,flen,vout,fout,pout,eout,elen)
def snap(buf): return C.string_at(C.addressof(buf),C.sizeof(buf))
def rejected_unchanged(vbuf,fbuf,vlen,flen,vout,fout,pout,eout,elen,watched):
    before=[snap(x) for x in watched]
    rc=raw_call(C.addressof(vbuf),vlen,C.addressof(fbuf),flen,vout,fout,pout,eout,elen)
    assert rc!=0
    assert [snap(x) for x in watched]==before

vb=C.create_string_buffer(vs);fb=C.create_string_buffer(fs)
vout=Graphics();fout=Graphics();pout=Plan();err=C.create_string_buffer(b'E'*128)
for alias in ('error_vs','error_fs','error_vsout','error_fsout','error_plan',
              'vsout_input','fsout_input','plan_input','outputs'):
    vv= C.create_string_buffer(vs); ff=C.create_string_buffer(fs)
    vo=Graphics(); fo=Graphics(); po=Plan(); ee=C.create_string_buffer(b'Q'*128)
    vp,fp,pp,ep=map(C.addressof,(vo,fo,po,ee)); elen=C.sizeof(ee)
    watched=[vv,ff,vo,fo,po,ee]
    if alias=='error_vs': ep=C.addressof(vv);elen=32
    elif alias=='error_fs': ep=C.addressof(ff);elen=32
    elif alias=='error_vsout': ep=vp;elen=16
    elif alias=='error_fsout': ep=fp;elen=16
    elif alias=='error_plan': ep=pp;elen=16
    elif alias=='vsout_input': vp=C.addressof(vv)
    elif alias=='fsout_input': fp=C.addressof(ff)
    elif alias=='plan_input': pp=C.addressof(vv)
    elif alias=='outputs': fp=vp
    rejected_unchanged(vv,ff,len(vs),len(fs),vp,fp,pp,ep,elen,watched)
# Partial overlap between distinct output structures.
region=C.create_string_buffer(C.sizeof(Graphics)+C.sizeof(Plan)+32)
region_before=snap(region); vv=C.create_string_buffer(vs);ff=C.create_string_buffer(fs)
rc=raw_call(C.addressof(vv),len(vs),C.addressof(ff),len(fs),C.addressof(region),
 C.addressof(region)+C.sizeof(Graphics)-8,C.addressof(region)+C.sizeof(Graphics)+C.sizeof(Plan),0,0)
assert rc!=0 and snap(region)==region_before
# Malformed integer ranges are rejected before dereference or output mutation.
for ptr,length,which in ((C.c_void_p((1<<(8*C.sizeof(C.c_void_p)))-8),32,'input'),
                         (C.c_void_p((1<<(8*C.sizeof(C.c_void_p)))-8),32,'output')):
    vo=Graphics();fo=Graphics();po=Plan();ee=C.create_string_buffer(b'Z'*32)
    before=(snap(vo),snap(fo),snap(po),snap(ee))
    if which=='input':
        rc=raw_call(ptr,length,C.addressof(fb),len(fs),C.addressof(vo),C.addressof(fo),C.addressof(po),C.addressof(ee),32)
    else:
        rc=raw_call(C.addressof(vb),len(vs),C.addressof(fb),len(fs),ptr,C.addressof(fo),C.addressof(po),C.addressof(ee),32)
    assert rc!=0 and (snap(vo),snap(fo),snap(po),snap(ee))==before
print(json.dumps({'positive':'pass','vertex_binary_sha256':'f65b226eb26a30602897f2577b9743916e739df6200b2dab81f45b7e07b7038b',
 'fragment_binary_sha256':'03209c66f0d2181a8b540bcc7d7c3e5e89bc7012f6e856d5c6da426a36dbfab6',
 'fragment_location2_refusal':msg_location,'storage_binding_refusal':msg_binding,
 'readonly_access_refusal':msg_access,'alias_preflight':'pass','alias_cases':9,
 'writable_buffer_refusal':msg_writable,'texture_fragment_refusal':msg_texture,
 'ssbo_store_refusal':msg_store,'ssbo_atomic_refusal':msg_atomic,
 'fragment_ssbo_refusal':msg_fs_ssbo,'output_vec3_refusal':msg_fs_vec3,'dual_source_index_refusal':msg_fs_dual,
 'exact_code_identity_refusal':msg_code,
 'resource_roles':'original rect StorageBuffer role maps to bounded backend UBO; matrix remains UBO'}))
