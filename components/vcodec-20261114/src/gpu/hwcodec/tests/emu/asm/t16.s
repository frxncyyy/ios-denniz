@ SPDX-License-Identifier: MIT
@ T32 smoke test using only 16-bit units, so it runs before V15/V16 land.
.syntax unified
.thumb
.text
	movs	r0, #1
	lsls	r1, r0, #8
	lsrs	r2, r1, #4
	movs	r3, #0
	it	eq
	moveq	r3, #0x55
	udf	#254
