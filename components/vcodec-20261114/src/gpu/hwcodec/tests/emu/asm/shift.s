@ SPDX-License-Identifier: MIT
@ T32 smoke test: shifts, rotate and conditional execution inside an IT block.
.syntax unified
.thumb
.text
	movs	r0, #1
	lsls	r1, r0, #8
	rors	r2, r1, #4
	movs	r3, #0
	it	eq
	moveq	r3, #0x55
	cmp	r2, #0x10
	udf	#254
