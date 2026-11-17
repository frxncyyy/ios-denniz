
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b3514 <_nwp_receive>:
fffffe00079b3514:      	stp	x28, x27, [sp, #-80]!
fffffe00079b3518:      	stp	x24, x23, [sp, #16]
fffffe00079b351c:      	stp	x22, x21, [sp, #32]
fffffe00079b3520:      	stp	x20, x19, [sp, #48]
fffffe00079b3524:      	stp	x29, x30, [sp, #64]
fffffe00079b3528:      	add	x29, sp, #64
fffffe00079b352c:      	sub	sp, sp, #688
fffffe00079b3530:      	adrp	x8, 0xfffffe0007c94000 <_derive_keys+0x108>
fffffe00079b3534:      	nop
fffffe00079b3538:      	ldr	x8, [x8, #3376]
fffffe00079b353c:      	stur	x8, [x29, #-72]
fffffe00079b3540:      	cbz	x3, 0xfffffe00079b36b0 <_nwp_receive+0x19c>
fffffe00079b3544:      	movi.2d	v0, #0000000000000000
fffffe00079b3548:      	mov	x19, x3
fffffe00079b354c:      	mov	x20, x0
fffffe00079b3550:      	str	xzr, [x3, #272]
fffffe00079b3554:      	stp	q0, q0, [x3, #16]
fffffe00079b3558:      	stp	q0, q0, [x3, #48]
fffffe00079b355c:      	stp	q0, q0, [x3, #80]
fffffe00079b3560:      	stp	q0, q0, [x3, #112]
fffffe00079b3564:      	stp	q0, q0, [x3, #144]
fffffe00079b3568:      	stp	q0, q0, [x3, #176]
fffffe00079b356c:      	stp	q0, q0, [x3, #208]
fffffe00079b3570:      	stp	q0, q0, [x3, #240]
fffffe00079b3574:      	str	q0, [x3]
fffffe00079b3578:      	cbz	x0, 0xfffffe00079b36e8 <_nwp_receive+0x1d4>
fffffe00079b357c:      	ldr	w23, [x20, #32]
fffffe00079b3580:      	cbz	w23, 0xfffffe00079b36f0 <_nwp_receive+0x1dc>
fffffe00079b3584:      	mov	x21, x1
fffffe00079b3588:      	cbz	x1, 0xfffffe00079b36b8 <_nwp_receive+0x1a4>
fffffe00079b358c:      	mov	x22, x2
fffffe00079b3590:      	sub	x8, x2, #641
fffffe00079b3594:      	cmn	x8, #542
fffffe00079b3598:      	b.lo	0xfffffe00079b36b8 <_nwp_receive+0x1a4>
fffffe00079b359c:      	ldrb	w8, [x21]
fffffe00079b35a0:      	sub	w8, w8, #3
fffffe00079b35a4:      	cmn	w8, #2
fffffe00079b35a8:      	b.lo	0xfffffe00079b36b8 <_nwp_receive+0x1a4>
fffffe00079b35ac:      	ldrb	w8, [x21, #1]
fffffe00079b35b0:      	cmp	w8, #3
fffffe00079b35b4:      	b.ne	0xfffffe00079b36b8 <_nwp_receive+0x1a4>
fffffe00079b35b8:      	ldrh	w8, [x21, #2]
fffffe00079b35bc:      	sub	x9, x22, #4
fffffe00079b35c0:      	rev16	w8, w8
fffffe00079b35c4:      	cmp	x9, x8
fffffe00079b35c8:      	b.ne	0xfffffe00079b36b8 <_nwp_receive+0x1a4>
fffffe00079b35cc:      	ldrb	w8, [x21, #4]
fffffe00079b35d0:      	cmp	w8, #2
fffffe00079b35d4:      	b.ne	0xfffffe00079b36b8 <_nwp_receive+0x1a4>
fffffe00079b35d8:      	ldurh	w8, [x21, #97]
fffffe00079b35dc:      	rev16	w1, w8
fffffe00079b35e0:      	sub	x8, x22, #99
fffffe00079b35e4:      	cmp	x8, x1
fffffe00079b35e8:      	b.ne	0xfffffe00079b36b8 <_nwp_receive+0x1a4>
fffffe00079b35ec:      	ldurh	w8, [x21, #7]
fffffe00079b35f0:      	rev16	w8, w8
fffffe00079b35f4:      	cmp	w8, #16
fffffe00079b35f8:      	b.ne	0xfffffe00079b3730 <_nwp_receive+0x21c>
fffffe00079b35fc:      	ldurh	w8, [x21, #5]
fffffe00079b3600:      	mov	w9, #5066
fffffe00079b3604:      	rev16	w8, w8
fffffe00079b3608:      	cmp	w8, w9
fffffe00079b360c:      	b.eq	0xfffffe00079b3760 <_nwp_receive+0x24c>
fffffe00079b3610:      	cmp	w8, #138
fffffe00079b3614:      	b.ne	0xfffffe00079b3768 <_nwp_receive+0x254>
fffffe00079b3618:      	mov	x22, x21
fffffe00079b361c:      	ldr	q0, [x22, #17]!
fffffe00079b3620:      	ldr	q1, [x22, #16]
fffffe00079b3624:      	orr.16b	v0, v0, v1
fffffe00079b3628:      	ext.16b	v1, v0, v0, #8
fffffe00079b362c:      	orr.8b	v0, v0, v1
fffffe00079b3630:      	umov.b	w8, v0[1]
fffffe00079b3634:      	umov.b	w9, v0[0]
fffffe00079b3638:      	umov.b	w10, v0[2]
fffffe00079b363c:      	umov.b	w11, v0[3]
fffffe00079b3640:      	umov.b	w12, v0[4]
fffffe00079b3644:      	orr	w8, w9, w8
fffffe00079b3648:      	umov.b	w9, v0[5]
fffffe00079b364c:      	orr	w8, w8, w10
fffffe00079b3650:      	umov.b	w10, v0[6]
fffffe00079b3654:      	orr	w8, w8, w11
fffffe00079b3658:      	umov.b	w11, v0[7]
fffffe00079b365c:      	orr	w8, w8, w12
fffffe00079b3660:      	orr	w8, w8, w9
fffffe00079b3664:      	orr	w8, w8, w10
fffffe00079b3668:      	orr	w8, w8, w11
fffffe00079b366c:      	tst	w8, #0xff
fffffe00079b3670:      	b.eq	0xfffffe00079b3680 <_nwp_receive+0x16c>
fffffe00079b3674:      	add	x0, x21, #99
fffffe00079b3678:      	bl	0xfffffe00079b3cb4 <_m1_data>
fffffe00079b367c:      	cbz	w0, 0xfffffe00079b3878 <_nwp_receive+0x364>
fffffe00079b3680:      	mov	x8, xzr
fffffe00079b3684:      	strb	wzr, [x20, x8]
fffffe00079b3688:      	add	x8, x8, #1
fffffe00079b368c:      	cmp	x8, #1432
fffffe00079b3690:      	b.ne	0xfffffe00079b3684 <_nwp_receive+0x170>
fffffe00079b3694:      	mov	x8, xzr
fffffe00079b3698:      	strb	wzr, [x19, x8]
fffffe00079b369c:      	add	x8, x8, #1
fffffe00079b36a0:      	cmp	x8, #280
fffffe00079b36a4:      	b.ne	0xfffffe00079b3698 <_nwp_receive+0x184>
fffffe00079b36a8:      	mov	w8, #5
fffffe00079b36ac:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b36b0:      	mov	w0, #-1
fffffe00079b36b4:      	b	0xfffffe00079b36fc <_nwp_receive+0x1e8>
fffffe00079b36b8:      	mov	x8, xzr
fffffe00079b36bc:      	strb	wzr, [x20, x8]
fffffe00079b36c0:      	add	x8, x8, #1
fffffe00079b36c4:      	cmp	x8, #1432
fffffe00079b36c8:      	b.ne	0xfffffe00079b36bc <_nwp_receive+0x1a8>
fffffe00079b36cc:      	mov	x8, xzr
fffffe00079b36d0:      	strb	wzr, [x19, x8]
fffffe00079b36d4:      	add	x8, x8, #1
fffffe00079b36d8:      	cmp	x8, #280
fffffe00079b36dc:      	b.ne	0xfffffe00079b36d0 <_nwp_receive+0x1bc>
fffffe00079b36e0:      	mov	w8, #3
fffffe00079b36e4:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b36e8:      	mov	w8, #1
fffffe00079b36ec:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b36f0:      	mov	w8, #2
fffffe00079b36f4:      	mov	w0, #-1
fffffe00079b36f8:      	str	w8, [x19, #8]
fffffe00079b36fc:      	adrp	x8, 0xfffffe0007c94000 <_mic+0x10>
fffffe00079b3700:      	nop
fffffe00079b3704:      	ldur	x9, [x29, #-72]
fffffe00079b3708:      	ldr	x8, [x8, #3376]
fffffe00079b370c:      	cmp	x8, x9
fffffe00079b3710:      	b.ne	0xfffffe00079b3c10 <_nwp_receive+0x6fc>
fffffe00079b3714:      	add	sp, sp, #688
fffffe00079b3718:      	ldp	x29, x30, [sp, #64]
fffffe00079b371c:      	ldp	x20, x19, [sp, #48]
fffffe00079b3720:      	ldp	x22, x21, [sp, #32]
fffffe00079b3724:      	ldp	x24, x23, [sp, #16]
fffffe00079b3728:      	ldp	x28, x27, [sp], #80
fffffe00079b372c:      	ret
fffffe00079b3730:      	mov	x8, xzr
fffffe00079b3734:      	strb	wzr, [x20, x8]
fffffe00079b3738:      	add	x8, x8, #1
fffffe00079b373c:      	cmp	x8, #1432
fffffe00079b3740:      	b.ne	0xfffffe00079b3734 <_nwp_receive+0x220>
fffffe00079b3744:      	mov	x8, xzr
fffffe00079b3748:      	strb	wzr, [x19, x8]
fffffe00079b374c:      	add	x8, x8, #1
fffffe00079b3750:      	cmp	x8, #280
fffffe00079b3754:      	b.ne	0xfffffe00079b3748 <_nwp_receive+0x234>
fffffe00079b3758:      	mov	w8, #4
fffffe00079b375c:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3760:      	cmp	w23, #1
fffffe00079b3764:      	b.hi	0xfffffe00079b3798 <_nwp_receive+0x284>
fffffe00079b3768:      	mov	x8, xzr
fffffe00079b376c:      	strb	wzr, [x20, x8]
fffffe00079b3770:      	add	x8, x8, #1
fffffe00079b3774:      	cmp	x8, #1432
fffffe00079b3778:      	b.ne	0xfffffe00079b376c <_nwp_receive+0x258>
fffffe00079b377c:      	mov	x8, xzr
fffffe00079b3780:      	strb	wzr, [x19, x8]
fffffe00079b3784:      	add	x8, x8, #1
fffffe00079b3788:      	cmp	x8, #280
fffffe00079b378c:      	b.ne	0xfffffe00079b3780 <_nwp_receive+0x26c>
fffffe00079b3790:      	mov	w8, #7
fffffe00079b3794:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3798:      	sub	x3, x29, #96
fffffe00079b379c:      	mov	x0, x20
fffffe00079b37a0:      	mov	x1, x21
fffffe00079b37a4:      	mov	w2, w22
fffffe00079b37a8:      	stp	xzr, xzr, [x29, #-96]
fffffe00079b37ac:      	bl	0xfffffe00079b4270 <_mic>
fffffe00079b37b0:      	ldur	q0, [x29, #-96]
fffffe00079b37b4:      	ldur	q1, [x21, #81]
fffffe00079b37b8:      	sturb	wzr, [x29, #-96]
fffffe00079b37bc:      	sturb	wzr, [x29, #-95]
fffffe00079b37c0:      	sturb	wzr, [x29, #-94]
fffffe00079b37c4:      	eor.16b	v0, v1, v0
fffffe00079b37c8:      	sturb	wzr, [x29, #-93]
fffffe00079b37cc:      	ext.16b	v1, v0, v0, #8
fffffe00079b37d0:      	sturb	wzr, [x29, #-92]
fffffe00079b37d4:      	sturb	wzr, [x29, #-91]
fffffe00079b37d8:      	sturb	wzr, [x29, #-90]
fffffe00079b37dc:      	sturb	wzr, [x29, #-89]
fffffe00079b37e0:      	orr.8b	v0, v0, v1
fffffe00079b37e4:      	sturb	wzr, [x29, #-88]
fffffe00079b37e8:      	umov.b	w8, v0[1]
fffffe00079b37ec:      	umov.b	w9, v0[0]
fffffe00079b37f0:      	umov.b	w10, v0[2]
fffffe00079b37f4:      	umov.b	w11, v0[3]
fffffe00079b37f8:      	umov.b	w12, v0[4]
fffffe00079b37fc:      	sturb	wzr, [x29, #-87]
fffffe00079b3800:      	sturb	wzr, [x29, #-86]
fffffe00079b3804:      	orr	w8, w9, w8
fffffe00079b3808:      	umov.b	w9, v0[5]
fffffe00079b380c:      	orr	w8, w8, w10
fffffe00079b3810:      	umov.b	w10, v0[6]
fffffe00079b3814:      	orr	w8, w8, w11
fffffe00079b3818:      	umov.b	w11, v0[7]
fffffe00079b381c:      	orr	w8, w8, w12
fffffe00079b3820:      	sturb	wzr, [x29, #-85]
fffffe00079b3824:      	orr	w8, w8, w9
fffffe00079b3828:      	sturb	wzr, [x29, #-84]
fffffe00079b382c:      	orr	w8, w8, w10
fffffe00079b3830:      	sturb	wzr, [x29, #-83]
fffffe00079b3834:      	orr	w8, w8, w11
fffffe00079b3838:      	sturb	wzr, [x29, #-82]
fffffe00079b383c:      	tst	w8, #0xff
fffffe00079b3840:      	sturb	wzr, [x29, #-81]
fffffe00079b3844:      	b.eq	0xfffffe00079b3924 <_nwp_receive+0x410>
fffffe00079b3848:      	mov	x8, xzr
fffffe00079b384c:      	strb	wzr, [x20, x8]
fffffe00079b3850:      	add	x8, x8, #1
fffffe00079b3854:      	cmp	x8, #1432
fffffe00079b3858:      	b.ne	0xfffffe00079b384c <_nwp_receive+0x338>
fffffe00079b385c:      	mov	x8, xzr
fffffe00079b3860:      	strb	wzr, [x19, x8]
fffffe00079b3864:      	add	x8, x8, #1
fffffe00079b3868:      	cmp	x8, #280
fffffe00079b386c:      	b.ne	0xfffffe00079b3860 <_nwp_receive+0x34c>
fffffe00079b3870:      	mov	w8, #8
fffffe00079b3874:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3878:      	cmp	w23, #1
fffffe00079b387c:      	b.eq	0xfffffe00079b39a8 <_nwp_receive+0x494>
fffffe00079b3880:      	cmp	w23, #2
fffffe00079b3884:      	b.ne	0xfffffe00079b38f4 <_nwp_receive+0x3e0>
fffffe00079b3888:      	add	x22, x20, #511
fffffe00079b388c:      	add	x0, x21, #9
fffffe00079b3890:      	mov	x1, x22
fffffe00079b3894:      	mov	w2, #8
fffffe00079b3898:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b389c:      	tbnz	w0, #31, 0xfffffe00079b38f4 <_nwp_receive+0x3e0>
fffffe00079b38a0:      	ldur	q0, [x21, #17]
fffffe00079b38a4:      	ldur	q1, [x20, #124]
fffffe00079b38a8:      	ldur	q2, [x21, #33]
fffffe00079b38ac:      	ldur	q3, [x20, #140]
fffffe00079b38b0:      	eor.16b	v0, v1, v0
fffffe00079b38b4:      	eor.16b	v1, v3, v2
fffffe00079b38b8:      	orr.16b	v0, v0, v1
fffffe00079b38bc:      	ushll2.8h	v1, v0, #0
fffffe00079b38c0:      	ushll.8h	v0, v0, #0
fffffe00079b38c4:      	ext.16b	v2, v1, v1, #8
fffffe00079b38c8:      	ext.16b	v3, v0, v0, #8
fffffe00079b38cc:      	orr.8b	v0, v0, v1
fffffe00079b38d0:      	orr.8b	v1, v3, v2
fffffe00079b38d4:      	orr.8b	v0, v0, v1
fffffe00079b38d8:      	ushll.4s	v0, v0, #0
fffffe00079b38dc:      	ext.16b	v1, v0, v0, #8
fffffe00079b38e0:      	orr.8b	v0, v0, v1
fffffe00079b38e4:      	mov.s	w8, v0[1]
fffffe00079b38e8:      	fmov	w9, s0
fffffe00079b38ec:      	orr	w8, w9, w8
fffffe00079b38f0:      	cbz	w8, 0xfffffe00079b3acc <_nwp_receive+0x5b8>
fffffe00079b38f4:      	mov	x8, xzr
fffffe00079b38f8:      	strb	wzr, [x20, x8]
fffffe00079b38fc:      	add	x8, x8, #1
fffffe00079b3900:      	cmp	x8, #1432
fffffe00079b3904:      	b.ne	0xfffffe00079b38f8 <_nwp_receive+0x3e4>
fffffe00079b3908:      	mov	x8, xzr
fffffe00079b390c:      	strb	wzr, [x19, x8]
fffffe00079b3910:      	add	x8, x8, #1
fffffe00079b3914:      	cmp	x8, #280
fffffe00079b3918:      	b.ne	0xfffffe00079b390c <_nwp_receive+0x3f8>
fffffe00079b391c:      	mov	w8, #6
fffffe00079b3920:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3924:      	ldur	q0, [x21, #17]
fffffe00079b3928:      	ldur	q1, [x20, #124]
fffffe00079b392c:      	ldur	q2, [x21, #33]
fffffe00079b3930:      	ldur	q3, [x20, #140]
fffffe00079b3934:      	eor.16b	v0, v1, v0
fffffe00079b3938:      	eor.16b	v1, v3, v2
fffffe00079b393c:      	orr.16b	v0, v0, v1
fffffe00079b3940:      	ushll2.8h	v1, v0, #0
fffffe00079b3944:      	ushll.8h	v0, v0, #0
fffffe00079b3948:      	ext.16b	v2, v1, v1, #8
fffffe00079b394c:      	ext.16b	v3, v0, v0, #8
fffffe00079b3950:      	orr.8b	v0, v0, v1
fffffe00079b3954:      	orr.8b	v1, v3, v2
fffffe00079b3958:      	orr.8b	v0, v0, v1
fffffe00079b395c:      	ushll.4s	v0, v0, #0
fffffe00079b3960:      	ext.16b	v1, v0, v0, #8
fffffe00079b3964:      	orr.8b	v0, v0, v1
fffffe00079b3968:      	mov.s	w8, v0[1]
fffffe00079b396c:      	fmov	w9, s0
fffffe00079b3970:      	orr	w8, w9, w8
fffffe00079b3974:      	cbz	w8, 0xfffffe00079b39b8 <_nwp_receive+0x4a4>
fffffe00079b3978:      	mov	x8, xzr
fffffe00079b397c:      	strb	wzr, [x20, x8]
fffffe00079b3980:      	add	x8, x8, #1
fffffe00079b3984:      	cmp	x8, #1432
fffffe00079b3988:      	b.ne	0xfffffe00079b397c <_nwp_receive+0x468>
fffffe00079b398c:      	mov	x8, xzr
fffffe00079b3990:      	strb	wzr, [x19, x8]
fffffe00079b3994:      	add	x8, x8, #1
fffffe00079b3998:      	cmp	x8, #280
fffffe00079b399c:      	b.ne	0xfffffe00079b3990 <_nwp_receive+0x47c>
fffffe00079b39a0:      	mov	w8, #13
fffffe00079b39a4:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b39a8:      	ldr	x8, [x20, #16]
fffffe00079b39ac:      	cbz	x8, 0xfffffe00079b3a0c <_nwp_receive+0x4f8>
fffffe00079b39b0:      	mov	w0, #4
fffffe00079b39b4:      	b	0xfffffe00079b36fc <_nwp_receive+0x1e8>
fffffe00079b39b8:      	ldr	w8, [x20, #1428]
fffffe00079b39bc:      	cbz	w8, 0xfffffe00079b3a78 <_nwp_receive+0x564>
fffffe00079b39c0:      	cmp	x8, x22
fffffe00079b39c4:      	b.ne	0xfffffe00079b39dc <_nwp_receive+0x4c8>
fffffe00079b39c8:      	add	x0, x21, #9
fffffe00079b39cc:      	add	x1, x20, #797
fffffe00079b39d0:      	mov	w2, #8
fffffe00079b39d4:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b39d8:      	tbz	w0, #31, 0xfffffe00079b3ae0 <_nwp_receive+0x5cc>
fffffe00079b39dc:      	mov	x8, xzr
fffffe00079b39e0:      	strb	wzr, [x20, x8]
fffffe00079b39e4:      	add	x8, x8, #1
fffffe00079b39e8:      	cmp	x8, #1432
fffffe00079b39ec:      	b.ne	0xfffffe00079b39e0 <_nwp_receive+0x4cc>
fffffe00079b39f0:      	mov	x8, xzr
fffffe00079b39f4:      	strb	wzr, [x19, x8]
fffffe00079b39f8:      	add	x8, x8, #1
fffffe00079b39fc:      	cmp	x8, #280
fffffe00079b3a00:      	b.ne	0xfffffe00079b39f4 <_nwp_receive+0x4e0>
fffffe00079b3a04:      	mov	w8, #9
fffffe00079b3a08:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3a0c:      	ldp	q1, q0, [x22]
fffffe00079b3a10:      	add	x8, x20, #511
fffffe00079b3a14:      	mov	x0, x20
fffffe00079b3a18:      	stur	q1, [x20, #124]
fffffe00079b3a1c:      	stur	q0, [x20, #140]
fffffe00079b3a20:      	ldur	x9, [x21, #9]
fffffe00079b3a24:      	str	x9, [x8]
fffffe00079b3a28:      	bl	0xfffffe00079b3fac <_derive_keys>
fffffe00079b3a2c:      	mov	w8, #2
fffffe00079b3a30:      	str	w8, [x20, #32]
fffffe00079b3a34:      	adrp	x8, 0xfffffe0007c94000 <_kde+0x254>
fffffe00079b3a38:      	nop
fffffe00079b3a3c:      	ldur	x9, [x29, #-72]
fffffe00079b3a40:      	ldr	x8, [x8, #3376]
fffffe00079b3a44:      	cmp	x8, x9
fffffe00079b3a48:      	b.ne	0xfffffe00079b3c10 <_nwp_receive+0x6fc>
fffffe00079b3a4c:      	mov	x0, x20
fffffe00079b3a50:      	mov	x1, x21
fffffe00079b3a54:      	mov	w2, #1
fffffe00079b3a58:      	mov	x3, x19
fffffe00079b3a5c:      	add	sp, sp, #688
fffffe00079b3a60:      	ldp	x29, x30, [sp, #64]
fffffe00079b3a64:      	ldp	x20, x19, [sp, #48]
fffffe00079b3a68:      	ldp	x22, x21, [sp, #32]
fffffe00079b3a6c:      	ldp	x24, x23, [sp, #16]
fffffe00079b3a70:      	ldp	x28, x27, [sp], #80
fffffe00079b3a74:      	b	0xfffffe00079b3d9c <_reply>
fffffe00079b3a78:      	ldr	w8, [x20, #32]
fffffe00079b3a7c:      	cmp	w8, #2
fffffe00079b3a80:      	b.ne	0xfffffe00079b3a9c <_nwp_receive+0x588>
fffffe00079b3a84:      	add	x0, x21, #9
fffffe00079b3a88:      	add	x1, x20, #511
fffffe00079b3a8c:      	mov	w2, #8
fffffe00079b3a90:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b3a94:      	cmp	w0, #0
fffffe00079b3a98:      	b.gt	0xfffffe00079b3b88 <_nwp_receive+0x674>
fffffe00079b3a9c:      	mov	x8, xzr
fffffe00079b3aa0:      	strb	wzr, [x20, x8]
fffffe00079b3aa4:      	add	x8, x8, #1
fffffe00079b3aa8:      	cmp	x8, #1432
fffffe00079b3aac:      	b.ne	0xfffffe00079b3aa0 <_nwp_receive+0x58c>
fffffe00079b3ab0:      	mov	x8, xzr
fffffe00079b3ab4:      	strb	wzr, [x19, x8]
fffffe00079b3ab8:      	add	x8, x8, #1
fffffe00079b3abc:      	cmp	x8, #280
fffffe00079b3ac0:      	b.ne	0xfffffe00079b3ab4 <_nwp_receive+0x5a0>
fffffe00079b3ac4:      	mov	w8, #11
fffffe00079b3ac8:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3acc:      	ldr	x8, [x20, #16]
fffffe00079b3ad0:      	cbnz	x8, 0xfffffe00079b39b0 <_nwp_receive+0x49c>
fffffe00079b3ad4:      	ldur	x8, [x21, #9]
fffffe00079b3ad8:      	str	x8, [x22]
fffffe00079b3adc:      	b	0xfffffe00079b3a34 <_nwp_receive+0x520>
fffffe00079b3ae0:      	cbz	x22, 0xfffffe00079b3b54 <_nwp_receive+0x640>
fffffe00079b3ae4:      	mov	x8, xzr
fffffe00079b3ae8:      	b	0xfffffe00079b3af8 <_nwp_receive+0x5e4>
fffffe00079b3aec:      	add	w8, w8, #1
fffffe00079b3af0:      	cmp	x8, x22
fffffe00079b3af4:      	b.hs	0xfffffe00079b3b54 <_nwp_receive+0x640>
fffffe00079b3af8:      	sub	w9, w8, #9
fffffe00079b3afc:      	cmp	w9, #8
fffffe00079b3b00:      	b.lo	0xfffffe00079b3aec <_nwp_receive+0x5d8>
fffffe00079b3b04:      	sub	w9, w8, #81
fffffe00079b3b08:      	cmp	w9, #16
fffffe00079b3b0c:      	b.lo	0xfffffe00079b3aec <_nwp_receive+0x5d8>
fffffe00079b3b10:      	add	x9, x20, x8
fffffe00079b3b14:      	ldrb	w10, [x21, x8]
fffffe00079b3b18:      	ldrb	w9, [x9, #788]
fffffe00079b3b1c:      	cmp	w10, w9
fffffe00079b3b20:      	b.eq	0xfffffe00079b3aec <_nwp_receive+0x5d8>
fffffe00079b3b24:      	mov	x8, xzr
fffffe00079b3b28:      	strb	wzr, [x20, x8]
fffffe00079b3b2c:      	add	x8, x8, #1
fffffe00079b3b30:      	cmp	x8, #1432
fffffe00079b3b34:      	b.ne	0xfffffe00079b3b28 <_nwp_receive+0x614>
fffffe00079b3b38:      	mov	x8, xzr
fffffe00079b3b3c:      	strb	wzr, [x19, x8]
fffffe00079b3b40:      	add	x8, x8, #1
fffffe00079b3b44:      	cmp	x8, #280
fffffe00079b3b48:      	b.ne	0xfffffe00079b3b3c <_nwp_receive+0x628>
fffffe00079b3b4c:      	mov	w8, #10
fffffe00079b3b50:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3b54:      	ldr	x8, [x20, #16]
fffffe00079b3b58:      	cbnz	x8, 0xfffffe00079b39b0 <_nwp_receive+0x49c>
fffffe00079b3b5c:      	add	x0, x20, #788
fffffe00079b3b60:      	mov	x1, x21
fffffe00079b3b64:      	mov	x2, x22
fffffe00079b3b68:      	mov	w3, #640
fffffe00079b3b6c:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b3b70:      	mov	x0, x20
fffffe00079b3b74:      	mov	x1, x21
fffffe00079b3b78:      	mov	w2, #3
fffffe00079b3b7c:      	mov	x3, x19
fffffe00079b3b80:      	bl	0xfffffe00079b3d9c <_reply>
fffffe00079b3b84:      	b	0xfffffe00079b36fc <_nwp_receive+0x1e8>
fffffe00079b3b88:      	ldr	x8, [x20, #16]
fffffe00079b3b8c:      	cbnz	x8, 0xfffffe00079b39b0 <_nwp_receive+0x49c>
fffffe00079b3b90:      	add	x0, sp, #16
fffffe00079b3b94:      	mov	w1, #640
fffffe00079b3b98:      	add	x24, sp, #16
fffffe00079b3b9c:      	bl	0xfffffe0007ba9860 <_bzero>
fffffe00079b3ba0:      	ldurh	w8, [x21, #97]
fffffe00079b3ba4:      	add	x0, x20, #172
fffffe00079b3ba8:      	add	x1, x21, #99
fffffe00079b3bac:      	add	x3, sp, #16
fffffe00079b3bb0:      	add	x5, sp, #8
fffffe00079b3bb4:      	mov	w4, #640
fffffe00079b3bb8:      	rev16	w23, w8
fffffe00079b3bbc:      	str	xzr, [sp, #8]
fffffe00079b3bc0:      	mov	x2, x23
fffffe00079b3bc4:      	bl	0xfffffe00079b29dc <_nwpc_unwrap>
fffffe00079b3bc8:      	cbz	w0, 0xfffffe00079b3c14 <_nwp_receive+0x700>
fffffe00079b3bcc:      	mov	x8, xzr
fffffe00079b3bd0:      	strb	wzr, [x24, x8]
fffffe00079b3bd4:      	add	x8, x8, #1
fffffe00079b3bd8:      	cmp	x8, #640
fffffe00079b3bdc:      	b.ne	0xfffffe00079b3bd0 <_nwp_receive+0x6bc>
fffffe00079b3be0:      	mov	x8, xzr
fffffe00079b3be4:      	strb	wzr, [x20, x8]
fffffe00079b3be8:      	add	x8, x8, #1
fffffe00079b3bec:      	cmp	x8, #1432
fffffe00079b3bf0:      	b.ne	0xfffffe00079b3be4 <_nwp_receive+0x6d0>
fffffe00079b3bf4:      	mov	x8, xzr
fffffe00079b3bf8:      	strb	wzr, [x19, x8]
fffffe00079b3bfc:      	add	x8, x8, #1
fffffe00079b3c00:      	cmp	x8, #280
fffffe00079b3c04:      	b.ne	0xfffffe00079b3bf8 <_nwp_receive+0x6e4>
fffffe00079b3c08:      	mov	w8, #12
fffffe00079b3c0c:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3c10:      	bl	0xfffffe0007a024b4 <___stack_chk_fail>
fffffe00079b3c14:      	sub	w2, w23, #8
fffffe00079b3c18:      	add	x3, x21, #65
fffffe00079b3c1c:      	add	x1, sp, #16
fffffe00079b3c20:      	mov	x0, x20
fffffe00079b3c24:      	add	x23, sp, #16
fffffe00079b3c28:      	bl	0xfffffe00079b4364 <_kde>
fffffe00079b3c2c:      	mov	x8, xzr
fffffe00079b3c30:      	cbz	w0, 0xfffffe00079b3c74 <_nwp_receive+0x760>
fffffe00079b3c34:      	strb	wzr, [x23, x8]
fffffe00079b3c38:      	add	x8, x8, #1
fffffe00079b3c3c:      	cmp	x8, #640
fffffe00079b3c40:      	b.ne	0xfffffe00079b3c34 <_nwp_receive+0x720>
fffffe00079b3c44:      	mov	x8, xzr
fffffe00079b3c48:      	strb	wzr, [x20, x8]
fffffe00079b3c4c:      	add	x8, x8, #1
fffffe00079b3c50:      	cmp	x8, #1432
fffffe00079b3c54:      	b.ne	0xfffffe00079b3c48 <_nwp_receive+0x734>
fffffe00079b3c58:      	mov	x8, xzr
fffffe00079b3c5c:      	strb	wzr, [x19, x8]
fffffe00079b3c60:      	add	x8, x8, #1
fffffe00079b3c64:      	cmp	x8, #280
fffffe00079b3c68:      	b.ne	0xfffffe00079b3c5c <_nwp_receive+0x748>
fffffe00079b3c6c:      	mov	w8, #14
fffffe00079b3c70:      	b	0xfffffe00079b36f4 <_nwp_receive+0x1e0>
fffffe00079b3c74:      	strb	wzr, [x23, x8]
fffffe00079b3c78:      	add	x8, x8, #1
fffffe00079b3c7c:      	cmp	x8, #640
fffffe00079b3c80:      	b.ne	0xfffffe00079b3c74 <_nwp_receive+0x760>
fffffe00079b3c84:      	add	x0, x20, #788
fffffe00079b3c88:      	mov	x1, x21
fffffe00079b3c8c:      	mov	x2, x22
fffffe00079b3c90:      	mov	w3, #640
fffffe00079b3c94:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b3c98:      	mov	w8, #3
fffffe00079b3c9c:      	mov	x0, x20
fffffe00079b3ca0:      	mov	x1, x21
fffffe00079b3ca4:      	mov	w2, #2
fffffe00079b3ca8:      	str	w22, [x20, #1428]
fffffe00079b3cac:      	str	w8, [x20, #32]
fffffe00079b3cb0:      	b	0xfffffe00079b3b7c <_nwp_receive+0x668>
