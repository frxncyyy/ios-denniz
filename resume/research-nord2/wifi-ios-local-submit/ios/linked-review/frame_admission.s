
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7338 <_frame>:
fffffe00079a7338:      	sub	sp, sp, #160
fffffe00079a733c:      	stp	x24, x23, [sp, #96]
fffffe00079a7340:      	stp	x22, x21, [sp, #112]
fffffe00079a7344:      	stp	x20, x19, [sp, #128]
fffffe00079a7348:      	stp	x29, x30, [sp, #144]
fffffe00079a734c:      	add	x29, sp, #144
fffffe00079a7350:      	adrp	x8, 0xfffffe0007c90000 <_nst_step+0x11c>
fffffe00079a7354:      	mov	w20, w4
fffffe00079a7358:      	mov	x19, x0
fffffe00079a735c:      	mov	x21, x3
fffffe00079a7360:      	mov	x23, x2
fffffe00079a7364:      	mov	x22, x1
fffffe00079a7368:      	nop
fffffe00079a736c:      	ldr	x8, [x8, #3376]
fffffe00079a7370:      	stur	x8, [x29, #-56]
fffffe00079a7374:      	cbz	x0, 0xfffffe00079a73b0 <_frame+0x78>
fffffe00079a7378:      	ldr	w8, [x19, #28]
fffffe00079a737c:      	cbz	w8, 0xfffffe00079a73b4 <_frame+0x7c>
fffffe00079a7380:      	ldr	x8, [x19]
fffffe00079a7384:      	cbz	x8, 0xfffffe00079a73b4 <_frame+0x7c>
fffffe00079a7388:      	ldr	x9, [x19, #8]
fffffe00079a738c:      	cbz	x9, 0xfffffe00079a73b0 <_frame+0x78>
fffffe00079a7390:      	ldr	x9, [x19, #16]
fffffe00079a7394:      	cbz	x9, 0xfffffe00079a73b0 <_frame+0x78>
fffffe00079a7398:      	ldr	w9, [x19, #44]
fffffe00079a739c:      	cbnz	w9, 0xfffffe00079a73b0 <_frame+0x78>
fffffe00079a73a0:      	ldr	w9, [x8, #12]
fffffe00079a73a4:      	cbnz	w9, 0xfffffe00079a73b0 <_frame+0x78>
fffffe00079a73a8:      	ldr	w8, [x8, #1876]
fffffe00079a73ac:      	cbz	w8, 0xfffffe00079a75d8 <_frame+0x2a0>
fffffe00079a73b0:      	mov	w8, wzr
fffffe00079a73b4:      	mov	w0, #-1
fffffe00079a73b8:      	sub	x9, x21, #2
fffffe00079a73bc:      	cmp	x9, #1566
fffffe00079a73c0:      	b.hi	0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a73c4:      	cbz	x23, 0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a73c8:      	cbz	x22, 0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a73cc:      	cbz	w8, 0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a73d0:      	ldr	w8, [x19, #56]
fffffe00079a73d4:      	cbnz	w8, 0xfffffe00079a73e8 <_frame+0xb0>
fffffe00079a73d8:      	ldr	w8, [x19, #60]
fffffe00079a73dc:      	cbnz	w8, 0xfffffe00079a73e8 <_frame+0xb0>
fffffe00079a73e0:      	ldr	w8, [x19, #952]
fffffe00079a73e4:      	cbz	w8, 0xfffffe00079a741c <_frame+0xe4>
fffffe00079a73e8:      	mov	w0, #1
fffffe00079a73ec:      	adrp	x8, 0xfffffe0007c90000 <_nst_step+0x1b8>
fffffe00079a73f0:      	nop
fffffe00079a73f4:      	ldur	x9, [x29, #-56]
fffffe00079a73f8:      	ldr	x8, [x8, #3376]
fffffe00079a73fc:      	cmp	x8, x9
fffffe00079a7400:      	b.ne	0xfffffe00079a7834 <_frame+0x4fc>
fffffe00079a7404:      	ldp	x29, x30, [sp, #144]
fffffe00079a7408:      	ldp	x20, x19, [sp, #128]
fffffe00079a740c:      	ldp	x22, x21, [sp, #112]
fffffe00079a7410:      	ldp	x24, x23, [sp, #96]
fffffe00079a7414:      	add	sp, sp, #160
fffffe00079a7418:      	ret
fffffe00079a741c:      	ldr	w8, [x19, #24]
fffffe00079a7420:      	cbz	w8, 0xfffffe00079a743c <_frame+0x104>
fffffe00079a7424:      	cmp	w20, #1
fffffe00079a7428:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a742c:      	ldrh	w8, [x23]
fffffe00079a7430:      	cmp	w8, #192
fffffe00079a7434:      	b.eq	0xfffffe00079a7444 <_frame+0x10c>
fffffe00079a7438:      	b	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a743c:      	cmp	w20, #1
fffffe00079a7440:      	b.ne	0xfffffe00079a75e8 <_frame+0x2b0>
fffffe00079a7444:      	ldr	w8, [x19, #104]
fffffe00079a7448:      	sub	w9, w8, #127
fffffe00079a744c:      	cmn	w9, #126
fffffe00079a7450:      	b.lo	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a7454:      	ldrh	w9, [x23]
fffffe00079a7458:      	ldur	q0, [x19, #212]
fffffe00079a745c:      	ldr	w10, [x19, #228]
fffffe00079a7460:      	cmp	w9, #176
fffffe00079a7464:      	str	q0, [sp, #16]
fffffe00079a7468:      	str	w10, [sp, #32]
fffffe00079a746c:      	b.eq	0xfffffe00079a7474 <_frame+0x13c>
fffffe00079a7470:      	cbnz	w9, 0xfffffe00079a747c <_frame+0x144>
fffffe00079a7474:      	mov	w9, #16
fffffe00079a7478:      	strb	w9, [sp, #18]
fffffe00079a747c:      	movi.2d	v0, #0000000000000000
fffffe00079a7480:      	strb	w5, [sp, #32]
fffffe00079a7484:      	add	x0, sp, #48
fffffe00079a7488:      	add	x2, sp, #8
fffffe00079a748c:      	add	x3, sp, #16
fffffe00079a7490:      	mov	w1, #32
fffffe00079a7494:      	mov	x4, x23
fffffe00079a7498:      	mov	x5, x21
fffffe00079a749c:      	strb	w8, [sp, #19]
fffffe00079a74a0:      	str	xzr, [sp, #8]
fffffe00079a74a4:      	stp	q0, q0, [sp, #48]
fffffe00079a74a8:      	bl	0xfffffe00079a63c0 <_sw_management_txd>
fffffe00079a74ac:      	mov	w8, w0
fffffe00079a74b0:      	mov	w0, #-1
fffffe00079a74b4:      	cmp	x21, #24
fffffe00079a74b8:      	b.lo	0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a74bc:      	cbnz	w8, 0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a74c0:      	ldr	x8, [sp, #8]
fffffe00079a74c4:      	cmp	x8, #32
fffffe00079a74c8:      	b.ne	0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a74cc:      	ldrb	w8, [x23, #4]
fffffe00079a74d0:      	ldrb	w9, [x19, #198]
fffffe00079a74d4:      	cmp	w8, w9
fffffe00079a74d8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a74dc:      	ldrb	w9, [x23, #5]
fffffe00079a74e0:      	ldrb	w10, [x19, #199]
fffffe00079a74e4:      	cmp	w9, w10
fffffe00079a74e8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a74ec:      	ldrb	w10, [x23, #6]
fffffe00079a74f0:      	ldrb	w11, [x19, #200]
fffffe00079a74f4:      	cmp	w10, w11
fffffe00079a74f8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a74fc:      	ldrb	w11, [x23, #7]
fffffe00079a7500:      	ldrb	w12, [x19, #201]
fffffe00079a7504:      	cmp	w11, w12
fffffe00079a7508:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a750c:      	ldrb	w12, [x23, #8]
fffffe00079a7510:      	ldrb	w13, [x19, #202]
fffffe00079a7514:      	cmp	w12, w13
fffffe00079a7518:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a751c:      	ldrb	w13, [x23, #9]
fffffe00079a7520:      	ldrb	w14, [x19, #203]
fffffe00079a7524:      	cmp	w13, w14
fffffe00079a7528:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a752c:      	ldrb	w14, [x23, #10]
fffffe00079a7530:      	ldrb	w15, [x19, #192]
fffffe00079a7534:      	cmp	w14, w15
fffffe00079a7538:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a753c:      	ldrb	w14, [x23, #11]
fffffe00079a7540:      	ldrb	w15, [x19, #193]
fffffe00079a7544:      	cmp	w14, w15
fffffe00079a7548:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a754c:      	ldrb	w14, [x23, #12]
fffffe00079a7550:      	ldrb	w15, [x19, #194]
fffffe00079a7554:      	cmp	w14, w15
fffffe00079a7558:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a755c:      	ldrb	w14, [x23, #13]
fffffe00079a7560:      	ldrb	w15, [x19, #195]
fffffe00079a7564:      	cmp	w14, w15
fffffe00079a7568:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a756c:      	ldrb	w14, [x23, #14]
fffffe00079a7570:      	ldrb	w15, [x19, #196]
fffffe00079a7574:      	cmp	w14, w15
fffffe00079a7578:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a757c:      	ldrb	w14, [x23, #15]
fffffe00079a7580:      	ldrb	w15, [x19, #197]
fffffe00079a7584:      	cmp	w14, w15
fffffe00079a7588:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a758c:      	ldrb	w14, [x23, #16]
fffffe00079a7590:      	cmp	w14, w8
fffffe00079a7594:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a7598:      	ldrb	w8, [x23, #17]
fffffe00079a759c:      	cmp	w8, w9
fffffe00079a75a0:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a75a4:      	ldrb	w8, [x23, #18]
fffffe00079a75a8:      	cmp	w8, w10
fffffe00079a75ac:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a75b0:      	ldrb	w8, [x23, #19]
fffffe00079a75b4:      	cmp	w8, w11
fffffe00079a75b8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a75bc:      	ldrb	w8, [x23, #20]
fffffe00079a75c0:      	cmp	w8, w12
fffffe00079a75c4:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a75c8:      	ldrb	w8, [x23, #21]
fffffe00079a75cc:      	cmp	w8, w13
fffffe00079a75d0:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a75d4:      	b	0xfffffe00079a771c <_frame+0x3e4>
fffffe00079a75d8:      	ldr	w8, [x19, #148]
fffffe00079a75dc:      	cmp	w8, #0
fffffe00079a75e0:      	cset	w8, eq
fffffe00079a75e4:      	b	0xfffffe00079a73b4 <_frame+0x7c>
fffffe00079a75e8:      	ldur	q0, [x19, #212]
fffffe00079a75ec:      	mov	w9, #127
fffffe00079a75f0:      	ldr	w8, [x19, #228]
fffffe00079a75f4:      	add	x0, sp, #48
fffffe00079a75f8:      	add	x2, sp, #8
fffffe00079a75fc:      	add	x3, sp, #16
fffffe00079a7600:      	str	q0, [sp, #16]
fffffe00079a7604:      	mov	w1, #32
fffffe00079a7608:      	movi.2d	v0, #0000000000000000
fffffe00079a760c:      	str	w8, [sp, #32]
fffffe00079a7610:      	strb	w5, [sp, #32]
fffffe00079a7614:      	mov	x4, x23
fffffe00079a7618:      	mov	x5, x21
fffffe00079a761c:      	cmp	w20, #2
fffffe00079a7620:      	strb	w9, [sp, #19]
fffffe00079a7624:      	str	xzr, [sp, #8]
fffffe00079a7628:      	stp	q0, q0, [sp, #48]
fffffe00079a762c:      	b.ne	0xfffffe00079a7638 <_frame+0x300>
fffffe00079a7630:      	bl	0xfffffe00079a6734 <_sw_security_txd>
fffffe00079a7634:      	b	0xfffffe00079a763c <_frame+0x304>
fffffe00079a7638:      	bl	0xfffffe00079a673c <_sw_data_txd>
fffffe00079a763c:      	mov	w8, w0
fffffe00079a7640:      	mov	w0, #-1
fffffe00079a7644:      	cbnz	w8, 0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a7648:      	ldr	x8, [sp, #8]
fffffe00079a764c:      	cmp	x8, #32
fffffe00079a7650:      	b.ne	0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a7654:      	ldrb	w8, [x23, #6]
fffffe00079a7658:      	ldrb	w9, [x19, #192]
fffffe00079a765c:      	cmp	w8, w9
fffffe00079a7660:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a7664:      	ldrb	w8, [x23, #7]
fffffe00079a7668:      	ldrb	w9, [x19, #193]
fffffe00079a766c:      	cmp	w8, w9
fffffe00079a7670:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a7674:      	ldrb	w8, [x23, #8]
fffffe00079a7678:      	ldrb	w9, [x19, #194]
fffffe00079a767c:      	cmp	w8, w9
fffffe00079a7680:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a7684:      	ldrb	w8, [x23, #9]
fffffe00079a7688:      	ldrb	w9, [x19, #195]
fffffe00079a768c:      	cmp	w8, w9
fffffe00079a7690:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a7694:      	ldrb	w8, [x23, #10]
fffffe00079a7698:      	ldrb	w9, [x19, #196]
fffffe00079a769c:      	cmp	w8, w9
fffffe00079a76a0:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a76a4:      	ldrb	w8, [x23, #11]
fffffe00079a76a8:      	ldrb	w9, [x19, #197]
fffffe00079a76ac:      	cmp	w8, w9
fffffe00079a76b0:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a76b4:      	cmp	w20, #2
fffffe00079a76b8:      	b.ne	0xfffffe00079a771c <_frame+0x3e4>
fffffe00079a76bc:      	ldrb	w8, [x23]
fffffe00079a76c0:      	ldrb	w9, [x19, #198]
fffffe00079a76c4:      	cmp	w8, w9
fffffe00079a76c8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a76cc:      	ldrb	w8, [x23, #1]
fffffe00079a76d0:      	ldrb	w9, [x19, #199]
fffffe00079a76d4:      	cmp	w8, w9
fffffe00079a76d8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a76dc:      	ldrb	w8, [x23, #2]
fffffe00079a76e0:      	ldrb	w9, [x19, #200]
fffffe00079a76e4:      	cmp	w8, w9
fffffe00079a76e8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a76ec:      	ldrb	w8, [x23, #3]
fffffe00079a76f0:      	ldrb	w9, [x19, #201]
fffffe00079a76f4:      	cmp	w8, w9
fffffe00079a76f8:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a76fc:      	ldrb	w8, [x23, #4]
fffffe00079a7700:      	ldrb	w9, [x19, #202]
fffffe00079a7704:      	cmp	w8, w9
fffffe00079a7708:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a770c:      	ldrb	w8, [x23, #5]
fffffe00079a7710:      	ldrb	w9, [x19, #203]
fffffe00079a7714:      	cmp	w8, w9
fffffe00079a7718:      	b.ne	0xfffffe00079a77a8 <_frame+0x470>
fffffe00079a771c:      	mov	x8, xzr
fffffe00079a7720:      	add	x9, x19, #1161
fffffe00079a7724:      	strb	wzr, [x9, x8]
fffffe00079a7728:      	add	x8, x8, #1
fffffe00079a772c:      	cmp	x8, #1, lsl #12
fffffe00079a7730:      	b.ne	0xfffffe00079a7724 <_frame+0x3ec>
fffffe00079a7734:      	ldp	q0, q1, [sp, #48]
fffffe00079a7738:      	stp	q0, q1, [x9]
fffffe00079a773c:      	cbz	x21, 0xfffffe00079a7774 <_frame+0x43c>
fffffe00079a7740:      	add	x8, x19, #1193
fffffe00079a7744:      	cmp	x21, #8
fffffe00079a7748:      	b.lo	0xfffffe00079a775c <_frame+0x424>
fffffe00079a774c:      	sub	x10, x19, x23
fffffe00079a7750:      	add	x10, x10, #1193
fffffe00079a7754:      	cmp	x10, #32
fffffe00079a7758:      	b.hs	0xfffffe00079a77b0 <_frame+0x478>
fffffe00079a775c:      	mov	x9, x23
fffffe00079a7760:      	mov	x10, x21
fffffe00079a7764:      	ldrb	w11, [x9], #1
fffffe00079a7768:      	subs	x10, x10, #1
fffffe00079a776c:      	strb	w11, [x8], #1
fffffe00079a7770:      	b.ne	0xfffffe00079a7764 <_frame+0x42c>
fffffe00079a7774:      	ldrb	w8, [sp, #19]
fffffe00079a7778:      	mov	w0, wzr
fffffe00079a777c:      	ldrb	w9, [sp, #16]
fffffe00079a7780:      	add	w10, w21, #32
fffffe00079a7784:      	str	x22, [x19, #160]
fffffe00079a7788:      	str	xzr, [x19, #136]
fffffe00079a778c:      	stp	w8, w9, [x19, #120]
fffffe00079a7790:      	mov	w8, #1
fffffe00079a7794:      	stp	w10, wzr, [x19, #108]
fffffe00079a7798:      	stp	w20, wzr, [x19, #128]
fffffe00079a779c:      	str	w8, [x19, #56]
fffffe00079a77a0:      	str	wzr, [x19, #960]
fffffe00079a77a4:      	b	0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a77a8:      	mov	w0, #-1
fffffe00079a77ac:      	b	0xfffffe00079a73ec <_frame+0xb4>
fffffe00079a77b0:      	cmp	x21, #32
fffffe00079a77b4:      	b.hs	0xfffffe00079a77c0 <_frame+0x488>
fffffe00079a77b8:      	mov	x11, xzr
fffffe00079a77bc:      	b	0xfffffe00079a77f8 <_frame+0x4c0>
fffffe00079a77c0:      	and	x11, x21, #0xffffffffffffffe0
fffffe00079a77c4:      	add	x9, x9, #48
fffffe00079a77c8:      	add	x10, x23, #16
fffffe00079a77cc:      	mov	x12, x11
fffffe00079a77d0:      	ldp	q0, q1, [x10, #-16]
fffffe00079a77d4:      	add	x10, x10, #32
fffffe00079a77d8:      	subs	x12, x12, #32
fffffe00079a77dc:      	stp	q0, q1, [x9, #-16]
fffffe00079a77e0:      	add	x9, x9, #32
fffffe00079a77e4:      	b.ne	0xfffffe00079a77d0 <_frame+0x498>
fffffe00079a77e8:      	cmp	x11, x21
fffffe00079a77ec:      	b.eq	0xfffffe00079a7774 <_frame+0x43c>
fffffe00079a77f0:      	tst	x21, #0x18
fffffe00079a77f4:      	b.eq	0xfffffe00079a7838 <_frame+0x500>
fffffe00079a77f8:      	and	x12, x21, #0xfffffffffffffff8
fffffe00079a77fc:      	add	x13, x11, x19
fffffe00079a7800:      	add	x9, x23, x12
fffffe00079a7804:      	add	x8, x8, x12
fffffe00079a7808:      	and	x10, x21, #0x7
fffffe00079a780c:      	add	x13, x13, #1193
fffffe00079a7810:      	add	x14, x23, x11
fffffe00079a7814:      	sub	x11, x11, x12
fffffe00079a7818:      	ldr	d0, [x14], #8
fffffe00079a781c:      	adds	x11, x11, #8
fffffe00079a7820:      	str	d0, [x13], #8
fffffe00079a7824:      	b.ne	0xfffffe00079a7818 <_frame+0x4e0>
fffffe00079a7828:      	cmp	x12, x21
fffffe00079a782c:      	b.ne	0xfffffe00079a7764 <_frame+0x42c>
fffffe00079a7830:      	b	0xfffffe00079a7774 <_frame+0x43c>
fffffe00079a7834:      	bl	0xfffffe00079fd594 <___stack_chk_fail>
fffffe00079a7838:      	and	x10, x21, #0x1f
fffffe00079a783c:      	add	x8, x8, x11
fffffe00079a7840:      	add	x9, x23, x11
fffffe00079a7844:      	b	0xfffffe00079a7764 <_frame+0x42c>
