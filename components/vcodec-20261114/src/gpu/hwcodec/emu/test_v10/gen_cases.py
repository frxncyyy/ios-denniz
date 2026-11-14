#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# gen_cases.py -- case-table generator for the V10 tests (A32 data processing, moves,
# multiplies).  NOT run by run_tests.sh: it was used once to work out the expected
# register/flag values from the ARM ARM pseudocode and to emit
#   * encodings.txt  (label | assembly line; assembled by mkenc.py at test time)
#   * cases_v10.inc  (the case table; every expected value is a literal)
# The literals in cases_v10.inc are authoritative.  This script is kept so a reviewer
# can see how they were derived; it also re-checks each llvm-mc encoding's fields
# against the intended instruction.

import re
import subprocess
import sys

M = 0xffffffff
STD = [0x11111111, 0x12345678, 0x0000000f, 0x00000005, 0xffffffff, 0x80000000,
       0x00000000, 0x7fffffff, 0x00000020, 0x00000021, 0x000000ff, 0x00000001,
       0x0000001f, 0x00001000, 0x00002000]
PC = 0x1008
NEXT = 0x1004
R = ["r%d" % i for i in range(16)]
MN = [("and", "ands"), ("eor", "eors"), ("sub", "subs"), ("rsb", "rsbs"),
      ("add", "adds"), ("adc", "adcs"), ("sbc", "sbcs"), ("rsc", "rscs"),
      ("tst", "tst"), ("teq", "teq"), ("cmp", "cmp"), ("cmn", "cmn"),
      ("orr", "orrs"), ("mov", "movs"), ("bic", "bics"), ("mvn", "mvns")]
SHN = ["lsl", "lsr", "asr", "ror"]

cases = []


def ror32(v, n):
    n &= 31
    return ((v >> n) | (v << (32 - n))) & M if n else v


def expand_imm(imm12, cin):
    rot = (imm12 >> 8) & 15
    v = imm12 & 0xff
    if rot == 0:
        return v, cin
    v = ror32(v, 2 * rot)
    return v, (v >> 31) & 1


def imm12_of(value):
    for rot in range(16):
        r = 2 * rot
        imm8 = ((value << r) | (value >> (32 - r))) & M if r else value
        if imm8 <= 0xff:
            return (rot << 8) | imm8
    return None


def shift_imm(v, typ, amt, cin):
    if typ == 0:
        if amt == 0:
            return v, cin
        return (v << amt) & M, (v >> (32 - amt)) & 1
    if typ == 1:
        if amt == 0:
            return 0, (v >> 31) & 1
        return v >> amt, (v >> (amt - 1)) & 1
    if typ == 2:
        if amt == 0:
            return (M if (v >> 31) else 0), (v >> 31) & 1
        r = v >> amt
        if v >> 31:
            r |= (M << (32 - amt)) & M
        return r, (v >> (amt - 1)) & 1
    if amt == 0:                                   # RRX
        return ((cin << 31) | (v >> 1)) & M, v & 1
    return ror32(v, amt), (v >> (amt - 1)) & 1


def shift_reg(v, typ, amt, cin):
    if amt == 0:
        return v, cin
    if typ == 0:
        if amt < 32:
            return (v << amt) & M, (v >> (32 - amt)) & 1
        return 0, (v & 1) if amt == 32 else 0
    if typ == 1:
        if amt < 32:
            return v >> amt, (v >> (amt - 1)) & 1
        return 0, ((v >> 31) & 1) if amt == 32 else 0
    if typ == 2:
        if amt < 32:
            r = v >> amt
            if v >> 31:
                r |= (M << (32 - amt)) & M
            return r, (v >> (amt - 1)) & 1
        return (M if (v >> 31) else 0), (v >> 31) & 1
    n = amt & 31
    if n == 0:
        return v, (v >> 31) & 1
    return ror32(v, n), (v >> (n - 1)) & 1


def addc(x, y, cin):
    u = x + y + cin
    r = u & M
    return r, (u >> 32) & 1, ((~(x ^ y) & (x ^ r)) >> 31) & 1


