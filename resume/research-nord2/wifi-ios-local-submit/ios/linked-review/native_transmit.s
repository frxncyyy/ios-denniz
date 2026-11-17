
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a8258 <_transmit>:
fffffe00079a8258:      	sub	sp, sp, #176
fffffe00079a825c:      	stp	x28, x27, [sp, #80]
fffffe00079a8260:      	stp	x26, x25, [sp, #96]
fffffe00079a8264:      	stp	x24, x23, [sp, #112]
fffffe00079a8268:      	stp	x22, x21, [sp, #128]
fffffe00079a826c:      	stp	x20, x19, [sp, #144]
fffffe00079a8270:      	stp	x29, x30, [sp, #160]
fffffe00079a8274:      	add	x29, sp, #160
fffffe00079a8278:      	ldr	w8, [x0, #56]
fffffe00079a827c:      	cbz	w8, 0xfffffe00079a8458 <_transmit+0x200>
fffffe00079a8280:      	ldr	w8, [x0, #128]
fffffe00079a8284:      	mov	x20, x4
fffffe00079a8288:      	mov	x21, x3
fffffe00079a828c:      	mov	x19, x0
fffffe00079a8290:      	mov	x22, x2
fffffe00079a8294:      	mov	x23, x1
fffffe00079a8298:      	cmp	w8, #3
fffffe00079a829c:      	b.ne	0xfffffe00079a82b0 <_transmit+0x58>
fffffe00079a82a0:      	ldr	w9, [x19, #1140]
fffffe00079a82a4:      	cbz	w9, 0xfffffe00079a82b0 <_transmit+0x58>
fffffe00079a82a8:      	ldr	w9, [x19, #132]
fffffe00079a82ac:      	cbz	w9, 0xfffffe00079a8458 <_transmit+0x200>
fffffe00079a82b0:      	ldr	w10, [x19, #24]
fffffe00079a82b4:      	ldr	w9, [x19, #132]
fffffe00079a82b8:      	cbz	w10, 0xfffffe00079a82e4 <_transmit+0x8c>
fffffe00079a82bc:      	cbnz	w9, 0xfffffe00079a82e4 <_transmit+0x8c>
fffffe00079a82c0:      	mov	w9, w8
fffffe00079a82c4:      	cbz	w8, 0xfffffe00079a82e4 <_transmit+0x8c>
fffffe00079a82c8:      	cmp	w8, #1
fffffe00079a82cc:      	b.ne	0xfffffe00079a83a0 <_transmit+0x148>
fffffe00079a82d0:      	add	x8, x19, #1193
fffffe00079a82d4:      	ldrh	w8, [x8]
fffffe00079a82d8:      	cmp	w8, #192
fffffe00079a82dc:      	b.ne	0xfffffe00079a83a0 <_transmit+0x148>
fffffe00079a82e0:      	mov	w9, wzr
fffffe00079a82e4:      	ldr	x8, [x19]
fffffe00079a82e8:      	str	wzr, [sp, #12]
fffffe00079a82ec:      	ldr	w10, [x19, #84]
fffffe00079a82f0:      	ldr	x8, [x8, #2832]
fffffe00079a82f4:      	lsl	w10, w10, #4
fffffe00079a82f8:      	add	x24, x8, x10
fffffe00079a82fc:      	cbz	w9, 0xfffffe00079a834c <_transmit+0xf4>
fffffe00079a8300:      	mov	w8, #4
fffffe00079a8304:      	mov	x0, x19
fffffe00079a8308:      	mov	x1, x23
fffffe00079a830c:      	mov	x2, x22
fffffe00079a8310:      	str	w8, [x19, #40]
fffffe00079a8314:      	bl	0xfffffe00079a806c <_before>
fffffe00079a8318:      	cbnz	w0, 0xfffffe00079a8340 <_transmit+0xe8>
fffffe00079a831c:      	ldr	x8, [x23]
fffffe00079a8320:      	mov	w9, #21528
fffffe00079a8324:      	movk	w9, #6146, lsl #16
fffffe00079a8328:      	add	x2, sp, #12
fffffe00079a832c:      	orr	w1, w9, #0x4
fffffe00079a8330:      	mov	x0, x22
fffffe00079a8334:      	ldr	x8, [x8, #8]
fffffe00079a8338:      	blr	x8
fffffe00079a833c:      	cbz	w0, 0xfffffe00079a8448 <_transmit+0x1f0>
fffffe00079a8340:      	mov	x0, x19
fffffe00079a8344:      	mov	w1, #5
fffffe00079a8348:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a834c:      	mov	x0, x19
fffffe00079a8350:      	mov	x1, x23
fffffe00079a8354:      	mov	x2, x22
fffffe00079a8358:      	bl	0xfffffe00079a806c <_before>
fffffe00079a835c:      	cbnz	w0, 0xfffffe00079a8380 <_transmit+0x128>
fffffe00079a8360:      	ldr	x8, [x23]
fffffe00079a8364:      	mov	w1, #240
fffffe00079a8368:      	add	x2, sp, #12
fffffe00079a836c:      	mov	x0, x22
fffffe00079a8370:      	movk	w1, #6150, lsl #16
fffffe00079a8374:      	ldr	x8, [x8, #8]
fffffe00079a8378:      	blr	x8
fffffe00079a837c:      	cbz	w0, 0xfffffe00079a847c <_transmit+0x224>
fffffe00079a8380:      	mov	x0, x19
fffffe00079a8384:      	mov	w1, #5
fffffe00079a8388:      	bl	0xfffffe00079aa178 <_fail>
fffffe00079a838c:      	mov	x0, x19
fffffe00079a8390:      	mov	w1, #12
fffffe00079a8394:      	bl	0xfffffe00079aa178 <_fail>
fffffe00079a8398:      	mov	w0, #-1
fffffe00079a839c:      	b	0xfffffe00079a845c <_transmit+0x204>
fffffe00079a83a0:      	mov	w8, #1161
fffffe00079a83a4:      	mov	w9, #5257
fffffe00079a83a8:      	strb	wzr, [x19, x8]
fffffe00079a83ac:      	add	x8, x8, #1
fffffe00079a83b0:      	cmp	x8, x9
fffffe00079a83b4:      	b.ne	0xfffffe00079a83a8 <_transmit+0x150>
fffffe00079a83b8:      	ldr	w8, [x19, #128]
fffffe00079a83bc:      	mov	w9, #2
fffffe00079a83c0:      	mov	w10, #16
fffffe00079a83c4:      	str	wzr, [x19, #56]
fffffe00079a83c8:      	movi.2d	v0, #0000000000000000
fffffe00079a83cc:      	stp	xzr, xzr, [sp, #56]
fffffe00079a83d0:      	cmp	w8, #3
fffffe00079a83d4:      	csel	w9, w10, w9, eq
fffffe00079a83d8:      	cmp	w8, #2
fffffe00079a83dc:      	mov	w8, #15
fffffe00079a83e0:      	ldr	x10, [x19, #160]
fffffe00079a83e4:      	csel	w9, w8, w9, eq
fffffe00079a83e8:      	mov	w8, #1
fffffe00079a83ec:      	stur	q0, [sp, #24]
fffffe00079a83f0:      	stur	q0, [sp, #40]
fffffe00079a83f4:      	str	x10, [sp, #32]
fffffe00079a83f8:      	strb	wzr, [sp, #41]
fffffe00079a83fc:      	stp	w9, w8, [sp, #16]
fffffe00079a8400:      	strb	wzr, [sp, #43]
fffffe00079a8404:      	strb	wzr, [sp, #45]
fffffe00079a8408:      	strb	wzr, [sp, #47]
fffffe00079a840c:      	strb	wzr, [sp, #49]
fffffe00079a8410:      	cbz	x21, 0xfffffe00079a8458 <_transmit+0x200>
fffffe00079a8414:      	ldr	x9, [x19, #152]
fffffe00079a8418:      	add	x1, sp, #16
fffffe00079a841c:      	mov	x0, x20
fffffe00079a8420:      	str	w8, [x19, #148]
fffffe00079a8424:      	str	x9, [sp, #24]
fffffe00079a8428:      	blr	x21
fffffe00079a842c:      	mov	w8, w0
fffffe00079a8430:      	mov	w0, wzr
fffffe00079a8434:      	str	wzr, [x19, #148]
fffffe00079a8438:      	tbz	w8, #31, 0xfffffe00079a845c <_transmit+0x204>
fffffe00079a843c:      	mov	x0, x19
fffffe00079a8440:      	mov	w1, #10
fffffe00079a8444:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a8448:      	ldr	w8, [sp, #12]
fffffe00079a844c:      	ldr	w9, [x19, #84]
fffffe00079a8450:      	cmp	w8, w9
fffffe00079a8454:      	b.ne	0xfffffe00079a8564 <_transmit+0x30c>
fffffe00079a8458:      	mov	w0, wzr
fffffe00079a845c:      	ldp	x29, x30, [sp, #160]
fffffe00079a8460:      	ldp	x20, x19, [sp, #144]
fffffe00079a8464:      	ldp	x22, x21, [sp, #128]
fffffe00079a8468:      	ldp	x24, x23, [sp, #112]
fffffe00079a846c:      	ldp	x26, x25, [sp, #96]
fffffe00079a8470:      	ldp	x28, x27, [sp, #80]
fffffe00079a8474:      	add	sp, sp, #176
fffffe00079a8478:      	ret
fffffe00079a847c:      	ldr	w8, [sp, #12]
fffffe00079a8480:      	mov	w9, #3
fffffe00079a8484:      	bics	wzr, w9, w8
fffffe00079a8488:      	b.ne	0xfffffe00079a838c <_transmit+0x134>
fffffe00079a848c:      	mov	x0, x19
fffffe00079a8490:      	mov	x1, x23
fffffe00079a8494:      	mov	x2, x22
fffffe00079a8498:      	bl	0xfffffe00079a8184 <_coherent>
fffffe00079a849c:      	cbnz	w0, 0xfffffe00079a838c <_transmit+0x134>
fffffe00079a84a0:      	mov	w3, #21528
fffffe00079a84a4:      	add	x4, sp, #12
fffffe00079a84a8:      	mov	x0, x19
fffffe00079a84ac:      	mov	x1, x23
fffffe00079a84b0:      	mov	x2, x22
fffffe00079a84b4:      	movk	w3, #6146, lsl #16
fffffe00079a84b8:      	mov	w25, #21528
fffffe00079a84bc:      	movk	w25, #6146, lsl #16
fffffe00079a84c0:      	bl	0xfffffe00079aa260 <_rr>
fffffe00079a84c4:      	cbnz	w0, 0xfffffe00079a838c <_transmit+0x134>
fffffe00079a84c8:      	ldr	w8, [sp, #12]
fffffe00079a84cc:      	ldr	w9, [x19, #84]
fffffe00079a84d0:      	cmp	w8, w9
fffffe00079a84d4:      	b.ne	0xfffffe00079a838c <_transmit+0x134>
fffffe00079a84d8:      	add	w3, w25, #4
fffffe00079a84dc:      	add	x4, sp, #12
fffffe00079a84e0:      	mov	x0, x19
fffffe00079a84e4:      	mov	x1, x23
fffffe00079a84e8:      	mov	x2, x22
fffffe00079a84ec:      	bl	0xfffffe00079aa260 <_rr>
fffffe00079a84f0:      	cbnz	w0, 0xfffffe00079a838c <_transmit+0x134>
fffffe00079a84f4:      	ldr	w8, [sp, #12]
fffffe00079a84f8:      	ldr	w9, [x19, #84]
fffffe00079a84fc:      	cmp	w8, w9
fffffe00079a8500:      	b.ne	0xfffffe00079a838c <_transmit+0x134>
fffffe00079a8504:      	mov	w8, #1
fffffe00079a8508:      	mov	x0, x19
fffffe00079a850c:      	mov	x1, x23
fffffe00079a8510:      	mov	x2, x22
fffffe00079a8514:      	mov	x3, x24
fffffe00079a8518:      	mov	w4, #16
fffffe00079a851c:      	mov	w5, #2
fffffe00079a8520:      	str	w8, [x19, #40]
fffffe00079a8524:      	bl	0xfffffe00079aa2d0 <_sy>
fffffe00079a8528:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a852c:      	add	x26, x19, #1145
fffffe00079a8530:      	mov	x0, x19
fffffe00079a8534:      	mov	x1, x23
fffffe00079a8538:      	mov	x2, x22
fffffe00079a853c:      	mov	x3, x24
fffffe00079a8540:      	mov	x4, x26
fffffe00079a8544:      	mov	w5, #16
fffffe00079a8548:      	bl	0xfffffe00079aa368 <_mr>
fffffe00079a854c:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a8550:      	ldrsb	w8, [x19, #1152]
fffffe00079a8554:      	tbnz	w8, #31, 0xfffffe00079a8830 <_transmit+0x5d8>
fffffe00079a8558:      	mov	x0, x19
fffffe00079a855c:      	mov	w1, #13
fffffe00079a8560:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a8564:      	add	w9, w9, #1
fffffe00079a8568:      	and	w9, w9, #0x3ff
fffffe00079a856c:      	cmp	w8, w9
fffffe00079a8570:      	b.ne	0xfffffe00079a859c <_transmit+0x344>
fffffe00079a8574:      	mov	w8, #1
fffffe00079a8578:      	mov	x0, x19
fffffe00079a857c:      	mov	x1, x23
fffffe00079a8580:      	mov	x2, x22
fffffe00079a8584:      	str	w8, [x19, #40]
fffffe00079a8588:      	bl	0xfffffe00079a806c <_before>
fffffe00079a858c:      	cbz	w0, 0xfffffe00079a85a8 <_transmit+0x350>
fffffe00079a8590:      	mov	x0, x19
fffffe00079a8594:      	mov	w1, #9
fffffe00079a8598:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a859c:      	mov	x0, x19
fffffe00079a85a0:      	mov	w1, #16
fffffe00079a85a4:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a85a8:      	mov	x0, x19
fffffe00079a85ac:      	mov	x1, x24
fffffe00079a85b0:      	mov	w2, #16
fffffe00079a85b4:      	mov	w3, wzr
fffffe00079a85b8:      	bl	0xfffffe00079a789c <_nst_memory_range>
fffffe00079a85bc:      	cbz	w0, 0xfffffe00079a8590 <_transmit+0x338>
fffffe00079a85c0:      	ldr	x8, [x23]
fffffe00079a85c4:      	mov	x0, x22
fffffe00079a85c8:      	mov	x1, x24
fffffe00079a85cc:      	mov	w2, #16
fffffe00079a85d0:      	mov	w3, #2
fffffe00079a85d4:      	ldr	x8, [x8, #40]
fffffe00079a85d8:      	blr	x8
fffffe00079a85dc:      	cbnz	w0, 0xfffffe00079a8590 <_transmit+0x338>
fffffe00079a85e0:      	add	x4, x19, #1145
fffffe00079a85e4:      	mov	x0, x19
fffffe00079a85e8:      	mov	x1, x23
fffffe00079a85ec:      	mov	x2, x22
fffffe00079a85f0:      	mov	x3, x24
fffffe00079a85f4:      	mov	w5, #16
fffffe00079a85f8:      	bl	0xfffffe00079aa368 <_mr>
fffffe00079a85fc:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a8600:      	ldrsb	w8, [x19, #1152]
fffffe00079a8604:      	tbz	w8, #31, 0xfffffe00079a8458 <_transmit+0x200>
fffffe00079a8608:      	ldp	x1, x2, [x19, #152]
fffffe00079a860c:      	movi.2d	v0, #0000000000000000
fffffe00079a8610:      	add	x24, x19, #1161
fffffe00079a8614:      	ldr	w4, [x19, #108]
fffffe00079a8618:      	add	x0, sp, #16
fffffe00079a861c:      	ldr	w5, [x19, #128]
fffffe00079a8620:      	mov	x3, x24
fffffe00079a8624:      	mov	w6, #2
fffffe00079a8628:      	str	xzr, [sp, #64]
fffffe00079a862c:      	stp	q0, q0, [sp, #32]
fffffe00079a8630:      	str	q0, [sp, #16]
fffffe00079a8634:      	bl	0xfffffe00079ab2c0 <_nstd_tx>
fffffe00079a8638:      	add	x0, sp, #16
fffffe00079a863c:      	bl	0xfffffe00079ab604 <_nstd_log>
fffffe00079a8640:      	ldr	w8, [x19, #112]
fffffe00079a8644:      	cmp	w8, #28
fffffe00079a8648:      	b.ne	0xfffffe00079a8660 <_transmit+0x408>
fffffe00079a864c:      	ldrb	w8, [x19, #1227]
fffffe00079a8650:      	add	x26, x19, #960
fffffe00079a8654:      	cmp	w8, #1
fffffe00079a8658:      	cset	w27, eq
fffffe00079a865c:      	b	0xfffffe00079a86b4 <_transmit+0x45c>
fffffe00079a8660:      	ldr	w9, [x19, #960]
fffffe00079a8664:      	add	x26, x19, #960
fffffe00079a8668:      	cmp	w9, #2
fffffe00079a866c:      	b.ne	0xfffffe00079a8688 <_transmit+0x430>
fffffe00079a8670:      	cmp	w8, #22
fffffe00079a8674:      	b.ne	0xfffffe00079a8688 <_transmit+0x430>
fffffe00079a8678:      	mov	w8, #1
fffffe00079a867c:      	mov	w27, wzr
fffffe00079a8680:      	str	w8, [x19, #1128]
fffffe00079a8684:      	b	0xfffffe00079a86b4 <_transmit+0x45c>
fffffe00079a8688:      	cmp	w8, #7
fffffe00079a868c:      	b.ne	0xfffffe00079a86b0 <_transmit+0x458>
fffffe00079a8690:      	ldrb	w8, [x19, #1225]
fffffe00079a8694:      	cbz	w8, 0xfffffe00079a86b0 <_transmit+0x458>
fffffe00079a8698:      	ldrb	w8, [x19, #1227]
fffffe00079a869c:      	cbz	w8, 0xfffffe00079a887c <_transmit+0x624>
fffffe00079a86a0:      	mov	w8, #1
fffffe00079a86a4:      	mov	w27, wzr
fffffe00079a86a8:      	str	w8, [x19, #936]
fffffe00079a86ac:      	b	0xfffffe00079a86b4 <_transmit+0x45c>
fffffe00079a86b0:      	mov	w27, wzr
fffffe00079a86b4:      	ldr	w25, [x19, #108]
fffffe00079a86b8:      	mov	w8, #1161
fffffe00079a86bc:      	mov	w9, #5257
fffffe00079a86c0:      	strb	wzr, [x19, x8]
fffffe00079a86c4:      	add	x8, x8, #1
fffffe00079a86c8:      	cmp	x8, x9
fffffe00079a86cc:      	b.ne	0xfffffe00079a86c0 <_transmit+0x468>
fffffe00079a86d0:      	mov	w8, #2
fffffe00079a86d4:      	ldr	x9, [x19]
fffffe00079a86d8:      	mov	x0, x19
fffffe00079a86dc:      	mov	x1, x23
fffffe00079a86e0:      	mov	x2, x22
fffffe00079a86e4:      	mov	x4, x24
fffffe00079a86e8:      	str	w8, [x19, #40]
fffffe00079a86ec:      	mov	w5, w25
fffffe00079a86f0:      	ldr	x3, [x9, #2856]
fffffe00079a86f4:      	bl	0xfffffe00079aa3fc <_mw>
fffffe00079a86f8:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a86fc:      	ldr	x8, [x19]
fffffe00079a8700:      	mov	x0, x19
fffffe00079a8704:      	mov	x1, x23
fffffe00079a8708:      	mov	x2, x22
fffffe00079a870c:      	mov	w4, w25
fffffe00079a8710:      	mov	w5, #1
fffffe00079a8714:      	ldr	x3, [x8, #2856]
fffffe00079a8718:      	bl	0xfffffe00079aa2d0 <_sy>
fffffe00079a871c:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a8720:      	ldr	w8, [x19, #84]
fffffe00079a8724:      	ldr	x9, [x19]
fffffe00079a8728:      	add	w8, w8, #1
fffffe00079a872c:      	and	w8, w8, #0x3ff
fffffe00079a8730:      	str	w8, [x9, #2864]
fffffe00079a8734:      	ldr	x8, [x19]
fffffe00079a8738:      	ldr	w9, [x8, #2864]
fffffe00079a873c:      	str	w9, [x19, #84]
fffffe00079a8740:      	ldr	w9, [x8, #1920]
fffffe00079a8744:      	add	w9, w9, #1
fffffe00079a8748:      	str	w9, [x8, #1920]
fffffe00079a874c:      	ldr	w8, [x19, #128]
fffffe00079a8750:      	cbnz	w8, 0xfffffe00079a8764 <_transmit+0x50c>
fffffe00079a8754:      	ldr	x8, [x19]
fffffe00079a8758:      	ldrb	w9, [x8, #2876]
fffffe00079a875c:      	add	w9, w9, #1
fffffe00079a8760:      	strb	w9, [x8, #2876]
fffffe00079a8764:      	ldr	x8, [x19]
fffffe00079a8768:      	mov	w9, #1
fffffe00079a876c:      	str	wzr, [x8, #1908]
fffffe00079a8770:      	ldr	w8, [x19, #96]
fffffe00079a8774:      	str	w9, [x19, #136]
fffffe00079a8778:      	str	wzr, [x19, #56]
fffffe00079a877c:      	str	wzr, [x19, #40]
fffffe00079a8780:      	add	w8, w8, #1
fffffe00079a8784:      	str	w8, [x19, #96]
fffffe00079a8788:      	cbz	w27, 0xfffffe00079a87d4 <_transmit+0x57c>
fffffe00079a878c:      	movi.2d	v0, #0000000000000000
fffffe00079a8790:      	mov	w8, #12
fffffe00079a8794:      	ldr	x9, [x19, #160]
fffffe00079a8798:      	add	x3, sp, #16
fffffe00079a879c:      	ldrb	w10, [x19, #984]
fffffe00079a87a0:      	mov	x0, x19
fffffe00079a87a4:      	mov	x1, x21
fffffe00079a87a8:      	mov	x2, x20
fffffe00079a87ac:      	strb	wzr, [x19, #985]
fffffe00079a87b0:      	str	xzr, [x19, #976]
fffffe00079a87b4:      	str	xzr, [sp, #64]
fffffe00079a87b8:      	stp	q0, q0, [sp, #16]
fffffe00079a87bc:      	str	q0, [sp, #48]
fffffe00079a87c0:      	str	w8, [sp, #16]
fffffe00079a87c4:      	str	x9, [sp, #32]
fffffe00079a87c8:      	strb	w10, [sp, #46]
fffffe00079a87cc:      	bl	0xfffffe00079a8f10 <_emit>
fffffe00079a87d0:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a87d4:      	ldr	w8, [x19, #128]
fffffe00079a87d8:      	sub	w9, w8, #2
fffffe00079a87dc:      	cmp	w9, #2
fffffe00079a87e0:      	b.hs	0xfffffe00079a8854 <_transmit+0x5fc>
fffffe00079a87e4:      	movi.2d	v0, #0000000000000000
fffffe00079a87e8:      	cmp	w8, #2
fffffe00079a87ec:      	mov	w8, #15
fffffe00079a87f0:      	ldr	x9, [x19, #160]
fffffe00079a87f4:      	cinc	w8, w8, ne
fffffe00079a87f8:      	str	xzr, [sp, #64]
fffffe00079a87fc:      	stp	q0, q0, [sp, #16]
fffffe00079a8800:      	str	w8, [sp, #16]
fffffe00079a8804:      	str	x9, [sp, #32]
fffffe00079a8808:      	ldp	w8, w9, [x19, #120]
fffffe00079a880c:      	str	q0, [sp, #48]
fffffe00079a8810:      	strb	w8, [sp, #42]
fffffe00079a8814:      	strb	w9, [sp, #43]
fffffe00079a8818:      	add	x3, sp, #16
fffffe00079a881c:      	mov	x0, x19
fffffe00079a8820:      	mov	x1, x21
fffffe00079a8824:      	mov	x2, x20
fffffe00079a8828:      	bl	0xfffffe00079a8f10 <_emit>
fffffe00079a882c:      	b	0xfffffe00079a845c <_transmit+0x204>
fffffe00079a8830:      	ldr	x8, [x19]
fffffe00079a8834:      	mov	x0, x26
fffffe00079a8838:      	ldr	w2, [x19, #108]
fffffe00079a883c:      	ldr	x1, [x8, #2856]
fffffe00079a8840:      	bl	0xfffffe0007990d40 <_nwt_tx_desc>
fffffe00079a8844:      	cbz	w0, 0xfffffe00079a8894 <_transmit+0x63c>
fffffe00079a8848:      	mov	x0, x19
fffffe00079a884c:      	mov	w1, #14
fffffe00079a8850:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a8854:      	cbnz	w8, 0xfffffe00079a8458 <_transmit+0x200>
fffffe00079a8858:      	ldr	w8, [x26]
fffffe00079a885c:      	cbz	w8, 0xfffffe00079a88b0 <_transmit+0x658>
fffffe00079a8860:      	cmp	w8, #1
fffffe00079a8864:      	b.ne	0xfffffe00079a8458 <_transmit+0x200>
fffffe00079a8868:      	ldr	w8, [x19, #956]
fffffe00079a886c:      	mov	w0, wzr
fffffe00079a8870:      	add	w8, w8, #1
fffffe00079a8874:      	str	w8, [x19, #956]
fffffe00079a8878:      	b	0xfffffe00079a845c <_transmit+0x204>
fffffe00079a887c:      	mov	w27, wzr
fffffe00079a8880:      	mov	w8, #1
fffffe00079a8884:      	ldrb	w9, [x19, #1237]
fffffe00079a8888:      	str	w8, [x19, #940]
fffffe00079a888c:      	strb	w9, [x19, #1144]
fffffe00079a8890:      	b	0xfffffe00079a86b4 <_transmit+0x45c>
fffffe00079a8894:      	ldr	w8, [x19, #68]
fffffe00079a8898:      	cbz	w8, 0xfffffe00079a88a4 <_transmit+0x64c>
fffffe00079a889c:      	ldr	w9, [x19, #72]
fffffe00079a88a0:      	cbz	w9, 0xfffffe00079a88e0 <_transmit+0x688>
fffffe00079a88a4:      	mov	x0, x19
fffffe00079a88a8:      	mov	w1, #35
fffffe00079a88ac:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a88b0:      	movi.2d	v0, #0000000000000000
fffffe00079a88b4:      	mov	w8, #1
fffffe00079a88b8:      	ldp	w10, w11, [x19, #112]
fffffe00079a88bc:      	ldr	x9, [x19, #160]
fffffe00079a88c0:      	str	xzr, [sp, #64]
fffffe00079a88c4:      	stp	q0, q0, [sp, #16]
fffffe00079a88c8:      	str	q0, [sp, #48]
fffffe00079a88cc:      	str	w8, [sp, #16]
fffffe00079a88d0:      	str	x9, [sp, #32]
fffffe00079a88d4:      	strb	w10, [sp, #40]
fffffe00079a88d8:      	strb	w11, [sp, #41]
fffffe00079a88dc:      	b	0xfffffe00079a8818 <_transmit+0x5c0>
fffffe00079a88e0:      	ldr	w9, [x19, #76]
fffffe00079a88e4:      	sub	w8, w8, #1
fffffe00079a88e8:      	mov	w10, #1
fffffe00079a88ec:      	mov	w11, #2
fffffe00079a88f0:      	add	x25, x19, #1161
fffffe00079a88f4:      	ldr	w5, [x19, #108]
fffffe00079a88f8:      	add	w9, w9, #1
fffffe00079a88fc:      	mov	x0, x19
fffffe00079a8900:      	stp	w8, w10, [x19, #68]
fffffe00079a8904:      	ldr	x8, [x19]
fffffe00079a8908:      	str	w11, [x19, #40]
fffffe00079a890c:      	mov	x1, x23
fffffe00079a8910:      	str	w9, [x19, #76]
fffffe00079a8914:      	mov	x2, x22
fffffe00079a8918:      	ldr	x3, [x8, #2856]
fffffe00079a891c:      	mov	x4, x25
fffffe00079a8920:      	bl	0xfffffe00079aa3fc <_mw>
fffffe00079a8924:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a8928:      	ldr	x8, [x19]
fffffe00079a892c:      	mov	x0, x19
fffffe00079a8930:      	ldr	w4, [x19, #108]
fffffe00079a8934:      	mov	x1, x23
fffffe00079a8938:      	mov	x2, x22
fffffe00079a893c:      	mov	w5, #1
fffffe00079a8940:      	ldr	x3, [x8, #2856]
fffffe00079a8944:      	bl	0xfffffe00079aa2d0 <_sy>
fffffe00079a8948:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a894c:      	mov	x0, x19
fffffe00079a8950:      	mov	x1, x23
fffffe00079a8954:      	mov	x2, x22
fffffe00079a8958:      	mov	x3, x24
fffffe00079a895c:      	mov	x4, x26
fffffe00079a8960:      	mov	w5, #16
fffffe00079a8964:      	bl	0xfffffe00079aa3fc <_mw>
fffffe00079a8968:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a896c:      	mov	x0, x19
fffffe00079a8970:      	mov	x1, x23
fffffe00079a8974:      	mov	x2, x22
fffffe00079a8978:      	mov	x3, x24
fffffe00079a897c:      	mov	w4, #16
fffffe00079a8980:      	mov	w5, #1
fffffe00079a8984:      	bl	0xfffffe00079aa2d0 <_sy>
fffffe00079a8988:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a898c:      	ldr	x8, [x19, #168]
fffffe00079a8990:      	mov	x9, #-50880
fffffe00079a8994:      	movk	x9, #65490, lsl #16
fffffe00079a8998:      	cmp	x8, x9
fffffe00079a899c:      	b.lo	0xfffffe00079a89ac <_transmit+0x754>
fffffe00079a89a0:      	mov	x0, x19
fffffe00079a89a4:      	mov	w1, #15
fffffe00079a89a8:      	b	0xfffffe00079a8394 <_transmit+0x13c>
fffffe00079a89ac:      	add	x8, x8, #732, lsl #12
fffffe00079a89b0:      	mov	w9, #3
fffffe00079a89b4:      	add	x8, x8, #1728
fffffe00079a89b8:      	ldr	x10, [x19]
fffffe00079a89bc:      	mov	w26, #1
fffffe00079a89c0:      	mov	w3, #21528
fffffe00079a89c4:      	str	w9, [x19, #40]
fffffe00079a89c8:      	mov	x0, x19
fffffe00079a89cc:      	str	x8, [x19, #184]
fffffe00079a89d0:      	mov	x1, x23
fffffe00079a89d4:      	str	w26, [x10, #1908]
fffffe00079a89d8:      	mov	x2, x22
fffffe00079a89dc:      	ldr	w8, [x19, #84]
fffffe00079a89e0:      	movk	w3, #6146, lsl #16
fffffe00079a89e4:      	add	w8, w8, #1
fffffe00079a89e8:      	and	w4, w8, #0x3ff
fffffe00079a89ec:      	bl	0xfffffe00079aa4a0 <_rw>
fffffe00079a89f0:      	cbnz	w0, 0xfffffe00079a8398 <_transmit+0x140>
fffffe00079a89f4:      	ldr	w8, [x19, #92]
fffffe00079a89f8:      	str	w26, [x19, #132]
fffffe00079a89fc:      	ldr	w9, [x19, #68]
fffffe00079a8a00:      	ldr	w10, [x19, #80]
fffffe00079a8a04:      	ldr	w5, [x19, #128]
fffffe00079a8a08:      	add	w8, w8, #1
fffffe00079a8a0c:      	add	w9, w9, #1
fffffe00079a8a10:      	add	w10, w10, #1
fffffe00079a8a14:      	cmp	w5, #1
fffffe00079a8a18:      	str	w8, [x19, #92]
fffffe00079a8a1c:      	stp	w9, wzr, [x19, #68]
fffffe00079a8a20:      	str	w10, [x19, #80]
fffffe00079a8a24:      	b.ne	0xfffffe00079a8a3c <_transmit+0x7e4>
fffffe00079a8a28:      	ldr	w8, [x19, #104]
fffffe00079a8a2c:      	mov	w9, #1
fffffe00079a8a30:      	add	w8, w8, #1
fffffe00079a8a34:      	str	w9, [x19, #60]
fffffe00079a8a38:      	str	w8, [x19, #104]
fffffe00079a8a3c:      	ldp	x1, x2, [x19, #152]
fffffe00079a8a40:      	movi.2d	v0, #0000000000000000
fffffe00079a8a44:      	add	x0, sp, #16
fffffe00079a8a48:      	ldr	w4, [x19, #108]
fffffe00079a8a4c:      	mov	x3, x25
fffffe00079a8a50:      	mov	w6, wzr
fffffe00079a8a54:      	str	xzr, [sp, #64]
fffffe00079a8a58:      	stp	q0, q0, [sp, #32]
fffffe00079a8a5c:      	str	q0, [sp, #16]
fffffe00079a8a60:      	bl	0xfffffe00079ab2c0 <_nstd_tx>
fffffe00079a8a64:      	add	x0, sp, #16
fffffe00079a8a68:      	bl	0xfffffe00079ab604 <_nstd_log>
fffffe00079a8a6c:      	ldp	x1, x2, [x19, #152]
fffffe00079a8a70:      	add	x0, sp, #16
fffffe00079a8a74:      	mov	x3, x25
fffffe00079a8a78:      	ldr	w4, [x19, #108]
fffffe00079a8a7c:      	mov	w6, #1
fffffe00079a8a80:      	ldr	w5, [x19, #128]
fffffe00079a8a84:      	bl	0xfffffe00079ab2c0 <_nstd_tx>
fffffe00079a8a88:      	add	x0, sp, #16
fffffe00079a8a8c:      	bl	0xfffffe00079ab604 <_nstd_log>
fffffe00079a8a90:      	ldr	w8, [x19, #112]
fffffe00079a8a94:      	cmp	w8, #7
fffffe00079a8a98:      	b.eq	0xfffffe00079a8ad4 <_transmit+0x87c>
fffffe00079a8a9c:      	cmp	w8, #28
fffffe00079a8aa0:      	b.eq	0xfffffe00079a8af0 <_transmit+0x898>
fffffe00079a8aa4:      	cmp	w8, #19
fffffe00079a8aa8:      	b.ne	0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8aac:      	ldrb	w8, [x19, #1245]
fffffe00079a8ab0:      	cmp	w8, #3
fffffe00079a8ab4:      	b.ne	0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8ab8:      	ldrb	w8, [x19, #1276]
fffffe00079a8abc:      	cbz	w8, 0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8ac0:      	mov	w8, #1
fffffe00079a8ac4:      	ldr	w9, [x19, #116]
fffffe00079a8ac8:      	str	w8, [x19, #944]
fffffe00079a8acc:      	str	w9, [x19, #948]
fffffe00079a8ad0:      	b	0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8ad4:      	ldrb	w8, [x19, #1225]
fffffe00079a8ad8:      	cbz	w8, 0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8adc:      	ldrb	w8, [x19, #1227]
fffffe00079a8ae0:      	cbz	w8, 0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8ae4:      	mov	w8, #1
fffffe00079a8ae8:      	str	w8, [x19, #932]
fffffe00079a8aec:      	b	0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8af0:      	ldrb	w8, [x19, #1227]
fffffe00079a8af4:      	cbnz	w8, 0xfffffe00079a8300 <_transmit+0xa8>
fffffe00079a8af8:      	ldrb	w8, [x19, #1226]
fffffe00079a8afc:      	mov	w9, #1
fffffe00079a8b00:      	strb	w8, [x19, #984]
fffffe00079a8b04:      	strb	w9, [x19, #985]
fffffe00079a8b08:      	b	0xfffffe00079a8300 <_transmit+0xa8>
