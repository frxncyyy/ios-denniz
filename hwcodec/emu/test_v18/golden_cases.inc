/* SPDX-License-Identifier: MIT */
/*
 * golden_cases.inc -- hand-derived expected values for the V18 class.
 *
 * Each case below was worked out by hand from the ARM ARM pseudocode (the arithmetic is in the
 * comment), independently of ref_model.py and of the C implementation.  The encodings are the
 * llvm-mc words from golden_encodings.txt (E_G_* in enc_v18.h).
 *
 * d = old Vd, n = Vn, m = Vm, ed = expected Vd, qc = expected FPSCR.QC (APSR.Q).
 * For the shift forms Vn holds the shift counts (the third assembly operand) and Vm the values.
 */

/* ---- saturating add/sub ------------------------------------------------------------------- */

/* 0x7f + 1 = 128 > 127: saturate to 0x7f, QC. */
CASE_D("golden vqadd.s8 sat+", E_G_VQADD_S8, 0,
       UINT64_C(0x7f7f7f7f7f7f7f7f), UINT64_C(0x0101010101010101),
       UINT64_C(0x7f7f7f7f7f7f7f7f), 1)
/* -128 + -1 = -129: saturate to -128 (0x80), QC. */
CASE_D("golden vqadd.s8 sat-", E_G_VQADD_S8, 0,
       UINT64_C(0x8080808080808080), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x8080808080808080), 1)
/* 0x10 + 0x20 = 0x30, no saturation. */
CASE_D("golden vqadd.s8 plain", E_G_VQADD_S8, 0,
       UINT64_C(0x1010101010101010), UINT64_C(0x2020202020202020),
       UINT64_C(0x3030303030303030), 0)
/* Per-lane mix: 6,5,4,3,2,1 get +1; 0x80 (-128)+1 = -127 (0x81); 0x7f+1 saturates. */
CASE_D("golden vqadd.s8 mixed", E_G_VQADD_S8, 0,
       UINT64_C(0x7f80010203040506), UINT64_C(0x0101010101010101),
       UINT64_C(0x7f81020304050607), 1)
/* 0xff + 2 = 257 saturates to 0xff (unsigned), QC. */
CASE_D("golden vqadd.u8 sat", E_G_VQADD_U8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0202020202020202),
       UINT64_C(0xffffffffffffffff), 1)
/* 0x10 + 0x20 = 0x30 unsigned, no saturation. */
CASE_D("golden vqadd.u8 plain", E_G_VQADD_U8, 0,
       UINT64_C(0x1010101010101010), UINT64_C(0x2020202020202020),
       UINT64_C(0x3030303030303030), 0)
/* 0x7fff + 1 saturates to 0x7fff, QC. */
CASE_D("golden vqadd.s16 sat", E_G_VQADD_S16, 0,
       UINT64_C(0x7fff7fff7fff7fff), UINT64_C(0x0001000100010001),
       UINT64_C(0x7fff7fff7fff7fff), 1)
/* -32768 + -1 = -32769 saturates to -32768 (0x8000), QC. */
CASE_D("golden vqadd.s16 -1", E_G_VQADD_S16, 0,
       UINT64_C(0x8000800080008000), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x8000800080008000), 1)
/* 0xffff + 1 = 0x10000 saturates to 0xffff, QC. */
CASE_D("golden vqadd.u16 sat", E_G_VQADD_U16, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0001000100010001),
       UINT64_C(0xffffffffffffffff), 1)
/* 0x7fffffff + 1 saturates, QC. */
CASE_D("golden vqadd.s32 sat", E_G_VQADD_S32, 0,
       UINT64_C(0x7fffffff7fffffff), UINT64_C(0x0000000100000001),
       UINT64_C(0x7fffffff7fffffff), 1)
/* 0x7fffffffffffffff + 1 saturates, QC. */
CASE_D("golden vqadd.s64 sat", E_G_VQADD_S64, 0,
       UINT64_C(0x7fffffffffffffff), UINT64_C(0x0000000000000001),
       UINT64_C(0x7fffffffffffffff), 1)
/* 0xffffffffffffffff + 1 saturates (unsigned), QC. */
CASE_D("golden vqadd.u64 sat", E_G_VQADD_U64, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000001),
       UINT64_C(0xffffffffffffffff), 1)
/* 0 - 1 saturates to 0 (unsigned), QC. */
CASE_D("golden vqsub.u8 sat", E_G_VQSUB_U8, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0x0101010101010101),
       UINT64_C(0x0000000000000000), 1)
/* 0x7fff - 0xffff = 0x8000 (signed wraps? no: 32767 - (-1) = 32768 saturates), QC. */
CASE_D("golden vqsub.s16 sat", E_G_VQSUB_S16, 0,
       UINT64_C(0x7fff7fff7fff7fff), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x7fff7fff7fff7fff), 1)
/* -2^31 - 1 saturates to -2^31, QC. */
CASE_D("golden vqsub.s32 sat", E_G_VQSUB_S32, 0,
       UINT64_C(0x8000000080000000), UINT64_C(0x0000000100000001),
       UINT64_C(0x8000000080000000), 1)
