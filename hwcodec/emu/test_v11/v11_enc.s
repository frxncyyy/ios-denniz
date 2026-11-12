@ SPDX-License-Identifier: MIT
@ v11_enc.s -- assembly source for the V11 test encodings.
@
@ Every encoding used by t_v11.c is produced from this file by
@   llvm-mc -triple=armv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8 --show-encoding
@ in run_tests.sh.  A line "@ <name>" names the following instruction; gen_enc.py
@ turns the pairs into #define ENC_<name> <word>.
@
@ Instructions that the class must reject are built in t_v11.c by substituting a
@ field of one of these words (documented at each use); no encoding is typed by hand.
	.syntax unified
	.arm
	.text

@ ---- extend ----
@ sxtb_r0_r1
sxtb r0, r1
@ sxtb_r0_r1_ror8
sxtb r0, r1, ror #8
@ sxtb_r0_r1_ror16
sxtb r0, r1, ror #16
@ sxtb_r0_r1_ror24
sxtb r0, r1, ror #24
@ sxth_r0_r1
sxth r0, r1
@ sxth_r0_r1_ror8
sxth r0, r1, ror #8
@ uxtb_r0_r1
uxtb r0, r1
@ uxtb_r0_r1_ror8
uxtb r0, r1, ror #8
@ uxtb_r0_r1_ror16
uxtb r0, r1, ror #16
@ uxtb_r0_r1_ror24
uxtb r0, r1, ror #24
@ uxth_r0_r1
uxth r0, r1
@ uxth_r0_r1_ror16
uxth r0, r1, ror #16
@ sxtb_r5_r6
sxtb r5, r6
@ uxtb_r5_r6_ror8
uxtb r5, r6, ror #8
@ sxtab_r0_r1_r2
sxtab r0, r1, r2
@ sxtab_r0_r1_r2_ror8
sxtab r0, r1, r2, ror #8
@ sxtah_r0_r1_r2
sxtah r0, r1, r2
@ sxtah_r0_r1_r2_ror16
sxtah r0, r1, r2, ror #16
@ uxtab_r0_r1_r2
uxtab r0, r1, r2
@ uxtab_r0_r1_r2_ror16
uxtab r0, r1, r2, ror #16
@ uxtah_r0_r1_r2
uxtah r0, r1, r2
@ sxtb16_r0_r1
sxtb16 r0, r1
@ sxtb16_r0_r1_ror8
sxtb16 r0, r1, ror #8
@ uxtb16_r0_r1
uxtb16 r0, r1
@ uxtb16_r0_r1_ror24
uxtb16 r0, r1, ror #24
@ sxtab16_r0_r1_r2
sxtab16 r0, r1, r2
@ sxtab16_r0_r1_r2_ror8
sxtab16 r0, r1, r2, ror #8
@ uxtab16_r0_r1_r2
uxtab16 r0, r1, r2
@ sxtb_r0_r13
sxtb r0, r13

@ ---- bit field ----
@ ubfx_r0_r1_4_8
ubfx r0, r1, #4, #8
@ ubfx_r0_r1_0_1
ubfx r0, r1, #0, #1
@ ubfx_r0_r1_31_1
ubfx r0, r1, #31, #1
@ ubfx_r5_r6_3_5
ubfx r5, r6, #3, #5
@ sbfx_r0_r1_0_32
sbfx r0, r1, #0, #32
@ sbfx_r0_r1_7_9
sbfx r0, r1, #7, #9
@ sbfx_r0_r1_31_1
sbfx r0, r1, #31, #1
@ bfi_r0_r1_4_8
bfi r0, r1, #4, #8
@ bfi_r0_r1_0_32
bfi r0, r1, #0, #32
@ bfi_r0_r1_31_1
bfi r0, r1, #31, #1
@ bfi_r5_r6_7_9
bfi r5, r6, #7, #9
@ bfc_r0_4_8
bfc r0, #4, #8
@ bfc_r0_31_1
bfc r0, #31, #1
@ bfc_r0_0_32
bfc r0, #0, #32

@ ---- reverse ----
@ rev_r0_r1
rev r0, r1
@ rev16_r0_r1
rev16 r0, r1
@ revsh_r0_r1
revsh r0, r1
@ rbit_r0_r1
rbit r0, r1
@ rev_r5_r6
rev r5, r6

