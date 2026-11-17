
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7240 <_transmit>:
fffffe00079a7240:      	sub	sp, sp, #176
fffffe00079a7244:      	stp	x28, x27, [sp, #80]
fffffe00079a7248:      	stp	x26, x25, [sp, #96]
fffffe00079a724c:      	stp	x24, x23, [sp, #112]
fffffe00079a7250:      	stp	x22, x21, [sp, #128]
fffffe00079a7254:      	stp	x20, x19, [sp, #144]
fffffe00079a7258:      	stp	x29, x30, [sp, #160]
fffffe00079a725c:      	add	x29, sp, #160
fffffe00079a7260:      	ldr	w8, [x0, #56]
fffffe00079a7264:      	cbz	w8, 0xfffffe00079a7428 <_transmit+0x1e8>
fffffe00079a7268:      	ldr	w8, [x0, #128]
fffffe00079a726c:      	mov	x20, x4
fffffe00079a7270:      	mov	x21, x3
fffffe00079a7274:      	mov	x19, x0
fffffe00079a7278:      	mov	x22, x2
fffffe00079a727c:      	mov	x23, x1
fffffe00079a7280:      	cmp	w8, #3
fffffe00079a7284:      	b.ne	0xfffffe00079a7298 <_transmit+0x58>
fffffe00079a7288:      	ldr	w9, [x19, #1140]
fffffe00079a728c:      	cbz	w9, 0xfffffe00079a7298 <_transmit+0x58>
fffffe00079a7290:      	ldr	w9, [x19, #132]
fffffe00079a7294:      	cbz	w9, 0xfffffe00079a7428 <_transmit+0x1e8>
fffffe00079a7298:      	ldr	w10, [x19, #24]
fffffe00079a729c:      	ldr	w9, [x19, #132]
fffffe00079a72a0:      	cbz	w10, 0xfffffe00079a72cc <_transmit+0x8c>
fffffe00079a72a4:      	cbnz	w9, 0xfffffe00079a72cc <_transmit+0x8c>
fffffe00079a72a8:      	mov	w9, w8
fffffe00079a72ac:      	cbz	w8, 0xfffffe00079a72cc <_transmit+0x8c>
fffffe00079a72b0:      	cmp	w8, #1
fffffe00079a72b4:      	b.ne	0xfffffe00079a7388 <_transmit+0x148>
fffffe00079a72b8:      	add	x8, x19, #1193
fffffe00079a72bc:      	ldrh	w8, [x8]
fffffe00079a72c0:      	cmp	w8, #192
fffffe00079a72c4:      	b.ne	0xfffffe00079a7388 <_transmit+0x148>
fffffe00079a72c8:      	mov	w9, wzr
fffffe00079a72cc:      	ldr	x8, [x19]
fffffe00079a72d0:      	str	wzr, [sp, #12]
fffffe00079a72d4:      	ldr	w10, [x19, #84]
fffffe00079a72d8:      	ldr	x8, [x8, #2832]
fffffe00079a72dc:      	lsl	w10, w10, #4
fffffe00079a72e0:      	add	x24, x8, x10
fffffe00079a72e4:      	cbz	w9, 0xfffffe00079a7334 <_transmit+0xf4>
fffffe00079a72e8:      	mov	w8, #4
fffffe00079a72ec:      	mov	x0, x19
fffffe00079a72f0:      	mov	x1, x23
fffffe00079a72f4:      	mov	x2, x22
fffffe00079a72f8:      	str	w8, [x19, #40]
fffffe00079a72fc:      	bl	0xfffffe00079a7054 <_before>
fffffe00079a7300:      	cbnz	w0, 0xfffffe00079a7328 <_transmit+0xe8>
fffffe00079a7304:      	ldr	x8, [x23]
fffffe00079a7308:      	mov	w9, #21528
fffffe00079a730c:      	movk	w9, #6146, lsl #16
fffffe00079a7310:      	add	x2, sp, #12
fffffe00079a7314:      	orr	w1, w9, #0x4
fffffe00079a7318:      	mov	x0, x22
fffffe00079a731c:      	ldr	x8, [x8, #8]
fffffe00079a7320:      	blr	x8
fffffe00079a7324:      	cbz	w0, 0xfffffe00079a7418 <_transmit+0x1d8>
fffffe00079a7328:      	mov	x0, x19
fffffe00079a732c:      	mov	w1, #5
fffffe00079a7330:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a7334:      	mov	x0, x19
fffffe00079a7338:      	mov	x1, x23
fffffe00079a733c:      	mov	x2, x22
fffffe00079a7340:      	bl	0xfffffe00079a7054 <_before>
fffffe00079a7344:      	cbnz	w0, 0xfffffe00079a7368 <_transmit+0x128>
fffffe00079a7348:      	ldr	x8, [x23]
fffffe00079a734c:      	mov	w1, #240
fffffe00079a7350:      	add	x2, sp, #12
fffffe00079a7354:      	mov	x0, x22
fffffe00079a7358:      	movk	w1, #6150, lsl #16
fffffe00079a735c:      	ldr	x8, [x8, #8]
fffffe00079a7360:      	blr	x8
fffffe00079a7364:      	cbz	w0, 0xfffffe00079a744c <_transmit+0x20c>
fffffe00079a7368:      	mov	x0, x19
fffffe00079a736c:      	mov	w1, #5
fffffe00079a7370:      	bl	0xfffffe00079a9104 <_fail>
fffffe00079a7374:      	mov	x0, x19
fffffe00079a7378:      	mov	w1, #12
fffffe00079a737c:      	bl	0xfffffe00079a9104 <_fail>
fffffe00079a7380:      	mov	w0, #-1
fffffe00079a7384:      	b	0xfffffe00079a742c <_transmit+0x1ec>
fffffe00079a7388:      	mov	w8, #1161
fffffe00079a738c:      	mov	w9, #5257
fffffe00079a7390:      	strb	wzr, [x19, x8]
fffffe00079a7394:      	add	x8, x8, #1
fffffe00079a7398:      	cmp	x8, x9
fffffe00079a739c:      	b.ne	0xfffffe00079a7390 <_transmit+0x150>
fffffe00079a73a0:      	movi.2d	v0, #0000000000000000
fffffe00079a73a4:      	adrp	x8, 0xfffffe000700a000 <_sw_rlm+0x20>
fffffe00079a73a8:      	ldr	x9, [x19, #160]
fffffe00079a73ac:      	str	wzr, [x19, #56]
fffffe00079a73b0:      	stp	xzr, xzr, [sp, #56]
fffffe00079a73b4:      	stur	q0, [sp, #24]
fffffe00079a73b8:      	stur	q0, [sp, #40]
fffffe00079a73bc:      	ldr	d0, [x8, #1640]
fffffe00079a73c0:      	str	x9, [sp, #32]
fffffe00079a73c4:      	strb	wzr, [sp, #41]
fffffe00079a73c8:      	strb	wzr, [sp, #43]
fffffe00079a73cc:      	str	d0, [sp, #16]
fffffe00079a73d0:      	strb	wzr, [sp, #45]
fffffe00079a73d4:      	strb	wzr, [sp, #47]
fffffe00079a73d8:      	strb	wzr, [sp, #49]
fffffe00079a73dc:      	cbz	x21, 0xfffffe00079a7428 <_transmit+0x1e8>
fffffe00079a73e0:      	ldr	x8, [x19, #152]
fffffe00079a73e4:      	mov	w9, #1
fffffe00079a73e8:      	add	x1, sp, #16
fffffe00079a73ec:      	mov	x0, x20
fffffe00079a73f0:      	str	x8, [sp, #24]
fffffe00079a73f4:      	str	w9, [x19, #148]
fffffe00079a73f8:      	blr	x21
fffffe00079a73fc:      	mov	w8, w0
fffffe00079a7400:      	mov	w0, wzr
fffffe00079a7404:      	str	wzr, [x19, #148]
fffffe00079a7408:      	tbz	w8, #31, 0xfffffe00079a742c <_transmit+0x1ec>
fffffe00079a740c:      	mov	x0, x19
fffffe00079a7410:      	mov	w1, #10
fffffe00079a7414:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a7418:      	ldr	w8, [sp, #12]
fffffe00079a741c:      	ldr	w9, [x19, #84]
fffffe00079a7420:      	cmp	w8, w9
fffffe00079a7424:      	b.ne	0xfffffe00079a7534 <_transmit+0x2f4>
fffffe00079a7428:      	mov	w0, wzr
fffffe00079a742c:      	ldp	x29, x30, [sp, #160]
fffffe00079a7430:      	ldp	x20, x19, [sp, #144]
fffffe00079a7434:      	ldp	x22, x21, [sp, #128]
fffffe00079a7438:      	ldp	x24, x23, [sp, #112]
fffffe00079a743c:      	ldp	x26, x25, [sp, #96]
fffffe00079a7440:      	ldp	x28, x27, [sp, #80]
fffffe00079a7444:      	add	sp, sp, #176
fffffe00079a7448:      	ret
fffffe00079a744c:      	ldr	w8, [sp, #12]
fffffe00079a7450:      	mov	w9, #3
fffffe00079a7454:      	bics	wzr, w9, w8
fffffe00079a7458:      	b.ne	0xfffffe00079a7374 <_transmit+0x134>
fffffe00079a745c:      	mov	x0, x19
fffffe00079a7460:      	mov	x1, x23
fffffe00079a7464:      	mov	x2, x22
fffffe00079a7468:      	bl	0xfffffe00079a716c <_coherent>
fffffe00079a746c:      	cbnz	w0, 0xfffffe00079a7374 <_transmit+0x134>
fffffe00079a7470:      	mov	w3, #21528
fffffe00079a7474:      	add	x4, sp, #12
fffffe00079a7478:      	mov	x0, x19
fffffe00079a747c:      	mov	x1, x23
fffffe00079a7480:      	mov	x2, x22
fffffe00079a7484:      	movk	w3, #6146, lsl #16
fffffe00079a7488:      	mov	w25, #21528
fffffe00079a748c:      	movk	w25, #6146, lsl #16
fffffe00079a7490:      	bl	0xfffffe00079a91ec <_rr>
fffffe00079a7494:      	cbnz	w0, 0xfffffe00079a7374 <_transmit+0x134>
fffffe00079a7498:      	ldr	w8, [sp, #12]
fffffe00079a749c:      	ldr	w9, [x19, #84]
fffffe00079a74a0:      	cmp	w8, w9
fffffe00079a74a4:      	b.ne	0xfffffe00079a7374 <_transmit+0x134>
fffffe00079a74a8:      	add	w3, w25, #4
fffffe00079a74ac:      	add	x4, sp, #12
fffffe00079a74b0:      	mov	x0, x19
fffffe00079a74b4:      	mov	x1, x23
fffffe00079a74b8:      	mov	x2, x22
fffffe00079a74bc:      	bl	0xfffffe00079a91ec <_rr>
fffffe00079a74c0:      	cbnz	w0, 0xfffffe00079a7374 <_transmit+0x134>
fffffe00079a74c4:      	ldr	w8, [sp, #12]
fffffe00079a74c8:      	ldr	w9, [x19, #84]
fffffe00079a74cc:      	cmp	w8, w9
fffffe00079a74d0:      	b.ne	0xfffffe00079a7374 <_transmit+0x134>
fffffe00079a74d4:      	mov	w8, #1
fffffe00079a74d8:      	mov	x0, x19
fffffe00079a74dc:      	mov	x1, x23
fffffe00079a74e0:      	mov	x2, x22
fffffe00079a74e4:      	mov	x3, x24
fffffe00079a74e8:      	mov	w4, #16
fffffe00079a74ec:      	mov	w5, #2
fffffe00079a74f0:      	str	w8, [x19, #40]
fffffe00079a74f4:      	bl	0xfffffe00079a925c <_sy>
fffffe00079a74f8:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a74fc:      	add	x26, x19, #1145
fffffe00079a7500:      	mov	x0, x19
fffffe00079a7504:      	mov	x1, x23
fffffe00079a7508:      	mov	x2, x22
fffffe00079a750c:      	mov	x3, x24
fffffe00079a7510:      	mov	x4, x26
fffffe00079a7514:      	mov	w5, #16
fffffe00079a7518:      	bl	0xfffffe00079a92f4 <_mr>
fffffe00079a751c:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a7520:      	ldrsb	w8, [x19, #1152]
fffffe00079a7524:      	tbnz	w8, #31, 0xfffffe00079a77d0 <_transmit+0x590>
fffffe00079a7528:      	mov	x0, x19
fffffe00079a752c:      	mov	w1, #13
fffffe00079a7530:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a7534:      	add	w9, w9, #1
fffffe00079a7538:      	and	w9, w9, #0x3ff
fffffe00079a753c:      	cmp	w8, w9
fffffe00079a7540:      	b.ne	0xfffffe00079a756c <_transmit+0x32c>
fffffe00079a7544:      	mov	w8, #1
fffffe00079a7548:      	mov	x0, x19
fffffe00079a754c:      	mov	x1, x23
fffffe00079a7550:      	mov	x2, x22
fffffe00079a7554:      	str	w8, [x19, #40]
fffffe00079a7558:      	bl	0xfffffe00079a7054 <_before>
fffffe00079a755c:      	cbz	w0, 0xfffffe00079a7578 <_transmit+0x338>
fffffe00079a7560:      	mov	x0, x19
fffffe00079a7564:      	mov	w1, #9
fffffe00079a7568:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a756c:      	mov	x0, x19
fffffe00079a7570:      	mov	w1, #16
fffffe00079a7574:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a7578:      	mov	x0, x19
fffffe00079a757c:      	mov	x1, x24
fffffe00079a7580:      	mov	w2, #16
fffffe00079a7584:      	mov	w3, wzr
fffffe00079a7588:      	bl	0xfffffe00079a6874 <_nst_memory_range>
fffffe00079a758c:      	cbz	w0, 0xfffffe00079a7560 <_transmit+0x320>
fffffe00079a7590:      	ldr	x8, [x23]
fffffe00079a7594:      	mov	x0, x22
fffffe00079a7598:      	mov	x1, x24
fffffe00079a759c:      	mov	w2, #16
fffffe00079a75a0:      	mov	w3, #2
fffffe00079a75a4:      	ldr	x8, [x8, #40]
fffffe00079a75a8:      	blr	x8
fffffe00079a75ac:      	cbnz	w0, 0xfffffe00079a7560 <_transmit+0x320>
fffffe00079a75b0:      	add	x4, x19, #1145
fffffe00079a75b4:      	mov	x0, x19
fffffe00079a75b8:      	mov	x1, x23
fffffe00079a75bc:      	mov	x2, x22
fffffe00079a75c0:      	mov	x3, x24
fffffe00079a75c4:      	mov	w5, #16
fffffe00079a75c8:      	bl	0xfffffe00079a92f4 <_mr>
fffffe00079a75cc:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a75d0:      	ldrsb	w8, [x19, #1152]
fffffe00079a75d4:      	tbz	w8, #31, 0xfffffe00079a7428 <_transmit+0x1e8>
fffffe00079a75d8:      	ldp	x1, x2, [x19, #152]
fffffe00079a75dc:      	movi.2d	v0, #0000000000000000
fffffe00079a75e0:      	add	x24, x19, #1161
fffffe00079a75e4:      	ldr	w4, [x19, #108]
fffffe00079a75e8:      	add	x0, sp, #16
fffffe00079a75ec:      	ldr	w5, [x19, #128]
fffffe00079a75f0:      	mov	x3, x24
fffffe00079a75f4:      	mov	w6, #2
fffffe00079a75f8:      	str	xzr, [sp, #64]
fffffe00079a75fc:      	stp	q0, q0, [sp, #32]
fffffe00079a7600:      	str	q0, [sp, #16]
fffffe00079a7604:      	bl	0xfffffe00079aa24c <_nstd_tx>
fffffe00079a7608:      	add	x0, sp, #16
fffffe00079a760c:      	bl	0xfffffe00079aa590 <_nstd_log>
fffffe00079a7610:      	ldr	w8, [x19, #112]
fffffe00079a7614:      	cmp	w8, #28
fffffe00079a7618:      	b.ne	0xfffffe00079a7630 <_transmit+0x3f0>
fffffe00079a761c:      	ldrb	w8, [x19, #1227]
fffffe00079a7620:      	add	x26, x19, #960
fffffe00079a7624:      	cmp	w8, #1
fffffe00079a7628:      	cset	w27, eq
fffffe00079a762c:      	b	0xfffffe00079a7684 <_transmit+0x444>
fffffe00079a7630:      	ldr	w9, [x19, #960]
fffffe00079a7634:      	add	x26, x19, #960
fffffe00079a7638:      	cmp	w9, #2
fffffe00079a763c:      	b.ne	0xfffffe00079a7658 <_transmit+0x418>
fffffe00079a7640:      	cmp	w8, #22
fffffe00079a7644:      	b.ne	0xfffffe00079a7658 <_transmit+0x418>
fffffe00079a7648:      	mov	w8, #1
fffffe00079a764c:      	mov	w27, wzr
fffffe00079a7650:      	str	w8, [x19, #1128]
fffffe00079a7654:      	b	0xfffffe00079a7684 <_transmit+0x444>
fffffe00079a7658:      	cmp	w8, #7
fffffe00079a765c:      	b.ne	0xfffffe00079a7680 <_transmit+0x440>
fffffe00079a7660:      	ldrb	w8, [x19, #1225]
fffffe00079a7664:      	cbz	w8, 0xfffffe00079a7680 <_transmit+0x440>
fffffe00079a7668:      	ldrb	w8, [x19, #1227]
fffffe00079a766c:      	cbz	w8, 0xfffffe00079a77f4 <_transmit+0x5b4>
fffffe00079a7670:      	mov	w8, #1
fffffe00079a7674:      	mov	w27, wzr
fffffe00079a7678:      	str	w8, [x19, #936]
fffffe00079a767c:      	b	0xfffffe00079a7684 <_transmit+0x444>
fffffe00079a7680:      	mov	w27, wzr
fffffe00079a7684:      	ldr	w25, [x19, #108]
fffffe00079a7688:      	mov	w8, #1161
fffffe00079a768c:      	mov	w9, #5257
fffffe00079a7690:      	strb	wzr, [x19, x8]
fffffe00079a7694:      	add	x8, x8, #1
fffffe00079a7698:      	cmp	x8, x9
fffffe00079a769c:      	b.ne	0xfffffe00079a7690 <_transmit+0x450>
fffffe00079a76a0:      	mov	w8, #2
fffffe00079a76a4:      	ldr	x9, [x19]
fffffe00079a76a8:      	mov	x0, x19
fffffe00079a76ac:      	mov	x1, x23
fffffe00079a76b0:      	mov	x2, x22
fffffe00079a76b4:      	mov	x4, x24
fffffe00079a76b8:      	str	w8, [x19, #40]
fffffe00079a76bc:      	mov	w5, w25
fffffe00079a76c0:      	ldr	x3, [x9, #2856]
fffffe00079a76c4:      	bl	0xfffffe00079a9388 <_mw>
fffffe00079a76c8:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a76cc:      	ldr	x8, [x19]
fffffe00079a76d0:      	mov	x0, x19
fffffe00079a76d4:      	mov	x1, x23
fffffe00079a76d8:      	mov	x2, x22
fffffe00079a76dc:      	mov	w4, w25
fffffe00079a76e0:      	mov	w5, #1
fffffe00079a76e4:      	ldr	x3, [x8, #2856]
fffffe00079a76e8:      	bl	0xfffffe00079a925c <_sy>
fffffe00079a76ec:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a76f0:      	ldr	w8, [x19, #84]
fffffe00079a76f4:      	ldr	x9, [x19]
fffffe00079a76f8:      	add	w8, w8, #1
fffffe00079a76fc:      	and	w8, w8, #0x3ff
fffffe00079a7700:      	str	w8, [x9, #2864]
fffffe00079a7704:      	ldr	x8, [x19]
fffffe00079a7708:      	ldr	w9, [x8, #2864]
fffffe00079a770c:      	str	w9, [x19, #84]
fffffe00079a7710:      	ldr	w9, [x8, #1920]
fffffe00079a7714:      	add	w9, w9, #1
fffffe00079a7718:      	str	w9, [x8, #1920]
fffffe00079a771c:      	ldr	w8, [x19, #128]
fffffe00079a7720:      	cbnz	w8, 0xfffffe00079a7734 <_transmit+0x4f4>
fffffe00079a7724:      	ldr	x8, [x19]
fffffe00079a7728:      	ldrb	w9, [x8, #2876]
fffffe00079a772c:      	add	w9, w9, #1
fffffe00079a7730:      	strb	w9, [x8, #2876]
fffffe00079a7734:      	ldr	x8, [x19]
fffffe00079a7738:      	mov	w9, #1
fffffe00079a773c:      	str	wzr, [x8, #1908]
fffffe00079a7740:      	ldr	w8, [x19, #96]
fffffe00079a7744:      	str	w9, [x19, #136]
fffffe00079a7748:      	str	wzr, [x19, #56]
fffffe00079a774c:      	str	wzr, [x19, #40]
fffffe00079a7750:      	add	w8, w8, #1
fffffe00079a7754:      	str	w8, [x19, #96]
fffffe00079a7758:      	cbz	w27, 0xfffffe00079a77a4 <_transmit+0x564>
fffffe00079a775c:      	movi.2d	v0, #0000000000000000
fffffe00079a7760:      	mov	w8, #12
fffffe00079a7764:      	ldr	x9, [x19, #160]
fffffe00079a7768:      	add	x3, sp, #16
fffffe00079a776c:      	ldrb	w10, [x19, #984]
fffffe00079a7770:      	mov	x0, x19
fffffe00079a7774:      	mov	x1, x21
fffffe00079a7778:      	mov	x2, x20
fffffe00079a777c:      	strb	wzr, [x19, #985]
fffffe00079a7780:      	str	xzr, [x19, #976]
fffffe00079a7784:      	str	xzr, [sp, #64]
fffffe00079a7788:      	stp	q0, q0, [sp, #16]
fffffe00079a778c:      	str	q0, [sp, #48]
fffffe00079a7790:      	str	w8, [sp, #16]
fffffe00079a7794:      	str	x9, [sp, #32]
fffffe00079a7798:      	strb	w10, [sp, #46]
fffffe00079a779c:      	bl	0xfffffe00079a7e9c <_emit>
fffffe00079a77a0:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a77a4:      	ldr	w8, [x19, #128]
fffffe00079a77a8:      	cbnz	w8, 0xfffffe00079a7428 <_transmit+0x1e8>
fffffe00079a77ac:      	ldr	w8, [x26]
fffffe00079a77b0:      	cbz	w8, 0xfffffe00079a7828 <_transmit+0x5e8>
fffffe00079a77b4:      	cmp	w8, #1
fffffe00079a77b8:      	b.ne	0xfffffe00079a7428 <_transmit+0x1e8>
fffffe00079a77bc:      	ldr	w8, [x19, #956]
fffffe00079a77c0:      	mov	w0, wzr
fffffe00079a77c4:      	add	w8, w8, #1
fffffe00079a77c8:      	str	w8, [x19, #956]
fffffe00079a77cc:      	b	0xfffffe00079a742c <_transmit+0x1ec>
fffffe00079a77d0:      	ldr	x8, [x19]
fffffe00079a77d4:      	mov	x0, x26
fffffe00079a77d8:      	ldr	w2, [x19, #108]
fffffe00079a77dc:      	ldr	x1, [x8, #2856]
fffffe00079a77e0:      	bl	0xfffffe00079902a8 <_nwt_tx_desc>
fffffe00079a77e4:      	cbz	w0, 0xfffffe00079a780c <_transmit+0x5cc>
fffffe00079a77e8:      	mov	x0, x19
fffffe00079a77ec:      	mov	w1, #14
fffffe00079a77f0:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a77f4:      	mov	w27, wzr
fffffe00079a77f8:      	mov	w8, #1
fffffe00079a77fc:      	ldrb	w9, [x19, #1237]
fffffe00079a7800:      	str	w8, [x19, #940]
fffffe00079a7804:      	strb	w9, [x19, #1144]
fffffe00079a7808:      	b	0xfffffe00079a7684 <_transmit+0x444>
fffffe00079a780c:      	ldr	w8, [x19, #68]
fffffe00079a7810:      	cbz	w8, 0xfffffe00079a781c <_transmit+0x5dc>
fffffe00079a7814:      	ldr	w9, [x19, #72]
fffffe00079a7818:      	cbz	w9, 0xfffffe00079a786c <_transmit+0x62c>
fffffe00079a781c:      	mov	x0, x19
fffffe00079a7820:      	mov	w1, #35
fffffe00079a7824:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a7828:      	movi.2d	v0, #0000000000000000
fffffe00079a782c:      	mov	w8, #1
fffffe00079a7830:      	ldp	w10, w11, [x19, #112]
fffffe00079a7834:      	ldr	x9, [x19, #160]
fffffe00079a7838:      	add	x3, sp, #16
fffffe00079a783c:      	mov	x0, x19
fffffe00079a7840:      	mov	x1, x21
fffffe00079a7844:      	mov	x2, x20
fffffe00079a7848:      	str	xzr, [sp, #64]
fffffe00079a784c:      	stp	q0, q0, [sp, #16]
fffffe00079a7850:      	str	q0, [sp, #48]
fffffe00079a7854:      	str	w8, [sp, #16]
fffffe00079a7858:      	str	x9, [sp, #32]
fffffe00079a785c:      	strb	w10, [sp, #40]
fffffe00079a7860:      	strb	w11, [sp, #41]
fffffe00079a7864:      	bl	0xfffffe00079a7e9c <_emit>
fffffe00079a7868:      	b	0xfffffe00079a742c <_transmit+0x1ec>
fffffe00079a786c:      	ldr	w9, [x19, #76]
fffffe00079a7870:      	sub	w8, w8, #1
fffffe00079a7874:      	mov	w10, #1
fffffe00079a7878:      	mov	w11, #2
fffffe00079a787c:      	add	x25, x19, #1161
fffffe00079a7880:      	ldr	w5, [x19, #108]
fffffe00079a7884:      	add	w9, w9, #1
fffffe00079a7888:      	mov	x0, x19
fffffe00079a788c:      	stp	w8, w10, [x19, #68]
fffffe00079a7890:      	ldr	x8, [x19]
fffffe00079a7894:      	str	w11, [x19, #40]
fffffe00079a7898:      	mov	x1, x23
fffffe00079a789c:      	str	w9, [x19, #76]
fffffe00079a78a0:      	mov	x2, x22
fffffe00079a78a4:      	ldr	x3, [x8, #2856]
fffffe00079a78a8:      	mov	x4, x25
fffffe00079a78ac:      	bl	0xfffffe00079a9388 <_mw>
fffffe00079a78b0:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a78b4:      	ldr	x8, [x19]
fffffe00079a78b8:      	mov	x0, x19
fffffe00079a78bc:      	ldr	w4, [x19, #108]
fffffe00079a78c0:      	mov	x1, x23
fffffe00079a78c4:      	mov	x2, x22
fffffe00079a78c8:      	mov	w5, #1
fffffe00079a78cc:      	ldr	x3, [x8, #2856]
fffffe00079a78d0:      	bl	0xfffffe00079a925c <_sy>
fffffe00079a78d4:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a78d8:      	mov	x0, x19
fffffe00079a78dc:      	mov	x1, x23
fffffe00079a78e0:      	mov	x2, x22
fffffe00079a78e4:      	mov	x3, x24
fffffe00079a78e8:      	mov	x4, x26
fffffe00079a78ec:      	mov	w5, #16
fffffe00079a78f0:      	bl	0xfffffe00079a9388 <_mw>
fffffe00079a78f4:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a78f8:      	mov	x0, x19
fffffe00079a78fc:      	mov	x1, x23
fffffe00079a7900:      	mov	x2, x22
fffffe00079a7904:      	mov	x3, x24
fffffe00079a7908:      	mov	w4, #16
fffffe00079a790c:      	mov	w5, #1
fffffe00079a7910:      	bl	0xfffffe00079a925c <_sy>
fffffe00079a7914:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a7918:      	ldr	x8, [x19, #168]
fffffe00079a791c:      	mov	x9, #-50880
fffffe00079a7920:      	movk	x9, #65490, lsl #16
fffffe00079a7924:      	cmp	x8, x9
fffffe00079a7928:      	b.lo	0xfffffe00079a7938 <_transmit+0x6f8>
fffffe00079a792c:      	mov	x0, x19
fffffe00079a7930:      	mov	w1, #15
fffffe00079a7934:      	b	0xfffffe00079a737c <_transmit+0x13c>
fffffe00079a7938:      	add	x8, x8, #732, lsl #12
fffffe00079a793c:      	mov	w9, #3
fffffe00079a7940:      	add	x8, x8, #1728
fffffe00079a7944:      	ldr	x10, [x19]
fffffe00079a7948:      	mov	w26, #1
fffffe00079a794c:      	mov	w3, #21528
fffffe00079a7950:      	str	w9, [x19, #40]
fffffe00079a7954:      	mov	x0, x19
fffffe00079a7958:      	str	x8, [x19, #184]
fffffe00079a795c:      	mov	x1, x23
fffffe00079a7960:      	str	w26, [x10, #1908]
fffffe00079a7964:      	mov	x2, x22
fffffe00079a7968:      	ldr	w8, [x19, #84]
fffffe00079a796c:      	movk	w3, #6146, lsl #16
fffffe00079a7970:      	add	w8, w8, #1
fffffe00079a7974:      	and	w4, w8, #0x3ff
fffffe00079a7978:      	bl	0xfffffe00079a942c <_rw>
fffffe00079a797c:      	cbnz	w0, 0xfffffe00079a7380 <_transmit+0x140>
fffffe00079a7980:      	ldr	w8, [x19, #92]
fffffe00079a7984:      	str	w26, [x19, #132]
fffffe00079a7988:      	ldr	w9, [x19, #68]
fffffe00079a798c:      	ldr	w10, [x19, #80]
fffffe00079a7990:      	add	w8, w8, #1
fffffe00079a7994:      	ldr	w5, [x19, #128]
fffffe00079a7998:      	add	w9, w9, #1
fffffe00079a799c:      	add	w10, w10, #1
fffffe00079a79a0:      	str	w8, [x19, #92]
fffffe00079a79a4:      	stp	w9, wzr, [x19, #68]
fffffe00079a79a8:      	str	w10, [x19, #80]
fffffe00079a79ac:      	cbz	w5, 0xfffffe00079a79b8 <_transmit+0x778>
fffffe00079a79b0:      	mov	w8, #1
fffffe00079a79b4:      	str	w8, [x19, #60]
fffffe00079a79b8:      	ldp	x1, x2, [x19, #152]
fffffe00079a79bc:      	movi.2d	v0, #0000000000000000
fffffe00079a79c0:      	add	x0, sp, #16
fffffe00079a79c4:      	ldr	w4, [x19, #108]
fffffe00079a79c8:      	mov	x3, x25
fffffe00079a79cc:      	mov	w6, wzr
fffffe00079a79d0:      	str	xzr, [sp, #64]
fffffe00079a79d4:      	stp	q0, q0, [sp, #32]
fffffe00079a79d8:      	str	q0, [sp, #16]
fffffe00079a79dc:      	bl	0xfffffe00079aa24c <_nstd_tx>
fffffe00079a79e0:      	add	x0, sp, #16
fffffe00079a79e4:      	bl	0xfffffe00079aa590 <_nstd_log>
fffffe00079a79e8:      	ldp	x1, x2, [x19, #152]
fffffe00079a79ec:      	add	x0, sp, #16
fffffe00079a79f0:      	mov	x3, x25
fffffe00079a79f4:      	ldr	w4, [x19, #108]
fffffe00079a79f8:      	mov	w6, #1
fffffe00079a79fc:      	ldr	w5, [x19, #128]
fffffe00079a7a00:      	bl	0xfffffe00079aa24c <_nstd_tx>
fffffe00079a7a04:      	add	x0, sp, #16
fffffe00079a7a08:      	bl	0xfffffe00079aa590 <_nstd_log>
fffffe00079a7a0c:      	ldr	w8, [x19, #112]
fffffe00079a7a10:      	cmp	w8, #7
fffffe00079a7a14:      	b.eq	0xfffffe00079a7a50 <_transmit+0x810>
fffffe00079a7a18:      	cmp	w8, #28
fffffe00079a7a1c:      	b.eq	0xfffffe00079a7a6c <_transmit+0x82c>
fffffe00079a7a20:      	cmp	w8, #19
fffffe00079a7a24:      	b.ne	0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a28:      	ldrb	w8, [x19, #1245]
fffffe00079a7a2c:      	cmp	w8, #3
fffffe00079a7a30:      	b.ne	0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a34:      	ldrb	w8, [x19, #1276]
fffffe00079a7a38:      	cbz	w8, 0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a3c:      	mov	w8, #1
fffffe00079a7a40:      	ldr	w9, [x19, #116]
fffffe00079a7a44:      	str	w8, [x19, #944]
fffffe00079a7a48:      	str	w9, [x19, #948]
fffffe00079a7a4c:      	b	0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a50:      	ldrb	w8, [x19, #1225]
fffffe00079a7a54:      	cbz	w8, 0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a58:      	ldrb	w8, [x19, #1227]
fffffe00079a7a5c:      	cbz	w8, 0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a60:      	mov	w8, #1
fffffe00079a7a64:      	str	w8, [x19, #932]
fffffe00079a7a68:      	b	0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a6c:      	ldrb	w8, [x19, #1227]
fffffe00079a7a70:      	cbnz	w8, 0xfffffe00079a72e8 <_transmit+0xa8>
fffffe00079a7a74:      	ldrb	w8, [x19, #1226]
fffffe00079a7a78:      	mov	w9, #1
fffffe00079a7a7c:      	strb	w8, [x19, #984]
fffffe00079a7a80:      	strb	w9, [x19, #985]
fffffe00079a7a84:      	b	0xfffffe00079a72e8 <_transmit+0xa8>
