
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a6db0 <_nst_step>:
fffffe00079a6db0:      	sub	sp, sp, #160
fffffe00079a6db4:      	stp	x28, x27, [sp, #64]
fffffe00079a6db8:      	stp	x26, x25, [sp, #80]
fffffe00079a6dbc:      	stp	x24, x23, [sp, #96]
fffffe00079a6dc0:      	stp	x22, x21, [sp, #112]
fffffe00079a6dc4:      	stp	x20, x19, [sp, #128]
fffffe00079a6dc8:      	stp	x29, x30, [sp, #144]
fffffe00079a6dcc:      	add	x29, sp, #144
fffffe00079a6dd0:      	cbz	x0, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6dd4:      	mov	x19, x0
fffffe00079a6dd8:      	ldr	w8, [x0, #28]
fffffe00079a6ddc:      	cbz	w8, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6de0:      	ldr	x8, [x19]
fffffe00079a6de4:      	cbz	x8, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6de8:      	ldr	x9, [x19, #8]
fffffe00079a6dec:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6df0:      	ldr	x9, [x19, #16]
fffffe00079a6df4:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6df8:      	ldr	w9, [x19, #44]
fffffe00079a6dfc:      	cbnz	w9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e00:      	ldr	w9, [x8, #12]
fffffe00079a6e04:      	cbnz	w9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e08:      	ldr	w8, [x8, #1876]
fffffe00079a6e0c:      	cbnz	w8, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e10:      	ldr	w8, [x19, #36]
fffffe00079a6e14:      	cbz	w8, 0xfffffe00079a6e3c <_nst_step+0x8c>
fffffe00079a6e18:      	mov	w0, #-1
fffffe00079a6e1c:      	ldp	x29, x30, [sp, #144]
fffffe00079a6e20:      	ldp	x20, x19, [sp, #128]
fffffe00079a6e24:      	ldp	x22, x21, [sp, #112]
fffffe00079a6e28:      	ldp	x24, x23, [sp, #96]
fffffe00079a6e2c:      	ldp	x26, x25, [sp, #80]
fffffe00079a6e30:      	ldp	x28, x27, [sp, #64]
fffffe00079a6e34:      	add	sp, sp, #160
fffffe00079a6e38:      	ret
fffffe00079a6e3c:      	mov	x24, x1
fffffe00079a6e40:      	mov	w0, #-1
fffffe00079a6e44:      	cbz	x1, 0xfffffe00079a6e1c <_nst_step+0x6c>
fffffe00079a6e48:      	ldr	w8, [x19, #148]
fffffe00079a6e4c:      	cbnz	w8, 0xfffffe00079a6e1c <_nst_step+0x6c>
fffffe00079a6e50:      	ldr	x8, [x24]
fffffe00079a6e54:      	cbz	x8, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e58:      	ldr	x9, [x24, #8]
fffffe00079a6e5c:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e60:      	ldr	x9, [x8]
fffffe00079a6e64:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e68:      	ldr	x9, [x8, #8]
fffffe00079a6e6c:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e70:      	ldr	x9, [x8, #16]
fffffe00079a6e74:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e78:      	ldr	x9, [x8, #24]
fffffe00079a6e7c:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e80:      	ldr	x9, [x8, #32]
fffffe00079a6e84:      	cbz	x9, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6e88:      	mov	w22, w3
fffffe00079a6e8c:      	mov	w0, #-1
fffffe00079a6e90:      	sub	w9, w3, #17
fffffe00079a6e94:      	cmn	w9, #16
fffffe00079a6e98:      	b.lo	0xfffffe00079a6e1c <_nst_step+0x6c>
fffffe00079a6e9c:      	ldr	x8, [x8, #40]
fffffe00079a6ea0:      	cbz	x8, 0xfffffe00079a6e1c <_nst_step+0x6c>
fffffe00079a6ea4:      	movi.2s	v0, #1
fffffe00079a6ea8:      	mov	x0, x19
fffffe00079a6eac:      	mov	x1, x24
fffffe00079a6eb0:      	mov	x20, x5
fffffe00079a6eb4:      	mov	x21, x4
fffffe00079a6eb8:      	mov	x23, x2
fffffe00079a6ebc:      	str	wzr, [x19, #40]
fffffe00079a6ec0:      	str	d0, [x19, #32]
fffffe00079a6ec4:      	bl	0xfffffe00079a7054 <_before>
fffffe00079a6ec8:      	cbnz	w0, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6ecc:      	mov	x0, x19
fffffe00079a6ed0:      	mov	x1, x24
fffffe00079a6ed4:      	mov	x2, x23
fffffe00079a6ed8:      	bl	0xfffffe00079a716c <_coherent>
fffffe00079a6edc:      	cbnz	w0, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6ee0:      	mov	x0, x19
fffffe00079a6ee4:      	mov	x1, x24
fffffe00079a6ee8:      	mov	x2, x23
fffffe00079a6eec:      	mov	x3, x21
fffffe00079a6ef0:      	mov	x4, x20
fffffe00079a6ef4:      	bl	0xfffffe00079a7240 <_transmit>
fffffe00079a6ef8:      	cbnz	w0, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6efc:      	mov	w25, wzr
fffffe00079a6f00:      	adrp	x27, 0xfffffe000702d000 <_rate_ok+0x20>
fffffe00079a6f04:      	add	x27, x27, #2636
fffffe00079a6f08:      	b	0xfffffe00079a6f28 <_nst_step+0x178>
fffffe00079a6f0c:      	mov	w8, #1
fffffe00079a6f10:      	cmp	w28, #0
fffffe00079a6f14:      	mov	w9, #3
fffffe00079a6f18:      	csel	w9, w9, wzr, eq
fffffe00079a6f1c:      	cmp	w0, #0
fffffe00079a6f20:      	csel	w8, w8, w9, lt
fffffe00079a6f24:      	cbnz	w8, 0xfffffe00079a6f80 <_nst_step+0x1d0>
fffffe00079a6f28:      	cmp	w25, w22
fffffe00079a6f2c:      	b.hs	0xfffffe00079a6f88 <_nst_step+0x1d8>
fffffe00079a6f30:      	mov	x26, xzr
fffffe00079a6f34:      	mov	w28, wzr
fffffe00079a6f38:      	ldr	w3, [x27, x26, lsl #2]
fffffe00079a6f3c:      	mov	x0, x19
fffffe00079a6f40:      	mov	x1, x24
fffffe00079a6f44:      	mov	x2, x23
fffffe00079a6f48:      	mov	x4, x21
fffffe00079a6f4c:      	mov	x5, x20
fffffe00079a6f50:      	bl	0xfffffe00079a7a88 <_receive>
fffffe00079a6f54:      	tbnz	w0, #31, 0xfffffe00079a6f0c <_nst_step+0x15c>
fffffe00079a6f58:      	cmp	w0, #0
fffffe00079a6f5c:      	mov	w8, #4
fffffe00079a6f60:      	cinc	w25, w25, ne
fffffe00079a6f64:      	cinc	w28, w28, ne
fffffe00079a6f68:      	cmp	x26, #3
fffffe00079a6f6c:      	b.hi	0xfffffe00079a6f10 <_nst_step+0x160>
fffffe00079a6f70:      	add	x26, x26, #1
fffffe00079a6f74:      	cmp	w25, w22
fffffe00079a6f78:      	b.lo	0xfffffe00079a6f38 <_nst_step+0x188>
fffffe00079a6f7c:      	b	0xfffffe00079a6f10 <_nst_step+0x160>
fffffe00079a6f80:      	cmp	w8, #3
fffffe00079a6f84:      	b.ne	0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a6f88:      	ldr	w8, [x19, #60]
fffffe00079a6f8c:      	cbz	w8, 0xfffffe00079a7030 <_nst_step+0x280>
fffffe00079a6f90:      	ldr	w8, [x19, #136]
fffffe00079a6f94:      	cbz	w8, 0xfffffe00079a7030 <_nst_step+0x280>
fffffe00079a6f98:      	ldr	w8, [x19, #140]
fffffe00079a6f9c:      	cbz	w8, 0xfffffe00079a7014 <_nst_step+0x264>
fffffe00079a6fa0:      	ldr	w8, [x19, #144]
fffffe00079a6fa4:      	mov	w9, #2
fffffe00079a6fa8:      	ldr	x10, [x19, #160]
fffffe00079a6fac:      	stp	xzr, xzr, [sp, #40]
fffffe00079a6fb0:      	strh	wzr, [sp, #32]
fffffe00079a6fb4:      	stp	w9, w8, [sp, #8]
fffffe00079a6fb8:      	stp	xzr, x10, [sp, #16]
fffffe00079a6fbc:      	ldp	w9, w10, [x19, #120]
fffffe00079a6fc0:      	str	wzr, [sp, #36]
fffffe00079a6fc4:      	strb	wzr, [sp, #41]
fffffe00079a6fc8:      	str	xzr, [sp, #56]
fffffe00079a6fcc:      	strb	w10, [sp, #35]
fffffe00079a6fd0:      	cmp	w9, #127
fffffe00079a6fd4:      	mov	w10, #1
fffffe00079a6fd8:      	strb	w9, [sp, #34]
fffffe00079a6fdc:      	csinc	w9, w10, w9, eq
fffffe00079a6fe0:      	cmp	w8, #256
fffffe00079a6fe4:      	str	wzr, [x19, #60]
fffffe00079a6fe8:      	str	w9, [x19, #104]
fffffe00079a6fec:      	b.ne	0xfffffe00079a6ff8 <_nst_step+0x248>
fffffe00079a6ff0:      	mov	x0, x19
fffffe00079a6ff4:      	bl	0xfffffe00079a5718 <_nst_close_port>
fffffe00079a6ff8:      	add	x3, sp, #8
fffffe00079a6ffc:      	mov	x0, x19
fffffe00079a7000:      	mov	x1, x21
fffffe00079a7004:      	mov	x2, x20
fffffe00079a7008:      	bl	0xfffffe00079a7e9c <_emit>
fffffe00079a700c:      	cbnz	w0, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a7010:      	b	0xfffffe00079a7030 <_nst_step+0x280>
fffffe00079a7014:      	mov	x0, x19
fffffe00079a7018:      	bl	0xfffffe00079a7e48 <_retired_air_failure>
fffffe00079a701c:      	cbz	w0, 0xfffffe00079a7030 <_nst_step+0x280>
fffffe00079a7020:      	ldr	x8, [x19, #168]
fffffe00079a7024:      	ldr	x9, [x19, #184]
fffffe00079a7028:      	cmp	x8, x9
fffffe00079a702c:      	b.hs	0xfffffe00079a704c <_nst_step+0x29c>
fffffe00079a7030:      	mov	x0, x19
fffffe00079a7034:      	mov	x1, x21
fffffe00079a7038:      	mov	x2, x20
fffffe00079a703c:      	bl	0xfffffe00079a7f04 <_peer_progress>
fffffe00079a7040:      	cbnz	w0, 0xfffffe00079a6e18 <_nst_step+0x68>
fffffe00079a7044:      	stp	wzr, wzr, [x19, #36]
fffffe00079a7048:      	b	0xfffffe00079a6e1c <_nst_step+0x6c>
fffffe00079a704c:      	mov	w8, #256
fffffe00079a7050:      	b	0xfffffe00079a6fa4 <_nst_step+0x1f4>
