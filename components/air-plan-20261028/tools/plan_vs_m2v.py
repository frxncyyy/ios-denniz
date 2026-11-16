#!/usr/bin/env python3
"""AIR-derived render plan (tegu_mali_air_render_metadata) versus the m2v translation's metadata,
field by field, over the captured CoreAnimation corpus (pipeline-corpus/corpus.txt, unique records
a-NNN as dump_meta.py numbers them; m2v metadata in meta/a-NNN.meta.bin). Host only.

usage: plan_vs_m2v.py LIB [ids...] [--show]   (fork per case; rows to runs/<lib dir>/plan.jsonl)
"""
import ctypes as C, json, os, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
OUT = HERE.parent
sys.path.insert(0, str(HERE.parents[1] / 'air-nir-20261028/tools'))
import air_vs_m2v as A  # noqa: E402
R = A.R
U32 = C.c_uint32


class Layout(C.Structure):
    _fields_ = [('index', U32), ('stride', U32), ('step_function', U32), ('step_rate', U32)]


class Target(C.Structure):
    _fields_ = [('format', U32), ('write_mask', U32)]


class Blend(C.Structure):
    _fields_ = [(n, U32) for n in ('version', 'enabled', 'rgb_op', 'rgb_src', 'rgb_dst', 'alpha_op', 'alpha_src', 'alpha_dst', 'write_mask')]


class Desc(C.Structure):
    _fields_ = [('version', U32), ('attribute_count', U32), ('layout_count', U32), ('target_count', U32),
                ('vertex', A.AirFunction), ('fragment', A.AirFunction),
                ('attributes', C.POINTER(A.AirAttribute)), ('layouts', C.POINTER(Layout)),
                ('targets', C.POINTER(Target)), ('blends', C.POINTER(Blend)), ('color_format', U32), ('reserved', U32)]


def load(lib):
    m = C.CDLL(str(lib))
    m.tegu_mali_air_render_metadata.argtypes = [C.c_void_p, C.POINTER(C.c_void_p), C.POINTER(C.c_size_t), C.POINTER(U32), C.c_char_p, C.c_size_t]
    m.tegu_mali_air_render_metadata.restype = C.c_int
    m.tegu_mali_air_metadata_free.argtypes = [C.c_void_p]
    return m


def build_desc(c, keep):
    cap = c['cap']
    lib = (R.CORPUS / cap['library']).read_bytes()
    buf = C.create_string_buffer(lib, len(lib))
    keep.append(buf)

    def fn(name, rows):
        cs, k = A.constants(rows)
        keep.extend([cs, k, name.encode()])
        return A.AirFunction(C.cast(buf, C.c_void_p), len(lib), keep[-1], cs, len(rows), 0)
    at, ly, tg, bl = cap['attrs'], cap['layouts'], cap['targets'], cap['blends']
    attrs = (A.AirAttribute * max(1, len(at)))(*[A.AirAttribute(*a) for a in at])
    lays = (Layout * max(1, len(ly)))(*[Layout(*a) for a in ly])
    tgs = (Target * max(1, len(tg)))(*[Target(*a) for a in tg])
    bls = (Blend * max(1, len(bl)))(*[Blend(*a) for a in bl])
    keep.extend([attrs, lays, tgs, bls])
    d = Desc(1, len(at), len(ly), len(tg), fn(cap['vertex'], cap['vc']), fn(cap['fragment'], cap['fc']),
             attrs, lays, tgs if tg else None, bls if bl else None, cap['format'] if not tg else 0, 0)
    return d


def run(m, c):
    keep = []
    d = build_desc(c, keep)
    out, n, facts = C.c_void_p(), C.c_size_t(), U32()
    err = C.create_string_buffer(512)
    t0 = time.process_time()
    rc = m.tegu_mali_air_render_metadata(C.byref(d), C.byref(out), C.byref(n), C.byref(facts), err, len(err))
    ms = (time.process_time() - t0) * 1e3
    if rc:
        return {'rc': rc, 'error': err.value.decode(errors='replace'), 'ms': ms}
    text = C.string_at(out, n.value)
    m.tegu_mali_air_metadata_free(out)
    return {'rc': 0, 'meta': json.loads(text), 'facts': facts.value, 'ms': ms}


PLANNER = ('layout', 'interface', 'dependencies', 'vertex_input', 'graphics', 'variant_bits')


