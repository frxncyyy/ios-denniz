#!/usr/bin/env python3
"""Time AIR pair compiles per captured CA record: exact (function constants
folded) vs RUNTIME_SPEC stand-in (constants from push words), C6u flow
(AIR-planned modules). usage: air_compile_timing.py BRIDGE MALI [filter] [reps]"""
import ctypes as C, json, os, sys, time, collections
MODES = os.environ.get('MODES', 'exact,rspec').split(',')
from pathlib import Path
T = Path(__file__).resolve().parents[2] / 'air-plan-20261028/tools'
sys.path.insert(0, str(T))
import plan_abi as B  # noqa: E402
import standin as S  # noqa: E402
P, A, R = B.P, B.A, B.R


def main():
    bridge, mali = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    filt = sys.argv[3] if len(sys.argv) > 3 else ''
    cases = dict(A.cases_all())
    profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    B.Lb = L = R.Libs(bridge, mali)
    b, m = B.load(bridge, mali)
    L.m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p] * 17
    rows = []
    for cid, c in cases.items():
        if filt and filt not in c['name']:
            continue
        cap = c['cap']
        try:
            vs, fs, meta, keep = S.create(L, b, m, c, cap, profile, 'air')
        except Exception as ex:
            print(cid, 'create failed', ex); continue
        libbytes = (R.CORPUS / cap['library']).read_bytes()
        attrs = (A.AirAttribute * max(1, len(cap['attrs'])))(*[A.AirAttribute(*a) for a in cap['attrs']])
        vn, fn = cap['vertex'].encode(), cap['fragment'].encode()
        V = C.c_void_p
        res = {}
        for mode in MODES:
            opt = A.options_for(cap)
            if mode.startswith('rspec'):
                opt[1] |= 64
            lib = C.create_string_buffer(libbytes, len(libbytes))
            vrows, frows = (cap['vc'], cap['fc']) if not mode.endswith('noconst') else ([], [])
            vcs, k1 = A.constants(vrows)
            fcs, k2 = A.constants(frows)
            ap = A.AirPair(1, len(cap['attrs']), A.AirFunction(C.cast(lib, V), len(libbytes), vn, vcs, len(vrows), 0),
                           A.AirFunction(C.cast(lib, V), len(libbytes), fn, fcs, len(frows), 0), attrs, meta, len(meta))
            v, f = R.live.Graphics(), R.live.Graphics()
            pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
            e2 = C.create_string_buffer(1024)
            t0 = time.perf_counter()
            rc = L.m.tegu_mali_compile_air_pair_options(C.byref(ap), vs, len(vs), fs, len(fs), None, 0, None, 0, opt,
                                                        C.byref(v), C.byref(f), pair, plan, varying, e2, len(e2))
            ms = (time.perf_counter() - t0) * 1e3
            res[mode] = (rc, round(ms, 1), v.size, f.size, e2.value.decode(errors='replace')[:60] if rc else '')
        rows.append((max(r[1] for r in res.values()), cid, c['name'], res))
        print(cid, c['name'], json.dumps(res), flush=True)
    rows.sort(reverse=True)
    print('SLOWEST runtime-spec:')
    for r in rows[:15]:
        print(' ', r[1], r[2], r[3])
    for md in MODES:
        xs = [r[3][md][1] for r in rows]
        print('mode %s total_ms=%.0f max_ms=%.0f n=%d' % (md, sum(xs), max(xs), len(xs)))


if __name__ == '__main__':
    main()
