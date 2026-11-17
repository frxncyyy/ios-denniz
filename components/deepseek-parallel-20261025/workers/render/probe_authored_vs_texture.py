#!/usr/bin/env python3
"""Assemble the authored VS/FS texture probe and check plan + native compile."""
import ctypes as C
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = Path('$NORD2_WORK/gpu')
BASE = ROOT / 'build/ui-corpus-20261025'
sys.path.insert(0, str(ROOT / 'metal/tegu'))
sys.path.insert(0, str(HERE))
import check_fixed_ui_live_variants as live
from probe_magma_plan import Resource, Draw, Binding, GenericPlan, texture, buffer


class MBinding(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in
                ('binding', 'kind', 'stage_mask', 'declared_bytes', 'input_attachment_index')] + [
                ('reserved', C.c_uint32 * 3)]


class MPlan(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'count', 'reads_framebuffer', 'reserved')] + [
        ('bindings', MBinding * 16)]


def sampler(stage, index, minf=0, magf=0):
    r = Resource()
    r.kind, r.stage, r.index = 3, stage, index
    r.normalized_coordinates = 1
    r.max_anisotropy = 1
    r.min_filter, r.mag_filter = minf, magf
    return r


def main():
    work = HERE / 'authored'
    work.mkdir(exist_ok=True)
    subprocess.run(['llvm-as', str(HERE / 'vertex_texture_probe.ll'), '-o', str(work / 'probe.air')], check=True)
    data = (work / 'probe.air').read_bytes()
    lib = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    profile = live.profile_bytes(BASE / 'baseline/tegu-air-profile.h')
    err = C.create_string_buffer(4096)
    h = lib.tegu_air_library_open(data, len(data), err, len(err))
    assert h, err.value.decode()
    out = [live.Blob(), live.Blob(), live.Blob()]
    pb = C.create_string_buffer(profile)
    pipeline = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
        h, b'vt_vertex', len(b'vt_vertex'), h, b'vt_fragment', len(b'vt_fragment'),
        80, None, 0, None, 0, pb, len(profile), *[C.byref(x) for x in out], err, len(err))
    assert pipeline, err.value.decode()
    vs = C.string_at(out[0].bytes, out[0].length)
    fs = C.string_at(out[1].bytes, out[1].length)
    (work / 'vertex.spv').write_bytes(vs)
    (work / 'fragment.spv').write_bytes(fs)
    (work / 'metadata.json').write_bytes(C.string_at(out[2].bytes, out[2].length))
    print('translated', len(vs), len(fs))
    rows = [texture(1, 0, 0x101), sampler(1, 0), texture(2, 0, 0x201), sampler(2, 0)]
    arr = (Resource * len(rows))(*rows)
    draw = Draw(1, 0, 3, 1, 0)
    plan = GenericPlan()
    lib.tegu_air_render_pipeline_plan_generic.restype = C.c_int32
    lib.tegu_air_render_pipeline_plan_generic.argtypes = [
        C.c_void_p, C.POINTER(Resource), C.c_size_t, C.POINTER(live.VertexLayout), C.c_size_t,
        C.POINTER(Draw), C.POINTER(GenericPlan), C.c_char_p, C.c_size_t]
    rc = lib.tegu_air_render_pipeline_plan_generic(pipeline, arr, len(arr), None, 0,
                                                   C.byref(draw), C.byref(plan), err, len(err))
    print('plan rc', rc, err.value.decode(errors='replace'))
    if rc == 0:
        for i in range(plan.binding_count):
            b = plan.bindings[i]
            print(' ', b.binding, 'kind', b.kind, 'stage', b.stage, 'index', b.index,
                  'sampler_stage', b.sampler_stage, 'sampler_index', b.sampler_index,
                  'format', b.format, b.width, b.height)
    compiler = live.configure_compiler(BASE / 'baseline/libnord2_mali_runtime.so')
    compiler.tegu_mali_compile_generic_pair.argtypes = [
        C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(live.Graphics),
        C.POINTER(live.Graphics), C.POINTER(live.Pair), C.POINTER(MPlan), C.c_void_p, C.c_size_t]
    compiler.tegu_mali_compile_generic_pair.restype = C.c_int
    gv, gf, pair, mp = live.Graphics(), live.Graphics(), live.Pair(), MPlan()
    err.value = b''
    rc = compiler.tegu_mali_compile_generic_pair(vs, len(vs), fs, len(fs), C.byref(gv), C.byref(gf),
                                                C.byref(pair), C.byref(mp), err, len(err))
    print('compile rc', rc, err.value.decode(errors='replace'), 'count', mp.count)
    for i in range(mp.count):
        b = mp.bindings[i]
        print(' ', b.binding, 'kind', b.kind, 'stages', hex(b.stage_mask), 'bytes', b.declared_bytes)
    compiler.tegu_mali_graphics_free(C.byref(gv))
    compiler.tegu_mali_graphics_free(C.byref(gf))
    lib.tegu_air_render_pipeline_close(pipeline)
    lib.tegu_air_library_close(h)


if __name__ == '__main__':
    main()
