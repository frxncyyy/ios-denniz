# SPDX-License-Identifier: MIT
# ldst.s -- the Advanced SIMD element/structure load/store encodings used by t_ldst.c (package V20).
# Format: name: instruction   (assembled by mkenc.py with llvm-mc; never hand-typed hex)

# ---- VLD1/VST1, one to four registers, all sizes, offset / register / writeback
vld1_8_1:       vld1.8 {d0}, [r1]
vld1_8_2:       vld1.8 {d0, d1}, [r1]
vld1_8_3:       vld1.8 {d0, d1, d2}, [r1]
vld1_8_4:       vld1.8 {d0, d1, d2, d3}, [r1]
vld1_16_1:      vld1.16 {d0}, [r1]
vld1_16_2:      vld1.16 {d0, d1}, [r1]
vld1_32_1:      vld1.32 {d0}, [r1]
vld1_32_4:      vld1.32 {d0, d1, d2, d3}, [r1]
vld1_64_1:      vld1.64 {d0}, [r1]
vld1_64_2:      vld1.64 {d0, d1}, [r1]
vst1_8_1:       vst1.8 {d0}, [r1]
vst1_8_4:       vst1.8 {d0, d1, d2, d3}, [r1]
vst1_16_2:      vst1.16 {d0, d1}, [r1]
vst1_32_2:      vst1.32 {d0, d1}, [r1]
vst1_64_1:      vst1.64 {d0}, [r1]
vld1_8_1_wb:    vld1.8 {d0}, [r1]!
vld1_8_2_wb:    vld1.8 {d0, d1}, [r1]!
vld1_32_1_reg:  vld1.32 {d0}, [r1], r2
vst1_8_1_wb:    vst1.8 {d0}, [r1]!
vst1_8_2_reg:   vst1.8 {d0, d1}, [r1], r2
vld1_8_2_al64:  vld1.8 {d0, d1}, [r1:64]
vld1_32_1_al64: vld1.32 {d0}, [r1:64]
vld1_64_2_al128: vld1.64 {d0, d1}, [r1:128]
vld1_8_hi:      vld1.8 {d16}, [r1]
vld1_8_hi2:     vld1.8 {d30, d31}, [r1]

# ---- VLD2/3/4, multiple structures
vld2_8_2:       vld2.8 {d0, d1}, [r1]
vld2_16_2:      vld2.16 {d0, d1}, [r1]
vld2_32_2:      vld2.32 {d0, d1}, [r1]
vld2_8_4:       vld2.8 {d0, d1, d2, d3}, [r1]
vld2_16_4:      vld2.16 {d0, d1, d2, d3}, [r1]
vld2_32_4:      vld2.32 {d0, d1, d2, d3}, [r1]
vst2_8_2:       vst2.8 {d0, d1}, [r1]
vst2_32_2:      vst2.32 {d0, d1}, [r1]
vst2_8_4:       vst2.8 {d0, d1, d2, d3}, [r1]
vld3_8_3:       vld3.8 {d0, d1, d2}, [r1]
vld3_16_3:      vld3.16 {d0, d1, d2}, [r1]
vld3_32_3:      vld3.32 {d0, d1, d2}, [r1]
vst3_8_3:       vst3.8 {d0, d1, d2}, [r1]
vld4_8_4:       vld4.8 {d0, d1, d2, d3}, [r1]
vld4_16_4:      vld4.16 {d0, d1, d2, d3}, [r1]
vld4_32_4:      vld4.32 {d0, d1, d2, d3}, [r1]
vst4_8_4:       vst4.8 {d0, d1, d2, d3}, [r1]
vst4_16_4:      vst4.16 {d0, d1, d2, d3}, [r1]
vld2_8_2_wb:    vld2.8 {d0, d1}, [r1]!
vld3_8_3_reg:   vld3.8 {d0, d1, d2}, [r1], r2
vld4_32_4_reg:  vld4.32 {d0, d1, d2, d3}, [r1], r2
vld2_8_2_al64:  vld2.8 {d0, d1}, [r1:64]
vld4_8_4_al128: vld4.8 {d0, d1, d2, d3}, [r1:128]
vld4_8_hi:      vld4.8 {d28, d29, d30, d31}, [r1]

