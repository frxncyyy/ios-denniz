#!/usr/bin/env python3
"""Check storage texture stand-in creation through the public C ABI."""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess
import tempfile


class Blob(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('length', C.c_size_t)]


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bridge', type=Path, required=True)
p.add_argument('--translator', type=Path, required=True)
a = p.parse_args()

lib = C.CDLL(str(a.bridge.resolve()))
lib.tegu_air_library_open.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
lib.tegu_air_library_open.restype = C.c_void_p
lib.tegu_air_library_close.argtypes = [C.c_void_p]
lib.tegu_air_pipeline_create.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
    C.c_void_p, C.c_size_t, C.POINTER(Blob), C.POINTER(Blob), C.c_void_p, C.c_size_t]
lib.tegu_air_pipeline_create.restype = C.c_void_p
lib.tegu_air_pipeline_dispatchable.argtypes = [C.c_void_p]
lib.tegu_air_pipeline_dispatchable.restype = C.c_int32
lib.tegu_air_pipeline_close.argtypes = [C.c_void_p]
lib.tegu_air_pipeline_plan.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
    C.c_void_p, C.c_void_p, C.POINTER(Blob), C.c_void_p, C.c_size_t]
lib.tegu_air_pipeline_native_dispatch.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
    C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_size_t]
lib.tegu_air_blob_free.argtypes = [C.POINTER(Blob)]

error = C.create_string_buffer(2048)
fixture = a.translator / 'crates/metal2vulkan-validation/fixtures/public/engine/a2_storage_read_formats.air.ll'
profile_path = a.translator / 'crates/metal2vulkan-validation/profiles/vk12-bare.json'
with tempfile.TemporaryDirectory(prefix='tegu-air-standin-') as tmp:
    air = Path(tmp) / 'storage_read_formats.air'
    subprocess.run(['llvm-as', str(fixture), '-o', str(air)], check=True)
    data = air.read_bytes()
    profile = profile_path.read_bytes()
    handle = lib.tegu_air_library_open(data, len(data), error, len(error))
    assert handle, error.value
    spirv, metadata = Blob(), Blob()
    pipeline = None
    try:
        name = b'a2_storage_read_formats'
        pipeline = lib.tegu_air_pipeline_create(handle, name, len(name), profile, len(profile),
            C.byref(spirv), C.byref(metadata), error, len(error))
        assert pipeline, error.value
        result = json.loads(C.string_at(metadata.bytes, metadata.length))
        creation = result['creation_compile']
        assert creation['kind'] == 'storage_texture_stand_in', creation
        assert creation['dispatchable'] is False, creation
        assert len(creation['variant']['storage_images']) == 3, creation
        assert lib.tegu_air_pipeline_dispatchable(pipeline) == 0

        groups = (C.c_uint32 * 3)(1, 1, 1)
        local = (C.c_uint32 * 3)(1, 1, 1)
        plan = Blob()
        assert lib.tegu_air_pipeline_plan(pipeline, None, 0, groups, local,
            C.byref(plan), error, len(error)) == -1
        assert b'creation-only' in error.value and not plan.bytes and not plan.length
        native = (C.c_uint8 * 1368)()
        assert lib.tegu_air_pipeline_native_dispatch(pipeline, None, 0, groups, local,
            C.byref(native), error, len(error)) == -1
        assert b'creation-only' in error.value
    finally:
        if pipeline:
            lib.tegu_air_pipeline_close(pipeline)
        for blob in (spirv, metadata):
            lib.tegu_air_blob_free(C.byref(blob))
        lib.tegu_air_library_close(handle)

print('PASS storage texture stand-in pipeline creation is identified and dispatch planning refuses it')