def asm_of(c):
    f = c["form"]
    op = c.get("op")
    s = c.get("s", 0)
    if f in ("imm", "reg", "regshift"):
        if op in (8, 9, 10, 11):
            mn = MN[op][0]
            head = "%s %s" % (mn, R[c["rn"]])
        elif op in (13, 15):
            head = "%s %s" % (MN[op][s], R[c["rd"]])
        else:
            head = "%s %s, %s" % (MN[op][s], R[c["rd"]], R[c["rn"]])
        if f == "imm":
            return "%s, #0x%x" % (head, c["imm"])
        if op in (13, 15):
            head = "%s %s" % (MN[op][s], R[c["rd"]])
        if f == "reg":
            if c["sh"] is None:
                return "%s, %s" % (head, R[c["rm"]])
            t, amt = c["sh"]
            if t == 3 and amt == 0:
                return "%s, %s, rrx" % (head, R[c["rm"]])
            return "%s, %s, %s #%d" % (head, R[c["rm"]], SHN[t], amt)
        t = c["sh"]
        return "%s, %s, %s %s" % (head, R[c["rm"]], SHN[t], R[c["rs"]])
    if f == "movw":
        return "movw %s, #0x%x" % (R[c["rd"]], c["imm"])
    if f == "movt":
        return "movt %s, #0x%x" % (R[c["rd"]], c["imm"])
    if f in ("mul", "mla", "mls"):
        return "%s%s %s, %s, %s%s" % (f, "s" if s else "", R[c["rd"]], R[c["rm"]],
                                      R[c["rs"]],
                                      "" if f == "mul" else ", " + R[c["ra"]])
    if f in ("umull", "umlal", "smull", "smlal"):
        return "%s%s %s, %s, %s, %s" % (f, "s" if s else "", R[c["ra"]], R[c["rd"]],
                                        R[c["rm"]], R[c["rs"]])
    if f == "umaal":
        return "umaal %s, %s, %s, %s" % (R[c["ra"]], R[c["rd"]], R[c["rm"]], R[c["rs"]])
    if f == "mrs":
        return "mrs %s, %s" % (R[c["rd"]], "cpsr" if c["cpsr"] else "spsr")
    if f == "msr":
        return "msr %s, %s" % (c["target"], R[c["rm"]])
    if f == "clz":
        return "clz %s, %s" % (R[c["rd"]], R[c["rm"]])
    raise AssertionError(f)


def add(label, form, asm=None, **kw):
    c = dict(label=label, form=form, **kw)
    if asm is None:
        asm = asm_of(c)
    c["asm"] = asm
    cases.append(c)
    return c


# ---------------------------------------------------------------- ALU immediate
IMMS = [
    ("i0", 0xff, 0, 1, 0, (0, 0, 0, 0, 0, 0, 0)),
    ("i1", 0x80000000, 1, 5, 1, (0, 0, 0, 0, 0, 0, 0)),
    ("i2", 0xf000000f, 1, 4, 1, (0, 0, 0, 0, 0, 0, 0)),
    ("i3", 0x1, 1, 7, 1, (0, 0, 0, 0, 0, 0, 0)),
    ("i4", 0x0, 1, 6, 1, (0, 0, 0, 0, 0, 0, 0)),
    ("i5", 0x4, 0, 13, 0, (0, 0, 0, 0, 0, 0, 0)),
    ("i6", 0x1, 1, 6, 1, (0, 0, 0, 0, 0, 0, 0)),
    ("i7", 0x80000000, 1, 1, 1, (1, 0, 1, 0, 0, 0, 0)),   # C-in = 1
    ("i8", 0x00ff0000, 1, 1, 1, (0, 0, 0, 0, 0, 0, 0)),   # rotation, C-out = 0
]
for op in range(16):
    for (tag, imm, s, rn, rd, fin) in IMMS:
        if tag == "i7" and op not in (5, 6, 7):
            continue
        add("%s_%s" % (MN[op][0], tag), "imm", op=op, rd=rd, rn=rn,
            s=(1 if op in (8, 9, 10, 11) else s), imm=imm, fin=fin)

# ---------------------------------------------------------------- ALU register (immediate shift)
REGS = [
    ("r0", 0, 1, None, 0, (0, 0, 0, 0, 0, 0, 0)),          # plain register
    ("r1", 2, 3, (0, 3), 1, (0, 0, 0, 0, 0, 0, 0)),        # LSL #3
    ("r2", 4, 1, (1, 1), 1, (0, 0, 0, 0, 0, 0, 0)),        # LSR #1
    ("r3", 5, 31, (2, 31), 1, (0, 0, 0, 0, 0, 0, 0)),      # ASR #31
    ("r4", 3, 8, (3, 8), 0, (0, 0, 0, 0, 0, 0, 0)),        # ROR #8
    ("r5", 2, 0, (3, 0), 1, (1, 0, 1, 0, 0, 0, 0)),        # RRX, C-in 1
    ("r6", 1, 0, (0, 0), 0, (1, 0, 1, 0, 0, 0, 0)),        # LSL #0, C-in 1
    ("r7", 4, 32, (1, 32), 1, (0, 0, 0, 0, 0, 0, 0)),       # LSR #32
    ("r8", 5, 32, (2, 32), 1, (0, 0, 0, 0, 0, 0, 0)),       # ASR #32
    ("r9", 1, 31, (0, 31), 1, (0, 0, 0, 0, 0, 0, 0)),      # LSL #31
    ("ra", 2, 0, (3, 0), 1, (0, 0, 0, 0, 0, 0, 0)),        # RRX, C-in 0
]
for op in range(16):
    for (tag, rm, amt, sh, s, fin) in REGS:
        add("%s_%s" % (MN[op][0], tag), "reg", op=op, rd=0, rn=1,
            s=(1 if op in (8, 9, 10, 11) else s), rm=rm, sh=sh, fin=fin)

