#!/usr/bin/env python3
"""SPDX-License-Identifier: MIT

ref_model.py -- scalar reference model for the V18 class (NEON three registers of the same length),
written from the ARM ARM pseudocode (the operation descriptions are cited next to each function).
It is used by gen_cases.py to compute the EXPECTED values that end up as literals in the generated
cases; it is deliberately a separate implementation from emu/n2a32_neon_3same.c (Python integers,
lane-by-lane, no bit tricks) so that a coding slip in one is unlikely to be repeated in the other.
The hand-written golden cases in golden_cases.inc are an independent check of this model.
"""

import struct

F32_DEFAULT_NAN = 0x7FC00000


def mask(es):
    return (1 << es) - 1


def sx(v, es):
    """Sign-extend the low es bits of v."""
    v &= mask(es)
    if v >= (1 << (es - 1)):
        return v - (1 << es)
    return v


def asr(v, s):
    """Arithmetic shift right of a Python integer (floor division by 2**s)."""
    if s <= 0:
        return v
    return v >> s


def clamp_s(v, es):
    hi = (1 << (es - 1)) - 1
    lo = -(1 << (es - 1))
    if v > hi:
        return hi, 1
    if v < lo:
        return lo, 1
    return v, 0


def clamp_u(v, es):
    hi = mask(es)
    if v > hi:
        return hi, 1
    if v < 0:
        return 0, 1
    return v, 0


def shl_or_asr_signed(value, sh, es):
    """VSHL (register): shift is Elem[Vn]<7:0> sign-extended."""
    if sh >= 0:
        if sh >= es:
            return 0
        return (value << sh) & mask(es)
    return asr(sx(value, es), -sh) & mask(es)


def shl_or_lsr_unsigned(value, sh, es):
    if sh >= 0:
        if sh >= es:
            return 0
        return (value << sh) & mask(es)
    return (value & mask(es)) >> (-sh) if -sh < 64 else 0


def signed_byte(v):
    b = v & 0xFF
    return b - 0x100 if b >= 0x80 else b


# ---------------------------------------------------------------------------------------- integer


def _i_lane(op, es, sg, acc, a, b):
    """One integer element (a = Vn element, b = Vm element, acc = old Vd).  Returns (result, sat)."""
    if op == "add":
        return (a + b) & mask(es), 0
    if op == "sub":
        return (a - b) & mask(es), 0
    if op == "hadd":
        v = sx(a, es) + sx(b, es) if sg else a + b
        return (asr(v, 1) if sg else (v >> 1)) & mask(es), 0
    if op == "hsub":
        v = sx(a, es) - sx(b, es) if sg else a - b
        # unsigned VHSUB: esize+1-bit intermediate, logical shift right
        if sg:
            return asr(v, 1) & mask(es), 0
        return ((v & ((1 << (es + 1)) - 1)) >> 1) & mask(es), 0
    if op == "rhadd":
        v = (sx(a, es) + sx(b, es) + 1) if sg else (a + b + 1)
        return (asr(v, 1) if sg else (v >> 1)) & mask(es), 0
    if op == "qadd":
        v = sx(a, es) + sx(b, es) if sg else a + b
        return clamp_s(v, es) if sg else clamp_u(v, es)
    if op == "qsub":
        v = sx(a, es) - sx(b, es) if sg else a - b
        return clamp_s(v, es) if sg else clamp_u(v, es)
    if op in ("cgt", "cge", "ceq", "tst"):
        if op == "tst":
            t = (a & b) != 0
        elif op == "ceq":
            t = a == b
        elif op == "cgt":
            t = (sx(a, es) > sx(b, es)) if sg else (a > b)
        else:
            t = (sx(a, es) >= sx(b, es)) if sg else (a >= b)
        return (mask(es) if t else 0), 0
    if op == "shl":
        # shift count from Vn, value from Vm
        return (shl_or_asr_signed(b, signed_byte(a), es) if sg
                else shl_or_lsr_unsigned(b, signed_byte(a), es)), 0
    if op in ("qshl", "qrshl"):
        sh = signed_byte(a)
        round_ = (op == "qrshl")
        if sh >= 0:
            if sg:
                v = sx(b, es) << sh
            else:
                v = b << sh
            return clamp_s(v, es) if sg else clamp_u(v, es)
        s = -sh
        if round_:
            # Round(x, s) = (x + 2**(s-1)) >> s with unbounded intermediates: for s >= 64 the
            # value is 0 for every element (|x| < 2**64 and the half-addend dominates).
            if s >= 64:
                v = 0
            elif sg:
                sv = sx(b, es)
                v = asr(sv, s) + ((sv >> (s - 1)) & 1)
            else:
                v = (b >> s) + ((b >> (s - 1)) & 1)
            return clamp_s(v, es) if sg else clamp_u(v, es)
        if sg:
            return asr(sx(b, es), s) & mask(es), 0
        return ((b >> s) if s < 64 else 0), 0
    if op == "rshl":
        sh = signed_byte(a)
        if sh >= 0:
            if sh >= es:
                return 0, 0
            return (b << sh) & mask(es), 0
        s = -sh
        if s >= 64:
            return 0, 0
        if sg:
            sv = sx(b, es)
            return (asr(sv, s) + ((sv >> (s - 1)) & 1)) & mask(es), 0
        return ((b >> s) + ((b >> (s - 1)) & 1)) & mask(es), 0
    if op in ("max", "min"):
        if sg:
            x, y = sx(a, es), sx(b, es)
        else:
            x, y = a, b
        return (x if (x > y if op == "max" else x < y) else y), 0
    if op in ("abd", "aba"):
        if sg:
            diff = abs(sx(a, es) - sx(b, es))
        else:
            diff = abs(a - b)
        if op == "aba":
            return (acc + diff) & mask(es), 0
        return diff & mask(es), 0
    if op == "mul":
        return (a * b) & mask(es), 0
    if op == "pmul":
        assert es == 8
        r = 0
        for i in range(8):
            if (b >> i) & 1:
                r ^= a << i
        return r & 0xFF, 0
    if op == "mla":
        return (acc + a * b) & mask(es), 0
    if op == "mls":
        return (acc - a * b) & mask(es), 0
    if op in ("qdmulh", "qrdmulh"):
        p = sx(a, es) * sx(b, es)
        if op == "qrdmulh":
            p += 1 << (es - 2)
        return clamp_s(asr(p, es - 1), es)
    raise ValueError(op)


