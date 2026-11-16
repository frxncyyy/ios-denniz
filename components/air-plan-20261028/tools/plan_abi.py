#!/usr/bin/env python3
"""ABI equivalence of the AIR-planned render pipeline handle (tegu_air_render_pipeline_create_planned,
metadata from tegu_mali_air_render_metadata) and the m2v-translated handle of the same captured CA record:
the planner's outputs for the same draw inputs, byte for byte (GenericPlan + GraphicsSpecializationPlan of
tegu_air_render_pipeline_plan_generic_specialized, vertex fetch, push), the buffer templates of the plan
memo, a serialize/deserialize round trip of the AIR handle, and creation time. Host only.

usage: plan_abi.py BRIDGE MALI [ids...]
"""
import ctypes as C, hashlib, json, os, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
OUT = HERE.parent
sys.path.insert(0, str(HERE))
import plan_vs_m2v as P  # noqa: E402
A, R = P.A, P.R
U32, U64 = C.c_uint32, C.c_uint64
V_ = C.c_void_p


class Res(C.Structure):
    _fields_ = [(n, U32) for n in ('kind', 'stage', 'index', 'format', 'texture_type', 'width', 'height', 'array_length',
                                   'mipmap_levels', 'sample_count', 'usage', 'sparse', 'depth_twin')] + \
               [('swizzle', U32 * 4)] + [(n, U32) for n in ('parent_level', 'parent_width', 'parent_height', 'parent_depth',
                                                            'own_format_views', 'address_mode_identity')] + \
               [(n, U64) for n in ('length', 'offset', 'gpu_address', 'resource_id')] + [('stride', U32), ('reserved', U32)] + \
               [(n, U32) for n in ('min_filter', 'mag_filter', 'mip_filter', 'address_s', 'address_t', 'address_r', 'border_color',
                                   'normalized_coordinates', 'lod_average', 'max_anisotropy', 'compare_function', 'reduction_mode')] + \
               [('lod_min_clamp', C.c_float), ('lod_max_clamp', C.c_float)]


class Draw(C.Structure):
    _fields_ = [(n, U32) for n in ('version', 'vertex_start', 'vertex_count', 'instance_count', 'base_instance')]


class Blob(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('length', C.c_size_t)]


TTYPE = {'Type1D': 0, 'Type2D': 2, 'Type3D': 7, 'Type2DMultisample': 4}