# ---------------------------------------------------------------- ALU register (register shift)
RREGS = [
    ("s0", 2, 3, (0, 3, 1), (0, 0, 0, 0, 0, 0, 0)),        # LSL by 5
    ("s1", 1, 8, (1, 32, 1), (0, 0, 0, 0, 0, 0, 0)),       # LSR by 32
    ("s2", 5, 9, (2, 33, 1), (0, 0, 0, 0, 0, 0, 0)),       # ASR by 33
    ("s3", 1, 10, (3, 255, 1), (0, 0, 0, 0, 0, 0, 0)),     # ROR by 255
    ("s4", 1, 6, (0, 0, 1), (1, 0, 1, 0, 0, 0, 0)),        # LSL by 0, C-in 1
    ("s5", 2, 8, (1, 32, 1), (0, 0, 0, 0, 0, 0, 0)),       # LSR by 32
    ("s6", 5, 8, (3, 32, 1), (0, 0, 0, 0, 0, 0, 0)),       # ROR by 32 (amount mod 32 == 0)
    ("s7", 1, 9, (0, 33, 1), (0, 0, 0, 0, 0, 0, 0)),       # LSL by 33 (> 32)
    ("s8", 4, 9, (1, 33, 1), (0, 0, 0, 0, 0, 0, 0)),       # LSR by 33 (> 32)
]
for op in range(16):
    for (tag, rm, rs, (t, amt, s), fin) in RREGS:
        add("%s_%s" % (MN[op][0], tag), "regshift", op=op, rd=0, rn=1,
            s=(1 if op in (8, 9, 10, 11) else s), rm=rm, rs=rs, sh=t, fin=fin)

# ---------------------------------------------------------------- r15 as ALU data operand
# ARM ARM calls these UNPREDICTABLE; the interpreter reads c->r[15] (documented choice).
add("pc_rm", "reg", op=4, rd=0, rn=1, s=0, rm=15, sh=None, fin=(0, 0, 0, 0, 0, 0, 0))
add("pc_rn", "reg", op=4, rd=0, rn=15, s=0, rm=1, sh=None, fin=(0, 0, 0, 0, 0, 0, 0))

# ---------------------------------------------------------------- ADR and PC writes
add("adr_add8", "imm", op=4, rd=0, rn=15, s=0, imm=8, fin=(0, 0, 0, 0, 0, 0, 0))
add("adr_sub4", "imm", op=2, rd=0, rn=15, s=0, imm=4, fin=(0, 0, 0, 0, 0, 0, 0))
add("adr_big", "imm", op=4, rd=1, rn=15, s=0, imm=0x80000000, fin=(0, 0, 0, 0, 0, 0, 0))
add("adr_sp", "imm", op=4, rd=13, rn=15, s=0, imm=16, fin=(0, 0, 0, 0, 0, 0, 0))
add("adr_adds", "imm", op=4, rd=14, rn=15, s=1, imm=4, fin=(0, 0, 1, 0, 0, 0, 0))
add("adr_neg", "imm", op=2, rd=0, rn=15, s=0, imm=0x80000000, fin=(0, 0, 0, 0, 0, 0, 0))
add("br_mov", "reg", op=13, rd=15, s=0, rm=0, sh=None, sets=[(0, 0x2000)], fin=(0, 0, 0, 0, 0, 0, 0))
add("br_mov_thumb", "reg", op=13, rd=15, s=0, rm=0, sh=None, sets=[(0, 0x2001)], fin=(0, 0, 0, 0, 0, 0, 0))
add("br_add", "imm", op=4, rd=15, rn=1, s=0, imm=4, fin=(0, 0, 0, 0, 0, 0, 0))
add("br_orr", "imm", op=12, rd=15, rn=0, s=0, imm=1, sets=[(0, 0x2000)], fin=(0, 0, 0, 0, 0, 0, 0))
add("br_bic", "imm", op=14, rd=15, rn=0, s=0, imm=3, sets=[(0, 0x2007)], fin=(0, 0, 0, 0, 0, 0, 0))

