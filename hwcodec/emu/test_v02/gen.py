#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# gen.py -- assemble every routing-table example and the guest programs with llvm-mc (never hand-typed hex)
# and emit $OUT/n2enc.h for t_core.c.  Usage: python3 gen.py <outdir>
import os
import re
import subprocess
import sys

TRIPLE_ARM = "armv8a-linux-gnueabi"
TRIPLE_THUMB = "thumbv8a-linux-gnueabi"
MATTR = "+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8"

# name, mode, assembly snippet.  One instruction per snippet (labels allowed after it).
CASES = [
    # ---- A32 data processing / media / memory / misc / vfp / neon ----
    ("A_ADD_REG", "arm", "add r0, r1, r2"),
    ("A_MOV_IMM", "arm", "mov r0, #1"),
    ("A_MOVEQ_IMM", "arm", "moveq r0, #1"),
    ("A_MOV2_IMM", "arm", "mov r1, #2"),
    ("A_MOVW", "arm", "movw r0, #0x1234"),
    ("A_MRS", "arm", "mrs r0, apsr"),
    ("A_CLZ", "arm", "clz r0, r1"),
    ("A_MOV_FP_SP", "arm", "mov fp, sp"),
    ("A_MOV_SHIFT_REG", "arm", "mov r2, r0, lsl r1"),
    ("A_LDRH_NEG", "arm", "ldrh r2, [r0, #-2]"),
    ("A_YIELD", "arm", "yield"),
    ("A_MSR_IMM", "arm", "msr apsr_nzcvq, #0"),
    ("A_MUL", "arm", "mul r0, r1, r2"),
    ("A_NOP", "arm", "nop"),
    ("A_SXTB", "arm", "sxtb r0, r1"),
    ("A_SDIV", "arm", "sdiv r0, r1, r2"),
    ("A_QADD", "arm", "qadd r0, r1, r2"),
    ("A_SMLABB", "arm", "smlabb r0, r1, r2, r3"),
    ("A_SMULBB", "arm", "smulbb r0, r1, r2"),
    ("A_SMULWB", "arm", "smulwb r0, r1, r2"),
    ("A_LDR_IMM", "arm", "ldr r0, [r1, #4]"),
    ("A_LDR0", "arm", "ldr r0, [r1]"),
    ("A_STR0", "arm", "str r2, [r1]"),
    ("A_STR_IMM", "arm", "str r2, [r1, #4]"),
    ("A_LDR_REG", "arm", "ldr r0, [r1, r2]"),
    ("A_LDRH", "arm", "ldrh r0, [r1]"),
    ("A_LDREX", "arm", "ldrex r0, [r1]"),
    ("A_LDM", "arm", "ldm r0, {r1}"),
    ("A_PLD", "arm", "pld [r0]"),
    ("A_PLI", "arm", "pli [r0]"),
    ("A_PLD_REG", "arm", "pld [r0, r1]"),
    ("A_PLI_NEG", "arm", "pli [r0, #-4]"),
    ("A_PLD_NEG", "arm", "pld [r0, #-4]"),
    ("A_PLDW", "arm", "pldw [r0]"),
    ("A_PLDW_REG", "arm", "pldw [r0, r1]"),
    ("A_PLI_REG", "arm", "pli [r0, r1]"),
    ("A_B", "arm", "b #0"),
    ("A_BL", "arm", "bl #0"),
    ("A_BLX_REG", "arm", "blx r0"),
    ("A_BX", "arm", "bx r0"),
    ("A_SVC", "arm", "svc #0"),
    ("A_BKPT", "arm", "bkpt #0"),
    ("A_UDF", "arm", "udf #0"),
    ("A_DMB", "arm", "dmb sy"),
    ("A_MRC15", "arm", "mrc p15, 0, r0, c13, c0, 3"),
    ("A_VADD", "arm", "vadd.f32 s0, s1, s2"),
    ("A_VLDR", "arm", "vldr s0, [r0]"),
    ("A_VMAXNM", "arm", "vmaxnm.f32 s0, s1, s2"),
    ("A_NEON_ADD", "arm", "vadd.i8 q0, q1, q2"),
    ("A_NEON_SUB", "arm", "vsub.i16 d0, d1, d2"),
    ("A_NEON_SHL", "arm", "vshl.i8 q0, q1, #1"),
    ("A_NEON_ADDL", "arm", "vaddl.s8 q0, d1, d2"),
    ("A_NEON_MULL", "arm", "vmull.s16 q0, d1, d2"),
    ("A_NEON_MLAS", "arm", "vmla.f32 q0, q1, d2[1]"),
    ("A_NEON_MOVL", "arm", "vmovl.s16 q0, d1"),
    ("A_NEON_EXT", "arm", "vext.8 q0, q1, q2, #1"),
    ("A_NEON_TBL", "arm", "vtbl.8 d0, {d1, d2, d3}, d4"),
    ("A_NEON_DUP", "arm", "vdup.8 d0, d1[3]"),
    ("A_NEON_IMM", "arm", "vmov.i8 q0, #0"),
    ("A_NEON_REV", "arm", "vrev64.8 q0, q1"),
    ("A_NEON_CVT", "arm", "vcvt.f32.s32 q0, q1"),
    ("A_NEON_VLD", "arm", "vld1.8 {d0}, [r0]"),
    ("A_NEON_VST", "arm", "vst1.32 {d0, d1, d2, d3}, [r0]!"),
    ("A_NEON_VLDLANE", "arm", "vld1.8 {d0[1]}, [r0]"),
    ("A_NEON_SHRN64", "arm", "vshrn.i64 d0, q1, #1"),
    ("A_NEON_ADDL32", "arm", "vaddl.u32 q8, d1, d2"),
    ("A_NEON_MLAS32", "arm", "vmla.i32 q9, q9, d2[1]"),
    ("A_NEON_SHL64", "arm", "vshl.i64 q0, q1, #63"),
    ("A_NEON_IM16", "arm", "vmov.i16 d0, #0x2"),
    ("A_NEON_IM32D", "arm", "vmov.i32 d0, #0xff"),
    ("A_NEON_IMF32D", "arm", "vmov.f32 d0, #1.0"),
    ("A_NEON_VORRD", "arm", "vorr.i32 d0, #0xff"),
    # ---- T32 16-bit ----
    ("T_MOV_REG", "thumb", "mov r0, r1"),
    ("T_NOP", "thumb", "nop"),
    ("T_IT", "thumb", "it eq"),
    ("T_PUSH", "thumb", "push {r0, lr}"),
    ("T_POP", "thumb", "pop {r0, pc}"),
    ("T_BX", "thumb", "bx lr"),
    ("T_SVC", "thumb", "svc #0"),
    ("T_BKPT", "thumb", "bkpt #0"),
    ("T_UDF", "thumb", "udf #0"),
    ("T_LDRLIT", "thumb", "ldr r0, [pc, #16]"),
    ("T_ADD_IMM", "thumb", "adds r0, #1"),
    ("T_B", "thumb", "b #0"),
    ("T_BEQ", "thumb", "beq #0"),
    ("T_CBZ", "thumb", "cbz r0, #0"),
    # ---- T32 32-bit ----
    ("T32_ADD", "thumb", "add.w r0, r1, r2"),
    ("T32_AND", "thumb", "and.w r0, r1, r2"),
    ("T32_MOV_IMM", "thumb", "mov.w r0, #0xff"),
    ("T32_MOVW", "thumb", "movw r0, #0x1234"),
    ("T32_ADDW", "thumb", "addw r0, r1, #0x123"),
    ("T32_SSAT", "thumb", "ssat r0, #8, r1"),
    ("T32_UBFX", "thumb", "ubfx r0, r1, #4, #8"),
    ("T32_MUL", "thumb", "mul r0, r1, r2"),
    ("T32_UDIV", "thumb", "udiv r0, r1, r2"),
    ("T32_CLZ", "thumb", "clz r0, r1"),
    ("T32_REV", "thumb", "rev.w r0, r1"),
    ("T32_QADD", "thumb", "qadd r0, r1, r2"),
    ("T32_SXTAB", "thumb", "sxtab r0, r1, r2, ror #8"),
    ("T32_PKHBT", "thumb", "pkhbt r0, r1, r2, lsl #3"),
    ("T32_LDR", "thumb", "ldr.w r0, [r1, #4]"),
    ("T32_STR", "thumb", "str.w r0, [r1, #4]"),
    ("T32_LDRB", "thumb", "ldrb.w r0, [r1, #4]"),
    ("T32_LDRH", "thumb", "ldrh.w r0, [r1, #4]"),
    ("T32_LDRSB", "thumb", "ldrsb.w r0, [r1, #4]"),
    ("T32_LDR_REG", "thumb", "ldr.w r0, [r1, r2, lsl #2]"),
    ("T32_LDRD", "thumb", "ldrd r0, r1, [r2, #8]"),
    ("T32_LDREX", "thumb", "ldrex r0, [r1]"),
    ("T32_TBB", "thumb", "tbb [r0, r1]"),
    ("T32_LDM", "thumb", "ldmia.w r0!, {r1, r2}"),
    ("T32_STMDB", "thumb", "stmdb.w r0!, {r1, r2}"),
    ("T32_PLD", "thumb", "pld [r0]"),
    ("T32_PLI", "thumb", "pli [r0]"),
    ("T32_B", "thumb", "b.w #0"),
    ("T32_BL", "thumb", "bl #0"),
    ("T32_BLX", "thumb", "blx #0"),
    ("T32_BEQ", "thumb", "beq.w #0"),
    ("T32_MRS", "thumb", "mrs r0, apsr"),
    ("T32_MSR", "thumb", "msr apsr_nzcvq, r0"),
    ("T32_DMB", "thumb", "dmb sy"),
    ("T32_UDF", "thumb", "udf.w #0"),
    ("T32_VMOV", "thumb", "vmov.f32 s0, s1"),
    ("T32_VLDR", "thumb", "vldr s0, [r0]"),
    ("T32_VPUSH", "thumb", "vpush {d8}"),
    ("T32_VMAXNM", "thumb", "vmaxnm.f32 s0, s1, s2"),
    ("T32_NEON_ADD", "thumb", "vadd.i8 q0, q1, q2"),
    ("T32_NEON_SUB", "thumb", "vsub.i16 d0, d1, d2"),
    ("T32_NEON_SHL", "thumb", "vshl.i8 q0, q1, #1"),
    ("T32_NEON_IMM", "thumb", "vmov.i8 q0, #0"),
    ("T32_NEON_VLD", "thumb", "vld1.8 {d0}, [r0]"),
    ("T32_NEON_VST", "thumb", "vst1.32 {d0, d1, d2, d3}, [r0]!"),
]

