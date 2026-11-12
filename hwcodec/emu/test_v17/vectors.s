@ Assembly source for the V17 (VFP) test encodings.  Every instruction is labelled enc_*; run_tests.sh
@ assembles this file with llvm-mc and turns each label into one 0x........ literal.  No hex is typed
@ by hand anywhere.

	.syntax unified
	.arm

@ ---------------------------------------------------------------- VLDR / VSTR
enc_vldr_s0_r0:          vldr s0, [r0]
enc_vldr_s4_r1_p4:       vldr s4, [r1, #4]
enc_vldr_s8_r2_m8:       vldr s8, [r2, #-8]
enc_vldr_d1_r3_p16:      vldr d1, [r3, #16]
enc_vldr_d17_r4_p8:      vldr d17, [r4, #8]
enc_vldr_s2_pc:          vldr s2, [pc, #8]
enc_vldr_s3_sp_m4:       vldr s3, [sp, #-4]
enc_vstr_s0_r0:          vstr s0, [r0]
enc_vstr_s6_r1_p12:      vstr s6, [r1, #12]
enc_vstr_d2_r2_m16:      vstr d2, [r2, #-16]
enc_vstr_d18_sp:         vstr d18, [sp]
enc_vstr_s1_pc:          vstr s1, [pc, #4]

@ ---------------------------------------------------------------- VLDM / VSTM / VPUSH / VPOP / FLDMX
enc_vldmia_r0_s0_s2:     vldmia r0, {s0-s2}
enc_vldmia_r1w_d0_d1:    vldmia r1!, {d0-d1}
enc_vldmdb_r2w_d2_d3:    vldmdb r2!, {d2-d3}
enc_vstmia_r3_s4_s5:     vstmia r3, {s4-s5}
enc_vstmia_r4w_d4_d5:    vstmia r4!, {d4-d5}
enc_vstmdb_r5w_s8_s9:    vstmdb r5!, {s8-s9}
enc_vpush_s0_s3:         vpush {s0-s3}
enc_vpop_d0_d1:          vpop {d0-d1}
enc_vpush_d16_d17:       vpush {d16-d17}
enc_vpop_s16_s19:        vpop {s16-s19}
enc_vldm_r6_s0_s31:      vldmia r6, {s0-s31}
enc_fldmiax_r0_d0_d1:    fldmiax r0, {d0-d1}
enc_fldmiax_r1w_d2_d3:   fldmiax r1!, {d2-d3}
enc_fstmiax_r2_d4_d5:    fstmiax r2, {d4-d5}
enc_fstmiax_r3w_d6:      fstmiax r3!, {d6}
enc_fldmdbx_r4w_d8_d9:   fldmdbx r4!, {d8-d9}
enc_fstmdbx_r5w_d10:     fstmdbx r5!, {d10}

@ ---------------------------------------------------------------- VMOV core <-> FP
enc_vmov_r0_s0:          vmov r0, s0
enc_vmov_r1_s5:          vmov r1, s5
enc_vmov_s2_r3:          vmov s2, r3
enc_vmov_s7_r8:          vmov s7, r8
enc_vmov_d0_r0_r1:       vmov d0, r0, r1
enc_vmov_d5_r2_r3:       vmov d5, r2, r3
enc_vmov_d16_r4_r5:      vmov d16, r4, r5
enc_vmov_r4_r5_d2:       vmov r4, r5, d2
enc_vmov_r6_r7_d10:      vmov r6, r7, d10
enc_vmov_s0_s1_r0_r1:    vmov s0, s1, r0, r1
enc_vmov_r2_r3_s4_s5:    vmov r2, r3, s4, s5

@ ---------------------------------------------------------------- VMOV lane / VDUP
enc_vmov_r0_d0_0_32:     vmov.32 r0, d0[0]
enc_vmov_r1_d0_1_32:     vmov.32 r1, d0[1]
enc_vmov_r2_d5_0_32:     vmov.32 r2, d5[0]
enc_vmov_r3_d17_1_32:    vmov.32 r3, d17[1]
enc_vmov_d1_0_r4:        vmov.32 d1[0], r4
enc_vmov_d3_1_r5:        vmov.32 d3[1], r5
enc_vmov_d21_1_r6:       vmov.32 d21[1], r6
enc_vmov_r7_d2_2_16:     vmov.u16 r7, d2[2]
enc_vmov_r8_d2_3_u16:    vmov.u16 r8, d2[3]
enc_vmov_r9_d3_1_s16:    vmov.s16 r9, d3[1]
enc_vmov_d4_3_r10_16:    vmov.16 d4[3], r10
enc_vmov_r11_d5_5_u8:    vmov.u8 r11, d5[5]
enc_vmov_r12_d5_6_s8:    vmov.s8 r12, d5[6]
enc_vmov_d6_7_r13_8:     vmov.8 d6[7], r13
enc_vmov_d6_0_r0_8:      vmov.8 d6[0], r0
enc_vdup_8_d0_r0:        vdup.8 d0, r0
enc_vdup_16_d1_r2:       vdup.16 d1, r2
enc_vdup_32_d2_r3:       vdup.32 d2, r3
enc_vdup_8_q0_r4:        vdup.8 q0, r4
enc_vdup_16_q1_r5:       vdup.16 q1, r5
enc_vdup_32_q2_r6:       vdup.32 q2, r6
enc_vdup_32_d16_r7:      vdup.32 d16, r7

@ ---------------------------------------------------------------- VMOV immediate / register
enc_vmov_f32_s0_p1:      vmov.f32 s0, #1.0
enc_vmov_f32_s1_m2_5:    vmov.f32 s1, #-2.5
enc_vmov_f32_s4_p0_125:  vmov.f32 s4, #0.125
enc_vmov_f64_d0_p1:      vmov.f64 d0, #1.0
enc_vmov_f64_d1_m2_5:    vmov.f64 d1, #-2.5
enc_vmov_f64_d16_p3_75:  vmov.f64 d16, #3.75
enc_vmov_f32_s5_p2:      vmov.f32 s5, #2.0
enc_vmov_f32_s6_m2:      vmov.f32 s6, #-2.0
enc_vmov_f32_s7_m1:      vmov.f32 s7, #-1.0
enc_vmov_f32_s8_p4:      vmov.f32 s8, #4.0
enc_vmov_f64_d3_p2:      vmov.f64 d3, #2.0
enc_vmov_f64_d4_m2:      vmov.f64 d4, #-2.0
enc_vmov_f64_d5_m1:      vmov.f64 d5, #-1.0
enc_vmov_f32_s0_s1:      vmov.f32 s0, s1
enc_vmov_f32_s5_s9:      vmov.f32 s5, s9
enc_vmov_f64_d0_d1:      vmov.f64 d0, d1
enc_vmov_f64_d7_d20:     vmov.f64 d7, d20

@ ---------------------------------------------------------------- VMRS / VMSR
enc_vmrs_apsr:           vmrs apsr_nzcv, fpscr
enc_vmrs_r0:             vmrs r0, fpscr
enc_vmrs_r12:            vmrs r12, fpscr
enc_vmsr_r0:             vmsr fpscr, r0
enc_vmsr_r5:             vmsr fpscr, r5
enc_vmrs_r0_fpexc:       vmrs r0, fpexc
enc_vmsr_r0_fpexc:       vmsr fpexc, r0
enc_vmrs_r0_fpsid:       vmrs r0, fpsid
enc_vmrs_r0_mvfr0:       vmrs r0, mvfr0

@ ---------------------------------------------------------------- arithmetic (f32)
enc_vadd_f32:            vadd.f32 s0, s1, s2
enc_vadd_f32_hi:         vadd.f32 s10, s11, s12
enc_vadd_f32_negzero:    vadd.f32 s20, s21, s22
enc_vsub_f32:            vsub.f32 s0, s1, s2
enc_vsub_f32_hi:         vsub.f32 s10, s11, s12
enc_vmul_f32:            vmul.f32 s0, s1, s2
enc_vmul_f32_hi:         vmul.f32 s10, s11, s12
enc_vnmul_f32:           vnmul.f32 s0, s1, s2
enc_vnmul_f32_hi:        vnmul.f32 s10, s11, s12
enc_vdiv_f32:            vdiv.f32 s0, s1, s2
enc_vdiv_f32_hi:         vdiv.f32 s10, s11, s12
enc_vmla_f32:            vmla.f32 s0, s1, s2
enc_vmla_f32_hi:         vmla.f32 s10, s11, s12
enc_vmls_f32:            vmls.f32 s0, s1, s2
enc_vmls_f32_hi:         vmls.f32 s10, s11, s12
enc_vnmla_f32:           vnmla.f32 s0, s1, s2
enc_vnmls_f32:           vnmls.f32 s0, s1, s2
enc_vfma_f32:            vfma.f32 s0, s1, s2
enc_vfma_f32_hi:         vfma.f32 s10, s11, s12
enc_vfms_f32:            vfms.f32 s0, s1, s2
enc_vfnma_f32:           vfnma.f32 s0, s1, s2
enc_vfnms_f32:           vfnms.f32 s0, s1, s2

@ ---------------------------------------------------------------- arithmetic (f64)
enc_vadd_f64:            vadd.f64 d0, d1, d2
enc_vadd_f64_hi:         vadd.f64 d10, d11, d12
enc_vsub_f64:            vsub.f64 d0, d1, d2
enc_vsub_f64_hi:         vsub.f64 d10, d11, d12
enc_vmul_f64:            vmul.f64 d0, d1, d2
enc_vmul_f64_hi:         vmul.f64 d10, d11, d12
enc_vnmul_f64:           vnmul.f64 d0, d1, d2
enc_vdiv_f64:            vdiv.f64 d0, d1, d2
enc_vdiv_f64_hi:         vdiv.f64 d10, d11, d12
enc_vmla_f64:            vmla.f64 d0, d1, d2
enc_vmla_f64_hi:         vmla.f64 d10, d11, d12
enc_vmls_f64:            vmls.f64 d0, d1, d2
enc_vnmla_f64:           vnmla.f64 d0, d1, d2
enc_vnmls_f64:           vnmls.f64 d0, d1, d2
enc_vfma_f64:            vfma.f64 d0, d1, d2
enc_vfms_f64:            vfms.f64 d0, d1, d2
enc_vfnma_f64:           vfnma.f64 d0, d1, d2
enc_vfnms_f64:           vfnms.f64 d0, d1, d2

@ ---------------------------------------------------------------- unary
enc_vabs_f32:            vabs.f32 s0, s1
enc_vabs_f32_hi:         vabs.f32 s10, s11
enc_vabs_f64:            vabs.f64 d2, d3
enc_vneg_f32:            vneg.f32 s4, s5
enc_vneg_f32_hi:         vneg.f32 s14, s15
enc_vneg_f64:            vneg.f64 d6, d7
enc_vsqrt_f32:           vsqrt.f32 s8, s9
enc_vsqrt_f32_hi:        vsqrt.f32 s18, s19
enc_vsqrt_f64:           vsqrt.f64 d10, d11

@ ---------------------------------------------------------------- compare
enc_vcmp_f32:            vcmp.f32 s0, s1
enc_vcmpe_f32:           vcmpe.f32 s2, s3
enc_vcmp_f64:            vcmp.f64 d0, d1
enc_vcmpe_f64:           vcmpe.f64 d2, d3
enc_vcmp_f32_zero:       vcmp.f32 s4, #0
enc_vcmpe_f32_zero:      vcmpe.f32 s5, #0
enc_vcmp_f64_zero:       vcmp.f64 d4, #0
enc_vcmpe_f64_zero:      vcmpe.f64 d5, #0

@ ---------------------------------------------------------------- VCVT single <-> double / int
enc_vcvt_f64_f32:        vcvt.f64.f32 d0, s1
enc_vcvt_f64_f32_hi:     vcvt.f64.f32 d16, s20
enc_vcvt_f32_f64:        vcvt.f32.f64 s2, d3
enc_vcvt_f32_f64_hi:     vcvt.f32.f64 s20, d17
enc_vcvt_s32_f32:        vcvt.s32.f32 s0, s1
enc_vcvt_u32_f32:        vcvt.u32.f32 s2, s3
enc_vcvt_s32_f64:        vcvt.s32.f64 s4, d1
enc_vcvt_u32_f64:        vcvt.u32.f64 s5, d2
enc_vcvtr_s32_f32:       vcvtr.s32.f32 s6, s7
enc_vcvtr_u32_f32:       vcvtr.u32.f32 s8, s9
enc_vcvtr_s32_f64:       vcvtr.s32.f64 s10, d3
enc_vcvtr_u32_f64:       vcvtr.u32.f64 s11, d4
enc_vcvt_f32_s32:        vcvt.f32.s32 s12, s13
enc_vcvt_f32_u32:        vcvt.f32.u32 s14, s15
enc_vcvt_f64_s32:        vcvt.f64.s32 d8, s1
enc_vcvt_f64_u32:        vcvt.f64.u32 d9, s2

@ ---------------------------------------------------------------- VCVT fixed point
enc_vcvt_f32_s16:        vcvt.f32.s16 s0, s0, #2
enc_vcvt_f32_u16:        vcvt.f32.u16 s1, s1, #3
enc_vcvt_f32_s32_fx:     vcvt.f32.s32 s2, s2, #7
enc_vcvt_f32_u32_fx:     vcvt.f32.u32 s3, s3, #8
enc_vcvt_f64_s16:        vcvt.f64.s16 d0, d0, #2
enc_vcvt_f64_u32_fx:     vcvt.f64.u32 d1, d1, #10
enc_vcvt_s16_f32:        vcvt.s16.f32 s4, s4, #2
enc_vcvt_u16_f32:        vcvt.u16.f32 s5, s5, #3
enc_vcvt_s32_f32_fx:     vcvt.s32.f32 s6, s6, #7
enc_vcvt_u32_f32_fx:     vcvt.u32.f32 s7, s7, #8
enc_vcvt_s16_f64:        vcvt.s16.f64 d2, d2, #4
enc_vcvt_u32_f64_fx:     vcvt.u32.f64 d3, d3, #9
enc_vcvt_s32_f64_fx:     vcvt.s32.f64 d4, d4, #3
enc_vcvt_f64_s32b:       vcvt.f64.s32 d5, d5, #5

@ ---------------------------------------------------------------- half precision
enc_vcvtb_f32_f16:       vcvtb.f32.f16 s0, s1
enc_vcvtt_f32_f16:       vcvtt.f32.f16 s2, s3
enc_vcvtb_f16_f32:       vcvtb.f16.f32 s4, s5
enc_vcvtt_f16_f32:       vcvtt.f16.f32 s6, s7
enc_vcvtb_f32_f16_hi:    vcvtb.f32.f16 s8, s9
enc_vcvtt_f16_f32_hi:    vcvtt.f16.f32 s10, s11

@ ---------------------------------------------------------------- ARMv8 VSEL / VMAXNM / VMINNM
enc_vseleq_f32:          vseleq.f32 s0, s1, s2
enc_vselvs_f32:          vselvs.f32 s3, s4, s5
enc_vselge_f32:          vselge.f32 s6, s7, s8
enc_vselgt_f32:          vselgt.f32 s9, s10, s11
enc_vseleq_f64:          vseleq.f64 d0, d1, d2
enc_vselge_f64:          vselge.f64 d3, d4, d5
enc_vselgt_f64:          vselgt.f64 d16, d17, d18
enc_vselvs_f64:          vselvs.f64 d6, d7, d8
enc_vmaxnm_f32:          vmaxnm.f32 s0, s1, s2
enc_vminnm_f32:          vminnm.f32 s3, s4, s5
enc_vmaxnm_f64:          vmaxnm.f64 d0, d1, d2
enc_vminnm_f64:          vminnm.f64 d3, d4, d5
enc_vmaxnm_f32_hi:       vmaxnm.f32 s12, s13, s14
enc_vminnm_f64_hi:       vminnm.f64 d16, d17, d18

@ ---------------------------------------------------------------- ARMv8 VRINT
enc_vrinta_f32:          vrinta.f32 s0, s1
enc_vrintn_f32:          vrintn.f32 s2, s3
enc_vrintp_f32:          vrintp.f32 s4, s5
enc_vrintm_f32:          vrintm.f32 s6, s7
enc_vrinta_f64:          vrinta.f64 d0, d1
enc_vrintn_f64:          vrintn.f64 d2, d3
enc_vrintp_f64:          vrintp.f64 d4, d5
enc_vrintm_f64:          vrintm.f64 d6, d7
enc_vrintx_f32:          vrintx.f32 s8, s9
enc_vrintz_f32:          vrintz.f32 s10, s11
enc_vrintr_f32:          vrintr.f32 s12, s13
enc_vrintx_f64:          vrintx.f64 d8, d9
enc_vrintz_f64:          vrintz.f64 d10, d11
enc_vrintr_f64:          vrintr.f64 d12, d13

@ ---------------------------------------------------------------- ARMv8 VCVTA/N/P/M
enc_vcvta_s32_f32:       vcvta.s32.f32 s0, s1
enc_vcvtn_s32_f32:       vcvtn.s32.f32 s2, s3
enc_vcvtp_s32_f32:       vcvtp.s32.f32 s4, s5
enc_vcvtm_s32_f32:       vcvtm.s32.f32 s6, s7
enc_vcvta_u32_f32:       vcvta.u32.f32 s8, s9
enc_vcvtn_u32_f32:       vcvtn.u32.f32 s10, s11
enc_vcvtp_u32_f32:       vcvtp.u32.f32 s12, s13
enc_vcvtm_u32_f32:       vcvtm.u32.f32 s14, s15
enc_vcvta_s32_f64:       vcvta.s32.f64 s0, d1
enc_vcvtn_u32_f64:       vcvtn.u32.f64 s1, d2
enc_vcvtp_s32_f64:       vcvtp.s32.f64 s2, d3
enc_vcvtm_u32_f64:       vcvtm.u32.f64 s3, d4

@ ---------------------------------------------------------------- extra coverage
enc_vstr_s2_r3_m4:       vstr s2, [r3, #-4]
enc_vstr_d4_r6_p24:      vstr d4, [r6, #24]
enc_vpush_s16_s19:       vpush {s16-s19}
enc_vpop_s0_s2:          vpop {s0-s2}
enc_vpush_d0_d3:         vpush {d0-d3}
enc_vpop_d20_d21:        vpop {d20-d21}
enc_vmov_f32_s1_s2:      vmov.f32 s1, s2
enc_vmov_f64_d1_d2:      vmov.f64 d1, d2
enc_vcvt_f64_f32_qnan:   vcvt.f64.f32 d1, s2
enc_vcvt_f32_f64_qnan:   vcvt.f32.f64 s1, d2
enc_vcvt_f32_s32_rp:     vcvt.f32.s32 s16, s17
enc_vcvt_s32_f32_rtz:    vcvt.s32.f32 s18, s19
enc_vcvt_f32_s16_fb0:    vcvt.f32.s16 s20, s20, #0
enc_vcvt_s16_f32_fb0:    vcvt.s16.f32 s21, s21, #0
enc_vcvt_u32_f64_fb16:   vcvt.u32.f64 d6, d6, #16
enc_vcvt_f64_u16:        vcvt.f64.u16 d7, d7, #4
enc_vrintz_f64_neg:      vrintz.f64 d14, d15
enc_vrintr_f64_rp:       vrintr.f64 d16, d17
enc_vrintm_f32_neg:      vrintm.f32 s22, s23
enc_vrintp_f32_neg:      vrintp.f32 s24, s25
enc_vrintn_f32_neg:      vrintn.f32 s26, s27
enc_vrinta_f64_neg:      vrinta.f64 d18, d19
enc_vcvtn_s32_f64:       vcvtn.s32.f64 s4, d5
enc_vcvtp_u32_f32_b:     vcvtp.u32.f32 s5, s6
enc_vcvtm_s32_f32_neg:   vcvtm.s32.f32 s6, s7
enc_vcvta_u32_f64:       vcvta.u32.f64 s7, d8
enc_vmaxnm_both_snan:    vmaxnm.f32 s16, s17, s18
enc_vminnm_qnan_dn:      vminnm.f32 s19, s20, s21
enc_vrintx_qnan_dn:      vrintx.f32 s22, s23
enc_vdup_8_q1_r1:        vdup.8 q1, r1
enc_vdup_16_d17_r9:      vdup.16 d17, r9
enc_vmov_s8_r1_d7_2:     vmov.s8 r1, d7[2]
enc_vmov_u16_d8_3_r2:    vmov.u16 d8[3], r2
enc_vmov_d9_1_r3_32:     vmov.32 d9[1], r3
enc_vmov_r4_d10_1_32:    vmov.32 r4, d10[1]
enc_vselge_f32_false:    vselge.f32 s28, s29, s30
enc_vselgt_f64_false:    vselgt.f64 d20, d21, d22

	.syntax unified
	.thumb
enc_t32_vldr_d1_r4:      vldr d1, [r4, #-8]
enc_t32_vstr_s2_r5:      vstr s2, [r5, #4]
enc_t32_vpush_d2_d3:     vpush {d2-d3}
enc_t32_vpop_s0_s2:      vpop {s0-s2}
enc_t32_vmov_d1_r0_r1:   vmov d1, r0, r1
enc_t32_vmov_r4_r5_d1:   vmov r4, r5, d1
enc_t32_vcvt_f64_u32:    vcvt.f64.u32 d1, s1
enc_t32_vcvt_u16_f32:    vcvt.u16.f32 s1, s1, #2
enc_t32_vcvt_s16_f64:    vcvt.s16.f64 d1, d1, #3
enc_t32_vcvtn_s32_f32:   vcvtn.s32.f32 s1, s2
enc_t32_vcvtm_u32_f64:   vcvtm.u32.f64 s1, d1
enc_t32_vminnm_f64:      vminnm.f64 d1, d2, d3
enc_t32_vrintn_f32:      vrintn.f32 s1, s2
enc_t32_vrintm_f64:      vrintm.f64 d1, d2
enc_t32_vselvs_f32:      vselvs.f32 s1, s2, s3

