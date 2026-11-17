
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a94bc <_decode>:
fffffe00079a94bc:      	sub	sp, sp, #176
fffffe00079a94c0:      	stp	x28, x27, [sp, #80]
fffffe00079a94c4:      	stp	x26, x25, [sp, #96]
fffffe00079a94c8:      	stp	x24, x23, [sp, #112]
fffffe00079a94cc:      	stp	x22, x21, [sp, #128]
fffffe00079a94d0:      	stp	x20, x19, [sp, #144]
fffffe00079a94d4:      	stp	x29, x30, [sp, #160]
fffffe00079a94d8:      	add	x29, sp, #160
fffffe00079a94dc:      	movi.2d	v0, #0000000000000000
fffffe00079a94e0:      	ldr	w21, [x0, #9116]
fffffe00079a94e4:      	str	xzr, [x0, #9168]
fffffe00079a94e8:      	cmp	w21, #24
fffffe00079a94ec:      	str	q0, [x0, #9120]
fffffe00079a94f0:      	str	q0, [x0, #9136]
fffffe00079a94f4:      	str	q0, [x0, #9152]
fffffe00079a94f8:      	b.lo	0xfffffe00079a974c <_decode+0x290>
fffffe00079a94fc:      	mov	w8, #5273
fffffe00079a9500:      	mov	x19, x0
fffffe00079a9504:      	add	x20, x0, x8
fffffe00079a9508:      	mov	w8, #5274
fffffe00079a950c:      	add	x22, x0, x8
fffffe00079a9510:      	ldrb	w23, [x20, #3]
fffffe00079a9514:      	cmp	w23, #7
fffffe00079a9518:      	b.hi	0xfffffe00079a954c <_decode+0x90>
fffffe00079a951c:      	mov	w8, #6
fffffe00079a9520:      	ldur	q0, [x19, #152]
fffffe00079a9524:      	movi.2d	v1, #0000000000000000
fffffe00079a9528:      	add	x0, sp, #16
fffffe00079a952c:      	str	xzr, [sp, #64]
fffffe00079a9530:      	str	w8, [sp, #32]
fffffe00079a9534:      	ldrh	w8, [x20]
fffffe00079a9538:      	str	q0, [sp, #16]
fffffe00079a953c:      	stur	q1, [sp, #36]
fffffe00079a9540:      	stur	q1, [sp, #52]
fffffe00079a9544:      	stp	w21, w8, [sp, #40]
fffffe00079a9548:      	bl	0xfffffe00079aa590 <_nstd_log>
fffffe00079a954c:      	lsr	w8, w23, #3
fffffe00079a9550:      	cmp	w8, #2
fffffe00079a9554:      	b.eq	0xfffffe00079a966c <_decode+0x1b0>
fffffe00079a9558:      	cmp	w8, #7
fffffe00079a955c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9560:      	ldrh	w8, [x20, #2]
fffffe00079a9564:      	mov	w23, #14351
fffffe00079a9568:      	mov	w10, #14336
fffffe00079a956c:      	and	w9, w8, w23
fffffe00079a9570:      	cmp	w9, w10
fffffe00079a9574:      	b.ne	0xfffffe00079a95d4 <_decode+0x118>
fffffe00079a9578:      	cmp	w21, #29
fffffe00079a957c:      	b.lo	0xfffffe00079a95d4 <_decode+0x118>
fffffe00079a9580:      	ldrb	w9, [x20, #28]
fffffe00079a9584:      	cmp	w9, #15
fffffe00079a9588:      	b.ne	0xfffffe00079a95d4 <_decode+0x118>
fffffe00079a958c:      	ldp	x1, x2, [x19, #152]
fffffe00079a9590:      	movi.2d	v0, #0000000000000000
fffffe00079a9594:      	add	x0, sp, #16
fffffe00079a9598:      	ldp	w6, w7, [x19, #120]
fffffe00079a959c:      	ldr	w5, [x19, #60]
fffffe00079a95a0:      	mov	x3, x20
fffffe00079a95a4:      	ldr	w8, [x19, #140]
fffffe00079a95a8:      	mov	x4, x21
fffffe00079a95ac:      	str	xzr, [sp, #64]
fffffe00079a95b0:      	stp	q0, q0, [sp, #32]
fffffe00079a95b4:      	str	q0, [sp, #16]
fffffe00079a95b8:      	str	w8, [sp]
fffffe00079a95bc:      	bl	0xfffffe00079aa314 <_nstd_txs>
fffffe00079a95c0:      	add	x0, sp, #16
fffffe00079a95c4:      	bl	0xfffffe00079aa590 <_nstd_log>
fffffe00079a95c8:      	ldrb	w8, [x20, #2]
fffffe00079a95cc:      	ldrb	w9, [x20, #3]
fffffe00079a95d0:      	bfi	w8, w9, #8, #24
fffffe00079a95d4:      	and	w8, w8, w23
fffffe00079a95d8:      	mov	w9, #14337
fffffe00079a95dc:      	cmp	w8, w9
fffffe00079a95e0:      	b.eq	0xfffffe00079a9674 <_decode+0x1b8>
fffffe00079a95e4:      	mov	w0, wzr
fffffe00079a95e8:      	mov	w9, #14336
fffffe00079a95ec:      	cmp	w8, w9
fffffe00079a95f0:      	b.ne	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a95f4:      	cmp	w21, #36
fffffe00079a95f8:      	b.lo	0xfffffe00079a974c <_decode+0x290>
fffffe00079a95fc:      	ldrb	w8, [x20, #28]
fffffe00079a9600:      	add	x23, x20, #36
fffffe00079a9604:      	sub	x9, x21, #36
fffffe00079a9608:      	ldrb	w10, [x20, #29]
fffffe00079a960c:      	cmp	w8, #15
fffffe00079a9610:      	str	x23, [x19, #9160]
fffffe00079a9614:      	str	x9, [x19, #9168]
fffffe00079a9618:      	strb	w10, [x20, #3872]
fffffe00079a961c:      	b.ne	0xfffffe00079a9b7c <_decode+0x6c0>
fffffe00079a9620:      	cmp	x9, #88
fffffe00079a9624:      	b.lo	0xfffffe00079a9b7c <_decode+0x6c0>
fffffe00079a9628:      	ldr	w8, [x19, #60]
fffffe00079a962c:      	cbz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079a9630:      	ldrb	w8, [x23]
fffffe00079a9634:      	ldr	w9, [x19, #120]
fffffe00079a9638:      	cmp	w9, w8
fffffe00079a963c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9640:      	ldrb	w8, [x20, #40]
fffffe00079a9644:      	ldr	w9, [x19, #124]
fffffe00079a9648:      	cmp	w9, w8
fffffe00079a964c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9650:      	ldr	w8, [x19, #140]
fffffe00079a9654:      	cbnz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079a9658:      	mov	w8, #1
fffffe00079a965c:      	ldrb	w9, [x20, #37]
fffffe00079a9660:      	mov	w0, wzr
fffffe00079a9664:      	stp	w8, w9, [x19, #140]
fffffe00079a9668:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a966c:      	mov	w14, #1
fffffe00079a9670:      	b	0xfffffe00079a9678 <_decode+0x1bc>
fffffe00079a9674:      	mov	w14, wzr
fffffe00079a9678:      	ldrb	w9, [x20]
fffffe00079a967c:      	mov	w0, wzr
fffffe00079a9680:      	ldrb	w8, [x22]
fffffe00079a9684:      	bfi	x9, x8, #8, #8
fffffe00079a9688:      	cmp	w9, #24
fffffe00079a968c:      	b.lo	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9690:      	cmp	x9, x21
fffffe00079a9694:      	b.hi	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9698:      	ldrb	w11, [x22, #5]
fffffe00079a969c:      	ldrb	w8, [x22, #6]
fffffe00079a96a0:      	lsl	w10, w11, #16
fffffe00079a96a4:      	bfi	w10, w8, #24, #8
fffffe00079a96a8:      	and	w8, w10, #0xf800000
fffffe00079a96ac:      	cbnz	w8, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a96b0:      	ldrb	w15, [x22, #9]
fffffe00079a96b4:      	ldrb	w8, [x22, #10]
fffffe00079a96b8:      	lsl	w10, w15, #16
fffffe00079a96bc:      	bfi	w10, w8, #24, #8
fffffe00079a96c0:      	mov	w8, #192937984
fffffe00079a96c4:      	and	w8, w10, w8
fffffe00079a96c8:      	cbnz	w8, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a96cc:      	ldrb	w8, [x22, #15]
fffffe00079a96d0:      	and	w8, w8, #0x3
fffffe00079a96d4:      	cbnz	w8, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a96d8:      	ldrb	w8, [x22, #16]
fffffe00079a96dc:      	and	w8, w8, #0x18
fffffe00079a96e0:      	cbnz	w8, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a96e4:      	ldrb	w13, [x22, #4]
fffffe00079a96e8:      	mov	w8, #40
fffffe00079a96ec:      	mov	w10, #24
fffffe00079a96f0:      	ldrb	w16, [x22, #8]
fffffe00079a96f4:      	tst	w13, #0x40
fffffe00079a96f8:      	csel	x8, x10, x8, eq
fffffe00079a96fc:      	csel	x17, xzr, x10, eq
fffffe00079a9700:      	tst	w13, #0x8
fffffe00079a9704:      	add	x12, x8, #16
fffffe00079a9708:      	csel	x10, xzr, x8, eq
fffffe00079a970c:      	csel	x8, x8, x12, eq
fffffe00079a9710:      	tst	w13, #0x10
fffffe00079a9714:      	add	x12, x8, #8
fffffe00079a9718:      	csel	x8, x8, x12, eq
fffffe00079a971c:      	tst	w13, #0x20
fffffe00079a9720:      	add	x12, x8, #8
fffffe00079a9724:      	csel	x8, x8, x12, eq
fffffe00079a9728:      	sxtb	w12, w13
fffffe00079a972c:      	add	x0, x8, #72
fffffe00079a9730:      	cmp	w12, #0
fffffe00079a9734:      	lsr	x12, x16, #5
fffffe00079a9738:      	csel	x8, x0, x8, lt
fffffe00079a973c:      	and	x12, x12, #0x6
fffffe00079a9740:      	add	x8, x8, x12
fffffe00079a9744:      	cmp	x8, x9
fffffe00079a9748:      	b.ls	0xfffffe00079a9770 <_decode+0x2b4>
fffffe00079a974c:      	mov	w0, wzr
fffffe00079a9750:      	ldp	x29, x30, [sp, #160]
fffffe00079a9754:      	ldp	x20, x19, [sp, #144]
fffffe00079a9758:      	ldp	x22, x21, [sp, #128]
fffffe00079a975c:      	ldp	x24, x23, [sp, #112]
fffffe00079a9760:      	ldp	x26, x25, [sp, #96]
fffffe00079a9764:      	ldp	x28, x27, [sp, #80]
fffffe00079a9768:      	add	sp, sp, #176
fffffe00079a976c:      	ret
fffffe00079a9770:      	ldrb	w23, [x22, #3]
fffffe00079a9774:      	lsl	w12, w13, #8
fffffe00079a9778:      	add	x21, x20, x8
fffffe00079a977c:      	sub	x24, x9, x8
fffffe00079a9780:      	and	w8, w12, #0x300
fffffe00079a9784:      	and	w12, w11, #0x1f
fffffe00079a9788:      	orr	w8, w8, w23
fffffe00079a978c:      	tbnz	w16, #5, 0xfffffe00079a97e8 <_decode+0x32c>
fffffe00079a9790:      	cmp	x24, #1
fffffe00079a9794:      	cset	w9, hi
fffffe00079a9798:      	and	w9, w14, w9
fffffe00079a979c:      	cmp	w9, #1
fffffe00079a97a0:      	b.ne	0xfffffe00079a99f8 <_decode+0x53c>
fffffe00079a97a4:      	ldrb	w9, [x21]
fffffe00079a97a8:      	and	w9, w9, #0xc
fffffe00079a97ac:      	cmp	w9, #8
fffffe00079a97b0:      	b.ne	0xfffffe00079a99f8 <_decode+0x53c>
fffffe00079a97b4:      	ldr	w9, [x19, #1112]
fffffe00079a97b8:      	subs	x10, x24, #36
fffffe00079a97bc:      	add	w9, w9, #1
fffffe00079a97c0:      	str	w9, [x19, #1112]
fffffe00079a97c4:      	b.lo	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a97c8:      	sub	x9, x24, #18
fffffe00079a97cc:      	cmp	x9, #1514
fffffe00079a97d0:      	b.ls	0xfffffe00079a9d88 <_decode+0x8cc>
fffffe00079a97d4:      	ldr	w8, [x19, #1120]
fffffe00079a97d8:      	mov	w0, wzr
fffffe00079a97dc:      	add	w8, w8, #1
fffffe00079a97e0:      	str	w8, [x19, #1120]
fffffe00079a97e4:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a97e8:      	mov	w0, wzr
fffffe00079a97ec:      	cbz	w14, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a97f0:      	cmp	x24, #14
fffffe00079a97f4:      	b.lo	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a97f8:      	cmp	x24, #1514
fffffe00079a97fc:      	b.hi	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9800:      	tbz	w13, #6, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9804:      	add	x14, x17, #16
fffffe00079a9808:      	cmp	x14, x9
fffffe00079a980c:      	b.hi	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9810:      	add	x6, x20, x17
fffffe00079a9814:      	ldrb	w14, [x19, #198]
fffffe00079a9818:      	ldrb	w17, [x6, #2]
fffffe00079a981c:      	cmp	w17, w14
fffffe00079a9820:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9824:      	ldrb	w1, [x6, #3]
fffffe00079a9828:      	ldrb	w14, [x19, #199]
fffffe00079a982c:      	cmp	w1, w14
fffffe00079a9830:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9834:      	ldrb	w2, [x6, #4]
fffffe00079a9838:      	ldrb	w14, [x19, #200]
fffffe00079a983c:      	cmp	w2, w14
fffffe00079a9840:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9844:      	ldrb	w3, [x6, #5]
fffffe00079a9848:      	ldrb	w14, [x19, #201]
fffffe00079a984c:      	cmp	w3, w14
fffffe00079a9850:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9854:      	ldrb	w4, [x6, #6]
fffffe00079a9858:      	ldrb	w14, [x19, #202]
fffffe00079a985c:      	cmp	w4, w14
fffffe00079a9860:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9864:      	ldrb	w5, [x6, #7]
fffffe00079a9868:      	ldrb	w14, [x19, #203]
fffffe00079a986c:      	cmp	w5, w14
fffffe00079a9870:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9874:      	ldrb	w14, [x21]
fffffe00079a9878:      	tbnz	w14, #0, 0xfffffe00079a98d8 <_decode+0x41c>
fffffe00079a987c:      	ldrb	w16, [x19, #192]
fffffe00079a9880:      	cmp	w14, w16
fffffe00079a9884:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9888:      	ldrb	w16, [x21, #1]
fffffe00079a988c:      	ldrb	w0, [x19, #193]
fffffe00079a9890:      	cmp	w16, w0
fffffe00079a9894:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9898:      	ldrb	w16, [x21, #2]
fffffe00079a989c:      	ldrb	w0, [x19, #194]
fffffe00079a98a0:      	cmp	w16, w0
fffffe00079a98a4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a98a8:      	ldrb	w16, [x21, #3]
fffffe00079a98ac:      	ldrb	w0, [x19, #195]
fffffe00079a98b0:      	cmp	w16, w0
fffffe00079a98b4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a98b8:      	ldrb	w16, [x21, #4]
fffffe00079a98bc:      	ldrb	w0, [x19, #196]
fffffe00079a98c0:      	cmp	w16, w0
fffffe00079a98c4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a98c8:      	ldrb	w16, [x21, #5]
fffffe00079a98cc:      	ldrb	w0, [x19, #197]
fffffe00079a98d0:      	cmp	w16, w0
fffffe00079a98d4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a98d8:      	ldrb	w16, [x6, #1]
fffffe00079a98dc:      	mov	w0, wzr
fffffe00079a98e0:      	mov	w7, #132
fffffe00079a98e4:      	tst	w16, w7
fffffe00079a98e8:      	b.ne	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a98ec:      	ldrb	w25, [x6]
fffffe00079a98f0:      	mov	w26, #780
fffffe00079a98f4:      	mov	w7, w25
fffffe00079a98f8:      	bfi	w7, w16, #8, #8
fffffe00079a98fc:      	and	w7, w7, w26
fffffe00079a9900:      	cmp	w7, #520
fffffe00079a9904:      	b.ne	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9908:      	str	x15, [sp, #8]
fffffe00079a990c:      	ldrb	w15, [x6, #8]
fffffe00079a9910:      	tst	w15, #0xf
fffffe00079a9914:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9918:      	ldrb	w15, [x21, #12]
fffffe00079a991c:      	cmp	w15, #136
fffffe00079a9920:      	b.ne	0xfffffe00079aa074 <_decode+0xbb8>
fffffe00079a9924:      	ldrb	w15, [x21, #13]
fffffe00079a9928:      	cmp	w15, #142
fffffe00079a992c:      	b.ne	0xfffffe00079aa090 <_decode+0xbd4>
fffffe00079a9930:      	ldrb	w15, [x21, #6]
fffffe00079a9934:      	cmp	w15, w17
fffffe00079a9938:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a993c:      	ldrb	w15, [x21, #7]
fffffe00079a9940:      	cmp	w15, w1
fffffe00079a9944:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9948:      	ldrb	w15, [x21, #8]
fffffe00079a994c:      	cmp	w15, w2
fffffe00079a9950:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9954:      	ldrb	w15, [x21, #9]
fffffe00079a9958:      	cmp	w15, w3
fffffe00079a995c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9960:      	ldrb	w15, [x21, #10]
fffffe00079a9964:      	cmp	w15, w4
fffffe00079a9968:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a996c:      	ldrb	w15, [x21, #11]
fffffe00079a9970:      	cmp	w15, w5
fffffe00079a9974:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9978:      	ldrb	w15, [x19, #192]
fffffe00079a997c:      	cmp	w14, w15
fffffe00079a9980:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9984:      	ldrb	w15, [x21, #1]
fffffe00079a9988:      	ldrb	w17, [x19, #193]
fffffe00079a998c:      	cmp	w15, w17
fffffe00079a9990:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9994:      	ldrb	w15, [x21, #2]
fffffe00079a9998:      	ldrb	w17, [x19, #194]
fffffe00079a999c:      	cmp	w15, w17
fffffe00079a99a0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a99a4:      	ldrb	w15, [x21, #3]
fffffe00079a99a8:      	ldrb	w17, [x19, #195]
fffffe00079a99ac:      	cmp	w15, w17
fffffe00079a99b0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a99b4:      	ldrb	w15, [x21, #4]
fffffe00079a99b8:      	ldrb	w17, [x19, #196]
fffffe00079a99bc:      	cmp	w15, w17
fffffe00079a99c0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a99c4:      	ldrb	w15, [x21, #5]
fffffe00079a99c8:      	mov	w0, wzr
fffffe00079a99cc:      	ldrb	w17, [x19, #197]
fffffe00079a99d0:      	cmp	w15, w17
fffffe00079a99d4:      	b.ne	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a99d8:      	subs	x15, x24, #18
fffffe00079a99dc:      	b.lo	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a99e0:      	ldrh	w17, [x21, #16]
fffffe00079a99e4:      	rev16	w17, w17
fffffe00079a99e8:      	cmp	x17, x15
fffffe00079a99ec:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a99f0:      	mov	w26, #1
fffffe00079a99f4:      	b	0xfffffe00079aa09c <_decode+0xbe0>
fffffe00079a99f8:      	cmp	x24, #24
fffffe00079a99fc:      	b.lo	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a00:      	mov	w0, wzr
fffffe00079a9a04:      	and	w8, w16, #0x1f
fffffe00079a9a08:      	cmp	w8, #12
fffffe00079a9a0c:      	b.ne	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9a10:      	cbnz	w12, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9a14:      	ldrb	w8, [x21, #1]
fffffe00079a9a18:      	tst	w8, #0xffffffc7
fffffe00079a9a1c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a20:      	ldrb	w8, [x21, #22]
fffffe00079a9a24:      	tst	w8, #0xf
fffffe00079a9a28:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a2c:      	ldrb	w8, [x21, #10]
fffffe00079a9a30:      	ldrb	w9, [x19, #198]
fffffe00079a9a34:      	cmp	w8, w9
fffffe00079a9a38:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a3c:      	ldrb	w9, [x21, #11]
fffffe00079a9a40:      	ldrb	w10, [x19, #199]
fffffe00079a9a44:      	cmp	w9, w10
fffffe00079a9a48:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a4c:      	ldrb	w10, [x21, #12]
fffffe00079a9a50:      	ldrb	w11, [x19, #200]
fffffe00079a9a54:      	cmp	w10, w11
fffffe00079a9a58:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a5c:      	ldrb	w11, [x21, #13]
fffffe00079a9a60:      	ldrb	w12, [x19, #201]
fffffe00079a9a64:      	cmp	w11, w12
fffffe00079a9a68:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a6c:      	ldrb	w12, [x21, #14]
fffffe00079a9a70:      	ldrb	w13, [x19, #202]
fffffe00079a9a74:      	cmp	w12, w13
fffffe00079a9a78:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a7c:      	ldrb	w13, [x21, #15]
fffffe00079a9a80:      	ldrb	w14, [x19, #203]
fffffe00079a9a84:      	cmp	w13, w14
fffffe00079a9a88:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a8c:      	ldrb	w14, [x21, #16]
fffffe00079a9a90:      	cmp	w14, w8
fffffe00079a9a94:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9a98:      	ldrb	w8, [x21, #17]
fffffe00079a9a9c:      	cmp	w8, w9
fffffe00079a9aa0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9aa4:      	ldrb	w8, [x21, #18]
fffffe00079a9aa8:      	cmp	w8, w10
fffffe00079a9aac:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9ab0:      	ldrb	w8, [x21, #19]
fffffe00079a9ab4:      	cmp	w8, w11
fffffe00079a9ab8:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9abc:      	ldrb	w8, [x21, #20]
fffffe00079a9ac0:      	cmp	w8, w12
fffffe00079a9ac4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9ac8:      	ldrb	w8, [x21, #21]
fffffe00079a9acc:      	cmp	w8, w13
fffffe00079a9ad0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9ad4:      	ldrb	w8, [x21]
fffffe00079a9ad8:      	mov	w9, #1610612736
fffffe00079a9adc:      	add	w9, w9, w8, lsl #24
fffffe00079a9ae0:      	extr	w9, w8, w9, #28
fffffe00079a9ae4:      	and	w9, w9, #0xff
fffffe00079a9ae8:      	cmp	w9, #7
fffffe00079a9aec:      	b.hi	0xfffffe00079aa198 <_decode+0xcdc>
fffffe00079a9af0:      	mov	w10, #1
fffffe00079a9af4:      	lsl	w9, w10, w9
fffffe00079a9af8:      	mov	w10, #135
fffffe00079a9afc:      	tst	w9, w10
fffffe00079a9b00:      	b.eq	0xfffffe00079aa198 <_decode+0xcdc>
fffffe00079a9b04:      	ldrb	w8, [x21, #4]
fffffe00079a9b08:      	ldrb	w9, [x19, #192]
fffffe00079a9b0c:      	cmp	w8, w9
fffffe00079a9b10:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9b14:      	ldrb	w8, [x21, #5]
fffffe00079a9b18:      	ldrb	w9, [x19, #193]
fffffe00079a9b1c:      	cmp	w8, w9
fffffe00079a9b20:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9b24:      	ldrb	w8, [x21, #6]
fffffe00079a9b28:      	ldrb	w9, [x19, #194]
fffffe00079a9b2c:      	cmp	w8, w9
fffffe00079a9b30:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9b34:      	ldrb	w8, [x21, #7]
fffffe00079a9b38:      	ldrb	w9, [x19, #195]
fffffe00079a9b3c:      	cmp	w8, w9
fffffe00079a9b40:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9b44:      	ldrb	w8, [x21, #8]
fffffe00079a9b48:      	ldrb	w9, [x19, #196]
fffffe00079a9b4c:      	cmp	w8, w9
fffffe00079a9b50:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9b54:      	ldrb	w8, [x21, #9]
fffffe00079a9b58:      	ldrb	w9, [x19, #197]
fffffe00079a9b5c:      	cmp	w8, w9
fffffe00079a9b60:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9b64:      	mov	w8, #6
fffffe00079a9b68:      	mov	w0, wzr
fffffe00079a9b6c:      	str	x21, [x19, #9160]
fffffe00079a9b70:      	str	x24, [x19, #9168]
fffffe00079a9b74:      	str	w8, [x19, #9120]
fffffe00079a9b78:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9b7c:      	cmp	w8, #16
fffffe00079a9b80:      	b.ne	0xfffffe00079a9c34 <_decode+0x778>
fffffe00079a9b84:      	cmp	x9, #24
fffffe00079a9b88:      	b.lo	0xfffffe00079a9c34 <_decode+0x778>
fffffe00079a9b8c:      	ldrb	w8, [x19, #985]
fffffe00079a9b90:      	cbz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079a9b94:      	ldrb	w8, [x23]
fffffe00079a9b98:      	ldrb	w9, [x19, #204]
fffffe00079a9b9c:      	cmp	w8, w9
fffffe00079a9ba0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9ba4:      	ldrb	w9, [x20, #37]
fffffe00079a9ba8:      	ldrb	w10, [x19, #984]
fffffe00079a9bac:      	cmp	w9, w10
fffffe00079a9bb0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9bb4:      	ldrb	w10, [x20, #39]
fffffe00079a9bb8:      	ldrb	w11, [x19, #210]
fffffe00079a9bbc:      	cmp	w10, w11
fffffe00079a9bc0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9bc4:      	ldrb	w11, [x20, #41]
fffffe00079a9bc8:      	ldrb	w12, [x19, #209]
fffffe00079a9bcc:      	cmp	w11, w12
fffffe00079a9bd0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9bd4:      	ldrb	w12, [x20, #38]
fffffe00079a9bd8:      	mov	w0, wzr
fffffe00079a9bdc:      	cbnz	w12, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9be0:      	ldr	w12, [x20, #48]
fffffe00079a9be4:      	cbz	w12, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9be8:      	mov	w13, #60000
fffffe00079a9bec:      	cmp	w12, w13
fffffe00079a9bf0:      	b.hi	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9bf4:      	mov	w14, #1000
fffffe00079a9bf8:      	ldr	x13, [x19, #168]
fffffe00079a9bfc:      	mul	x14, x12, x14
fffffe00079a9c00:      	cmn	x14, x13
fffffe00079a9c04:      	b.hs	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c08:      	adrp	x15, 0xfffffe0007007000 <_transmit+0x340>
fffffe00079a9c0c:      	strb	w8, [x22, #3874]
fffffe00079a9c10:      	add	x8, x13, x14
fffffe00079a9c14:      	strb	w9, [x22, #3876]
fffffe00079a9c18:      	strb	w10, [x22, #3878]
fffffe00079a9c1c:      	ldr	d0, [x15, #328]
fffffe00079a9c20:      	strb	w11, [x22, #3877]
fffffe00079a9c24:      	str	x8, [x19, #976]
fffffe00079a9c28:      	str	w12, [x19, #9156]
fffffe00079a9c2c:      	str	d0, [x19, #9120]
fffffe00079a9c30:      	b	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c34:      	cmp	w8, #12
fffffe00079a9c38:      	b.ne	0xfffffe00079a9cfc <_decode+0x840>
fffffe00079a9c3c:      	cmp	x9, #8
fffffe00079a9c40:      	b.lo	0xfffffe00079a9cfc <_decode+0x840>
fffffe00079a9c44:      	ldr	w8, [x19, #944]
fffffe00079a9c48:      	cbz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c4c:      	ldr	w8, [x19, #948]
fffffe00079a9c50:      	cmp	w8, w10
fffffe00079a9c54:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c58:      	ldrb	w8, [x23]
fffffe00079a9c5c:      	ldrb	w9, [x19, #198]
fffffe00079a9c60:      	cmp	w8, w9
fffffe00079a9c64:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c68:      	ldrb	w8, [x22, #36]
fffffe00079a9c6c:      	ldrb	w9, [x19, #199]
fffffe00079a9c70:      	cmp	w8, w9
fffffe00079a9c74:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c78:      	ldrb	w8, [x22, #37]
fffffe00079a9c7c:      	ldrb	w9, [x19, #200]
fffffe00079a9c80:      	cmp	w8, w9
fffffe00079a9c84:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c88:      	ldrb	w8, [x22, #38]
fffffe00079a9c8c:      	ldrb	w9, [x19, #201]
fffffe00079a9c90:      	cmp	w8, w9
fffffe00079a9c94:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9c98:      	ldrb	w8, [x22, #39]
fffffe00079a9c9c:      	ldrb	w9, [x19, #202]
fffffe00079a9ca0:      	cmp	w8, w9
fffffe00079a9ca4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9ca8:      	ldrb	w8, [x22, #40]
fffffe00079a9cac:      	ldrb	w9, [x19, #203]
fffffe00079a9cb0:      	cmp	w8, w9
fffffe00079a9cb4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9cb8:      	ldrb	w8, [x22, #41]
fffffe00079a9cbc:      	ldrb	w9, [x19, #206]
fffffe00079a9cc0:      	cmp	w8, w9
fffffe00079a9cc4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9cc8:      	ldrb	w9, [x22, #42]
fffffe00079a9ccc:      	ldrb	w10, [x19, #204]
fffffe00079a9cd0:      	cmp	w9, w10
fffffe00079a9cd4:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9cd8:      	mov	w10, #1
fffffe00079a9cdc:      	mov	w11, #4
fffffe00079a9ce0:      	str	wzr, [x19, #944]
fffffe00079a9ce4:      	mov	w0, wzr
fffffe00079a9ce8:      	strb	w8, [x22, #3875]
fffffe00079a9cec:      	str	w10, [x19, #924]
fffffe00079a9cf0:      	str	w11, [x19, #9120]
fffffe00079a9cf4:      	strb	w9, [x22, #3874]
fffffe00079a9cf8:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9cfc:      	cmp	w8, #17
fffffe00079a9d00:      	b.ne	0xfffffe00079a9d30 <_decode+0x874>
fffffe00079a9d04:      	cmp	x9, #4
fffffe00079a9d08:      	b.lo	0xfffffe00079a9d30 <_decode+0x874>
fffffe00079a9d0c:      	ldrb	w8, [x23]
fffffe00079a9d10:      	ldrb	w9, [x19, #204]
fffffe00079a9d14:      	cmp	w8, w9
fffffe00079a9d18:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9d1c:      	ldrb	w8, [x22, #36]
fffffe00079a9d20:      	cmp	w8, #1
fffffe00079a9d24:      	b.hi	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9d28:      	str	w8, [x19, #1140]
fffffe00079a9d2c:      	b	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9d30:      	cmp	w8, #19
fffffe00079a9d34:      	b.ne	0xfffffe00079a9f90 <_decode+0xad4>
fffffe00079a9d38:      	cmp	x9, #4
fffffe00079a9d3c:      	b.lo	0xfffffe00079a9f90 <_decode+0xad4>
fffffe00079a9d40:      	ldrb	w8, [x23]
fffffe00079a9d44:      	ldrb	w9, [x19, #204]
fffffe00079a9d48:      	cmp	w8, w9
fffffe00079a9d4c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9d50:      	ldr	w8, [x19, #924]
fffffe00079a9d54:      	cbz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079a9d58:      	mov	x0, x19
fffffe00079a9d5c:      	bl	0xfffffe00079a5718 <_nst_close_port>
fffffe00079a9d60:      	mov	w8, #1
fffffe00079a9d64:      	mov	w9, #13
fffffe00079a9d68:      	ldrb	w10, [x22, #36]
fffffe00079a9d6c:      	mov	w0, wzr
fffffe00079a9d70:      	ldrb	w11, [x23]
fffffe00079a9d74:      	str	w8, [x19, #1108]
fffffe00079a9d78:      	str	w9, [x19, #9120]
fffffe00079a9d7c:      	str	w10, [x19, #9124]
fffffe00079a9d80:      	strb	w11, [x22, #3874]
fffffe00079a9d84:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9d88:      	and	w11, w16, #0x1f
fffffe00079a9d8c:      	cmp	w11, #12
fffffe00079a9d90:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9d94:      	cbnz	w12, 0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9d98:      	ldrb	w11, [x21, #1]
fffffe00079a9d9c:      	mov	w13, #50944
fffffe00079a9da0:      	ldrb	w12, [x21]
fffffe00079a9da4:      	and	w11, w13, w11, lsl #8
fffffe00079a9da8:      	orr	w11, w11, w12
fffffe00079a9dac:      	cmp	w11, #520
fffffe00079a9db0:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9db4:      	ldrb	w11, [x21, #22]
fffffe00079a9db8:      	tst	w11, #0xf
fffffe00079a9dbc:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9dc0:      	ldrb	w11, [x19, #207]
fffffe00079a9dc4:      	cmp	w8, w11
fffffe00079a9dc8:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9dcc:      	ldrb	w8, [x21, #4]
fffffe00079a9dd0:      	ldrb	w11, [x19, #192]
fffffe00079a9dd4:      	cmp	w8, w11
fffffe00079a9dd8:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9ddc:      	ldrb	w11, [x21, #5]
fffffe00079a9de0:      	ldrb	w12, [x19, #193]
fffffe00079a9de4:      	cmp	w11, w12
fffffe00079a9de8:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9dec:      	ldrb	w12, [x21, #6]
fffffe00079a9df0:      	ldrb	w13, [x19, #194]
fffffe00079a9df4:      	cmp	w12, w13
fffffe00079a9df8:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9dfc:      	ldrb	w13, [x21, #7]
fffffe00079a9e00:      	ldrb	w14, [x19, #195]
fffffe00079a9e04:      	cmp	w13, w14
fffffe00079a9e08:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e0c:      	ldrb	w14, [x21, #8]
fffffe00079a9e10:      	ldrb	w15, [x19, #196]
fffffe00079a9e14:      	cmp	w14, w15
fffffe00079a9e18:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e1c:      	ldrb	w15, [x21, #9]
fffffe00079a9e20:      	ldrb	w16, [x19, #197]
fffffe00079a9e24:      	cmp	w15, w16
fffffe00079a9e28:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e2c:      	ldrb	w16, [x21, #10]
fffffe00079a9e30:      	ldrb	w17, [x19, #198]
fffffe00079a9e34:      	cmp	w16, w17
fffffe00079a9e38:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e3c:      	ldrb	w17, [x21, #11]
fffffe00079a9e40:      	ldrb	w0, [x19, #199]
fffffe00079a9e44:      	cmp	w17, w0
fffffe00079a9e48:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e4c:      	ldrb	w1, [x21, #12]
fffffe00079a9e50:      	ldrb	w0, [x19, #200]
fffffe00079a9e54:      	cmp	w1, w0
fffffe00079a9e58:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e5c:      	ldrb	w2, [x21, #13]
fffffe00079a9e60:      	ldrb	w0, [x19, #201]
fffffe00079a9e64:      	cmp	w2, w0
fffffe00079a9e68:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e6c:      	ldrb	w3, [x21, #14]
fffffe00079a9e70:      	ldrb	w0, [x19, #202]
fffffe00079a9e74:      	cmp	w3, w0
fffffe00079a9e78:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e7c:      	ldrb	w4, [x21, #15]
fffffe00079a9e80:      	ldrb	w0, [x19, #203]
fffffe00079a9e84:      	cmp	w4, w0
fffffe00079a9e88:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e8c:      	ldrb	w0, [x21, #16]
fffffe00079a9e90:      	cmp	w0, w16
fffffe00079a9e94:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9e98:      	ldrb	w0, [x21, #17]
fffffe00079a9e9c:      	cmp	w0, w17
fffffe00079a9ea0:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9ea4:      	ldrb	w0, [x21, #18]
fffffe00079a9ea8:      	cmp	w0, w1
fffffe00079a9eac:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9eb0:      	ldrb	w0, [x21, #19]
fffffe00079a9eb4:      	cmp	w0, w2
fffffe00079a9eb8:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9ebc:      	ldrb	w0, [x21, #20]
fffffe00079a9ec0:      	cmp	w0, w3
fffffe00079a9ec4:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9ec8:      	ldrb	w0, [x21, #21]
fffffe00079a9ecc:      	cmp	w0, w4
fffffe00079a9ed0:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9ed4:      	ldrb	w0, [x21, #24]
fffffe00079a9ed8:      	cmp	w0, #170
fffffe00079a9edc:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9ee0:      	ldrb	w0, [x21, #25]
fffffe00079a9ee4:      	cmp	w0, #170
fffffe00079a9ee8:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9eec:      	ldrb	w0, [x21, #26]
fffffe00079a9ef0:      	cmp	w0, #3
fffffe00079a9ef4:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9ef8:      	ldrb	w0, [x21, #27]
fffffe00079a9efc:      	cbnz	w0, 0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9f00:      	ldrb	w0, [x21, #28]
fffffe00079a9f04:      	cbnz	w0, 0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9f08:      	ldrb	w0, [x21, #29]
fffffe00079a9f0c:      	cbnz	w0, 0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9f10:      	ldrb	w0, [x21, #30]
fffffe00079a9f14:      	cmp	w0, #136
fffffe00079a9f18:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9f1c:      	ldrb	w0, [x21, #31]
fffffe00079a9f20:      	cmp	w0, #142
fffffe00079a9f24:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9f28:      	ldrh	w0, [x21, #34]
fffffe00079a9f2c:      	rev16	w0, w0
fffffe00079a9f30:      	cmp	x0, x10
fffffe00079a9f34:      	b.ne	0xfffffe00079a97d4 <_decode+0x318>
fffffe00079a9f38:      	strb	w8, [x21, #18]!
fffffe00079a9f3c:      	strb	w11, [x21, #1]
fffffe00079a9f40:      	mov	w10, #7
fffffe00079a9f44:      	strb	w12, [x21, #2]
fffffe00079a9f48:      	mov	w0, wzr
fffffe00079a9f4c:      	strb	w13, [x21, #3]
fffffe00079a9f50:      	strb	w14, [x21, #4]
fffffe00079a9f54:      	strb	w15, [x21, #5]
fffffe00079a9f58:      	strb	w16, [x21, #6]
fffffe00079a9f5c:      	strb	w17, [x21, #7]
fffffe00079a9f60:      	strb	w1, [x21, #8]
fffffe00079a9f64:      	strb	w2, [x21, #9]
fffffe00079a9f68:      	strb	w3, [x21, #10]
fffffe00079a9f6c:      	strb	w4, [x21, #11]
fffffe00079a9f70:      	ldr	w8, [x19, #1116]
fffffe00079a9f74:      	str	w10, [x19, #9120]
fffffe00079a9f78:      	str	x21, [x19, #9160]
fffffe00079a9f7c:      	str	x9, [x19, #9168]
fffffe00079a9f80:      	add	w8, w8, #1
fffffe00079a9f84:      	strb	w23, [x22, #3873]
fffffe00079a9f88:      	str	w8, [x19, #1116]
fffffe00079a9f8c:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079a9f90:      	cmp	w8, #36
fffffe00079a9f94:      	b.ne	0xfffffe00079aa044 <_decode+0xb88>
fffffe00079a9f98:      	cmp	x9, #8
fffffe00079a9f9c:      	b.lo	0xfffffe00079aa044 <_decode+0xb88>
fffffe00079a9fa0:      	ldr	w8, [x19, #932]
fffffe00079a9fa4:      	cbz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079a9fa8:      	ldr	w8, [x19, #928]
fffffe00079a9fac:      	cbnz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079a9fb0:      	ldrb	w8, [x23]
fffffe00079a9fb4:      	ldrb	w9, [x19, #204]
fffffe00079a9fb8:      	cmp	w8, w9
fffffe00079a9fbc:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9fc0:      	ldrb	w9, [x22, #37]
fffffe00079a9fc4:      	ldrb	w10, [x19, #198]
fffffe00079a9fc8:      	cmp	w9, w10
fffffe00079a9fcc:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9fd0:      	ldrb	w9, [x22, #38]
fffffe00079a9fd4:      	ldrb	w10, [x19, #199]
fffffe00079a9fd8:      	cmp	w9, w10
fffffe00079a9fdc:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9fe0:      	ldrb	w9, [x22, #39]
fffffe00079a9fe4:      	ldrb	w10, [x19, #200]
fffffe00079a9fe8:      	cmp	w9, w10
fffffe00079a9fec:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079a9ff0:      	ldrb	w9, [x22, #40]
fffffe00079a9ff4:      	ldrb	w10, [x19, #201]
fffffe00079a9ff8:      	cmp	w9, w10
fffffe00079a9ffc:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa000:      	ldrb	w9, [x22, #41]
fffffe00079aa004:      	ldrb	w10, [x19, #202]
fffffe00079aa008:      	cmp	w9, w10
fffffe00079aa00c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa010:      	ldrb	w9, [x22, #42]
fffffe00079aa014:      	ldrb	w10, [x19, #203]
fffffe00079aa018:      	cmp	w9, w10
fffffe00079aa01c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa020:      	mov	w9, #1
fffffe00079aa024:      	mov	w10, #5
fffffe00079aa028:      	strb	w8, [x22, #3874]
fffffe00079aa02c:      	mov	w8, #8
fffffe00079aa030:      	mov	w0, wzr
fffffe00079aa034:      	str	w9, [x19, #928]
fffffe00079aa038:      	str	w10, [x19, #9120]
fffffe00079aa03c:      	str	x8, [x19, #9168]
fffffe00079aa040:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa044:      	mov	w0, #-1
fffffe00079aa048:      	cmp	w8, #253
fffffe00079aa04c:      	b.eq	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa050:      	cmp	w8, #240
fffffe00079aa054:      	b.eq	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa058:      	cmp	w8, #237
fffffe00079aa05c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa060:      	ldrb	w8, [x22, #31]
fffffe00079aa064:      	cmp	w8, #35
fffffe00079aa068:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa06c:      	mov	w0, #-1
fffffe00079aa070:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa074:      	ldr	w17, [x19, #920]
fffffe00079aa078:      	cbz	w17, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa07c:      	mov	w26, wzr
fffffe00079aa080:      	mov	w0, wzr
fffffe00079aa084:      	cmp	w15, #6
fffffe00079aa088:      	b.hs	0xfffffe00079aa09c <_decode+0xbe0>
fffffe00079aa08c:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa090:      	ldr	w15, [x19, #920]
fffffe00079aa094:      	mov	w26, wzr
fffffe00079aa098:      	cbz	w15, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa09c:      	ubfx	w27, w11, #5, #2
fffffe00079aa0a0:      	cbz	w12, 0xfffffe00079aa13c <_decode+0xc80>
fffffe00079aa0a4:      	mov	w0, wzr
fffffe00079aa0a8:      	tbz	w13, #3, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa0ac:      	cmp	w12, #4
fffffe00079aa0b0:      	b.ne	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa0b4:      	add	x11, x10, #16
fffffe00079aa0b8:      	cmp	x11, x9
fffffe00079aa0bc:      	b.hi	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa0c0:      	tbz	w16, #6, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa0c4:      	tst	w14, #0x1
fffffe00079aa0c8:      	mov	w9, #776
fffffe00079aa0cc:      	mov	w11, #632
fffffe00079aa0d0:      	add	x12, x19, #632
fffffe00079aa0d4:      	csel	x9, x11, x9, eq
fffffe00079aa0d8:      	add	x11, x19, #776
fffffe00079aa0dc:      	csel	x28, x12, x11, eq
fffffe00079aa0e0:      	ldr	w9, [x19, x9]
fffffe00079aa0e4:      	cbz	w9, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa0e8:      	ldrb	w9, [x28, #4]
fffffe00079aa0ec:      	cmp	w27, w9
fffffe00079aa0f0:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa0f4:      	ldrb	w9, [x19, #207]
fffffe00079aa0f8:      	cmp	w8, w9
fffffe00079aa0fc:      	b.ne	0xfffffe00079aa184 <_decode+0xcc8>
fffffe00079aa100:      	add	x0, x20, x10
fffffe00079aa104:      	bl	0xfffffe00079a6cf4 <_pn48>
fffffe00079aa108:      	cbz	x0, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa10c:      	ldr	x8, [sp, #8]
fffffe00079aa110:      	sxtb	w9, w25
fffffe00079aa114:      	cmp	w9, #0
fffffe00079aa118:      	mov	w9, #16
fffffe00079aa11c:      	and	x8, x8, #0xf
fffffe00079aa120:      	csel	x8, x9, x8, ge
fffffe00079aa124:      	add	x8, x28, x8, lsl #3
fffffe00079aa128:      	ldr	x9, [x8, #8]!
fffffe00079aa12c:      	cmp	x0, x9
fffffe00079aa130:      	b.ls	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa134:      	str	x0, [x8]
fffffe00079aa138:      	b	0xfffffe00079aa15c <_decode+0xca0>
fffffe00079aa13c:      	tst	w16, #0x40
fffffe00079aa140:      	cset	w9, eq
fffffe00079aa144:      	and	w9, w26, w9
fffffe00079aa148:      	cmp	w9, #1
fffffe00079aa14c:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa150:      	ldrb	w9, [x19, #207]
fffffe00079aa154:      	cmp	w8, w9
fffffe00079aa158:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa15c:      	cmp	w26, #0
fffffe00079aa160:      	mov	w8, #7
fffffe00079aa164:      	mov	w0, wzr
fffffe00079aa168:      	cinc	w8, w8, eq
fffffe00079aa16c:      	str	x21, [x19, #9160]
fffffe00079aa170:      	str	x24, [x19, #9168]
fffffe00079aa174:      	strb	w23, [x22, #3873]
fffffe00079aa178:      	str	w8, [x19, #9120]
fffffe00079aa17c:      	strb	w27, [x22, #3879]
fffffe00079aa180:      	b	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa184:      	tbz	w14, #0, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa188:      	ldrb	w9, [x19, #208]
fffffe00079aa18c:      	cmp	w8, w9
fffffe00079aa190:      	b.ne	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa194:      	b	0xfffffe00079aa100 <_decode+0xc44>
fffffe00079aa198:      	mov	w0, wzr
fffffe00079aa19c:      	cmp	w8, #128
fffffe00079aa1a0:      	b.ne	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa1a4:      	cmp	x24, #36
fffffe00079aa1a8:      	b.lo	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa1ac:      	ldr	w8, [x19, #924]
fffffe00079aa1b0:      	cbz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa1b4:      	ldr	w8, [x19, #1124]
fffffe00079aa1b8:      	cbnz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa1bc:      	ldr	w8, [x19, #1128]
fffffe00079aa1c0:      	cbnz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa1c4:      	ldr	w8, [x19, #24]
fffffe00079aa1c8:      	cbnz	w8, 0xfffffe00079a974c <_decode+0x290>
fffffe00079aa1cc:      	ldr	w8, [x19, #1108]
fffffe00079aa1d0:      	mov	w0, wzr
fffffe00079aa1d4:      	cbnz	w8, 0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa1d8:      	cmp	x24, #37
fffffe00079aa1dc:      	b.lo	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa1e0:      	mov	w8, #36
fffffe00079aa1e4:      	mov	w9, #1
fffffe00079aa1e8:      	b	0xfffffe00079aa200 <_decode+0xd44>
fffffe00079aa1ec:      	add	x8, x8, x10
fffffe00079aa1f0:      	mov	w0, wzr
fffffe00079aa1f4:      	add	x8, x8, #2
fffffe00079aa1f8:      	cmp	x24, x8
fffffe00079aa1fc:      	b.ls	0xfffffe00079a9750 <_decode+0x294>
fffffe00079aa200:      	sub	x10, x24, x8
fffffe00079aa204:      	subs	x12, x10, #2
fffffe00079aa208:      	b.lo	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa20c:      	add	x11, x8, #1
fffffe00079aa210:      	ldrb	w10, [x21, x11]
fffffe00079aa214:      	cmp	x12, x10
fffffe00079aa218:      	b.lo	0xfffffe00079a974c <_decode+0x290>
fffffe00079aa21c:      	ldrb	w12, [x21, x8]
fffffe00079aa220:      	cmp	w12, #5
fffffe00079aa224:      	b.ne	0xfffffe00079aa1ec <_decode+0xd30>
fffffe00079aa228:      	cmp	w10, #4
fffffe00079aa22c:      	b.lo	0xfffffe00079aa1ec <_decode+0xd30>
fffffe00079aa230:      	add	x12, x8, x21
fffffe00079aa234:      	ldrb	w12, [x12, #3]
fffffe00079aa238:      	cbz	w12, 0xfffffe00079aa1ec <_decode+0xd30>
fffffe00079aa23c:      	str	w12, [x19, #1136]
fffffe00079aa240:      	str	w9, [x19, #1132]
fffffe00079aa244:      	ldrb	w10, [x21, x11]
fffffe00079aa248:      	b	0xfffffe00079aa1ec <_decode+0xd30>
