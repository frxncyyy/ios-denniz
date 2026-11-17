
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ae590 <_decode>:
fffffe00079ae590:      	sub	sp, sp, #176
fffffe00079ae594:      	stp	x28, x27, [sp, #80]
fffffe00079ae598:      	stp	x26, x25, [sp, #96]
fffffe00079ae59c:      	stp	x24, x23, [sp, #112]
fffffe00079ae5a0:      	stp	x22, x21, [sp, #128]
fffffe00079ae5a4:      	stp	x20, x19, [sp, #144]
fffffe00079ae5a8:      	stp	x29, x30, [sp, #160]
fffffe00079ae5ac:      	add	x29, sp, #160
fffffe00079ae5b0:      	movi.2d	v0, #0000000000000000
fffffe00079ae5b4:      	ldr	w21, [x0, #9116]
fffffe00079ae5b8:      	str	xzr, [x0, #9168]
fffffe00079ae5bc:      	cmp	w21, #24
fffffe00079ae5c0:      	str	q0, [x0, #9120]
fffffe00079ae5c4:      	str	q0, [x0, #9136]
fffffe00079ae5c8:      	str	q0, [x0, #9152]
fffffe00079ae5cc:      	b.lo	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae5d0:      	mov	w8, #5273
fffffe00079ae5d4:      	mov	x19, x0
fffffe00079ae5d8:      	add	x20, x0, x8
fffffe00079ae5dc:      	mov	w8, #5274
fffffe00079ae5e0:      	add	x23, x0, x8
fffffe00079ae5e4:      	ldrb	w22, [x20, #3]
fffffe00079ae5e8:      	cmp	w22, #7
fffffe00079ae5ec:      	b.hi	0xfffffe00079ae620 <_decode+0x90>
fffffe00079ae5f0:      	mov	w8, #6
fffffe00079ae5f4:      	ldur	q0, [x19, #152]
fffffe00079ae5f8:      	movi.2d	v1, #0000000000000000
fffffe00079ae5fc:      	add	x0, sp, #16
fffffe00079ae600:      	str	xzr, [sp, #64]
fffffe00079ae604:      	str	w8, [sp, #32]
fffffe00079ae608:      	ldrh	w8, [x20]
fffffe00079ae60c:      	str	q0, [sp, #16]
fffffe00079ae610:      	stur	q1, [sp, #36]
fffffe00079ae614:      	stur	q1, [sp, #52]
fffffe00079ae618:      	stp	w21, w8, [sp, #40]
fffffe00079ae61c:      	bl	0xfffffe00079af698 <_nstd_log>
fffffe00079ae620:      	lsr	w8, w22, #3
fffffe00079ae624:      	cmp	w8, #2
fffffe00079ae628:      	b.eq	0xfffffe00079ae770 <_decode+0x1e0>
fffffe00079ae62c:      	cmp	w8, #7
fffffe00079ae630:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae634:      	ldrh	w8, [x20, #2]
fffffe00079ae638:      	mov	w22, #14351
fffffe00079ae63c:      	mov	w10, #14336
fffffe00079ae640:      	and	w9, w8, w22
fffffe00079ae644:      	cmp	w9, w10
fffffe00079ae648:      	b.ne	0xfffffe00079ae6a8 <_decode+0x118>
fffffe00079ae64c:      	cmp	w21, #29
fffffe00079ae650:      	b.lo	0xfffffe00079ae6a8 <_decode+0x118>
fffffe00079ae654:      	ldrb	w9, [x20, #28]
fffffe00079ae658:      	cmp	w9, #15
fffffe00079ae65c:      	b.ne	0xfffffe00079ae6a8 <_decode+0x118>
fffffe00079ae660:      	ldp	x1, x2, [x19, #152]
fffffe00079ae664:      	movi.2d	v0, #0000000000000000
fffffe00079ae668:      	add	x0, sp, #16
fffffe00079ae66c:      	ldp	w6, w7, [x19, #120]
fffffe00079ae670:      	ldr	w5, [x19, #60]
fffffe00079ae674:      	mov	x3, x20
fffffe00079ae678:      	ldr	w8, [x19, #140]
fffffe00079ae67c:      	mov	x4, x21
fffffe00079ae680:      	str	xzr, [sp, #64]
fffffe00079ae684:      	stp	q0, q0, [sp, #32]
fffffe00079ae688:      	str	q0, [sp, #16]
fffffe00079ae68c:      	str	w8, [sp]
fffffe00079ae690:      	bl	0xfffffe00079af41c <_nstd_txs>
fffffe00079ae694:      	add	x0, sp, #16
fffffe00079ae698:      	bl	0xfffffe00079af698 <_nstd_log>
fffffe00079ae69c:      	ldrb	w8, [x20, #2]
fffffe00079ae6a0:      	ldrb	w9, [x20, #3]
fffffe00079ae6a4:      	bfi	w8, w9, #8, #24
fffffe00079ae6a8:      	and	w8, w8, w22
fffffe00079ae6ac:      	mov	w9, #14337
fffffe00079ae6b0:      	cmp	w8, w9
fffffe00079ae6b4:      	b.eq	0xfffffe00079ae778 <_decode+0x1e8>
fffffe00079ae6b8:      	mov	w0, wzr
fffffe00079ae6bc:      	mov	w9, #14336
fffffe00079ae6c0:      	cmp	w8, w9
fffffe00079ae6c4:      	b.ne	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae6c8:      	cmp	w21, #36
fffffe00079ae6cc:      	b.lo	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae6d0:      	ldrb	w8, [x20, #28]
fffffe00079ae6d4:      	add	x22, x20, #36
fffffe00079ae6d8:      	sub	x21, x21, #36
fffffe00079ae6dc:      	ldrb	w9, [x20, #29]
fffffe00079ae6e0:      	cmp	w8, #15
fffffe00079ae6e4:      	str	x22, [x19, #9160]
fffffe00079ae6e8:      	str	x21, [x19, #9168]
fffffe00079ae6ec:      	strb	w9, [x20, #3872]
fffffe00079ae6f0:      	b.ne	0xfffffe00079aeafc <_decode+0x56c>
fffffe00079ae6f4:      	ldr	w8, [x19, #920]
fffffe00079ae6f8:      	mov	w9, #9176
fffffe00079ae6fc:      	add	x0, x19, x9
fffffe00079ae700:      	ldr	x1, [x19, #152]
fffffe00079ae704:      	mov	x2, x22
fffffe00079ae708:      	mov	x3, x21
fffffe00079ae70c:      	cmp	w8, #0
fffffe00079ae710:      	cset	w4, ne
fffffe00079ae714:      	bl	0xfffffe00079aff20 <_nsdd_event>
fffffe00079ae718:      	ldrb	w8, [x20, #28]
fffffe00079ae71c:      	cmp	w8, #15
fffffe00079ae720:      	b.ne	0xfffffe00079aeafc <_decode+0x56c>
fffffe00079ae724:      	cmp	x21, #88
fffffe00079ae728:      	b.lo	0xfffffe00079aeafc <_decode+0x56c>
fffffe00079ae72c:      	ldr	w8, [x19, #60]
fffffe00079ae730:      	cbz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae734:      	ldrb	w8, [x22]
fffffe00079ae738:      	ldr	w9, [x19, #120]
fffffe00079ae73c:      	cmp	w9, w8
fffffe00079ae740:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae744:      	ldrb	w8, [x20, #40]
fffffe00079ae748:      	ldr	w9, [x19, #124]
fffffe00079ae74c:      	cmp	w9, w8
fffffe00079ae750:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae754:      	ldr	w8, [x19, #140]
fffffe00079ae758:      	cbnz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae75c:      	mov	w8, #1
fffffe00079ae760:      	ldrb	w9, [x20, #37]
fffffe00079ae764:      	mov	w0, wzr
fffffe00079ae768:      	stp	w8, w9, [x19, #140]
fffffe00079ae76c:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae770:      	mov	w14, #1
fffffe00079ae774:      	b	0xfffffe00079ae77c <_decode+0x1ec>
fffffe00079ae778:      	mov	w14, wzr
fffffe00079ae77c:      	ldrb	w9, [x20]
fffffe00079ae780:      	mov	w0, wzr
fffffe00079ae784:      	ldrb	w8, [x23]
fffffe00079ae788:      	bfi	x9, x8, #8, #8
fffffe00079ae78c:      	cmp	w9, #24
fffffe00079ae790:      	b.lo	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae794:      	cmp	x9, x21
fffffe00079ae798:      	b.hi	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae79c:      	ldrb	w11, [x23, #5]
fffffe00079ae7a0:      	ldrb	w8, [x23, #6]
fffffe00079ae7a4:      	lsl	w10, w11, #16
fffffe00079ae7a8:      	bfi	w10, w8, #24, #8
fffffe00079ae7ac:      	and	w8, w10, #0xf800000
fffffe00079ae7b0:      	cbnz	w8, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae7b4:      	ldrb	w15, [x23, #9]
fffffe00079ae7b8:      	ldrb	w8, [x23, #10]
fffffe00079ae7bc:      	lsl	w10, w15, #16
fffffe00079ae7c0:      	bfi	w10, w8, #24, #8
fffffe00079ae7c4:      	mov	w8, #192937984
fffffe00079ae7c8:      	and	w8, w10, w8
fffffe00079ae7cc:      	cbnz	w8, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae7d0:      	ldrb	w8, [x23, #15]
fffffe00079ae7d4:      	and	w8, w8, #0x3
fffffe00079ae7d8:      	cbnz	w8, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae7dc:      	ldrb	w8, [x23, #16]
fffffe00079ae7e0:      	and	w8, w8, #0x18
fffffe00079ae7e4:      	cbnz	w8, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae7e8:      	ldrb	w13, [x23, #4]
fffffe00079ae7ec:      	mov	w8, #40
fffffe00079ae7f0:      	mov	w10, #24
fffffe00079ae7f4:      	ldrb	w16, [x23, #8]
fffffe00079ae7f8:      	tst	w13, #0x40
fffffe00079ae7fc:      	csel	x8, x10, x8, eq
fffffe00079ae800:      	csel	x17, xzr, x10, eq
fffffe00079ae804:      	tst	w13, #0x8
fffffe00079ae808:      	add	x12, x8, #16
fffffe00079ae80c:      	csel	x10, xzr, x8, eq
fffffe00079ae810:      	csel	x8, x8, x12, eq
fffffe00079ae814:      	tst	w13, #0x10
fffffe00079ae818:      	add	x12, x8, #8
fffffe00079ae81c:      	csel	x8, x8, x12, eq
fffffe00079ae820:      	tst	w13, #0x20
fffffe00079ae824:      	add	x12, x8, #8
fffffe00079ae828:      	csel	x8, x8, x12, eq
fffffe00079ae82c:      	sxtb	w12, w13
fffffe00079ae830:      	add	x0, x8, #72
fffffe00079ae834:      	cmp	w12, #0
fffffe00079ae838:      	lsr	x12, x16, #5
fffffe00079ae83c:      	csel	x8, x0, x8, lt
fffffe00079ae840:      	and	x12, x12, #0x6
fffffe00079ae844:      	add	x8, x8, x12
fffffe00079ae848:      	cmp	x8, x9
fffffe00079ae84c:      	b.ls	0xfffffe00079ae874 <_decode+0x2e4>
fffffe00079ae850:      	mov	w0, wzr
fffffe00079ae854:      	ldp	x29, x30, [sp, #160]
fffffe00079ae858:      	ldp	x20, x19, [sp, #144]
fffffe00079ae85c:      	ldp	x22, x21, [sp, #128]
fffffe00079ae860:      	ldp	x24, x23, [sp, #112]
fffffe00079ae864:      	ldp	x26, x25, [sp, #96]
fffffe00079ae868:      	ldp	x28, x27, [sp, #80]
fffffe00079ae86c:      	add	sp, sp, #176
fffffe00079ae870:      	ret
fffffe00079ae874:      	ldrb	w22, [x23, #3]
fffffe00079ae878:      	lsl	w12, w13, #8
fffffe00079ae87c:      	add	x21, x20, x8
fffffe00079ae880:      	sub	x24, x9, x8
fffffe00079ae884:      	and	w8, w12, #0x300
fffffe00079ae888:      	and	w12, w11, #0x1f
fffffe00079ae88c:      	orr	w8, w8, w22
fffffe00079ae890:      	tbnz	w16, #5, 0xfffffe00079ae8ec <_decode+0x35c>
fffffe00079ae894:      	cmp	x24, #1
fffffe00079ae898:      	cset	w9, hi
fffffe00079ae89c:      	and	w9, w14, w9
fffffe00079ae8a0:      	cmp	w9, #1
fffffe00079ae8a4:      	b.ne	0xfffffe00079aebb4 <_decode+0x624>
fffffe00079ae8a8:      	ldrb	w9, [x21]
fffffe00079ae8ac:      	and	w9, w9, #0xc
fffffe00079ae8b0:      	cmp	w9, #8
fffffe00079ae8b4:      	b.ne	0xfffffe00079aebb4 <_decode+0x624>
fffffe00079ae8b8:      	ldr	w9, [x19, #1112]
fffffe00079ae8bc:      	subs	x10, x24, #36
fffffe00079ae8c0:      	add	w9, w9, #1
fffffe00079ae8c4:      	str	w9, [x19, #1112]
fffffe00079ae8c8:      	b.lo	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079ae8cc:      	sub	x9, x24, #18
fffffe00079ae8d0:      	cmp	x9, #1514
fffffe00079ae8d4:      	b.ls	0xfffffe00079aee90 <_decode+0x900>
fffffe00079ae8d8:      	ldr	w8, [x19, #1120]
fffffe00079ae8dc:      	mov	w0, wzr
fffffe00079ae8e0:      	add	w8, w8, #1
fffffe00079ae8e4:      	str	w8, [x19, #1120]
fffffe00079ae8e8:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae8ec:      	mov	w0, wzr
fffffe00079ae8f0:      	cbz	w14, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae8f4:      	cmp	x24, #14
fffffe00079ae8f8:      	b.lo	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae8fc:      	cmp	x24, #1514
fffffe00079ae900:      	b.hi	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae904:      	tbz	w13, #6, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae908:      	add	x14, x17, #16
fffffe00079ae90c:      	cmp	x14, x9
fffffe00079ae910:      	b.hi	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae914:      	add	x6, x20, x17
fffffe00079ae918:      	ldrb	w14, [x19, #198]
fffffe00079ae91c:      	ldrb	w17, [x6, #2]
fffffe00079ae920:      	cmp	w17, w14
fffffe00079ae924:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae928:      	ldrb	w1, [x6, #3]
fffffe00079ae92c:      	ldrb	w14, [x19, #199]
fffffe00079ae930:      	cmp	w1, w14
fffffe00079ae934:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae938:      	ldrb	w2, [x6, #4]
fffffe00079ae93c:      	ldrb	w14, [x19, #200]
fffffe00079ae940:      	cmp	w2, w14
fffffe00079ae944:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae948:      	ldrb	w3, [x6, #5]
fffffe00079ae94c:      	ldrb	w14, [x19, #201]
fffffe00079ae950:      	cmp	w3, w14
fffffe00079ae954:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae958:      	ldrb	w4, [x6, #6]
fffffe00079ae95c:      	ldrb	w14, [x19, #202]
fffffe00079ae960:      	cmp	w4, w14
fffffe00079ae964:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae968:      	ldrb	w5, [x6, #7]
fffffe00079ae96c:      	ldrb	w14, [x19, #203]
fffffe00079ae970:      	cmp	w5, w14
fffffe00079ae974:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae978:      	ldrb	w14, [x21]
fffffe00079ae97c:      	tbnz	w14, #0, 0xfffffe00079ae9dc <_decode+0x44c>
fffffe00079ae980:      	ldrb	w16, [x19, #192]
fffffe00079ae984:      	cmp	w14, w16
fffffe00079ae988:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae98c:      	ldrb	w16, [x21, #1]
fffffe00079ae990:      	ldrb	w0, [x19, #193]
fffffe00079ae994:      	cmp	w16, w0
fffffe00079ae998:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae99c:      	ldrb	w16, [x21, #2]
fffffe00079ae9a0:      	ldrb	w0, [x19, #194]
fffffe00079ae9a4:      	cmp	w16, w0
fffffe00079ae9a8:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae9ac:      	ldrb	w16, [x21, #3]
fffffe00079ae9b0:      	ldrb	w0, [x19, #195]
fffffe00079ae9b4:      	cmp	w16, w0
fffffe00079ae9b8:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae9bc:      	ldrb	w16, [x21, #4]
fffffe00079ae9c0:      	ldrb	w0, [x19, #196]
fffffe00079ae9c4:      	cmp	w16, w0
fffffe00079ae9c8:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae9cc:      	ldrb	w16, [x21, #5]
fffffe00079ae9d0:      	ldrb	w0, [x19, #197]
fffffe00079ae9d4:      	cmp	w16, w0
fffffe00079ae9d8:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079ae9dc:      	ldrb	w16, [x6, #1]
fffffe00079ae9e0:      	mov	w0, wzr
fffffe00079ae9e4:      	mov	w7, #132
fffffe00079ae9e8:      	tst	w16, w7
fffffe00079ae9ec:      	b.ne	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079ae9f0:      	ldrb	w25, [x6]
fffffe00079ae9f4:      	mov	w26, #780
fffffe00079ae9f8:      	mov	w7, w25
fffffe00079ae9fc:      	bfi	w7, w16, #8, #8
fffffe00079aea00:      	and	w7, w7, w26
fffffe00079aea04:      	cmp	w7, #520
fffffe00079aea08:      	b.ne	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aea0c:      	str	x15, [sp, #8]
fffffe00079aea10:      	ldrb	w15, [x6, #8]
fffffe00079aea14:      	tst	w15, #0xf
fffffe00079aea18:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea1c:      	ldrb	w15, [x21, #12]
fffffe00079aea20:      	cmp	w15, #136
fffffe00079aea24:      	b.ne	0xfffffe00079af17c <_decode+0xbec>
fffffe00079aea28:      	ldrb	w15, [x21, #13]
fffffe00079aea2c:      	cmp	w15, #142
fffffe00079aea30:      	b.ne	0xfffffe00079af198 <_decode+0xc08>
fffffe00079aea34:      	ldrb	w15, [x21, #6]
fffffe00079aea38:      	cmp	w15, w17
fffffe00079aea3c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea40:      	ldrb	w15, [x21, #7]
fffffe00079aea44:      	cmp	w15, w1
fffffe00079aea48:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea4c:      	ldrb	w15, [x21, #8]
fffffe00079aea50:      	cmp	w15, w2
fffffe00079aea54:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea58:      	ldrb	w15, [x21, #9]
fffffe00079aea5c:      	cmp	w15, w3
fffffe00079aea60:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea64:      	ldrb	w15, [x21, #10]
fffffe00079aea68:      	cmp	w15, w4
fffffe00079aea6c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea70:      	ldrb	w15, [x21, #11]
fffffe00079aea74:      	cmp	w15, w5
fffffe00079aea78:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea7c:      	ldrb	w15, [x19, #192]
fffffe00079aea80:      	cmp	w14, w15
fffffe00079aea84:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea88:      	ldrb	w15, [x21, #1]
fffffe00079aea8c:      	ldrb	w17, [x19, #193]
fffffe00079aea90:      	cmp	w15, w17
fffffe00079aea94:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aea98:      	ldrb	w15, [x21, #2]
fffffe00079aea9c:      	ldrb	w17, [x19, #194]
fffffe00079aeaa0:      	cmp	w15, w17
fffffe00079aeaa4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeaa8:      	ldrb	w15, [x21, #3]
fffffe00079aeaac:      	ldrb	w17, [x19, #195]
fffffe00079aeab0:      	cmp	w15, w17
fffffe00079aeab4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeab8:      	ldrb	w15, [x21, #4]
fffffe00079aeabc:      	ldrb	w17, [x19, #196]
fffffe00079aeac0:      	cmp	w15, w17
fffffe00079aeac4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeac8:      	ldrb	w15, [x21, #5]
fffffe00079aeacc:      	mov	w0, wzr
fffffe00079aead0:      	ldrb	w17, [x19, #197]
fffffe00079aead4:      	cmp	w15, w17
fffffe00079aead8:      	b.ne	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aeadc:      	subs	x15, x24, #18
fffffe00079aeae0:      	b.lo	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aeae4:      	ldrh	w17, [x21, #16]
fffffe00079aeae8:      	rev16	w17, w17
fffffe00079aeaec:      	cmp	x17, x15
fffffe00079aeaf0:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeaf4:      	mov	w26, #1
fffffe00079aeaf8:      	b	0xfffffe00079af1a4 <_decode+0xc14>
fffffe00079aeafc:      	cmp	w8, #16
fffffe00079aeb00:      	b.ne	0xfffffe00079aed38 <_decode+0x7a8>
fffffe00079aeb04:      	cmp	x21, #24
fffffe00079aeb08:      	b.lo	0xfffffe00079aed38 <_decode+0x7a8>
fffffe00079aeb0c:      	ldrb	w8, [x19, #985]
fffffe00079aeb10:      	cbz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeb14:      	ldrb	w8, [x22]
fffffe00079aeb18:      	ldrb	w9, [x19, #204]
fffffe00079aeb1c:      	cmp	w8, w9
fffffe00079aeb20:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeb24:      	ldrb	w9, [x20, #37]
fffffe00079aeb28:      	ldrb	w10, [x19, #984]
fffffe00079aeb2c:      	cmp	w9, w10
fffffe00079aeb30:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeb34:      	ldrb	w10, [x20, #39]
fffffe00079aeb38:      	ldrb	w11, [x19, #210]
fffffe00079aeb3c:      	cmp	w10, w11
fffffe00079aeb40:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeb44:      	ldrb	w11, [x20, #41]
fffffe00079aeb48:      	ldrb	w12, [x19, #209]
fffffe00079aeb4c:      	cmp	w11, w12
fffffe00079aeb50:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeb54:      	ldrb	w12, [x20, #38]
fffffe00079aeb58:      	mov	w0, wzr
fffffe00079aeb5c:      	cbnz	w12, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aeb60:      	ldr	w12, [x20, #48]
fffffe00079aeb64:      	cbz	w12, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aeb68:      	mov	w13, #60000
fffffe00079aeb6c:      	cmp	w12, w13
fffffe00079aeb70:      	b.hi	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aeb74:      	mov	w14, #1000
fffffe00079aeb78:      	ldr	x13, [x19, #168]
fffffe00079aeb7c:      	mul	x14, x12, x14
fffffe00079aeb80:      	cmn	x14, x13
fffffe00079aeb84:      	b.hs	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeb88:      	adrp	x15, 0xfffffe000700a000 <_transmit+0x2a4>
fffffe00079aeb8c:      	strb	w8, [x23, #3874]
fffffe00079aeb90:      	add	x8, x13, x14
fffffe00079aeb94:      	strb	w9, [x23, #3876]
fffffe00079aeb98:      	strb	w10, [x23, #3878]
fffffe00079aeb9c:      	ldr	d0, [x15, #3432]
fffffe00079aeba0:      	strb	w11, [x23, #3877]
fffffe00079aeba4:      	str	x8, [x19, #976]
fffffe00079aeba8:      	str	w12, [x19, #9156]
fffffe00079aebac:      	str	d0, [x19, #9120]
fffffe00079aebb0:      	b	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aebb4:      	cmp	x24, #24
fffffe00079aebb8:      	b.lo	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aebbc:      	mov	w0, wzr
fffffe00079aebc0:      	and	w8, w16, #0x1f
fffffe00079aebc4:      	cmp	w8, #12
fffffe00079aebc8:      	b.ne	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aebcc:      	cbnz	w12, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aebd0:      	ldrb	w8, [x21, #1]
fffffe00079aebd4:      	tst	w8, #0xffffffc7
fffffe00079aebd8:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aebdc:      	ldrb	w8, [x21, #22]
fffffe00079aebe0:      	tst	w8, #0xf
fffffe00079aebe4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aebe8:      	ldrb	w8, [x21, #10]
fffffe00079aebec:      	ldrb	w9, [x19, #198]
fffffe00079aebf0:      	cmp	w8, w9
fffffe00079aebf4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aebf8:      	ldrb	w9, [x21, #11]
fffffe00079aebfc:      	ldrb	w10, [x19, #199]
fffffe00079aec00:      	cmp	w9, w10
fffffe00079aec04:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec08:      	ldrb	w10, [x21, #12]
fffffe00079aec0c:      	ldrb	w11, [x19, #200]
fffffe00079aec10:      	cmp	w10, w11
fffffe00079aec14:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec18:      	ldrb	w11, [x21, #13]
fffffe00079aec1c:      	ldrb	w12, [x19, #201]
fffffe00079aec20:      	cmp	w11, w12
fffffe00079aec24:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec28:      	ldrb	w12, [x21, #14]
fffffe00079aec2c:      	ldrb	w13, [x19, #202]
fffffe00079aec30:      	cmp	w12, w13
fffffe00079aec34:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec38:      	ldrb	w13, [x21, #15]
fffffe00079aec3c:      	ldrb	w14, [x19, #203]
fffffe00079aec40:      	cmp	w13, w14
fffffe00079aec44:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec48:      	ldrb	w14, [x21, #16]
fffffe00079aec4c:      	cmp	w14, w8
fffffe00079aec50:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec54:      	ldrb	w8, [x21, #17]
fffffe00079aec58:      	cmp	w8, w9
fffffe00079aec5c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec60:      	ldrb	w8, [x21, #18]
fffffe00079aec64:      	cmp	w8, w10
fffffe00079aec68:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec6c:      	ldrb	w8, [x21, #19]
fffffe00079aec70:      	cmp	w8, w11
fffffe00079aec74:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec78:      	ldrb	w8, [x21, #20]
fffffe00079aec7c:      	cmp	w8, w12
fffffe00079aec80:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec84:      	ldrb	w8, [x21, #21]
fffffe00079aec88:      	cmp	w8, w13
fffffe00079aec8c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aec90:      	ldrb	w8, [x21]
fffffe00079aec94:      	mov	w9, #1610612736
fffffe00079aec98:      	add	w9, w9, w8, lsl #24
fffffe00079aec9c:      	extr	w9, w8, w9, #28
fffffe00079aeca0:      	and	w9, w9, #0xff
fffffe00079aeca4:      	cmp	w9, #7
fffffe00079aeca8:      	b.hi	0xfffffe00079af2a0 <_decode+0xd10>
fffffe00079aecac:      	mov	w10, #1
fffffe00079aecb0:      	lsl	w9, w10, w9
fffffe00079aecb4:      	mov	w10, #135
fffffe00079aecb8:      	tst	w9, w10
fffffe00079aecbc:      	b.eq	0xfffffe00079af2a0 <_decode+0xd10>
fffffe00079aecc0:      	ldrb	w8, [x21, #4]
fffffe00079aecc4:      	ldrb	w9, [x19, #192]
fffffe00079aecc8:      	cmp	w8, w9
fffffe00079aeccc:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aecd0:      	ldrb	w8, [x21, #5]
fffffe00079aecd4:      	ldrb	w9, [x19, #193]
fffffe00079aecd8:      	cmp	w8, w9
fffffe00079aecdc:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aece0:      	ldrb	w8, [x21, #6]
fffffe00079aece4:      	ldrb	w9, [x19, #194]
fffffe00079aece8:      	cmp	w8, w9
fffffe00079aecec:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aecf0:      	ldrb	w8, [x21, #7]
fffffe00079aecf4:      	ldrb	w9, [x19, #195]
fffffe00079aecf8:      	cmp	w8, w9
fffffe00079aecfc:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed00:      	ldrb	w8, [x21, #8]
fffffe00079aed04:      	ldrb	w9, [x19, #196]
fffffe00079aed08:      	cmp	w8, w9
fffffe00079aed0c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed10:      	ldrb	w8, [x21, #9]
fffffe00079aed14:      	ldrb	w9, [x19, #197]
fffffe00079aed18:      	cmp	w8, w9
fffffe00079aed1c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed20:      	mov	w8, #6
fffffe00079aed24:      	mov	w0, wzr
fffffe00079aed28:      	str	x21, [x19, #9160]
fffffe00079aed2c:      	str	x24, [x19, #9168]
fffffe00079aed30:      	str	w8, [x19, #9120]
fffffe00079aed34:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aed38:      	cmp	w8, #12
fffffe00079aed3c:      	b.ne	0xfffffe00079aee04 <_decode+0x874>
fffffe00079aed40:      	cmp	x21, #8
fffffe00079aed44:      	b.lo	0xfffffe00079aee04 <_decode+0x874>
fffffe00079aed48:      	ldr	w8, [x19, #944]
fffffe00079aed4c:      	cbz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed50:      	ldrb	w8, [x20, #29]
fffffe00079aed54:      	ldr	w9, [x19, #948]
fffffe00079aed58:      	cmp	w9, w8
fffffe00079aed5c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed60:      	ldrb	w8, [x22]
fffffe00079aed64:      	ldrb	w9, [x19, #198]
fffffe00079aed68:      	cmp	w8, w9
fffffe00079aed6c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed70:      	ldrb	w8, [x23, #36]
fffffe00079aed74:      	ldrb	w9, [x19, #199]
fffffe00079aed78:      	cmp	w8, w9
fffffe00079aed7c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed80:      	ldrb	w8, [x23, #37]
fffffe00079aed84:      	ldrb	w9, [x19, #200]
fffffe00079aed88:      	cmp	w8, w9
fffffe00079aed8c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aed90:      	ldrb	w8, [x23, #38]
fffffe00079aed94:      	ldrb	w9, [x19, #201]
fffffe00079aed98:      	cmp	w8, w9
fffffe00079aed9c:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aeda0:      	ldrb	w8, [x23, #39]
fffffe00079aeda4:      	ldrb	w9, [x19, #202]
fffffe00079aeda8:      	cmp	w8, w9
fffffe00079aedac:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aedb0:      	ldrb	w8, [x23, #40]
fffffe00079aedb4:      	ldrb	w9, [x19, #203]
fffffe00079aedb8:      	cmp	w8, w9
fffffe00079aedbc:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aedc0:      	ldrb	w8, [x23, #41]
fffffe00079aedc4:      	ldrb	w9, [x19, #206]
fffffe00079aedc8:      	cmp	w8, w9
fffffe00079aedcc:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aedd0:      	ldrb	w9, [x23, #42]
fffffe00079aedd4:      	ldrb	w10, [x19, #204]
fffffe00079aedd8:      	cmp	w9, w10
fffffe00079aeddc:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aede0:      	mov	w10, #1
fffffe00079aede4:      	mov	w11, #4
fffffe00079aede8:      	str	wzr, [x19, #944]
fffffe00079aedec:      	mov	w0, wzr
fffffe00079aedf0:      	strb	w8, [x23, #3875]
fffffe00079aedf4:      	str	w10, [x19, #924]
fffffe00079aedf8:      	str	w11, [x19, #9120]
fffffe00079aedfc:      	strb	w9, [x23, #3874]
fffffe00079aee00:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aee04:      	cmp	w8, #17
fffffe00079aee08:      	b.ne	0xfffffe00079aee38 <_decode+0x8a8>
fffffe00079aee0c:      	cmp	x21, #4
fffffe00079aee10:      	b.lo	0xfffffe00079aee38 <_decode+0x8a8>
fffffe00079aee14:      	ldrb	w8, [x22]
fffffe00079aee18:      	ldrb	w9, [x19, #204]
fffffe00079aee1c:      	cmp	w8, w9
fffffe00079aee20:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aee24:      	ldrb	w8, [x23, #36]
fffffe00079aee28:      	cmp	w8, #1
fffffe00079aee2c:      	b.hi	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aee30:      	str	w8, [x19, #1140]
fffffe00079aee34:      	b	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aee38:      	cmp	w8, #19
fffffe00079aee3c:      	b.ne	0xfffffe00079af098 <_decode+0xb08>
fffffe00079aee40:      	cmp	x21, #4
fffffe00079aee44:      	b.lo	0xfffffe00079af098 <_decode+0xb08>
fffffe00079aee48:      	ldrb	w8, [x22]
fffffe00079aee4c:      	ldrb	w9, [x19, #204]
fffffe00079aee50:      	cmp	w8, w9
fffffe00079aee54:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aee58:      	ldr	w8, [x19, #924]
fffffe00079aee5c:      	cbz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079aee60:      	mov	x0, x19
fffffe00079aee64:      	bl	0xfffffe00079aa740 <_nst_close_port>
fffffe00079aee68:      	mov	w8, #1
fffffe00079aee6c:      	mov	w9, #13
fffffe00079aee70:      	ldrb	w10, [x23, #36]
fffffe00079aee74:      	mov	w0, wzr
fffffe00079aee78:      	ldrb	w11, [x22]
fffffe00079aee7c:      	str	w8, [x19, #1108]
fffffe00079aee80:      	str	w9, [x19, #9120]
fffffe00079aee84:      	str	w10, [x19, #9124]
fffffe00079aee88:      	strb	w11, [x23, #3874]
fffffe00079aee8c:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079aee90:      	and	w11, w16, #0x1f
fffffe00079aee94:      	cmp	w11, #12
fffffe00079aee98:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aee9c:      	cbnz	w12, 0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aeea0:      	ldrb	w11, [x21, #1]
fffffe00079aeea4:      	mov	w13, #50944
fffffe00079aeea8:      	ldrb	w12, [x21]
fffffe00079aeeac:      	and	w11, w13, w11, lsl #8
fffffe00079aeeb0:      	orr	w11, w11, w12
fffffe00079aeeb4:      	cmp	w11, #520
fffffe00079aeeb8:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aeebc:      	ldrb	w11, [x21, #22]
fffffe00079aeec0:      	tst	w11, #0xf
fffffe00079aeec4:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aeec8:      	ldrb	w11, [x19, #207]
fffffe00079aeecc:      	cmp	w8, w11
fffffe00079aeed0:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aeed4:      	ldrb	w8, [x21, #4]
fffffe00079aeed8:      	ldrb	w11, [x19, #192]
fffffe00079aeedc:      	cmp	w8, w11
fffffe00079aeee0:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aeee4:      	ldrb	w11, [x21, #5]
fffffe00079aeee8:      	ldrb	w12, [x19, #193]
fffffe00079aeeec:      	cmp	w11, w12
fffffe00079aeef0:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aeef4:      	ldrb	w12, [x21, #6]
fffffe00079aeef8:      	ldrb	w13, [x19, #194]
fffffe00079aeefc:      	cmp	w12, w13
fffffe00079aef00:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef04:      	ldrb	w13, [x21, #7]
fffffe00079aef08:      	ldrb	w14, [x19, #195]
fffffe00079aef0c:      	cmp	w13, w14
fffffe00079aef10:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef14:      	ldrb	w14, [x21, #8]
fffffe00079aef18:      	ldrb	w15, [x19, #196]
fffffe00079aef1c:      	cmp	w14, w15
fffffe00079aef20:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef24:      	ldrb	w15, [x21, #9]
fffffe00079aef28:      	ldrb	w16, [x19, #197]
fffffe00079aef2c:      	cmp	w15, w16
fffffe00079aef30:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef34:      	ldrb	w16, [x21, #10]
fffffe00079aef38:      	ldrb	w17, [x19, #198]
fffffe00079aef3c:      	cmp	w16, w17
fffffe00079aef40:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef44:      	ldrb	w17, [x21, #11]
fffffe00079aef48:      	ldrb	w0, [x19, #199]
fffffe00079aef4c:      	cmp	w17, w0
fffffe00079aef50:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef54:      	ldrb	w1, [x21, #12]
fffffe00079aef58:      	ldrb	w0, [x19, #200]
fffffe00079aef5c:      	cmp	w1, w0
fffffe00079aef60:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef64:      	ldrb	w2, [x21, #13]
fffffe00079aef68:      	ldrb	w0, [x19, #201]
fffffe00079aef6c:      	cmp	w2, w0
fffffe00079aef70:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef74:      	ldrb	w3, [x21, #14]
fffffe00079aef78:      	ldrb	w0, [x19, #202]
fffffe00079aef7c:      	cmp	w3, w0
fffffe00079aef80:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef84:      	ldrb	w4, [x21, #15]
fffffe00079aef88:      	ldrb	w0, [x19, #203]
fffffe00079aef8c:      	cmp	w4, w0
fffffe00079aef90:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aef94:      	ldrb	w0, [x21, #16]
fffffe00079aef98:      	cmp	w0, w16
fffffe00079aef9c:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aefa0:      	ldrb	w0, [x21, #17]
fffffe00079aefa4:      	cmp	w0, w17
fffffe00079aefa8:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aefac:      	ldrb	w0, [x21, #18]
fffffe00079aefb0:      	cmp	w0, w1
fffffe00079aefb4:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aefb8:      	ldrb	w0, [x21, #19]
fffffe00079aefbc:      	cmp	w0, w2
fffffe00079aefc0:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aefc4:      	ldrb	w0, [x21, #20]
fffffe00079aefc8:      	cmp	w0, w3
fffffe00079aefcc:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aefd0:      	ldrb	w0, [x21, #21]
fffffe00079aefd4:      	cmp	w0, w4
fffffe00079aefd8:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aefdc:      	ldrb	w0, [x21, #24]
fffffe00079aefe0:      	cmp	w0, #170
fffffe00079aefe4:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aefe8:      	ldrb	w0, [x21, #25]
fffffe00079aefec:      	cmp	w0, #170
fffffe00079aeff0:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079aeff4:      	ldrb	w0, [x21, #26]
fffffe00079aeff8:      	cmp	w0, #3
fffffe00079aeffc:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079af000:      	ldrb	w0, [x21, #27]
fffffe00079af004:      	cbnz	w0, 0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079af008:      	ldrb	w0, [x21, #28]
fffffe00079af00c:      	cbnz	w0, 0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079af010:      	ldrb	w0, [x21, #29]
fffffe00079af014:      	cbnz	w0, 0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079af018:      	ldrb	w0, [x21, #30]
fffffe00079af01c:      	cmp	w0, #136
fffffe00079af020:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079af024:      	ldrb	w0, [x21, #31]
fffffe00079af028:      	cmp	w0, #142
fffffe00079af02c:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079af030:      	ldrh	w0, [x21, #34]
fffffe00079af034:      	rev16	w0, w0
fffffe00079af038:      	cmp	x0, x10
fffffe00079af03c:      	b.ne	0xfffffe00079ae8d8 <_decode+0x348>
fffffe00079af040:      	strb	w8, [x21, #18]!
fffffe00079af044:      	strb	w11, [x21, #1]
fffffe00079af048:      	mov	w10, #7
fffffe00079af04c:      	strb	w12, [x21, #2]
fffffe00079af050:      	mov	w0, wzr
fffffe00079af054:      	strb	w13, [x21, #3]
fffffe00079af058:      	strb	w14, [x21, #4]
fffffe00079af05c:      	strb	w15, [x21, #5]
fffffe00079af060:      	strb	w16, [x21, #6]
fffffe00079af064:      	strb	w17, [x21, #7]
fffffe00079af068:      	strb	w1, [x21, #8]
fffffe00079af06c:      	strb	w2, [x21, #9]
fffffe00079af070:      	strb	w3, [x21, #10]
fffffe00079af074:      	strb	w4, [x21, #11]
fffffe00079af078:      	ldr	w8, [x19, #1116]
fffffe00079af07c:      	str	w10, [x19, #9120]
fffffe00079af080:      	str	x21, [x19, #9160]
fffffe00079af084:      	str	x9, [x19, #9168]
fffffe00079af088:      	add	w8, w8, #1
fffffe00079af08c:      	strb	w22, [x23, #3873]
fffffe00079af090:      	str	w8, [x19, #1116]
fffffe00079af094:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af098:      	cmp	w8, #36
fffffe00079af09c:      	b.ne	0xfffffe00079af14c <_decode+0xbbc>
fffffe00079af0a0:      	cmp	x21, #8
fffffe00079af0a4:      	b.lo	0xfffffe00079af14c <_decode+0xbbc>
fffffe00079af0a8:      	ldr	w8, [x19, #932]
fffffe00079af0ac:      	cbz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af0b0:      	ldr	w8, [x19, #928]
fffffe00079af0b4:      	cbnz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af0b8:      	ldrb	w8, [x22]
fffffe00079af0bc:      	ldrb	w9, [x19, #204]
fffffe00079af0c0:      	cmp	w8, w9
fffffe00079af0c4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af0c8:      	ldrb	w9, [x23, #37]
fffffe00079af0cc:      	ldrb	w10, [x19, #198]
fffffe00079af0d0:      	cmp	w9, w10
fffffe00079af0d4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af0d8:      	ldrb	w9, [x23, #38]
fffffe00079af0dc:      	ldrb	w10, [x19, #199]
fffffe00079af0e0:      	cmp	w9, w10
fffffe00079af0e4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af0e8:      	ldrb	w9, [x23, #39]
fffffe00079af0ec:      	ldrb	w10, [x19, #200]
fffffe00079af0f0:      	cmp	w9, w10
fffffe00079af0f4:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af0f8:      	ldrb	w9, [x23, #40]
fffffe00079af0fc:      	ldrb	w10, [x19, #201]
fffffe00079af100:      	cmp	w9, w10
fffffe00079af104:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af108:      	ldrb	w9, [x23, #41]
fffffe00079af10c:      	ldrb	w10, [x19, #202]
fffffe00079af110:      	cmp	w9, w10
fffffe00079af114:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af118:      	ldrb	w9, [x23, #42]
fffffe00079af11c:      	ldrb	w10, [x19, #203]
fffffe00079af120:      	cmp	w9, w10
fffffe00079af124:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af128:      	mov	w9, #1
fffffe00079af12c:      	mov	w10, #5
fffffe00079af130:      	strb	w8, [x23, #3874]
fffffe00079af134:      	mov	w8, #8
fffffe00079af138:      	mov	w0, wzr
fffffe00079af13c:      	str	w9, [x19, #928]
fffffe00079af140:      	str	w10, [x19, #9120]
fffffe00079af144:      	str	x8, [x19, #9168]
fffffe00079af148:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af14c:      	mov	w0, #-1
fffffe00079af150:      	cmp	w8, #253
fffffe00079af154:      	b.eq	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af158:      	cmp	w8, #240
fffffe00079af15c:      	b.eq	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af160:      	cmp	w8, #237
fffffe00079af164:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af168:      	ldrb	w8, [x23, #31]
fffffe00079af16c:      	cmp	w8, #35
fffffe00079af170:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af174:      	mov	w0, #-1
fffffe00079af178:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af17c:      	ldr	w17, [x19, #920]
fffffe00079af180:      	cbz	w17, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af184:      	mov	w26, wzr
fffffe00079af188:      	mov	w0, wzr
fffffe00079af18c:      	cmp	w15, #6
fffffe00079af190:      	b.hs	0xfffffe00079af1a4 <_decode+0xc14>
fffffe00079af194:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af198:      	ldr	w15, [x19, #920]
fffffe00079af19c:      	mov	w26, wzr
fffffe00079af1a0:      	cbz	w15, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af1a4:      	ubfx	w27, w11, #5, #2
fffffe00079af1a8:      	cbz	w12, 0xfffffe00079af244 <_decode+0xcb4>
fffffe00079af1ac:      	mov	w0, wzr
fffffe00079af1b0:      	tbz	w13, #3, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af1b4:      	cmp	w12, #4
fffffe00079af1b8:      	b.ne	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af1bc:      	add	x11, x10, #16
fffffe00079af1c0:      	cmp	x11, x9
fffffe00079af1c4:      	b.hi	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af1c8:      	tbz	w16, #6, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af1cc:      	tst	w14, #0x1
fffffe00079af1d0:      	mov	w9, #776
fffffe00079af1d4:      	mov	w11, #632
fffffe00079af1d8:      	add	x12, x19, #632
fffffe00079af1dc:      	csel	x9, x11, x9, eq
fffffe00079af1e0:      	add	x11, x19, #776
fffffe00079af1e4:      	csel	x28, x12, x11, eq
fffffe00079af1e8:      	ldr	w9, [x19, x9]
fffffe00079af1ec:      	cbz	w9, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af1f0:      	ldrb	w9, [x28, #4]
fffffe00079af1f4:      	cmp	w27, w9
fffffe00079af1f8:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af1fc:      	ldrb	w9, [x19, #207]
fffffe00079af200:      	cmp	w8, w9
fffffe00079af204:      	b.ne	0xfffffe00079af28c <_decode+0xcfc>
fffffe00079af208:      	add	x0, x20, x10
fffffe00079af20c:      	bl	0xfffffe00079abd18 <_pn48>
fffffe00079af210:      	cbz	x0, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af214:      	ldr	x8, [sp, #8]
fffffe00079af218:      	sxtb	w9, w25
fffffe00079af21c:      	cmp	w9, #0
fffffe00079af220:      	mov	w9, #16
fffffe00079af224:      	and	x8, x8, #0xf
fffffe00079af228:      	csel	x8, x9, x8, ge
fffffe00079af22c:      	add	x8, x28, x8, lsl #3
fffffe00079af230:      	ldr	x9, [x8, #8]!
fffffe00079af234:      	cmp	x0, x9
fffffe00079af238:      	b.ls	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af23c:      	str	x0, [x8]
fffffe00079af240:      	b	0xfffffe00079af264 <_decode+0xcd4>
fffffe00079af244:      	tst	w16, #0x40
fffffe00079af248:      	cset	w9, eq
fffffe00079af24c:      	and	w9, w26, w9
fffffe00079af250:      	cmp	w9, #1
fffffe00079af254:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af258:      	ldrb	w9, [x19, #207]
fffffe00079af25c:      	cmp	w8, w9
fffffe00079af260:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af264:      	cmp	w26, #0
fffffe00079af268:      	mov	w8, #7
fffffe00079af26c:      	mov	w0, wzr
fffffe00079af270:      	cinc	w8, w8, eq
fffffe00079af274:      	str	x21, [x19, #9160]
fffffe00079af278:      	str	x24, [x19, #9168]
fffffe00079af27c:      	strb	w22, [x23, #3873]
fffffe00079af280:      	str	w8, [x19, #9120]
fffffe00079af284:      	strb	w27, [x23, #3879]
fffffe00079af288:      	b	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af28c:      	tbz	w14, #0, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af290:      	ldrb	w9, [x19, #208]
fffffe00079af294:      	cmp	w8, w9
fffffe00079af298:      	b.ne	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af29c:      	b	0xfffffe00079af208 <_decode+0xc78>
fffffe00079af2a0:      	mov	w0, wzr
fffffe00079af2a4:      	cmp	w8, #128
fffffe00079af2a8:      	b.ne	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af2ac:      	cmp	x24, #36
fffffe00079af2b0:      	b.lo	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af2b4:      	ldr	w8, [x19, #924]
fffffe00079af2b8:      	cbz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af2bc:      	ldr	w8, [x19, #1124]
fffffe00079af2c0:      	cbnz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af2c4:      	ldr	w8, [x19, #1128]
fffffe00079af2c8:      	cbnz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af2cc:      	ldr	w8, [x19, #24]
fffffe00079af2d0:      	cbnz	w8, 0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af2d4:      	ldr	w8, [x19, #1108]
fffffe00079af2d8:      	mov	w0, wzr
fffffe00079af2dc:      	cbnz	w8, 0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af2e0:      	cmp	x24, #37
fffffe00079af2e4:      	b.lo	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af2e8:      	mov	w8, #36
fffffe00079af2ec:      	mov	w9, #1
fffffe00079af2f0:      	b	0xfffffe00079af308 <_decode+0xd78>
fffffe00079af2f4:      	add	x8, x8, x10
fffffe00079af2f8:      	mov	w0, wzr
fffffe00079af2fc:      	add	x8, x8, #2
fffffe00079af300:      	cmp	x24, x8
fffffe00079af304:      	b.ls	0xfffffe00079ae854 <_decode+0x2c4>
fffffe00079af308:      	sub	x10, x24, x8
fffffe00079af30c:      	subs	x12, x10, #2
fffffe00079af310:      	b.lo	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af314:      	add	x11, x8, #1
fffffe00079af318:      	ldrb	w10, [x21, x11]
fffffe00079af31c:      	cmp	x12, x10
fffffe00079af320:      	b.lo	0xfffffe00079ae850 <_decode+0x2c0>
fffffe00079af324:      	ldrb	w12, [x21, x8]
fffffe00079af328:      	cmp	w12, #5
fffffe00079af32c:      	b.ne	0xfffffe00079af2f4 <_decode+0xd64>
fffffe00079af330:      	cmp	w10, #4
fffffe00079af334:      	b.lo	0xfffffe00079af2f4 <_decode+0xd64>
fffffe00079af338:      	add	x12, x8, x21
fffffe00079af33c:      	ldrb	w12, [x12, #3]
fffffe00079af340:      	cbz	w12, 0xfffffe00079af2f4 <_decode+0xd64>
fffffe00079af344:      	str	w12, [x19, #1136]
fffffe00079af348:      	str	w9, [x19, #1132]
fffffe00079af34c:      	ldrb	w10, [x21, x11]
fffffe00079af350:      	b	0xfffffe00079af2f4 <_decode+0xd64>