/* -2^63 - 1 saturates to -2^63, QC. */
CASE_D("golden vqsub.s64 sat", E_G_VQSUB_S64, 0,
       UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000001),
       UINT64_C(0x8000000000000000), 1)
/* 0 - 1 saturates to 0 (64-bit unsigned), QC. */
CASE_D("golden vqsub.u64 sat", E_G_VQSUB_U64, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000001),
       UINT64_C(0x0000000000000000), 1)

/* ---- halving ------------------------------------------------------------------------------ */

/* (-1 + -1) >> 1 = -1 (arithmetic). */
CASE_D("golden vhadd.s8 -1-1", E_G_VHADD_S8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffffffffffff),
       UINT64_C(0xffffffffffffffff), 0)
/* (127 + 1) >> 1 = 64. */
CASE_D("golden vhadd.s8 127+1", E_G_VHADD_S8, 0,
       UINT64_C(0x7f7f7f7f7f7f7f7f), UINT64_C(0x0101010101010101),
       UINT64_C(0x4040404040404040), 0)
/* (-128 + -128) >> 1 = -128. */
CASE_D("golden vhadd.s8 -128", E_G_VHADD_S8, 0,
       UINT64_C(0x8080808080808080), UINT64_C(0x8080808080808080),
       UINT64_C(0x8080808080808080), 0)
/* (255 + 1) >> 1 = 128 unsigned. */
CASE_D("golden vhadd.u8 255+1", E_G_VHADD_U8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0101010101010101),
       UINT64_C(0x8080808080808080), 0)
/* (0x7fff + 1) >> 1 = 0x4000. */
CASE_D("golden vhadd.s16", E_G_VHADD_S16, 0,
       UINT64_C(0x7fff7fff7fff7fff), UINT64_C(0x0001000100010001),
       UINT64_C(0x4000400040004000), 0)
/* Unsigned VHSUB uses the esize+1-bit intermediate: 0 - 1 = 0x1ff, >> 1 = 0xff. */
CASE_D("golden vhsub.u8 0-1", E_G_VHSUB_U8, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0x0101010101010101),
       UINT64_C(0xffffffffffffffff), 0)
/* (-32768 - 32767) = -65535, >> 1 arithmetic = -32768. */
CASE_D("golden vhsub.s16", E_G_VHSUB_S16, 0,
       UINT64_C(0x8000800080008000), UINT64_C(0x7fff7fff7fff7fff),
       UINT64_C(0x8000800080008000), 0)
/* (-2 + 1 + 1) >> 1 = 0; (127 + 1 + 1) >> 1 = 64. */
CASE_D("golden vrhadd.s8", E_G_VRHADD_S8, 0,
       UINT64_C(0xfe7f7f7f7f7f7f7f), UINT64_C(0x0101010101010101),
       UINT64_C(0x0040404040404040), 0)
/* (255 + 255 + 1) >> 1 = 255 (unsigned). */
CASE_D("golden vrhadd.u8 max", E_G_VRHADD_U8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffffffffffff),
       UINT64_C(0xffffffffffffffff), 0)
/* (0xffff + 1 + 1) >> 1 = 0x8000. */
CASE_D("golden vrhadd.u16 round", E_G_VRHADD_U16, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0001000100010001),
       UINT64_C(0x8000800080008000), 0)

/* ---- compares / test ---------------------------------------------------------------------- */

/* Signed: -128 > 127 is false. */
CASE_D("golden vcgt.s8 false", E_G_VCGT_S8, 0,
       UINT64_C(0x8080808080808080), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0x0000000000000000), 0)
/* Signed: 127 > -128 is true -> all ones. */
CASE_D("golden vcgt.s8 true", E_G_VCGT_S8, 0,
       UINT64_C(0x7f7f7f7f7f7f7f7f), UINT64_C(0x8080808080808080),
       UINT64_C(0xffffffffffffffff), 0)
/* Unsigned: 0x80 > 0x7f is true. */
CASE_D("golden vcgt.u8", E_G_VCGT_U8, 0,
       UINT64_C(0x8080808080808080), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0xffffffffffffffff), 0)
/* Equal -> all ones (signed or unsigned). */
CASE_D("golden vcge.s16 equal", E_G_VCGE_S16, 0,
       UINT64_C(0x8000800080008000), UINT64_C(0x8000800080008000),
       UINT64_C(0xffffffffffffffff), 0)
/* One lane differs. */
CASE_D("golden vceq.i8", E_G_VCEQ_I8, 0,
       UINT64_C(0x5555555555555555), UINT64_C(0x5555555555555556),
       UINT64_C(0xffffffffffffff00), 0)
/* (0x0f & 0x10) == 0 in every lane; (0x0f & 0x01) != 0 -> ones. */
CASE_D("golden vtst.8 zero", E_G_VTST_8, 0,
       UINT64_C(0x0f0f0f0f0f0f0f0f), UINT64_C(0x1010101010101010),
       UINT64_C(0x0000000000000000), 0)