ENC_RE = re.compile(r"encoding: \[([^\]]*)\]")
BYTE_RE = re.compile(r"0x([0-9a-fA-F]{2})")


def assemble(text, thumb):
    triple = TRIPLE_THUMB if thumb else TRIPLE_ARM
    p = subprocess.run(["llvm-mc", "-triple=" + triple, "-mattr=" + MATTR, "--show-encoding"],
                       input=text + "\n", capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write("llvm-mc failed for %r:\n%s\n" % (text, p.stderr))
        sys.exit(1)
    encs = []
    for line in p.stdout.splitlines():
        m = ENC_RE.search(line)
        if m:
            encs.append([int(x, 16) for x in BYTE_RE.findall(m.group(1))])
    if len(encs) != 1:
        sys.stderr.write("expected one encoding for %r, got %d\n" % (text, len(encs)))
        sys.exit(1)
    b = encs[0]
    if thumb:
        if len(b) == 2:
            return b[0] | (b[1] << 8), 2
        if len(b) == 4:
            hw1 = b[0] | (b[1] << 8)
            hw2 = b[2] | (b[3] << 8)
            return (hw1 << 16) | hw2, 4
        raise SystemExit("bad thumb length %r" % (b,))
    if len(b) != 4:
        raise SystemExit("bad arm length %r" % (b,))
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24), 4


