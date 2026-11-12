#!/usr/bin/env python3
"""SPDX-License-Identifier: MIT

gen_cases.py -- generate the V18 test cases and their llvm-mc encodings.

Every instruction word the tests use is assembled here with

    llvm-mc -triple=armv8a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8 --show-encoding

from assembly text built from the mnemonic/datatype/register tables below.  No encoding is typed by
hand anywhere.  The EXPECTED register values come from ref_model.py (written from the ARM ARM
pseudocode, independent of the C implementation); the cases with hand-derived expected values are in
golden_cases.inc, outside this generator.

Outputs (into the directory given as argv[1]):
    enc_v18.h        E_<LABEL> for every instruction, in assembly order
    cases_v18.inc    one "CASE_D(...)"/"CASE_Q(...)" entry per generated case

It also checks coverage: every allocated (U, size, opc, bit4, Q) combination of this instruction class
that llvm-mc decodes (except the deliberately unimplemented SHA space) must have at least six cases.
"""

import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True          # keep __pycache__ out of the tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_model as M

MASK64 = 0xFFFFFFFFFFFFFFFF
MASK128 = (1 << 128) - 1

# Element value pools: edge values for each element size (saturation boundaries, sign changes,
# carries, all-ones, powers of two).
VALS = {
    8: [0x00, 0x01, 0x7F, 0x80, 0xFF, 0x02, 0xFE, 0x40, 0xC0, 0x55, 0xAA, 0x0F],
    16: [0x0000, 0x0001, 0x7FFF, 0x8000, 0xFFFF, 0x4000, 0xC000, 0x00FF, 0xFF00, 0x0002,
         0x8001, 0x7FFE],
    32: [0x00000000, 0x00000001, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x40000000, 0xC0000000,
         0x00000002, 0xFFFFFFFE, 0x00010000, 0x80000001, 0x7FFFFFFE],
    64: [0x0000000000000000, 0x0000000000000001, 0x7FFFFFFFFFFFFFFF, 0x8000000000000000,
         0xFFFFFFFFFFFFFFFF, 0x4000000000000000, 0xC000000000000000, 0x0000000000000002,
         0xFFFFFFFFFFFFFFFE, 0x0000000100000000, 0x8000000000000001, 0x7FFFFFFFFFFFFFFE],
}
# Shift counts: the low byte of each element is the signed count; the high bytes are garbage on
# purpose (they must be ignored).
# Shift counts (one signed byte per element).  Beside small counts the list holds +/- (esize-1),
# esize and esize+1 for every element size (8/16/32/64) so that the "shift >= element size" and the
# "shift == esize-1" boundaries are exercised for all four sizes.
SHIFTS = [0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
          0x7F, 0x80, 0x81, 0xFF, 0xF8, 0xF1, 0xF0, 0xEF, 0xE1, 0xE0, 0xDF,
          0xC1, 0xC0, 0xBF, 0x40, 0x41]

# float32 patterns: zeros, ones, infinities, quiet and signalling NaNs, denormals, large/small,
# the constants used by the reciprocal steps.
F32VALS = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000,
           0x7FC00000, 0x7F800001, 0x00000001, 0x80000001, 0x3F000000, 0x40000000,
           0x40400000, 0xC0400000, 0x7F7FFFFF, 0x00800000, 0x4B000000, 0x3EAAAAAB,
           0x00000002, 0x7FFFFFFF, 0x4CBEBC20, 0x3F800001]

# Distinct registers inside each triple (the driver rejects an inconsistent alias), covering low
# and high D registers (the D/N/M encoding bits) and Q0/Q8/Q15 (even D numbers, as required).
REG_TUPLES_D = [(1, 2, 3), (16, 17, 18), (31, 30, 29), (4, 5, 6),
                (5, 20, 7), (16, 2, 31), (9, 10, 11), (24, 25, 26)]
REG_TUPLES_Q = [(1, 2, 3), (8, 9, 10), (15, 14, 13), (2, 3, 4),
                (5, 12, 7), (8, 2, 15), (9, 10, 11), (12, 13, 14)]

NCASES = 8


def lanes_of(es):
    return 64 // es


def pattern(es, idx, q, which, salt=0):
    """A deterministic per-lane pattern of interesting values, as a 64/128-bit integer."""
    pool = VALS[es]
    out = 0
    total = (2 if q else 1) * lanes_of(es)
    for lane in range(total):
        v = pool[(idx * 5 + lane * 7 + which * 3 + salt) % len(pool)]
        out |= (v & M.mask(es)) << (lane * es)
    return out


