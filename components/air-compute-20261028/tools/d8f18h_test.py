#!/usr/bin/env python3
"""Host test (air-compute-opus): SpringBoard MPS kernel D8F18H (MPSImage.framework default.metallib, MPSGaussianBlur)
through the AIR-only compute path, plain create and create_ex with an EMPTY stage-input descriptor (what MPS passes),
both kUseSoftwareLinearFilter values. usage: d8f18h_test.py [constant 0|1] [libnord2_mali_runtime.so]"""
import json,sys,ctypes as C,hashlib
from pathlib import Path
W=Path('$NORD2_WORK')
sys.path.insert(0,str(W/'gpu/build/shader-coverage-20261027/tools'));sys.path.insert(0,str(W/'gpu/build/air-compute-20261028/tools'))
import replay as R, air_compute_corpus as A
G=W/'gpu'
L=R.Libs(G/'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so',Path(sys.argv[2]) if len(sys.argv)>2 else G/'build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so')
m=L.m;m.tegu_mali_compile_compute_air.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(R.MBlob),C.c_void_p,C.c_size_t]
profile=R.live.profile_bytes(G/'build/tegu-frontend/tegu-air-profile.h')
err=C.create_string_buffer(4096)
lib=R.whole_lib(L,R.CORPUS/'libraries/8d267b143b7b37a35f3d2e74736e3958fb44786b3ecf343e58fe7a23bb7efbc7.metallib',err)
val=int(sys.argv[1]) if len(sys.argv)>1 else 0
rows=[{'name':'kUseSoftwareLinearFilter','type_raw':53,'index':0,'value_hex':'%02x'%val,'kind':1}]
h=R.specialize(L,lib,'D8F18H',rows,err)
n=b'D8F18H'
for mode in ('plain','empty_stage_in'):
  spv,meta=R.live.Blob(),R.live.Blob();err.value=b''
  if mode=='plain': p=L.b.tegu_air_pipeline_create(h,n,len(n),profile,len(profile),C.byref(spv),C.byref(meta),err,len(err))
  else:
    opt=R.ComputeOptions(1,0,None,0,None,0,None,0,0,0,1,0)
    p=L.b.tegu_air_pipeline_create_ex(h,n,len(n),C.byref(opt),profile,len(profile),C.byref(spv),C.byref(meta),err,len(err))
  assert p,(mode,err.value)
  md=C.string_at(meta.bytes,meta.length);print(mode,'meta',hashlib.sha256(md).hexdigest()[:16],len(md))
  raw=(R.CORPUS/'libraries/8d267b143b7b37a35f3d2e74736e3958fb44786b3ecf343e58fe7a23bb7efbc7.metallib').read_bytes()
  kc,keep=A.constants(rows);src=C.create_string_buffer(raw,len(raw))
  for flags in (0,1):
    pk=R.MBlob();err.value=b''
    req=A.AirCompute(1,flags,C.cast(src,C.c_void_p),len(raw),n,kc,1,0,md,len(md))
    rc=m.tegu_mali_compile_compute_air(C.byref(req),None,0,C.byref(pk),err,len(err))
    print(' ',mode,'v%d'%(flags+1),'rc',rc,err.value[:150],pk.size)
    if not rc: print('  abi',A.abi(C.string_at(pk.data,pk.size)));break
  assert not rc,(mode,err.value)
print('PASS D8F18H AIR compute')
