#!/usr/bin/env python3
"""Host test for gpu/metal/tegu/tessellator.h (drawPatches CPU tessellator).

The C tessellator is built as a shared object with ASan+UBSan and driven with
the real factor-buffer decoder. The checks do not reuse its algorithm:

  * axis oracle: segment counts per partition mode and, for fractional modes,
    point placement rebuilt as list insertion (the lower lattice with its
    centre-most points duplicated, interpolated against the upper lattice);
  * mesh invariants: no gaps/overlaps (|area| sums to the domain area), one
    orientation, no zero-area triangles for uniform cases, a closed manifold
    (interior edges in exactly two triangles, boundary edges on the domain
    boundary), unique vertices, every vertex referenced;
  * outer edges carry exactly the oracle's points, so two patches sharing an
    edge factor share their edge vertices (watertightness across patches);
  * continuity: just above an odd/even segment boundary the point set tends to
    the previous lattice (no popping);
  * quad inside lattice: with uniform integer factors the interior vertices
    are exactly {(i/n,j/n)};
  * culling (factor <= 0, NaN), scale/clamp, winding, half-factor decoding,
    capacity-probe (zero-capacity first call sizes the buffers exactly).
Hardware vertex ORDER and diagonal choice are not claimed (see the header)."""
import ctypes
import math
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

if os.environ.get('TESS_SAN', '1') == '1' and 'TESS_REEXEC' not in os.environ:
    # The sanitizer runtime must come first in the process: re-exec with it preloaded.
    asan = subprocess.run(['cc', '-print-file-name=libasan.so'], capture_output=True, text=True).stdout.strip()
    env = dict(os.environ, TESS_REEXEC='1', LD_PRELOAD=asan, ASAN_OPTIONS='detect_leaks=0:abort_on_error=0')
    os.execve(sys.executable, [sys.executable] + sys.argv, env)
G = Path(__file__).resolve().parents[1]
OUT = G / 'build/api-surface-20261027/tess'
OUT.mkdir(parents=True, exist_ok=True)
WRAP = OUT / 'tess_wrap.c'
WRAP.write_text(r'''
#include "../../../metal/tegu/tessellator.h"
int run(unsigned domain, unsigned partition, unsigned winding, float maxf, const float *edge, const float *inside,
        float *vert, unsigned vcap, unsigned *idx, unsigned icap, unsigned *nv, unsigned *ni, unsigned base) {
    TessConfig c = {domain, partition, winding, maxf};
    TessVertex *tv = vcap ? (TessVertex *)__builtin_alloca(sizeof(TessVertex) * vcap) : 0;
    TessOut o = {tv, idx, vcap, icap, 0, 0, base, 0};
    int r = tess_patch(&c, edge, inside, &o);
    for (unsigned i = 0; i < o.vertex_count && i < vcap; i++) { vert[3*i] = tv[i].u; vert[3*i+1] = tv[i].v; vert[3*i+2] = tv[i].w; }
    *nv = o.vertex_count; *ni = o.index_count; return r;
}
int axis(float f, unsigned partition, float maxf, float *s) {
    TessAxis a; int r = tess_axis(f, partition, maxf, &a); if (r) return -1;
    for (int i = 0; i <= a.n; i++) { s[i] = a.s[i]; }
    return a.n;
}
float half_to_float(unsigned short h) { return tess_half_to_float(h); }
void read_factors(unsigned domain, const unsigned char *p, float scale, float *edge, float *inside) { tess_read_factors(domain, p, scale, edge, inside); }
''')
SO = OUT / 'libtess.so'
SAN = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined'] if os.environ.get('TESS_SAN', '1') == '1' else []
subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror'] + SAN +
               ['-shared', '-fPIC', str(WRAP), '-o', str(SO)], check=True)
