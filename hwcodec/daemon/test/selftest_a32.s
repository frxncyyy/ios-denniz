@ SPDX-License-Identifier: MIT
@
@ selftest_a32.s -- the A32 part of vcodecd --selftest's guest program (see daemon/n2vcd_selftest.c).
@
@ The loop is called with n2a32_call(cpu, selftest_a32_loop, args, 3):
@   arg0 (r0) = guest address of the probe host call (moved to r7, called with BLX)
@   arg1 (r1) = iteration bound, moved to r5 (large enough that the instruction limit stops the run)
@   arg2 (r2) = expected probe cookie 0x600d0000 | thread id, moved to r8
@ Returns (mismatches << 24) | iterations; the caller checks both halves.
@ r6 keeps the link register across BLX because the probe would otherwise overwrite LR.
@
@ Assembled by daemon/test/gen_selftest_prog.sh with
@   llvm-mc -triple=armv7-none-eabi -filetype=obj
@ and copied byte for byte into daemon/n2vcd_selftest_prog.h.  run_tests.sh re-assembles and diffs.
	.syntax unified
	.arm
	.text
	.global selftest_a32_loop
	.type selftest_a32_loop, %function
selftest_a32_loop:
	mov	r6, lr
	mov	r4, #0
	mov	r9, #0
1:	add	r4, r4, #1
	blx	r7
	cmp	r0, r8
	addne	r9, r9, #1
	cmp	r4, r5
	bne	1b
	mov	r0, r9, lsl #24
	orr	r0, r0, r4
	bx	r6
	.size selftest_a32_loop, .-selftest_a32_loop

	.global selftest_a32_ok
	.type selftest_a32_ok, %function
selftest_a32_ok:
	movw	r0, #0x600d
	bx	lr
	.size selftest_a32_ok, .-selftest_a32_ok

	.global selftest_a32_fault
	.type selftest_a32_fault, %function
selftest_a32_fault:
	ldr	r0, [r0]
	bx	lr
	.size selftest_a32_fault, .-selftest_a32_fault