# ---------------------------------------------------------------- MOVW / MOVT
for (tag, form, rd, imm, sets) in [
        ("w0", "movw", 0, 0x1234, []), ("w1", "movw", 0, 0x0, []),
        ("w2", "movw", 7, 0xffff, []), ("w3", "movw", 13, 0x8000, []),
        ("w4", "movw", 1, 0x0001, []), ("w5", "movw", 14, 0xf0f0, []),
        ("t0", "movt", 0, 0x1234, []), ("t1", "movt", 0, 0x0, []),
        ("t2", "movt", 7, 0xffff, []), ("t3", "movt", 13, 0xabcd, []),
        ("t4", "movt", 4, 0x0001, []), ("t5", "movt", 1, 0x8000, [])]:
    add("mov_%s" % tag, form, rd=rd, imm=imm, sets=sets, fin=(0, 0, 0, 0, 0, 0, 0))

# ---------------------------------------------------------------- multiplies
MULS = [
    ("m0", "mul", 0, None, 1, 2, 0, (0, 0, 0, 0, 0, 0, 0), []),
    ("m1", "mul", 0, None, 4, 4, 1, (0, 0, 0, 0, 0, 0, 0), []),
    ("m2", "mul", 0, None, 6, 1, 1, (0, 0, 0, 0, 0, 0, 0), []),
    ("m3", "mul", 0, None, 5, 5, 1, (0, 0, 0, 0, 0, 0, 0), []),
    ("m4", "mul", 0, None, 4, 5, 1, (0, 0, 0, 0, 0, 0, 0), []),
    ("m5", "mul", 0, None, 7, 3, 0, (1, 0, 1, 1, 1, 0, 0), []),
    ("a0", "mla", 0, 3, 1, 2, 0, (0, 0, 0, 0, 0, 0, 0), []),
    ("a1", "mla", 0, 5, 4, 4, 1, (0, 0, 0, 0, 0, 0, 0), []),
    ("a2", "mla", 0, 2, 6, 1, 1, (1, 1, 1, 1, 0, 0, 0), []),
    ("a3", "mla", 0, 4, 4, 4, 1, (0, 0, 0, 0, 0, 0, 0), []),
    ("a4", "mla", 0, 7, 5, 5, 1, (0, 0, 0, 0, 0, 0, 0), []),
    ("a5", "mla", 13, 1, 2, 3, 0, (0, 0, 1, 0, 0, 0, 0), []),
    ("l0", "mls", 0, 3, 1, 2, 0, (0, 0, 0, 0, 0, 0, 0), []),
    ("l1", "mls", 0, 6, 5, 5, 0, (0, 0, 1, 1, 1, 0, 0), []),
    ("l2", "mls", 0, 6, 4, 4, 0, (0, 0, 0, 0, 0, 0, 0), []),
    ("l3", "mls", 0, 4, 6, 1, 0, (0, 0, 0, 0, 0, 0, 0), []),
    ("l4", "mls", 0, 7, 2, 3, 0, (0, 0, 0, 0, 0, 0, 0), []),
    ("l5", "mls", 14, 1, 2, 3, 0, (0, 0, 0, 0, 0, 0, 0), []),
]
for (tag, form, rd, ra, rm, rs, s, fin, sets) in MULS:
    add("%s_%s" % (form, tag), form, rd=rd, ra=ra, rm=rm, rs=rs, s=s, fin=fin, sets=sets)