# ------------------------------------------------------------------------------------------ float


def f32_is_nan(u):
    return (u & 0x7F800000) == 0x7F800000 and (u & 0x007FFFFF) != 0


def f32_is_qnan(u):
    return f32_is_nan(u) and (u & 0x00400000) != 0


def f32_is_inf(u):
    return (u & 0x7FFFFFFF) == 0x7F800000


def f32_is_zero(u):
    return (u & 0x7FFFFFFF) == 0


def f32_of(u):
    return struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0]


def f32_bits(x):
    """Round a Python float to binary32 (round-to-nearest-even), saturating to infinity."""
    if x != x:
        return F32_DEFAULT_NAN
    try:
        return struct.unpack("<I", struct.pack("<f", x))[0]
    except OverflowError:
        return 0xFF800000 if x < 0 else 0x7F800000


def f32_flush_in(u):
    """FPUnpack with FZ=1: denormal input -> signed zero."""
    if (u & 0x7F800000) == 0:
        return u & 0x80000000
    return u


def f32_result(x):
    """Result processing with DN=1 and FZ=1."""
    if x != x:
        return F32_DEFAULT_NAN
    u = f32_bits(x)
    if (u & 0x7F800000) == 0 and (u & 0x007FFFFF) != 0:
        u &= 0x80000000
    return u


