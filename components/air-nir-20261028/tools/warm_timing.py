#!/usr/bin/env python3
"""In-process compile timing (warm caches, as in a long-lived backboardd): per record, m2v translate
(Rust bridge, specialize + pipeline create), m2v Mali compile, AIR compile (air_to_nir + adapter + pan/bi).
Each compile is repeated 3x; the min CPU time is reported. Host x86, single thread."""
import hashlib, ctypes as C, json, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import air_vs_m2v as A
R = A.R
from pathlib import Path
lib = Path(sys.argv[1]); out = sys.argv[2]; ids = sys.argv[3].split(',') if len(sys.argv) > 3 else None
L = R.Libs(R.GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so', lib)
m = L.m
m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p] * 17; m.tegu_mali_compile_air_pair_options.restype = C.c_int
m.tegu_mali_graphics_free.argtypes = [C.c_void_p]
profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
libbytes = {}
res = open(out, 'w')
for cid, case in A.cases_all():
    if ids and cid not in ids:
        continue
    c = case['cap']
    t = []
    for _ in range(3):
        t0 = time.thread_time(); vs, fs, meta = A.translate(L, c, profile); t.append(time.thread_time() - t0)
    lb = libbytes.setdefault(c['library'], (R.CORPUS / c['library']).read_bytes())
    row = {'id': cid, 'name': case['name'], 'translate_ms': min(t) * 1e3}
    for mode in ('m2v', 'air'):
        best = 1e9
        for _ in range(3):
            v, f = R.live.Graphics(), R.live.Graphics()
            pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
            err = C.create_string_buffer(1024); opt = A.options_for(c)
            t0 = time.thread_time()
            if mode == 'm2v':
                rc = m.tegu_mali_compile_generic_pair_options(vs, len(vs), fs, len(fs), None, 0, None, 0, opt, C.byref(v), C.byref(f), pair, plan, varying, err, len(err))
            else:
                vcs, k1 = A.constants(c['vc']); fcs, k2 = A.constants(c['fc'])
                buf = C.create_string_buffer(lb, len(lb))
                attrs = (A.AirAttribute * max(1, len(c['attrs'])))(*[A.AirAttribute(*a) for a in c['attrs']])
                ap = A.AirPair(1, len(c['attrs']), A.AirFunction(C.cast(buf, C.c_void_p), len(lb), c['vertex'].encode(), vcs, len(c['vc']), 0),
                               A.AirFunction(C.cast(buf, C.c_void_p), len(lb), c['fragment'].encode(), fcs, len(c['fc']), 0), attrs, meta, len(meta))
                rc = m.tegu_mali_compile_air_pair_options(C.byref(ap), vs, len(vs), fs, len(fs), None, 0, None, 0, opt, C.byref(v), C.byref(f), pair, plan, varying, err, len(err))
            dt = time.thread_time() - t0
            best = min(best, dt)
            if not rc:
                import hashlib
                row[mode + '_fs_sha'] = hashlib.sha256(C.string_at(f.code, f.size)).hexdigest()[:16]
                row[mode + '_vs_sha'] = hashlib.sha256(C.string_at(v.code, v.size)).hexdigest()[:16]
                row[mode + '_plan'] = plan.raw[:16 + 32 * 16].hex()
                m.tegu_mali_graphics_free(C.byref(v)); m.tegu_mali_graphics_free(C.byref(f))
        row[mode + '_ms'] = best * 1e3; row[mode + '_rc'] = rc
    res.write(json.dumps(row) + '\n'); res.flush()
