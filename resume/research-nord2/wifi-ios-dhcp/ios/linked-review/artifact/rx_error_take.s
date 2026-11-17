
$NORD2_REPO/research/ci-39865241324/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b1998 <_nsdd_rx_error_take>:
fffffe00079b1998:      	mov	w8, wzr
fffffe00079b199c:      	cbz	x0, 0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19a0:      	cbz	x1, 0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19a4:      	cbz	x2, 0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19a8:      	ldr	w8, [x0, #444]
fffffe00079b19ac:      	cbz	w8, 0xfffffe00079b19b8 <_nsdd_rx_error_take+0x20>
fffffe00079b19b0:      	mov	w0, wzr
fffffe00079b19b4:      	ret
fffffe00079b19b8:      	sub	x9, x4, #3841
fffffe00079b19bc:      	cmn	x9, #3817
fffffe00079b19c0:      	b.lo	0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19c4:      	cbz	x3, 0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19c8:      	ldr	w9, [x0, #412]
fffffe00079b19cc:      	cmp	w9, #7
fffffe00079b19d0:      	b.hi	0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19d4:      	ldrh	w10, [x3]
fffffe00079b19d8:      	mov	w8, wzr
fffffe00079b19dc:      	cmp	w10, #24
fffffe00079b19e0:      	b.lo	0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19e4:      	cmp	x10, x4
fffffe00079b19e8:      	b.hi	0xfffffe00079b1adc <_nsdd_rx_error_take+0x144>
fffffe00079b19ec:      	ldrb	w8, [x3, #3]
fffffe00079b19f0:      	lsr	w10, w8, #3
fffffe00079b19f4:      	cmp	w10, #2
fffffe00079b19f8:      	b.eq	0xfffffe00079b1a20 <_nsdd_rx_error_take+0x88>
fffffe00079b19fc:      	cmp	w10, #7
fffffe00079b1a00:      	b.ne	0xfffffe00079b19b0 <_nsdd_rx_error_take+0x18>
fffffe00079b1a04:      	ldrb	w11, [x3, #2]
fffffe00079b1a08:      	bfi	w11, w8, #8, #8
fffffe00079b1a0c:      	mov	w8, #14351
fffffe00079b1a10:      	and	w8, w11, w8
fffffe00079b1a14:      	mov	w11, #14337
fffffe00079b1a18:      	cmp	w8, w11
fffffe00079b1a1c:      	b.ne	0xfffffe00079b19b0 <_nsdd_rx_error_take+0x18>
fffffe00079b1a20:      	ldrb	w12, [x3, #6]
fffffe00079b1a24:      	ldrb	w11, [x3, #10]
fffffe00079b1a28:      	ldrb	w8, [x3, #7]
fffffe00079b1a2c:      	ldrb	w13, [x3, #11]
fffffe00079b1a30:      	lsl	w16, w12, #16
fffffe00079b1a34:      	ldrb	w14, [x3, #16]
fffffe00079b1a38:      	lsl	w17, w11, #16
fffffe00079b1a3c:      	ldrb	w15, [x3, #17]
fffffe00079b1a40:      	bfi	w16, w8, #24, #8
fffffe00079b1a44:      	bfi	w17, w13, #24, #8
fffffe00079b1a48:      	mov	w8, #192937984
fffffe00079b1a4c:      	and	w13, w17, w8
fffffe00079b1a50:      	mov	w8, #6147
fffffe00079b1a54:      	bfi	w14, w15, #8, #24
fffffe00079b1a58:      	ands	w15, w16, #0xf800000
fffffe00079b1a5c:      	and	w14, w14, w8
fffffe00079b1a60:      	b.ne	0xfffffe00079b1a6c <_nsdd_rx_error_take+0xd4>
fffffe00079b1a64:      	cbnz	w13, 0xfffffe00079b1a6c <_nsdd_rx_error_take+0xd4>
fffffe00079b1a68:      	cbz	w14, 0xfffffe00079b19b0 <_nsdd_rx_error_take+0x18>
fffffe00079b1a6c:      	ldrb	w16, [x3, #14]
fffffe00079b1a70:      	add	w9, w9, #1
fffffe00079b1a74:      	ldrb	w17, [x3, #9]
fffffe00079b1a78:      	mov	w8, #1
fffffe00079b1a7c:      	ldrb	w4, [x3, #5]
fffffe00079b1a80:      	and	w11, w11, #0xf
fffffe00079b1a84:      	ldrb	w3, [x3, #4]
fffffe00079b1a88:      	stp	w9, w15, [x1, #8]
fffffe00079b1a8c:      	and	w9, w12, #0x1f
fffffe00079b1a90:      	stp	w13, w14, [x1, #16]
fffffe00079b1a94:      	ubfx	w12, w12, #5, #2
fffffe00079b1a98:      	ubfx	w13, w17, #5, #1
fffffe00079b1a9c:      	bfi	w3, w4, #8, #2
fffffe00079b1aa0:      	str	x2, [x1]
fffffe00079b1aa4:      	ands	w2, w16, #0x3
fffffe00079b1aa8:      	lsr	w14, w4, #3
fffffe00079b1aac:      	cmp	w2, #1
fffffe00079b1ab0:      	stp	w12, w13, [x1, #32]
fffffe00079b1ab4:      	stp	w3, w9, [x1, #24]
fffffe00079b1ab8:      	cinc	w9, w8, ne
fffffe00079b1abc:      	ands	w12, w16, #0x3
fffffe00079b1ac0:      	stp	w14, w10, [x1, #40]
fffffe00079b1ac4:      	csel	w9, wzr, w9, eq
fffffe00079b1ac8:      	stp	w11, wzr, [x1, #56]
fffffe00079b1acc:      	stp	w12, w9, [x1, #48]
fffffe00079b1ad0:      	ldr	w9, [x0, #412]
fffffe00079b1ad4:      	add	w9, w9, #1
fffffe00079b1ad8:      	str	w9, [x0, #412]
fffffe00079b1adc:      	mov	w0, w8
fffffe00079b1ae0:      	ret