# ---- single structure to one lane
vld1_8_l0:      vld1.8 {d0[0]}, [r1]
vld1_8_l3:      vld1.8 {d0[3]}, [r1]
# lane-load preservation: the other lanes of Dd and the other half of Qd must not change
vld1_8_d18_l3:  vld1.8 {d18[3]}, [r1]
vld1_32_d18_l0: vld1.32 {d18[0]}, [r1]
vld1_8_l7:      vld1.8 {d0[7]}, [r1]
vld1_16_l1:     vld1.16 {d0[1]}, [r1]
vld1_16_l3:     vld1.16 {d0[3]}, [r1]
vld1_32_l0:     vld1.32 {d0[0]}, [r1]
vld1_32_l1:     vld1.32 {d0[1]}, [r1]
vst1_8_l0:      vst1.8 {d0[0]}, [r1]
vst1_32_l1:     vst1.32 {d0[1]}, [r1]
vld2_8_l0:      vld2.8 {d0[0], d1[0]}, [r1]
vld2_8_l3:      vld2.8 {d0[3], d1[3]}, [r1]
vld2_16_l1:     vld2.16 {d0[1], d1[1]}, [r1]
vld2_32_l1:     vld2.32 {d0[1], d1[1]}, [r1]
vld3_8_l2:      vld3.8 {d0[2], d1[2], d2[2]}, [r1]
vld3_16_l0:     vld3.16 {d0[0], d1[0], d2[0]}, [r1]
vld4_8_l0:      vld4.8 {d0[0], d1[0], d2[0], d3[0]}, [r1]
vld4_16_l1:     vld4.16 {d0[1], d1[1], d2[1], d3[1]}, [r1]
vst2_8_l1:      vst2.8 {d0[1], d1[1]}, [r1]
vst4_32_l0:     vst4.32 {d0[0], d1[0], d2[0], d3[0]}, [r1]
vld2_8_l1_wb:   vld2.8 {d0[1], d1[1]}, [r1]!
vld1_8_l3_reg:  vld1.8 {d0[3]}, [r1], r2
vld4_8_l0_hi:   vld4.8 {d28[0], d29[0], d30[0], d31[0]}, [r1]
# ARMv8 register-stride-2 lane forms: .16 uses bit 5, .32 uses bit 6
vld2_16_l1_s2:  vld2.16 {d0[1], d2[1]}, [r1]
vld3_32_l0_s2:  vld3.32 {d0[0], d2[0], d4[0]}, [r1]
vld4_16_l0_s2:  vld4.16 {d16[0], d18[0], d20[0], d22[0]}, [r1]
vld4_32_l1_s2:  vld4.32 {d1[1], d3[1], d5[1], d7[1]}, [r1]
vst2_32_l1_s2:  vst2.32 {d0[1], d2[1]}, [r1]

# ---- single structure to all lanes
vld1_8_all:     vld1.8 {d0[]}, [r1]
vld1_16_all:    vld1.16 {d0[]}, [r1]
vld1_32_all:    vld1.32 {d0[]}, [r1]
vld2_8_all:     vld2.8 {d0[], d1[]}, [r1]
vld2_16_all:    vld2.16 {d0[], d1[]}, [r1]
vld2_32_all:    vld2.32 {d0[], d1[]}, [r1]
vld3_8_all:     vld3.8 {d0[], d1[], d2[]}, [r1]
vld3_32_all:    vld3.32 {d0[], d1[], d2[]}, [r1]
vld4_8_all:     vld4.8 {d0[], d1[], d2[], d3[]}, [r1]
vld4_16_all:    vld4.16 {d0[], d1[], d2[], d3[]}, [r1]
vld4_32_all:    vld4.32 {d0[], d1[], d2[], d3[]}, [r1]
vld1_32_all_wb: vld1.32 {d0[]}, [r1]!

# ---- not ours
undef_vadd:     vadd.i8 d0, d1, d2
undef_vld1_t32: vld1.8 {d0}, [r1]

# ---- ARMv8 register-stride and to-all-lanes variants
vld4_8_stride2:   vld4.8 {d0, d2, d4, d6}, [r1]
vld3_16_stride2:  vld3.16 {d0, d2, d4}, [r1]
vld2_32_stride2:  vld2.32 {d0, d2}, [r1]
vst4_8_stride2:   vst4.8 {d0, d2, d4, d6}, [r1]
vld1_8_all2:      vld1.8 {d0[], d1[]}, [r1]
vld2_8_all_s2:    vld2.8 {d0[], d2[]}, [r1]
vld4_32_all_al128: vld4.32 {d0[], d1[], d2[], d3[]}, [r1:128]
vld2_32_all_al64: vld2.32 {d0[], d2[]}, [r1:64]
vld1_8_sp:        vld1.8 {d0}, [sp]
vst1_8_sp:        vst1.8 {d0}, [sp]
vld1_8_sp_wb:     vld1.8 {d0}, [sp]!