def boundary_shifts(es):
    """The shift counts every shift form must exercise, one per case index."""
    return [0, 1, es - 1, -(es - 1), es, -es, es + 1, -(es + 1)]


def boundary_pattern(es, idx, q):
    """A shift operand whose low byte in every lane is boundary_shifts(es)[idx] (high bytes 0x5a,
    which must be ignored)."""
    sh = boundary_shifts(es)[idx % 8] & 0xFF
    v = sh
    for k in range(1, es // 8):
        v |= 0x5A << (8 * k)
    out = 0
    for lane in range((2 if q else 1) * lanes_of(es)):
        out |= (v & M.mask(es)) << (lane * es)
    return out


def shift_pattern(es, idx, q, which):
    """Shift operand: low byte of each element from SHIFTS, higher bytes garbage (0x5a)."""
    out = 0
    total = (2 if q else 1) * lanes_of(es)
    for lane in range(total):
        b = SHIFTS[(idx * 3 + lane * 5 + which) % len(SHIFTS)]
        v = b
        for k in range(1, es // 8):
            v |= 0x5A << (8 * k)
        out |= (v & M.mask(es)) << (lane * es)
    return out


def fpattern(idx, q, which):
    out = 0
    total = (2 if q else 1) * 2
    for lane in range(total):
        v = F32VALS[(idx * 5 + lane * 7 + which * 3) % len(F32VALS)]
        out |= v << (lane * 32)
    return out


def fmt64(v):
    return "UINT64_C(0x%016x)" % (v & MASK64)


def repeat_lane(v, es, q):
    out = 0
    for lane in range((2 if q else 1) * lanes_of(es)):
        out |= (v & M.mask(es)) << (lane * es)
    return out


SATURATING = ("qadd", "qsub", "qshl", "qrshl", "qdmulh", "qrdmulh")

_PAIR_CACHE = {}


def saturating_pairs(op, es, sg):
    """(pair that saturates, pair that does not) for a saturating operation, searched in the
    element pools with ref_model; used for the first two cases of every saturating form."""
    key = (op, es, sg)
    if key in _PAIR_CACHE:
        return _PAIR_CACHE[key]
    pool = VALS[es]
    nvals = SHIFTS if op in ("qshl", "qrshl") else pool
    sat_pair = nosat = None
    for nv in nvals:
        for mv in pool:
            _v, sat = M._i_lane(op, es, sg, 0, nv & M.mask(es), mv & M.mask(es))
            if sat and sat_pair is None:
                sat_pair = (nv, mv)
            if not sat and nosat is None:
                nosat = (nv, mv)
            if sat_pair is not None and nosat is not None:
                break
        if sat_pair is not None and nosat is not None:
            break
    if sat_pair is None or nosat is None:
        raise SystemExit("no %s/%s pair for %s size %d" %
                         ("saturating" if sat_pair is None else "", "non-saturating",
                          op, es))
    _PAIR_CACHE[key] = (sat_pair, nosat)
    return sat_pair, nosat


def build_forms():
    """(label, asm, domain, op, es, sg, q, pair) for every form of the class."""
    forms = []
    int_ops = [
        ("vadd", "add", [8, 16, 32, 64], None),
        ("vsub", "sub", [8, 16, 32, 64], None),
        ("vhadd", "hadd", [8, 16, 32], True),
        ("vhsub", "hsub", [8, 16, 32], True),
        ("vrhadd", "rhadd", [8, 16, 32], True),
        ("vqadd", "qadd", [8, 16, 32, 64], True),
        ("vqsub", "qsub", [8, 16, 32, 64], True),
        ("vcgt", "cgt", [8, 16, 32], True),
        ("vcge", "cge", [8, 16, 32], True),
        ("vceq", "ceq", [8, 16, 32], None),
        ("vtst", "tst", [8, 16, 32], None),
        ("vshl", "shl", [8, 16, 32, 64], True),
        ("vqshl", "qshl", [8, 16, 32, 64], True),
        ("vrshl", "rshl", [8, 16, 32, 64], True),
        ("vqrshl", "qrshl", [8, 16, 32, 64], True),
        ("vmax", "max", [8, 16, 32], True),
        ("vmin", "min", [8, 16, 32], True),
        ("vabd", "abd", [8, 16, 32], True),
        ("vaba", "aba", [8, 16, 32], True),
        ("vmul", "mul", [8, 16, 32], None),
        ("vmla", "mla", [8, 16, 32], None),
        ("vmls", "mls", [8, 16, 32], None),
        ("vqdmulh", "qdmulh", [16, 32], "s"),
        ("vqrdmulh", "qrdmulh", [16, 32], "s"),
    ]
    for mn, op, sizes, signed in int_ops:
        for es in sizes:
            modes = [True, False] if signed is True else [False]
            if signed == "s":
                modes = [True]
            for sg in modes:
                if mn in ("vceq",):
                    dt = "i%d" % es
                elif mn == "vtst":
                    dt = "%d" % es
                elif signed is True:
                    dt = ("s" if sg else "u") + str(es)
                elif signed == "s":
                    dt = "s%d" % es
                else:
                    dt = "i%d" % es
                for q in (0, 1):
                    order = ("{d}, {vm}, {vn}" if op in ("shl", "qshl", "rshl", "qrshl")
                             else "{d}, {vn}, {vm}")
                    asm = "%s.%s %s" % (mn, dt, order)
                    label = "%s_%s_%s" % (mn, dt, "q" if q else "d")
                    forms.append((label, asm, "int", op, es, sg, q, False))
    # VMUL.P8
    for q in (0, 1):
        forms.append(("vmul_p8_%s" % ("q" if q else "d"),
                      "vmul.p8 {d}, {vn}, {vm}", "int", "pmul", 8, False, q, False))
    # pairwise integer
    for mn, op in (("vpadd", "add"), ("vpmax", "max"), ("vpmin", "min")):
        for es in (8, 16, 32):
            modes = [False] if mn == "vpadd" else [True, False]
            for sg in modes:
                dt = "i%d" % es if mn == "vpadd" else (("s" if sg else "u") + str(es))
                forms.append(("%s_%s_d" % (mn, dt), "%s.%s {d}, {vn}, {vm}" % (mn, dt),
                              "pair", op, es, sg, 0, True))
    # logic
    for mn, op in (("vand", "and"), ("vbic", "bic"), ("vorr", "orr"), ("vorn", "orn"),
                   ("veor", "eor"), ("vbsl", "bsl"), ("vbit", "bit"), ("vbif", "bif")):
        for q in (0, 1):
            forms.append((mn + ("_q" if q else "_d"), "%s {d}, {vn}, {vm}" % mn,
                          "logic", op, 0, False, q, False))
    # float
    float_ops = [("vadd", "add"), ("vsub", "sub"), ("vmul", "mul"), ("vmla", "mla"),
                 ("vmls", "mls"), ("vfma", "fma"), ("vfms", "fms"), ("vceq", "ceq"),
                 ("vcge", "cge"), ("vcgt", "cgt"), ("vacge", "acge"), ("vacgt", "acgt"),
                 ("vmax", "max"), ("vmin", "min"), ("vabd", "abd"), ("vrecps", "recps"),
                 ("vrsqrts", "rsqrts"), ("vmaxnm", "maxnm"), ("vminnm", "minnm")]
    for mn, op in float_ops:
        for q in (0, 1):
            forms.append((mn + "_f32_" + ("q" if q else "d"),
                          "%s.f32 {d}, {vn}, {vm}" % mn, "float", op, 32, False, q, False))
    for mn, op in (("vpadd", "add"), ("vpmax", "max"), ("vpmin", "min")):
        forms.append((mn + "_f32_d", "%s.f32 {d}, {vn}, {vm}" % mn,
                      "pairf", op, 32, False, 0, True))
    return forms


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: gen_cases.py outdir\n")
        return 2
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)

    forms = build_forms()
    labels, asms, cases = [], [], []

    for label, asm, domain, op, es, sg, q, pair in forms:
        tuples = REG_TUPLES_Q if q else REG_TUPLES_D
        for idx in range(NCASES):
            rd, rn, rm = tuples[idx]
            if q:
                dname, nname, mname = "q%d" % rd, "q%d" % rn, "q%d" % rm
            else:
                dname, nname, mname = "d%d" % rd, "d%d" % rn, "d%d" % rm
            text = asm.format(d=dname, vn=nname, vm=mname)
            lab = ("%s_%d" % (label, idx)).upper()
            labels.append(lab)
            asms.append(text)

            if domain == "pairf":
                dv = 0
                nv = fpattern(idx, False, 1)
                mv = fpattern(idx, False, 2)
                ed, _ed1, qc = M.run_pair("float", op, 32, sg, dv, nv, mv)
                cases.append(("CASE_D", lab, text, [dv], [nv], [mv], [ed], qc))
                continue

            if domain == "float":
                dv = fpattern(idx, q, 0)
                nv = fpattern(idx, q, 1)
                mv = fpattern(idx, q, 2)
                e0, e1, qc = M.run_case("float", op, 32, sg, q, dv, nv, mv)
                if q:
                    cases.append(("CASE_Q", lab, text, [dv, dv >> 64], [nv, nv >> 64],
                                  [mv, mv >> 64], [e0, e1], qc))
                else:
                    cases.append(("CASE_D", lab, text, [dv], [nv], [mv], [e0], qc))
                continue

            if domain == "logic":
                dv = pattern(8, idx, q, 0)
                nv = pattern(8, idx, q, 1)
                mv = pattern(8, idx, q, 2)
                e0, e1, qc = M.run_logic(op, q, dv, nv, mv)
                if q:
                    cases.append(("CASE_Q", lab, text, [dv, dv >> 64], [nv, nv >> 64],
                                  [mv, mv >> 64], [e0, e1], qc))
                else:
                    cases.append(("CASE_D", lab, text, [dv], [nv], [mv], [e0], qc))
                continue

            if domain == "pair":
                dv = pattern(es, idx, False, 0)
                nv = pattern(es, idx, False, 1)
                mv = pattern(es, idx, False, 2)
                ed, _ed1, qc = M.run_pair("int", op, es, sg, dv, nv, mv)
                cases.append(("CASE_D", lab, text, [dv], [nv], [mv], [ed], qc))
                continue

            # integer element-wise; for the shift operations Vn carries the shift counts
            if op in ("shl", "qshl", "rshl", "qrshl"):
                nv = boundary_pattern(es, idx, q)
            else:
                nv = pattern(es, idx, q, 1)
            dv = pattern(es, idx, q, 0)
            mv = pattern(es, idx, q, 2)
            if op in SATURATING and idx < 2:
                # case 0 must saturate (QC=1), case 1 must not: needs extreme lane values
                nl, ml = saturating_pairs(op, es, sg)[idx]
                nv = repeat_lane(nl, es, q)
                mv = repeat_lane(ml, es, q)
            e0, e1, qc = M.run_case("int", op, es, sg, q, dv, nv, mv)
            if q:
                cases.append(("CASE_Q", lab, text, [dv, dv >> 64], [nv, nv >> 64],
                              [mv, mv >> 64], [e0, e1], qc))
            else:
                cases.append(("CASE_D", lab, text, [dv], [nv], [mv], [e0], qc))

    # Hand-written golden encodings (assembly text in the tree; values in golden_cases.inc).
    golden_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden_encodings.txt")
    with open(golden_path) as f:
        for raw in f:
            raw = raw.strip()
            if not raw or raw.startswith("#"):
                continue
            label, asm = raw.split("|", 1)
            labels.append(label.strip().upper())
            asms.append(asm.strip())

    # assemble everything in one llvm-mc run
    src = "\t.syntax unified\n\t.arm\n\t.text\n" + "".join("\t%s\n" % a for a in asms)
    cmd = ["llvm-mc", "-triple=armv8a-linux-gnueabi",
           "-mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8", "--show-encoding"]
    p = subprocess.run(cmd, input=src, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout + p.stderr)
        return 1
    words = []
    for line in p.stdout.splitlines():
        m = re.search(r"@ encoding: \[([0-9a-fA-Fx,]+)\]", line)
        if not m:
            continue
        bs = [int(x, 16) for x in m.group(1).split(",")]
        if len(bs) != 4:
            sys.stderr.write("unexpected encoding line: %s\n" % line)
            return 1
        words.append(bs[0] | bs[1] << 8 | bs[2] << 16 | bs[3] << 24)
    if len(words) != len(labels):
        sys.stderr.write("%d encodings for %d instructions\n" % (len(words), len(labels)))
        return 1

    with open(os.path.join(outdir, "enc_v18.h"), "w") as f:
        f.write("/* SPDX-License-Identifier: MIT */\n")
        f.write("/* Generated by gen_cases.py with llvm-mc; do not edit. */\n")
        f.write("#ifndef ENC_V18_H\n#define ENC_V18_H\n")
        for lab, w in zip(labels, words):
            f.write("#define E_%s 0x%08xu\n" % (lab, w))
        f.write("#endif\n")

    with open(os.path.join(outdir, "cases_v18.inc"), "w") as f:
        f.write("/* SPDX-License-Identifier: MIT */\n")
        f.write("/* Generated by gen_cases.py; expected values from ref_model.py. Do not edit. */\n")
        for kind, lab, text, dv, nv, mv, ed, qc in cases:
            if kind == "CASE_D":
                f.write('CASE_D("%s", E_%s, %s, %s, %s, %s, %d) /* %s */\n' %
                        (lab.lower(), lab, fmt64(dv[0]), fmt64(nv[0]), fmt64(mv[0]),
                         fmt64(ed[0]), qc, text))
            else:
                f.write('CASE_Q("%s", E_%s, %s, %s, %s, %s, %s, %s, %s, %s, %d) /* %s */\n' %
                        (lab.lower(), lab, fmt64(dv[0]), fmt64(dv[1]), fmt64(nv[0]), fmt64(nv[1]),
                         fmt64(mv[0]), fmt64(mv[1]), fmt64(ed[0]), fmt64(ed[1]), qc, text))

    # ---- coverage: every allocated non-SHA combination needs >= 6 cases
    combos = {}
    for kind, lab, text, dv, nv, mv, ed, qc in cases:
        w = words[labels.index(lab)]
        key = ((w >> 24) & 1, (w >> 20) & 3, (w >> 8) & 0xF, (w >> 4) & 1, (w >> 6) & 1)
        combos[key] = combos.get(key, 0) + 1
    allocated = oracle_allocated()
    missing = []
    for key in sorted(allocated):
        if sha_space(key):
            continue
        if combos.get(key, 0) < 6:
            missing.append((key, combos.get(key, 0)))
    if missing:
        for key, n in missing:
            sys.stderr.write("form U=%d size=%d opc=%x b4=%d Q=%d has %d cases (need 6)\n"
                             % (key[0], key[1], key[2], key[3], key[4], n))
        return 1

    # Flag coverage: every saturating form must have cases that set QC and cases that leave it
    # clear (the value pools contain saturation boundaries; this check proves it).
    qc_set = {}
    for kind, lab, text, dv, nv, mv, ed, qc in cases:
        if qc:
            qc_set[lab.rsplit("_", 1)[0].lower()] = qc_set.get(lab.rsplit("_", 1)[0].lower(), 0) + 1
    saturating = ("vqadd", "vqsub", "vqshl", "vqrshl", "vqdmulh", "vqrdmulh")
    for label, asm, domain, op, es, sg, q, pair in forms:
        if not label.startswith(saturating):
            continue
        n = qc_set.get(label, 0)
        if n == 0 or n == NCASES:
            sys.stderr.write("form %s sets QC in %d of %d cases (need a mix)\n"
                             % (label, n, NCASES))
            return 1
    print("V18-GEN forms %d cases %d allocated-combos %d covered %d"
          % (len(forms), len(cases), len(allocated), len(combos)))
    return 0


def sha_space(key):
    """The SHA1/SHA256 space that shares this encoding class: deliberately unimplemented."""
    u, size, opc, b4, q = key
    del u
    return opc == 0xC and b4 == 0 and q == 1 and size in (0, 1, 2, 3)


def disasm(words):
    data = "".join("0x%02x 0x%02x 0x%02x 0x%02x\n" %
                   (w & 0xff, (w >> 8) & 0xff, (w >> 16) & 0xff, (w >> 24) & 0xff) for w in words)
    p = subprocess.run(["llvm-mc", "--disassemble", "-triple=armv8a-linux-gnueabi",
                        "-mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8"],
                       input=data, capture_output=True, text=True)
    bad = set(int(m.group(1)) for m in
              re.finditer(r"<stdin>:(\d+):1: warning: invalid instruction encoding", p.stderr))
    out = [l.strip() for l in p.stdout.splitlines() if l.strip()]
    res = []
    oi = 0
    for i, w in enumerate(words):
        if (i + 1) in bad:
            res.append(None)
        else:
            res.append(out[oi])
            oi += 1
    assert oi == len(out)
    return res


def oracle_allocated():
    """The (U, size, opc, bit4, Q) combinations llvm-mc decodes in this instruction space.

    Q form registers are encoded as even D-register numbers (Qd = D(Vd), D(Vd+1)), so the Q probes
    use even fields (2, 4, 6) as well as all-zero ones.
    """
    patterns = {0: [0x00000000, 0x00021003, 0x004100A2],
                1: [0x00000040, 0x00042046, 0x000EC04A]}
    keys = set()
    for u in (0, 1):
        for size in (0, 1, 2, 3):
            for opc in range(16):
                for b4 in (0, 1):
                    for q in (0, 1):
                        words = [0xF2000000 | (u << 24) | (size << 20) | (opc << 8) |
                                 (b4 << 4) | pat for pat in patterns[q]]
                        res = disasm(words)
                        if any(r is not None for r in res):
                            keys.add((u, size, opc, b4, q))
    return keys


if __name__ == "__main__":
    sys.exit(main())