@ ---- saturate ----
@ ssat_r0_1_r1
ssat r0, #1, r1
@ ssat_r0_16_r1
ssat r0, #16, r1
@ ssat_r0_32_r1
ssat r0, #32, r1
@ ssat_r0_16_r1_lsl5
ssat r0, #16, r1, lsl #5
@ ssat_r0_16_r1_asr3
ssat r0, #16, r1, asr #3
@ ssat_r0_16_r1_asr32
ssat r0, #16, r1, asr #32
@ ssat_r5_8_r6
ssat r5, #8, r6
@ usat_r0_0_r1
usat r0, #0, r1
@ usat_r0_8_r1
usat r0, #8, r1
@ usat_r0_31_r1
usat r0, #31, r1
@ usat_r0_8_r1_lsl4
usat r0, #8, r1, lsl #4
@ usat_r0_8_r1_asr2
usat r0, #8, r1, asr #2
@ usat_r0_8_r1_asr32
usat r0, #8, r1, asr #32
@ ssat16_r0_1_r1
ssat16 r0, #1, r1
@ ssat16_r0_16_r1
ssat16 r0, #16, r1
@ ssat16_r5_8_r6
ssat16 r5, #8, r6
@ usat16_r0_0_r1
usat16 r0, #0, r1
@ usat16_r0_15_r1
usat16 r0, #15, r1

@ ---- pack / select ----
@ pkhbt_r0_r1_r2
pkhbt r0, r1, r2
@ pkhbt_r0_r1_r2_lsl8
pkhbt r0, r1, r2, lsl #8
@ pkhbt_r0_r1_r2_lsl31
pkhbt r0, r1, r2, lsl #31
@ pkhtb_r0_r1_r2
pkhtb r0, r1, r2
@ pkhtb_r0_r1_r2_asr8
pkhtb r0, r1, r2, asr #8
@ pkhtb_r0_r1_r2_asr16
pkhtb r0, r1, r2, asr #16
@ pkhbt_r0_r1_r2_lsl16
pkhbt r0, r1, r2, lsl #16
@ pkhtb_r0_r1_r2_asr32
pkhtb r0, r1, r2, asr #32
@ sel_r0_r1_r2
sel r0, r1, r2
@ sel_r5_r6_r7
sel r5, r6, r7

@ ---- parallel add/sub, signed ----
@ sadd16_r0_r1_r2
sadd16 r0, r1, r2
@ sasx_r0_r1_r2
sasx r0, r1, r2
@ ssax_r0_r1_r2
ssax r0, r1, r2
@ ssub16_r0_r1_r2
ssub16 r0, r1, r2
@ sadd8_r0_r1_r2
sadd8 r0, r1, r2
@ ssub8_r0_r1_r2
ssub8 r0, r1, r2
@ sadd16_r5_r6_r7
sadd16 r5, r6, r7
@ sadd16_r0_r13_r2
sadd16 r0, r13, r2

@ ---- parallel add/sub, signed saturating ----
@ qadd16_r0_r1_r2
qadd16 r0, r1, r2
@ qasx_r0_r1_r2
qasx r0, r1, r2
@ qsax_r0_r1_r2
qsax r0, r1, r2
@ qsub16_r0_r1_r2
qsub16 r0, r1, r2
@ qadd8_r0_r1_r2
qadd8 r0, r1, r2
@ qsub8_r0_r1_r2
qsub8 r0, r1, r2

@ ---- parallel add/sub, signed halving ----
@ shadd16_r0_r1_r2
shadd16 r0, r1, r2
@ shasx_r0_r1_r2
shasx r0, r1, r2
@ shsax_r0_r1_r2
shsax r0, r1, r2
@ shsub16_r0_r1_r2
shsub16 r0, r1, r2
@ shadd8_r0_r1_r2
shadd8 r0, r1, r2
@ shsub8_r0_r1_r2
shsub8 r0, r1, r2

@ ---- parallel add/sub, unsigned ----
@ uadd16_r0_r1_r2
uadd16 r0, r1, r2
@ uasx_r0_r1_r2
uasx r0, r1, r2
@ usax_r0_r1_r2
usax r0, r1, r2
@ usub16_r0_r1_r2
usub16 r0, r1, r2
@ uadd8_r0_r1_r2
uadd8 r0, r1, r2
@ usub8_r0_r1_r2
usub8 r0, r1, r2

@ ---- parallel add/sub, unsigned saturating ----
@ uqadd16_r0_r1_r2
uqadd16 r0, r1, r2
@ uqasx_r0_r1_r2
uqasx r0, r1, r2
@ uqsax_r0_r1_r2
uqsax r0, r1, r2
@ uqsub16_r0_r1_r2
uqsub16 r0, r1, r2
@ uqadd8_r0_r1_r2
uqadd8 r0, r1, r2
@ uqsub8_r0_r1_r2
uqsub8 r0, r1, r2

@ ---- parallel add/sub, unsigned halving ----
@ uhadd16_r0_r1_r2
uhadd16 r0, r1, r2
@ uhasx_r0_r1_r2
uhasx r0, r1, r2
@ uhsax_r0_r1_r2
uhsax r0, r1, r2
@ uhsub16_r0_r1_r2
uhsub16 r0, r1, r2
@ uhadd8_r0_r1_r2
uhadd8 r0, r1, r2
@ uhsub8_r0_r1_r2
uhsub8 r0, r1, r2

