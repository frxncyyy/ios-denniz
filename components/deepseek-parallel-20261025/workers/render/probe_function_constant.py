#!/usr/bin/env python3
"""Check non-default function-constant specialization on the render path."""
import ctypes as C
import hashlib
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
G = HERE.parents[3]
TEgu = G / 'metal/tegu'
BASE = G / 'build/ui-corpus-20261025'
sys.path.insert(0, str(TEgu))
import check_fixed_ui_live_variants as live


def main():
    work = HERE / 'authored'
    work.mkdir(exist_ok=True)
    subprocess.run(['llvm-as', str(TEgu / 'function_constant_probe.ll'), '-o', str(work / 'fc.air')], check=True)
    data = (work / 'fc.air').read_bytes()
    lib = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    profile = live.profile_bytes(BASE / 'baseline/tegu-air-profile.h')
    err = C.create_string_buffer(4096)
    base = lib.tegu_air_library_open(data, len(data), err, len(err))
    assert base, err.value.decode()
    def specialize(value):
        rows, keep = live.constants({'red': (53, bytes([value]))})
        out = lib.tegu_air_library_specialize(base, b'test_fragment', len(b'test_fragment'), rows, 1, err, len(err))
        assert out, err.value.decode()
        return out
    specialized0 = specialize(0)
    specialized1 = specialize(1)

    def create(flib):
        out = [live.Blob(), live.Blob(), live.Blob()]
        pb = C.create_string_buffer(profile)
        p = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
            base, b'test_vertex', len(b'test_vertex'), flib, b'test_fragment', len(b'test_fragment'),
            80, None, 0, None, 0, pb, len(profile), *[C.byref(x) for x in out], err, len(err))
        assert p, err.value.decode()
        vs = C.string_at(out[0].bytes, out[0].length)
        fs = C.string_at(out[1].bytes, out[1].length)
        meta = C.string_at(out[2].bytes, out[2].length)
        return p, vs, fs, meta

    p0, vs0, fs0, meta0 = create(specialized0)
    p1, vs1, fs1, meta1 = create(specialized1)
    print('default fragment', len(fs0), hashlib.sha256(fs0).hexdigest())
    print('red=true fragment', len(fs1), hashlib.sha256(fs1).hexdigest())
    print('different fragment SPIR-V', fs0 != fs1)
    # Check the generic planner still sees a default-only specialization.
    lib.tegu_air_render_pipeline_plan_draw_typed.restype = C.c_int32
    from probe_magma_plan import Resource, Draw, GenericPlan
    lib.tegu_air_render_pipeline_plan_generic.restype = C.c_int32
    lib.tegu_air_render_pipeline_plan_generic.argtypes = [
        C.c_void_p, C.POINTER(Resource), C.c_size_t, C.POINTER(live.VertexLayout), C.c_size_t,
        C.POINTER(Draw), C.POINTER(GenericPlan), C.c_char_p, C.c_size_t]
    for label, p in (('default', p0), ('red=true', p1)):
        plan = GenericPlan()
        d = Draw(1, 0, 3, 1, 0)
        err.value = b''
        rc = lib.tegu_air_render_pipeline_plan_generic(p, None, 0, None, 0, C.byref(d), C.byref(plan), err, len(err))
        print(label, 'generic plan', rc, err.value.decode(errors='replace'), 'bindings', plan.binding_count)

    for p in (p0, p1):
        lib.tegu_air_render_pipeline_close(p)
    lib.tegu_air_library_close(specialized0)
    lib.tegu_air_library_close(specialized1)
    lib.tegu_air_library_close(base)


if __name__ == '__main__':
    main()
