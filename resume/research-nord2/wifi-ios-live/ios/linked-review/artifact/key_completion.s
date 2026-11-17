
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00075223a4 <_njs_event>:
fffffe00075223a4:      	stp	x22, x21, [sp, #-48]!
fffffe00075223a8:      	stp	x20, x19, [sp, #16]
fffffe00075223ac:      	stp	x29, x30, [sp, #32]
fffffe00075223b0:      	add	x29, sp, #32
fffffe00075223b4:      	adrp	x20, 0xfffffe00084f8000 <_ncd_run+0x590>
fffffe00075223b8:      	add	x20, x20, #544
fffffe00075223bc:      	cmp	x0, x20
fffffe00075223c0:      	b.ne	0xfffffe00075226d8 <_njs_event+0x334>
fffffe00075223c4:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x5a0>
fffffe00075223c8:      	ldr	x9, [x1, #8]
fffffe00075223cc:      	mov	x19, x1
fffffe00075223d0:      	ldr	x8, [x8, #752]
fffffe00075223d4:      	cmp	x9, x8
fffffe00075223d8:      	b.ne	0xfffffe00075226d8 <_njs_event+0x334>
fffffe00075223dc:      	ldr	w8, [x19]
fffffe00075223e0:      	cmp	w8, #1
fffffe00075223e4:      	b.ne	0xfffffe0007522498 <_njs_event+0xf4>
fffffe00075223e8:      	ldrb	w8, [x19, #24]
fffffe00075223ec:      	cmp	w8, #7
fffffe00075223f0:      	b.ne	0xfffffe0007522440 <_njs_event+0x9c>
fffffe00075223f4:      	adrp	x8, 0xfffffe0008516000 <_ncd_run+0x648>
fffffe00075223f8:      	ldr	w8, [x8, #2052]
fffffe00075223fc:      	cbz	w8, 0xfffffe0007522448 <_njs_event+0xa4>
fffffe0007522400:      	adrp	x8, 0xfffffe0008516000 <_ncd_run+0x654>
fffffe0007522404:      	add	x8, x8, #2008
fffffe0007522408:      	ldr	x9, [x19, #16]
fffffe000752240c:      	ldr	x10, [x8]
fffffe0007522410:      	cmp	x9, x10
fffffe0007522414:      	b.ne	0xfffffe0007522448 <_njs_event+0xa4>
fffffe0007522418:      	ldr	w9, [x8, #48]
fffffe000752241c:      	cbnz	w9, 0xfffffe0007522448 <_njs_event+0xa4>
fffffe0007522420:      	adrp	x9, 0xfffffe0008516000 <_ncd_run+0x674>
fffffe0007522424:      	add	x9, x9, #2060
fffffe0007522428:      	ldr	w10, [x9]
fffffe000752242c:      	ldr	w9, [x9, #8]
fffffe0007522430:      	cbz	w10, 0xfffffe000752245c <_njs_event+0xb8>
fffffe0007522434:      	adrp	x10, 0xfffffe0008516000 <_ncd_run+0x688>
fffffe0007522438:      	str	w9, [x10, #2044]
fffffe000752243c:      	b	0xfffffe000752248c <_njs_event+0xe8>
fffffe0007522440:      	mov	w8, #1
fffffe0007522444:      	b	0xfffffe0007522498 <_njs_event+0xf4>
fffffe0007522448:      	adrp	x8, 0xfffffe0008516000 <_ncd_run+0x69c>
fffffe000752244c:      	mov	w9, #1
fffffe0007522450:      	mov	w0, #-1
fffffe0007522454:      	str	w9, [x8, #2056]
fffffe0007522458:      	b	0xfffffe00075226dc <_njs_event+0x338>
fffffe000752245c:      	adrp	x10, 0xfffffe0008516000 <_ncd_run+0x6b0>
fffffe0007522460:      	mov	w11, #1
fffffe0007522464:      	ldr	w10, [x10, #2064]
fffffe0007522468:      	lsl	w11, w11, w10
fffffe000752246c:      	adrp	x10, 0xfffffe0008516000 <_ncd_run+0x6c0>
fffffe0007522470:      	cbz	w9, 0xfffffe0007522480 <_njs_event+0xdc>
fffffe0007522474:      	ldr	w9, [x10, #2048]
fffffe0007522478:      	orr	w9, w9, w11
fffffe000752247c:      	b	0xfffffe0007522488 <_njs_event+0xe4>
fffffe0007522480:      	ldr	w9, [x10, #2048]
fffffe0007522484:      	bic	w9, w9, w11
fffffe0007522488:      	str	w9, [x10, #2048]
fffffe000752248c:      	str	wzr, [x8, #44]
fffffe0007522490:      	str	xzr, [x8]
fffffe0007522494:      	ldr	w8, [x19]
fffffe0007522498:      	adrp	x9, 0xfffffe00084f8000 <_ncd_run+0x674>
fffffe000752249c:      	add	x9, x9, #644
fffffe00075224a0:      	ldr	w10, [x9, #8]
fffffe00075224a4:      	str	w8, [x9]
fffffe00075224a8:      	ldr	w8, [x19, #4]
fffffe00075224ac:      	add	w10, w10, #1
fffffe00075224b0:      	stp	w8, w10, [x9, #4]
fffffe00075224b4:      	ldr	w8, [x19]
fffffe00075224b8:      	cmp	w8, #2
fffffe00075224bc:      	b.ne	0xfffffe000752252c <_njs_event+0x188>
fffffe00075224c0:      	ldr	x8, [x19, #16]
fffffe00075224c4:      	cmn	x8, #1
fffffe00075224c8:      	b.ne	0xfffffe0007522528 <_njs_event+0x184>
fffffe00075224cc:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x6a8>
fffffe00075224d0:      	ldr	w9, [x8, #660]
fffffe00075224d4:      	add	w9, w9, #1
fffffe00075224d8:      	str	w9, [x8, #660]
fffffe00075224dc:      	ldr	w8, [x19, #4]
fffffe00075224e0:      	cbz	w8, 0xfffffe00075224f8 <_njs_event+0x154>
fffffe00075224e4:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x6c0>
fffffe00075224e8:      	ldr	w9, [x8, #668]
fffffe00075224ec:      	add	w9, w9, #1
fffffe00075224f0:      	str	w9, [x8, #668]
fffffe00075224f4:      	b	0xfffffe0007522508 <_njs_event+0x164>
fffffe00075224f8:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x6d4>
fffffe00075224fc:      	ldr	w9, [x8, #664]
fffffe0007522500:      	add	w9, w9, #1
fffffe0007522504:      	str	w9, [x8, #664]
fffffe0007522508:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x6e4>
fffffe000752250c:      	ldr	w9, [x8, #684]
fffffe0007522510:      	cmp	w9, #7
fffffe0007522514:      	b.hi	0xfffffe0007522528 <_njs_event+0x184>
fffffe0007522518:      	add	w9, w9, #1
fffffe000752251c:      	mov	w0, #1
fffffe0007522520:      	str	w9, [x8, #684]
fffffe0007522524:      	bl	0xfffffe0007512bf0 <_njs_trace_locked>
fffffe0007522528:      	ldr	w8, [x19]
fffffe000752252c:      	sub	w8, w8, #8
fffffe0007522530:      	cmp	w8, #8
fffffe0007522534:      	b.hi	0xfffffe00075225f8 <_njs_event+0x254>
fffffe0007522538:      	adrp	x9, 0xfffffe000700d000 <_nss_owner_loop+0x2c30>
fffffe000752253c:      	add	x9, x9, #3664
fffffe0007522540:      	mov	w3, #1
fffffe0007522544:      	adr	x10, #16
fffffe0007522548:      	ldrb	w11, [x9, x8]
fffffe000752254c:      	add	x10, x10, x11, lsl #2
fffffe0007522550:      	br	x10
fffffe0007522554:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x730>
fffffe0007522558:      	add	x8, x8, #560
fffffe000752255c:      	mov	w0, wzr
fffffe0007522560:      	ldr	w9, [x8]
fffffe0007522564:      	cbnz	w9, 0xfffffe00075226dc <_njs_event+0x338>
fffffe0007522568:      	ldr	w9, [x8, #4]
fffffe000752256c:      	cbnz	w9, 0xfffffe00075226dc <_njs_event+0x338>
fffffe0007522570:      	ldr	w8, [x8, #36]
fffffe0007522574:      	cbz	w8, 0xfffffe00075226dc <_njs_event+0x338>
fffffe0007522578:      	adrp	x0, 0xfffffe000850b000 <_ncd_run+0x7a0>
fffffe000752257c:      	add	x0, x0, #784
fffffe0007522580:      	bl	0xfffffe00079bbb50 <_nwo_authorized>
fffffe0007522584:      	cbz	w0, 0xfffffe00075226dc <_njs_event+0x338>
fffffe0007522588:      	ldr	x8, [x19, #40]
fffffe000752258c:      	cbz	x8, 0xfffffe00075226d8 <_njs_event+0x334>
fffffe0007522590:      	ldr	x8, [x19, #48]
fffffe0007522594:      	sub	x8, x8, #14
fffffe0007522598:      	cmp	x8, #1500
fffffe000752259c:      	b.hi	0xfffffe00075226d8 <_njs_event+0x334>
fffffe00075225a0:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x77c>
fffffe00075225a4:      	ldr	w21, [x8, #612]
fffffe00075225a8:      	cmp	w21, #16
fffffe00075225ac:      	b.ne	0xfffffe00075226ec <_njs_event+0x348>
fffffe00075225b0:      	mov	w0, #1
fffffe00075225b4:      	b	0xfffffe00075226dc <_njs_event+0x338>
fffffe00075225b8:      	mov	w3, #4
fffffe00075225bc:      	b	0xfffffe00075225c4 <_njs_event+0x220>
fffffe00075225c0:      	mov	w3, #7
fffffe00075225c4:      	ldr	w8, [x19, #4]
fffffe00075225c8:      	adrp	x0, 0xfffffe000850b000 <_ncd_run+0x7f0>
fffffe00075225cc:      	add	x0, x0, #784
fffffe00075225d0:      	ldp	x1, x2, [x19, #8]
fffffe00075225d4:      	cmp	w8, #0
fffffe00075225d8:      	cset	w4, eq
fffffe00075225dc:      	ldr	x5, [x0, #4848]
fffffe00075225e0:      	bl	0xfffffe00079bca08 <_nwn_peer_complete>
fffffe00075225e4:      	asr	w0, w0, #31
fffffe00075225e8:      	b	0xfffffe00075226dc <_njs_event+0x338>
fffffe00075225ec:      	ldr	x8, [x19, #16]
fffffe00075225f0:      	cmn	x8, #1
fffffe00075225f4:      	b.eq	0xfffffe0007522684 <_njs_event+0x2e0>
fffffe00075225f8:      	adrp	x0, 0xfffffe000850b000 <_ncd_run+0x820>
fffffe00075225fc:      	add	x0, x0, #784
fffffe0007522600:      	mov	x1, x19
fffffe0007522604:      	ldr	x2, [x0, #4848]
fffffe0007522608:      	bl	0xfffffe00079bcb9c <_nwn_event>
fffffe000752260c:      	ldr	w8, [x19]
fffffe0007522610:      	cmp	w8, #13
fffffe0007522614:      	b.ne	0xfffffe0007522638 <_njs_event+0x294>
fffffe0007522618:      	adrp	x8, 0xfffffe0007009000 <_nss_owner_loop+0x2d00>
fffffe000752261c:      	ldr	d0, [x8, #400]
fffffe0007522620:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x7fc>
fffffe0007522624:      	add	x8, x8, #3840
fffffe0007522628:      	str	d0, [x8]
fffffe000752262c:      	ldr	w9, [x19, #4]
fffffe0007522630:      	str	w9, [x8, #8]
fffffe0007522634:      	ldr	w8, [x19]
fffffe0007522638:      	cmp	w8, #6
fffffe000752263c:      	b.ne	0xfffffe00075225e4 <_njs_event+0x240>
fffffe0007522640:      	adrp	x8, 0xfffffe000850b000 <_ncd_run+0x868>
fffffe0007522644:      	ldr	w8, [x8, #792]
fffffe0007522648:      	cmp	w8, #5
fffffe000752264c:      	b.ne	0xfffffe00075225e4 <_njs_event+0x240>
fffffe0007522650:      	ldr	x8, [x19, #40]
fffffe0007522654:      	cbz	x8, 0xfffffe00075225e4 <_njs_event+0x240>
fffffe0007522658:      	ldr	x8, [x19, #48]
fffffe000752265c:      	cmp	x8, #26
fffffe0007522660:      	b.lo	0xfffffe00075225e4 <_njs_event+0x240>
fffffe0007522664:      	mov	w8, #1
fffffe0007522668:      	adrp	x9, 0xfffffe00084f8000 <_ncd_run+0x844>
fffffe000752266c:      	add	x9, x9, #3840
fffffe0007522670:      	str	w8, [x9]
fffffe0007522674:      	ldr	x8, [x19, #40]
fffffe0007522678:      	ldrh	w8, [x8, #24]
fffffe000752267c:      	str	w8, [x9, #4]
fffffe0007522680:      	b	0xfffffe00075225e4 <_njs_event+0x240>
fffffe0007522684:      	ldr	w8, [x19, #4]
fffffe0007522688:      	cbz	w8, 0xfffffe00075226a0 <_njs_event+0x2fc>
fffffe000752268c:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x868>
fffffe0007522690:      	ldr	w9, [x8, #676]
fffffe0007522694:      	add	w9, w9, #1
fffffe0007522698:      	str	w9, [x8, #676]
fffffe000752269c:      	b	0xfffffe00075226b0 <_njs_event+0x30c>
fffffe00075226a0:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x87c>
fffffe00075226a4:      	ldr	w9, [x8, #672]
fffffe00075226a8:      	add	w9, w9, #1
fffffe00075226ac:      	str	w9, [x8, #672]
fffffe00075226b0:      	adrp	x8, 0xfffffe00084f8000 <_ncd_run+0x88c>
fffffe00075226b4:      	ldr	w9, [x8, #688]
fffffe00075226b8:      	cmp	w9, #7
fffffe00075226bc:      	b.hi	0xfffffe00075226d8 <_njs_event+0x334>
fffffe00075226c0:      	add	w9, w9, #1
fffffe00075226c4:      	mov	w0, #1
fffffe00075226c8:      	str	w9, [x8, #688]
fffffe00075226cc:      	bl	0xfffffe0007512bf0 <_njs_trace_locked>
fffffe00075226d0:      	mov	w0, wzr
fffffe00075226d4:      	b	0xfffffe00075226dc <_njs_event+0x338>
fffffe00075226d8:      	mov	w0, wzr
fffffe00075226dc:      	ldp	x29, x30, [sp, #32]
fffffe00075226e0:      	ldp	x20, x19, [sp, #16]
fffffe00075226e4:      	ldp	x22, x21, [sp], #48
fffffe00075226e8:      	ret
fffffe00075226ec:      	mov	w8, #1514
fffffe00075226f0:      	add	w9, w21, #1
fffffe00075226f4:      	mov	w10, #24288
fffffe00075226f8:      	mul	x8, x21, x8
fffffe00075226fc:      	str	w9, [x20, #68]
fffffe0007522700:      	add	x9, x20, x8
fffffe0007522704:      	subs	x8, x10, x8
fffffe0007522708:      	ldp	x1, x2, [x19, #40]
fffffe000752270c:      	add	x9, x9, #24, lsl #12
fffffe0007522710:      	csel	x3, xzr, x8, lo
fffffe0007522714:      	add	x0, x9, #1728
fffffe0007522718:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe000752271c:      	mov	w8, #58720
fffffe0007522720:      	ldr	x9, [x19, #48]
fffffe0007522724:      	movk	w8, #1, lsl #16
fffffe0007522728:      	add	x10, x20, x21, lsl #2
fffffe000752272c:      	mov	w0, wzr
fffffe0007522730:      	str	w9, [x10, x8]
fffffe0007522734:      	add	x9, x20, x21, lsl #3
fffffe0007522738:      	ldr	x8, [x19, #8]
fffffe000752273c:      	str	x8, [x9, #216]
fffffe0007522740:      	b	0xfffffe00075226dc <_njs_event+0x338>
