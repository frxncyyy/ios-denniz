
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079af84c <_nsdd_classify>:
fffffe00079af84c:      	sub	sp, sp, #16
fffffe00079af850:      	mov	w8, #-1
fffffe00079af854:      	cbz	x0, 0xfffffe00079af978 <_nsdd_classify+0x12c>
fffffe00079af858:      	stp	xzr, xzr, [x0]
fffffe00079af85c:      	str	wzr, [x0, #16]
fffffe00079af860:      	cbz	x1, 0xfffffe00079af978 <_nsdd_classify+0x12c>
fffffe00079af864:      	sub	x9, x2, #1519
fffffe00079af868:      	cmn	x9, #1505
fffffe00079af86c:      	b.lo	0xfffffe00079af978 <_nsdd_classify+0x12c>
fffffe00079af870:      	str	w2, [x0, #4]
fffffe00079af874:      	ldrh	w8, [x1, #12]
fffffe00079af878:      	rev16	w8, w8
fffffe00079af87c:      	str	w8, [x0]
fffffe00079af880:      	ldrb	w9, [x1]
fffffe00079af884:      	and	w10, w9, #0x1
fffffe00079af888:      	str	w10, [x0, #8]
fffffe00079af88c:      	tbz	w9, #0, 0xfffffe00079af8cc <_nsdd_classify+0x80>
fffffe00079af890:      	ldur	s0, [x1, #2]
fffffe00079af894:      	ushll.8h	v0, v0, #0
fffffe00079af898:      	xtn.8b	v0, v0
fffffe00079af89c:      	fmov	w9, s0
fffffe00079af8a0:      	cmn	w9, #1
fffffe00079af8a4:      	b.ne	0xfffffe00079af8cc <_nsdd_classify+0x80>
fffffe00079af8a8:      	ldrb	w9, [x1, #1]
fffffe00079af8ac:      	mov	w10, #255
fffffe00079af8b0:      	bics	wzr, w10, w9
fffffe00079af8b4:      	b.ne	0xfffffe00079af8cc <_nsdd_classify+0x80>
fffffe00079af8b8:      	ldrb	w9, [x1]
fffffe00079af8bc:      	cmp	w9, #255
fffffe00079af8c0:      	b.ne	0xfffffe00079af8cc <_nsdd_classify+0x80>
fffffe00079af8c4:      	mov	w9, #2
fffffe00079af8c8:      	str	w9, [x0, #8]
fffffe00079af8cc:      	cmp	w8, #2048
fffffe00079af8d0:      	b.ne	0xfffffe00079af974 <_nsdd_classify+0x128>
fffffe00079af8d4:      	sub	x11, x2, #14
fffffe00079af8d8:      	cmp	x11, #20
fffffe00079af8dc:      	b.hs	0xfffffe00079af8e8 <_nsdd_classify+0x9c>
fffffe00079af8e0:      	mov	w9, #2
fffffe00079af8e4:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af8e8:      	add	x8, x1, #14
fffffe00079af8ec:      	mov	w9, #3
fffffe00079af8f0:      	ldrb	w10, [x8]
fffffe00079af8f4:      	and	w12, w10, #0xf0
fffffe00079af8f8:      	cmp	w12, #64
fffffe00079af8fc:      	b.ne	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af900:      	lsl	w10, w10, #2
fffffe00079af904:      	and	x10, x10, #0x3c
fffffe00079af908:      	cmp	w10, #20
fffffe00079af90c:      	b.lo	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af910:      	cmp	x11, x10
fffffe00079af914:      	b.lo	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af918:      	ldrh	w9, [x1, #16]
fffffe00079af91c:      	rev16	w12, w9
fffffe00079af920:      	mov	w9, #4
fffffe00079af924:      	cmp	w12, w10
fffffe00079af928:      	b.lo	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af92c:      	cmp	x11, x12
fffffe00079af930:      	b.lo	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af934:      	ldrb	w9, [x1, #20]
fffffe00079af938:      	ldrb	w11, [x1, #21]
fffffe00079af93c:      	bfi	w11, w9, #8, #6
fffffe00079af940:      	cbz	w11, 0xfffffe00079af954 <_nsdd_classify+0x108>
fffffe00079af944:      	mov	w9, #5
fffffe00079af948:      	mov	w8, wzr
fffffe00079af94c:      	str	w9, [x0, #16]
fffffe00079af950:      	b	0xfffffe00079af978 <_nsdd_classify+0x12c>
fffffe00079af954:      	ldrb	w9, [x1, #23]
fffffe00079af958:      	cmp	w9, #17
fffffe00079af95c:      	b.ne	0xfffffe00079af974 <_nsdd_classify+0x128>
fffffe00079af960:      	sub	x12, x12, x10
fffffe00079af964:      	cmp	x12, #8
fffffe00079af968:      	b.hs	0xfffffe00079af984 <_nsdd_classify+0x138>
fffffe00079af96c:      	mov	w9, #6
fffffe00079af970:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af974:      	mov	w8, wzr
fffffe00079af978:      	mov	w0, w8
fffffe00079af97c:      	add	sp, sp, #16
fffffe00079af980:      	ret
fffffe00079af984:      	add	x10, x8, x10
fffffe00079af988:      	mov	w9, #6
fffffe00079af98c:      	ldrh	w8, [x10, #4]
fffffe00079af990:      	rev16	w11, w8
fffffe00079af994:      	cmp	w11, #8
fffffe00079af998:      	b.lo	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af99c:      	cmp	x12, x11
fffffe00079af9a0:      	b.lo	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af9a4:      	ldrh	w8, [x10]
fffffe00079af9a8:      	ldrh	w9, [x10, #2]
fffffe00079af9ac:      	rev16	w12, w8
fffffe00079af9b0:      	rev16	w9, w9
fffffe00079af9b4:      	cmp	w12, #67
fffffe00079af9b8:      	b.ne	0xfffffe00079af9c4 <_nsdd_classify+0x178>
fffffe00079af9bc:      	cmp	w9, #68
fffffe00079af9c0:      	b.eq	0xfffffe00079af9d8 <_nsdd_classify+0x18c>
fffffe00079af9c4:      	mov	w8, wzr
fffffe00079af9c8:      	cmp	w12, #68
fffffe00079af9cc:      	b.ne	0xfffffe00079af978 <_nsdd_classify+0x12c>
fffffe00079af9d0:      	cmp	w9, #67
fffffe00079af9d4:      	b.ne	0xfffffe00079af978 <_nsdd_classify+0x12c>
fffffe00079af9d8:      	sub	x8, x11, #8
fffffe00079af9dc:      	cmp	x8, #240
fffffe00079af9e0:      	b.hs	0xfffffe00079af9ec <_nsdd_classify+0x1a0>
fffffe00079af9e4:      	mov	w9, #7
fffffe00079af9e8:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079af9ec:      	add	x9, x10, #8
fffffe00079af9f0:      	ldrb	w10, [x9]
fffffe00079af9f4:      	sub	w10, w10, #1
fffffe00079af9f8:      	cmp	w10, #1
fffffe00079af9fc:      	b.hi	0xfffffe00079afa68 <_nsdd_classify+0x21c>
fffffe00079afa00:      	ldrb	w10, [x9, #1]
fffffe00079afa04:      	cmp	w10, #1
fffffe00079afa08:      	b.ne	0xfffffe00079afa68 <_nsdd_classify+0x21c>
fffffe00079afa0c:      	ldrb	w10, [x9, #2]
fffffe00079afa10:      	cmp	w10, #6
fffffe00079afa14:      	b.ne	0xfffffe00079afa68 <_nsdd_classify+0x21c>
fffffe00079afa18:      	ldrb	w10, [x9, #236]
fffffe00079afa1c:      	cmp	w10, #99
fffffe00079afa20:      	b.ne	0xfffffe00079afa70 <_nsdd_classify+0x224>
fffffe00079afa24:      	ldrb	w10, [x9, #237]
fffffe00079afa28:      	cmp	w10, #130
fffffe00079afa2c:      	b.ne	0xfffffe00079afa70 <_nsdd_classify+0x224>
fffffe00079afa30:      	ldrb	w10, [x9, #238]
fffffe00079afa34:      	cmp	w10, #83
fffffe00079afa38:      	b.ne	0xfffffe00079afa70 <_nsdd_classify+0x224>
fffffe00079afa3c:      	ldrb	w10, [x9, #239]
fffffe00079afa40:      	cmp	w10, #99
fffffe00079afa44:      	b.ne	0xfffffe00079afa70 <_nsdd_classify+0x224>
fffffe00079afa48:      	cmp	x8, #241
fffffe00079afa4c:      	b.lo	0xfffffe00079afb04 <_nsdd_classify+0x2b8>
fffffe00079afa50:      	mov	w10, wzr
fffffe00079afa54:      	mov	w13, wzr
fffffe00079afa58:      	sub	x11, x11, #10
fffffe00079afa5c:      	mov	w12, #257
fffffe00079afa60:      	mov	w14, #240
fffffe00079afa64:      	b	0xfffffe00079afa90 <_nsdd_classify+0x244>
fffffe00079afa68:      	mov	w9, #8
fffffe00079afa6c:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afa70:      	mov	w9, #9
fffffe00079afa74:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afa78:      	cmp	w16, #52
fffffe00079afa7c:      	b.eq	0xfffffe00079afb24 <_nsdd_classify+0x2d8>
fffffe00079afa80:      	add	x15, x14, x15
fffffe00079afa84:      	mov	x14, x15
fffffe00079afa88:      	cmp	x15, x8
fffffe00079afa8c:      	b.hs	0xfffffe00079afaf4 <_nsdd_classify+0x2a8>
fffffe00079afa90:      	subs	w12, w12, #1
fffffe00079afa94:      	b.eq	0xfffffe00079afb0c <_nsdd_classify+0x2c0>
fffffe00079afa98:      	ldrb	w16, [x9, x14]
fffffe00079afa9c:      	add	x15, x14, #1
fffffe00079afaa0:      	cbz	w16, 0xfffffe00079afa84 <_nsdd_classify+0x238>
fffffe00079afaa4:      	cmp	w16, #255
fffffe00079afaa8:      	b.eq	0xfffffe00079afaf4 <_nsdd_classify+0x2a8>
fffffe00079afaac:      	cmp	x8, x15
fffffe00079afab0:      	b.eq	0xfffffe00079afb14 <_nsdd_classify+0x2c8>
fffffe00079afab4:      	ldrb	w15, [x9, x15]
fffffe00079afab8:      	sub	x17, x11, x14
fffffe00079afabc:      	cmp	x17, x15
fffffe00079afac0:      	b.lo	0xfffffe00079afb14 <_nsdd_classify+0x2c8>
fffffe00079afac4:      	add	x14, x14, #2
fffffe00079afac8:      	cmp	w16, #53
fffffe00079afacc:      	b.ne	0xfffffe00079afa78 <_nsdd_classify+0x22c>
fffffe00079afad0:      	cbnz	w13, 0xfffffe00079afb2c <_nsdd_classify+0x2e0>
fffffe00079afad4:      	cmp	w15, #1
fffffe00079afad8:      	b.ne	0xfffffe00079afb1c <_nsdd_classify+0x2d0>
fffffe00079afadc:      	ldrb	w10, [x9, x14]
fffffe00079afae0:      	sub	w13, w10, #9
fffffe00079afae4:      	cmn	w13, #8
fffffe00079afae8:      	b.lo	0xfffffe00079afb1c <_nsdd_classify+0x2d0>
fffffe00079afaec:      	mov	w13, #1
fffffe00079afaf0:      	b	0xfffffe00079afa80 <_nsdd_classify+0x234>
fffffe00079afaf4:      	cbz	w13, 0xfffffe00079afb04 <_nsdd_classify+0x2b8>
fffffe00079afaf8:      	mov	w9, #1
fffffe00079afafc:      	str	w10, [x0, #12]
fffffe00079afb00:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afb04:      	mov	w9, #11
fffffe00079afb08:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afb0c:      	mov	w9, #14
fffffe00079afb10:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afb14:      	mov	w9, #10
fffffe00079afb18:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afb1c:      	mov	w9, #13
fffffe00079afb20:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afb24:      	mov	w9, #15
fffffe00079afb28:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
fffffe00079afb2c:      	mov	w9, #12
fffffe00079afb30:      	b	0xfffffe00079af948 <_nsdd_classify+0xfc>
