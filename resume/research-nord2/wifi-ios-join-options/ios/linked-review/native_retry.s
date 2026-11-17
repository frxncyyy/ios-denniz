
$NORD2_REPO/research/ci-39823157589/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a8960 <_nst_peer_begin>:
fffffe00079a8960:      	sub	sp, sp, #96
fffffe00079a8964:      	stp	x20, x19, [sp, #64]
fffffe00079a8968:      	stp	x29, x30, [sp, #80]
fffffe00079a896c:      	add	x29, sp, #80
fffffe00079a8970:      	adrp	x9, 0xfffffe0007c8c000 <_sy+0x118>
fffffe00079a8974:      	nop
fffffe00079a8978:      	ldr	x9, [x9, #3376]
fffffe00079a897c:      	stur	x9, [x29, #-24]
fffffe00079a8980:      	cbz	x0, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8984:      	mov	x8, x0
fffffe00079a8988:      	ldr	w9, [x0, #28]
fffffe00079a898c:      	cbz	w9, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8990:      	ldr	x9, [x8]
fffffe00079a8994:      	cbz	x9, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8998:      	ldr	x10, [x8, #8]
fffffe00079a899c:      	cbz	x10, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a89a0:      	ldr	x10, [x8, #16]
fffffe00079a89a4:      	cbz	x10, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a89a8:      	ldr	w10, [x8, #44]
fffffe00079a89ac:      	cbnz	w10, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a89b0:      	ldr	w10, [x9, #12]
fffffe00079a89b4:      	cbnz	w10, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a89b8:      	ldr	w9, [x9, #1876]
fffffe00079a89bc:      	cbz	w9, 0xfffffe00079a89ec <_nst_peer_begin+0x8c>
fffffe00079a89c0:      	mov	w0, #-1
fffffe00079a89c4:      	adrp	x8, 0xfffffe0007c8c000 <_mr+0x34>
fffffe00079a89c8:      	nop
fffffe00079a89cc:      	ldur	x9, [x29, #-24]
fffffe00079a89d0:      	ldr	x8, [x8, #3376]
fffffe00079a89d4:      	cmp	x8, x9
fffffe00079a89d8:      	b.ne	0xfffffe00079a8e4c <_nst_peer_begin+0x4ec>
fffffe00079a89dc:      	ldp	x29, x30, [sp, #80]
fffffe00079a89e0:      	ldp	x20, x19, [sp, #64]
fffffe00079a89e4:      	add	sp, sp, #96
fffffe00079a89e8:      	ret
fffffe00079a89ec:      	mov	w0, #-1
fffffe00079a89f0:      	sub	w9, w2, #4
fffffe00079a89f4:      	cmn	w9, #3
fffffe00079a89f8:      	b.lo	0xfffffe00079a89c4 <_nst_peer_begin+0x64>
fffffe00079a89fc:      	cbz	x1, 0xfffffe00079a89c4 <_nst_peer_begin+0x64>
fffffe00079a8a00:      	ldr	w9, [x8, #148]
fffffe00079a8a04:      	cbnz	w9, 0xfffffe00079a89c4 <_nst_peer_begin+0x64>
fffffe00079a8a08:      	ldr	w9, [x8, #56]
fffffe00079a8a0c:      	cbnz	w9, 0xfffffe00079a8a20 <_nst_peer_begin+0xc0>
fffffe00079a8a10:      	ldr	w9, [x8, #60]
fffffe00079a8a14:      	cbnz	w9, 0xfffffe00079a8a20 <_nst_peer_begin+0xc0>
fffffe00079a8a18:      	ldr	w9, [x8, #952]
fffffe00079a8a1c:      	cbz	w9, 0xfffffe00079a8a28 <_nst_peer_begin+0xc8>
fffffe00079a8a20:      	mov	w0, #1
fffffe00079a8a24:      	b	0xfffffe00079a89c4 <_nst_peer_begin+0x64>
fffffe00079a8a28:      	cmp	w2, #2
fffffe00079a8a2c:      	str	x8, [sp, #24]
fffffe00079a8a30:      	str	w2, [sp, #20]
fffffe00079a8a34:      	str	x1, [sp, #8]
fffffe00079a8a38:      	b.eq	0xfffffe00079a8bd8 <_nst_peer_begin+0x278>
fffffe00079a8a3c:      	cmp	w2, #1
fffffe00079a8a40:      	b.ne	0xfffffe00079a8e00 <_nst_peer_begin+0x4a0>
fffffe00079a8a44:      	cbz	x3, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a48:      	mov	x19, x3
fffffe00079a8a4c:      	ldr	x8, [x3, #16]
fffffe00079a8a50:      	cbz	x8, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a54:      	ldr	x8, [x19, #24]
fffffe00079a8a58:      	sub	x8, x8, #33
fffffe00079a8a5c:      	cmn	x8, #32
fffffe00079a8a60:      	b.lo	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a64:      	ldr	x0, [x19, #32]
fffffe00079a8a68:      	cbz	x0, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a6c:      	ldr	x1, [x19, #40]
fffffe00079a8a70:      	sub	x8, x1, #13
fffffe00079a8a74:      	cmn	x8, #12
fffffe00079a8a78:      	b.lo	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a7c:      	ldrb	w8, [x19, #59]
fffffe00079a8a80:      	cmp	w8, #1
fffffe00079a8a84:      	b.hi	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a88:      	ldrh	w8, [x19, #50]
fffffe00079a8a8c:      	cbz	w8, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a90:      	ldrh	w8, [x19, #56]
fffffe00079a8a94:      	cbz	w8, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8a98:      	ldr	x8, [sp, #24]
fffffe00079a8a9c:      	ldrb	w9, [x19]
fffffe00079a8aa0:      	ldrb	w8, [x8, #192]
fffffe00079a8aa4:      	cmp	w9, w8
fffffe00079a8aa8:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8aac:      	ldr	x8, [sp, #24]
fffffe00079a8ab0:      	ldrb	w9, [x19, #1]
fffffe00079a8ab4:      	ldrb	w8, [x8, #193]
fffffe00079a8ab8:      	cmp	w9, w8
fffffe00079a8abc:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ac0:      	ldr	x8, [sp, #24]
fffffe00079a8ac4:      	ldrb	w9, [x19, #2]
fffffe00079a8ac8:      	ldrb	w8, [x8, #194]
fffffe00079a8acc:      	cmp	w9, w8
fffffe00079a8ad0:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ad4:      	ldr	x8, [sp, #24]
fffffe00079a8ad8:      	ldrb	w9, [x19, #3]
fffffe00079a8adc:      	ldrb	w8, [x8, #195]
fffffe00079a8ae0:      	cmp	w9, w8
fffffe00079a8ae4:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ae8:      	ldr	x8, [sp, #24]
fffffe00079a8aec:      	ldrb	w9, [x19, #4]
fffffe00079a8af0:      	ldrb	w8, [x8, #196]
fffffe00079a8af4:      	cmp	w9, w8
fffffe00079a8af8:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8afc:      	ldr	x8, [sp, #24]
fffffe00079a8b00:      	ldrb	w9, [x19, #5]
fffffe00079a8b04:      	ldrb	w8, [x8, #197]
fffffe00079a8b08:      	cmp	w9, w8
fffffe00079a8b0c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b10:      	ldr	x8, [sp, #24]
fffffe00079a8b14:      	ldrb	w9, [x19, #6]
fffffe00079a8b18:      	ldrb	w8, [x8, #198]
fffffe00079a8b1c:      	cmp	w9, w8
fffffe00079a8b20:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b24:      	ldr	x8, [sp, #24]
fffffe00079a8b28:      	ldrb	w9, [x19, #7]
fffffe00079a8b2c:      	ldrb	w8, [x8, #199]
fffffe00079a8b30:      	cmp	w9, w8
fffffe00079a8b34:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b38:      	ldr	x8, [sp, #24]
fffffe00079a8b3c:      	ldrb	w9, [x19, #8]
fffffe00079a8b40:      	ldrb	w8, [x8, #200]
fffffe00079a8b44:      	cmp	w9, w8
fffffe00079a8b48:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b4c:      	ldr	x8, [sp, #24]
fffffe00079a8b50:      	ldrb	w9, [x19, #9]
fffffe00079a8b54:      	ldrb	w8, [x8, #201]
fffffe00079a8b58:      	cmp	w9, w8
fffffe00079a8b5c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b60:      	ldr	x8, [sp, #24]
fffffe00079a8b64:      	ldrb	w9, [x19, #10]
fffffe00079a8b68:      	ldrb	w8, [x8, #202]
fffffe00079a8b6c:      	cmp	w9, w8
fffffe00079a8b70:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b74:      	ldr	x8, [sp, #24]
fffffe00079a8b78:      	ldrb	w9, [x19, #11]
fffffe00079a8b7c:      	ldrb	w8, [x8, #203]
fffffe00079a8b80:      	cmp	w9, w8
fffffe00079a8b84:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b88:      	ldr	x8, [sp, #24]
fffffe00079a8b8c:      	add	x2, x8, #1102
fffffe00079a8b90:      	add	x3, x8, #1104
fffffe00079a8b94:      	bl	0xfffffe00079a87d8 <_rate_sets>
fffffe00079a8b98:      	cbnz	w0, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8b9c:      	ldp	q1, q0, [x19, #32]
fffffe00079a8ba0:      	mov	x8, x19
fffffe00079a8ba4:      	ldp	q3, q2, [x19]
fffffe00079a8ba8:      	ldr	x10, [sp, #24]
fffffe00079a8bac:      	str	q0, [x10, #1040]
fffffe00079a8bb0:      	add	x8, x10, #1056
fffffe00079a8bb4:      	stp	q2, q1, [x10, #1008]
fffffe00079a8bb8:      	str	q3, [x10, #992]
fffffe00079a8bbc:      	ldr	x9, [x19, #24]
fffffe00079a8bc0:      	cbz	x9, 0xfffffe00079a90a8 <_nst_peer_begin+0x748>
fffffe00079a8bc4:      	ldr	x10, [x19, #16]
fffffe00079a8bc8:      	cmp	x9, #8
fffffe00079a8bcc:      	b.hs	0xfffffe00079a8e50 <_nst_peer_begin+0x4f0>
fffffe00079a8bd0:      	mov	x11, x8
fffffe00079a8bd4:      	b	0xfffffe00079a9098 <_nst_peer_begin+0x738>
fffffe00079a8bd8:      	mov	w0, #-1
fffffe00079a8bdc:      	str	x4, [sp]
fffffe00079a8be0:      	cbz	x4, 0xfffffe00079a89c4 <_nst_peer_begin+0x64>
fffffe00079a8be4:      	sub	x8, x5, #513
fffffe00079a8be8:      	cmn	x8, #483
fffffe00079a8bec:      	ldr	x8, [sp]
fffffe00079a8bf0:      	b.lo	0xfffffe00079a89c4 <_nst_peer_begin+0x64>
fffffe00079a8bf4:      	ldrb	w9, [x8, #1]
fffffe00079a8bf8:      	mov	w10, #63232
fffffe00079a8bfc:      	ldrb	w8, [x8]
fffffe00079a8c00:      	and	w9, w10, w9, lsl #8
fffffe00079a8c04:      	orr	w8, w9, w8
fffffe00079a8c08:      	cmp	w8, #16
fffffe00079a8c0c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8c10:      	ldr	x8, [sp]
fffffe00079a8c14:      	ldrb	w9, [x8, #4]
fffffe00079a8c18:      	ldr	x8, [sp, #24]
fffffe00079a8c1c:      	ldrb	w8, [x8, #192]
fffffe00079a8c20:      	cmp	w9, w8
fffffe00079a8c24:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8c28:      	ldr	x8, [sp]
fffffe00079a8c2c:      	ldrb	w9, [x8, #5]
fffffe00079a8c30:      	ldr	x8, [sp, #24]
fffffe00079a8c34:      	ldrb	w8, [x8, #193]
fffffe00079a8c38:      	cmp	w9, w8
fffffe00079a8c3c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8c40:      	ldr	x8, [sp]
fffffe00079a8c44:      	ldrb	w9, [x8, #6]
fffffe00079a8c48:      	ldr	x8, [sp, #24]
fffffe00079a8c4c:      	ldrb	w8, [x8, #194]
fffffe00079a8c50:      	cmp	w9, w8
fffffe00079a8c54:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8c58:      	ldr	x8, [sp]
fffffe00079a8c5c:      	ldrb	w9, [x8, #7]
fffffe00079a8c60:      	ldr	x8, [sp, #24]
fffffe00079a8c64:      	ldrb	w8, [x8, #195]
fffffe00079a8c68:      	cmp	w9, w8
fffffe00079a8c6c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8c70:      	ldr	x8, [sp]
fffffe00079a8c74:      	ldrb	w9, [x8, #8]
fffffe00079a8c78:      	ldr	x8, [sp, #24]
fffffe00079a8c7c:      	ldrb	w8, [x8, #196]
fffffe00079a8c80:      	cmp	w9, w8
fffffe00079a8c84:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8c88:      	ldr	x8, [sp]
fffffe00079a8c8c:      	ldrb	w9, [x8, #9]
fffffe00079a8c90:      	ldr	x8, [sp, #24]
fffffe00079a8c94:      	ldrb	w8, [x8, #197]
fffffe00079a8c98:      	cmp	w9, w8
fffffe00079a8c9c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ca0:      	ldr	x8, [sp]
fffffe00079a8ca4:      	ldr	x9, [sp, #24]
fffffe00079a8ca8:      	ldrb	w8, [x8, #10]
fffffe00079a8cac:      	ldrb	w9, [x9, #198]
fffffe00079a8cb0:      	cmp	w8, w9
fffffe00079a8cb4:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8cb8:      	ldr	x9, [sp]
fffffe00079a8cbc:      	ldr	x10, [sp, #24]
fffffe00079a8cc0:      	ldrb	w9, [x9, #11]
fffffe00079a8cc4:      	ldrb	w10, [x10, #199]
fffffe00079a8cc8:      	cmp	w9, w10
fffffe00079a8ccc:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8cd0:      	ldr	x10, [sp]
fffffe00079a8cd4:      	ldr	x11, [sp, #24]
fffffe00079a8cd8:      	ldrb	w10, [x10, #12]
fffffe00079a8cdc:      	ldrb	w11, [x11, #200]
fffffe00079a8ce0:      	cmp	w10, w11
fffffe00079a8ce4:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ce8:      	ldr	x11, [sp]
fffffe00079a8cec:      	ldr	x12, [sp, #24]
fffffe00079a8cf0:      	ldrb	w11, [x11, #13]
fffffe00079a8cf4:      	ldrb	w12, [x12, #201]
fffffe00079a8cf8:      	cmp	w11, w12
fffffe00079a8cfc:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d00:      	ldr	x12, [sp]
fffffe00079a8d04:      	ldr	x13, [sp, #24]
fffffe00079a8d08:      	ldrb	w12, [x12, #14]
fffffe00079a8d0c:      	ldrb	w13, [x13, #202]
fffffe00079a8d10:      	cmp	w12, w13
fffffe00079a8d14:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d18:      	ldr	x13, [sp]
fffffe00079a8d1c:      	ldr	x14, [sp, #24]
fffffe00079a8d20:      	ldrb	w13, [x13, #15]
fffffe00079a8d24:      	ldrb	w14, [x14, #203]
fffffe00079a8d28:      	cmp	w13, w14
fffffe00079a8d2c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d30:      	ldr	x14, [sp]
fffffe00079a8d34:      	ldrb	w14, [x14, #16]
fffffe00079a8d38:      	cmp	w14, w8
fffffe00079a8d3c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d40:      	ldr	x8, [sp]
fffffe00079a8d44:      	ldrb	w8, [x8, #17]
fffffe00079a8d48:      	cmp	w8, w9
fffffe00079a8d4c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d50:      	ldr	x8, [sp]
fffffe00079a8d54:      	ldrb	w8, [x8, #18]
fffffe00079a8d58:      	cmp	w8, w10
fffffe00079a8d5c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d60:      	ldr	x8, [sp]
fffffe00079a8d64:      	ldrb	w8, [x8, #19]
fffffe00079a8d68:      	cmp	w8, w11
fffffe00079a8d6c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d70:      	ldr	x8, [sp]
fffffe00079a8d74:      	ldrb	w8, [x8, #20]
fffffe00079a8d78:      	cmp	w8, w12
fffffe00079a8d7c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d80:      	ldr	x8, [sp]
fffffe00079a8d84:      	ldrb	w8, [x8, #21]
fffffe00079a8d88:      	cmp	w8, w13
fffffe00079a8d8c:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d90:      	ldr	x8, [sp]
fffffe00079a8d94:      	ldrh	w8, [x8, #26]
fffffe00079a8d98:      	cbnz	w8, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8d9c:      	ldr	x8, [sp]
fffffe00079a8da0:      	mov	w9, #17
fffffe00079a8da4:      	ldrb	w8, [x8, #24]
fffffe00079a8da8:      	bics	wzr, w9, w8
fffffe00079a8dac:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8db0:      	ldr	x8, [sp]
fffffe00079a8db4:      	ldrb	w19, [x8, #28]
fffffe00079a8db8:      	ldrb	w8, [x8, #29]
fffffe00079a8dbc:      	bfi	w19, w8, #8, #6
fffffe00079a8dc0:      	sub	w8, w19, #2008
fffffe00079a8dc4:      	cmn	w8, #2007
fffffe00079a8dc8:      	b.lo	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8dcc:      	cmp	x5, #31
fffffe00079a8dd0:      	str	wzr, [sp, #48]
fffffe00079a8dd4:      	str	xzr, [sp, #40]
fffffe00079a8dd8:      	b.lo	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ddc:      	ldr	x8, [sp]
fffffe00079a8de0:      	mov	w9, wzr
fffffe00079a8de4:      	mov	w10, wzr
fffffe00079a8de8:      	mov	x1, xzr
fffffe00079a8dec:      	mov	w15, #30
fffffe00079a8df0:      	mov	w11, #12
fffffe00079a8df4:      	add	x8, x8, #2
fffffe00079a8df8:      	add	x12, sp, #40
fffffe00079a8dfc:      	b	0xfffffe00079a8e88 <_nst_peer_begin+0x528>
fffffe00079a8e00:      	ldr	w9, [x8, #920]
fffffe00079a8e04:      	cbz	w9, 0xfffffe00079a8e28 <_nst_peer_begin+0x4c8>
fffffe00079a8e08:      	ldr	w9, [x8, #24]
fffffe00079a8e0c:      	cbnz	w9, 0xfffffe00079a8e28 <_nst_peer_begin+0x4c8>
fffffe00079a8e10:      	ldr	x9, [x8, #168]
fffffe00079a8e14:      	mov	w10, #50048
fffffe00079a8e18:      	movk	w10, #457, lsl #16
fffffe00079a8e1c:      	adds	x9, x9, x10
fffffe00079a8e20:      	csinv	x9, x9, xzr, lo
fffffe00079a8e24:      	str	x9, [x8, #176]
fffffe00079a8e28:      	str	wzr, [x8, #920]
fffffe00079a8e2c:      	ldr	x9, [sp, #24]
fffffe00079a8e30:      	mov	w0, wzr
fffffe00079a8e34:      	ldr	x8, [sp, #8]
fffffe00079a8e38:      	str	wzr, [x9, #956]
fffffe00079a8e3c:      	str	x8, [x9, #968]
fffffe00079a8e40:      	ldr	w8, [sp, #20]
fffffe00079a8e44:      	str	w8, [x9, #952]
fffffe00079a8e48:      	b	0xfffffe00079a89c4 <_nst_peer_begin+0x64>
fffffe00079a8e4c:      	bl	0xfffffe00079fc494 <___stack_chk_fail>
fffffe00079a8e50:      	ldr	x11, [sp, #24]
fffffe00079a8e54:      	sub	x11, x11, x10
fffffe00079a8e58:      	add	x12, x11, #1056
fffffe00079a8e5c:      	mov	x11, x8
fffffe00079a8e60:      	cmp	x12, #32
fffffe00079a8e64:      	b.lo	0xfffffe00079a9098 <_nst_peer_begin+0x738>
fffffe00079a8e68:      	cmp	x9, #32
fffffe00079a8e6c:      	b.hs	0xfffffe00079a9008 <_nst_peer_begin+0x6a8>
fffffe00079a8e70:      	mov	x12, xzr
fffffe00079a8e74:      	b	0xfffffe00079a9044 <_nst_peer_begin+0x6e4>
fffffe00079a8e78:      	add	x1, x1, x13
fffffe00079a8e7c:      	add	x15, x14, x13
fffffe00079a8e80:      	cmp	x15, x5
fffffe00079a8e84:      	b.hs	0xfffffe00079a91b0 <_nst_peer_begin+0x850>
fffffe00079a8e88:      	sub	x13, x5, x15
fffffe00079a8e8c:      	subs	x14, x13, #2
fffffe00079a8e90:      	b.lo	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8e94:      	ldr	x13, [sp]
fffffe00079a8e98:      	add	x13, x15, x13
fffffe00079a8e9c:      	ldrb	w13, [x13, #1]
fffffe00079a8ea0:      	cmp	x14, x13
fffffe00079a8ea4:      	b.lo	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ea8:      	ldr	x14, [sp]
fffffe00079a8eac:      	ldrb	w16, [x14, x15]
fffffe00079a8eb0:      	add	x14, x15, #2
fffffe00079a8eb4:      	cmp	w16, #50
fffffe00079a8eb8:      	b.eq	0xfffffe00079a8ec4 <_nst_peer_begin+0x564>
fffffe00079a8ebc:      	cmp	w16, #1
fffffe00079a8ec0:      	b.ne	0xfffffe00079a8e7c <_nst_peer_begin+0x51c>
fffffe00079a8ec4:      	cbz	w13, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ec8:      	cmp	w16, #50
fffffe00079a8ecc:      	b.eq	0xfffffe00079a8eec <_nst_peer_begin+0x58c>
fffffe00079a8ed0:      	cmp	w16, #1
fffffe00079a8ed4:      	b.ne	0xfffffe00079a8ef4 <_nst_peer_begin+0x594>
fffffe00079a8ed8:      	cbnz	w10, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8edc:      	cmp	w13, #8
fffffe00079a8ee0:      	b.hi	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ee4:      	mov	w10, #1
fffffe00079a8ee8:      	b	0xfffffe00079a8ef4 <_nst_peer_begin+0x594>
fffffe00079a8eec:      	cbnz	w9, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8ef0:      	mov	w9, #1
fffffe00079a8ef4:      	sub	x16, x11, x1
fffffe00079a8ef8:      	cmp	x16, x13
fffffe00079a8efc:      	b.lo	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a8f00:      	ldr	x17, [sp]
fffffe00079a8f04:      	add	x16, x12, x1
fffffe00079a8f08:      	cmp	w13, #8
fffffe00079a8f0c:      	add	x17, x17, x14
fffffe00079a8f10:      	b.hs	0xfffffe00079a8f1c <_nst_peer_begin+0x5bc>
fffffe00079a8f14:      	mov	x2, x13
fffffe00079a8f18:      	b	0xfffffe00079a8ff4 <_nst_peer_begin+0x694>
fffffe00079a8f1c:      	cmp	w13, #32
fffffe00079a8f20:      	b.hs	0xfffffe00079a8f2c <_nst_peer_begin+0x5cc>
fffffe00079a8f24:      	mov	x0, xzr
fffffe00079a8f28:      	b	0xfffffe00079a8fa8 <_nst_peer_begin+0x648>
fffffe00079a8f2c:      	ldp	q0, q1, [x17]
fffffe00079a8f30:      	and	x0, x13, #0xe0
fffffe00079a8f34:      	cmp	x0, #32
fffffe00079a8f38:      	stp	q0, q1, [x16]
fffffe00079a8f3c:      	b.eq	0xfffffe00079a8f98 <_nst_peer_begin+0x638>
fffffe00079a8f40:      	ldp	q0, q1, [x17, #32]
fffffe00079a8f44:      	cmp	x0, #64
fffffe00079a8f48:      	stp	q0, q1, [x16, #32]
fffffe00079a8f4c:      	b.eq	0xfffffe00079a8f98 <_nst_peer_begin+0x638>
fffffe00079a8f50:      	ldp	q0, q1, [x17, #64]
fffffe00079a8f54:      	cmp	x0, #96
fffffe00079a8f58:      	stp	q0, q1, [x16, #64]
fffffe00079a8f5c:      	b.eq	0xfffffe00079a8f98 <_nst_peer_begin+0x638>
fffffe00079a8f60:      	ldp	q0, q1, [x17, #96]
fffffe00079a8f64:      	cmp	x0, #128
fffffe00079a8f68:      	stp	q0, q1, [x16, #96]
fffffe00079a8f6c:      	b.eq	0xfffffe00079a8f98 <_nst_peer_begin+0x638>
fffffe00079a8f70:      	ldp	q0, q1, [x17, #128]
fffffe00079a8f74:      	cmp	x0, #160
fffffe00079a8f78:      	stp	q0, q1, [x16, #128]
fffffe00079a8f7c:      	b.eq	0xfffffe00079a8f98 <_nst_peer_begin+0x638>
fffffe00079a8f80:      	ldp	q0, q1, [x17, #160]
fffffe00079a8f84:      	cmp	x0, #192
fffffe00079a8f88:      	stp	q0, q1, [x16, #160]
fffffe00079a8f8c:      	b.eq	0xfffffe00079a8f98 <_nst_peer_begin+0x638>
fffffe00079a8f90:      	ldp	q0, q1, [x17, #192]
fffffe00079a8f94:      	stp	q0, q1, [x16, #192]
fffffe00079a8f98:      	cmp	x0, x13
fffffe00079a8f9c:      	b.eq	0xfffffe00079a8e78 <_nst_peer_begin+0x518>
fffffe00079a8fa0:      	tst	x13, #0x18
fffffe00079a8fa4:      	b.eq	0xfffffe00079a8fe8 <_nst_peer_begin+0x688>
fffffe00079a8fa8:      	and	x3, x13, #0xf8
fffffe00079a8fac:      	add	x4, x1, x0
fffffe00079a8fb0:      	add	x15, x0, x15
fffffe00079a8fb4:      	and	x2, x13, #0x7
fffffe00079a8fb8:      	add	x17, x17, x3
fffffe00079a8fbc:      	add	x16, x16, x3
fffffe00079a8fc0:      	add	x4, x12, x4
fffffe00079a8fc4:      	add	x15, x8, x15
fffffe00079a8fc8:      	sub	x0, x0, x3
fffffe00079a8fcc:      	ldr	d0, [x15], #8
fffffe00079a8fd0:      	adds	x0, x0, #8
fffffe00079a8fd4:      	str	d0, [x4], #8
fffffe00079a8fd8:      	b.ne	0xfffffe00079a8fcc <_nst_peer_begin+0x66c>
fffffe00079a8fdc:      	cmp	x3, x13
fffffe00079a8fe0:      	b.ne	0xfffffe00079a8ff4 <_nst_peer_begin+0x694>
fffffe00079a8fe4:      	b	0xfffffe00079a8e78 <_nst_peer_begin+0x518>
fffffe00079a8fe8:      	and	x2, x13, #0x1f
fffffe00079a8fec:      	add	x16, x16, x0
fffffe00079a8ff0:      	add	x17, x17, x0
fffffe00079a8ff4:      	ldrb	w15, [x17], #1
fffffe00079a8ff8:      	subs	x2, x2, #1
fffffe00079a8ffc:      	strb	w15, [x16], #1
fffffe00079a9000:      	b.ne	0xfffffe00079a8ff4 <_nst_peer_begin+0x694>
fffffe00079a9004:      	b	0xfffffe00079a8e78 <_nst_peer_begin+0x518>
fffffe00079a9008:      	ldr	x11, [sp, #24]
fffffe00079a900c:      	and	x12, x9, #0xffffffffffffffe0
fffffe00079a9010:      	add	x13, x10, #16
fffffe00079a9014:      	mov	x14, x12
fffffe00079a9018:      	add	x11, x11, #1072
fffffe00079a901c:      	ldp	q0, q1, [x13, #-16]
fffffe00079a9020:      	add	x13, x13, #32
fffffe00079a9024:      	subs	x14, x14, #32
fffffe00079a9028:      	stp	q0, q1, [x11, #-16]
fffffe00079a902c:      	add	x11, x11, #32
fffffe00079a9030:      	b.ne	0xfffffe00079a901c <_nst_peer_begin+0x6bc>
fffffe00079a9034:      	cmp	x9, x12
fffffe00079a9038:      	b.eq	0xfffffe00079a90a8 <_nst_peer_begin+0x748>
fffffe00079a903c:      	tst	x9, #0x18
fffffe00079a9040:      	b.eq	0xfffffe00079a908c <_nst_peer_begin+0x72c>
fffffe00079a9044:      	ldr	x11, [sp, #24]
fffffe00079a9048:      	and	x13, x9, #0xfffffffffffffff8
fffffe00079a904c:      	add	x15, x10, x13
fffffe00079a9050:      	and	x14, x9, #0x7
fffffe00079a9054:      	add	x10, x10, x12
fffffe00079a9058:      	add	x16, x12, x11
fffffe00079a905c:      	add	x11, x8, x13
fffffe00079a9060:      	add	x16, x16, #1056
fffffe00079a9064:      	sub	x12, x12, x13
fffffe00079a9068:      	ldr	d0, [x10], #8
fffffe00079a906c:      	adds	x12, x12, #8
fffffe00079a9070:      	str	d0, [x16], #8
fffffe00079a9074:      	b.ne	0xfffffe00079a9068 <_nst_peer_begin+0x708>
fffffe00079a9078:      	cmp	x9, x13
fffffe00079a907c:      	mov	x10, x15
fffffe00079a9080:      	mov	x9, x14
fffffe00079a9084:      	b.ne	0xfffffe00079a9098 <_nst_peer_begin+0x738>
fffffe00079a9088:      	b	0xfffffe00079a90a8 <_nst_peer_begin+0x748>
fffffe00079a908c:      	and	x9, x9, #0x1f
fffffe00079a9090:      	add	x11, x8, x12
fffffe00079a9094:      	add	x10, x10, x12
fffffe00079a9098:      	ldrb	w12, [x10], #1
fffffe00079a909c:      	subs	x9, x9, #1
fffffe00079a90a0:      	strb	w12, [x11], #1
fffffe00079a90a4:      	b.ne	0xfffffe00079a9098 <_nst_peer_begin+0x738>
fffffe00079a90a8:      	ldr	x9, [sp, #24]
fffffe00079a90ac:      	ldr	x10, [x19, #40]
fffffe00079a90b0:      	add	x9, x9, #1088
fffffe00079a90b4:      	cbz	x10, 0xfffffe00079a9194 <_nst_peer_begin+0x834>
fffffe00079a90b8:      	ldr	x11, [x19, #32]
fffffe00079a90bc:      	cmp	x10, #8
fffffe00079a90c0:      	b.hs	0xfffffe00079a90cc <_nst_peer_begin+0x76c>
fffffe00079a90c4:      	mov	x12, x9
fffffe00079a90c8:      	b	0xfffffe00079a9184 <_nst_peer_begin+0x824>
fffffe00079a90cc:      	ldr	x12, [sp, #24]
fffffe00079a90d0:      	sub	x12, x12, x11
fffffe00079a90d4:      	add	x13, x12, #1088
fffffe00079a90d8:      	mov	x12, x9
fffffe00079a90dc:      	cmp	x13, #32
fffffe00079a90e0:      	b.lo	0xfffffe00079a9184 <_nst_peer_begin+0x824>
fffffe00079a90e4:      	cmp	x10, #32
fffffe00079a90e8:      	b.hs	0xfffffe00079a90f4 <_nst_peer_begin+0x794>
fffffe00079a90ec:      	mov	x13, xzr
fffffe00079a90f0:      	b	0xfffffe00079a9130 <_nst_peer_begin+0x7d0>
fffffe00079a90f4:      	ldr	x12, [sp, #24]
fffffe00079a90f8:      	and	x13, x10, #0xffffffffffffffe0
fffffe00079a90fc:      	add	x14, x11, #16
fffffe00079a9100:      	mov	x15, x13
fffffe00079a9104:      	add	x12, x12, #1104
fffffe00079a9108:      	ldp	q0, q1, [x14, #-16]
fffffe00079a910c:      	add	x14, x14, #32
fffffe00079a9110:      	subs	x15, x15, #32
fffffe00079a9114:      	stp	q0, q1, [x12, #-16]
fffffe00079a9118:      	add	x12, x12, #32
fffffe00079a911c:      	b.ne	0xfffffe00079a9108 <_nst_peer_begin+0x7a8>
fffffe00079a9120:      	cmp	x10, x13
fffffe00079a9124:      	b.eq	0xfffffe00079a9194 <_nst_peer_begin+0x834>
fffffe00079a9128:      	tst	x10, #0x18
fffffe00079a912c:      	b.eq	0xfffffe00079a9178 <_nst_peer_begin+0x818>
fffffe00079a9130:      	ldr	x12, [sp, #24]
fffffe00079a9134:      	and	x14, x10, #0xfffffffffffffff8
fffffe00079a9138:      	add	x16, x11, x14
fffffe00079a913c:      	and	x15, x10, #0x7
fffffe00079a9140:      	add	x11, x11, x13
fffffe00079a9144:      	add	x17, x13, x12
fffffe00079a9148:      	add	x12, x9, x14
fffffe00079a914c:      	add	x17, x17, #1088
fffffe00079a9150:      	sub	x13, x13, x14
fffffe00079a9154:      	ldr	d0, [x11], #8
fffffe00079a9158:      	adds	x13, x13, #8
fffffe00079a915c:      	str	d0, [x17], #8
fffffe00079a9160:      	b.ne	0xfffffe00079a9154 <_nst_peer_begin+0x7f4>
fffffe00079a9164:      	cmp	x10, x14
fffffe00079a9168:      	mov	x11, x16
fffffe00079a916c:      	mov	x10, x15
fffffe00079a9170:      	b.ne	0xfffffe00079a9184 <_nst_peer_begin+0x824>
fffffe00079a9174:      	b	0xfffffe00079a9194 <_nst_peer_begin+0x834>
fffffe00079a9178:      	and	x10, x10, #0x1f
fffffe00079a917c:      	add	x12, x9, x13
fffffe00079a9180:      	add	x11, x11, x13
fffffe00079a9184:      	ldrb	w13, [x11], #1
fffffe00079a9188:      	subs	x10, x10, #1
fffffe00079a918c:      	strb	w13, [x12], #1
fffffe00079a9190:      	b.ne	0xfffffe00079a9184 <_nst_peer_begin+0x824>
fffffe00079a9194:      	ldr	x19, [sp, #24]
fffffe00079a9198:      	ldrh	w0, [x19, #1104]
fffffe00079a919c:      	str	x8, [x19, #1008]
fffffe00079a91a0:      	str	x9, [x19, #1024]
fffffe00079a91a4:      	bl	0xfffffe00079a88bc <_lowest>
fffffe00079a91a8:      	strh	w0, [x19, #218]
fffffe00079a91ac:      	b	0xfffffe00079a8e2c <_nst_peer_begin+0x4cc>
fffffe00079a91b0:      	str	wzr, [sp, #36]
fffffe00079a91b4:      	cbz	w10, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a91b8:      	add	x0, sp, #40
fffffe00079a91bc:      	add	x2, sp, #38
fffffe00079a91c0:      	add	x3, sp, #36
fffffe00079a91c4:      	bl	0xfffffe00079a87d8 <_rate_sets>
fffffe00079a91c8:      	cbnz	w0, 0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a91cc:      	ldr	x8, [sp, #24]
fffffe00079a91d0:      	ldrh	w0, [sp, #36]
fffffe00079a91d4:      	ldrh	w8, [x8, #1102]
fffffe00079a91d8:      	bics	wzr, w0, w8
fffffe00079a91dc:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a91e0:      	ldrh	w9, [sp, #38]
fffffe00079a91e4:      	ands	w8, w9, w8
fffffe00079a91e8:      	b.eq	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a91ec:      	bics	wzr, w0, w8
fffffe00079a91f0:      	b.ne	0xfffffe00079a89c0 <_nst_peer_begin+0x60>
fffffe00079a91f4:      	ldr	x20, [sp, #24]
fffffe00079a91f8:      	strh	w8, [x20, #1102]
fffffe00079a91fc:      	strh	w0, [x20, #1104]
fffffe00079a9200:      	strh	w19, [x20, #1100]
fffffe00079a9204:      	bl	0xfffffe00079a88bc <_lowest>
fffffe00079a9208:      	ldr	x8, [sp]
fffffe00079a920c:      	strh	w0, [x20, #218]
fffffe00079a9210:      	ldrh	w10, [x20, #1040]
fffffe00079a9214:      	ldrb	w9, [x8, #24]
fffffe00079a9218:      	ldrb	w8, [x8, #25]
fffffe00079a921c:      	bfi	w9, w8, #8, #24
fffffe00079a9220:      	mov	w8, #17
fffffe00079a9224:      	orr	w8, w9, w8
fffffe00079a9228:      	and	w8, w8, w10
fffffe00079a922c:      	strh	w8, [x20, #1040]
fffffe00079a9230:      	b	0xfffffe00079a8e2c <_nst_peer_begin+0x4cc>