lib = ctypes.CDLL(str(SO))
F = ctypes.c_float
FP = ctypes.POINTER(F)
UP = ctypes.POINTER(ctypes.c_uint)
lib.run.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, F, FP, FP, FP, ctypes.c_uint, UP, ctypes.c_uint, UP, UP, ctypes.c_uint]
lib.axis.argtypes = [F, ctypes.c_uint, F, FP]
lib.half_to_float.argtypes = [ctypes.c_ushort]
lib.half_to_float.restype = F
lib.read_factors.argtypes = [ctypes.c_uint, ctypes.c_char_p, F, FP, FP]
POW2, INTEGER, ODD, EVEN = 0, 1, 2, 3
TRI, QUAD = 0, 1
CW, CCW = 0, 1
failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)
        if len(failures) < 40:
            print('FAIL', msg)


def run(domain, part, winding, edge, inside, maxf=64.0, base=0):
    e = (F * 4)(*(list(edge) + [1.0] * (4 - len(edge))))
    i = (F * 2)(*(list(inside) + [1.0] * (2 - len(inside))))
    nv, ni = ctypes.c_uint(), ctypes.c_uint()
    r = lib.run(domain, part, winding, maxf, e, i, None, 0, None, 0, ctypes.byref(nv), ctypes.byref(ni), base)
    cnt = (nv.value, ni.value)
    verts = (F * max(1, 3 * nv.value))()
    idx = (ctypes.c_uint * max(1, ni.value))()
    nv2, ni2 = ctypes.c_uint(), ctypes.c_uint()
    r2 = lib.run(domain, part, winding, maxf, e, i, verts, nv.value, idx, ni.value, ctypes.byref(nv2), ctypes.byref(ni2), base)
    check(r == r2 and cnt == (nv2.value, ni2.value), 'capacity probe disagrees with real run %s' % (cnt,))
    if r:
        return r, None, None
    v = [(verts[3*k], verts[3*k+1], verts[3*k+2]) for k in range(nv.value)]
    t = [(idx[3*k] - base, idx[3*k+1] - base, idx[3*k+2] - base) for k in range(ni.value // 3)]
    return 0, v, t


# ---------------- independent axis oracle -----------------
def oracle_axis(f, part, maxf=64.0):
    if not f > 0:
        return None
    hi = min(maxf, 64.0)
    lo = 1.0
    if part == ODD:
        hi = min(hi, 63.0)
    if part == EVEN:
        lo = 2.0
    hi = max(hi, lo)
    f = min(max(f, lo), hi)
    n = math.ceil(f - 1e-12) if f != int(f) else int(f)
    if part == POW2:
        n = 1 << (n - 1).bit_length() if n > 1 else 1
    elif part == ODD and n % 2 == 0:
        n += 1
    elif part == EVEN and n % 2 == 1:
        n += 1
    upper = [j / n for j in range(n + 1)]
    frac = part in (ODD, EVEN)
    lower_n = n - 2
    if not frac or lower_n < 1:
        return n, upper
    t = min(1.0, max(0.0, (f - lower_n) / 2.0))
    lower = [i / lower_n for i in range(lower_n + 1)]
    if part == EVEN:
        mid = lower_n // 2
        ext = lower[:mid + 1] + [lower[mid]] * 2 + lower[mid + 1:]
    else:
        a = (lower_n - 1) // 2
        ext = lower[:a + 1] + [lower[a]] + [lower[a + 1]] + lower[a + 1:]
    assert len(ext) == n + 1, (f, part, n, len(ext))
    return n, [e * (1 - t) + u * t for e, u in zip(ext, upper)]


def c_axis(f, part, maxf=64.0):
    s = (F * 65)()
    n = lib.axis(f, part, maxf, s)
    return None if n < 0 else (n, [s[i] for i in range(n + 1)])


# ---------------- mesh checks -----------------
def area2(v, t):
    a, b, c = (v[x] for x in t)
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def key(p):
    return (round(p[0] * 1e5), round(p[1] * 1e5), round(p[2] * 1e5))


def mesh_checks(tag, domain, part, winding, edge, inside, v, t, uniform, maxf=64.0):
    # indices in range, vertices used and unique
    check(all(0 <= i < len(v) for tri in t for i in tri), tag + ' index out of range')
    used = set(i for tri in t for i in tri)
    check(len(used) == len(v), tag + ' unreferenced vertices %d/%d' % (len(used), len(v)))
    check(len({key(p) for p in v}) == len(v), tag + ' duplicate vertex positions')
    # domain membership
    for p in v:
        if domain == QUAD:
            ok = -1e-6 <= p[0] <= 1 + 1e-6 and -1e-6 <= p[1] <= 1 + 1e-6 and abs(p[2]) < 1e-6
        else:
            ok = min(p) >= -1e-6 and abs(p[0] + p[1] + p[2] - 1) < 2e-5
        check(ok, tag + ' vertex outside domain %s' % (p,))
    # orientation and area
    total = 0.0
    sign = 1 if winding == CCW else -1
    for tri in t:
        a = area2(v, tri)
        total += abs(a) / 2
        if uniform:
            check(a * sign > 1e-9, tag + ' zero/inverted tri %s a=%g' % (tri, a))
        else:
            check(a * sign > -1e-7, tag + ' inverted tri %s a=%g' % (tri, a))
    want = 1.0 if domain == QUAD else 0.5
    check(abs(total - want) < 2e-4, tag + ' area %.6f != %.6f' % (total, want))
    # manifold
    cnt = {}
    for tri in t:
        for k in range(3):
            e = tuple(sorted((tri[k], tri[(k + 1) % 3])))
            cnt[e] = cnt.get(e, 0) + 1
    check(all(c <= 2 for c in cnt.values()), tag + ' edge in >2 triangles')

    def on_boundary(p):
        if domain == QUAD:
            return min(p[0], p[1], 1 - p[0], 1 - p[1]) < 1e-6
        return min(p) < 1e-6

    for e, c in cnt.items():
        if c == 1:
            check(on_boundary(v[e[0]]) and on_boundary(v[e[1]]), tag + ' open interior edge %s' % (e,))
    # outer edge points equal the oracle's
    if domain == QUAD:
        sides = [(edge[0], lambda p: abs(p[0]) < 1e-6, 1), (edge[1], lambda p: abs(p[1]) < 1e-6, 0),
                 (edge[2], lambda p: abs(p[0] - 1) < 1e-6, 1), (edge[3], lambda p: abs(p[1] - 1) < 1e-6, 0)]
        for f, pred, coord in sides:
            ox = oracle_axis(f, part, maxf)
            got = sorted(p[coord] for p in v if pred(p))
            want_pts = ox[1]
            check(len(got) == len(want_pts) and all(abs(a - b) < 2e-5 for a, b in zip(got, sorted(want_pts))),
                  tag + ' edge points %s != %s' % ([round(g, 4) for g in got], [round(w, 4) for w in want_pts]))
    else:
        sides = [(edge[0], lambda p: abs(p[0]) < 1e-6, 1), (edge[1], lambda p: abs(p[1]) < 1e-6, 0),
                 (edge[2], lambda p: abs(p[2]) < 1e-6, 0)]
        for f, pred, coord in sides:
            ox = oracle_axis(f, part, maxf)
            got = sorted(p[coord] for p in v if pred(p))
            want_pts = sorted(ox[1])
            # along the u=0 side the free coordinate v runs 0..1; same for the others
            check(len(got) == len(want_pts) and all(abs(a - b) < 2e-5 for a, b in zip(got, want_pts)),
                  tag + ' tri edge points %s != %s' % ([round(g, 4) for g in got], [round(w, 4) for w in want_pts]))


def case(domain, part, winding, edge, inside, uniform=True, maxf=64.0, tag=None):
    tag = tag or 'd%d p%d w%d e%s i%s' % (domain, part, winding, edge, inside)
    r, v, t = run(domain, part, winding, edge, inside, maxf, base=7)
    if r:
        check(False, tag + ' unexpectedly culled/failed %d' % r)
        return None
    mesh_checks(tag, domain, part, winding, edge, inside, v, t, uniform, maxf)
    return v, t


# ---------------- axis tests -----------------
def test_axis():
    factors = [0.5, 1, 1.01, 1.5, 2, 2.01, 2.5, 3, 3.3, 3.999, 4, 5, 6.5, 7, 8, 9.1, 15.9, 16, 17, 33, 62.5, 63, 63.5, 64, 70]
    for part in range(4):
        for f in factors:
            for maxf in (64.0, 16.0, 5.0):
                o = oracle_axis(f, part, maxf)
                c = c_axis(f, part, maxf)
                check(o is not None and c is not None, 'axis %s %s culled' % (part, f))
                if not (o and c):
                    continue
                check(o[0] == c[0], 'axis segments part=%d f=%g max=%g: oracle %d c %d' % (part, f, maxf, o[0], c[0]))
                if o[0] == c[0]:
                    check(all(abs(a - b) < 1e-5 for a, b in zip(o[1], c[1])),
                          'axis placement part=%d f=%g max=%g' % (part, f, maxf))
                    s = c[1]
                    check(abs(s[0]) == 0 and abs(s[-1] - 1) == 0, 'axis ends')
                    check(all(s[i] <= s[i + 1] + 1e-7 for i in range(len(s) - 1)), 'axis monotone part=%d f=%g' % (part, f))
                    check(all(abs(s[i] + s[len(s) - 1 - i] - 1) < 1e-6 for i in range(len(s))), 'axis symmetric part=%d f=%g' % (part, f))
    for f in (0.0, -1.0, float('nan'), float('-inf')):
        for part in range(4):
            check(c_axis(f, part) is None, 'axis f=%s must cull' % f)
    # known segment counts
    known = {(POW2, 5): 8, (POW2, 8): 8, (POW2, 8.5): 16, (INTEGER, 5.2): 6, (ODD, 1.0): 1, (ODD, 1.2): 3, (ODD, 4.0): 5,
             (EVEN, 1.0): 2, (EVEN, 2.0): 2, (EVEN, 2.1): 4, (EVEN, 5.0): 6, (INTEGER, 100): 64, (ODD, 100): 63, (INTEGER, 0.2): 1}
    for (part, f), n in known.items():
        check(c_axis(f, part)[0] == n, 'known count part=%d f=%g' % (part, f))
    # integer factors are exactly uniform in every mode
    for part in (POW2, INTEGER, EVEN):
        for n in (2, 4, 8, 16, 64):
            s = c_axis(float(n), part)[1]
            check(all(abs(s[i] - i / n) < 1e-6 for i in range(n + 1)), 'uniform part=%d n=%d' % (part, n))
    for part in (POW2, INTEGER, EVEN):
        s = c_axis(2.0, part)[1]
        check(abs(s[1] - 0.5) < 1e-7, 'uniform n=2')
    for n in (1, 3, 5, 9, 63):
        s = c_axis(float(n), ODD)[1]
        check(all(abs(s[i] - i / n) < 1e-6 for i in range(n + 1)), 'uniform odd n=%d' % n)
    # continuity: just above the previous odd/even factor the point set is the previous lattice
    for part, base in ((ODD, [1, 3, 5, 9, 31]), (EVEN, [2, 4, 8, 16, 62])):
        for l in base:
            o = c_axis(l + 0.001, part)
            prev = c_axis(float(l), part)
            pts = sorted(set(round(x, 3) for x in o[1]))
            ref = sorted(set(round(x, 3) for x in prev[1]))
            check(len(pts) == len(ref) and all(abs(a - b) < 3e-3 for a, b in zip(pts, ref)),
                  'continuity part=%d at %d: %s vs %s' % (part, l, pts[:6], ref[:6]))
    # within a band the born pair separates monotonically: points up to the split index move
    # toward their edge, the points past it toward the centre-side neighbour's growth (outward from centre)
    for part, lo in ((ODD, 3), (EVEN, 4)):
        prev = None
        for k in range(1, 20):
            f = lo + 2 * k / 20
            n, s = c_axis(f, part)
            split = n // 2 - 1 if part == EVEN else (n - 3) // 2
            if prev:
                for j in range(1, n // 2 + 1):
                    if part == EVEN and j == n // 2:
                        continue
                    d = s[j] - prev[j]
                    check((d <= 1e-6) if j <= split else (d >= -1e-6),
                          'band movement not monotone part=%d f=%g j=%d d=%g' % (part, f, j, d))
            prev = s


# ---------------- mesh tests -----------------
def test_meshes():
    # trivial cases
    r = case(QUAD, INTEGER, CCW, [1, 1, 1, 1], [1, 1])
    check(r and len(r[1]) == 2 and len(r[0]) == 4, 'quad factor 1 must be two triangles')
    r = case(TRI, INTEGER, CCW, [1, 1, 1], [1])
    check(r and len(r[1]) == 1 and len(r[0]) == 3, 'triangle factor 1 must be one triangle')
    # integer uniform sweeps
    for part in (INTEGER, POW2):
        for n in (1, 2, 3, 4, 5, 8):
            for domain in (QUAD, TRI):
                case(domain, part, CCW, [n] * 4, [n, n])
    # quad inside lattice equals {(i/n,j/n)} for uniform integer factors
    for n in (2, 3, 4, 5, 6, 8, 9):
        r = case(QUAD, INTEGER, CCW, [n] * 4, [n, n])
        inner = {key(p) for p in r[0] if 1e-6 < p[0] < 1 - 1e-6 and 1e-6 < p[1] < 1 - 1e-6}
        want = {key((i / n, j / n, 0)) for i in range(1, n) for j in range(1, n)}
        check(inner == want, 'quad n=%d interior lattice differs (%d vs %d)' % (n, len(inner), len(want)))
        # mismatched inside factors keep the outer ring
        case(QUAD, INTEGER, CCW, [n] * 4, [n, max(1, n - 1)])
    # unequal edges / unequal inside, all partitions
    random.seed(0x7e55)
    fac = [1, 1.2, 1.5, 2, 2.4, 3, 3.5, 4, 4.7, 5, 6, 7.3, 8, 9, 12.5, 16]
    for part in range(4):
        for k in range(120):
            qe = [random.choice(fac) for _ in range(4)]
            qi = [random.choice(fac) for _ in range(2)]
            case(QUAD, part, random.choice((CW, CCW)), qe, qi, uniform=False, maxf=16.0)
            te = [random.choice(fac) for _ in range(3)]
            ti = [random.choice(fac)]
            case(TRI, part, random.choice((CW, CCW)), te, ti, uniform=False, maxf=16.0)
    # extremes
    for domain in (QUAD, TRI):
        for part in range(4):
            case(domain, part, CCW, [64] * 4, [64, 64], uniform=False)
            case(domain, part, CCW, [1, 64, 1, 64], [2, 63], uniform=False)
            case(domain, part, CCW, [64, 1, 1, 1], [1, 1], uniform=False)
            case(domain, part, CCW, [1, 1, 1, 1], [64, 64], uniform=False)
            case(domain, part, CCW, [100, 100, 100, 100], [100, 100], uniform=False, maxf=7.0)
    # edge sharing: two patches with the same factor on an edge have identical edge vertices
    for part in range(4):
        for f in (1, 2.5, 3, 4.2, 7, 11.5):
            a = run(QUAD, part, CCW, [3, 2, f, 5], [4, 3], 64.0)[1]
            b = run(QUAD, part, CCW, [f, 6, 2, 3], [5, 5], 64.0)[1]
            ea = sorted(round(p[1], 5) for p in a if abs(p[0] - 1) < 1e-6)
            eb = sorted(round(p[1], 5) for p in b if abs(p[0]) < 1e-6)
            check(ea == eb, 'shared edge differs part=%d f=%g' % (part, f))
    # winding is the exact reversal
    for domain in (QUAD, TRI):
        cw = run(domain, INTEGER, CW, [3, 4, 3, 5], [4, 5])[2]
        ccw = run(domain, INTEGER, CCW, [3, 4, 3, 5], [4, 5])[2]
        check([(a, c, b) for a, b, c in ccw] == cw, 'winding reversal domain=%d' % domain)
    # culling
    for bad in (0.0, -2.0, float('nan'), float('-inf')):
        for domain in (QUAD, TRI):
            for slot in range(3):
                e = [2.0, 2.0, 2.0, 2.0]
                e[slot] = bad
                check(run(domain, INTEGER, CCW, e, [2, 2])[0] == 1, 'cull edge %s' % bad)
            check(run(domain, INTEGER, CCW, [2] * 4, [bad, 2])[0] == 1, 'cull inside %s' % bad)
    check(run(QUAD, 9, CCW, [1] * 4, [1, 1])[0] == -1, 'bad partition rejected')
    check(run(5, 0, CCW, [1] * 4, [1, 1])[0] == -1, 'bad domain rejected')
    # exact sizing: vertex_count/index_count are the real numbers for a hand-checked case
    r, v, t = run(QUAD, INTEGER, CCW, [2, 2, 2, 2], [2, 2])
    check(len(v) == 9 and len(t) == 8, 'quad 2/2 expected 9 vertices 8 triangles, got %d/%d' % (len(v), len(t)))
    r, v, t = run(TRI, INTEGER, CCW, [2, 2, 2], [2])
    check(len(v) == 7 and len(t) == 6, 'tri 2/2 expected 7 vertices 6 triangles, got %d/%d' % (len(v), len(t)))
    r, v, t = run(QUAD, INTEGER, CCW, [3, 3, 3, 3], [3, 3])
    check(len(v) == 16 and len(t) == 18, 'quad 3/3 expected 16 vertices 18 triangles, got %d/%d' % (len(v), len(t)))


def test_half():
    for h, f in ((0x3c00, 1.0), (0x4000, 2.0), (0x4400, 4.0), (0x5400, 64.0), (0x3800, 0.5), (0x0001, 2 ** -24),
                 (0x0400, 2 ** -14), (0x7bff, 65504.0), (0xbc00, -1.0), (0x0000, 0.0)):
        check(lib.half_to_float(h) == f, 'half 0x%04x' % h)
    check(math.isinf(lib.half_to_float(0x7c00)) and math.isnan(lib.half_to_float(0x7e00)), 'half inf/nan')
    for _ in range(2000):
        h = random.randrange(0x10000)
        want = struct.unpack('<e', struct.pack('<H', h))[0]
        got = lib.half_to_float(h)
        check((math.isnan(want) and math.isnan(got)) or got == want, 'half exhaustive 0x%04x' % h)
    buf = struct.pack('<6H', 0x4000, 0x4400, 0x4800, 0x3c00, 0x4200, 0x3800)
    e, i = (F * 4)(), (F * 2)()
    lib.read_factors(QUAD, buf, 0.5, e, i)
    check([e[k] for k in range(4)] == [1.0, 2.0, 4.0, 0.5] and [i[0], i[1]] == [1.5, 0.25], 'quad factor struct')
    lib.read_factors(TRI, buf, 2.0, e, i)
    check([e[k] for k in range(3)] == [4.0, 8.0, 16.0] and i[0] == 2.0, 'triangle factor struct')


def main():
    test_axis()
    test_half()
    test_meshes()
    if failures:
        print('FAILED: %d' % len(failures))
        return 1
    print('tessellator_test PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