LONGS = [
    ("u0", "umull", 0, 1, 2, 3, 0, []), ("u1", "umull", 0, 1, 4, 4, 1, []),
    ("u2", "umull", 0, 1, 6, 4, 1, []), ("u3", "umull", 0, 1, 5, 5, 0, []),
    ("u4", "umull", 13, 14, 1, 2, 0, []), ("u5", "umull", 0, 1, 7, 7, 1, []),
    ("ua0", "umlal", 0, 1, 2, 3, 0, []), ("ua1", "umlal", 0, 1, 4, 4, 1, []),
    ("ua2", "umlal", 0, 1, 6, 1, 1, [(0, 0), (1, 0)]),
    ("ua3", "umlal", 0, 1, 5, 5, 0, []), ("ua4", "umlal", 13, 14, 1, 2, 0, []),
    ("ua5", "umlal", 0, 1, 7, 7, 1, []),
    ("s0", "smull", 0, 1, 4, 4, 0, []), ("s1", "smull", 0, 1, 5, 4, 1, []),
    ("s2", "smull", 0, 1, 6, 1, 1, []), ("s3", "smull", 0, 1, 7, 7, 0, []),
    ("s4", "smull", 13, 14, 5, 5, 0, []), ("s5", "smull", 0, 1, 4, 7, 1, []),
    ("sa0", "smlal", 0, 1, 2, 3, 0, []), ("sa1", "smlal", 0, 1, 4, 4, 1, []),
    ("sa2", "smlal", 0, 1, 5, 4, 1, []), ("sa3", "smlal", 0, 1, 6, 1, 1, [(0, 0), (1, 0)]),
    ("sa4", "smlal", 13, 14, 1, 2, 0, []), ("sa5", "smlal", 0, 1, 7, 7, 1, []),
    ("um0", "umaal", 0, 1, 2, 3, 0, []), ("um1", "umaal", 0, 1, 4, 4, 0, []),
    ("um2", "umaal", 0, 1, 6, 1, 0, [(0, 0), (1, 0)]),
    ("um3", "umaal", 0, 1, 5, 5, 0, []), ("um4", "umaal", 13, 14, 1, 2, 0, []),
    ("um5", "umaal", 0, 1, 7, 7, 0, [(0, 0xffffffff), (1, 0xffffffff)]),
]
for (tag, form, lo, hi, rm, rs, s, sets) in LONGS:
    add("%s_%s" % (form, tag), form, ra=lo, rd=hi, rm=rm, rs=rs, s=s, sets=sets,
        fin=(0, 0, 0, 0, 0, 0, 0))

# ---------------------------------------------------------------- MRS / MSR / CLZ
MRS = [
    ("q0", 0, (1, 0, 1, 1, 1, 0xa, 0)),
    ("q1", 1, (0, 0, 0, 0, 0, 0, 0)),
    ("q2", 2, (0, 0, 0, 0, 0, 1, 0)),
    ("q3", 13, (0, 1, 0, 0, 0, 0xf, 0)),
    ("q4", 0, (1, 1, 1, 1, 1, 0xf, 1)),
    ("q5", 14, (0, 0, 1, 0, 0, 0, 0)),
]
for (tag, rd, fin) in MRS:
    add("mrs_%s" % tag, "mrs", rd=rd, cpsr=1, fin=fin)

MSR = [
    ("a0", "apsr_nzcvq", 0, (0, 0, 0, 0, 0, 0, 0), [(0, 0xf0000000)]),
    ("a1", "apsr_nzcvq", 1, (0, 0, 0, 0, 0, 0, 0), [(1, 0x12345678)]),
    ("a2", "apsr_g", 5, (0, 0, 0, 0, 0, 0, 0), [(5, 0x80000000)]),
    ("a3", "apsr_g", 1, (1, 0, 1, 0, 1, 0, 0), [(1, 0x12345678)]),
    ("a4", "apsr_nzcvqg", 4, (0, 0, 0, 0, 0, 0, 0), [(4, 0xffffffff)]),
    ("a5", "apsr_nzcvq", 13, (1, 0, 0, 1, 1, 0x5, 0), [(13, 0x70000000)]),
]
for (tag, target, rm, fin, sets) in MSR:
    add("msr_%s" % tag, "msr", target=target, rm=rm, fin=fin, sets=sets)

CLZ = [("m0", 0, 1), ("m1", 0, 6), ("m2", 0, 5), ("m3", 0, 4), ("m4", 0, 2),
       ("m5", 13, 14)]
for (tag, rd, rm) in CLZ:
    add("clz_%s" % tag, "clz", rd=rd, rm=rm, fin=(1, 1, 1, 1, 1, 0, 0))

