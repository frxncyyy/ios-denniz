#!/usr/bin/env python3
"""Test the actual C ABI against authored AIR and optional donor metallibs."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess

class Blob(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('length', C.c_size_t)]

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--bridge', type=Path, required=True)
p.add_argument('--translator', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--mali-compiler', type=Path, required=True)
p.add_argument('--donor', type=Path, action='append', default=[])
a=p.parse_args()
a.work.mkdir(parents=True, exist_ok=False)
lib=C.CDLL(str(a.bridge.resolve()))
lib.tegu_air_library_open.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t]
lib.tegu_air_library_open.restype=C.c_void_p
lib.tegu_air_library_close.argtypes=[C.c_void_p]
lib.tegu_air_library_close.restype=None
lib.tegu_air_function_count.argtypes=[C.c_void_p]
lib.tegu_air_function_count.restype=C.c_size_t
lib.tegu_air_function_info.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(C.c_uint32)]
lib.tegu_air_function_info.restype=C.c_size_t
lib.tegu_air_compile_compute.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Blob),C.POINTER(Blob),C.c_void_p,C.c_size_t]
lib.tegu_air_compile_compute.restype=C.c_int32
lib.tegu_air_blob_free.argtypes=[C.POINTER(Blob)]
lib.tegu_air_blob_free.restype=None
error=C.create_string_buffer(2048)
assert not lib.tegu_air_library_open(None,0,error,len(error)) and error.value
assert not lib.tegu_air_library_open(b'garbage',7,error,len(error)) and error.value
assert lib.tegu_air_function_count(None)==0
lib.tegu_air_library_close(None)
fixture=a.translator/'crates/metal2vulkan-validation/fixtures/public/examples/runtime_examples.air.ll'
air=a.work/'runtime_examples.air'
subprocess.run(['llvm-as',str(fixture),'-o',str(air)],check=True)
profile=(a.translator/'crates/metal2vulkan-validation/profiles/vk12-bare.json').read_bytes()
reports=[]
for source in [air]+a.donor:
    data=source.read_bytes()
    handle=lib.tegu_air_library_open(data,len(data),error,len(error))
    assert handle, error.value
    try:
        functions=[]
        for index in range(lib.tegu_air_function_count(handle)):
            stage=C.c_uint32()
            needed=lib.tegu_air_function_info(handle,index,None,0,C.byref(stage))
            assert 1<needed<=4096
            name=C.create_string_buffer(needed)
            assert lib.tegu_air_function_info(handle,index,name,len(name),C.byref(stage))==needed
            functions.append(dict(name=name.value.decode(),type=stage.value))
            short=C.create_string_buffer(1)
            assert lib.tegu_air_function_info(handle,index,short,1,C.byref(stage))==needed and short.raw==b'\0'
        assert not lib.tegu_air_function_info(handle,len(functions),None,0,C.byref(stage))
        reports.append(dict(path=str(source),sha256=hashlib.sha256(data).hexdigest(),functions=functions))
        if source!=air:
            continue
        assert dict(name='scale_and_offset',type=3) in functions
        for name,prof,success in [(b'missing_function',profile,False),(b'scale_and_offset',b'{}',False),(b'scale_and_offset',profile,True)]:
            spirv,metadata=Blob(),Blob()
            rc=lib.tegu_air_compile_compute(handle,name,len(name),prof,len(prof),C.byref(spirv),C.byref(metadata),error,len(error))
            if not success:
                assert rc!=0 and error.value and not spirv.bytes and not metadata.bytes
                continue
            assert rc==0 and not error.value, error.value
            try:
                spv=C.string_at(spirv.bytes,spirv.length)
                meta=json.loads(C.string_at(metadata.bytes,metadata.length))
                assert meta['entry_point']=='main' and meta['constant_blob'] is None
                assert meta['required_subgroup_size'] is None
                (a.work/'scale.spv').write_bytes(spv)
                (a.work/'scale.json').write_text(json.dumps(meta,indent=2)+'\n')
            finally:
                lib.tegu_air_blob_free(C.byref(spirv));lib.tegu_air_blob_free(C.byref(metadata))
            lib.tegu_air_blob_free(C.byref(spirv))
            assert not spirv.bytes and not spirv.length and not metadata.bytes
    finally:
        lib.tegu_air_library_close(handle)
for target_env in meta['validation']['target_envs']:
    subprocess.run(['spirv-val','--target-env',target_env,*meta['validation']['relaxations'],str(a.work/'scale.spv')],check=True)
subprocess.run([str(a.mali_compiler.resolve()),str(a.work/'scale.spv'),str(a.work/'scale.bin'),str(a.work/'scale.tgp')],check=True)
result=dict(libraries=reports,mali_sha256=hashlib.sha256((a.work/'scale.bin').read_bytes()).hexdigest(),
            library_parse=True,c_abi_compile=True,invalid_inputs_refused=True,owned_outputs_freed=True,
            native_ios_tested=False,gpu_execution_tested=False)
(a.work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='libraries'},indent=2))
