
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7dd8 <_nst_step>:
fffffe00079a7dd8:      	sub	sp, sp, #160
fffffe00079a7ddc:      	stp	x28, x27, [sp, #64]
fffffe00079a7de0:      	stp	x26, x25, [sp, #80]
fffffe00079a7de4:      	stp	x24, x23, [sp, #96]
fffffe00079a7de8:      	stp	x22, x21, [sp, #112]
fffffe00079a7dec:      	stp	x20, x19, [sp, #128]
fffffe00079a7df0:      	stp	x29, x30, [sp, #144]
fffffe00079a7df4:      	add	x29, sp, #144
fffffe00079a7df8:      	cbz	x0, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7dfc:      	mov	x19, x0
fffffe00079a7e00:      	ldr	w8, [x0, #28]
fffffe00079a7e04:      	cbz	w8, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e08:      	ldr	x8, [x19]
fffffe00079a7e0c:      	cbz	x8, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e10:      	ldr	x9, [x19, #8]
fffffe00079a7e14:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e18:      	ldr	x9, [x19, #16]
fffffe00079a7e1c:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e20:      	ldr	w9, [x19, #44]
fffffe00079a7e24:      	cbnz	w9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e28:      	ldr	w9, [x8, #12]
fffffe00079a7e2c:      	cbnz	w9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e30:      	ldr	w8, [x8, #1876]
fffffe00079a7e34:      	cbnz	w8, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e38:      	ldr	w8, [x19, #36]
fffffe00079a7e3c:      	cbz	w8, 0xfffffe00079a7e64 <_nst_step+0x8c>
fffffe00079a7e40:      	mov	w0, #-1
fffffe00079a7e44:      	ldp	x29, x30, [sp, #144]
fffffe00079a7e48:      	ldp	x20, x19, [sp, #128]
fffffe00079a7e4c:      	ldp	x22, x21, [sp, #112]
fffffe00079a7e50:      	ldp	x24, x23, [sp, #96]
fffffe00079a7e54:      	ldp	x26, x25, [sp, #80]
fffffe00079a7e58:      	ldp	x28, x27, [sp, #64]
fffffe00079a7e5c:      	add	sp, sp, #160
fffffe00079a7e60:      	ret
fffffe00079a7e64:      	mov	x24, x1
fffffe00079a7e68:      	mov	w0, #-1
fffffe00079a7e6c:      	cbz	x1, 0xfffffe00079a7e44 <_nst_step+0x6c>
fffffe00079a7e70:      	ldr	w8, [x19, #148]
fffffe00079a7e74:      	cbnz	w8, 0xfffffe00079a7e44 <_nst_step+0x6c>
fffffe00079a7e78:      	ldr	x8, [x24]
fffffe00079a7e7c:      	cbz	x8, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e80:      	ldr	x9, [x24, #8]
fffffe00079a7e84:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e88:      	ldr	x9, [x8]
fffffe00079a7e8c:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e90:      	ldr	x9, [x8, #8]
fffffe00079a7e94:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7e98:      	ldr	x9, [x8, #16]
fffffe00079a7e9c:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7ea0:      	ldr	x9, [x8, #24]
fffffe00079a7ea4:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7ea8:      	ldr	x9, [x8, #32]
fffffe00079a7eac:      	cbz	x9, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7eb0:      	mov	w22, w3
fffffe00079a7eb4:      	mov	w0, #-1
fffffe00079a7eb8:      	sub	w9, w3, #17
fffffe00079a7ebc:      	cmn	w9, #16
fffffe00079a7ec0:      	b.lo	0xfffffe00079a7e44 <_nst_step+0x6c>
fffffe00079a7ec4:      	ldr	x8, [x8, #40]
fffffe00079a7ec8:      	cbz	x8, 0xfffffe00079a7e44 <_nst_step+0x6c>
fffffe00079a7ecc:      	movi.2s	v0, #1
fffffe00079a7ed0:      	mov	x0, x19
fffffe00079a7ed4:      	mov	x1, x24
fffffe00079a7ed8:      	mov	x20, x5
fffffe00079a7edc:      	mov	x21, x4
fffffe00079a7ee0:      	mov	x23, x2
fffffe00079a7ee4:      	str	wzr, [x19, #40]
fffffe00079a7ee8:      	str	d0, [x19, #32]
fffffe00079a7eec:      	bl	0xfffffe00079a806c <_before>
fffffe00079a7ef0:      	cbnz	w0, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7ef4:      	mov	x0, x19
fffffe00079a7ef8:      	mov	x1, x24
fffffe00079a7efc:      	mov	x2, x23
fffffe00079a7f00:      	bl	0xfffffe00079a8184 <_coherent>
fffffe00079a7f04:      	cbnz	w0, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7f08:      	mov	x0, x19
fffffe00079a7f0c:      	mov	x1, x24
fffffe00079a7f10:      	mov	x2, x23
fffffe00079a7f14:      	mov	x3, x21
fffffe00079a7f18:      	mov	x4, x20
fffffe00079a7f1c:      	bl	0xfffffe00079a8258 <_transmit>
fffffe00079a7f20:      	cbnz	w0, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7f24:      	mov	w25, wzr
fffffe00079a7f28:      	adrp	x27, 0xfffffe000702d000 <_rate_ok+0x18>
fffffe00079a7f2c:      	add	x27, x27, #2492
fffffe00079a7f30:      	b	0xfffffe00079a7f50 <_nst_step+0x178>
fffffe00079a7f34:      	mov	w8, #1
fffffe00079a7f38:      	cmp	w28, #0
fffffe00079a7f3c:      	mov	w9, #3
fffffe00079a7f40:      	csel	w9, w9, wzr, eq
fffffe00079a7f44:      	cmp	w0, #0
fffffe00079a7f48:      	csel	w8, w8, w9, lt
fffffe00079a7f4c:      	cbnz	w8, 0xfffffe00079a7fa8 <_nst_step+0x1d0>
fffffe00079a7f50:      	cmp	w25, w22
fffffe00079a7f54:      	b.hs	0xfffffe00079a7fb0 <_nst_step+0x1d8>
fffffe00079a7f58:      	mov	x26, xzr
fffffe00079a7f5c:      	mov	w28, wzr
fffffe00079a7f60:      	ldr	w3, [x27, x26, lsl #2]
fffffe00079a7f64:      	mov	x0, x19
fffffe00079a7f68:      	mov	x1, x24
fffffe00079a7f6c:      	mov	x2, x23
fffffe00079a7f70:      	mov	x4, x21
fffffe00079a7f74:      	mov	x5, x20
fffffe00079a7f78:      	bl	0xfffffe00079a8b0c <_receive>
fffffe00079a7f7c:      	tbnz	w0, #31, 0xfffffe00079a7f34 <_nst_step+0x15c>
fffffe00079a7f80:      	cmp	w0, #0
fffffe00079a7f84:      	mov	w8, #4
fffffe00079a7f88:      	cinc	w25, w25, ne
fffffe00079a7f8c:      	cinc	w28, w28, ne
fffffe00079a7f90:      	cmp	x26, #3
fffffe00079a7f94:      	b.hi	0xfffffe00079a7f38 <_nst_step+0x160>
fffffe00079a7f98:      	add	x26, x26, #1
fffffe00079a7f9c:      	cmp	w25, w22
fffffe00079a7fa0:      	b.lo	0xfffffe00079a7f60 <_nst_step+0x188>
fffffe00079a7fa4:      	b	0xfffffe00079a7f38 <_nst_step+0x160>
fffffe00079a7fa8:      	cmp	w8, #3
fffffe00079a7fac:      	b.ne	0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a7fb0:      	ldr	w8, [x19, #60]
fffffe00079a7fb4:      	cbz	w8, 0xfffffe00079a8048 <_nst_step+0x270>
fffffe00079a7fb8:      	ldr	w8, [x19, #136]
fffffe00079a7fbc:      	cbz	w8, 0xfffffe00079a8048 <_nst_step+0x270>
fffffe00079a7fc0:      	ldr	w8, [x19, #140]
fffffe00079a7fc4:      	cbz	w8, 0xfffffe00079a802c <_nst_step+0x254>
fffffe00079a7fc8:      	ldr	w8, [x19, #144]
fffffe00079a7fcc:      	ldr	x9, [x19, #160]
fffffe00079a7fd0:      	cmp	w8, #256
fffffe00079a7fd4:      	stp	xzr, xzr, [sp, #40]
fffffe00079a7fd8:      	strh	wzr, [sp, #32]
fffffe00079a7fdc:      	stp	xzr, x9, [sp, #16]
fffffe00079a7fe0:      	ldp	w10, w9, [x19, #120]
fffffe00079a7fe4:      	str	wzr, [sp, #36]
fffffe00079a7fe8:      	strb	wzr, [sp, #41]
fffffe00079a7fec:      	str	xzr, [sp, #56]
fffffe00079a7ff0:      	strb	w10, [sp, #34]
fffffe00079a7ff4:      	mov	w10, #2
fffffe00079a7ff8:      	strb	w9, [sp, #35]
fffffe00079a7ffc:      	str	wzr, [x19, #60]
fffffe00079a8000:      	stp	w10, w8, [sp, #8]
fffffe00079a8004:      	b.ne	0xfffffe00079a8010 <_nst_step+0x238>
fffffe00079a8008:      	mov	x0, x19
fffffe00079a800c:      	bl	0xfffffe00079a6744 <_nst_close_port>
fffffe00079a8010:      	add	x3, sp, #8
fffffe00079a8014:      	mov	x0, x19
fffffe00079a8018:      	mov	x1, x21
fffffe00079a801c:      	mov	x2, x20
fffffe00079a8020:      	bl	0xfffffe00079a8f10 <_emit>
fffffe00079a8024:      	cbnz	w0, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a8028:      	b	0xfffffe00079a8048 <_nst_step+0x270>
fffffe00079a802c:      	mov	x0, x19
fffffe00079a8030:      	bl	0xfffffe00079a8ecc <_retired_air_failure>
fffffe00079a8034:      	cbz	w0, 0xfffffe00079a8048 <_nst_step+0x270>
fffffe00079a8038:      	ldr	x8, [x19, #168]
fffffe00079a803c:      	ldr	x9, [x19, #184]
fffffe00079a8040:      	cmp	x8, x9
fffffe00079a8044:      	b.hs	0xfffffe00079a8064 <_nst_step+0x28c>
fffffe00079a8048:      	mov	x0, x19
fffffe00079a804c:      	mov	x1, x21
fffffe00079a8050:      	mov	x2, x20
fffffe00079a8054:      	bl	0xfffffe00079a8f78 <_peer_progress>
fffffe00079a8058:      	cbnz	w0, 0xfffffe00079a7e40 <_nst_step+0x68>
fffffe00079a805c:      	stp	wzr, wzr, [x19, #36]
fffffe00079a8060:      	b	0xfffffe00079a7e44 <_nst_step+0x6c>
fffffe00079a8064:      	mov	w8, #256
fffffe00079a8068:      	b	0xfffffe00079a7fcc <_nst_step+0x1f4>
