#!/usr/bin/env python3
"""AIR-only compute replay of the installed kernel corpus (ui-corpus-20261025 functions.json, stage
kernel): the same m2v translation inputs as shader-coverage replay.py run_compute (m2v still runs, for
the planner metadata only), then tegu_mali_compile_compute_air (TGP1 first, then TGP2, as
runtime_pipeline.inc) with
  creation: no constants (what pipeline creation compiles), and
  region:   synthetic region constants (LocalSize 8x8x1 or 64x1x1 when 2D does not fit, MultiRegion 1,
            TgLength0 = 256, SamplerState 0) -- origin synthetic, never application values.
The m2v SPIR-V of the same translation is compiled with the same constants through
tegu_mali_compile/compute_v2 and the two packets' ABI (header words except code/registers/preload/push
count, every manifest record) are compared. Fork per case, RLIMIT_CPU, one job by default.

usage: air_compute_corpus.py --run NAME [--classes coreanimation,ui_frameworks] [--jobs 1] [--compiler lib]
"""
import argparse, ctypes as C, hashlib, json, os, resource, signal, struct, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
OUT = HERE.parent
GPU = HERE.parents[2]
sys.path.insert(0, str(GPU / 'build/shader-coverage-20261027/tools'))
import replay as R  # noqa: E402

U32 = C.c_uint32
BLOBS, SPEC = [], {}


class RecBlob(R.live.Blob):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        BLOBS.append(self)


_spec = R.specialize


def spec(L, h, fname, consts, err):
    SPEC[fname] = consts
    return _spec(L, h, fname, consts, err)


class KConst(C.Structure):
    _fields_ = [('name', C.c_char_p), ('index', U32), ('size', U32), ('bits', C.c_uint64)]


class AirCompute(C.Structure):
    _fields_ = [('version', U32), ('flags', U32), ('bitcode', C.c_void_p), ('size', C.c_size_t), ('entry', C.c_char_p),
                ('constants', C.POINTER(KConst)), ('constant_count', U32), ('reserved', U32),
                ('metadata', C.c_char_p), ('metadata_size', C.c_size_t)]


class Spec(C.Structure):
    _fields_ = [('id', U32), ('size', U32), ('data', C.c_uint8 * 8)]


def constants(rows):
    arr = (KConst * max(1, len(rows)))()
    keep = []
    for i, c in enumerate(rows):
        raw = bytes.fromhex(c['value_hex'])
        nm = c['name'].encode() if c.get('kind', 1 if c.get('selector', 'name') == 'name' else 0) == 1 and c['name'] else None
        keep.append(nm)
        arr[i].name = nm
        arr[i].index = c['index']
        arr[i].size = len(raw)
        arr[i].bits = int.from_bytes(raw[:8].ljust(8, b'\0'), 'little')
    return arr, keep


def region_constants(meta):
    """synthetic region values keyed by the metadata's ids"""
    out = []
    local = [8, 8, 1]
    for c in meta['specialization']['constants']:
        k = c['key']
        name = k if isinstance(k, str) else next(iter(k))
        v = c['default']
        if name == 'LocalSize':
            v = local['XYZ'.index(k['LocalSize'])]
        elif name == 'FlatLocalSize':
            v = local[0] * local[1] * local[2]
        elif name == 'MultiRegion':
            v = 1
        elif name == 'TgLength':
            v = 256 if k['TgLength'] == 0 else 0
        out.append((c['id'], c['size'], v))
    arr = (Spec * max(1, len(out)))()
    for i, (cid, size, v) in enumerate(out):
        arr[i].id = cid
        arr[i].size = size
        for b in range(size):
            arr[i].data[b] = (v >> (8 * b)) & 0xff
    return arr, len(out)


def abi(pkg):
    """the runtime-visible ABI of a TGP1/TGP2 packet"""
    w = struct.unpack_from('<32I', pkg.ljust(128, b'\0'))
    if w[1] == 1 and w[0] != 0:
        # TGP1: 16 words; 8 bindings, 9..11 local, 12 tls, 13 wls
        return {'v': 1, 'bindings': w[8], 'local': w[9:12], 'tls': w[12], 'wls': w[13]}
    man = []
    for i in range(w[13]):
        r = struct.unpack_from('<16I', pkg, 128 + 64 * i)
        man.append(r[:9] + (r[12],))
    return {'v': 2, 'bindings': w[8], 'local': w[9:12], 'flags': w[16], 'tls': w[17], 'wls': w[18], 'bufmask': w[19],
            'manifest': sorted(man)}


