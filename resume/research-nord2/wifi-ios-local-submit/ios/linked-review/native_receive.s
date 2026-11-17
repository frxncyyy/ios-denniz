
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a8b0c <_receive>:
fffffe00079a8b0c:      	sub	sp, sp, #160
fffffe00079a8b10:      	stp	x28, x27, [sp, #64]
fffffe00079a8b14:      	stp	x26, x25, [sp, #80]
fffffe00079a8b18:      	stp	x24, x23, [sp, #96]
fffffe00079a8b1c:      	stp	x22, x21, [sp, #112]
fffffe00079a8b20:      	stp	x20, x19, [sp, #128]
fffffe00079a8b24:      	stp	x29, x30, [sp, #144]
fffffe00079a8b28:      	add	x29, sp, #144
fffffe00079a8b2c:      	mov	w8, #80
fffffe00079a8b30:      	mov	x20, x5
fffffe00079a8b34:      	mov	x21, x4
fffffe00079a8b38:      	mov	w22, w3
fffffe00079a8b3c:      	umaddl	x25, w3, w8, x0
fffffe00079a8b40:      	mov	x19, x0
fffffe00079a8b44:      	mov	x23, x2
fffffe00079a8b48:      	mov	x24, x1
fffffe00079a8b4c:      	str	wzr, [sp, #60]
fffffe00079a8b50:      	str	w3, [x0, #100]
fffffe00079a8b54:      	ldr	w26, [x25, #248]
fffffe00079a8b58:      	str	wzr, [x0, #40]
fffffe00079a8b5c:      	bl	0xfffffe00079a806c <_before>
fffffe00079a8b60:      	cbnz	w0, 0xfffffe00079a8b80 <_receive+0x74>
fffffe00079a8b64:      	ldr	x8, [x24]
fffffe00079a8b68:      	add	w1, w26, #12
fffffe00079a8b6c:      	add	x2, sp, #60
fffffe00079a8b70:      	mov	x0, x23
fffffe00079a8b74:      	ldr	x8, [x8, #8]
fffffe00079a8b78:      	blr	x8
fffffe00079a8b7c:      	cbz	w0, 0xfffffe00079a8bb0 <_receive+0xa4>
fffffe00079a8b80:      	mov	x0, x19
fffffe00079a8b84:      	mov	w1, #5
fffffe00079a8b88:      	bl	0xfffffe00079aa178 <_fail>
fffffe00079a8b8c:      	mov	w0, #-1
fffffe00079a8b90:      	ldp	x29, x30, [sp, #144]
fffffe00079a8b94:      	ldp	x20, x19, [sp, #128]
fffffe00079a8b98:      	ldp	x22, x21, [sp, #112]
fffffe00079a8b9c:      	ldp	x24, x23, [sp, #96]
fffffe00079a8ba0:      	ldp	x26, x25, [sp, #80]
fffffe00079a8ba4:      	ldp	x28, x27, [sp, #64]
fffffe00079a8ba8:      	add	sp, sp, #160
fffffe00079a8bac:      	ret
fffffe00079a8bb0:      	add	x26, x25, #232
fffffe00079a8bb4:      	ldr	w8, [sp, #60]
fffffe00079a8bb8:      	ldr	w9, [x26, #20]
fffffe00079a8bbc:      	cmp	w8, w9
fffffe00079a8bc0:      	b.hs	0xfffffe00079a8be4 <_receive+0xd8>
fffffe00079a8bc4:      	mov	w10, w22
fffffe00079a8bc8:      	mov	w9, #80
fffffe00079a8bcc:      	madd	x25, x10, x9, x19
fffffe00079a8bd0:      	ldr	w28, [x25, #264]
fffffe00079a8bd4:      	cmp	w8, w28
fffffe00079a8bd8:      	b.ne	0xfffffe00079a8bf0 <_receive+0xe4>
fffffe00079a8bdc:      	mov	w0, wzr
fffffe00079a8be0:      	b	0xfffffe00079a8b90 <_receive+0x84>
fffffe00079a8be4:      	mov	x0, x19
fffffe00079a8be8:      	mov	w1, #20
fffffe00079a8bec:      	b	0xfffffe00079a8b88 <_receive+0x7c>
fffffe00079a8bf0:      	ldp	x27, x9, [x26]
fffffe00079a8bf4:      	mov	w8, #5
fffffe00079a8bf8:      	mov	x0, x19
fffffe00079a8bfc:      	mov	x1, x24
fffffe00079a8c00:      	mov	x2, x23
fffffe00079a8c04:      	str	w8, [x19, #40]
fffffe00079a8c08:      	stp	x9, x10, [sp, #40]
fffffe00079a8c0c:      	bl	0xfffffe00079a806c <_before>
fffffe00079a8c10:      	cbz	w0, 0xfffffe00079a8c20 <_receive+0x114>
fffffe00079a8c14:      	mov	x0, x19
fffffe00079a8c18:      	mov	w1, #9
fffffe00079a8c1c:      	b	0xfffffe00079a8b88 <_receive+0x7c>
fffffe00079a8c20:      	lsl	w8, w28, #4
fffffe00079a8c24:      	mov	x0, x19
fffffe00079a8c28:      	add	x27, x27, x8
fffffe00079a8c2c:      	mov	w2, #16
fffffe00079a8c30:      	mov	x1, x27
fffffe00079a8c34:      	mov	w3, wzr
fffffe00079a8c38:      	bl	0xfffffe00079a789c <_nst_memory_range>
fffffe00079a8c3c:      	cbz	w0, 0xfffffe00079a8c14 <_receive+0x108>
fffffe00079a8c40:      	ldr	x8, [x24]
fffffe00079a8c44:      	mov	x0, x23
fffffe00079a8c48:      	mov	x1, x27
fffffe00079a8c4c:      	mov	w2, #16
fffffe00079a8c50:      	mov	w3, #2
fffffe00079a8c54:      	ldr	x8, [x8, #40]
fffffe00079a8c58:      	blr	x8
fffffe00079a8c5c:      	cbnz	w0, 0xfffffe00079a8c14 <_receive+0x108>
fffffe00079a8c60:      	mov	x0, x19
fffffe00079a8c64:      	mov	x1, x24
fffffe00079a8c68:      	mov	x2, x23
fffffe00079a8c6c:      	bl	0xfffffe00079a806c <_before>
fffffe00079a8c70:      	cbz	w0, 0xfffffe00079a8c80 <_receive+0x174>
fffffe00079a8c74:      	mov	x0, x19
fffffe00079a8c78:      	mov	w1, #7
fffffe00079a8c7c:      	b	0xfffffe00079a8b88 <_receive+0x7c>
fffffe00079a8c80:      	mov	x0, x19
fffffe00079a8c84:      	mov	x1, x27
fffffe00079a8c88:      	mov	w2, #16
fffffe00079a8c8c:      	mov	w3, wzr
fffffe00079a8c90:      	bl	0xfffffe00079a789c <_nst_memory_range>
fffffe00079a8c94:      	cbz	w0, 0xfffffe00079a8c74 <_receive+0x168>
fffffe00079a8c98:      	ldr	x8, [x24]
fffffe00079a8c9c:      	mov	w9, #5257
fffffe00079a8ca0:      	add	x2, x19, x9
fffffe00079a8ca4:      	mov	x0, x23
fffffe00079a8ca8:      	mov	x1, x27
fffffe00079a8cac:      	mov	w3, #16
fffffe00079a8cb0:      	ldr	x8, [x8, #24]
fffffe00079a8cb4:      	str	x2, [sp, #32]
fffffe00079a8cb8:      	blr	x8
fffffe00079a8cbc:      	cbnz	w0, 0xfffffe00079a8c74 <_receive+0x168>
fffffe00079a8cc0:      	mov	w8, #5261
fffffe00079a8cc4:      	add	x9, x19, x8
fffffe00079a8cc8:      	ldrb	w10, [x9, #2]
fffffe00079a8ccc:      	ldrh	w8, [x9]
fffffe00079a8cd0:      	ldrb	w9, [x9, #3]
fffffe00079a8cd4:      	bfi	w8, w10, #16, #8
fffffe00079a8cd8:      	bfi	w8, w9, #24, #8
fffffe00079a8cdc:      	tbz	w8, #31, 0xfffffe00079a8bdc <_receive+0xd0>
fffffe00079a8ce0:      	ldr	x11, [sp, #48]
fffffe00079a8ce4:      	mov	w10, #80
fffffe00079a8ce8:      	ands	w9, w9, #0x40
fffffe00079a8cec:      	ubfx	w4, w8, #16, #14
fffffe00079a8cf0:      	cset	w8, eq
fffffe00079a8cf4:      	stp	x26, x27, [sp, #16]
fffffe00079a8cf8:      	madd	x10, x11, x10, x19
fffffe00079a8cfc:      	str	x25, [sp, #8]
fffffe00079a8d00:      	movi.2d	v0, #0000000000000000
fffffe00079a8d04:      	str	w4, [x19, #9116]
fffffe00079a8d08:      	ldr	w11, [x10, #268]
fffffe00079a8d0c:      	str	w8, [x10, #268]
fffffe00079a8d10:      	ldr	x10, [sp, #40]
fffffe00079a8d14:      	lsl	w8, w28, #12
fffffe00079a8d18:      	str	q0, [x19, #9120]
fffffe00079a8d1c:      	str	xzr, [x19, #9168]
fffffe00079a8d20:      	add	x8, x10, x8
fffffe00079a8d24:      	str	q0, [x19, #9152]
fffffe00079a8d28:      	str	q0, [x19, #9136]
fffffe00079a8d2c:      	cbnz	w11, 0xfffffe00079a8dc0 <_receive+0x2b4>
fffffe00079a8d30:      	cbz	w9, 0xfffffe00079a8dc0 <_receive+0x2b4>
fffffe00079a8d34:      	cbz	w4, 0xfffffe00079a8dc0 <_receive+0x2b4>
fffffe00079a8d38:      	cmp	w4, #3840
fffffe00079a8d3c:      	b.hi	0xfffffe00079a8dc0 <_receive+0x2b4>
fffffe00079a8d40:      	mov	w9, #6
fffffe00079a8d44:      	mov	x0, x19
fffffe00079a8d48:      	mov	x1, x24
fffffe00079a8d4c:      	mov	x2, x23
fffffe00079a8d50:      	mov	x3, x8
fffffe00079a8d54:      	mov	w5, #2
fffffe00079a8d58:      	str	w9, [x19, #40]
fffffe00079a8d5c:      	str	x8, [sp, #40]
fffffe00079a8d60:      	bl	0xfffffe00079aa2d0 <_sy>
fffffe00079a8d64:      	cbnz	w0, 0xfffffe00079a8b8c <_receive+0x80>
fffffe00079a8d68:      	mov	w8, #5273
fffffe00079a8d6c:      	ldr	w5, [x19, #9116]
fffffe00079a8d70:      	add	x27, x19, x8
fffffe00079a8d74:      	mov	x0, x19
fffffe00079a8d78:      	mov	x1, x24
fffffe00079a8d7c:      	mov	x2, x23
fffffe00079a8d80:      	ldr	x3, [sp, #40]
fffffe00079a8d84:      	mov	x4, x27
fffffe00079a8d88:      	bl	0xfffffe00079aa368 <_mr>
fffffe00079a8d8c:      	cbnz	w0, 0xfffffe00079a8b8c <_receive+0x80>
fffffe00079a8d90:      	ldp	x0, x1, [x19, #152]
fffffe00079a8d94:      	mov	w2, w22
fffffe00079a8d98:      	mov	x3, x27
fffffe00079a8d9c:      	ldr	w4, [x19, #9116]
fffffe00079a8da0:      	bl	0xfffffe00079ab4f8 <_nstd_rx>
fffffe00079a8da4:      	mov	x0, x19
fffffe00079a8da8:      	bl	0xfffffe00079aa530 <_decode>
fffffe00079a8dac:      	ldr	x8, [sp, #40]
fffffe00079a8db0:      	cbz	w0, 0xfffffe00079a8dc0 <_receive+0x2b4>
fffffe00079a8db4:      	mov	x0, x19
fffffe00079a8db8:      	mov	w1, #21
fffffe00079a8dbc:      	b	0xfffffe00079a8b88 <_receive+0x7c>
fffffe00079a8dc0:      	ldr	x0, [sp, #32]
fffffe00079a8dc4:      	mov	x1, x8
fffffe00079a8dc8:      	mov	w2, #3840
fffffe00079a8dcc:      	bl	0xfffffe0007990dc8 <_nwt_rx_desc>
fffffe00079a8dd0:      	cbz	w0, 0xfffffe00079a8de0 <_receive+0x2d4>
fffffe00079a8dd4:      	mov	x0, x19
fffffe00079a8dd8:      	mov	w1, #22
fffffe00079a8ddc:      	b	0xfffffe00079a8b88 <_receive+0x7c>
fffffe00079a8de0:      	ldp	x3, x4, [sp, #24]
fffffe00079a8de4:      	mov	w8, #7
fffffe00079a8de8:      	mov	x0, x19
fffffe00079a8dec:      	mov	x1, x24
fffffe00079a8df0:      	mov	x2, x23
fffffe00079a8df4:      	mov	w5, #16
fffffe00079a8df8:      	str	w8, [x19, #40]
fffffe00079a8dfc:      	bl	0xfffffe00079aa3fc <_mw>
fffffe00079a8e00:      	cbnz	w0, 0xfffffe00079a8b8c <_receive+0x80>
fffffe00079a8e04:      	mov	x0, x19
fffffe00079a8e08:      	mov	x1, x24
fffffe00079a8e0c:      	mov	x2, x23
fffffe00079a8e10:      	ldr	x3, [sp, #24]
fffffe00079a8e14:      	mov	w4, #16
fffffe00079a8e18:      	mov	w5, #1
fffffe00079a8e1c:      	bl	0xfffffe00079aa2d0 <_sy>
fffffe00079a8e20:      	cbnz	w0, 0xfffffe00079a8b8c <_receive+0x80>
fffffe00079a8e24:      	ldr	x8, [sp, #8]
fffffe00079a8e28:      	mov	w9, #8
fffffe00079a8e2c:      	mov	x0, x19
fffffe00079a8e30:      	mov	x1, x24
fffffe00079a8e34:      	mov	x2, x23
fffffe00079a8e38:      	add	x25, x8, #264
fffffe00079a8e3c:      	ldr	x8, [sp, #16]
fffffe00079a8e40:      	str	w9, [x19, #40]
fffffe00079a8e44:      	ldr	w8, [x8, #16]
fffffe00079a8e48:      	ldr	w4, [x25]
fffffe00079a8e4c:      	add	w3, w8, #8
fffffe00079a8e50:      	bl	0xfffffe00079aa4a0 <_rw>
fffffe00079a8e54:      	cbnz	w0, 0xfffffe00079a8b8c <_receive+0x80>
fffffe00079a8e58:      	ldr	x8, [sp, #16]
fffffe00079a8e5c:      	cmp	w22, #1
fffffe00079a8e60:      	ldr	w9, [x25]
fffffe00079a8e64:      	ldr	w8, [x8, #20]
fffffe00079a8e68:      	add	w9, w9, #1
fffffe00079a8e6c:      	udiv	w10, w9, w8
fffffe00079a8e70:      	msub	w8, w10, w8, w9
fffffe00079a8e74:      	str	w8, [x25]
fffffe00079a8e78:      	b.ne	0xfffffe00079a8e84 <_receive+0x378>
fffffe00079a8e7c:      	ldr	x9, [x19]
fffffe00079a8e80:      	str	w8, [x9, #2868]
fffffe00079a8e84:      	ldr	x8, [sp, #48]
fffffe00079a8e88:      	mov	w9, #80
fffffe00079a8e8c:      	ldr	w10, [x19, #9120]
fffffe00079a8e90:      	str	wzr, [x19, #40]
fffffe00079a8e94:      	madd	x8, x8, x9, x19
fffffe00079a8e98:      	ldr	w9, [x8, #276]
fffffe00079a8e9c:      	add	w9, w9, #1
fffffe00079a8ea0:      	str	w9, [x8, #276]
fffffe00079a8ea4:      	cbz	w10, 0xfffffe00079a8ec4 <_receive+0x3b8>
fffffe00079a8ea8:      	mov	w8, #9120
fffffe00079a8eac:      	mov	x0, x19
fffffe00079a8eb0:      	add	x3, x19, x8
fffffe00079a8eb4:      	mov	x1, x21
fffffe00079a8eb8:      	mov	x2, x20
fffffe00079a8ebc:      	bl	0xfffffe00079a8f10 <_emit>
fffffe00079a8ec0:      	cbnz	w0, 0xfffffe00079a8b8c <_receive+0x80>
fffffe00079a8ec4:      	mov	w0, #1
fffffe00079a8ec8:      	b	0xfffffe00079a8b90 <_receive+0x84>
