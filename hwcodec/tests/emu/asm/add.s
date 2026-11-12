@ SPDX-License-Identifier: MIT
@ A32 smoke test: add, shift, store to the mapped scratch page, stop.
.syntax unified
.text
	mov	r0, #5
	mov	r1, #7
	add	r2, r0, r1
	mov	r3, r2, lsl #2
	movw	r4, #0x0000
	movt	r4, #0x0003		@ r4 = 0x00030000
	str	r3, [r4]
	udf	#254