CASE_D("golden vtst.8 set", E_G_VTST_8, 0,
       UINT64_C(0x0f0f0f0f0f0f0f0f), UINT64_C(0x0101010101010101),
       UINT64_C(0xffffffffffffffff), 0)

/* ---- shifts by signed variable ------------------------------------------------------------ */

/* Signed: -127 << 1 = -254 truncated to 8 bits = 0x02. */
CASE_D("golden vshl.s8 lsl", E_G_VSHL_S8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x8181818181818181),
       UINT64_C(0x0202020202020202), 0)
/* Signed: -128 >> 1 (arithmetic) = -64 = 0xc0. */
CASE_D("golden vshl.s8 asr", E_G_VSHL_S8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x8080808080808080),
       UINT64_C(0xc0c0c0c0c0c0c0c0), 0)
/* Shift count 8 == element size: all bits shifted out. */
CASE_D("golden vshl.s8 shift8", E_G_VSHL_S8, 0,
       UINT64_C(0x0808080808080808), UINT64_C(0x8181818181818181),
       UINT64_C(0x0000000000000000), 0)
/* Shift count -127: signed arithmetic shift gives -1 for negative values. */
CASE_D("golden vshl.s8 asr127", E_G_VSHL_S8, 0,
       UINT64_C(0x8181818181818181), UINT64_C(0x8080808080808080),
       UINT64_C(0xffffffffffffffff), 0)
/* Unsigned: 0x81 >> 1 = 0x40 (logical). */
CASE_D("golden vshl.u8 lsr", E_G_VSHL_U8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x8181818181818181),
       UINT64_C(0x4040404040404040), 0)
/* Signed 16-bit: -32768 >> 2 (arithmetic) = -8192 = 0xe000. */
CASE_D("golden vshl.s16 asr2", E_G_VSHL_S16, 0,
       UINT64_C(0xfffefffefffefffe), UINT64_C(0x8000800080008000),
       UINT64_C(0xe000e000e000e000), 0)
/* Signed 64-bit: -2^63 >> 1 = 0xc000000000000000. */
CASE_D("golden vshl.s64 asr", E_G_VSHL_S64, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x8000000000000000),
       UINT64_C(0xc000000000000000), 0)
/* Saturating left shift: 127 << 1 = 254 saturates to 127, QC. */
CASE_D("golden vqshl.s8 sat", E_G_VQSHL_S8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0x7f7f7f7f7f7f7f7f), 1)
/* 64 << 1 = 128 saturates to 127, QC. */
CASE_D("golden vqshl.s8 sat2", E_G_VQSHL_S8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x4040404040404040),
       UINT64_C(0x7f7f7f7f7f7f7f7f), 1)
/* -64 << 1 = -128: representable, no saturation. */
CASE_D("golden vqshl.s8 neg", E_G_VQSHL_S8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0xc0c0c0c0c0c0c0c0),
       UINT64_C(0x8080808080808080), 0)
/* Right shift by -1: -127 >> 1 = -64, no saturation. */
CASE_D("golden vqshl.s8 rshift", E_G_VQSHL_S8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x8181818181818181),
       UINT64_C(0xc0c0c0c0c0c0c0c0), 0)
/* Unsigned: 0x81 << 1 = 0x102 saturates to 0xff, QC. */
CASE_D("golden vqshl.u8 sat", E_G_VQSHL_U8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x8181818181818181),
       UINT64_C(0xffffffffffffffff), 1)
/* Unsigned: 0x40 << 1 = 0x80, no saturation. */
CASE_D("golden vqshl.u8 plain", E_G_VQSHL_U8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x4040404040404040),
       UINT64_C(0x8080808080808080), 0)
/* Signed 16-bit: 0x4000 << 1 = 0x8000 saturates to 0x7fff, QC. */
CASE_D("golden vqshl.s16 sat", E_G_VQSHL_S16, 0,
       UINT64_C(0x0001000100010001), UINT64_C(0x4000400040004000),
       UINT64_C(0x7fff7fff7fff7fff), 1)
/* Rounding right shift: (-127 + 1) >> 1 = -63 = 0xc1. */
CASE_D("golden vrshl.s8 round", E_G_VRSHL_S8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x8181818181818181),
       UINT64_C(0xc1c1c1c1c1c1c1c1), 0)
/* (-1 + 1) >> 1 = 0. */
CASE_D("golden vrshl.s8 zero", E_G_VRSHL_S8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x0000000000000000), 0)
/* Unsigned: (255 + 1) >> 1 = 128 = 0x80. */
CASE_D("golden vrshl.u8 round", E_G_VRSHL_U8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x8080808080808080), 0)
/* Unsigned: (5 + 2) >> 2 = 1. */
CASE_D("golden vrshl.u8 r2", E_G_VRSHL_U8, 0,
       UINT64_C(0xfefefefefefefefe), UINT64_C(0x0505050505050505),
       UINT64_C(0x0101010101010101), 0)
/* Signed 16-bit: (-32768 + 2) >> 2 = floor(-32766/4) = -8192. */
CASE_D("golden vrshl.s16 round", E_G_VRSHL_S16, 0,
       UINT64_C(0xfffefffefffefffe), UINT64_C(0x8000800080008000),
       UINT64_C(0xe000e000e000e000), 0)
