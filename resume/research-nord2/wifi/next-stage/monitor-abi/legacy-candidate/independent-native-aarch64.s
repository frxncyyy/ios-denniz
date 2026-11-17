	.text
	.file	"wifi_emi_legacy.c"
	.globl	wifi_emi_legacy_offset          // -- Begin function wifi_emi_legacy_offset
	.p2align	2
	.type	wifi_emi_legacy_offset,@function
wifi_emi_legacy_offset:                 // @wifi_emi_legacy_offset
// %bb.0:
	mov	w8, w0
	mov	w0, #-1
	cbz	x1, .LBB0_3
// %bb.1:
	cmp	w8, #127                        // =127
	b.hi	.LBB0_3
// %bb.2:
	ubfiz	w9, w8, #8, #2
	add	w8, w8, #256                    // =256
	and	w8, w8, #0xfffffffc
	mov	w0, wzr
	add	w8, w8, w9
	str	w8, [x1]
.LBB0_3:
	ret
.Lfunc_end0:
	.size	wifi_emi_legacy_offset, .Lfunc_end0-wifi_emi_legacy_offset
                                        // -- End function
	.globl	wifi_emi_legacy_args_valid      // -- Begin function wifi_emi_legacy_args_valid
	.p2align	2
	.type	wifi_emi_legacy_args_valid,@function
wifi_emi_legacy_args_valid:             // @wifi_emi_legacy_args_valid
// %bb.0:
	cbz	x0, .LBB1_13
// %bb.1:
	ldr	x8, [x0]
	mov	w9, #609
	movk	w9, #33280, lsl #16
	cmp	x8, x9
	b.ne	.LBB1_8
