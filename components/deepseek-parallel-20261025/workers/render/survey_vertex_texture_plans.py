#!/usr/bin/env python3
"""Plan every fetch-OK real installed VS-texture pair with explicit facts.

No production edit, no GPU. The baseline translator is the same frozen library
used by the corpus baseline. Missing function constants / fetch format refusals
are counted separately rather than cross-producted.
"""
import ctypes as C
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
G = HERE.parents[3]
BASE = G / 'build/ui-corpus-20261025'
sys.path.insert(0, str(G / 'metal/tegu'))
sys.path.insert(0, str(HERE))
import check_fixed_ui_live_variants as live
from probe_magma_plan import Resource, Draw, GenericPlan, texture, buffer


class A(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('location', 'buffer_index', 'format', 'offset', 'stride', 'input_rate', 'divisor')]


class F(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'mask', 'count', 'reserved')] + [('attributes', A * 31)]


def sampler(stage, index):
    r = Resource()
    r.kind, r.stage, r.index = 3, stage, index
    r.normalized_coordinates = 1
    r.max_anisotropy = 1
    return r


def main():
    lib = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    profile = live.profile_bytes(BASE / 'baseline/tegu-air-profile.h')
    lib.tegu_air_render_pipeline_vertex_fetch.argtypes = [C.c_void_p, C.POINTER(F), C.c_char_p, C.c_size_t]
    lib.tegu_air_render_pipeline_vertex_fetch.restype = C.c_int32
    lib.tegu_air_render_pipeline_plan_generic.restype = C.c_int32
    lib.tegu_air_render_pipeline_plan_generic.argtypes = [
        C.c_void_p, C.POINTER(Resource), C.c_size_t, C.POINTER(live.VertexLayout), C.c_size_t,
        C.POINTER(Draw), C.POINTER(GenericPlan), C.c_char_p, C.c_size_t]
    jobs = json.load(open(BASE / 'graphics-jobs.json'))['jobs']
    vertex_tex = set()
    for line in open(BASE / 'resources.jsonl'):
        d = json.loads(line)
        if '"air.texture"' in '\n'.join(d.get('metadata') or []):
            vertex_tex.add(d['air_sha256'])
    results = []
    for job in jobs:
        if job['vertex']['air_sha256'] not in vertex_tex:
            continue
        err = C.create_string_buffer(4096)
        vp = BASE / job['vertex']['air_saved_as']
        fp = BASE / job['fragment']['air_saved_as']
        vh = fh = pipeline = 0
        try:
            vh = lib.tegu_air_library_open(vp.read_bytes(), vp.stat().st_size, err, len(err))
            fh = lib.tegu_air_library_open(fp.read_bytes(), fp.stat().st_size, err, len(err))
            if not vh or not fh:
                results.append((job['id'], 'library', err.value.decode(errors='replace')))
                continue
            attrs = (live.VertexAttribute * len(job.get('attributes', [])))(
                *[live.VertexAttribute(**a) for a in job.get('attributes', [])])
            layouts = (live.VertexLayout * len(job.get('layouts', [])))(
                *[live.VertexLayout(**a) for a in job.get('layouts', [])])
            out = [live.Blob(), live.Blob(), live.Blob()]
            pb = C.create_string_buffer(profile)
            vn = job['vertex']['name'].encode()
            fn = job['fragment']['name'].encode()
            pipeline = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
                vh, vn, len(vn), fh, fn, len(fn), 80, attrs, len(attrs), layouts, len(layouts),
                pb, len(profile), *[C.byref(x) for x in out], err, len(err))
            if not pipeline:
                results.append((job['id'], 'translate', err.value.decode(errors='replace')))
                continue
            fetch = F()
            if lib.tegu_air_render_pipeline_vertex_fetch(pipeline, C.byref(fetch), err, len(err)):
                results.append((job['id'], 'fetch', err.value.decode(errors='replace')))
                continue
            meta = json.loads(C.string_at(out[2].bytes, out[2].length))
            slots = set()
            for dep in meta.get('dependencies', []):
                for v in dep.get('fact', {}).values():
                    if not isinstance(v, dict):
                        continue
                    slot = v.get('slot')
                    if isinstance(slot, dict):
                        slots.add((slot.get('class'), slot.get('stage'), slot.get('index')))
            rows = []
            for stage in (1, 2):
                for i in range(4):
                    rows.append(buffer(stage, i, 4096))
            for cls, st, idx in sorted(slots, key=lambda s: (str(s[0]), s[1] or 0, s[2] or 0)):
                if cls == 'Texture' and st in ('Vertex', 'Fragment'):
                    rows.append(texture(1 if st == 'Vertex' else 2, idx, 0x600000 + (1 if st == 'Vertex' else 2) * 0x100 + idx))
                elif cls == 'Sampler' and st in ('Vertex', 'Fragment'):
                    rows.append(sampler(1 if st == 'Vertex' else 2, idx))
            arr = (Resource * len(rows))(*rows)
            draw = Draw(1, 0, 3, 1, 0)
            plan = GenericPlan()
            err.value = b''
            rc = lib.tegu_air_render_pipeline_plan_generic(
                pipeline, arr, len(arr), None, 0, C.byref(draw), C.byref(plan), err, len(err))
            results.append((job['id'], 'plan' if rc == 0 else 'plan_refused',
                            f'bindings={plan.binding_count} {err.value.decode(errors="replace")}'))
        except Exception as exc:
            results.append((job['id'], 'exception', repr(exc)))
        finally:
            if pipeline:
                lib.tegu_air_render_pipeline_close(pipeline)
            if vh:
                lib.tegu_air_library_close(vh)
            if fh:
                lib.tegu_air_library_close(fh)
    counts = {}
    for r in results:
        counts[r[1]] = counts.get(r[1], 0) + 1
    print('summary', counts)
    for r in results:
        print(*r)


if __name__ == '__main__':
    main()
