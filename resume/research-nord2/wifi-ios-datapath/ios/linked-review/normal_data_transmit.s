
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ae1f0 <_data_transmit>:
fffffe00079ae1f0:      	sub	sp, sp, #224
fffffe00079ae1f4:      	stp	x28, x27, [sp, #128]
fffffe00079ae1f8:      	stp	x26, x25, [sp, #144]
fffffe00079ae1fc:      	stp	x24, x23, [sp, #160]
fffffe00079ae200:      	stp	x22, x21, [sp, #176]
fffffe00079ae204:      	stp	x20, x19, [sp, #192]
fffffe00079ae208:      	stp	x29, x30, [sp, #208]
fffffe00079ae20c:      	add	x29, sp, #208
fffffe00079ae210:      	mov	w8, #10252
fffffe00079ae214:      	ldr	x9, [x0, #10232]
fffffe00079ae218:      	add	x28, x0, x8
fffffe00079ae21c:      	ldr	w8, [x0, #10240]
fffffe00079ae220:      	mov	x20, x4
fffffe00079ae224:      	mov	x21, x3
fffffe00079ae228:      	mov	x19, x0
fffffe00079ae22c:      	mov	x22, x2
fffffe00079ae230:      	lsl	w8, w8, #4
fffffe00079ae234:      	mov	x23, x1
fffffe00079ae238:      	ldr	w10, [x0, #132]
fffffe00079ae23c:      	add	x24, x9, x8
fffffe00079ae240:      	stur	wzr, [x29, #-84]
fffffe00079ae244:      	cbz	w10, 0xfffffe00079ae2c4 <_data_transmit+0xd4>
fffffe00079ae248:      	ldr	w8, [x19, #10256]
fffffe00079ae24c:      	cbz	w8, 0xfffffe00079ae91c <_data_transmit+0x72c>
fffffe00079ae250:      	ldr	w8, [x19, #10260]
fffffe00079ae254:      	cbz	w8, 0xfffffe00079ae99c <_data_transmit+0x7ac>
fffffe00079ae258:      	mov	w9, #10316
fffffe00079ae25c:      	mov	x8, xzr
fffffe00079ae260:      	add	x24, x19, x9
fffffe00079ae264:      	strb	wzr, [x24, x8]
fffffe00079ae268:      	add	x8, x8, #1
fffffe00079ae26c:      	cmp	x8, #1, lsl #12
fffffe00079ae270:      	b.ne	0xfffffe00079ae264 <_data_transmit+0x74>
fffffe00079ae274:      	mov	w8, #1761
fffffe00079ae278:      	mov	w9, #5857
fffffe00079ae27c:      	strb	wzr, [x19, x8]
fffffe00079ae280:      	add	x8, x8, #1
fffffe00079ae284:      	cmp	x8, x9
fffffe00079ae288:      	b.ne	0xfffffe00079ae27c <_data_transmit+0x8c>
fffffe00079ae28c:      	mov	w8, #15
fffffe00079ae290:      	mov	x0, x19
fffffe00079ae294:      	mov	x1, x23
fffffe00079ae298:      	mov	x2, x22
fffffe00079ae29c:      	ldr	x25, [x19, #10224]
fffffe00079ae2a0:      	str	w8, [x19, #40]
fffffe00079ae2a4:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ae2a8:      	cbz	w0, 0xfffffe00079ae324 <_data_transmit+0x134>
fffffe00079ae2ac:      	mov	w1, #8
fffffe00079ae2b0:      	mov	x0, x19
fffffe00079ae2b4:      	bl	0xfffffe00079afe24 <_fail>
fffffe00079ae2b8:      	mov	x0, x19
fffffe00079ae2bc:      	mov	w1, #46
fffffe00079ae2c0:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae2c4:      	mov	w8, #10224
fffffe00079ae2c8:      	add	x25, x19, x8
fffffe00079ae2cc:      	mov	x0, x25
fffffe00079ae2d0:      	bl	0xfffffe00079ab2d0 <_nstx_idle>
fffffe00079ae2d4:      	cbz	w0, 0xfffffe00079ae318 <_data_transmit+0x128>
fffffe00079ae2d8:      	mov	x0, x19
fffffe00079ae2dc:      	mov	x1, x23
fffffe00079ae2e0:      	mov	x2, x22
fffffe00079ae2e4:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ae2e8:      	cbnz	w0, 0xfffffe00079ae30c <_data_transmit+0x11c>
fffffe00079ae2ec:      	ldr	x8, [x23]
fffffe00079ae2f0:      	mov	w1, #240
fffffe00079ae2f4:      	sub	x2, x29, #84
fffffe00079ae2f8:      	mov	x0, x22
fffffe00079ae2fc:      	movk	w1, #6150, lsl #16
fffffe00079ae300:      	ldr	x8, [x8, #8]
fffffe00079ae304:      	blr	x8
fffffe00079ae308:      	cbz	w0, 0xfffffe00079ae384 <_data_transmit+0x194>
fffffe00079ae30c:      	mov	x0, x19
fffffe00079ae310:      	mov	w1, #5
fffffe00079ae314:      	bl	0xfffffe00079afe24 <_fail>
fffffe00079ae318:      	mov	x0, x19
fffffe00079ae31c:      	mov	w1, #40
fffffe00079ae320:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae324:      	mov	x0, x19
fffffe00079ae328:      	mov	x1, x25
fffffe00079ae32c:      	mov	w2, #4096
fffffe00079ae330:      	mov	w3, #1
fffffe00079ae334:      	mov	w26, #1
fffffe00079ae338:      	bl	0xfffffe00079ac720 <_nst_memory_range>
fffffe00079ae33c:      	cbz	w0, 0xfffffe00079ae2ac <_data_transmit+0xbc>
fffffe00079ae340:      	str	w26, [x19, #52]
fffffe00079ae344:      	mov	x0, x22
fffffe00079ae348:      	ldr	x8, [x23]
fffffe00079ae34c:      	mov	x1, x25
fffffe00079ae350:      	mov	x2, x24
fffffe00079ae354:      	mov	w3, #4096
fffffe00079ae358:      	ldr	x8, [x8, #32]
fffffe00079ae35c:      	blr	x8
fffffe00079ae360:      	cbnz	w0, 0xfffffe00079ae2ac <_data_transmit+0xbc>
fffffe00079ae364:      	mov	x0, x19
fffffe00079ae368:      	mov	x1, x23
fffffe00079ae36c:      	mov	x2, x22
fffffe00079ae370:      	ldr	x25, [x19, #10224]
fffffe00079ae374:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ae378:      	cbz	w0, 0xfffffe00079ae470 <_data_transmit+0x280>
fffffe00079ae37c:      	mov	w1, #9
fffffe00079ae380:      	b	0xfffffe00079ae2b0 <_data_transmit+0xc0>
fffffe00079ae384:      	ldur	w8, [x29, #-84]
fffffe00079ae388:      	mov	w9, #3
fffffe00079ae38c:      	bics	wzr, w9, w8
fffffe00079ae390:      	b.ne	0xfffffe00079ae318 <_data_transmit+0x128>
fffffe00079ae394:      	mov	x0, x19
fffffe00079ae398:      	mov	x1, x23
fffffe00079ae39c:      	mov	x2, x22
fffffe00079ae3a0:      	bl	0xfffffe00079ad2f0 <_coherent>
fffffe00079ae3a4:      	cbnz	w0, 0xfffffe00079ae318 <_data_transmit+0x128>
fffffe00079ae3a8:      	mov	w3, #21256
fffffe00079ae3ac:      	sub	x4, x29, #84
fffffe00079ae3b0:      	mov	x0, x19
fffffe00079ae3b4:      	mov	x1, x23
fffffe00079ae3b8:      	mov	x2, x22
fffffe00079ae3bc:      	movk	w3, #6146, lsl #16
fffffe00079ae3c0:      	mov	w26, #21256
fffffe00079ae3c4:      	movk	w26, #6146, lsl #16
fffffe00079ae3c8:      	bl	0xfffffe00079aff1c <_rr>
fffffe00079ae3cc:      	cbnz	w0, 0xfffffe00079ae318 <_data_transmit+0x128>
fffffe00079ae3d0:      	ldur	w8, [x29, #-84]
fffffe00079ae3d4:      	ldr	w9, [x19, #10240]
fffffe00079ae3d8:      	cmp	w8, w9
fffffe00079ae3dc:      	b.ne	0xfffffe00079ae318 <_data_transmit+0x128>
fffffe00079ae3e0:      	add	w3, w26, #4
fffffe00079ae3e4:      	sub	x4, x29, #84
fffffe00079ae3e8:      	mov	x0, x19
fffffe00079ae3ec:      	mov	x1, x23
fffffe00079ae3f0:      	mov	x2, x22
fffffe00079ae3f4:      	bl	0xfffffe00079aff1c <_rr>
fffffe00079ae3f8:      	cbnz	w0, 0xfffffe00079ae318 <_data_transmit+0x128>
fffffe00079ae3fc:      	ldur	w8, [x29, #-84]
fffffe00079ae400:      	ldr	w9, [x19, #10240]
fffffe00079ae404:      	cmp	w8, w9
fffffe00079ae408:      	b.ne	0xfffffe00079ae318 <_data_transmit+0x128>
fffffe00079ae40c:      	mov	w8, #10
fffffe00079ae410:      	mov	x0, x19
fffffe00079ae414:      	mov	x1, x23
fffffe00079ae418:      	mov	x2, x22
fffffe00079ae41c:      	mov	x3, x24
fffffe00079ae420:      	mov	w4, #16
fffffe00079ae424:      	mov	w5, #2
fffffe00079ae428:      	str	w8, [x19, #40]
fffffe00079ae42c:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079ae430:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079ae434:      	mov	w8, #10300
fffffe00079ae438:      	mov	x0, x19
fffffe00079ae43c:      	add	x27, x19, x8
fffffe00079ae440:      	mov	x1, x23
fffffe00079ae444:      	mov	x2, x22
fffffe00079ae448:      	mov	x3, x24
fffffe00079ae44c:      	mov	x4, x27
fffffe00079ae450:      	mov	w5, #16
fffffe00079ae454:      	bl	0xfffffe00079b0024 <_mr>
fffffe00079ae458:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079ae45c:      	ldrsb	w8, [x28, #55]
fffffe00079ae460:      	tbnz	w8, #31, 0xfffffe00079ae5e8 <_data_transmit+0x3f8>
fffffe00079ae464:      	mov	x0, x19
fffffe00079ae468:      	mov	w1, #41
fffffe00079ae46c:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae470:      	mov	x0, x19
fffffe00079ae474:      	mov	x1, x25
fffffe00079ae478:      	mov	w2, #4096
fffffe00079ae47c:      	mov	w3, #1
fffffe00079ae480:      	bl	0xfffffe00079ac720 <_nst_memory_range>
fffffe00079ae484:      	cbz	w0, 0xfffffe00079ae37c <_data_transmit+0x18c>
fffffe00079ae488:      	ldr	x8, [x23]
fffffe00079ae48c:      	mov	x0, x22
fffffe00079ae490:      	mov	x1, x25
fffffe00079ae494:      	mov	w2, #4096
fffffe00079ae498:      	mov	w3, #1
fffffe00079ae49c:      	ldr	x8, [x8, #40]
fffffe00079ae4a0:      	blr	x8
fffffe00079ae4a4:      	cbnz	w0, 0xfffffe00079ae37c <_data_transmit+0x18c>
fffffe00079ae4a8:      	mov	x0, x19
fffffe00079ae4ac:      	mov	x1, x23
fffffe00079ae4b0:      	mov	x2, x22
fffffe00079ae4b4:      	ldr	x25, [x19, #10224]
fffffe00079ae4b8:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ae4bc:      	cbnz	w0, 0xfffffe00079ae37c <_data_transmit+0x18c>
fffffe00079ae4c0:      	mov	x0, x19
fffffe00079ae4c4:      	mov	x1, x25
fffffe00079ae4c8:      	mov	w2, #4096
fffffe00079ae4cc:      	mov	w3, wzr
fffffe00079ae4d0:      	bl	0xfffffe00079ac720 <_nst_memory_range>
fffffe00079ae4d4:      	cbz	w0, 0xfffffe00079ae37c <_data_transmit+0x18c>
fffffe00079ae4d8:      	ldr	x8, [x23]
fffffe00079ae4dc:      	mov	x0, x22
fffffe00079ae4e0:      	mov	x1, x25
fffffe00079ae4e4:      	mov	w2, #4096
fffffe00079ae4e8:      	mov	w3, #2
fffffe00079ae4ec:      	ldr	x8, [x8, #40]
fffffe00079ae4f0:      	blr	x8
fffffe00079ae4f4:      	cbnz	w0, 0xfffffe00079ae37c <_data_transmit+0x18c>
fffffe00079ae4f8:      	mov	w8, #14416
fffffe00079ae4fc:      	ldr	x3, [x19, #10224]
fffffe00079ae500:      	add	x25, x19, x8
fffffe00079ae504:      	mov	x0, x19
fffffe00079ae508:      	mov	x1, x23
fffffe00079ae50c:      	mov	x2, x22
fffffe00079ae510:      	mov	x4, x25
fffffe00079ae514:      	mov	w5, #4096
fffffe00079ae518:      	bl	0xfffffe00079b0024 <_mr>
fffffe00079ae51c:      	cbnz	w0, 0xfffffe00079ae2b8 <_data_transmit+0xc8>
fffffe00079ae520:      	mov	x8, xzr
fffffe00079ae524:      	mov	w9, #4100
fffffe00079ae528:      	cmp	x8, #1, lsl #12
fffffe00079ae52c:      	b.eq	0xfffffe00079ae54c <_data_transmit+0x35c>
fffffe00079ae530:      	add	x10, x24, x8
fffffe00079ae534:      	add	x8, x8, #1
fffffe00079ae538:      	ldrb	w11, [x10, x9]
fffffe00079ae53c:      	ldrb	w10, [x10]
fffffe00079ae540:      	cmp	w10, w11
fffffe00079ae544:      	b.eq	0xfffffe00079ae528 <_data_transmit+0x338>
fffffe00079ae548:      	b	0xfffffe00079ae2b8 <_data_transmit+0xc8>
fffffe00079ae54c:      	mov	x8, xzr
fffffe00079ae550:      	strb	wzr, [x25, x8]
fffffe00079ae554:      	add	x8, x8, #1
fffffe00079ae558:      	cmp	x8, #1, lsl #12
fffffe00079ae55c:      	b.ne	0xfffffe00079ae550 <_data_transmit+0x360>
fffffe00079ae560:      	mov	w8, #1
fffffe00079ae564:      	ldr	w9, [x19, #10272]
fffffe00079ae568:      	ldr	w10, [x19, #10240]
fffffe00079ae56c:      	add	x3, sp, #16
fffffe00079ae570:      	movi.2d	v0, #0000000000000000
fffffe00079ae574:      	mov	x0, x19
fffffe00079ae578:      	str	w8, [x19, #136]
fffffe00079ae57c:      	add	w8, w9, #1
fffffe00079ae580:      	add	w9, w10, #1
fffffe00079ae584:      	mov	x1, x21
fffffe00079ae588:      	and	w9, w9, #0x3ff
fffffe00079ae58c:      	mov	x2, x20
fffffe00079ae590:      	str	w8, [x19, #10272]
fffffe00079ae594:      	ldr	w8, [x19, #128]
fffffe00079ae598:      	stp	q0, q0, [sp, #16]
fffffe00079ae59c:      	str	w9, [x19, #10240]
fffffe00079ae5a0:      	ldr	x9, [x19, #160]
fffffe00079ae5a4:      	cmp	w8, #2
fffffe00079ae5a8:      	mov	w8, #15
fffffe00079ae5ac:      	cinc	w8, w8, ne
fffffe00079ae5b0:      	str	xzr, [x28]
fffffe00079ae5b4:      	str	x9, [sp, #32]
fffffe00079ae5b8:      	ldrb	w9, [x19, #207]
fffffe00079ae5bc:      	str	wzr, [x19, #10260]
fffffe00079ae5c0:      	str	w8, [sp, #16]
fffffe00079ae5c4:      	mov	w8, #127
fffffe00079ae5c8:      	str	wzr, [x19, #56]
fffffe00079ae5cc:      	str	wzr, [x19, #40]
fffffe00079ae5d0:      	str	xzr, [sp, #64]
fffffe00079ae5d4:      	str	q0, [sp, #48]
fffffe00079ae5d8:      	strb	w8, [sp, #42]
fffffe00079ae5dc:      	strb	w9, [sp, #43]
fffffe00079ae5e0:      	bl	0xfffffe00079ae188 <_emit>
fffffe00079ae5e4:      	b	0xfffffe00079ae96c <_data_transmit+0x77c>
fffffe00079ae5e8:      	ldr	w10, [x19, #228]
fffffe00079ae5ec:      	mov	w11, #10316
fffffe00079ae5f0:      	ldrb	w8, [x19, #1773]
fffffe00079ae5f4:      	add	x26, x19, #1793
fffffe00079ae5f8:      	ldr	w12, [x19, #108]
fffffe00079ae5fc:      	mov	w9, #127
fffffe00079ae600:      	str	w10, [sp, #96]
fffffe00079ae604:      	ldr	w10, [x19, #128]
fffffe00079ae608:      	ubfx	w8, w8, #1, #1
fffffe00079ae60c:      	ldr	w7, [x19, #10244]
fffffe00079ae610:      	ldur	q0, [x19, #212]
fffffe00079ae614:      	add	x0, x19, x11
fffffe00079ae618:      	cmp	w10, #2
fffffe00079ae61c:      	sub	w5, w12, #32
fffffe00079ae620:      	strb	w8, [sp, #96]
fffffe00079ae624:      	cset	w6, eq
fffffe00079ae628:      	ldr	x8, [x19, #10224]
fffffe00079ae62c:      	sub	x2, x29, #96
fffffe00079ae630:      	add	x3, sp, #80
fffffe00079ae634:      	mov	w1, #4096
fffffe00079ae638:      	mov	x4, x26
fffffe00079ae63c:      	str	q0, [sp, #80]
fffffe00079ae640:      	strb	w9, [sp, #83]
fffffe00079ae644:      	stur	xzr, [x29, #-96]
fffffe00079ae648:      	str	w7, [x19, #10248]
fffffe00079ae64c:      	stp	x8, x0, [sp]
fffffe00079ae650:      	bl	0xfffffe00079aad5c <_nstx_build>
fffffe00079ae654:      	cbnz	w0, 0xfffffe00079ae680 <_data_transmit+0x490>
fffffe00079ae658:      	ldr	w8, [x19, #108]
fffffe00079ae65c:      	ldur	x9, [x29, #-96]
fffffe00079ae660:      	add	w8, w8, #32
fffffe00079ae664:      	cmp	x9, x8
fffffe00079ae668:      	b.ne	0xfffffe00079ae680 <_data_transmit+0x490>
fffffe00079ae66c:      	ldr	x1, [x25]
fffffe00079ae670:      	mov	x0, x27
fffffe00079ae674:      	mov	w2, #64
fffffe00079ae678:      	bl	0xfffffe0007995038 <_nwt_tx_desc>
fffffe00079ae67c:      	cbz	w0, 0xfffffe00079ae68c <_data_transmit+0x49c>
fffffe00079ae680:      	mov	x0, x19
fffffe00079ae684:      	mov	w1, #42
fffffe00079ae688:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae68c:      	mov	w8, #11
fffffe00079ae690:      	ldr	x3, [x19, #10224]
fffffe00079ae694:      	mov	x0, x19
fffffe00079ae698:      	mov	x1, x23
fffffe00079ae69c:      	mov	x2, x22
fffffe00079ae6a0:      	ldr	x4, [sp, #8]
fffffe00079ae6a4:      	mov	w5, #4096
fffffe00079ae6a8:      	str	w8, [x19, #40]
fffffe00079ae6ac:      	bl	0xfffffe00079b00b8 <_mw>
fffffe00079ae6b0:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079ae6b4:      	ldr	x3, [x19, #10224]
fffffe00079ae6b8:      	mov	x0, x19
fffffe00079ae6bc:      	mov	x1, x23
fffffe00079ae6c0:      	mov	x2, x22
fffffe00079ae6c4:      	mov	w4, #4096
fffffe00079ae6c8:      	mov	w5, #1
fffffe00079ae6cc:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079ae6d0:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079ae6d4:      	mov	w8, #12
fffffe00079ae6d8:      	ldr	x3, [x19, #10224]
fffffe00079ae6dc:      	mov	x0, x19
fffffe00079ae6e0:      	mov	x1, x23
fffffe00079ae6e4:      	mov	x2, x22
fffffe00079ae6e8:      	mov	w4, #4096
fffffe00079ae6ec:      	mov	w5, #2
fffffe00079ae6f0:      	str	w8, [x19, #40]
fffffe00079ae6f4:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079ae6f8:      	cbnz	w0, 0xfffffe00079ae750 <_data_transmit+0x560>
fffffe00079ae6fc:      	mov	w8, #14416
fffffe00079ae700:      	ldr	x3, [x19, #10224]
fffffe00079ae704:      	add	x25, x19, x8
fffffe00079ae708:      	mov	x0, x19
fffffe00079ae70c:      	mov	x1, x23
fffffe00079ae710:      	mov	x2, x22
fffffe00079ae714:      	mov	x4, x25
fffffe00079ae718:      	mov	w5, #4096
fffffe00079ae71c:      	bl	0xfffffe00079b0024 <_mr>
fffffe00079ae720:      	cbnz	w0, 0xfffffe00079ae750 <_data_transmit+0x560>
fffffe00079ae724:      	mov	x8, xzr
fffffe00079ae728:      	mov	w9, #4100
fffffe00079ae72c:      	cmp	x8, #1, lsl #12
fffffe00079ae730:      	b.eq	0xfffffe00079ae75c <_data_transmit+0x56c>
fffffe00079ae734:      	ldr	x10, [sp, #8]
fffffe00079ae738:      	add	x10, x10, x8
fffffe00079ae73c:      	add	x8, x8, #1
fffffe00079ae740:      	ldrb	w11, [x10, x9]
fffffe00079ae744:      	ldrb	w10, [x10]
fffffe00079ae748:      	cmp	w10, w11
fffffe00079ae74c:      	b.eq	0xfffffe00079ae72c <_data_transmit+0x53c>
fffffe00079ae750:      	mov	x0, x19
fffffe00079ae754:      	mov	w1, #43
fffffe00079ae758:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae75c:      	mov	x8, xzr
fffffe00079ae760:      	strb	wzr, [x25, x8]
fffffe00079ae764:      	add	x8, x8, #1
fffffe00079ae768:      	cmp	x8, #1, lsl #12
fffffe00079ae76c:      	b.ne	0xfffffe00079ae760 <_data_transmit+0x570>
fffffe00079ae770:      	mov	w8, #11
fffffe00079ae774:      	mov	x0, x19
fffffe00079ae778:      	mov	x1, x23
fffffe00079ae77c:      	mov	x2, x22
fffffe00079ae780:      	mov	x3, x24
fffffe00079ae784:      	mov	x4, x27
fffffe00079ae788:      	mov	w5, #16
fffffe00079ae78c:      	str	w8, [x19, #40]
fffffe00079ae790:      	bl	0xfffffe00079b00b8 <_mw>
fffffe00079ae794:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079ae798:      	mov	x0, x19
fffffe00079ae79c:      	mov	x1, x23
fffffe00079ae7a0:      	mov	x2, x22
fffffe00079ae7a4:      	mov	x3, x24
fffffe00079ae7a8:      	mov	w4, #16
fffffe00079ae7ac:      	mov	w5, #1
fffffe00079ae7b0:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079ae7b4:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079ae7b8:      	ldr	x8, [x19, #168]
fffffe00079ae7bc:      	mov	x9, #-38528
fffffe00079ae7c0:      	movk	x9, #65383, lsl #16
fffffe00079ae7c4:      	cmp	x8, x9
fffffe00079ae7c8:      	b.lo	0xfffffe00079ae7d8 <_data_transmit+0x5e8>
fffffe00079ae7cc:      	mov	x0, x19
fffffe00079ae7d0:      	mov	w1, #44
fffffe00079ae7d4:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae7d8:      	add	x8, x8, #2441, lsl #12
fffffe00079ae7dc:      	mov	w10, #13
fffffe00079ae7e0:      	add	x9, x8, #1664
fffffe00079ae7e4:      	ldr	w8, [x19, #10284]
fffffe00079ae7e8:      	str	w10, [x19, #40]
fffffe00079ae7ec:      	str	x9, [x19, #184]
fffffe00079ae7f0:      	cbz	w8, 0xfffffe00079ae7fc <_data_transmit+0x60c>
fffffe00079ae7f4:      	ldr	w9, [x19, #10288]
fffffe00079ae7f8:      	cbz	w9, 0xfffffe00079ae808 <_data_transmit+0x618>
fffffe00079ae7fc:      	mov	x0, x19
fffffe00079ae800:      	mov	w1, #47
fffffe00079ae804:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae808:      	ldr	w10, [x19, #10240]
fffffe00079ae80c:      	mov	w3, #21256
fffffe00079ae810:      	ldr	w9, [x19, #10292]
fffffe00079ae814:      	sub	w8, w8, #1
fffffe00079ae818:      	mov	w25, #1
fffffe00079ae81c:      	mov	x0, x19
fffffe00079ae820:      	add	w10, w10, #1
fffffe00079ae824:      	mov	x1, x23
fffffe00079ae828:      	add	w9, w9, #1
fffffe00079ae82c:      	and	w4, w10, #0x3ff
fffffe00079ae830:      	mov	x2, x22
fffffe00079ae834:      	movk	w3, #6146, lsl #16
fffffe00079ae838:      	str	w8, [x19, #10284]
fffffe00079ae83c:      	str	w25, [x19, #10288]
fffffe00079ae840:      	str	w9, [x19, #10292]
fffffe00079ae844:      	bl	0xfffffe00079b015c <_rw>
fffffe00079ae848:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079ae84c:      	ldr	w8, [x19, #10284]
fffffe00079ae850:      	mov	x4, x26
fffffe00079ae854:      	ldr	w9, [x19, #10296]
fffffe00079ae858:      	str	w25, [x19, #10252]
fffffe00079ae85c:      	ldr	w11, [x19, #10244]
fffffe00079ae860:      	str	w25, [x19, #132]
fffffe00079ae864:      	add	w8, w8, #1
fffffe00079ae868:      	ldr	w10, [x19, #10264]
fffffe00079ae86c:      	add	w9, w9, #1
fffffe00079ae870:      	ldr	x1, [x19, #152]
fffffe00079ae874:      	add	w11, w11, #1
fffffe00079ae878:      	ldr	w3, [x19, #128]
fffffe00079ae87c:      	str	w8, [x19, #10284]
fffffe00079ae880:      	and	w8, w11, #0x7ff
fffffe00079ae884:      	str	w9, [x19, #10296]
fffffe00079ae888:      	ldr	w9, [x19, #92]
fffffe00079ae88c:      	add	w10, w10, #1
fffffe00079ae890:      	str	wzr, [x19, #10288]
fffffe00079ae894:      	str	w8, [x19, #10244]
fffffe00079ae898:      	ldr	w8, [x19, #108]
fffffe00079ae89c:      	add	w2, w9, #1
fffffe00079ae8a0:      	mov	w9, #9776
fffffe00079ae8a4:      	add	x0, x19, x9
fffffe00079ae8a8:      	str	w10, [x19, #10264]
fffffe00079ae8ac:      	sub	w5, w8, #32
fffffe00079ae8b0:      	str	w2, [x19, #92]
fffffe00079ae8b4:      	bl	0xfffffe00079b1a7c <_nsdd_publish>
fffffe00079ae8b8:      	ldr	x25, [sp, #8]
fffffe00079ae8bc:      	add	x0, sp, #16
fffffe00079ae8c0:      	ldp	x1, x2, [x19, #152]
fffffe00079ae8c4:      	movi.2d	v0, #0000000000000000
fffffe00079ae8c8:      	mov	w6, wzr
fffffe00079ae8cc:      	ldr	w4, [x19, #108]
fffffe00079ae8d0:      	mov	x3, x25
fffffe00079ae8d4:      	ldr	w5, [x19, #128]
fffffe00079ae8d8:      	str	xzr, [sp, #64]
fffffe00079ae8dc:      	stp	q0, q0, [sp, #32]
fffffe00079ae8e0:      	str	q0, [sp, #16]
fffffe00079ae8e4:      	bl	0xfffffe00079b10f8 <_nstd_tx>
fffffe00079ae8e8:      	add	x0, sp, #16
fffffe00079ae8ec:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079ae8f0:      	ldp	x1, x2, [x19, #152]
fffffe00079ae8f4:      	add	x0, sp, #16
fffffe00079ae8f8:      	mov	x3, x25
fffffe00079ae8fc:      	ldr	w4, [x19, #108]
fffffe00079ae900:      	mov	w6, #1
fffffe00079ae904:      	ldr	w5, [x19, #128]
fffffe00079ae908:      	bl	0xfffffe00079b10f8 <_nstd_tx>
fffffe00079ae90c:      	add	x0, sp, #16
fffffe00079ae910:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079ae914:      	ldr	w8, [x19, #10256]
fffffe00079ae918:      	cbnz	w8, 0xfffffe00079ae250 <_data_transmit+0x60>
fffffe00079ae91c:      	mov	w8, #14
fffffe00079ae920:      	mov	x0, x19
fffffe00079ae924:      	mov	x1, x23
fffffe00079ae928:      	mov	x2, x22
fffffe00079ae92c:      	str	w8, [x19, #40]
fffffe00079ae930:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ae934:      	cbnz	w0, 0xfffffe00079ae95c <_data_transmit+0x76c>
fffffe00079ae938:      	ldr	x8, [x23]
fffffe00079ae93c:      	mov	w9, #21256
fffffe00079ae940:      	movk	w9, #6146, lsl #16
fffffe00079ae944:      	sub	x2, x29, #84
fffffe00079ae948:      	orr	w1, w9, #0x4
fffffe00079ae94c:      	mov	x0, x22
fffffe00079ae950:      	ldr	x8, [x8, #8]
fffffe00079ae954:      	blr	x8
fffffe00079ae958:      	cbz	w0, 0xfffffe00079ae98c <_data_transmit+0x79c>
fffffe00079ae95c:      	mov	x0, x19
fffffe00079ae960:      	mov	w1, #5
fffffe00079ae964:      	bl	0xfffffe00079afe24 <_fail>
fffffe00079ae968:      	mov	w0, #-1
fffffe00079ae96c:      	ldp	x29, x30, [sp, #208]
fffffe00079ae970:      	ldp	x20, x19, [sp, #192]
fffffe00079ae974:      	ldp	x22, x21, [sp, #176]
fffffe00079ae978:      	ldp	x24, x23, [sp, #160]
fffffe00079ae97c:      	ldp	x26, x25, [sp, #144]
fffffe00079ae980:      	ldp	x28, x27, [sp, #128]
fffffe00079ae984:      	add	sp, sp, #224
fffffe00079ae988:      	ret
fffffe00079ae98c:      	ldur	w8, [x29, #-84]
fffffe00079ae990:      	ldr	w9, [x19, #10240]
fffffe00079ae994:      	cmp	w8, w9
fffffe00079ae998:      	b.ne	0xfffffe00079ae9a4 <_data_transmit+0x7b4>
fffffe00079ae99c:      	mov	w0, wzr
fffffe00079ae9a0:      	b	0xfffffe00079ae96c <_data_transmit+0x77c>
fffffe00079ae9a4:      	add	w9, w9, #1
fffffe00079ae9a8:      	and	w9, w9, #0x3ff
fffffe00079ae9ac:      	cmp	w8, w9
fffffe00079ae9b0:      	b.ne	0xfffffe00079ae9dc <_data_transmit+0x7ec>
fffffe00079ae9b4:      	mov	w8, #10
fffffe00079ae9b8:      	mov	x0, x19
fffffe00079ae9bc:      	mov	x1, x23
fffffe00079ae9c0:      	mov	x2, x22
fffffe00079ae9c4:      	str	w8, [x19, #40]
fffffe00079ae9c8:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ae9cc:      	cbz	w0, 0xfffffe00079ae9e8 <_data_transmit+0x7f8>
fffffe00079ae9d0:      	mov	x0, x19
fffffe00079ae9d4:      	mov	w1, #9
fffffe00079ae9d8:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae9dc:      	mov	x0, x19
fffffe00079ae9e0:      	mov	w1, #45
fffffe00079ae9e4:      	b	0xfffffe00079ae964 <_data_transmit+0x774>
fffffe00079ae9e8:      	mov	x0, x19
fffffe00079ae9ec:      	mov	x1, x24
fffffe00079ae9f0:      	mov	w2, #16
fffffe00079ae9f4:      	mov	w3, wzr
fffffe00079ae9f8:      	bl	0xfffffe00079ac720 <_nst_memory_range>
fffffe00079ae9fc:      	cbz	w0, 0xfffffe00079ae9d0 <_data_transmit+0x7e0>
fffffe00079aea00:      	ldr	x8, [x23]
fffffe00079aea04:      	mov	x0, x22
fffffe00079aea08:      	mov	x1, x24
fffffe00079aea0c:      	mov	w2, #16
fffffe00079aea10:      	mov	w3, #2
fffffe00079aea14:      	ldr	x8, [x8, #40]
fffffe00079aea18:      	blr	x8
fffffe00079aea1c:      	cbnz	w0, 0xfffffe00079ae9d0 <_data_transmit+0x7e0>
fffffe00079aea20:      	mov	w8, #10300
fffffe00079aea24:      	mov	x0, x19
fffffe00079aea28:      	add	x4, x19, x8
fffffe00079aea2c:      	mov	x1, x23
fffffe00079aea30:      	mov	x2, x22
fffffe00079aea34:      	mov	x3, x24
fffffe00079aea38:      	mov	w5, #16
fffffe00079aea3c:      	bl	0xfffffe00079b0024 <_mr>
fffffe00079aea40:      	cbnz	w0, 0xfffffe00079ae968 <_data_transmit+0x778>
fffffe00079aea44:      	ldrsb	w8, [x28, #55]
fffffe00079aea48:      	tbz	w8, #31, 0xfffffe00079ae99c <_data_transmit+0x7ac>
fffffe00079aea4c:      	mov	w9, #1
fffffe00079aea50:      	ldr	w10, [x19, #96]
fffffe00079aea54:      	ldr	w8, [x19, #10268]
fffffe00079aea58:      	add	x0, sp, #16
fffffe00079aea5c:      	ldp	x1, x2, [x19, #152]
fffffe00079aea60:      	movi.2d	v0, #0000000000000000
fffffe00079aea64:      	str	w9, [x19, #10256]
fffffe00079aea68:      	add	w9, w10, #1
fffffe00079aea6c:      	mov	w10, #10316
fffffe00079aea70:      	add	w8, w8, #1
fffffe00079aea74:      	ldr	w4, [x19, #108]
fffffe00079aea78:      	add	x3, x19, x10
fffffe00079aea7c:      	ldr	w5, [x19, #128]
fffffe00079aea80:      	mov	w6, #2
fffffe00079aea84:      	str	xzr, [sp, #64]
fffffe00079aea88:      	stp	q0, q0, [sp, #32]
fffffe00079aea8c:      	str	w8, [x19, #10268]
fffffe00079aea90:      	str	w9, [x19, #96]
fffffe00079aea94:      	str	q0, [sp, #16]
fffffe00079aea98:      	bl	0xfffffe00079b10f8 <_nstd_tx>
fffffe00079aea9c:      	add	x0, sp, #16
fffffe00079aeaa0:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079aeaa4:      	ldr	w8, [x19, #10260]
fffffe00079aeaa8:      	cbnz	w8, 0xfffffe00079ae258 <_data_transmit+0x68>
fffffe00079aeaac:      	b	0xfffffe00079ae99c <_data_transmit+0x7ac>