def compile_pair(L, meta_bytes, spv, src, consts, kc, kn, spec_arr, spec_n, row, tag):
    err = C.create_string_buffer(2048)
    # m2v path
    pk = R.MBlob()
    m2v_rc, entry = 1, None
    if os.environ.get('AIR_ONLY'):
        m2v_rc, entry = -1, 'skipped'
    elif spec_n == 0 and row.get('dispatchable', 1):
        m2v_rc = L.m.tegu_mali_compile(spv, len(spv), C.byref(pk), err, len(err))
        entry = 'v1'
    if m2v_rc > 0:
        pk = R.MBlob()
        if spec_n:
            m2v_rc = L.m.tegu_mali_compile_compute_v2(spv, len(spv), C.byref(spec_arr), spec_n, C.byref(pk), err, len(err))
        else:
            m2v_rc = L.m.tegu_mali_compile_compute_v2(spv, len(spv), None, 0, C.byref(pk), err, len(err))
        entry = 'v2'
    row[tag + '_m2v'] = 'ok' if not m2v_rc else 'skipped' if m2v_rc < 0 else 'refused'
    m2v_pkg = C.string_at(pk.data, pk.size) if not m2v_rc else None
    if m2v_rc > 0:
        row[tag + '_m2v_error'] = err.value.decode(errors='replace')[:300]
    # AIR path, same order: TGP1 then TGP2 (creation), TGP2 only (region, as tegu_v2_variant)
    rc = 1
    air_pkg = None
    t0 = time.process_time()
    for flags in ((0, 1) if spec_n == 0 else (1,)):
        pa = R.MBlob()
        err.value = b''
        req = AirCompute(1, flags, C.cast(src, C.c_void_p), len(src.raw), kn, kc, len(consts) if consts else 0, 0, meta_bytes, len(meta_bytes))
        rc = L.m.tegu_mali_compile_compute_air(C.byref(req), C.byref(spec_arr) if spec_n else None, spec_n, C.byref(pa), err, len(err))
        if not rc:
            air_pkg = C.string_at(pa.data, pa.size)
            row[tag + '_air_entry'] = 'v%d' % (flags + 1)
            break
        row[tag + '_air_v%d_error' % (flags + 1)] = err.value.decode(errors='replace')[:300]
    row[tag + '_air_ms'] = round((time.process_time() - t0) * 1e3, 1)
    row[tag + '_air'] = 'ok' if not rc else 'refused'
    if not rc:
        row[tag + '_air_error'] = None
        row[tag + '_air_bytes'] = len(air_pkg)
    else:
        row[tag + '_air_error'] = err.value.decode(errors='replace')[:300]
    if air_pkg and m2v_pkg:
        a, b = abi(air_pkg), abi(m2v_pkg)
        row[tag + '_abi'] = 'same' if a == b else 'differs'
        if a != b:
            row[tag + '_abi_air'] = a
            row[tag + '_abi_m2v'] = b
        row[tag + '_m2v_entry'] = entry
        row[tag + '_code_air'] = len(air_pkg)
        row[tag + '_code_m2v'] = len(m2v_pkg)


def run_case(L, case, profile, out):
    row = R.run_compute(L, case, profile, out, False)
    if row.get('translation') != 'translated':
        row['air'] = 'no_m2v_translation'
        return row
    spv_blob, meta_blob = BLOBS[-2], BLOBS[-1]
    spv = C.string_at(spv_blob.bytes, spv_blob.length)
    meta_bytes = C.string_at(meta_blob.bytes, meta_blob.length)
    meta = json.loads(meta_bytes)
    e = case['entry']
    name = e['name']
    if name in SPEC and SPEC[name]:
        raw = (R.CORPUS / e['library_saved_as']).read_bytes()
        kc, keep = constants(SPEC[name])
        consts = SPEC[name]
    else:
        raw = (R.CORPUS / e['air_saved_as']).read_bytes()
        kc, keep = constants([])
        consts = []
    src = C.create_string_buffer(raw, len(raw))
    kn = name.encode()
    row['m2v_meta_sha'] = hashlib.sha256(meta_bytes).hexdigest()[:16]
    for k in ('tile_origin', 'stage_input_origin', 'linked_origin'):
        if k in row:
            row['air_input'] = k
    compile_pair(L, meta_bytes, spv, src, consts, kc, kn, None, 0, row, 'creation')
    arr, n = region_constants(meta)
    if n:
        compile_pair(L, meta_bytes, spv, src, consts, kc, kn, arr, n, row, 'region')
    return row