/* Saturating rounding left shift: 127 << 1 saturates, QC. */
CASE_D("golden vqrshl.s8 sat", E_G_VQRSHL_S8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0x7f7f7f7f7f7f7f7f), 1)
/* Saturating rounding right shift: (-128 + 1) >> 1 = -64. */
CASE_D("golden vqrshl.s8 round", E_G_VQRSHL_S8, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x8080808080808080),
       UINT64_C(0xc0c0c0c0c0c0c0c0), 0)
/* Unsigned: 0x81 << 1 saturates, QC. */
CASE_D("golden vqrshl.u8 sat", E_G_VQRSHL_U8, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x8181818181818181),
       UINT64_C(0xffffffffffffffff), 1)

/* ---- max/min/abd/aba ---------------------------------------------------------------------- */

/* Signed max: max(-128, 127) = 127. */
CASE_D("golden vmax.s8", E_G_VMAX_S8, 0,
       UINT64_C(0x8080808080808080), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0x7f7f7f7f7f7f7f7f), 0)
/* Unsigned max: max(0x80, 0x7f) = 0x80. */
CASE_D("golden vmax.u8", E_G_VMAX_U8, 0,
       UINT64_C(0x8080808080808080), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0x8080808080808080), 0)
/* Signed min: min(-32768, 32767) = -32768. */
CASE_D("golden vmin.s16", E_G_VMIN_S16, 0,
       UINT64_C(0x8000800080008000), UINT64_C(0x7fff7fff7fff7fff),
       UINT64_C(0x8000800080008000), 0)
/* Unsigned min: min(0xffffffff, 0) = 0. */
CASE_D("golden vmin.u32", E_G_VMIN_U32, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000000),
       UINT64_C(0x0000000000000000), 0)
/* Signed abd: |(-128) - 127| = 255 = 0xff. */
CASE_D("golden vabd.s8", E_G_VABD_S8, 0,
       UINT64_C(0x8080808080808080), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0xffffffffffffffff), 0)
/* Unsigned abd: |0 - 0xffff| = 0xffff (16-bit). */
CASE_D("golden vabd.u16", E_G_VABD_U16, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0xffffffffffffffff),
       UINT64_C(0xffffffffffffffff), 0)
/* VABA.U8: 0x10 + |0 - 5| = 0x15. */
CASE_D("golden vaba.u8", E_G_VABA_U8, UINT64_C(0x1010101010101010),
       UINT64_C(0x0000000000000000), UINT64_C(0x0505050505050505),
       UINT64_C(0x1515151515151515), 0)
/* VABA.S8 accumulates modulo 256: 0xff + 255 = 0xfe. */
CASE_D("golden vaba.s8 wrap", E_G_VABA_S8, UINT64_C(0xffffffffffffffff),
       UINT64_C(0x8080808080808080), UINT64_C(0x7f7f7f7f7f7f7f7f),
       UINT64_C(0xfefefefefefefefe), 0)

/* ---- multiplies --------------------------------------------------------------------------- */

/* 0x10 * 0x11 = 0x110, low byte = 0x10. */
CASE_D("golden vmul.i8", E_G_VMUL_I8, 0,
       UINT64_C(0x1010101010101010), UINT64_C(0x1111111111111111),
       UINT64_C(0x1010101010101010), 0)
/* 0x0100 * 0x0100 = 0x10000, low half = 0. */
CASE_D("golden vmul.i16", E_G_VMUL_I16, 0,
       UINT64_C(0x0100010001000100), UINT64_C(0x0100010001000100),
       UINT64_C(0x0000000000000000), 0)
/* 0x00010000 * 0x00010000 = 2^32, low word = 0. */
CASE_D("golden vmul.i32", E_G_VMUL_I32, 0,
       UINT64_C(0x0001000000010000), UINT64_C(0x0001000000010000),
       UINT64_C(0x0000000000000000), 0)
/* Polynomial: (x+1)*(x^2+1) = x^3+x^2+x+1 = 0x0f. */
CASE_D("golden vmul.p8", E_G_VMUL_P8, 0,
       UINT64_C(0x0303030303030303), UINT64_C(0x0505050505050505),
       UINT64_C(0x0f0f0f0f0f0f0f0f), 0)
/* VMLA.I16: 1 + 2*3 = 7. */
CASE_D("golden vmla.i16", E_G_VMLA_I16, UINT64_C(0x0001000100010001),
       UINT64_C(0x0002000200020002), UINT64_C(0x0003000300030003),
       UINT64_C(0x0007000700070007), 0)
/* VMLS.I32: 0x10 - 3*7 = 0x10 - 21 = -5. */
CASE_D("golden vmls.i32", E_G_VMLS_I32, UINT64_C(0x0000001000000010),
       UINT64_C(0x0000000300000003), UINT64_C(0x0000000700000007),
       UINT64_C(0xfffffffbfffffffb), 0)
