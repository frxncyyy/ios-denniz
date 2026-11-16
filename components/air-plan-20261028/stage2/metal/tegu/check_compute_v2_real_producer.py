#!/usr/bin/env python3
"""Compile real AIR planner output with its exact region specialization.

Requires locally extracted TestFastMath/accumulateKernel fixtures and the real
Rust producer probe module; never fetches Apple binaries or executes a GPU job.
"""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import struct,subprocess
from check_compute_v2_compiler import invoke

# --air (air-compute-opus): the same planner output compiled from the kernel's
# AIR through tegu_mali_compile_compute_air (production AIR-only compute path,
# runtime_pipeline.inc tegu_compute_compile) with the m2v metadata of the
# translation whose plan is instantiated: the creation compile's, or the
# dispatch variant's (tegu_air_pipeline_variant_metadata).
class _AirC(C.Structure):
 _fields_=[('version',C.c_uint32),('flags',C.c_uint32),('bitcode',C.c_void_p),('size',C.c_size_t),('entry',C.c_char_p),
  ('constants',C.c_void_p),('constant_count',C.c_uint32),('reserved',C.c_uint32),('metadata',C.c_char_p),('metadata_size',C.c_size_t)]
class _Blob(C.Structure):_fields_=[('data',C.c_void_p),('size',C.c_size_t)]
class _Spec(C.Structure):_fields_=[('id',C.c_uint32),('size',C.c_uint32),('data',C.c_uint8*8)]
def air_worker(compiler,air,entry,meta,spec,packet_path):
 lib=C.CDLL(str(compiler));fn=lib.tegu_mali_compile_compute_air
 fn.argtypes=[C.POINTER(_AirC),C.c_void_p,C.c_size_t,C.POINTER(_Blob),C.c_void_p,C.c_size_t];lib.tegu_mali_free.argtypes=[C.POINTER(_Blob)]
 code=Path(air).read_bytes();md=Path(meta).read_bytes();constants=json.loads(Path(spec).read_text())
 table=(_Spec*max(1,len(constants)))()
 for i,(ident,hexdata) in enumerate(constants):
  v=bytes.fromhex(hexdata);table[i].id=ident;table[i].size=len(v)
  for j,x in enumerate(v):table[i].data[j]=x
 src=C.create_string_buffer(code,len(code));req=_AirC(1,1,C.cast(src,C.c_void_p),len(code),entry.encode(),None,0,0,md,len(md))
 out=_Blob();err=C.create_string_buffer(1024);rc=fn(C.byref(req),table,len(constants),C.byref(out),err,len(err))
 packet=C.string_at(out.data,out.size) if out.size else b'';lib.tegu_mali_free(C.byref(out))
 r={'rc':rc,'error':err.value.decode(errors='replace'),'bytes':len(packet),'sha256':hashlib.sha256(packet).hexdigest(),'entry':'tegu_mali_compile_compute_air'}
 if packet:
  h=list(struct.unpack('<32I',packet[:128]));r['header']=h
  r['manifest']=[list(struct.unpack('<16I',packet[128+i*64:192+i*64])) for i in range(h[13])]
  r['tls_bytes'],r['wls_bytes']=h[17],h[18];r['code_sha256']=hashlib.sha256(packet[h[15]:]).hexdigest()
  Path(packet_path).write_bytes(packet)
 print(json.dumps(r))
