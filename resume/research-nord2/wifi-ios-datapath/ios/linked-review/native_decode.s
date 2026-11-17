
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b01ec <_decode>:
fffffe00079b01ec:      	sub	sp, sp, #192
fffffe00079b01f0:      	stp	x28, x27, [sp, #96]
fffffe00079b01f4:      	stp	x26, x25, [sp, #112]
fffffe00079b01f8:      	stp	x24, x23, [sp, #128]
fffffe00079b01fc:      	stp	x22, x21, [sp, #144]
fffffe00079b0200:      	stp	x20, x19, [sp, #160]
fffffe00079b0204:      	stp	x29, x30, [sp, #176]
fffffe00079b0208:      	add	x29, sp, #176
fffffe00079b020c:      	mov	w8, #9720
fffffe00079b0210:      	ldr	w21, [x0, #9716]
fffffe00079b0214:      	movi.2d	v0, #0000000000000000
fffffe00079b0218:      	mov	x19, x0
fffffe00079b021c:      	add	x8, x0, x8
fffffe00079b0220:      	str	xzr, [x0, #9768]
fffffe00079b0224:      	cmp	w21, #3
fffffe00079b0228:      	stp	q0, q0, [x8]
fffffe00079b022c:      	str	q0, [x8, #32]
fffffe00079b0230:      	b.hi	0xfffffe00079b024c <_decode+0x60>
fffffe00079b0234:      	mov	w8, #9776
fffffe00079b0238:      	ldr	x1, [x19, #152]
fffffe00079b023c:      	add	x0, x19, x8
fffffe00079b0240:      	mov	w2, #1
fffffe00079b0244:      	bl	0xfffffe00079b1978 <_nsdd_reject>
fffffe00079b0248:      	b	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b024c:      	mov	w8, #5873
fffffe00079b0250:      	add	x20, x19, x8
fffffe00079b0254:      	ldrb	w8, [x20, #3]
fffffe00079b0258:      	lsr	w22, w8, #3
fffffe00079b025c:      	cmp	w22, #6
fffffe00079b0260:      	b.ne	0xfffffe00079b029c <_decode+0xb0>
fffffe00079b0264:      	mov	w8, #10224
fffffe00079b0268:      	mov	x1, x20
fffffe00079b026c:      	add	x0, x19, x8
fffffe00079b0270:      	mov	x2, x21
fffffe00079b0274:      	bl	0xfffffe00079ab228 <_nstx_report>
fffffe00079b0278:      	mov	w0, wzr
fffffe00079b027c:      	ldp	x29, x30, [sp, #176]
fffffe00079b0280:      	ldp	x20, x19, [sp, #160]
fffffe00079b0284:      	ldp	x22, x21, [sp, #144]
fffffe00079b0288:      	ldp	x24, x23, [sp, #128]
fffffe00079b028c:      	ldp	x26, x25, [sp, #112]
fffffe00079b0290:      	ldp	x28, x27, [sp, #96]
fffffe00079b0294:      	add	sp, sp, #192
fffffe00079b0298:      	ret
fffffe00079b029c:      	cmp	w21, #23
fffffe00079b02a0:      	b.ls	0xfffffe00079b0234 <_decode+0x48>
fffffe00079b02a4:      	mov	w9, #5874
fffffe00079b02a8:      	cmp	w8, #7
fffffe00079b02ac:      	add	x23, x19, x9
fffffe00079b02b0:      	b.hi	0xfffffe00079b02e4 <_decode+0xf8>
fffffe00079b02b4:      	mov	w8, #6
fffffe00079b02b8:      	ldur	q0, [x19, #152]
fffffe00079b02bc:      	movi.2d	v1, #0000000000000000
fffffe00079b02c0:      	add	x0, sp, #32
fffffe00079b02c4:      	str	xzr, [sp, #80]
fffffe00079b02c8:      	str	w8, [sp, #48]
fffffe00079b02cc:      	ldrh	w8, [x20]
fffffe00079b02d0:      	str	q0, [sp, #32]
fffffe00079b02d4:      	stur	q1, [sp, #52]
fffffe00079b02d8:      	stur	q1, [sp, #68]
fffffe00079b02dc:      	stp	w21, w8, [sp, #56]
fffffe00079b02e0:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079b02e4:      	cmp	w22, #2
fffffe00079b02e8:      	b.eq	0xfffffe00079b0430 <_decode+0x244>
fffffe00079b02ec:      	cmp	w22, #7
fffffe00079b02f0:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b02f4:      	ldrh	w8, [x20, #2]
fffffe00079b02f8:      	mov	w22, #14351
fffffe00079b02fc:      	mov	w10, #14336
fffffe00079b0300:      	and	w9, w8, w22
fffffe00079b0304:      	cmp	w9, w10
fffffe00079b0308:      	b.ne	0xfffffe00079b0368 <_decode+0x17c>
fffffe00079b030c:      	cmp	w21, #29
fffffe00079b0310:      	b.lo	0xfffffe00079b0368 <_decode+0x17c>
fffffe00079b0314:      	ldrb	w9, [x20, #28]
fffffe00079b0318:      	cmp	w9, #15
fffffe00079b031c:      	b.ne	0xfffffe00079b0368 <_decode+0x17c>
fffffe00079b0320:      	ldp	x1, x2, [x19, #152]
fffffe00079b0324:      	movi.2d	v0, #0000000000000000
fffffe00079b0328:      	add	x0, sp, #32
fffffe00079b032c:      	ldp	w6, w7, [x19, #120]
fffffe00079b0330:      	ldr	w5, [x19, #60]
fffffe00079b0334:      	mov	x3, x20
fffffe00079b0338:      	ldr	w8, [x19, #140]
fffffe00079b033c:      	mov	x4, x21
fffffe00079b0340:      	str	xzr, [sp, #80]
fffffe00079b0344:      	stp	q0, q0, [sp, #48]
fffffe00079b0348:      	str	q0, [sp, #32]
fffffe00079b034c:      	str	w8, [sp]
fffffe00079b0350:      	bl	0xfffffe00079b11c0 <_nstd_txs>
fffffe00079b0354:      	add	x0, sp, #32
fffffe00079b0358:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079b035c:      	ldrb	w8, [x20, #2]
fffffe00079b0360:      	ldrb	w9, [x20, #3]
fffffe00079b0364:      	bfi	w8, w9, #8, #24
fffffe00079b0368:      	and	w8, w8, w22
fffffe00079b036c:      	mov	w9, #14337
fffffe00079b0370:      	cmp	w8, w9
fffffe00079b0374:      	b.eq	0xfffffe00079b0438 <_decode+0x24c>
fffffe00079b0378:      	mov	w0, wzr
fffffe00079b037c:      	mov	w9, #14336
fffffe00079b0380:      	cmp	w8, w9
fffffe00079b0384:      	b.ne	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0388:      	cmp	w21, #36
fffffe00079b038c:      	b.lo	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0390:      	ldrb	w8, [x20, #28]
fffffe00079b0394:      	add	x22, x20, #36
fffffe00079b0398:      	sub	x21, x21, #36
fffffe00079b039c:      	ldrb	w9, [x20, #29]
fffffe00079b03a0:      	cmp	w8, #15
fffffe00079b03a4:      	str	x22, [x19, #9760]
fffffe00079b03a8:      	str	x21, [x19, #9768]
fffffe00079b03ac:      	strb	w9, [x20, #3872]
fffffe00079b03b0:      	b.ne	0xfffffe00079b05b8 <_decode+0x3cc>
fffffe00079b03b4:      	ldr	w8, [x19, #1520]
fffffe00079b03b8:      	mov	w9, #9776
fffffe00079b03bc:      	add	x0, x19, x9
fffffe00079b03c0:      	ldr	x1, [x19, #152]
fffffe00079b03c4:      	mov	x2, x22
fffffe00079b03c8:      	mov	x3, x21
fffffe00079b03cc:      	cmp	w8, #0
fffffe00079b03d0:      	cset	w4, ne
fffffe00079b03d4:      	bl	0xfffffe00079b2160 <_nsdd_event>
fffffe00079b03d8:      	ldrb	w8, [x20, #28]
fffffe00079b03dc:      	cmp	w8, #15
fffffe00079b03e0:      	b.ne	0xfffffe00079b05b8 <_decode+0x3cc>
fffffe00079b03e4:      	cmp	x21, #88
fffffe00079b03e8:      	b.lo	0xfffffe00079b05b8 <_decode+0x3cc>
fffffe00079b03ec:      	ldr	w8, [x19, #60]
fffffe00079b03f0:      	cbz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b03f4:      	ldrb	w8, [x22]
fffffe00079b03f8:      	ldr	w9, [x19, #120]
fffffe00079b03fc:      	cmp	w9, w8
fffffe00079b0400:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0404:      	ldrb	w8, [x20, #40]
fffffe00079b0408:      	ldr	w9, [x19, #124]
fffffe00079b040c:      	cmp	w9, w8
fffffe00079b0410:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0414:      	ldr	w8, [x19, #140]
fffffe00079b0418:      	cbnz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b041c:      	mov	w8, #1
fffffe00079b0420:      	ldrb	w9, [x20, #37]
fffffe00079b0424:      	mov	w0, wzr
fffffe00079b0428:      	stp	w8, w9, [x19, #140]
fffffe00079b042c:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0430:      	mov	w15, #1
fffffe00079b0434:      	b	0xfffffe00079b043c <_decode+0x250>
fffffe00079b0438:      	mov	w15, wzr
fffffe00079b043c:      	ldrb	w10, [x20]
fffffe00079b0440:      	ldrb	w8, [x23]
fffffe00079b0444:      	bfi	x10, x8, #8, #8
fffffe00079b0448:      	cmp	w10, #24
fffffe00079b044c:      	b.lo	0xfffffe00079b0234 <_decode+0x48>
fffffe00079b0450:      	cmp	x10, x21
fffffe00079b0454:      	b.hi	0xfffffe00079b0234 <_decode+0x48>
fffffe00079b0458:      	ldrb	w12, [x23, #5]
fffffe00079b045c:      	ldrb	w8, [x23, #6]
fffffe00079b0460:      	lsl	w9, w12, #16
fffffe00079b0464:      	bfi	w9, w8, #24, #8
fffffe00079b0468:      	tst	w9, #0xf800000
fffffe00079b046c:      	b.ne	0xfffffe00079b0520 <_decode+0x334>
fffffe00079b0470:      	ldrb	w16, [x23, #9]
fffffe00079b0474:      	ldrb	w8, [x23, #10]
fffffe00079b0478:      	lsl	w9, w16, #16
fffffe00079b047c:      	bfi	w9, w8, #24, #8
fffffe00079b0480:      	mov	w8, #192937984
fffffe00079b0484:      	and	w8, w9, w8
fffffe00079b0488:      	cbnz	w8, 0xfffffe00079b0520 <_decode+0x334>
fffffe00079b048c:      	ldrb	w8, [x23, #15]
fffffe00079b0490:      	and	w8, w8, #0x3
fffffe00079b0494:      	cbnz	w8, 0xfffffe00079b0520 <_decode+0x334>
fffffe00079b0498:      	ldrb	w8, [x23, #16]
fffffe00079b049c:      	and	w8, w8, #0x18
fffffe00079b04a0:      	cbnz	w8, 0xfffffe00079b0520 <_decode+0x334>
fffffe00079b04a4:      	ldrb	w14, [x23, #4]
fffffe00079b04a8:      	mov	w8, #40
fffffe00079b04ac:      	mov	w9, #24
fffffe00079b04b0:      	ldrb	w17, [x23, #8]
fffffe00079b04b4:      	tst	w14, #0x40
fffffe00079b04b8:      	csel	x8, x9, x8, eq
fffffe00079b04bc:      	csel	x0, xzr, x9, eq
fffffe00079b04c0:      	tst	w14, #0x8
fffffe00079b04c4:      	add	x9, x8, #16
fffffe00079b04c8:      	csel	x11, xzr, x8, eq
fffffe00079b04cc:      	csel	x8, x8, x9, eq
fffffe00079b04d0:      	tst	w14, #0x10
fffffe00079b04d4:      	add	x9, x8, #8
fffffe00079b04d8:      	csel	x8, x8, x9, eq
fffffe00079b04dc:      	tst	w14, #0x20
fffffe00079b04e0:      	add	x9, x8, #8
fffffe00079b04e4:      	csel	x8, x8, x9, eq
fffffe00079b04e8:      	sxtb	w9, w14
fffffe00079b04ec:      	add	x13, x8, #72
fffffe00079b04f0:      	cmp	w9, #0
fffffe00079b04f4:      	lsr	x9, x17, #5
fffffe00079b04f8:      	csel	x8, x13, x8, lt
fffffe00079b04fc:      	and	x9, x9, #0x6
fffffe00079b0500:      	add	x9, x8, x9
fffffe00079b0504:      	cmp	x9, x10
fffffe00079b0508:      	b.ls	0xfffffe00079b0534 <_decode+0x348>
fffffe00079b050c:      	ldr	x1, [x19, #152]
fffffe00079b0510:      	mov	w8, #9776
fffffe00079b0514:      	add	x0, x19, x8
fffffe00079b0518:      	mov	w2, #3
fffffe00079b051c:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b0520:      	ldr	x1, [x19, #152]
fffffe00079b0524:      	mov	w8, #9776
fffffe00079b0528:      	add	x0, x19, x8
fffffe00079b052c:      	mov	w2, #2
fffffe00079b0530:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b0534:      	ldrb	w8, [x23, #3]
fffffe00079b0538:      	lsl	w13, w14, #8
fffffe00079b053c:      	add	x21, x20, x9
fffffe00079b0540:      	sub	x22, x10, x9
fffffe00079b0544:      	and	w9, w13, #0x300
fffffe00079b0548:      	and	w13, w12, #0x1f
fffffe00079b054c:      	orr	w9, w9, w8
fffffe00079b0550:      	tbnz	w17, #5, 0xfffffe00079b0670 <_decode+0x484>
fffffe00079b0554:      	cmp	x22, #1
fffffe00079b0558:      	cset	w10, hi
fffffe00079b055c:      	and	w10, w15, w10
fffffe00079b0560:      	cmp	w10, #1
fffffe00079b0564:      	b.ne	0xfffffe00079b0890 <_decode+0x6a4>
fffffe00079b0568:      	ldrb	w10, [x21]
fffffe00079b056c:      	and	w10, w10, #0xc
fffffe00079b0570:      	cmp	w10, #8
fffffe00079b0574:      	b.ne	0xfffffe00079b0890 <_decode+0x6a4>
fffffe00079b0578:      	ldr	w10, [x19, #1712]
fffffe00079b057c:      	subs	x11, x22, #36
fffffe00079b0580:      	add	w10, w10, #1
fffffe00079b0584:      	str	w10, [x19, #1712]
fffffe00079b0588:      	b.lo	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b058c:      	sub	x10, x22, #18
fffffe00079b0590:      	cmp	x10, #1514
fffffe00079b0594:      	b.ls	0xfffffe00079b0b64 <_decode+0x978>
fffffe00079b0598:      	ldr	w8, [x19, #1720]
fffffe00079b059c:      	mov	w9, #9776
fffffe00079b05a0:      	ldr	x1, [x19, #152]
fffffe00079b05a4:      	add	x0, x19, x9
fffffe00079b05a8:      	mov	w2, #4
fffffe00079b05ac:      	add	w8, w8, #1
fffffe00079b05b0:      	str	w8, [x19, #1720]
fffffe00079b05b4:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b05b8:      	cmp	w8, #16
fffffe00079b05bc:      	b.ne	0xfffffe00079b07c4 <_decode+0x5d8>
fffffe00079b05c0:      	cmp	x21, #24
fffffe00079b05c4:      	b.lo	0xfffffe00079b07c4 <_decode+0x5d8>
fffffe00079b05c8:      	ldrb	w8, [x19, #1585]
fffffe00079b05cc:      	cbz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b05d0:      	ldrb	w8, [x22]
fffffe00079b05d4:      	ldrb	w9, [x19, #204]
fffffe00079b05d8:      	cmp	w8, w9
fffffe00079b05dc:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b05e0:      	ldrb	w9, [x20, #37]
fffffe00079b05e4:      	ldrb	w10, [x19, #1584]
fffffe00079b05e8:      	cmp	w9, w10
fffffe00079b05ec:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b05f0:      	ldrb	w10, [x20, #39]
fffffe00079b05f4:      	ldrb	w11, [x19, #210]
fffffe00079b05f8:      	cmp	w10, w11
fffffe00079b05fc:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0600:      	ldrb	w11, [x20, #41]
fffffe00079b0604:      	ldrb	w12, [x19, #209]
fffffe00079b0608:      	cmp	w11, w12
fffffe00079b060c:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0610:      	ldrb	w12, [x20, #38]
fffffe00079b0614:      	mov	w0, wzr
fffffe00079b0618:      	cbnz	w12, 0xfffffe00079b027c <_decode+0x90>
fffffe00079b061c:      	ldr	w12, [x20, #48]
fffffe00079b0620:      	cbz	w12, 0xfffffe00079b027c <_decode+0x90>
fffffe00079b0624:      	mov	w13, #60000
fffffe00079b0628:      	cmp	w12, w13
fffffe00079b062c:      	b.hi	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0630:      	mov	w14, #1000
fffffe00079b0634:      	ldr	x13, [x19, #168]
fffffe00079b0638:      	mul	x14, x12, x14
fffffe00079b063c:      	cmn	x14, x13
fffffe00079b0640:      	b.hs	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0644:      	adrp	x15, 0xfffffe000700a000 <_receive+0x24c>
fffffe00079b0648:      	strb	w8, [x23, #3874]
fffffe00079b064c:      	add	x8, x13, x14
fffffe00079b0650:      	strb	w9, [x23, #3876]
fffffe00079b0654:      	strb	w10, [x23, #3878]
fffffe00079b0658:      	ldr	d0, [x15, #2856]
fffffe00079b065c:      	strb	w11, [x23, #3877]
fffffe00079b0660:      	str	x8, [x19, #1576]
fffffe00079b0664:      	str	w12, [x19, #9756]
fffffe00079b0668:      	str	d0, [x19, #9720]
fffffe00079b066c:      	b	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0670:      	cbz	w15, 0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b0674:      	cmp	x22, #14
fffffe00079b0678:      	b.lo	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b067c:      	cmp	x22, #1514
fffffe00079b0680:      	b.hi	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b0684:      	tbz	w14, #6, 0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b0688:      	add	x15, x0, #16
fffffe00079b068c:      	cmp	x15, x10
fffffe00079b0690:      	b.hi	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b0694:      	add	x5, x20, x0
fffffe00079b0698:      	ldrb	w15, [x19, #198]
fffffe00079b069c:      	ldrb	w17, [x5, #2]
fffffe00079b06a0:      	cmp	w17, w15
fffffe00079b06a4:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b06a8:      	ldrb	w0, [x5, #3]
fffffe00079b06ac:      	ldrb	w15, [x19, #199]
fffffe00079b06b0:      	cmp	w0, w15
fffffe00079b06b4:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b06b8:      	ldrb	w1, [x5, #4]
fffffe00079b06bc:      	ldrb	w15, [x19, #200]
fffffe00079b06c0:      	cmp	w1, w15
fffffe00079b06c4:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b06c8:      	ldrb	w2, [x5, #5]
fffffe00079b06cc:      	ldrb	w15, [x19, #201]
fffffe00079b06d0:      	cmp	w2, w15
fffffe00079b06d4:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b06d8:      	ldrb	w3, [x5, #6]
fffffe00079b06dc:      	ldrb	w15, [x19, #202]
fffffe00079b06e0:      	cmp	w3, w15
fffffe00079b06e4:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b06e8:      	ldrb	w4, [x5, #7]
fffffe00079b06ec:      	ldrb	w15, [x19, #203]
fffffe00079b06f0:      	cmp	w4, w15
fffffe00079b06f4:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b06f8:      	ldrb	w15, [x21]
fffffe00079b06fc:      	tbnz	w15, #0, 0xfffffe00079b075c <_decode+0x570>
fffffe00079b0700:      	ldrb	w6, [x19, #192]
fffffe00079b0704:      	cmp	w15, w6
fffffe00079b0708:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b070c:      	ldrb	w6, [x21, #1]
fffffe00079b0710:      	ldrb	w7, [x19, #193]
fffffe00079b0714:      	cmp	w6, w7
fffffe00079b0718:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b071c:      	ldrb	w6, [x21, #2]
fffffe00079b0720:      	ldrb	w7, [x19, #194]
fffffe00079b0724:      	cmp	w6, w7
fffffe00079b0728:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b072c:      	ldrb	w6, [x21, #3]
fffffe00079b0730:      	ldrb	w7, [x19, #195]
fffffe00079b0734:      	cmp	w6, w7
fffffe00079b0738:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b073c:      	ldrb	w6, [x21, #4]
fffffe00079b0740:      	ldrb	w7, [x19, #196]
fffffe00079b0744:      	cmp	w6, w7
fffffe00079b0748:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b074c:      	ldrb	w6, [x21, #5]
fffffe00079b0750:      	ldrb	w7, [x19, #197]
fffffe00079b0754:      	cmp	w6, w7
fffffe00079b0758:      	b.ne	0xfffffe00079b07b0 <_decode+0x5c4>
fffffe00079b075c:      	str	w8, [sp, #28]
fffffe00079b0760:      	ldrb	w8, [x5, #1]
fffffe00079b0764:      	str	x16, [sp, #16]
fffffe00079b0768:      	mov	w16, #132
fffffe00079b076c:      	tst	w8, w16
fffffe00079b0770:      	b.ne	0xfffffe00079b079c <_decode+0x5b0>
fffffe00079b0774:      	ldrb	w24, [x5]
fffffe00079b0778:      	mov	w6, #780
fffffe00079b077c:      	mov	w16, w24
fffffe00079b0780:      	bfi	w16, w8, #8, #8
fffffe00079b0784:      	and	w16, w16, w6
fffffe00079b0788:      	cmp	w16, #520
fffffe00079b078c:      	b.ne	0xfffffe00079b079c <_decode+0x5b0>
fffffe00079b0790:      	ldrb	w16, [x5, #8]
fffffe00079b0794:      	tst	w16, #0xf
fffffe00079b0798:      	b.eq	0xfffffe00079b0d9c <_decode+0xbb0>
fffffe00079b079c:      	ldr	x1, [x19, #152]
fffffe00079b07a0:      	mov	w8, #9776
fffffe00079b07a4:      	add	x0, x19, x8
fffffe00079b07a8:      	mov	w2, #7
fffffe00079b07ac:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b07b0:      	ldr	x1, [x19, #152]
fffffe00079b07b4:      	mov	w8, #9776
fffffe00079b07b8:      	add	x0, x19, x8
fffffe00079b07bc:      	mov	w2, #6
fffffe00079b07c0:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b07c4:      	cmp	w8, #12
fffffe00079b07c8:      	b.ne	0xfffffe00079b0a24 <_decode+0x838>
fffffe00079b07cc:      	cmp	x21, #8
fffffe00079b07d0:      	b.lo	0xfffffe00079b0a24 <_decode+0x838>
fffffe00079b07d4:      	ldr	w8, [x19, #1544]
fffffe00079b07d8:      	cbz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b07dc:      	ldrb	w8, [x20, #29]
fffffe00079b07e0:      	ldr	w9, [x19, #1548]
fffffe00079b07e4:      	cmp	w9, w8
fffffe00079b07e8:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b07ec:      	ldrb	w8, [x22]
fffffe00079b07f0:      	ldrb	w9, [x19, #198]
fffffe00079b07f4:      	cmp	w8, w9
fffffe00079b07f8:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b07fc:      	ldrb	w8, [x23, #36]
fffffe00079b0800:      	ldrb	w9, [x19, #199]
fffffe00079b0804:      	cmp	w8, w9
fffffe00079b0808:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b080c:      	ldrb	w8, [x23, #37]
fffffe00079b0810:      	ldrb	w9, [x19, #200]
fffffe00079b0814:      	cmp	w8, w9
fffffe00079b0818:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b081c:      	ldrb	w8, [x23, #38]
fffffe00079b0820:      	ldrb	w9, [x19, #201]
fffffe00079b0824:      	cmp	w8, w9
fffffe00079b0828:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b082c:      	ldrb	w8, [x23, #39]
fffffe00079b0830:      	ldrb	w9, [x19, #202]
fffffe00079b0834:      	cmp	w8, w9
fffffe00079b0838:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b083c:      	ldrb	w8, [x23, #40]
fffffe00079b0840:      	ldrb	w9, [x19, #203]
fffffe00079b0844:      	cmp	w8, w9
fffffe00079b0848:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b084c:      	ldrb	w8, [x23, #41]
fffffe00079b0850:      	ldrb	w9, [x19, #206]
fffffe00079b0854:      	cmp	w8, w9
fffffe00079b0858:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b085c:      	ldrb	w9, [x23, #42]
fffffe00079b0860:      	ldrb	w10, [x19, #204]
fffffe00079b0864:      	cmp	w9, w10
fffffe00079b0868:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b086c:      	mov	w10, #1
fffffe00079b0870:      	mov	w11, #4
fffffe00079b0874:      	str	wzr, [x19, #1544]
fffffe00079b0878:      	mov	w0, wzr
fffffe00079b087c:      	strb	w8, [x23, #3875]
fffffe00079b0880:      	str	w10, [x19, #1524]
fffffe00079b0884:      	str	w11, [x19, #9720]
fffffe00079b0888:      	strb	w9, [x23, #3874]
fffffe00079b088c:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0890:      	cmp	x22, #24
fffffe00079b0894:      	b.lo	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0898:      	and	w8, w17, #0x1f
fffffe00079b089c:      	cmp	w8, #12
fffffe00079b08a0:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b08a4:      	cbnz	w13, 0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b08a8:      	ldrb	w8, [x21, #1]
fffffe00079b08ac:      	tst	w8, #0xffffffc7
fffffe00079b08b0:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b08b4:      	ldrb	w8, [x21, #22]
fffffe00079b08b8:      	tst	w8, #0xf
fffffe00079b08bc:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b08c0:      	ldrb	w8, [x21, #10]
fffffe00079b08c4:      	ldrb	w9, [x19, #198]
fffffe00079b08c8:      	cmp	w8, w9
fffffe00079b08cc:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b08d0:      	ldrb	w9, [x21, #11]
fffffe00079b08d4:      	ldrb	w10, [x19, #199]
fffffe00079b08d8:      	cmp	w9, w10
fffffe00079b08dc:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b08e0:      	ldrb	w10, [x21, #12]
fffffe00079b08e4:      	ldrb	w11, [x19, #200]
fffffe00079b08e8:      	cmp	w10, w11
fffffe00079b08ec:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b08f0:      	ldrb	w11, [x21, #13]
fffffe00079b08f4:      	ldrb	w12, [x19, #201]
fffffe00079b08f8:      	cmp	w11, w12
fffffe00079b08fc:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0900:      	ldrb	w12, [x21, #14]
fffffe00079b0904:      	ldrb	w13, [x19, #202]
fffffe00079b0908:      	cmp	w12, w13
fffffe00079b090c:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0910:      	ldrb	w13, [x21, #15]
fffffe00079b0914:      	ldrb	w14, [x19, #203]
fffffe00079b0918:      	cmp	w13, w14
fffffe00079b091c:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0920:      	ldrb	w14, [x21, #16]
fffffe00079b0924:      	cmp	w14, w8
fffffe00079b0928:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b092c:      	ldrb	w8, [x21, #17]
fffffe00079b0930:      	cmp	w8, w9
fffffe00079b0934:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0938:      	ldrb	w8, [x21, #18]
fffffe00079b093c:      	cmp	w8, w10
fffffe00079b0940:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0944:      	ldrb	w8, [x21, #19]
fffffe00079b0948:      	cmp	w8, w11
fffffe00079b094c:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0950:      	ldrb	w8, [x21, #20]
fffffe00079b0954:      	cmp	w8, w12
fffffe00079b0958:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b095c:      	ldrb	w8, [x21, #21]
fffffe00079b0960:      	cmp	w8, w13
fffffe00079b0964:      	b.ne	0xfffffe00079b0a10 <_decode+0x824>
fffffe00079b0968:      	ldrb	w8, [x21]
fffffe00079b096c:      	mov	w9, #1610612736
fffffe00079b0970:      	add	w9, w9, w8, lsl #24
fffffe00079b0974:      	extr	w9, w8, w9, #28
fffffe00079b0978:      	and	w9, w9, #0xff
fffffe00079b097c:      	cmp	w9, #7
fffffe00079b0980:      	b.hi	0xfffffe00079b1044 <_decode+0xe58>
fffffe00079b0984:      	mov	w10, #1
fffffe00079b0988:      	lsl	w9, w10, w9
fffffe00079b098c:      	mov	w10, #135
fffffe00079b0990:      	tst	w9, w10
fffffe00079b0994:      	b.eq	0xfffffe00079b1044 <_decode+0xe58>
fffffe00079b0998:      	ldrb	w8, [x21, #4]
fffffe00079b099c:      	ldrb	w9, [x19, #192]
fffffe00079b09a0:      	cmp	w8, w9
fffffe00079b09a4:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b09a8:      	ldrb	w8, [x21, #5]
fffffe00079b09ac:      	ldrb	w9, [x19, #193]
fffffe00079b09b0:      	cmp	w8, w9
fffffe00079b09b4:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b09b8:      	ldrb	w8, [x21, #6]
fffffe00079b09bc:      	ldrb	w9, [x19, #194]
fffffe00079b09c0:      	cmp	w8, w9
fffffe00079b09c4:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b09c8:      	ldrb	w8, [x21, #7]
fffffe00079b09cc:      	ldrb	w9, [x19, #195]
fffffe00079b09d0:      	cmp	w8, w9
fffffe00079b09d4:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b09d8:      	ldrb	w8, [x21, #8]
fffffe00079b09dc:      	ldrb	w9, [x19, #196]
fffffe00079b09e0:      	cmp	w8, w9
fffffe00079b09e4:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b09e8:      	ldrb	w8, [x21, #9]
fffffe00079b09ec:      	ldrb	w9, [x19, #197]
fffffe00079b09f0:      	cmp	w8, w9
fffffe00079b09f4:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b09f8:      	mov	w8, #6
fffffe00079b09fc:      	mov	w0, wzr
fffffe00079b0a00:      	str	x21, [x19, #9760]
fffffe00079b0a04:      	str	x22, [x19, #9768]
fffffe00079b0a08:      	str	w8, [x19, #9720]
fffffe00079b0a0c:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0a10:      	ldr	x1, [x19, #152]
fffffe00079b0a14:      	mov	w8, #9776
fffffe00079b0a18:      	add	x0, x19, x8
fffffe00079b0a1c:      	mov	w2, #5
fffffe00079b0a20:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b0a24:      	cmp	w8, #17
fffffe00079b0a28:      	b.ne	0xfffffe00079b0a58 <_decode+0x86c>
fffffe00079b0a2c:      	cmp	x21, #4
fffffe00079b0a30:      	b.lo	0xfffffe00079b0a58 <_decode+0x86c>
fffffe00079b0a34:      	ldrb	w8, [x22]
fffffe00079b0a38:      	ldrb	w9, [x19, #204]
fffffe00079b0a3c:      	cmp	w8, w9
fffffe00079b0a40:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0a44:      	ldrb	w8, [x23, #36]
fffffe00079b0a48:      	cmp	w8, #1
fffffe00079b0a4c:      	b.hi	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0a50:      	str	w8, [x19, #1740]
fffffe00079b0a54:      	b	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0a58:      	cmp	w8, #19
fffffe00079b0a5c:      	b.ne	0xfffffe00079b0ab0 <_decode+0x8c4>
fffffe00079b0a60:      	cmp	x21, #4
fffffe00079b0a64:      	b.lo	0xfffffe00079b0ab0 <_decode+0x8c4>
fffffe00079b0a68:      	ldrb	w8, [x22]
fffffe00079b0a6c:      	ldrb	w9, [x19, #204]
fffffe00079b0a70:      	cmp	w8, w9
fffffe00079b0a74:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0a78:      	ldr	w8, [x19, #1524]
fffffe00079b0a7c:      	cbz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0a80:      	mov	x0, x19
fffffe00079b0a84:      	bl	0xfffffe00079ab318 <_nst_close_port>
fffffe00079b0a88:      	mov	w8, #1
fffffe00079b0a8c:      	mov	w9, #13
fffffe00079b0a90:      	ldrb	w10, [x23, #36]
fffffe00079b0a94:      	mov	w0, wzr
fffffe00079b0a98:      	ldrb	w11, [x22]
fffffe00079b0a9c:      	str	w8, [x19, #1708]
fffffe00079b0aa0:      	str	w9, [x19, #9720]
fffffe00079b0aa4:      	str	w10, [x19, #9724]
fffffe00079b0aa8:      	strb	w11, [x23, #3874]
fffffe00079b0aac:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0ab0:      	cmp	w8, #36
fffffe00079b0ab4:      	b.ne	0xfffffe00079b0d6c <_decode+0xb80>
fffffe00079b0ab8:      	cmp	x21, #8
fffffe00079b0abc:      	b.lo	0xfffffe00079b0d6c <_decode+0xb80>
fffffe00079b0ac0:      	ldr	w8, [x19, #1532]
fffffe00079b0ac4:      	cbz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0ac8:      	ldr	w8, [x19, #1528]
fffffe00079b0acc:      	cbnz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0ad0:      	ldrb	w8, [x22]
fffffe00079b0ad4:      	ldrb	w9, [x19, #204]
fffffe00079b0ad8:      	cmp	w8, w9
fffffe00079b0adc:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0ae0:      	ldrb	w9, [x23, #37]
fffffe00079b0ae4:      	ldrb	w10, [x19, #198]
fffffe00079b0ae8:      	cmp	w9, w10
fffffe00079b0aec:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0af0:      	ldrb	w9, [x23, #38]
fffffe00079b0af4:      	ldrb	w10, [x19, #199]
fffffe00079b0af8:      	cmp	w9, w10
fffffe00079b0afc:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0b00:      	ldrb	w9, [x23, #39]
fffffe00079b0b04:      	ldrb	w10, [x19, #200]
fffffe00079b0b08:      	cmp	w9, w10
fffffe00079b0b0c:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0b10:      	ldrb	w9, [x23, #40]
fffffe00079b0b14:      	ldrb	w10, [x19, #201]
fffffe00079b0b18:      	cmp	w9, w10
fffffe00079b0b1c:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0b20:      	ldrb	w9, [x23, #41]
fffffe00079b0b24:      	ldrb	w10, [x19, #202]
fffffe00079b0b28:      	cmp	w9, w10
fffffe00079b0b2c:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0b30:      	ldrb	w9, [x23, #42]
fffffe00079b0b34:      	ldrb	w10, [x19, #203]
fffffe00079b0b38:      	cmp	w9, w10
fffffe00079b0b3c:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0b40:      	mov	w9, #1
fffffe00079b0b44:      	mov	w10, #5
fffffe00079b0b48:      	strb	w8, [x23, #3874]
fffffe00079b0b4c:      	mov	w8, #8
fffffe00079b0b50:      	mov	w0, wzr
fffffe00079b0b54:      	str	w9, [x19, #1528]
fffffe00079b0b58:      	str	w10, [x19, #9720]
fffffe00079b0b5c:      	str	x8, [x19, #9768]
fffffe00079b0b60:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0b64:      	and	w12, w17, #0x1f
fffffe00079b0b68:      	cmp	w12, #12
fffffe00079b0b6c:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0b70:      	cbnz	w13, 0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0b74:      	ldrb	w12, [x21, #1]
fffffe00079b0b78:      	mov	w14, #50944
fffffe00079b0b7c:      	ldrb	w13, [x21]
fffffe00079b0b80:      	and	w12, w14, w12, lsl #8
fffffe00079b0b84:      	orr	w12, w12, w13
fffffe00079b0b88:      	cmp	w12, #520
fffffe00079b0b8c:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0b90:      	ldrb	w12, [x21, #22]
fffffe00079b0b94:      	tst	w12, #0xf
fffffe00079b0b98:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0b9c:      	ldrb	w12, [x19, #207]
fffffe00079b0ba0:      	cmp	w9, w12
fffffe00079b0ba4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0ba8:      	ldrb	w9, [x21, #4]
fffffe00079b0bac:      	ldrb	w12, [x19, #192]
fffffe00079b0bb0:      	cmp	w9, w12
fffffe00079b0bb4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0bb8:      	ldrb	w12, [x21, #5]
fffffe00079b0bbc:      	ldrb	w13, [x19, #193]
fffffe00079b0bc0:      	cmp	w12, w13
fffffe00079b0bc4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0bc8:      	ldrb	w13, [x21, #6]
fffffe00079b0bcc:      	ldrb	w14, [x19, #194]
fffffe00079b0bd0:      	cmp	w13, w14
fffffe00079b0bd4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0bd8:      	ldrb	w14, [x21, #7]
fffffe00079b0bdc:      	ldrb	w15, [x19, #195]
fffffe00079b0be0:      	cmp	w14, w15
fffffe00079b0be4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0be8:      	ldrb	w15, [x21, #8]
fffffe00079b0bec:      	ldrb	w16, [x19, #196]
fffffe00079b0bf0:      	cmp	w15, w16
fffffe00079b0bf4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0bf8:      	ldrb	w16, [x21, #9]
fffffe00079b0bfc:      	ldrb	w17, [x19, #197]
fffffe00079b0c00:      	cmp	w16, w17
fffffe00079b0c04:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c08:      	ldrb	w17, [x21, #10]
fffffe00079b0c0c:      	ldrb	w0, [x19, #198]
fffffe00079b0c10:      	cmp	w17, w0
fffffe00079b0c14:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c18:      	ldrb	w1, [x21, #11]
fffffe00079b0c1c:      	ldrb	w0, [x19, #199]
fffffe00079b0c20:      	cmp	w1, w0
fffffe00079b0c24:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c28:      	ldrb	w2, [x21, #12]
fffffe00079b0c2c:      	ldrb	w0, [x19, #200]
fffffe00079b0c30:      	cmp	w2, w0
fffffe00079b0c34:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c38:      	ldrb	w3, [x21, #13]
fffffe00079b0c3c:      	ldrb	w0, [x19, #201]
fffffe00079b0c40:      	cmp	w3, w0
fffffe00079b0c44:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c48:      	ldrb	w4, [x21, #14]
fffffe00079b0c4c:      	ldrb	w0, [x19, #202]
fffffe00079b0c50:      	cmp	w4, w0
fffffe00079b0c54:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c58:      	ldrb	w5, [x21, #15]
fffffe00079b0c5c:      	ldrb	w0, [x19, #203]
fffffe00079b0c60:      	cmp	w5, w0
fffffe00079b0c64:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c68:      	ldrb	w0, [x21, #16]
fffffe00079b0c6c:      	cmp	w0, w17
fffffe00079b0c70:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c74:      	ldrb	w0, [x21, #17]
fffffe00079b0c78:      	cmp	w0, w1
fffffe00079b0c7c:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c80:      	ldrb	w0, [x21, #18]
fffffe00079b0c84:      	cmp	w0, w2
fffffe00079b0c88:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c8c:      	ldrb	w0, [x21, #19]
fffffe00079b0c90:      	cmp	w0, w3
fffffe00079b0c94:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0c98:      	ldrb	w0, [x21, #20]
fffffe00079b0c9c:      	cmp	w0, w4
fffffe00079b0ca0:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0ca4:      	ldrb	w0, [x21, #21]
fffffe00079b0ca8:      	cmp	w0, w5
fffffe00079b0cac:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0cb0:      	ldrb	w0, [x21, #24]
fffffe00079b0cb4:      	cmp	w0, #170
fffffe00079b0cb8:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0cbc:      	ldrb	w0, [x21, #25]
fffffe00079b0cc0:      	cmp	w0, #170
fffffe00079b0cc4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0cc8:      	ldrb	w0, [x21, #26]
fffffe00079b0ccc:      	cmp	w0, #3
fffffe00079b0cd0:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0cd4:      	ldrb	w0, [x21, #27]
fffffe00079b0cd8:      	cbnz	w0, 0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0cdc:      	ldrb	w0, [x21, #28]
fffffe00079b0ce0:      	cbnz	w0, 0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0ce4:      	ldrb	w0, [x21, #29]
fffffe00079b0ce8:      	cbnz	w0, 0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0cec:      	ldrb	w0, [x21, #30]
fffffe00079b0cf0:      	cmp	w0, #136
fffffe00079b0cf4:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0cf8:      	ldrb	w0, [x21, #31]
fffffe00079b0cfc:      	cmp	w0, #142
fffffe00079b0d00:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0d04:      	ldrh	w0, [x21, #34]
fffffe00079b0d08:      	rev16	w0, w0
fffffe00079b0d0c:      	cmp	x0, x11
fffffe00079b0d10:      	b.ne	0xfffffe00079b0598 <_decode+0x3ac>
fffffe00079b0d14:      	strb	w9, [x21, #18]!
fffffe00079b0d18:      	strb	w12, [x21, #1]
fffffe00079b0d1c:      	mov	w11, #7
fffffe00079b0d20:      	strb	w13, [x21, #2]
fffffe00079b0d24:      	mov	w0, wzr
fffffe00079b0d28:      	strb	w14, [x21, #3]
fffffe00079b0d2c:      	strb	w15, [x21, #4]
fffffe00079b0d30:      	strb	w16, [x21, #5]
fffffe00079b0d34:      	strb	w17, [x21, #6]
fffffe00079b0d38:      	strb	w1, [x21, #7]
fffffe00079b0d3c:      	strb	w2, [x21, #8]
fffffe00079b0d40:      	strb	w3, [x21, #9]
fffffe00079b0d44:      	strb	w4, [x21, #10]
fffffe00079b0d48:      	strb	w5, [x21, #11]
fffffe00079b0d4c:      	ldr	w9, [x19, #1716]
fffffe00079b0d50:      	str	w11, [x19, #9720]
fffffe00079b0d54:      	str	x21, [x19, #9760]
fffffe00079b0d58:      	str	x10, [x19, #9768]
fffffe00079b0d5c:      	add	w9, w9, #1
fffffe00079b0d60:      	strb	w8, [x23, #3873]
fffffe00079b0d64:      	str	w9, [x19, #1716]
fffffe00079b0d68:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0d6c:      	mov	w0, #-1
fffffe00079b0d70:      	cmp	w8, #253
fffffe00079b0d74:      	b.eq	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0d78:      	cmp	w8, #240
fffffe00079b0d7c:      	b.eq	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0d80:      	cmp	w8, #237
fffffe00079b0d84:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0d88:      	ldrb	w8, [x23, #31]
fffffe00079b0d8c:      	cmp	w8, #35
fffffe00079b0d90:      	b.ne	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b0d94:      	mov	w0, #-1
fffffe00079b0d98:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0d9c:      	ldrb	w16, [x21, #12]
fffffe00079b0da0:      	cmp	w16, #136
fffffe00079b0da4:      	b.ne	0xfffffe00079b0e78 <_decode+0xc8c>
fffffe00079b0da8:      	ldrb	w16, [x21, #13]
fffffe00079b0dac:      	cmp	w16, #142
fffffe00079b0db0:      	b.ne	0xfffffe00079b0f64 <_decode+0xd78>
fffffe00079b0db4:      	ldrb	w16, [x21, #6]
fffffe00079b0db8:      	cmp	w16, w17
fffffe00079b0dbc:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0dc0:      	ldrb	w16, [x21, #7]
fffffe00079b0dc4:      	cmp	w16, w0
fffffe00079b0dc8:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0dcc:      	ldrb	w16, [x21, #8]
fffffe00079b0dd0:      	cmp	w16, w1
fffffe00079b0dd4:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0dd8:      	ldrb	w16, [x21, #9]
fffffe00079b0ddc:      	cmp	w16, w2
fffffe00079b0de0:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0de4:      	ldrb	w16, [x21, #10]
fffffe00079b0de8:      	cmp	w16, w3
fffffe00079b0dec:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0df0:      	ldrb	w16, [x21, #11]
fffffe00079b0df4:      	cmp	w16, w4
fffffe00079b0df8:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0dfc:      	ldrb	w16, [x19, #192]
fffffe00079b0e00:      	cmp	w15, w16
fffffe00079b0e04:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e08:      	ldrb	w16, [x21, #1]
fffffe00079b0e0c:      	ldrb	w17, [x19, #193]
fffffe00079b0e10:      	cmp	w16, w17
fffffe00079b0e14:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e18:      	ldrb	w16, [x21, #2]
fffffe00079b0e1c:      	ldrb	w17, [x19, #194]
fffffe00079b0e20:      	cmp	w16, w17
fffffe00079b0e24:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e28:      	ldrb	w16, [x21, #3]
fffffe00079b0e2c:      	ldrb	w17, [x19, #195]
fffffe00079b0e30:      	cmp	w16, w17
fffffe00079b0e34:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e38:      	ldrb	w16, [x21, #4]
fffffe00079b0e3c:      	ldrb	w17, [x19, #196]
fffffe00079b0e40:      	cmp	w16, w17
fffffe00079b0e44:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e48:      	ldrb	w16, [x21, #5]
fffffe00079b0e4c:      	ldrb	w17, [x19, #197]
fffffe00079b0e50:      	cmp	w16, w17
fffffe00079b0e54:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e58:      	subs	x16, x22, #18
fffffe00079b0e5c:      	b.lo	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e60:      	ldrh	w17, [x21, #16]
fffffe00079b0e64:      	rev16	w17, w17
fffffe00079b0e68:      	cmp	x17, x16
fffffe00079b0e6c:      	b.ne	0xfffffe00079b0f80 <_decode+0xd94>
fffffe00079b0e70:      	mov	w25, #1
fffffe00079b0e74:      	b	0xfffffe00079b0e8c <_decode+0xca0>
fffffe00079b0e78:      	ldr	w17, [x19, #1520]
fffffe00079b0e7c:      	cbz	w17, 0xfffffe00079b0f6c <_decode+0xd80>
fffffe00079b0e80:      	cmp	w16, #6
fffffe00079b0e84:      	b.lo	0xfffffe00079b0f6c <_decode+0xd80>
fffffe00079b0e88:      	mov	w25, wzr
fffffe00079b0e8c:      	ubfx	x26, x12, #5, #2
fffffe00079b0e90:      	cbz	w13, 0xfffffe00079b0f94 <_decode+0xda8>
fffffe00079b0e94:      	tbz	w14, #3, 0xfffffe00079b0fe0 <_decode+0xdf4>
fffffe00079b0e98:      	cmp	w13, #4
fffffe00079b0e9c:      	b.ne	0xfffffe00079b0fe0 <_decode+0xdf4>
fffffe00079b0ea0:      	add	x12, x11, #16
fffffe00079b0ea4:      	cmp	x12, x10
fffffe00079b0ea8:      	b.hi	0xfffffe00079b0fe0 <_decode+0xdf4>
fffffe00079b0eac:      	tbz	w8, #6, 0xfffffe00079b0fe0 <_decode+0xdf4>
fffffe00079b0eb0:      	mov	w8, #144
fffffe00079b0eb4:      	add	x10, x19, #632
fffffe00079b0eb8:      	tst	w15, #0x1
fffffe00079b0ebc:      	madd	x8, x26, x8, x19
fffffe00079b0ec0:      	add	x8, x8, #920
fffffe00079b0ec4:      	csel	x27, x10, x8, eq
fffffe00079b0ec8:      	ldr	w8, [x27]
fffffe00079b0ecc:      	cbz	w8, 0xfffffe00079b101c <_decode+0xe30>
fffffe00079b0ed0:      	ldrb	w8, [x27, #4]
fffffe00079b0ed4:      	cmp	w26, w8
fffffe00079b0ed8:      	b.ne	0xfffffe00079b101c <_decode+0xe30>
fffffe00079b0edc:      	ldrb	w8, [x19, #207]
fffffe00079b0ee0:      	cmp	w9, w8
fffffe00079b0ee4:      	b.ne	0xfffffe00079b1008 <_decode+0xe1c>
fffffe00079b0ee8:      	add	x0, x20, x11
fffffe00079b0eec:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079b0ef0:      	cbz	x0, 0xfffffe00079b1030 <_decode+0xe44>
fffffe00079b0ef4:      	ldr	x8, [sp, #16]
fffffe00079b0ef8:      	sxtb	w9, w24
fffffe00079b0efc:      	cmp	w9, #0
fffffe00079b0f00:      	mov	w9, #16
fffffe00079b0f04:      	and	x8, x8, #0xf
fffffe00079b0f08:      	csel	x8, x9, x8, ge
fffffe00079b0f0c:      	add	x8, x27, x8, lsl #3
fffffe00079b0f10:      	ldr	x9, [x8, #8]!
fffffe00079b0f14:      	cmp	x0, x9
fffffe00079b0f18:      	b.ls	0xfffffe00079b1030 <_decode+0xe44>
fffffe00079b0f1c:      	str	x0, [x8]
fffffe00079b0f20:      	ldrb	w8, [x21]
fffffe00079b0f24:      	tbz	w8, #0, 0xfffffe00079b0fb4 <_decode+0xdc8>
fffffe00079b0f28:      	ldrb	w8, [x19, #780]
fffffe00079b0f2c:      	cmp	w26, w8
fffffe00079b0f30:      	b.ne	0xfffffe00079b0fb4 <_decode+0xdc8>
fffffe00079b0f34:      	ldp	q0, q1, [x27, #96]
fffffe00079b0f38:      	add	x8, x19, #776
fffffe00079b0f3c:      	stp	q0, q1, [x8, #96]
fffffe00079b0f40:      	ldr	q2, [x27, #128]
fffffe00079b0f44:      	ldp	q3, q0, [x27, #32]
fffffe00079b0f48:      	str	q2, [x8, #128]
fffffe00079b0f4c:      	ldp	q2, q1, [x27, #64]
fffffe00079b0f50:      	stp	q3, q0, [x8, #32]
fffffe00079b0f54:      	ldp	q3, q0, [x27]
fffffe00079b0f58:      	stp	q2, q1, [x8, #64]
fffffe00079b0f5c:      	stp	q3, q0, [x8]
fffffe00079b0f60:      	b	0xfffffe00079b0fb4 <_decode+0xdc8>
fffffe00079b0f64:      	ldr	w16, [x19, #1520]
fffffe00079b0f68:      	cbnz	w16, 0xfffffe00079b0e88 <_decode+0xc9c>
fffffe00079b0f6c:      	ldr	x1, [x19, #152]
fffffe00079b0f70:      	mov	w8, #9776
fffffe00079b0f74:      	add	x0, x19, x8
fffffe00079b0f78:      	mov	w2, #9
fffffe00079b0f7c:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b0f80:      	ldr	x1, [x19, #152]
fffffe00079b0f84:      	mov	w8, #9776
fffffe00079b0f88:      	add	x0, x19, x8
fffffe00079b0f8c:      	mov	w2, #8
fffffe00079b0f90:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b0f94:      	tst	w8, #0x40
fffffe00079b0f98:      	cset	w8, eq
fffffe00079b0f9c:      	and	w8, w25, w8
fffffe00079b0fa0:      	cmp	w8, #1
fffffe00079b0fa4:      	b.ne	0xfffffe00079b0ff4 <_decode+0xe08>
fffffe00079b0fa8:      	ldrb	w8, [x19, #207]
fffffe00079b0fac:      	cmp	w9, w8
fffffe00079b0fb0:      	b.ne	0xfffffe00079b0ff4 <_decode+0xe08>
fffffe00079b0fb4:      	cmp	w25, #0
fffffe00079b0fb8:      	mov	w8, #7
fffffe00079b0fbc:      	mov	w0, wzr
fffffe00079b0fc0:      	cinc	w9, w8, eq
fffffe00079b0fc4:      	ldr	w8, [sp, #28]
fffffe00079b0fc8:      	str	x21, [x19, #9760]
fffffe00079b0fcc:      	str	x22, [x19, #9768]
fffffe00079b0fd0:      	str	w9, [x19, #9720]
fffffe00079b0fd4:      	strb	w8, [x23, #3873]
fffffe00079b0fd8:      	strb	w26, [x23, #3879]
fffffe00079b0fdc:      	b	0xfffffe00079b027c <_decode+0x90>
fffffe00079b0fe0:      	ldr	x1, [x19, #152]
fffffe00079b0fe4:      	mov	w8, #9776
fffffe00079b0fe8:      	add	x0, x19, x8
fffffe00079b0fec:      	mov	w2, #10
fffffe00079b0ff0:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b0ff4:      	ldr	x1, [x19, #152]
fffffe00079b0ff8:      	mov	w8, #9776
fffffe00079b0ffc:      	add	x0, x19, x8
fffffe00079b1000:      	mov	w2, #13
fffffe00079b1004:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b1008:      	tbz	w15, #0, 0xfffffe00079b101c <_decode+0xe30>
fffffe00079b100c:      	add	x8, x19, x26
fffffe00079b1010:      	ldrb	w8, [x8, #1496]
fffffe00079b1014:      	cmp	w9, w8
fffffe00079b1018:      	b.eq	0xfffffe00079b0ee8 <_decode+0xcfc>
fffffe00079b101c:      	ldr	x1, [x19, #152]
fffffe00079b1020:      	mov	w8, #9776
fffffe00079b1024:      	add	x0, x19, x8
fffffe00079b1028:      	mov	w2, #11
fffffe00079b102c:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b1030:      	ldr	x1, [x19, #152]
fffffe00079b1034:      	mov	w8, #9776
fffffe00079b1038:      	add	x0, x19, x8
fffffe00079b103c:      	mov	w2, #12
fffffe00079b1040:      	b	0xfffffe00079b0244 <_decode+0x58>
fffffe00079b1044:      	mov	w0, wzr
fffffe00079b1048:      	cmp	w8, #128
fffffe00079b104c:      	b.ne	0xfffffe00079b027c <_decode+0x90>
fffffe00079b1050:      	cmp	x22, #36
fffffe00079b1054:      	b.lo	0xfffffe00079b027c <_decode+0x90>
fffffe00079b1058:      	ldr	w8, [x19, #1524]
fffffe00079b105c:      	cbz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b1060:      	ldr	w8, [x19, #1724]
fffffe00079b1064:      	cbnz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b1068:      	ldr	w8, [x19, #1728]
fffffe00079b106c:      	cbnz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b1070:      	ldr	w8, [x19, #24]
fffffe00079b1074:      	cbnz	w8, 0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b1078:      	ldr	w8, [x19, #1708]
fffffe00079b107c:      	mov	w0, wzr
fffffe00079b1080:      	cbnz	w8, 0xfffffe00079b027c <_decode+0x90>
fffffe00079b1084:      	cmp	x22, #37
fffffe00079b1088:      	b.lo	0xfffffe00079b027c <_decode+0x90>
fffffe00079b108c:      	mov	w8, #36
fffffe00079b1090:      	mov	w9, #1
fffffe00079b1094:      	b	0xfffffe00079b10ac <_decode+0xec0>
fffffe00079b1098:      	add	x8, x8, x10
fffffe00079b109c:      	mov	w0, wzr
fffffe00079b10a0:      	add	x8, x8, #2
fffffe00079b10a4:      	cmp	x22, x8
fffffe00079b10a8:      	b.ls	0xfffffe00079b027c <_decode+0x90>
fffffe00079b10ac:      	sub	x10, x22, x8
fffffe00079b10b0:      	subs	x12, x10, #2
fffffe00079b10b4:      	b.lo	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b10b8:      	add	x11, x8, #1
fffffe00079b10bc:      	ldrb	w10, [x21, x11]
fffffe00079b10c0:      	cmp	x12, x10
fffffe00079b10c4:      	b.lo	0xfffffe00079b0278 <_decode+0x8c>
fffffe00079b10c8:      	ldrb	w12, [x21, x8]
fffffe00079b10cc:      	cmp	w12, #5
fffffe00079b10d0:      	b.ne	0xfffffe00079b1098 <_decode+0xeac>
fffffe00079b10d4:      	cmp	w10, #4
fffffe00079b10d8:      	b.lo	0xfffffe00079b1098 <_decode+0xeac>
fffffe00079b10dc:      	add	x12, x8, x21
fffffe00079b10e0:      	ldrb	w12, [x12, #3]
fffffe00079b10e4:      	cbz	w12, 0xfffffe00079b1098 <_decode+0xeac>
fffffe00079b10e8:      	str	w12, [x19, #1736]
fffffe00079b10ec:      	str	w9, [x19, #1732]
fffffe00079b10f0:      	ldrb	w10, [x21, x11]
fffffe00079b10f4:      	b	0xfffffe00079b1098 <_decode+0xeac>
