#!/usr/bin/env python3
"""Probe the installed Icon VS/FS pair: VS texture7 + runtime sampler7."""
import ctypes as C
import json
import sys
from pathlib import Path

ROOT = Path('$NORD2_WORK/gpu')
BASE = ROOT / 'build/ui-corpus-20261025'
sys.path.insert(0, str(ROOT / 'metal/tegu'))
import check_fixed_ui_live_variants as live
from probe_magma_plan import Resource, Draw, Binding, GenericPlan, texture, buffer


def main():
    lib = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    profile = live.profile_bytes(BASE / 'baseline/tegu-air-profile.h')
    lib.tegu_air_render_pipeline_plan_generic.restype = C.c_int32
    lib.tegu_air_render_pipeline_plan_generic.argtypes = [
        C.c_void_p, C.POINTER(Resource), C.c_size_t, C.POINTER(live.VertexLayout), C.c_size_t,
        C.POINTER(Draw), C.POINTER(GenericPlan), C.c_char_p, C.c_size_t]
    job = next(x for x in json.load(open(BASE / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-01134')
    err = C.create_string_buffer(4096)
    vp = BASE / job['vertex']['air_saved_as']
    fp = BASE / job['fragment']['air_saved_as']
    vh = lib.tegu_air_library_open(vp.read_bytes(), vp.stat().st_size, err, len(err))
    fh = lib.tegu_air_library_open(fp.read_bytes(), fp.stat().st_size, err, len(err))
    assert vh and fh, err.value.decode()
    attrs = (live.VertexAttribute * len(job.get('attributes', [])))(*[
        live.VertexAttribute(**a) for a in job.get('attributes', [])])
    layouts = (live.VertexLayout * len(job.get('layouts', [])))(*[
        live.VertexLayout(**a) for a in job.get('layouts', [])])
    out = [live.Blob(), live.Blob(), live.Blob()]
    pb = C.create_string_buffer(profile)
    vn = job['vertex']['name'].encode()
    fn = job['fragment']['name'].encode()
    pipeline = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
        vh, vn, len(vn), fh, fn, len(fn), 80, attrs, len(attrs), layouts, len(layouts),
        pb, len(profile), *[C.byref(x) for x in out], err, len(err))
    assert pipeline, err.value.decode()
    meta = json.loads(C.string_at(out[2].bytes, out[2].length))
    print('deps', json.dumps(meta['dependencies'])[:3000])
    print('attributes', job.get('attributes'), 'layouts', job.get('layouts'))
    # Discover declared bindings from metadata dependencies and resource slots.
    slots = set()
    for dep in meta['dependencies']:
        for v in dep['fact'].values():
            if isinstance(v, dict):
                for slot in (v.get('slot'), v.get('slots')):
                    if isinstance(slot, dict):
                        slots.add((slot['class'], slot['stage'], slot['index']))
    print('slots', sorted(slots))
    # Build resources: VS buffer0 (the synthetic fetch), texture7, sampler7;
    # fragment textures 0..3 if declared and one sampler at 0.
    rows = [buffer(1, 0, 4096)]
    rows.append(texture(1, 7, 0x107, fmt=80, w=64, h=64))
    rows.append(texture(2, 0, 0x200, fmt=80, w=64, h=64))
    rows.append(texture(2, 1, 0x201, fmt=80, w=64, h=64))
    rows.append(texture(2, 2, 0x202, fmt=80, w=64, h=64))
    for idx in (7,):
        s = Resource(); s.kind, s.stage, s.index = 3, 1, idx
        s.normalized_coordinates = s.max_anisotropy = 1
        rows.append(s)
    for idx in (0,):
        s = Resource(); s.kind, s.stage, s.index = 3, 2, idx
        s.normalized_coordinates = s.max_anisotropy = 1
        rows.append(s)
    arr = (Resource * len(rows))(*rows)
    draw = Draw(1, 0, 3, 1, 0)
    plan = GenericPlan()
    rc = lib.tegu_air_render_pipeline_plan_generic(pipeline, arr, len(arr), None, 0,
                                                   C.byref(draw), C.byref(plan), err, len(err))
    print('plan rc', rc, err.value.decode(errors='replace'))
    if rc == 0:
        print('bindings', plan.binding_count, 'pushes', plan.push_count)
        for i in range(plan.binding_count):
            b = plan.bindings[i]
            print(' ', {n: getattr(b, n) for n, _ in Binding._fields_})
    lib.tegu_air_render_pipeline_close(pipeline)
    lib.tegu_air_library_close(vh)
    lib.tegu_air_library_close(fh)


if __name__ == '__main__':
    main()