# ---------------------------------------------------------------- must be UNDEF
UNDEF = [
    ("bx", "bx r0"), ("blx", "blx r0"), ("qadd", "qadd r0, r1, r2"),
    ("qsub", "qsub r0, r1, r2"), ("qdadd", "qdadd r0, r1, r2"),
    ("ldrh", "ldrh r0, [r1]"), ("strh", "strh r0, [r1]"), ("ldrsb", "ldrsb r0, [r1]"),
    ("ldr", "ldr r0, [r1]"), ("str", "str r0, [r1]"), ("ldrd", "ldrd r0, r1, [r2]"),
    ("strd", "strd r0, r1, [r2]"), ("nop", "nop"), ("yield", "yield"),
    ("wfe", "wfe"), ("wfi", "wfi"), ("sev", "sev"), ("dbg", "dbg #0"),
    ("msri", "msr apsr_nzcvq, #0"), ("msrs", "msr spsr_f, r0"),
    ("msrall", "msr cpsr_fsxc, r0"), ("spsr", "mrs r0, spsr"),
    ("pld", "pld [r0]"), ("movs_pc", "movs pc, lr"), ("adds_pc", "adds pc, r1, #4"), ("b", "b #0"), ("bl", "bl #0"), ("svc", "svc #0"),
    ("bkpt", "bkpt #0"), ("udf", "udf #0"), ("sdiv", "sdiv r0, r1, r2"),
    ("udiv", "udiv r0, r1, r2"), ("rev", "rev r0, r1"), ("sxtb", "sxtb r0, r1"),
    ("pkh", "pkhbt r0, r1, r2"), ("sel", "sel r0, r1, r2"),
    ("ssat", "ssat r0, #1, r1"), ("usat", "usat r0, #1, r1"),
    ("swp", "swp r0, r1, [r2]"), ("mrc", "mrc p15, 0, r0, c13, c0, 3"),
    ("clrex", "clrex"),
    ("ldrex", "ldrex r0, [r1]"), ("strex", "strex r0, r1, [r2]"),
    ("ldm", "ldm r0, {r1}"), ("stm", "stm r0, {r1}"),
    ("pop", "pop {r0}"), ("push", "push {r0}"),
]
undef_cases = list(UNDEF)

# ---------------------------------------------------------------- assemble once
def assemble(lines):
    text = "\t.syntax unified\n\t.arm\n\t.text\n" + "".join("\t%s\n" % a for a in lines)
    cmd = ["llvm-mc", "-triple=armv7a-linux-gnueabi",
           "-mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8", "--show-encoding"]
    p = subprocess.run(cmd, input=text, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout + p.stderr)
        raise SystemExit("llvm-mc failed")
    encs = []
    for line in p.stdout.splitlines():
        m = re.search(r"@ encoding: \[([0-9a-fx,]+)\]", line)
        if m:
            b = [int(x, 16) for x in m.group(1).split(",")]
            encs.append(b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24)
    return encs


all_cases = cases + [dict(label="u_" + t[0], form="undef", asm=t[1]) for t in UNDEF]
encs = assemble([c["asm"] for c in all_cases])
if len(encs) != len(all_cases):
    raise SystemExit("assembled %d encodings for %d cases" % (len(encs), len(all_cases)))
for c, e in zip(all_cases, encs):
    c["insn"] = e


# ---------------------------------------------------------------- model
def check_alu_encoding(c, insn):
    assert (insn >> 28) != 0xf, c
    op = (insn >> 21) & 15
    s = (insn >> 20) & 1
    assert op == c["op"], (c, hex(insn))
    assert s == c.get("s", 0), (c, hex(insn))
    if c["form"] == "imm":
        assert (insn >> 25) & 1 == 1
        assert expand_imm(insn & 0xfff, 0)[0] == c["imm"], (c, hex(insn))
        c["imm12"] = insn & 0xfff
        if c["op"] not in (8, 9, 10, 11, 13, 15):
            assert ((insn >> 16) & 15) == c["rn"], (c, hex(insn))
        if c["op"] not in (8, 9, 10, 11):
            assert ((insn >> 12) & 15) == c["rd"], (c, hex(insn))
    else:
        assert (insn >> 25) & 1 == 0
        if c["op"] not in (8, 9, 10, 11):
            assert ((insn >> 12) & 15) == c["rd"], (c, hex(insn))
        if c["op"] not in (8, 9, 10, 11, 13, 15):
            assert ((insn >> 16) & 15) == c["rn"], (c, hex(insn))
        if c["form"] == "reg":
            assert (insn >> 4) & 1 == 0
            if c["sh"] is not None:
                assert ((insn >> 5) & 3) == c["sh"][0], (c, hex(insn))
                if c["sh"][1] == 32:
                    assert ((insn >> 7) & 31) == 0, (c, hex(insn))
                else:
                    assert ((insn >> 7) & 31) == c["sh"][1], (c, hex(insn))
            else:
                assert ((insn >> 5) & 3) == 0 and ((insn >> 7) & 31) == 0, (c, hex(insn))
            assert (insn & 15) == c["rm"], (c, hex(insn))
        else:
            assert (insn >> 4) & 1 == 1
            assert (insn >> 7) & 1 == 0
            assert ((insn >> 8) & 15) == c["rs"], (c, hex(insn))
            assert ((insn >> 5) & 3) == c["sh"]
            assert (insn & 15) == c["rm"], (c, hex(insn))


