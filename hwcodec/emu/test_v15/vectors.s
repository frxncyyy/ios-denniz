@ SPDX-License-Identifier: MIT
@ Test encodings for package V15 (T32 32-bit data processing).
@ gen_enc.py assembles this file with llvm-mc and emits enc_gen.h mapping every
@ enc_* label to the 32-bit instruction word the assembler produced.
@ Every instruction here must be the 32-bit form: gen_enc.py rejects anything
@ that does not assemble to exactly one 32-bit word.
	.syntax unified
	.thumb
	.text

@ ---------------------------------------------------------------- region A
enc_and_01:
	and.w r1, r2, r3
enc_and_02:
	and.w r1, r2, r3, lsl #4
enc_and_03:
	ands.w r1, r2, r3, lsr #31
enc_and_04:
	and.w r1, r2, r3, asr #32
enc_and_05:
	ands.w r1, r2, r3, ror #7
enc_and_06:
	ands.w r1, r2, r3, rrx
enc_and_07:
	and.w r0, r1, r2, lsl #0
enc_and_08:
	tst.w r0, r1
enc_and_09:
	tst.w r0, r1, lsl #2
enc_bic_01:
	bic.w r4, r5, r6
enc_bic_02:
	bics.w r4, r5, r6, lsl #8
enc_bic_03:
	bic.w r4, r5, r6, ror #1
enc_orr_01:
	orr.w r7, r8, r9
enc_orr_02:
	orrs.w r7, r8, r9, ror #13
enc_orr_03:
	mov.w r10, r11
enc_orr_04:
	lsl.w r10, r11, #3
enc_orr_05:
	lsls.w r0, r1, #31
enc_orr_06:
	lsr.w r0, r1, #1
enc_orr_07:
	asr.w r0, r1, #32
enc_orr_08:
	ror.w r0, r1, #1
enc_orr_09:
	rrx r0, r1
enc_orr_10:
	lsrs.w r0, r1, #32
enc_orn_01:
	orn r0, r1, r2
enc_orn_02:
	orns r0, r1, r2, lsl #5
enc_orn_03:
	mvn.w r0, r1
enc_orn_04:
	mvn.w r0, r1, asr #4
enc_orn_05:
	mvns.w r0, r1
enc_eor_01:
	eor.w r0, r1, r2
enc_eor_02:
	eors.w r0, r1, r2, ror #31
enc_eor_03:
	teq.w r0, r1
enc_eor_04:
	teq.w r0, r1, lsl #16
enc_pkhbt_01:
	pkhbt r0, r1, r2
enc_pkhbt_02:
	pkhbt r0, r1, r2, lsl #8
enc_pkhtb_01:
	pkhtb r0, r1, r2, asr #1
enc_pkhtb_02:
	pkhtb r0, r1, r2, asr #16
enc_add_01:
	add.w r0, r1, r2
enc_add_02:
	adds.w r0, r1, r2
enc_add_03:
	adds.w r0, r1, r2, lsl #2
enc_add_04:
	add.w r0, r1, r2, ror #3
enc_add_05:
	cmn.w r0, r1
enc_add_06:
	cmn.w r0, r1, lsl #1
enc_adc_01:
	adc.w r0, r1, r2
enc_adc_02:
	adcs.w r0, r1, r2
enc_adc_03:
	adcs.w r0, r1, r2, lsl #4
enc_sbc_01:
	sbc.w r0, r1, r2
enc_sbc_02:
	sbcs.w r0, r1, r2, lsl #1
enc_sub_01:
	sub.w r0, r1, r2
enc_sub_02:
	subs.w r0, r1, r2
enc_sub_03:
	cmp.w r0, r1
enc_sub_04:
	cmp.w r0, r1, asr #1
enc_sub_05:
	subs.w r0, r1, r2, lsr #4
enc_rsb_01:
	rsb r0, r1, r2
enc_rsb_02:
	rsbs r0, r1, r2, lsl #2
enc_rsb_03:
	rsbs r0, r1, r2, ror #16

@ ---------------------------------------------------------------- region B
enc_bi_and_01:
	and.w r0, r1, #0xff
enc_bi_and_02:
	ands.w r0, r1, #0x80000000
enc_bi_and_03:
	tst.w r0, #0x00ff00ff
enc_bi_and_04:
	tst.w r0, #0xff000000
enc_bi_bic_01:
	bic.w r0, r1, #0xff
enc_bi_bic_02:
	bics.w r0, r1, #0x00ff00ff
enc_bi_eor_01:
	eor.w r0, r1, #0xaaaaaaaa
