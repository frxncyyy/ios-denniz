
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe000751e4b4 <_nss_owner_loop>:
fffffe000751e4b4:      	stp	d11, d10, [sp, #-128]!
fffffe000751e4b8:      	stp	d9, d8, [sp, #16]
fffffe000751e4bc:      	stp	x28, x27, [sp, #32]
fffffe000751e4c0:      	stp	x26, x25, [sp, #48]
fffffe000751e4c4:      	stp	x24, x23, [sp, #64]
fffffe000751e4c8:      	stp	x22, x21, [sp, #80]
fffffe000751e4cc:      	stp	x20, x19, [sp, #96]
fffffe000751e4d0:      	stp	x29, x30, [sp, #112]
fffffe000751e4d4:      	add	x29, sp, #112
fffffe000751e4d8:      	sub	sp, sp, #960
fffffe000751e4dc:      	adrp	x0, 0xfffffe000709a000 <_ncd_worker+0x778>
fffffe000751e4e0:      	add	x0, x0, #1224
fffffe000751e4e4:      	sub	x1, x29, #168
fffffe000751e4e8:      	mov	w2, #4
fffffe000751e4ec:      	stur	wzr, [x29, #-168]
fffffe000751e4f0:      	sub	x19, x29, #168
fffffe000751e4f4:      	bl	0xfffffe0007af46c4 <_PE_parse_boot_argn>
fffffe000751e4f8:      	ldur	w8, [x29, #-168]
fffffe000751e4fc:      	cmp	w8, #1
fffffe000751e500:      	b.ne	0xfffffe000751e544 <_nss_owner_loop+0x90>
fffffe000751e504:      	adrp	x8, 0xfffffe0008526000 <_njs_event+0x180>
fffffe000751e508:      	ldr	x20, [x8, #2648]
fffffe000751e50c:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe000751e510:      	cmp	x20, x0
fffffe000751e514:      	b.ne	0xfffffe000751e544 <_nss_owner_loop+0x90>
fffffe000751e518:      	add	x0, sp, #608
fffffe000751e51c:      	mov	w1, #8
fffffe000751e520:      	str	xzr, [sp, #608]
fffffe000751e524:      	bl	0xfffffe000747d5b4 <_read_random>
fffffe000751e528:      	ldr	x0, [sp, #608]
fffffe000751e52c:      	cbz	x0, 0xfffffe000751e544 <_nss_owner_loop+0x90>
fffffe000751e530:      	bl	0xfffffe00079b4aa4 <_nwl_initialize>
fffffe000751e534:      	cbnz	w0, 0xfffffe000751e544 <_nss_owner_loop+0x90>
fffffe000751e538:      	adrp	x0, 0xfffffe000709a000 <_ncd_worker+0x7d4>
fffffe000751e53c:      	add	x0, x0, #1238
fffffe000751e540:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751e544:      	sub	x8, x29, #192
fffffe000751e548:      	adrp	x13, 0xfffffe00084f8000 <_njs_event+0x10c>
fffffe000751e54c:      	add	x13, x13, #904
fffffe000751e550:      	orr	x9, x8, #0x6
fffffe000751e554:      	orr	x8, x19, #0x4
fffffe000751e558:      	adrp	x14, 0xfffffe00084f8000 <_njs_event+0x11c>
fffffe000751e55c:      	add	x14, x14, #572
fffffe000751e560:      	adrp	x21, 0xfffffe00084f8000 <_njs_event+0x124>
fffffe000751e564:      	add	x21, x21, #544
fffffe000751e568:      	adrp	x25, 0xfffffe00084f8000 <_njs_event+0x12c>
fffffe000751e56c:      	add	x25, x25, #560
fffffe000751e570:      	stp	x8, x9, [sp, #224]
fffffe000751e574:      	mov	w8, #4344
fffffe000751e578:      	add	x9, x21, #19, lsl #12
fffffe000751e57c:      	add	x8, x13, x8
fffffe000751e580:      	adrp	x12, 0xfffffe00084f8000 <_njs_event+0x144>
fffffe000751e584:      	add	x12, x12, #552
fffffe000751e588:      	adrp	x15, 0xfffffe00084f8000 <_njs_event+0x14c>
fffffe000751e58c:      	add	x15, x15, #596
fffffe000751e590:      	movi.2s	v8, #1
fffffe000751e594:      	str	x8, [sp, #424]
fffffe000751e598:      	add	x8, x14, #21, lsl #12
fffffe000751e59c:      	add	x8, x8, #1780
fffffe000751e5a0:      	adrp	x27, 0xfffffe00084dd000 <_njs_event+0xf8>
fffffe000751e5a4:      	str	x8, [sp, #216]
fffffe000751e5a8:      	add	x8, x21, #21, lsl #12
fffffe000751e5ac:      	add	x10, x8, #1808
fffffe000751e5b0:      	add	x8, x9, #240
fffffe000751e5b4:      	mov	w9, #4880
fffffe000751e5b8:      	stp	x8, x10, [sp, #160]
fffffe000751e5bc:      	adrp	x10, 0xfffffe000850c000 <_njs_event+0x1d0>
fffffe000751e5c0:      	add	x10, x10, #1552
fffffe000751e5c4:      	add	x8, x21, #21, lsl #12
fffffe000751e5c8:      	add	x11, x8, #1776
fffffe000751e5cc:      	add	x8, x10, x9
fffffe000751e5d0:      	adrp	x10, 0xfffffe000850b000 <_njs_event+0x1e0>
fffffe000751e5d4:      	add	x10, x10, #784
fffffe000751e5d8:      	stp	x8, x11, [sp, #144]
fffffe000751e5dc:      	mov	w8, #9728
fffffe000751e5e0:      	add	x8, x10, x8
fffffe000751e5e4:      	adrp	x11, 0xfffffe00084f8000 <_njs_event+0x1a8>
fffffe000751e5e8:      	add	x11, x11, #604
fffffe000751e5ec:      	add	x9, x11, #19, lsl #12
fffffe000751e5f0:      	str	x8, [sp, #176]
fffffe000751e5f4:      	add	x8, x11, #21, lsl #12
fffffe000751e5f8:      	add	x10, x9, #180
fffffe000751e5fc:      	add	x8, x8, #1812
fffffe000751e600:      	add	x9, x11, #24, lsl #12
fffffe000751e604:      	adrp	x11, 0xfffffe000700d000 <_ncd_worker+0x66c>
fffffe000751e608:      	stp	x8, x10, [sp, #488]
fffffe000751e60c:      	add	x8, x9, #1636
fffffe000751e610:      	adrp	x10, 0xfffffe00084f8000 <_njs_event+0x1d4>
fffffe000751e614:      	add	x10, x10, #556
fffffe000751e618:      	add	x9, x10, #19, lsl #12
fffffe000751e61c:      	str	x8, [sp, #480]
fffffe000751e620:      	add	x8, x25, #19, lsl #12
fffffe000751e624:      	add	x8, x8, #224
fffffe000751e628:      	str	x8, [sp, #552]
fffffe000751e62c:      	add	x8, x9, #1692
fffffe000751e630:      	adrp	x9, 0xfffffe000700d000 <_ncd_worker+0x698>
fffffe000751e634:      	str	x8, [sp, #464]
fffffe000751e638:      	add	x8, x10, #21, lsl #12
fffffe000751e63c:      	ldr	q0, [x9, #3584]
fffffe000751e640:      	add	x8, x8, #1860
fffffe000751e644:      	adrp	x9, 0xfffffe000700c000 <_ncd_worker+0x6a8>
fffffe000751e648:      	add	x10, x10, #24, lsl #12
fffffe000751e64c:      	str	q0, [sp, #336]
fffffe000751e650:      	ldr	q0, [x11, #3600]
fffffe000751e654:      	str	x8, [sp, #456]
fffffe000751e658:      	add	x8, x10, #1684
fffffe000751e65c:      	adrp	x10, 0xfffffe000700d000 <_ncd_worker+0x6c4>
fffffe000751e660:      	nop
fffffe000751e664:      	str	q0, [sp, #320]
fffffe000751e668:      	ldr	q0, [x9, #2800]
fffffe000751e66c:      	adrp	x9, 0xfffffe000700d000 <_ncd_worker+0x6d4>
fffffe000751e670:      	str	x8, [sp, #448]
fffffe000751e674:      	add	x8, x12, #19, lsl #12
fffffe000751e678:      	str	q0, [sp, #304]
fffffe000751e67c:      	ldr	q0, [x10, #3616]
fffffe000751e680:      	ldr	d9, [x9, #3392]
fffffe000751e684:      	nop
fffffe000751e688:      	adrp	x10, 0xfffffe0007009000 <_ncd_worker+0x6e0>
fffffe000751e68c:      	add	x12, x8, #232
fffffe000751e690:      	str	q0, [sp, #288]
fffffe000751e694:      	ldr	q0, [x11, #3408]
fffffe000751e698:      	adrp	x8, 0xfffffe000700d000 <_ncd_worker+0x700>
fffffe000751e69c:      	adrp	x11, 0xfffffe000700c000 <_ncd_worker+0x700>
fffffe000751e6a0:      	ldr	d10, [x10, #400]
fffffe000751e6a4:      	mov	w10, #4784
fffffe000751e6a8:      	str	q0, [sp, #256]
fffffe000751e6ac:      	ldr	q0, [x9, #3552]
fffffe000751e6b0:      	adrp	x9, 0xfffffe000850b000 <_njs_event+0x2c0>
fffffe000751e6b4:      	add	x9, x9, #2112
fffffe000751e6b8:      	add	x10, x12, x10
fffffe000751e6bc:      	ldr	d11, [x11, #264]
fffffe000751e6c0:      	str	q0, [sp, #528]
fffffe000751e6c4:      	ldr	q0, [x8, #3568]
fffffe000751e6c8:      	mov	w8, #8400
fffffe000751e6cc:      	adrp	x11, 0xfffffe0008516000 <_njs_event+0x308>
fffffe000751e6d0:      	add	x11, x11, #2112
fffffe000751e6d4:      	add	x8, x9, x8
fffffe000751e6d8:      	str	x10, [sp, #280]
fffffe000751e6dc:      	adrp	x10, 0xfffffe00084f8000 <_njs_event+0x2a0>
fffffe000751e6e0:      	add	x10, x10, #588
fffffe000751e6e4:      	add	x9, x15, #19, lsl #12
fffffe000751e6e8:      	str	x8, [sp, #248]
fffffe000751e6ec:      	mov	w8, #4660
fffffe000751e6f0:      	add	x8, x10, x8
fffffe000751e6f4:      	adrp	x10, 0xfffffe0008526000 <_njs_event+0x370>
fffffe000751e6f8:      	add	x10, x10, #2724
fffffe000751e6fc:      	str	q0, [sp, #384]
fffffe000751e700:      	str	x12, [sp, #368]
fffffe000751e704:      	str	x8, [sp, #104]
fffffe000751e708:      	add	x8, x9, #188
fffffe000751e70c:      	mov	w9, #8696
fffffe000751e710:      	str	x8, [sp, #408]
fffffe000751e714:      	adrp	x8, 0xfffffe00084f8000 <_njs_event+0x2d8>
fffffe000751e718:      	add	x8, x8, #608
fffffe000751e71c:      	add	x8, x8, #19, lsl #12
fffffe000751e720:      	add	x8, x8, #176
fffffe000751e724:      	str	x8, [sp, #472]
fffffe000751e728:      	add	x8, x10, x9
fffffe000751e72c:      	adrp	x10, 0xfffffe00084ef000 <_njs_event+0x2cc>
fffffe000751e730:      	add	x10, x10, #2776
fffffe000751e734:      	mov	w9, #33264
fffffe000751e738:      	str	x8, [sp, #520]
fffffe000751e73c:      	adrp	x8, 0xfffffe00084dd000 <_njs_event+0x294>
fffffe000751e740:      	add	x8, x8, #2480
fffffe000751e744:      	add	x8, x8, #26, lsl #12
fffffe000751e748:      	add	x8, x8, #792
fffffe000751e74c:      	str	x8, [sp, #504]
fffffe000751e750:      	add	x8, x10, x9
fffffe000751e754:      	adrp	x10, 0xfffffe0008528000 <_njs_snapshot_locked+0x38>
fffffe000751e758:      	add	x10, x10, #3744
fffffe000751e75c:      	mov	w9, #9744
fffffe000751e760:      	str	x8, [sp, #376]
fffffe000751e764:      	mov	w8, #8128
fffffe000751e768:      	add	x8, x10, x8
fffffe000751e76c:      	str	x8, [sp, #240]
fffffe000751e770:      	add	x8, x11, x9
fffffe000751e774:      	str	x8, [sp, #512]
fffffe000751e778:      	add	x8, sp, #608
fffffe000751e77c:      	add	x10, x8, #16
fffffe000751e780:      	add	x9, x8, #24
fffffe000751e784:      	add	x8, x8, #92
fffffe000751e788:      	stp	x9, x10, [sp, #128]
fffffe000751e78c:      	mov	w9, #59352
fffffe000751e790:      	str	x8, [sp, #120]
fffffe000751e794:      	mov	w8, #22680
fffffe000751e798:      	add	x8, x13, x8
fffffe000751e79c:      	mov	w10, #55608
fffffe000751e7a0:      	str	x8, [sp, #360]
fffffe000751e7a4:      	mov	w8, #41016
fffffe000751e7a8:      	add	x8, x13, x8
fffffe000751e7ac:      	str	x8, [sp, #400]
fffffe000751e7b0:      	add	x8, x13, x9
fffffe000751e7b4:      	mov	w9, #37072
fffffe000751e7b8:      	str	x8, [sp, #416]
fffffe000751e7bc:      	adrp	x8, 0xfffffe0008516000 <_njs_snapshot_locked+0x58>
fffffe000751e7c0:      	add	x8, x8, #2072
fffffe000751e7c4:      	add	x9, x8, x9
fffffe000751e7c8:      	str	x9, [sp, #208]
fffffe000751e7cc:      	add	x9, x8, x10
fffffe000751e7d0:      	mov	w10, #12500
fffffe000751e7d4:      	movk	w10, #1, lsl #16
fffffe000751e7d8:      	str	x9, [sp, #200]
fffffe000751e7dc:      	mov	w9, #17364
fffffe000751e7e0:      	movk	w9, #1, lsl #16
fffffe000751e7e4:      	add	x9, x14, x9
fffffe000751e7e8:      	str	x9, [sp, #192]
fffffe000751e7ec:      	add	x9, x14, x10
fffffe000751e7f0:      	str	x9, [sp, #184]
fffffe000751e7f4:      	mov	w9, #55716
fffffe000751e7f8:      	add	x8, x8, x9
fffffe000751e7fc:      	str	x8, [sp, #112]
fffffe000751e800:      	mov	w8, #22300
fffffe000751e804:      	movk	w8, #1, lsl #16
fffffe000751e808:      	add	x8, x15, x8
fffffe000751e80c:      	str	x8, [sp, #440]
fffffe000751e810:      	mov	w8, #34412
fffffe000751e814:      	movk	w8, #1, lsl #16
fffffe000751e818:      	add	x8, x15, x8
fffffe000751e81c:      	str	x8, [sp, #432]
fffffe000751e820:      	b	0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe000751e824:      	mov	w23, #3
fffffe000751e828:      	mov	x0, x24
fffffe000751e82c:      	mov	w1, w23
fffffe000751e830:      	bl	0xfffffe0007521e64 <_nss_deliver>
fffffe000751e834:      	add	x8, sp, #608
fffffe000751e838:      	sub	x0, x29, #168
fffffe000751e83c:      	stur	wzr, [x29, #-160]
fffffe000751e840:      	str	xzr, [x8, #296]
fffffe000751e844:      	bl	0xfffffe00079b4bc8 <_nwl_take>
fffffe000751e848:      	cbz	w0, 0xfffffe000751e8ac <_nss_owner_loop+0x3f8>
fffffe000751e84c:      	movi.2d	v0, #0000000000000000
fffffe000751e850:      	add	x8, sp, #608
fffffe000751e854:      	stp	q0, q0, [x8, #128]
fffffe000751e858:      	stp	q0, q0, [x8, #160]
fffffe000751e85c:      	stp	q0, q0, [x8, #192]
fffffe000751e860:      	stp	q0, q0, [x8, #224]
fffffe000751e864:      	adrp	x8, 0xfffffe00084dd000 <_njs_snapshot_locked+0x1c>
fffffe000751e868:      	add	x8, x8, #2464
fffffe000751e86c:      	stp	q0, q0, [sp, #608]
fffffe000751e870:      	stp	q0, q0, [sp, #640]
fffffe000751e874:      	stp	q0, q0, [sp, #672]
fffffe000751e878:      	stp	q0, q0, [sp, #704]
fffffe000751e87c:      	ldar	w8, [x8]
fffffe000751e880:      	cmp	w8, #1
fffffe000751e884:      	b.ne	0xfffffe000751e89c <_nss_owner_loop+0x3e8>
fffffe000751e888:      	adrp	x8, 0xfffffe0008526000 <_njs_snapshot_locked+0x164>
fffffe000751e88c:      	ldr	x19, [x8, #2648]
fffffe000751e890:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe000751e894:      	cmp	x19, x0
fffffe000751e898:      	b.eq	0xfffffe000751ebd8 <_nss_owner_loop+0x724>
fffffe000751e89c:      	sub	x0, x29, #168
fffffe000751e8a0:      	add	x2, sp, #608
fffffe000751e8a4:      	mov	w1, #1
fffffe000751e8a8:      	bl	0xfffffe00079b4c30 <_nwl_complete>
fffffe000751e8ac:      	ldr	x0, [x27, #2456]
fffffe000751e8b0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e8b4:      	adrp	x10, 0xfffffe00084f8000 <_njs_snapshot_locked+0xd8>
fffffe000751e8b8:      	add	x10, x10, #552
fffffe000751e8bc:      	mov	w22, wzr
fffffe000751e8c0:      	ldp	w8, w9, [x10, #8]
fffffe000751e8c4:      	ldp	w19, w26, [x10]
fffffe000751e8c8:      	orr	w8, w8, w9
fffffe000751e8cc:      	cmp	w8, #0
fffffe000751e8d0:      	cset	w20, eq
fffffe000751e8d4:      	cbz	w19, 0xfffffe000751e924 <_nss_owner_loop+0x470>
fffffe000751e8d8:      	cbnz	w26, 0xfffffe000751e924 <_nss_owner_loop+0x470>
fffffe000751e8dc:      	mov	w8, #55440
fffffe000751e8e0:      	adrp	x11, 0xfffffe00084dd000 <_njs_snapshot_locked+0x98>
fffffe000751e8e4:      	add	x11, x11, #2584
fffffe000751e8e8:      	mov	w9, #36960
fffffe000751e8ec:      	mov	w10, #18480
fffffe000751e8f0:      	ldr	w8, [x11, x8]
fffffe000751e8f4:      	ldr	w9, [x11, x9]
fffffe000751e8f8:      	ldr	w10, [x11, x10]
fffffe000751e8fc:      	sub	w8, w8, #1
fffffe000751e900:      	sub	w9, w9, #1
fffffe000751e904:      	cmp	w8, #2
fffffe000751e908:      	ccmp	w9, #2, #0, hs
fffffe000751e90c:      	ldr	w8, [x11]
fffffe000751e910:      	sub	w9, w10, #1
fffffe000751e914:      	ccmp	w9, #2, #0, hs
fffffe000751e918:      	sub	w8, w8, #1
fffffe000751e91c:      	ccmp	w8, #2, #0, hs
fffffe000751e920:      	cset	w22, lo
fffffe000751e924:      	adrp	x8, 0xfffffe00084f8000 <_njs_snapshot_locked+0x148>
fffffe000751e928:      	add	x8, x8, #712
fffffe000751e92c:      	ldr	x0, [x27, #2456]
fffffe000751e930:      	ldp	x24, x23, [x8]
fffffe000751e934:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e938:      	orr	w8, w19, w26
fffffe000751e93c:      	cbz	w8, 0xfffffe000751eaf0 <_nss_owner_loop+0x63c>
fffffe000751e940:      	cbz	w19, 0xfffffe000751e9a4 <_nss_owner_loop+0x4f0>
fffffe000751e944:      	add	x26, sp, #608
fffffe000751e948:      	add	x0, sp, #608
fffffe000751e94c:      	str	xzr, [sp, #608]
fffffe000751e950:      	str	xzr, [x26, #296]
fffffe000751e954:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751e958:      	ldr	x0, [sp, #608]
fffffe000751e95c:      	sub	x1, x29, #168
fffffe000751e960:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751e964:      	ldr	x8, [x26, #296]
fffffe000751e968:      	mov	x9, #63439
fffffe000751e96c:      	movk	x9, #58195, lsl #16
fffffe000751e970:      	movk	x9, #39845, lsl #32
fffffe000751e974:      	lsr	x8, x8, #3
fffffe000751e978:      	movk	x9, #8388, lsl #48
fffffe000751e97c:      	umulh	x8, x8, x9
fffffe000751e980:      	lsr	x8, x8, #4
fffffe000751e984:      	cmp	x8, x24
fffffe000751e988:      	cset	w9, hs
fffffe000751e98c:      	and	w9, w20, w9
fffffe000751e990:      	cmp	w9, #1
fffffe000751e994:      	b.ne	0xfffffe000751eb8c <_nss_owner_loop+0x6d8>
fffffe000751e998:      	cmp	x8, x23
fffffe000751e99c:      	b.hs	0xfffffe000751eb8c <_nss_owner_loop+0x6d8>
fffffe000751e9a0:      	cbnz	w22, 0xfffffe000751eaf4 <_nss_owner_loop+0x640>
fffffe000751e9a4:      	adrp	x8, 0xfffffe0008526000 <_njs_rejoin_scan_done+0x44>
fffffe000751e9a8:      	add	x8, x8, #2660
fffffe000751e9ac:      	add	x9, sp, #608
fffffe000751e9b0:      	str	xzr, [sp, #608]
fffffe000751e9b4:      	ldr	w8, [x8]
fffffe000751e9b8:      	str	xzr, [x9, #296]
fffffe000751e9bc:      	cbz	w8, 0xfffffe0007521adc <_nss_owner_loop+0x3628>
fffffe000751e9c0:      	adrp	x8, 0xfffffe0008526000 <_njs_rejoin_scan_done+0x60>
fffffe000751e9c4:      	add	x8, x8, #2660
fffffe000751e9c8:      	ldr	w8, [x8, #8]
fffffe000751e9cc:      	cbnz	w8, 0xfffffe0007521adc <_nss_owner_loop+0x3628>
fffffe000751e9d0:      	adrp	x8, 0xfffffe0008526000 <_njs_rejoin_scan_done+0x70>
fffffe000751e9d4:      	ldr	x20, [x8, #2648]
fffffe000751e9d8:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe000751e9dc:      	cmp	x20, x0
fffffe000751e9e0:      	str	x0, [sp, #592]
fffffe000751e9e4:      	b.ne	0xfffffe0007521adc <_nss_owner_loop+0x3628>
fffffe000751e9e8:      	add	x0, sp, #608
fffffe000751e9ec:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751e9f0:      	ldr	x0, [sp, #608]
fffffe000751e9f4:      	sub	x1, x29, #168
fffffe000751e9f8:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751e9fc:      	add	x8, sp, #608
fffffe000751ea00:      	mov	x9, #63439
fffffe000751ea04:      	movk	x9, #58195, lsl #16
fffffe000751ea08:      	movk	x9, #39845, lsl #32
fffffe000751ea0c:      	ldr	x8, [x8, #296]
fffffe000751ea10:      	movk	x9, #8388, lsl #48
fffffe000751ea14:      	lsr	x8, x8, #3
fffffe000751ea18:      	umulh	x8, x8, x9
fffffe000751ea1c:      	lsr	x26, x8, #4
fffffe000751ea20:      	cbz	w19, 0xfffffe000751f0f0 <_nss_owner_loop+0xc3c>
fffffe000751ea24:      	ldr	x0, [x27, #2456]
fffffe000751ea28:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751ea2c:      	ldp	w8, w9, [x25]
fffffe000751ea30:      	orr	w10, w8, w9
fffffe000751ea34:      	ldr	x9, [x25, #160]
fffffe000751ea38:      	cmp	w10, #0
fffffe000751ea3c:      	cset	w8, ne
fffffe000751ea40:      	cbnz	w10, 0xfffffe000751ec08 <_nss_owner_loop+0x754>
fffffe000751ea44:      	ldr	x10, [x25, #152]
fffffe000751ea48:      	cmp	x10, x26
fffffe000751ea4c:      	b.hi	0xfffffe000751ec08 <_nss_owner_loop+0x754>
fffffe000751ea50:      	cmp	x9, x26
fffffe000751ea54:      	b.ls	0xfffffe000751ec08 <_nss_owner_loop+0x754>
fffffe000751ea58:      	adrp	x19, 0xfffffe00084f8000 <_njs_rejoin_scan_done+0x40>
fffffe000751ea5c:      	add	x19, x19, #904
fffffe000751ea60:      	add	x23, x19, #1496
fffffe000751ea64:      	adrp	x20, 0xfffffe00084dd000 <_njs_notice+0x90>
fffffe000751ea68:      	add	x20, x20, #2484
fffffe000751ea6c:      	mov	x0, x19
fffffe000751ea70:      	ldr	x1, [sp, #424]
fffffe000751ea74:      	mov	x2, x20
fffffe000751ea78:      	mov	x3, x26
fffffe000751ea7c:      	mov	x4, x23
fffffe000751ea80:      	bl	0xfffffe00079a964c <_niwj_select>
fffffe000751ea84:      	cbz	w0, 0xfffffe000751ef54 <_nss_owner_loop+0xaa0>
fffffe000751ea88:      	mov	w8, #22680
fffffe000751ea8c:      	mov	x0, x19
fffffe000751ea90:      	add	x1, x19, x8
fffffe000751ea94:      	mov	x2, x20
fffffe000751ea98:      	mov	x3, x26
fffffe000751ea9c:      	mov	x4, x23
fffffe000751eaa0:      	bl	0xfffffe00079a964c <_niwj_select>
fffffe000751eaa4:      	cbz	w0, 0xfffffe000751ef98 <_nss_owner_loop+0xae4>
fffffe000751eaa8:      	mov	w8, #41016
fffffe000751eaac:      	add	x23, x19, #1496
fffffe000751eab0:      	add	x1, x19, x8
fffffe000751eab4:      	mov	x0, x19
fffffe000751eab8:      	mov	x2, x20
fffffe000751eabc:      	mov	x3, x26
fffffe000751eac0:      	mov	x4, x23
fffffe000751eac4:      	bl	0xfffffe00079a964c <_niwj_select>
fffffe000751eac8:      	cbz	w0, 0xfffffe000751efe0 <_nss_owner_loop+0xb2c>
fffffe000751eacc:      	mov	w8, #59352
fffffe000751ead0:      	mov	x0, x19
fffffe000751ead4:      	add	x1, x19, x8
fffffe000751ead8:      	mov	x2, x20
fffffe000751eadc:      	mov	x3, x26
fffffe000751eae0:      	mov	x4, x23
fffffe000751eae4:      	bl	0xfffffe00079a964c <_niwj_select>
fffffe000751eae8:      	cbnz	w0, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751eaec:      	b	0xfffffe000751f028 <_nss_owner_loop+0xb74>
fffffe000751eaf0:      	bl	0xfffffe0007521fd8 <_njs_rejoin_refresh>
fffffe000751eaf4:      	adrp	x19, 0xfffffe00084dd000 <_njs_rejoin_scan_done+0x70>
fffffe000751eaf8:      	add	x19, x19, #2456
fffffe000751eafc:      	ldr	x0, [x19]
fffffe000751eb00:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751eb04:      	mov	w8, #18608
fffffe000751eb08:      	ldr	w9, [x19, #128]
fffffe000751eb0c:      	ldr	w8, [x19, x8]
fffffe000751eb10:      	cmp	w9, #1
fffffe000751eb14:      	b.ne	0xfffffe000751eb4c <_nss_owner_loop+0x698>
fffffe000751eb18:      	adrp	x24, 0xfffffe00084dd000 <_njs_rejoin_scan_done+0x94>
fffffe000751eb1c:      	add	x24, x24, #2584
fffffe000751eb20:      	adrp	x20, 0xfffffe00084ef000 <_njs_rejoin_scan_done+0xe4>
fffffe000751eb24:      	add	x20, x20, #2776
fffffe000751eb28:      	cmp	w8, #1
fffffe000751eb2c:      	b.ne	0xfffffe000751eb64 <_nss_owner_loop+0x6b0>
fffffe000751eb30:      	adrp	x24, 0xfffffe00084dd000 <_njs_rejoin_scan_done+0xac>
fffffe000751eb34:      	add	x24, x24, #2584
fffffe000751eb38:      	ldr	x8, [x24, #18496]
fffffe000751eb3c:      	ldr	x9, [x24, #16]
fffffe000751eb40:      	cmp	x8, x9
fffffe000751eb44:      	b.lo	0xfffffe000751eb5c <_nss_owner_loop+0x6a8>
fffffe000751eb48:      	b	0xfffffe000751eb64 <_nss_owner_loop+0x6b0>
fffffe000751eb4c:      	adrp	x20, 0xfffffe00084ef000 <_njs_rejoin_scan_done+0x110>
fffffe000751eb50:      	add	x20, x20, #2776
fffffe000751eb54:      	cmp	w8, #1
fffffe000751eb58:      	b.ne	0xfffffe000751ebac <_nss_owner_loop+0x6f8>
fffffe000751eb5c:      	adrp	x24, 0xfffffe00084e2000 <_njs_rejoin_scan_done+0xec>
fffffe000751eb60:      	add	x24, x24, #584
fffffe000751eb64:      	adrp	x8, 0xfffffe00084e6000 <_njs_rejoin_scan_done+0x104>
fffffe000751eb68:      	ldr	w8, [x8, #2680]
fffffe000751eb6c:      	cmp	w8, #1
fffffe000751eb70:      	b.ne	0xfffffe000751ec2c <_nss_owner_loop+0x778>
fffffe000751eb74:      	adrp	x8, 0xfffffe00084e6000 <_njs_rejoin_scan_done+0x114>
fffffe000751eb78:      	ldr	x9, [x24, #16]
fffffe000751eb7c:      	ldr	x8, [x8, #2696]
fffffe000751eb80:      	cmp	x8, x9
fffffe000751eb84:      	b.lo	0xfffffe000751ebbc <_nss_owner_loop+0x708>
fffffe000751eb88:      	b	0xfffffe000751ec2c <_nss_owner_loop+0x778>
fffffe000751eb8c:      	cmp	x8, x23
fffffe000751eb90:      	mov	w9, #9
fffffe000751eb94:      	cset	w8, hs
fffffe000751eb98:      	tst	w20, w8
fffffe000751eb9c:      	mov	w8, #2
fffffe000751eba0:      	csel	w0, w9, w8, ne
fffffe000751eba4:      	bl	0xfffffe0007522178 <_njs_early_finish>
fffffe000751eba8:      	b	0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe000751ebac:      	adrp	x8, 0xfffffe00084e6000 <_njs_rejoin_scan_done+0x14c>
fffffe000751ebb0:      	ldr	w8, [x8, #2680]
fffffe000751ebb4:      	cmp	w8, #1
fffffe000751ebb8:      	b.ne	0xfffffe000751ec28 <_nss_owner_loop+0x774>
fffffe000751ebbc:      	mov	w8, #18480
fffffe000751ebc0:      	adrp	x24, 0xfffffe00084e6000 <_njs_rejoin_scan_done+0x160>
fffffe000751ebc4:      	add	x24, x24, #2680
fffffe000751ebc8:      	ldr	w8, [x24, x8]
fffffe000751ebcc:      	cmp	w8, #1
fffffe000751ebd0:      	b.eq	0xfffffe000751ec40 <_nss_owner_loop+0x78c>
fffffe000751ebd4:      	b	0xfffffe000751ec64 <_nss_owner_loop+0x7b0>
fffffe000751ebd8:      	ldr	x0, [x27, #2456]
fffffe000751ebdc:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751ebe0:      	adrp	x8, 0xfffffe00084f8000 <_njs_rejoin_scan_done+0x1c8>
fffffe000751ebe4:      	ldur	w19, [x29, #-164]
fffffe000751ebe8:      	adrp	x26, 0xfffffe0008531000 <_njs_peer+0x70>
fffffe000751ebec:      	ldr	x22, [x8, #752]
fffffe000751ebf0:      	cmp	w19, #1
fffffe000751ebf4:      	b.ne	0xfffffe000751eee4 <_nss_owner_loop+0xa30>
fffffe000751ebf8:      	ldr	x0, [x27, #2456]
fffffe000751ebfc:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ec00:      	mov	w24, wzr
fffffe000751ec04:      	b	0xfffffe0007520cf4 <_nss_owner_loop+0x2840>
fffffe000751ec08:      	cmp	x9, x26
fffffe000751ec0c:      	cset	w9, hi
fffffe000751ec10:      	orr	w8, w8, w9
fffffe000751ec14:      	mov	w9, #9
fffffe000751ec18:      	cmp	w8, #0
fffffe000751ec1c:      	mov	w8, #2
fffffe000751ec20:      	csel	w23, w8, w9, ne
fffffe000751ec24:      	b	0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751ec28:      	mov	x24, xzr
fffffe000751ec2c:      	adrp	x8, 0xfffffe00084eb000 <_njs_rejoin_scan_done+0x1e0>
fffffe000751ec30:      	ldr	w8, [x8, #680]
fffffe000751ec34:      	cmp	w8, #1
fffffe000751ec38:      	b.ne	0xfffffe000751ec60 <_nss_owner_loop+0x7ac>
fffffe000751ec3c:      	cbz	x24, 0xfffffe000751ec54 <_nss_owner_loop+0x7a0>
fffffe000751ec40:      	adrp	x8, 0xfffffe00084eb000 <_njs_rejoin_scan_done+0x1f4>
fffffe000751ec44:      	ldr	x9, [x24, #16]
fffffe000751ec48:      	ldr	x8, [x8, #696]
fffffe000751ec4c:      	cmp	x8, x9
fffffe000751ec50:      	b.hs	0xfffffe000751ec64 <_nss_owner_loop+0x7b0>
fffffe000751ec54:      	adrp	x24, 0xfffffe00084eb000 <_njs_rejoin_scan_done+0x208>
fffffe000751ec58:      	add	x24, x24, #680
fffffe000751ec5c:      	b	0xfffffe000751ec64 <_nss_owner_loop+0x7b0>
fffffe000751ec60:      	cbz	x24, 0xfffffe000751ee88 <_nss_owner_loop+0x9d4>
fffffe000751ec64:      	mov	w23, #2
fffffe000751ec68:      	ldr	w19, [x24, #4]
fffffe000751ec6c:      	str	w23, [x24]
fffffe000751ec70:      	ldr	x0, [x27, #2456]
fffffe000751ec74:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ec78:      	cbnz	w19, 0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe000751ec7c:      	adrp	x22, 0xfffffe0008526000 <_nss_build+0x4c>
fffffe000751ec80:      	add	x22, x22, #2648
fffffe000751ec84:      	ldr	x19, [x22]
fffffe000751ec88:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe000751ec8c:      	mov	w23, #3
fffffe000751ec90:      	cmp	x19, x0
fffffe000751ec94:      	b.ne	0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe000751ec98:      	ldur	d0, [x22, #20]
fffffe000751ec9c:      	ldr	x8, [sp, #520]
fffffe000751eca0:      	ld1.s	{ v0 }[1], [x8]
fffffe000751eca4:      	adrp	x8, 0xfffffe0008526000 <_ncc_ap_report+0xc>
fffffe000751eca8:      	add	x8, x8, #2724
fffffe000751ecac:      	ldr	d1, [x8]
fffffe000751ecb0:      	mov.d	v0[1], v1[0]
fffffe000751ecb4:      	ldr	q1, [sp, #528]
fffffe000751ecb8:      	cmeq.4s	v0, v0, v1
fffffe000751ecbc:      	mvn.16b	v0, v0
fffffe000751ecc0:      	xtn.4h	v0, v0
fffffe000751ecc4:      	umov.h	w8, v0[1]
fffffe000751ecc8:      	umov.h	w9, v0[2]
fffffe000751eccc:      	umov.h	w10, v0[0]
fffffe000751ecd0:      	umov.h	w11, v0[3]
fffffe000751ecd4:      	and	w8, w8, #0x1
fffffe000751ecd8:      	and	w9, w9, #0x1
fffffe000751ecdc:      	bfi	w10, w8, #1, #1
fffffe000751ece0:      	bfi	w10, w9, #2, #1
fffffe000751ece4:      	bfi	w10, w11, #3, #29
fffffe000751ece8:      	and	w8, w10, #0xf
fffffe000751ecec:      	cbnz	w8, 0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe000751ecf0:      	ldr	w8, [x22, #12]
fffffe000751ecf4:      	cbz	w8, 0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe000751ecf8:      	adrp	x9, 0xfffffe000852a000 <_ncc_ap_report+0x70>
fffffe000751ecfc:      	add	x9, x9, #3704
fffffe000751ed00:      	ldr	w8, [x9]
fffffe000751ed04:      	cbnz	w8, 0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe000751ed08:      	ldr	w8, [x9, #12]
fffffe000751ed0c:      	cbnz	w8, 0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe000751ed10:      	adrp	x14, 0xfffffe0008525000 <_ncc_ap_report+0x74>
fffffe000751ed14:      	add	x14, x14, #1592
fffffe000751ed18:      	adrp	x13, 0xfffffe00084dd000 <_njs_peer+0x50>
fffffe000751ed1c:      	add	x13, x13, #2480
fffffe000751ed20:      	mov	w12, #60
fffffe000751ed24:      	ldr	w9, [x24, #60]
fffffe000751ed28:      	ldr	w8, [x14, #44]
fffffe000751ed2c:      	add	x11, x13, #20
fffffe000751ed30:      	add	x3, x13, #4
fffffe000751ed34:      	add	x10, x24, #64
fffffe000751ed38:      	cmp	w9, #0
fffffe000751ed3c:      	mov	x26, x0
fffffe000751ed40:      	sub	w8, w8, #1
fffffe000751ed44:      	csel	x4, x11, x10, eq
fffffe000751ed48:      	ldr	x1, [x14, #4048]
fffffe000751ed4c:      	add	x0, x14, #201
fffffe000751ed50:      	umaddl	x8, w8, w12, x14
fffffe000751ed54:      	ldr	w12, [x13, #16]
fffffe000751ed58:      	ldrb	w13, [x13]
fffffe000751ed5c:      	mov	w6, #1
fffffe000751ed60:      	mov	w7, #2
fffffe000751ed64:      	mov	w23, #1
fffffe000751ed68:      	csel	w5, w12, w9, eq
fffffe000751ed6c:      	ldr	x9, [sp, #504]
fffffe000751ed70:      	ldrb	w2, [x8, #4071]
fffffe000751ed74:      	add	w22, w13, #1
fffffe000751ed78:      	ldrh	w8, [x24, #142]
fffffe000751ed7c:      	strh	wzr, [sp, #10]
fffffe000751ed80:      	str	x9, [sp, #16]
fffffe000751ed84:      	str	w23, [sp, #4]
fffffe000751ed88:      	strh	w8, [sp, #8]
fffffe000751ed8c:      	strb	w22, [sp]
fffffe000751ed90:      	bl	0xfffffe00079a1390 <_ncs_build>
fffffe000751ed94:      	cbnz	w0, 0xfffffe000751e824 <_nss_owner_loop+0x370>
fffffe000751ed98:      	adrp	x28, 0xfffffe0008526000 <_ncc_cold_read+0x70>
fffffe000751ed9c:      	add	x28, x28, #2680
fffffe000751eda0:      	mov	x0, x20
fffffe000751eda4:      	mov	x1, x28
fffffe000751eda8:      	mov	w2, #8736
fffffe000751edac:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751edb0:      	mov	w8, #8736
fffffe000751edb4:      	adrp	x19, 0xfffffe000852a000 <_ncc_cold_read+0x9c>
fffffe000751edb8:      	add	x19, x19, #3680
fffffe000751edbc:      	add	x0, x20, x8
fffffe000751edc0:      	mov	x1, x19
fffffe000751edc4:      	mov	w2, #24528
fffffe000751edc8:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751edcc:      	mov	x0, x28
fffffe000751edd0:      	mov	w1, wzr
fffffe000751edd4:      	mov	w2, #8736
fffffe000751edd8:      	mov	w3, #8744
fffffe000751eddc:      	str	x20, [x20, #8744]
fffffe000751ede0:      	bl	0xfffffe000746b32c <___memset_chk>
fffffe000751ede4:      	mov	x0, x19
fffffe000751ede8:      	mov	w1, #24528
fffffe000751edec:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe000751edf0:      	adrp	x9, 0xfffffe0008528000 <_ncc_cold_read+0xd0>
fffffe000751edf4:      	add	x9, x9, #3700
fffffe000751edf8:      	ldr	q0, [sp, #384]
fffffe000751edfc:      	add	x2, sp, #608
fffffe000751ee00:      	adrp	x0, 0xfffffe0008524000 <_ncc_cold_read+0xd0>
fffffe000751ee04:      	add	x0, x0, #1216
fffffe000751ee08:      	ldp	w8, w9, [x9]
fffffe000751ee0c:      	str	x23, [sp, #616]
fffffe000751ee10:      	str	q0, [sp, #640]
fffffe000751ee14:      	cmp	w8, #0
fffffe000751ee18:      	adrp	x8, 0xfffffe000700e000 <_nbc_report+0x1bc>
fffffe000751ee1c:      	add	x8, x8, #276
fffffe000751ee20:      	ccmp	w9, #0, #0, ne
fffffe000751ee24:      	str	x8, [sp, #608]
fffffe000751ee28:      	adr	x8, #15912
fffffe000751ee2c:      	nop
fffffe000751ee30:      	cset	w19, eq
fffffe000751ee34:      	str	x8, [sp, #624]
fffffe000751ee38:      	ldr	x8, [sp, #376]
fffffe000751ee3c:      	str	x8, [sp, #632]
fffffe000751ee40:      	b.ne	0xfffffe000751f87c <_nss_owner_loop+0x13c8>
fffffe000751ee44:      	adrp	x1, 0xfffffe0008528000 <_ncc_cold_read+0x124>
fffffe000751ee48:      	add	x1, x1, #3232
fffffe000751ee4c:      	mov	x3, x28
fffffe000751ee50:      	bl	0xfffffe000799cbb4 <_nct_scan_return_prepare>
fffffe000751ee54:      	cbnz	w0, 0xfffffe000751f8c4 <_nss_owner_loop+0x1410>
fffffe000751ee58:      	mov	w5, #40832
fffffe000751ee5c:      	and	w4, w22, #0xff
fffffe000751ee60:      	adrp	x0, 0xfffffe000852a000 <_ncc_native_check+0x18>
fffffe000751ee64:      	add	x0, x0, #3680
fffffe000751ee68:      	adrp	x1, 0xfffffe0007c5f000 <_nss_owner_loop+0x26b8>
fffffe000751ee6c:      	add	x1, x1, #1624
fffffe000751ee70:      	mov	x2, x28
fffffe000751ee74:      	adrp	x3, 0xfffffe0008528000 <_ncc_native_check+0x24>
fffffe000751ee78:      	add	x3, x3, #3296
fffffe000751ee7c:      	movk	w5, #213, lsl #16
fffffe000751ee80:      	bl	0xfffffe000799edc0 <_ncr_scan_return_prepare>
fffffe000751ee84:      	b	0xfffffe000751f8b8 <_nss_owner_loop+0x1404>
fffffe000751ee88:      	adrp	x9, 0xfffffe00084f8000 <_ncc_cold_read+0xa8>
fffffe000751ee8c:      	add	x9, x9, #552
fffffe000751ee90:      	ldp	w8, w9, [x9]
fffffe000751ee94:      	orr	w8, w8, w9
fffffe000751ee98:      	cbnz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ee9c:      	adrp	x8, 0xfffffe0008531000 <_ncc_native_check+0x70>
fffffe000751eea0:      	add	x8, x8, #152
fffffe000751eea4:      	mov	w9, #6
fffffe000751eea8:      	stlr	w9, [x8]
fffffe000751eeac:      	bl	0xfffffe00079b4a8c <_nwl_enabled>
fffffe000751eeb0:      	cbz	w0, 0xfffffe00075209ac <_nss_owner_loop+0x24f8>
fffffe000751eeb4:      	mov	w1, #16960
fffffe000751eeb8:      	add	x2, sp, #608
fffffe000751eebc:      	mov	w0, #100
fffffe000751eec0:      	movk	w1, #15, lsl #16
fffffe000751eec4:      	str	xzr, [sp, #608]
fffffe000751eec8:      	bl	0xfffffe0007337480 <_clock_interval_to_deadline>
fffffe000751eecc:      	ldr	x2, [sp, #608]
fffffe000751eed0:      	adrp	x0, 0xfffffe00084dd000 <_ncc_cold_read+0x84>
fffffe000751eed4:      	add	x0, x0, #2456
fffffe000751eed8:      	mov	w1, wzr
fffffe000751eedc:      	bl	0xfffffe000736ca44 <_assert_wait_deadline>
fffffe000751eee0:      	b	0xfffffe00075209bc <_nss_owner_loop+0x2508>
fffffe000751eee4:      	lsr	x8, x22, #16
fffffe000751eee8:      	mov	w24, #2
fffffe000751eeec:      	cbnz	x8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe000751eef0:      	ldur	w8, [x29, #-160]
fffffe000751eef4:      	adrp	x9, 0xfffffe00084f8000 <_ncc_cold_read+0x114>
fffffe000751eef8:      	add	x9, x9, #556
fffffe000751eefc:      	cmp	x22, x8
fffffe000751ef00:      	b.ne	0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe000751ef04:      	adrp	x8, 0xfffffe00084dd000 <_ncc_cold_read+0xb8>
fffffe000751ef08:      	mov	w24, #3
fffffe000751ef0c:      	ldr	w8, [x8, #2468]
fffffe000751ef10:      	cbnz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe000751ef14:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_check+0x7c>
fffffe000751ef18:      	ldr	w8, [x8, #2116]
fffffe000751ef1c:      	cbnz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe000751ef20:      	adrp	x8, 0xfffffe00084f8000 <_ncc_native_check+0x10>
fffffe000751ef24:      	ldr	w8, [x8, #584]
fffffe000751ef28:      	cbnz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe000751ef2c:      	sub	w8, w19, #2
fffffe000751ef30:      	cmp	w8, #2
fffffe000751ef34:      	b.hi	0xfffffe00075209d0 <_nss_owner_loop+0x251c>
fffffe000751ef38:      	ldr	w2, [x26, #160]
fffffe000751ef3c:      	cmp	w19, #4
fffffe000751ef40:      	b.eq	0xfffffe0007520b44 <_nss_owner_loop+0x2690>
fffffe000751ef44:      	cmp	w2, #32
fffffe000751ef48:      	b.ne	0xfffffe0007520b44 <_nss_owner_loop+0x2690>
fffffe000751ef4c:      	mov	w24, #4
fffffe000751ef50:      	b	0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe000751ef54:      	add	x0, x19, #104
fffffe000751ef58:      	mov	x1, x23
fffffe000751ef5c:      	mov	w2, #1392
fffffe000751ef60:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751ef64:      	mov	x0, x19
fffffe000751ef68:      	ldr	x1, [sp, #360]
fffffe000751ef6c:      	mov	x2, x20
fffffe000751ef70:      	mov	x3, x26
fffffe000751ef74:      	mov	x4, x23
fffffe000751ef78:      	bl	0xfffffe00079a964c <_niwj_select>
fffffe000751ef7c:      	cbnz	w0, 0xfffffe000751efac <_nss_owner_loop+0xaf8>
fffffe000751ef80:      	adrp	x9, 0xfffffe00084f8000 <_ncc_native_check+0x70>
fffffe000751ef84:      	add	x9, x9, #2396
fffffe000751ef88:      	ldrsh	w8, [x9, #1392]
fffffe000751ef8c:      	ldrsh	w9, [x9]
fffffe000751ef90:      	cmp	w8, w9
fffffe000751ef94:      	b.le	0xfffffe000751efac <_nss_owner_loop+0xaf8>
fffffe000751ef98:      	adrp	x0, 0xfffffe00084f8000 <_ncc_native_check+0x88>
fffffe000751ef9c:      	add	x0, x0, #1008
fffffe000751efa0:      	add	x1, x0, #1392
fffffe000751efa4:      	mov	w2, #1392
fffffe000751efa8:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751efac:      	add	x4, x19, #1496
fffffe000751efb0:      	mov	x0, x19
fffffe000751efb4:      	ldr	x1, [sp, #400]
fffffe000751efb8:      	mov	x2, x20
fffffe000751efbc:      	mov	x3, x26
fffffe000751efc0:      	bl	0xfffffe00079a964c <_niwj_select>
fffffe000751efc4:      	cbnz	w0, 0xfffffe000751eff4 <_nss_owner_loop+0xb40>
fffffe000751efc8:      	adrp	x9, 0xfffffe00084f8000 <_ncc_native_check+0xb8>
fffffe000751efcc:      	add	x9, x9, #2396
fffffe000751efd0:      	ldrsh	w8, [x9, #1392]
fffffe000751efd4:      	ldrsh	w9, [x9]
fffffe000751efd8:      	cmp	w8, w9
fffffe000751efdc:      	b.le	0xfffffe000751eff4 <_nss_owner_loop+0xb40>
fffffe000751efe0:      	adrp	x0, 0xfffffe00084f8000 <_ncc_native_check+0xd0>
fffffe000751efe4:      	add	x0, x0, #1008
fffffe000751efe8:      	add	x1, x0, #1392
fffffe000751efec:      	mov	w2, #1392
fffffe000751eff0:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751eff4:      	add	x4, x19, #1496
fffffe000751eff8:      	mov	x0, x19
fffffe000751effc:      	ldr	x1, [sp, #416]
fffffe000751f000:      	mov	x2, x20
fffffe000751f004:      	mov	x3, x26
fffffe000751f008:      	bl	0xfffffe00079a964c <_niwj_select>
fffffe000751f00c:      	cbnz	w0, 0xfffffe000751f03c <_nss_owner_loop+0xb88>
fffffe000751f010:      	adrp	x9, 0xfffffe00084f8000 <_ncc_native_check+0xfc>
fffffe000751f014:      	add	x9, x9, #2396
fffffe000751f018:      	ldrsh	w8, [x9, #1392]
fffffe000751f01c:      	ldrsh	w9, [x9]
fffffe000751f020:      	cmp	w8, w9
fffffe000751f024:      	b.le	0xfffffe000751f03c <_nss_owner_loop+0xb88>
fffffe000751f028:      	adrp	x0, 0xfffffe00084f8000 <_ncc_native_check+0x114>
fffffe000751f02c:      	add	x0, x0, #1008
fffffe000751f030:      	add	x1, x0, #1392
fffffe000751f034:      	mov	w2, #1392
fffffe000751f038:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751f03c:      	adrp	x9, 0xfffffe00084f8000 <_ncc_native_check+0x128>
fffffe000751f040:      	add	x9, x9, #720
fffffe000751f044:      	ldr	x24, [x9, #32]
fffffe000751f048:      	sub	x8, x24, #256
fffffe000751f04c:      	cmn	x8, #255
fffffe000751f050:      	b.hs	0xfffffe000751f544 <_nss_owner_loop+0x1090>
fffffe000751f054:      	mov	w23, #2
fffffe000751f058:      	ldr	x0, [x27, #2456]
fffffe000751f05c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f060:      	mov	w0, w23
fffffe000751f064:      	bl	0xfffffe0007522178 <_njs_early_finish>
fffffe000751f068:      	ldr	x0, [x27, #2456]
fffffe000751f06c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f070:      	adrp	x8, 0xfffffe00084f8000 <_ncc_native_check+0x15c>
fffffe000751f074:      	ldr	x0, [x27, #2456]
fffffe000751f078:      	ldr	w19, [x8, #556]
fffffe000751f07c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f080:      	cbz	w19, 0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe000751f084:      	adrp	x10, 0xfffffe0008526000 <_ncc_native_read+0x8c>
fffffe000751f088:      	add	x10, x10, #2648
fffffe000751f08c:      	add	x9, sp, #608
fffffe000751f090:      	str	xzr, [sp, #608]
fffffe000751f094:      	ldr	w8, [x10, #12]
fffffe000751f098:      	str	xzr, [x9, #296]
fffffe000751f09c:      	cbz	w8, 0xfffffe0007521adc <_nss_owner_loop+0x3628>
fffffe000751f0a0:      	ldr	w8, [x10, #20]
fffffe000751f0a4:      	cbnz	w8, 0xfffffe0007521adc <_nss_owner_loop+0x3628>
fffffe000751f0a8:      	ldr	x8, [x10]
fffffe000751f0ac:      	ldr	x9, [sp, #592]
fffffe000751f0b0:      	cmp	x8, x9
fffffe000751f0b4:      	b.ne	0xfffffe0007521adc <_nss_owner_loop+0x3628>
fffffe000751f0b8:      	add	x0, sp, #608
fffffe000751f0bc:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751f0c0:      	ldr	x0, [sp, #608]
fffffe000751f0c4:      	sub	x1, x29, #168
fffffe000751f0c8:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751f0cc:      	add	x8, sp, #608
fffffe000751f0d0:      	mov	x9, #63439
fffffe000751f0d4:      	movk	x9, #58195, lsl #16
fffffe000751f0d8:      	movk	x9, #39845, lsl #32
fffffe000751f0dc:      	ldr	x8, [x8, #296]
fffffe000751f0e0:      	movk	x9, #8388, lsl #48
fffffe000751f0e4:      	lsr	x8, x8, #3
fffffe000751f0e8:      	umulh	x8, x8, x9
fffffe000751f0ec:      	lsr	x26, x8, #4
fffffe000751f0f0:      	ldr	x0, [x27, #2456]
fffffe000751f0f4:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f0f8:      	adrp	x8, 0xfffffe00084f8000 <_ncc_native_read+0x48>
fffffe000751f0fc:      	ldr	w20, [x8, #568]
fffffe000751f100:      	cbz	w20, 0xfffffe000751f108 <_nss_owner_loop+0xc54>
fffffe000751f104:      	str	wzr, [x8, #568]
fffffe000751f108:      	ldp	w8, w9, [x25]
fffffe000751f10c:      	cmp	w20, #0
fffffe000751f110:      	cset	w22, ne
fffffe000751f114:      	orr	w9, w8, w9
fffffe000751f118:      	lsr	x8, x26, #3
fffffe000751f11c:      	cbz	w9, 0xfffffe000751f180 <_nss_owner_loop+0xccc>
fffffe000751f120:      	mov	x9, #63439
fffffe000751f124:      	adrp	x19, 0xfffffe00084f8000 <_ncc_native_read+0x74>
fffffe000751f128:      	add	x19, x19, #604
fffffe000751f12c:      	movk	x9, #58195, lsl #16
fffffe000751f130:      	movk	x9, #39845, lsl #32
fffffe000751f134:      	ldr	x0, [sp, #496]
fffffe000751f138:      	movk	x9, #8388, lsl #48
fffffe000751f13c:      	ldur	x1, [x19, #148]
fffffe000751f140:      	umulh	x8, x8, x9
fffffe000751f144:      	lsr	x24, x8, #4
fffffe000751f148:      	mov	x2, x24
fffffe000751f14c:      	bl	0xfffffe00079bcacc <_nwn_cancel>
fffffe000751f150:      	ldr	w8, [x19, #4]
fffffe000751f154:      	mov	w1, #12112
fffffe000751f158:      	ldr	w9, [x19, #72]
fffffe000751f15c:      	ldr	x0, [sp, #488]
fffffe000751f160:      	add	w8, w9, w8
fffffe000751f164:      	str	w8, [x19, #72]
fffffe000751f168:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000751f16c:      	movi.2d	v0, #0000000000000000
fffffe000751f170:      	ldr	x8, [sp, #480]
fffffe000751f174:      	str	xzr, [x19]
fffffe000751f178:      	stp	q0, q0, [x8]
fffffe000751f17c:      	b	0xfffffe000751f198 <_nss_owner_loop+0xce4>
fffffe000751f180:      	mov	x9, #63439
fffffe000751f184:      	movk	x9, #58195, lsl #16
fffffe000751f188:      	movk	x9, #39845, lsl #32
fffffe000751f18c:      	movk	x9, #8388, lsl #48
fffffe000751f190:      	umulh	x8, x8, x9
fffffe000751f194:      	lsr	x24, x8, #4
fffffe000751f198:      	adrp	x0, 0xfffffe000850b000 <_ncc_native_write>
fffffe000751f19c:      	add	x0, x0, #784
fffffe000751f1a0:      	mov	x1, x24
fffffe000751f1a4:      	mov	w26, #58720
fffffe000751f1a8:      	movk	w26, #1, lsl #16
fffffe000751f1ac:      	bl	0xfffffe00079bc90c <_nwn_pump>
fffffe000751f1b0:      	tbnz	w0, #31, 0xfffffe0007521ad4 <_nss_owner_loop+0x3620>
fffffe000751f1b4:      	ldp	w8, w9, [x25]
fffffe000751f1b8:      	str	w22, [sp, #560]
fffffe000751f1bc:      	str	w20, [sp, #584]
fffffe000751f1c0:      	orr	w8, w8, w9
fffffe000751f1c4:      	cbnz	w8, 0xfffffe000751f28c <_nss_owner_loop+0xdd8>
fffffe000751f1c8:      	ldr	x0, [sp, #472]
fffffe000751f1cc:      	bl	0xfffffe00079bbb50 <_nwo_authorized>
fffffe000751f1d0:      	cbz	w0, 0xfffffe000751f28c <_nss_owner_loop+0xdd8>
fffffe000751f1d4:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_write+0x68>
fffffe000751f1d8:      	nop
fffffe000751f1dc:      	ldr	w8, [x8, #2128]
fffffe000751f1e0:      	cbnz	w8, 0xfffffe000751f28c <_nss_owner_loop+0xdd8>
fffffe000751f1e4:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_write+0x78>
fffffe000751f1e8:      	add	x8, x8, #2128
fffffe000751f1ec:      	ldr	w8, [x8, #4]
fffffe000751f1f0:      	cbnz	w8, 0xfffffe000751f28c <_nss_owner_loop+0xdd8>
fffffe000751f1f4:      	adrp	x8, 0xfffffe00084f8000 <_ncc_native_write+0x10>
fffffe000751f1f8:      	nop
fffffe000751f1fc:      	ldr	w8, [x8, #608]
fffffe000751f200:      	cbz	w8, 0xfffffe000751f28c <_nss_owner_loop+0xdd8>
fffffe000751f204:      	ldr	w19, [x21, #60]
fffffe000751f208:      	mov	w9, #34464
fffffe000751f20c:      	movk	w9, #1, lsl #16
fffffe000751f210:      	adrp	x0, 0xfffffe0008516000 <_ncc_native_write+0xa4>
fffffe000751f214:      	add	x0, x0, #2072
fffffe000751f218:      	mov	x1, #-1
fffffe000751f21c:      	add	x8, x21, x19, lsl #2
fffffe000751f220:      	add	x20, x8, x9
fffffe000751f224:      	mov	w8, #1514
fffffe000751f228:      	mov	w9, #22352
fffffe000751f22c:      	madd	x8, x19, x8, x21
fffffe000751f230:      	movk	w9, #1, lsl #16
fffffe000751f234:      	ldr	w3, [x20]
fffffe000751f238:      	add	x23, x8, x9
fffffe000751f23c:      	mov	x2, x23
fffffe000751f240:      	bl	0xfffffe00079adfdc <_nst_data>
fffffe000751f244:      	cmp	w0, #0
fffffe000751f248:      	b.gt	0xfffffe000751f28c <_nss_owner_loop+0xdd8>
fffffe000751f24c:      	tbz	w0, #31, 0xfffffe000751f260 <_nss_owner_loop+0xdac>
fffffe000751f250:      	adrp	x9, 0xfffffe00084f8000 <_ncc_native_write+0x6c>
fffffe000751f254:      	ldr	w8, [x9, #676]
fffffe000751f258:      	add	w8, w8, #1
fffffe000751f25c:      	str	w8, [x9, #676]
fffffe000751f260:      	mov	x0, x23
fffffe000751f264:      	mov	w1, #1514
fffffe000751f268:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000751f26c:      	adrp	x10, 0xfffffe00084f8000 <_ncc_native_write+0x88>
fffffe000751f270:      	add	x10, x10, #604
fffffe000751f274:      	add	w9, w19, #1
fffffe000751f278:      	str	wzr, [x20]
fffffe000751f27c:      	and	w9, w9, #0x7
fffffe000751f280:      	ldr	w8, [x10, #4]
fffffe000751f284:      	sub	w8, w8, #1
fffffe000751f288:      	stp	w9, w8, [x10]
fffffe000751f28c:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_write+0x120>
fffffe000751f290:      	nop
fffffe000751f294:      	ldr	w8, [x8, #2128]
fffffe000751f298:      	cbz	w8, 0xfffffe000751f340 <_nss_owner_loop+0xe8c>
fffffe000751f29c:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_write+0x130>
fffffe000751f2a0:      	add	x8, x8, #2128
fffffe000751f2a4:      	ldr	w8, [x8, #76]
fffffe000751f2a8:      	cbnz	w8, 0xfffffe000751f340 <_nss_owner_loop+0xe8c>
fffffe000751f2ac:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_write+0x140>
fffffe000751f2b0:      	add	x8, x8, #2128
fffffe000751f2b4:      	ldr	w8, [x8, #60]
fffffe000751f2b8:      	cmp	w8, #7
fffffe000751f2bc:      	b.ne	0xfffffe000751f340 <_nss_owner_loop+0xe8c>
fffffe000751f2c0:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_write+0x154>
fffffe000751f2c4:      	ldr	w8, [x8, #2052]
fffffe000751f2c8:      	cbz	w8, 0xfffffe000751f2e8 <_nss_owner_loop+0xe34>
fffffe000751f2cc:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_write+0x160>
fffffe000751f2d0:      	adrp	x9, 0xfffffe0008516000 <_ncc_native_write+0x164>
fffffe000751f2d4:      	ldr	x8, [x8, #2008]
fffffe000751f2d8:      	ldr	x9, [x9, #2240]
fffffe000751f2dc:      	cmp	x8, x9
fffffe000751f2e0:      	b.ne	0xfffffe000751f334 <_nss_owner_loop+0xe80>
fffffe000751f2e4:      	b	0xfffffe000751f340 <_nss_owner_loop+0xe8c>
fffffe000751f2e8:      	adrp	x13, 0xfffffe0008516000 <_ncc_native_barrier>
fffffe000751f2ec:      	add	x13, x13, #2240
fffffe000751f2f0:      	mov	w8, #1
fffffe000751f2f4:      	adrp	x12, 0xfffffe0008516000 <_ncc_native_barrier+0xc>
fffffe000751f2f8:      	add	x12, x12, #2008
fffffe000751f2fc:      	ldrb	w11, [x13, #1665]
fffffe000751f300:      	ldr	x10, [x13]
fffffe000751f304:      	str	w8, [x12, #44]
fffffe000751f308:      	ldrb	w9, [x13, #1667]
fffffe000751f30c:      	ldrb	w8, [x13, #1677]
fffffe000751f310:      	cmp	w11, #1
fffffe000751f314:      	str	x10, [x12]
fffffe000751f318:      	str	w9, [x12, #52]
fffffe000751f31c:      	stp	w8, w11, [x12, #56]
fffffe000751f320:      	b.hi	0xfffffe000751f334 <_nss_owner_loop+0xe80>
fffffe000751f324:      	cmp	w9, #1
fffffe000751f328:      	b.hi	0xfffffe000751f334 <_nss_owner_loop+0xe80>
fffffe000751f32c:      	cmp	w8, #4
fffffe000751f330:      	b.lo	0xfffffe000751f340 <_nss_owner_loop+0xe8c>
fffffe000751f334:      	mov	w8, #1
fffffe000751f338:      	adrp	x9, 0xfffffe0008516000 <_nis_scope_or_common+0x28>
fffffe000751f33c:      	str	w8, [x9, #2056]
fffffe000751f340:      	adrp	x8, 0xfffffe0008516000 <_nis_scope_or_common+0x30>
fffffe000751f344:      	ldr	w8, [x8, #2116]
fffffe000751f348:      	cbnz	w8, 0xfffffe0007521ad4 <_nss_owner_loop+0x3620>
fffffe000751f34c:      	adrp	x0, 0xfffffe0008516000 <_nis_scope_or_common+0x3c>
fffffe000751f350:      	add	x0, x0, #2072
fffffe000751f354:      	adrp	x1, 0xfffffe000721b000 <_nss_owner_loop+0x290>
fffffe000751f358:      	add	x1, x1, #1232
fffffe000751f35c:      	adrp	x2, 0xfffffe0008525000 <_nis_scope_or_common+0x88>
fffffe000751f360:      	add	x2, x2, #1592
fffffe000751f364:      	mov	w3, #16
fffffe000751f368:      	adr	x4, #12348
fffffe000751f36c:      	nop
fffffe000751f370:      	mov	x5, x21
fffffe000751f374:      	bl	0xfffffe00079ae7e8 <_nst_step>
fffffe000751f378:      	tbnz	w0, #31, 0xfffffe0007521ad4 <_nss_owner_loop+0x3620>
fffffe000751f37c:      	bl	0xfffffe0007522744 <_njs_snapshot_locked>
fffffe000751f380:      	mov	w0, wzr
fffffe000751f384:      	bl	0xfffffe0007512bf0 <_njs_trace_locked>
fffffe000751f388:      	ldr	x0, [sp, #552]
fffffe000751f38c:      	bl	0xfffffe00079bbb50 <_nwo_authorized>
fffffe000751f390:      	adrp	x8, 0xfffffe0008516000 <_nis_scope_or_common+0x80>
fffffe000751f394:      	cmp	w0, #0
fffffe000751f398:      	ldr	w10, [x25, #36]
fffffe000751f39c:      	ldr	w8, [x8, #3600]
fffffe000751f3a0:      	ccmp	w8, #0, #4, ne
fffffe000751f3a4:      	ldp	w8, w9, [x25]
fffffe000751f3a8:      	cset	w19, eq
fffffe000751f3ac:      	cmp	w8, #0
fffffe000751f3b0:      	ldr	w8, [x25, #3276]
fffffe000751f3b4:      	cset	w22, ne
fffffe000751f3b8:      	cmp	w9, #0
fffffe000751f3bc:      	cset	w20, ne
fffffe000751f3c0:      	cmp	w10, #0
fffffe000751f3c4:      	cset	w28, ne
fffffe000751f3c8:      	cmp	w8, #0
fffffe000751f3cc:      	cset	w8, eq
fffffe000751f3d0:      	orr	w8, w8, w22
fffffe000751f3d4:      	orr	w8, w8, w20
fffffe000751f3d8:      	and	w23, w8, w28
fffffe000751f3dc:      	cmp	w23, #1
fffffe000751f3e0:      	b.ne	0xfffffe000751f428 <_nss_owner_loop+0xf74>
fffffe000751f3e4:      	adrp	x26, 0xfffffe00084f8000 <_nis_scope_or_common+0x5c>
fffffe000751f3e8:      	add	x26, x26, #596
fffffe000751f3ec:      	movi.2d	v0, #0000000000000000
fffffe000751f3f0:      	ldr	x0, [sp, #440]
fffffe000751f3f4:      	mov	w1, #12112
fffffe000751f3f8:      	ldr	w8, [x26, #12]
fffffe000751f3fc:      	ldr	w9, [x26, #80]
fffffe000751f400:      	str	d0, [x26]
fffffe000751f404:      	add	w8, w9, w8
fffffe000751f408:      	str	w8, [x26, #80]
fffffe000751f40c:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000751f410:      	movi.2d	v0, #0000000000000000
fffffe000751f414:      	ldr	x8, [sp, #432]
fffffe000751f418:      	str	d0, [x26, #8]
fffffe000751f41c:      	mov	w26, #58720
fffffe000751f420:      	movk	w26, #1, lsl #16
fffffe000751f424:      	stp	q0, q0, [x8]
fffffe000751f428:      	adrp	x8, 0xfffffe000850b000 <_nis_scope_or_common+0xec>
fffffe000751f42c:      	ldr	w8, [x8, #788]
fffffe000751f430:      	sub	w10, w8, #21
fffffe000751f434:      	cmp	w10, #1
fffffe000751f438:      	str	w10, [sp, #580]
fffffe000751f43c:      	b.hi	0xfffffe000751f454 <_nss_owner_loop+0xfa0>
fffffe000751f440:      	adrp	x9, 0xfffffe00084f8000 <_nis_scope_or_common+0xb8>
fffffe000751f444:      	ldr	w8, [x9, #580]
fffffe000751f448:      	cbz	w8, 0xfffffe000751f45c <_nss_owner_loop+0xfa8>
fffffe000751f44c:      	str	wzr, [sp, #568]
fffffe000751f450:      	b	0xfffffe000751f46c <_nss_owner_loop+0xfb8>
fffffe000751f454:      	str	wzr, [sp, #568]
fffffe000751f458:      	b	0xfffffe000751f4c0 <_nss_owner_loop+0x100c>
fffffe000751f45c:      	mov	w10, #1
fffffe000751f460:      	mov	w8, #1
fffffe000751f464:      	str	w8, [sp, #568]
fffffe000751f468:      	str	w10, [x9, #580]
fffffe000751f46c:      	adrp	x26, 0xfffffe00084f8000 <_nis_scope_or_common+0xe4>
fffffe000751f470:      	add	x26, x26, #556
fffffe000751f474:      	mov	w9, #1
fffffe000751f478:      	ldr	x0, [sp, #464]
fffffe000751f47c:      	mov	w1, #2616
fffffe000751f480:      	str	wzr, [x26]
fffffe000751f484:      	str	w9, [x26, #28]
fffffe000751f488:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000751f48c:      	ldr	w8, [x26, #52]
fffffe000751f490:      	mov	w1, #12112
fffffe000751f494:      	ldr	w9, [x26, #120]
fffffe000751f498:      	ldr	x0, [sp, #456]
fffffe000751f49c:      	add	w8, w9, w8
fffffe000751f4a0:      	str	w8, [x26, #120]
fffffe000751f4a4:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000751f4a8:      	movi.2d	v0, #0000000000000000
fffffe000751f4ac:      	str	xzr, [x26, #48]
fffffe000751f4b0:      	mov	w26, #58720
fffffe000751f4b4:      	ldr	x8, [sp, #448]
fffffe000751f4b8:      	movk	w26, #1, lsl #16
fffffe000751f4bc:      	stp	q0, q0, [x8]
fffffe000751f4c0:      	ldr	x0, [x27, #2456]
fffffe000751f4c4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f4c8:      	cbz	w23, 0xfffffe000751f750 <_nss_owner_loop+0x129c>
fffffe000751f4cc:      	ldr	x0, [x21]
fffffe000751f4d0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f4d4:      	ldr	x0, [x21, #208]
fffffe000751f4d8:      	mov	w1, wzr
fffffe000751f4dc:      	bl	0xfffffe0007af1854 <_nord2_wifi_ifnet_link>
fffffe000751f4e0:      	mov	w23, w0
fffffe000751f4e4:      	ldr	x0, [x21]
fffffe000751f4e8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f4ec:      	cbz	w23, 0xfffffe000751f750 <_nss_owner_loop+0x129c>
fffffe000751f4f0:      	ldr	x0, [x27, #2456]
fffffe000751f4f4:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f4f8:      	adrp	x8, 0xfffffe00084f8000 <_nis_scope_or_common+0x170>
fffffe000751f4fc:      	ldr	w8, [x8, #616]
fffffe000751f500:      	cmp	w8, #111
fffffe000751f504:      	b.hi	0xfffffe000751f748 <_nss_owner_loop+0x1294>
fffffe000751f508:      	add	w8, w8, #1
fffffe000751f50c:      	ldr	x0, [x27, #2456]
fffffe000751f510:      	adrp	x26, 0xfffffe00084f8000 <_nis_scope_or_common+0x188>
fffffe000751f514:      	add	x26, x26, #616
fffffe000751f518:      	str	w8, [x26]
fffffe000751f51c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f520:      	ldr	x8, [x26, #136]
fffffe000751f524:      	adrp	x0, 0xfffffe000709a000 <_nct_native_run+0x120>
fffffe000751f528:      	add	x0, x0, #1571
fffffe000751f52c:      	mov	w26, #58720
fffffe000751f530:      	movk	w26, #1, lsl #16
fffffe000751f534:      	stp	xzr, x23, [sp, #8]
fffffe000751f538:      	str	x8, [sp]
fffffe000751f53c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751f540:      	b	0xfffffe000751f750 <_nss_owner_loop+0x129c>
fffffe000751f544:      	adrp	x8, 0xfffffe0008524000 <_nis_native_check>
fffffe000751f548:      	ldr	x22, [x9]
fffffe000751f54c:      	ldr	w8, [x8, #424]
fffffe000751f550:      	cbz	w8, 0xfffffe000751f5bc <_nss_owner_loop+0x1108>
fffffe000751f554:      	adrp	x8, 0xfffffe0008516000 <_nis_inner+0x34>
fffffe000751f558:      	ldr	w8, [x8, #2040]
fffffe000751f55c:      	cbz	w8, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f560:      	adrp	x9, 0xfffffe0008516000 <_nis_inner+0x40>
fffffe000751f564:      	add	x9, x9, #2016
fffffe000751f568:      	mov	w23, #2
fffffe000751f56c:      	ldr	x8, [x9]
fffffe000751f570:      	cmp	x8, x24
fffffe000751f574:      	b.ne	0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f578:      	ldur	q0, [x9, #28]
fffffe000751f57c:      	cmtst.4s	v0, v0, v0
fffffe000751f580:      	xtn.4h	v0, v0
fffffe000751f584:      	umov.h	w8, v0[1]
fffffe000751f588:      	umov.h	w9, v0[2]
fffffe000751f58c:      	umov.h	w10, v0[0]
fffffe000751f590:      	umov.h	w11, v0[3]
fffffe000751f594:      	and	w8, w8, #0x1
fffffe000751f598:      	and	w9, w9, #0x1
fffffe000751f59c:      	bfi	w10, w8, #1, #1
fffffe000751f5a0:      	bfi	w10, w9, #2, #1
fffffe000751f5a4:      	bfi	w10, w11, #3, #29
fffffe000751f5a8:      	and	w8, w10, #0xf
fffffe000751f5ac:      	cbnz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f5b0:      	adrp	x8, 0xfffffe0008524000 <_nis_native_check+0x6c>
fffffe000751f5b4:      	ldr	w8, [x8, #428]
fffffe000751f5b8:      	cbnz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f5bc:      	adrp	x9, 0xfffffe0008526000 <_nis_native_check+0x80>
fffffe000751f5c0:      	add	x9, x9, #2648
fffffe000751f5c4:      	ldr	x10, [sp, #592]
fffffe000751f5c8:      	mov	w23, #2
fffffe000751f5cc:      	ldr	x8, [x9]
fffffe000751f5d0:      	cmp	x8, x10
fffffe000751f5d4:      	b.ne	0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f5d8:      	ldr	w8, [x9, #12]
fffffe000751f5dc:      	cbz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f5e0:      	ldr	w8, [x9, #16]
fffffe000751f5e4:      	cbnz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f5e8:      	ldr	w8, [x9, #20]
fffffe000751f5ec:      	cbnz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f5f0:      	adrp	x8, 0xfffffe0008528000 <_nis_native_check+0xbc>
fffffe000751f5f4:      	ldr	w8, [x8, #3228]
fffffe000751f5f8:      	cbnz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f5fc:      	adrp	x9, 0xfffffe00084dd000 <_nis_scope_or_common+0x208>
fffffe000751f600:      	add	x9, x9, #2464
fffffe000751f604:      	mov	w23, #2
fffffe000751f608:      	ldar	w8, [x9]
fffffe000751f60c:      	cmp	w8, #1
fffffe000751f610:      	b.ne	0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f614:      	ldr	w8, [x9, #4]
fffffe000751f618:      	cbnz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f61c:      	adrp	x8, 0xfffffe0008528000 <_nis_native_check+0xe8>
fffffe000751f620:      	nop
fffffe000751f624:      	ldr	w8, [x8, #3752]
fffffe000751f628:      	cbz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f62c:      	adrp	x8, 0xfffffe0008524000 <_nis_native_check+0xe8>
fffffe000751f630:      	ldr	w8, [x8, #1644]
fffffe000751f634:      	cbz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f638:      	adrp	x8, 0xfffffe0008528000 <_nis_native_check+0x104>
fffffe000751f63c:      	add	x8, x8, #3752
fffffe000751f640:      	ldr	w8, [x8, #8144]
fffffe000751f644:      	cbnz	w8, 0xfffffe000751f058 <_nss_owner_loop+0xba4>
fffffe000751f648:      	adrp	x0, 0xfffffe000852b000 <_nis_native_check+0x120>
fffffe000751f64c:      	add	x0, x0, #1440
fffffe000751f650:      	bl	0xfffffe00079acf40 <_nst_scan_admissible>
fffffe000751f654:      	cbz	w0, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f658:      	adrp	x0, 0xfffffe0008526000 <_nis_native_check+0x11c>
fffffe000751f65c:      	add	x0, x0, #2680
fffffe000751f660:      	bl	0xfffffe000799c814 <_nct_done_receipt>
fffffe000751f664:      	cbz	w0, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f668:      	adrp	x8, 0xfffffe00084dd000 <_nis_native_check+0x8>
fffffe000751f66c:      	ldr	w8, [x8, #2584]
fffffe000751f670:      	sub	w8, w8, #1
fffffe000751f674:      	cmp	w8, #2
fffffe000751f678:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f67c:      	adrp	x8, 0xfffffe00084e2000 <_nis_native_check+0x30>
fffffe000751f680:      	ldr	w8, [x8, #584]
fffffe000751f684:      	sub	w8, w8, #1
fffffe000751f688:      	cmp	w8, #2
fffffe000751f68c:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f690:      	adrp	x8, 0xfffffe00084e6000 <_nis_native_check+0x54>
fffffe000751f694:      	ldr	w8, [x8, #2680]
fffffe000751f698:      	sub	w8, w8, #1
fffffe000751f69c:      	cmp	w8, #2
fffffe000751f6a0:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f6a4:      	adrp	x8, 0xfffffe00084eb000 <_nis_native_check+0x7c>
fffffe000751f6a8:      	ldr	w8, [x8, #680]
fffffe000751f6ac:      	sub	w8, w8, #1
fffffe000751f6b0:      	cmp	w8, #2
fffffe000751f6b4:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f6b8:      	adrp	x8, 0xfffffe0008525000 <_nis_native_check+0x178>
fffffe000751f6bc:      	str	xzr, [sp, #616]
fffffe000751f6c0:      	str	xzr, [sp, #608]
fffffe000751f6c4:      	str	xzr, [sp, #624]
fffffe000751f6c8:      	ldr	w8, [x8, #1636]
fffffe000751f6cc:      	sub	w9, w8, #17
fffffe000751f6d0:      	cmn	w9, #16
fffffe000751f6d4:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f6d8:      	sub	w8, w8, #1
fffffe000751f6dc:      	mov	w10, #60
fffffe000751f6e0:      	adrp	x9, 0xfffffe0008525000 <_nis_read+0x20>
fffffe000751f6e4:      	add	x9, x9, #1592
fffffe000751f6e8:      	umaddl	x8, w8, w10, x9
fffffe000751f6ec:      	add	x0, x9, #201
fffffe000751f6f0:      	add	x3, sp, #608
fffffe000751f6f4:      	ldr	x1, [x9, #4048]
fffffe000751f6f8:      	ldrb	w2, [x8, #4071]
fffffe000751f6fc:      	bl	0xfffffe00079a1230 <_ncs_cap_mac>
fffffe000751f700:      	cbnz	w0, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f704:      	ldrb	w8, [sp, #613]
fffffe000751f708:      	cmp	w8, #4
fffffe000751f70c:      	b.ne	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f710:      	ldrb	w8, [sp, #609]
fffffe000751f714:      	cmp	w8, #7
fffffe000751f718:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000751f71c:      	adrp	x8, 0xfffffe00084dd000 <_nis_native_check+0xbc>
fffffe000751f720:      	ldr	w8, [x8, #2496]
fffffe000751f724:      	cbz	w8, 0xfffffe0007521030 <_nss_owner_loop+0x2b7c>
fffffe000751f728:      	adrp	x11, 0xfffffe00084f8000 <_nis_native_check+0x134>
fffffe000751f72c:      	add	x11, x11, #2120
fffffe000751f730:      	mov	w9, wzr
fffffe000751f734:      	adrp	x12, 0xfffffe00084dd000 <_nis_native_check+0xd4>
fffffe000751f738:      	add	x12, x12, #2501
fffffe000751f73c:      	ldrb	w10, [x11]
fffffe000751f740:      	ldrb	w11, [x11, #1]
fffffe000751f744:      	b	0xfffffe0007521004 <_nss_owner_loop+0x2b50>
fffffe000751f748:      	ldr	x0, [x27, #2456]
fffffe000751f74c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f750:      	orr	w8, w19, w22
fffffe000751f754:      	orr	w8, w8, w20
fffffe000751f758:      	orr	w8, w8, w28
fffffe000751f75c:      	tbz	w8, #0, 0xfffffe000751f768 <_nss_owner_loop+0x12b4>
fffffe000751f760:      	mov	w24, wzr
fffffe000751f764:      	b	0xfffffe000751f998 <_nss_owner_loop+0x14e4>
fffffe000751f768:      	ldr	x0, [x21]
fffffe000751f76c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f770:      	ldr	x0, [x27, #2456]
fffffe000751f774:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f778:      	ldp	w8, w9, [x21, #16]
fffffe000751f77c:      	orr	w8, w8, w9
fffffe000751f780:      	cbnz	w8, 0xfffffe000751f7c0 <_nss_owner_loop+0x130c>
fffffe000751f784:      	adrp	x0, 0xfffffe000850b000 <_nis_read+0x5c>
fffffe000751f788:      	add	x0, x0, #784
fffffe000751f78c:      	bl	0xfffffe00079bbb50 <_nwo_authorized>
fffffe000751f790:      	cbz	w0, 0xfffffe000751f7c0 <_nss_owner_loop+0x130c>
fffffe000751f794:      	mov	w19, #1
fffffe000751f798:      	ldr	x0, [x27, #2456]
fffffe000751f79c:      	adrp	x20, 0xfffffe00084f8000 <_nis_read+0x28>
fffffe000751f7a0:      	add	x20, x20, #600
fffffe000751f7a4:      	str	w19, [x20]
fffffe000751f7a8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f7ac:      	ldr	x0, [x20, #152]
fffffe000751f7b0:      	mov	w1, #1
fffffe000751f7b4:      	bl	0xfffffe0007af1854 <_nord2_wifi_ifnet_link>
fffffe000751f7b8:      	mov	w23, w0
fffffe000751f7bc:      	b	0xfffffe000751f7d0 <_nss_owner_loop+0x131c>
fffffe000751f7c0:      	ldr	x0, [x27, #2456]
fffffe000751f7c4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f7c8:      	mov	w19, wzr
fffffe000751f7cc:      	mov	w23, wzr
fffffe000751f7d0:      	ldr	x0, [x27, #2456]
fffffe000751f7d4:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f7d8:      	str	wzr, [x25, #40]
fffffe000751f7dc:      	cbz	w19, 0xfffffe000751f818 <_nss_owner_loop+0x1364>
fffffe000751f7e0:      	cbnz	w23, 0xfffffe000751f818 <_nss_owner_loop+0x1364>
fffffe000751f7e4:      	ldr	w8, [x25]
fffffe000751f7e8:      	cbnz	w8, 0xfffffe000751f818 <_nss_owner_loop+0x1364>
fffffe000751f7ec:      	ldr	w8, [x25, #4]
fffffe000751f7f0:      	cbnz	w8, 0xfffffe000751f818 <_nss_owner_loop+0x1364>
fffffe000751f7f4:      	mov	w8, #1
fffffe000751f7f8:      	adrp	x20, 0xfffffe00084f8000 <_nis_read+0x84>
fffffe000751f7fc:      	add	x20, x20, #576
fffffe000751f800:      	str	w8, [x20, #20]
fffffe000751f804:      	bl	0xfffffe0007522744 <_njs_snapshot_locked>
fffffe000751f808:      	ldr	w8, [x20]
fffffe000751f80c:      	cbz	w8, 0xfffffe000751f8fc <_nss_owner_loop+0x1448>
fffffe000751f810:      	mov	w24, wzr
fffffe000751f814:      	b	0xfffffe000751f908 <_nss_owner_loop+0x1454>
fffffe000751f818:      	adrp	x20, 0xfffffe00084f8000 <_nis_read+0xa4>
fffffe000751f81c:      	add	x20, x20, #596
fffffe000751f820:      	ldr	x0, [sp, #408]
fffffe000751f824:      	mov	x2, x24
fffffe000751f828:      	ldur	x1, [x20, #156]
fffffe000751f82c:      	str	wzr, [x20]
fffffe000751f830:      	bl	0xfffffe00079bcacc <_nwn_cancel>
fffffe000751f834:      	ldr	w8, [x20, #12]
fffffe000751f838:      	mov	w1, #12112
fffffe000751f83c:      	ldr	w9, [x20, #80]
fffffe000751f840:      	add	w8, w9, w8
fffffe000751f844:      	mov	w9, #22300
fffffe000751f848:      	movk	w9, #1, lsl #16
fffffe000751f84c:      	add	x0, x20, x9
fffffe000751f850:      	str	w8, [x20, #80]
fffffe000751f854:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000751f858:      	mov	w8, #34412
fffffe000751f85c:      	str	xzr, [x20, #8]
fffffe000751f860:      	movk	w8, #1, lsl #16
fffffe000751f864:      	movi.2d	v0, #0000000000000000
fffffe000751f868:      	add	x8, x20, x8
fffffe000751f86c:      	stp	q0, q0, [x8]
fffffe000751f870:      	bl	0xfffffe0007522744 <_njs_snapshot_locked>
fffffe000751f874:      	mov	w24, wzr
fffffe000751f878:      	b	0xfffffe000751f908 <_nss_owner_loop+0x1454>
fffffe000751f87c:      	mov	x1, x20
fffffe000751f880:      	mov	x3, x28
fffffe000751f884:      	bl	0xfffffe000799c99c <_nct_continue_prepare>
fffffe000751f888:      	cbnz	w0, 0xfffffe000751f8c4 <_nss_owner_loop+0x1410>
fffffe000751f88c:      	mov	w5, #40832
fffffe000751f890:      	and	w4, w22, #0xff
fffffe000751f894:      	adrp	x0, 0xfffffe000852a000 <_nis_write_mask+0x130>
fffffe000751f898:      	add	x0, x0, #3680
fffffe000751f89c:      	adrp	x1, 0xfffffe0007c5f000 <_nss_owner_loop+0x30e8>
fffffe000751f8a0:      	add	x1, x1, #1624
fffffe000751f8a4:      	mov	x2, x28
fffffe000751f8a8:      	adrp	x3, 0xfffffe00084f1000 <_nis_write_mask+0x60>
fffffe000751f8ac:      	add	x3, x3, #3320
fffffe000751f8b0:      	movk	w5, #213, lsl #16
fffffe000751f8b4:      	bl	0xfffffe000799eaec <_ncr_continue_prepare>
fffffe000751f8b8:      	adrp	x28, 0xfffffe0008526000 <_nis_write_mask+0x144>
fffffe000751f8bc:      	add	x28, x28, #2680
fffffe000751f8c0:      	cbz	w0, 0xfffffe0007520a28 <_nss_owner_loop+0x2574>
fffffe000751f8c4:      	mov	x0, x28
fffffe000751f8c8:      	mov	x1, x20
fffffe000751f8cc:      	mov	w2, #8736
fffffe000751f8d0:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751f8d4:      	mov	w8, #8736
fffffe000751f8d8:      	adrp	x19, 0xfffffe000852a000 <_nis_write_mask+0x174>
fffffe000751f8dc:      	add	x19, x19, #3680
fffffe000751f8e0:      	add	x1, x20, x8
fffffe000751f8e4:      	mov	x0, x19
fffffe000751f8e8:      	mov	w2, #24528
fffffe000751f8ec:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000751f8f0:      	mov	w23, #3
fffffe000751f8f4:      	str	x28, [x19, #8]
fffffe000751f8f8:      	b	0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe000751f8fc:      	mov	w24, #1
fffffe000751f900:      	adrp	x8, 0xfffffe00084f8000 <_nis_write_mask+0xd4>
fffffe000751f904:      	str	w24, [x8, #576]
fffffe000751f908:      	adrp	x8, 0xfffffe00084f8000 <_nis_write_mask+0xdc>
fffffe000751f90c:      	ldr	x0, [x27, #2456]
fffffe000751f910:      	ldr	w20, [x8, #596]
fffffe000751f914:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f918:      	cbz	w19, 0xfffffe000751f930 <_nss_owner_loop+0x147c>
fffffe000751f91c:      	cbnz	w20, 0xfffffe000751f930 <_nss_owner_loop+0x147c>
fffffe000751f920:      	adrp	x8, 0xfffffe00084f8000 <_nis_write_mask+0xf4>
fffffe000751f924:      	mov	w1, wzr
fffffe000751f928:      	ldr	x0, [x8, #752]
fffffe000751f92c:      	bl	0xfffffe0007af1854 <_nord2_wifi_ifnet_link>
fffffe000751f930:      	adrp	x8, 0xfffffe00084f8000 <_nis_write_mask+0x104>
fffffe000751f934:      	ldr	x0, [x8, #544]
fffffe000751f938:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f93c:      	cbz	w23, 0xfffffe000751f998 <_nss_owner_loop+0x14e4>
fffffe000751f940:      	ldr	x0, [x27, #2456]
fffffe000751f944:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f948:      	adrp	x8, 0xfffffe00084f8000 <_nis_write_mask+0x11c>
fffffe000751f94c:      	ldr	w8, [x8, #616]
fffffe000751f950:      	cmp	w8, #111
fffffe000751f954:      	b.hi	0xfffffe000751f990 <_nss_owner_loop+0x14dc>
fffffe000751f958:      	add	w8, w8, #1
fffffe000751f95c:      	ldr	x0, [x27, #2456]
fffffe000751f960:      	adrp	x19, 0xfffffe00084f8000 <_nis_write_mask+0x134>
fffffe000751f964:      	add	x19, x19, #616
fffffe000751f968:      	str	w8, [x19]
fffffe000751f96c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f970:      	ldr	x8, [x19, #136]
fffffe000751f974:      	adrp	x0, 0xfffffe000709a000 <_nss_owner_loop+0x2ac>
fffffe000751f978:      	add	x0, x0, #1571
fffffe000751f97c:      	mov	w9, #1
fffffe000751f980:      	str	x8, [sp]
fffffe000751f984:      	stp	x9, x23, [sp, #8]
fffffe000751f988:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751f98c:      	b	0xfffffe000751f998 <_nss_owner_loop+0x14e4>
fffffe000751f990:      	ldr	x0, [x27, #2456]
fffffe000751f994:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f998:      	adrp	x8, 0xfffffe00084f8000 <_nis_write_mask+0x16c>
fffffe000751f99c:      	ldr	w8, [x8, #612]
fffffe000751f9a0:      	cbz	w8, 0xfffffe000751fa74 <_nss_owner_loop+0x15c0>
fffffe000751f9a4:      	mov	x19, xzr
fffffe000751f9a8:      	adrp	x23, 0xfffffe0008510000 <_nis_id_check+0x10>
fffffe000751f9ac:      	add	x23, x23, #2272
fffffe000751f9b0:      	b	0xfffffe000751f9e4 <_nss_owner_loop+0x1530>
fffffe000751f9b4:      	ldr	x0, [x27, #2456]
fffffe000751f9b8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751f9bc:      	mov	x0, x23
fffffe000751f9c0:      	mov	w1, #1514
fffffe000751f9c4:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000751f9c8:      	ldr	w8, [x21, #68]
fffffe000751f9cc:      	add	x9, x21, x19, lsl #2
fffffe000751f9d0:      	add	x19, x19, #1
fffffe000751f9d4:      	add	x23, x23, #1514
fffffe000751f9d8:      	str	wzr, [x9, x26]
fffffe000751f9dc:      	cmp	x19, x8
fffffe000751f9e0:      	b.hs	0xfffffe000751fa74 <_nss_owner_loop+0x15c0>
fffffe000751f9e4:      	ldr	x0, [x27, #2456]
fffffe000751f9e8:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751f9ec:      	ldr	w8, [x25, #36]
fffffe000751f9f0:      	cbz	w8, 0xfffffe000751f9b4 <_nss_owner_loop+0x1500>
fffffe000751f9f4:      	ldr	w8, [x25]
fffffe000751f9f8:      	cbnz	w8, 0xfffffe000751f9b4 <_nss_owner_loop+0x1500>
fffffe000751f9fc:      	ldr	w8, [x25, #4]
fffffe000751fa00:      	cbnz	w8, 0xfffffe000751f9b4 <_nss_owner_loop+0x1500>
fffffe000751fa04:      	add	x20, x21, x19, lsl #3
fffffe000751fa08:      	ldr	x0, [x27, #2456]
fffffe000751fa0c:      	ldr	x22, [x21, #208]
fffffe000751fa10:      	ldr	x28, [x20, #216]
fffffe000751fa14:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fa18:      	cmp	x28, x22
fffffe000751fa1c:      	b.ne	0xfffffe000751f9bc <_nss_owner_loop+0x1508>
fffffe000751fa20:      	add	x8, x21, x19, lsl #2
fffffe000751fa24:      	ldr	x0, [x20, #216]
fffffe000751fa28:      	mov	x1, x23
fffffe000751fa2c:      	ldr	w2, [x8, x26]
fffffe000751fa30:      	bl	0xfffffe0007af1b00 <_nord2_wifi_ifnet_receive>
fffffe000751fa34:      	cbnz	w0, 0xfffffe000751f9bc <_nss_owner_loop+0x1508>
fffffe000751fa38:      	ldr	x0, [x27, #2456]
fffffe000751fa3c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751fa40:      	adrp	x10, 0xfffffe00084f8000 <_nis_id_check+0x48>
fffffe000751fa44:      	add	x10, x10, #680
fffffe000751fa48:      	ldp	w9, w8, [x10]
fffffe000751fa4c:      	add	w9, w9, #1
fffffe000751fa50:      	cmp	w8, #7
fffffe000751fa54:      	str	w9, [x10]
fffffe000751fa58:      	b.hi	0xfffffe000751f9b4 <_nss_owner_loop+0x1500>
fffffe000751fa5c:      	add	w8, w8, #1
fffffe000751fa60:      	adrp	x9, 0xfffffe00084f8000 <_nis_id_check+0x68>
fffffe000751fa64:      	mov	w0, #1
fffffe000751fa68:      	str	w8, [x9, #684]
fffffe000751fa6c:      	bl	0xfffffe0007512bf0 <_njs_trace_locked>
fffffe000751fa70:      	b	0xfffffe000751f9b4 <_nss_owner_loop+0x1500>
fffffe000751fa74:      	adrp	x8, 0xfffffe00084f8000 <_nis_id_check+0x7c>
fffffe000751fa78:      	str	wzr, [x8, #612]
fffffe000751fa7c:      	ldr	w8, [sp, #584]
fffffe000751fa80:      	cbz	w8, 0xfffffe000751fa94 <_nss_owner_loop+0x15e0>
fffffe000751fa84:      	ldr	w23, [sp, #568]
fffffe000751fa88:      	mov	w0, #4
fffffe000751fa8c:      	bl	0xfffffe00075228d0 <_njs_notice>
fffffe000751fa90:      	b	0xfffffe000751faac <_nss_owner_loop+0x15f8>
fffffe000751fa94:      	ldr	w9, [sp, #560]
fffffe000751fa98:      	cmp	w24, #0
fffffe000751fa9c:      	cset	w8, eq
fffffe000751faa0:      	ldr	w23, [sp, #568]
fffffe000751faa4:      	orr	w8, w8, w9
fffffe000751faa8:      	tbz	w8, #0, 0xfffffe000751fb5c <_nss_owner_loop+0x16a8>
fffffe000751faac:      	cbz	w23, 0xfffffe000751fac8 <_nss_owner_loop+0x1614>
fffffe000751fab0:      	adrp	x8, 0xfffffe00084f8000 <_nis_id_check+0xb8>
fffffe000751fab4:      	ldr	w8, [x8, #576]
fffffe000751fab8:      	cmp	w8, #0
fffffe000751fabc:      	mov	w8, #2
fffffe000751fac0:      	cinc	w0, w8, ne
fffffe000751fac4:      	bl	0xfffffe00075228d0 <_njs_notice>
fffffe000751fac8:      	ldr	w8, [sp, #580]
fffffe000751facc:      	cmp	w8, #1
fffffe000751fad0:      	b.hi	0xfffffe000751fb20 <_nss_owner_loop+0x166c>
fffffe000751fad4:      	mov	w0, #4
fffffe000751fad8:      	bl	0xfffffe00075228d0 <_njs_notice>
fffffe000751fadc:      	adrp	x8, 0xfffffe000850b000 <_nis_acquire+0x48>
fffffe000751fae0:      	ldr	w8, [x8, #788]
fffffe000751fae4:      	cmp	w8, #21
fffffe000751fae8:      	b.ne	0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe000751faec:      	adrp	x8, 0xfffffe0008526000 <_nis_acquire+0xc4>
fffffe000751faf0:      	ldr	x9, [sp, #592]
fffffe000751faf4:      	ldr	x8, [x8, #2648]
fffffe000751faf8:      	cmp	x8, x9
fffffe000751fafc:      	b.ne	0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe000751fb00:      	ldr	x0, [x27, #2456]
fffffe000751fb04:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751fb08:      	adrp	x8, 0xfffffe00084f8000 <_nis_acquire+0x28>
fffffe000751fb0c:      	ldr	w8, [x8, #592]
fffffe000751fb10:      	cbz	w8, 0xfffffe000751fb88 <_nss_owner_loop+0x16d4>
fffffe000751fb14:      	ldr	x0, [x27, #2456]
fffffe000751fb18:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fb1c:      	b	0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe000751fb20:      	add	x2, sp, #608
fffffe000751fb24:      	mov	w0, #1000
fffffe000751fb28:      	mov	w1, #1000
fffffe000751fb2c:      	str	xzr, [sp, #608]
fffffe000751fb30:      	bl	0xfffffe0007337480 <_clock_interval_to_deadline>
fffffe000751fb34:      	adrp	x19, 0xfffffe00084dd000 <_nis_id_check+0xd0>
fffffe000751fb38:      	add	x19, x19, #2456
fffffe000751fb3c:      	ldr	x0, [x19]
fffffe000751fb40:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751fb44:      	ldr	x2, [sp, #608]
fffffe000751fb48:      	mov	x0, x19
fffffe000751fb4c:      	mov	w1, wzr
fffffe000751fb50:      	bl	0xfffffe000736ca44 <_assert_wait_deadline>
fffffe000751fb54:      	ldr	x0, [x19]
fffffe000751fb58:      	b	0xfffffe00075209c0 <_nss_owner_loop+0x250c>
fffffe000751fb5c:      	ldr	x0, [x27, #2456]
fffffe000751fb60:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751fb64:      	ldp	w20, w19, [x25]
fffffe000751fb68:      	ldr	x0, [x27, #2456]
fffffe000751fb6c:      	ldr	w22, [x25, #36]
fffffe000751fb70:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fb74:      	cbz	w22, 0xfffffe000751faac <_nss_owner_loop+0x15f8>
fffffe000751fb78:      	cbnz	w20, 0xfffffe000751faac <_nss_owner_loop+0x15f8>
fffffe000751fb7c:      	cbnz	w19, 0xfffffe000751faac <_nss_owner_loop+0x15f8>
fffffe000751fb80:      	mov	w0, #1
fffffe000751fb84:      	b	0xfffffe000751fa8c <_nss_owner_loop+0x15d8>
fffffe000751fb88:      	adrp	x8, 0xfffffe0008516000 <_nis_acquire+0x120>
fffffe000751fb8c:      	add	x8, x8, #2044
fffffe000751fb90:      	adrp	x9, 0xfffffe0008526000 <_nis_release+0x14>
fffffe000751fb94:      	add	x9, x9, #2660
fffffe000751fb98:      	adrp	x11, 0xfffffe0007c60000 <_nss_owner_loop+0x33e8>
fffffe000751fb9c:      	add	x11, x11, #948
fffffe000751fba0:      	ldr	q0, [x8]
fffffe000751fba4:      	add	x8, x9, #8
fffffe000751fba8:      	ldr	d1, [x9]
fffffe000751fbac:      	adrp	x9, 0xfffffe0008528000 <_nis_release+0x38>
fffffe000751fbb0:      	adrp	x0, 0xfffffe0007c9c000 <_nss_owner_loop+0x34f0>
fffffe000751fbb4:      	ldur	q3, [x11, #8]
fffffe000751fbb8:      	adrp	x2, 0xfffffe00084f8000 <_nis_acquire+0xd8>
fffffe000751fbbc:      	add	x2, x2, #552
fffffe000751fbc0:      	ldr	d2, [x9, #3696]
fffffe000751fbc4:      	adrp	x9, 0xfffffe00084dd000 <_nis_acquire+0x78>
fffffe000751fbc8:      	add	x9, x9, #2468
fffffe000751fbcc:      	ld1.s	{ v1 }[2], [x8]
fffffe000751fbd0:      	add	x0, x0, #648
fffffe000751fbd4:      	mov	w16, #17308
fffffe000751fbd8:      	ldr	q4, [sp, #336]
fffffe000751fbdc:      	movk	w16, #1, lsl #16
fffffe000751fbe0:      	ld1.s	{ v2 }[2], [x9]
fffffe000751fbe4:      	add	x1, x2, #3264
fffffe000751fbe8:      	ldr	w8, [x0, #304]
fffffe000751fbec:      	add	x17, x2, #3272
fffffe000751fbf0:      	ld1.s	{ v1 }[3], [x11]
fffffe000751fbf4:      	cmtst.4s	v0, v0, v0
fffffe000751fbf8:      	ldr	x3, [sp, #368]
fffffe000751fbfc:      	cmp	w8, #0
fffffe000751fc00:      	ldr	d6, [x2, #32]
fffffe000751fc04:      	ldp	w10, w8, [x2]
fffffe000751fc08:      	cset	w9, eq
fffffe000751fc0c:      	ldr	w13, [x0, #280]
fffffe000751fc10:      	cmeq.4s	v3, v3, v4
fffffe000751fc14:      	ldr	w14, [x0, #16]
fffffe000751fc18:      	cmtst.4s	v5, v1, v1
fffffe000751fc1c:      	ldr	q7, [x17]
fffffe000751fc20:      	cmp	w10, #0
fffffe000751fc24:      	adrp	x10, 0xfffffe0008528000 <_nis_id_read+0x58>
fffffe000751fc28:      	add	x10, x10, #3228
fffffe000751fc2c:      	orr	w13, w13, w14
fffffe000751fc30:      	cmeq.4s	v1, v1, #0
fffffe000751fc34:      	add	x14, x3, #4
fffffe000751fc38:      	mvn.16b	v4, v3
fffffe000751fc3c:      	ldr	w15, [x0, #60]
fffffe000751fc40:      	ld1.s	{ v2 }[3], [x10]
fffffe000751fc44:      	cset	w10, ne
fffffe000751fc48:      	uzp1.8h	v1, v1, v3
fffffe000751fc4c:      	ldr	w0, [x0, #300]
fffffe000751fc50:      	uzp1.8h	v3, v5, v4
fffffe000751fc54:      	ldr	s4, [x3, #4096]
fffffe000751fc58:      	ldr	d5, [x2, #56]
fffffe000751fc5c:      	cmtst.4s	v2, v2, v2
fffffe000751fc60:      	ldp	x11, x12, [x2, #336]
fffffe000751fc64:      	uzp1.8h	v0, v0, v2
fffffe000751fc68:      	ldr	d2, [x2, x16]
fffffe000751fc6c:      	add	x16, x2, #140
fffffe000751fc70:      	ld1.s	{ v5 }[2], [x1]
fffffe000751fc74:      	cmp	x12, #0
fffffe000751fc78:      	uzp1.4s	v4, v4, v2
fffffe000751fc7c:      	ld1.s	{ v6 }[1], [x16]
fffffe000751fc80:      	mov.s	v5[3], v7[3]
fffffe000751fc84:      	ld1.s	{ v7 }[3], [x14]
fffffe000751fc88:      	ldr	x14, [sp, #280]
fffffe000751fc8c:      	cset	w12, ne
fffffe000751fc90:      	cmp	w13, #0
fffffe000751fc94:      	ldr	w16, [x2, #20]
fffffe000751fc98:      	mov.s	v4[1], v2[1]
fffffe000751fc9c:      	ldur	d2, [x2, #44]
fffffe000751fca0:      	cset	w13, ne
fffffe000751fca4:      	cmp	w15, #0
fffffe000751fca8:      	ccmp	w0, #0, #4, eq
fffffe000751fcac:      	mov.d	v6[1], v2[0]
fffffe000751fcb0:      	ldp	q16, q2, [sp, #288]
fffffe000751fcb4:      	ccmp	w16, #0, #4, ne
fffffe000751fcb8:      	cmeq.4s	v7, v7, v16
fffffe000751fcbc:      	cset	w15, eq
fffffe000751fcc0:      	uzp1.16b	v16, v0, v3
fffffe000751fcc4:      	cmp	w8, #0
fffffe000751fcc8:      	ld1.s	{ v4 }[3], [x14]
fffffe000751fccc:      	xtn2.16b	v17, v1
fffffe000751fcd0:      	cmeq.4s	v2, v5, v2
fffffe000751fcd4:      	ldr	w14, [x2, #28]
fffffe000751fcd8:      	cmtst.4s	v5, v6, v6
fffffe000751fcdc:      	ldr	w16, [x3, #4796]
fffffe000751fce0:      	mvn.16b	v2, v2
fffffe000751fce4:      	ldr	x8, [x2, #200]
fffffe000751fce8:      	cmtst.4s	v4, v4, v4
fffffe000751fcec:      	ccmp	w14, #0, #4, eq
fffffe000751fcf0:      	uzp1.8h	v2, v5, v2
fffffe000751fcf4:      	ldr	x14, [x3, #1272]
fffffe000751fcf8:      	mvn.16b	v5, v7
fffffe000751fcfc:      	uzp1.8h	v4, v5, v4
fffffe000751fd00:      	ccmp	w16, #0, #4, ne
fffffe000751fd04:      	cmeq.4s	v0, v6, #0
fffffe000751fd08:      	xtn.4h	v0, v0
fffffe000751fd0c:      	ccmp	x8, #0, #4, ne
fffffe000751fd10:      	uzp1.16b	v1, v2, v4
fffffe000751fd14:      	ldr	q2, [sp, #320]
fffffe000751fd18:      	xtn.8b	v0, v0
fffffe000751fd1c:      	cset	w16, eq
fffffe000751fd20:      	cmp	x14, x8
fffffe000751fd24:      	tbl.16b	v2, { v16, v17 }, v2
fffffe000751fd28:      	ldr	x14, [x3, #4776]
fffffe000751fd2c:      	mov.b	v1[1], v0[1]
fffffe000751fd30:      	ccmp	x14, #0, #0, eq
fffffe000751fd34:      	orr.16b	v0, v2, v1
fffffe000751fd38:      	umaxv.16b	b0, v0
fffffe000751fd3c:      	ccmp	x11, #0, #0, eq
fffffe000751fd40:      	cset	w11, ne
fffffe000751fd44:      	fmov	w14, s0
fffffe000751fd48:      	orr	w9, w14, w9
fffffe000751fd4c:      	orr	w9, w9, w13
fffffe000751fd50:      	orr	w9, w9, w15
fffffe000751fd54:      	orr	w9, w9, w10
fffffe000751fd58:      	orr	w10, w16, w11
fffffe000751fd5c:      	orr	w9, w9, w10
fffffe000751fd60:      	orr	w9, w9, w12
fffffe000751fd64:      	tbnz	w9, #0, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fd68:      	ldr	x9, [sp, #368]
fffffe000751fd6c:      	adrp	x10, 0xfffffe0008516000 <_OUTLINED_FUNCTION_1+0x8>
fffffe000751fd70:      	add	x10, x10, #2072
fffffe000751fd74:      	ldr	x9, [x9, #4800]
fffffe000751fd78:      	cmp	x9, x10
fffffe000751fd7c:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fd80:      	adrp	x13, 0xfffffe000850b000 <_nis_delay+0x30>
fffffe000751fd84:      	add	x13, x13, #2112
fffffe000751fd88:      	adrp	x17, 0xfffffe0008516000 <_nbc_prepare+0x8>
fffffe000751fd8c:      	add	x17, x17, #2072
fffffe000751fd90:      	add	x9, x17, #28
fffffe000751fd94:      	mov	w10, #55696
fffffe000751fd98:      	ldr	d0, [x13, #8400]
fffffe000751fd9c:      	adrp	x16, 0xfffffe000852a000 <_nbc_prepare+0x6c>
fffffe000751fda0:      	add	x16, x16, #3680
fffffe000751fda4:      	ldur	d2, [x17, #44]
fffffe000751fda8:      	ldr	d1, [x17, x10]
fffffe000751fdac:      	ld1.s	{ v0 }[2], [x9]
fffffe000751fdb0:      	add	x9, x17, #36
fffffe000751fdb4:      	ldr	x10, [x17, #16]
fffffe000751fdb8:      	ldr	x13, [x13]
fffffe000751fdbc:      	mov.d	v2[1], v1[0]
fffffe000751fdc0:      	ldr	q1, [sp, #256]
fffffe000751fdc4:      	ld1.s	{ v0 }[3], [x9]
fffffe000751fdc8:      	cmp	x10, x16
fffffe000751fdcc:      	cmeq.4s	v1, v2, v1
fffffe000751fdd0:      	cmeq.4s	v2, v0, #0
fffffe000751fdd4:      	cmtst.4s	v0, v0, v0
fffffe000751fdd8:      	uzp1.8h	v1, v2, v1
fffffe000751fddc:      	xtn.4h	v0, v0
fffffe000751fde0:      	uzp1.16b	v0, v1, v0
fffffe000751fde4:      	tbl.8b	v0, { v0 }, v9
fffffe000751fde8:      	umov.b	w9, v0[1]
fffffe000751fdec:      	umov.b	w12, v0[2]
fffffe000751fdf0:      	umov.b	w11, v0[0]
fffffe000751fdf4:      	umov.b	w14, v0[3]
fffffe000751fdf8:      	umov.b	w15, v0[4]
fffffe000751fdfc:      	umov.b	w10, v0[5]
fffffe000751fe00:      	umov.b	w16, v0[6]
fffffe000751fe04:      	and	w9, w9, #0x1
fffffe000751fe08:      	and	w12, w12, #0x1
fffffe000751fe0c:      	and	w11, w11, #0x1
fffffe000751fe10:      	and	w14, w14, #0x1
fffffe000751fe14:      	and	w10, w10, #0x1
fffffe000751fe18:      	bfi	w11, w9, #1, #1
fffffe000751fe1c:      	and	w9, w15, #0x1
fffffe000751fe20:      	bfi	w11, w12, #2, #1
fffffe000751fe24:      	ldr	w12, [x17, #152]
fffffe000751fe28:      	bfi	w11, w14, #3, #1
fffffe000751fe2c:      	umov.b	w14, v0[7]
fffffe000751fe30:      	bfi	w11, w9, #4, #1
fffffe000751fe34:      	cset	w9, eq
fffffe000751fe38:      	and	w15, w16, #0x1
fffffe000751fe3c:      	cmp	w12, #0
fffffe000751fe40:      	ldr	x12, [sp, #248]
fffffe000751fe44:      	bfi	w11, w10, #5, #1
fffffe000751fe48:      	orr	w10, w11, w15, lsl #6
fffffe000751fe4c:      	ldr	w11, [x17, #40]
fffffe000751fe50:      	orr	w10, w10, w14, lsl #7
fffffe000751fe54:      	ccmp	x13, x12, #0, eq
fffffe000751fe58:      	mov	w12, #255
fffffe000751fe5c:      	bic	w10, w12, w10
fffffe000751fe60:      	orr	w10, w10, w11
fffffe000751fe64:      	ccmp	w10, #0, #0, eq
fffffe000751fe68:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fe6c:      	adrp	x10, 0xfffffe0008516000 <_nbc_prepare+0xec>
fffffe000751fe70:      	add	x10, x10, #2072
fffffe000751fe74:      	adrp	x11, 0xfffffe0007c5f000 <_nss_owner_loop+0x36c0>
fffffe000751fe78:      	add	x11, x11, #1624
fffffe000751fe7c:      	ldr	x10, [x10]
fffffe000751fe80:      	cmp	x10, x11
fffffe000751fe84:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fe88:      	adrp	x10, 0xfffffe0008516000 <_nbc_prepare+0x108>
fffffe000751fe8c:      	add	x10, x10, #2072
fffffe000751fe90:      	adrp	x11, 0xfffffe0008526000 <_nbc_prepare+0x150>
fffffe000751fe94:      	add	x11, x11, #2680
fffffe000751fe98:      	ldr	x10, [x10, #8]
fffffe000751fe9c:      	cmp	x10, x11
fffffe000751fea0:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fea4:      	tbz	w9, #0, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fea8:      	adrp	x9, 0xfffffe000850b000 <_nbc_prepare+0xfc>
fffffe000751feac:      	add	x9, x9, #2112
fffffe000751feb0:      	ldr	x9, [x9, #8408]
fffffe000751feb4:      	cbnz	x9, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751feb8:      	adrp	x9, 0xfffffe0008516000 <_nbc_prepare+0x138>
fffffe000751febc:      	add	x9, x9, #2072
fffffe000751fec0:      	ldr	x9, [x9, #160]
fffffe000751fec4:      	cmp	x9, x8
fffffe000751fec8:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fecc:      	adrp	x0, 0xfffffe0008526000 <_cap_receipt+0x2c>
fffffe000751fed0:      	add	x0, x0, #2680
fffffe000751fed4:      	bl	0xfffffe000799c814 <_nct_done_receipt>
fffffe000751fed8:      	cbz	w0, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fedc:      	adrp	x8, 0xfffffe0008516000 <_nbc_prepare+0x15c>
fffffe000751fee0:      	adrp	x10, 0xfffffe0008524000 <_cap_receipt+0x38>
fffffe000751fee4:      	add	x10, x10, #1216
fffffe000751fee8:      	ldr	x8, [x8, #2080]
fffffe000751feec:      	ldr	x9, [x8]
fffffe000751fef0:      	cmp	x9, x10
fffffe000751fef4:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751fef8:      	ldr	x9, [x8, #8]
fffffe000751fefc:      	adrp	x10, 0xfffffe0008525000 <_cap_receipt+0x58>
fffffe000751ff00:      	add	x10, x10, #1592
fffffe000751ff04:      	cmp	x9, x10
fffffe000751ff08:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ff0c:      	ldr	x9, [x8, #16]
fffffe000751ff10:      	adrp	x10, 0xfffffe0007c5f000 <_nss_owner_loop+0x375c>
fffffe000751ff14:      	add	x10, x10, #1624
fffffe000751ff18:      	cmp	x9, x10
fffffe000751ff1c:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ff20:      	adrp	x10, 0xfffffe0007c9c000 <_njs_cache_scan_locked+0x48>
fffffe000751ff24:      	ldr	x9, [x8, #24]
fffffe000751ff28:      	add	x10, x10, #648
fffffe000751ff2c:      	cmp	x9, x10
fffffe000751ff30:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ff34:      	adrp	x10, 0xfffffe0008516000 <_cap_receipt+0x54>
fffffe000751ff38:      	add	x10, x10, #2096
fffffe000751ff3c:      	ldr	w9, [x8, #132]
fffffe000751ff40:      	ldr	w8, [x8, #68]
fffffe000751ff44:      	ldr	w10, [x10, #64]
fffffe000751ff48:      	add	w8, w8, w9
fffffe000751ff4c:      	cmp	w10, w8
fffffe000751ff50:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ff54:      	adrp	x8, 0xfffffe0008516000 <_cap_receipt+0x74>
fffffe000751ff58:      	nop
fffffe000751ff5c:      	ldr	w8, [x8, #2096]
fffffe000751ff60:      	cbz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ff64:      	adrp	x8, 0xfffffe0008516000 <_cap_receipt+0x84>
fffffe000751ff68:      	add	x8, x8, #2096
fffffe000751ff6c:      	ldr	w8, [x8, #92]
fffffe000751ff70:      	cmp	w8, #23
fffffe000751ff74:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ff78:      	adrp	x8, 0xfffffe0008516000 <_cap_receipt+0x98>
fffffe000751ff7c:      	add	x8, x8, #2096
fffffe000751ff80:      	ldr	w8, [x8, #108]
fffffe000751ff84:      	cbnz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000751ff88:      	adrp	x11, 0xfffffe0008516000 <_cap_receipt+0xa8>
fffffe000751ff8c:      	add	x11, x11, #2128
fffffe000751ff90:      	add	x8, x11, #1476
fffffe000751ff94:      	add	x9, x11, #1504
fffffe000751ff98:      	ldr	d0, [x11]
fffffe000751ff9c:      	ldr	d1, [x8]
fffffe000751ffa0:      	add	x8, x11, #1472
fffffe000751ffa4:      	ldr	w10, [x11, #1508]
fffffe000751ffa8:      	ld1.s	{ v0 }[2], [x9]
fffffe000751ffac:      	add	x9, x11, #1496
fffffe000751ffb0:      	ld1.s	{ v1 }[2], [x8]
fffffe000751ffb4:      	add	x8, x11, #584
fffffe000751ffb8:      	ld1.s	{ v0 }[3], [x9]
fffffe000751ffbc:      	adrp	x9, 0xfffffe0007c60000 <_nss_owner_loop+0x380c>
fffffe000751ffc0:      	ld1.s	{ v1 }[3], [x8]
fffffe000751ffc4:      	ldr	w8, [x11, #64]
fffffe000751ffc8:      	ldrb	w9, [x9, #412]
fffffe000751ffcc:      	cmtst.4s	v0, v0, v0
fffffe000751ffd0:      	cmtst.4s	v1, v1, v1
fffffe000751ffd4:      	sub	w9, w9, #1
fffffe000751ffd8:      	cmp	w8, w9, uxtb
fffffe000751ffdc:      	ldr	w8, [x11, #84]
fffffe000751ffe0:      	uzp1.8h	v0, v0, v1
fffffe000751ffe4:      	cset	w9, eq
fffffe000751ffe8:      	cmp	w10, #3
fffffe000751ffec:      	cset	w10, eq
fffffe000751fff0:      	cmp	w8, #0
fffffe000751fff4:      	xtn.8b	v0, v0
fffffe000751fff8:      	umaxv.8b	b0, v0
fffffe000751fffc:      	fmov	w11, s0
fffffe0007520000:      	bic	w8, w10, w11
fffffe0007520004:      	cset	w10, ne
fffffe0007520008:      	and	w8, w8, w9
fffffe000752000c:      	and	w8, w8, w10
fffffe0007520010:      	cmp	w8, #1
fffffe0007520014:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520018:      	adrp	x8, 0xfffffe0008516000 <_cap_receipt+0x134>
fffffe000752001c:      	add	x8, x8, #2128
fffffe0007520020:      	ldr	w8, [x8, #728]
fffffe0007520024:      	cbnz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520028:      	adrp	x8, 0xfffffe0008516000 <_cap_receipt+0x144>
fffffe000752002c:      	add	x8, x8, #2128
fffffe0007520030:      	ldrb	w8, [x8, #1537]
fffffe0007520034:      	cbnz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520038:      	adrp	x0, 0xfffffe0008519000 <_cap_receipt+0x160>
fffffe000752003c:      	add	x0, x0, #32
fffffe0007520040:      	bl	0xfffffe00079acb40 <_nstx_idle>
fffffe0007520044:      	cbz	w0, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520048:      	adrp	x8, 0xfffffe0008516000 <_cap_receipt+0x164>
fffffe000752004c:      	nop
fffffe0007520050:      	ldr	w8, [x8, #2164]
fffffe0007520054:      	cbz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520058:      	adrp	x9, 0xfffffe0008516000 <_cap_receipt+0x174>
fffffe000752005c:      	add	x9, x9, #2164
fffffe0007520060:      	ldr	w9, [x9, #4]
fffffe0007520064:      	cmp	w8, w9
fffffe0007520068:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000752006c:      	adrp	x9, 0xfffffe0008516000 <_nbc_lease_window+0x8>
fffffe0007520070:      	ldr	w9, [x9, #2160]
fffffe0007520074:      	cmn	w8, w9
fffffe0007520078:      	b.hs	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000752007c:      	adrp	x10, 0xfffffe0007c5f000 <_njs_cache_scan_locked+0xac>
fffffe0007520080:      	ldr	w10, [x10, #3552]
fffffe0007520084:      	cmn	w10, #1
fffffe0007520088:      	b.eq	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000752008c:      	adrp	x11, 0xfffffe0008519000 <_nbc_lease_window+0x34>
fffffe0007520090:      	add	w8, w9, w8
fffffe0007520094:      	ldr	w11, [x11, #76]
fffffe0007520098:      	sub	w8, w8, w11
fffffe000752009c:      	cmp	w10, w8
fffffe00075200a0:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe00075200a4:      	adrp	x9, 0xfffffe0007c5f000 <_njs_cache_scan_locked+0xd4>
fffffe00075200a8:      	add	x9, x9, #3540
fffffe00075200ac:      	movi.2d	v1, #0000000000000000
fffffe00075200b0:      	ldr	w8, [x9, #956]
fffffe00075200b4:      	fmov	s0, w8
fffffe00075200b8:      	ld1.s	{ v0 }[1], [x9], #4
fffffe00075200bc:      	ldr	d2, [x9]
fffffe00075200c0:      	adrp	x9, 0xfffffe0008516000 <_nbc_lease_window+0x5c>
fffffe00075200c4:      	add	x9, x9, #2156
fffffe00075200c8:      	mov.d	v0[1], v2[0]
fffffe00075200cc:      	ld1.s	{ v1 }[0], [x9]
fffffe00075200d0:      	cmeq.4s	v0, v0, v1
fffffe00075200d4:      	xtn.4h	v0, v0
fffffe00075200d8:      	umov.h	w9, v0[1]
fffffe00075200dc:      	umov.h	w10, v0[2]
fffffe00075200e0:      	umov.h	w11, v0[0]
fffffe00075200e4:      	umov.h	w12, v0[3]
fffffe00075200e8:      	and	w9, w9, #0x1
fffffe00075200ec:      	and	w10, w10, #0x1
fffffe00075200f0:      	bfi	w11, w9, #1, #1
fffffe00075200f4:      	mov	w9, #15
fffffe00075200f8:      	bfi	w11, w10, #2, #1
fffffe00075200fc:      	bfi	w11, w12, #3, #29
fffffe0007520100:      	bics	wzr, w9, w11
fffffe0007520104:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520108:      	cmp	w8, #1023
fffffe000752010c:      	b.hi	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520110:      	adrp	x8, 0xfffffe0007c5f000 <_njs_cache_scan_locked+0x140>
fffffe0007520114:      	add	x8, x8, #1636
fffffe0007520118:      	ldr	w8, [x8, #1872]
fffffe000752011c:      	cbnz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520120:      	adrp	x8, 0xfffffe0007c5f000 <_njs_cache_scan_locked+0x150>
fffffe0007520124:      	nop
fffffe0007520128:      	ldr	w8, [x8, #1636]
fffffe000752012c:      	cbnz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520130:      	adrp	x8, 0xfffffe0008516000 <_nbc_lease_window+0xcc>
fffffe0007520134:      	ldr	w8, [x8, #2144]
fffffe0007520138:      	cbnz	w8, 0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000752013c:      	adrp	x9, 0xfffffe0008516000 <_nbc_lease_window+0xd8>
fffffe0007520140:      	add	x9, x9, #2136
fffffe0007520144:      	ldp	w9, w8, [x9]
fffffe0007520148:      	cmp	w8, w9
fffffe000752014c:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520150:      	adrp	x10, 0xfffffe0008516000 <_nbc_lease_window+0xec>
fffffe0007520154:      	add	x10, x10, #2148
fffffe0007520158:      	ldp	w9, w10, [x10]
fffffe000752015c:      	cmp	w9, w10
fffffe0007520160:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520164:      	adrp	x9, 0xfffffe0008516000 <_nbc_lease_window+0x100>
fffffe0007520168:      	ldr	x9, [x9, #2080]
fffffe000752016c:      	ldr	w9, [x9, #108]
fffffe0007520170:      	cmp	w8, w9
fffffe0007520174:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520178:      	adrp	x9, 0xfffffe0008524000 <_nbc_lease_window+0x14c>
fffffe000752017c:      	ldr	w9, [x9, #1648]
fffffe0007520180:      	sub	w9, w9, #1
fffffe0007520184:      	cmp	w8, w9
fffffe0007520188:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000752018c:      	adrp	x8, 0xfffffe0007c60000 <_nss_deliver+0x28>
fffffe0007520190:      	adrp	x9, 0xfffffe0008516000 <_nbc_lease_window+0x12c>
fffffe0007520194:      	ldr	w8, [x8, #404]
fffffe0007520198:      	ldr	w9, [x9, #2424]
fffffe000752019c:      	cmp	w8, w9
fffffe00075201a0:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe00075201a4:      	mov	x8, xzr
fffffe00075201a8:      	adrp	x9, 0xfffffe0008516000 <_nbc_lease_window+0x144>
fffffe00075201ac:      	add	x9, x9, #2348
fffffe00075201b0:      	adrp	x10, 0xfffffe0007c5f000 <_nss_deliver+0x48>
fffffe00075201b4:      	add	x10, x10, #1624
fffffe00075201b8:      	add	x10, x10, x8
fffffe00075201bc:      	ldur	x11, [x9, #-36]
fffffe00075201c0:      	ldr	x12, [x10, #216]
fffffe00075201c4:      	cmp	x11, x12
fffffe00075201c8:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe00075201cc:      	ldur	x11, [x9, #-28]
fffffe00075201d0:      	ldr	x10, [x10, #224]
fffffe00075201d4:      	cmp	x11, x10
fffffe00075201d8:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe00075201dc:      	adrp	x10, 0xfffffe0007c5f000 <_nss_deliver+0x74>
fffffe00075201e0:      	add	x10, x10, #1624
fffffe00075201e4:      	add	x11, x10, x8
fffffe00075201e8:      	ldur	w10, [x9, #-20]
fffffe00075201ec:      	ldr	w12, [x11, #232]
fffffe00075201f0:      	cmp	w10, w12
fffffe00075201f4:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe00075201f8:      	ldur	w10, [x9, #-16]
fffffe00075201fc:      	ldr	w11, [x11, #236]
fffffe0007520200:      	cmp	w10, w11
fffffe0007520204:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520208:      	adrp	x11, 0xfffffe0007c5f000 <_nss_deliver+0xa0>
fffffe000752020c:      	add	x11, x11, #1624
fffffe0007520210:      	add	x11, x11, x8
fffffe0007520214:      	ldur	w12, [x9, #-12]
fffffe0007520218:      	ldr	w13, [x11, #240]
fffffe000752021c:      	cmp	w12, w13
fffffe0007520220:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520224:      	ldur	w12, [x9, #-8]
fffffe0007520228:      	ldr	w11, [x11, #244]
fffffe000752022c:      	cmp	w12, w11
fffffe0007520230:      	b.ne	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520234:      	ldur	w11, [x9, #-4]
fffffe0007520238:      	cmp	w11, w10
fffffe000752023c:      	b.hs	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe0007520240:      	ldr	w10, [x9]
fffffe0007520244:      	cmp	w10, #2
fffffe0007520248:      	b.hs	0xfffffe000751fb14 <_nss_owner_loop+0x1660>
fffffe000752024c:      	add	x8, x8, #32
fffffe0007520250:      	add	x9, x9, #80
fffffe0007520254:      	cmp	x8, #160
fffffe0007520258:      	b.ne	0xfffffe00075201b0 <_nss_owner_loop+0x1cfc>
fffffe000752025c:      	adrp	x10, 0xfffffe0008516000 <_nbc_run+0x98>
fffffe0007520260:      	add	x10, x10, #2108
fffffe0007520264:      	mov	w8, #1
fffffe0007520268:      	adrp	x9, 0xfffffe0008528000 <_nbc_run+0xec>
fffffe000752026c:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0xe4>
fffffe0007520270:      	add	x0, x0, #1592
fffffe0007520274:      	ldp	w24, w26, [x10]
fffffe0007520278:      	str	w8, [x9, #3696]
fffffe000752027c:      	stur	wzr, [x29, #-192]
fffffe0007520280:      	str	d8, [x10]
fffffe0007520284:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520288:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752028c:      	adrp	x10, 0xfffffe0008526000 <_nbc_run+0x108>
fffffe0007520290:      	add	x10, x10, #2648
fffffe0007520294:      	add	x9, sp, #608
fffffe0007520298:      	str	xzr, [sp, #608]
fffffe000752029c:      	ldr	w8, [x10, #12]
fffffe00075202a0:      	str	xzr, [x9, #296]
fffffe00075202a4:      	cbz	w8, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075202a8:      	ldr	w8, [x10, #20]
fffffe00075202ac:      	cbnz	w8, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075202b0:      	ldr	x8, [x10]
fffffe00075202b4:      	ldr	x9, [sp, #592]
fffffe00075202b8:      	cmp	x8, x9
fffffe00075202bc:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075202c0:      	add	x0, sp, #608
fffffe00075202c4:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe00075202c8:      	ldr	x0, [sp, #608]
fffffe00075202cc:      	sub	x1, x29, #168
fffffe00075202d0:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe00075202d4:      	add	x8, sp, #608
fffffe00075202d8:      	mov	x9, #63439
fffffe00075202dc:      	movk	x9, #58195, lsl #16
fffffe00075202e0:      	movk	x9, #39845, lsl #32
fffffe00075202e4:      	ldr	x8, [x8, #296]
fffffe00075202e8:      	movk	x9, #8388, lsl #48
fffffe00075202ec:      	lsr	x8, x8, #3
fffffe00075202f0:      	umulh	x8, x8, x9
fffffe00075202f4:      	adrp	x9, 0xfffffe0008516000 <_nbc_run+0x130>
fffffe00075202f8:      	add	x9, x9, #2248
fffffe00075202fc:      	lsr	x8, x8, #4
fffffe0007520300:      	ldr	x9, [x9]
fffffe0007520304:      	cmp	x8, x9
fffffe0007520308:      	b.lo	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752030c:      	adrp	x9, 0xfffffe0008516000 <_nbc_run+0x148>
fffffe0007520310:      	add	x9, x9, #2248
fffffe0007520314:      	ldr	x9, [x9, #8]
fffffe0007520318:      	cmp	x8, x9
fffffe000752031c:      	b.hs	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520320:      	adrp	x9, 0xfffffe0008516000 <_nbc_run+0x15c>
fffffe0007520324:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x19c>
fffffe0007520328:      	add	x0, x0, #1592
fffffe000752032c:      	str	x8, [x9, #2248]
fffffe0007520330:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520334:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520338:      	adrp	x8, 0xfffffe0008524000 <_nbc_run+0x1ac>
fffffe000752033c:      	mov	w9, #3
fffffe0007520340:      	ldr	x8, [x8, #736]
fffffe0007520344:      	ldr	w8, [x8, #240]
fffffe0007520348:      	dmb	oshld
fffffe000752034c:      	bics	wzr, w9, w8
fffffe0007520350:      	stur	w8, [x29, #-192]
fffffe0007520354:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520358:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x1d0>
fffffe000752035c:      	add	x0, x0, #1592
fffffe0007520360:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520364:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520368:      	adrp	x8, 0xfffffe0007c60000 <_njs_rejoin_refresh+0x90>
fffffe000752036c:      	ldr	w8, [x8, #956]
fffffe0007520370:      	sub	w8, w8, #4
fffffe0007520374:      	cmp	w8, #3
fffffe0007520378:      	cset	w0, lo
fffffe000752037c:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe0007520380:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520384:      	adrp	x8, 0xfffffe0008528000 <_nbc_run+0x208>
fffffe0007520388:      	ldr	w8, [x8, #3752]
fffffe000752038c:      	cbz	w8, 0xfffffe000752039c <_nss_owner_loop+0x1ee8>
fffffe0007520390:      	adrp	x8, 0xfffffe0008528000 <_nbc_run+0x214>
fffffe0007520394:      	ldr	w8, [x8, #3228]
fffffe0007520398:      	cbnz	w8, 0xfffffe00075203a8 <_nss_owner_loop+0x1ef4>
fffffe000752039c:      	adrp	x0, 0xfffffe0007c5f000 <_njs_rejoin_refresh+0xc0>
fffffe00075203a0:      	add	x0, x0, #1624
fffffe00075203a4:      	bl	0xfffffe0007516738 <_nst_native_rx_active>
fffffe00075203a8:      	adrp	x8, 0xfffffe00084dd000 <_nbc_run+0x100>
fffffe00075203ac:      	mov	w9, #16896
fffffe00075203b0:      	movk	w9, #6146, lsl #16
fffffe00075203b4:      	adrp	x10, 0xfffffe0007c60000 <_njs_rejoin_refresh+0xdc>
fffffe00075203b8:      	add	x10, x10, #1044
fffffe00075203bc:      	ldr	x8, [x8, #1664]
fffffe00075203c0:      	ldr	w8, [x8, #512]
fffffe00075203c4:      	dmb	oshld
fffffe00075203c8:      	stp	w9, w8, [x10]
fffffe00075203cc:      	tst	w8, #0x300000
fffffe00075203d0:      	stur	w8, [x29, #-192]
fffffe00075203d4:      	mov	w8, #1
fffffe00075203d8:      	str	w8, [x10, #8]
fffffe00075203dc:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075203e0:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x258>
fffffe00075203e4:      	add	x0, x0, #1592
fffffe00075203e8:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe00075203ec:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075203f0:      	adrp	x8, 0xfffffe0007c60000 <_njs_rejoin_refresh+0x118>
fffffe00075203f4:      	ldr	w8, [x8, #956]
fffffe00075203f8:      	sub	w8, w8, #4
fffffe00075203fc:      	cmp	w8, #3
fffffe0007520400:      	cset	w0, lo
fffffe0007520404:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe0007520408:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752040c:      	adrp	x8, 0xfffffe0008528000 <_nbc_run+0x290>
fffffe0007520410:      	ldr	w8, [x8, #3752]
fffffe0007520414:      	cbz	w8, 0xfffffe0007520424 <_nss_owner_loop+0x1f70>
fffffe0007520418:      	adrp	x8, 0xfffffe0008528000 <_nbc_run+0x29c>
fffffe000752041c:      	ldr	w8, [x8, #3228]
fffffe0007520420:      	cbnz	w8, 0xfffffe0007520430 <_nss_owner_loop+0x1f7c>
fffffe0007520424:      	adrp	x0, 0xfffffe0007c5f000 <_njs_rejoin_refresh+0x148>
fffffe0007520428:      	add	x0, x0, #1624
fffffe000752042c:      	bl	0xfffffe0007516738 <_nst_native_rx_active>
fffffe0007520430:      	adrp	x8, 0xfffffe00084dd000 <_nbc_run+0x188>
fffffe0007520434:      	mov	w9, #16896
fffffe0007520438:      	movk	w9, #6146, lsl #16
fffffe000752043c:      	adrp	x10, 0xfffffe0007c60000 <_njs_rejoin_refresh+0x164>
fffffe0007520440:      	add	x10, x10, #1044
fffffe0007520444:      	add	w9, w9, #1, lsl #12
fffffe0007520448:      	ldr	x8, [x8, #1664]
fffffe000752044c:      	ldr	w8, [x8, #4608]
fffffe0007520450:      	dmb	oshld
fffffe0007520454:      	stur	w8, [x29, #-192]
fffffe0007520458:      	tst	w8, #0x300000
fffffe000752045c:      	stp	w9, w8, [x10]
fffffe0007520460:      	mov	w8, #1
fffffe0007520464:      	str	w8, [x10, #8]
fffffe0007520468:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752046c:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x2e4>
fffffe0007520470:      	add	x0, x0, #1592
fffffe0007520474:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520478:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752047c:      	mov	w8, #21256
fffffe0007520480:      	sub	x2, x29, #192
fffffe0007520484:      	movk	w8, #6146, lsl #16
fffffe0007520488:      	adrp	x0, 0xfffffe0007c5f000 <_njs_early_finish+0xc>
fffffe000752048c:      	add	x0, x0, #1624
fffffe0007520490:      	add	w1, w8, #272
fffffe0007520494:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe0007520498:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752049c:      	adrp	x9, 0xfffffe0008516000 <_nbc_run+0x2d8>
fffffe00075204a0:      	ldur	w8, [x29, #-192]
fffffe00075204a4:      	ldr	w9, [x9, #2156]
fffffe00075204a8:      	cmp	w8, w9
fffffe00075204ac:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075204b0:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x328>
fffffe00075204b4:      	add	x0, x0, #1592
fffffe00075204b8:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe00075204bc:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075204c0:      	mov	w8, #21256
fffffe00075204c4:      	sub	x2, x29, #192
fffffe00075204c8:      	movk	w8, #6146, lsl #16
fffffe00075204cc:      	adrp	x0, 0xfffffe0007c5f000 <_njs_early_finish+0x50>
fffffe00075204d0:      	add	x0, x0, #1624
fffffe00075204d4:      	add	w1, w8, #276
fffffe00075204d8:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe00075204dc:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075204e0:      	adrp	x8, 0xfffffe0008516000 <_nbc_run+0x31c>
fffffe00075204e4:      	ldur	w19, [x29, #-192]
fffffe00075204e8:      	ldr	w8, [x8, #2156]
fffffe00075204ec:      	cmp	w19, w8
fffffe00075204f0:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075204f4:      	adrp	x8, 0xfffffe0007c60000 <_njs_early_finish+0x7c>
fffffe00075204f8:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x370>
fffffe00075204fc:      	add	x0, x0, #1592
fffffe0007520500:      	ldr	x20, [x8, #368]
fffffe0007520504:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520508:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752050c:      	adrp	x8, 0xfffffe0007c60000 <_njs_early_finish+0x94>
fffffe0007520510:      	ldr	w8, [x8, #956]
fffffe0007520514:      	sub	w8, w8, #4
fffffe0007520518:      	cmp	w8, #3
fffffe000752051c:      	cset	w0, lo
fffffe0007520520:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe0007520524:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520528:      	lsl	w8, w19, #4
fffffe000752052c:      	adrp	x0, 0xfffffe0007c5f000 <_njs_fatal_owner+0x10>
fffffe0007520530:      	add	x0, x0, #1624
fffffe0007520534:      	add	x23, x20, x8
fffffe0007520538:      	mov	x1, x23
fffffe000752053c:      	mov	w2, #16
fffffe0007520540:      	mov	w3, wzr
fffffe0007520544:      	bl	0xfffffe000751688c <_nwfn_memory_range>
fffffe0007520548:      	cbnz	w0, 0xfffffe0007520568 <_nss_owner_loop+0x20b4>
fffffe000752054c:      	adrp	x0, 0xfffffe0007c5f000 <_njs_fatal_owner+0x30>
fffffe0007520550:      	add	x0, x0, #1624
fffffe0007520554:      	mov	x1, x23
fffffe0007520558:      	mov	w2, #16
fffffe000752055c:      	mov	w3, #1
fffffe0007520560:      	bl	0xfffffe000751688c <_nwfn_memory_range>
fffffe0007520564:      	cbz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520568:      	dsb	sy
fffffe000752056c:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x3e4>
fffffe0007520570:      	add	x0, x0, #1592
fffffe0007520574:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520578:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752057c:      	adrp	x8, 0xfffffe0007c60000 <_njs_fatal_owner+0x64>
fffffe0007520580:      	ldr	w8, [x8, #956]
fffffe0007520584:      	sub	w8, w8, #4
fffffe0007520588:      	cmp	w8, #3
fffffe000752058c:      	cset	w0, lo
fffffe0007520590:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe0007520594:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520598:      	adrp	x0, 0xfffffe0007c5f000 <_njs_fatal_owner+0x7c>
fffffe000752059c:      	add	x0, x0, #1624
fffffe00075205a0:      	mov	x1, x23
fffffe00075205a4:      	mov	w2, #16
fffffe00075205a8:      	mov	w3, wzr
fffffe00075205ac:      	bl	0xfffffe000751688c <_nwfn_memory_range>
fffffe00075205b0:      	cbz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075205b4:      	adrp	x9, 0xfffffe0007c60000 <_njs_fatal_owner+0x9c>
fffffe00075205b8:      	add	x9, x9, #592
fffffe00075205bc:      	ldr	x8, [x9, #312]
fffffe00075205c0:      	ldr	x9, [x9]
fffffe00075205c4:      	add	x8, x8, x23
fffffe00075205c8:      	sub	x8, x8, x9
fffffe00075205cc:      	ldrb	wzr, [x8]
fffffe00075205d0:      	ldrb	wzr, [x8, #1]
fffffe00075205d4:      	ldrb	wzr, [x8, #2]
fffffe00075205d8:      	ldrb	wzr, [x8, #3]
fffffe00075205dc:      	ldrb	wzr, [x8, #4]
fffffe00075205e0:      	ldrb	wzr, [x8, #5]
fffffe00075205e4:      	ldrb	wzr, [x8, #6]
fffffe00075205e8:      	ldrsb	w9, [x8, #7]
fffffe00075205ec:      	ldrb	wzr, [x8, #8]
fffffe00075205f0:      	ldrb	wzr, [x8, #9]
fffffe00075205f4:      	ldrb	wzr, [x8, #10]
fffffe00075205f8:      	ldrb	wzr, [x8, #11]
fffffe00075205fc:      	ldrb	wzr, [x8, #12]
fffffe0007520600:      	ldrb	wzr, [x8, #13]
fffffe0007520604:      	ldrb	wzr, [x8, #14]
fffffe0007520608:      	ldrb	wzr, [x8, #15]
fffffe000752060c:      	tbz	w9, #31, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520610:      	mov	w8, #10
fffffe0007520614:      	adrp	x9, 0xfffffe0008516000 <_nbc_run+0x450>
fffffe0007520618:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x490>
fffffe000752061c:      	add	x0, x0, #1592
fffffe0007520620:      	str	w8, [x9, #2112]
fffffe0007520624:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520628:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752062c:      	mov	w1, #21256
fffffe0007520630:      	sub	x2, x29, #192
fffffe0007520634:      	adrp	x0, 0xfffffe0007c5f000 <_njs_fatal_owner+0x118>
fffffe0007520638:      	add	x0, x0, #1624
fffffe000752063c:      	movk	w1, #6146, lsl #16
fffffe0007520640:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe0007520644:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520648:      	adrp	x9, 0xfffffe0008519000 <_nbc_run+0x490>
fffffe000752064c:      	ldur	w8, [x29, #-192]
fffffe0007520650:      	ldr	w9, [x9, #48]
fffffe0007520654:      	cmp	w8, w9
fffffe0007520658:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752065c:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x4d4>
fffffe0007520660:      	add	x0, x0, #1592
fffffe0007520664:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520668:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752066c:      	mov	w8, #21256
fffffe0007520670:      	sub	x2, x29, #192
fffffe0007520674:      	movk	w8, #6146, lsl #16
fffffe0007520678:      	adrp	x0, 0xfffffe0007c5f000 <_njs_fatal_owner+0x15c>
fffffe000752067c:      	add	x0, x0, #1624
fffffe0007520680:      	add	w1, w8, #4
fffffe0007520684:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe0007520688:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe000752068c:      	adrp	x8, 0xfffffe0008519000 <_nbc_run+0x4d4>
fffffe0007520690:      	ldur	w19, [x29, #-192]
fffffe0007520694:      	ldr	w8, [x8, #48]
fffffe0007520698:      	cmp	w19, w8
fffffe000752069c:      	b.ne	0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075206a0:      	adrp	x8, 0xfffffe0008519000 <_nbc_run+0x4e8>
fffffe00075206a4:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x51c>
fffffe00075206a8:      	add	x0, x0, #1592
fffffe00075206ac:      	ldr	x20, [x8, #40]
fffffe00075206b0:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe00075206b4:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075206b8:      	adrp	x8, 0xfffffe0007c60000 <_njs_event+0x14>
fffffe00075206bc:      	ldr	w8, [x8, #956]
fffffe00075206c0:      	sub	w8, w8, #4
fffffe00075206c4:      	cmp	w8, #3
fffffe00075206c8:      	cset	w0, lo
fffffe00075206cc:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe00075206d0:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075206d4:      	lsl	w8, w19, #4
fffffe00075206d8:      	adrp	x0, 0xfffffe0007c5f000 <_njs_event+0x30>
fffffe00075206dc:      	add	x0, x0, #1624
fffffe00075206e0:      	add	x23, x20, x8
fffffe00075206e4:      	mov	x1, x23
fffffe00075206e8:      	mov	w2, #16
fffffe00075206ec:      	mov	w3, wzr
fffffe00075206f0:      	bl	0xfffffe000751688c <_nwfn_memory_range>
fffffe00075206f4:      	cbnz	w0, 0xfffffe0007520714 <_nss_owner_loop+0x2260>
fffffe00075206f8:      	adrp	x0, 0xfffffe0007c5f000 <_njs_event+0x50>
fffffe00075206fc:      	add	x0, x0, #1624
fffffe0007520700:      	mov	x1, x23
fffffe0007520704:      	mov	w2, #16
fffffe0007520708:      	mov	w3, #1
fffffe000752070c:      	bl	0xfffffe000751688c <_nwfn_memory_range>
fffffe0007520710:      	cbz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520714:      	dsb	sy
fffffe0007520718:      	adrp	x0, 0xfffffe0008525000 <_nbc_run+0x590>
fffffe000752071c:      	add	x0, x0, #1592
fffffe0007520720:      	bl	0xfffffe000751631c <_ncd_native_owner>
fffffe0007520724:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520728:      	adrp	x8, 0xfffffe0007c60000 <_njs_event+0x84>
fffffe000752072c:      	ldr	w8, [x8, #956]
fffffe0007520730:      	sub	w8, w8, #4
fffffe0007520734:      	cmp	w8, #3
fffffe0007520738:      	cset	w0, lo
fffffe000752073c:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe0007520740:      	cbnz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520744:      	adrp	x0, 0xfffffe0007c5f000 <_njs_event+0x9c>
fffffe0007520748:      	add	x0, x0, #1624
fffffe000752074c:      	mov	x1, x23
fffffe0007520750:      	mov	w2, #16
fffffe0007520754:      	mov	w3, wzr
fffffe0007520758:      	bl	0xfffffe000751688c <_nwfn_memory_range>
fffffe000752075c:      	cbz	w0, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe0007520760:      	adrp	x9, 0xfffffe0007c60000 <_njs_event+0xbc>
fffffe0007520764:      	add	x9, x9, #592
fffffe0007520768:      	ldr	x8, [x9, #312]
fffffe000752076c:      	ldr	x9, [x9]
fffffe0007520770:      	add	x8, x8, x23
fffffe0007520774:      	sub	x8, x8, x9
fffffe0007520778:      	ldrb	wzr, [x8]
fffffe000752077c:      	ldrb	wzr, [x8, #1]
fffffe0007520780:      	ldrb	wzr, [x8, #2]
fffffe0007520784:      	ldrb	wzr, [x8, #3]
fffffe0007520788:      	ldrb	wzr, [x8, #4]
fffffe000752078c:      	ldrb	wzr, [x8, #5]
fffffe0007520790:      	ldrb	wzr, [x8, #6]
fffffe0007520794:      	ldrsb	w9, [x8, #7]
fffffe0007520798:      	ldrb	wzr, [x8, #8]
fffffe000752079c:      	ldrb	wzr, [x8, #9]
fffffe00075207a0:      	ldrb	wzr, [x8, #10]
fffffe00075207a4:      	ldrb	wzr, [x8, #11]
fffffe00075207a8:      	ldrb	wzr, [x8, #12]
fffffe00075207ac:      	ldrb	wzr, [x8, #13]
fffffe00075207b0:      	ldrb	wzr, [x8, #14]
fffffe00075207b4:      	ldrb	wzr, [x8, #15]
fffffe00075207b8:      	tbz	w9, #31, 0xfffffe0007521cb8 <_nss_owner_loop+0x3804>
fffffe00075207bc:      	adrp	x19, 0xfffffe0007c5f000 <_njs_event+0x114>
fffffe00075207c0:      	add	x19, x19, #1624
fffffe00075207c4:      	adrp	x22, 0xfffffe0008516000 <_nbc_run+0x600>
fffffe00075207c8:      	add	x22, x22, #2108
fffffe00075207cc:      	adrp	x10, 0xfffffe0008524000 <_nbc_run+0x640>
fffffe00075207d0:      	add	x10, x10, #1216
fffffe00075207d4:      	adrp	x23, 0xfffffe0008528000 <_nbc_run+0x658>
fffffe00075207d8:      	add	x23, x23, #3232
fffffe00075207dc:      	adrp	x11, 0xfffffe0008525000 <_nbc_run+0x654>
fffffe00075207e0:      	add	x11, x11, #1592
fffffe00075207e4:      	ldr	w8, [x19, #1928]
fffffe00075207e8:      	adrp	x12, 0xfffffe0007c9c000 <_njs_event+0x234>
fffffe00075207ec:      	mov	w20, #1
fffffe00075207f0:      	stp	w24, w26, [x22]
fffffe00075207f4:      	str	x10, [x23]
fffffe00075207f8:      	ldr	w9, [x19, #2872]
fffffe00075207fc:      	stp	x11, x19, [x23, #8]
fffffe0007520800:      	ldr	w10, [x10, #432]
fffffe0007520804:      	ldr	w11, [x22, #28]
fffffe0007520808:      	mov	w1, #7808
fffffe000752080c:      	ldur	q0, [x22, #204]
fffffe0007520810:      	movk	w1, #1, lsl #16
fffffe0007520814:      	add	x12, x12, #648
fffffe0007520818:      	stp	w20, w8, [x23, #32]
fffffe000752081c:      	ldur	q1, [x22, #252]
fffffe0007520820:      	add	x8, x22, #348
fffffe0007520824:      	ldur	q2, [x22, #236]
fffffe0007520828:      	stp	w9, w10, [x23, #40]
fffffe000752082c:      	ldur	q3, [x22, #220]
fffffe0007520830:      	add	x9, x22, #332
fffffe0007520834:      	add	x10, x22, #316
fffffe0007520838:      	str	x12, [x23, #24]
fffffe000752083c:      	stp	q2, q1, [x23, #96]
fffffe0007520840:      	ldr	q1, [x8]
fffffe0007520844:      	add	x8, x22, #300
fffffe0007520848:      	add	x12, x22, #268
fffffe000752084c:      	ldr	q2, [x9]
fffffe0007520850:      	stp	q0, q3, [x23, #64]
fffffe0007520854:      	ldr	q0, [x10]
fffffe0007520858:      	add	x9, x22, #364
fffffe000752085c:      	ldr	q3, [x8]
fffffe0007520860:      	add	x8, x22, #284
fffffe0007520864:      	stp	q2, q1, [x23, #192]
fffffe0007520868:      	add	x10, x22, #428
fffffe000752086c:      	ldr	q2, [x9]
fffffe0007520870:      	add	x9, x22, #396
fffffe0007520874:      	stp	q3, q0, [x23, #160]
fffffe0007520878:      	ldr	q0, [x12]
fffffe000752087c:      	ldr	q1, [x8]
fffffe0007520880:      	add	x8, x22, #412
fffffe0007520884:      	ldr	q3, [x10]
fffffe0007520888:      	add	x10, x22, #492
fffffe000752088c:      	str	w11, [x23, #48]
fffffe0007520890:      	adrp	x11, 0xfffffe0008516000 <_rr+0x58>
fffffe0007520894:      	add	x11, x11, #1984
fffffe0007520898:      	stp	q0, q1, [x23, #128]
fffffe000752089c:      	ldr	q0, [x8]
fffffe00075208a0:      	add	x8, x22, #380
fffffe00075208a4:      	ldr	q1, [x9]
fffffe00075208a8:      	add	x9, x22, #508
fffffe00075208ac:      	ldr	q4, [x10]
fffffe00075208b0:      	add	x10, x22, #444
fffffe00075208b4:      	ldr	x0, [sp, #104]
fffffe00075208b8:      	str	xzr, [x11, #16]
fffffe00075208bc:      	stp	q1, q0, [x23, #256]
fffffe00075208c0:      	ldr	q0, [x8]
fffffe00075208c4:      	ldr	q1, [x9]
fffffe00075208c8:      	add	x8, x22, #476
fffffe00075208cc:      	add	x9, x22, #460
fffffe00075208d0:      	stur	d10, [x11, #52]
fffffe00075208d4:      	stp	q2, q0, [x23, #224]
fffffe00075208d8:      	ldr	q0, [x8]
fffffe00075208dc:      	stp	q4, q1, [x23, #352]
fffffe00075208e0:      	ldr	q2, [x9]
fffffe00075208e4:      	add	x8, x22, #588
fffffe00075208e8:      	ldr	q1, [x10]
fffffe00075208ec:      	add	x9, x22, #572
fffffe00075208f0:      	add	x10, x22, #556
fffffe00075208f4:      	stp	q2, q0, [x23, #320]
fffffe00075208f8:      	ldr	q0, [x8]
fffffe00075208fc:      	stp	q3, q1, [x23, #288]
fffffe0007520900:      	add	x8, x22, #524
fffffe0007520904:      	ldr	q1, [x9]
fffffe0007520908:      	add	x9, x22, #540
fffffe000752090c:      	ldr	q2, [x10]
fffffe0007520910:      	ldrb	w10, [x19, #2884]
fffffe0007520914:      	stp	q1, q0, [x23, #432]
fffffe0007520918:      	ldr	q0, [x8]
fffffe000752091c:      	ldr	q1, [x9]
fffffe0007520920:      	strb	w10, [x23, #52]
fffffe0007520924:      	mov	w10, #10228
fffffe0007520928:      	str	q0, [x23, #384]
fffffe000752092c:      	ldur	x8, [x22, #124]
fffffe0007520930:      	ldur	x9, [x22, #140]
fffffe0007520934:      	stp	q1, q2, [x23, #400]
fffffe0007520938:      	ldr	d0, [x22, x10]
fffffe000752093c:      	ldr	w10, [x22, #68]
fffffe0007520940:      	str	x8, [x23, #56]
fffffe0007520944:      	stp	x8, x9, [x11]
fffffe0007520948:      	str	d0, [x11, #40]
fffffe000752094c:      	str	w10, [x11, #48]
fffffe0007520950:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe0007520954:      	mov	w12, #55664
fffffe0007520958:      	ldur	x8, [x22, #124]
fffffe000752095c:      	ldrb	w9, [x19, #2884]
fffffe0007520960:      	adrp	x13, 0xfffffe00084f8000 <_rr+0xb0>
fffffe0007520964:      	add	x13, x13, #588
fffffe0007520968:      	ldr	w10, [x19, #1928]
fffffe000752096c:      	ldr	w11, [x19, #2872]
fffffe0007520970:      	str	wzr, [x22, x12]
fffffe0007520974:      	ldr	w12, [x19, #2876]
fffffe0007520978:      	adrp	x0, 0xfffffe000709a000 <_nss_owner_loop+0x12ac>
fffffe000752097c:      	add	x0, x0, #1624
fffffe0007520980:      	str	wzr, [x22, #1704]
fffffe0007520984:      	str	w20, [x23, #468]
fffffe0007520988:      	str	d11, [x13]
fffffe000752098c:      	stp	x9, x12, [sp, #24]
fffffe0007520990:      	stp	x10, x11, [sp, #8]
fffffe0007520994:      	str	x8, [sp]
fffffe0007520998:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000752099c:      	ldr	x0, [x27, #2456]
fffffe00075209a0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe00075209a4:      	bl	0xfffffe0007521fd8 <_njs_rejoin_refresh>
fffffe00075209a8:      	b	0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe00075209ac:      	adrp	x0, 0xfffffe00084dd000 <_rr+0x90>
fffffe00075209b0:      	add	x0, x0, #2456
fffffe00075209b4:      	mov	w1, wzr
fffffe00075209b8:      	bl	0xfffffe000736c68c <_assert_wait>
fffffe00075209bc:      	ldr	x0, [x27, #2456]
fffffe00075209c0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe00075209c4:      	mov	x0, xzr
fffffe00075209c8:      	bl	0xfffffe000736d140 <_thread_block>
fffffe00075209cc:      	b	0xfffffe000751e834 <_nss_owner_loop+0x380>
fffffe00075209d0:      	sub	w8, w19, #5
fffffe00075209d4:      	cmp	w8, #1
fffffe00075209d8:      	b.hi	0xfffffe0007520b7c <_nss_owner_loop+0x26c8>
fffffe00075209dc:      	ldr	w8, [x9]
fffffe00075209e0:      	mov	w24, #3
fffffe00075209e4:      	adrp	x10, 0xfffffe0008516000 <_coherent+0xf8>
fffffe00075209e8:      	add	x10, x10, #2096
fffffe00075209ec:      	cbz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe00075209f0:      	ldr	w8, [x10, #4]
fffffe00075209f4:      	cbz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe00075209f8:      	ldr	w8, [x10]
fffffe00075209fc:      	cbnz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe0007520a00:      	ldr	w8, [x9, #8]
fffffe0007520a04:      	cbnz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe0007520a08:      	ldr	w8, [x9, #4]
fffffe0007520a0c:      	cbnz	w8, 0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe0007520a10:      	cmp	w19, #6
fffffe0007520a14:      	mov	w24, wzr
fffffe0007520a18:      	cset	w8, eq
fffffe0007520a1c:      	adrp	x9, 0xfffffe0008516000 <_coherent+0x130>
fffffe0007520a20:      	str	w8, [x9, #2180]
fffffe0007520a24:      	b	0xfffffe0007520cec <_nss_owner_loop+0x2838>
fffffe0007520a28:      	mov	w8, #1
fffffe0007520a2c:      	adrp	x9, 0xfffffe0008531000 <_sy+0x64>
fffffe0007520a30:      	add	x9, x9, #152
fffffe0007520a34:      	sub	x2, x29, #168
fffffe0007520a38:      	adrp	x0, 0xfffffe000721b000 <_nss_owner_loop+0x1970>
fffffe0007520a3c:      	add	x0, x0, #1232
fffffe0007520a40:      	str	w8, [x28, #8740]
fffffe0007520a44:      	mov	w8, #5
fffffe0007520a48:      	adrp	x1, 0xfffffe0008525000 <_sy+0x50>
fffffe0007520a4c:      	add	x1, x1, #1592
fffffe0007520a50:      	mov	x3, x28
fffffe0007520a54:      	stlr	w8, [x9]
fffffe0007520a58:      	adrp	x9, 0xfffffe000799f000 <_nss_owner_loop+0x37a0>
fffffe0007520a5c:      	add	x8, sp, #608
fffffe0007520a60:      	add	x10, x9, #376
fffffe0007520a64:      	ldr	x9, [sp, #240]
fffffe0007520a68:      	stp	x10, x9, [x8, #296]
fffffe0007520a6c:      	adr	x10, #-10528
fffffe0007520a70:      	nop
fffffe0007520a74:      	adrp	x9, 0xfffffe0008528000 <_sy+0x88>
fffffe0007520a78:      	add	x9, x9, #3744
fffffe0007520a7c:      	stp	x10, x9, [x8, #312]
fffffe0007520a80:      	bl	0xfffffe000799d464 <_nct_run>
fffffe0007520a84:      	cmp	w0, #1
fffffe0007520a88:      	str	wzr, [x28, #8740]
fffffe0007520a8c:      	b.ne	0xfffffe0007520ba0 <_nss_owner_loop+0x26ec>
fffffe0007520a90:      	ldr	w8, [x28, #4572]
fffffe0007520a94:      	cbz	w8, 0xfffffe0007520ba0 <_nss_owner_loop+0x26ec>
fffffe0007520a98:      	cbz	w19, 0xfffffe0007520b14 <_nss_owner_loop+0x2660>
fffffe0007520a9c:      	adrp	x9, 0xfffffe000852a000 <_sy+0xb8>
fffffe0007520aa0:      	add	x9, x9, #3824
fffffe0007520aa4:      	adrp	x8, 0xfffffe0008528000 <_sy+0xb8>
fffffe0007520aa8:      	add	x8, x8, #3296
fffffe0007520aac:      	ldp	q0, q1, [x9, #32]
fffffe0007520ab0:      	ldp	q3, q2, [x9]
fffffe0007520ab4:      	stp	q0, q1, [x8, #32]
fffffe0007520ab8:      	ldp	q0, q1, [x9, #96]
fffffe0007520abc:      	stp	q3, q2, [x8]
fffffe0007520ac0:      	ldp	q2, q3, [x9, #128]
fffffe0007520ac4:      	stp	q0, q1, [x8, #96]
fffffe0007520ac8:      	ldp	q0, q1, [x9, #64]
fffffe0007520acc:      	stp	q2, q3, [x8, #128]
fffffe0007520ad0:      	ldp	q3, q2, [x9, #192]
fffffe0007520ad4:      	stp	q0, q1, [x8, #64]
fffffe0007520ad8:      	ldp	q0, q1, [x9, #160]
fffffe0007520adc:      	stp	q3, q2, [x8, #192]
fffffe0007520ae0:      	ldp	q3, q2, [x9, #288]
fffffe0007520ae4:      	stp	q0, q1, [x8, #160]
fffffe0007520ae8:      	ldp	q1, q0, [x9, #256]
fffffe0007520aec:      	stp	q3, q2, [x8, #288]
fffffe0007520af0:      	ldp	q2, q3, [x9, #224]
fffffe0007520af4:      	stp	q1, q0, [x8, #256]
fffffe0007520af8:      	ldp	q1, q0, [x9, #368]
fffffe0007520afc:      	stp	q2, q3, [x8, #224]
fffffe0007520b00:      	stp	q1, q0, [x8, #368]
fffffe0007520b04:      	ldr	q3, [x9, #320]
fffffe0007520b08:      	ldp	q0, q2, [x9, #336]
fffffe0007520b0c:      	str	q3, [x8, #320]
fffffe0007520b10:      	stp	q0, q2, [x8, #336]
fffffe0007520b14:      	adrp	x19, 0xfffffe0008526000 <_mr+0x64>
fffffe0007520b18:      	add	x19, x19, #2680
fffffe0007520b1c:      	mov	x0, x19
fffffe0007520b20:      	mov	x1, x20
fffffe0007520b24:      	mov	w2, #8736
fffffe0007520b28:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe0007520b2c:      	adrp	x8, 0xfffffe000852a000 <_mr+0x8c>
fffffe0007520b30:      	add	x8, x8, #3688
fffffe0007520b34:      	mov	w23, #3
fffffe0007520b38:      	str	x19, [x8]
fffffe0007520b3c:      	str	xzr, [x8, #16]
fffffe0007520b40:      	b	0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe0007520b44:      	cmp	w19, #2
fffffe0007520b48:      	mov	w8, #1
fffffe0007520b4c:      	cinc	w8, w8, ne
fffffe0007520b50:      	cmp	w19, #4
fffffe0007520b54:      	csel	w8, wzr, w8, eq
fffffe0007520b58:      	adrp	x9, 0xfffffe0008531000 <_mw+0x18>
fffffe0007520b5c:      	adrp	x11, 0xfffffe0008531000 <_mw+0x1c>
fffffe0007520b60:      	adrp	x12, 0xfffffe0008516000 <_mr+0x70>
fffffe0007520b64:      	add	x12, x12, #2096
fffffe0007520b68:      	str	w8, [x9, #156]
fffffe0007520b6c:      	str	wzr, [x11, #164]
fffffe0007520b70:      	b.ne	0xfffffe0007520cb0 <_nss_owner_loop+0x27fc>
fffffe0007520b74:      	mov	w1, wzr
fffffe0007520b78:      	b	0xfffffe0007520cdc <_nss_owner_loop+0x2828>
fffffe0007520b7c:      	ldr	x0, [x27, #2456]
fffffe0007520b80:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007520b84:      	cmp	w19, #7
fffffe0007520b88:      	b.ne	0xfffffe0007520ff0 <_nss_owner_loop+0x2b3c>
fffffe0007520b8c:      	bl	0xfffffe0007512730 <_nord2_wifi_join_disconnect>
fffffe0007520b90:      	cmp	w0, #0
fffffe0007520b94:      	mov	w8, #3
fffffe0007520b98:      	csel	w24, wzr, w8, eq
fffffe0007520b9c:      	b	0xfffffe0007520cf4 <_nss_owner_loop+0x2840>
fffffe0007520ba0:      	mov	w9, #1
fffffe0007520ba4:      	cbz	w19, 0xfffffe0007520bb0 <_nss_owner_loop+0x26fc>
fffffe0007520ba8:      	adrp	x8, 0xfffffe0008528000 <_mw+0x44>
fffffe0007520bac:      	str	w9, [x8, #3704]
fffffe0007520bb0:      	adrp	x8, 0xfffffe00084dd000 <_sy+0x98>
fffffe0007520bb4:      	strb	w22, [x8, #2480]
fffffe0007520bb8:      	tbnz	w0, #31, 0xfffffe0007521b48 <_nss_owner_loop+0x3694>
fffffe0007520bbc:      	adrp	x8, 0xfffffe000852a000 <_mw+0x60>
fffffe0007520bc0:      	add	x8, x8, #3788
fffffe0007520bc4:      	ldr	w8, [x8, #1756]
fffffe0007520bc8:      	cbnz	w8, 0xfffffe0007520c00 <_nss_owner_loop+0x274c>
fffffe0007520bcc:      	adrp	x8, 0xfffffe000852a000 <_mw+0x70>
fffffe0007520bd0:      	nop
fffffe0007520bd4:      	ldr	w8, [x8, #3788]
fffffe0007520bd8:      	cbz	w8, 0xfffffe0007520c00 <_nss_owner_loop+0x274c>
fffffe0007520bdc:      	adrp	x8, 0xfffffe0008526000 <_mw+0x70>
fffffe0007520be0:      	adrp	x10, 0xfffffe000852b000 <_mw+0x88>
fffffe0007520be4:      	add	x10, x10, #1444
fffffe0007520be8:      	ldr	w8, [x8, #2772]
fffffe0007520bec:      	str	wzr, [x10]
fffffe0007520bf0:      	str	w9, [x10, #12]
fffffe0007520bf4:      	cbz	w8, 0xfffffe0007520c00 <_nss_owner_loop+0x274c>
fffffe0007520bf8:      	adrp	x8, 0xfffffe000852b000 <_mw+0xa0>
fffffe0007520bfc:      	str	w9, [x8, #1452]
fffffe0007520c00:      	adrp	x10, 0xfffffe0008526000 <_mw+0x94>
fffffe0007520c04:      	add	x10, x10, #2648
fffffe0007520c08:      	add	x9, sp, #608
fffffe0007520c0c:      	ldr	w8, [x10, #12]
fffffe0007520c10:      	str	xzr, [x9, #272]
fffffe0007520c14:      	str	xzr, [x9, #328]
fffffe0007520c18:      	cbz	w8, 0xfffffe0007521b68 <_nss_owner_loop+0x36b4>
fffffe0007520c1c:      	ldr	w8, [x10, #20]
fffffe0007520c20:      	cbnz	w8, 0xfffffe0007521b68 <_nss_owner_loop+0x36b4>
fffffe0007520c24:      	ldr	x8, [x10]
fffffe0007520c28:      	cmp	x8, x26
fffffe0007520c2c:      	b.ne	0xfffffe0007521b68 <_nss_owner_loop+0x36b4>
fffffe0007520c30:      	sub	x0, x29, #192
fffffe0007520c34:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe0007520c38:      	add	x19, sp, #608
fffffe0007520c3c:      	sub	x1, x29, #136
fffffe0007520c40:      	ldr	x0, [x19, #272]
fffffe0007520c44:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe0007520c48:      	ldr	x8, [x19, #328]
fffffe0007520c4c:      	mov	x9, #63439
fffffe0007520c50:      	movk	x9, #58195, lsl #16
fffffe0007520c54:      	movk	x9, #39845, lsl #32
fffffe0007520c58:      	lsr	x8, x8, #3
fffffe0007520c5c:      	movk	x9, #8388, lsl #48
fffffe0007520c60:      	umulh	x8, x8, x9
fffffe0007520c64:      	adrp	x9, 0xfffffe000852b000 <_rw+0x48>
fffffe0007520c68:      	lsr	x19, x8, #4
fffffe0007520c6c:      	ldr	x9, [x9, #1488]
fffffe0007520c70:      	cmp	x19, x9
fffffe0007520c74:      	b.lo	0xfffffe0007521b68 <_nss_owner_loop+0x36b4>
fffffe0007520c78:      	add	x0, x24, #144
fffffe0007520c7c:      	adrp	x1, 0xfffffe000852b000 <_rw+0x60>
fffffe0007520c80:      	add	x1, x1, #1440
fffffe0007520c84:      	mov	w2, #18336
fffffe0007520c88:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe0007520c8c:      	ldr	w8, [x24, #152]
fffffe0007520c90:      	str	x19, [x24, #32]
fffffe0007520c94:      	cbz	w8, 0xfffffe0007520ca8 <_nss_owner_loop+0x27f4>
fffffe0007520c98:      	ldr	w8, [x24, #160]
fffffe0007520c9c:      	cbnz	w8, 0xfffffe0007520ca8 <_nss_owner_loop+0x27f4>
fffffe0007520ca0:      	ldr	w8, [x24, #156]
fffffe0007520ca4:      	cbz	w8, 0xfffffe0007521020 <_nss_owner_loop+0x2b6c>
fffffe0007520ca8:      	mov	w23, #1
fffffe0007520cac:      	b	0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe0007520cb0:      	adrp	x9, 0xfffffe00084f8000 <_mw+0x8c>
fffffe0007520cb4:      	add	w2, w2, #1
fffffe0007520cb8:      	ldr	w9, [x9, #556]
fffffe0007520cbc:      	str	w2, [x26, #160]
fffffe0007520cc0:      	cmp	w9, #0
fffffe0007520cc4:      	ldp	w9, w10, [x12]
fffffe0007520cc8:      	ccmp	w10, #0, #4, ne
fffffe0007520ccc:      	ccmp	w9, #0, #0, ne
fffffe0007520cd0:      	cset	w9, ne
fffffe0007520cd4:      	csel	w1, wzr, w8, ne
fffffe0007520cd8:      	str	w9, [x11, #164]
fffffe0007520cdc:      	adrp	x0, 0xfffffe0008518000 <_rw+0x74>
fffffe0007520ce0:      	add	x0, x0, #3664
fffffe0007520ce4:      	bl	0xfffffe00079b3c9c <_nsdd_capture>
fffffe0007520ce8:      	mov	w24, wzr
fffffe0007520cec:      	ldr	x0, [x27, #2456]
fffffe0007520cf0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007520cf4:      	adrp	x23, 0xfffffe00084dd000 <_mw+0x64>
fffffe0007520cf8:      	add	x23, x23, #2456
fffffe0007520cfc:      	ldr	x0, [x23]
fffffe0007520d00:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007520d04:      	movi.2d	v0, #0000000000000000
fffffe0007520d08:      	add	x20, sp, #608
fffffe0007520d0c:      	sub	x0, x29, #192
fffffe0007520d10:      	ldr	x19, [x21, #208]
fffffe0007520d14:      	str	xzr, [x20, #272]
fffffe0007520d18:      	stp	q0, q0, [x20, #128]
fffffe0007520d1c:      	stp	q0, q0, [x20, #160]
fffffe0007520d20:      	stp	q0, q0, [x20, #192]
fffffe0007520d24:      	stp	q0, q0, [x20, #224]
fffffe0007520d28:      	stp	q0, q0, [sp, #608]
fffffe0007520d2c:      	stp	q0, q0, [sp, #640]
fffffe0007520d30:      	stp	q0, q0, [sp, #672]
fffffe0007520d34:      	stp	q0, q0, [sp, #704]
fffffe0007520d38:      	str	xzr, [x20, #328]
fffffe0007520d3c:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe0007520d40:      	ldr	x0, [x20, #272]
fffffe0007520d44:      	sub	x1, x29, #136
fffffe0007520d48:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe0007520d4c:      	adrp	x7, 0xfffffe0008516000 <_drain+0x4>
fffffe0007520d50:      	add	x7, x7, #2112
fffffe0007520d54:      	ldr	w9, [x21, #52]
fffffe0007520d58:      	lsr	x12, x19, #32
fffffe0007520d5c:      	ldr	x14, [x20, #328]
fffffe0007520d60:      	str	w19, [sp, #644]
fffffe0007520d64:      	ldp	w10, w8, [x7]
fffffe0007520d68:      	ldr	w17, [x21, #3288]
fffffe0007520d6c:      	str	w12, [sp, #648]
fffffe0007520d70:      	ldr	w2, [x21, #40]
fffffe0007520d74:      	ldr	w3, [x23, #12]
fffffe0007520d78:      	cmp	w8, #0
fffffe0007520d7c:      	ldr	w8, [x21, #3280]
fffffe0007520d80:      	cset	w11, ne
fffffe0007520d84:      	cmp	w9, #0
fffffe0007520d88:      	ldr	w9, [x21, #3284]
fffffe0007520d8c:      	cset	w13, ne
fffffe0007520d90:      	cmp	w8, #0
fffffe0007520d94:      	mov	w8, #16960
fffffe0007520d98:      	movk	w8, #15, lsl #16
fffffe0007520d9c:      	cset	w15, ne
fffffe0007520da0:      	cmp	w9, #0
fffffe0007520da4:      	ldr	w9, [x7, #1488]
fffffe0007520da8:      	udiv	x14, x14, x8
fffffe0007520dac:      	ldr	w8, [x21, #28]
fffffe0007520db0:      	cset	w16, ne
fffffe0007520db4:      	cmp	w17, #0
fffffe0007520db8:      	cset	w17, ne
fffffe0007520dbc:      	cmp	w9, #0
fffffe0007520dc0:      	ldp	w5, w4, [x21, #8]
fffffe0007520dc4:      	cset	w0, ne
fffffe0007520dc8:      	cmp	w8, #0
fffffe0007520dcc:      	ldr	d0, [x21, #64]
fffffe0007520dd0:      	cset	w1, ne
fffffe0007520dd4:      	ldur	d1, [x7, #52]
fffffe0007520dd8:      	cmp	w2, #0
fffffe0007520ddc:      	ldr	q2, [x7, #9744]
fffffe0007520de0:      	cset	w2, ne
fffffe0007520de4:      	cset	w9, eq
fffffe0007520de8:      	cmp	w3, #0
fffffe0007520dec:      	cset	w3, ne
fffffe0007520df0:      	cset	w12, eq
fffffe0007520df4:      	cmp	w4, #0
fffffe0007520df8:      	str	w10, [sp, #656]
fffffe0007520dfc:      	mov.d	v0[1], v1[0]
fffffe0007520e00:      	cset	w6, ne
fffffe0007520e04:      	rev64.4s	v1, v2
fffffe0007520e08:      	bfi	w6, w13, #1, #1
fffffe0007520e0c:      	ldr	w13, [x21, #3272]
fffffe0007520e10:      	bfi	w6, w15, #2, #1
fffffe0007520e14:      	bfi	w6, w16, #3, #1
fffffe0007520e18:      	lsr	x4, x14, #32
fffffe0007520e1c:      	bfi	w6, w17, #4, #1
fffffe0007520e20:      	str	w14, [sp, #756]
fffffe0007520e24:      	ext.16b	v1, v2, v1, #8
fffffe0007520e28:      	str	w13, [sp, #652]
fffffe0007520e2c:      	ldr	x13, [sp, #512]
fffffe0007520e30:      	bfi	w6, w0, #5, #1
fffffe0007520e34:      	orr	w10, w6, w1, lsl #6
fffffe0007520e38:      	ldr	w14, [x7, #10036]
fffffe0007520e3c:      	orr	w10, w10, w2, lsl #7
fffffe0007520e40:      	ldr	w15, [x7, #9916]
fffffe0007520e44:      	stp	q0, q1, [sp, #672]
fffffe0007520e48:      	ldur	d0, [x13, #212]
fffffe0007520e4c:      	ldr	w13, [x7, #8]
fffffe0007520e50:      	orr	w10, w10, w3, lsl #8
fffffe0007520e54:      	ldr	d1, [x7, #9888]
fffffe0007520e58:      	str	w11, [sp, #664]
fffffe0007520e5c:      	ldr	w11, [x7, #9932]
fffffe0007520e60:      	str	w14, [sp, #712]
fffffe0007520e64:      	str	w13, [sp, #660]
fffffe0007520e68:      	ldr	w13, [x7, #9896]
fffffe0007520e6c:      	mov.d	v0[1], v1[0]
fffffe0007520e70:      	ldr	d1, [x7, #10048]
fffffe0007520e74:      	str	w10, [sp, #668]
fffffe0007520e78:      	ldr	w10, [x7, #9952]
fffffe0007520e7c:      	add	x14, sp, #461
fffffe0007520e80:      	str	w15, [sp, #724]
fffffe0007520e84:      	ldr	w15, [x7, #10224]
fffffe0007520e88:      	str	w11, [sp, #728]
fffffe0007520e8c:      	ldr	w11, [x7, #68]
fffffe0007520e90:      	str	w13, [sp, #752]
fffffe0007520e94:      	ldr	w13, [x7, #392]
fffffe0007520e98:      	stur	d1, [x14, #255]
fffffe0007520e9c:      	ldr	w16, [x26, #160]
fffffe0007520ea0:      	str	q0, [x20, #128]
fffffe0007520ea4:      	str	w10, [sp, #732]
fffffe0007520ea8:      	ldr	d0, [x7, #16]
fffffe0007520eac:      	ldr	d1, [x7, #10240]
fffffe0007520eb0:      	str	w15, [sp, #776]
fffffe0007520eb4:      	ldr	w10, [x7, #312]
fffffe0007520eb8:      	mov	w17, #32
fffffe0007520ebc:      	ldr	w15, [x7, #10236]
fffffe0007520ec0:      	str	w11, [sp, #772]
fffffe0007520ec4:      	ldr	w11, [x7, #472]
fffffe0007520ec8:      	str	w13, [sp, #792]
fffffe0007520ecc:      	ldr	x13, [x21]
fffffe0007520ed0:      	sub	w16, w17, w16
fffffe0007520ed4:      	rev64.2s	v0, v0
fffffe0007520ed8:      	rev64.2s	v1, v1
fffffe0007520edc:      	str	w10, [sp, #788]
fffffe0007520ee0:      	cset	w10, eq
fffffe0007520ee4:      	cmp	w15, #0
fffffe0007520ee8:      	adrp	x14, 0xfffffe0008531000 <_drain+0x20c>
fffffe0007520eec:      	ldr	w17, [x7, #44]
fffffe0007520ef0:      	str	w11, [sp, #796]
fffffe0007520ef4:      	cset	w11, ne
fffffe0007520ef8:      	cmp	x13, #0
fffffe0007520efc:      	str	w16, [sp, #768]
fffffe0007520f00:      	cset	w16, ne
fffffe0007520f04:      	and	w12, w16, w12
fffffe0007520f08:      	ldr	x16, [x21, #352]
fffffe0007520f0c:      	cmp	w5, #0
fffffe0007520f10:      	ldr	w14, [x14, #156]
fffffe0007520f14:      	str	w17, [sp, #780]
fffffe0007520f18:      	and	w10, w12, w10
fffffe0007520f1c:      	mov.d	v1[1], v0[0]
fffffe0007520f20:      	cset	w12, eq
fffffe0007520f24:      	ldr	x17, [x21, #184]
fffffe0007520f28:      	and	w10, w10, w12
fffffe0007520f2c:      	cmp	x16, #0
fffffe0007520f30:      	and	w9, w10, w9
fffffe0007520f34:      	str	w14, [sp, #764]
fffffe0007520f38:      	ldr	w14, [x7, #552]
fffffe0007520f3c:      	cset	w10, ne
fffffe0007520f40:      	eor	w9, w9, #0x1
fffffe0007520f44:      	cmp	x17, #254
fffffe0007520f48:      	orr	w9, w9, w10
fffffe0007520f4c:      	cmeq.4s	v0, v1, #0
fffffe0007520f50:      	cset	w10, hi
fffffe0007520f54:      	movi.4s	v1, #1
fffffe0007520f58:      	orr	w10, w9, w10
fffffe0007520f5c:      	ldr	d2, [x7, #9760]
fffffe0007520f60:      	str	w14, [sp, #800]
fffffe0007520f64:      	ldr	w0, [x7, #232]
fffffe0007520f68:      	eor	w9, w10, #0x1
fffffe0007520f6c:      	ldr	w14, [x21, #112]
fffffe0007520f70:      	str	w4, [sp, #760]
fffffe0007520f74:      	ldr	w15, [x21, #128]
fffffe0007520f78:      	str	d2, [sp, #704]
fffffe0007520f7c:      	ldr	w13, [x21, #136]
fffffe0007520f80:      	str	w0, [sp, #784]
fffffe0007520f84:      	bic.16b	v0, v1, v0
fffffe0007520f88:      	str	w11, [sp, #804]
fffffe0007520f8c:      	stur	q0, [x20, #200]
fffffe0007520f90:      	str	w14, [sp, #824]
fffffe0007520f94:      	str	w15, [sp, #828]
fffffe0007520f98:      	str	w13, [sp, #832]
fffffe0007520f9c:      	tbnz	w10, #0, 0xfffffe0007520fb0 <_nss_owner_loop+0x2afc>
fffffe0007520fa0:      	cbz	w8, 0xfffffe0007520fb0 <_nss_owner_loop+0x2afc>
fffffe0007520fa4:      	bl	0xfffffe0007512468 <_njs_rejoin_ready_locked>
fffffe0007520fa8:      	cmp	w0, #0
fffffe0007520fac:      	cset	w9, ne
fffffe0007520fb0:      	ldr	x0, [x27, #2456]
fffffe0007520fb4:      	str	w9, [sp, #836]
fffffe0007520fb8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007520fbc:      	sub	x0, x29, #168
fffffe0007520fc0:      	add	x2, sp, #608
fffffe0007520fc4:      	mov	w1, w24
fffffe0007520fc8:      	bl	0xfffffe00079b4c30 <_nwl_complete>
fffffe0007520fcc:      	ldp	w8, w9, [x29, #-168]
fffffe0007520fd0:      	ldur	w10, [x29, #-160]
fffffe0007520fd4:      	adrp	x0, 0xfffffe000709a000 <_nss_owner_loop+0x1908>
fffffe0007520fd8:      	add	x0, x0, #1275
fffffe0007520fdc:      	stp	x24, x22, [sp, #24]
fffffe0007520fe0:      	stp	x9, x10, [sp, #8]
fffffe0007520fe4:      	str	x8, [sp]
fffffe0007520fe8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007520fec:      	b	0xfffffe000751e8ac <_nss_owner_loop+0x3f8>
fffffe0007520ff0:      	mov	w24, wzr
fffffe0007520ff4:      	b	0xfffffe0007520cf4 <_nss_owner_loop+0x2840>
fffffe0007520ff8:      	add	x12, x12, #2
fffffe0007520ffc:      	subs	x8, x8, #1
fffffe0007521000:      	b.eq	0xfffffe0007521034 <_nss_owner_loop+0x2b80>
fffffe0007521004:      	ldurb	w13, [x12, #-1]
fffffe0007521008:      	cmp	w13, w10
fffffe000752100c:      	b.ne	0xfffffe0007520ff8 <_nss_owner_loop+0x2b44>
fffffe0007521010:      	ldrb	w13, [x12]
fffffe0007521014:      	cmp	w13, w11
fffffe0007521018:      	csinc	w9, w9, wzr, ne
fffffe000752101c:      	b	0xfffffe0007520ff8 <_nss_owner_loop+0x2b44>
fffffe0007521020:      	ldr	w8, [x24, #168]
fffffe0007521024:      	cmp	w8, #0
fffffe0007521028:      	cset	w23, ne
fffffe000752102c:      	b	0xfffffe000751e828 <_nss_owner_loop+0x374>
fffffe0007521030:      	mov	w9, wzr
fffffe0007521034:      	cbz	w9, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521038:      	adrp	x8, 0xfffffe00084f8000 <_drain+0x274>
fffffe000752103c:      	add	x8, x8, #1060
fffffe0007521040:      	ldrb	w8, [x8]
fffffe0007521044:      	cbnz	w8, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521048:      	adrp	x8, 0xfffffe00084f8000 <_drain+0x284>
fffffe000752104c:      	add	x8, x8, #1060
fffffe0007521050:      	ldr	w8, [x8, #24]
fffffe0007521054:      	cbnz	w8, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521058:      	adrp	x9, 0xfffffe00084f8000 <_drain+0x294>
fffffe000752105c:      	add	x9, x9, #1053
fffffe0007521060:      	ldurh	w8, [x9, #25]
fffffe0007521064:      	ldurh	w9, [x9, #27]
fffffe0007521068:      	cmp	w8, w9
fffffe000752106c:      	b.ne	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521070:      	adrp	x8, 0xfffffe00084f8000 <_drain+0x2ac>
fffffe0007521074:      	add	x8, x8, #1053
fffffe0007521078:      	ldrb	w8, [x8]
fffffe000752107c:      	cbz	w8, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521080:      	adrp	x8, 0xfffffe00084f8000 <_drain+0x2bc>
fffffe0007521084:      	ldrb	w8, [x8, #1052]
fffffe0007521088:      	sub	w8, w8, #33
fffffe000752108c:      	cmn	w8, #32
fffffe0007521090:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521094:      	adrp	x8, 0xfffffe00084f8000 <_drain+0x2d0>
fffffe0007521098:      	add	x8, x8, #1074
fffffe000752109c:      	ldrb	w1, [x8, #1048]
fffffe00075210a0:      	sub	w8, w1, #13
fffffe00075210a4:      	cmn	w8, #12
fffffe00075210a8:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe00075210ac:      	adrp	x8, 0xfffffe00084f8000 <_drain+0x2e8>
fffffe00075210b0:      	add	x8, x8, #1074
fffffe00075210b4:      	ldrh	w8, [x8]
fffffe00075210b8:      	cbz	w8, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe00075210bc:      	sub	x2, x29, #136
fffffe00075210c0:      	sub	x3, x29, #194
fffffe00075210c4:      	sub	x4, x29, #196
fffffe00075210c8:      	adrp	x0, 0xfffffe00084f8000 <_drain+0x304>
fffffe00075210cc:      	add	x0, x0, #2123
fffffe00075210d0:      	sturh	wzr, [x29, #-136]
fffffe00075210d4:      	stur	wzr, [x29, #-196]
fffffe00075210d8:      	bl	0xfffffe00079b0a98 <_nst_legacy_rates>
fffffe00075210dc:      	cbnz	w0, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe00075210e0:      	adrp	x12, 0xfffffe00084f8000 <_drain+0x31c>
fffffe00075210e4:      	add	x12, x12, #1008
fffffe00075210e8:      	adrp	x11, 0xfffffe00084dd000 <_drain+0x2b8>
fffffe00075210ec:      	add	x11, x11, #2484
fffffe00075210f0:      	ldrb	w8, [sp, #613]
fffffe00075210f4:      	sturb	wzr, [x29, #-180]
fffffe00075210f8:      	ldrb	w9, [x12, #1113]
fffffe00075210fc:      	ldr	w10, [x11]
fffffe0007521100:      	sturb	w8, [x29, #-176]
fffffe0007521104:      	ldrb	w8, [sp, #612]
fffffe0007521108:      	sturb	w9, [x29, #-174]
fffffe000752110c:      	ldrh	w9, [x11, #4]
fffffe0007521110:      	stur	w10, [x29, #-192]
fffffe0007521114:      	ldrh	w10, [x12, #4]
fffffe0007521118:      	ldr	x11, [sp, #232]
fffffe000752111c:      	sturb	w8, [x29, #-179]
fffffe0007521120:      	sturh	w9, [x29, #-188]
fffffe0007521124:      	ldrb	w9, [x12, #1112]
fffffe0007521128:      	strh	w10, [x11, #4]
fffffe000752112c:      	ldr	w10, [x12]
fffffe0007521130:      	mov	w12, #1280
fffffe0007521134:      	cmp	w9, #1
fffffe0007521138:      	sturb	w9, [x29, #-175]
fffffe000752113c:      	str	w10, [x11]
fffffe0007521140:      	sturh	w12, [x29, #-178]
fffffe0007521144:      	b.ne	0xfffffe0007521170 <_nss_owner_loop+0x2cbc>
fffffe0007521148:      	adrp	x10, 0xfffffe00084f8000 <_drain+0x384>
fffffe000752114c:      	ldurh	w9, [x29, #-136]
fffffe0007521150:      	mov	w11, #17
fffffe0007521154:      	ldrh	w10, [x10, #1076]
fffffe0007521158:      	tst	w9, #0x3fc0
fffffe000752115c:      	and	w9, w10, #0x20
fffffe0007521160:      	mov	w10, #1041
fffffe0007521164:      	csel	w10, w11, w10, eq
fffffe0007521168:      	orr	w28, w10, w9
fffffe000752116c:      	b	0xfffffe0007521174 <_nss_owner_loop+0x2cc0>
fffffe0007521170:      	mov	w28, #17
fffffe0007521174:      	adrp	x9, 0xfffffe00084f8000 <_drain+0x3b0>
fffffe0007521178:      	add	x9, x9, #1062
fffffe000752117c:      	ldrb	w9, [x9]
fffffe0007521180:      	cbz	w9, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521184:      	adrp	x9, 0xfffffe00084f8000 <_drain+0x3c0>
fffffe0007521188:      	ldrb	w9, [x9, #1061]
fffffe000752118c:      	tbz	w9, #2, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521190:      	adrp	x9, 0xfffffe00084dd000 <_drain+0x360>
fffffe0007521194:      	add	x9, x9, #2484
fffffe0007521198:      	adrp	x11, 0xfffffe00084f8000 <_drain+0x3d4>
fffffe000752119c:      	add	x11, x11, #1067
fffffe00075211a0:      	ldrh	w3, [x9, #4]
fffffe00075211a4:      	ldr	w17, [x9]
fffffe00075211a8:      	adrp	x9, 0xfffffe00084f8000 <_drain+0x3e4>
fffffe00075211ac:      	add	x9, x9, #1008
fffffe00075211b0:      	ldrb	w10, [x11]
fffffe00075211b4:      	ldrh	w2, [x9]
fffffe00075211b8:      	cmp	w10, #255
fffffe00075211bc:      	ldur	w0, [x9, #2]
fffffe00075211c0:      	ldrb	w16, [x9, #44]
fffffe00075211c4:      	ldrb	w15, [x9, #1114]
fffffe00075211c8:      	adrp	x9, 0xfffffe00084f8000 <_drain+0x404>
fffffe00075211cc:      	add	x9, x9, #1062
fffffe00075211d0:      	ldrh	w14, [x9, #12]
fffffe00075211d4:      	ldrb	w9, [x11, #1]
fffffe00075211d8:      	b.ne	0xfffffe00075211e4 <_nss_owner_loop+0x2d30>
fffffe00075211dc:      	cmp	w9, #255
fffffe00075211e0:      	b.eq	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe00075211e4:      	cmp	w10, #255
fffffe00075211e8:      	mov	w11, #220
fffffe00075211ec:      	csel	w10, wzr, w10, eq
fffffe00075211f0:      	cmp	w9, #255
fffffe00075211f4:      	csel	w9, wzr, w9, eq
fffffe00075211f8:      	cmp	w10, w9
fffffe00075211fc:      	csel	w10, w10, w9, hi
fffffe0007521200:      	adrp	x9, 0xfffffe00084f8000 <_drain+0x43c>
fffffe0007521204:      	cmp	w10, #220
fffffe0007521208:      	csel	w1, w10, w11, lo
fffffe000752120c:      	ldrh	w9, [x9, #1078]
fffffe0007521210:      	cbz	x9, 0xfffffe0007521280 <_nss_owner_loop+0x2dcc>
fffffe0007521214:      	mov	x11, xzr
fffffe0007521218:      	mov	w23, wzr
fffffe000752121c:      	b	0xfffffe000752122c <_nss_owner_loop+0x2d78>
fffffe0007521220:      	add	x11, x11, x10
fffffe0007521224:      	cmp	x11, x9
fffffe0007521228:      	b.hs	0xfffffe0007521284 <_nss_owner_loop+0x2dd0>
fffffe000752122c:      	sub	x10, x9, x11
fffffe0007521230:      	subs	x12, x10, #2
fffffe0007521234:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521238:      	add	x10, x11, x21
fffffe000752123c:      	ldrb	w10, [x10, #553]
fffffe0007521240:      	cmp	x12, x10
fffffe0007521244:      	b.lo	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521248:      	add	x12, x21, x11
fffffe000752124c:      	add	x11, x11, #2
fffffe0007521250:      	ldrb	w12, [x12, #552]
fffffe0007521254:      	cmp	w12, #42
fffffe0007521258:      	b.ne	0xfffffe0007521220 <_nss_owner_loop+0x2d6c>
fffffe000752125c:      	cmp	w10, #1
fffffe0007521260:      	b.ne	0xfffffe0007521220 <_nss_owner_loop+0x2d6c>
fffffe0007521264:      	add	x12, x21, x11
fffffe0007521268:      	and	w13, w28, #0xffffffdf
fffffe000752126c:      	ldrb	w12, [x12, #552]
fffffe0007521270:      	tst	w12, #0x4
fffffe0007521274:      	ubfx	w23, w12, #1, #1
fffffe0007521278:      	csel	w28, w28, w13, eq
fffffe000752127c:      	b	0xfffffe0007521220 <_nss_owner_loop+0x2d6c>
fffffe0007521280:      	mov	w23, wzr
fffffe0007521284:      	ldr	x9, [sp, #224]
fffffe0007521288:      	sturb	w8, [x29, #-167]
fffffe000752128c:      	ldurh	w8, [x29, #-196]
fffffe0007521290:      	stp	x3, x2, [sp, #72]
fffffe0007521294:      	str	w1, [sp, #92]
fffffe0007521298:      	stp	xzr, xzr, [x9]
fffffe000752129c:      	mov	w9, #5
fffffe00075212a0:      	sturh	w8, [x29, #-162]
fffffe00075212a4:      	mov	w8, #8
fffffe00075212a8:      	str	x0, [sp, #96]
fffffe00075212ac:      	sturb	w9, [x29, #-168]
fffffe00075212b0:      	mov	w9, #257
fffffe00075212b4:      	sturh	w8, [x29, #-160]
fffffe00075212b8:      	mov	w8, #2000
fffffe00075212bc:      	str	x17, [sp, #584]
fffffe00075212c0:      	sturh	w9, [x29, #-166]
fffffe00075212c4:      	mov	w9, #30
fffffe00075212c8:      	str	x16, [sp, #560]
fffffe00075212cc:      	str	x15, [sp, #568]
fffffe00075212d0:      	sturb	w9, [x29, #-164]
fffffe00075212d4:      	adrp	x9, 0xfffffe0008524000 <_before+0x9c>
fffffe00075212d8:      	str	w14, [sp, #580]
fffffe00075212dc:      	stur	w8, [x29, #-156]
fffffe00075212e0:      	ldr	w19, [x9, #424]
fffffe00075212e4:      	cbz	w19, 0xfffffe0007521310 <_nss_owner_loop+0x2e5c>
fffffe00075212e8:      	adrp	x8, 0xfffffe0008516000 <_before+0x78>
fffffe00075212ec:      	nop
fffffe00075212f0:      	ldr	x8, [x8, #1984]
fffffe00075212f4:      	cmp	x8, x24
fffffe00075212f8:      	b.hs	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe00075212fc:      	adrp	x8, 0xfffffe0008516000 <_before+0x8c>
fffffe0007521300:      	add	x8, x8, #1984
fffffe0007521304:      	ldr	w8, [x8, #48]
fffffe0007521308:      	cmp	w8, #27
fffffe000752130c:      	b.hi	0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe0007521310:      	adrp	x20, 0xfffffe000851b000 <_before+0xb4>
fffffe0007521314:      	add	x20, x20, #128
fffffe0007521318:      	mov	x0, x20
fffffe000752131c:      	mov	w1, #18536
fffffe0007521320:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe0007521324:      	add	x4, sp, #608
fffffe0007521328:      	sub	x5, x29, #192
fffffe000752132c:      	sub	x6, x29, #168
fffffe0007521330:      	mov	x0, x20
fffffe0007521334:      	adrp	x1, 0xfffffe0007c5f000 <_ncc_native_read+0x18>
fffffe0007521338:      	add	x1, x1, #1624
fffffe000752133c:      	adrp	x2, 0xfffffe0008526000 <_before+0x10c>
fffffe0007521340:      	add	x2, x2, #2680
fffffe0007521344:      	adrp	x3, 0xfffffe000852a000 <_before+0x124>
fffffe0007521348:      	add	x3, x3, #3680
fffffe000752134c:      	mov	x7, x24
fffffe0007521350:      	stp	x26, x22, [sp]
fffffe0007521354:      	bl	0xfffffe00079acff4 <_nst_prepare>
fffffe0007521358:      	cbnz	w0, 0xfffffe000751f054 <_nss_owner_loop+0xba0>
fffffe000752135c:      	cbz	w19, 0xfffffe0007521380 <_nss_owner_loop+0x2ecc>
fffffe0007521360:      	adrp	x8, 0xfffffe0008516000 <_before+0xf0>
fffffe0007521364:      	add	x8, x8, #2024
fffffe0007521368:      	adrp	x9, 0xfffffe000851b000 <_before+0x10c>
fffffe000752136c:      	add	x9, x9, #232
fffffe0007521370:      	ldr	d0, [x8]
fffffe0007521374:      	ldr	w8, [x8, #8]
fffffe0007521378:      	str	d0, [x9, #10160]
fffffe000752137c:      	str	w8, [x9]
fffffe0007521380:      	ldr	x9, [sp, #584]
fffffe0007521384:      	adrp	x19, 0xfffffe0008516000 <_before+0x114>
fffffe0007521388:      	add	x19, x19, #2072
fffffe000752138c:      	ldr	x8, [sp, #72]
fffffe0007521390:      	ldr	x0, [sp, #208]
fffffe0007521394:      	mov	x1, x19
fffffe0007521398:      	mov	w2, #18536
fffffe000752139c:      	bfi	x9, x8, #32, #16
fffffe00075213a0:      	ldr	x8, [sp, #80]
fffffe00075213a4:      	bfi	x9, x8, #48, #16
fffffe00075213a8:      	str	x9, [sp, #584]
fffffe00075213ac:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe00075213b0:      	adrp	x20, 0xfffffe0008516000 <_before+0x140>
fffffe00075213b4:      	add	x20, x20, #1984
fffffe00075213b8:      	mov	w9, #55696
fffffe00075213bc:      	adrp	x10, 0xfffffe0008528000 <_nnr_plan+0x38>
fffffe00075213c0:      	add	x10, x10, #3696
fffffe00075213c4:      	mov	x0, x19
fffffe00075213c8:      	ldp	q0, q1, [x20, #32]
fffffe00075213cc:      	mov	w2, #18536
fffffe00075213d0:      	ldr	d3, [x19, x9]
fffffe00075213d4:      	ldr	x9, [sp, #200]
fffffe00075213d8:      	ldr	q2, [x20, #64]
fffffe00075213dc:      	ldr	x8, [x20, #80]
fffffe00075213e0:      	stp	q0, q1, [x9, #32]
fffffe00075213e4:      	ldr	d4, [x10]
fffffe00075213e8:      	ldp	q0, q1, [x20]
fffffe00075213ec:      	str	q2, [x9, #64]
fffffe00075213f0:      	str	x8, [x9, #80]
fffffe00075213f4:      	mov.d	v3[1], v4[0]
fffffe00075213f8:      	stp	q0, q1, [x9]
fffffe00075213fc:      	mov	w9, #55724
fffffe0007521400:      	ldr	w8, [x10, #8]
fffffe0007521404:      	str	w8, [x19, x9]
fffffe0007521408:      	mov	w8, #18536
fffffe000752140c:      	add	x22, x19, x8
fffffe0007521410:      	mov	w8, #55708
fffffe0007521414:      	mov	x1, x22
fffffe0007521418:      	mov	w9, #1
fffffe000752141c:      	str	q3, [x19, x8]
fffffe0007521420:      	mov	w8, #55704
fffffe0007521424:      	str	w9, [x19, x8]
fffffe0007521428:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe000752142c:      	mov	x0, x22
fffffe0007521430:      	mov	w1, #18536
fffffe0007521434:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe0007521438:      	adrp	x8, 0xfffffe0008528000 <_nnr_begin+0x14>
fffffe000752143c:      	add	x8, x8, #3696
fffffe0007521440:      	ldr	x16, [sp, #216]
fffffe0007521444:      	mov	w14, #1
fffffe0007521448:      	ldr	x10, [sp, #584]
fffffe000752144c:      	adrp	x0, 0xfffffe000709a000 <_nss_owner_loop+0x1d7c>
fffffe0007521450:      	add	x0, x0, #1462
fffffe0007521454:      	str	wzr, [x8, #8]
fffffe0007521458:      	str	xzr, [x8]
fffffe000752145c:      	mov	w8, #55696
fffffe0007521460:      	ldr	x9, [sp, #96]
fffffe0007521464:      	strb	w14, [x16, #59]
fffffe0007521468:      	ldr	w12, [sp, #580]
fffffe000752146c:      	str	xzr, [x20, #32]
fffffe0007521470:      	str	d8, [x19, x8]
fffffe0007521474:      	adrp	x19, 0xfffffe00084f8000 <_nnr_plan+0x30>
fffffe0007521478:      	add	x19, x19, #572
fffffe000752147c:      	ldr	x8, [sp, #560]
fffffe0007521480:      	add	x11, x19, #448
fffffe0007521484:      	stp	x10, x9, [x16]
fffffe0007521488:      	add	x9, x19, #1551
fffffe000752148c:      	mov	w10, #10
fffffe0007521490:      	ldr	w13, [sp, #92]
fffffe0007521494:      	strh	w12, [x16, #56]
fffffe0007521498:      	stp	x11, x8, [x16, #16]
fffffe000752149c:      	ldr	x8, [sp, #568]
fffffe00075214a0:      	strh	w10, [x16, #50]
fffffe00075214a4:      	ldurh	w10, [x29, #-136]
fffffe00075214a8:      	ldurh	w11, [x29, #-194]
fffffe00075214ac:      	strb	w13, [x16, #58]
fffffe00075214b0:      	stp	x9, x8, [x16, #32]
fffffe00075214b4:      	ldurb	w8, [x29, #-175]
fffffe00075214b8:      	ldurb	w9, [x29, #-174]
fffffe00075214bc:      	str	wzr, [x20, #72]
fffffe00075214c0:      	ldurh	w12, [x29, #-196]
fffffe00075214c4:      	stp	xzr, xzr, [x20, #56]
fffffe00075214c8:      	ldurb	w13, [x29, #-179]
fffffe00075214cc:      	strh	w28, [x16, #48]
fffffe00075214d0:      	ldurb	w14, [x29, #-177]
fffffe00075214d4:      	str	wzr, [x16, #52]
fffffe00075214d8:      	ldurb	w15, [x29, #-176]
fffffe00075214dc:      	strb	w23, [x16, #60]
fffffe00075214e0:      	strb	wzr, [x16, #63]
fffffe00075214e4:      	sturh	wzr, [x16, #61]
fffffe00075214e8:      	stp	x14, x15, [sp, #56]
fffffe00075214ec:      	stp	x12, x13, [sp, #40]
fffffe00075214f0:      	stp	x10, x11, [sp, #24]
fffffe00075214f4:      	stp	x8, x9, [sp, #8]
fffffe00075214f8:      	str	x24, [sp]
fffffe00075214fc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007521500:      	ldp	x1, x0, [sp, #184]
fffffe0007521504:      	mov	w2, #4864
fffffe0007521508:      	ldr	w28, [x19]
fffffe000752150c:      	ldr	w22, [x19, #20]
fffffe0007521510:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe0007521514:      	mov	w8, #22228
fffffe0007521518:      	movk	w8, #1, lsl #16
fffffe000752151c:      	ldr	q0, [x19, x8]
fffffe0007521520:      	mov	w8, #22244
fffffe0007521524:      	movk	w8, #1, lsl #16
fffffe0007521528:      	str	q0, [x19, x8]
fffffe000752152c:      	bl	0xfffffe00079b4a8c <_nwl_enabled>
fffffe0007521530:      	cbz	w0, 0xfffffe0007521590 <_nss_owner_loop+0x30dc>
fffffe0007521534:      	adrp	x8, 0xfffffe0008531000 <_nbc_caps_decode+0x24>
fffffe0007521538:      	ldr	w1, [x8, #156]
fffffe000752153c:      	cbz	w1, 0xfffffe0007521590 <_nss_owner_loop+0x30dc>
fffffe0007521540:      	adrp	x9, 0xfffffe0008531000 <_nbc_caps_decode+0x30>
fffffe0007521544:      	ldr	w8, [x9, #164]
fffffe0007521548:      	cbz	w8, 0xfffffe000752155c <_nss_owner_loop+0x30a8>
fffffe000752154c:      	adrp	x8, 0xfffffe0008531000 <_nbc_caps_decode+0x3c>
fffffe0007521550:      	str	wzr, [x9, #164]
fffffe0007521554:      	ldr	w2, [x8, #160]
fffffe0007521558:      	b	0xfffffe0007521584 <_nss_owner_loop+0x30d0>
fffffe000752155c:      	adrp	x8, 0xfffffe0008531000 <_nbc_caps_decode+0x4c>
fffffe0007521560:      	ldr	w2, [x8, #160]
fffffe0007521564:      	cmp	w2, #31
fffffe0007521568:      	b.hi	0xfffffe0007521578 <_nss_owner_loop+0x30c4>
fffffe000752156c:      	add	w2, w2, #1
fffffe0007521570:      	str	w2, [x8, #160]
fffffe0007521574:      	b	0xfffffe0007521584 <_nss_owner_loop+0x30d0>
fffffe0007521578:      	mov	w1, wzr
fffffe000752157c:      	adrp	x8, 0xfffffe0008531000 <_nbc_caps_decode+0x6c>
fffffe0007521580:      	str	wzr, [x8, #156]
fffffe0007521584:      	adrp	x0, 0xfffffe0008518000 <_nbc_caps_decode+0x10>
fffffe0007521588:      	add	x0, x0, #3664
fffffe000752158c:      	bl	0xfffffe00079b3c9c <_nsdd_capture>
fffffe0007521590:      	adrp	x8, 0xfffffe00084f8000 <_nnr_reserve_once+0x18>
fffffe0007521594:      	ldr	w8, [x8, #572]
fffffe0007521598:      	cbz	w8, 0xfffffe00075215b8 <_nss_owner_loop+0x3104>
fffffe000752159c:      	adrp	x0, 0xfffffe000850b000 <_nnr_reserve_once+0x70>
fffffe00075215a0:      	add	x0, x0, #784
fffffe00075215a4:      	mov	w1, #4864
fffffe00075215a8:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe00075215ac:      	ldr	x0, [sp, #176]
fffffe00075215b0:      	mov	w1, #16
fffffe00075215b4:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe00075215b8:      	mov	w8, #1
fffffe00075215bc:      	str	wzr, [x19, #20]
fffffe00075215c0:      	mov	x20, x19
fffffe00075215c4:      	add	x0, sp, #608
fffffe00075215c8:      	str	xzr, [sp, #608]
fffffe00075215cc:      	str	w8, [x19]
fffffe00075215d0:      	add	x19, sp, #608
fffffe00075215d4:      	str	xzr, [x19, #296]
fffffe00075215d8:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe00075215dc:      	ldr	x0, [sp, #608]
fffffe00075215e0:      	sub	x1, x29, #168
fffffe00075215e4:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe00075215e8:      	ldr	x8, [x19, #296]
fffffe00075215ec:      	mov	x10, #63439
fffffe00075215f0:      	movk	x10, #58195, lsl #16
fffffe00075215f4:      	movk	x10, #39845, lsl #32
fffffe00075215f8:      	lsr	x9, x8, #3
fffffe00075215fc:      	movk	x10, #8388, lsl #48
fffffe0007521600:      	umulh	x9, x9, x10
fffffe0007521604:      	lsr	x19, x9, #4
fffffe0007521608:      	cmp	x19, x26
fffffe000752160c:      	b.lo	0xfffffe0007521790 <_nss_owner_loop+0x32dc>
fffffe0007521610:      	ldur	x9, [x20, #148]
fffffe0007521614:      	cmp	x19, x9
fffffe0007521618:      	b.hs	0xfffffe0007521790 <_nss_owner_loop+0x32dc>
fffffe000752161c:      	mov	x11, #63439
fffffe0007521620:      	lsr	x9, x9, #3
fffffe0007521624:      	movk	x11, #58195, lsl #16
fffffe0007521628:      	ldr	x10, [sp, #136]
fffffe000752162c:      	movk	x11, #39845, lsl #32
fffffe0007521630:      	ldr	x12, [sp, #128]
fffffe0007521634:      	movk	x11, #8388, lsl #48
fffffe0007521638:      	adrp	x1, 0xfffffe0008516000 <_nbc_caps_decode+0xbc>
fffffe000752163c:      	add	x1, x1, #2072
fffffe0007521640:      	str	xzr, [x10, #144]
fffffe0007521644:      	umulh	x9, x9, x11
fffffe0007521648:      	ldr	x11, [sp, #168]
fffffe000752164c:      	movi.2d	v2, #0000000000000000
fffffe0007521650:      	mov	w13, #4000
fffffe0007521654:      	lsr	x9, x9, #4
fffffe0007521658:      	add	x3, sp, #608
fffffe000752165c:      	ldp	q0, q1, [x11]
fffffe0007521660:      	adr	x4, #5476
fffffe0007521664:      	nop
fffffe0007521668:      	stp	q2, q2, [x10, #16]
fffffe000752166c:      	mov	x5, x21
fffffe0007521670:      	stp	q2, q2, [x10, #48]
fffffe0007521674:      	add	x23, x21, #360
fffffe0007521678:      	stp	q2, q2, [x10, #80]
fffffe000752167c:      	stp	q2, q2, [x10, #112]
fffffe0007521680:      	str	q2, [x10]
fffffe0007521684:      	ldr	x10, [x21, #208]
fffffe0007521688:      	stp	q0, q1, [x12]
fffffe000752168c:      	ldr	q0, [x11, #32]
fffffe0007521690:      	str	x9, [sp, #616]
fffffe0007521694:      	ldr	x9, [x11, #48]
fffffe0007521698:      	mov	w11, #30000
fffffe000752169c:      	str	x10, [sp, #608]
fffffe00075216a0:      	str	q0, [x12, #32]
fffffe00075216a4:      	str	x9, [x12, #48]
fffffe00075216a8:      	ldrh	w9, [x21, #1576]
fffffe00075216ac:      	str	w11, [sp, #624]
fffffe00075216b0:      	ldrb	w11, [x1, #212]
fffffe00075216b4:      	ldr	x12, [sp, #120]
fffffe00075216b8:      	strb	w10, [sp, #689]
fffffe00075216bc:      	ldrb	w10, [x1, #1510]
fffffe00075216c0:      	str	w13, [sp, #696]
fffffe00075216c4:      	strb	w11, [sp, #688]
fffffe00075216c8:      	add	x11, sp, #436
fffffe00075216cc:      	ldp	x2, x0, [sp, #152]
fffffe00075216d0:      	strb	w10, [x12, #2]
fffffe00075216d4:      	add	x10, x21, #400
fffffe00075216d8:      	sturh	w9, [x11, #255]
fffffe00075216dc:      	ldrh	w9, [x1, #1508]
fffffe00075216e0:      	mov	w11, #4
fffffe00075216e4:      	str	x10, [sp, #712]
fffffe00075216e8:      	ldrh	w10, [x21, #534]
fffffe00075216ec:      	strh	w9, [x12]
fffffe00075216f0:      	ldrb	w9, [x1, #216]
fffffe00075216f4:      	strb	w11, [sp, #693]
fffffe00075216f8:      	ldrb	w11, [x1, #215]
fffffe00075216fc:      	add	x12, x21, #1594
fffffe0007521700:      	strb	w9, [sp, #704]
fffffe0007521704:      	mov	w9, #16960
fffffe0007521708:      	movk	w9, #15, lsl #16
fffffe000752170c:      	strb	w11, [sp, #703]
fffffe0007521710:      	add	x11, x21, #432
fffffe0007521714:      	str	x12, [sp, #728]
fffffe0007521718:      	udiv	x6, x8, x9
fffffe000752171c:      	ldrh	w8, [x21, #1592]
fffffe0007521720:      	add	x9, x21, #552
fffffe0007521724:      	str	x11, [sp, #720]
fffffe0007521728:      	add	x11, sp, #608
fffffe000752172c:      	stp	x8, x9, [x11, #128]
fffffe0007521730:      	adr	x8, #5220
fffffe0007521734:      	nop
fffffe0007521738:      	stp	x10, x8, [x11, #144]
fffffe000752173c:      	bl	0xfffffe00079bc4a4 <_nwn_begin>
fffffe0007521740:      	mov	w24, w0
fffffe0007521744:      	add	x0, sp, #608
fffffe0007521748:      	mov	w1, #168
fffffe000752174c:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe0007521750:      	mov	x0, x23
fffffe0007521754:      	mov	w1, #104
fffffe0007521758:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe000752175c:      	cmp	w24, #1
fffffe0007521760:      	cset	w8, eq
fffffe0007521764:      	stp	wzr, w8, [x21, #8]
fffffe0007521768:      	b.ne	0xfffffe0007521924 <_nss_owner_loop+0x3470>
fffffe000752176c:      	adrp	x0, 0xfffffe000851f000 <_nni_basic+0x24>
fffffe0007521770:      	add	x0, x0, #2280
fffffe0007521774:      	mov	w8, #18632
fffffe0007521778:      	mov	w1, #18624
fffffe000752177c:      	str	wzr, [x0, x8]
fffffe0007521780:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe0007521784:      	bl	0xfffffe0007522744 <_njs_snapshot_locked>
fffffe0007521788:      	mov	w19, wzr
fffffe000752178c:      	b	0xfffffe0007521a6c <_nss_owner_loop+0x35b8>
fffffe0007521790:      	adrp	x11, 0xfffffe0008516000 <_nni_basic+0x24>
fffffe0007521794:      	add	x11, x11, #2124
fffffe0007521798:      	add	x8, x11, #40
fffffe000752179c:      	adrp	x9, 0xfffffe00084f8000 <_nbc_caps_decode+0x1a8>
fffffe00075217a0:      	ldr	d0, [x11]
fffffe00075217a4:      	ldur	d1, [x11, #4]
fffffe00075217a8:      	ldr	x9, [x9, #752]
fffffe00075217ac:      	ld1.s	{ v0 }[1], [x8]
fffffe00075217b0:      	mov	w8, #55652
fffffe00075217b4:      	ldur	x10, [x11, #108]
fffffe00075217b8:      	ldr	w8, [x11, x8]
fffffe00075217bc:      	mov.d	v0[1], v1[0]
fffffe00075217c0:      	cmp	w8, #0
fffffe00075217c4:      	ldr	w8, [x11, #100]
fffffe00075217c8:      	cmtst.4s	v0, v0, v0
fffffe00075217cc:      	cset	w11, ne
fffffe00075217d0:      	cmp	x10, x9
fffffe00075217d4:      	cset	w9, eq
fffffe00075217d8:      	cmp	w8, #0
fffffe00075217dc:      	xtn.4h	v0, v0
fffffe00075217e0:      	cset	w10, eq
fffffe00075217e4:      	umaxv.4h	h0, v0
fffffe00075217e8:      	fmov	w8, s0
fffffe00075217ec:      	bic	w8, w11, w8
fffffe00075217f0:      	and	w8, w8, w10
fffffe00075217f4:      	and	w8, w8, w9
fffffe00075217f8:      	subs	w20, w8, #1
fffffe00075217fc:      	b.ne	0xfffffe0007521894 <_nss_owner_loop+0x33e0>
fffffe0007521800:      	adrp	x24, 0xfffffe0008516000 <_ncp_query+0x20>
fffffe0007521804:      	add	x24, x24, #2072
fffffe0007521808:      	mov	w8, #37072
fffffe000752180c:      	mov	x0, x24
fffffe0007521810:      	add	x23, x24, x8
fffffe0007521814:      	mov	w2, #18536
fffffe0007521818:      	mov	x1, x23
fffffe000752181c:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe0007521820:      	mov	w8, #55608
fffffe0007521824:      	adrp	x10, 0xfffffe0008516000 <_ncp_query+0x44>
fffffe0007521828:      	add	x10, x10, #1984
fffffe000752182c:      	add	x8, x24, x8
fffffe0007521830:      	mov	w9, #55716
fffffe0007521834:      	mov	w11, #55708
fffffe0007521838:      	add	x9, x24, x9
fffffe000752183c:      	mov	x0, x23
fffffe0007521840:      	ldp	q0, q1, [x8, #32]
fffffe0007521844:      	mov	w1, #18624
fffffe0007521848:      	stp	q0, q1, [x10, #32]
fffffe000752184c:      	ldr	q2, [x8, #64]
fffffe0007521850:      	ldp	q3, q0, [x8]
fffffe0007521854:      	str	q2, [x10, #64]
fffffe0007521858:      	ldr	x8, [x8, #80]
fffffe000752185c:      	stp	q3, q0, [x10]
fffffe0007521860:      	ldr	d0, [x9]
fffffe0007521864:      	ldr	w9, [x9, #8]
fffffe0007521868:      	str	x8, [x10, #80]
fffffe000752186c:      	adrp	x8, 0xfffffe0008528000 <_ncp_parse+0x7c>
fffffe0007521870:      	add	x8, x8, #3696
fffffe0007521874:      	ldr	d1, [x24, x11]
fffffe0007521878:      	mov	w11, #55696
fffffe000752187c:      	str	d0, [x8]
fffffe0007521880:      	str	w9, [x8, #8]
fffffe0007521884:      	mov	w8, #55704
fffffe0007521888:      	str	d1, [x24, x11]
fffffe000752188c:      	str	wzr, [x24, x8]
fffffe0007521890:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe0007521894:      	mov	w8, #12500
fffffe0007521898:      	adrp	x26, 0xfffffe00084f8000 <_ncp_query+0x40>
fffffe000752189c:      	add	x26, x26, #572
fffffe00075218a0:      	movk	w8, #1, lsl #16
fffffe00075218a4:      	add	x0, x26, x8
fffffe00075218a8:      	mov	w8, #17364
fffffe00075218ac:      	movk	w8, #1, lsl #16
fffffe00075218b0:      	mov	w2, #4864
fffffe00075218b4:      	add	x23, x26, x8
fffffe00075218b8:      	str	w28, [x26]
fffffe00075218bc:      	mov	x1, x23
fffffe00075218c0:      	str	w22, [x26, #20]
fffffe00075218c4:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe00075218c8:      	mov	w8, #22244
fffffe00075218cc:      	mov	x0, x23
fffffe00075218d0:      	movk	w8, #1, lsl #16
fffffe00075218d4:      	mov	w1, #4864
fffffe00075218d8:      	add	x24, x26, x8
fffffe00075218dc:      	mov	w8, #22228
fffffe00075218e0:      	movk	w8, #1, lsl #16
fffffe00075218e4:      	ldr	q0, [x24]
fffffe00075218e8:      	str	q0, [x26, x8]
fffffe00075218ec:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe00075218f0:      	mov	x0, x24
fffffe00075218f4:      	mov	w1, #16
fffffe00075218f8:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe00075218fc:      	ldr	x0, [x27, #2456]
fffffe0007521900:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521904:      	ldur	x8, [x26, #148]
fffffe0007521908:      	mov	w9, #9
fffffe000752190c:      	cmp	x19, x8
fffffe0007521910:      	mov	w8, #2
fffffe0007521914:      	csel	w0, w8, w9, lo
fffffe0007521918:      	bl	0xfffffe0007522178 <_njs_early_finish>
fffffe000752191c:      	tbz	w20, #31, 0xfffffe000751f068 <_nss_owner_loop+0xbb4>
fffffe0007521920:      	b	0xfffffe0007521ae0 <_nss_owner_loop+0x362c>
fffffe0007521924:      	adrp	x9, 0xfffffe0008516000 <_ncp_parse+0xec>
fffffe0007521928:      	add	x9, x9, #2124
fffffe000752192c:      	add	x8, x9, #40
fffffe0007521930:      	mov	w19, #-1
fffffe0007521934:      	ldr	d0, [x9]
fffffe0007521938:      	ldur	d1, [x9, #4]
fffffe000752193c:      	ld1.s	{ v0 }[1], [x8]
fffffe0007521940:      	mov.d	v0[1], v1[0]
fffffe0007521944:      	cmtst.4s	v0, v0, v0
fffffe0007521948:      	xtn.4h	v0, v0
fffffe000752194c:      	umaxv.4h	h0, v0
fffffe0007521950:      	fmov	w8, s0
fffffe0007521954:      	tbnz	w8, #0, 0xfffffe0007521a14 <_nss_owner_loop+0x3560>
fffffe0007521958:      	mov	w8, #55652
fffffe000752195c:      	ldr	w8, [x9, x8]
fffffe0007521960:      	cbz	w8, 0xfffffe0007521a14 <_nss_owner_loop+0x3560>
fffffe0007521964:      	ldr	w8, [x9, #100]
fffffe0007521968:      	cbnz	w8, 0xfffffe0007521a14 <_nss_owner_loop+0x3560>
fffffe000752196c:      	adrp	x8, 0xfffffe00084f8000 <_ncp_parse+0xbc>
fffffe0007521970:      	ldur	x9, [x9, #108]
fffffe0007521974:      	ldr	x8, [x8, #752]
fffffe0007521978:      	cmp	x9, x8
fffffe000752197c:      	b.ne	0xfffffe0007521a14 <_nss_owner_loop+0x3560>
fffffe0007521980:      	adrp	x19, 0xfffffe0008516000 <_ncp_parse+0x148>
fffffe0007521984:      	add	x19, x19, #2072
fffffe0007521988:      	mov	w8, #37072
fffffe000752198c:      	mov	x0, x19
fffffe0007521990:      	add	x23, x19, x8
fffffe0007521994:      	mov	w2, #18536
fffffe0007521998:      	mov	x1, x23
fffffe000752199c:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe00075219a0:      	mov	w8, #55608
fffffe00075219a4:      	adrp	x10, 0xfffffe0008516000 <_ncp_parse+0x16c>
fffffe00075219a8:      	add	x10, x10, #1984
fffffe00075219ac:      	add	x8, x19, x8
fffffe00075219b0:      	mov	w9, #55708
fffffe00075219b4:      	ldr	x11, [sp, #112]
fffffe00075219b8:      	mov	x0, x23
fffffe00075219bc:      	mov	w1, #18624
fffffe00075219c0:      	ldp	q0, q1, [x8, #32]
fffffe00075219c4:      	stp	q0, q1, [x10, #32]
fffffe00075219c8:      	ldr	q2, [x8, #64]
fffffe00075219cc:      	ldp	q3, q0, [x8]
fffffe00075219d0:      	str	q2, [x10, #64]
fffffe00075219d4:      	ldr	d1, [x19, x9]
fffffe00075219d8:      	mov	w9, #55696
fffffe00075219dc:      	ldr	x8, [x8, #80]
fffffe00075219e0:      	stp	q3, q0, [x10]
fffffe00075219e4:      	ldr	d0, [x11]
fffffe00075219e8:      	str	d1, [x19, x9]
fffffe00075219ec:      	ldr	w9, [x11, #8]
fffffe00075219f0:      	str	x8, [x10, #80]
fffffe00075219f4:      	adrp	x8, 0xfffffe0008528000 <_ncp_parse+0x204>
fffffe00075219f8:      	add	x8, x8, #3696
fffffe00075219fc:      	str	d0, [x8]
fffffe0007521a00:      	str	w9, [x8, #8]
fffffe0007521a04:      	mov	w8, #55704
fffffe0007521a08:      	str	wzr, [x19, x8]
fffffe0007521a0c:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe0007521a10:      	mov	w19, wzr
fffffe0007521a14:      	mov	w8, #12500
fffffe0007521a18:      	adrp	x20, 0xfffffe00084f8000 <_ncp_parse+0x168>
fffffe0007521a1c:      	add	x20, x20, #572
fffffe0007521a20:      	movk	w8, #1, lsl #16
fffffe0007521a24:      	add	x23, x20, x8
fffffe0007521a28:      	mov	w1, #4864
fffffe0007521a2c:      	mov	x0, x23
fffffe0007521a30:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe0007521a34:      	mov	w8, #17364
fffffe0007521a38:      	mov	x0, x23
fffffe0007521a3c:      	movk	w8, #1, lsl #16
fffffe0007521a40:      	mov	w2, #4864
fffffe0007521a44:      	add	x1, x20, x8
fffffe0007521a48:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe0007521a4c:      	mov	w8, #22244
fffffe0007521a50:      	str	w28, [x20]
fffffe0007521a54:      	movk	w8, #1, lsl #16
fffffe0007521a58:      	str	w22, [x20, #20]
fffffe0007521a5c:      	ldr	q0, [x20, x8]
fffffe0007521a60:      	mov	w8, #22228
fffffe0007521a64:      	movk	w8, #1, lsl #16
fffffe0007521a68:      	str	q0, [x20, x8]
fffffe0007521a6c:      	adrp	x0, 0xfffffe000850c000 <_ncp_parse+0x20c>
fffffe0007521a70:      	add	x0, x0, #1552
fffffe0007521a74:      	mov	w1, #4864
fffffe0007521a78:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe0007521a7c:      	ldr	x0, [sp, #144]
fffffe0007521a80:      	mov	w1, #16
fffffe0007521a84:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe0007521a88:      	ldr	x0, [x27, #2456]
fffffe0007521a8c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521a90:      	cmp	w24, #1
fffffe0007521a94:      	b.ne	0xfffffe0007521abc <_nss_owner_loop+0x3608>
fffffe0007521a98:      	ldr	x0, [x27, #2456]
fffffe0007521a9c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521aa0:      	mov	w0, #1
fffffe0007521aa4:      	mov	w19, #1
fffffe0007521aa8:      	bl	0xfffffe0007512bf0 <_njs_trace_locked>
fffffe0007521aac:      	ldr	x0, [x27, #2456]
fffffe0007521ab0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521ab4:      	tbz	w19, #31, 0xfffffe000751f068 <_nss_owner_loop+0xbb4>
fffffe0007521ab8:      	b	0xfffffe0007521ae0 <_nss_owner_loop+0x362c>
fffffe0007521abc:      	mov	w0, #2
fffffe0007521ac0:      	bl	0xfffffe0007522178 <_njs_early_finish>
fffffe0007521ac4:      	cmp	w19, #0
fffffe0007521ac8:      	csetm	w19, ne
fffffe0007521acc:      	tbz	w19, #31, 0xfffffe000751f068 <_nss_owner_loop+0xbb4>
fffffe0007521ad0:      	b	0xfffffe0007521ae0 <_nss_owner_loop+0x362c>
fffffe0007521ad4:      	ldr	x0, [x27, #2456]
fffffe0007521ad8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521adc:      	bl	0xfffffe0007522218 <_njs_fatal_owner>
fffffe0007521ae0:      	adrp	x8, 0xfffffe0007c9c000 <_nis_write_mask+0x13c>
fffffe0007521ae4:      	mov	w19, #1
fffffe0007521ae8:      	adrp	x20, 0xfffffe00084dd000 <_ncp_parse+0x1cc>
fffffe0007521aec:      	add	x20, x20, #2456
fffffe0007521af0:      	adrp	x10, 0xfffffe0007c5f000 <_nis_write_mask+0x58>
fffffe0007521af4:      	add	x10, x10, #1636
fffffe0007521af8:      	add	x8, x8, #648
fffffe0007521afc:      	ldr	x0, [x20]
fffffe0007521b00:      	str	w19, [x10, #1872]
fffffe0007521b04:      	str	w19, [x8, #280]
fffffe0007521b08:      	adrp	x8, 0xfffffe0008526000 <_ncp_parse+0x310>
fffffe0007521b0c:      	str	w19, [x10]
fffffe0007521b10:      	str	w19, [x10, #3408]
fffffe0007521b14:      	str	w19, [x8, #2668]
fffffe0007521b18:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521b1c:      	ldr	x0, [x20]
fffffe0007521b20:      	str	w19, [x20, #12]
fffffe0007521b24:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521b28:      	bl	0xfffffe000751a36c <_nwm_failure_drain>
fffffe0007521b2c:      	mov	w8, #137
fffffe0007521b30:      	adrp	x0, 0xfffffe000709a000 <_nss_owner_loop+0x2460>
fffffe0007521b34:      	add	x0, x0, #1051
fffffe0007521b38:      	adrp	x9, 0xfffffe000709a000 <_nss_owner_loop+0x2468>
fffffe0007521b3c:      	add	x9, x9, #1124
fffffe0007521b40:      	stp	x9, x8, [sp]
fffffe0007521b44:      	bl	0xfffffe0007b218bc <_panic>
fffffe0007521b48:      	adrp	x19, 0xfffffe000852b000 <_ncp_parse+0x364>
fffffe0007521b4c:      	add	x19, x19, #1440
fffffe0007521b50:      	add	x0, x24, #144
fffffe0007521b54:      	mov	x1, x19
fffffe0007521b58:      	mov	w2, #18336
fffffe0007521b5c:      	bl	0xfffffe0007bb0110 <_memmove>
fffffe0007521b60:      	ldr	x8, [x19, #48]
fffffe0007521b64:      	str	x8, [x24, #32]
fffffe0007521b68:      	adrp	x8, 0xfffffe0007c9c000 <_nis_barrier+0x20>
fffffe0007521b6c:      	mov	w19, #1
fffffe0007521b70:      	adrp	x20, 0xfffffe00084dd000 <_ncp_parse+0x254>
fffffe0007521b74:      	add	x20, x20, #2456
fffffe0007521b78:      	adrp	x10, 0xfffffe0007c5f000 <_nis_write_mask+0xe0>
fffffe0007521b7c:      	add	x10, x10, #1636
fffffe0007521b80:      	add	x8, x8, #648
fffffe0007521b84:      	ldr	x0, [x20]
fffffe0007521b88:      	str	w19, [x10, #1872]
fffffe0007521b8c:      	str	w19, [x8, #280]
fffffe0007521b90:      	adrp	x8, 0xfffffe0008526000 <_ncp_parse+0x398>
fffffe0007521b94:      	str	w19, [x10]
fffffe0007521b98:      	str	w19, [x10, #3408]
fffffe0007521b9c:      	str	w19, [x8, #2668]
fffffe0007521ba0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521ba4:      	ldr	x0, [x20]
fffffe0007521ba8:      	str	w19, [x20, #12]
fffffe0007521bac:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521bb0:      	mov	x0, x24
fffffe0007521bb4:      	mov	w1, #4
fffffe0007521bb8:      	bl	0xfffffe0007521e64 <_nss_deliver>
fffffe0007521bbc:      	ldr	x0, [x20]
fffffe0007521bc0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521bc4:      	ldr	x0, [x20]
fffffe0007521bc8:      	str	w19, [x20, #12]
fffffe0007521bcc:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521bd0:      	ldr	x0, [x20]
fffffe0007521bd4:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521bd8:      	ldr	x0, [x20]
fffffe0007521bdc:      	ldr	w19, [x20, #128]
fffffe0007521be0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521be4:      	cmp	w19, #1
fffffe0007521be8:      	b.ne	0xfffffe0007521bfc <_nss_owner_loop+0x3748>
fffffe0007521bec:      	adrp	x0, 0xfffffe00084dd000 <_ncp_parse+0x2d0>
fffffe0007521bf0:      	add	x0, x0, #2584
fffffe0007521bf4:      	mov	w1, #4
fffffe0007521bf8:      	bl	0xfffffe0007521e64 <_nss_deliver>
fffffe0007521bfc:      	adrp	x20, 0xfffffe00084dd000 <_ncp_parse+0x2e0>
fffffe0007521c00:      	add	x20, x20, #2456
fffffe0007521c04:      	ldr	x0, [x20]
fffffe0007521c08:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521c0c:      	mov	w8, #18608
fffffe0007521c10:      	ldr	x0, [x20]
fffffe0007521c14:      	ldr	w19, [x20, x8]
fffffe0007521c18:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521c1c:      	cmp	w19, #1
fffffe0007521c20:      	b.ne	0xfffffe0007521c34 <_nss_owner_loop+0x3780>
fffffe0007521c24:      	adrp	x0, 0xfffffe00084e2000 <_ncp_parse+0x31c>
fffffe0007521c28:      	add	x0, x0, #584
fffffe0007521c2c:      	mov	w1, #4
fffffe0007521c30:      	bl	0xfffffe0007521e64 <_nss_deliver>
fffffe0007521c34:      	adrp	x20, 0xfffffe00084dd000 <_ncp_parse+0x318>
fffffe0007521c38:      	add	x20, x20, #2456
fffffe0007521c3c:      	ldr	x0, [x20]
fffffe0007521c40:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521c44:      	mov	w8, #37088
fffffe0007521c48:      	ldr	x0, [x20]
fffffe0007521c4c:      	ldr	w19, [x20, x8]
fffffe0007521c50:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521c54:      	cmp	w19, #1
fffffe0007521c58:      	b.ne	0xfffffe0007521c6c <_nss_owner_loop+0x37b8>
fffffe0007521c5c:      	adrp	x0, 0xfffffe00084e6000 <_ncp_parse+0x364>
fffffe0007521c60:      	add	x0, x0, #2680
fffffe0007521c64:      	mov	w1, #4
fffffe0007521c68:      	bl	0xfffffe0007521e64 <_nss_deliver>
fffffe0007521c6c:      	adrp	x20, 0xfffffe00084dd000 <_ncp_parse+0x350>
fffffe0007521c70:      	add	x20, x20, #2456
fffffe0007521c74:      	ldr	x0, [x20]
fffffe0007521c78:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007521c7c:      	mov	w8, #55568
fffffe0007521c80:      	ldr	x0, [x20]
fffffe0007521c84:      	ldr	w19, [x20, x8]
fffffe0007521c88:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007521c8c:      	cmp	w19, #1
fffffe0007521c90:      	b.ne	0xfffffe0007521ca4 <_nss_owner_loop+0x37f0>
fffffe0007521c94:      	adrp	x0, 0xfffffe00084eb000 <_ncp_parse+0x3b0>
fffffe0007521c98:      	add	x0, x0, #680
fffffe0007521c9c:      	mov	w1, #4
fffffe0007521ca0:      	bl	0xfffffe0007521e64 <_nss_deliver>
fffffe0007521ca4:      	bl	0xfffffe000751a36c <_nwm_failure_drain>
fffffe0007521ca8:      	mov	w8, #151
fffffe0007521cac:      	adrp	x0, 0xfffffe000709a000 <_nss_owner_loop+0x25dc>
fffffe0007521cb0:      	add	x0, x0, #1151
fffffe0007521cb4:      	b	0xfffffe0007521b38 <_nss_owner_loop+0x3684>
fffffe0007521cb8:      	ldr	x0, [x27, #2456]
fffffe0007521cbc:      	adrp	x8, 0xfffffe0008516000 <_ncp_parse+0x484>
fffffe0007521cc0:      	add	x8, x8, #2108
fffffe0007521cc4:      	stp	w24, w26, [x8]
fffffe0007521cc8:      	b	0xfffffe0007521ad8 <_nss_owner_loop+0x3624>
