#!/usr/bin/env python3
"""Host-only TGP2 compiler regression; isolate every compiler invocation.

Checks actual machine-code packages and semantic manifests. No GPU execution.
The optional real producer probe consumes the Rust planner's real specialization
and bound-format variant, never fabricated package bytes.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

class Blob(C.Structure):
    _fields_=[('data',C.c_void_p),('size',C.c_size_t)]
class Spec(C.Structure):
    _fields_=[('id',C.c_uint32),('size',C.c_uint32),('data',C.c_uint8*8)]

def compile_one(lib_path,spv,constants=(),legacy=False):
    lib=C.CDLL(str(lib_path));out=Blob();error=C.create_string_buffer(1024)
    data=Path(spv).read_bytes();table=(Spec*len(constants))()
    for i,(ident,hexdata) in enumerate(constants):
        value=bytes.fromhex(hexdata);table[i].id=ident;table[i].size=len(value)
        for j,v in enumerate(value):table[i].data[j]=v
    fn=lib.tegu_mali_compile_specialized if legacy else lib.tegu_mali_compile_compute_v2
    fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
    lib.tegu_mali_free.argtypes=[C.POINTER(Blob)]
    rc=fn(data,len(data),table,len(constants),C.byref(out),error,len(error))
    packet=C.string_at(out.data,out.size) if out.size else b''
    assert not rc or not out.data and not out.size
    lib.tegu_mali_free(C.byref(out))
    result={'rc':rc,'error':error.value.decode(),'bytes':len(packet),'sha256':hashlib.sha256(packet).hexdigest()}
    if packet:
        if legacy:result['header']=list(struct.unpack('<16I',packet[:64]))
        else:
            h=list(struct.unpack('<32I',packet[:128]));result['header']=h
            result['manifest']=[list(struct.unpack('<16I',packet[128+i*64:192+i*64])) for i in range(h[13])]
            # Words 17/18 are the additive per-thread TLS / per-workgroup WLS
            # byte requirements (Valhall limits); 19..31 stay reserved zero.
            assert len(packet)==h[15]+h[2] and h[15]==128+64*h[13] and not any(h[19:])
            assert h[17]<0x8000 and h[18]<=32768
            result['tls_bytes'],result['wls_bytes']=h[17],h[18]
            assert all(not any(r[9:12]+r[13:16]) for r in result['manifest'])
            result['code_sha256']=hashlib.sha256(packet[h[15]:]).hexdigest()
    return result,packet

def api_checks(lib_path,spv):
    lib=C.CDLL(str(lib_path));fn=lib.tegu_mali_compile_compute_v2
    fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
    lib.tegu_mali_free.argtypes=[C.POINTER(Blob)]
    data=Path(spv).read_bytes();storage=C.create_string_buffer(b'X'+data);address=C.addressof(storage)+1
    out=Blob();err=C.create_string_buffer(256);checks=[]
    rc=fn(address,len(data),None,0,C.byref(out),err,256)
    packet=C.string_at(out.data,out.size) if out.size else b'';lib.tegu_mali_free(C.byref(out))
    aligned,reference=compile_one(lib_path,spv)
    checks.append(rc==0 and packet==reference)
    out=Blob(1234,8);rc=fn(address,len(data),None,0,C.byref(out),err,256)
    checks.append(rc==2 and out.data==1234 and out.size==8)
    out=Blob();original=storage.raw
    rc=fn(address,len(data),None,0,C.cast(address,C.POINTER(Blob)),err,256)
    checks.append(rc==2 and storage.raw==original)
    rc=fn(address,len(data),None,0,C.byref(out),address,64)
    checks.append(rc==2 and storage.raw==original and not out.data and not out.size)
    spec=(Spec*1)();spec[0].id=100;spec[0].size=4;original=C.string_at(spec,C.sizeof(spec))
    rc=fn(address,len(data),spec,1,C.cast(spec,C.POINTER(Blob)),err,256)
    checks.append(rc==2 and C.string_at(spec,C.sizeof(spec))==original)
    rc=fn(address,len(data),spec,1,C.byref(out),spec,C.sizeof(spec))
    checks.append(rc==2 and C.string_at(spec,C.sizeof(spec))==original)
    rc=fn(address,len(data),None,65,C.byref(out),err,256)
    checks.append(rc==2 and not out.data and not out.size)
    malformed=C.create_string_buffer(data[:20]+bytes(4));rc=fn(malformed,24,None,0,C.byref(out),err,256)
    checks.append(rc==2 and not out.data and not out.size and b'instruction length' in err.value)
    physical=bytearray(data)
    for offset in range(20,len(data),4):
        word=struct.unpack_from('<I',data,offset)[0]
        if word==((3<<16)|14):
            struct.pack_into('<I',physical,offset+4,5348);break
    physical=C.create_string_buffer(bytes(physical));rc=fn(physical,len(data),None,0,C.byref(out),err,256)
    checks.append(rc==4 and not out.data and not out.size and b'physical pointers' in err.value)
    return {'checks':checks,'passed':all(checks)}

def worker(args):
    spec=json.loads(Path(args.spec).read_text()) if args.spec else []
    result,packet=compile_one(args.compiler,args.spv,spec,args.legacy)
    if args.packet and packet:Path(args.packet).write_bytes(packet)
    print(json.dumps(result));return

def invoke(compiler,spv,out,spec=(),legacy=False,packet=None):
    specfile=out/(spv.stem+'.spec.json');specfile.write_text(json.dumps(spec))
    cmd=[sys.executable,__file__,'--worker','--compiler',str(compiler),'--spv',str(spv),'--spec',str(specfile)]
    if legacy:cmd+=['--legacy']
    if packet:cmd+=['--packet',str(packet)]
    run=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    (out/(spv.stem+('.legacy' if legacy else '')+'.log')).write_text(run.stdout)
    if run.returncode: return {'process_exit':run.returncode,'error':run.stdout[-2000:]}
    return json.loads(run.stdout.strip().splitlines()[-1])

def synthetic_cases():
    from check_compute_texture_boundary import compute_cases
    for name,decl,body,_,_ in compute_cases():
        yield name,decl,body,4 if name in ('gradient_min_lod','sparse','descriptor_array') else 0
    yield 'separate_high_bindings','layout(set=0,binding=255) uniform texture2D t; layout(set=0,binding=31) uniform sampler s;','outb.value=textureLod(sampler2D(t,s),uv.xy,0);',0
    for kind,coord in (('1DArray','uv.xy'),('CubeArray','uv')):
        yield kind+'_texture','layout(set=0,binding=3) uniform sampler'+kind+' s;',f'outb.value=textureLod(s,{coord},0);',0
    for kind,coord in (('1DArray','ivec2(uv.xy)'),('Cube','ivec3(uv.xyz)'),('CubeArray','ivec3(uv.xyz)'),('2DMSArray','ivec3(uv.xyz)')):
        decl='layout(rgba32f,set=0,binding=3) uniform image'+kind+' im;'
        sample=',0' if kind=='2DMSArray' else ''
        yield kind+'_storage_read',decl,'outb.value=imageLoad(im,'+coord+sample+');',0
        yield kind+'_storage_write',decl,'imageStore(im,'+coord+sample+',uv);',0
    yield 'sampler_mixed_compare','layout(set=0,binding=3) uniform texture2D t; layout(set=0,binding=5) uniform sampler s;','outb.value=textureLod(sampler2D(t,s),uv.xy,0)+texture(sampler2DShadow(t,s),uv.xyz);',4
    yield 'native_binding_256','layout(set=0,binding=256) uniform sampler2D s;','outb.value=textureLod(s,uv.xy,0);',4
    yield 'set_one','layout(set=1,binding=1) uniform sampler2D s;','outb.value=textureLod(s,uv.xy,0);',4
    yield 'image_buffer','layout(rgba32f,set=0,binding=3) uniform imageBuffer im;','outb.value=imageLoad(im,int(uv.x));',0
    yield 'image_read_write','layout(r32ui,set=0,binding=3) uniform uimage2D im;','imageStore(im,ivec2(uv.xy),imageLoad(im,ivec2(uv.xy)));',0
    yield 'buffer_read_write','','outb.value+=uv;',0
    yield 'ubo','layout(set=0,binding=7) uniform U {vec4 x;} u;','outb.value=u.x;',0
    yield 'ssbo_atomic','layout(set=0,binding=7) buffer B {uint x;} b;','outb.value=vec4(atomicAdd(b.x,1));',0
    yield 'spec_resource','layout(constant_id=100) const bool live=false; layout(set=0,binding=3) uniform sampler2D s;','outb.value=live?textureLod(s,uv.xy,0):uv;',0
    for op in ('Add','Min','Max'):
        ext='#extension GL_EXT_shader_atomic_float : require\n#extension GL_EXT_shader_atomic_float2 : require\n'
        yield 'float_atomic_'+op,ext+'layout(set=0,binding=7) buffer B {float x;} b;',f'outb.value=vec4(atomic{op}(b.x,1.0));',0
        yield 'float_image_atomic_'+op,ext+'layout(r32f,set=0,binding=3) uniform image2D im;',f'outb.value=vec4(imageAtomic{op}(im,ivec2(uv.xy),1.0));',0
    yield 'float64_atomic_add','#extension GL_EXT_shader_atomic_float : require\nlayout(set=0,binding=7) buffer B {double x;} b;','outb.value=vec4(atomicAdd(b.x,1.0lf));',4
    ext64='#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require\n#extension GL_EXT_shader_atomic_int64 : require\n'
    for op in ('Add','Min','Max','And','Or','Xor','Exchange'):
        yield 'uint64_atomic_'+op,ext64+'layout(set=0,binding=7) buffer B {uint64_t x;} b;',f'outb.value=vec4(atomic{op}(b.x,uint64_t(1)));',0
    yield 'uint64_atomic_cas',ext64+'layout(set=0,binding=7) buffer B {uint64_t x;} b;','outb.value=vec4(atomicCompSwap(b.x,uint64_t(1),uint64_t(2)));',0

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True);p.add_argument('--out',type=Path)
    p.add_argument('--baseline',type=Path);p.add_argument('--worker',action='store_true')
    p.add_argument('--api-worker',action='store_true');p.add_argument('--spv',type=Path);p.add_argument('--spec');p.add_argument('--legacy',action='store_true');p.add_argument('--packet')
    a=p.parse_args();a.compiler=a.compiler.resolve()
    if a.api_worker:print(json.dumps(api_checks(a.compiler,a.spv)));return
    if a.worker:return worker(a)
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);rows=[]
    for name,decl,body,expected in synthetic_cases():
        source=out/(name+'.comp');spv=out/(name+'.spv')
        source.write_text('#version 450\n'+decl+'\nlayout(local_size_x=1) in;\nlayout(set=0,binding=0) buffer Out { vec4 value; } outb;\nvoid main() { vec4 uv=vec4(gl_GlobalInvocationID,1); '+body+' }\n')
        subprocess.run(['glslangValidator','-V',str(source),'-o',str(spv)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.0',str(spv)],check=True,capture_output=True)
        r=invoke(a.compiler,spv,out,packet=out/(name+'.tgp2'))
        checks=[r.get('rc')==expected]
        if r.get('rc')==0:
            m=r['manifest'];by={(x[1],x[2]):x for x in m}
            if (0,2) in by:checks+=[by[0,2][4]==(0x300 if name=='buffer_read_write' else 0x200)]
            if '_image_read' in name:checks+=[by[3,4][4]==0x900,by[3,4][8]==109]
            if '_image_write' in name:checks+=[by[3,4][4]==0xa00,by[3,4][8]==109]
            if name=='image_atomic':checks+=[by[3,4][4]==0x1b00,by[3,4][8]==98]
            if name=='image_read_write':checks+=[by[3,4][4]==0xb00]
            if name=='ssbo_atomic' or name.startswith(('float_atomic_','uint64_atomic_')):checks+=[by[7,2][4]==0x1300]
            if name.startswith('float_image_atomic_'):checks+=[by[3,4][4]==0x1b00,by[3,4][8]==100]
            if name=='ubo':checks+=[by[7,1][4]==0x100]
            if name=='separate_high_bindings':checks+=[by[255,3][5:7]==[1,255],by[31,5][5:7]==[2,31]]
            if name=='separate_fetch':checks+=[(0xffffffff,5) in by,by[0xffffffff,5][5:8]==[2,256,1],(5,5) not in by]
            if name=='shadow':checks+=[by[3,3][12]&64!=0,by[3,5][7]&4!=0]
            if name=='array_fetch':checks+=[by[3,3][12]&16!=0]
            if name=='spec_resource':checks+=[len(m)==1]
        row={'case':name,'expected':expected,'passed':all(checks),'result':r};rows.append(row);print(json.dumps(row),flush=True)
    spv=out/'spec_resource.spv'
    for name,spec,expected in [('enabled',[(100,'01000000')],0),('bad_width',[(100,'01')],2),('unknown',[(101,'00000000')],2),('duplicate',[(100,'00000000'),(100,'00000000')],2)]:
        r=invoke(a.compiler,spv,out,spec=spec)
        ok=r.get('rc')==expected and (name!='enabled' or len(r.get('manifest',[]))==3)
        rows.append({'case':'specialization_'+name,'passed':ok,'result':r})
    if a.baseline:
        for name in ('buffer_only','unused_texture','buffer_read_write','ubo','ssbo_atomic'):
            spv=out/(name+'.spv');r=invoke(a.compiler,spv,out,legacy=True);base=invoke(a.baseline.resolve(),spv,out,legacy=True)
            rows.append({'case':'legacy_exact_'+name,'passed':r==base,'result':r,'baseline':base})
    api=subprocess.run([sys.executable,__file__,'--api-worker','--compiler',str(a.compiler),'--spv',str(out/'buffer_only.spv')],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    api_result=json.loads(api.stdout.strip().splitlines()[-1]) if not api.returncode else {'passed':False,'error':api.stdout}
    rows.append({'case':'api_span_alias_unaligned','passed':api_result['passed'],'result':api_result})
    for name in ('float_atomic_Add','float_atomic_Min','float_atomic_Max'):
        r=invoke(a.compiler,out/(name+'.spv'),out,legacy=True)
        rows.append({'case':'legacy_safe_'+name,'passed':r.get('rc')==4 and 'TGP2 CAS' in r.get('error',''),'result':r})
    report={'compiler_sha256':hashlib.sha256(a.compiler.read_bytes()).hexdigest(),'gpu_executed':False,'cases':rows}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    passed=sum(x['passed'] for x in rows);print(f'{passed}/{len(rows)} TGP2 compiler checks passed; GPU_EXECUTED=0')
    if passed!=len(rows):raise SystemExit(1)
if __name__=='__main__':main()