def model(c):
    r = list(STD) + [PC]
    for (i, v) in c.get("sets", []):
        r[i] = v
    n, z, cf, v, q, ge, thumb = c.get("fin", (0,) * 7)
    nextpc = NEXT
    form = c["form"]
    if form == "undef":
        return None
    if form in ("imm", "reg", "regshift"):
        op = c["op"]
        s = c.get("s", 0)
        check_alu_encoding(c, c["insn"])
        insn = c["insn"]
        if form == "imm":
            op2, carry = expand_imm(insn & 0xfff, cf)
        elif form == "reg":
            if c["sh"] is None:
                op2, carry = r[insn & 15], cf
            else:
                t, amt = c["sh"]
                enc_amt = 0 if amt == 32 else amt
                op2, carry = shift_imm(r[insn & 15], t, enc_amt, cf)
        else:
            t = c["sh"]
            op2, carry = shift_reg(r[insn & 15], t, r[(insn >> 8) & 15] & 0xff, cf)
        a = r[(insn >> 16) & 15]
        if op == 0:
            res = a & op2
        elif op == 1:
            res = a ^ op2
        elif op == 2:
            res, carry, ov = addc(a, (~op2) & M, 1)
        elif op == 3:
            res, carry, ov = addc(op2, (~a) & M, 1)
        elif op == 4:
            res, carry, ov = addc(a, op2, 0)
        elif op == 5:
            res, carry, ov = addc(a, op2, cf)
        elif op == 6:
            res, carry, ov = addc(a, (~op2) & M, cf)
        elif op == 7:
            res, carry, ov = addc(op2, (~a) & M, cf)
        elif op in (8, 9):
            res = (a & op2) if op == 8 else (a ^ op2)
        elif op in (10, 11):
            res, carry, ov = addc(a, (~op2) & M, 1) if op == 10 else addc(a, op2, 0)
        elif op == 12:
            res = a | op2
        elif op == 13:
            res = op2
        elif op == 14:
            res = a & ((~op2) & M)
        else:
            res = (~op2) & M
        if s:
            n = (res >> 31) & 1
            z = 1 if res == 0 else 0
            cf = carry & 1
            if op not in (0, 1, 8, 9, 12, 13, 14, 15):
                v = ov
        if op in (8, 9, 10, 11):
            pass
        elif ((insn >> 12) & 15) == 15:
            if s:
                return None                      # ALUExceptionReturn: UNDEF
            if res & 1:
                thumb = 1
                nextpc = res & ~1
            else:
                thumb = 0
                nextpc = res & ~3
        else:
            r[(insn >> 12) & 15] = res
        return r, (n, z, cf, v, q, ge), nextpc, thumb

    if form in ("movw", "movt"):
        rd = c["rd"]
        imm = c["imm"]
        if form == "movw":
            r[rd] = imm
        else:
            r[rd] = (r[rd] & 0xffff) | (imm << 16)
        return r, (n, z, cf, v, q, ge), nextpc, thumb

    insn = c["insn"]
    if form in ("mul", "mla", "mls"):
        rd, rs, rm = c["rd"], c["rs"], c["rm"]
        prod = r[rm] * r[rs]
        if form == "mul":
            res = prod & M
        elif form == "mla":
            res = (prod + r[c["ra"]]) & M
        else:
            res = (r[c["ra"]] - prod) & M
        r[rd] = res
        if c.get("s"):
            n = (res >> 31) & 1
            z = 1 if res == 0 else 0
        return r, (n, z, cf, v, q, ge), nextpc, thumb

    if form in ("umull", "umlal", "smull", "smlal", "umaal"):
        lo, hi = c["ra"], c["rd"]
        if form in ("smull", "smlal"):
            a = r[c["rm"]]
            b = r[c["rs"]]
            sa = a - (1 << 32) if a >> 31 else a
            sb = b - (1 << 32) if b >> 31 else b
            prod = (sa * sb) & ((1 << 64) - 1)
        else:
            prod = r[c["rm"]] * r[c["rs"]]
        if form in ("umlal", "smlal"):
            prod = (prod + ((r[hi] << 32) | r[lo])) & ((1 << 64) - 1)
        if form == "umaal":
            total = (prod + r[lo] + r[hi]) & ((1 << 64) - 1)
            r[lo] = total & M
            r[hi] = (total >> 32) & M
        else:
            r[lo] = prod & M
            r[hi] = (prod >> 32) & M
            if c.get("s"):
                n = (prod >> 63) & 1
                z = 1 if prod == 0 else 0
        return r, (n, z, cf, v, q, ge), nextpc, thumb

    if form == "mrs":
        if c["rd"] == 15 or not c["cpsr"]:
            return None
        val = ((n << 31) | (z << 30) | (cf << 29) | (v << 28) | (q << 27) |
               (ge << 16) | 0x1d0)
        if thumb:
            val |= 0x20
        r[c["rd"]] = val
        return r, (n, z, cf, v, q, ge), nextpc, thumb

    if form == "msr":
        tgt = c["target"]
        mask = {"apsr_nzcvq": 8, "apsr_g": 4, "apsr_nzcvqg": 12}[tgt]
        val = r[c["rm"]]
        if mask & 8:
            n, z, cf, v, q = [(val >> b) & 1 for b in (31, 30, 29, 28, 27)]
        if mask & 4:
            ge = (val >> 16) & 15
        return r, (n, z, cf, v, q, ge), nextpc, thumb

    if form == "clz":
        if c["rd"] == 15 or c["rm"] == 15:
            return None
        val = r[c["rm"]]
        cnt = 32 if val == 0 else 32 - val.bit_length()
        r[c["rd"]] = cnt
        return r, (n, z, cf, v, q, ge), nextpc, thumb

    raise AssertionError(form)


