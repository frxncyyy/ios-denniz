
$NORD2_REPO/research/ci-39865241324/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aa798 <_sw_sta>:
fffffe00079aa798:      	sub	sp, sp, #224
fffffe00079aa79c:      	stp	x29, x30, [sp, #208]
fffffe00079aa7a0:      	add	x29, sp, #208
fffffe00079aa7a4:      	adrp	x8, 0xfffffe0007c98000 <_nst_close_port+0x38>
fffffe00079aa7a8:      	mov	x9, sp
fffffe00079aa7ac:      	movi.2d	v0, #0000000000000000
fffffe00079aa7b0:      	nop
fffffe00079aa7b4:      	ldr	x8, [x8, #3440]
fffffe00079aa7b8:      	stur	x8, [x29, #-8]
fffffe00079aa7bc:      	str	xzr, [sp, #192]
fffffe00079aa7c0:      	stur	q0, [x9, #178]
fffffe00079aa7c4:      	stur	q0, [x9, #162]
fffffe00079aa7c8:      	stur	q0, [x9, #146]
fffffe00079aa7cc:      	stur	q0, [x9, #130]
fffffe00079aa7d0:      	stur	q0, [sp, #114]
fffffe00079aa7d4:      	stur	q0, [sp, #98]
fffffe00079aa7d8:      	stur	q0, [sp, #82]
fffffe00079aa7dc:      	stur	q0, [sp, #66]
fffffe00079aa7e0:      	stur	q0, [sp, #50]
fffffe00079aa7e4:      	stur	q0, [sp, #34]
fffffe00079aa7e8:      	stur	q0, [sp, #18]
fffffe00079aa7ec:      	cbz	x2, 0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa7f0:      	str	xzr, [x2]
fffffe00079aa7f4:      	cbz	x3, 0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa7f8:      	mov	x11, x3
fffffe00079aa7fc:      	ldrb	w8, [x11, #7]!
fffffe00079aa800:      	tbnz	w8, #0, 0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa804:      	ldrb	w9, [x3, #8]
fffffe00079aa808:      	ldrb	w10, [x3, #9]
fffffe00079aa80c:      	ldrb	w12, [x3, #10]
fffffe00079aa810:      	ldrb	w13, [x3, #11]
fffffe00079aa814:      	orr	w9, w9, w10
fffffe00079aa818:      	ldrb	w10, [x3, #12]
fffffe00079aa81c:      	orr	w9, w9, w12
fffffe00079aa820:      	orr	w9, w9, w13
fffffe00079aa824:      	orr	w9, w9, w10
fffffe00079aa828:      	orr	w8, w9, w8
fffffe00079aa82c:      	cbz	w8, 0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa830:      	ldrb	w9, [x3, #5]
fffffe00079aa834:      	sub	w8, w9, #4
fffffe00079aa838:      	cmn	w8, #3
fffffe00079aa83c:      	b.lo	0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa840:      	ldrb	w10, [x3, #6]
fffffe00079aa844:      	cmp	w10, #1
fffffe00079aa848:      	b.hi	0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa84c:      	ldrb	w8, [x3, #4]
fffffe00079aa850:      	cmp	w8, #65
fffffe00079aa854:      	b.ne	0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa858:      	ldrh	w15, [x3, #18]
fffffe00079aa85c:      	cmp	w15, #2007
fffffe00079aa860:      	b.hi	0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa864:      	ldrh	w14, [x3, #20]
fffffe00079aa868:      	cbz	w14, 0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa86c:      	ldrb	w12, [x3, #2]
fffffe00079aa870:      	cmp	w12, #255
fffffe00079aa874:      	b.eq	0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa878:      	ldrb	w8, [x3, #3]
fffffe00079aa87c:      	cmp	w8, #255
fffffe00079aa880:      	b.ne	0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa884:      	ldrb	w13, [x3, #16]
fffffe00079aa888:      	cmp	w13, #1
fffffe00079aa88c:      	b.ls	0xfffffe00079aa8bc <_sw_sta+0x124>
fffffe00079aa890:      	mov	w8, #-1
fffffe00079aa894:      	adrp	x9, 0xfffffe0007c98000 <_nst_lease_window+0xb8>
fffffe00079aa898:      	nop
fffffe00079aa89c:      	ldur	x10, [x29, #-8]
fffffe00079aa8a0:      	ldr	x9, [x9, #3440]
fffffe00079aa8a4:      	cmp	x9, x10
fffffe00079aa8a8:      	b.ne	0xfffffe00079aa9e0 <_sw_sta+0x248>
fffffe00079aa8ac:      	ldp	x29, x30, [sp, #208]
fffffe00079aa8b0:      	mov	w0, w8
fffffe00079aa8b4:      	add	sp, sp, #224
fffffe00079aa8b8:      	ret
fffffe00079aa8bc:      	ldrh	w16, [x3, #22]
fffffe00079aa8c0:      	mov	w8, #-1
fffffe00079aa8c4:      	cbz	w16, 0xfffffe00079aa894 <_sw_sta+0xfc>
fffffe00079aa8c8:      	ldrh	w17, [x3, #24]
fffffe00079aa8cc:      	cbz	w17, 0xfffffe00079aa894 <_sw_sta+0xfc>
fffffe00079aa8d0:      	mov	w4, #49200
fffffe00079aa8d4:      	and	w4, w16, w4
fffffe00079aa8d8:      	cbnz	w4, 0xfffffe00079aa894 <_sw_sta+0xfc>
fffffe00079aa8dc:      	ldrb	w4, [x3, #13]
fffffe00079aa8e0:      	mov	w8, #-1
fffffe00079aa8e4:      	sub	w5, w4, #16
fffffe00079aa8e8:      	cmn	w5, #15
fffffe00079aa8ec:      	b.lo	0xfffffe00079aa894 <_sw_sta+0xfc>
fffffe00079aa8f0:      	bic	w5, w17, w16
fffffe00079aa8f4:      	cbnz	w5, 0xfffffe00079aa894 <_sw_sta+0xfc>
fffffe00079aa8f8:      	ldrb	w8, [x3, #1]
fffffe00079aa8fc:      	mov	w5, #65
fffffe00079aa900:      	ldr	w6, [x11]
fffffe00079aa904:      	strb	w9, [sp, #20]
fffffe00079aa908:      	strb	w10, [sp, #51]
fffffe00079aa90c:      	ldrb	w9, [x3, #15]
fffffe00079aa910:      	strb	w8, [sp]
fffffe00079aa914:      	ldrh	w8, [x11, #4]
fffffe00079aa918:      	lsr	w11, w15, #8
fffffe00079aa91c:      	ldrh	w10, [x3, #26]
fffffe00079aa920:      	strb	w5, [sp, #1]
fffffe00079aa924:      	strh	w8, [sp, #6]
fffffe00079aa928:      	lsr	w8, w14, #8
fffffe00079aa92c:      	strb	w11, [sp, #9]
fffffe00079aa930:      	ldrb	w11, [x3]
fffffe00079aa934:      	stur	w6, [sp, #2]
fffffe00079aa938:      	strb	w8, [sp, #11]
fffffe00079aa93c:      	lsr	w8, w16, #8
fffffe00079aa940:      	strb	w11, [sp, #12]
fffffe00079aa944:      	lsr	w11, w17, #8
fffffe00079aa948:      	strb	w15, [sp, #8]
fffffe00079aa94c:      	strb	w8, [sp, #15]
fffffe00079aa950:      	ldrb	w8, [x3, #14]
fffffe00079aa954:      	strb	w14, [sp, #10]
fffffe00079aa958:      	strb	w4, [sp, #13]
fffffe00079aa95c:      	strb	w8, [sp, #50]
fffffe00079aa960:      	mov	w8, #255
fffffe00079aa964:      	strb	w16, [sp, #14]
fffffe00079aa968:      	strb	w17, [sp, #16]
fffffe00079aa96c:      	strb	w11, [sp, #17]
fffffe00079aa970:      	strb	w13, [sp, #23]
fffffe00079aa974:      	strb	w12, [sp, #54]
fffffe00079aa978:      	strb	w8, [sp, #55]
fffffe00079aa97c:      	strb	w9, [sp, #68]
fffffe00079aa980:      	strh	w10, [sp, #74]
fffffe00079aa984:      	cbz	x0, 0xfffffe00079aa890 <_sw_sta+0xf8>
fffffe00079aa988:      	cmp	x1, #200
fffffe00079aa98c:      	b.hs	0xfffffe00079aa998 <_sw_sta+0x200>
fffffe00079aa990:      	mov	w8, #-2
fffffe00079aa994:      	b	0xfffffe00079aa894 <_sw_sta+0xfc>
fffffe00079aa998:      	ldp	q0, q1, [sp, #160]
fffffe00079aa99c:      	mov	w8, wzr
fffffe00079aa9a0:      	ldp	q2, q3, [sp, #96]
fffffe00079aa9a4:      	stp	q0, q1, [x0, #160]
fffffe00079aa9a8:      	ldp	q1, q0, [sp, #128]
fffffe00079aa9ac:      	stp	q2, q3, [x0, #96]
fffffe00079aa9b0:      	ldp	q2, q3, [sp, #32]
fffffe00079aa9b4:      	stp	q1, q0, [x0, #128]
fffffe00079aa9b8:      	ldp	q1, q0, [sp, #64]
fffffe00079aa9bc:      	stp	q2, q3, [x0, #32]
fffffe00079aa9c0:      	ldr	x9, [sp, #192]
fffffe00079aa9c4:      	stp	q1, q0, [x0, #64]
fffffe00079aa9c8:      	ldp	q0, q2, [sp]
fffffe00079aa9cc:      	str	x9, [x0, #192]
fffffe00079aa9d0:      	mov	w9, #200
fffffe00079aa9d4:      	stp	q0, q2, [x0]
fffffe00079aa9d8:      	str	x9, [x2]
fffffe00079aa9dc:      	b	0xfffffe00079aa894 <_sw_sta+0xfc>
fffffe00079aa9e0:      	bl	0xfffffe0007a06554 <___stack_chk_fail>
