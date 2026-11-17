
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b490c <_nwpd_take>:
fffffe00079b490c:      	mov	w8, wzr
fffffe00079b4910:      	cbz	x0, 0xfffffe00079b4a7c <_nwpd_take+0x170>
fffffe00079b4914:      	cbz	x1, 0xfffffe00079b4a7c <_nwpd_take+0x170>
fffffe00079b4918:      	cmp	x6, #640
fffffe00079b491c:      	b.hi	0xfffffe00079b4a7c <_nwpd_take+0x170>
fffffe00079b4920:      	cmp	x5, #0
fffffe00079b4924:      	ldr	w9, [sp]
fffffe00079b4928:      	ccmp	x6, #0, #4, eq
fffffe00079b492c:      	mov	w8, wzr
fffffe00079b4930:      	cset	w10, ne
fffffe00079b4934:      	cmp	w9, #14
fffffe00079b4938:      	b.hi	0xfffffe00079b4a7c <_nwpd_take+0x170>
fffffe00079b493c:      	cmp	w4, #5
fffffe00079b4940:      	b.hi	0xfffffe00079b4a7c <_nwpd_take+0x170>
fffffe00079b4944:      	tbnz	w10, #0, 0xfffffe00079b4a7c <_nwpd_take+0x170>
fffffe00079b4948:      	sub	w10, w7, #5
fffffe00079b494c:      	cmn	w10, #6
fffffe00079b4950:      	b.lo	0xfffffe00079b4a7c <_nwpd_take+0x170>
fffffe00079b4954:      	ldr	w11, [x0, #12]
fffffe00079b4958:      	cmp	w11, #39
fffffe00079b495c:      	b.hi	0xfffffe00079b4a14 <_nwpd_take+0x108>
fffffe00079b4960:      	cmp	w7, #0
fffffe00079b4964:      	mov	w8, #4
fffffe00079b4968:      	cset	w13, ge
fffffe00079b496c:      	cmp	w4, #5
fffffe00079b4970:      	cset	w14, eq
fffffe00079b4974:      	mov	w15, #8
fffffe00079b4978:      	tst	w14, w13
fffffe00079b497c:      	mov	w10, #2
fffffe00079b4980:      	csel	x8, x8, xzr, ne
fffffe00079b4984:      	cmp	w7, #0
fffffe00079b4988:      	csel	x8, x15, x8, lt
fffffe00079b498c:      	csel	w10, w10, w14, lt
fffffe00079b4990:      	tst	w14, w13
fffffe00079b4994:      	mov	w13, #24
fffffe00079b4998:      	csel	w13, w13, w15, ne
fffffe00079b499c:      	ldr	w12, [x0, x8]
fffffe00079b49a0:      	cmp	w12, w13
fffffe00079b49a4:      	b.hs	0xfffffe00079b4a14 <_nwpd_take+0x108>
fffffe00079b49a8:      	add	w13, w11, #1
fffffe00079b49ac:      	add	w11, w7, #1
fffffe00079b49b0:      	cmp	x6, #4
fffffe00079b49b4:      	str	w13, [x0, #12]
fffffe00079b49b8:      	b.lo	0xfffffe00079b4a1c <_nwpd_take+0x110>
fffffe00079b49bc:      	ldrh	w14, [x5, #2]
fffffe00079b49c0:      	mov	w15, wzr
fffffe00079b49c4:      	ldrb	w16, [x5]
fffffe00079b49c8:      	cmp	x6, #7
fffffe00079b49cc:      	ldrb	w17, [x5, #1]
fffffe00079b49d0:      	rev16	w14, w14
fffffe00079b49d4:      	b.lo	0xfffffe00079b4a34 <_nwpd_take+0x128>
fffffe00079b49d8:      	cmp	w17, #3
fffffe00079b49dc:      	b.ne	0xfffffe00079b4a34 <_nwpd_take+0x128>
fffffe00079b49e0:      	ldrb	w17, [x5, #6]
fffffe00079b49e4:      	ldrb	w5, [x5, #5]
fffffe00079b49e8:      	mov	w15, w17
fffffe00079b49ec:      	bfi	w15, w5, #8, #8
fffffe00079b49f0:      	cmp	w15, #138
fffffe00079b49f4:      	b.eq	0xfffffe00079b4a3c <_nwpd_take+0x130>
fffffe00079b49f8:      	mov	w5, #5066
fffffe00079b49fc:      	cmp	w15, w5
fffffe00079b4a00:      	b.ne	0xfffffe00079b4a44 <_nwpd_take+0x138>
fffffe00079b4a04:      	mov	w15, #5066
fffffe00079b4a08:      	mov	w17, #3
fffffe00079b4a0c:      	mov	w5, #3
fffffe00079b4a10:      	b	0xfffffe00079b4a54 <_nwpd_take+0x148>
fffffe00079b4a14:      	mov	w0, wzr
fffffe00079b4a18:      	ret
fffffe00079b4a1c:      	mov	w14, wzr
fffffe00079b4a20:      	mov	w17, wzr
fffffe00079b4a24:      	mov	w16, wzr
fffffe00079b4a28:      	mov	w15, wzr
fffffe00079b4a2c:      	mov	w5, wzr
fffffe00079b4a30:      	b	0xfffffe00079b4a54 <_nwpd_take+0x148>
fffffe00079b4a34:      	mov	w5, w15
fffffe00079b4a38:      	b	0xfffffe00079b4a54 <_nwpd_take+0x148>
fffffe00079b4a3c:      	mov	w5, #1
fffffe00079b4a40:      	b	0xfffffe00079b4a50 <_nwpd_take+0x144>
fffffe00079b4a44:      	tst	w17, #0x8
fffffe00079b4a48:      	mov	w17, #5
fffffe00079b4a4c:      	csel	w5, w17, wzr, eq
fffffe00079b4a50:      	mov	w17, #3
fffffe00079b4a54:      	add	w12, w12, #1
fffffe00079b4a58:      	str	w12, [x0, x8]
fffffe00079b4a5c:      	mov	w8, #1
fffffe00079b4a60:      	stp	x2, x3, [x1]
fffffe00079b4a64:      	stp	w13, w10, [x1, #16]
fffffe00079b4a68:      	stp	w4, w6, [x1, #24]
fffffe00079b4a6c:      	stp	w16, w17, [x1, #32]
fffffe00079b4a70:      	stp	w14, w15, [x1, #40]
fffffe00079b4a74:      	stp	w5, w11, [x1, #48]
fffffe00079b4a78:      	stp	w9, wzr, [x1, #56]
fffffe00079b4a7c:      	mov	w0, w8
fffffe00079b4a80:      	ret
