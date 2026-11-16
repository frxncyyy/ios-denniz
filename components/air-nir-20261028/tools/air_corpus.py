#!/usr/bin/env python3
"""AIR-only coverage of the installed graphics corpus (ui-corpus-20261025 graphics-jobs.json, 1883 pairs):
the same translation inputs as shader-coverage replay.py run_graphics (m2v translation still runs, for the
planner metadata only), then tegu_mali_compile_air_pair_options with the production options. No m2v
code generation. Fork per case (crash = one row), --jobs parallel, RLIMIT_CPU per case.

usage: air_corpus.py --run NAME [--jobs 4] [--compiler lib] [--filter ids.txt]
"""
import argparse, ctypes as C, json, os, resource, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import air_vs_m2v as A  # noqa: E402
R = A.R
BLOBS, SPEC = [], {}


class RecBlob(R.live.Blob):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        BLOBS.append(self)


_spec = R.specialize


def spec(L, h, fname, consts, err):
    SPEC[fname] = consts
    return _spec(L, h, fname, consts, err)


def options(job):
    att = job.get('attachments', [])
    flags = 1 | 8 | 16
    active = [i for i, x in enumerate(att) if x.get('write_mask', 15)]
    targets = [x['format'] for x in att]
    if len(active) > 1 and len(att) <= 4 and all(targets):
        return (A.U32 * 12)(2, flags | 32, 0, 0, len(targets), *(targets + [0] * (4 - len(targets))), 0, 0, 0)
    if len(att) > 1:
        return (A.U32 * 4)(1, flags | 2, active[0] if active else 0, 0)
    return (A.U32 * 4)(1, flags, 0, 0)


def air_compile_pair(L, job, vs, fs, row, err, out=None):
    meta = C.string_at(BLOBS[-1].bytes, BLOBS[-1].length)
    keep = []

    def source(st):
        name = job[st]['name']
        if name in SPEC and SPEC[name] is not None:
            b = (R.CORPUS / job[st]['library_saved_as']).read_bytes()
            cs, k = A.constants(SPEC[name])
        else:
            b = (R.CORPUS / job[st]['air_saved_as']).read_bytes()
            cs, k = A.constants([])
        buf = C.create_string_buffer(b, len(b))
        keep.extend([buf, cs, k])
        return A.AirFunction(C.cast(buf, C.c_void_p), len(b), name.encode(), cs, len(SPEC.get(name) or []), 0)
    at = job.get('attributes', [])
    attrs = (A.AirAttribute * max(1, len(at)))(*[A.AirAttribute(a['index'], a['format'], a['offset'], a['buffer_index']) for a in at])
    ap = A.AirPair(1, len(at), source('vertex'), source('fragment'), attrs, meta, len(meta))
    v, f = R.live.Graphics(), R.live.Graphics()
    pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
    e = C.create_string_buffer(1024)
    t0 = time.process_time()
    rc = L.m.tegu_mali_compile_air_pair_options(C.byref(ap), vs, len(vs), fs, len(fs), None, 0, None, 0, options(job),
                                                C.byref(v), C.byref(f), pair, plan, varying, e, len(e))
    row.update(air_rc=rc, air_ms=round((time.process_time() - t0) * 1e3, 2))
    row['compile'] = 'compiled' if rc == 0 else 'refused'
    if rc:
        row['error'] = e.value.decode(errors='replace')
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--run', required=True)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--compiler', type=Path, default=A.OUT / 'libs/air3/libnord2_mali_runtime.so')
    ap.add_argument('--filter')
    a = ap.parse_args()
    run = A.OUT / 'runs' / a.run
    (run / 'cases').mkdir(parents=True, exist_ok=True)
    L = R.Libs(R.GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so', a.compiler)
    L.m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p] * 17
    L.m.tegu_mali_compile_air_pair_options.restype = C.c_int
    R.live.Blob = RecBlob
    R.specialize = spec
    R.compile_pair = air_compile_pair
    profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    gpu = R.GPU
    R.GPU = A.OUT / 'runs/fakegpu'  # donor constants from the air lines of corpus.txt only (current format)
    cases = R.build_cases('graphics', a.filter)
    R.GPU = gpu
    done = set()
    res = run / 'results.jsonl'
    if res.exists():
        done = {json.loads(l)['id'] for l in open(res)}
    todo = [c for c in cases if c['id'] not in done]
    print(len(cases), 'cases', len(todo), 'to run', flush=True)
    out = open(res, 'a')
    running = {}

    def finish(pid, status):
        c = running.pop(pid)
        p = run / 'cases' / (c['id'] + '.json')
        row = json.loads(p.read_text()) if p.exists() else {'translation': '?', 'compile': 'crash', 'error': f'status={status}'}
        row.update(id=c['id'], name=c['name'], cls=c['class'], library=c['library_source'], tess=bool(c.get('tess')))
        out.write(json.dumps(row) + '\n'); out.flush()
    for c in todo:
        while len(running) >= a.jobs:
            pid, st = os.wait()
            finish(pid, st)
        pid = os.fork()
        if pid == 0:
            fd = os.open(run / 'cases' / (c['id'] + '.stderr'), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
            os.dup2(fd, 2)
            resource.setrlimit(resource.RLIMIT_CPU, (60, 61))
            try:
                if c.get('tess'):
                    row = {'translation': 'not_attempted', 'compile': 'refused', 'error': 'air: tessellation (no AIR-only path)'}
                elif c['job']['pairing_evidence'] == 'captured_fixed_ui':
                    row = {'translation': 'not_attempted', 'compile': 'skipped', 'error': 'captured_fixed_ui serial replay (covered by the CA corpus run)'}
                else:
                    row = R.run_graphics(L, c, profile, run / 'cases', False)
            except Exception as x:  # noqa: BLE001
                row = {'compile': 'harness_error', 'error': repr(x)[:300]}
            (run / 'cases' / (c['id'] + '.json')).write_text(json.dumps(row))
            os._exit(0)
        running[pid] = c
    while running:
        pid, st = os.wait()
        finish(pid, st)


if __name__ == '__main__':
    main()