def emit():
    out = []
    for c in cases:
        m = model(c)
        if m is None:
            raise SystemExit("case %s unexpectedly UNDEF" % c["label"])
        r, fl, nextpc, thumb = m
        base = list(STD)
        for (i, v) in c.get("sets", []):
            base[i] = v
        dw = [(i, r[i]) for i in range(15) if r[i] != base[i]]
        if len(dw) > 2:
            raise SystemExit("case %s writes %d regs" % (c["label"], len(dw)))
        fin = c.get("fin", (0,) * 7)
        flout = [-1 if cur == want else want for cur, want in zip(fin[:6], fl)]
        thout = -1 if fin[6] == thumb else thumb
        sets = c.get("sets", [])
        sr = [sets[i][0] if i < len(sets) else 0 for i in range(3)]
        sv = [sets[i][1] if i < len(sets) else 0 for i in range(3)]
        dwr = [dw[i][0] if i < len(dw) else 0 for i in range(2)]
        dwv = [dw[i][1] if i < len(dw) else 0 for i in range(2)]
        line = ('\t{ "%s", E_%s, %s, %d, {%d,%d,%d}, {%s}, %d, {%d,%d}, {%s}, %s,%d, %d },'
                % (c["label"], c["label"].upper(),
                   ",".join(str(x) for x in fin), len(sets), sr[0], sr[1], sr[2],
                   ",".join("0x%08xu" % v for v in sv), len(dw), dwr[0], dwr[1],
                   ",".join("0x%08xu" % v for v in dwv),
                   ",".join(str(x) for x in flout), thout,
                   -1 if nextpc == NEXT else nextpc))
        out.append(line)
    return "\n".join(out)


def emit_undef():
    out = []
    for c in all_cases:
        if c["form"] != "undef":
            continue
        out.append('\t{ "%s", E_%s },' % (c["label"], c["label"].upper()))
    return "\n".join(out)


hdr = open(sys.argv[1] if len(sys.argv) > 1 else "cases_v10.inc", "w")
hdr.write("/* SPDX-License-Identifier: MIT */\n")
hdr.write("/* Generated by gen_cases.py from the ARM ARM pseudocode; literals. Do not edit. */\n")
hdr.write("/* Columns: name, insn, in n,z,c,v,q,ge,thumb, nset, sr[3], sv[3], ndw, dw[2], dv[2],\n")
hdr.write(" *          out n,z,c,v,q,ge,thumb (-1 = unchanged), next_pc (-1 = unchanged). */\n")
hdr.write(emit())
hdr.write("\n")
hdr.close()

asmf = open(sys.argv[2] if len(sys.argv) > 2 else "encodings.txt", "w")
asmf.write("# label | assembly line; assembled by mkenc.py with llvm-mc --show-encoding\n")
for c in all_cases:
    asmf.write("%s|%s\n" % (c["label"], c["asm"]))
asmf.close()

uf = open(sys.argv[3] if len(sys.argv) > 3 else "undef_v10.inc", "w")
uf.write("/* SPDX-License-Identifier: MIT */\n")
uf.write("/* Generated by gen_cases.py: encodings that must return N2A32_UNDEF. */\n")
uf.write(emit_undef())
uf.write("\n")
uf.close()
print("wrote %d cases, %d undef" % (len(cases), len(undef_cases)))
