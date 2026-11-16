#!/usr/bin/env python3
"""C6t BadArgument (n2r_validate site 265) host repro: for every captured CA
record with a framebuffer read, compile what the frontend draws with after
the tile-fetch refusal (memory-fetch SELECT_RT base: DRAW_PARAMETERS|SELECT_RT|
EXTENDED_VARYINGS|THREAD_STORAGE, one job per active target) through AIR and
m2v, and apply gd_request_ok's shader-level checks (nord2_render_resources.inc)
plus the consumed-binding plan.  usage: fbfetch_validate.py BRIDGE MALI [filter]"""
import ctypes as C, json, struct, sys, collections
from pathlib import Path
T = Path(__file__).resolve().parents[2] / 'air-plan-20261028/tools'
sys.path.insert(0, str(T))
import plan_abi as B  # noqa: E402
import standin as S  # noqa: E402
P, A, R = B.P, B.A, B.R
PUSH, MAXATTR = 128, 8


def shader_checks(v, f, pair):
    (ver, flags, pos_off, pos_size, ppo, pps, sec_off, sec_size, sec_regs, gsize) = struct.unpack_from('<10I', pair, 0)
    vs_attr_mask = struct.unpack_from('<I', pair, 48)[0]
    bad = []
    if not v.size or not f.size: bad.append('no code')
    if pos_off >= sec_off: bad.append('position_offset>=secondary_offset(%d,%d)' % (pos_off, sec_off))
    if sec_off >= v.size: bad.append('secondary_offset>=vertex_bytes(%d,%d)' % (sec_off, v.size))
    if not gsize or gsize > 64: bad.append('varying_bytes=%d' % gsize)
    for name, r in (('vertex', v.registers), ('secondary', sec_regs), ('fragment', f.registers)):
        if not r or r > 64: bad.append('%s_registers=%d' % (name, r))
    for name, w in (('vertex', v.fau_words), ('fragment', f.fau_words)):
        if w > 32 + PUSH // 4: bad.append('%s_fau_words=%d' % (name, w))
    if bin(vs_attr_mask).count('1') > MAXATTR: bad.append('attributes=%d' % bin(vs_attr_mask).count('1'))
    return bad, dict(varying=gsize, sec_off=sec_off, vbytes=v.size, vregs=v.registers, fregs=f.registers, sregs=sec_regs,
                     vfau=v.fau_words, ffau=f.fau_words, attrs=hex(vs_attr_mask))


def plan_view(plan):
    ver, count, reads_fb, reserved = struct.unpack_from('<4I', plan, 0)
    out = []
    for i in range(min(count, 32)):
        bnd, kind, smask, decl, iai = struct.unpack_from('<5I', plan, 16 + 32 * i)
        out.append((bnd, kind, smask, iai))
    return reads_fb, out


def main():
    bridge, mali = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    filt = sys.argv[3] if len(sys.argv) > 3 else ''
    cases = dict(A.cases_all())
    profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
    B.Lb = L = R.Libs(bridge, mali)
    b, m = L.b, L.m
    L.m.tegu_mali_compile_air_pair_options.argtypes = [C.c_void_p] * 17
    summary = collections.Counter()
    for cid, c in cases.items():
        cap = c['cap']
        if filt and filt not in c['name']:
            continue
        vs, fs, meta, keep = S.create(L, b, m, c, cap, profile, 'm2v')
        md = json.loads(meta)
        if not md['graphics']['input_attachments']:
            continue
        libbytes = (R.CORPUS / cap['library']).read_bytes()
        tg = cap['targets'] or [(cap['format'], 15)]
        for rt, t in enumerate(tg):
            if not t[1]:
                continue
            row = {}
            for path in ('m2v', 'air'):
                opt = (A.U32 * 4)(1, 1 | 2 | 8 | 16, rt if len(tg) > 1 else 0, 0)
                if len(tg) == 1:
                    opt = (A.U32 * 4)(1, 1 | 8 | 16, 0, 0)
                v, f = R.live.Graphics(), R.live.Graphics()
                pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
                e = C.create_string_buffer(1024)
                if path == 'm2v':
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
                if rc:
                    row[path] = ('rc=%d %s' % (rc, e.value.decode(errors='replace')[:80]),)
                    continue
                bad, info = shader_checks(v, f, pair.raw)
                rfb, pl = plan_view(plan.raw)
                row[path] = (bad, info, rfb, pl)
            key = (json.dumps(row.get('m2v', [None])[0]), json.dumps(row.get('air', [None])[0]))
            summary[key] += 1
            m2, ai = row.get('m2v'), row.get('air')
            differs = m2 != ai
            if (ai and len(ai) > 1 and ai[0]) or (m2 and len(m2) > 1 and m2[0]) or differs:
                print(cid, c['name'], 'rt', rt, 'targets', tg, 'inputs', [x['color_index'] for x in md['graphics']['input_attachments']])
                for pth in ('m2v', 'air'):
                    print('   ', pth, row.get(pth))
    print('SUMMARY (m2v_refusals, air_refusals): count')
    for k, n in summary.most_common():
        print(n, k)


if __name__ == '__main__':
    main()
