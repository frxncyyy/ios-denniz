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
from check_compute_v2_compiler import invoke

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--producer',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--producer-rust-source',type=Path,help='Exact source used to build the selected producer cdylib');a=p.parse_args()
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
    packet=out/(label+f'.region{n}.tgp2');r=invoke(a.compiler.resolve(),spv,out,constants,packet=packet)
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
 report={'compiler_sha256':hashlib.sha256(a.compiler.read_bytes()).hexdigest(),'producer_sha256':hashlib.sha256(m.library_path().read_bytes()).hexdigest(),'producer_rust_source':str(rust_source.resolve()),'producer_rust_sha256':hashlib.sha256(rust_source.read_bytes()).hexdigest(),'gpu_executed':False,'cases':cases}
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(f'{sum(x["passed"] for x in cases)}/{len(cases)} real producer/compiler cases passed')
 if not all(x['passed'] for x in cases):raise SystemExit(1)
if __name__=='__main__':main()
