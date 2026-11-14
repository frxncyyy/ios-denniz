# SPDX-License-Identifier: MIT
# misc.s -- the Advanced SIMD data-processing encodings used by t_misc.c (package V20).
# Format: name: instruction   (assembled by mkenc.py with llvm-mc; never hand-typed hex)

# ---- VREV64 / VREV32 / VREV16
rev64_8_d:      vrev64.8 d0, d1
rev64_16_d:     vrev64.16 d0, d1
rev64_32_d:     vrev64.32 d0, d1
rev64_8_q:      vrev64.8 q0, q1
rev64_16_q:     vrev64.16 q0, q1
rev64_32_q:     vrev64.32 q0, q1
rev32_8_d:      vrev32.8 d0, d1
rev32_16_d:     vrev32.16 d0, d1
rev32_8_q:      vrev32.8 q0, q1
rev32_16_q:     vrev32.16 q0, q1
rev16_8_d:      vrev16.8 d0, d1
rev16_8_q:      vrev16.8 q0, q1
rev64_32_hi:    vrev64.32 d16, d17

# ---- VPADDL / VPADAL
paddl_s8_d:     vpaddl.s8 d0, d1
paddl_s16_d:    vpaddl.s16 d0, d1
paddl_s32_d:    vpaddl.s32 d0, d1
paddl_u8_d:     vpaddl.u8 d0, d1
paddl_u16_q:    vpaddl.u16 q0, q1
paddl_u32_q:    vpaddl.u32 q0, q1
padal_s8_d:     vpadal.s8 d0, d1
padal_u16_d:    vpadal.u16 d0, d1
padal_s32_q:    vpadal.s32 q0, q1
padal_u8_q:     vpadal.u8 q0, q1

# ---- VCLS / VCLZ / VCNT / VMVN
cls_s8_d:       vcls.s8 d0, d1
cls_s16_d:      vcls.s16 d0, d1
cls_s32_d:      vcls.s32 d0, d1
cls_s8_q:       vcls.s8 q0, q1
clz_i8_d:       vclz.i8 d0, d1
clz_i16_q:      vclz.i16 q0, q1
clz_i32_q:      vclz.i32 q0, q1
cnt_8_d:        vcnt.8 d0, d1
cnt_8_q:        vcnt.8 q0, q1
mvn_d:          vmvn d0, d1
mvn_q:          vmvn q0, q1

# ---- VQABS / VQNEG
qabs_s8_d:      vqabs.s8 d0, d1
qabs_s16_d:     vqabs.s16 d0, d1
qabs_s32_q:     vqabs.s32 q0, q1
qneg_s8_d:      vqneg.s8 d0, d1
qneg_s16_q:     vqneg.s16 q0, q1
qneg_s32_d:     vqneg.s32 d0, d1

# ---- VCMP #0, integer and float
cgt_s8:         vcgt.s8 d0, d1, #0
cge_s16:        vcge.s16 d0, d1, #0
ceq_s32:        vceq.i32 d0, d1, #0
cle_s8_q:       vcle.s8 q0, q1, #0
clt_s16_q:      vclt.s16 q0, q1, #0
cgt_s32_d:      vcgt.s32 d0, d1, #0
cgt_f32:        vcgt.f32 d0, d1, #0
cge_f32_q:      vcge.f32 q0, q1, #0
ceq_f32:        vceq.f32 d0, d1, #0
cle_f32_q:      vcle.f32 q0, q1, #0
clt_f32:        vclt.f32 d0, d1, #0

# ---- VABS / VNEG
abs_s8:         vabs.s8 d0, d1
abs_s16:        vabs.s16 d0, d1
abs_s32:        vabs.s32 d0, d1
neg_s8_q:       vneg.s8 q0, q1
neg_s16_q:      vneg.s16 q0, q1
neg_s32:        vneg.s32 d0, d1
abs_f32:        vabs.f32 d0, d1
neg_f32_q:      vneg.f32 q0, q1
abs_f32_q:      vabs.f32 q0, q1

# ---- VSWP
swp_d:          vswp d0, d1
swp_q:          vswp q0, q1
swp_hi:         vswp d16, d17

