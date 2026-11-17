
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b6544 <_nwp_receive>:
fffffe00079b6544:      	stp	x24, x23, [sp, #-64]!
fffffe00079b6548:      	stp	x22, x21, [sp, #16]
fffffe00079b654c:      	stp	x20, x19, [sp, #32]
fffffe00079b6550:      	stp	x29, x30, [sp, #48]
fffffe00079b6554:      	add	x29, sp, #48
fffffe00079b6558:      	sub	sp, sp, #688
fffffe00079b655c:      	adrp	x8, 0xfffffe0007c98000 <_group_receive+0x364>
fffffe00079b6560:      	nop
fffffe00079b6564:      	ldr	x8, [x8, #3440]
fffffe00079b6568:      	stur	x8, [x29, #-56]
fffffe00079b656c:      	cbz	x3, 0xfffffe00079b6664 <_nwp_receive+0x120>
fffffe00079b6570:      	movi.2d	v0, #0000000000000000
fffffe00079b6574:      	mov	x19, x3
fffffe00079b6578:      	mov	x20, x0
fffffe00079b657c:      	str	xzr, [x3, #272]
fffffe00079b6580:      	stp	q0, q0, [x3, #16]
fffffe00079b6584:      	stp	q0, q0, [x3, #48]
fffffe00079b6588:      	stp	q0, q0, [x3, #80]
fffffe00079b658c:      	stp	q0, q0, [x3, #112]
fffffe00079b6590:      	stp	q0, q0, [x3, #144]
fffffe00079b6594:      	stp	q0, q0, [x3, #176]
fffffe00079b6598:      	stp	q0, q0, [x3, #208]
fffffe00079b659c:      	stp	q0, q0, [x3, #240]
fffffe00079b65a0:      	str	q0, [x3]
fffffe00079b65a4:      	cbz	x0, 0xfffffe00079b669c <_nwp_receive+0x158>
fffffe00079b65a8:      	ldr	w22, [x20, #32]
fffffe00079b65ac:      	cbz	w22, 0xfffffe00079b66a4 <_nwp_receive+0x160>
fffffe00079b65b0:      	mov	x21, x1
fffffe00079b65b4:      	cbz	x1, 0xfffffe00079b666c <_nwp_receive+0x128>
fffffe00079b65b8:      	sub	x8, x2, #641
fffffe00079b65bc:      	cmn	x8, #542
fffffe00079b65c0:      	b.lo	0xfffffe00079b666c <_nwp_receive+0x128>
fffffe00079b65c4:      	ldrb	w8, [x21]
fffffe00079b65c8:      	sub	w8, w8, #3
fffffe00079b65cc:      	cmn	w8, #2
fffffe00079b65d0:      	b.lo	0xfffffe00079b666c <_nwp_receive+0x128>
fffffe00079b65d4:      	ldrb	w8, [x21, #1]
fffffe00079b65d8:      	cmp	w8, #3
fffffe00079b65dc:      	b.ne	0xfffffe00079b666c <_nwp_receive+0x128>
fffffe00079b65e0:      	ldrh	w8, [x21, #2]
fffffe00079b65e4:      	sub	x9, x2, #4
fffffe00079b65e8:      	rev16	w8, w8
fffffe00079b65ec:      	cmp	x9, x8
fffffe00079b65f0:      	b.ne	0xfffffe00079b666c <_nwp_receive+0x128>
fffffe00079b65f4:      	ldrb	w8, [x21, #4]
fffffe00079b65f8:      	cmp	w8, #2
fffffe00079b65fc:      	b.ne	0xfffffe00079b666c <_nwp_receive+0x128>
fffffe00079b6600:      	ldurh	w8, [x21, #97]
fffffe00079b6604:      	rev16	w1, w8
fffffe00079b6608:      	sub	x8, x2, #99
fffffe00079b660c:      	cmp	x8, x1
fffffe00079b6610:      	b.ne	0xfffffe00079b666c <_nwp_receive+0x128>
fffffe00079b6614:      	ldurh	w8, [x21, #5]
fffffe00079b6618:      	mov	w9, #4994
fffffe00079b661c:      	rev16	w8, w8
fffffe00079b6620:      	cmp	w8, w9
fffffe00079b6624:      	b.ne	0xfffffe00079b66e0 <_nwp_receive+0x19c>
fffffe00079b6628:      	adrp	x8, 0xfffffe0007c98000 <_group_receive+0x430>
fffffe00079b662c:      	nop
fffffe00079b6630:      	ldur	x9, [x29, #-56]
fffffe00079b6634:      	ldr	x8, [x8, #3440]
fffffe00079b6638:      	cmp	x8, x9
fffffe00079b663c:      	b.ne	0xfffffe00079b6c38 <_nwp_receive+0x6f4>
fffffe00079b6640:      	mov	x0, x20
fffffe00079b6644:      	mov	x1, x21
fffffe00079b6648:      	mov	x3, x19
fffffe00079b664c:      	add	sp, sp, #688
fffffe00079b6650:      	ldp	x29, x30, [sp, #48]
fffffe00079b6654:      	ldp	x20, x19, [sp, #32]
fffffe00079b6658:      	ldp	x22, x21, [sp, #16]
fffffe00079b665c:      	ldp	x24, x23, [sp], #64
fffffe00079b6660:      	b	0xfffffe00079b6d80 <_group_receive>
fffffe00079b6664:      	mov	w0, #-1
fffffe00079b6668:      	b	0xfffffe00079b66b0 <_nwp_receive+0x16c>
fffffe00079b666c:      	mov	x8, xzr
fffffe00079b6670:      	strb	wzr, [x20, x8]
fffffe00079b6674:      	add	x8, x8, #1
fffffe00079b6678:      	cmp	x8, #2616
fffffe00079b667c:      	b.ne	0xfffffe00079b6670 <_nwp_receive+0x12c>
fffffe00079b6680:      	mov	x8, xzr
fffffe00079b6684:      	strb	wzr, [x19, x8]
fffffe00079b6688:      	add	x8, x8, #1
fffffe00079b668c:      	cmp	x8, #280
fffffe00079b6690:      	b.ne	0xfffffe00079b6684 <_nwp_receive+0x140>
fffffe00079b6694:      	mov	w8, #3
fffffe00079b6698:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b669c:      	mov	w8, #1
fffffe00079b66a0:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b66a4:      	mov	w8, #2
fffffe00079b66a8:      	mov	w0, #-1
fffffe00079b66ac:      	str	w8, [x19, #8]
fffffe00079b66b0:      	adrp	x8, 0xfffffe0007c98000 <_group_receive+0x4b8>
fffffe00079b66b4:      	nop
fffffe00079b66b8:      	ldur	x9, [x29, #-56]
fffffe00079b66bc:      	ldr	x8, [x8, #3440]
fffffe00079b66c0:      	cmp	x8, x9
fffffe00079b66c4:      	b.ne	0xfffffe00079b6c38 <_nwp_receive+0x6f4>
fffffe00079b66c8:      	add	sp, sp, #688
fffffe00079b66cc:      	ldp	x29, x30, [sp, #48]
fffffe00079b66d0:      	ldp	x20, x19, [sp, #32]
fffffe00079b66d4:      	ldp	x22, x21, [sp, #16]
fffffe00079b66d8:      	ldp	x24, x23, [sp], #64
fffffe00079b66dc:      	ret
fffffe00079b66e0:      	ldurh	w9, [x21, #7]
fffffe00079b66e4:      	rev16	w9, w9
fffffe00079b66e8:      	cmp	w9, #16
fffffe00079b66ec:      	b.ne	0xfffffe00079b6724 <_nwp_receive+0x1e0>
fffffe00079b66f0:      	ldr	w9, [x20, #232]
fffffe00079b66f4:      	cbz	w9, 0xfffffe00079b6754 <_nwp_receive+0x210>
fffffe00079b66f8:      	mov	x8, xzr
fffffe00079b66fc:      	strb	wzr, [x20, x8]
fffffe00079b6700:      	add	x8, x8, #1
fffffe00079b6704:      	cmp	x8, #2616
fffffe00079b6708:      	b.ne	0xfffffe00079b66fc <_nwp_receive+0x1b8>
fffffe00079b670c:      	mov	x8, xzr
fffffe00079b6710:      	strb	wzr, [x19, x8]
fffffe00079b6714:      	add	x8, x8, #1
fffffe00079b6718:      	cmp	x8, #280
fffffe00079b671c:      	b.ne	0xfffffe00079b6710 <_nwp_receive+0x1cc>
fffffe00079b6720:      	b	0xfffffe00079b6830 <_nwp_receive+0x2ec>
fffffe00079b6724:      	mov	x8, xzr
fffffe00079b6728:      	strb	wzr, [x20, x8]
fffffe00079b672c:      	add	x8, x8, #1
fffffe00079b6730:      	cmp	x8, #2616
fffffe00079b6734:      	b.ne	0xfffffe00079b6728 <_nwp_receive+0x1e4>
fffffe00079b6738:      	mov	x8, xzr
fffffe00079b673c:      	strb	wzr, [x19, x8]
fffffe00079b6740:      	add	x8, x8, #1
fffffe00079b6744:      	cmp	x8, #280
fffffe00079b6748:      	b.ne	0xfffffe00079b673c <_nwp_receive+0x1f8>
fffffe00079b674c:      	mov	w8, #4
fffffe00079b6750:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6754:      	mov	w9, #5066
fffffe00079b6758:      	cmp	w8, w9
fffffe00079b675c:      	b.eq	0xfffffe00079b6800 <_nwp_receive+0x2bc>
fffffe00079b6760:      	cmp	w8, #138
fffffe00079b6764:      	b.ne	0xfffffe00079b6808 <_nwp_receive+0x2c4>
fffffe00079b6768:      	mov	x23, x21
fffffe00079b676c:      	ldr	q0, [x23, #17]!
fffffe00079b6770:      	ldr	q1, [x23, #16]
fffffe00079b6774:      	orr.16b	v0, v0, v1
fffffe00079b6778:      	ext.16b	v1, v0, v0, #8
fffffe00079b677c:      	orr.8b	v0, v0, v1
fffffe00079b6780:      	umov.b	w8, v0[1]
fffffe00079b6784:      	umov.b	w9, v0[0]
fffffe00079b6788:      	umov.b	w10, v0[2]
fffffe00079b678c:      	umov.b	w11, v0[3]
fffffe00079b6790:      	umov.b	w12, v0[4]
fffffe00079b6794:      	orr	w8, w9, w8
fffffe00079b6798:      	umov.b	w9, v0[5]
fffffe00079b679c:      	orr	w8, w8, w10
fffffe00079b67a0:      	umov.b	w10, v0[6]
fffffe00079b67a4:      	orr	w8, w8, w11
fffffe00079b67a8:      	umov.b	w11, v0[7]
fffffe00079b67ac:      	orr	w8, w8, w12
fffffe00079b67b0:      	orr	w8, w8, w9
fffffe00079b67b4:      	orr	w8, w8, w10
fffffe00079b67b8:      	orr	w8, w8, w11
fffffe00079b67bc:      	tst	w8, #0xff
fffffe00079b67c0:      	b.eq	0xfffffe00079b67d0 <_nwp_receive+0x28c>
fffffe00079b67c4:      	add	x0, x21, #99
fffffe00079b67c8:      	bl	0xfffffe00079b7828 <_m1_data>
fffffe00079b67cc:      	cbz	w0, 0xfffffe00079b6918 <_nwp_receive+0x3d4>
fffffe00079b67d0:      	mov	x8, xzr
fffffe00079b67d4:      	strb	wzr, [x20, x8]
fffffe00079b67d8:      	add	x8, x8, #1
fffffe00079b67dc:      	cmp	x8, #2616
fffffe00079b67e0:      	b.ne	0xfffffe00079b67d4 <_nwp_receive+0x290>
fffffe00079b67e4:      	mov	x8, xzr
fffffe00079b67e8:      	strb	wzr, [x19, x8]
fffffe00079b67ec:      	add	x8, x8, #1
fffffe00079b67f0:      	cmp	x8, #280
fffffe00079b67f4:      	b.ne	0xfffffe00079b67e8 <_nwp_receive+0x2a4>
fffffe00079b67f8:      	mov	w8, #5
fffffe00079b67fc:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6800:      	cmp	w22, #1
fffffe00079b6804:      	b.hi	0xfffffe00079b6838 <_nwp_receive+0x2f4>
fffffe00079b6808:      	mov	x8, xzr
fffffe00079b680c:      	strb	wzr, [x20, x8]
fffffe00079b6810:      	add	x8, x8, #1
fffffe00079b6814:      	cmp	x8, #2616
fffffe00079b6818:      	b.ne	0xfffffe00079b680c <_nwp_receive+0x2c8>
fffffe00079b681c:      	mov	x8, xzr
fffffe00079b6820:      	strb	wzr, [x19, x8]
fffffe00079b6824:      	add	x8, x8, #1
fffffe00079b6828:      	cmp	x8, #280
fffffe00079b682c:      	b.ne	0xfffffe00079b6820 <_nwp_receive+0x2dc>
fffffe00079b6830:      	mov	w8, #7
fffffe00079b6834:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6838:      	sub	x3, x29, #80
fffffe00079b683c:      	mov	x0, x20
fffffe00079b6840:      	mov	x1, x21
fffffe00079b6844:      	stp	xzr, xzr, [x29, #-80]
fffffe00079b6848:      	str	x2, [sp]
fffffe00079b684c:      	bl	0xfffffe00079b7bd4 <_mic>
fffffe00079b6850:      	ldur	q0, [x29, #-80]
fffffe00079b6854:      	ldur	q1, [x21, #81]
fffffe00079b6858:      	sturb	wzr, [x29, #-80]
fffffe00079b685c:      	sturb	wzr, [x29, #-79]
fffffe00079b6860:      	sturb	wzr, [x29, #-78]
fffffe00079b6864:      	eor.16b	v0, v1, v0
fffffe00079b6868:      	sturb	wzr, [x29, #-77]
fffffe00079b686c:      	ext.16b	v1, v0, v0, #8
fffffe00079b6870:      	sturb	wzr, [x29, #-76]
fffffe00079b6874:      	sturb	wzr, [x29, #-75]
fffffe00079b6878:      	sturb	wzr, [x29, #-74]
fffffe00079b687c:      	sturb	wzr, [x29, #-73]
fffffe00079b6880:      	orr.8b	v0, v0, v1
fffffe00079b6884:      	sturb	wzr, [x29, #-72]
fffffe00079b6888:      	umov.b	w8, v0[1]
fffffe00079b688c:      	umov.b	w9, v0[0]
fffffe00079b6890:      	umov.b	w10, v0[2]
fffffe00079b6894:      	umov.b	w11, v0[3]
fffffe00079b6898:      	umov.b	w12, v0[4]
fffffe00079b689c:      	sturb	wzr, [x29, #-71]
fffffe00079b68a0:      	sturb	wzr, [x29, #-70]
fffffe00079b68a4:      	orr	w8, w9, w8
fffffe00079b68a8:      	umov.b	w9, v0[5]
fffffe00079b68ac:      	orr	w8, w8, w10
fffffe00079b68b0:      	umov.b	w10, v0[6]
fffffe00079b68b4:      	orr	w8, w8, w11
fffffe00079b68b8:      	umov.b	w11, v0[7]
fffffe00079b68bc:      	orr	w8, w8, w12
fffffe00079b68c0:      	sturb	wzr, [x29, #-69]
fffffe00079b68c4:      	orr	w8, w8, w9
fffffe00079b68c8:      	sturb	wzr, [x29, #-68]
fffffe00079b68cc:      	orr	w8, w8, w10
fffffe00079b68d0:      	sturb	wzr, [x29, #-67]
fffffe00079b68d4:      	orr	w8, w8, w11
fffffe00079b68d8:      	sturb	wzr, [x29, #-66]
fffffe00079b68dc:      	tst	w8, #0xff
fffffe00079b68e0:      	sturb	wzr, [x29, #-65]
fffffe00079b68e4:      	b.eq	0xfffffe00079b69c4 <_nwp_receive+0x480>
fffffe00079b68e8:      	mov	x8, xzr
fffffe00079b68ec:      	strb	wzr, [x20, x8]
fffffe00079b68f0:      	add	x8, x8, #1
fffffe00079b68f4:      	cmp	x8, #2616
fffffe00079b68f8:      	b.ne	0xfffffe00079b68ec <_nwp_receive+0x3a8>
fffffe00079b68fc:      	mov	x8, xzr
fffffe00079b6900:      	strb	wzr, [x19, x8]
fffffe00079b6904:      	add	x8, x8, #1
fffffe00079b6908:      	cmp	x8, #280
fffffe00079b690c:      	b.ne	0xfffffe00079b6900 <_nwp_receive+0x3bc>
fffffe00079b6910:      	mov	w8, #8
fffffe00079b6914:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6918:      	cmp	w22, #1
fffffe00079b691c:      	b.eq	0xfffffe00079b6a48 <_nwp_receive+0x504>
fffffe00079b6920:      	cmp	w22, #2
fffffe00079b6924:      	b.ne	0xfffffe00079b6994 <_nwp_receive+0x450>
fffffe00079b6928:      	add	x22, x20, #1691
fffffe00079b692c:      	add	x0, x21, #9
fffffe00079b6930:      	mov	x1, x22
fffffe00079b6934:      	mov	w2, #8
fffffe00079b6938:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b693c:      	tbnz	w0, #31, 0xfffffe00079b6994 <_nwp_receive+0x450>
fffffe00079b6940:      	ldur	q0, [x21, #17]
fffffe00079b6944:      	ldur	q1, [x20, #124]
fffffe00079b6948:      	ldur	q2, [x21, #33]
fffffe00079b694c:      	ldur	q3, [x20, #140]
fffffe00079b6950:      	eor.16b	v0, v1, v0
fffffe00079b6954:      	eor.16b	v1, v3, v2
fffffe00079b6958:      	orr.16b	v0, v0, v1
fffffe00079b695c:      	ushll2.8h	v1, v0, #0
fffffe00079b6960:      	ushll.8h	v0, v0, #0
fffffe00079b6964:      	ext.16b	v2, v1, v1, #8
fffffe00079b6968:      	ext.16b	v3, v0, v0, #8
fffffe00079b696c:      	orr.8b	v0, v0, v1
fffffe00079b6970:      	orr.8b	v1, v3, v2
fffffe00079b6974:      	orr.8b	v0, v0, v1
fffffe00079b6978:      	ushll.4s	v0, v0, #0
fffffe00079b697c:      	ext.16b	v1, v0, v0, #8
fffffe00079b6980:      	orr.8b	v0, v0, v1
fffffe00079b6984:      	mov.s	w8, v0[1]
fffffe00079b6988:      	fmov	w9, s0
fffffe00079b698c:      	orr	w8, w9, w8
fffffe00079b6990:      	cbz	w8, 0xfffffe00079b6b6c <_nwp_receive+0x628>
fffffe00079b6994:      	mov	x8, xzr
fffffe00079b6998:      	strb	wzr, [x20, x8]
fffffe00079b699c:      	add	x8, x8, #1
fffffe00079b69a0:      	cmp	x8, #2616
fffffe00079b69a4:      	b.ne	0xfffffe00079b6998 <_nwp_receive+0x454>
fffffe00079b69a8:      	mov	x8, xzr
fffffe00079b69ac:      	strb	wzr, [x19, x8]
fffffe00079b69b0:      	add	x8, x8, #1
fffffe00079b69b4:      	cmp	x8, #280
fffffe00079b69b8:      	b.ne	0xfffffe00079b69ac <_nwp_receive+0x468>
fffffe00079b69bc:      	mov	w8, #6
fffffe00079b69c0:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b69c4:      	ldur	q0, [x21, #17]
fffffe00079b69c8:      	ldur	q1, [x20, #124]
fffffe00079b69cc:      	ldur	q2, [x21, #33]
fffffe00079b69d0:      	ldur	q3, [x20, #140]
fffffe00079b69d4:      	eor.16b	v0, v1, v0
fffffe00079b69d8:      	eor.16b	v1, v3, v2
fffffe00079b69dc:      	orr.16b	v0, v0, v1
fffffe00079b69e0:      	ushll2.8h	v1, v0, #0
fffffe00079b69e4:      	ushll.8h	v0, v0, #0
fffffe00079b69e8:      	ext.16b	v2, v1, v1, #8
fffffe00079b69ec:      	ext.16b	v3, v0, v0, #8
fffffe00079b69f0:      	orr.8b	v0, v0, v1
fffffe00079b69f4:      	orr.8b	v1, v3, v2
fffffe00079b69f8:      	orr.8b	v0, v0, v1
fffffe00079b69fc:      	ushll.4s	v0, v0, #0
fffffe00079b6a00:      	ext.16b	v1, v0, v0, #8
fffffe00079b6a04:      	orr.8b	v0, v0, v1
fffffe00079b6a08:      	mov.s	w8, v0[1]
fffffe00079b6a0c:      	fmov	w9, s0
fffffe00079b6a10:      	orr	w8, w9, w8
fffffe00079b6a14:      	cbz	w8, 0xfffffe00079b6a58 <_nwp_receive+0x514>
fffffe00079b6a18:      	mov	x8, xzr
fffffe00079b6a1c:      	strb	wzr, [x20, x8]
fffffe00079b6a20:      	add	x8, x8, #1
fffffe00079b6a24:      	cmp	x8, #2616
fffffe00079b6a28:      	b.ne	0xfffffe00079b6a1c <_nwp_receive+0x4d8>
fffffe00079b6a2c:      	mov	x8, xzr
fffffe00079b6a30:      	strb	wzr, [x19, x8]
fffffe00079b6a34:      	add	x8, x8, #1
fffffe00079b6a38:      	cmp	x8, #280
fffffe00079b6a3c:      	b.ne	0xfffffe00079b6a30 <_nwp_receive+0x4ec>
fffffe00079b6a40:      	mov	w8, #13
fffffe00079b6a44:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6a48:      	ldr	x8, [x20, #16]
fffffe00079b6a4c:      	cbz	x8, 0xfffffe00079b6ab0 <_nwp_receive+0x56c>
fffffe00079b6a50:      	mov	w0, #4
fffffe00079b6a54:      	b	0xfffffe00079b66b0 <_nwp_receive+0x16c>
fffffe00079b6a58:      	ldr	w8, [x20, #2608]
fffffe00079b6a5c:      	cbz	w8, 0xfffffe00079b6b18 <_nwp_receive+0x5d4>
fffffe00079b6a60:      	ldr	x9, [sp]
fffffe00079b6a64:      	cmp	x8, x9
fffffe00079b6a68:      	b.ne	0xfffffe00079b6a80 <_nwp_receive+0x53c>
fffffe00079b6a6c:      	add	x0, x21, #9
fffffe00079b6a70:      	add	x1, x20, #1977
fffffe00079b6a74:      	mov	w2, #8
fffffe00079b6a78:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b6a7c:      	tbz	w0, #31, 0xfffffe00079b6b80 <_nwp_receive+0x63c>
fffffe00079b6a80:      	mov	x8, xzr
fffffe00079b6a84:      	strb	wzr, [x20, x8]
fffffe00079b6a88:      	add	x8, x8, #1
fffffe00079b6a8c:      	cmp	x8, #2616
fffffe00079b6a90:      	b.ne	0xfffffe00079b6a84 <_nwp_receive+0x540>
fffffe00079b6a94:      	mov	x8, xzr
fffffe00079b6a98:      	strb	wzr, [x19, x8]
fffffe00079b6a9c:      	add	x8, x8, #1
fffffe00079b6aa0:      	cmp	x8, #280
fffffe00079b6aa4:      	b.ne	0xfffffe00079b6a98 <_nwp_receive+0x554>
fffffe00079b6aa8:      	mov	w8, #9
fffffe00079b6aac:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6ab0:      	ldp	q1, q0, [x23]
fffffe00079b6ab4:      	add	x8, x20, #1691
fffffe00079b6ab8:      	mov	x0, x20
fffffe00079b6abc:      	stur	q1, [x20, #124]
fffffe00079b6ac0:      	stur	q0, [x20, #140]
fffffe00079b6ac4:      	ldur	x9, [x21, #9]
fffffe00079b6ac8:      	str	x9, [x8]
fffffe00079b6acc:      	bl	0xfffffe00079b7910 <_derive_keys>
fffffe00079b6ad0:      	mov	w8, #2
fffffe00079b6ad4:      	str	w8, [x20, #32]
fffffe00079b6ad8:      	adrp	x8, 0xfffffe0007c98000 <_group_receive+0x8e0>
fffffe00079b6adc:      	nop
fffffe00079b6ae0:      	ldur	x9, [x29, #-56]
fffffe00079b6ae4:      	ldr	x8, [x8, #3440]
fffffe00079b6ae8:      	cmp	x8, x9
fffffe00079b6aec:      	b.ne	0xfffffe00079b6c38 <_nwp_receive+0x6f4>
fffffe00079b6af0:      	mov	x0, x20
fffffe00079b6af4:      	mov	x1, x21
fffffe00079b6af8:      	mov	w2, #1
fffffe00079b6afc:      	mov	x3, x19
fffffe00079b6b00:      	add	sp, sp, #688
fffffe00079b6b04:      	ldp	x29, x30, [sp, #48]
fffffe00079b6b08:      	ldp	x20, x19, [sp, #32]
fffffe00079b6b0c:      	ldp	x22, x21, [sp, #16]
fffffe00079b6b10:      	ldp	x24, x23, [sp], #64
fffffe00079b6b14:      	b	0xfffffe00079b631c <_reply>
fffffe00079b6b18:      	ldr	w8, [x20, #32]
fffffe00079b6b1c:      	cmp	w8, #2
fffffe00079b6b20:      	b.ne	0xfffffe00079b6b3c <_nwp_receive+0x5f8>
fffffe00079b6b24:      	add	x0, x21, #9
fffffe00079b6b28:      	add	x1, x20, #1691
fffffe00079b6b2c:      	mov	w2, #8
fffffe00079b6b30:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b6b34:      	cmp	w0, #0
fffffe00079b6b38:      	b.gt	0xfffffe00079b6c3c <_nwp_receive+0x6f8>
fffffe00079b6b3c:      	mov	x8, xzr
fffffe00079b6b40:      	strb	wzr, [x20, x8]
fffffe00079b6b44:      	add	x8, x8, #1
fffffe00079b6b48:      	cmp	x8, #2616
fffffe00079b6b4c:      	b.ne	0xfffffe00079b6b40 <_nwp_receive+0x5fc>
fffffe00079b6b50:      	mov	x8, xzr
fffffe00079b6b54:      	strb	wzr, [x19, x8]
fffffe00079b6b58:      	add	x8, x8, #1
fffffe00079b6b5c:      	cmp	x8, #280
fffffe00079b6b60:      	b.ne	0xfffffe00079b6b54 <_nwp_receive+0x610>
fffffe00079b6b64:      	mov	w8, #11
fffffe00079b6b68:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6b6c:      	ldr	x8, [x20, #16]
fffffe00079b6b70:      	cbnz	x8, 0xfffffe00079b6a50 <_nwp_receive+0x50c>
fffffe00079b6b74:      	ldur	x8, [x21, #9]
fffffe00079b6b78:      	str	x8, [x22]
fffffe00079b6b7c:      	b	0xfffffe00079b6ad8 <_nwp_receive+0x594>
fffffe00079b6b80:      	ldr	x8, [sp]
fffffe00079b6b84:      	cbz	x8, 0xfffffe00079b6bfc <_nwp_receive+0x6b8>
fffffe00079b6b88:      	mov	x8, xzr
fffffe00079b6b8c:      	b	0xfffffe00079b6ba0 <_nwp_receive+0x65c>
fffffe00079b6b90:      	ldr	x9, [sp]
fffffe00079b6b94:      	add	w8, w8, #1
fffffe00079b6b98:      	cmp	x8, x9
fffffe00079b6b9c:      	b.hs	0xfffffe00079b6bfc <_nwp_receive+0x6b8>
fffffe00079b6ba0:      	sub	w9, w8, #9
fffffe00079b6ba4:      	cmp	w9, #8
fffffe00079b6ba8:      	b.lo	0xfffffe00079b6b90 <_nwp_receive+0x64c>
fffffe00079b6bac:      	sub	w9, w8, #81
fffffe00079b6bb0:      	cmp	w9, #16
fffffe00079b6bb4:      	b.lo	0xfffffe00079b6b90 <_nwp_receive+0x64c>
fffffe00079b6bb8:      	add	x9, x20, x8
fffffe00079b6bbc:      	ldrb	w10, [x21, x8]
fffffe00079b6bc0:      	ldrb	w9, [x9, #1968]
fffffe00079b6bc4:      	cmp	w10, w9
fffffe00079b6bc8:      	b.eq	0xfffffe00079b6b90 <_nwp_receive+0x64c>
fffffe00079b6bcc:      	mov	x8, xzr
fffffe00079b6bd0:      	strb	wzr, [x20, x8]
fffffe00079b6bd4:      	add	x8, x8, #1
fffffe00079b6bd8:      	cmp	x8, #2616
fffffe00079b6bdc:      	b.ne	0xfffffe00079b6bd0 <_nwp_receive+0x68c>
fffffe00079b6be0:      	mov	x8, xzr
fffffe00079b6be4:      	strb	wzr, [x19, x8]
fffffe00079b6be8:      	add	x8, x8, #1
fffffe00079b6bec:      	cmp	x8, #280
fffffe00079b6bf0:      	b.ne	0xfffffe00079b6be4 <_nwp_receive+0x6a0>
fffffe00079b6bf4:      	mov	w8, #10
fffffe00079b6bf8:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6bfc:      	ldr	x8, [x20, #16]
fffffe00079b6c00:      	cbnz	x8, 0xfffffe00079b6a50 <_nwp_receive+0x50c>
fffffe00079b6c04:      	add	x0, x20, #1968
fffffe00079b6c08:      	mov	x1, x21
fffffe00079b6c0c:      	ldr	x2, [sp]
fffffe00079b6c10:      	mov	w3, #640
fffffe00079b6c14:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b6c18:      	ldur	x8, [x21, #9]
fffffe00079b6c1c:      	mov	x0, x20
fffffe00079b6c20:      	mov	x1, x21
fffffe00079b6c24:      	mov	w2, #3
fffffe00079b6c28:      	str	x8, [x20, #248]
fffffe00079b6c2c:      	mov	x3, x19
fffffe00079b6c30:      	bl	0xfffffe00079b631c <_reply>
fffffe00079b6c34:      	b	0xfffffe00079b66b0 <_nwp_receive+0x16c>
fffffe00079b6c38:      	bl	0xfffffe0007a06354 <___stack_chk_fail>
fffffe00079b6c3c:      	ldr	x8, [x20, #16]
fffffe00079b6c40:      	cbnz	x8, 0xfffffe00079b6a50 <_nwp_receive+0x50c>
fffffe00079b6c44:      	add	x0, sp, #16
fffffe00079b6c48:      	mov	w1, #640
fffffe00079b6c4c:      	add	x23, sp, #16
fffffe00079b6c50:      	bl	0xfffffe0007bad720 <_bzero>
fffffe00079b6c54:      	ldurh	w8, [x21, #97]
fffffe00079b6c58:      	add	x0, x20, #172
fffffe00079b6c5c:      	add	x1, x21, #99
fffffe00079b6c60:      	add	x3, sp, #16
fffffe00079b6c64:      	add	x5, sp, #8
fffffe00079b6c68:      	mov	w4, #640
fffffe00079b6c6c:      	rev16	w22, w8
fffffe00079b6c70:      	str	xzr, [sp, #8]
fffffe00079b6c74:      	mov	x2, x22
fffffe00079b6c78:      	bl	0xfffffe00079b56f4 <_nwpc_unwrap>
fffffe00079b6c7c:      	cbz	w0, 0xfffffe00079b6cc4 <_nwp_receive+0x780>
fffffe00079b6c80:      	mov	x8, xzr
fffffe00079b6c84:      	strb	wzr, [x23, x8]
fffffe00079b6c88:      	add	x8, x8, #1
fffffe00079b6c8c:      	cmp	x8, #640
fffffe00079b6c90:      	b.ne	0xfffffe00079b6c84 <_nwp_receive+0x740>
fffffe00079b6c94:      	mov	x8, xzr
fffffe00079b6c98:      	strb	wzr, [x20, x8]
fffffe00079b6c9c:      	add	x8, x8, #1
fffffe00079b6ca0:      	cmp	x8, #2616
fffffe00079b6ca4:      	b.ne	0xfffffe00079b6c98 <_nwp_receive+0x754>
fffffe00079b6ca8:      	mov	x8, xzr
fffffe00079b6cac:      	strb	wzr, [x19, x8]
fffffe00079b6cb0:      	add	x8, x8, #1
fffffe00079b6cb4:      	cmp	x8, #280
fffffe00079b6cb8:      	b.ne	0xfffffe00079b6cac <_nwp_receive+0x768>
fffffe00079b6cbc:      	mov	w8, #12
fffffe00079b6cc0:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6cc4:      	sub	w2, w22, #8
fffffe00079b6cc8:      	add	x3, x21, #65
fffffe00079b6ccc:      	add	x1, sp, #16
fffffe00079b6cd0:      	mov	x0, x20
fffffe00079b6cd4:      	add	x22, sp, #16
fffffe00079b6cd8:      	bl	0xfffffe00079b7cc8 <_kde>
fffffe00079b6cdc:      	mov	x8, xzr
fffffe00079b6ce0:      	cbz	w0, 0xfffffe00079b6d24 <_nwp_receive+0x7e0>
fffffe00079b6ce4:      	strb	wzr, [x22, x8]
fffffe00079b6ce8:      	add	x8, x8, #1
fffffe00079b6cec:      	cmp	x8, #640
fffffe00079b6cf0:      	b.ne	0xfffffe00079b6ce4 <_nwp_receive+0x7a0>
fffffe00079b6cf4:      	mov	x8, xzr
fffffe00079b6cf8:      	strb	wzr, [x20, x8]
fffffe00079b6cfc:      	add	x8, x8, #1
fffffe00079b6d00:      	cmp	x8, #2616
fffffe00079b6d04:      	b.ne	0xfffffe00079b6cf8 <_nwp_receive+0x7b4>
fffffe00079b6d08:      	mov	x8, xzr
fffffe00079b6d0c:      	strb	wzr, [x19, x8]
fffffe00079b6d10:      	add	x8, x8, #1
fffffe00079b6d14:      	cmp	x8, #280
fffffe00079b6d18:      	b.ne	0xfffffe00079b6d0c <_nwp_receive+0x7c8>
fffffe00079b6d1c:      	mov	w8, #14
fffffe00079b6d20:      	b	0xfffffe00079b66a8 <_nwp_receive+0x164>
fffffe00079b6d24:      	strb	wzr, [x22, x8]
fffffe00079b6d28:      	add	x8, x8, #1
fffffe00079b6d2c:      	cmp	x8, #640
fffffe00079b6d30:      	b.ne	0xfffffe00079b6d24 <_nwp_receive+0x7e0>
fffffe00079b6d34:      	ldr	x22, [sp]
fffffe00079b6d38:      	add	x0, x20, #1968
fffffe00079b6d3c:      	mov	x1, x21
fffffe00079b6d40:      	mov	w3, #640
fffffe00079b6d44:      	mov	x2, x22
fffffe00079b6d48:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b6d4c:      	str	w22, [x20, #2608]
fffffe00079b6d50:      	ldur	q0, [x20, #204]
fffffe00079b6d54:      	mov	w8, #1
fffffe00079b6d58:      	ldur	x9, [x21, #9]
fffffe00079b6d5c:      	mov	w10, #3
fffffe00079b6d60:      	mov	x0, x20
fffffe00079b6d64:      	mov	x1, x21
fffffe00079b6d68:      	mov	w2, #2
fffffe00079b6d6c:      	str	q0, [x20, #896]
fffffe00079b6d70:      	str	w8, [x20, #236]
fffffe00079b6d74:      	str	x9, [x20, #248]
fffffe00079b6d78:      	str	w10, [x20, #32]
fffffe00079b6d7c:      	b	0xfffffe00079b6c2c <_nwp_receive+0x6e8>
