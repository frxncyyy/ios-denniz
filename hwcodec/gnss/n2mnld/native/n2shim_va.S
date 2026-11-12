/* SPDX-License-Identifier: MIT
 * n2shim_va.S -- Linux-AAPCS64 variadic entry points for libmnl's 6 variadic imports (printf, fprintf, sscanf, fscanf,
 * fcntl, ioctl). A Linux caller passes the variadic arguments in x0..x7 / q0..q7 and then on the stack; a Darwin callee
 * would read them from the stack only. Each trampoline saves x0..x7 and q0..q7 in a 192-byte block (the "register save
 * area" of a Linux va_list: gr_top = block + 64, vr_top = block + 192) and calls
 *     long n2vi_NAME(uint64_t *saved, void *caller_stack)
 * where caller_stack = the caller's sp at the call (its stacked arguments). The C side builds the va_list (n2shim.c lva).
 * x18 is never touched (Darwin platform register); frame record kept for backtraces.
 */
	.text
	.p2align 2
.macro VTRAMP name
	.globl _n2v_\name
_n2v_\name:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	sub	sp, sp, #192
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	q0, q1, [sp, #64]
	stp	q2, q3, [sp, #96]
	stp	q4, q5, [sp, #128]
	stp	q6, q7, [sp, #160]
	mov	x0, sp
	add	x1, x29, #16
	bl	_n2vi_\name
	mov	sp, x29
	ldp	x29, x30, [sp], #16
	ret
.endm
	VTRAMP printf
	VTRAMP fprintf
	VTRAMP sscanf
	VTRAMP fscanf
	VTRAMP fcntl
	VTRAMP ioctl

/* import-call trace (n2trace.c). libmnl's GOT slot i points to thunk i = n2t_thunks + 8*i: `adr x17, . ; b common`.
 * common: n2t_active == 0 -> tail jump to n2t_real[i] (x16/x17 are the IP scratch registers, free at a call).
 * Else save x0-x8/q0-q7, call n2t_enter(saved, thunk, lr) -> {fn, lr'}; restore and `br fn` with lr = lr' (either the
 * caller's lr, or n2t_ret which logs the return value and returns to the lr n2t_leave pops). sp is unchanged at the
 * branch, so stacked (variadic / 9th+) arguments are where the callee expects them. */
	.p2align 3
	.globl _n2t_thunks
_n2t_thunks:
	.rept 160
	adr	x17, .
	b	_n2t_common
	.endr
_n2t_common:
	adrp	x16, _n2t_active@PAGE
	ldr	w16, [x16, _n2t_active@PAGEOFF]
	cbnz	w16, 1f
	adrp	x16, _n2t_thunks@PAGE
	add	x16, x16, _n2t_thunks@PAGEOFF
	sub	x17, x17, x16
	adrp	x16, _n2t_real@PAGE
	add	x16, x16, _n2t_real@PAGEOFF
	ldr	x16, [x16, x17]
	br	x16
1:	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	sub	sp, sp, #208
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	q0, q1, [sp, #64]
	stp	q2, q3, [sp, #96]
	stp	q4, q5, [sp, #128]
	stp	q6, q7, [sp, #160]
	str	x8, [sp, #192]
	mov	x0, sp
	mov	x1, x17
	mov	x2, x30
	bl	_n2t_enter
	mov	x16, x0
	mov	x17, x1
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	q0, q1, [sp, #64]
	ldp	q2, q3, [sp, #96]
	ldp	q4, q5, [sp, #128]
	ldp	q6, q7, [sp, #160]
	ldr	x8, [sp, #192]
	mov	sp, x29
	ldp	x29, x30, [sp], #16
	mov	x30, x17
	br	x16
	.globl _n2t_ret
_n2t_ret:                                     /* a traced import returned here: x0/x1, q0/q1 = its result */
	sub	sp, sp, #64
	stp	x0, x1, [sp, #0]
	stp	q0, q1, [sp, #16]
	stp	x29, x30, [sp, #48]
	bl	_n2t_leave                            /* x0 = result in, caller's lr out */
	mov	x17, x0
	ldp	x0, x1, [sp, #0]
	ldp	q0, q1, [sp, #16]
	ldp	x29, x30, [sp, #48]
	add	sp, sp, #64
	mov	x30, x17
	ret
