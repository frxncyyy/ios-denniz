
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b36f8 <_nsdd_pid_status>:
fffffe00079b36f8:      	sub	sp, sp, #112
fffffe00079b36fc:      	stp	x29, x30, [sp, #96]
fffffe00079b3700:      	add	x29, sp, #96
fffffe00079b3704:      	cbz	x0, 0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3708:      	cbz	x1, 0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b370c:      	ldr	w2, [x1, #4]
fffffe00079b3710:      	cmp	w2, #255
fffffe00079b3714:      	b.hi	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3718:      	ldr	w3, [x1, #8]
fffffe00079b371c:      	cmp	w3, #255
fffffe00079b3720:      	b.hi	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3724:      	ldr	w9, [x1, #12]
fffffe00079b3728:      	cmp	w9, #255
fffffe00079b372c:      	b.hi	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3730:      	ldr	w9, [x1, #28]
fffffe00079b3734:      	cmp	w9, #255
fffffe00079b3738:      	b.hi	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b373c:      	ldr	w9, [x1, #32]
fffffe00079b3740:      	cmp	w9, #1
fffffe00079b3744:      	b.hi	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3748:      	ldrh	w9, [x1, #18]
fffffe00079b374c:      	cbnz	w9, 0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3750:      	ldr	w9, [x1, #20]
fffffe00079b3754:      	cmp	w9, #255
fffffe00079b3758:      	b.hi	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b375c:      	ldrh	w9, [x1, #26]
fffffe00079b3760:      	cbnz	w9, 0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3764:      	ldr	w9, [x1, #36]
fffffe00079b3768:      	cmp	w9, #255
fffffe00079b376c:      	b.ls	0xfffffe00079b377c <_nsdd_pid_status+0x84>
fffffe00079b3770:      	ldp	x29, x30, [sp, #96]
fffffe00079b3774:      	add	sp, sp, #112
fffffe00079b3778:      	ret
fffffe00079b377c:      	ldr	w9, [x1, #40]
fffffe00079b3780:      	cmp	w9, #255
fffffe00079b3784:      	b.hi	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3788:      	cmp	w2, #127
fffffe00079b378c:      	b.eq	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
fffffe00079b3790:      	mov	x8, x0
fffffe00079b3794:      	sub	w9, w2, #127
fffffe00079b3798:      	cmn	w9, #126
fffffe00079b379c:      	b.lo	0xfffffe00079b37f4 <_nsdd_pid_status+0xfc>
fffffe00079b37a0:      	mov	w9, #72
fffffe00079b37a4:      	adrp	x10, 0xfffffe0008544000 <_stc_receipt+0x4cc>
fffffe00079b37a8:      	add	x10, x10, #3392
fffffe00079b37ac:      	mul	x9, x2, x9
fffffe00079b37b0:      	ldr	x9, [x10, x9]
fffffe00079b37b4:      	cbz	x9, 0xfffffe00079b37f4 <_nsdd_pid_status+0xfc>
fffffe00079b37b8:      	mov	w11, #72
fffffe00079b37bc:      	madd	x11, x2, x11, x10
fffffe00079b37c0:      	ldr	w11, [x11, #12]
fffffe00079b37c4:      	cmp	w11, w3
fffffe00079b37c8:      	b.ne	0xfffffe00079b380c <_nsdd_pid_status+0x114>
fffffe00079b37cc:      	cmp	x9, x8
fffffe00079b37d0:      	b.ne	0xfffffe00079b381c <_nsdd_pid_status+0x124>
fffffe00079b37d4:      	mov	w9, #72
fffffe00079b37d8:      	madd	x9, x2, x9, x10
fffffe00079b37dc:      	ldr	w9, [x9, #68]
fffffe00079b37e0:      	cbz	w9, 0xfffffe00079b382c <_nsdd_pid_status+0x134>
fffffe00079b37e4:      	mov	x0, x8
fffffe00079b37e8:      	mov	x1, x8
fffffe00079b37ec:      	mov	w4, #4
fffffe00079b37f0:      	b	0xfffffe00079b3800 <_nsdd_pid_status+0x108>
fffffe00079b37f4:      	mov	x0, x8
fffffe00079b37f8:      	mov	x1, xzr
fffffe00079b37fc:      	mov	w4, #1
fffffe00079b3800:      	ldp	x29, x30, [sp, #96]
fffffe00079b3804:      	add	sp, sp, #112
fffffe00079b3808:      	b	0xfffffe00079b38c0 <_pid_note>
fffffe00079b380c:      	mov	x0, x8
fffffe00079b3810:      	mov	x1, x9
fffffe00079b3814:      	mov	w4, #2
fffffe00079b3818:      	b	0xfffffe00079b3800 <_nsdd_pid_status+0x108>
fffffe00079b381c:      	mov	x0, x8
fffffe00079b3820:      	mov	x1, x9
fffffe00079b3824:      	mov	w4, #3
fffffe00079b3828:      	b	0xfffffe00079b3800 <_nsdd_pid_status+0x108>
fffffe00079b382c:      	mov	w9, #72
fffffe00079b3830:      	madd	x9, x2, x9, x10
fffffe00079b3834:      	ldr	w9, [x9, #48]
fffffe00079b3838:      	cbz	w9, 0xfffffe00079b385c <_nsdd_pid_status+0x164>
fffffe00079b383c:      	mov	w9, #72
fffffe00079b3840:      	madd	x9, x2, x9, x10
fffffe00079b3844:      	ldr	w11, [x9, #60]!
fffffe00079b3848:      	cbz	w11, 0xfffffe00079b386c <_nsdd_pid_status+0x174>
fffffe00079b384c:      	mov	x0, x8
fffffe00079b3850:      	mov	x1, x8
fffffe00079b3854:      	mov	w4, #6
fffffe00079b3858:      	b	0xfffffe00079b3800 <_nsdd_pid_status+0x108>
fffffe00079b385c:      	mov	x0, x8
fffffe00079b3860:      	mov	x1, x8
fffffe00079b3864:      	mov	w4, #5
fffffe00079b3868:      	b	0xfffffe00079b3800 <_nsdd_pid_status+0x108>
fffffe00079b386c:      	mov	w11, #1
fffffe00079b3870:      	mov	w12, #72
fffffe00079b3874:      	adrp	x0, 0xfffffe0007134000 <_group_slot+0x50>
fffffe00079b3878:      	add	x0, x0, #1533
fffffe00079b387c:      	madd	x10, x2, x12, x10
fffffe00079b3880:      	str	w11, [x9]
fffffe00079b3884:      	ldr	w9, [x1, #12]
fffffe00079b3888:      	str	w9, [x10, #64]
fffffe00079b388c:      	ldp	w9, w10, [x1, #8]
fffffe00079b3890:      	ldp	w11, w12, [x1, #24]
fffffe00079b3894:      	ldp	w14, w13, [x1, #16]
fffffe00079b3898:      	ldp	w15, w16, [x1, #32]
fffffe00079b389c:      	ldr	w17, [x1, #40]
fffffe00079b38a0:      	stp	x2, x9, [sp, #8]
fffffe00079b38a4:      	stp	x11, x12, [sp, #40]
fffffe00079b38a8:      	stp	x10, x13, [sp, #24]
fffffe00079b38ac:      	stp	x16, x17, [sp, #72]
fffffe00079b38b0:      	stp	x15, x14, [sp, #56]
fffffe00079b38b4:      	str	x8, [sp]
fffffe00079b38b8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b38bc:      	b	0xfffffe00079b3770 <_nsdd_pid_status+0x78>
