
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ad3c4 <_transmit>:
fffffe00079ad3c4:      	sub	sp, sp, #176
fffffe00079ad3c8:      	stp	x28, x27, [sp, #80]
fffffe00079ad3cc:      	stp	x26, x25, [sp, #96]
fffffe00079ad3d0:      	stp	x24, x23, [sp, #112]
fffffe00079ad3d4:      	stp	x22, x21, [sp, #128]
fffffe00079ad3d8:      	stp	x20, x19, [sp, #144]
fffffe00079ad3dc:      	stp	x29, x30, [sp, #160]
fffffe00079ad3e0:      	add	x29, sp, #160
fffffe00079ad3e4:      	ldr	w8, [x0, #56]
fffffe00079ad3e8:      	cbz	w8, 0xfffffe00079ad608 <_transmit+0x244>
fffffe00079ad3ec:      	ldr	w8, [x0, #128]
fffffe00079ad3f0:      	mov	x20, x4
fffffe00079ad3f4:      	mov	x21, x3
fffffe00079ad3f8:      	mov	x19, x0
fffffe00079ad3fc:      	mov	x22, x2
fffffe00079ad400:      	mov	x23, x1
fffffe00079ad404:      	cmp	w8, #3
fffffe00079ad408:      	b.ne	0xfffffe00079ad41c <_transmit+0x58>
fffffe00079ad40c:      	ldr	w9, [x19, #1740]
fffffe00079ad410:      	cbz	w9, 0xfffffe00079ad41c <_transmit+0x58>
fffffe00079ad414:      	ldr	w9, [x19, #132]
fffffe00079ad418:      	cbz	w9, 0xfffffe00079ad608 <_transmit+0x244>
fffffe00079ad41c:      	ldr	w9, [x19, #24]
fffffe00079ad420:      	cbz	w9, 0xfffffe00079ad42c <_transmit+0x68>
fffffe00079ad424:      	ldr	w9, [x19, #132]
fffffe00079ad428:      	cbz	w9, 0xfffffe00079ad4d8 <_transmit+0x114>
fffffe00079ad42c:      	and	w8, w8, #0xfffffffe
fffffe00079ad430:      	cmp	w8, #2
fffffe00079ad434:      	b.ne	0xfffffe00079ad46c <_transmit+0xa8>
fffffe00079ad438:      	mov	x0, x19
fffffe00079ad43c:      	mov	x1, x23
fffffe00079ad440:      	mov	x2, x22
fffffe00079ad444:      	mov	x3, x21
fffffe00079ad448:      	mov	x4, x20
fffffe00079ad44c:      	ldp	x29, x30, [sp, #160]
fffffe00079ad450:      	ldp	x20, x19, [sp, #144]
fffffe00079ad454:      	ldp	x22, x21, [sp, #128]
fffffe00079ad458:      	ldp	x24, x23, [sp, #112]
fffffe00079ad45c:      	ldp	x26, x25, [sp, #96]
fffffe00079ad460:      	ldp	x28, x27, [sp, #80]
fffffe00079ad464:      	add	sp, sp, #176
fffffe00079ad468:      	b	0xfffffe00079ae1f0 <_data_transmit>
fffffe00079ad46c:      	ldr	w8, [x19, #132]
fffffe00079ad470:      	ldr	x9, [x19]
fffffe00079ad474:      	str	wzr, [sp, #12]
fffffe00079ad478:      	ldr	w10, [x19, #84]
fffffe00079ad47c:      	ldr	x9, [x9, #2840]
fffffe00079ad480:      	lsl	w10, w10, #4
fffffe00079ad484:      	add	x24, x9, x10
fffffe00079ad488:      	cbz	w8, 0xfffffe00079ad4fc <_transmit+0x138>
fffffe00079ad48c:      	mov	w8, #4
fffffe00079ad490:      	mov	x0, x19
fffffe00079ad494:      	mov	x1, x23
fffffe00079ad498:      	mov	x2, x22
fffffe00079ad49c:      	str	w8, [x19, #40]
fffffe00079ad4a0:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ad4a4:      	cbnz	w0, 0xfffffe00079ad4cc <_transmit+0x108>
fffffe00079ad4a8:      	ldr	x8, [x23]
fffffe00079ad4ac:      	mov	w9, #21528
fffffe00079ad4b0:      	movk	w9, #6146, lsl #16
fffffe00079ad4b4:      	add	x2, sp, #12
fffffe00079ad4b8:      	orr	w1, w9, #0x4
fffffe00079ad4bc:      	mov	x0, x22
fffffe00079ad4c0:      	ldr	x8, [x8, #8]
fffffe00079ad4c4:      	blr	x8
fffffe00079ad4c8:      	cbz	w0, 0xfffffe00079ad5f8 <_transmit+0x234>
fffffe00079ad4cc:      	mov	x0, x19
fffffe00079ad4d0:      	mov	w1, #5
fffffe00079ad4d4:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079ad4d8:      	cbz	w8, 0xfffffe00079ad470 <_transmit+0xac>
fffffe00079ad4dc:      	cmp	w8, #1
fffffe00079ad4e0:      	b.ne	0xfffffe00079ad550 <_transmit+0x18c>
fffffe00079ad4e4:      	add	x8, x19, #1793
fffffe00079ad4e8:      	ldrh	w8, [x8]
fffffe00079ad4ec:      	cmp	w8, #192
fffffe00079ad4f0:      	b.ne	0xfffffe00079ad550 <_transmit+0x18c>
fffffe00079ad4f4:      	mov	w8, wzr
fffffe00079ad4f8:      	b	0xfffffe00079ad470 <_transmit+0xac>
fffffe00079ad4fc:      	mov	x0, x19
fffffe00079ad500:      	mov	x1, x23
fffffe00079ad504:      	mov	x2, x22
fffffe00079ad508:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ad50c:      	cbnz	w0, 0xfffffe00079ad530 <_transmit+0x16c>
fffffe00079ad510:      	ldr	x8, [x23]
fffffe00079ad514:      	mov	w1, #240
fffffe00079ad518:      	add	x2, sp, #12
fffffe00079ad51c:      	mov	x0, x22
fffffe00079ad520:      	movk	w1, #6150, lsl #16
fffffe00079ad524:      	ldr	x8, [x8, #8]
fffffe00079ad528:      	blr	x8
fffffe00079ad52c:      	cbz	w0, 0xfffffe00079ad62c <_transmit+0x268>
fffffe00079ad530:      	mov	x0, x19
fffffe00079ad534:      	mov	w1, #5
fffffe00079ad538:      	bl	0xfffffe00079afe24 <_fail>
fffffe00079ad53c:      	mov	x0, x19
fffffe00079ad540:      	mov	w1, #12
fffffe00079ad544:      	bl	0xfffffe00079afe24 <_fail>
fffffe00079ad548:      	mov	w0, #-1
fffffe00079ad54c:      	b	0xfffffe00079ad60c <_transmit+0x248>
fffffe00079ad550:      	mov	w8, #1761
fffffe00079ad554:      	mov	w9, #5857
fffffe00079ad558:      	strb	wzr, [x19, x8]
fffffe00079ad55c:      	add	x8, x8, #1
fffffe00079ad560:      	cmp	x8, x9
fffffe00079ad564:      	b.ne	0xfffffe00079ad558 <_transmit+0x194>
fffffe00079ad568:      	ldr	w8, [x19, #128]
fffffe00079ad56c:      	mov	w9, #2
fffffe00079ad570:      	mov	w10, #16
fffffe00079ad574:      	str	wzr, [x19, #56]
fffffe00079ad578:      	movi.2d	v0, #0000000000000000
fffffe00079ad57c:      	stp	xzr, xzr, [sp, #56]
fffffe00079ad580:      	cmp	w8, #3
fffffe00079ad584:      	csel	w9, w10, w9, eq
fffffe00079ad588:      	cmp	w8, #2
fffffe00079ad58c:      	mov	w8, #15
fffffe00079ad590:      	ldr	x10, [x19, #160]
fffffe00079ad594:      	csel	w9, w8, w9, eq
fffffe00079ad598:      	mov	w8, #1
fffffe00079ad59c:      	stur	q0, [sp, #24]
fffffe00079ad5a0:      	stur	q0, [sp, #40]
fffffe00079ad5a4:      	str	x10, [sp, #32]
fffffe00079ad5a8:      	strb	wzr, [sp, #41]
fffffe00079ad5ac:      	stp	w9, w8, [sp, #16]
fffffe00079ad5b0:      	strb	wzr, [sp, #43]
fffffe00079ad5b4:      	strb	wzr, [sp, #45]
fffffe00079ad5b8:      	strb	wzr, [sp, #47]
fffffe00079ad5bc:      	strb	wzr, [sp, #49]
fffffe00079ad5c0:      	cbz	x21, 0xfffffe00079ad608 <_transmit+0x244>
fffffe00079ad5c4:      	ldr	x9, [x19, #152]
fffffe00079ad5c8:      	add	x1, sp, #16
fffffe00079ad5cc:      	mov	x0, x20
fffffe00079ad5d0:      	str	w8, [x19, #148]
fffffe00079ad5d4:      	str	x9, [sp, #24]
fffffe00079ad5d8:      	blr	x21
fffffe00079ad5dc:      	mov	w8, w0
fffffe00079ad5e0:      	mov	w0, wzr
fffffe00079ad5e4:      	str	wzr, [x19, #148]
fffffe00079ad5e8:      	tbz	w8, #31, 0xfffffe00079ad60c <_transmit+0x248>
fffffe00079ad5ec:      	mov	x0, x19
fffffe00079ad5f0:      	mov	w1, #10
fffffe00079ad5f4:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079ad5f8:      	ldr	w8, [sp, #12]
fffffe00079ad5fc:      	ldr	w9, [x19, #84]
fffffe00079ad600:      	cmp	w8, w9
fffffe00079ad604:      	b.ne	0xfffffe00079ad714 <_transmit+0x350>
fffffe00079ad608:      	mov	w0, wzr
fffffe00079ad60c:      	ldp	x29, x30, [sp, #160]
fffffe00079ad610:      	ldp	x20, x19, [sp, #144]
fffffe00079ad614:      	ldp	x22, x21, [sp, #128]
fffffe00079ad618:      	ldp	x24, x23, [sp, #112]
fffffe00079ad61c:      	ldp	x26, x25, [sp, #96]
fffffe00079ad620:      	ldp	x28, x27, [sp, #80]
fffffe00079ad624:      	add	sp, sp, #176
fffffe00079ad628:      	ret
fffffe00079ad62c:      	ldr	w8, [sp, #12]
fffffe00079ad630:      	mov	w9, #3
fffffe00079ad634:      	bics	wzr, w9, w8
fffffe00079ad638:      	b.ne	0xfffffe00079ad53c <_transmit+0x178>
fffffe00079ad63c:      	mov	x0, x19
fffffe00079ad640:      	mov	x1, x23
fffffe00079ad644:      	mov	x2, x22
fffffe00079ad648:      	bl	0xfffffe00079ad2f0 <_coherent>
fffffe00079ad64c:      	cbnz	w0, 0xfffffe00079ad53c <_transmit+0x178>
fffffe00079ad650:      	mov	w3, #21528
fffffe00079ad654:      	add	x4, sp, #12
fffffe00079ad658:      	mov	x0, x19
fffffe00079ad65c:      	mov	x1, x23
fffffe00079ad660:      	mov	x2, x22
fffffe00079ad664:      	movk	w3, #6146, lsl #16
fffffe00079ad668:      	mov	w25, #21528
fffffe00079ad66c:      	movk	w25, #6146, lsl #16
fffffe00079ad670:      	bl	0xfffffe00079aff1c <_rr>
fffffe00079ad674:      	cbnz	w0, 0xfffffe00079ad53c <_transmit+0x178>
fffffe00079ad678:      	ldr	w8, [sp, #12]
fffffe00079ad67c:      	ldr	w9, [x19, #84]
fffffe00079ad680:      	cmp	w8, w9
fffffe00079ad684:      	b.ne	0xfffffe00079ad53c <_transmit+0x178>
fffffe00079ad688:      	add	w3, w25, #4
fffffe00079ad68c:      	add	x4, sp, #12
fffffe00079ad690:      	mov	x0, x19
fffffe00079ad694:      	mov	x1, x23
fffffe00079ad698:      	mov	x2, x22
fffffe00079ad69c:      	bl	0xfffffe00079aff1c <_rr>
fffffe00079ad6a0:      	cbnz	w0, 0xfffffe00079ad53c <_transmit+0x178>
fffffe00079ad6a4:      	ldr	w8, [sp, #12]
fffffe00079ad6a8:      	ldr	w9, [x19, #84]
fffffe00079ad6ac:      	cmp	w8, w9
fffffe00079ad6b0:      	b.ne	0xfffffe00079ad53c <_transmit+0x178>
fffffe00079ad6b4:      	mov	w8, #1
fffffe00079ad6b8:      	mov	x0, x19
fffffe00079ad6bc:      	mov	x1, x23
fffffe00079ad6c0:      	mov	x2, x22
fffffe00079ad6c4:      	mov	x3, x24
fffffe00079ad6c8:      	mov	w4, #16
fffffe00079ad6cc:      	mov	w5, #2
fffffe00079ad6d0:      	str	w8, [x19, #40]
fffffe00079ad6d4:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079ad6d8:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079ad6dc:      	add	x26, x19, #1745
fffffe00079ad6e0:      	mov	x0, x19
fffffe00079ad6e4:      	mov	x1, x23
fffffe00079ad6e8:      	mov	x2, x22
fffffe00079ad6ec:      	mov	x3, x24
fffffe00079ad6f0:      	mov	x4, x26
fffffe00079ad6f4:      	mov	w5, #16
fffffe00079ad6f8:      	bl	0xfffffe00079b0024 <_mr>
fffffe00079ad6fc:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079ad700:      	ldrsb	w8, [x19, #1752]
fffffe00079ad704:      	tbnz	w8, #31, 0xfffffe00079ad868 <_transmit+0x4a4>
fffffe00079ad708:      	mov	x0, x19
fffffe00079ad70c:      	mov	w1, #13
fffffe00079ad710:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079ad714:      	add	w9, w9, #1
fffffe00079ad718:      	and	w9, w9, #0x3ff
fffffe00079ad71c:      	cmp	w8, w9
fffffe00079ad720:      	b.ne	0xfffffe00079ad74c <_transmit+0x388>
fffffe00079ad724:      	mov	w8, #1
fffffe00079ad728:      	mov	x0, x19
fffffe00079ad72c:      	mov	x1, x23
fffffe00079ad730:      	mov	x2, x22
fffffe00079ad734:      	str	w8, [x19, #40]
fffffe00079ad738:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ad73c:      	cbz	w0, 0xfffffe00079ad758 <_transmit+0x394>
fffffe00079ad740:      	mov	x0, x19
fffffe00079ad744:      	mov	w1, #9
fffffe00079ad748:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079ad74c:      	mov	x0, x19
fffffe00079ad750:      	mov	w1, #16
fffffe00079ad754:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079ad758:      	mov	x0, x19
fffffe00079ad75c:      	mov	x1, x24
fffffe00079ad760:      	mov	w2, #16
fffffe00079ad764:      	mov	w3, wzr
fffffe00079ad768:      	bl	0xfffffe00079ac720 <_nst_memory_range>
fffffe00079ad76c:      	cbz	w0, 0xfffffe00079ad740 <_transmit+0x37c>
fffffe00079ad770:      	ldr	x8, [x23]
fffffe00079ad774:      	mov	x0, x22
fffffe00079ad778:      	mov	x1, x24
fffffe00079ad77c:      	mov	w2, #16
fffffe00079ad780:      	mov	w3, #2
fffffe00079ad784:      	ldr	x8, [x8, #40]
fffffe00079ad788:      	blr	x8
fffffe00079ad78c:      	cbnz	w0, 0xfffffe00079ad740 <_transmit+0x37c>
fffffe00079ad790:      	add	x4, x19, #1745
fffffe00079ad794:      	mov	x0, x19
fffffe00079ad798:      	mov	x1, x23
fffffe00079ad79c:      	mov	x2, x22
fffffe00079ad7a0:      	mov	x3, x24
fffffe00079ad7a4:      	mov	w5, #16
fffffe00079ad7a8:      	bl	0xfffffe00079b0024 <_mr>
fffffe00079ad7ac:      	cbz	w0, 0xfffffe00079ad7b8 <_transmit+0x3f4>
fffffe00079ad7b0:      	mov	w0, #-1
fffffe00079ad7b4:      	b	0xfffffe00079ad60c <_transmit+0x248>
fffffe00079ad7b8:      	ldrsb	w8, [x19, #1752]
fffffe00079ad7bc:      	tbz	w8, #31, 0xfffffe00079ad608 <_transmit+0x244>
fffffe00079ad7c0:      	ldp	x1, x2, [x19, #152]
fffffe00079ad7c4:      	movi.2d	v0, #0000000000000000
fffffe00079ad7c8:      	add	x24, x19, #1761
fffffe00079ad7cc:      	ldr	w4, [x19, #108]
fffffe00079ad7d0:      	add	x0, sp, #16
fffffe00079ad7d4:      	ldr	w5, [x19, #128]
fffffe00079ad7d8:      	mov	x3, x24
fffffe00079ad7dc:      	mov	w6, #2
fffffe00079ad7e0:      	str	xzr, [sp, #64]
fffffe00079ad7e4:      	stp	q0, q0, [sp, #32]
fffffe00079ad7e8:      	str	q0, [sp, #16]
fffffe00079ad7ec:      	bl	0xfffffe00079b10f8 <_nstd_tx>
fffffe00079ad7f0:      	add	x0, sp, #16
fffffe00079ad7f4:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079ad7f8:      	ldr	w8, [x19, #112]
fffffe00079ad7fc:      	cmp	w8, #28
fffffe00079ad800:      	b.ne	0xfffffe00079ad818 <_transmit+0x454>
fffffe00079ad804:      	ldrb	w8, [x19, #1827]
fffffe00079ad808:      	add	x26, x19, #1560
fffffe00079ad80c:      	cmp	w8, #1
fffffe00079ad810:      	cset	w27, eq
fffffe00079ad814:      	b	0xfffffe00079ad894 <_transmit+0x4d0>
fffffe00079ad818:      	ldr	w9, [x19, #1560]
fffffe00079ad81c:      	add	x26, x19, #1560
fffffe00079ad820:      	cmp	w9, #2
fffffe00079ad824:      	b.ne	0xfffffe00079ad840 <_transmit+0x47c>
fffffe00079ad828:      	cmp	w8, #22
fffffe00079ad82c:      	b.ne	0xfffffe00079ad840 <_transmit+0x47c>
fffffe00079ad830:      	mov	w8, #1
fffffe00079ad834:      	mov	w27, wzr
fffffe00079ad838:      	str	w8, [x19, #1728]
fffffe00079ad83c:      	b	0xfffffe00079ad894 <_transmit+0x4d0>
fffffe00079ad840:      	cmp	w8, #7
fffffe00079ad844:      	b.ne	0xfffffe00079ad890 <_transmit+0x4cc>
fffffe00079ad848:      	ldrb	w9, [x19, #1825]
fffffe00079ad84c:      	ldrb	w8, [x19, #1827]
fffffe00079ad850:      	cbz	w9, 0xfffffe00079ad88c <_transmit+0x4c8>
fffffe00079ad854:      	cbz	w8, 0xfffffe00079ad9e0 <_transmit+0x61c>
fffffe00079ad858:      	mov	w8, #1
fffffe00079ad85c:      	mov	w27, wzr
fffffe00079ad860:      	str	w8, [x19, #1536]
fffffe00079ad864:      	b	0xfffffe00079ad894 <_transmit+0x4d0>
fffffe00079ad868:      	ldr	x8, [x19]
fffffe00079ad86c:      	mov	x0, x26
fffffe00079ad870:      	ldr	w2, [x19, #108]
fffffe00079ad874:      	ldr	x1, [x8, #2864]
fffffe00079ad878:      	bl	0xfffffe0007995038 <_nwt_tx_desc>
fffffe00079ad87c:      	cbz	w0, 0xfffffe00079ada08 <_transmit+0x644>
fffffe00079ad880:      	mov	x0, x19
fffffe00079ad884:      	mov	w1, #14
fffffe00079ad888:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079ad88c:      	cbz	w8, 0xfffffe00079ada24 <_transmit+0x660>
fffffe00079ad890:      	mov	w27, wzr
fffffe00079ad894:      	ldr	w25, [x19, #108]
fffffe00079ad898:      	mov	w8, #1761
fffffe00079ad89c:      	mov	w9, #5857
fffffe00079ad8a0:      	strb	wzr, [x19, x8]
fffffe00079ad8a4:      	add	x8, x8, #1
fffffe00079ad8a8:      	cmp	x8, x9
fffffe00079ad8ac:      	b.ne	0xfffffe00079ad8a0 <_transmit+0x4dc>
fffffe00079ad8b0:      	mov	w8, #2
fffffe00079ad8b4:      	ldr	x9, [x19]
fffffe00079ad8b8:      	mov	x0, x19
fffffe00079ad8bc:      	mov	x1, x23
fffffe00079ad8c0:      	mov	x2, x22
fffffe00079ad8c4:      	mov	x4, x24
fffffe00079ad8c8:      	str	w8, [x19, #40]
fffffe00079ad8cc:      	mov	w5, w25
fffffe00079ad8d0:      	ldr	x3, [x9, #2864]
fffffe00079ad8d4:      	bl	0xfffffe00079b00b8 <_mw>
fffffe00079ad8d8:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079ad8dc:      	ldr	x8, [x19]
fffffe00079ad8e0:      	mov	x0, x19
fffffe00079ad8e4:      	mov	x1, x23
fffffe00079ad8e8:      	mov	x2, x22
fffffe00079ad8ec:      	mov	w4, w25
fffffe00079ad8f0:      	mov	w5, #1
fffffe00079ad8f4:      	ldr	x3, [x8, #2864]
fffffe00079ad8f8:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079ad8fc:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079ad900:      	ldr	w8, [x19, #84]
fffffe00079ad904:      	ldr	x9, [x19]
fffffe00079ad908:      	add	w8, w8, #1
fffffe00079ad90c:      	and	w8, w8, #0x3ff
fffffe00079ad910:      	str	w8, [x9, #2872]
fffffe00079ad914:      	ldr	x8, [x19]
fffffe00079ad918:      	ldr	w9, [x8, #2872]
fffffe00079ad91c:      	str	w9, [x19, #84]
fffffe00079ad920:      	ldr	w9, [x8, #1928]
fffffe00079ad924:      	add	w9, w9, #1
fffffe00079ad928:      	str	w9, [x8, #1928]
fffffe00079ad92c:      	ldr	w8, [x19, #128]
fffffe00079ad930:      	cbnz	w8, 0xfffffe00079ad944 <_transmit+0x580>
fffffe00079ad934:      	ldr	x8, [x19]
fffffe00079ad938:      	ldrb	w9, [x8, #2884]
fffffe00079ad93c:      	add	w9, w9, #1
fffffe00079ad940:      	strb	w9, [x8, #2884]
fffffe00079ad944:      	ldr	x8, [x19]
fffffe00079ad948:      	mov	w9, #1
fffffe00079ad94c:      	str	wzr, [x8, #1916]
fffffe00079ad950:      	ldr	w8, [x19, #96]
fffffe00079ad954:      	str	w9, [x19, #136]
fffffe00079ad958:      	str	wzr, [x19, #56]
fffffe00079ad95c:      	str	wzr, [x19, #40]
fffffe00079ad960:      	add	w8, w8, #1
fffffe00079ad964:      	str	w8, [x19, #96]
fffffe00079ad968:      	cbz	w27, 0xfffffe00079ad9b4 <_transmit+0x5f0>
fffffe00079ad96c:      	movi.2d	v0, #0000000000000000
fffffe00079ad970:      	mov	w8, #12
fffffe00079ad974:      	ldr	x9, [x19, #160]
fffffe00079ad978:      	add	x3, sp, #16
fffffe00079ad97c:      	ldrb	w10, [x19, #1584]
fffffe00079ad980:      	mov	x0, x19
fffffe00079ad984:      	mov	x1, x21
fffffe00079ad988:      	mov	x2, x20
fffffe00079ad98c:      	strb	wzr, [x19, #1585]
fffffe00079ad990:      	str	xzr, [x19, #1576]
fffffe00079ad994:      	str	xzr, [sp, #64]
fffffe00079ad998:      	stp	q0, q0, [sp, #16]
fffffe00079ad99c:      	str	q0, [sp, #48]
fffffe00079ad9a0:      	str	w8, [sp, #16]
fffffe00079ad9a4:      	str	x9, [sp, #32]
fffffe00079ad9a8:      	strb	w10, [sp, #46]
fffffe00079ad9ac:      	bl	0xfffffe00079ae188 <_emit>
fffffe00079ad9b0:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079ad9b4:      	ldr	w8, [x19, #128]
fffffe00079ad9b8:      	cbnz	w8, 0xfffffe00079ad608 <_transmit+0x244>
fffffe00079ad9bc:      	ldr	w8, [x26]
fffffe00079ad9c0:      	cbz	w8, 0xfffffe00079ada74 <_transmit+0x6b0>
fffffe00079ad9c4:      	cmp	w8, #1
fffffe00079ad9c8:      	b.ne	0xfffffe00079ad608 <_transmit+0x244>
fffffe00079ad9cc:      	ldr	w8, [x19, #1556]
fffffe00079ad9d0:      	mov	w0, wzr
fffffe00079ad9d4:      	add	w8, w8, #1
fffffe00079ad9d8:      	str	w8, [x19, #1556]
fffffe00079ad9dc:      	b	0xfffffe00079ad60c <_transmit+0x248>
fffffe00079ad9e0:      	ldr	w8, [x19, #1508]
fffffe00079ad9e4:      	mov	w9, #1
fffffe00079ad9e8:      	ldrb	w10, [x19, #1837]
fffffe00079ad9ec:      	mov	w27, wzr
fffffe00079ad9f0:      	add	w8, w8, #1
fffffe00079ad9f4:      	str	w9, [x19, #1540]
fffffe00079ad9f8:      	strb	w10, [x19, #1744]
fffffe00079ad9fc:      	str	w9, [x19, #1516]
fffffe00079ada00:      	str	w8, [x19, #1508]
fffffe00079ada04:      	b	0xfffffe00079ad894 <_transmit+0x4d0>
fffffe00079ada08:      	ldr	w8, [x19, #68]
fffffe00079ada0c:      	cbz	w8, 0xfffffe00079ada18 <_transmit+0x654>
fffffe00079ada10:      	ldr	w9, [x19, #72]
fffffe00079ada14:      	cbz	w9, 0xfffffe00079adac8 <_transmit+0x704>
fffffe00079ada18:      	mov	x0, x19
fffffe00079ada1c:      	mov	w1, #35
fffffe00079ada20:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079ada24:      	ldrb	w9, [x19, #1837]
fffffe00079ada28:      	mov	w10, #144
fffffe00079ada2c:      	mov	x8, xzr
fffffe00079ada30:      	str	wzr, [x19, #1516]
fffffe00079ada34:      	madd	x9, x9, x10, x19
fffffe00079ada38:      	add	x9, x9, #920
fffffe00079ada3c:      	strb	wzr, [x9, x8]
fffffe00079ada40:      	add	x8, x8, #1
fffffe00079ada44:      	cmp	x8, #144
fffffe00079ada48:      	b.ne	0xfffffe00079ada3c <_transmit+0x678>
fffffe00079ada4c:      	mov	x9, xzr
fffffe00079ada50:      	add	x8, x19, #776
fffffe00079ada54:      	strb	wzr, [x8, x9]
fffffe00079ada58:      	add	x9, x9, #1
fffffe00079ada5c:      	cmp	x9, #144
fffffe00079ada60:      	b.ne	0xfffffe00079ada54 <_transmit+0x690>
fffffe00079ada64:      	ldr	w9, [x19, #920]
fffffe00079ada68:      	cbz	w9, 0xfffffe00079adab8 <_transmit+0x6f4>
fffffe00079ada6c:      	add	x9, x19, #920
fffffe00079ada70:      	b	0xfffffe00079adbb0 <_transmit+0x7ec>
fffffe00079ada74:      	movi.2d	v0, #0000000000000000
fffffe00079ada78:      	mov	w8, #1
fffffe00079ada7c:      	ldp	w10, w11, [x19, #112]
fffffe00079ada80:      	ldr	x9, [x19, #160]
fffffe00079ada84:      	add	x3, sp, #16
fffffe00079ada88:      	mov	x0, x19
fffffe00079ada8c:      	mov	x1, x21
fffffe00079ada90:      	mov	x2, x20
fffffe00079ada94:      	str	xzr, [sp, #64]
fffffe00079ada98:      	stp	q0, q0, [sp, #16]
fffffe00079ada9c:      	str	q0, [sp, #48]
fffffe00079adaa0:      	str	w8, [sp, #16]
fffffe00079adaa4:      	str	x9, [sp, #32]
fffffe00079adaa8:      	strb	w10, [sp, #40]
fffffe00079adaac:      	strb	w11, [sp, #41]
fffffe00079adab0:      	bl	0xfffffe00079ae188 <_emit>
fffffe00079adab4:      	b	0xfffffe00079ad60c <_transmit+0x248>
fffffe00079adab8:      	ldr	w9, [x19, #1064]
fffffe00079adabc:      	cbz	w9, 0xfffffe00079adb94 <_transmit+0x7d0>
fffffe00079adac0:      	add	x9, x19, #1064
fffffe00079adac4:      	b	0xfffffe00079adbb0 <_transmit+0x7ec>
fffffe00079adac8:      	ldr	w9, [x19, #76]
fffffe00079adacc:      	sub	w8, w8, #1
fffffe00079adad0:      	mov	w10, #1
fffffe00079adad4:      	mov	w11, #2
fffffe00079adad8:      	add	x25, x19, #1761
fffffe00079adadc:      	ldr	w5, [x19, #108]
fffffe00079adae0:      	add	w9, w9, #1
fffffe00079adae4:      	mov	x0, x19
fffffe00079adae8:      	stp	w8, w10, [x19, #68]
fffffe00079adaec:      	ldr	x8, [x19]
fffffe00079adaf0:      	str	w11, [x19, #40]
fffffe00079adaf4:      	mov	x1, x23
fffffe00079adaf8:      	str	w9, [x19, #76]
fffffe00079adafc:      	mov	x2, x22
fffffe00079adb00:      	ldr	x3, [x8, #2864]
fffffe00079adb04:      	mov	x4, x25
fffffe00079adb08:      	bl	0xfffffe00079b00b8 <_mw>
fffffe00079adb0c:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079adb10:      	ldr	x8, [x19]
fffffe00079adb14:      	mov	x0, x19
fffffe00079adb18:      	ldr	w4, [x19, #108]
fffffe00079adb1c:      	mov	x1, x23
fffffe00079adb20:      	mov	x2, x22
fffffe00079adb24:      	mov	w5, #1
fffffe00079adb28:      	ldr	x3, [x8, #2864]
fffffe00079adb2c:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079adb30:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079adb34:      	mov	x0, x19
fffffe00079adb38:      	mov	x1, x23
fffffe00079adb3c:      	mov	x2, x22
fffffe00079adb40:      	mov	x3, x24
fffffe00079adb44:      	mov	x4, x26
fffffe00079adb48:      	mov	w5, #16
fffffe00079adb4c:      	bl	0xfffffe00079b00b8 <_mw>
fffffe00079adb50:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079adb54:      	mov	x0, x19
fffffe00079adb58:      	mov	x1, x23
fffffe00079adb5c:      	mov	x2, x22
fffffe00079adb60:      	mov	x3, x24
fffffe00079adb64:      	mov	w4, #16
fffffe00079adb68:      	mov	w5, #1
fffffe00079adb6c:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079adb70:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079adb74:      	ldr	x8, [x19, #168]
fffffe00079adb78:      	mov	x9, #-50880
fffffe00079adb7c:      	movk	x9, #65490, lsl #16
fffffe00079adb80:      	cmp	x8, x9
fffffe00079adb84:      	b.lo	0xfffffe00079adbe0 <_transmit+0x81c>
fffffe00079adb88:      	mov	x0, x19
fffffe00079adb8c:      	mov	w1, #15
fffffe00079adb90:      	b	0xfffffe00079ad544 <_transmit+0x180>
fffffe00079adb94:      	ldr	w9, [x19, #1208]
fffffe00079adb98:      	cbz	w9, 0xfffffe00079adba4 <_transmit+0x7e0>
fffffe00079adb9c:      	add	x9, x19, #1208
fffffe00079adba0:      	b	0xfffffe00079adbb0 <_transmit+0x7ec>
fffffe00079adba4:      	ldr	w9, [x19, #1352]
fffffe00079adba8:      	cbz	w9, 0xfffffe00079ad890 <_transmit+0x4cc>
fffffe00079adbac:      	add	x9, x19, #1352
fffffe00079adbb0:      	ldp	q0, q1, [x9, #96]
fffffe00079adbb4:      	mov	w27, wzr
fffffe00079adbb8:      	stp	q0, q1, [x8, #96]
fffffe00079adbbc:      	ldr	q2, [x9, #128]
fffffe00079adbc0:      	ldp	q3, q0, [x9, #32]
fffffe00079adbc4:      	str	q2, [x8, #128]
fffffe00079adbc8:      	ldp	q2, q1, [x9, #64]
fffffe00079adbcc:      	stp	q3, q0, [x8, #32]
fffffe00079adbd0:      	ldp	q3, q0, [x9]
fffffe00079adbd4:      	stp	q2, q1, [x8, #64]
fffffe00079adbd8:      	stp	q3, q0, [x8]
fffffe00079adbdc:      	b	0xfffffe00079ad894 <_transmit+0x4d0>
fffffe00079adbe0:      	add	x8, x8, #732, lsl #12
fffffe00079adbe4:      	mov	w9, #3
fffffe00079adbe8:      	add	x8, x8, #1728
fffffe00079adbec:      	ldr	x10, [x19]
fffffe00079adbf0:      	mov	w26, #1
fffffe00079adbf4:      	mov	w3, #21528
fffffe00079adbf8:      	str	w9, [x19, #40]
fffffe00079adbfc:      	mov	x0, x19
fffffe00079adc00:      	str	x8, [x19, #184]
fffffe00079adc04:      	mov	x1, x23
fffffe00079adc08:      	str	w26, [x10, #1916]
fffffe00079adc0c:      	mov	x2, x22
fffffe00079adc10:      	ldr	w8, [x19, #84]
fffffe00079adc14:      	movk	w3, #6146, lsl #16
fffffe00079adc18:      	add	w8, w8, #1
fffffe00079adc1c:      	and	w4, w8, #0x3ff
fffffe00079adc20:      	bl	0xfffffe00079b015c <_rw>
fffffe00079adc24:      	cbnz	w0, 0xfffffe00079ad7b0 <_transmit+0x3ec>
fffffe00079adc28:      	ldr	w8, [x19, #92]
fffffe00079adc2c:      	str	w26, [x19, #132]
fffffe00079adc30:      	ldr	w9, [x19, #68]
fffffe00079adc34:      	ldr	w10, [x19, #80]
fffffe00079adc38:      	ldr	w3, [x19, #128]
fffffe00079adc3c:      	add	w2, w8, #1
fffffe00079adc40:      	add	w8, w9, #1
fffffe00079adc44:      	add	w9, w10, #1
fffffe00079adc48:      	cmp	w3, #1
fffffe00079adc4c:      	str	w2, [x19, #92]
fffffe00079adc50:      	stp	w8, wzr, [x19, #68]
fffffe00079adc54:      	str	w9, [x19, #80]
fffffe00079adc58:      	b.ne	0xfffffe00079adc70 <_transmit+0x8ac>
fffffe00079adc5c:      	ldr	w8, [x19, #104]
fffffe00079adc60:      	mov	w9, #1
fffffe00079adc64:      	add	w8, w8, #1
fffffe00079adc68:      	str	w9, [x19, #60]
fffffe00079adc6c:      	str	w8, [x19, #104]
fffffe00079adc70:      	ldr	w8, [x19, #108]
fffffe00079adc74:      	mov	w9, #9776
fffffe00079adc78:      	add	x0, x19, x9
fffffe00079adc7c:      	ldr	x1, [x19, #152]
fffffe00079adc80:      	add	x4, x19, #1793
fffffe00079adc84:      	sub	w5, w8, #32
fffffe00079adc88:      	bl	0xfffffe00079b1a7c <_nsdd_publish>
fffffe00079adc8c:      	ldp	x1, x2, [x19, #152]
fffffe00079adc90:      	movi.2d	v0, #0000000000000000
fffffe00079adc94:      	add	x0, sp, #16
fffffe00079adc98:      	ldr	w4, [x19, #108]
fffffe00079adc9c:      	mov	x3, x25
fffffe00079adca0:      	ldr	w5, [x19, #128]
fffffe00079adca4:      	mov	w6, wzr
fffffe00079adca8:      	str	xzr, [sp, #64]
fffffe00079adcac:      	stp	q0, q0, [sp, #32]
fffffe00079adcb0:      	str	q0, [sp, #16]
fffffe00079adcb4:      	bl	0xfffffe00079b10f8 <_nstd_tx>
fffffe00079adcb8:      	add	x0, sp, #16
fffffe00079adcbc:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079adcc0:      	ldp	x1, x2, [x19, #152]
fffffe00079adcc4:      	add	x0, sp, #16
fffffe00079adcc8:      	mov	x3, x25
fffffe00079adccc:      	ldr	w4, [x19, #108]
fffffe00079adcd0:      	mov	w6, #1
fffffe00079adcd4:      	ldr	w5, [x19, #128]
fffffe00079adcd8:      	bl	0xfffffe00079b10f8 <_nstd_tx>
fffffe00079adcdc:      	add	x0, sp, #16
fffffe00079adce0:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079adce4:      	ldr	w8, [x19, #112]
fffffe00079adce8:      	cmp	w8, #7
fffffe00079adcec:      	b.eq	0xfffffe00079add28 <_transmit+0x964>
fffffe00079adcf0:      	cmp	w8, #28
fffffe00079adcf4:      	b.eq	0xfffffe00079add44 <_transmit+0x980>
fffffe00079adcf8:      	cmp	w8, #19
fffffe00079adcfc:      	b.ne	0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add00:      	ldrb	w8, [x19, #1845]
fffffe00079add04:      	cmp	w8, #3
fffffe00079add08:      	b.ne	0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add0c:      	ldrb	w8, [x19, #1876]
fffffe00079add10:      	cbz	w8, 0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add14:      	mov	w8, #1
fffffe00079add18:      	ldr	w9, [x19, #116]
fffffe00079add1c:      	str	w8, [x19, #1544]
fffffe00079add20:      	str	w9, [x19, #1548]
fffffe00079add24:      	b	0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add28:      	ldrb	w8, [x19, #1825]
fffffe00079add2c:      	cbz	w8, 0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add30:      	ldrb	w8, [x19, #1827]
fffffe00079add34:      	cbz	w8, 0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add38:      	mov	w8, #1
fffffe00079add3c:      	str	w8, [x19, #1532]
fffffe00079add40:      	b	0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add44:      	ldrb	w8, [x19, #1827]
fffffe00079add48:      	cbnz	w8, 0xfffffe00079ad48c <_transmit+0xc8>
fffffe00079add4c:      	ldrb	w8, [x19, #1826]
fffffe00079add50:      	mov	w9, #1
fffffe00079add54:      	strb	w8, [x19, #1584]
fffffe00079add58:      	strb	w9, [x19, #1585]
fffffe00079add5c:      	b	0xfffffe00079ad48c <_transmit+0xc8>
