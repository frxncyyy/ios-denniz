#!/usr/bin/env python3
"""Survey real installed VS-texture pairs for frontend-side blockers.

Uses the frozen baseline AIR bridge. No production edit and no GPU work.
Reports the translator's linked vertex fetch mask/formats and whether the
current bounded native fetch list can represent it.
"""
import ctypes as C
import json
import sys
from pathlib import Path

ROOT = Path('$NORD2_WORK/gpu')
BASE = ROOT / 'build/ui-corpus-20261025'
sys.path.insert(0, str(ROOT / 'metal/tegu'))
import check_fixed_ui_live_variants as live


class A(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('location', 'buffer_index', 'format', 'offset', 'stride', 'input_rate', 'divisor')]


class F(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'mask', 'count', 'reserved')] + [('attributes', A * 31)]


def main():
    lib = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    profile = live.profile_bytes(BASE / 'baseline/tegu-air-profile.h')
    lib.tegu_air_render_pipeline_vertex_fetch.argtypes = [C.c_void_p, C.POINTER(F), C.c_char_p, C.c_size_t]
    lib.tegu_air_render_pipeline_vertex_fetch.restype = C.c_int32
    jobs = json.load(open(BASE / 'graphics-jobs.json'))['jobs']
    # entries with a texture in the vertex AIR
    vertex_tex = set()
    for line in open(BASE / 'resources.jsonl'):
        d = json.loads(line)
        if '"air.texture"' in '\n'.join(d.get('metadata') or []):
            vertex_tex.add(d['air_sha256'])
    rows = []
    for job in jobs:
        if job['vertex']['air_sha256'] not in vertex_tex:
            continue
        err = C.create_string_buffer(4096)
        vp = BASE / job['vertex']['air_saved_as']
        fp = BASE / job['fragment']['air_saved_as']
        vh = fh = 0
        try:
            vh = lib.tegu_air_library_open(vp.read_bytes(), vp.stat().st_size, err, len(err))
            fh = lib.tegu_air_library_open(fp.read_bytes(), fp.stat().st_size, err, len(err))
            if not vh or not fh:
                rows.append((job['id'], 'library', err.value.decode(errors='replace')))
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
                rows.append((job['id'], 'translate', err.value.decode(errors='replace')))
                continue
            fetch = F()
            rc = lib.tegu_air_render_pipeline_vertex_fetch(pipeline, C.byref(fetch), err, len(err))
            formats = [fetch.attributes[i].format for i in range(fetch.count)]
            detail = (f'mask={fetch.mask:#x} count={fetch.count} formats={formats} '
                      f'err={err.value.decode(errors="replace")} layouts={job.get("layouts")}')
            if rc:
                # The native fetch API zeroes its output on refusal. The saved
                # translation metadata is the only exact linked-VkFormat source.
                meta_path = BASE / 'graphics-baseline' / job['id'] / 'metadata.json'
                if meta_path.exists():
                    vi = json.load(open(meta_path)).get('vertex_input') or {}
                    linked = [a['format'] for a in vi.get('attributes', [])]
                    detail += f' linked_formats={linked}'
            rows.append((job['id'], 'fetch' if rc == 0 else 'fetch_refused', detail))
            lib.tegu_air_render_pipeline_close(pipeline)
        except Exception as exc:
            rows.append((job['id'], 'exception', repr(exc)))
        finally:
            if vh:
                lib.tegu_air_library_close(vh)
            if fh:
                lib.tegu_air_library_close(fh)
    by = {}
    for r in rows:
        by.setdefault(r[1], 0)
        by[r[1]] += 1
    print('summary', by)
    for r in rows:
        print(*r)


if __name__ == '__main__':
    main()
