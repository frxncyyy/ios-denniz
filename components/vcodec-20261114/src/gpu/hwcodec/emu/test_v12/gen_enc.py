#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""V12 test support: turn the assembly text below into a C header of A32 encodings.

Modes (run by run_tests.sh, which calls llvm-mc between them):
    gen_enc.py asm       -> assembly source for the armv7a feature set of the task
    gen_enc.py asm-mp    -> the PLDW cases (need the MP extensions feature)
    gen_enc.py header MAIN.enc MP.enc -> C defines E_<NAME> plus the ALL_ENC[] table

No encoding is ever hand-typed: the words in the header are exactly the bytes llvm-mc
reports for the assembly text below.  Expected register/memory values live in t_v12.c
and come from the ARM ARM pseudocode, not from this script.
"""
import re
import sys

# (name, assembly).  Kept in lockstep with t_v12.c.
MAIN = [
    # --- LDR/STR word and byte -------------------------------------------------
    ("LDR_IMM0", "ldr r0, [r1]"),
    ("LDR_IMM4", "ldr r0, [r1, #4]"),
    ("LDR_IMM_NEG4", "ldr r0, [r1, #-4]"),
    ("LDR_IMM_ABC", "ldr r2, [r1, #0xabc]"),
    ("LDR_POST4", "ldr r0, [r1], #4"),
    ("LDR_POST_NEG4", "ldr r0, [r1], #-4"),
    ("LDR_PRE_NEG4_WB", "ldr r3, [r1, #-4]!"),
    ("LDR_REG", "ldr r0, [r1, r2]"),
    ("LDR_REG_LSL2", "ldr r0, [r1, r2, lsl #2]"),
    ("LDR_REG_LSR1", "ldr r0, [r1, r2, lsr #1]"),
    ("LDR_REG_LSR32", "ldr r0, [r1, r2, lsr #32]"),
    ("LDR_REG_ASR4", "ldr r0, [r1, r2, asr #4]"),
    ("LDR_REG_ASR32", "ldr r0, [r1, r2, asr #32]"),
    ("LDR_REG_ROR8", "ldr r0, [r1, r2, ror #8]"),
    ("LDR_REG_RRX", "ldr r0, [r1, r2, rrx]"),
    ("LDR_REG_NEG", "ldr r0, [r1, -r2]"),
    ("LDR_REG_NEG_LSL3", "ldr r0, [r1, -r2, lsl #3]"),
    ("LDR_POST_REG", "ldr r0, [r1], r2"),
    ("LDR_POST_REG_NEG_LSL1", "ldr r0, [r1], -r2, lsl #1"),
    ("LDR_LITERAL", "ldr r0, [pc, #16]"),
    ("LDR_LITERAL_NEG", "ldr r0, [pc, #-4]"),
    ("LDR_PC", "ldr pc, [r1]"),
    ("LDR_PC_WB", "ldr pc, [r1, #4]!"),
    ("LDRB_IMM3", "ldrb r0, [r1, #3]"),
    ("LDRB_NEG1", "ldrb r0, [r1, #-1]"),
    ("LDRB_POST1", "ldrb r0, [r1], #1"),
    ("LDRB_REG_LSL1", "ldrb r0, [r1, r2, lsl #1]"),
    ("LDRB_REG_NEG", "ldrb r0, [r1, -r2]"),
    ("STRB_REG", "strb r0, [r1, r2, lsl #2]"),
    ("STR_IMM0", "str r0, [r1]"),
    ("STR_IMM4", "str r0, [r1, #4]"),
    ("STR_PRE_NEG4_WB", "str r0, [r1, #-4]!"),
    ("STR_POST4", "str r0, [r1], #4"),
    ("STR_REG_ROR16", "str r2, [r1, r3, ror #16]"),
    ("STR_REG_NEG", "str r0, [r1, -r2, lsl #1]"),
    ("STR_POST_REG", "str r0, [r1], r2"),
    ("STR_PC", "str pc, [r1]"),
    ("STRB_IMM3", "strb r0, [r1, #3]"),
    ("STRB_POST_NEG3", "strb r0, [r1], #-3"),
    ("LDRT4", "ldrt r0, [r1], #4"),
    ("STRT_NEG4", "strt r0, [r1], #-4"),
    ("LDRBT", "ldrbt r0, [r1]"),
    ("STRBT1", "strbt r0, [r1], #1"),
    # --- halfword / signed / doubleword ---------------------------------------
    ("LDRH_IMM6", "ldrh r0, [r1, #6]"),
    ("LDRH_NEG6", "ldrh r0, [r1, #-6]"),
    ("LDRH_POST6", "ldrh r0, [r1], #6"),
    ("LDRH_PRE_WB", "ldrh r0, [r1, #6]!"),
    ("LDRH_REG", "ldrh r0, [r1, r2]"),
    ("LDRH_REG_NEG", "ldrh r0, [r1, -r2]"),
    ("LDRH_PC", "ldrh pc, [r1]"),
    ("STRH_IMM2", "strh r0, [r1, #2]"),
    ("STRH_POST_NEG2", "strh r0, [r1], #-2"),
    ("STRH_REG", "strh r0, [r1, r2]"),
    ("STRH_REG_NEG", "strh r0, [r1, -r2]"),
    ("LDRSB_1", "ldrsb r0, [r1, #1]"),
    ("LDRSB_POST_NEG1", "ldrsb r0, [r1], #-1"),
    ("LDRSB_REG", "ldrsb r0, [r1, r2]"),
    ("LDRSB_REG_NEG", "ldrsb r0, [r1, -r2]"),
    ("LDRSH_2", "ldrsh r0, [r1, #2]"),
    ("LDRSH_POST2", "ldrsh r0, [r1], #2"),
    ("LDRSH_REG", "ldrsh r0, [r1, r2]"),
    ("LDRSH_REG_NEG", "ldrsh r0, [r1, -r2]"),
    ("LDRSH_PC", "ldrsh pc, [r1]"),
    ("LDRD_IMM8", "ldrd r0, r1, [r5, #8]"),
    ("LDRD_NEG8", "ldrd r2, r3, [r5, #-8]"),
    ("LDRD_POST8", "ldrd r0, r1, [r5], #8"),
    ("LDRD_PRE_WB", "ldrd r0, r1, [r5, #8]!"),
    ("LDRD_REG", "ldrd r0, r1, [r5, r6]"),
    ("LDRD_REG_NEG", "ldrd r0, r1, [r5, -r6]"),
    ("STRD_IMM8", "strd r0, r1, [r5, #8]"),
    ("STRD_POST_NEG8", "strd r2, r3, [r5], #-8"),
    ("STRD_REG", "strd r2, r3, [r5, r6]"),
    ("STRD_REG_NEG", "strd r2, r3, [r5, -r6]"),
    ("STRD_PRE_NEG8_WB", "strd r0, r1, [r5, #-8]!"),
    # --- LDM / STM -------------------------------------------------------------
    ("LDMIA", "ldmia r0, {r1, r2}"),
    ("LDMIA_WB", "ldmia r0!, {r1, r2}"),
    ("LDMIB", "ldmib r0, {r1-r3}"),
    ("LDMDA_WB", "ldmda r0!, {r1-r3}"),
    ("LDMDB_WB", "ldmdb r0!, {r1-r3}"),
    ("LDM_PC", "ldmia r0, {r1-r3, pc}"),
    ("LDM_PC_WB", "ldmia r0!, {r1-r3, pc}"),
    ("POP_PC", "pop {r0, pc}"),
    ("PUSH_LR", "push {r0-r2, lr}"),
    ("STMIA_WB", "stmia r0!, {r1, r2}"),
    ("STMIB", "stmib r0, {r1, r2}"),
    ("STMDA_WB", "stmda r0!, {r1, r2}"),
    ("STMDB_WB", "stmdb r0!, {r1, r2}"),
    ("STMIB_BASE_IN_LIST", "stmib r0, {r0-r3}"),
    ("STMDB_BASE_IN_LIST_WB", "stmdb r0!, {r0-r3}"),
    ("STM_PC", "stmia r0, {pc}"),
    ("LDM_S", "ldm r0, {r1, r2}^"),
    ("STM_S", "stm r0, {r1, r2}^"),
    # --- exclusives ------------------------------------------------------------
    ("LDREX", "ldrex r0, [r1]"),
    ("STREX", "strex r2, r0, [r1]"),
    ("LDREXB", "ldrexb r0, [r1]"),
    ("STREXB", "strexb r2, r0, [r1]"),
    ("LDREXH", "ldrexh r0, [r1]"),
    ("STREXH", "strexh r2, r0, [r1]"),
    ("LDREXD", "ldrexd r0, [r4]"),
    ("STREXD", "strexd r2, r0, [r4]"),
    ("CLREX", "clrex"),
    # Not V12 encodings: they share bits 7-4 == 1001 with the exclusive space.  arm_mem
    # must return UNDEF for them (the core routes them to arm_dp); tested in t_dispatch.
    ("MUL", "mul r0, r1, r2"),
    ("MLA", "mla r0, r1, r2, r3"),
    ("UMULL", "umull r0, r1, r2, r3"),
    ("MLS", "mls r0, r1, r2, r3"),
    ("SWP", "swp r0, r1, [r2]"),
    # --- preload ---------------------------------------------------------------
    ("PLD", "pld [r1]"),
    ("PLD_IMM4", "pld [r1, #4]"),
    ("PLD_NEG_REG", "pld [r1, -r2]"),
    ("PLD_REG_LSL2", "pld [r1, r2, lsl #2]"),
    ("PLD_REG_RRX", "pld [r1, r2, rrx]"),
    ("PLI", "pli [r1]"),
    ("PLI_IMM4", "pli [r1, #4]"),
    ("PLI_REG", "pli [r1, r2]"),
    ("PLI_REG_NEG", "pli [r1, -r2]"),
    # --- branches / system -----------------------------------------------------
    ("B_FWD", "b #0x100"),
    ("B_BACK", "b #-8"),
    ("B_EQ", "beq #0x80"),
    ("BL_FWD", "bl #0x100"),
    ("BL_BACK", "bl #-8"),
    ("BLX_H1", "blx #0x1122"),
    ("BLX_H0", "blx #0x1120"),
    ("BX", "bx r0"),
    ("BLX_REG", "blx r0"),
    ("BX_PC", "bx pc"),
    ("SVC0", "svc #0"),
    ("SVC_BIG", "svc #0x123456"),
    ("BKPT0", "bkpt #0"),
    ("BKPT_BIG", "bkpt #0x1234"),
    ("UDF0", "udf #0"),
    ("UDF_BIG", "udf #0x123"),
    ("NOP", "nop"),
    ("YIELD", "yield"),
    ("WFE", "wfe"),
    ("WFI", "wfi"),
    ("SEV", "sev"),
    ("DBG0", "dbg #0"),
    ("DBG15", "dbg #15"),
    ("DMB_SY", "dmb sy"),
    ("DMB_ISH", "dmb ish"),
    ("DSB_SY", "dsb sy"),
    ("DSB_ISHST", "dsb ishst"),
    ("ISB_SY", "isb sy"),
    ("MRC_TP0", "mrc p15, 0, r0, c13, c0, 3"),
    ("MRC_TP9", "mrc p15, 0, r9, c13, c0, 3"),
    ("MRC_TP_COND", "mrceq p15, 0, r5, c13, c0, 3"),
    ("MRC_OTHER", "mrc p15, 0, r0, c13, c0, 2"),
    ("MCR_SAME", "mcr p15, 0, r0, c13, c0, 3"),
    ("MRC_CP14", "mrc p14, 0, r0, c13, c0, 3"),
    ("MCRR", "mcrr p15, 0, r0, r1, c2"),
    ("MRRC", "mrrc p15, 0, r0, r1, c2"),
]

# PLDW only assembles with the MP extensions feature; same triple, one extra feature.
MP = [
    ("PLDW", "pldw [r1]"),
    ("PLDW_IMM4", "pldw [r1, #4]"),
    ("PLDW_NEG_REG", "pldw [r1, -r2]"),
    ("PLDW_REG_LSL2", "pldw [r1, r2, lsl #2]"),
]

ENC_RE = re.compile(r"@ encoding: \[0x([0-9a-fA-F]{2}),0x([0-9a-fA-F]{2}),"
                    r"0x([0-9a-fA-F]{2}),0x([0-9a-fA-F]{2})\]")


def emit_asm(cases):
    print(".syntax unified")
    print(".arm")
    for _, text in cases:
        print("\t" + text)


def parse(path):
    words = []
    with open(path, "r", encoding="utf-8") as fp:
        for line in fp:
            m = ENC_RE.search(line)
            if m:
                b = [int(x, 16) for x in m.groups()]
                words.append(b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24)
    return words


def emit_header(main_path, mp_path):
    cases = MAIN + MP
    words = parse(main_path) + parse(mp_path)
    if len(words) != len(cases):
        sys.stderr.write("gen_enc: %d encodings for %d cases\n" % (len(words), len(cases)))
        sys.exit(1)
    print("/* generated by test_v12/gen_enc.py from llvm-mc output -- do not edit */")
    for (name, _), w in zip(cases, words):
        print("#define E_%s 0x%08Xu" % (name, w))
    print("struct enc_entry { const char *name; uint32_t word; };")
    print("static const struct enc_entry ALL_ENC[] = {")
    for (name, _), w in zip(cases, words):
        print('\t{ "%s", 0x%08Xu },' % (name, w))
    print("};")
    print("#define ALL_ENC_N (sizeof ALL_ENC / sizeof ALL_ENC[0])")


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "asm":
        emit_asm(MAIN)
    elif len(sys.argv) >= 2 and sys.argv[1] == "asm-mp":
        emit_asm(MP)
    elif len(sys.argv) >= 4 and sys.argv[1] == "header":
        emit_header(sys.argv[2], sys.argv[3])
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