/* VQDMULH.S16: (2*0x4000*0x4000) >> 16 = 2^13 = 0x2000. */
CASE_D("golden vqdmulh.s16", E_G_VQDMULH_S16, 0,
       UINT64_C(0x4000400040004000), UINT64_C(0x4000400040004000),
       UINT64_C(0x2000200020002000), 0)
/* VQDMULH.S16: (2 * -2^15 * -2^15) >> 16 = 2^15 saturates to 0x7fff, QC. */
CASE_D("golden vqdmulh.s16 sat", E_G_VQDMULH_S16, 0,
       UINT64_C(0x8000800080008000), UINT64_C(0x8000800080008000),
       UINT64_C(0x7fff7fff7fff7fff), 1)
/* VQDMULH.S16: (2*1*-1) >> 16 = -1 (arithmetic shift). */
CASE_D("golden vqdmulh.s16 -1", E_G_VQDMULH_S16, 0,
       UINT64_C(0x0001000100010001), UINT64_C(0xffffffffffffffff),
       UINT64_C(0xffffffffffffffff), 0)
/* VQDMULH.S32: (2 * 2^30 * 2^30) >> 32 = 2^29 = 0x20000000. */
CASE_D("golden vqdmulh.s32", E_G_VQDMULH_S32, 0,
       UINT64_C(0x4000000040000000), UINT64_C(0x4000000040000000),
       UINT64_C(0x2000000020000000), 0)
/* VQDMULH.S32: -2^31 * -2^31 doubling saturates, QC. */
CASE_D("golden vqdmulh.s32 sat", E_G_VQDMULH_S32, 0,
       UINT64_C(0x8000000080000000), UINT64_C(0x8000000080000000),
       UINT64_C(0x7fffffff7fffffff), 1)
/* VQRDMULH.S16: (2*1*0x4000 + 0x8000) >> 16 = 1. */
CASE_D("golden vqrdmulh.s16 round", E_G_VQRDMULH_S16, 0,
       UINT64_C(0x0001000100010001), UINT64_C(0x4000400040004000),
       UINT64_C(0x0001000100010001), 0)
/* VQRDMULH.S16: (2*1*-1 + 0x8000) >> 16 = (0x7ffe) >> 16 = 0. */
CASE_D("golden vqrdmulh.s16 zero", E_G_VQRDMULH_S16, 0,
       UINT64_C(0x0001000100010001), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x0000000000000000), 0)
/* VQRDMULH.S32: (2 * 2^30 * 2^30 + 2^31) >> 32 = 0x20000000 (the rounding bit is below the shift). */
CASE_D("golden vqrdmulh.s32", E_G_VQRDMULH_S32, 0,
       UINT64_C(0x4000000040000000), UINT64_C(0x4000000040000000),
       UINT64_C(0x2000000020000000), 0)

/* ---- logic -------------------------------------------------------------------------------- */

CASE_D("golden vand", E_G_VAND, 0,
       UINT64_C(0xf0f0f0f0f0f0f0f0), UINT64_C(0xff00ff00ff00ff00),
       UINT64_C(0xf000f000f000f000), 0)
CASE_D("golden vbic", E_G_VBIC, 0,
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0f0f0f0f0f0f0f0f),
       UINT64_C(0xf0f0f0f0f0f0f0f0), 0)
CASE_D("golden vorr", E_G_VORR, 0,
       UINT64_C(0xf000000000000000), UINT64_C(0x000000000000000f),
       UINT64_C(0xf00000000000000f), 0)
CASE_D("golden vorn", E_G_VORN, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
       UINT64_C(0xffffffffffffffff), 0)
CASE_D("golden veor", E_G_VEOR, 0,
       UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x5555555555555555), 0)
/* VBSL: mask in Vd -> (Vn & Vd) | (Vm & ~Vd). */
CASE_D("golden vbsl", E_G_VBSL, UINT64_C(0xff00ff00ff00ff00),
       UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0x5555555555555555),
       UINT64_C(0xaa55aa55aa55aa55), 0)
/* VBIT: (Vn & Vm) | (Vd & ~Vm). */
CASE_D("golden vbit", E_G_VBIT, UINT64_C(0xffffffffffffffff),
       UINT64_C(0x0000000000000000), UINT64_C(0x0f0f0f0f0f0f0f0f),
       UINT64_C(0xf0f0f0f0f0f0f0f0), 0)
/* VBIF: (Vd & Vm) | (Vn & ~Vm). */
CASE_D("golden vbif", E_G_VBIF, UINT64_C(0xffffffffffffffff),
       UINT64_C(0x0000000000000000), UINT64_C(0x0f0f0f0f0f0f0f0f),
       UINT64_C(0x0f0f0f0f0f0f0f0f), 0)

/* ---- pairwise ----------------------------------------------------------------------------- */

/* (1+2, 3+4) from Vn and (5+6, 7+8) from Vm. */
CASE_D("golden vpadd.i16", E_G_VPADD_I16, 0,
       UINT64_C(0x0004000300020001), UINT64_C(0x0008000700060005),
       UINT64_C(0x000f000b00070003), 0)