def f_add(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return F32_DEFAULT_NAN
    return f32_result(f32_of(a) + f32_of(b))


def f_sub(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return F32_DEFAULT_NAN
    return f32_result(f32_of(a) - f32_of(b))


def f_mul(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return F32_DEFAULT_NAN
    return f32_result(f32_of(a) * f32_of(b))


def f_mla(a, b, d, sub):
    a, b, d = f32_flush_in(a), f32_flush_in(b), f32_flush_in(d)
    if f32_is_nan(a) or f32_is_nan(b) or f32_is_nan(d):
        return F32_DEFAULT_NAN
    p = f32_result(f32_of(a) * f32_of(b))
    if sub:
        p ^= 0x80000000
    return f32_result(f32_of(d) + f32_of(p))


def f_fma(a, b, d, sub):
    a, b, d = f32_flush_in(a), f32_flush_in(b), f32_flush_in(d)
    if f32_is_nan(a) or f32_is_nan(b) or f32_is_nan(d):
        return F32_DEFAULT_NAN
    va, vb, vd = f32_of(a), f32_of(b), f32_of(d)
    if sub:
        va = -va
    return f32_result(va * vb + vd)


def f_max(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return F32_DEFAULT_NAN
    if f32_is_zero(a) and f32_is_zero(b):
        return (a & b) & 0x80000000
    return a if f32_of(a) > f32_of(b) else b


def f_min(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return F32_DEFAULT_NAN
    if f32_is_zero(a) and f32_is_zero(b):
        return (a | b) & 0x80000000
    return a if f32_of(a) < f32_of(b) else b


def f_maxnm(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_qnan(a) and not f32_is_qnan(b):
        a = 0xFF800000
    elif not f32_is_qnan(a) and f32_is_qnan(b):
        b = 0xFF800000
    return f_max(a, b)


def f_minnm(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_qnan(a) and not f32_is_qnan(b):
        a = 0x7F800000
    elif not f32_is_qnan(a) and f32_is_qnan(b):
        b = 0x7F800000
    return f_min(a, b)


def f_recps(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return F32_DEFAULT_NAN
    if (f32_is_inf(a) and f32_is_zero(b)) or (f32_is_zero(a) and f32_is_inf(b)):
        return f32_bits(2.0)
    if f32_is_inf(a) or f32_is_inf(b):
        return (((a ^ b) & 0x80000000) ^ 0x80000000) | 0x7F800000
    return f32_result(2.0 - f32_of(a) * f32_of(b))


def f_rsqrts(a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return F32_DEFAULT_NAN
    if (f32_is_inf(a) and f32_is_zero(b)) or (f32_is_zero(a) and f32_is_inf(b)):
        return f32_bits(1.5)
    if f32_is_inf(a) or f32_is_inf(b):
        return (((a ^ b) & 0x80000000) ^ 0x80000000) | 0x7F800000
    return f32_result((3.0 - f32_of(a) * f32_of(b)) * 0.5)


def f_cmp(op, a, b):
    a, b = f32_flush_in(a), f32_flush_in(b)
    if f32_is_nan(a) or f32_is_nan(b):
        return 0
    if op == "abd":
        r = f32_result(f32_of(a) - f32_of(b))
        return r & 0x7FFFFFFF
    if op == "ceq":
        t = a == b
    elif op == "cge":
        t = f32_of(a) >= f32_of(b)
    elif op == "cgt":
        t = f32_of(a) > f32_of(b)
    elif op == "acge":
        t = f32_of(a & 0x7FFFFFFF) >= f32_of(b & 0x7FFFFFFF)
    elif op == "acgt":
        t = f32_of(a & 0x7FFFFFFF) > f32_of(b & 0x7FFFFFFF)
    else:
        raise ValueError(op)
    return 0xFFFFFFFF if t else 0


_FOPS = {
    "add": lambda a, b, d: f_add(a, b),
    "sub": lambda a, b, d: f_sub(a, b),
    "mul": lambda a, b, d: f_mul(a, b),
    "mla": lambda a, b, d: f_mla(a, b, d, 0),
    "mls": lambda a, b, d: f_mla(a, b, d, 1),
    "fma": lambda a, b, d: f_fma(a, b, d, 0),
    "fms": lambda a, b, d: f_fma(a, b, d, 1),
    "max": lambda a, b, d: f_max(a, b),
    "min": lambda a, b, d: f_min(a, b),
    "maxnm": lambda a, b, d: f_maxnm(a, b),
    "minnm": lambda a, b, d: f_minnm(a, b),
    "recps": lambda a, b, d: f_recps(a, b),
    "rsqrts": lambda a, b, d: f_rsqrts(a, b),
    "ceq": lambda a, b, d: f_cmp("ceq", a, b),
    "cge": lambda a, b, d: f_cmp("cge", a, b),
    "cgt": lambda a, b, d: f_cmp("cgt", a, b),
    "acge": lambda a, b, d: f_cmp("acge", a, b),
    "acgt": lambda a, b, d: f_cmp("acgt", a, b),
    "abd": lambda a, b, d: f_cmp("abd", a, b),
}


def f_lane(op, d, n, m):
    return _FOPS[op](n, m, d)


# ------------------------------------------------------------------------------------- register level


def run_case(domain, op, es, sg, q, dval, nval, mval):
    """Apply one instruction to 64/128-bit register values.

    dval/nval/mval are Python ints (128-bit for q, 64-bit otherwise).  Returns (dest0, dest1, qc):
    dest1 is None for the D form.
    """
    if domain == "int":
        esz = es // 8
        regs = 2 if q else 1
        dest = []
        qc = 0
        for r in range(regs):
            base = 64 * r
            dv = (dval >> base) & 0xFFFFFFFFFFFFFFFF
            nv = (nval >> base) & 0xFFFFFFFFFFFFFFFF
            mv = (mval >> base) & 0xFFFFFFFFFFFFFFFF
            out = 0
            for e in range(64 // es):
                acc = (dv >> (e * es)) & mask(es)
                a = (nv >> (e * es)) & mask(es)
                b = (mv >> (e * es)) & mask(es)
                v, sat = _i_lane(op, es, sg, acc, a, b)
                qc |= sat
                out |= (v & mask(es)) << (e * es)
            dest.append(out)
        return dest[0], (dest[1] if q else None), qc
    assert domain == "float" and es == 32
    regs = 2 if q else 1
    dest = []
    qc = 0
    for r in range(regs):
        base = 64 * r
        dv = (dval >> base) & 0xFFFFFFFFFFFFFFFF
        nv = (nval >> base) & 0xFFFFFFFFFFFFFFFF
        mv = (mval >> base) & 0xFFFFFFFFFFFFFFFF
        out = 0
        for e in range(2):
            acc = (dv >> (e * 32)) & 0xFFFFFFFF
            a = (nv >> (e * 32)) & 0xFFFFFFFF
            b = (mv >> (e * 32)) & 0xFFFFFFFF
            v = f_lane(op, acc, a, b)
            out |= (v & 0xFFFFFFFF) << (e * 32)
        dest.append(out)
    return dest[0], (dest[1] if q else None), qc


def run_logic(op, q, dval, nval, mval):
    """VAND/VBIC/VORR/VORN/VEOR/VBSL/VBIT/VBIF: 64-bit chunks, no element size."""
    regs = 2 if q else 1
    dest = []
    for r in range(regs):
        base = 64 * r
        a = (nval >> base) & 0xFFFFFFFFFFFFFFFF
        b = (mval >> base) & 0xFFFFFFFFFFFFFFFF
        acc = (dval >> base) & 0xFFFFFFFFFFFFFFFF
        if op == "and":
            v = a & b
        elif op == "bic":
            v = a & ~b
        elif op == "orr":
            v = a | b
        elif op == "orn":
            v = a | ~b
        elif op == "eor":
            v = a ^ b
        elif op == "bsl":
            v = (a & acc) | (b & ~acc)
        elif op == "bit":
            v = (a & b) | (acc & ~b)
        else:
            v = (acc & b) | (a & ~b)
        dest.append(v & 0xFFFFFFFFFFFFFFFF)
    return dest[0], (dest[1] if q else None), 0


def run_pair(domain, op, es, sg, dval, nval, mval):
    """VPADD/VPMAX/VPMIN (D form only): [0] from the Vn pairs, [1] from the Vm pairs."""
    if domain == "int":
        per = 64 // es
        h = per // 2
        out = 0
        qc = 0
        for e in range(h):
            a = (nval >> (2 * e * es)) & mask(es)
            b = (nval >> ((2 * e + 1) * es)) & mask(es)
            v, sat = _i_lane(op, es, sg, 0, a, b)
            qc |= sat
            out |= (v & mask(es)) << (e * es)
            a = (mval >> (2 * e * es)) & mask(es)
            b = (mval >> ((2 * e + 1) * es)) & mask(es)
            v, sat = _i_lane(op, es, sg, 0, a, b)
            qc |= sat
            out |= (v & mask(es)) << ((e + h) * es)
        return out, None, qc
    n0 = nval & 0xFFFFFFFF
    n1 = (nval >> 32) & 0xFFFFFFFF
    m0 = mval & 0xFFFFFFFF
    m1 = (mval >> 32) & 0xFFFFFFFF
    out = f_lane(op, 0, n0, n1) | (f_lane(op, 0, m0, m1) << 32)
    return out, None, 0
