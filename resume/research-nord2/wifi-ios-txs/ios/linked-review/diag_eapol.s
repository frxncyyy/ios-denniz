
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aa3fc <_nstd_eapol>:
fffffe00079aa3fc:      	movi.2d	v0, #0000000000000000
fffffe00079aa400:      	mov	w8, #5
fffffe00079aa404:      	stp	x1, x2, [x0]
fffffe00079aa408:      	stp	w8, w4, [x0, #16]
fffffe00079aa40c:      	stur	q0, [x0, #40]
fffffe00079aa410:      	stur	q0, [x0, #24]
fffffe00079aa414:      	cbz	x3, 0xfffffe00079aa480 <_nstd_eapol+0x84>
fffffe00079aa418:      	subs	x8, x4, #4
fffffe00079aa41c:      	b.lo	0xfffffe00079aa480 <_nstd_eapol+0x84>
fffffe00079aa420:      	ldrb	w9, [x3]
fffffe00079aa424:      	cmp	x4, #7
fffffe00079aa428:      	str	w9, [x0, #24]
fffffe00079aa42c:      	ldrb	w9, [x3, #1]
fffffe00079aa430:      	str	w9, [x0, #28]
fffffe00079aa434:      	ldrh	w9, [x3, #2]
fffffe00079aa438:      	rev16	w9, w9
fffffe00079aa43c:      	str	w9, [x0, #40]
fffffe00079aa440:      	b.lo	0xfffffe00079aa480 <_nstd_eapol+0x84>
fffffe00079aa444:      	ldrb	w10, [x3, #1]
fffffe00079aa448:      	cmp	w10, #3
fffffe00079aa44c:      	b.ne	0xfffffe00079aa480 <_nstd_eapol+0x84>
fffffe00079aa450:      	ldurh	w10, [x3, #5]
fffffe00079aa454:      	mov	w11, #5066
fffffe00079aa458:      	rev16	w10, w10
fffffe00079aa45c:      	cmp	w10, w11
fffffe00079aa460:      	mov	w11, #3
fffffe00079aa464:      	csel	w11, w11, wzr, eq
fffffe00079aa468:      	cmp	w10, #138
fffffe00079aa46c:      	csinc	w11, w11, wzr, ne
fffffe00079aa470:      	cmp	x8, x9
fffffe00079aa474:      	cset	w8, eq
fffffe00079aa478:      	stp	w10, w11, [x0, #32]
fffffe00079aa47c:      	str	w8, [x0, #44]
fffffe00079aa480:      	ret