/* Same for bytes: 1+2, 3+4, 5+6, 7+8 from Vn, 9+10 ... 15+16 from Vm. */
CASE_D("golden vpadd.i8", E_G_VPADD_I8, 0,
       UINT64_C(0x0807060504030201), UINT64_C(0x100f0e0d0c0b0a09),
       UINT64_C(0x1f1b17130f0b0703), 0)
/* Signed pairwise max of (0x80=-128, 0x7f=127), (0x10,0x20), (0x30,0x40), (0x50,0x60)
 * and the same for Vm = 1..8. */
CASE_D("golden vpmax.s8", E_G_VPMAX_S8, 0,
       UINT64_C(0x6050403020107f80), UINT64_C(0x0807060504030201),
       UINT64_C(0x080604026040207f), 0)
/* Unsigned 16-bit pairwise max: max(3,0xffff)=0xffff, max(1,2)=2, max(5,6)=6, max(7,8)=8. */
CASE_D("golden vpmax.u16", E_G_VPMAX_U16, 0,
       UINT64_C(0x00020001ffff0003), UINT64_C(0x0008000700060005),
       UINT64_C(0x000800060002ffff), 0)
/* Signed 32-bit pairwise min: min(0x7fffffff, 0x80000000) = 0x80000000; min(0,0) = 0. */
CASE_D("golden vpmin.s32", E_G_VPMIN_S32, 0,
       UINT64_C(0x800000007fffffff), UINT64_C(0x0000000000000000),
       UINT64_C(0x0000000080000000), 0)
/* Unsigned 8-bit pairwise min. */
CASE_D("golden vpmin.u8", E_G_VPMIN_U8, 0,
       UINT64_C(0x6050403020107f80), UINT64_C(0x0807060504030201),
       UINT64_C(0x070503015030107f), 0)

/* ---- plain add/sub and the high-register encoding ----------------------------------------- */

/* 1 + 0xffffffffffffffff = 0 (mod 2^64). */
CASE_D("golden vadd.i64 wrap", E_G_VADD_I64, 0,
       UINT64_C(0x0000000000000001), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x0000000000000000), 0)
/* 0 - 1 = 0xffffffff in each 32-bit lane. */
CASE_D("golden vsub.i32", E_G_VSUB_I32, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000100000001),
       UINT64_C(0xffffffffffffffff), 0)
/* d16/d17/d18: 1 + 2 = 3 (checks the D/N/M register bits). */
CASE_D("golden vadd.i8 high regs", E_G_VADD_I8_HIGH, 0,
       UINT64_C(0x0101010101010101), UINT64_C(0x0202020202020202),
       UINT64_C(0x0303030303030303), 0)

/* ---- float32 ------------------------------------------------------------------------------ */

/* 1.0 + 2.0 = 3.0 (both lanes). */
CASE_D("golden vadd.f32", E_G_VADD_F32, 0,
       UINT64_C(0x3f8000003f800000), UINT64_C(0x4000000040000000),
       UINT64_C(0x4040000040400000), 0)
/* Denormal input is flushed to zero: 0x00000001 + 0 = +0. */
CASE_D("golden vadd.f32 denormal", E_G_VADD_F32, 0,
       UINT64_C(0x0000000100000001), UINT64_C(0x0000000000000000),
       UINT64_C(0x0000000000000000), 0)
/* Any NaN gives the default NaN (DN=1 in the standard FPSCR value). */
CASE_D("golden vadd.f32 qnan", E_G_VADD_F32, 0,
       UINT64_C(0x7fc000007fc00000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0x7fc000007fc00000), 0)
/* inf + -inf = default NaN. */
CASE_D("golden vadd.f32 inf-inf", E_G_VADD_F32, 0,
       UINT64_C(0x7f8000007f800000), UINT64_C(0xff800000ff800000),
       UINT64_C(0x7fc000007fc00000), 0)
/* 1.0 - 1.0 = +0; 0.5 - 1.0 = -0.5. */
CASE_D("golden vsub.f32", E_G_VSUB_F32, 0,
       UINT64_C(0x3f8000003f000000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0x00000000bf000000), 0)
/* 3.0 * 0.5 = 1.5. */
CASE_D("golden vmul.f32", E_G_VMUL_F32, 0,
       UINT64_C(0x4040000040400000), UINT64_C(0x3f0000003f000000),
       UINT64_C(0x3fc000003fc00000), 0)
/* Non-fused VMLA: (1+2^-23)*(1-2^-24) rounds to 1.0, then -1 + 1 = +0. */
CASE_D("golden vmla.f32 not fused", E_G_VMLA_F32, UINT64_C(0xbf800000bf800000),
       UINT64_C(0x3f8000013f800001), UINT64_C(0x3f7fffff3f7fffff),
       UINT64_C(0x0000000000000000), 0)
/* Fused VFMA: 1 - (1+2^-23)*(1-2^-24) + ... : d=-1, so the exact result is
 * 2^-24 - 2^-47 = 1.111...1(22 ones) * 2^-25: mantissa 0x7ffffe, exponent 0x66, = 0x337ffffe. */
