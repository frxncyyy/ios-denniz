
$NORD2_REPO/research/ci-39865241324/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b01f4 <_decode>:
fffffe00079b01f4:      	sub	sp, sp, #192
fffffe00079b01f8:      	stp	x28, x27, [sp, #96]
fffffe00079b01fc:      	stp	x26, x25, [sp, #112]
fffffe00079b0200:      	stp	x24, x23, [sp, #128]
fffffe00079b0204:      	stp	x22, x21, [sp, #144]
fffffe00079b0208:      	stp	x20, x19, [sp, #160]
fffffe00079b020c:      	stp	x29, x30, [sp, #176]
fffffe00079b0210:      	add	x29, sp, #176
fffffe00079b0214:      	mov	w8, #9720
fffffe00079b0218:      	ldr	w21, [x0, #9716]
fffffe00079b021c:      	movi.2d	v0, #0000000000000000
fffffe00079b0220:      	mov	x19, x0
fffffe00079b0224:      	add	x8, x0, x8
fffffe00079b0228:      	str	xzr, [x0, #9768]
fffffe00079b022c:      	cmp	w21, #3
fffffe00079b0230:      	stp	q0, q0, [x8]
fffffe00079b0234:      	str	q0, [x8, #32]
fffffe00079b0238:      	b.hi	0xfffffe00079b0254 <_decode+0x60>
fffffe00079b023c:      	mov	w8, #9776
fffffe00079b0240:      	ldr	x1, [x19, #152]
fffffe00079b0244:      	add	x0, x19, x8
fffffe00079b0248:      	mov	w2, #1
fffffe00079b024c:      	bl	0xfffffe00079b1b68 <_nsdd_reject>
fffffe00079b0250:      	b	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0254:      	mov	w8, #5873
fffffe00079b0258:      	add	x20, x19, x8
fffffe00079b025c:      	ldrb	w8, [x20, #3]
fffffe00079b0260:      	lsr	w22, w8, #3
fffffe00079b0264:      	cmp	w22, #6
fffffe00079b0268:      	b.ne	0xfffffe00079b02a4 <_decode+0xb0>
fffffe00079b026c:      	mov	w8, #10224
fffffe00079b0270:      	mov	x1, x20
fffffe00079b0274:      	add	x0, x19, x8
fffffe00079b0278:      	mov	x2, x21
fffffe00079b027c:      	bl	0xfffffe00079ab234 <_nstx_report>
fffffe00079b0280:      	mov	w0, wzr
fffffe00079b0284:      	ldp	x29, x30, [sp, #176]
fffffe00079b0288:      	ldp	x20, x19, [sp, #160]
fffffe00079b028c:      	ldp	x22, x21, [sp, #144]
fffffe00079b0290:      	ldp	x24, x23, [sp, #128]
fffffe00079b0294:      	ldp	x26, x25, [sp, #112]
fffffe00079b0298:      	ldp	x28, x27, [sp, #96]
fffffe00079b029c:      	add	sp, sp, #192
fffffe00079b02a0:      	ret
fffffe00079b02a4:      	cmp	w21, #23
fffffe00079b02a8:      	b.ls	0xfffffe00079b023c <_decode+0x48>
fffffe00079b02ac:      	mov	w9, #5874
fffffe00079b02b0:      	cmp	w8, #7
fffffe00079b02b4:      	add	x23, x19, x9
fffffe00079b02b8:      	b.hi	0xfffffe00079b02ec <_decode+0xf8>
fffffe00079b02bc:      	mov	w8, #6
fffffe00079b02c0:      	ldur	q0, [x19, #152]
fffffe00079b02c4:      	movi.2d	v1, #0000000000000000
fffffe00079b02c8:      	add	x0, sp, #32
fffffe00079b02cc:      	str	xzr, [sp, #80]
fffffe00079b02d0:      	str	w8, [sp, #48]
fffffe00079b02d4:      	ldrh	w8, [x20]
fffffe00079b02d8:      	str	q0, [sp, #32]
fffffe00079b02dc:      	stur	q1, [sp, #52]
fffffe00079b02e0:      	stur	q1, [sp, #68]
fffffe00079b02e4:      	stp	w21, w8, [sp, #56]
fffffe00079b02e8:      	bl	0xfffffe00079b145c <_nstd_log>
fffffe00079b02ec:      	cmp	w22, #2
fffffe00079b02f0:      	b.eq	0xfffffe00079b0438 <_decode+0x244>
fffffe00079b02f4:      	cmp	w22, #7
fffffe00079b02f8:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b02fc:      	ldrh	w8, [x20, #2]
fffffe00079b0300:      	mov	w22, #14351
fffffe00079b0304:      	mov	w10, #14336
fffffe00079b0308:      	and	w9, w8, w22
fffffe00079b030c:      	cmp	w9, w10
fffffe00079b0310:      	b.ne	0xfffffe00079b0370 <_decode+0x17c>
fffffe00079b0314:      	cmp	w21, #29
fffffe00079b0318:      	b.lo	0xfffffe00079b0370 <_decode+0x17c>
fffffe00079b031c:      	ldrb	w9, [x20, #28]
fffffe00079b0320:      	cmp	w9, #15
fffffe00079b0324:      	b.ne	0xfffffe00079b0370 <_decode+0x17c>
fffffe00079b0328:      	ldp	x1, x2, [x19, #152]
fffffe00079b032c:      	movi.2d	v0, #0000000000000000
fffffe00079b0330:      	add	x0, sp, #32
fffffe00079b0334:      	ldp	w6, w7, [x19, #120]
fffffe00079b0338:      	ldr	w5, [x19, #60]
fffffe00079b033c:      	mov	x3, x20
fffffe00079b0340:      	ldr	w8, [x19, #140]
fffffe00079b0344:      	mov	x4, x21
fffffe00079b0348:      	str	xzr, [sp, #80]
fffffe00079b034c:      	stp	q0, q0, [sp, #48]
fffffe00079b0350:      	str	q0, [sp, #32]
fffffe00079b0354:      	str	w8, [sp]
fffffe00079b0358:      	bl	0xfffffe00079b11e0 <_nstd_txs>
fffffe00079b035c:      	add	x0, sp, #32
fffffe00079b0360:      	bl	0xfffffe00079b145c <_nstd_log>
fffffe00079b0364:      	ldrb	w8, [x20, #2]
fffffe00079b0368:      	ldrb	w9, [x20, #3]
fffffe00079b036c:      	bfi	w8, w9, #8, #24
fffffe00079b0370:      	and	w8, w8, w22
fffffe00079b0374:      	mov	w9, #14337
fffffe00079b0378:      	cmp	w8, w9
fffffe00079b037c:      	b.eq	0xfffffe00079b0440 <_decode+0x24c>
fffffe00079b0380:      	mov	w0, wzr
fffffe00079b0384:      	mov	w9, #14336
fffffe00079b0388:      	cmp	w8, w9
fffffe00079b038c:      	b.ne	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0390:      	cmp	w21, #36
fffffe00079b0394:      	b.lo	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0398:      	ldrb	w8, [x20, #28]
fffffe00079b039c:      	add	x22, x20, #36
fffffe00079b03a0:      	sub	x21, x21, #36
fffffe00079b03a4:      	ldrb	w9, [x20, #29]
fffffe00079b03a8:      	cmp	w8, #15
fffffe00079b03ac:      	str	x22, [x19, #9760]
fffffe00079b03b0:      	str	x21, [x19, #9768]
fffffe00079b03b4:      	strb	w9, [x20, #3872]
fffffe00079b03b8:      	b.ne	0xfffffe00079b05d8 <_decode+0x3e4>
fffffe00079b03bc:      	ldr	w8, [x19, #1520]
fffffe00079b03c0:      	mov	w9, #9776
fffffe00079b03c4:      	add	x0, x19, x9
fffffe00079b03c8:      	ldr	x1, [x19, #152]
fffffe00079b03cc:      	mov	x2, x22
fffffe00079b03d0:      	mov	x3, x21
fffffe00079b03d4:      	cmp	w8, #0
fffffe00079b03d8:      	cset	w4, ne
fffffe00079b03dc:      	bl	0xfffffe00079b2350 <_nsdd_event>
fffffe00079b03e0:      	ldrb	w8, [x20, #28]
fffffe00079b03e4:      	cmp	w8, #15
fffffe00079b03e8:      	b.ne	0xfffffe00079b05d8 <_decode+0x3e4>
fffffe00079b03ec:      	cmp	x21, #88
fffffe00079b03f0:      	b.lo	0xfffffe00079b05d8 <_decode+0x3e4>
fffffe00079b03f4:      	ldr	w8, [x19, #60]
fffffe00079b03f8:      	cbz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b03fc:      	ldrb	w8, [x22]
fffffe00079b0400:      	ldr	w9, [x19, #120]
fffffe00079b0404:      	cmp	w9, w8
fffffe00079b0408:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b040c:      	ldrb	w8, [x20, #40]
fffffe00079b0410:      	ldr	w9, [x19, #124]
fffffe00079b0414:      	cmp	w9, w8
fffffe00079b0418:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b041c:      	ldr	w8, [x19, #140]
fffffe00079b0420:      	cbnz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0424:      	mov	w8, #1
fffffe00079b0428:      	ldrb	w9, [x20, #37]
fffffe00079b042c:      	mov	w0, wzr
fffffe00079b0430:      	stp	w8, w9, [x19, #140]
fffffe00079b0434:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0438:      	mov	w15, #1
fffffe00079b043c:      	b	0xfffffe00079b0444 <_decode+0x250>
fffffe00079b0440:      	mov	w15, wzr
fffffe00079b0444:      	ldrb	w10, [x20]
fffffe00079b0448:      	ldrb	w8, [x23]
fffffe00079b044c:      	bfi	x10, x8, #8, #8
fffffe00079b0450:      	cmp	w10, #24
fffffe00079b0454:      	b.lo	0xfffffe00079b023c <_decode+0x48>
fffffe00079b0458:      	cmp	x10, x21
fffffe00079b045c:      	b.hi	0xfffffe00079b023c <_decode+0x48>
fffffe00079b0460:      	ldrb	w12, [x23, #5]
fffffe00079b0464:      	ldrb	w8, [x23, #6]
fffffe00079b0468:      	lsl	w9, w12, #16
fffffe00079b046c:      	bfi	w9, w8, #24, #8
fffffe00079b0470:      	tst	w9, #0xf800000
fffffe00079b0474:      	b.ne	0xfffffe00079b0528 <_decode+0x334>
fffffe00079b0478:      	ldrb	w16, [x23, #9]
fffffe00079b047c:      	ldrb	w8, [x23, #10]
fffffe00079b0480:      	lsl	w9, w16, #16
fffffe00079b0484:      	bfi	w9, w8, #24, #8
fffffe00079b0488:      	mov	w8, #192937984
fffffe00079b048c:      	and	w8, w9, w8
fffffe00079b0490:      	cbnz	w8, 0xfffffe00079b0528 <_decode+0x334>
fffffe00079b0494:      	ldrb	w8, [x23, #15]
fffffe00079b0498:      	and	w8, w8, #0x3
fffffe00079b049c:      	cbnz	w8, 0xfffffe00079b0528 <_decode+0x334>
fffffe00079b04a0:      	ldrb	w8, [x23, #16]
fffffe00079b04a4:      	and	w8, w8, #0x18
fffffe00079b04a8:      	cbnz	w8, 0xfffffe00079b0528 <_decode+0x334>
fffffe00079b04ac:      	ldrb	w14, [x23, #4]
fffffe00079b04b0:      	mov	w8, #40
fffffe00079b04b4:      	mov	w9, #24
fffffe00079b04b8:      	ldrb	w17, [x23, #8]
fffffe00079b04bc:      	tst	w14, #0x40
fffffe00079b04c0:      	csel	x8, x9, x8, eq
fffffe00079b04c4:      	csel	x0, xzr, x9, eq
fffffe00079b04c8:      	tst	w14, #0x8
fffffe00079b04cc:      	add	x9, x8, #16
fffffe00079b04d0:      	csel	x11, xzr, x8, eq
fffffe00079b04d4:      	csel	x8, x8, x9, eq
fffffe00079b04d8:      	tst	w14, #0x10
fffffe00079b04dc:      	add	x9, x8, #8
fffffe00079b04e0:      	csel	x8, x8, x9, eq
fffffe00079b04e4:      	tst	w14, #0x20
fffffe00079b04e8:      	add	x9, x8, #8
fffffe00079b04ec:      	csel	x8, x8, x9, eq
fffffe00079b04f0:      	sxtb	w9, w14
fffffe00079b04f4:      	add	x13, x8, #72
fffffe00079b04f8:      	cmp	w9, #0
fffffe00079b04fc:      	lsr	x9, x17, #5
fffffe00079b0500:      	csel	x8, x13, x8, lt
fffffe00079b0504:      	and	x9, x9, #0x6
fffffe00079b0508:      	add	x9, x8, x9
fffffe00079b050c:      	cmp	x9, x10
fffffe00079b0510:      	b.ls	0xfffffe00079b0554 <_decode+0x360>
fffffe00079b0514:      	ldr	x1, [x19, #152]
fffffe00079b0518:      	mov	w8, #9776
fffffe00079b051c:      	add	x0, x19, x8
fffffe00079b0520:      	mov	w2, #3
fffffe00079b0524:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b0528:      	mov	w8, #9776
fffffe00079b052c:      	ldr	x1, [x19, #152]
fffffe00079b0530:      	add	x22, x19, x8
fffffe00079b0534:      	mov	x2, x20
fffffe00079b0538:      	mov	x0, x22
fffffe00079b053c:      	mov	x3, x21
fffffe00079b0540:      	bl	0xfffffe00079b1ae4 <_nsdd_rx_error>
fffffe00079b0544:      	ldr	x1, [x19, #152]
fffffe00079b0548:      	mov	x0, x22
fffffe00079b054c:      	mov	w2, #2
fffffe00079b0550:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b0554:      	ldrb	w8, [x23, #3]
fffffe00079b0558:      	lsl	w13, w14, #8
fffffe00079b055c:      	add	x21, x20, x9
fffffe00079b0560:      	sub	x22, x10, x9
fffffe00079b0564:      	and	w9, w13, #0x300
fffffe00079b0568:      	and	w13, w12, #0x1f
fffffe00079b056c:      	orr	w9, w9, w8
fffffe00079b0570:      	tbnz	w17, #5, 0xfffffe00079b0690 <_decode+0x49c>
fffffe00079b0574:      	cmp	x22, #1
fffffe00079b0578:      	cset	w10, hi
fffffe00079b057c:      	and	w10, w15, w10
fffffe00079b0580:      	cmp	w10, #1
fffffe00079b0584:      	b.ne	0xfffffe00079b08b0 <_decode+0x6bc>
fffffe00079b0588:      	ldrb	w10, [x21]
fffffe00079b058c:      	and	w10, w10, #0xc
fffffe00079b0590:      	cmp	w10, #8
fffffe00079b0594:      	b.ne	0xfffffe00079b08b0 <_decode+0x6bc>
fffffe00079b0598:      	ldr	w10, [x19, #1712]
fffffe00079b059c:      	subs	x11, x22, #36
fffffe00079b05a0:      	add	w10, w10, #1
fffffe00079b05a4:      	str	w10, [x19, #1712]
fffffe00079b05a8:      	b.lo	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b05ac:      	sub	x10, x22, #18
fffffe00079b05b0:      	cmp	x10, #1514
fffffe00079b05b4:      	b.ls	0xfffffe00079b0b84 <_decode+0x990>
fffffe00079b05b8:      	ldr	w8, [x19, #1720]
fffffe00079b05bc:      	mov	w9, #9776
fffffe00079b05c0:      	ldr	x1, [x19, #152]
fffffe00079b05c4:      	add	x0, x19, x9
fffffe00079b05c8:      	mov	w2, #4
fffffe00079b05cc:      	add	w8, w8, #1
fffffe00079b05d0:      	str	w8, [x19, #1720]
fffffe00079b05d4:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b05d8:      	cmp	w8, #16
fffffe00079b05dc:      	b.ne	0xfffffe00079b07e4 <_decode+0x5f0>
fffffe00079b05e0:      	cmp	x21, #24
fffffe00079b05e4:      	b.lo	0xfffffe00079b07e4 <_decode+0x5f0>
fffffe00079b05e8:      	ldrb	w8, [x19, #1585]
fffffe00079b05ec:      	cbz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b05f0:      	ldrb	w8, [x22]
fffffe00079b05f4:      	ldrb	w9, [x19, #204]
fffffe00079b05f8:      	cmp	w8, w9
fffffe00079b05fc:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0600:      	ldrb	w9, [x20, #37]
fffffe00079b0604:      	ldrb	w10, [x19, #1584]
fffffe00079b0608:      	cmp	w9, w10
fffffe00079b060c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0610:      	ldrb	w10, [x20, #39]
fffffe00079b0614:      	ldrb	w11, [x19, #210]
fffffe00079b0618:      	cmp	w10, w11
fffffe00079b061c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0620:      	ldrb	w11, [x20, #41]
fffffe00079b0624:      	ldrb	w12, [x19, #209]
fffffe00079b0628:      	cmp	w11, w12
fffffe00079b062c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0630:      	ldrb	w12, [x20, #38]
fffffe00079b0634:      	mov	w0, wzr
fffffe00079b0638:      	cbnz	w12, 0xfffffe00079b0284 <_decode+0x90>
fffffe00079b063c:      	ldr	w12, [x20, #48]
fffffe00079b0640:      	cbz	w12, 0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0644:      	mov	w13, #60000
fffffe00079b0648:      	cmp	w12, w13
fffffe00079b064c:      	b.hi	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0650:      	mov	w14, #1000
fffffe00079b0654:      	ldr	x13, [x19, #168]
fffffe00079b0658:      	mul	x14, x12, x14
fffffe00079b065c:      	cmn	x14, x13
fffffe00079b0660:      	b.hs	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0664:      	adrp	x15, 0xfffffe000700a000 <_receive+0x260>
fffffe00079b0668:      	strb	w8, [x23, #3874]
fffffe00079b066c:      	add	x8, x13, x14
fffffe00079b0670:      	strb	w9, [x23, #3876]
fffffe00079b0674:      	strb	w10, [x23, #3878]
fffffe00079b0678:      	ldr	d0, [x15, #2728]
fffffe00079b067c:      	strb	w11, [x23, #3877]
fffffe00079b0680:      	str	x8, [x19, #1576]
fffffe00079b0684:      	str	w12, [x19, #9756]
fffffe00079b0688:      	str	d0, [x19, #9720]
fffffe00079b068c:      	b	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0690:      	cbz	w15, 0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b0694:      	cmp	x22, #14
fffffe00079b0698:      	b.lo	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b069c:      	cmp	x22, #1514
fffffe00079b06a0:      	b.hi	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b06a4:      	tbz	w14, #6, 0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b06a8:      	add	x15, x0, #16
fffffe00079b06ac:      	cmp	x15, x10
fffffe00079b06b0:      	b.hi	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b06b4:      	add	x5, x20, x0
fffffe00079b06b8:      	ldrb	w15, [x19, #198]
fffffe00079b06bc:      	ldrb	w17, [x5, #2]
fffffe00079b06c0:      	cmp	w17, w15
fffffe00079b06c4:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b06c8:      	ldrb	w0, [x5, #3]
fffffe00079b06cc:      	ldrb	w15, [x19, #199]
fffffe00079b06d0:      	cmp	w0, w15
fffffe00079b06d4:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b06d8:      	ldrb	w1, [x5, #4]
fffffe00079b06dc:      	ldrb	w15, [x19, #200]
fffffe00079b06e0:      	cmp	w1, w15
fffffe00079b06e4:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b06e8:      	ldrb	w2, [x5, #5]
fffffe00079b06ec:      	ldrb	w15, [x19, #201]
fffffe00079b06f0:      	cmp	w2, w15
fffffe00079b06f4:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b06f8:      	ldrb	w3, [x5, #6]
fffffe00079b06fc:      	ldrb	w15, [x19, #202]
fffffe00079b0700:      	cmp	w3, w15
fffffe00079b0704:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b0708:      	ldrb	w4, [x5, #7]
fffffe00079b070c:      	ldrb	w15, [x19, #203]
fffffe00079b0710:      	cmp	w4, w15
fffffe00079b0714:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b0718:      	ldrb	w15, [x21]
fffffe00079b071c:      	tbnz	w15, #0, 0xfffffe00079b077c <_decode+0x588>
fffffe00079b0720:      	ldrb	w6, [x19, #192]
fffffe00079b0724:      	cmp	w15, w6
fffffe00079b0728:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b072c:      	ldrb	w6, [x21, #1]
fffffe00079b0730:      	ldrb	w7, [x19, #193]
fffffe00079b0734:      	cmp	w6, w7
fffffe00079b0738:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b073c:      	ldrb	w6, [x21, #2]
fffffe00079b0740:      	ldrb	w7, [x19, #194]
fffffe00079b0744:      	cmp	w6, w7
fffffe00079b0748:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b074c:      	ldrb	w6, [x21, #3]
fffffe00079b0750:      	ldrb	w7, [x19, #195]
fffffe00079b0754:      	cmp	w6, w7
fffffe00079b0758:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b075c:      	ldrb	w6, [x21, #4]
fffffe00079b0760:      	ldrb	w7, [x19, #196]
fffffe00079b0764:      	cmp	w6, w7
fffffe00079b0768:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b076c:      	ldrb	w6, [x21, #5]
fffffe00079b0770:      	ldrb	w7, [x19, #197]
fffffe00079b0774:      	cmp	w6, w7
fffffe00079b0778:      	b.ne	0xfffffe00079b07d0 <_decode+0x5dc>
fffffe00079b077c:      	str	w8, [sp, #28]
fffffe00079b0780:      	ldrb	w8, [x5, #1]
fffffe00079b0784:      	str	x16, [sp, #16]
fffffe00079b0788:      	mov	w16, #132
fffffe00079b078c:      	tst	w8, w16
fffffe00079b0790:      	b.ne	0xfffffe00079b07bc <_decode+0x5c8>
fffffe00079b0794:      	ldrb	w24, [x5]
fffffe00079b0798:      	mov	w6, #780
fffffe00079b079c:      	mov	w16, w24
fffffe00079b07a0:      	bfi	w16, w8, #8, #8
fffffe00079b07a4:      	and	w16, w16, w6
fffffe00079b07a8:      	cmp	w16, #520
fffffe00079b07ac:      	b.ne	0xfffffe00079b07bc <_decode+0x5c8>
fffffe00079b07b0:      	ldrb	w16, [x5, #8]
fffffe00079b07b4:      	tst	w16, #0xf
fffffe00079b07b8:      	b.eq	0xfffffe00079b0dbc <_decode+0xbc8>
fffffe00079b07bc:      	ldr	x1, [x19, #152]
fffffe00079b07c0:      	mov	w8, #9776
fffffe00079b07c4:      	add	x0, x19, x8
fffffe00079b07c8:      	mov	w2, #7
fffffe00079b07cc:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b07d0:      	ldr	x1, [x19, #152]
fffffe00079b07d4:      	mov	w8, #9776
fffffe00079b07d8:      	add	x0, x19, x8
fffffe00079b07dc:      	mov	w2, #6
fffffe00079b07e0:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b07e4:      	cmp	w8, #12
fffffe00079b07e8:      	b.ne	0xfffffe00079b0a44 <_decode+0x850>
fffffe00079b07ec:      	cmp	x21, #8
fffffe00079b07f0:      	b.lo	0xfffffe00079b0a44 <_decode+0x850>
fffffe00079b07f4:      	ldr	w8, [x19, #1544]
fffffe00079b07f8:      	cbz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b07fc:      	ldrb	w8, [x20, #29]
fffffe00079b0800:      	ldr	w9, [x19, #1548]
fffffe00079b0804:      	cmp	w9, w8
fffffe00079b0808:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b080c:      	ldrb	w8, [x22]
fffffe00079b0810:      	ldrb	w9, [x19, #198]
fffffe00079b0814:      	cmp	w8, w9
fffffe00079b0818:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b081c:      	ldrb	w8, [x23, #36]
fffffe00079b0820:      	ldrb	w9, [x19, #199]
fffffe00079b0824:      	cmp	w8, w9
fffffe00079b0828:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b082c:      	ldrb	w8, [x23, #37]
fffffe00079b0830:      	ldrb	w9, [x19, #200]
fffffe00079b0834:      	cmp	w8, w9
fffffe00079b0838:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b083c:      	ldrb	w8, [x23, #38]
fffffe00079b0840:      	ldrb	w9, [x19, #201]
fffffe00079b0844:      	cmp	w8, w9
fffffe00079b0848:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b084c:      	ldrb	w8, [x23, #39]
fffffe00079b0850:      	ldrb	w9, [x19, #202]
fffffe00079b0854:      	cmp	w8, w9
fffffe00079b0858:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b085c:      	ldrb	w8, [x23, #40]
fffffe00079b0860:      	ldrb	w9, [x19, #203]
fffffe00079b0864:      	cmp	w8, w9
fffffe00079b0868:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b086c:      	ldrb	w8, [x23, #41]
fffffe00079b0870:      	ldrb	w9, [x19, #206]
fffffe00079b0874:      	cmp	w8, w9
fffffe00079b0878:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b087c:      	ldrb	w9, [x23, #42]
fffffe00079b0880:      	ldrb	w10, [x19, #204]
fffffe00079b0884:      	cmp	w9, w10
fffffe00079b0888:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b088c:      	mov	w10, #1
fffffe00079b0890:      	mov	w11, #4
fffffe00079b0894:      	str	wzr, [x19, #1544]
fffffe00079b0898:      	mov	w0, wzr
fffffe00079b089c:      	strb	w8, [x23, #3875]
fffffe00079b08a0:      	str	w10, [x19, #1524]
fffffe00079b08a4:      	str	w11, [x19, #9720]
fffffe00079b08a8:      	strb	w9, [x23, #3874]
fffffe00079b08ac:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b08b0:      	cmp	x22, #24
fffffe00079b08b4:      	b.lo	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b08b8:      	and	w8, w17, #0x1f
fffffe00079b08bc:      	cmp	w8, #12
fffffe00079b08c0:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b08c4:      	cbnz	w13, 0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b08c8:      	ldrb	w8, [x21, #1]
fffffe00079b08cc:      	tst	w8, #0xffffffc7
fffffe00079b08d0:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b08d4:      	ldrb	w8, [x21, #22]
fffffe00079b08d8:      	tst	w8, #0xf
fffffe00079b08dc:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b08e0:      	ldrb	w8, [x21, #10]
fffffe00079b08e4:      	ldrb	w9, [x19, #198]
fffffe00079b08e8:      	cmp	w8, w9
fffffe00079b08ec:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b08f0:      	ldrb	w9, [x21, #11]
fffffe00079b08f4:      	ldrb	w10, [x19, #199]
fffffe00079b08f8:      	cmp	w9, w10
fffffe00079b08fc:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0900:      	ldrb	w10, [x21, #12]
fffffe00079b0904:      	ldrb	w11, [x19, #200]
fffffe00079b0908:      	cmp	w10, w11
fffffe00079b090c:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0910:      	ldrb	w11, [x21, #13]
fffffe00079b0914:      	ldrb	w12, [x19, #201]
fffffe00079b0918:      	cmp	w11, w12
fffffe00079b091c:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0920:      	ldrb	w12, [x21, #14]
fffffe00079b0924:      	ldrb	w13, [x19, #202]
fffffe00079b0928:      	cmp	w12, w13
fffffe00079b092c:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0930:      	ldrb	w13, [x21, #15]
fffffe00079b0934:      	ldrb	w14, [x19, #203]
fffffe00079b0938:      	cmp	w13, w14
fffffe00079b093c:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0940:      	ldrb	w14, [x21, #16]
fffffe00079b0944:      	cmp	w14, w8
fffffe00079b0948:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b094c:      	ldrb	w8, [x21, #17]
fffffe00079b0950:      	cmp	w8, w9
fffffe00079b0954:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0958:      	ldrb	w8, [x21, #18]
fffffe00079b095c:      	cmp	w8, w10
fffffe00079b0960:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0964:      	ldrb	w8, [x21, #19]
fffffe00079b0968:      	cmp	w8, w11
fffffe00079b096c:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0970:      	ldrb	w8, [x21, #20]
fffffe00079b0974:      	cmp	w8, w12
fffffe00079b0978:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b097c:      	ldrb	w8, [x21, #21]
fffffe00079b0980:      	cmp	w8, w13
fffffe00079b0984:      	b.ne	0xfffffe00079b0a30 <_decode+0x83c>
fffffe00079b0988:      	ldrb	w8, [x21]
fffffe00079b098c:      	mov	w9, #1610612736
fffffe00079b0990:      	add	w9, w9, w8, lsl #24
fffffe00079b0994:      	extr	w9, w8, w9, #28
fffffe00079b0998:      	and	w9, w9, #0xff
fffffe00079b099c:      	cmp	w9, #7
fffffe00079b09a0:      	b.hi	0xfffffe00079b1064 <_decode+0xe70>
fffffe00079b09a4:      	mov	w10, #1
fffffe00079b09a8:      	lsl	w9, w10, w9
fffffe00079b09ac:      	mov	w10, #135
fffffe00079b09b0:      	tst	w9, w10
fffffe00079b09b4:      	b.eq	0xfffffe00079b1064 <_decode+0xe70>
fffffe00079b09b8:      	ldrb	w8, [x21, #4]
fffffe00079b09bc:      	ldrb	w9, [x19, #192]
fffffe00079b09c0:      	cmp	w8, w9
fffffe00079b09c4:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b09c8:      	ldrb	w8, [x21, #5]
fffffe00079b09cc:      	ldrb	w9, [x19, #193]
fffffe00079b09d0:      	cmp	w8, w9
fffffe00079b09d4:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b09d8:      	ldrb	w8, [x21, #6]
fffffe00079b09dc:      	ldrb	w9, [x19, #194]
fffffe00079b09e0:      	cmp	w8, w9
fffffe00079b09e4:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b09e8:      	ldrb	w8, [x21, #7]
fffffe00079b09ec:      	ldrb	w9, [x19, #195]
fffffe00079b09f0:      	cmp	w8, w9
fffffe00079b09f4:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b09f8:      	ldrb	w8, [x21, #8]
fffffe00079b09fc:      	ldrb	w9, [x19, #196]
fffffe00079b0a00:      	cmp	w8, w9
fffffe00079b0a04:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0a08:      	ldrb	w8, [x21, #9]
fffffe00079b0a0c:      	ldrb	w9, [x19, #197]
fffffe00079b0a10:      	cmp	w8, w9
fffffe00079b0a14:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0a18:      	mov	w8, #6
fffffe00079b0a1c:      	mov	w0, wzr
fffffe00079b0a20:      	str	x21, [x19, #9760]
fffffe00079b0a24:      	str	x22, [x19, #9768]
fffffe00079b0a28:      	str	w8, [x19, #9720]
fffffe00079b0a2c:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0a30:      	ldr	x1, [x19, #152]
fffffe00079b0a34:      	mov	w8, #9776
fffffe00079b0a38:      	add	x0, x19, x8
fffffe00079b0a3c:      	mov	w2, #5
fffffe00079b0a40:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b0a44:      	cmp	w8, #17
fffffe00079b0a48:      	b.ne	0xfffffe00079b0a78 <_decode+0x884>
fffffe00079b0a4c:      	cmp	x21, #4
fffffe00079b0a50:      	b.lo	0xfffffe00079b0a78 <_decode+0x884>
fffffe00079b0a54:      	ldrb	w8, [x22]
fffffe00079b0a58:      	ldrb	w9, [x19, #204]
fffffe00079b0a5c:      	cmp	w8, w9
fffffe00079b0a60:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0a64:      	ldrb	w8, [x23, #36]
fffffe00079b0a68:      	cmp	w8, #1
fffffe00079b0a6c:      	b.hi	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0a70:      	str	w8, [x19, #1740]
fffffe00079b0a74:      	b	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0a78:      	cmp	w8, #19
fffffe00079b0a7c:      	b.ne	0xfffffe00079b0ad0 <_decode+0x8dc>
fffffe00079b0a80:      	cmp	x21, #4
fffffe00079b0a84:      	b.lo	0xfffffe00079b0ad0 <_decode+0x8dc>
fffffe00079b0a88:      	ldrb	w8, [x22]
fffffe00079b0a8c:      	ldrb	w9, [x19, #204]
fffffe00079b0a90:      	cmp	w8, w9
fffffe00079b0a94:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0a98:      	ldr	w8, [x19, #1524]
fffffe00079b0a9c:      	cbz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0aa0:      	mov	x0, x19
fffffe00079b0aa4:      	bl	0xfffffe00079ab324 <_nst_close_port>
fffffe00079b0aa8:      	mov	w8, #1
fffffe00079b0aac:      	mov	w9, #13
fffffe00079b0ab0:      	ldrb	w10, [x23, #36]
fffffe00079b0ab4:      	mov	w0, wzr
fffffe00079b0ab8:      	ldrb	w11, [x22]
fffffe00079b0abc:      	str	w8, [x19, #1708]
fffffe00079b0ac0:      	str	w9, [x19, #9720]
fffffe00079b0ac4:      	str	w10, [x19, #9724]
fffffe00079b0ac8:      	strb	w11, [x23, #3874]
fffffe00079b0acc:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0ad0:      	cmp	w8, #36
fffffe00079b0ad4:      	b.ne	0xfffffe00079b0d8c <_decode+0xb98>
fffffe00079b0ad8:      	cmp	x21, #8
fffffe00079b0adc:      	b.lo	0xfffffe00079b0d8c <_decode+0xb98>
fffffe00079b0ae0:      	ldr	w8, [x19, #1532]
fffffe00079b0ae4:      	cbz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0ae8:      	ldr	w8, [x19, #1528]
fffffe00079b0aec:      	cbnz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0af0:      	ldrb	w8, [x22]
fffffe00079b0af4:      	ldrb	w9, [x19, #204]
fffffe00079b0af8:      	cmp	w8, w9
fffffe00079b0afc:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0b00:      	ldrb	w9, [x23, #37]
fffffe00079b0b04:      	ldrb	w10, [x19, #198]
fffffe00079b0b08:      	cmp	w9, w10
fffffe00079b0b0c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0b10:      	ldrb	w9, [x23, #38]
fffffe00079b0b14:      	ldrb	w10, [x19, #199]
fffffe00079b0b18:      	cmp	w9, w10
fffffe00079b0b1c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0b20:      	ldrb	w9, [x23, #39]
fffffe00079b0b24:      	ldrb	w10, [x19, #200]
fffffe00079b0b28:      	cmp	w9, w10
fffffe00079b0b2c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0b30:      	ldrb	w9, [x23, #40]
fffffe00079b0b34:      	ldrb	w10, [x19, #201]
fffffe00079b0b38:      	cmp	w9, w10
fffffe00079b0b3c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0b40:      	ldrb	w9, [x23, #41]
fffffe00079b0b44:      	ldrb	w10, [x19, #202]
fffffe00079b0b48:      	cmp	w9, w10
fffffe00079b0b4c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0b50:      	ldrb	w9, [x23, #42]
fffffe00079b0b54:      	ldrb	w10, [x19, #203]
fffffe00079b0b58:      	cmp	w9, w10
fffffe00079b0b5c:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0b60:      	mov	w9, #1
fffffe00079b0b64:      	mov	w10, #5
fffffe00079b0b68:      	strb	w8, [x23, #3874]
fffffe00079b0b6c:      	mov	w8, #8
fffffe00079b0b70:      	mov	w0, wzr
fffffe00079b0b74:      	str	w9, [x19, #1528]
fffffe00079b0b78:      	str	w10, [x19, #9720]
fffffe00079b0b7c:      	str	x8, [x19, #9768]
fffffe00079b0b80:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0b84:      	and	w12, w17, #0x1f
fffffe00079b0b88:      	cmp	w12, #12
fffffe00079b0b8c:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0b90:      	cbnz	w13, 0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0b94:      	ldrb	w12, [x21, #1]
fffffe00079b0b98:      	mov	w14, #50944
fffffe00079b0b9c:      	ldrb	w13, [x21]
fffffe00079b0ba0:      	and	w12, w14, w12, lsl #8
fffffe00079b0ba4:      	orr	w12, w12, w13
fffffe00079b0ba8:      	cmp	w12, #520
fffffe00079b0bac:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0bb0:      	ldrb	w12, [x21, #22]
fffffe00079b0bb4:      	tst	w12, #0xf
fffffe00079b0bb8:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0bbc:      	ldrb	w12, [x19, #207]
fffffe00079b0bc0:      	cmp	w9, w12
fffffe00079b0bc4:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0bc8:      	ldrb	w9, [x21, #4]
fffffe00079b0bcc:      	ldrb	w12, [x19, #192]
fffffe00079b0bd0:      	cmp	w9, w12
fffffe00079b0bd4:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0bd8:      	ldrb	w12, [x21, #5]
fffffe00079b0bdc:      	ldrb	w13, [x19, #193]
fffffe00079b0be0:      	cmp	w12, w13
fffffe00079b0be4:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0be8:      	ldrb	w13, [x21, #6]
fffffe00079b0bec:      	ldrb	w14, [x19, #194]
fffffe00079b0bf0:      	cmp	w13, w14
fffffe00079b0bf4:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0bf8:      	ldrb	w14, [x21, #7]
fffffe00079b0bfc:      	ldrb	w15, [x19, #195]
fffffe00079b0c00:      	cmp	w14, w15
fffffe00079b0c04:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c08:      	ldrb	w15, [x21, #8]
fffffe00079b0c0c:      	ldrb	w16, [x19, #196]
fffffe00079b0c10:      	cmp	w15, w16
fffffe00079b0c14:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c18:      	ldrb	w16, [x21, #9]
fffffe00079b0c1c:      	ldrb	w17, [x19, #197]
fffffe00079b0c20:      	cmp	w16, w17
fffffe00079b0c24:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c28:      	ldrb	w17, [x21, #10]
fffffe00079b0c2c:      	ldrb	w0, [x19, #198]
fffffe00079b0c30:      	cmp	w17, w0
fffffe00079b0c34:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c38:      	ldrb	w1, [x21, #11]
fffffe00079b0c3c:      	ldrb	w0, [x19, #199]
fffffe00079b0c40:      	cmp	w1, w0
fffffe00079b0c44:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c48:      	ldrb	w2, [x21, #12]
fffffe00079b0c4c:      	ldrb	w0, [x19, #200]
fffffe00079b0c50:      	cmp	w2, w0
fffffe00079b0c54:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c58:      	ldrb	w3, [x21, #13]
fffffe00079b0c5c:      	ldrb	w0, [x19, #201]
fffffe00079b0c60:      	cmp	w3, w0
fffffe00079b0c64:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c68:      	ldrb	w4, [x21, #14]
fffffe00079b0c6c:      	ldrb	w0, [x19, #202]
fffffe00079b0c70:      	cmp	w4, w0
fffffe00079b0c74:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c78:      	ldrb	w5, [x21, #15]
fffffe00079b0c7c:      	ldrb	w0, [x19, #203]
fffffe00079b0c80:      	cmp	w5, w0
fffffe00079b0c84:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c88:      	ldrb	w0, [x21, #16]
fffffe00079b0c8c:      	cmp	w0, w17
fffffe00079b0c90:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0c94:      	ldrb	w0, [x21, #17]
fffffe00079b0c98:      	cmp	w0, w1
fffffe00079b0c9c:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0ca0:      	ldrb	w0, [x21, #18]
fffffe00079b0ca4:      	cmp	w0, w2
fffffe00079b0ca8:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0cac:      	ldrb	w0, [x21, #19]
fffffe00079b0cb0:      	cmp	w0, w3
fffffe00079b0cb4:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0cb8:      	ldrb	w0, [x21, #20]
fffffe00079b0cbc:      	cmp	w0, w4
fffffe00079b0cc0:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0cc4:      	ldrb	w0, [x21, #21]
fffffe00079b0cc8:      	cmp	w0, w5
fffffe00079b0ccc:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0cd0:      	ldrb	w0, [x21, #24]
fffffe00079b0cd4:      	cmp	w0, #170
fffffe00079b0cd8:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0cdc:      	ldrb	w0, [x21, #25]
fffffe00079b0ce0:      	cmp	w0, #170
fffffe00079b0ce4:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0ce8:      	ldrb	w0, [x21, #26]
fffffe00079b0cec:      	cmp	w0, #3
fffffe00079b0cf0:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0cf4:      	ldrb	w0, [x21, #27]
fffffe00079b0cf8:      	cbnz	w0, 0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0cfc:      	ldrb	w0, [x21, #28]
fffffe00079b0d00:      	cbnz	w0, 0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0d04:      	ldrb	w0, [x21, #29]
fffffe00079b0d08:      	cbnz	w0, 0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0d0c:      	ldrb	w0, [x21, #30]
fffffe00079b0d10:      	cmp	w0, #136
fffffe00079b0d14:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0d18:      	ldrb	w0, [x21, #31]
fffffe00079b0d1c:      	cmp	w0, #142
fffffe00079b0d20:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0d24:      	ldrh	w0, [x21, #34]
fffffe00079b0d28:      	rev16	w0, w0
fffffe00079b0d2c:      	cmp	x0, x11
fffffe00079b0d30:      	b.ne	0xfffffe00079b05b8 <_decode+0x3c4>
fffffe00079b0d34:      	strb	w9, [x21, #18]!
fffffe00079b0d38:      	strb	w12, [x21, #1]
fffffe00079b0d3c:      	mov	w11, #7
fffffe00079b0d40:      	strb	w13, [x21, #2]
fffffe00079b0d44:      	mov	w0, wzr
fffffe00079b0d48:      	strb	w14, [x21, #3]
fffffe00079b0d4c:      	strb	w15, [x21, #4]
fffffe00079b0d50:      	strb	w16, [x21, #5]
fffffe00079b0d54:      	strb	w17, [x21, #6]
fffffe00079b0d58:      	strb	w1, [x21, #7]
fffffe00079b0d5c:      	strb	w2, [x21, #8]
fffffe00079b0d60:      	strb	w3, [x21, #9]
fffffe00079b0d64:      	strb	w4, [x21, #10]
fffffe00079b0d68:      	strb	w5, [x21, #11]
fffffe00079b0d6c:      	ldr	w9, [x19, #1716]
fffffe00079b0d70:      	str	w11, [x19, #9720]
fffffe00079b0d74:      	str	x21, [x19, #9760]
fffffe00079b0d78:      	str	x10, [x19, #9768]
fffffe00079b0d7c:      	add	w9, w9, #1
fffffe00079b0d80:      	strb	w8, [x23, #3873]
fffffe00079b0d84:      	str	w9, [x19, #1716]
fffffe00079b0d88:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0d8c:      	mov	w0, #-1
fffffe00079b0d90:      	cmp	w8, #253
fffffe00079b0d94:      	b.eq	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0d98:      	cmp	w8, #240
fffffe00079b0d9c:      	b.eq	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0da0:      	cmp	w8, #237
fffffe00079b0da4:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0da8:      	ldrb	w8, [x23, #31]
fffffe00079b0dac:      	cmp	w8, #35
fffffe00079b0db0:      	b.ne	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b0db4:      	mov	w0, #-1
fffffe00079b0db8:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b0dbc:      	ldrb	w16, [x21, #12]
fffffe00079b0dc0:      	cmp	w16, #136
fffffe00079b0dc4:      	b.ne	0xfffffe00079b0e98 <_decode+0xca4>
fffffe00079b0dc8:      	ldrb	w16, [x21, #13]
fffffe00079b0dcc:      	cmp	w16, #142
fffffe00079b0dd0:      	b.ne	0xfffffe00079b0f84 <_decode+0xd90>
fffffe00079b0dd4:      	ldrb	w16, [x21, #6]
fffffe00079b0dd8:      	cmp	w16, w17
fffffe00079b0ddc:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0de0:      	ldrb	w16, [x21, #7]
fffffe00079b0de4:      	cmp	w16, w0
fffffe00079b0de8:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0dec:      	ldrb	w16, [x21, #8]
fffffe00079b0df0:      	cmp	w16, w1
fffffe00079b0df4:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0df8:      	ldrb	w16, [x21, #9]
fffffe00079b0dfc:      	cmp	w16, w2
fffffe00079b0e00:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e04:      	ldrb	w16, [x21, #10]
fffffe00079b0e08:      	cmp	w16, w3
fffffe00079b0e0c:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e10:      	ldrb	w16, [x21, #11]
fffffe00079b0e14:      	cmp	w16, w4
fffffe00079b0e18:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e1c:      	ldrb	w16, [x19, #192]
fffffe00079b0e20:      	cmp	w15, w16
fffffe00079b0e24:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e28:      	ldrb	w16, [x21, #1]
fffffe00079b0e2c:      	ldrb	w17, [x19, #193]
fffffe00079b0e30:      	cmp	w16, w17
fffffe00079b0e34:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e38:      	ldrb	w16, [x21, #2]
fffffe00079b0e3c:      	ldrb	w17, [x19, #194]
fffffe00079b0e40:      	cmp	w16, w17
fffffe00079b0e44:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e48:      	ldrb	w16, [x21, #3]
fffffe00079b0e4c:      	ldrb	w17, [x19, #195]
fffffe00079b0e50:      	cmp	w16, w17
fffffe00079b0e54:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e58:      	ldrb	w16, [x21, #4]
fffffe00079b0e5c:      	ldrb	w17, [x19, #196]
fffffe00079b0e60:      	cmp	w16, w17
fffffe00079b0e64:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e68:      	ldrb	w16, [x21, #5]
fffffe00079b0e6c:      	ldrb	w17, [x19, #197]
fffffe00079b0e70:      	cmp	w16, w17
fffffe00079b0e74:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e78:      	subs	x16, x22, #18
fffffe00079b0e7c:      	b.lo	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e80:      	ldrh	w17, [x21, #16]
fffffe00079b0e84:      	rev16	w17, w17
fffffe00079b0e88:      	cmp	x17, x16
fffffe00079b0e8c:      	b.ne	0xfffffe00079b0fa0 <_decode+0xdac>
fffffe00079b0e90:      	mov	w25, #1
fffffe00079b0e94:      	b	0xfffffe00079b0eac <_decode+0xcb8>
fffffe00079b0e98:      	ldr	w17, [x19, #1520]
fffffe00079b0e9c:      	cbz	w17, 0xfffffe00079b0f8c <_decode+0xd98>
fffffe00079b0ea0:      	cmp	w16, #6
fffffe00079b0ea4:      	b.lo	0xfffffe00079b0f8c <_decode+0xd98>
fffffe00079b0ea8:      	mov	w25, wzr
fffffe00079b0eac:      	ubfx	x26, x12, #5, #2
fffffe00079b0eb0:      	cbz	w13, 0xfffffe00079b0fb4 <_decode+0xdc0>
fffffe00079b0eb4:      	tbz	w14, #3, 0xfffffe00079b1000 <_decode+0xe0c>
fffffe00079b0eb8:      	cmp	w13, #4
fffffe00079b0ebc:      	b.ne	0xfffffe00079b1000 <_decode+0xe0c>
fffffe00079b0ec0:      	add	x12, x11, #16
fffffe00079b0ec4:      	cmp	x12, x10
fffffe00079b0ec8:      	b.hi	0xfffffe00079b1000 <_decode+0xe0c>
fffffe00079b0ecc:      	tbz	w8, #6, 0xfffffe00079b1000 <_decode+0xe0c>
fffffe00079b0ed0:      	mov	w8, #144
fffffe00079b0ed4:      	add	x10, x19, #632
fffffe00079b0ed8:      	tst	w15, #0x1
fffffe00079b0edc:      	madd	x8, x26, x8, x19
fffffe00079b0ee0:      	add	x8, x8, #920
fffffe00079b0ee4:      	csel	x27, x10, x8, eq
fffffe00079b0ee8:      	ldr	w8, [x27]
fffffe00079b0eec:      	cbz	w8, 0xfffffe00079b103c <_decode+0xe48>
fffffe00079b0ef0:      	ldrb	w8, [x27, #4]
fffffe00079b0ef4:      	cmp	w26, w8
fffffe00079b0ef8:      	b.ne	0xfffffe00079b103c <_decode+0xe48>
fffffe00079b0efc:      	ldrb	w8, [x19, #207]
fffffe00079b0f00:      	cmp	w9, w8
fffffe00079b0f04:      	b.ne	0xfffffe00079b1028 <_decode+0xe34>
fffffe00079b0f08:      	add	x0, x20, x11
fffffe00079b0f0c:      	bl	0xfffffe00079ace50 <_pn48>
fffffe00079b0f10:      	cbz	x0, 0xfffffe00079b1050 <_decode+0xe5c>
fffffe00079b0f14:      	ldr	x8, [sp, #16]
fffffe00079b0f18:      	sxtb	w9, w24
fffffe00079b0f1c:      	cmp	w9, #0
fffffe00079b0f20:      	mov	w9, #16
fffffe00079b0f24:      	and	x8, x8, #0xf
fffffe00079b0f28:      	csel	x8, x9, x8, ge
fffffe00079b0f2c:      	add	x8, x27, x8, lsl #3
fffffe00079b0f30:      	ldr	x9, [x8, #8]!
fffffe00079b0f34:      	cmp	x0, x9
fffffe00079b0f38:      	b.ls	0xfffffe00079b1050 <_decode+0xe5c>
fffffe00079b0f3c:      	str	x0, [x8]
fffffe00079b0f40:      	ldrb	w8, [x21]
fffffe00079b0f44:      	tbz	w8, #0, 0xfffffe00079b0fd4 <_decode+0xde0>
fffffe00079b0f48:      	ldrb	w8, [x19, #780]
fffffe00079b0f4c:      	cmp	w26, w8
fffffe00079b0f50:      	b.ne	0xfffffe00079b0fd4 <_decode+0xde0>
fffffe00079b0f54:      	ldp	q0, q1, [x27, #96]
fffffe00079b0f58:      	add	x8, x19, #776
fffffe00079b0f5c:      	stp	q0, q1, [x8, #96]
fffffe00079b0f60:      	ldr	q2, [x27, #128]
fffffe00079b0f64:      	ldp	q3, q0, [x27, #32]
fffffe00079b0f68:      	str	q2, [x8, #128]
fffffe00079b0f6c:      	ldp	q2, q1, [x27, #64]
fffffe00079b0f70:      	stp	q3, q0, [x8, #32]
fffffe00079b0f74:      	ldp	q3, q0, [x27]
fffffe00079b0f78:      	stp	q2, q1, [x8, #64]
fffffe00079b0f7c:      	stp	q3, q0, [x8]
fffffe00079b0f80:      	b	0xfffffe00079b0fd4 <_decode+0xde0>
fffffe00079b0f84:      	ldr	w16, [x19, #1520]
fffffe00079b0f88:      	cbnz	w16, 0xfffffe00079b0ea8 <_decode+0xcb4>
fffffe00079b0f8c:      	ldr	x1, [x19, #152]
fffffe00079b0f90:      	mov	w8, #9776
fffffe00079b0f94:      	add	x0, x19, x8
fffffe00079b0f98:      	mov	w2, #9
fffffe00079b0f9c:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b0fa0:      	ldr	x1, [x19, #152]
fffffe00079b0fa4:      	mov	w8, #9776
fffffe00079b0fa8:      	add	x0, x19, x8
fffffe00079b0fac:      	mov	w2, #8
fffffe00079b0fb0:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b0fb4:      	tst	w8, #0x40
fffffe00079b0fb8:      	cset	w8, eq
fffffe00079b0fbc:      	and	w8, w25, w8
fffffe00079b0fc0:      	cmp	w8, #1
fffffe00079b0fc4:      	b.ne	0xfffffe00079b1014 <_decode+0xe20>
fffffe00079b0fc8:      	ldrb	w8, [x19, #207]
fffffe00079b0fcc:      	cmp	w9, w8
fffffe00079b0fd0:      	b.ne	0xfffffe00079b1014 <_decode+0xe20>
fffffe00079b0fd4:      	cmp	w25, #0
fffffe00079b0fd8:      	mov	w8, #7
fffffe00079b0fdc:      	mov	w0, wzr
fffffe00079b0fe0:      	cinc	w9, w8, eq
fffffe00079b0fe4:      	ldr	w8, [sp, #28]
fffffe00079b0fe8:      	str	x21, [x19, #9760]
fffffe00079b0fec:      	str	x22, [x19, #9768]
fffffe00079b0ff0:      	str	w9, [x19, #9720]
fffffe00079b0ff4:      	strb	w8, [x23, #3873]
fffffe00079b0ff8:      	strb	w26, [x23, #3879]
fffffe00079b0ffc:      	b	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b1000:      	ldr	x1, [x19, #152]
fffffe00079b1004:      	mov	w8, #9776
fffffe00079b1008:      	add	x0, x19, x8
fffffe00079b100c:      	mov	w2, #10
fffffe00079b1010:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b1014:      	ldr	x1, [x19, #152]
fffffe00079b1018:      	mov	w8, #9776
fffffe00079b101c:      	add	x0, x19, x8
fffffe00079b1020:      	mov	w2, #13
fffffe00079b1024:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b1028:      	tbz	w15, #0, 0xfffffe00079b103c <_decode+0xe48>
fffffe00079b102c:      	add	x8, x19, x26
fffffe00079b1030:      	ldrb	w8, [x8, #1496]
fffffe00079b1034:      	cmp	w9, w8
fffffe00079b1038:      	b.eq	0xfffffe00079b0f08 <_decode+0xd14>
fffffe00079b103c:      	ldr	x1, [x19, #152]
fffffe00079b1040:      	mov	w8, #9776
fffffe00079b1044:      	add	x0, x19, x8
fffffe00079b1048:      	mov	w2, #11
fffffe00079b104c:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b1050:      	ldr	x1, [x19, #152]
fffffe00079b1054:      	mov	w8, #9776
fffffe00079b1058:      	add	x0, x19, x8
fffffe00079b105c:      	mov	w2, #12
fffffe00079b1060:      	b	0xfffffe00079b024c <_decode+0x58>
fffffe00079b1064:      	mov	w0, wzr
fffffe00079b1068:      	cmp	w8, #128
fffffe00079b106c:      	b.ne	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b1070:      	cmp	x22, #36
fffffe00079b1074:      	b.lo	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b1078:      	ldr	w8, [x19, #1524]
fffffe00079b107c:      	cbz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b1080:      	ldr	w8, [x19, #1724]
fffffe00079b1084:      	cbnz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b1088:      	ldr	w8, [x19, #1728]
fffffe00079b108c:      	cbnz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b1090:      	ldr	w8, [x19, #24]
fffffe00079b1094:      	cbnz	w8, 0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b1098:      	ldr	w8, [x19, #1708]
fffffe00079b109c:      	mov	w0, wzr
fffffe00079b10a0:      	cbnz	w8, 0xfffffe00079b0284 <_decode+0x90>
fffffe00079b10a4:      	cmp	x22, #37
fffffe00079b10a8:      	b.lo	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b10ac:      	mov	w8, #36
fffffe00079b10b0:      	mov	w9, #1
fffffe00079b10b4:      	b	0xfffffe00079b10cc <_decode+0xed8>
fffffe00079b10b8:      	add	x8, x8, x10
fffffe00079b10bc:      	mov	w0, wzr
fffffe00079b10c0:      	add	x8, x8, #2
fffffe00079b10c4:      	cmp	x22, x8
fffffe00079b10c8:      	b.ls	0xfffffe00079b0284 <_decode+0x90>
fffffe00079b10cc:      	sub	x10, x22, x8
fffffe00079b10d0:      	subs	x12, x10, #2
fffffe00079b10d4:      	b.lo	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b10d8:      	add	x11, x8, #1
fffffe00079b10dc:      	ldrb	w10, [x21, x11]
fffffe00079b10e0:      	cmp	x12, x10
fffffe00079b10e4:      	b.lo	0xfffffe00079b0280 <_decode+0x8c>
fffffe00079b10e8:      	ldrb	w12, [x21, x8]
fffffe00079b10ec:      	cmp	w12, #5
fffffe00079b10f0:      	b.ne	0xfffffe00079b10b8 <_decode+0xec4>
fffffe00079b10f4:      	cmp	w10, #4
fffffe00079b10f8:      	b.lo	0xfffffe00079b10b8 <_decode+0xec4>
fffffe00079b10fc:      	add	x12, x8, x21
fffffe00079b1100:      	ldrb	w12, [x12, #3]
fffffe00079b1104:      	cbz	w12, 0xfffffe00079b10b8 <_decode+0xec4>
fffffe00079b1108:      	str	w12, [x19, #1736]
fffffe00079b110c:      	str	w9, [x19, #1732]
fffffe00079b1110:      	ldrb	w10, [x21, x11]
fffffe00079b1114:      	b	0xfffffe00079b10b8 <_decode+0xec4>
