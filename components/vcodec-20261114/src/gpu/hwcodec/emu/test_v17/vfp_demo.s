@ SPDX-License-Identifier: MIT
@ V17 core-integration demo: scalar VFP through n2a32_core (A32).  Stores results at 0x30000.
@ Expected:
@   0x30000 40400000  vsqrt(4)+1 = 3.0
@   0x30004 3f800000  VSELEQ(1.0,2.0) with Z=1 -> 1.0
@   0x30008 40400000  VRINTA(2.5) = 3.0
@   0x3000c 40000000  VMAXNM(1.0,2.0) = 2.0
@   0x30010 41c80000  VCVT.F32.S16(100, #2) = 25.0
@   0x30018 00000064  VDUP.32 d5,r6 -> d5 = 0x0000006400000064
@   0x30020 00000064  VMOV.32 r9,d5[1]
	.syntax unified
	.arm
	.text
	ldr	r0, =0x30000

	vldr	s0, c_one
	vldr	s1, c_four
	vsqrt.f32 s2, s1
	vadd.f32 s3, s0, s2
	vmov	r1, s3
	str	r1, [r0]

	mov	r2, #5
	cmp	r2, #5			@ Z=1 for the VSEL below
	vldr	s4, c_one
	vldr	s5, c_two
	vseleq.f32 s6, s4, s5
	vmov	r3, s6
	str	r3, [r0, #4]

	vldr	s7, c_2_5
	vrinta.f32 s8, s7
	vmov	r4, s8
	str	r4, [r0, #8]

	vmaxnm.f32 s9, s4, s5
	vmov	r5, s9
	str	r5, [r0, #12]

	mov	r6, #100
	vmov	s10, r6
	vcvt.f32.s16 s10, s10, #2
	vmov	r7, s10
	str	r7, [r0, #16]

	vdup.32 d5, r6
	vstr	d5, [r0, #24]
	vmov.32 r9, d5[1]
	str	r9, [r0, #32]

	udf	#254

	.align 2
c_one:	.word 0x3F800000
c_two:	.word 0x40000000
c_four:	.word 0x40800000
c_2_5:	.word 0x40200000