CASE_D("golden vfma.f32 fused", E_G_VFMA_F32, UINT64_C(0xbf800000bf800000),
       UINT64_C(0x3f8000013f800001), UINT64_C(0x3f7fffff3f7fffff),
       UINT64_C(0x337ffffe337ffffe), 0)
/* Non-fused VMLS: 1 - round((1+2^-23)*(1-2^-24)) = 1 - 1 = +0. */
CASE_D("golden vmls.f32 not fused", E_G_VMLS_F32, UINT64_C(0x3f8000003f800000),
       UINT64_C(0x3f8000013f800001), UINT64_C(0x3f7fffff3f7fffff),
       UINT64_C(0x0000000000000000), 0)
/* Fused VFMS: 1 - (1+2^-23)*(1-2^-24) = -(2^-24 - 2^-47) = 0xb37ffffe. */
CASE_D("golden vfms.f32 fused", E_G_VFMS_F32, UINT64_C(0x3f8000003f800000),
       UINT64_C(0x3f8000013f800001), UINT64_C(0x3f7fffff3f7fffff),
       UINT64_C(0xb37ffffeb37ffffe), 0)
/* 1.0 == 1.0 -> ones; +0 == -0 -> ones; NaN == NaN -> zero. */
CASE_D("golden vceq.f32", E_G_VCEQ_F32, 0,
       UINT64_C(0x3f80000000000000), UINT64_C(0x3f80000080000000),
       UINT64_C(0xffffffffffffffff), 0)
CASE_D("golden vceq.f32 nan", E_G_VCEQ_F32, 0,
       UINT64_C(0x7fc000007fc00000), UINT64_C(0x7fc000007fc00000),
       UINT64_C(0x0000000000000000), 0)
/* -1 >= 1 is false; -0 >= +0 is true. */
CASE_D("golden vcge.f32", E_G_VCGE_F32, 0,
       UINT64_C(0xbf80000080000000), UINT64_C(0x3f80000000000000),
       UINT64_C(0x00000000ffffffff), 0)
/* 2 > 1 true; 1 > 1 false; NaN > x false. */
CASE_D("golden vcgt.f32", E_G_VCGT_F32, 0,
       UINT64_C(0x400000007fc00000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0xffffffff00000000), 0)
/* | -1 | >= | 1 | is true; | -1 | > | 1 | is false. */
CASE_D("golden vacge.f32", E_G_VACGE_F32, 0,
       UINT64_C(0xbf800000bf800000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0xffffffffffffffff), 0)
CASE_D("golden vacgt.f32", E_G_VACGT_F32, 0,
       UINT64_C(0xbf80000040000000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0x00000000ffffffff), 0)
/* max(+0, -0) = +0; max(1, 2) = 2; max(NaN, 1) = default NaN. */
CASE_D("golden vmax.f32 zeros", E_G_VMAX_F32, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0x8000000080000000),
       UINT64_C(0x0000000000000000), 0)
CASE_D("golden vmax.f32 nan", E_G_VMAX_F32, 0,
       UINT64_C(0x7fc000007fc00000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0x7fc000007fc00000), 0)
/* min(+0, -0) = -0; min(1, 2) = 1. */
CASE_D("golden vmin.f32 zeros", E_G_VMIN_F32, 0,
       UINT64_C(0x0000000000000000), UINT64_C(0x8000000080000000),
       UINT64_C(0x8000000080000000), 0)
CASE_D("golden vmin.f32", E_G_VMIN_F32, 0,
       UINT64_C(0x3f8000003f800000), UINT64_C(0x4000000040000000),
       UINT64_C(0x3f8000003f800000), 0)
/* VMAXNM: a single quiet NaN is ignored; a signalling NaN still gives the default NaN. */
CASE_D("golden vmaxnm.f32 qnan", E_G_VMAXNM_F32, 0,
       UINT64_C(0x7fc000003f800000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0x3f8000003f800000), 0)
CASE_D("golden vmaxnm.f32 snan", E_G_VMAXNM_F32, 0,
       UINT64_C(0x7f8000013f800000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0x7fc000003f800000), 0)
CASE_D("golden vmaxnm.f32 both nan", E_G_VMAXNM_F32, 0,
       UINT64_C(0x7fc000007fc00000), UINT64_C(0x7fc000007fc00000),
       UINT64_C(0x7fc000007fc00000), 0)
/* VMINNM ignores a single quiet NaN. */
CASE_D("golden vminnm.f32 qnan", E_G_VMINNM_F32, 0,
       UINT64_C(0x7fc000007fc00000), UINT64_C(0x4000000040000000),
       UINT64_C(0x4000000040000000), 0)
/* |1 - 3| = 2; |-1 - 1| = 2. */
CASE_D("golden vabd.f32", E_G_VABD_F32, 0,
       UINT64_C(0x3f800000bf800000), UINT64_C(0x404000003f800000),
       UINT64_C(0x4000000040000000), 0)
/* 2 - 0.5*2 = 1.0; 0*inf -> 2.0; inf*1 -> -inf (the first operand is negated first);
 * NaN -> default NaN. */