enc_bi_eor_02:
	teq.w r0, #0xf0000000
enc_bi_eor_03:
	eors.w r0, r1, #0xff00ff00
enc_bi_orr_01:
	orr.w r0, r1, #0x100
enc_bi_orr_02:
	orrs.w r0, r1, #0x80000000
enc_bi_orr_03:
	orr.w r0, r1, #0xab00ab
enc_bi_orr_04:
	orr.w r0, r1, #0x00ff00ff
enc_bi_orr_05:
	mov.w r0, #0x01010101
enc_bi_orr_06:
	mov.w r0, #0xff000000
enc_bi_orr_07:
	movs.w r0, #0
enc_bi_orn_01:
	orn.w r0, r1, #0x7
enc_bi_orn_02:
	mvn.w r0, #0xff
enc_bi_add_01:
	add.w r0, r1, #0x100
enc_bi_add_02:
	adds.w r0, r1, #0xff
enc_bi_add_03:
	add.w r0, r1, #0xab00ab
enc_bi_add_04:
	cmn.w r0, #0x1000
enc_bi_sub_01:
	sub.w r0, r1, #0x100
enc_bi_sub_02:
	subs.w r0, r1, #1
enc_bi_sub_03:
	cmp.w r0, #0x80
enc_bi_sub_04:
	subs.w r0, r1, #0xFF00FF00
enc_bi_rsb_01:
	rsb.w r0, r1, #0x1000
enc_bi_rsb_02:
	rsbs.w r0, r1, #0x80000000

@ ---------------------------------------------------------------- region C
enc_addw_01:
	addw r0, r1, #0x123
enc_addw_02:
	addw r0, r1, #0xf00
enc_addw_03:
	addw r0, sp, #0x7f0
enc_addw_04:
	addw sp, sp, #0x40
enc_subw_01:
	subw r0, r1, #0x123
enc_subw_02:
	subw r9, sp, #0x10
enc_movw_01:
	movw r0, #0x1234
enc_movw_02:
	movw r7, #0xffff
enc_movw_03:
	movw r12, #0x0001
enc_movt_01:
	movt r0, #0xabcd
enc_movt_02:
	movt r12, #0x8000
enc_ssat_01:
	ssat r0, #8, r1
enc_ssat_02:
	ssat r0, #1, r1, lsl #3
enc_ssat_03:
	ssat r0, #32, r1
enc_ssat_04:
	ssat r0, #16, r1, asr #20
enc_ssat_05:
	ssat r5, #12, r9, asr #1
enc_ssat16_01:
	ssat16 r0, #8, r1
enc_ssat16_02:
	ssat16 r0, #16, r1
enc_ssat16_03:
	ssat16 r3, #1, r4
enc_usat_01:
	usat r0, #8, r1
enc_usat_02:
	usat r0, #0, r1
enc_usat_03:
	usat r0, #31, r1, asr #1
enc_usat_04:
	usat r5, #16, r9, lsl #4
enc_usat16_01:
	usat16 r0, #8, r1
enc_usat16_02:
	usat16 r0, #15, r1
enc_sbfx_01:
	sbfx r0, r1, #4, #8
enc_sbfx_02:
	sbfx r0, r1, #0, #32
enc_sbfx_03:
	sbfx r0, r1, #31, #1
enc_sbfx_04:
	sbfx r5, r9, #3, #17
enc_ubfx_01:
	ubfx r0, r1, #4, #8
enc_ubfx_02:
	ubfx r0, r1, #0, #1
enc_ubfx_03:
	ubfx r0, r1, #31, #1
enc_ubfx_04:
	ubfx r5, r9, #3, #17
enc_bfi_01:
	bfi r0, r1, #8, #8
enc_bfi_02:
	bfi r0, r1, #0, #32
enc_bfi_03:
	bfi r5, r9, #3, #17
enc_bfc_01:
	bfc r0, #4, #12
enc_bfc_02:
	bfc r0, #0, #32
enc_bfc_03:
	bfc r5, #3, #17

@ ADR: the target is placed at a fixed offset so the immediate is known.
	.p2align 2
enc_adr_fwd:
	adr.w r1, .Ladr_fwd_target
	nop.w
.Ladr_fwd_target:
	nop.w
	nop.w
	nop.w
.Ladr_back_target:
	nop.w
enc_adr_back:
	adr.w r1, .Ladr_back_target

@ ---------------------------------------------------------------- region D, shifts
enc_lslr_01:
	lsl.w r0, r1, r2
enc_lslr_02:
	lsls.w r0, r1, r2
enc_lsrr_01:
	lsr.w r0, r1, r2
