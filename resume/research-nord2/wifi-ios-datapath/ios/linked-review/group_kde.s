
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b82b8 <_group_kde>:
fffffe00079b82b8:      	cbz	w2, 0xfffffe00079b86ac <_group_kde+0x3f4>
fffffe00079b82bc:      	adrp	x12, 0xfffffe000700b000 <_nwp_ap_rsn+0x10c>
fffffe00079b82c0:      	adrp	x13, 0xfffffe000700b000 <_nwp_ap_rsn+0x110>
fffffe00079b82c4:      	adrp	x14, 0xfffffe000700b000 <_nwp_ap_rsn+0x114>
fffffe00079b82c8:      	adrp	x15, 0xfffffe000700b000 <_nwp_ap_rsn+0x118>
fffffe00079b82cc:      	mov	w10, wzr
fffffe00079b82d0:      	mov	w11, wzr
fffffe00079b82d4:      	mov	w9, wzr
fffffe00079b82d8:      	mov	w8, wzr
fffffe00079b82dc:      	ldr	q0, [x12, #3072]
fffffe00079b82e0:      	add	x12, x0, #1704
fffffe00079b82e4:      	ldr	q1, [x13, #3024]
fffffe00079b82e8:      	add	x13, x0, #1434
fffffe00079b82ec:      	ldr	q2, [x14, #3040]
fffffe00079b82f0:      	ldr	q3, [x15, #3056]
fffffe00079b82f4:      	b	0xfffffe00079b8304 <_group_kde+0x4c>
fffffe00079b82f8:      	add	w8, w15, w14
fffffe00079b82fc:      	cmp	w8, w2
fffffe00079b8300:      	b.hs	0xfffffe00079b86b0 <_group_kde+0x3f8>
fffffe00079b8304:      	mov	w14, w8
fffffe00079b8308:      	add	x16, x1, x14
fffffe00079b830c:      	ldrb	w17, [x16]
fffffe00079b8310:      	cmp	w17, #221
fffffe00079b8314:      	b.ne	0xfffffe00079b832c <_group_kde+0x74>
fffffe00079b8318:      	add	x8, x14, #1
fffffe00079b831c:      	cmp	w8, w2
fffffe00079b8320:      	b.eq	0xfffffe00079b86b0 <_group_kde+0x3f8>
fffffe00079b8324:      	ldrb	w15, [x1, x8]
fffffe00079b8328:      	cbz	w15, 0xfffffe00079b86c0 <_group_kde+0x408>
fffffe00079b832c:      	sub	w8, w2, w14
fffffe00079b8330:      	subs	w15, w8, #2
fffffe00079b8334:      	b.lo	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8338:      	add	x8, x14, x1
fffffe00079b833c:      	ldrb	w8, [x8, #1]
fffffe00079b8340:      	cmp	w15, w8
fffffe00079b8344:      	b.lo	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8348:      	add	x15, x8, #2
fffffe00079b834c:      	cmp	w17, #244
fffffe00079b8350:      	b.eq	0xfffffe00079b8380 <_group_kde+0xc8>
fffffe00079b8354:      	cmp	w17, #48
fffffe00079b8358:      	b.ne	0xfffffe00079b83a4 <_group_kde+0xec>
fffffe00079b835c:      	cbnz	w11, 0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8360:      	ldr	w17, [x0, #1700]
fffffe00079b8364:      	cmp	w15, w17
fffffe00079b8368:      	b.ne	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b836c:      	cmp	w8, #6
fffffe00079b8370:      	b.hs	0xfffffe00079b8448 <_group_kde+0x190>
fffffe00079b8374:      	mov	x17, xzr
fffffe00079b8378:      	mov	w16, wzr
fffffe00079b837c:      	b	0xfffffe00079b8558 <_group_kde+0x2a0>
fffffe00079b8380:      	cbnz	w10, 0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8384:      	ldr	w17, [x0, #1964]
fffffe00079b8388:      	cmp	w15, w17
fffffe00079b838c:      	b.ne	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8390:      	cmp	w8, #6
fffffe00079b8394:      	b.hs	0xfffffe00079b845c <_group_kde+0x1a4>
fffffe00079b8398:      	mov	x17, xzr
fffffe00079b839c:      	mov	w16, wzr
fffffe00079b83a0:      	b	0xfffffe00079b8674 <_group_kde+0x3bc>
fffffe00079b83a4:      	cmp	w17, #221
fffffe00079b83a8:      	b.ne	0xfffffe00079b83d8 <_group_kde+0x120>
fffffe00079b83ac:      	cmp	w8, #4
fffffe00079b83b0:      	b.lo	0xfffffe00079b83d8 <_group_kde+0x120>
fffffe00079b83b4:      	ldrb	w8, [x16, #2]
fffffe00079b83b8:      	cbnz	w8, 0xfffffe00079b82f8 <_group_kde+0x40>
fffffe00079b83bc:      	ldrb	w8, [x16, #3]
fffffe00079b83c0:      	cmp	w8, #15
fffffe00079b83c4:      	b.ne	0xfffffe00079b82f8 <_group_kde+0x40>
fffffe00079b83c8:      	ldrb	w8, [x16, #4]
fffffe00079b83cc:      	cmp	w8, #172
fffffe00079b83d0:      	b.ne	0xfffffe00079b82f8 <_group_kde+0x40>
fffffe00079b83d4:      	b	0xfffffe00079b83e0 <_group_kde+0x128>
fffffe00079b83d8:      	cmp	w17, #221
fffffe00079b83dc:      	b.ne	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b83e0:      	mov	w8, #-1
fffffe00079b83e4:      	cbnz	w9, 0xfffffe00079b86b8 <_group_kde+0x400>
fffffe00079b83e8:      	cmp	w15, #24
fffffe00079b83ec:      	b.ne	0xfffffe00079b86b8 <_group_kde+0x400>
fffffe00079b83f0:      	ldrb	w8, [x16, #2]
fffffe00079b83f4:      	cbnz	w8, 0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b83f8:      	ldrb	w8, [x16, #3]
fffffe00079b83fc:      	cmp	w8, #15
fffffe00079b8400:      	b.ne	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8404:      	ldrb	w8, [x16, #4]
fffffe00079b8408:      	cmp	w8, #172
fffffe00079b840c:      	b.ne	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8410:      	ldrb	w8, [x16, #5]
fffffe00079b8414:      	cmp	w8, #1
fffffe00079b8418:      	b.ne	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b841c:      	ldrb	w8, [x16, #6]
fffffe00079b8420:      	cmp	w8, #7
fffffe00079b8424:      	b.hi	0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8428:      	ldrb	w9, [x16, #7]
fffffe00079b842c:      	cbnz	w9, 0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8430:      	and	w8, w8, #0x3
fffffe00079b8434:      	mov	w9, #1
fffffe00079b8438:      	str	w8, [x4]
fffffe00079b843c:      	ldur	q4, [x16, #8]
fffffe00079b8440:      	str	q4, [x3]
fffffe00079b8444:      	b	0xfffffe00079b82f8 <_group_kde+0x40>
fffffe00079b8448:      	cmp	w8, #14
fffffe00079b844c:      	b.hs	0xfffffe00079b8474 <_group_kde+0x1bc>
fffffe00079b8450:      	mov	w16, wzr
fffffe00079b8454:      	mov	x17, xzr
fffffe00079b8458:      	b	0xfffffe00079b84f0 <_group_kde+0x238>
fffffe00079b845c:      	mov	w5, w15
fffffe00079b8460:      	cmp	w8, #14
fffffe00079b8464:      	b.hs	0xfffffe00079b8590 <_group_kde+0x2d8>
fffffe00079b8468:      	mov	w16, wzr
fffffe00079b846c:      	mov	x17, xzr
fffffe00079b8470:      	b	0xfffffe00079b860c <_group_kde+0x354>
fffffe00079b8474:      	and	x17, x15, #0x1f0
fffffe00079b8478:      	mov	x6, x13
fffffe00079b847c:      	movi.2d	v4, #0000000000000000
fffffe00079b8480:      	mov	x5, x17
fffffe00079b8484:      	movi.2d	v5, #0000000000000000
fffffe00079b8488:      	movi.2d	v6, #0000000000000000
fffffe00079b848c:      	movi.2d	v7, #0000000000000000
fffffe00079b8490:      	ldr	q16, [x16], #16
fffffe00079b8494:      	ldr	q17, [x6], #16
fffffe00079b8498:      	subs	x5, x5, #16
fffffe00079b849c:      	eor.16b	v16, v17, v16
fffffe00079b84a0:      	tbl.16b	v17, { v16 }, v3
fffffe00079b84a4:      	tbl.16b	v18, { v16 }, v2
fffffe00079b84a8:      	tbl.16b	v19, { v16 }, v1
fffffe00079b84ac:      	tbl.16b	v16, { v16 }, v0
fffffe00079b84b0:      	orr.16b	v7, v7, v17
fffffe00079b84b4:      	orr.16b	v6, v6, v18
fffffe00079b84b8:      	orr.16b	v5, v5, v19
fffffe00079b84bc:      	orr.16b	v4, v4, v16
fffffe00079b84c0:      	b.ne	0xfffffe00079b8490 <_group_kde+0x1d8>
fffffe00079b84c4:      	orr.16b	v4, v4, v6
fffffe00079b84c8:      	cmp	x17, x15
fffffe00079b84cc:      	orr.16b	v5, v5, v7
fffffe00079b84d0:      	orr.16b	v4, v4, v5
fffffe00079b84d4:      	ext.16b	v5, v4, v4, #8
fffffe00079b84d8:      	orr.8b	v4, v4, v5
fffffe00079b84dc:      	mov.s	w16, v4[1]
fffffe00079b84e0:      	fmov	w5, s4
fffffe00079b84e4:      	orr	w16, w5, w16
fffffe00079b84e8:      	b.eq	0xfffffe00079b8584 <_group_kde+0x2cc>
fffffe00079b84ec:      	tbz	w15, #3, 0xfffffe00079b8558 <_group_kde+0x2a0>
fffffe00079b84f0:      	movi.2d	v4, #0000000000000000
fffffe00079b84f4:      	mov	x6, x17
fffffe00079b84f8:      	and	x17, x15, #0x1f8
fffffe00079b84fc:      	add	x5, x6, x14
fffffe00079b8500:      	movi.2d	v5, #0000000000000000
fffffe00079b8504:      	mov.s	v4[0], w16
fffffe00079b8508:      	add	x16, x1, x5
fffffe00079b850c:      	add	x5, x13, x6
fffffe00079b8510:      	sub	x6, x6, x17
fffffe00079b8514:      	ldr	d6, [x16], #8
fffffe00079b8518:      	ldr	d7, [x5], #8
fffffe00079b851c:      	adds	x6, x6, #8
fffffe00079b8520:      	eor.8b	v6, v7, v6
fffffe00079b8524:      	tbl.16b	v7, { v6 }, v1
fffffe00079b8528:      	tbl.16b	v6, { v6 }, v0
fffffe00079b852c:      	orr.16b	v5, v5, v7
fffffe00079b8530:      	orr.16b	v4, v4, v6
fffffe00079b8534:      	b.ne	0xfffffe00079b8514 <_group_kde+0x25c>
fffffe00079b8538:      	orr.16b	v4, v4, v5
fffffe00079b853c:      	cmp	x17, x15
fffffe00079b8540:      	ext.16b	v5, v4, v4, #8
fffffe00079b8544:      	orr.8b	v4, v4, v5
fffffe00079b8548:      	mov.s	w16, v4[1]
fffffe00079b854c:      	fmov	w5, s4
fffffe00079b8550:      	orr	w16, w5, w16
fffffe00079b8554:      	b.eq	0xfffffe00079b8584 <_group_kde+0x2cc>
fffffe00079b8558:      	sub	x8, x8, x17
fffffe00079b855c:      	add	x5, x17, x14
fffffe00079b8560:      	add	x8, x8, #2
fffffe00079b8564:      	add	x17, x13, x17
fffffe00079b8568:      	add	x5, x1, x5
fffffe00079b856c:      	ldrb	w6, [x5], #1
fffffe00079b8570:      	ldrb	w7, [x17], #1
fffffe00079b8574:      	subs	x8, x8, #1
fffffe00079b8578:      	eor	w6, w7, w6
fffffe00079b857c:      	orr	w16, w16, w6
fffffe00079b8580:      	b.ne	0xfffffe00079b856c <_group_kde+0x2b4>
fffffe00079b8584:      	cbnz	w16, 0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b8588:      	add	w11, w11, #1
fffffe00079b858c:      	b	0xfffffe00079b82f8 <_group_kde+0x40>
fffffe00079b8590:      	and	x17, x5, #0x1f0
fffffe00079b8594:      	mov	x7, x12
fffffe00079b8598:      	movi.2d	v4, #0000000000000000
fffffe00079b859c:      	mov	x6, x17
fffffe00079b85a0:      	movi.2d	v5, #0000000000000000
fffffe00079b85a4:      	movi.2d	v6, #0000000000000000
fffffe00079b85a8:      	movi.2d	v7, #0000000000000000
fffffe00079b85ac:      	ldr	q16, [x16], #16
fffffe00079b85b0:      	ldr	q17, [x7], #16
fffffe00079b85b4:      	subs	x6, x6, #16
fffffe00079b85b8:      	eor.16b	v16, v17, v16
fffffe00079b85bc:      	tbl.16b	v17, { v16 }, v3
fffffe00079b85c0:      	tbl.16b	v18, { v16 }, v2
fffffe00079b85c4:      	tbl.16b	v19, { v16 }, v1
fffffe00079b85c8:      	tbl.16b	v16, { v16 }, v0
fffffe00079b85cc:      	orr.16b	v7, v7, v17
fffffe00079b85d0:      	orr.16b	v6, v6, v18
fffffe00079b85d4:      	orr.16b	v5, v5, v19
fffffe00079b85d8:      	orr.16b	v4, v4, v16
fffffe00079b85dc:      	b.ne	0xfffffe00079b85ac <_group_kde+0x2f4>
fffffe00079b85e0:      	orr.16b	v4, v4, v6
fffffe00079b85e4:      	cmp	x17, x5
fffffe00079b85e8:      	orr.16b	v5, v5, v7
fffffe00079b85ec:      	orr.16b	v4, v4, v5
fffffe00079b85f0:      	ext.16b	v5, v4, v4, #8
fffffe00079b85f4:      	orr.8b	v4, v4, v5
fffffe00079b85f8:      	mov.s	w16, v4[1]
fffffe00079b85fc:      	fmov	w6, s4
fffffe00079b8600:      	orr	w16, w6, w16
fffffe00079b8604:      	b.eq	0xfffffe00079b86a0 <_group_kde+0x3e8>
fffffe00079b8608:      	tbz	w5, #3, 0xfffffe00079b8674 <_group_kde+0x3bc>
fffffe00079b860c:      	movi.2d	v4, #0000000000000000
fffffe00079b8610:      	mov	x7, x17
fffffe00079b8614:      	and	x17, x5, #0x1f8
fffffe00079b8618:      	add	x6, x7, x14
fffffe00079b861c:      	movi.2d	v5, #0000000000000000
fffffe00079b8620:      	mov.s	v4[0], w16
fffffe00079b8624:      	add	x16, x1, x6
fffffe00079b8628:      	add	x6, x12, x7
fffffe00079b862c:      	sub	x7, x7, x17
fffffe00079b8630:      	ldr	d6, [x16], #8
fffffe00079b8634:      	ldr	d7, [x6], #8
fffffe00079b8638:      	adds	x7, x7, #8
fffffe00079b863c:      	eor.8b	v6, v7, v6
fffffe00079b8640:      	tbl.16b	v7, { v6 }, v1
fffffe00079b8644:      	tbl.16b	v6, { v6 }, v0
fffffe00079b8648:      	orr.16b	v5, v5, v7
fffffe00079b864c:      	orr.16b	v4, v4, v6
fffffe00079b8650:      	b.ne	0xfffffe00079b8630 <_group_kde+0x378>
fffffe00079b8654:      	orr.16b	v4, v4, v5
fffffe00079b8658:      	cmp	x17, x5
fffffe00079b865c:      	ext.16b	v5, v4, v4, #8
fffffe00079b8660:      	orr.8b	v4, v4, v5
fffffe00079b8664:      	mov.s	w16, v4[1]
fffffe00079b8668:      	fmov	w6, s4
fffffe00079b866c:      	orr	w16, w6, w16
fffffe00079b8670:      	b.eq	0xfffffe00079b86a0 <_group_kde+0x3e8>
fffffe00079b8674:      	sub	x8, x8, x17
fffffe00079b8678:      	add	x5, x17, x14
fffffe00079b867c:      	add	x8, x8, #2
fffffe00079b8680:      	add	x17, x12, x17
fffffe00079b8684:      	add	x5, x1, x5
fffffe00079b8688:      	ldrb	w6, [x5], #1
fffffe00079b868c:      	ldrb	w7, [x17], #1
fffffe00079b8690:      	subs	x8, x8, #1
fffffe00079b8694:      	eor	w6, w7, w6
fffffe00079b8698:      	orr	w16, w16, w6
fffffe00079b869c:      	b.ne	0xfffffe00079b8688 <_group_kde+0x3d0>
fffffe00079b86a0:      	cbnz	w16, 0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b86a4:      	add	w10, w10, #1
fffffe00079b86a8:      	b	0xfffffe00079b82f8 <_group_kde+0x40>
fffffe00079b86ac:      	mov	w9, wzr
fffffe00079b86b0:      	cmp	w9, #1
fffffe00079b86b4:      	csetm	w8, ne
fffffe00079b86b8:      	mov	w0, w8
fffffe00079b86bc:      	ret
fffffe00079b86c0:      	cmp	w8, w2
fffffe00079b86c4:      	b.hs	0xfffffe00079b86b0 <_group_kde+0x3f8>
fffffe00079b86c8:      	mov	w8, w8
fffffe00079b86cc:      	mvn	w10, w14
fffffe00079b86d0:      	add	x8, x1, x8
fffffe00079b86d4:      	add	w10, w10, w2
fffffe00079b86d8:      	ldrb	w11, [x8]
fffffe00079b86dc:      	cbnz	w11, 0xfffffe00079b86f0 <_group_kde+0x438>
fffffe00079b86e0:      	add	x8, x8, #1
fffffe00079b86e4:      	subs	w10, w10, #1
fffffe00079b86e8:      	b.ne	0xfffffe00079b86d8 <_group_kde+0x420>
fffffe00079b86ec:      	b	0xfffffe00079b86b0 <_group_kde+0x3f8>
fffffe00079b86f0:      	mov	w0, #-1
fffffe00079b86f4:      	ret
