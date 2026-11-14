#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""V16 test support: turn assembly text into a C header of T32 encodings.

Modes (run by run_tests.sh, which calls llvm-mc between them):

    gen_enc.py asm      -> the assembly source
    gen_enc.py header ENC -> C defines E_<NAME>, the ALL_ENC[] table and the branch tables

No encoding is ever hand-typed: every word in the header is exactly what
llvm-mc -triple=thumbv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8
reports for the assembly text below.  Expected register/flag/memory values live in
t_v16.c and follow the ARM ARM pseudocode, not this script.  The branch offsets in the
generated tables are the literals of OFFSETS_* (i.e. what the assembler was asked to
encode), never read back from the encoding.
"""
import re
import sys

# ---------------------------------------------------------------------------------------
# Branch offset tables: every entry becomes one `b.w #<off>` (or B<c>.W / BL / BLX) and
# one { word, offset } pair in the C table.  The offsets cover every immediate field and
# both signs: bit 1..11 (imm11), 12..17 (imm6), 18..21 (imm10), 22/23 (J2/J1), sign (S).
OFFSETS_BW = [
    0, 2, 4, 0x40, 0x100, 0x7FE, 0x800, 0x1000, 0x2000, 0x4000, 0x8000,
    0x10000, 0x20000, 0x40000, 0x80000, 0xC0000, 0x100000, 0x1FFFFE, 0x200000,
    0x400000, 0x800000, 0xC00000, 0xFFFFFE,
    -2, -4, -0x40, -0x100, -0x800, -0x1000, -0x10000, -0x40000, -0x80000,
    -0x100000, -0x200000, -0x400000, -0x800000, -0x1000000,
]
OFFSETS_BC = [  # conditional: +-1 MB
    0, 2, 4, 0x40, 0x100, 0x7FE, 0x1000, 0x10000, 0x20000, 0x40000, 0x80000,
    0xC0000, 0xFFFFE,
    -2, -4, -0x40, -0x800, -0x10000, -0x40000, -0x80000, -0xFFFFE,
]
OFFSETS_BL = [
    0, 4, 0x100, 0x1000, 0x10000, 0x40000, 0x80000, 0x100000, 0x200000,
    0x400000, 0x800000, 0xFFFFFE, -4, -0x1000, -0x10000, -0x40000,
    -0x100000, -0x400000, -0x800000, -0x1000000,
]
OFFSETS_BLX = [
    0, 4, 0x100, 0x1000, 0x10000, 0x40000, 0x80000, 0x100000, 0x200000,
    0x400000, 0x800000, 0xFFFFFC, -4, -0x1000, -0x10000, -0x40000,
    -0x100000, -0x400000, -0x800000, -0x1000000,
]

CONDS = ["eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
         "hi", "ls", "ge", "lt", "gt", "le"]

# ---------------------------------------------------------------------------------------
# Fixed encodings, one assembly line each.
CASES = [
    # ---- load/store single: immediate 12 -------------------------------------------
    ("LDRB_I12_0", "ldrb.w r0, [r1]"),
    ("LDRB_I12_4", "ldrb.w r0, [r1, #4]"),
    ("LDRB_I12_ABC", "ldrb.w r0, [r1, #0xabc]"),
    ("LDRB_I12_FFF", "ldrb.w r0, [r1, #0xfff]"),
    ("STRB_I12_0", "strb.w r0, [r1]"),
    ("STRB_I12_4", "strb.w r0, [r1, #4]"),
    ("STRB_I12_ABC", "strb.w r2, [r3, #0xabc]"),
    ("LDRH_I12_0", "ldrh.w r0, [r1]"),
    ("LDRH_I12_6", "ldrh.w r0, [r1, #6]"),
    ("LDRH_I12_ABC", "ldrh.w r4, [r5, #0xabc]"),
    ("STRH_I12_6", "strh.w r0, [r1, #6]"),
    ("STRH_I12_ABC", "strh.w r6, [r7, #0xabc]"),
    ("LDR_I12_0", "ldr.w r0, [r1]"),
    ("LDR_I12_4", "ldr.w r0, [r1, #4]"),
    ("LDR_I12_ABC", "ldr.w r2, [r3, #0xabc]"),
    ("LDR_I12_FFC", "ldr.w r8, [r9, #0xffc]"),
    ("STR_I12_4", "str.w r0, [r1, #4]"),
    ("STR_I12_ABC", "str.w r2, [r3, #0xabc]"),
    ("LDRSB_I12_0", "ldrsb.w r0, [r1]"),
    ("LDRSB_I12_4", "ldrsb.w r0, [r1, #4]"),
    ("LDRSB_I12_ABC", "ldrsb.w r2, [r3, #0xabc]"),
    ("LDRSH_I12_0", "ldrsh.w r0, [r1]"),
    ("LDRSH_I12_6", "ldrsh.w r0, [r1, #6]"),
    ("LDRSH_I12_ABC", "ldrsh.w r2, [r3, #0xabc]"),
    # ---- load/store single: immediate 8 pre/post/writeback --------------------------
    ("LDRB_I8_NEG", "ldrb r0, [r1, #-4]"),
    ("LDRB_I8_POST", "ldrb r0, [r1], #4"),
    ("LDRB_I8_POST_NEG", "ldrb r0, [r1], #-4"),
    ("LDRB_I8_PRE", "ldrb r0, [r1, #4]!"),
    ("LDRB_I8_PRE_NEG", "ldrb r0, [r1, #-4]!"),
    ("LDRB_I8_FF", "ldrb r0, [r1, #-0xff]!"),
    ("STRB_I8_POST", "strb r0, [r1], #4"),
    ("STRB_I8_PRE_NEG", "strb r0, [r1, #-4]!"),
    ("STRB_I8_NEG", "strb r0, [r1, #-3]"),
    ("LDRH_I8_NEG", "ldrh r0, [r1, #-6]"),
    ("LDRH_I8_POST", "ldrh r0, [r1], #6"),
    ("LDRH_I8_PRE", "ldrh r0, [r1, #6]!"),
    ("STRH_I8_POST", "strh r0, [r1], #-6"),
    ("STRH_I8_PRE", "strh r0, [r1, #6]!"),
    ("LDR_I8_NEG", "ldr r0, [r1, #-4]"),
    ("LDR_I8_POST", "ldr r0, [r1], #4"),
    ("LDR_I8_POST_NEG", "ldr r0, [r1], #-4"),
    ("LDR_I8_PRE", "ldr r0, [r1, #4]!"),
    ("LDR_I8_PRE_NEG", "ldr r0, [r1, #-4]!"),
    ("STR_I8_POST", "str r0, [r1], #4"),
    ("STR_I8_PRE_NEG", "str r0, [r1, #-4]!"),
    ("LDRSB_I8_NEG", "ldrsb r0, [r1, #-4]"),
    ("LDRSB_I8_POST", "ldrsb r0, [r1], #4"),
    ("LDRSH_I8_NEG", "ldrsh r0, [r1, #-6]"),
    ("LDRSH_I8_PRE", "ldrsh r0, [r1, #6]!"),
    # ---- load/store single: unprivileged ------------------------------------------
    ("LDRT_0", "ldrt r0, [r1]"),
    ("LDRT_4", "ldrt r0, [r1, #4]"),
    ("STRT_4", "strt r0, [r1, #4]"),
    ("LDRBT_4", "ldrbt r0, [r1, #4]"),
    ("STRBT_4", "strbt r0, [r1, #4]"),
    ("LDRHT_4", "ldrht r0, [r1, #4]"),
    ("STRHT_4", "strht r0, [r1, #4]"),
    ("LDRSBT_4", "ldrsbt r0, [r1, #4]"),
    ("LDRSHT_4", "ldrsht r0, [r1, #4]"),
    # ---- load/store single: register ----------------------------------------------
    ("LDRB_R0", "ldrb.w r0, [r1, r2]"),
    ("LDRB_R1", "ldrb.w r0, [r1, r2, lsl #1]"),
    ("LDRB_R2", "ldrb.w r0, [r1, r2, lsl #2]"),
    ("LDRB_R3", "ldrb.w r0, [r1, r2, lsl #3]"),
    ("STRB_R3", "strb.w r0, [r1, r2, lsl #3]"),
    ("LDRH_R3", "ldrh.w r0, [r1, r2, lsl #3]"),
    ("STRH_R1", "strh.w r0, [r1, r2, lsl #1]"),
    ("LDR_R0", "ldr.w r0, [r1, r2]"),
    ("LDR_R2", "ldr.w r0, [r1, r2, lsl #2]"),
    ("LDR_R3", "ldr.w r0, [r1, r2, lsl #3]"),
    ("STR_R2", "str.w r0, [r1, r2, lsl #2]"),
    ("LDRSB_R1", "ldrsb.w r0, [r1, r2, lsl #1]"),
    ("LDRSH_R3", "ldrsh.w r0, [r1, r2, lsl #3]"),
    ("LDR_R1", "ldr.w r0, [r1, r2, lsl #1]"),
    ("STR_R0", "str.w r0, [r1, r2]"),
    ("LDRH_R1", "ldrh.w r0, [r1, r2, lsl #1]"),
    ("LDRH_R2", "ldrh.w r0, [r1, r2, lsl #2]"),
    ("LDRSB_R0", "ldrsb.w r0, [r1, r2]"),
    ("LDRSH_R0", "ldrsh.w r0, [r1, r2]"),
    # ---- load/store single: literal (Rn == 1111) -----------------------------------
    ("LDR_LIT", "ldr.w r0, [pc, #4]"),
    ("LDR_LIT_NEG", "ldr.w r0, [pc, #-4]"),
    ("LDR_LIT_ABC", "ldr.w r2, [pc, #0xabc]"),
    ("LDR_LIT_NEG_800", "ldr.w r2, [pc, #-0x800]"),
    ("LDRB_LIT", "ldrb.w r0, [pc, #4]"),
    ("LDRB_LIT_NEG", "ldrb.w r0, [pc, #-4]"),
    ("LDRH_LIT", "ldrh.w r0, [pc, #6]"),
    ("LDRH_LIT_NEG_800", "ldrh.w r0, [pc, #-0x800]"),
    ("LDRSB_LIT", "ldrsb.w r0, [pc, #4]"),
    ("LDRSB_LIT_NEG", "ldrsb.w r0, [pc, #-0x800]"),
    ("LDRSH_LIT", "ldrsh.w r0, [pc, #6]"),
    ("LDRSH_LIT_NEG", "ldrsh.w r0, [pc, #-0x800]"),
    # ---- load/store single: pc as data / pc as destination -------------------------
    ("LDR_PC", "ldr.w pc, [r1]"),
    ("LDR_PC_I12", "ldr.w pc, [r1, #4]"),
    ("LDR_PC_POST", "ldr.w pc, [r1], #4"),
    ("LDR_PC_LIT", "ldr.w pc, [pc, #4]"),
    ("STR_PC", "str.w pc, [r1]"),
    ("STR_PC_I12", "str.w pc, [r1, #4]"),
    ("STR_PC_REG", "str.w pc, [r1, r2]"),
    ("LDR_PC_REG", "ldr.w pc, [r1, r2]"),
    # ---- preloads ------------------------------------------------------------------
    ("PLD_I12_0", "pld [r1]"),
    ("PLD_I12_4", "pld [r1, #4]"),
    ("PLD_I12_ABC", "pld [r1, #0xabc]"),
    ("PLD_I8_NEG", "pld [r1, #-4]"),
    ("PLD_I8_FF", "pld [r1, #-0xff]"),
    ("PLD_REG", "pld [r1, r2]"),
    ("PLD_REG_LSL2", "pld [r1, r2, lsl #2]"),
    ("PLD_LIT", "pld [pc, #4]"),
    ("PLD_LIT_NEG", "pld [pc, #-4]"),
    ("PLI_I12_4", "pli [r1, #4]"),
    ("PLI_I8_NEG", "pli [r1, #-4]"),
    ("PLI_REG_LSL3", "pli [r1, r2, lsl #3]"),
    ("PLI_LIT", "pli [pc, #4]"),
    ("PLI_LIT_NEG", "pli [pc, #-4]"),
    # ---- load/store dual -----------------------------------------------------------
    ("LDRD_8", "ldrd r0, r1, [r5, #8]"),
    ("LDRD_NEG8", "ldrd r0, r1, [r5, #-8]"),
    ("LDRD_0", "ldrd r2, r3, [r5]"),
    ("LDRD_POST8", "ldrd r0, r1, [r5], #8"),
    ("LDRD_POST_NEG8", "ldrd r0, r1, [r5], #-8"),
    ("LDRD_PRE_WB", "ldrd r0, r1, [r5, #8]!"),
    ("LDRD_PRE_NEG_WB", "ldrd r0, r1, [r5, #-8]!"),
    ("LDRD_4", "ldrd r4, r5, [r6, #4]"),
    ("LDRD_3FC", "ldrd r8, r9, [r10, #0x3fc]"),
    ("STRD_8", "strd r0, r1, [r5, #8]"),
    ("STRD_NEG8", "strd r0, r1, [r5, #-8]"),
    ("STRD_0", "strd r2, r3, [r5]"),
    ("STRD_POST8", "strd r0, r1, [r5], #8"),
    ("STRD_POST_NEG8", "strd r2, r3, [r5], #-8"),
    ("STRD_PRE_WB", "strd r0, r1, [r5, #8]!"),
    ("STRD_PRE_NEG_WB", "strd r2, r3, [r5, #-8]!"),
    ("STRD_3FC", "strd r10, r11, [r12, #0x3fc]"),
    # Arbitrary register pairs: the vendor libraries use these (ARMv8-A allows them, the
    # ARMv7 wording calls them UNPREDICTABLE), e.g. "strd r3, r1, [sp, #8]" and
    # "ldrd r2, r0, [sp]" are inside libvpud_vcodec.so.
    ("LDRD_ODD_RT", "ldrd r3, r2, [r0, #64]"),
    ("LDRD_NONCONS", "ldrd r2, r0, [sp]"),
    ("LDRD_SAME", "ldrd r4, r6, [r1]"),
    ("STRD_ODD_RT", "strd r3, r1, [sp, #8]"),
    ("STRD_NONCONS", "strd r5, r8, [sp]"),
    ("STRD_SAME", "strd r6, r2, [r7]"),
    ("LDRD_LIT", "ldrd r0, r1, [pc, #8]"),
    ("LDRD_LIT_NEG", "ldrd r0, r1, [pc, #-8]"),
    ("STRD_LIT", "strd r0, r1, [pc, #8]"),
    ("STRD_LIT_NEG", "strd r0, r1, [pc, #-8]"),
    # ---- LDM / STM -----------------------------------------------------------------
    ("LDMIA", "ldm.w r0, {r1, r2}"),
    ("LDMIA_WB", "ldm.w r0!, {r1, r2}"),
    ("LDMIA_MANY", "ldm.w r0, {r1-r3, r5, r8-r11}"),
    ("LDMIA_WB_MANY", "ldm.w r3!, {r0, r1, r2, r4-r9}"),
    ("STMIA", "stm.w r0, {r1, r2}"),
    ("STMIA_WB", "stm.w r0!, {r1, r2}"),
    ("STMIA_MANY", "stm.w r5!, {r0-r4, r6, r7}"),
    ("LDMDB", "ldmdb r0, {r1, r2}"),
    ("LDMDB_WB", "ldmdb r0!, {r1-r4}"),
    ("STMDB", "stmdb r0, {r1, r2}"),
    ("STMDB_WB", "stmdb r0!, {r1-r4}"),
    ("PUSH_W", "push.w {r0-r2, lr}"),
    ("PUSH_W_ONE", "push.w {r4, lr}"),
    ("PUSH_W_MANY", "push.w {r4-r11, lr}"),
    ("POP_W", "pop.w {r0, r2}"),
    ("POP_W_PC", "pop.w {r0, pc}"),
    ("POP_W_PC_MANY", "pop.w {r4-r11, pc}"),
    ("POP_W_LR", "pop.w {r4, lr}"),
    ("LDM_LR", "ldm.w r0, {r1, lr}"),
    ("STMDB_LR", "stmdb r0!, {r1, lr}"),
    ("LDMIA_PC", "ldm.w r0, {r1-r3, pc}"),
    ("LDMIA_PC_WB", "ldm.w r0!, {r1-r3, pc}"),
    ("LDMDB_PC", "ldmdb r0!, {r1, pc}"),
    # ---- exclusives ----------------------------------------------------------------
    ("LDREX_0", "ldrex r0, [r1]"),
    ("LDREX_4", "ldrex r0, [r1, #4]"),
    ("LDREX_3FC", "ldrex r6, [r7, #0x3fc]"),
    ("STREX_0", "strex r2, r0, [r1]"),
    ("STREX_4", "strex r2, r0, [r1, #4]"),
    ("STREX_3FC", "strex r9, r10, [r11, #0x3fc]"),
    ("LDREXB", "ldrexb r0, [r1]"),
    ("LDREXH", "ldrexh r3, [r4]"),
    ("LDREXD", "ldrexd r0, r1, [r2]"),
    ("LDREXD_46", "ldrexd r4, r5, [r6]"),
    ("STREXB", "strexb r2, r0, [r1]"),
    ("STREXH", "strexh r5, r6, [r7]"),
    ("STREXD", "strexd r3, r0, r1, [r2]"),
    ("CLREX", "clrex"),
    # ---- table branch --------------------------------------------------------------
    ("TBB", "tbb [r0, r1]"),
    ("TBB_PC", "tbb [pc, r1]"),
    ("TBB_R3", "tbb [r4, r3]"),
    ("TBH", "tbh [r0, r1, lsl #1]"),
    ("TBH_PC", "tbh [pc, r2, lsl #1]"),
    ("TBH_R7", "tbh [r6, r7, lsl #1]"),
    # ---- branches (fixed, plus the generated tables below) --------------------------
    ("BW_FWD", "b.w #0x100"),
    ("BW_BACK", "b.w #-0x100"),
    ("BW_ZERO", "b.w #0"),
    ("BL_FWD", "bl #0x100"),
    ("BL_BACK", "bl #-0x100"),
    ("BLX_FWD", "blx #0x100"),
    ("BLX_BACK", "blx #-0x100"),
    ("BNE_W", "bne.w #0x80"),
    ("BEQ_W_NEG", "beq.w #-0x80"),
    ("BHI_W_BIG", "bhi.w #0x40000"),
    ("BLE_W_NEG_BIG", "ble.w #-0x40000"),
    # ---- system / coprocessor ------------------------------------------------------
    ("MRS_R0", "mrs r0, apsr"),
    ("MRS_R7", "mrs r7, apsr"),
    ("MRS_R12", "mrs r12, apsr"),
    ("MSR_NZCVQ", "msr APSR_nzcvq, r0"),
    ("MSR_G", "msr APSR_g, r1"),
    ("MSR_NZCVQG", "msr APSR_nzcvqg, r2"),
    ("NOP_W", "nop.w"),
    ("YIELD_W", "yield.w"),
    ("WFE_W", "wfe.w"),
    ("WFI_W", "wfi.w"),
    ("SEV_W", "sev.w"),
    ("DBG0_W", "dbg #0"),
    ("DBG15_W", "dbg #15"),
    ("DMB_SY", "dmb sy"),
    ("DMB_ISH", "dmb ish"),
    ("DMB_ISHST", "dmb ishst"),
    ("DSB_SY", "dsb sy"),
    ("DSB_ISHST", "dsb ishst"),
    ("ISB_SY", "isb sy"),
    ("UDF0_W", "udf.w #0"),
    ("UDF_W_123", "udf.w #0x123"),
    ("MRC_TPID0", "mrc p15, 0, r0, c13, c0, 3"),
    ("MRC_TPID9", "mrc p15, 0, r9, c13, c0, 3"),
    ("MRC_TPID12", "mrc p15, 0, r12, c13, c0, 3"),
    # ---- encodings of other classes / other cp15 words: must be UNDEF here ----------
    ("X_MRC_OPC2_2", "mrc p15, 0, r0, c13, c0, 2"),
    ("X_MRC_CRN_12", "mrc p15, 0, r0, c12, c0, 3"),
    ("X_MCR_SAME", "mcr p15, 0, r0, c13, c0, 3"),
    ("X_MCR_OPC1", "mrc p15, 1, r0, c13, c0, 3"),
    ("X_MRC_CP14", "mrc p14, 0, r0, c13, c0, 3"),
    ("X_MCRR", "mcrr p15, 0, r0, r1, c2"),
    ("X_MRRC", "mrrc p15, 1, r0, r1, c14"),
    ("X_SVC", "svc #0"),
    ("X_BKPT", "bkpt #0"),
    ("X_T16_LDR", "ldr r0, [r1]"),
    ("X_T16_PUSH", "push {r4, lr}"),
    ("X_T16_UDf", "udf #0"),
    ("X_T16_BLX_REG", "blx r0"),
    ("X_LDC", "ldc p12, c0, [r0]"),
    ("X_STC", "stc p12, c0, [r0]"),
    ("X_CDP", "cdp p12, 0, c0, c0, c0, 0"),
]

# PLDW only assembles with the MP extensions feature; same triple, one extra feature.
MP = [
    ("PLDW_0", "pldw [r1]"),
    ("PLDW_4", "pldw [r1, #4]"),
    ("PLDW_ABC", "pldw [r1, #0xabc]"),
    ("PLDW_I8_NEG", "pldw [r1, #-4]"),
    ("PLDW_REG", "pldw [r1, r2]"),
    ("PLDW_REG_LSL2", "pldw [r1, r2, lsl #2]"),
    ("PLDW_LIT", "pldw [pc, #4]"),
]

ENC_RE4 = re.compile(r"@ encoding: \[0x([0-9a-fA-F]{2}),0x([0-9a-fA-F]{2}),"
                     r"0x([0-9a-fA-F]{2}),0x([0-9a-fA-F]{2})\]")
ENC_RE2 = re.compile(r"@ encoding: \[0x([0-9a-fA-F]{2}),0x([0-9a-fA-F]{2})\]")
ERR_RE = re.compile(r"^\S+:\d+:\d+: error")


def emit_asm(mp=0):
    out = [".syntax unified", ".thumb"]
    if mp:
        for _, text in MP:
            out.append("\t" + text)
        print("\n".join(out))
        return
    for _, text in CASES:
        out.append("\t" + text)
    for i, off in enumerate(OFFSETS_BW):
        out.append("\tb.w #%d" % off)
    for i, off in enumerate(OFFSETS_BL):
        out.append("\tbl #%d" % off)
    for i, off in enumerate(OFFSETS_BLX):
        out.append("\tblx #%d" % off)
    for c in CONDS:
        out.append("\tb%s.w #0x100" % c)
    for i, off in enumerate(OFFSETS_BC):
        out.append("\tbne.w #%d" % off)
    print("\n".join(out))


def parse(path):
    words = []
    errors = []
    with open(path, "r", encoding="utf-8") as fp:
        for line in fp:
            if ERR_RE.search(line):
                errors.append(line.rstrip())
            m = ENC_RE4.search(line)
            if m:
                b = [int(x, 16) for x in m.groups()]
                # llvm-mc prints the bytes in memory order (first halfword first, LE).
                words.append((b[1] << 8 | b[0]) << 16 | (b[3] << 8 | b[2]))
                continue
            m = ENC_RE2.search(line)
            if m:
                b = [int(x, 16) for x in m.groups()]
                # 16-bit instruction: the class functions see it as a small word.
                words.append(b[1] << 8 | b[0])
    return words, errors


def emit_header(path, mp_path):
    words, errors = parse(path)
    mp_words, mp_errors = parse(mp_path)
    errors += mp_errors
    words += mp_words
    if errors:
        sys.stderr.write("gen_enc: llvm-mc errors:\n" + "\n".join(errors) + "\n")
        sys.exit(1)
    n = len(CASES) + len(MP) + len(OFFSETS_BW) + len(OFFSETS_BL) + len(OFFSETS_BLX) + \
        len(CONDS) + len(OFFSETS_BC)
    if len(words) != n:
        sys.stderr.write("gen_enc: %d encodings for %d cases\n" % (len(words), n))
        sys.exit(1)
    print("/* generated by test_v16/gen_enc.py from llvm-mc output -- do not edit */")
    print("#ifndef V16_ENC_H")
    print("#define V16_ENC_H")
    print("#include <stdint.h>")
    pos = 0
    for name, _ in CASES:
        print("#define E_%s 0x%08Xu" % (name, words[pos]))
        pos += 1
    print("struct v16_br { uint32_t word; int32_t off; };")
    for tbl, offs in (("BW", OFFSETS_BW), ("BL", OFFSETS_BL), ("BLX", OFFSETS_BLX)):
        print("static const struct v16_br BR_%s[] = {" % tbl)
        for off in offs:
            print("\t{ 0x%08Xu, %d }," % (words[pos], off))
            pos += 1
        print("};")
    print("static const struct v16_br BR_BC_AL[] = {")
    for c in CONDS:
        w = words[pos]
        pos += 1
        print("\t{ 0x%08Xu, 0x100 },   /* b%s.w */" % (w, c))
    print("};")
    print("static const struct v16_br BR_BC[] = {")
    for off in OFFSETS_BC:
        print("\t{ 0x%08Xu, %d }," % (words[pos], off))
        pos += 1
    print("};")
    for name, _ in MP:                    # appended after the main file's encodings
        print("#define E_%s 0x%08Xu" % (name, words[pos]))
        pos += 1
    print("#endif")
    assert pos == len(words)


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: gen_enc.py asm|header ENC\n")
        return 2
    if sys.argv[1] == "asm":
        emit_asm(0)
        return 0
    if sys.argv[1] == "asm-mp":
        emit_asm(1)
        return 0
    if sys.argv[1] == "header" and len(sys.argv) == 4:
        emit_header(sys.argv[2], sys.argv[3])
        return 0
    sys.stderr.write("bad arguments\n")
    return 2


if __name__ == "__main__":
    sys.exit(main())
