#!/usr/bin/env python3
"""AIR-derived render plan versus m2v metadata over the installed graphics corpus
(ui-corpus-20261025 graphics-jobs.json; same translation inputs as shader-coverage replay.py
run_graphics). The m2v translation runs here ONLY as the oracle. Fork per case.

usage: plan_corpus.py --run NAME --lib LIB [--classes coreanimation,ui_frameworks] [--jobs 1]
rows: runs/NAME/results.jsonl (status identical | differs | air_refused | m2v_refused | crash)
"""
import argparse, ctypes as C, json, os, resource, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
OUT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'air-nir-20261028/tools'))
import air_corpus as X  # noqa: E402
import plan_vs_m2v as P  # noqa: E402
R, A = X.R, X.A
U32 = C.c_uint32
STATE = {}


def plan_pair(L, job, vs, fs, row, err, out=None):
    meta = json.loads(C.string_at(X.BLOBS[-1].bytes, X.BLOBS[-1].length))
    keep = []

    def source(st):
        name = job[st]['name']
        if name in X.SPEC and X.SPEC[name] is not None:
            b = (R.CORPUS / job[st]['library_saved_as']).read_bytes()
            cs, k = A.constants(X.SPEC[name])
            n = len(X.SPEC[name])
        else:
            b = (R.CORPUS / job[st]['air_saved_as']).read_bytes()
            cs, k = A.constants([])
            n = 0
        buf = C.create_string_buffer(b, len(b))
        keep.extend([buf, cs, k, name.encode()])
        return A.AirFunction(C.cast(buf, C.c_void_p), len(b), keep[-1], cs, n, 0)
    at, ly = job.get('attributes', []), job.get('layouts', [])
    atts = job.get('attachments')
    attrs = (A.AirAttribute * max(1, len(at)))(*[A.AirAttribute(a['index'], a['format'], a['offset'], a['buffer_index']) for a in at])
    lays = (P.Layout * max(1, len(ly)))(*[P.Layout(a['index'], a['stride'], a['step_function'], a['step_rate']) for a in ly])
    tgs = (P.Target * max(1, len(atts or [])))(*[P.Target(x['format'], x['write_mask']) for x in (atts or [])])
    keep.extend([attrs, lays, tgs])
    d = P.Desc(1, len(at), len(ly), len(atts or []), source('vertex'), source('fragment'), attrs, lays,
               tgs if atts else None, None, 0 if atts else job['format'], 0)
    o, n, facts = C.c_void_p(), C.c_size_t(), U32()
    e = C.create_string_buffer(512)
    t0 = time.process_time()
    rc = STATE['m'].tegu_mali_air_render_metadata(C.byref(d), C.byref(o), C.byref(n), C.byref(facts), e, len(e))
    row['plan_ms'] = round((time.process_time() - t0) * 1e3, 2)
    if rc:
        row.update(status='air_refused', error=e.value.decode(errors='replace'))
        row['compile'] = 'refused'
        return row
    air = json.loads(C.string_at(o, n.value))
    STATE['m'].tegu_mali_air_metadata_free(o)
    diffs = P.compare(meta, air)
    if diffs and out is not None:
        (out / 'm2v.meta.json').write_text(json.dumps(meta))
        (out / 'air.meta.json').write_text(json.dumps(air))
    elif diffs and STATE.get('dump'):
        Path(STATE['dump'], job['id'] + '.m2v.json').write_text(json.dumps(meta))
        Path(STATE['dump'], job['id'] + '.air.json').write_text(json.dumps(air))
    row.update(status='identical' if not diffs else 'differs', diffs=diffs[:20])
    row['compile'] = 'compiled'
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--run', required=True)
    ap.add_argument('--lib', required=True, type=Path)
    ap.add_argument('--classes', default='coreanimation,ui_frameworks')
    ap.add_argument('--jobs', type=int, default=1)
    ap.add_argument('--filter')
    a = ap.parse_args()
    run = OUT / 'runs' / a.run
    (run / 'cases').mkdir(parents=True, exist_ok=True)
    L = R.Libs(OUT / 'libs/libtegu_air_bridge.so', a.lib)
    STATE['m'] = P.load(a.lib.resolve())
    STATE['dump'] = str(run / 'metas')
    (run / 'metas').mkdir(exist_ok=True)
    R.live.Blob = X.RecBlob
    R.specialize = X.spec
    R.compile_pair = plan_pair
    profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    gpu = R.GPU
    R.GPU = A.OUT / 'runs/fakegpu'
    cases = R.build_cases('graphics', a.filter)
    R.GPU = gpu
    classes = set(a.classes.split(',')) if a.classes != 'all' else None
    cases = [c for c in cases if (classes is None or c['class'] in classes) and not c.get('tess')
             and c['job']['pairing_evidence'] != 'captured_fixed_ui']
    res = run / 'results.jsonl'
    done = {json.loads(l)['id'] for l in open(res)} if res.exists() else set()
    todo = [c for c in cases if c['id'] not in done]
    print(len(cases), 'cases', len(todo), 'to run', flush=True)
    out = open(res, 'a')
    running = {}

    def finish(pid, status):
        c = running.pop(pid)
        p = run / 'cases' / (c['id'] + '.json')
        row = json.loads(p.read_text()) if p.exists() else {'status': 'crash', 'error': f'status={status}'}
        if row.get('translation') == 'refused':
            row['status'] = 'm2v_refused'
        row.update(id=c['id'], name=c['name'], cls=c['class'], library=c['library_source'])
        out.write(json.dumps(row) + '\n')
        out.flush()
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
                row = R.run_graphics(L, c, profile, run / 'cases', False)
            except Exception as x:  # noqa: BLE001
                row = {'status': 'harness_error', 'error': repr(x)[:300]}
            (run / 'cases' / (c['id'] + '.json')).write_text(json.dumps(row))
            os._exit(0)
        running[pid] = c
    while running:
        pid, st = os.wait()
        finish(pid, st)
    rows = [json.loads(l) for l in open(res)]
    import collections
    cnt = collections.Counter((r['cls'], r.get('status')) for r in rows)
    for k, v in sorted(cnt.items()):
        print(v, k)


if __name__ == '__main__':
    main()
