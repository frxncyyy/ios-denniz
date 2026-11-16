#!/usr/bin/env python3
"""Host A/B of the captured CoreAnimation pipelines: m2v (AIR -> SPIR-V -> NIR) versus AIR-direct
(AIR -> NIR, tegu_mali_compile_air_pair_options) on the same Valhall (arch9) compiler, same options
as render_pipeline.inc. Every unique `TPC_MISS v1 air` record of pipeline-corpus/corpus.txt.

Each case runs in a forked child (stderr -> file, BIFROST_MESA_DEBUG=shaderdb) so a backend assert is
one row, not a dead batch. Host only; no GPU execution.

usage: air_vs_m2v.py --run NAME [--compiler libnord2_mali_runtime.so] [--ids a-000,a-005] [--limit N]
"""
import argparse, ctypes as C, hashlib, json, os, re, resource, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent
sys.path.insert(0, str(HERE.parents[1] / 'shader-coverage-20261027/tools'))
import replay as R  # noqa: E402

U32 = C.c_uint32


class AirConstant(C.Structure):
    _fields_ = [('name', C.c_char_p), ('index', U32), ('size', U32), ('bits', C.c_uint64)]


class AirFunction(C.Structure):
    _fields_ = [('bitcode', C.c_void_p), ('size', C.c_size_t), ('entry', C.c_char_p),
                ('constants', C.POINTER(AirConstant)), ('constant_count', U32), ('reserved', U32)]


class AirAttribute(C.Structure):
    _fields_ = [('index', U32), ('format', U32), ('offset', U32), ('buffer_index', U32)]


class AirPair(C.Structure):
    _fields_ = [('version', U32), ('attribute_count', U32), ('vertex', AirFunction), ('fragment', AirFunction),
                ('attributes', C.POINTER(AirAttribute)), ('metadata', C.c_char_p), ('metadata_size', C.c_size_t)]


def cases_all():
    libs = {e['library_sha256']: e for e in json.load(open(R.CORPUS / 'functions.json'))['entries']}
    seen = {}
    n = 0
    for line in open(R.GPU / 'build/pipeline-corpus/corpus.txt'):
        if not line.startswith('TPC_MISS v1 air '):
            continue
        f = dict(x.split('=', 1) for x in line.split() if '=' in x)
        k = tuple(sorted((a, b) for a, b in f.items() if a not in ('key', 'profile', 'msgcap')))
        if k in seen:
            continue
        vs = R.parse_spec(f['vspec'])[1] if f['vspec'] != '-' else []
        fs = R.parse_spec(f['fspec'])[1] if f['fspec'] != '-' else []
        sp = lambda key: [tuple(map(int, t.split('/'))) for t in f.get(key, '').rstrip(',').split(',') if t]
        cid = 'a-%03d' % n
        n += 1
        seen[k] = cid
        yield cid, {'name': f['vertex'] + ' + ' + f['fragment'],
                    'cap': {'library': libs[f['vlib']]['library_saved_as'], 'vertex': f['vertex'], 'fragment': f['fragment'],
                            'vc': vs, 'fc': fs, 'targets': sp('targets'), 'blends': sp('blends'), 'attrs': sp('attrs'),
                            'layouts': sp('layouts'), 'format': int(f.get('format', 0))}}


def options_for(c):
    att = [{'format': t[0], 'write_mask': t[1]} for t in c['targets']]
    flags = 1 | 8 | 16
    active = [i for i, x in enumerate(att) if x.get('write_mask', 15)]
    targets = [x['format'] for x in att]
    if len(active) > 1 and len(att) <= 4 and all(targets):
        return (U32 * 12)(2, flags | 32, 0, 0, len(targets), *(targets + [0] * (4 - len(targets))), 0, 0, 0)
    if len(att) > 1:
        return (U32 * 4)(1, flags | 2, active[0] if active else 0, 0)
    return (U32 * 4)(1, flags, 0, 0)


STAT = re.compile(r'(\w[\w ]*?)[:=]\s*([0-9.]+)')


