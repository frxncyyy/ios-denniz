
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ab048 <_nstx_decode_report>:
fffffe00079ab048:      	sub	sp, sp, #144
fffffe00079ab04c:      	mov	w8, #-1
fffffe00079ab050:      	cbz	x0, 0xfffffe00079ab1ec <_nstx_decode_report+0x1a4>
fffffe00079ab054:      	cbz	x1, 0xfffffe00079ab1ec <_nstx_decode_report+0x1a4>
fffffe00079ab058:      	sub	x9, x2, #3841
fffffe00079ab05c:      	cmn	x9, #3833
fffffe00079ab060:      	b.lo	0xfffffe00079ab1ec <_nstx_decode_report+0x1a4>
fffffe00079ab064:      	ldrh	w9, [x1]
fffffe00079ab068:      	cmp	w9, #8
fffffe00079ab06c:      	b.lo	0xfffffe00079ab1e8 <_nstx_decode_report+0x1a0>
fffffe00079ab070:      	cmp	x9, x2
fffffe00079ab074:      	b.hi	0xfffffe00079ab1e8 <_nstx_decode_report+0x1a0>
fffffe00079ab078:      	ldrb	w10, [x1, #3]
fffffe00079ab07c:      	and	w8, w10, #0xf8
fffffe00079ab080:      	cmp	w8, #48
fffffe00079ab084:      	b.ne	0xfffffe00079ab1e8 <_nstx_decode_report+0x1a0>
fffffe00079ab088:      	ldrb	w8, [x1, #6]
fffffe00079ab08c:      	mov	w12, #1023
fffffe00079ab090:      	movi.2d	v0, #0000000000000000
fffffe00079ab094:      	ldrb	w11, [x1, #2]
fffffe00079ab098:      	and	w8, w8, #0x7
fffffe00079ab09c:      	cmp	w8, #3
fffffe00079ab0a0:      	stp	q0, q0, [sp]
fffffe00079ab0a4:      	stp	wzr, w8, [sp, #128]
fffffe00079ab0a8:      	mov	w8, #127
fffffe00079ab0ac:      	csel	w12, w12, w8, eq
fffffe00079ab0b0:      	mov	w8, #-1
fffffe00079ab0b4:      	stp	q0, q0, [sp, #32]
fffffe00079ab0b8:      	stp	q0, q0, [sp, #64]
fffffe00079ab0bc:      	stp	q0, q0, [sp, #96]
fffffe00079ab0c0:      	b.hi	0xfffffe00079ab1ec <_nstx_decode_report+0x1a4>
fffffe00079ab0c4:      	bfi	w11, w10, #8, #8
fffffe00079ab0c8:      	and	w10, w11, w12
fffffe00079ab0cc:      	cmp	w10, #64
fffffe00079ab0d0:      	b.hi	0xfffffe00079ab1ec <_nstx_decode_report+0x1a4>
fffffe00079ab0d4:      	cbz	w10, 0xfffffe00079ab1f8 <_nstx_decode_report+0x1b0>
fffffe00079ab0d8:      	mov	w8, wzr
fffffe00079ab0dc:      	add	x11, x1, #11
fffffe00079ab0e0:      	mov	x12, #-8
fffffe00079ab0e4:      	mov	x13, sp
fffffe00079ab0e8:      	b	0xfffffe00079ab0fc <_nstx_decode_report+0xb4>
fffffe00079ab0ec:      	ldr	w8, [sp, #128]
fffffe00079ab0f0:      	add	x11, x11, #4
fffffe00079ab0f4:      	cmp	w8, w10
fffffe00079ab0f8:      	b.hs	0xfffffe00079ab198 <_nstx_decode_report+0x150>
fffffe00079ab0fc:      	add	x14, x9, x12
fffffe00079ab100:      	cmp	x14, #4
fffffe00079ab104:      	b.lo	0xfffffe00079ab1e8 <_nstx_decode_report+0x1a0>
fffffe00079ab108:      	sub	x12, x12, #4
fffffe00079ab10c:      	cmn	w12, #3844
fffffe00079ab110:      	b.eq	0xfffffe00079ab1e8 <_nstx_decode_report+0x1a0>
fffffe00079ab114:      	ldurb	w14, [x11, #-1]
fffffe00079ab118:      	ldrb	w15, [x11]
fffffe00079ab11c:      	ldurh	w16, [x11, #-3]
fffffe00079ab120:      	lsl	w14, w14, #16
fffffe00079ab124:      	ldr	w17, [sp, #132]
fffffe00079ab128:      	bfi	w14, w15, #24, #8
fffffe00079ab12c:      	orr	w15, w14, w16
fffffe00079ab130:      	cmp	w17, #3
fffffe00079ab134:      	b.ne	0xfffffe00079ab13c <_nstx_decode_report+0xf4>
fffffe00079ab138:      	tbnz	w15, #31, 0xfffffe00079ab0ec <_nstx_decode_report+0xa4>
fffffe00079ab13c:      	cmp	w17, #2
fffffe00079ab140:      	b.lo	0xfffffe00079ab154 <_nstx_decode_report+0x10c>
fffffe00079ab144:      	b.ne	0xfffffe00079ab150 <_nstx_decode_report+0x108>
fffffe00079ab148:      	and	w16, w16, #0x7fff
fffffe00079ab14c:      	b	0xfffffe00079ab154 <_nstx_decode_report+0x10c>
fffffe00079ab150:      	ubfx	w16, w14, #16, #15
fffffe00079ab154:      	cmp	w16, #2047
fffffe00079ab158:      	b.hi	0xfffffe00079ab1e8 <_nstx_decode_report+0x1a0>
fffffe00079ab15c:      	add	w17, w8, #1
fffffe00079ab160:      	str	w17, [sp, #128]
fffffe00079ab164:      	strh	w16, [x13, w8, uxtw #1]
fffffe00079ab168:      	ldr	w8, [sp, #132]
fffffe00079ab16c:      	cbnz	w8, 0xfffffe00079ab0ec <_nstx_decode_report+0xa4>
fffffe00079ab170:      	ldr	w8, [sp, #128]
fffffe00079ab174:      	cmp	w8, w10
fffffe00079ab178:      	b.hs	0xfffffe00079ab0ec <_nstx_decode_report+0xa4>
fffffe00079ab17c:      	lsr	w15, w15, #27
fffffe00079ab180:      	cbnz	w15, 0xfffffe00079ab1e8 <_nstx_decode_report+0x1a0>
fffffe00079ab184:      	add	w15, w8, #1
fffffe00079ab188:      	lsr	w14, w14, #16
fffffe00079ab18c:      	str	w15, [sp, #128]
fffffe00079ab190:      	strh	w14, [x13, x8, lsl #1]
fffffe00079ab194:      	b	0xfffffe00079ab0ec <_nstx_decode_report+0xa4>
fffffe00079ab198:      	cbz	w8, 0xfffffe00079ab1f8 <_nstx_decode_report+0x1b0>
fffffe00079ab19c:      	mov	x9, xzr
fffffe00079ab1a0:      	mov	w10, #1
fffffe00079ab1a4:      	mov	x11, sp
fffffe00079ab1a8:      	b	0xfffffe00079ab1bc <_nstx_decode_report+0x174>
fffffe00079ab1ac:      	add	x9, x9, #1
fffffe00079ab1b0:      	cmp	x9, x8
fffffe00079ab1b4:      	cset	w10, lo
fffffe00079ab1b8:      	b.eq	0xfffffe00079ab1f8 <_nstx_decode_report+0x1b0>
fffffe00079ab1bc:      	cbz	x9, 0xfffffe00079ab1ac <_nstx_decode_report+0x164>
fffffe00079ab1c0:      	ldrh	w12, [x11, x9, lsl #1]
fffffe00079ab1c4:      	mov	x13, xzr
fffffe00079ab1c8:      	ldrh	w14, [x11, x13, lsl #1]
fffffe00079ab1cc:      	cmp	w12, w14
fffffe00079ab1d0:      	b.eq	0xfffffe00079ab1e4 <_nstx_decode_report+0x19c>
fffffe00079ab1d4:      	add	x13, x13, #1
fffffe00079ab1d8:      	cmp	x9, x13
fffffe00079ab1dc:      	b.ne	0xfffffe00079ab1c8 <_nstx_decode_report+0x180>
fffffe00079ab1e0:      	b	0xfffffe00079ab1ac <_nstx_decode_report+0x164>
fffffe00079ab1e4:      	tbz	w10, #0, 0xfffffe00079ab1f8 <_nstx_decode_report+0x1b0>
fffffe00079ab1e8:      	mov	w8, #-1
fffffe00079ab1ec:      	mov	w0, w8
fffffe00079ab1f0:      	add	sp, sp, #144
fffffe00079ab1f4:      	ret
fffffe00079ab1f8:      	ldp	q0, q1, [sp, #96]
fffffe00079ab1fc:      	mov	w8, wzr
fffffe00079ab200:      	ldp	q2, q3, [sp, #32]
fffffe00079ab204:      	stp	q0, q1, [x0, #96]
fffffe00079ab208:      	ldp	q1, q0, [sp, #64]
fffffe00079ab20c:      	stp	q2, q3, [x0, #32]
fffffe00079ab210:      	stp	q1, q0, [x0, #64]
fffffe00079ab214:      	ldr	x9, [sp, #128]
fffffe00079ab218:      	ldp	q0, q2, [sp]
fffffe00079ab21c:      	str	x9, [x0, #128]
fffffe00079ab220:      	stp	q0, q2, [x0]
fffffe00079ab224:      	b	0xfffffe00079ab1ec <_nstx_decode_report+0x1a4>