def strip_m2v(d):
    """The m2v fields AIR code has no counterpart for: its colour rounding emulation."""
    d = json.loads(json.dumps(d))
    for p in d['interface']['push']:
        p['fields'] = [f for f in p['fields'] if 'ColorWriteRule' not in f['field']]
        n = len(p['fields'])
        p['push_range'] = None if not n else [p['push_range'][0], max(f['loc']['Push']['offset'] + f['size'] for f in p['fields']) - p['push_range'][0]]
    d['layout']['push_constants'] = [r for r in ({'offset': p['push_range'][0], 'size': p['push_range'][1], 'stages': 1 if p['stage'] == 'Vertex' else 16}
                                                 for p in d['interface']['push'] if p['push_range'])]
    d['graphics']['color_write_rules'] = []
    # an attachment m2v rounds in the shader takes no Vulkan blend there (link/graphics.rs
    # `replaced`); AIR code writes the unrounded colour and the attachment keeps its blend.
    rounded = set(d['stages'][-1].get('rounded_colors', [])) | set(d['stages'][-1].get('half_bit_colors', []))
    d['_rounded'] = sorted(rounded)
    return d


def diff(a, b, path=''):
    if type(a) != type(b):
        return [f'{path}: m2v {json.dumps(a)[:160]} air {json.dumps(b)[:160]}']
    if isinstance(a, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                out.append(f'{path}.{k}: only in {"m2v" if k in a else "air"}')
            else:
                out += diff(a[k], b[k], f'{path}.{k}')
        return out
    if isinstance(a, list):
        if len(a) != len(b):
            return [f'{path}: len m2v {len(a)} air {len(b)}: m2v {json.dumps(a)[:300]} air {json.dumps(b)[:300]}']
        out = []
        for i, (x, y) in enumerate(zip(a, b)):
            out += diff(x, y, f'{path}[{i}]')
        return out
    return [] if a == b else [f'{path}: m2v {json.dumps(a)[:160]} air {json.dumps(b)[:160]}']


def compare(m2v, air):
    m = strip_m2v(m2v)
    out = []
    for i in m['_rounded']:
        if i < len(m['graphics']['color_attachments']) and m['graphics']['color_attachments'][i] and air['graphics']['color_attachments'][i]:
            m['graphics']['color_attachments'][i]['blend'] = air['graphics']['color_attachments'][i]['blend']
    for k in PLANNER:
        out += diff(m[k], air[k], k)
    out += diff([s['specialization'] for s in m['stages']], [s['specialization'] for s in air['stages']], 'stages.specialization')
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show = '--show' in sys.argv
    lib = Path(args[0]).resolve()
    ids = args[1:]
    cases = dict(A.cases_all())
    if not ids:
        ids = list(cases)
    rows = []
    for cid in ids:
        r, w = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(r)
            try:
                res = run(load(lib), cases[cid])
            except Exception as x:  # noqa: BLE001
                res = {'rc': -1, 'error': repr(x)[:300]}
            os.write(w, json.dumps(res).encode())
            os._exit(0)
        os.close(w)
        data = b''
        while True:
            chunk = os.read(r, 1 << 20)
            if not chunk:
                break
            data += chunk
        os.close(r)
        _, st = os.waitpid(pid, 0)
        res = json.loads(data) if data else {'rc': -2, 'error': f'crash status {st}'}
        row = {'id': cid, 'name': cases[cid]['name'], 'rc': res['rc'], 'ms': round(res.get('ms', 0), 2)}
        if res['rc']:
            row['error'] = res['error']
        else:
            m2v = json.load(open(OUT / 'meta' / f'{cid}.meta.bin'))
            row['diffs'] = compare(m2v, res['meta'])
            if show:
                print(json.dumps(res['meta'], indent=1)[:6000])
        rows.append(row)
        status = 'refused ' + row['error'] if res['rc'] else ('IDENTICAL' if not row['diffs'] else f'{len(row["diffs"])} diffs')
        print(cid, row['name'], status, f'{row["ms"]}ms', flush=True)
        for x in row.get('diffs', [])[:12]:
            print('   ', x)
    (lib.parent / 'plan.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in rows))
    ok = sum(1 for r in rows if not r['rc'] and not r['diffs'])
    print(f'identical {ok}/{len(rows)} refused {sum(1 for r in rows if r["rc"])}')


if __name__ == '__main__':
    main()
