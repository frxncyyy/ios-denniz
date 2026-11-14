#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# V19 test-vector generator.
#
#   gen_vectors.py <outdir>
#
# Writes <outdir>/enc_v19.s (the assembly source of every encoding used), <outdir>/vec_v19.h
# (encoding + literal expected register values).  Every encoding comes from llvm-mc; every
# expected value comes from the ARM ARM pseudocode model below (not from n2a32_neon_shift.c).
import math
import os
import zlib
import re
import struct
import subprocess
import sys

MASK64 = (1 << 64) - 1
MASK128 = (1 << 128) - 1

# ---------------------------------------------------------------------------------------------------------
# ARM ARM pseudocode model (independent of the C implementation)

def get_elem(d, reg, esize, i):
    per = 64 // esize
    return (d[reg + i // per] >> ((i % per) * esize)) & ((1 << esize) - 1)


def set_elem(d, reg, esize, i, v):
    per = 64 // esize
    sh = (i % per) * esize
    m = ((1 << esize) - 1) << sh
    d[reg + i // per] = (d[reg + i // per] & ~m) | ((v & ((1 << esize) - 1)) << sh)


def get_vec(d, dreg, esize, i):
    per = 64 // esize
    return get_elem(d, dreg + i // per, esize, i % per)


def set_vec(d, dreg, esize, i, v):
    per = 64 // esize
    set_elem(d, dreg + i // per, esize, i % per, v)


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def sat_s(v, bits):
    lo = -(1 << (bits - 1))
    hi = (1 << (bits - 1)) - 1
    return lo if v < lo else hi if v > hi else v


def sat_u(v, bits):
    return 0 if v < 0 else (1 << bits) - 1 if v > (1 << bits) - 1 else v


def rshr_u(v, shift):
    return (v + (1 << (shift - 1))) >> shift


def rshr_s(v, shift):
    return (v + (1 << (shift - 1))) >> shift


def vsri(dv, sv, esize, shift):
    # shift right and insert into the low bits; the destination's top `shift` bits are kept
    if shift == 0:
        return sv
    if shift >= esize:
        return dv
    m = (1 << (esize - shift)) - 1
    return (dv & ~m) | ((sv >> shift) & m)


def vsli(dv, sv, esize, shift):
    # shift left and insert into the high bits; the destination's low `shift` bits are kept
    if shift == 0:
        return sv
    m = ((1 << (esize - shift)) - 1) << shift
    return (dv & ~m) | ((sv << shift) & m)


def f32_from_bits(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xFFFFFFFF))[0]


def f32_bits(x):
    try:
        return struct.unpack('<I', struct.pack('<f', x))[0]
    except OverflowError:
        # Python raises where IEEE 754 gives an infinity (the C code sees inf).
        return 0x7F800000 if x > 0 else 0xFF800000


def f32_std(x):
    if math.isnan(x):
        return f32_from_bits(0x7FC00000)
    if x != 0.0 and abs(x) < 2.0 ** -126:
        return math.copysign(0.0, x)
    return x


def f32_flush_in(x):
    if x != 0.0 and abs(x) < 2.0 ** -126:
        return math.copysign(0.0, x)
    return x


def f32_op(a, b, c, op):
    fa = f32_flush_in(f32_from_bits(a))
    fb = f32_flush_in(f32_from_bits(b))
    p = f32_std(f32_from_bits(f32_bits(fa * fb)))
    if op == 0:
        return f32_bits(p)
    fc = f32_flush_in(f32_from_bits(c))
    r = fc + p if op == 1 else fc - p
    return f32_bits(f32_std(f32_from_bits(f32_bits(r))))


def fp_to_fixed(u, fbits, unsigned):
    f = f32_from_bits(u)
    if math.isnan(f):
        return 0, 0
    scaled = f * float(1 << fbits)
    if unsigned:
        if scaled < 0.0:
            return 0, 1
        if scaled >= 4294967296.0:
            return 0xFFFFFFFF, 1
        return int(scaled) & 0xFFFFFFFF, 0
    if scaled >= 2147483648.0:
        return 0x7FFFFFFF, 1
    if scaled < -2147483648.0:
        return 0x80000000, 1
    return int(scaled) & 0xFFFFFFFF, 0


def fixed_to_fp(u, fbits, unsigned):
    v = (u & 0xFFFFFFFF) if unsigned else sext(u, 32)
    return f32_bits(f32_std(f32_from_bits(f32_bits(float(v) / float(1 << fbits)))))


def carryless8(a, b):
    r = 0
    for k in range(8):
        if (a >> k) & 1:
            r ^= (b << k) & 0xFFFF
    return r


def dbl_sat(p, bits):
    d = p * 2
    s = sat_s(d, bits)
    return s, 1 if s != d else 0


def add_sat(a, b, bits):
    s = a + b
    r = sat_s(s, bits)
    return r, 1 if r != s else 0


def sub_sat(a, b, bits):
    s = a - b
    r = sat_s(s, bits)
    return r, 1 if r != s else 0


def qdmulh(p, bits, rnd):
    d = p * 2 + ((1 << (bits - 1)) if rnd else 0)
    r = d >> bits
    s = sat_s(r, bits)
    return s, 1 if s != r else 0


def exec_shift2(d, m):
    esize, shift, U, opc = m['esize'], m['shift'], m['U'], m['opc']
    nd = 2 if m['Q'] else 1
    total = nd * (64 // esize)
    qc = 0
    for i in range(total):
        sv = get_vec(d, m['mreg'], esize, i)
        dv = get_vec(d, m['dreg'], esize, i)
        ssv = sext(sv, esize)
        if opc == 0:
            r = (sv >> shift) if U else (ssv >> shift)
        elif opc == 1:
            r = dv + ((sv >> shift) if U else (ssv >> shift))
        elif opc == 2:
            r = rshr_u(sv, shift) if U else rshr_s(ssv, shift)
        elif opc == 3:
            r = dv + (rshr_u(sv, shift) if U else rshr_s(ssv, shift))
        elif opc == 4:
            r = vsri(dv, sv, esize, shift)
        elif opc == 5:
            r = vsli(dv, sv, esize, shift) if U else (sv << shift)
        elif opc == 6:
            v = ssv << shift
            r = sat_u(v, esize)
            qc |= 1 if r != v else 0
        else:
            v = (sv << shift) if U else (ssv << shift)
            r = sat_u(v, esize) if U else sat_s(v, esize)
            qc |= 1 if r != v else 0
        set_vec(d, m['dreg'], esize, i, r)
    return qc


def exec_narrow(d, m):
    src, shift, U, opc, rnd = m['src_esize'], m['shift'], m['U'], m['opc'], m['round']
    dbits = src // 2
    total = 128 // src
    qc = 0
    for i in range(total):
        sv = get_vec(d, m['mreg'], src, i)
        ssv = sext(sv, src)
        add = (1 << (shift - 1)) if rnd else 0
        if opc == 8:
            if U == 0:
                r = (sv + add) >> shift
            else:
                v = (ssv + add) >> shift
                r = sat_u(v, dbits)
                qc |= 1 if r != v else 0
        else:
            if U == 0:
                v = (ssv + add) >> shift
                r = sat_s(v, dbits)
                qc |= 1 if r != v else 0
            else:
                v = (sv + add) >> shift
                r = sat_u(v, dbits)
                qc |= 1 if r != v else 0
        set_vec(d, m['dreg'], dbits, i, r)
    return qc


def exec_wide(d, m):
    src, shift, U = m['src_esize'], m['shift'], m['U']
    for i in range(64 // src):
        sv = get_elem(d, m['mreg'], src, i)
        w = sv if U else sext(sv, src)
        set_vec(d, m['dreg'], 2 * src, i, w << shift)


def exec_vcvt(d, m):
    qc = 0
    for i in range((2 if m['Q'] else 1) * 2):
        sv = get_vec(d, m['mreg'], 32, i)
        if m['dir'] == 'f2x':
            r, s = fp_to_fixed(sv, m['fbits'], m['U'])
            qc |= s
        else:
            r = fixed_to_fp(sv, m['fbits'], m['U'])
        set_vec(d, m['dreg'], 32, i, r)
    return qc


def exec_diff(d, m):
    opc, U, size, form = m['opc'], m['U'], m['size'], m['form']
    qc = 0
    if form == 'long':
        esize = 8 << size
        ew = 2 * esize
        total = 64 // esize
        for i in range(total):
            a = get_elem(d, m['nreg'], esize, i)
            b = get_elem(d, m['mreg'], esize, i)
            sa, sb = sext(a, esize), sext(b, esize)
            if opc == 0:
                r = (a + b) if U else (sa + sb)
            elif opc == 2:
                r = (a - b) if U else (sa - sb)
            elif opc == 5:
                cur = get_vec(d, m['dreg'], ew, i)
                r = cur + (abs(a - b) if U else abs(sa - sb))
            elif opc == 7:
                r = abs(a - b) if U else abs(sa - sb)
            elif opc == 8:
                p = (a * b) if U else (sa * sb)
                r = get_vec(d, m['dreg'], ew, i) + p
            elif opc == 0xa:
                p = (a * b) if U else (sa * sb)
                r = get_vec(d, m['dreg'], ew, i) - p
            elif opc == 0xc:
                r = (a * b) if U else (sa * sb)
            elif opc == 0xe:
                r = carryless8(a, b)
            else:
                p = sa * sb
                if opc == 9:
                    dd, s = dbl_sat(p, ew)
                    qc |= s
                    r, s = add_sat(sext(get_vec(d, m['dreg'], ew, i), ew), dd, ew)
                    qc |= s
                elif opc == 0xb:
                    dd, s = dbl_sat(p, ew)
                    qc |= s
                    r, s = sub_sat(sext(get_vec(d, m['dreg'], ew, i), ew), dd, ew)
                    qc |= s
                else:
                    r, s = dbl_sat(p, ew)
                    qc |= s
            set_vec(d, m['dreg'], ew, i, r)
    elif form == 'w':
        esize = 8 << size
        ew = 2 * esize
        for i in range(64 // esize):
            a = get_vec(d, m['nreg'], ew, i)
            b = get_elem(d, m['mreg'], esize, i)
            if opc == 1:
                r = (a + b) if U else (a + sext(b, esize))
            else:
                r = (a - b) if U else (a - sext(b, esize))
            set_vec(d, m['dreg'], ew, i, r)
    else:
        esize = 8 << size
        ew = 2 * esize
        for i in range(64 // esize):
            a = get_vec(d, m['nreg'], ew, i)
            b = get_vec(d, m['mreg'], ew, i)
            r = (a + b) if opc == 4 else (a - b)
            if U:
                r += 1 << (esize - 1)
            set_vec(d, m['dreg'], esize, i, (r & MASK64) >> esize)
    return qc


def exec_scalar(d, m):
    opc, U, size = m['opc'], m['U'], m['size']
    esize = 8 << size
    nd = 2 if (U and opc not in (2, 3, 6, 7, 0xa, 0xb)) else 1
    if opc in (2, 3, 6, 7, 0xa, 0xb):
        nd = 2
    qc = 0
    scalar = get_elem(d, m['sreg'], esize, m['lane'])
    if opc in (0, 4, 8):
        total = nd * (64 // esize)
        for i in range(total):
            a = get_vec(d, m['nreg'], esize, i)
            dv = get_vec(d, m['dreg'], esize, i)
            p = a * scalar
            r = dv + p if opc == 0 else dv - p if opc == 4 else p
            set_vec(d, m['dreg'], esize, i, r)
    elif opc in (0xc, 0xd):
        total = nd * (64 // esize)
        for i in range(total):
            a = sext(get_vec(d, m['nreg'], esize, i), esize)
            p = a * sext(scalar, esize)
            r, s = qdmulh(p, esize, opc == 0xd)
            qc |= s
            set_vec(d, m['dreg'], esize, i, r)
    elif opc in (1, 5, 9):
        total = nd * 2
        for i in range(total):
            a = get_vec(d, m['nreg'], 32, i)
            dv = get_vec(d, m['dreg'], 32, i)
            r = f32_op(a, scalar, dv, 1 if opc == 1 else 2 if opc == 5 else 0)
            set_vec(d, m['dreg'], 32, i, r)
    else:
        ew = 2 * esize
        total = 64 // esize
        for i in range(total):
            a = get_elem(d, m['nreg'], esize, i)
            sa, ss = sext(a, esize), sext(scalar, esize)
            if opc in (2, 6, 0xa):
                p = (a * scalar) if U else (sa * ss)
                cur = get_vec(d, m['dreg'], ew, i)
                r = cur + p if opc == 2 else cur - p if opc == 6 else p
            else:
                p = sa * ss
                if opc == 3:
                    dd, s = dbl_sat(p, ew)
                    qc |= s
                    r, s = add_sat(sext(get_vec(d, m['dreg'], ew, i), ew), dd, ew)
                    qc |= s
                elif opc == 7:
                    dd, s = dbl_sat(p, ew)
                    qc |= s
                    r, s = sub_sat(sext(get_vec(d, m['dreg'], ew, i), ew), dd, ew)
                    qc |= s
                else:
                    r, s = dbl_sat(p, ew)
                    qc |= s
            set_vec(d, m['dreg'], ew, i, r)
    return qc


EXEC = {
    'shift2': exec_shift2,
    'narrow': exec_narrow,
    'wide': exec_wide,
    'vcvt': exec_vcvt,
    'diff': exec_diff,
    'scalar': exec_scalar,
}

# ---------------------------------------------------------------------------------------------------------
# instruction list

INSNS = []


def add(name, asm, kind, **kw):
    INSNS.append(dict(name=name, asm=asm, kind=kind, **kw))


def add_shift_family(opc, base, types, shifts, u_values, q_values):
    for typ in types:
        for u in u_values:
            for q in q_values:
                for sh in shifts:
                    suffix = ('q' if q else 'd')
                    nm = f"{base}_{typ}_u{u}_q{q}_s{sh}".replace('.', '_')
                    asm = f"{base}.{typ} {suffix}0, {suffix}2, #{sh}"
                    add(nm, asm, 'shift2', opc=opc, U=u, esize={'8': 8, '16': 16, '32': 32, '64': 64}[typ],
                        shift=sh, Q=q, dreg=0, mreg=2)


# VSHR/VSRA/VRSHR/VRSRA: right shifts 1 and max for every size, both signs, D and Q
for opc, base in ((0, 'vshr'), (1, 'vsra'), (2, 'vrshr'), (3, 'vrsra')):
    for typ, esize in (('8', 8), ('16', 16), ('32', 32), ('64', 64)):
        for u in (0, 1):
            for q in (0, 1):
                for sh in (1, esize):
                    add(f"{base}_{typ}_u{u}_q{q}_s{sh}", f"{base}.{'u' if u else 's'}{typ} {'q' if q else 'd'}0, {'q1' if q else 'd2'}, #{sh}",
                        'shift2', opc=opc, U=u, esize=esize, shift=sh, Q=q, dreg=0, mreg=2)

# VSRI (right), VSHL immediate / VSLI (left)
for typ, esize in (('8', 8), ('16', 16), ('32', 32), ('64', 64)):
    for q in (0, 1):
        for sh in (1, esize):
            add(f"vsri_{typ}_q{q}_s{sh}", f"vsri.{typ} {'q' if q else 'd'}0, {'q1' if q else 'd2'}, #{sh}",
                'shift2', opc=4, U=1, esize=esize, shift=sh, Q=q, dreg=0, mreg=2)
        for sh in (0, esize - 1):
            add(f"vshl_{typ}_q{q}_s{sh}", f"vshl.i{typ} {'q' if q else 'd'}0, {'q1' if q else 'd2'}, #{sh}",
                'shift2', opc=5, U=0, esize=esize, shift=sh, Q=q, dreg=0, mreg=2)
            add(f"vsli_{typ}_q{q}_s{sh}", f"vsli.{typ} {'q' if q else 'd'}0, {'q1' if q else 'd2'}, #{sh}",
                'shift2', opc=5, U=1, esize=esize, shift=sh, Q=q, dreg=0, mreg=2)
        for sh in (0, 1, esize - 1):
            add(f"vqshlu_{typ}_q{q}_s{sh}", f"vqshlu.s{typ} {'q' if q else 'd'}0, {'q1' if q else 'd2'}, #{sh}",
                'shift2', opc=6, U=1, esize=esize, shift=sh, Q=q, dreg=0, mreg=2)
        for u in (0, 1):
            for sh in (0, 1, esize - 1):
                add(f"vqshl_{typ}_u{u}_q{q}_s{sh}", f"vqshl.{'u' if u else 's'}{typ} {'q' if q else 'd'}0, {'q1' if q else 'd2'}, #{sh}",
                    'shift2', opc=7, U=u, esize=esize, shift=sh, Q=q, dreg=0, mreg=2)

# narrowing: VSHRN/VRSHRN/VQSHRN/VQRSHRN/VQSHRUN/VQRSHRUN
for typ, src in (('16', 16), ('32', 32), ('64', 64)):
    for sh in (1, src // 2):
        add(f"vshrn_{typ}_s{sh}", f"vshrn.i{typ} d0, q2, #{sh}", 'narrow',
            opc=8, U=0, src_esize=src, shift=sh, round=0, dreg=0, mreg=4)
        add(f"vrshrn_{typ}_s{sh}", f"vrshrn.i{typ} d0, q2, #{sh}", 'narrow',
            opc=8, U=0, src_esize=src, shift=sh, round=1, dreg=0, mreg=4)
        add(f"vqshrun_{typ}_s{sh}", f"vqshrun.s{typ} d0, q2, #{sh}", 'narrow',
            opc=8, U=1, src_esize=src, shift=sh, round=0, dreg=0, mreg=4)
        add(f"vqrshrun_{typ}_s{sh}", f"vqrshrun.s{typ} d0, q2, #{sh}", 'narrow',
            opc=8, U=1, src_esize=src, shift=sh, round=1, dreg=0, mreg=4)
        for u in (0, 1):
            add(f"vqshrn_{typ}_u{u}_s{sh}", f"vqshrn.{'u' if u else 's'}{typ} d0, q2, #{sh}", 'narrow',
                opc=9, U=u, src_esize=src, shift=sh, round=0, dreg=0, mreg=4)
            add(f"vqrshrn_{typ}_u{u}_s{sh}", f"vqrshrn.{'u' if u else 's'}{typ} d0, q2, #{sh}", 'narrow',
                opc=9, U=u, src_esize=src, shift=sh, round=1, dreg=0, mreg=4)

# widening: VSHLL (by immediate) and VMOVL
for typ, src in (('8', 8), ('16', 16), ('32', 32)):
    for u in (0, 1):
        for sh in (1, src - 1):
            add(f"vshll_{typ}_u{u}_s{sh}", f"vshll.{'u' if u else 's'}{typ} q0, d2, #{sh}", 'wide',
                U=u, src_esize=src, shift=sh, dreg=0, mreg=2)
        add(f"vmovl_{typ}_u{u}", f"vmovl.{'u' if u else 's'}{typ} q0, d2", 'wide',
            U=u, src_esize=src, shift=0, dreg=0, mreg=2)

# VCVT fixed <-> float
for d, name in (('f2x', 'vcvt_s32_f32'), ('f2x', 'vcvt_u32_f32'), ('x2f', 'vcvt_f32_s32'), ('x2f', 'vcvt_f32_u32')):
    unsigned = 1 if ('u32' in name) else 0
    for fb in (1, 16, 32):
        asm = f"{'vcvt.s32.f32' if name == 'vcvt_s32_f32' else 'vcvt.u32.f32' if name == 'vcvt_u32_f32' else 'vcvt.f32.s32' if name == 'vcvt_f32_s32' else 'vcvt.f32.u32'} d0, d2, #{fb}"
        add(f"{name}_f{fb}", asm, 'vcvt', dir=d, U=unsigned, fbits=fb, Q=0, dreg=0, mreg=2)
    for q in (0, 1):
        asm = f"{'vcvt.s32.f32' if name == 'vcvt_s32_f32' else 'vcvt.u32.f32' if name == 'vcvt_u32_f32' else 'vcvt.f32.s32' if name == 'vcvt_f32_s32' else 'vcvt.f32.u32'} {'q' if q else 'd'}0, {'q1' if q else 'd2'}, #16"
        add(f"{name}_q{q}", asm, 'vcvt', dir=d, U=unsigned, fbits=16, Q=q, dreg=0, mreg=2)

# three registers of different lengths
for typ, size in (('8', 0), ('16', 1), ('32', 2)):
    for u in (0, 1):
        s = 'u' if u else 's'
        add(f"vaddl_{typ}_{s}", f"vaddl.{s}{typ} q0, d4, d6", 'diff', opc=0, U=u, size=size, form='long', dreg=0, nreg=4, mreg=6)
        add(f"vsubl_{typ}_{s}", f"vsubl.{s}{typ} q0, d4, d6", 'diff', opc=2, U=u, size=size, form='long', dreg=0, nreg=4, mreg=6)
        add(f"vabal_{typ}_{s}", f"vabal.{s}{typ} q0, d4, d6", 'diff', opc=5, U=u, size=size, form='long', dreg=0, nreg=4, mreg=6)
        add(f"vabdl_{typ}_{s}", f"vabdl.{s}{typ} q0, d4, d6", 'diff', opc=7, U=u, size=size, form='long', dreg=0, nreg=4, mreg=6)
        add(f"vmlal_{typ}_{s}", f"vmlal.{s}{typ} q0, d4, d6", 'diff', opc=8, U=u, size=size, form='long', dreg=0, nreg=4, mreg=6)
        add(f"vmlsl_{typ}_{s}", f"vmlsl.{s}{typ} q0, d4, d6", 'diff', opc=0xa, U=u, size=size, form='long', dreg=0, nreg=4, mreg=6)
        add(f"vmull_{typ}_{s}", f"vmull.{s}{typ} q0, d4, d6", 'diff', opc=0xc, U=u, size=size, form='long', dreg=0, nreg=4, mreg=6)
        add(f"vaddw_{typ}_{s}", f"vaddw.{s}{typ} q0, q1, d6", 'diff', opc=1, U=u, size=size, form='w', dreg=0, nreg=2, mreg=6)
        add(f"vsubw_{typ}_{s}", f"vsubw.{s}{typ} q0, q1, d6", 'diff', opc=3, U=u, size=size, form='w', dreg=0, nreg=2, mreg=6)

add("vmull_p8", "vmull.p8 q0, d4, d6", 'diff', opc=0xe, U=0, size=0, form='long', dreg=0, nreg=4, mreg=6)

for typ, size in (('16', 1), ('32', 2)):
    add(f"vqdmlal_{typ}", f"vqdmlal.s{typ} q0, d4, d6", 'diff', opc=9, U=0, size=size, form='long', dreg=0, nreg=4, mreg=6)
    add(f"vqdmlsl_{typ}", f"vqdmlsl.s{typ} q0, d4, d6", 'diff', opc=0xb, U=0, size=size, form='long', dreg=0, nreg=4, mreg=6)
    add(f"vqdmull_{typ}", f"vqdmull.s{typ} q0, d4, d6", 'diff', opc=0xd, U=0, size=size, form='long', dreg=0, nreg=4, mreg=6)

for typ, size in (('16', 0), ('32', 1), ('64', 2)):
    for opc, base in ((4, 'vaddhn'), (6, 'vsubhn')):
        add(f"{base}_{typ}", f"{base}.i{typ} d0, q1, q2", 'diff', opc=opc, U=0, size=size, form='hn', dreg=0, nreg=2, mreg=4)
        add(f"vr{base[1:]}_{typ}", f"vr{base[1:]}.i{typ} d0, q1, q2", 'diff', opc=opc, U=1, size=size, form='hn', dreg=0, nreg=2, mreg=4)

# two registers and a scalar: short integer
for typ, size in (('16', 1), ('32', 2)):
    for opc, base in ((0, 'vmla'), (4, 'vmls'), (8, 'vmul')):
        for q in (0, 1):
            for lane in (0, 3 if size == 1 else 1):
                add(f"{base}_{typ}_q{q}_l{lane}", f"{base}.i{typ} {'q' if q else 'd'}0, {'q' if q else 'd'}4, d6[{lane}]",
                    'scalar', opc=opc, U=q, size=size, lane=lane, sreg=6, dreg=0, nreg=8 if q else 4)
    for opc, base in ((0xc, 'vqdmulh'), (0xd, 'vqrdmulh')):
        for q in (0, 1):
            for lane in (0, 3 if size == 1 else 1):
                add(f"{base}_{typ}_q{q}_l{lane}", f"{base}.s{typ} {'q' if q else 'd'}0, {'q' if q else 'd'}4, d6[{lane}]",
                    'scalar', opc=opc, U=q, size=size, lane=lane, sreg=6, dreg=0, nreg=8 if q else 4)

# short float
for opc, base in ((1, 'vmla'), (5, 'vmls'), (9, 'vmul')):
    for q in (0, 1):
        for lane in (0, 1):
            add(f"{base}_f32_q{q}_l{lane}", f"{base}.f32 {'q' if q else 'd'}0, {'q' if q else 'd'}4, d6[{lane}]",
                'scalar', opc=opc, U=q, size=2, lane=lane, sreg=6, dreg=0, nreg=8 if q else 4)

# long scalar
for typ, size in (('16', 1), ('32', 2)):
    for u in (0, 1):
        s = 'u' if u else 's'
        for lane in (0, 3 if size == 1 else 1):
            add(f"vmlal_{typ}_{s}_l{lane}", f"vmlal.{s}{typ} q0, d4, d6[{lane}]", 'scalar',
                opc=2, U=u, size=size, lane=lane, sreg=6, dreg=0, nreg=4)
            add(f"vmlsl_{typ}_{s}_l{lane}", f"vmlsl.{s}{typ} q0, d4, d6[{lane}]", 'scalar',
                opc=6, U=u, size=size, lane=lane, sreg=6, dreg=0, nreg=4)
            add(f"vmull_{typ}_{s}_l{lane}", f"vmull.{s}{typ} q0, d4, d6[{lane}]", 'scalar',
                opc=0xa, U=u, size=size, lane=lane, sreg=6, dreg=0, nreg=4)
    for opc, base in ((3, 'vqdmlal'), (7, 'vqdmlsl'), (0xb, 'vqdmull')):
        for lane in (0, 3 if size == 1 else 1):
            add(f"{base}_{typ}_l{lane}", f"{base}.s{typ} q0, d4, d6[{lane}]", 'scalar',
                opc=opc, U=0, size=size, lane=lane, sreg=6, dreg=0, nreg=4)

# ---------------------------------------------------------------------------------------------------------
# deterministic vectors

def prng(state):
    state ^= (state << 13) & MASK64
    state ^= state >> 7
    state ^= (state << 17) & MASK64
    return state & MASK64


EDGE = [0x0000000000000000, 0xFFFFFFFFFFFFFFFF, 0x8000000000000000, 0x7FFFFFFFFFFFFFFF,
        0x0101010101010101, 0x8080808080808080, 0x0000000100000002, 0x0000000000000001]


def make_vectors(m, count=8):
    """Return a list of (in_regs[10], out_regs[10], fpscr_in, fpscr_out)."""
    vecs = []
    state = 0x9E3779B97F4A7C15 ^ zlib.crc32(m['name'].encode())
    for k in range(count):
        d = [0] * 10
        if k < len(EDGE):
            d[0] = EDGE[k]
            d[1] = EDGE[(k + 1) % len(EDGE)]
            d[2] = EDGE[(k + 2) % len(EDGE)]
            d[3] = EDGE[(k + 3) % len(EDGE)]
            d[4] = EDGE[(k + 4) % len(EDGE)]
            d[5] = EDGE[(k + 5) % len(EDGE)]
            d[6] = EDGE[(k + 6) % len(EDGE)]
            d[7] = EDGE[(k + 7) % len(EDGE)]
            d[8] = EDGE[(k + 2) % len(EDGE)]
            d[9] = EDGE[(k + 5) % len(EDGE)]
        else:
            for r in range(10):
                state = prng(state)
                d[r] = state
        din = d[:]
        qc = EXEC[m['kind']](d, m)
        fpscr_in = 0
        fpscr_out = (1 << 27) if qc else 0
        vecs.append((din, d[:], fpscr_in, fpscr_out))
    return vecs


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(outdir, exist_ok=True)
    lines = ['\t.text', '\t.arm']
    for m in INSNS:
        lines.append('\t@ ENC: ' + m['name'])
        lines.append('\t' + m['asm'])
    src = '\n'.join(lines) + '\n'
    sfile = os.path.join(outdir, 'enc_v19.s')
    with open(sfile, 'w') as f:
        f.write(src)
    p = subprocess.run(['llvm-mc', '-triple=armv8a-linux-gnueabi',
                        '-mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8', '--show-encoding', sfile],
                       capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stderr)
        sys.exit(1)
    words = []
    for line in p.stdout.splitlines():
        mt = re.search(r'encoding: \[(.*?)\]', line)
        if mt:
            b = [int(x, 16) for x in mt.group(1).split(',')]
            words.append(b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24)
    if len(words) != len(INSNS):
        sys.stderr.write(f"llvm-mc produced {len(words)} encodings for {len(INSNS)} instructions\n")
        sys.exit(1)

    hdr = ['/* generated by gen_vectors.py -- do not edit */',
           '#ifndef VEC_V19_H', '#define VEC_V19_H', '#include <stdint.h>', '']
    for m, w in zip(INSNS, words):
        hdr.append(f'#define ENC_{m["name"].upper()} 0x{w:08x}u')
    hdr.append('')
    hdr.append('struct v19_vec { const char *name; uint32_t insn; uint32_t fpscr_in; uint64_t in[10]; uint64_t out[10]; uint32_t fpscr_out; };')
    hdr.append('')
    allvecs = []
    for m, w in zip(INSNS, words):
        for (din, dout, fi, fo) in make_vectors(m):
            vals = ', '.join(f'0x{x:016x}ULL' for x in din)
            outs = ', '.join(f'0x{x:016x}ULL' for x in dout)
            allvecs.append(f'    {{ "{m["name"]}", 0x{w:08x}u, 0x{fi:08x}u, {{{vals}}}, {{{outs}}}, 0x{fo:08x}u }},')
    hdr.append('static const struct v19_vec v19_vecs[] = {')
    hdr.extend(allvecs)
    hdr.append('};')
    hdr.append(f'#define V19_VEC_COUNT {len(allvecs)}u')
    hdr.append('')
    hdr.append('#endif')
    with open(os.path.join(outdir, 'vec_v19.h'), 'w') as f:
        f.write('\n'.join(hdr) + '\n')
    print(f"gen_vectors: {len(INSNS)} encodings, {len(allvecs)} vectors")


if __name__ == '__main__':
    main()
