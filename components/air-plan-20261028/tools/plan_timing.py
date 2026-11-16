#!/usr/bin/env python3
"""Render pipeline creation + first AIR compile, before (m2v translation for the planner metadata) and after
(AIR-derived plan, no translation), over the 234 captured CA records, in one process per mode (warm caches as
on a device after the first variant). Records the same code hashes both ways. Host CPU time (process_time).

usage: plan_timing.py BRIDGE MALI old|new OUT.jsonl
"""
import ctypes as C, json, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import plan_abi as B  # noqa: E402
P, A, R = B.P, B.A, B.R


def main():
    bridge, mali, mode, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3], Path(sys.argv[4])
    cases = dict(A.cases_all())
    profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    B.Lb = L = R.Libs(bridge, mali)
    b, m = B.load(bridge, mali)
    L.m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p] * 17
    L.m.tegu_mali_compile_air_pair_options.restype = C.c_int
    rows = []
    for cid, c in cases.items():
        cap = c['cap']
        err = C.create_string_buffer(4096)
        libbytes = (R.CORPUS / cap['library']).read_bytes()
        base = R.open_lib(L, R.CORPUS / cap['library'], err)
        vh = R.specialize(L, base, cap['vertex'], cap['vc'], err)
        fh = R.specialize(L, base, cap['fragment'], cap['fc'], err)
        n = max(1, len(cap['targets']))
        vn, fn = cap['vertex'].encode(), cap['fragment'].encode()
        t0 = time.process_time()
        if mode == 'old':
            at = (R.Attachment * n)(*[R.Attachment(t[0], t[1]) for t in cap['targets']])
            bl = (R.live.Blend * n)(*[R.live.Blend(*x) for x in cap['blends']])
            attrs = (R.live.VertexAttribute * max(1, len(cap['attrs'])))(*[R.live.VertexAttribute(*a) for a in cap['attrs']])
            lays = (R.live.VertexLayout * max(1, len(cap['layouts'])))(*[R.live.VertexLayout(*a) for a in cap['layouts']])
            blobs = [R.live.Blob(), R.live.Blob(), R.live.Blob()]
            if cap['targets']:
                h = L.b.tegu_air_render_pipeline_create_with_attachment_blends(vh, vn, len(vn), fh, fn, len(fn), at, len(cap['targets']), bl,
                    attrs, len(cap['attrs']), lays, len(cap['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
            elif cap['blends']:
                cb = L.b.tegu_air_render_pipeline_create_with_blend
                cb.restype = C.c_void_p
                base_t = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes
                cb.argtypes = base_t[:11] + [C.POINTER(R.live.Blend)] + base_t[11:]
                h = cb(vh, vn, len(vn), fh, fn, len(fn), cap['format'], attrs, len(cap['attrs']), lays, len(cap['layouts']), bl,
                       profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
            else:
                h = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor(vh, vn, len(vn), fh, fn, len(fn), cap['format'],
                    attrs, len(cap['attrs']), lays, len(cap['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
        else:
            keep = []
            d = P.build_desc(c, keep)
            o, nn, facts = C.c_void_p(), C.c_size_t(), B.U32()
            e = C.create_string_buffer(512)
            rc = m.tegu_mali_air_render_metadata(C.byref(d), C.byref(o), C.byref(nn), C.byref(facts), e, len(e))
            h = None
            blobs = [B.Blob(), B.Blob(), B.Blob()]
            if rc == 0:
                at = (P.Target * n)(*[P.Target(*t) for t in cap['targets']])
                bl = (P.Blend * n)(*[P.Blend(*x) for x in cap['blends']])
                attrs = (A.AirAttribute * max(1, len(cap['attrs'])))(*[A.AirAttribute(*a) for a in cap['attrs']])
                lays = (P.Layout * max(1, len(cap['layouts'])))(*[P.Layout(*a) for a in cap['layouts']])
                tgt = cap['targets']
                V = C.c_void_p
                h = b.tegu_air_render_pipeline_create_planned(vh, vn, len(vn), fh, fn, len(fn), 0 if tgt else cap['format'],
                    C.cast(attrs, V), len(cap['attrs']), C.cast(lays, V), len(cap['layouts']), C.cast(bl, V) if (cap['blends'] and not tgt) else None,
                    C.cast(at, V) if tgt else None, len(tgt), C.cast(bl, V) if tgt else None, o, nn.value, profile, len(profile),
                    *[C.byref(x) for x in blobs], err, len(err))
                m.tegu_mali_air_metadata_free(o)
        t1 = time.process_time()
        row = {'id': cid, 'name': c['name'], 'create_ms': round((t1 - t0) * 1e3, 2), 'created': bool(h)}
        if h:
            vs, fs, meta = [C.string_at(x.bytes, x.length) for x in blobs]
            r = A.compile_one(L, 'air', cap, vs, fs, meta, libbytes)
            row.update(compile_ms=round(r['cpu_s'] * 1e3, 2), rc=r['rc'], vs_sha=r.get('vs_sha'), fs_sha=r.get('fs_sha'), error=r.get('error'))
        rows.append(row)
    out.write_text(''.join(json.dumps(r) + '\n' for r in rows))
    tot = lambda k: sum(r.get(k, 0) or 0 for r in rows)
    ff = [r for r in rows if 'fixed_frag' in r['name']]
    print(mode, 'created', sum(r['created'] for r in rows), 'compiled', sum(1 for r in rows if r.get('rc') == 0),
          f"create_ms total {tot('create_ms'):.0f} compile_ms total {tot('compile_ms'):.0f}",
          f"fixed_frag create median {sorted(r['create_ms'] for r in ff)[len(ff)//2]:.2f} compile median {sorted(r.get('compile_ms', 0) for r in ff)[len(ff)//2]:.2f}")


if __name__ == '__main__':
    main()