// %bb.2:
	ldr	x8, [x0, #16]
	cbnz	x8, .LBB1_8
// %bb.3:
	ldr	x8, [x0, #24]
	cbnz	x8, .LBB1_8
// %bb.4:
	ldr	x8, [x0, #32]
	cbnz	x8, .LBB1_8
// %bb.5:
	ldr	x8, [x0, #40]
	cbnz	x8, .LBB1_8
// %bb.6:
	ldr	x8, [x0, #48]
	cbnz	x8, .LBB1_8
// %bb.7:
	ldr	x8, [x0, #56]
	cbz	x8, .LBB1_9
.LBB1_8:
	mov	w0, wzr
	ret
.LBB1_9:
	ldr	x9, [x0, #8]
	mov	w10, #256
.LBB1_10:                               // =>This Inner Loop Header: Depth=1
	and	w11, w8, #0x300
	and	w12, w10, #0x7ffffffc
	add	w11, w12, w11
	cmp	x9, x11
	b.eq	.LBB1_12
// %bb.11:                              //   in Loop: Header=BB1_10 Depth=1
	add	w10, w10, #1                    // =1
	mov	w0, wzr
	cmp	w10, #384                       // =384
	add	w8, w8, #256                    // =256
	b.ne	.LBB1_10
	b	.LBB1_13
.LBB1_12:
	mov	w0, #1
.LBB1_13:
	ret
.Lfunc_end1:
	.size	wifi_emi_legacy_args_valid, .Lfunc_end1-wifi_emi_legacy_args_valid
                                        // -- End function
	.globl	wifi_emi_legacy_native_call     // -- Begin function wifi_emi_legacy_native_call
	.p2align	2
	.type	wifi_emi_legacy_native_call,@function
wifi_emi_legacy_native_call:            // @wifi_emi_legacy_native_call
// %bb.0:
	str	x19, [sp, #-16]!                // 8-byte Folded Spill
	mov	w19, #-1
	cbz	x2, .LBB2_10
// %bb.1:
	cbz	x1, .LBB2_10
// %bb.2:
	ldr	x8, [x1]
	mov	w9, #609
	movk	w9, #33280, lsl #16
	cmp	x8, x9
	b.ne	.LBB2_9
// %bb.3:
	ldr	x8, [x1, #16]
	cbnz	x8, .LBB2_9
// %bb.4:
	ldr	x8, [x1, #24]
	cbnz	x8, .LBB2_9
// %bb.5:
	ldr	x8, [x1, #32]
	cbnz	x8, .LBB2_9
// %bb.6:
	ldr	x8, [x1, #40]
	cbnz	x8, .LBB2_9
// %bb.7:
	ldr	x8, [x1, #48]
	cbnz	x8, .LBB2_9
// %bb.8:
	ldr	x8, [x1, #56]
	cbz	x8, .LBB2_11
.LBB2_9:
	mov	w19, #-1
.LBB2_10:
	mov	w0, w19
	ldr	x19, [sp], #16                  // 8-byte Folded Reload
	ret
.LBB2_11:
	ldr	x1, [x1, #8]
	mov	x18, x2
	mov	w9, #256
	mov	w19, #-1
.LBB2_12:                               // =>This Inner Loop Header: Depth=1
	and	w10, w8, #0x300
	and	w11, w9, #0x7ffffffc
	add	w10, w11, w10
	cmp	x1, x10
	b.eq	.LBB2_14
// %bb.13:                              //   in Loop: Header=BB2_12 Depth=1
	add	w9, w9, #1                      // =1
	add	w8, w8, #256                    // =256
	cmp	w9, #384                        // =384
	b.ne	.LBB2_12
	b	.LBB2_10
.LBB2_14:
	mov	w0, #609
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr
	mov	x5, xzr
	mov	x6, xzr
	mov	x7, xzr
	mov	w19, wzr
	movk	w0, #33280, lsl #16
	//APP
	smc	#0
	//NO_APP
	stp	x0, x1, [x18]
	stp	x2, x3, [x18, #16]
	b	.LBB2_10
.Lfunc_end2:
	.size	wifi_emi_legacy_native_call, .Lfunc_end2-wifi_emi_legacy_native_call
                                        // -- End function
	.globl	wifi_emi_legacy_collect         // -- Begin function wifi_emi_legacy_collect
	.p2align	2
	.type	wifi_emi_legacy_collect,@function
wifi_emi_legacy_collect:                // @wifi_emi_legacy_collect
// %bb.0:
	sub	sp, sp, #192                    // =192
	stp	x22, x21, [sp, #160]            // 16-byte Folded Spill
	mov	x21, x0
	mov	w0, #-1
	stp	x29, x30, [sp, #96]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #112]            // 16-byte Folded Spill
	stp	x26, x25, [sp, #128]            // 16-byte Folded Spill
	stp	x24, x23, [sp, #144]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #176]            // 16-byte Folded Spill
	add	x29, sp, #96                    // =96
	cbz	x21, .LBB3_7
// %bb.1:
	mov	x19, x2
	cbz	x2, .LBB3_7
// %bb.2:
	mov	x20, x1
	mov	x8, xzr
	movi	v0.2d, #0000000000000000
	add	x9, x19, #32                    // =32
	mov	w10, #256
	str	q0, [x19, #6144]
.LBB3_3:                                // =>This Inner Loop Header: Depth=1
	and	w11, w8, #0x300
	and	w12, w10, #0xfffffffc
	add	x8, x8, #256                    // =256
	add	w11, w12, w11
	stp	q0, q0, [x9, #-32]
	stp	wzr, wzr, [x9, #4]
	add	w10, w10, #1                    // =1
	cmp	x8, #8, lsl #12                 // =32768
	str	w11, [x9], #48
	b.ne	.LBB3_3
// %bb.4:
	ldr	w8, [x19, #32]
	mov	w23, #609
	movk	w23, #33280, lsl #16
	movi	v0.2d, #0000000000000000
	mov	w24, #1
	add	x1, sp, #32                     // =32
	mov	x2, sp
	mov	x0, x20
	add	x25, sp, #32                    // =32
	stp	q0, q0, [sp, #64]
	str	q0, [sp, #48]
	stp	q0, q0, [sp]
	stp	x23, x8, [sp, #32]
	str	w24, [x19, #6160]
	blr	x21
	mov	x22, xzr
	cbz	w0, .LBB3_8
.LBB3_5:
	mov	w8, #4
.LBB3_6:
	mov	w9, #48
	madd	x9, x22, x9, x19
	mov	w0, #1
	str	w8, [x9, #36]
.LBB3_7:
	ldp	x20, x19, [sp, #176]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #160]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #144]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #128]            // 16-byte Folded Reload
	ldp	x28, x27, [sp, #112]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #96]             // 16-byte Folded Reload
	add	sp, sp, #192                    // =192
	ret
.LBB3_8:
	add	x26, x25, #16                   // =16
	add	x25, x19, #40                   // =40
	mov	w28, #-3
	mov	w27, #-4
.LBB3_9:                                // =>This Inner Loop Header: Depth=1
	ldp	q0, q1, [sp]
	mov	w9, #2147483647
	fmov	x8, d0
	cmp	x9, x8, lsr #1
	stur	q0, [x25, #-40]
	stur	q1, [x25, #-24]
	b.eq	.LBB3_18
// %bb.10:                              //   in Loop: Header=BB3_9 Depth=1
	cmn	x8, #3                          // =3
	b.hi	.LBB3_18
// %bb.11:                              //   in Loop: Header=BB3_9 Depth=1
	neg	x9, x8
	cmp	x9, #3                          // =3
	b.eq	.LBB3_18
// %bb.12:                              //   in Loop: Header=BB3_9 Depth=1
	cmp	x8, x28
	b.eq	.LBB3_18
// %bb.13:                              //   in Loop: Header=BB3_9 Depth=1
	cmp	x9, #4                          // =4
	b.eq	.LBB3_18
// %bb.14:                              //   in Loop: Header=BB3_9 Depth=1
	cmp	x8, x27
	b.eq	.LBB3_18
// %bb.15:                              //   in Loop: Header=BB3_9 Depth=1
	lsr	x8, x8, #32
	cbnz	x8, .LBB3_19
// %bb.16:                              //   in Loop: Header=BB3_9 Depth=1
	ubfx	x8, x22, #5, #27
	stur	w24, [x25, #-4]
	add	x8, x19, x8, lsl #2
	ldr	w9, [x8, #6144]
	lsl	w10, w24, w22
	cmp	x22, #127                       // =127
	orr	w9, w9, w10
	str	w9, [x8, #6144]
	b.eq	.LBB3_20
// %bb.17:                              //   in Loop: Header=BB3_9 Depth=1
	ldr	w8, [x19, #6160]
	ldr	w9, [x25, #40]
	movi	v0.2d, #0000000000000000
	add	x1, sp, #32                     // =32
	add	w8, w8, #1                      // =1
	mov	x2, sp
	mov	x0, x20
	stp	q0, q0, [x26, #16]
	str	q0, [x26]
	stp	q0, q0, [sp]
	stp	x23, x9, [sp, #32]
	str	w8, [x19, #6160]
	blr	x21
	add	x25, x25, #48                   // =48
	add	x22, x22, #1                    // =1
	cbz	w0, .LBB3_9
	b	.LBB3_5
.LBB3_18:
	fmov	x8, d0
	str	w8, [x25]
	mov	w8, #2
	b	.LBB3_6
.LBB3_19:
	mov	w8, #3
	b	.LBB3_6
.LBB3_20:
	mov	w0, wzr
	b	.LBB3_7
.Lfunc_end3:
	.size	wifi_emi_legacy_collect, .Lfunc_end3-wifi_emi_legacy_collect
                                        // -- End function
	.ident	"Proton clang version 13.0.0 (https://github.com/llvm/llvm-project b4fd512c36ca344a3ff69350219e8b0a67e9472a)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