@ ---- saturating 32-bit ----
@ qadd_r0_r1_r2
qadd r0, r1, r2
@ qsub_r0_r1_r2
qsub r0, r1, r2
@ qdadd_r0_r1_r2
qdadd r0, r1, r2
@ qdsub_r0_r1_r2
qdsub r0, r1, r2
@ qadd_r4_r5_r6
qadd r4, r5, r6

@ ---- 16x16 multiplies ----
@ smulbb_r0_r1_r2
smulbb r0, r1, r2
@ smulbt_r0_r1_r2
smulbt r0, r1, r2
@ smultb_r0_r1_r2
smultb r0, r1, r2
@ smultt_r0_r1_r2
smultt r0, r1, r2
@ smulbb_r5_r6_r7
smulbb r5, r6, r7
@ smlabb_r0_r1_r2_r3
smlabb r0, r1, r2, r3
@ smlabt_r0_r1_r2_r3
smlabt r0, r1, r2, r3
@ smlatb_r0_r1_r2_r3
smlatb r0, r1, r2, r3
@ smlatt_r0_r1_r2_r3
smlatt r0, r1, r2, r3
@ smlabb_r4_r5_r6_r7
smlabb r4, r5, r6, r7
@ smulwb_r0_r1_r2
smulwb r0, r1, r2
@ smulwt_r0_r1_r2
smulwt r0, r1, r2
@ smlawb_r0_r1_r2_r3
smlawb r0, r1, r2, r3
@ smlawt_r0_r1_r2_r3
smlawt r0, r1, r2, r3
@ smlalbb_r0_r1_r2_r3
smlalbb r0, r1, r2, r3
@ smlalbt_r0_r1_r2_r3
smlalbt r0, r1, r2, r3
@ smlaltb_r0_r1_r2_r3
smlaltb r0, r1, r2, r3
@ smlaltt_r0_r1_r2_r3
smlaltt r0, r1, r2, r3
@ smuad_r0_r1_r2
smuad r0, r1, r2
@ smuadx_r0_r1_r2
smuadx r0, r1, r2
@ smusd_r0_r1_r2
smusd r0, r1, r2
@ smusdx_r0_r1_r2
smusdx r0, r1, r2
@ smlad_r0_r1_r2_r3
smlad r0, r1, r2, r3
@ smladx_r0_r1_r2_r3
smladx r0, r1, r2, r3
@ smlsd_r0_r1_r2_r3
smlsd r0, r1, r2, r3
@ smlsdx_r0_r1_r2_r3
smlsdx r0, r1, r2, r3
@ smlald_r0_r1_r2_r3
smlald r0, r1, r2, r3
@ smlaldx_r0_r1_r2_r3
smlaldx r0, r1, r2, r3
@ smlsld_r0_r1_r2_r3
smlsld r0, r1, r2, r3
@ smlsldx_r0_r1_r2_r3
smlsldx r0, r1, r2, r3
@ smmul_r0_r1_r2
smmul r0, r1, r2
@ smmulr_r0_r1_r2
smmulr r0, r1, r2
@ smmla_r0_r1_r2_r3
smmla r0, r1, r2, r3
@ smmlar_r0_r1_r2_r3
smmlar r0, r1, r2, r3
@ smmls_r0_r1_r2_r3
smmls r0, r1, r2, r3
@ smmlsr_r0_r1_r2_r3
smmlsr r0, r1, r2, r3
@ usad8_r0_r1_r2
usad8 r0, r1, r2
@ usada8_r0_r1_r2_r3
usada8 r0, r1, r2, r3
@ usad8_r5_r6_r7
usad8 r5, r6, r7

@ ---- divide ----
@ sdiv_r0_r1_r2
sdiv r0, r1, r2
@ udiv_r0_r1_r2
udiv r0, r1, r2
@ sdiv_r5_r6_r7
sdiv r5, r6, r7
@ udiv_r3_r4_r5
udiv r3, r4, r5

@ ---- not V11: the class must answer N2A32_UNDEF ----
@ mul_r0_r1_r2
mul r0, r1, r2
@ and_r0_r1_r2
and r0, r1, r2
@ ldr_r0_r1_0
ldr r0, [r1]
@ bx_r1
bx r1
@ mla_r0_r1_r2_r3
mla r0, r1, r2, r3
@ smull_r0_r1_r2_r3
smull r0, r1, r2, r3
@ clz_r0_r1
clz r0, r1
@ movw_r0_0x1234
movw r0, #0x1234
@ add_r0_r1_r2
add r0, r1, r2
@ mrs_r0_apsr
mrs r0, apsr