CASE_D("golden vrecps.f32", E_G_VRECPS_F32, 0,
       UINT64_C(0x3f0000007f800000), UINT64_C(0x400000003f800000),
       UINT64_C(0x3f800000ff800000), 0)
CASE_D("golden vrecps.f32 0*inf", E_G_VRECPS_F32, 0,
       UINT64_C(0x000000007fc00000), UINT64_C(0x7f8000003f800000),
       UINT64_C(0x400000007fc00000), 0)
/* (3 - 0*inf)/2 = 1.5; (3 - 1*1)/2 = 1.0; (3 - inf*1)/2 = -inf. */
CASE_D("golden vrsqrts.f32", E_G_VRSQRTS_F32, 0,
       UINT64_C(0x000000003f800000), UINT64_C(0x7f8000003f800000),
       UINT64_C(0x3fc000003f800000), 0)
CASE_D("golden vrsqrts.f32 inf", E_G_VRSQRTS_F32, 0,
       UINT64_C(0x7f8000007f800000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0xff800000ff800000), 0)
/* VPADD.F32: (1+2, 3+4) = (3, 7). */
CASE_D("golden vpadd.f32", E_G_VPADD_F32, 0,
       UINT64_C(0x400000003f800000), UINT64_C(0x4080000040400000),
       UINT64_C(0x40e0000040400000), 0)
/* VPMAX.F32: (max(1,2), max(3,0.5)) = (2, 3). */
CASE_D("golden vpmax.f32", E_G_VPMAX_F32, 0,
       UINT64_C(0x400000003f800000), UINT64_C(0x3f00000040400000),
       UINT64_C(0x4040000040000000), 0)
/* VPMIN.F32: (min(1,2), min(3,0.5)) = (1, 0.5). */
CASE_D("golden vpmin.f32", E_G_VPMIN_F32, 0,
       UINT64_C(0x400000003f800000), UINT64_C(0x3f00000040400000),
       UINT64_C(0x3f0000003f800000), 0)

/* ---- Q forms ------------------------------------------------------------------------------ */

/* (1,2)+(5,6) = (6,8); (3,4)+(7,8) = (10,12). */
CASE_Q("golden vadd.i32 q", E_G_VADD_I32_Q,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
       UINT64_C(0x0000000200000001), UINT64_C(0x0000000400000003),
       UINT64_C(0x0000000600000005), UINT64_C(0x0000000800000007),
       UINT64_C(0x0000000800000006), UINT64_C(0x0000000c0000000a), 0)
/* Low half 0x7fff + 1 saturates; high half -32768 + -1 saturates to -32768. */
CASE_Q("golden vqadd.s16 q", E_G_VQADD_S16_Q,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
       UINT64_C(0x7fff7fff7fff7fff), UINT64_C(0x8000800080008000),
       UINT64_C(0x0001000100010001), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x7fff7fff7fff7fff), UINT64_C(0x8000800080008000), 1)
/* AND over both halves. */
CASE_Q("golden vand q", E_G_VAND_Q,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
       UINT64_C(0xf0f0f0f0f0f0f0f0), UINT64_C(0xff00ff00ff00ff00),
       UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000000),
       UINT64_C(0xf0f0f0f0f0f0f0f0), UINT64_C(0x0000000000000000), 0)
/* VBSL: mask in the destination (high half all zero -> take Vm). */
CASE_Q("golden vbsl q", E_G_VBSL_Q,
       UINT64_C(0xff00ff00ff00ff00), UINT64_C(0x0000000000000000),
       UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x5555555555555555), UINT64_C(0x1111111111111111),
       UINT64_C(0xaa55aa55aa55aa55), UINT64_C(0x1111111111111111), 0)
/* Float Q form: (1,2)+(1,1) = (2,3); (3,4)+(1,1) = (4,5). */
CASE_Q("golden vadd.f32 q", E_G_VADD_F32_Q,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
       UINT64_C(0x400000003f800000), UINT64_C(0x4080000040400000),
       UINT64_C(0x3f8000003f800000), UINT64_C(0x3f8000003f800000),
       UINT64_C(0x4040000040000000), UINT64_C(0x40a0000040800000), 0)
/* Q form of VQDMULH.S16: low half 0x2000 lanes; high half saturates, QC. */
CASE_Q("golden vqdmulh.s16 q", E_G_VQDMULH_S16_Q,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
       UINT64_C(0x4000400040004000), UINT64_C(0x8000800080008000),
       UINT64_C(0x4000400040004000), UINT64_C(0x8000800080008000),
       UINT64_C(0x2000200020002000), UINT64_C(0x7fff7fff7fff7fff), 1)
/* Q form of VSHL.S8: Vm = Q1 values, Vn = Q2 shift counts (1 then -1). */
CASE_Q("golden vshl.s8 q", E_G_VSHL_S8_Q,
       UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
       UINT64_C(0x0101010101010101), UINT64_C(0xffffffffffffffff),
       UINT64_C(0x8181818181818181), UINT64_C(0x8080808080808080),
       UINT64_C(0x0202020202020202), UINT64_C(0xc0c0c0c0c0c0c0c0), 0)
