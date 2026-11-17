#!/usr/bin/env python3
"""Compare the reflected native generic plan with the translator draw plan."""
import ctypes as C
import json
import sys
from pathlib import Path

ROOT = Path('$NORD2_WORK/gpu')
BASE = ROOT / 'build/ui-corpus-20261025'
sys.path.insert(0, str(ROOT / 'metal/tegu'))
import check_fixed_ui_live_variants as live
from probe_magma_plan import Resource, Draw, Binding, GenericPlan, texture, buffer
from probe_icon_plan import main as _unused


class MBinding(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in
                ('binding', 'kind', 'stage_mask', 'declared_bytes', 'input_attachment_index')] + [
                ('reserved', C.c_uint32 * 3)]


class MPlan(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'count', 'reads_framebuffer', 'reserved')] + [
        ('bindings', MBinding * 16)]


def main():
    lib = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    profile = live.profile_bytes(BASE / 'baseline/tegu-air-profile.h')
    job = next(x for x in json.load(open(BASE / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-01134')
    err = C.create_string_buffer(4096)
    vp = BASE / job['vertex']['air_saved_as']
    fp = BASE / job['fragment']['air_saved_as']
    vh = lib.tegu_air_library_open(vp.read_bytes(), vp.stat().st_size, err, len(err))
    fh = lib.tegu_air_library_open(fp.read_bytes(), fp.stat().st_size, err, len(err))
    attrs = (live.VertexAttribute * len(job['attributes']))(*[live.VertexAttribute(**a) for a in job['attributes']])
    layouts = (live.VertexLayout * len(job['layouts']))(*[live.VertexLayout(**a) for a in job['layouts']])
    out = [live.Blob(), live.Blob(), live.Blob()]
    pb = C.create_string_buffer(profile)
    vn = job['vertex']['name'].encode()
    fn = job['fragment']['name'].encode()
    pipeline = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
        vh, vn, len(vn), fh, fn, len(fn), 80, attrs, len(attrs), layouts, len(layouts),
        pb, len(profile), *[C.byref(x) for x in out], err, len(err))
    assert pipeline, err.value.decode()
    vs = C.string_at(out[0].bytes, out[0].length)
    fs = C.string_at(out[1].bytes, out[1].length)
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
        print(' ', b.binding, 'kind', b.kind, 'stages', hex(b.stage_mask), 'bytes', b.declared_bytes,
              'attachment', b.input_attachment_index)
    compiler.tegu_mali_graphics_free(C.byref(gv))
    compiler.tegu_mali_graphics_free(C.byref(gf))
    lib.tegu_air_render_pipeline_close(pipeline)
    lib.tegu_air_library_close(vh)
    lib.tegu_air_library_close(fh)


if __name__ == '__main__':
    main()
