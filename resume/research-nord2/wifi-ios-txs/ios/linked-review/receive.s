
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7a88 <_receive>:
fffffe00079a7a88:      	sub	sp, sp, #160
fffffe00079a7a8c:      	stp	x28, x27, [sp, #64]
fffffe00079a7a90:      	stp	x26, x25, [sp, #80]
fffffe00079a7a94:      	stp	x24, x23, [sp, #96]
fffffe00079a7a98:      	stp	x22, x21, [sp, #112]
fffffe00079a7a9c:      	stp	x20, x19, [sp, #128]
fffffe00079a7aa0:      	stp	x29, x30, [sp, #144]
fffffe00079a7aa4:      	add	x29, sp, #144
fffffe00079a7aa8:      	mov	w8, #80
fffffe00079a7aac:      	mov	x20, x5
fffffe00079a7ab0:      	mov	x21, x4
fffffe00079a7ab4:      	mov	w22, w3
fffffe00079a7ab8:      	umaddl	x25, w3, w8, x0
fffffe00079a7abc:      	mov	x19, x0
fffffe00079a7ac0:      	mov	x23, x2
fffffe00079a7ac4:      	mov	x24, x1
fffffe00079a7ac8:      	str	wzr, [sp, #60]
fffffe00079a7acc:      	str	w3, [x0, #100]
fffffe00079a7ad0:      	ldr	w26, [x25, #248]
fffffe00079a7ad4:      	str	wzr, [x0, #40]
fffffe00079a7ad8:      	bl	0xfffffe00079a7054 <_before>
fffffe00079a7adc:      	cbnz	w0, 0xfffffe00079a7afc <_receive+0x74>
fffffe00079a7ae0:      	ldr	x8, [x24]
fffffe00079a7ae4:      	add	w1, w26, #12
fffffe00079a7ae8:      	add	x2, sp, #60
fffffe00079a7aec:      	mov	x0, x23
fffffe00079a7af0:      	ldr	x8, [x8, #8]
fffffe00079a7af4:      	blr	x8
fffffe00079a7af8:      	cbz	w0, 0xfffffe00079a7b2c <_receive+0xa4>
fffffe00079a7afc:      	mov	x0, x19
fffffe00079a7b00:      	mov	w1, #5
fffffe00079a7b04:      	bl	0xfffffe00079a9104 <_fail>
fffffe00079a7b08:      	mov	w0, #-1
fffffe00079a7b0c:      	ldp	x29, x30, [sp, #144]
fffffe00079a7b10:      	ldp	x20, x19, [sp, #128]
fffffe00079a7b14:      	ldp	x22, x21, [sp, #112]
fffffe00079a7b18:      	ldp	x24, x23, [sp, #96]
fffffe00079a7b1c:      	ldp	x26, x25, [sp, #80]
fffffe00079a7b20:      	ldp	x28, x27, [sp, #64]
fffffe00079a7b24:      	add	sp, sp, #160
fffffe00079a7b28:      	ret
fffffe00079a7b2c:      	add	x26, x25, #232
fffffe00079a7b30:      	ldr	w8, [sp, #60]
fffffe00079a7b34:      	ldr	w9, [x26, #20]
fffffe00079a7b38:      	cmp	w8, w9
fffffe00079a7b3c:      	b.hs	0xfffffe00079a7b60 <_receive+0xd8>
fffffe00079a7b40:      	mov	w10, w22
fffffe00079a7b44:      	mov	w9, #80
fffffe00079a7b48:      	madd	x25, x10, x9, x19
fffffe00079a7b4c:      	ldr	w28, [x25, #264]
fffffe00079a7b50:      	cmp	w8, w28
fffffe00079a7b54:      	b.ne	0xfffffe00079a7b6c <_receive+0xe4>
fffffe00079a7b58:      	mov	w0, wzr
fffffe00079a7b5c:      	b	0xfffffe00079a7b0c <_receive+0x84>
fffffe00079a7b60:      	mov	x0, x19
fffffe00079a7b64:      	mov	w1, #20
fffffe00079a7b68:      	b	0xfffffe00079a7b04 <_receive+0x7c>
fffffe00079a7b6c:      	ldp	x27, x9, [x26]
fffffe00079a7b70:      	mov	w8, #5
fffffe00079a7b74:      	mov	x0, x19
fffffe00079a7b78:      	mov	x1, x24
fffffe00079a7b7c:      	mov	x2, x23
fffffe00079a7b80:      	str	w8, [x19, #40]
fffffe00079a7b84:      	stp	x9, x10, [sp, #40]
fffffe00079a7b88:      	bl	0xfffffe00079a7054 <_before>
fffffe00079a7b8c:      	cbz	w0, 0xfffffe00079a7b9c <_receive+0x114>
fffffe00079a7b90:      	mov	x0, x19
fffffe00079a7b94:      	mov	w1, #9
fffffe00079a7b98:      	b	0xfffffe00079a7b04 <_receive+0x7c>
fffffe00079a7b9c:      	lsl	w8, w28, #4
fffffe00079a7ba0:      	mov	x0, x19
fffffe00079a7ba4:      	add	x27, x27, x8
fffffe00079a7ba8:      	mov	w2, #16
fffffe00079a7bac:      	mov	x1, x27
fffffe00079a7bb0:      	mov	w3, wzr
fffffe00079a7bb4:      	bl	0xfffffe00079a6874 <_nst_memory_range>
fffffe00079a7bb8:      	cbz	w0, 0xfffffe00079a7b90 <_receive+0x108>
fffffe00079a7bbc:      	ldr	x8, [x24]
fffffe00079a7bc0:      	mov	x0, x23
fffffe00079a7bc4:      	mov	x1, x27
fffffe00079a7bc8:      	mov	w2, #16
fffffe00079a7bcc:      	mov	w3, #2
fffffe00079a7bd0:      	ldr	x8, [x8, #40]
fffffe00079a7bd4:      	blr	x8
fffffe00079a7bd8:      	cbnz	w0, 0xfffffe00079a7b90 <_receive+0x108>
fffffe00079a7bdc:      	mov	x0, x19
fffffe00079a7be0:      	mov	x1, x24
fffffe00079a7be4:      	mov	x2, x23
fffffe00079a7be8:      	bl	0xfffffe00079a7054 <_before>
fffffe00079a7bec:      	cbz	w0, 0xfffffe00079a7bfc <_receive+0x174>
fffffe00079a7bf0:      	mov	x0, x19
fffffe00079a7bf4:      	mov	w1, #7
fffffe00079a7bf8:      	b	0xfffffe00079a7b04 <_receive+0x7c>
fffffe00079a7bfc:      	mov	x0, x19
fffffe00079a7c00:      	mov	x1, x27
fffffe00079a7c04:      	mov	w2, #16
fffffe00079a7c08:      	mov	w3, wzr
fffffe00079a7c0c:      	bl	0xfffffe00079a6874 <_nst_memory_range>
fffffe00079a7c10:      	cbz	w0, 0xfffffe00079a7bf0 <_receive+0x168>
fffffe00079a7c14:      	ldr	x8, [x24]
fffffe00079a7c18:      	mov	w9, #5257
fffffe00079a7c1c:      	add	x2, x19, x9
fffffe00079a7c20:      	mov	x0, x23
fffffe00079a7c24:      	mov	x1, x27
fffffe00079a7c28:      	mov	w3, #16
fffffe00079a7c2c:      	ldr	x8, [x8, #24]
fffffe00079a7c30:      	str	x2, [sp, #32]
fffffe00079a7c34:      	blr	x8
fffffe00079a7c38:      	cbnz	w0, 0xfffffe00079a7bf0 <_receive+0x168>
fffffe00079a7c3c:      	mov	w8, #5261
fffffe00079a7c40:      	add	x9, x19, x8
fffffe00079a7c44:      	ldrb	w10, [x9, #2]
fffffe00079a7c48:      	ldrh	w8, [x9]
fffffe00079a7c4c:      	ldrb	w9, [x9, #3]
fffffe00079a7c50:      	bfi	w8, w10, #16, #8
fffffe00079a7c54:      	bfi	w8, w9, #24, #8
fffffe00079a7c58:      	tbz	w8, #31, 0xfffffe00079a7b58 <_receive+0xd0>
fffffe00079a7c5c:      	ldr	x11, [sp, #48]
fffffe00079a7c60:      	mov	w10, #80
fffffe00079a7c64:      	ands	w9, w9, #0x40
fffffe00079a7c68:      	ubfx	w4, w8, #16, #14
fffffe00079a7c6c:      	cset	w8, eq
fffffe00079a7c70:      	stp	x26, x27, [sp, #16]
fffffe00079a7c74:      	madd	x10, x11, x10, x19
fffffe00079a7c78:      	str	x25, [sp, #8]
fffffe00079a7c7c:      	movi.2d	v0, #0000000000000000
fffffe00079a7c80:      	str	w4, [x19, #9116]
fffffe00079a7c84:      	ldr	w11, [x10, #268]
fffffe00079a7c88:      	str	w8, [x10, #268]
fffffe00079a7c8c:      	ldr	x10, [sp, #40]
fffffe00079a7c90:      	lsl	w8, w28, #12
fffffe00079a7c94:      	str	q0, [x19, #9120]
fffffe00079a7c98:      	str	xzr, [x19, #9168]
fffffe00079a7c9c:      	add	x8, x10, x8
fffffe00079a7ca0:      	str	q0, [x19, #9152]
fffffe00079a7ca4:      	str	q0, [x19, #9136]
fffffe00079a7ca8:      	cbnz	w11, 0xfffffe00079a7d3c <_receive+0x2b4>
fffffe00079a7cac:      	cbz	w9, 0xfffffe00079a7d3c <_receive+0x2b4>
fffffe00079a7cb0:      	cbz	w4, 0xfffffe00079a7d3c <_receive+0x2b4>
fffffe00079a7cb4:      	cmp	w4, #3840
fffffe00079a7cb8:      	b.hi	0xfffffe00079a7d3c <_receive+0x2b4>
fffffe00079a7cbc:      	mov	w9, #6
fffffe00079a7cc0:      	mov	x0, x19
fffffe00079a7cc4:      	mov	x1, x24
fffffe00079a7cc8:      	mov	x2, x23
fffffe00079a7ccc:      	mov	x3, x8
fffffe00079a7cd0:      	mov	w5, #2
fffffe00079a7cd4:      	str	w9, [x19, #40]
fffffe00079a7cd8:      	str	x8, [sp, #40]
fffffe00079a7cdc:      	bl	0xfffffe00079a925c <_sy>
fffffe00079a7ce0:      	cbnz	w0, 0xfffffe00079a7b08 <_receive+0x80>
fffffe00079a7ce4:      	mov	w8, #5273
fffffe00079a7ce8:      	ldr	w5, [x19, #9116]
fffffe00079a7cec:      	add	x27, x19, x8
fffffe00079a7cf0:      	mov	x0, x19
fffffe00079a7cf4:      	mov	x1, x24
fffffe00079a7cf8:      	mov	x2, x23
fffffe00079a7cfc:      	ldr	x3, [sp, #40]
fffffe00079a7d00:      	mov	x4, x27
fffffe00079a7d04:      	bl	0xfffffe00079a92f4 <_mr>
fffffe00079a7d08:      	cbnz	w0, 0xfffffe00079a7b08 <_receive+0x80>
fffffe00079a7d0c:      	ldp	x0, x1, [x19, #152]
fffffe00079a7d10:      	mov	w2, w22
fffffe00079a7d14:      	mov	x3, x27
fffffe00079a7d18:      	ldr	w4, [x19, #9116]
fffffe00079a7d1c:      	bl	0xfffffe00079aa484 <_nstd_rx>
fffffe00079a7d20:      	mov	x0, x19
fffffe00079a7d24:      	bl	0xfffffe00079a94bc <_decode>
fffffe00079a7d28:      	ldr	x8, [sp, #40]
fffffe00079a7d2c:      	cbz	w0, 0xfffffe00079a7d3c <_receive+0x2b4>
fffffe00079a7d30:      	mov	x0, x19
fffffe00079a7d34:      	mov	w1, #21
fffffe00079a7d38:      	b	0xfffffe00079a7b04 <_receive+0x7c>
fffffe00079a7d3c:      	ldr	x0, [sp, #32]
fffffe00079a7d40:      	mov	x1, x8
fffffe00079a7d44:      	mov	w2, #3840
fffffe00079a7d48:      	bl	0xfffffe0007990330 <_nwt_rx_desc>
fffffe00079a7d4c:      	cbz	w0, 0xfffffe00079a7d5c <_receive+0x2d4>
fffffe00079a7d50:      	mov	x0, x19
fffffe00079a7d54:      	mov	w1, #22
fffffe00079a7d58:      	b	0xfffffe00079a7b04 <_receive+0x7c>
fffffe00079a7d5c:      	ldp	x3, x4, [sp, #24]
fffffe00079a7d60:      	mov	w8, #7
fffffe00079a7d64:      	mov	x0, x19
fffffe00079a7d68:      	mov	x1, x24
fffffe00079a7d6c:      	mov	x2, x23
fffffe00079a7d70:      	mov	w5, #16
fffffe00079a7d74:      	str	w8, [x19, #40]
fffffe00079a7d78:      	bl	0xfffffe00079a9388 <_mw>
fffffe00079a7d7c:      	cbnz	w0, 0xfffffe00079a7b08 <_receive+0x80>
fffffe00079a7d80:      	mov	x0, x19
fffffe00079a7d84:      	mov	x1, x24
fffffe00079a7d88:      	mov	x2, x23
fffffe00079a7d8c:      	ldr	x3, [sp, #24]
fffffe00079a7d90:      	mov	w4, #16
fffffe00079a7d94:      	mov	w5, #1
fffffe00079a7d98:      	bl	0xfffffe00079a925c <_sy>
fffffe00079a7d9c:      	cbnz	w0, 0xfffffe00079a7b08 <_receive+0x80>
fffffe00079a7da0:      	ldr	x8, [sp, #8]
fffffe00079a7da4:      	mov	w9, #8
fffffe00079a7da8:      	mov	x0, x19
fffffe00079a7dac:      	mov	x1, x24
fffffe00079a7db0:      	mov	x2, x23
fffffe00079a7db4:      	add	x25, x8, #264
fffffe00079a7db8:      	ldr	x8, [sp, #16]
fffffe00079a7dbc:      	str	w9, [x19, #40]
fffffe00079a7dc0:      	ldr	w8, [x8, #16]
fffffe00079a7dc4:      	ldr	w4, [x25]
fffffe00079a7dc8:      	add	w3, w8, #8
fffffe00079a7dcc:      	bl	0xfffffe00079a942c <_rw>
fffffe00079a7dd0:      	cbnz	w0, 0xfffffe00079a7b08 <_receive+0x80>
fffffe00079a7dd4:      	ldr	x8, [sp, #16]
fffffe00079a7dd8:      	cmp	w22, #1
fffffe00079a7ddc:      	ldr	w9, [x25]
fffffe00079a7de0:      	ldr	w8, [x8, #20]
fffffe00079a7de4:      	add	w9, w9, #1
fffffe00079a7de8:      	udiv	w10, w9, w8
fffffe00079a7dec:      	msub	w8, w10, w8, w9
fffffe00079a7df0:      	str	w8, [x25]
fffffe00079a7df4:      	b.ne	0xfffffe00079a7e00 <_receive+0x378>
fffffe00079a7df8:      	ldr	x9, [x19]
fffffe00079a7dfc:      	str	w8, [x9, #2868]
fffffe00079a7e00:      	ldr	x8, [sp, #48]
fffffe00079a7e04:      	mov	w9, #80
fffffe00079a7e08:      	ldr	w10, [x19, #9120]
fffffe00079a7e0c:      	str	wzr, [x19, #40]
fffffe00079a7e10:      	madd	x8, x8, x9, x19
fffffe00079a7e14:      	ldr	w9, [x8, #276]
fffffe00079a7e18:      	add	w9, w9, #1
fffffe00079a7e1c:      	str	w9, [x8, #276]
fffffe00079a7e20:      	cbz	w10, 0xfffffe00079a7e40 <_receive+0x3b8>
fffffe00079a7e24:      	mov	w8, #9120
fffffe00079a7e28:      	mov	x0, x19
fffffe00079a7e2c:      	add	x3, x19, x8
fffffe00079a7e30:      	mov	x1, x21
fffffe00079a7e34:      	mov	x2, x20
fffffe00079a7e38:      	bl	0xfffffe00079a7e9c <_emit>
fffffe00079a7e3c:      	cbnz	w0, 0xfffffe00079a7b08 <_receive+0x80>
fffffe00079a7e40:      	mov	w0, #1
fffffe00079a7e44:      	b	0xfffffe00079a7b0c <_receive+0x84>
