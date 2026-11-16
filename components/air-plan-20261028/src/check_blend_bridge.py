#!/usr/bin/env python3
"""Check actual AIR blend compilation, native metadata gates and C ABI ownership.

Host-only. Shader/metadata success is not a GPU submission claim.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess


class Blob(C.Structure):
    _fields_=[('bytes',C.c_void_p),('length',C.c_size_t)]


class Attribute(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('index','format','offset','buffer_index')]


class Layout(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('index','stride','step_function','step_rate')]


class Blend(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('version','enabled','rgb_op','rgb_src','rgb_dst',
        'alpha_op','alpha_src','alpha_dst','write_mask')]


class Push(C.Structure):
    _fields_=[('version',C.c_uint32),('vertex_span',C.c_uint32),('fragment_span',C.c_uint32),
              ('vertex',C.c_ubyte*128),('fragment',C.c_ubyte*128)]


class FetchAttribute(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('location','buffer_index','format','offset','stride','input_rate','divisor')]


class Fetch(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('version','mask','count','reserved')]+[('attributes',FetchAttribute*31)]


class BlendState(C.Structure):
    _fields_=[('words',C.c_uint32*4),('reads_destination',C.c_uint32),('opaque',C.c_uint32)]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('bridge','air','profile','legacy','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    lib=C.CDLL(str(a.bridge.resolve()));err=C.create_string_buffer(2048)
    lib.tegu_air_library_open.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t]
    lib.tegu_air_library_open.restype=C.c_void_p
    lib.tegu_air_library_close.argtypes=[C.c_void_p]
    lib.tegu_air_render_pipeline_close.argtypes=[C.c_void_p]
    lib.tegu_air_blob_free.argtypes=[C.POINTER(Blob)]
    legacy=lib.tegu_air_render_pipeline_create_with_vertex_descriptor
    legacy.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,C.c_size_t,C.c_uint32,
        C.POINTER(Attribute),C.c_size_t,C.POINTER(Layout),C.c_size_t,C.c_void_p,C.c_size_t,
        C.POINTER(Blob),C.POINTER(Blob),C.POINTER(Blob),C.c_void_p,C.c_size_t]
    legacy.restype=C.c_void_p
    create=lib.tegu_air_render_pipeline_create_with_blend
    create.argtypes=legacy.argtypes[:11]+[C.POINTER(Blend)]+legacy.argtypes[11:]
    create.restype=C.c_void_p
    gate=lib.tegu_air_render_pipeline_native_check_with_blend
    gate.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Blend),C.c_void_p,C.c_size_t];gate.restype=C.c_int
    old=[]
    for n in ('native_check','native_check_with_vertex_fetch'):
        fn=getattr(lib,'tegu_air_render_pipeline_'+n);fn.argtypes=[C.c_void_p,C.c_uint32,C.c_void_p,C.c_size_t];fn.restype=C.c_int;old.append(fn)
    push=lib.tegu_air_render_pipeline_push
    push.argtypes=[C.c_void_p,C.POINTER(Push),C.c_void_p,C.c_size_t];push.restype=C.c_int
    fetch=lib.tegu_air_render_pipeline_vertex_fetch
    fetch.argtypes=[C.c_void_p,C.POINTER(Fetch),C.c_void_p,C.c_size_t];fetch.restype=C.c_int
    raw=a.air.read_bytes();profile=a.profile.read_bytes()
    library=lib.tegu_air_library_open(raw,len(raw),err,len(err));assert library,err.value
    attrs=(Attribute*2)(Attribute(7,31,0,1),Attribute(8,31,16,1));layouts=(Layout*1)(Layout(1,32,1,1))
    base=[library,b'varying_vertex',14,library,b'varying_fragment',16,70,attrs,2,layouts,1]
    handles=[];summary={}
    def compile_blend(name,b,fmt=70):
        args=list(base);args[6]=fmt;outputs=[Blob(),Blob(),Blob()]
        h=create(*args,C.byref(b) if b is not None else None,profile,len(profile),*map(C.byref,outputs),err,len(err))
        if not h:
            assert err.value and all(not v.bytes and not v.length for v in outputs)
            return None,err.value
        assert not err.value;handles.append(h)
        result={}
        for n,v in zip(('vertex','fragment','metadata'),outputs):
            binary=C.string_at(v.bytes,v.length);result[n]=binary
            path=a.out/f'{name}-{n}.{ "json" if n=="metadata" else "spv"}'
            path.write_bytes(binary)
            if n!='metadata':subprocess.run(['spirv-val','--target-env','vulkan1.2',str(path)],check=True)
            lib.tegu_air_blob_free(C.byref(v))
        return h,result
    try:
        modes={'disabled':Blend(1,0,0,1,0,0,1,0,15),'opaque':Blend(1,1,0,1,0,0,1,0,15),
            'opaque-subtract':Blend(1,1,1,1,0,1,1,0,15),
            'source-alpha':Blend(1,1,0,4,5,0,4,5,15),'premultiplied':Blend(1,1,0,1,5,0,1,5,15),
            'separate':Blend(1,1,0,4,5,0,1,5,15)}
        for name,b in modes.items():
            h,result=compile_blend(name,b);assert h,(name,result)
            assert gate(h,70,C.byref(b),err,len(err))==0,(name,err.value)
            snapshot=Blend.from_buffer_copy(bytes(b));b.rgb_src=0 if b.rgb_src else 1
            assert gate(h,70,C.byref(snapshot),err,len(err))==0
            assert gate(h,70,C.byref(b),err,len(err))==-1
            C.memmove(C.byref(b),C.byref(snapshot),C.sizeof(b))
            meta=json.loads(result['metadata']);actual=meta['graphics']['color_attachments'][0]['blend']
            vf={0:0,1:1,2:2,3:3,4:6,5:7,6:4,7:5,8:8,9:9}
            expected=None if name in ('disabled','opaque') else dict(src_color=vf[b.rgb_src],dst_color=vf[b.rgb_dst],color_op=b.rgb_op,
                src_alpha=vf[b.alpha_src],dst_alpha=vf[b.alpha_dst],alpha_op=b.alpha_op)
            assert actual==expected,(name,actual,expected)
            assert meta['graphics']['color_attachments'][0]['write_mask']==15
            reflected=Push();assert not push(h,C.byref(reflected),err,len(err))
            rounded=name in ('disabled','opaque')
            assert reflected.vertex_span==0 and reflected.fragment_span==(68 if rounded else 0)
            assert bytes(reflected.fragment)[64:68]==(bytes.fromhex('22220000') if rounded else bytes(4))
            push_data=dict(version=reflected.version,vertex_span=reflected.vertex_span,fragment_span=reflected.fragment_span,
                vertex=bytes(reflected.vertex).hex(),fragment=bytes(reflected.fragment).hex())
            (a.out/f'{name}-push.json').write_text(json.dumps(push_data,indent=2)+'\n')
            plan=Fetch();assert not fetch(h,C.byref(plan),err,len(err))
            assert plan.version==1 and plan.mask==3 and plan.count==2 and not plan.reserved
            attributes=[{n:getattr(row,n) for n,_ in FetchAttribute._fields_} for row in plan.attributes[:2]]
            assert attributes==[dict(location=i,buffer_index=1,format=109,offset=16*i,stride=32,input_rate=0,divisor=1) for i in range(2)]
            (a.out/f'{name}-fetch.json').write_text(json.dumps(dict(version=1,mask=3,count=2,attributes=attributes),indent=2)+'\n')
            assert old[1](h,70,err,len(err))==(-1 if b.enabled else 0)
            assert old[0](h,70,err,len(err))==-1  # Vertex fetch always needs its gate.
            other=Blend.from_buffer_copy(bytes(b));other.rgb_src=0 if b.rgb_src else 1
            if b.enabled:assert gate(h,70,C.byref(other),err,len(err))==-1
            assert gate(h,80,C.byref(b),err,len(err))==-1
            summary[name]=dict(vertex_sha256=hashlib.sha256(result['vertex']).hexdigest(),
                fragment_sha256=hashlib.sha256(result['fragment']).hexdigest(),compiled_blend=actual)
            if name=='disabled':
                for n in ('vertex','fragment'):
                    assert result[n]==(a.legacy/f'70-{n}.spv').read_bytes(),n
        # Real old ABI still produces the same linked shaders and metadata.
        outputs=[Blob(),Blob(),Blob()]
        h=legacy(*base,profile,len(profile),*map(C.byref,outputs),err,len(err));assert h;handles.append(h)
        for name,v in zip(('vertex','fragment','metadata'),outputs):
            actual=C.string_at(v.bytes,v.length)
            if name=='metadata':assert json.loads(actual)==json.loads((a.legacy/'70-metadata.json').read_bytes())
            else:assert actual==(a.legacy/f'70-{name}.spv').read_bytes()
            lib.tegu_air_blob_free(C.byref(v))
        for field,_ in Blend._fields_:
            invalid=Blend.from_buffer_copy(bytes(modes['source-alpha']));setattr(invalid,field,0xffffffff)
            assert compile_blend('invalid',invalid)[0] is None
        assert compile_blend('null',None)[0] is None
        # BGRA8 is now admitted for the same bounded real blend equations; a
        # mismatched format argument remains rejected by the metadata gate.
        bgra, bgra_result=compile_blend('source-alpha-bgra',modes['source-alpha'],80)
        assert bgra and gate(bgra,80,C.byref(modes['source-alpha']),err,len(err))==0
        assert gate(bgra,70,C.byref(modes['source-alpha']),err,len(err))==-1
        bgra_attachment=json.loads(bgra_result['metadata'])['graphics']['color_attachments'][0]
        assert bgra_attachment['format']==44
        assert bgra_attachment['blend']==summary['source-alpha']['compiled_blend']
        aliased=Blob()
        assert not create(*base,C.byref(modes['source-alpha']),profile,len(profile),
            C.byref(aliased),C.byref(aliased),C.byref(aliased),err,len(err))
        assert err.value and not aliased.bytes and not aliased.length
        guard=C.create_string_buffer(b'XY',3);outputs=[Blob(),Blob(),Blob()]
        assert not create(*base,None,profile,len(profile),*map(C.byref,outputs),guard,1)
        assert guard.raw==b'\0Y\0' and all(not v.bytes and not v.length for v in outputs)
        # New Rust gate's equation policy must match the independently Mesa-
        # qualified shared C packer, even when requested state differs from h.
        native=a.out/'blend.so'
        subprocess.run(['clang','-shared','-fPIC','-O2',str(Path(__file__).with_name('blend_state.c')),'-o',str(native)],check=True)
        cbuild=C.CDLL(str(native.resolve())).tegu_blend_build
        cbuild.argtypes=[C.POINTER(Blend),C.POINTER(BlendState)];cbuild.restype=C.c_int
        for o in range(3):
            for src in range(10):
                for dst in range(10):
                    for alpha in (False,True):
                        b=Blend(1,1,*( (0,4,5,o,src,dst) if alpha else (o,src,dst,0,4,5) ),15)
                        cstate=BlendState();allowed=not cbuild(C.byref(b),C.byref(cstate))
                        rc=gate(h,70,C.byref(b),err,len(err))
                        assert rc==-1  # h is unblended.
                        assert (err.value in (b'native render color attachment unsupported',b'native render blend descriptor differs from compiled input'))==allowed,(o,src,dst,alpha,err.value)
        assert gate(None,70,C.byref(modes['source-alpha']),err,len(err))==-1
        assert gate(h,70,None,err,len(err))==-1
        # Handles retain their compiler artifacts independently of the library.
        lib.tegu_air_library_close(library);library=None
        for name,handle in zip(modes,handles):
            assert not gate(handle,70,C.byref(modes[name]),err,len(err))
    finally:
        if library:lib.tegu_air_library_close(library)
        for h in handles:lib.tegu_air_render_pipeline_close(h)
    (a.out/'result.json').write_text(json.dumps(dict(passed=True,gpu_executed=False,modes=summary,
        legacy_abi_identity=True,policy_matches_c_600=True),indent=2)+'\n')
    print('PASS actual AIR blend descriptors/metadata, native gates, old ABI,600 C policy comparisons and lifetime; GPU_EXECUTED=0')


if __name__=='__main__':main()