enc_lsrr_02:
	lsrs.w r0, r1, r2
enc_asrr_01:
	asr.w r0, r1, r2
enc_asrr_02:
	asrs.w r0, r1, r2
enc_rorr_01:
	ror.w r0, r1, r2
enc_rorr_02:
	rors.w r0, r1, r2

@ ---------------------------------------------------------------- region D, extend
enc_sxtb_01:
	sxtb.w r0, r1
enc_sxtb_02:
	sxtb.w r0, r1, ror #8
enc_sxtb_03:
	sxtb.w r0, r1, ror #16
enc_sxtb_04:
	sxtb.w r0, r1, ror #24
enc_sxth_01:
	sxth.w r0, r1
enc_sxth_02:
	sxth.w r0, r1, ror #16
enc_uxtb_01:
	uxtb.w r0, r1
enc_uxtb_02:
	uxtb.w r0, r1, ror #8
enc_uxth_01:
	uxth.w r0, r1
enc_uxth_02:
	uxth.w r0, r1, ror #24
enc_sxtb16_01:
	sxtb16 r0, r1
enc_sxtb16_02:
	sxtb16 r0, r1, ror #8
enc_uxtb16_01:
	uxtb16 r0, r1
enc_uxtb16_02:
	uxtb16 r0, r1, ror #16
enc_sxtab_01:
	sxtab r0, r1, r2
enc_sxtab_02:
	sxtab r0, r1, r2, ror #24
enc_sxtah_01:
	sxtah r0, r1, r2, ror #16
enc_uxtab_01:
	uxtab r0, r1, r2
enc_uxtah_01:
	uxtah r0, r1, r2, ror #24
enc_sxtab16_01:
	sxtab16 r0, r1, r2
enc_uxtab16_01:
	uxtab16 r0, r1, r2, ror #8

@ ---------------------------------------------------------------- region D, misc
enc_rev_01:
	rev.w r0, r1
enc_rev16_01:
	rev16.w r0, r1
enc_revsh_01:
	revsh.w r0, r1
enc_rbit_01:
	rbit r0, r1
enc_clz_01:
	clz r0, r1
enc_sel_01:
	sel r0, r1, r2

@ ---------------------------------------------------------------- region D, parallel
enc_parallel_00:
	sadd8 r0, r1, r2
enc_parallel_01:
	sadd16 r0, r1, r2
enc_parallel_02:
	sasx r0, r1, r2
enc_parallel_03:
	ssub8 r0, r1, r2
enc_parallel_04:
	ssub16 r0, r1, r2
enc_parallel_05:
	ssax r0, r1, r2
enc_parallel_06:
	qadd8 r0, r1, r2
enc_parallel_07:
	qadd16 r0, r1, r2
enc_parallel_08:
	qasx r0, r1, r2
enc_parallel_09:
	qsub8 r0, r1, r2
enc_parallel_10:
	qsub16 r0, r1, r2
enc_parallel_11:
	qsax r0, r1, r2
enc_parallel_12:
	shadd8 r0, r1, r2
enc_parallel_13:
	shadd16 r0, r1, r2
enc_parallel_14:
	shasx r0, r1, r2
enc_parallel_15:
	shsub8 r0, r1, r2
enc_parallel_16:
	shsub16 r0, r1, r2
enc_parallel_17:
	shsax r0, r1, r2
enc_parallel_18:
	uadd8 r0, r1, r2
enc_parallel_19:
	uadd16 r0, r1, r2
enc_parallel_20:
	uasx r0, r1, r2
enc_parallel_21:
	usub8 r0, r1, r2
enc_parallel_22:
	usub16 r0, r1, r2
enc_parallel_23:
	usax r0, r1, r2
enc_parallel_24:
	uqadd8 r0, r1, r2
enc_parallel_25:
	uqadd16 r0, r1, r2
enc_parallel_26:
	uqasx r0, r1, r2
enc_parallel_27:
	uqsub8 r0, r1, r2
enc_parallel_28:
	uqsub16 r0, r1, r2
enc_parallel_29:
	uqsax r0, r1, r2
enc_parallel_30:
	uhadd8 r0, r1, r2
enc_parallel_31:
	uhadd16 r0, r1, r2
enc_parallel_32:
	uhasx r0, r1, r2
enc_parallel_33:
	uhsub8 r0, r1, r2
enc_parallel_34:
	uhsub16 r0, r1, r2
enc_parallel_35:
	uhsax r0, r1, r2

@ ---------------------------------------------------------------- region D, saturating 32-bit
enc_qadd_01:
	qadd r0, r1, r2
