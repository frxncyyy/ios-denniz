#!/usr/bin/env python3
"""RUNTIME_SPEC (draw-time stand-in) through the AIR compile, on the 234 captured CA records, with the
m2v metadata (C6t flow) or the AIR plan (new): status per record + the push layout the frontend's
tegu_spec_push_scan reads from the modules vs the plan's constants. usage: standin.py BRIDGE MALI m2v|air"""
import ctypes as C, json, sys, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import plan_abi as B  # noqa: E402
P, A, R = B.P, B.A, B.R


def scan(words):
    import struct
    w = struct.unpack('<%dI' % (len(words) // 4), words)
    types, consts, ids = {}, [], {}
    at = 5
    while at < len(w):
        op, ln = w[at] & 0xffff, w[at] >> 16
        if op == 21: types[w[at + 1]] = ('int', w[at + 2])
        if op == 20: types[w[at + 1]] = ('bool',)
        if op in (48, 49, 50): consts.append(w[at + 2])
        if op == 71 and ln >= 4 and w[at + 2] == 1: ids[w[at + 1]] = w[at + 3]
        at += ln
    return sorted(ids[c] for c in consts if c in ids)


def create(L, b, m, c, cap, profile, mode):
    err = C.create_string_buffer(4096)
    libbytes = (R.CORPUS / cap['library']).read_bytes()
    base = R.open_lib(L, R.CORPUS / cap['library'], err)
    vh = R.specialize(L, base, cap['vertex'], cap['vc'], err)
    fh = R.specialize(L, base, cap['fragment'], cap['fc'], err)
    n = max(1, len(cap['targets']))
    vn, fn = cap['vertex'].encode(), cap['fragment'].encode()
    attrs = (A.AirAttribute * max(1, len(cap['attrs'])))(*[A.AirAttribute(*a) for a in cap['attrs']])
    lays = (P.Layout * max(1, len(cap['layouts'])))(*[P.Layout(*a) for a in cap['layouts']])
    at = (P.Target * n)(*[P.Target(*t) for t in cap['targets']])
    bl = (P.Blend * n)(*[P.Blend(*x) for x in cap['blends']])
    blobs = [B.Blob(), B.Blob(), B.Blob()]
    V = C.c_void_p
    tgt = cap['targets']
    if mode == 'm2v':
        at_m = (R.Attachment * n)(*[R.Attachment(t[0], t[1]) for t in cap['targets']])
        bl_m = (R.live.Blend * n)(*[R.live.Blend(*x) for x in cap['blends']])
        attrs_m = (R.live.VertexAttribute * max(1, len(cap['attrs'])))(*[R.live.VertexAttribute(*a) for a in cap['attrs']])
        lays_m = (R.live.VertexLayout * max(1, len(cap['layouts'])))(*[R.live.VertexLayout(*a) for a in cap['layouts']])
        blobs = [R.live.Blob(), R.live.Blob(), R.live.Blob()]
        if tgt:
            h = L.b.tegu_air_render_pipeline_create_with_attachment_blends(vh, vn, len(vn), fh, fn, len(fn), at_m, len(tgt), bl_m,
                attrs_m, len(cap['attrs']), lays_m, len(cap['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
        elif not cap['blends']:
            h = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor(vh, vn, len(vn), fh, fn, len(fn), cap['format'],
                attrs_m, len(cap['attrs']), lays_m, len(cap['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
        else:
            cb = L.b.tegu_air_render_pipeline_create_with_blend
            cb.restype = C.c_void_p
            bt = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes
            cb.argtypes = bt[:11] + [C.POINTER(R.live.Blend)] + bt[11:]
            h = cb(vh, vn, len(vn), fh, fn, len(fn), cap['format'], attrs_m, len(cap['attrs']), lays_m, len(cap['layouts']),
                   bl_m if cap['blends'] else None, profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    else:
        keep = []
        d = P.build_desc(c, keep)
        o, nn, facts = C.c_void_p(), C.c_size_t(), B.U32()
        e = C.create_string_buffer(512)
        m.tegu_mali_air_render_metadata(C.byref(d), C.byref(o), C.byref(nn), C.byref(facts), e, len(e))
        h = b.tegu_air_render_pipeline_create_planned(vh, vn, len(vn), fh, fn, len(fn), 0 if tgt else cap['format'],
            C.cast(attrs, V), len(cap['attrs']), C.cast(lays, V), len(cap['layouts']), C.cast(bl, V) if (cap['blends'] and not tgt) else None,
            C.cast(at, V) if tgt else None, len(tgt), C.cast(bl, V) if tgt else None, o, nn.value, profile, len(profile),
            *[C.byref(x) for x in blobs], err, len(err))
    vs, fs, meta = [C.string_at(x.bytes, x.length) for x in blobs]
    return vs, fs, meta, (blobs,)


def main():
    bridge, mali, mode = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3]
    cases = dict(A.cases_all())
    profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    B.Lb = L = R.Libs(bridge, mali)
    b, m = B.load(bridge, mali)
    L.m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p] * 17
    cnt = collections.Counter()
    for cid, c in cases.items():
        cap = c['cap']
        vs, fs, meta, keepalive = create(L, b, m, c, cap, profile, mode)
        libbytes = (R.CORPUS / cap['library']).read_bytes()
        attrs = (A.AirAttribute * max(1, len(cap['attrs'])))(*[A.AirAttribute(*a) for a in cap['attrs']])
        vn, fn = cap['vertex'].encode(), cap['fragment'].encode()
        V = C.c_void_p
        md = json.loads(meta)
        plan_ids = [sorted(cst['id'] for cst in st['specialization']['constants']) for st in md['stages']]
        layout_ok = [scan(vs), scan(fs)] == plan_ids
        opt = A.options_for(cap)
        opt[1] |= 64
        lib = C.create_string_buffer(libbytes, len(libbytes))
        vcs, k1 = A.constants(cap['vc'])
        fcs, k2 = A.constants(cap['fc'])
        ap = A.AirPair(1, len(cap['attrs']), A.AirFunction(C.cast(lib, V), len(libbytes), vn, vcs, len(cap['vc']), 0),
                       A.AirFunction(C.cast(lib, V), len(libbytes), fn, fcs, len(cap['fc']), 0), attrs, meta, len(meta))
        v, f = R.live.Graphics(), R.live.Graphics()
        pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
        e2 = C.create_string_buffer(1024)
        rc = L.m.tegu_mali_compile_air_pair_options(C.byref(ap), vs, len(vs), fs, len(fs), None, 0, None, 0, opt,
                                                    C.byref(v), C.byref(f), pair, plan, varying, e2, len(e2))
        key = (rc, layout_ok, e2.value.decode(errors='replace')[:80] if rc else '', len(plan_ids[1]) > 0)
        cnt[key] += 1
    for k, v in cnt.most_common(): print(v, 'rc=%d layout_matches_plan=%s spec_constants=%s %s' % (k[0], k[1], k[3], k[2]))


if __name__ == '__main__':
    main()
