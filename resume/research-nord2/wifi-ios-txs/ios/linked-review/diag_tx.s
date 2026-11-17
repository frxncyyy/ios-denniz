
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aa24c <_nstd_tx>:
fffffe00079aa24c:      	cmp	w6, #1
fffffe00079aa250:      	mov	w8, #1
fffffe00079aa254:      	movi.2d	v0, #0000000000000000
fffffe00079aa258:      	cinc	w9, w8, eq
fffffe00079aa25c:      	cmp	w6, #2
fffffe00079aa260:      	mov	w10, #3
fffffe00079aa264:      	csel	w9, w10, w9, eq
fffffe00079aa268:      	stp	x1, x2, [x0]
fffffe00079aa26c:      	stur	q0, [x0, #24]
fffffe00079aa270:      	stur	q0, [x0, #40]
fffffe00079aa274:      	stp	w9, w5, [x0, #16]
fffffe00079aa278:      	str	w4, [x0, #24]
fffffe00079aa27c:      	cbz	x3, 0xfffffe00079aa2e0 <_nstd_tx+0x94>
fffffe00079aa280:      	cmp	x4, #32
fffffe00079aa284:      	b.lo	0xfffffe00079aa2e0 <_nstd_tx+0x94>
fffffe00079aa288:      	ldrb	w9, [x3, #3]
fffffe00079aa28c:      	cbz	w6, 0xfffffe00079aa2e8 <_nstd_tx+0x9c>
fffffe00079aa290:      	add	x8, x3, #21
fffffe00079aa294:      	ldrb	w10, [x3, #2]
fffffe00079aa298:      	ldrb	w11, [x3, #5]
fffffe00079aa29c:      	lsl	w9, w9, #24
fffffe00079aa2a0:      	ldrb	w12, [x3, #6]
fffffe00079aa2a4:      	ld1.b	{ v0 }[0], [x8]
fffffe00079aa2a8:      	add	x8, x3, #12
fffffe00079aa2ac:      	bfi	w9, w10, #16, #8
fffffe00079aa2b0:      	lsr	w11, w11, #7
fffffe00079aa2b4:      	ubfx	w9, w9, #23, #2
fffffe00079aa2b8:      	and	w12, w12, #0x3
fffffe00079aa2bc:      	ld1.b	{ v0 }[4], [x8]
fffffe00079aa2c0:      	adrp	x8, 0xfffffe000700a000 <_receive+0x1b8>
fffffe00079aa2c4:      	stp	w9, w12, [x0, #28]
fffffe00079aa2c8:      	str	w11, [x0, #36]
fffffe00079aa2cc:      	ldr	d1, [x8, #1656]
fffffe00079aa2d0:      	and	w8, w10, #0x7f
fffffe00079aa2d4:      	ushr.2s	v0, v0, #1
fffffe00079aa2d8:      	and.8b	v0, v0, v1
fffffe00079aa2dc:      	str	d0, [x0, #40]
fffffe00079aa2e0:      	str	w8, [x0, #48]
fffffe00079aa2e4:      	ret
fffffe00079aa2e8:      	ldrb	w10, [x3, #26]
fffffe00079aa2ec:      	lsr	w9, w9, #1
fffffe00079aa2f0:      	ldrb	w11, [x3, #27]
fffffe00079aa2f4:      	ldrb	w12, [x3, #20]
fffffe00079aa2f8:      	ldrb	w13, [x3, #4]
fffffe00079aa2fc:      	bfi	w10, w11, #8, #6
fffffe00079aa300:      	ldrh	w11, [x3]
fffffe00079aa304:      	stp	w12, w13, [x0, #28]
fffffe00079aa308:      	stp	w9, w10, [x0, #36]
fffffe00079aa30c:      	stp	w11, wzr, [x0, #44]
fffffe00079aa310:      	ret
