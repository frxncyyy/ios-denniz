@ SPDX-License-Identifier: MIT
@
@ selftest_t32.s -- the T32 (Thumb-2) part of vcodecd --selftest's guest program.  Same calling convention as
@ selftest_a32.s: arg0 -> r7 (BLX target), arg1 -> r5 (bound), arg2 -> r8 (expected cookie); returns
@ (mismatches << 24) | iterations.  Uses IT, a 32-bit ADD and the high-register MOV/BX forms, so a T32 path that
@ only knows 16-bit encodings fails the selftest.
@
@ Assembled by daemon/test/gen_selftest_prog.sh; run_tests.sh re-assembles and diffs the bytes.
	.syntax unified
	.thumb
	.text
	.global selftest_t32_loop
	.thumb_func
	.type selftest_t32_loop, %function
selftest_t32_loop:
	mov	r6, lr
	movs	r4, #0
	movs	r9, #0
1:	adds	r4, r4, #1
	blx	r7
	cmp	r0, r8
	it	ne
	addne	r9, r9, #1
	cmp	r4, r5
	bne	1b
	lsls	r0, r9, #24
	orrs	r0, r0, r4
	bx	r6
	.size selftest_t32_loop, .-selftest_t32_loop

	.thumb_func
	.global selftest_t32_ok
	.type selftest_t32_ok, %function
selftest_t32_ok:
	movs	r0, #0x11
	bx	lr
	.size selftest_t32_ok, .-selftest_t32_ok