# ---- VTRN / VUZP / VZIP
trn_8_d:        vtrn.8 d0, d1
trn_16_d:       vtrn.16 d0, d1
trn_32_d:       vtrn.32 d0, d1
trn_8_q:        vtrn.8 q0, q1
trn_16_q:       vtrn.16 q0, q1
trn_32_q:       vtrn.32 q0, q1
uzp_8_d:        vuzp.8 d0, d1
uzp_16_d:       vuzp.16 d0, d1
uzp_8_q:        vuzp.8 q0, q1
uzp_16_q:       vuzp.16 q0, q1
uzp_32_q:       vuzp.32 q0, q1
zip_8_d:        vzip.8 d0, d1
zip_16_d:       vzip.16 d0, d1
zip_8_q:        vzip.8 q0, q1
zip_16_q:       vzip.16 q0, q1
zip_32_q:       vzip.32 q0, q1

# ---- VMOVN / VQMOVN / VQMOVUN
movn_16:        vmovn.i16 d0, q0
movn_32:        vmovn.i32 d0, q0
movn_64:        vmovn.i64 d0, q0
qmovn_s16:      vqmovn.s16 d0, q0
qmovn_s32:      vqmovn.s32 d0, q0
qmovn_u16:      vqmovn.u16 d0, q0
qmovn_u32:      vqmovn.u32 d0, q0
qmovun_s16:     vqmovun.s16 d0, q0
qmovun_s32:     vqmovun.s32 d0, q0
movn_32_hi:     vmovn.i32 d16, q9

# ---- VSHLL (maximum shift)
shll_8:         vshll.i8 q0, d1, #8
shll_16:        vshll.i16 q0, d1, #16
shll_32:        vshll.i32 q0, d1, #32

# ---- VCVT float <-> integer
cvt_f32_s32:    vcvt.f32.s32 d0, d1
cvt_f32_u32:    vcvt.f32.u32 d0, d1
cvt_s32_f32:    vcvt.s32.f32 d0, d1
cvt_u32_f32:    vcvt.u32.f32 d0, d1
cvt_f32_s32_q:  vcvt.f32.s32 q0, q1
cvt_s32_f32_q:  vcvt.s32.f32 q0, q1
cvt_u32_f32_q:  vcvt.u32.f32 q0, q1
cvt_f32_u32_q:  vcvt.f32.u32 q0, q1

# ---- VRECPE / VRSQRTE
recpe_u32:      vrecpe.u32 d0, d1
recpe_f32:      vrecpe.f32 d0, d1
rsqrte_u32:     vrsqrte.u32 d0, d1
rsqrte_f32:     vrsqrte.f32 d0, d1
recpe_f32_q:    vrecpe.f32 q0, q1
rsqrte_u32_q:   vrsqrte.u32 q0, q1
recpe_u32_q:    vrecpe.u32 q0, q1
rsqrte_f32_q:   vrsqrte.f32 q0, q1

# ---- VRINT (ARMv8)
rintn:          vrintn.f32 d0, d1
rinta:          vrinta.f32 d0, d1
rintp:          vrintp.f32 d0, d1
rintm:          vrintm.f32 d0, d1
rintx:          vrintx.f32 d0, d1
rintz:          vrintz.f32 d0, d1
rintn_q:        vrintn.f32 q0, q1
rintz_q:        vrintz.f32 q0, q1
rinta_q:        vrinta.f32 q0, q1

# ---- VCVT{A,N,P,M} (ARMv8)
cvta_s32:       vcvta.s32.f32 d0, d1
cvtn_s32:       vcvtn.s32.f32 d0, d1
cvtp_s32:       vcvtp.s32.f32 d0, d1
cvtm_s32:       vcvtm.s32.f32 d0, d1
cvta_u32:       vcvta.u32.f32 d0, d1
cvtn_u32:       vcvtn.u32.f32 d0, d1
cvtp_u32_q:     vcvtp.u32.f32 q0, q1
cvtm_u32_q:     vcvtm.u32.f32 q0, q1