enc_qsub_01:
	qsub r0, r1, r2
enc_qdadd_01:
	qdadd r0, r1, r2
enc_qdsub_01:
	qdsub r0, r1, r2

@ ---------------------------------------------------------------- region D, multiply
enc_mul_01:
	mul r0, r1, r2
enc_mla_01:
	mla r0, r1, r2, r3
enc_mls_01:
	mls r0, r1, r2, r3
enc_smull_01:
	smull r0, r1, r2, r3
enc_umull_01:
	umull r0, r1, r2, r3
enc_smlal_01:
	smlal r0, r1, r2, r3
enc_umlal_01:
	umlal r0, r1, r2, r3
enc_umaal_01:
	umaal r0, r1, r2, r3
enc_sdiv_01:
	sdiv r0, r1, r2
enc_udiv_01:
	udiv r0, r1, r2
enc_smulbb_01:
	smulbb r0, r1, r2
enc_smulbt_01:
	smulbt r0, r1, r2
enc_smultb_01:
	smultb r0, r1, r2
enc_smultt_01:
	smultt r0, r1, r2
enc_smlabb_01:
	smlabb r0, r1, r2, r3
enc_smlabt_01:
	smlabt r0, r1, r2, r3
enc_smlatb_01:
	smlatb r0, r1, r2, r3
enc_smlatt_01:
	smlatt r0, r1, r2, r3
enc_smulwb_01:
	smulwb r0, r1, r2
enc_smulwt_01:
	smulwt r0, r1, r2
enc_smlawb_01:
	smlawb r0, r1, r2, r3
enc_smlawt_01:
	smlawt r0, r1, r2, r3
enc_smlalbb_01:
	smlalbb r0, r1, r2, r3
enc_smlalbt_01:
	smlalbt r0, r1, r2, r3
enc_smlaltb_01:
	smlaltb r0, r1, r2, r3
enc_smlaltt_01:
	smlaltt r0, r1, r2, r3
enc_smuad_01:
	smuad r0, r1, r2
enc_smuadx_01:
	smuadx r0, r1, r2
enc_smusd_01:
	smusd r0, r1, r2
enc_smusdx_01:
	smusdx r0, r1, r2
enc_smlad_01:
	smlad r0, r1, r2, r3
enc_smladx_01:
	smladx r0, r1, r2, r3
enc_smlsd_01:
	smlsd r0, r1, r2, r3
enc_smlsdx_01:
	smlsdx r0, r1, r2, r3
enc_smlald_01:
	smlald r0, r1, r2, r3
enc_smlaldx_01:
	smlaldx r0, r1, r2, r3
enc_smlsld_01:
	smlsld r0, r1, r2, r3
enc_smlsldx_01:
	smlsldx r0, r1, r2, r3
enc_smmul_01:
	smmul r0, r1, r2
enc_smmulr_01:
	smmulr r0, r1, r2
enc_smmla_01:
	smmla r0, r1, r2, r3
enc_smmlar_01:
	smmlar r0, r1, r2, r3
enc_smmls_01:
	smmls r0, r1, r2, r3
enc_smmlsr_01:
	smmlsr r0, r1, r2, r3
enc_usad8_01:
	usad8 r0, r1, r2
enc_usada8_01:
	usada8 r0, r1, r2, r3

@ ---------------------------------------------------------------- extra edge cases
enc_add_07:
	add.w sp, sp, r1
enc_add_08:
	add.w r0, sp, r2
enc_add_09:
	cmn.w r0, r1, lsr #1
enc_sub_06:
	sub.w sp, sp, r3
enc_sub_07:
	subs.w r0, r1, r2, rrx
enc_adc_04:
	adcs.w r0, r1, r2, lsl #1
enc_sbc_03:
	sbcs.w r0, r1, r2, lsl #1
enc_bic_04:
	bics.w r0, r1, r2, rrx
enc_bic_05:
	bic.w r0, r1, r2, asr #32
enc_orn_06:
	orns r0, r1, r2, asr #5
enc_eor_05:
	eors.w r0, r1, r2, rrx
enc_eor_06:
	teq.w r0, r1, rrx
enc_eor_07:
	eor.w r0, r1, r2, lsl #31
enc_and_10:
	tst.w r0, r1, asr #1
enc_and_11:
	and.w r0, r1, r2, lsr #32
enc_rsb_04:
	rsbs r0, r1, r2, asr #3
enc_orr_11:
	rrxs r0, r1
enc_bi_sub_05:
	sub.w r0, sp, #0
enc_usat16_03:
	usat16 r0, #0, r1
