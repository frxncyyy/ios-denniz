#!/usr/bin/env python3
"""Tile-fetch variants (SELECT_RT|TILE_FETCH, per active target) of the captured CA records with a
framebuffer read: m2v SPIR-V compile vs AIR compile (m2v metadata, the C6t flow, or the AIR plan).
usage: tile.py BRIDGE MALI m2v|air"""
import ctypes as C, json, sys, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import plan_abi as B  # noqa: E402
import os
MRT = os.environ.get('TILE_MRT') == '1'
import standin as S  # noqa: E402
P, A, R = B.P, B.A, B.R


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
        vs, fs, meta, keepalive = S.create(L, b, m, c, cap, profile, mode)
        md = json.loads(meta)
        if not md['graphics']['input_attachments']:
            continue
        libbytes = (R.CORPUS / cap['library']).read_bytes()
        for rt, t in enumerate(cap['targets'] or [(cap['format'], 15)]):
            if not t[1]:
                continue
            res = {}
            for path in ('m2v', 'air'):
                tg = cap['targets'] or [(cap['format'], 15)]
                if MRT and len(tg) > 1:
                    fm = [t[0] for t in tg] + [0] * (4 - len(tg))
                    opt = (A.U32 * 12)(2, 1 | 8 | 16 | 32 | 4, 0, 0, len(tg), *fm, 0, 0, 0)
                else:
                    opt = (A.U32 * 4)(1, 1 | 8 | 16 | 2 | 4, rt, 0)
                v, f = R.live.Graphics(), R.live.Graphics()
                pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
                e = C.create_string_buffer(1024)
                if path == 'm2v':
                    if mode != 'm2v':
                        continue
                    rc = L.m.tegu_mali_compile_generic_pair_options(vs, len(vs), fs, len(fs), None, 0, None, 0, opt, C.byref(v), C.byref(f),
                                                                     pair, plan, varying, e, len(e))
                else:
                    lib = C.create_string_buffer(libbytes, len(libbytes))
                    vcs, k1 = A.constants(cap['vc'])
                    fcs, k2 = A.constants(cap['fc'])
                    attrs = (A.AirAttribute * max(1, len(cap['attrs'])))(*[A.AirAttribute(*a) for a in cap['attrs']])
                    ap = A.AirPair(1, len(cap['attrs']), A.AirFunction(C.cast(lib, C.c_void_p), len(libbytes), cap['vertex'].encode(), vcs, len(cap['vc']), 0),
                                   A.AirFunction(C.cast(lib, C.c_void_p), len(libbytes), cap['fragment'].encode(), fcs, len(cap['fc']), 0), attrs, meta, len(meta))
                    rc = L.m.tegu_mali_compile_air_pair_options(C.byref(ap), vs, len(vs), fs, len(fs), None, 0, None, 0, opt,
                                                                C.byref(v), C.byref(f), pair, plan, varying, e, len(e))
                res[path] = (rc, e.value.decode(errors='replace')[:90])
            cnt[(json.dumps(res.get('m2v')), json.dumps(res['air']))] += 1
            if res['air'][0]:
                print(cid, c['name'], 'rt', rt, 'targets', cap['targets'], 'format', cap['format'], 'inputs', [x['color_index'] for x in md['graphics']['input_attachments']], 'outputs', md['graphics']['color_outputs'], file=sys.stderr)
    for k, v in cnt.most_common(): print(v, 'm2v', k[0], 'air', k[1])


if __name__ == '__main__':
    main()