def air_invoke(compiler,air,entry,meta,out,label,spec,packet):
 sf=out/(label+'.air.spec.json');sf.write_text(json.dumps(spec))
 run=subprocess.run([sys.executable,__file__,'--air-worker',str(compiler),str(air),entry,str(meta),str(sf),str(packet)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
 (out/(label+'.air.log')).write_text(run.stdout)
 if run.returncode:return {'process_exit':run.returncode,'error':run.stdout[-2000:]}
 return json.loads(run.stdout.strip().splitlines()[-1])
def air_metadata(m,lib,library,pipeline,function,variant,fx):
 Blob=type(variant)
 if variant.length:
  lib.tegu_air_pipeline_variant_metadata.restype=C.c_int32
  lib.tegu_air_pipeline_variant_metadata.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(Blob)]
  meta=Blob();rc=lib.tegu_air_pipeline_variant_metadata(pipeline,variant.bytes,variant.length,C.byref(meta))
  if rc:raise RuntimeError(f'variant metadata rc={rc}')
  return C.string_at(meta.bytes,meta.length)
 profile=(fx/'profile.json').read_bytes();spv,meta=Blob(),Blob();err=C.create_string_buffer(1024)
 p2=lib.tegu_air_pipeline_create(library,function.encode(),len(function),profile,len(profile),C.byref(spv),C.byref(meta),err,1024)
 if not p2:raise RuntimeError('pipeline_create for metadata: '+err.value.decode(errors='replace'))
 data=C.string_at(meta.bytes,meta.length);lib.tegu_air_pipeline_close(p2);return data

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--producer',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--producer-rust-source',type=Path,help='Exact source used to build the selected producer cdylib');p.add_argument('--air',action='store_true',help='compile through tegu_mali_compile_compute_air (AIR-only compute path)');a=p.parse_args()
 sys.path.insert(0,str(a.producer.resolve().parent));spec=importlib.util.spec_from_file_location('producer',a.producer);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 rust_source=a.producer_rust_source or m.GPU/'metal/tegu/air_bridge.rs';m.built=m.build_ctypes(m.abi_oracle.parse_rust(rust_source));R=m.built['ComputeResource'];lib=C.CDLL(str(m.library_path()));out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
 cases=[]
 for label,air,fn,mode,extents,local,resources in [
  ('fast_math','test_fast_math.air','TestFastMath',0,(4,4,1),(8,8,1),[m.buffer(R,0,m.READ|m.WRITE),m.texture(R,0,m.READ|m.SAMPLE),m.sampler(R,0)]),
  ('fast_math_linear','test_fast_math.air','TestFastMath',0,(4,4,1),(8,8,1),[m.buffer(R,0,m.READ|m.WRITE),m.texture(R,0,m.READ|m.SAMPLE),m.sampler(R,0,minf=1,magf=1,mipf=2,address=2,aniso=4,lod_max=3.0)]),
  ('fast_math_threads','test_fast_math.air','TestFastMath',1,(17,9,1),(8,4,1),[m.buffer(R,0,m.READ|m.WRITE),m.texture(R,0,m.READ|m.SAMPLE),m.sampler(R,0)]),
  ('accumulate','accumulate_kernel.air','accumulateKernel',0,(4,4,1),(8,8,1),[m.buffer(R,0,m.READ),m.texture(R,0,m.READ|m.SAMPLE),m.texture(R,1,m.STORAGE|m.WRITE,usage=3)]),
 ]:
  lib,library,pipeline,base=m.open_pipeline(lib,m.fixture_dir(),air,fn)
  try:
   rc,planned,variant,error=m.dispatch(lib,R,pipeline,resources,mode,extents,local)
   if rc:cases.append({'case':label,'passed':False,'planner_rc':rc,'error':error});continue
   v=C.cast(planned,C.POINTER(m.built['NativeDispatchV2'])).contents
   blob=variant if variant.length else base;spv=out/(label+'.spv');spv.write_bytes(C.string_at(blob.bytes,blob.length));(out/(label+'.producer.bin')).write_bytes(planned.raw)
   desc=[m.descriptor(planned,i) for i in range(v.descriptor_count)]
   for n in range(v.region_count):
    region=v.regions[n];constants=[(region.constants[k].id,bytes(region.constants[k].data)[:region.constants[k].size].hex()) for k in range(region.constant_count)]
    packet=out/(label+f'.region{n}.tgp2')
    if a.air:
     mp=out/(label+'.meta.json');mp.write_bytes(air_metadata(m,lib,library,pipeline,fn,variant,m.fixture_dir()))
     r=air_invoke(a.compiler.resolve(),m.fixture_dir()/air,fn,mp,out,label+f'.region{n}',constants,packet)
    else:r=invoke(a.compiler.resolve(),spv,out,constants,packet=packet)
    errors=[]
    if r.get('rc')!=0:errors.append('native compiler refused')
    else:
     manifest=r['manifest'];native={(x[1],x[2]):x for x in manifest if x[1]!=0xffffffff}
     expected=set()
     for d in desc:
      kinds=(3,5) if d['kind']==6 else (d['kind'],)
      for kind in kinds:
       expected.add((d['binding'],kind));record=native.get((d['binding'],kind))
       if record is None:errors.append(f'missing binding {d["binding"]} kind {kind}');continue
       if record[5:7]!=[0 if kind in (1,2) else 2 if kind==5 else 1,d['binding']]:errors.append('native table/index mismatch')
       access=0 if kind==5 else d['flags']&0x1f00
       if record[4]!=access:errors.append(f'access mismatch binding{d["binding"]}: manifest {record[4]:x} planner {access:x}')
       if bool(record[7]&2)!=(d['kind']==6):errors.append('declared combined mismatch')
     if set(native)!=expected:errors.append('unplanned live native descriptors')
     region_local=[int.from_bytes(bytes(region.constants[k].data)[:region.constants[k].size],'little') for ident in range(3) for k in range(region.constant_count) if region.constants[k].id==ident]
     if r['header'][9:12]!=region_local:errors.append('per-region workgroup specialization mismatch')
    row={'case':label,'region':n,'passed':not errors,'errors':errors,'variant':bool(variant.length),'constants':constants,'descriptors':desc,'result':r,'packet':str(packet)};cases.append(row);print(json.dumps(row),flush=True)
  finally:
   lib.tegu_air_pipeline_close(pipeline);lib.tegu_air_library_close(library)
 report={'compiler_sha256':hashlib.sha256(a.compiler.read_bytes()).hexdigest(),'producer_sha256':hashlib.sha256(m.library_path().read_bytes()).hexdigest(),'producer_rust_source':str(rust_source.resolve()),'producer_rust_sha256':hashlib.sha256(rust_source.read_bytes()).hexdigest(),'gpu_executed':False,'compile_path':'air' if a.air else 'm2v_spirv','cases':cases}
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(f'{sum(x["passed"] for x in cases)}/{len(cases)} real producer/compiler cases passed')
 if not all(x['passed'] for x in cases):raise SystemExit(1)
if __name__=='__main__':
 if len(sys.argv)==8 and sys.argv[1]=='--air-worker':air_worker(*sys.argv[2:])
 else:main()