# ---- VEXT
ext_d_0:        vext.8 d0, d1, d2, #0
ext_d_3:        vext.8 d0, d1, d2, #3
ext_d_4:        vext.8 d0, d1, d2, #4
ext_d_7:        vext.8 d0, d1, d2, #7
ext_d_16_1:     vext.16 d0, d1, d2, #1
ext_q_0:        vext.8 q0, q1, q2, #0
ext_q_4:        vext.8 q0, q1, q2, #4
ext_q_8:        vext.8 q0, q1, q2, #8
ext_q_15:       vext.8 q0, q1, q2, #15
ext_q_32_3:     vext.32 q0, q1, q2, #3
ext_q_64_1:     vext.64 q3, q0, q1, #1
ext_hi:         vext.8 d4, d20, d18, #5

# ---- VTBL / VTBX
tbl_1:          vtbl.8 d0, {d1}, d2
tbl_2:          vtbl.8 d0, {d1, d2}, d3
tbl_3:          vtbl.8 d0, {d1, d2, d3}, d4
tbl_4:          vtbl.8 d0, {d1, d2, d3, d4}, d5
tbx_1:          vtbx.8 d0, {d1}, d2
tbx_2:          vtbx.8 d0, {d1, d2}, d3
tbx_4:          vtbx.8 d0, {d1, d2, d3, d4}, d5
tbl_hi:         vtbl.8 d16, {d17, d18}, d19

# ---- VDUP (scalar lane)
dup_8:          vdup.8 d0, d1[3]
dup_16:         vdup.16 d0, d1[2]
dup_32:         vdup.32 d0, d1[1]
dup_8_q:        vdup.8 q0, d1[7]
dup_16_q:       vdup.16 q0, d1[1]
dup_32_q:       vdup.32 q0, d1[0]
dup_hi:         vdup.8 d16, d18[6]

# ---- one register, modified immediate
mov_i8_11:      vmov.i8 d0, #0x11
mov_i8_q:       vmov.i8 q0, #0x11
mov_i16_ff00:   vmov.i16 d0, #0xff00
mov_i32_ff:     vmov.i32 d0, #0xff
mov_i32_ff00:   vmov.i32 d0, #0xff00
mov_i32_ff0000: vmov.i32 d0, #0xff0000
mov_i32_ff000000: vmov.i32 d0, #0xff000000
mov_i32_q:      vmov.i32 q0, #0xff
mov_i64_ff:     vmov.i64 d0, #0xff
mov_i64_hi:     vmov.i64 d0, #0xff00000000000000
mov_i64_mix:    vmov.i64 d0, #0xff00ff00ff00ff00
mvn_i16:        vmvn.i16 d0, #0xff00
mvn_i32:        vmvn.i32 d0, #0xff
mvn_i32_q:      vmvn.i32 q0, #0xff
orr_i32:        vorr.i32 q0, #0xff
bic_i32:        vbic.i32 q0, #0xff
orr_i16:        vorr.i16 q0, #0xff00
bic_i16:        vbic.i16 q0, #0xff00
orr_i32_d:      vorr.i32 d0, #0xff
bic_i32_d:      vbic.i32 d0, #0xff
mov_f32_1:      vmov.f32 d0, #1.0
mov_f32_2:      vmov.f32 d0, #2.0
mov_f32_neg2:   vmov.f32 d0, #-2.0
mov_f32_0p5:    vmov.f32 d0, #0.5
mov_f32_q:      vmov.f32 q0, #1.0

# ---- not ours: must return N2A32_UNDEF
undef_f16_n:    vcvt.f16.f32 d0, q1
undef_f16_w:    vcvt.f32.f16 q0, d1
undef_3same:    vadd.i8 d0, d1, d2
undef_shift:    vshl.i8 d0, d1, #3
undef_vmovl:    vmovl.s16 q0, d1
undef_vld1:     vld1.8 {d0}, [r1]

# ---- VRECPE/VRSQRTE with the smallest inputs (clamping paths)
recpe_u32_small:  vrecpe.u32 d0, d1
rsqrte_u32_small: vrsqrte.u32 d0, d1