def parse_stats(text):
    """shaderdb lines -> list of dicts (one per compiled program, in order)."""
    out = []
    for line in text.splitlines():
        if 'shader:' in line or line.startswith('MESA_SHADER') or 'instrs' in line:
            d = {}
            for m in re.finditer(r'([0-9.]+)\s+([A-Za-z_][\w]*)', line):
                d[m.group(2)] = float(m.group(1))
            if d:
                d['_line'] = line.strip()
                out.append(d)
    return out


OPENED = {}


def translate(L, c, profile):
    err = C.create_string_buffer(4096)
    key = str(R.CORPUS / c['library'])
    if key not in OPENED:
        OPENED[key] = R.open_lib(L, R.CORPUS / c['library'], err)  # once per library, as the frontend does
    base = OPENED[key]
    vh = R.specialize(L, base, c['vertex'], c['vc'], err)
    fh = R.specialize(L, base, c['fragment'], c['fc'], err)
    n = max(1, len(c['targets']))
    at = (R.Attachment * n)(*[R.Attachment(t[0], t[1]) for t in c['targets']])
    bl = (R.live.Blend * n)(*[R.live.Blend(*b) for b in c['blends']])
    attrs = (R.live.VertexAttribute * max(1, len(c['attrs'])))(*[R.live.VertexAttribute(*a) for a in c['attrs']])
    lays = (R.live.VertexLayout * max(1, len(c['layouts'])))(*[R.live.VertexLayout(*a) for a in c['layouts']])
    blobs = [R.live.Blob(), R.live.Blob(), R.live.Blob()]
    vn, fn = c['vertex'].encode(), c['fragment'].encode()
    if c['targets']:
        p = L.b.tegu_air_render_pipeline_create_with_attachment_blends(vh, vn, len(vn), fh, fn, len(fn), at, len(c['targets']), bl,
            attrs, len(c['attrs']), lays, len(c['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    else:
        p = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor(vh, vn, len(vn), fh, fn, len(fn), c['format'],
            attrs, len(c['attrs']), lays, len(c['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    if not p:
        raise RuntimeError(err.value.decode(errors='replace'))
    return [C.string_at(b.bytes, b.length) for b in blobs]


def constants(rows):
    arr = (AirConstant * max(1, len(rows)))()
    keep = []
    for i, r in enumerate(rows):
        raw = bytes.fromhex(r['value_hex'])
        nm = r['name'].encode() if r.get('kind', 1) == 1 and r['name'] else None
        keep.append(nm)
        arr[i].name = nm
        arr[i].index = r['index']
        arr[i].size = len(raw)
        arr[i].bits = int.from_bytes(raw[:8].ljust(8, b'\0'), 'little')
    return arr, keep


def compile_one(L, mode, c, vs, fs, meta, libbytes):
    v, f = R.live.Graphics(), R.live.Graphics()
    pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
    err = C.create_string_buffer(1024)
    opt = options_for(c)
    t0 = time.process_time()
    if mode == 'm2v':
        rc = L.m.tegu_mali_compile_generic_pair_options(vs, len(vs), fs, len(fs), None, 0, None, 0, opt, C.byref(v), C.byref(f),
                                                         pair, plan, varying, err, len(err))
    else:
        vcs, k1 = constants(c['vc'])
        fcs, k2 = constants(c['fc'])
        lib = C.create_string_buffer(libbytes, len(libbytes))
        attrs = (AirAttribute * max(1, len(c['attrs'])))(*[AirAttribute(*a) for a in c['attrs']])
        ap = AirPair(1, len(c['attrs']),
                     AirFunction(C.cast(lib, C.c_void_p), len(libbytes), c['vertex'].encode(), vcs, len(c['vc']), 0),
                     AirFunction(C.cast(lib, C.c_void_p), len(libbytes), c['fragment'].encode(), fcs, len(c['fc']), 0),
                     attrs, meta, len(meta))
        rc = L.m.tegu_mali_compile_air_pair_options(C.byref(ap), vs, len(vs), fs, len(fs), None, 0, None, 0, opt,
                                                    C.byref(v), C.byref(f), pair, plan, varying, err, len(err))
    cpu = time.process_time() - t0
    row = {'rc': rc, 'cpu_s': round(cpu, 3)}
    if rc:
        row['error'] = err.value.decode(errors='replace')
    else:
        row.update(vs_sha=hashlib.sha256(C.string_at(v.code, v.size)).hexdigest()[:16], fs_sha=hashlib.sha256(C.string_at(f.code, f.size)).hexdigest()[:16],
                   vs_bytes=v.size, fs_bytes=f.size, vs_tls=v.tls, fs_tls=f.tls, plan=plan.raw[:16 + 32 * 16].hex(),
                   pair=pair.raw[:128].hex())
    return row


def child(L, mode, c, vs, fs, meta, libbytes, outdir, cid):
    errpath = outdir / f'{cid}.{mode}.stderr'
    fd = os.open(errpath, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
    os.dup2(fd, 2)
    resource.setrlimit(resource.RLIMIT_CPU, (120, 121))
    row = compile_one(L, mode, c, vs, fs, meta, libbytes)
    (outdir / f'{cid}.{mode}.json').write_text(json.dumps(row))
    os._exit(0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--run', required=True)
    ap.add_argument('--bridge', type=Path, default=R.GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so')
    ap.add_argument('--compiler', type=Path, default=OUT / 'libs/air1/libnord2_mali_runtime.so')
    ap.add_argument('--profile', type=Path, default=R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    ap.add_argument('--ids')
    ap.add_argument('--limit', type=int)
    ap.add_argument('--modes', default='m2v,air')
    a = ap.parse_args()
    os.environ['BIFROST_MESA_DEBUG'] = os.environ.get('AIRAB_BIFROST', 'shaderdb')
    run = OUT / 'runs' / a.run
    cdir = run / 'cases'
    cdir.mkdir(parents=True, exist_ok=True)
    ident = {k: {'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for k, p in
             (('bridge', a.bridge), ('compiler', a.compiler), ('profile', a.profile))}
    (run / 'identity.json').write_text(json.dumps(ident, indent=1))
    L = R.Libs(a.bridge, a.compiler)
    m = L.m
    if hasattr(m, 'tegu_mali_compile_air_pair_options'):
      m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                                                     C.c_void_p, C.c_size_t, C.c_void_p, C.POINTER(R.live.Graphics), C.POINTER(R.live.Graphics),
                                                     C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_size_t]
      m.tegu_mali_compile_air_pair_options.restype = C.c_int
    profile = R.live.profile_bytes(a.profile)
    cases = list(cases_all())
    if a.ids:
        want = set(a.ids.split(','))
        cases = [x for x in cases if x[0] in want]
    if a.limit:
        cases = cases[:a.limit]
    res = open(run / 'results.jsonl', 'a')
    libcache = {}
    for cid, case in cases:
        c = case['cap']
        row = {'id': cid, 'name': case['name']}
        try:
            t0 = time.process_time()
            vs, fs, meta = translate(L, c, profile)
            row['translate_cpu_s'] = round(time.process_time() - t0, 4)
        except RuntimeError as x:
            row['translation'] = 'refused: ' + str(x)
            res.write(json.dumps(row) + '\n'); res.flush()
            continue
        (cdir / f'{cid}.meta.json').write_bytes(meta)
        if c['library'] not in libcache:
            libcache[c['library']] = (R.CORPUS / c['library']).read_bytes()
        for mode in a.modes.split(','):
            pid = os.fork()
            if pid == 0:
                child(L, mode, c, vs, fs, meta, libcache[c['library']], cdir, cid)
            _, status = os.waitpid(pid, 0)
            jp = cdir / f'{cid}.{mode}.json'
            if jp.exists():
                r = json.loads(jp.read_text())
            else:
                tail = (cdir / f'{cid}.{mode}.stderr').read_text(errors='replace')[-400:]
                r = {'rc': -1, 'error': f'crash status={status}: ' + tail.replace('\n', ' | ')}
            r['stats'] = parse_stats((cdir / f'{cid}.{mode}.stderr').read_text(errors='replace'))
            row[mode] = r
        res.write(json.dumps(row) + '\n'); res.flush()
        print(cid, case['name'], ' '.join(f"{md}:{row[md]['rc']}" for md in a.modes.split(',')),
              (row.get('air') or {}).get('error', '')[:120], flush=True)


if __name__ == '__main__':
    main()