def compute_cases():
    """replay.py build_cases('compute') without the graphics donor scan (corpus.txt now also holds mali records)"""
    resources = {}
    for line in open(R.CORPUS / 'resources.jsonl'):
        r = json.loads(line)
        resources[r['air_sha256']] = r
    funcs = json.load(open(R.CORPUS / 'functions.json'))['entries']
    for e in funcs:
        if e['stage'] == 'visible':
            R.VISIBLE.setdefault(e['name'], e['library_saved_as'])
    fcc = {}
    if R.FC_PLAN.exists():
        for line in open(R.FC_PLAN):
            v = json.loads(line)
            if v['kind'] == 'compute_fc':
                fcc[v['kernel']['sha256']] = v['initial_constants'].get('kernel') or []
    seen = {}
    for e in funcs:
        if e['stage'] == 'kernel' and e['air_sha256'] not in seen:
            seen[e['air_sha256']] = e
    cases = []
    for e in seen.values():
        cases.append({'id': 'c-' + e['air_sha256'][:24], 'kind': 'compute', 'entry': e, 'name': e['name'], 'library_source': e['library_source'],
                      'provenance': 'installed_air_default_constants_none_supplied', 'constants_declared': len(e.get('constants') or []),
                      'fc': fcc.get(e['air_sha256']), 'stage_in': R.stage_in_attrs(resources.get(e['air_sha256'], {}))})
    order = ['coreanimation', 'ui_frameworks', 'camera_video', 'mps_ml', '3d_maps_ar', 'other']
    for c in cases:
        c['class'] = R.process_class(c['library_source'])
    cases.sort(key=lambda c: (order.index(c['class']), c['library_source'], c['id']))
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--run', required=True)
    ap.add_argument('--jobs', type=int, default=1)
    ap.add_argument('--classes', default='coreanimation,ui_frameworks')
    ap.add_argument('--bridge', type=Path, default=GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so')
    ap.add_argument('--compiler', type=Path, default=OUT / 'libs/c1/libnord2_mali_runtime.so')
    ap.add_argument('--profile', type=Path, default=GPU / 'build/tegu-frontend/tegu-air-profile.h')
    ap.add_argument('--cpu', type=int, default=60)
    ap.add_argument('--limit', type=int)
    ap.add_argument('--ids')
    a = ap.parse_args()
    run = OUT / 'runs' / a.run
    (run / 'cases').mkdir(parents=True, exist_ok=True)
    ident = {k: {'path': str(p), 'sha256': R.sha(p)} for k, p in (('bridge', a.bridge), ('compiler', a.compiler), ('profile', a.profile))}
    (run / 'identity.json').write_text(json.dumps(ident, indent=1))
    R.live.Blob = RecBlob
    R.specialize = spec
    L = R.Libs(a.bridge, a.compiler)
    m = L.m
    m.tegu_mali_compile_compute_air.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(R.MBlob), C.c_void_p, C.c_size_t]
    m.tegu_mali_compile_compute_air.restype = C.c_int
    m.tegu_mali_compile_compute_v2.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(R.MBlob), C.c_void_p, C.c_size_t]
    profile = R.live.profile_bytes(a.profile)
    classes = set(a.classes.split(','))
    cases = [c for c in compute_cases() if c['class'] in classes]
    if a.ids:
        ids = set(x.strip() for x in open(a.ids) if x.strip())
        cases = [c for c in cases if c['id'] in ids]
    if a.limit:
        cases = cases[:a.limit]
    res = run / 'results.jsonl'
    done = set()
    if res.exists():
        done = {json.loads(l)['id'] for l in open(res)}
    todo = [c for c in cases if c['id'] not in done]
    print(f'{len(cases)} cases, {len(todo)} to run', flush=True)
    stream = open(res, 'a')
    running = {}
    it = iter(todo)

    def launch(c):
        rfd, wfd = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(rfd)
            resource.setrlimit(resource.RLIMIT_CPU, (a.cpu, a.cpu + 5))
            ef = os.open(str(run / 'cases' / (c['id'] + '.stderr')), os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
            os.dup2(ef, 2)
            try:
                R.prefetch(L, c)
                row = run_case(L, c, profile, run / 'cases')
            except Exception as x:  # noqa: BLE001
                row = {'error': 'python: ' + repr(x)[:300]}
            os.write(wfd, json.dumps(row).encode())
            os._exit(0)
        os.close(wfd)
        running[pid] = (c, rfd, time.time())

    def finish(pid, status):
        c, rfd, t0 = running.pop(pid)
        data = b''
        while True:
            chunk = os.read(rfd, 65536)
            if not chunk:
                break
            data += chunk
        os.close(rfd)
        try:
            row = json.loads(data) if data else {}
        except ValueError:
            row = {}
        if not data:
            log = (run / 'cases' / (c['id'] + '.stderr')).read_text(errors='replace')[-2000:]
            row = {'crash': 'status=%d' % status, 'crash_log': R.classify_crash(log)}
        base = {'id': c['id'], 'name': c['name'], 'class': c['class'], 'library_source': c['library_source'],
                'provenance': c['provenance'], 'wall_s': round(time.time() - t0, 2)}
        base.update(row)
        stream.write(json.dumps(base) + '\n')
        stream.flush()

    for c in it:
        while len(running) >= a.jobs:
            pid, status = os.wait()
            finish(pid, status)
        launch(c)
    while running:
        pid, status = os.wait()
        finish(pid, status)
    print('done', flush=True)


if __name__ == '__main__':
    main()