def assemble_program(text, thumb):
    triple = TRIPLE_THUMB if thumb else TRIPLE_ARM
    out = os.path.join(OUT, "prog.o")
    src = os.path.join(OUT, "prog.s")
    binf = os.path.join(OUT, "prog.bin")
    with open(src, "w") as f:
        f.write(".syntax unified\n" + text + "\n")
    p = subprocess.run(["llvm-mc", "-triple=" + triple, "-mattr=" + MATTR, "-filetype=obj", src, "-o", out],
                       capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write("program assembly failed:\n%s\n%s\n" % (text, p.stderr))
        sys.exit(1)
    p = subprocess.run(["llvm-objcopy", "-O", "binary", "-j", ".text", out, binf], capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write("objcopy failed: %s\n" % p.stderr)
        sys.exit(1)
    with open(binf, "rb") as f:
        return list(f.read())


OUT = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(OUT, exist_ok=True)

lines = ["/* generated by gen.py from llvm-mc; do not edit */",
         "#ifndef N2ENC_H", "#define N2ENC_H", ""]
for name, mode, text in CASES:
    val, size = assemble(text, mode == "thumb")
    lines.append("#define ENC_%s 0x%08xu  /* %d-bit */" % (name, val, size * 8))
    if mode == "thumb" and size == 2:
        lines.append("#define SIZE_%s 2" % name)
    else:
        lines.append("#define SIZE_%s %d" % (name, size))
lines.append("")

# ---- guest programs ----
ARM_GUEST = """
.syntax unified
.arm
.global guest_arm
guest_arm:
    mov r4, lr
    mov r0, #1
    ldr r12, 1f
    blx r12
    add r0, r0, #10
    bx r4
    .align 2
1:  .word 0xfff00004
"""
THUMB_GUEST = """
.syntax unified
.thumb
.global guest_thumb
guest_thumb:
    movs r0, #100
    mov r6, lr
    ldr r7, 1f
    blx r7
    adds r0, r0, #2
    bx r6
    .align 2
1:  .word 0xfff00008
"""
IT_PROG = """
.syntax unified
.thumb
.global it_prog
it_prog:
    itte eq
    moveq r0, #1
    addeq r0, #2
    movne r0, #30
    movs r1, #4
    bx lr
"""
LOOP_PROG = """
.syntax unified
.arm
.global loop_prog
loop_prog:
1:  add r0, r0, #1
    b 1b
"""

for pname, text, thumb in (("guest_arm", ARM_GUEST, False), ("guest_thumb", THUMB_GUEST, True),
                           ("it_prog", IT_PROG, True), ("loop_prog", LOOP_PROG, False)):
    data = assemble_program(text, thumb)
    lines.append("static const unsigned char %s_bytes[] = {%s};" % (pname, ",".join(str(b) for b in data)))
    lines.append("#define %s_SIZE %du" % (pname.upper(), len(data)))

lines.append("#endif")
with open(os.path.join(OUT, "n2enc.h"), "w") as f:
    f.write("\n".join(lines) + "\n")
print("wrote %s/n2enc.h (%d encodings)" % (OUT, len(CASES)))
