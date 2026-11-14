@ SPDX-License-Identifier: MIT
@
@ guest_thumb.s -- the Thumb code the V34 tests execute in the real V02 core through n2a32_call().
@ Assembled with llvm-mc --triple=thumbv8a; every encoding is re-derived from the Arm ARM.
@
@ ONLY 16-BIT Thumb instructions are used on purpose: the test links V14's n2a32_t16.c and the stub classes, so a
@ failure here is a V34 failure and never a missing 32-bit class (MOVW/MOVT are V15's; the earlier version of this
@ file used them and stopped with N2A32_STOP_UNDEF on MOVW, which is exactly the kind of experiment pollution this
@ note prevents).

	.syntax unified
	.thumb
	.text

@ uint32_t main_args(uint32_t argc, char **argv, char **envp)
@   returns argc + (argv != NULL ? 0x100 : 0) + (envp != NULL ? 0x10000 : 0)
@ so __libc_init's argument marshalling is directly visible in the return value.
	.global main_args
	.thumb_func
main_args:
	push	{r4}
	mov	r3, r0
	cmp	r1, #0
	beq	1f
	movs	r4, #1
	lsls	r4, r4, #8		@ 0x100
	adds	r3, r4
1:
	cmp	r2, #0
	beq	2f
	movs	r4, #1
	lsls	r4, r4, #16		@ 0x10000
	adds	r3, r4
2:
	mov	r0, r3
	pop	{r4}
	bx	lr

@ uint32_t main_const(void) -> 42
	.global main_const
	.thumb_func
main_const:
	movs	r0, #42
	bx	lr

@ void atexit_inc(void *p): *(uint32_t *)p += 1
	.global atexit_inc
	.thumb_func
atexit_inc:
	ldr	r1, [r0]
	adds	r1, r1, #1
	str	r1, [r0]
	bx	lr

@ void onexit_marker(void): *(uint8_t *)0x30000100 = 0xed
	.global onexit_marker
	.thumb_func
onexit_marker:
	movs	r0, #3
	lsls	r0, r0, #28		@ 0x30000000
	movs	r1, #1
	lsls	r1, r1, #8		@ 0x100
	adds	r0, r0, r1		@ 0x30000100
	movs	r1, #0xed
	strb	r1, [r0]
	bx	lr

@ void main_touch(uint32_t *p): *p = 0x5a5a5a5a   (checks that a callback really runs guest code)
	.global main_touch
	.thumb_func
main_touch:
	movs	r1, #0x5a
	lsls	r2, r1, #8
	orrs	r1, r1, r2		@ 0x5a5a
	lsls	r2, r1, #16
	orrs	r1, r1, r2		@ 0x5a5a5a5a
	str	r1, [r0]
	bx	lr