def resources(meta, layouts):
    rows = []
    for l in layouts:  # vertex buffers: (index, stride, step_function, step_rate)
        r = Res()
        r.kind, r.stage, r.index, r.length, r.offset = 1, 1, l[0], l[1] * 64 + 256, 0
        rows.append(r)
    for slot, iface in meta['interface']['slots']:
        st = 1 if slot['stage'] == 'Vertex' else 2
        k = list(iface)[0]
        r = Res()
        r.stage, r.index = st, slot['index']
        if k == 'Buffer':
            if st == 1 and any(l[0] == slot['index'] for l in layouts):
                continue
            w = iface[k]['window']
            end = w['Bounded']['start'] + w['Bounded']['len'] if isinstance(w, dict) else 4096
            r.kind, r.length, r.offset = 1, max(256, (end + 255) // 256 * 256 + 256), 64
        elif k == 'Texture':
            t = iface[k]
            r.kind, r.format, r.texture_type = 2, 80, TTYPE[t['texture_type']]
            r.width = 64
            r.height = 1 if t['texture_type'] == 'Type1D' else 64
            r.array_length, r.mipmap_levels, r.usage, r.address_mode_identity = 1, 1, 1, 1
            r.sample_count = 4 if t['texture_type'] == 'Type2DMultisample' else 1
            r.swizzle[:] = [2, 3, 4, 5]
            r.resource_id = 0x1000 + st * 64 + slot['index']
        else:
            r.kind, r.min_filter, r.mag_filter, r.normalized_coordinates = 3, 1, 1, 1
            r.max_anisotropy, r.lod_max_clamp = 1, 1000.0
        rows.append(r)
    return rows


def plan_abi_view(g, sp, meta):
    """The plan as the frontend consumes it, minus m2v's ColorWriteRule push words (its own colour
    rounding emulation, read by no AIR code): every binding record, the specialization sidecar, the
    SlotBindOffset words and every push span start; the fragment span's length counts only the
    SlotBindOffset words."""
    import struct
    ver, bc, pc, pbl = struct.unpack_from('<4I', g, 0)
    head = g[16:32]
    bindings = g[32:32 + 120 * 16]
    spans = [struct.unpack_from('<4I', g, 32 + 120 * 16 + 16 * i) for i in range(pc)]
    pbytes = g[32 + 120 * 16 + 16 * 8:32 + 120 * 16 + 16 * 8 + 256]
    cwr = set()
    for p in meta['interface']['push']:
        for f in p['fields']:
            if 'ColorWriteRule' in f['field']:
                cwr.add(f['loc']['Push']['offset'])
    view = []
    for st, off, ln, bo in spans:
        words = []
        for k in range(0, ln, 4):
            if off + k in cwr:
                continue
            words.append((off + k, pbytes[bo + k:bo + k + 4].hex()))
        view.append((st, off, len(words) * 4, words))
    view = [v for v in view if v[2]]
    return hashlib.sha256(json.dumps([ver, bc, head.hex(), bindings.hex(), view, sp.hex()]).encode()).hexdigest()[:16]


def load(bridge, mali):
    b = C.CDLL(str(bridge))
    m = P.load(mali)
    b.tegu_air_render_pipeline_create_planned.restype = C.c_void_p
    V, Z = C.c_void_p, C.c_size_t
    b.tegu_air_render_pipeline_create_planned.argtypes = [V, C.c_char_p, Z, V, C.c_char_p, Z, U32, V, Z, V, Z, V, V, Z, V,
                                                          V, Z, C.c_char_p, Z, V, V, V, C.c_char_p, Z]
    b.tegu_air_render_pipeline_plan_generic_specialized.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                                                                     C.c_void_p, C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t]
    b.tegu_air_render_pipeline_plan_generic_buffer_templates.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                                                                          C.c_void_p, C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t]
    b.tegu_air_render_pipeline_vertex_fetch.argtypes = [C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t]
    b.tegu_air_render_pipeline_push.argtypes = [C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t]
    b.tegu_air_render_pipeline_serialize.argtypes = [C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t]
    b.tegu_air_render_pipeline_deserialize.restype = C.c_void_p
    b.tegu_air_render_pipeline_deserialize.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p,
                                                       C.c_void_p, C.c_char_p, C.c_size_t]
    b.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
    b.tegu_air_blob_free.argtypes = [C.c_void_p]
    return b, m


def planner_outputs(b, h, meta, cap):
    lays = cap['layouts']
    rs = resources(meta, lays)
    arr = (Res * max(1, len(rs)))(*rs)
    lay = (P.Layout * max(1, len(lays)))(*[P.Layout(*l) for l in lays])
    d = Draw(1, 0, 6, 1, 0)
    gp, sp = C.create_string_buffer(2848), C.create_string_buffer(3360)
    e = C.create_string_buffer(512)
    rc = b.tegu_air_render_pipeline_plan_generic_specialized(h, arr, len(rs), lay, len(lays), C.byref(d), gp, sp, e, len(e))
    out = {'plan_rc': rc, 'plan_err': e.value.decode(errors='replace'), 'plan': hashlib.sha256(gp.raw + sp.raw).hexdigest()[:16],
           'abi': plan_abi_view(gp.raw, sp.raw, meta)}
    if rc == 0:
        tp = C.create_string_buffer(1424)
        e2 = C.create_string_buffer(512)
        rc2 = b.tegu_air_render_pipeline_plan_generic_buffer_templates(h, arr, len(rs), lay, len(lays), C.byref(d), gp, tp, e2, len(e2))
        out.update(templates_rc=rc2, templates=hashlib.sha256(tp.raw).hexdigest()[:16] if rc2 == 0 else e2.value.decode(errors='replace'))
    vf = C.create_string_buffer(16 + 31 * 28)
    e3 = C.create_string_buffer(512)
    out['vertex_fetch'] = (b.tegu_air_render_pipeline_vertex_fetch(h, vf, e3, len(e3)), hashlib.sha256(vf.raw).hexdigest()[:16])
    pu = C.create_string_buffer(4096)
    e4 = C.create_string_buffer(512)
    out['push'] = (b.tegu_air_render_pipeline_push(h, pu, e4, len(e4)), hashlib.sha256(pu.raw).hexdigest()[:16])
    return out


