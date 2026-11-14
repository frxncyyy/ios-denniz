@ Thumb-2 VFP encodings for V17.  These are the same 32-bit VFP words with the A32 condition
@ nibble replaced by 1110/1101/1100; the core passes them to n2a32_vfp unchanged.  Tests confirm
@ they execute identically to their A32 twins.

	.syntax unified
	.thumb

enc_t32_vldr_s0_r0:      vldr s0, [r0]
enc_t32_vstr_d0_r1_p8:   vstr d0, [r1, #8]
enc_t32_vldr_s1_pc:      vldr s1, [pc, #16]
enc_t32_vldm_r2_d0_d1:   vldmia r2, {d0-d1}
enc_t32_vstm_r3w_s0_s1:  vstmia r3!, {s0-s1}
enc_t32_vpush_s0_s3:     vpush {s0-s3}
enc_t32_vpop_d0_d1:      vpop {d0-d1}
enc_t32_vadd_f32:        vadd.f32 s0, s1, s2
enc_t32_vadd_f64:        vadd.f64 d0, d1, d2
enc_t32_vsub_f64:        vsub.f64 d10, d11, d12
enc_t32_vmul_f64:        vmul.f64 d0, d1, d2
enc_t32_vdiv_f32:        vdiv.f32 s0, s1, s2
enc_t32_vmla_f64:        vmla.f64 d0, d1, d2
enc_t32_vfma_f32:        vfma.f32 s0, s1, s2
enc_t32_vabs_f32:        vabs.f32 s0, s1
enc_t32_vneg_f64:        vneg.f64 d0, d1
enc_t32_vsqrt_f64:       vsqrt.f64 d0, d1
enc_t32_vcmp_f32:        vcmp.f32 s0, s1
enc_t32_vcmpe_f64:       vcmpe.f64 d0, d1
enc_t32_vmov_r0_s0:      vmov r0, s0
enc_t32_vmov_s0_r0:      vmov s0, r0
enc_t32_vmov_d0_r0_r1:   vmov d0, r0, r1
enc_t32_vmov_r2_r3_d0:   vmov r2, r3, d0
enc_t32_vmov_32_r0_d0_1: vmov.32 r0, d0[1]
enc_t32_vmov_u8_r1_d1_2: vmov.u8 r1, d1[2]
enc_t32_vdup_32_d0_r0:   vdup.32 d0, r0
enc_t32_vmrs_apsr:       vmrs apsr_nzcv, fpscr
enc_t32_vmsr_r0:         vmsr fpscr, r0
enc_t32_vcvt_f64_s32:    vcvt.f64.s32 d0, s0
enc_t32_vcvt_s32_f64:    vcvt.s32.f64 s0, d0
enc_t32_vcvt_f32_f64:    vcvt.f32.f64 s0, d0
enc_t32_vcvt_f64_f32:    vcvt.f64.f32 d0, s0
enc_t32_vmov_f32_imm:    vmov.f32 s0, #1.0
enc_t32_vmov_f64_imm:    vmov.f64 d0, #-2.5
enc_t32_vseleq_f32:      vseleq.f32 s0, s1, s2
enc_t32_vmaxnm_f32:      vmaxnm.f32 s0, s1, s2
enc_t32_vrinta_f64:      vrinta.f64 d0, d1
enc_t32_vrintz_f32:      vrintz.f32 s0, s1
enc_t32_vcvta_s32_f32:   vcvta.s32.f32 s0, s1
enc_t32_vcvtb_f32_f16:   vcvtb.f32.f16 s0, s1
enc_t32_vcvtt_f16_f32:   vcvtt.f16.f32 s0, s1
enc_t32_fldmiax_r0:      fldmiax r0, {d0-d1}
enc_t32_fstmiax_r1:      fstmiax r1, {d0-d1}
