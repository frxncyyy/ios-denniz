
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ac254 <_transmit>:
fffffe00079ac254:      	sub	sp, sp, #176
fffffe00079ac258:      	stp	x28, x27, [sp, #80]
fffffe00079ac25c:      	stp	x26, x25, [sp, #96]
fffffe00079ac260:      	stp	x24, x23, [sp, #112]
fffffe00079ac264:      	stp	x22, x21, [sp, #128]
fffffe00079ac268:      	stp	x20, x19, [sp, #144]
fffffe00079ac26c:      	stp	x29, x30, [sp, #160]
fffffe00079ac270:      	add	x29, sp, #160
fffffe00079ac274:      	ldr	w8, [x0, #56]
fffffe00079ac278:      	cbz	w8, 0xfffffe00079ac454 <_transmit+0x200>
fffffe00079ac27c:      	ldr	w8, [x0, #128]
fffffe00079ac280:      	mov	x20, x4
fffffe00079ac284:      	mov	x21, x3
fffffe00079ac288:      	mov	x19, x0
fffffe00079ac28c:      	mov	x22, x2
fffffe00079ac290:      	mov	x23, x1
fffffe00079ac294:      	cmp	w8, #3
fffffe00079ac298:      	b.ne	0xfffffe00079ac2ac <_transmit+0x58>
fffffe00079ac29c:      	ldr	w9, [x19, #1140]
fffffe00079ac2a0:      	cbz	w9, 0xfffffe00079ac2ac <_transmit+0x58>
fffffe00079ac2a4:      	ldr	w9, [x19, #132]
fffffe00079ac2a8:      	cbz	w9, 0xfffffe00079ac454 <_transmit+0x200>
fffffe00079ac2ac:      	ldr	w10, [x19, #24]
fffffe00079ac2b0:      	ldr	w9, [x19, #132]
fffffe00079ac2b4:      	cbz	w10, 0xfffffe00079ac2e0 <_transmit+0x8c>
fffffe00079ac2b8:      	cbnz	w9, 0xfffffe00079ac2e0 <_transmit+0x8c>
fffffe00079ac2bc:      	mov	w9, w8
fffffe00079ac2c0:      	cbz	w8, 0xfffffe00079ac2e0 <_transmit+0x8c>
fffffe00079ac2c4:      	cmp	w8, #1
fffffe00079ac2c8:      	b.ne	0xfffffe00079ac39c <_transmit+0x148>
fffffe00079ac2cc:      	add	x8, x19, #1193
fffffe00079ac2d0:      	ldrh	w8, [x8]
fffffe00079ac2d4:      	cmp	w8, #192
fffffe00079ac2d8:      	b.ne	0xfffffe00079ac39c <_transmit+0x148>
fffffe00079ac2dc:      	mov	w9, wzr
fffffe00079ac2e0:      	ldr	x8, [x19]
fffffe00079ac2e4:      	str	wzr, [sp, #12]
fffffe00079ac2e8:      	ldr	w10, [x19, #84]
fffffe00079ac2ec:      	ldr	x8, [x8, #2832]
fffffe00079ac2f0:      	lsl	w10, w10, #4
fffffe00079ac2f4:      	add	x24, x8, x10
fffffe00079ac2f8:      	cbz	w9, 0xfffffe00079ac348 <_transmit+0xf4>
fffffe00079ac2fc:      	mov	w8, #4
fffffe00079ac300:      	mov	x0, x19
fffffe00079ac304:      	mov	x1, x23
fffffe00079ac308:      	mov	x2, x22
fffffe00079ac30c:      	str	w8, [x19, #40]
fffffe00079ac310:      	bl	0xfffffe00079ac068 <_before>
fffffe00079ac314:      	cbnz	w0, 0xfffffe00079ac33c <_transmit+0xe8>
fffffe00079ac318:      	ldr	x8, [x23]
fffffe00079ac31c:      	mov	w9, #21528
fffffe00079ac320:      	movk	w9, #6146, lsl #16
fffffe00079ac324:      	add	x2, sp, #12
fffffe00079ac328:      	orr	w1, w9, #0x4
fffffe00079ac32c:      	mov	x0, x22
fffffe00079ac330:      	ldr	x8, [x8, #8]
fffffe00079ac334:      	blr	x8
fffffe00079ac338:      	cbz	w0, 0xfffffe00079ac444 <_transmit+0x1f0>
fffffe00079ac33c:      	mov	x0, x19
fffffe00079ac340:      	mov	w1, #5
fffffe00079ac344:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac348:      	mov	x0, x19
fffffe00079ac34c:      	mov	x1, x23
fffffe00079ac350:      	mov	x2, x22
fffffe00079ac354:      	bl	0xfffffe00079ac068 <_before>
fffffe00079ac358:      	cbnz	w0, 0xfffffe00079ac37c <_transmit+0x128>
fffffe00079ac35c:      	ldr	x8, [x23]
fffffe00079ac360:      	mov	w1, #240
fffffe00079ac364:      	add	x2, sp, #12
fffffe00079ac368:      	mov	x0, x22
fffffe00079ac36c:      	movk	w1, #6150, lsl #16
fffffe00079ac370:      	ldr	x8, [x8, #8]
fffffe00079ac374:      	blr	x8
fffffe00079ac378:      	cbz	w0, 0xfffffe00079ac478 <_transmit+0x224>
fffffe00079ac37c:      	mov	x0, x19
fffffe00079ac380:      	mov	w1, #5
fffffe00079ac384:      	bl	0xfffffe00079ae1c8 <_fail>
fffffe00079ac388:      	mov	x0, x19
fffffe00079ac38c:      	mov	w1, #12
fffffe00079ac390:      	bl	0xfffffe00079ae1c8 <_fail>
fffffe00079ac394:      	mov	w0, #-1
fffffe00079ac398:      	b	0xfffffe00079ac458 <_transmit+0x204>
fffffe00079ac39c:      	mov	w8, #1161
fffffe00079ac3a0:      	mov	w9, #5257
fffffe00079ac3a4:      	strb	wzr, [x19, x8]
fffffe00079ac3a8:      	add	x8, x8, #1
fffffe00079ac3ac:      	cmp	x8, x9
fffffe00079ac3b0:      	b.ne	0xfffffe00079ac3a4 <_transmit+0x150>
fffffe00079ac3b4:      	ldr	w8, [x19, #128]
fffffe00079ac3b8:      	mov	w9, #2
fffffe00079ac3bc:      	mov	w10, #16
fffffe00079ac3c0:      	str	wzr, [x19, #56]
fffffe00079ac3c4:      	movi.2d	v0, #0000000000000000
fffffe00079ac3c8:      	stp	xzr, xzr, [sp, #56]
fffffe00079ac3cc:      	cmp	w8, #3
fffffe00079ac3d0:      	csel	w9, w10, w9, eq
fffffe00079ac3d4:      	cmp	w8, #2
fffffe00079ac3d8:      	mov	w8, #15
fffffe00079ac3dc:      	ldr	x10, [x19, #160]
fffffe00079ac3e0:      	csel	w9, w8, w9, eq
fffffe00079ac3e4:      	mov	w8, #1
fffffe00079ac3e8:      	stur	q0, [sp, #24]
fffffe00079ac3ec:      	stur	q0, [sp, #40]
fffffe00079ac3f0:      	str	x10, [sp, #32]
fffffe00079ac3f4:      	strb	wzr, [sp, #41]
fffffe00079ac3f8:      	stp	w9, w8, [sp, #16]
fffffe00079ac3fc:      	strb	wzr, [sp, #43]
fffffe00079ac400:      	strb	wzr, [sp, #45]
fffffe00079ac404:      	strb	wzr, [sp, #47]
fffffe00079ac408:      	strb	wzr, [sp, #49]
fffffe00079ac40c:      	cbz	x21, 0xfffffe00079ac454 <_transmit+0x200>
fffffe00079ac410:      	ldr	x9, [x19, #152]
fffffe00079ac414:      	add	x1, sp, #16
fffffe00079ac418:      	mov	x0, x20
fffffe00079ac41c:      	str	w8, [x19, #148]
fffffe00079ac420:      	str	x9, [sp, #24]
fffffe00079ac424:      	blr	x21
fffffe00079ac428:      	mov	w8, w0
fffffe00079ac42c:      	mov	w0, wzr
fffffe00079ac430:      	str	wzr, [x19, #148]
fffffe00079ac434:      	tbz	w8, #31, 0xfffffe00079ac458 <_transmit+0x204>
fffffe00079ac438:      	mov	x0, x19
fffffe00079ac43c:      	mov	w1, #10
fffffe00079ac440:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac444:      	ldr	w8, [sp, #12]
fffffe00079ac448:      	ldr	w9, [x19, #84]
fffffe00079ac44c:      	cmp	w8, w9
fffffe00079ac450:      	b.ne	0xfffffe00079ac560 <_transmit+0x30c>
fffffe00079ac454:      	mov	w0, wzr
fffffe00079ac458:      	ldp	x29, x30, [sp, #160]
fffffe00079ac45c:      	ldp	x20, x19, [sp, #144]
fffffe00079ac460:      	ldp	x22, x21, [sp, #128]
fffffe00079ac464:      	ldp	x24, x23, [sp, #112]
fffffe00079ac468:      	ldp	x26, x25, [sp, #96]
fffffe00079ac46c:      	ldp	x28, x27, [sp, #80]
fffffe00079ac470:      	add	sp, sp, #176
fffffe00079ac474:      	ret
fffffe00079ac478:      	ldr	w8, [sp, #12]
fffffe00079ac47c:      	mov	w9, #3
fffffe00079ac480:      	bics	wzr, w9, w8
fffffe00079ac484:      	b.ne	0xfffffe00079ac388 <_transmit+0x134>
fffffe00079ac488:      	mov	x0, x19
fffffe00079ac48c:      	mov	x1, x23
fffffe00079ac490:      	mov	x2, x22
fffffe00079ac494:      	bl	0xfffffe00079ac180 <_coherent>
fffffe00079ac498:      	cbnz	w0, 0xfffffe00079ac388 <_transmit+0x134>
fffffe00079ac49c:      	mov	w3, #21528
fffffe00079ac4a0:      	add	x4, sp, #12
fffffe00079ac4a4:      	mov	x0, x19
fffffe00079ac4a8:      	mov	x1, x23
fffffe00079ac4ac:      	mov	x2, x22
fffffe00079ac4b0:      	movk	w3, #6146, lsl #16
fffffe00079ac4b4:      	mov	w25, #21528
fffffe00079ac4b8:      	movk	w25, #6146, lsl #16
fffffe00079ac4bc:      	bl	0xfffffe00079ae2c0 <_rr>
fffffe00079ac4c0:      	cbnz	w0, 0xfffffe00079ac388 <_transmit+0x134>
fffffe00079ac4c4:      	ldr	w8, [sp, #12]
fffffe00079ac4c8:      	ldr	w9, [x19, #84]
fffffe00079ac4cc:      	cmp	w8, w9
fffffe00079ac4d0:      	b.ne	0xfffffe00079ac388 <_transmit+0x134>
fffffe00079ac4d4:      	add	w3, w25, #4
fffffe00079ac4d8:      	add	x4, sp, #12
fffffe00079ac4dc:      	mov	x0, x19
fffffe00079ac4e0:      	mov	x1, x23
fffffe00079ac4e4:      	mov	x2, x22
fffffe00079ac4e8:      	bl	0xfffffe00079ae2c0 <_rr>
fffffe00079ac4ec:      	cbnz	w0, 0xfffffe00079ac388 <_transmit+0x134>
fffffe00079ac4f0:      	ldr	w8, [sp, #12]
fffffe00079ac4f4:      	ldr	w9, [x19, #84]
fffffe00079ac4f8:      	cmp	w8, w9
fffffe00079ac4fc:      	b.ne	0xfffffe00079ac388 <_transmit+0x134>
fffffe00079ac500:      	mov	w8, #1
fffffe00079ac504:      	mov	x0, x19
fffffe00079ac508:      	mov	x1, x23
fffffe00079ac50c:      	mov	x2, x22
fffffe00079ac510:      	mov	x3, x24
fffffe00079ac514:      	mov	w4, #16
fffffe00079ac518:      	mov	w5, #2
fffffe00079ac51c:      	str	w8, [x19, #40]
fffffe00079ac520:      	bl	0xfffffe00079ae330 <_sy>
fffffe00079ac524:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac528:      	add	x26, x19, #1145
fffffe00079ac52c:      	mov	x0, x19
fffffe00079ac530:      	mov	x1, x23
fffffe00079ac534:      	mov	x2, x22
fffffe00079ac538:      	mov	x3, x24
fffffe00079ac53c:      	mov	x4, x26
fffffe00079ac540:      	mov	w5, #16
fffffe00079ac544:      	bl	0xfffffe00079ae3c8 <_mr>
fffffe00079ac548:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac54c:      	ldrsb	w8, [x19, #1152]
fffffe00079ac550:      	tbnz	w8, #31, 0xfffffe00079ac82c <_transmit+0x5d8>
fffffe00079ac554:      	mov	x0, x19
fffffe00079ac558:      	mov	w1, #13
fffffe00079ac55c:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac560:      	add	w9, w9, #1
fffffe00079ac564:      	and	w9, w9, #0x3ff
fffffe00079ac568:      	cmp	w8, w9
fffffe00079ac56c:      	b.ne	0xfffffe00079ac598 <_transmit+0x344>
fffffe00079ac570:      	mov	w8, #1
fffffe00079ac574:      	mov	x0, x19
fffffe00079ac578:      	mov	x1, x23
fffffe00079ac57c:      	mov	x2, x22
fffffe00079ac580:      	str	w8, [x19, #40]
fffffe00079ac584:      	bl	0xfffffe00079ac068 <_before>
fffffe00079ac588:      	cbz	w0, 0xfffffe00079ac5a4 <_transmit+0x350>
fffffe00079ac58c:      	mov	x0, x19
fffffe00079ac590:      	mov	w1, #9
fffffe00079ac594:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac598:      	mov	x0, x19
fffffe00079ac59c:      	mov	w1, #16
fffffe00079ac5a0:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac5a4:      	mov	x0, x19
fffffe00079ac5a8:      	mov	x1, x24
fffffe00079ac5ac:      	mov	w2, #16
fffffe00079ac5b0:      	mov	w3, wzr
fffffe00079ac5b4:      	bl	0xfffffe00079ab898 <_nst_memory_range>
fffffe00079ac5b8:      	cbz	w0, 0xfffffe00079ac58c <_transmit+0x338>
fffffe00079ac5bc:      	ldr	x8, [x23]
fffffe00079ac5c0:      	mov	x0, x22
fffffe00079ac5c4:      	mov	x1, x24
fffffe00079ac5c8:      	mov	w2, #16
fffffe00079ac5cc:      	mov	w3, #2
fffffe00079ac5d0:      	ldr	x8, [x8, #40]
fffffe00079ac5d4:      	blr	x8
fffffe00079ac5d8:      	cbnz	w0, 0xfffffe00079ac58c <_transmit+0x338>
fffffe00079ac5dc:      	add	x4, x19, #1145
fffffe00079ac5e0:      	mov	x0, x19
fffffe00079ac5e4:      	mov	x1, x23
fffffe00079ac5e8:      	mov	x2, x22
fffffe00079ac5ec:      	mov	x3, x24
fffffe00079ac5f0:      	mov	w5, #16
fffffe00079ac5f4:      	bl	0xfffffe00079ae3c8 <_mr>
fffffe00079ac5f8:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac5fc:      	ldrsb	w8, [x19, #1152]
fffffe00079ac600:      	tbz	w8, #31, 0xfffffe00079ac454 <_transmit+0x200>
fffffe00079ac604:      	ldp	x1, x2, [x19, #152]
fffffe00079ac608:      	movi.2d	v0, #0000000000000000
fffffe00079ac60c:      	add	x24, x19, #1161
fffffe00079ac610:      	ldr	w4, [x19, #108]
fffffe00079ac614:      	add	x0, sp, #16
fffffe00079ac618:      	ldr	w5, [x19, #128]
fffffe00079ac61c:      	mov	x3, x24
fffffe00079ac620:      	mov	w6, #2
fffffe00079ac624:      	str	xzr, [sp, #64]
fffffe00079ac628:      	stp	q0, q0, [sp, #32]
fffffe00079ac62c:      	str	q0, [sp, #16]
fffffe00079ac630:      	bl	0xfffffe00079af354 <_nstd_tx>
fffffe00079ac634:      	add	x0, sp, #16
fffffe00079ac638:      	bl	0xfffffe00079af698 <_nstd_log>
fffffe00079ac63c:      	ldr	w8, [x19, #112]
fffffe00079ac640:      	cmp	w8, #28
fffffe00079ac644:      	b.ne	0xfffffe00079ac65c <_transmit+0x408>
fffffe00079ac648:      	ldrb	w8, [x19, #1227]
fffffe00079ac64c:      	add	x26, x19, #960
fffffe00079ac650:      	cmp	w8, #1
fffffe00079ac654:      	cset	w27, eq
fffffe00079ac658:      	b	0xfffffe00079ac6b0 <_transmit+0x45c>
fffffe00079ac65c:      	ldr	w9, [x19, #960]
fffffe00079ac660:      	add	x26, x19, #960
fffffe00079ac664:      	cmp	w9, #2
fffffe00079ac668:      	b.ne	0xfffffe00079ac684 <_transmit+0x430>
fffffe00079ac66c:      	cmp	w8, #22
fffffe00079ac670:      	b.ne	0xfffffe00079ac684 <_transmit+0x430>
fffffe00079ac674:      	mov	w8, #1
fffffe00079ac678:      	mov	w27, wzr
fffffe00079ac67c:      	str	w8, [x19, #1128]
fffffe00079ac680:      	b	0xfffffe00079ac6b0 <_transmit+0x45c>
fffffe00079ac684:      	cmp	w8, #7
fffffe00079ac688:      	b.ne	0xfffffe00079ac6ac <_transmit+0x458>
fffffe00079ac68c:      	ldrb	w8, [x19, #1225]
fffffe00079ac690:      	cbz	w8, 0xfffffe00079ac6ac <_transmit+0x458>
fffffe00079ac694:      	ldrb	w8, [x19, #1227]
fffffe00079ac698:      	cbz	w8, 0xfffffe00079ac878 <_transmit+0x624>
fffffe00079ac69c:      	mov	w8, #1
fffffe00079ac6a0:      	mov	w27, wzr
fffffe00079ac6a4:      	str	w8, [x19, #936]
fffffe00079ac6a8:      	b	0xfffffe00079ac6b0 <_transmit+0x45c>
fffffe00079ac6ac:      	mov	w27, wzr
fffffe00079ac6b0:      	ldr	w25, [x19, #108]
fffffe00079ac6b4:      	mov	w8, #1161
fffffe00079ac6b8:      	mov	w9, #5257
fffffe00079ac6bc:      	strb	wzr, [x19, x8]
fffffe00079ac6c0:      	add	x8, x8, #1
fffffe00079ac6c4:      	cmp	x8, x9
fffffe00079ac6c8:      	b.ne	0xfffffe00079ac6bc <_transmit+0x468>
fffffe00079ac6cc:      	mov	w8, #2
fffffe00079ac6d0:      	ldr	x9, [x19]
fffffe00079ac6d4:      	mov	x0, x19
fffffe00079ac6d8:      	mov	x1, x23
fffffe00079ac6dc:      	mov	x2, x22
fffffe00079ac6e0:      	mov	x4, x24
fffffe00079ac6e4:      	str	w8, [x19, #40]
fffffe00079ac6e8:      	mov	w5, w25
fffffe00079ac6ec:      	ldr	x3, [x9, #2856]
fffffe00079ac6f0:      	bl	0xfffffe00079ae45c <_mw>
fffffe00079ac6f4:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac6f8:      	ldr	x8, [x19]
fffffe00079ac6fc:      	mov	x0, x19
fffffe00079ac700:      	mov	x1, x23
fffffe00079ac704:      	mov	x2, x22
fffffe00079ac708:      	mov	w4, w25
fffffe00079ac70c:      	mov	w5, #1
fffffe00079ac710:      	ldr	x3, [x8, #2856]
fffffe00079ac714:      	bl	0xfffffe00079ae330 <_sy>
fffffe00079ac718:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac71c:      	ldr	w8, [x19, #84]
fffffe00079ac720:      	ldr	x9, [x19]
fffffe00079ac724:      	add	w8, w8, #1
fffffe00079ac728:      	and	w8, w8, #0x3ff
fffffe00079ac72c:      	str	w8, [x9, #2864]
fffffe00079ac730:      	ldr	x8, [x19]
fffffe00079ac734:      	ldr	w9, [x8, #2864]
fffffe00079ac738:      	str	w9, [x19, #84]
fffffe00079ac73c:      	ldr	w9, [x8, #1920]
fffffe00079ac740:      	add	w9, w9, #1
fffffe00079ac744:      	str	w9, [x8, #1920]
fffffe00079ac748:      	ldr	w8, [x19, #128]
fffffe00079ac74c:      	cbnz	w8, 0xfffffe00079ac760 <_transmit+0x50c>
fffffe00079ac750:      	ldr	x8, [x19]
fffffe00079ac754:      	ldrb	w9, [x8, #2876]
fffffe00079ac758:      	add	w9, w9, #1
fffffe00079ac75c:      	strb	w9, [x8, #2876]
fffffe00079ac760:      	ldr	x8, [x19]
fffffe00079ac764:      	mov	w9, #1
fffffe00079ac768:      	str	wzr, [x8, #1908]
fffffe00079ac76c:      	ldr	w8, [x19, #96]
fffffe00079ac770:      	str	w9, [x19, #136]
fffffe00079ac774:      	str	wzr, [x19, #56]
fffffe00079ac778:      	str	wzr, [x19, #40]
fffffe00079ac77c:      	add	w8, w8, #1
fffffe00079ac780:      	str	w8, [x19, #96]
fffffe00079ac784:      	cbz	w27, 0xfffffe00079ac7d0 <_transmit+0x57c>
fffffe00079ac788:      	movi.2d	v0, #0000000000000000
fffffe00079ac78c:      	mov	w8, #12
fffffe00079ac790:      	ldr	x9, [x19, #160]
fffffe00079ac794:      	add	x3, sp, #16
fffffe00079ac798:      	ldrb	w10, [x19, #984]
fffffe00079ac79c:      	mov	x0, x19
fffffe00079ac7a0:      	mov	x1, x21
fffffe00079ac7a4:      	mov	x2, x20
fffffe00079ac7a8:      	strb	wzr, [x19, #985]
fffffe00079ac7ac:      	str	xzr, [x19, #976]
fffffe00079ac7b0:      	str	xzr, [sp, #64]
fffffe00079ac7b4:      	stp	q0, q0, [sp, #16]
fffffe00079ac7b8:      	str	q0, [sp, #48]
fffffe00079ac7bc:      	str	w8, [sp, #16]
fffffe00079ac7c0:      	str	x9, [sp, #32]
fffffe00079ac7c4:      	strb	w10, [sp, #46]
fffffe00079ac7c8:      	bl	0xfffffe00079acf50 <_emit>
fffffe00079ac7cc:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac7d0:      	ldr	w8, [x19, #128]
fffffe00079ac7d4:      	sub	w9, w8, #2
fffffe00079ac7d8:      	cmp	w9, #2
fffffe00079ac7dc:      	b.hs	0xfffffe00079ac850 <_transmit+0x5fc>
fffffe00079ac7e0:      	movi.2d	v0, #0000000000000000
fffffe00079ac7e4:      	cmp	w8, #2
fffffe00079ac7e8:      	mov	w8, #15
fffffe00079ac7ec:      	ldr	x9, [x19, #160]
fffffe00079ac7f0:      	cinc	w8, w8, ne
fffffe00079ac7f4:      	str	xzr, [sp, #64]
fffffe00079ac7f8:      	stp	q0, q0, [sp, #16]
fffffe00079ac7fc:      	str	w8, [sp, #16]
fffffe00079ac800:      	str	x9, [sp, #32]
fffffe00079ac804:      	ldp	w8, w9, [x19, #120]
fffffe00079ac808:      	str	q0, [sp, #48]
fffffe00079ac80c:      	strb	w8, [sp, #42]
fffffe00079ac810:      	strb	w9, [sp, #43]
fffffe00079ac814:      	add	x3, sp, #16
fffffe00079ac818:      	mov	x0, x19
fffffe00079ac81c:      	mov	x1, x21
fffffe00079ac820:      	mov	x2, x20
fffffe00079ac824:      	bl	0xfffffe00079acf50 <_emit>
fffffe00079ac828:      	b	0xfffffe00079ac458 <_transmit+0x204>
fffffe00079ac82c:      	ldr	x8, [x19]
fffffe00079ac830:      	mov	x0, x26
fffffe00079ac834:      	ldr	w2, [x19, #108]
fffffe00079ac838:      	ldr	x1, [x8, #2856]
fffffe00079ac83c:      	bl	0xfffffe0007994d3c <_nwt_tx_desc>
fffffe00079ac840:      	cbz	w0, 0xfffffe00079ac890 <_transmit+0x63c>
fffffe00079ac844:      	mov	x0, x19
fffffe00079ac848:      	mov	w1, #14
fffffe00079ac84c:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac850:      	cbnz	w8, 0xfffffe00079ac454 <_transmit+0x200>
fffffe00079ac854:      	ldr	w8, [x26]
fffffe00079ac858:      	cbz	w8, 0xfffffe00079ac8ac <_transmit+0x658>
fffffe00079ac85c:      	cmp	w8, #1
fffffe00079ac860:      	b.ne	0xfffffe00079ac454 <_transmit+0x200>
fffffe00079ac864:      	ldr	w8, [x19, #956]
fffffe00079ac868:      	mov	w0, wzr
fffffe00079ac86c:      	add	w8, w8, #1
fffffe00079ac870:      	str	w8, [x19, #956]
fffffe00079ac874:      	b	0xfffffe00079ac458 <_transmit+0x204>
fffffe00079ac878:      	mov	w27, wzr
fffffe00079ac87c:      	mov	w8, #1
fffffe00079ac880:      	ldrb	w9, [x19, #1237]
fffffe00079ac884:      	str	w8, [x19, #940]
fffffe00079ac888:      	strb	w9, [x19, #1144]
fffffe00079ac88c:      	b	0xfffffe00079ac6b0 <_transmit+0x45c>
fffffe00079ac890:      	ldr	w8, [x19, #68]
fffffe00079ac894:      	cbz	w8, 0xfffffe00079ac8a0 <_transmit+0x64c>
fffffe00079ac898:      	ldr	w9, [x19, #72]
fffffe00079ac89c:      	cbz	w9, 0xfffffe00079ac8dc <_transmit+0x688>
fffffe00079ac8a0:      	mov	x0, x19
fffffe00079ac8a4:      	mov	w1, #35
fffffe00079ac8a8:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac8ac:      	movi.2d	v0, #0000000000000000
fffffe00079ac8b0:      	mov	w8, #1
fffffe00079ac8b4:      	ldp	w10, w11, [x19, #112]
fffffe00079ac8b8:      	ldr	x9, [x19, #160]
fffffe00079ac8bc:      	str	xzr, [sp, #64]
fffffe00079ac8c0:      	stp	q0, q0, [sp, #16]
fffffe00079ac8c4:      	str	q0, [sp, #48]
fffffe00079ac8c8:      	str	w8, [sp, #16]
fffffe00079ac8cc:      	str	x9, [sp, #32]
fffffe00079ac8d0:      	strb	w10, [sp, #40]
fffffe00079ac8d4:      	strb	w11, [sp, #41]
fffffe00079ac8d8:      	b	0xfffffe00079ac814 <_transmit+0x5c0>
fffffe00079ac8dc:      	ldr	w9, [x19, #76]
fffffe00079ac8e0:      	sub	w8, w8, #1
fffffe00079ac8e4:      	mov	w10, #1
fffffe00079ac8e8:      	mov	w11, #2
fffffe00079ac8ec:      	add	x25, x19, #1161
fffffe00079ac8f0:      	ldr	w5, [x19, #108]
fffffe00079ac8f4:      	add	w9, w9, #1
fffffe00079ac8f8:      	mov	x0, x19
fffffe00079ac8fc:      	stp	w8, w10, [x19, #68]
fffffe00079ac900:      	ldr	x8, [x19]
fffffe00079ac904:      	str	w11, [x19, #40]
fffffe00079ac908:      	mov	x1, x23
fffffe00079ac90c:      	str	w9, [x19, #76]
fffffe00079ac910:      	mov	x2, x22
fffffe00079ac914:      	ldr	x3, [x8, #2856]
fffffe00079ac918:      	mov	x4, x25
fffffe00079ac91c:      	bl	0xfffffe00079ae45c <_mw>
fffffe00079ac920:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac924:      	ldr	x8, [x19]
fffffe00079ac928:      	mov	x0, x19
fffffe00079ac92c:      	ldr	w4, [x19, #108]
fffffe00079ac930:      	mov	x1, x23
fffffe00079ac934:      	mov	x2, x22
fffffe00079ac938:      	mov	w5, #1
fffffe00079ac93c:      	ldr	x3, [x8, #2856]
fffffe00079ac940:      	bl	0xfffffe00079ae330 <_sy>
fffffe00079ac944:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac948:      	mov	x0, x19
fffffe00079ac94c:      	mov	x1, x23
fffffe00079ac950:      	mov	x2, x22
fffffe00079ac954:      	mov	x3, x24
fffffe00079ac958:      	mov	x4, x26
fffffe00079ac95c:      	mov	w5, #16
fffffe00079ac960:      	bl	0xfffffe00079ae45c <_mw>
fffffe00079ac964:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac968:      	mov	x0, x19
fffffe00079ac96c:      	mov	x1, x23
fffffe00079ac970:      	mov	x2, x22
fffffe00079ac974:      	mov	x3, x24
fffffe00079ac978:      	mov	w4, #16
fffffe00079ac97c:      	mov	w5, #1
fffffe00079ac980:      	bl	0xfffffe00079ae330 <_sy>
fffffe00079ac984:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac988:      	ldr	x8, [x19, #168]
fffffe00079ac98c:      	mov	x9, #-50880
fffffe00079ac990:      	movk	x9, #65490, lsl #16
fffffe00079ac994:      	cmp	x8, x9
fffffe00079ac998:      	b.lo	0xfffffe00079ac9a8 <_transmit+0x754>
fffffe00079ac99c:      	mov	x0, x19
fffffe00079ac9a0:      	mov	w1, #15
fffffe00079ac9a4:      	b	0xfffffe00079ac390 <_transmit+0x13c>
fffffe00079ac9a8:      	add	x8, x8, #732, lsl #12
fffffe00079ac9ac:      	mov	w9, #3
fffffe00079ac9b0:      	add	x8, x8, #1728
fffffe00079ac9b4:      	ldr	x10, [x19]
fffffe00079ac9b8:      	mov	w26, #1
fffffe00079ac9bc:      	mov	w3, #21528
fffffe00079ac9c0:      	str	w9, [x19, #40]
fffffe00079ac9c4:      	mov	x0, x19
fffffe00079ac9c8:      	str	x8, [x19, #184]
fffffe00079ac9cc:      	mov	x1, x23
fffffe00079ac9d0:      	str	w26, [x10, #1908]
fffffe00079ac9d4:      	mov	x2, x22
fffffe00079ac9d8:      	ldr	w8, [x19, #84]
fffffe00079ac9dc:      	movk	w3, #6146, lsl #16
fffffe00079ac9e0:      	add	w8, w8, #1
fffffe00079ac9e4:      	and	w4, w8, #0x3ff
fffffe00079ac9e8:      	bl	0xfffffe00079ae500 <_rw>
fffffe00079ac9ec:      	cbnz	w0, 0xfffffe00079ac394 <_transmit+0x140>
fffffe00079ac9f0:      	ldr	w8, [x19, #92]
fffffe00079ac9f4:      	str	w26, [x19, #132]
fffffe00079ac9f8:      	ldr	w9, [x19, #68]
fffffe00079ac9fc:      	ldr	w10, [x19, #80]
fffffe00079aca00:      	ldr	w3, [x19, #128]
fffffe00079aca04:      	add	w2, w8, #1
fffffe00079aca08:      	add	w8, w9, #1
fffffe00079aca0c:      	add	w9, w10, #1
fffffe00079aca10:      	cmp	w3, #1
fffffe00079aca14:      	str	w2, [x19, #92]
fffffe00079aca18:      	stp	w8, wzr, [x19, #68]
fffffe00079aca1c:      	str	w9, [x19, #80]
fffffe00079aca20:      	b.ne	0xfffffe00079aca38 <_transmit+0x7e4>
fffffe00079aca24:      	ldr	w8, [x19, #104]
fffffe00079aca28:      	mov	w9, #1
fffffe00079aca2c:      	add	w8, w8, #1
fffffe00079aca30:      	str	w9, [x19, #60]
fffffe00079aca34:      	str	w8, [x19, #104]
fffffe00079aca38:      	ldr	w8, [x19, #108]
fffffe00079aca3c:      	mov	w9, #9176
fffffe00079aca40:      	add	x0, x19, x9
fffffe00079aca44:      	ldr	x1, [x19, #152]
fffffe00079aca48:      	add	x4, x19, #1193
fffffe00079aca4c:      	sub	w5, w8, #32
fffffe00079aca50:      	bl	0xfffffe00079afbd4 <_nsdd_publish>
fffffe00079aca54:      	ldp	x1, x2, [x19, #152]
fffffe00079aca58:      	movi.2d	v0, #0000000000000000
fffffe00079aca5c:      	add	x0, sp, #16
fffffe00079aca60:      	ldr	w4, [x19, #108]
fffffe00079aca64:      	mov	x3, x25
fffffe00079aca68:      	ldr	w5, [x19, #128]
fffffe00079aca6c:      	mov	w6, wzr
fffffe00079aca70:      	str	xzr, [sp, #64]
fffffe00079aca74:      	stp	q0, q0, [sp, #32]
fffffe00079aca78:      	str	q0, [sp, #16]
fffffe00079aca7c:      	bl	0xfffffe00079af354 <_nstd_tx>
fffffe00079aca80:      	add	x0, sp, #16
fffffe00079aca84:      	bl	0xfffffe00079af698 <_nstd_log>
fffffe00079aca88:      	ldp	x1, x2, [x19, #152]
fffffe00079aca8c:      	add	x0, sp, #16
fffffe00079aca90:      	mov	x3, x25
fffffe00079aca94:      	ldr	w4, [x19, #108]
fffffe00079aca98:      	mov	w6, #1
fffffe00079aca9c:      	ldr	w5, [x19, #128]
fffffe00079acaa0:      	bl	0xfffffe00079af354 <_nstd_tx>
fffffe00079acaa4:      	add	x0, sp, #16
fffffe00079acaa8:      	bl	0xfffffe00079af698 <_nstd_log>
fffffe00079acaac:      	ldr	w8, [x19, #112]
fffffe00079acab0:      	cmp	w8, #7
fffffe00079acab4:      	b.eq	0xfffffe00079acaf0 <_transmit+0x89c>
fffffe00079acab8:      	cmp	w8, #28
fffffe00079acabc:      	b.eq	0xfffffe00079acb0c <_transmit+0x8b8>
fffffe00079acac0:      	cmp	w8, #19
fffffe00079acac4:      	b.ne	0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acac8:      	ldrb	w8, [x19, #1245]
fffffe00079acacc:      	cmp	w8, #3
fffffe00079acad0:      	b.ne	0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acad4:      	ldrb	w8, [x19, #1276]
fffffe00079acad8:      	cbz	w8, 0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acadc:      	mov	w8, #1
fffffe00079acae0:      	ldr	w9, [x19, #116]
fffffe00079acae4:      	str	w8, [x19, #944]
fffffe00079acae8:      	str	w9, [x19, #948]
fffffe00079acaec:      	b	0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acaf0:      	ldrb	w8, [x19, #1225]
fffffe00079acaf4:      	cbz	w8, 0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acaf8:      	ldrb	w8, [x19, #1227]
fffffe00079acafc:      	cbz	w8, 0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acb00:      	mov	w8, #1
fffffe00079acb04:      	str	w8, [x19, #932]
fffffe00079acb08:      	b	0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acb0c:      	ldrb	w8, [x19, #1227]
fffffe00079acb10:      	cbnz	w8, 0xfffffe00079ac2fc <_transmit+0xa8>
fffffe00079acb14:      	ldrb	w8, [x19, #1226]
fffffe00079acb18:      	mov	w9, #1
fffffe00079acb1c:      	strb	w8, [x19, #984]
fffffe00079acb20:      	strb	w9, [x19, #985]
fffffe00079acb24:      	b	0xfffffe00079ac2fc <_transmit+0xa8>