def run_case(b, m, c, profile):
    cap = c['cap']
    err = C.create_string_buffer(4096)
    libbytes = (R.CORPUS / cap['library']).read_bytes()
    base = R.open_lib(Lb, R.CORPUS / cap['library'], err)
    vh = R.specialize(Lb, base, cap['vertex'], cap['vc'], err)
    fh = R.specialize(Lb, base, cap['fragment'], cap['fc'], err)
    n = max(1, len(cap['targets']))
    at = (P.Target * n)(*[P.Target(*t) for t in cap['targets']])
    bl = (P.Blend * n)(*[P.Blend(*x) for x in cap['blends']])
    attrs = (A.AirAttribute * max(1, len(cap['attrs'])))(*[A.AirAttribute(*a) for a in cap['attrs']])
    lays = (P.Layout * max(1, len(cap['layouts'])))(*[P.Layout(*a) for a in cap['layouts']])
    vn, fn = cap['vertex'].encode(), cap['fragment'].encode()
    row = {}
    # m2v handle (the oracle) through the same entries the frontend uses (replay's record types)
    at_m = (R.Attachment * n)(*[R.Attachment(t[0], t[1]) for t in cap['targets']])
    bl_m = (R.live.Blend * n)(*[R.live.Blend(*x) for x in cap['blends']])
    attrs_m = (R.live.VertexAttribute * max(1, len(cap['attrs'])))(*[R.live.VertexAttribute(*a) for a in cap['attrs']])
    lays_m = (R.live.VertexLayout * max(1, len(cap['layouts'])))(*[R.live.VertexLayout(*a) for a in cap['layouts']])
    blobs = [R.live.Blob(), R.live.Blob(), R.live.Blob()]
    t0 = time.perf_counter()
    if cap['targets']:
        hm = Lb.b.tegu_air_render_pipeline_create_with_attachment_blends(vh, vn, len(vn), fh, fn, len(fn), at_m, len(cap['targets']), bl_m,
            attrs_m, len(cap['attrs']), lays_m, len(cap['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    elif cap['blends']:
        cb = Lb.b.tegu_air_render_pipeline_create_with_blend
        cb.restype = C.c_void_p
        base_t = Lb.b.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes
        cb.argtypes = base_t[:11] + [C.POINTER(R.live.Blend)] + base_t[11:]
        hm = cb(vh, vn, len(vn), fh, fn, len(fn), cap['format'], attrs_m, len(cap['attrs']), lays_m, len(cap['layouts']), bl_m,
                profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    else:
        hm = Lb.b.tegu_air_render_pipeline_create_with_vertex_descriptor(vh, vn, len(vn), fh, fn, len(fn), cap['format'],
            attrs_m, len(cap['attrs']), lays_m, len(cap['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    row['m2v_ms'] = (time.perf_counter() - t0) * 1e3
    mmeta = json.loads(C.string_at(blobs[2].bytes, blobs[2].length))
    # AIR-planned handle: reflection, then the bridge entry
    keep = []
    d = P.build_desc(c, keep)
    o, nn, facts = C.c_void_p(), C.c_size_t(), U32()
    e = C.create_string_buffer(512)
    t0 = time.perf_counter()
    rc = m.tegu_mali_air_render_metadata(C.byref(d), C.byref(o), C.byref(nn), C.byref(facts), e, len(e))
    t1 = time.perf_counter()
    if rc:
        row.update(air='refused', error=e.value.decode(errors='replace'))
        return row
    ablobs = [Blob(), Blob(), Blob()]
    tgt = cap['targets']
    ha = b.tegu_air_render_pipeline_create_planned(vh, vn, len(vn), fh, fn, len(fn), 0 if tgt else cap['format'],
        C.cast(attrs, V_), len(cap['attrs']), C.cast(lays, V_), len(cap['layouts']), C.cast(bl, V_) if (cap['blends'] and not tgt) else None,
        C.cast(at, V_) if tgt else None, len(tgt), C.cast(bl, V_) if tgt else None, o, nn.value, profile, len(profile),
        *[C.byref(x) for x in ablobs], err, len(err))
    t2 = time.perf_counter()
    m.tegu_mali_air_metadata_free(o)
    row.update(reflect_ms=(t1 - t0) * 1e3, create_ms=(t2 - t1) * 1e3)
    if not ha:
        row.update(air='create_refused', error=err.value.decode(errors='replace'))
        return row
    ameta = json.loads(C.string_at(ablobs[2].bytes, ablobs[2].length))
    row['meta_diffs'] = P.compare(mmeta, ameta)
    pm, pa = planner_outputs(b, hm, mmeta, cap), planner_outputs(b, ha, ameta, cap)
    row['planner_identical'] = pm == pa
    strip = lambda o: {k: v for k, v in o.items() if k != 'plan'}
    row['planner_abi_equivalent'] = strip(pm) == strip(pa)
    row['planner'] = {k: pa.get(k) for k in ('plan_rc', 'plan_err', 'templates_rc')}
    if pm != pa:
        row['planner_m2v'], row['planner_air'] = pm, pa
    # serialize -> deserialize -> same planner outputs, same blobs
    ser = Blob()
    e5 = C.create_string_buffer(256)
    if b.tegu_air_render_pipeline_serialize(ha, C.byref(ser), e5, len(e5)) == 0:
        rb = [Blob(), Blob(), Blob()]
        e6 = C.create_string_buffer(256)
        hd = b.tegu_air_render_pipeline_deserialize(ser.bytes, ser.length, profile, len(profile), *[C.byref(x) for x in rb], e6, len(e6))
        row['roundtrip'] = bool(hd) and planner_outputs(b, hd, ameta, cap) == pa and \
            all(C.string_at(x.bytes, x.length) == C.string_at(y.bytes, y.length) for x, y in zip(rb[:2], ablobs[:2]))
        if not hd:
            row['roundtrip_err'] = e6.value.decode()
    else:
        row['roundtrip'] = 'serialize: ' + e5.value.decode()
    return row


def main():
    global Lb
    bridge, mali = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    ids = sys.argv[3:]
    cases = dict(A.cases_all())
    ids = ids or list(cases)
    profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    Lb = R.Libs(bridge, mali)
    b, m = load(bridge, mali)
    rows = []
    for cid in ids:
        try:
            row = run_case(b, m, cases[cid], profile)
        except Exception as x:  # noqa: BLE001
            row = {'air': 'harness_error', 'error': repr(x)[:300]}
        row.update(id=cid, name=cases[cid]['name'])
        rows.append(row)
        ok = row.get('planner_abi_equivalent') and not row.get('meta_diffs') and row.get('roundtrip') is True
        print(cid, row['name'], 'OK' if ok else json.dumps({k: v for k, v in row.items() if k not in ('id', 'name')})[:600], flush=True)
    (OUT / 'abi.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in rows))
    good = [r for r in rows if r.get('planner_abi_equivalent') and not r.get('meta_diffs') and r.get('roundtrip') is True]
    same = [r for r in rows if r.get('planner_identical') and not r.get('meta_diffs') and r.get('roundtrip') is True]
    print(f'planner bytes identical {len(same)}/{len(rows)}; ABI-equivalent (modulo m2v ColorWriteRule words) {len(good)}/{len(rows)}')
    import statistics
    for k in ('m2v_ms', 'reflect_ms', 'create_ms'):
        v = [r[k] for r in rows if k in r]
        ff = [r[k] for r in rows if k in r and 'fixed_frag' in r['name']]
        if v:
            print(f'{k}: median {statistics.median(v):.2f} total {sum(v):.0f} | fixed_frag median {statistics.median(ff) if ff else 0:.2f}')


if __name__ == '__main__':
    main()
