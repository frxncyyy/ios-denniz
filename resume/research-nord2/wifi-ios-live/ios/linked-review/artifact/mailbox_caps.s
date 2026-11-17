
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4d78 <_nwl_caps>:
fffffe00079b4d78:      	sub	sp, sp, #96
fffffe00079b4d7c:      	stp	x29, x30, [sp, #80]
fffffe00079b4d80:      	add	x29, sp, #80
fffffe00079b4d84:      	adrp	x9, 0xfffffe0007c9c000 <_stop+0x24>
fffffe00079b4d88:      	mov	x8, xzr
fffffe00079b4d8c:      	nop
fffffe00079b4d90:      	ldr	x9, [x9, #3440]
fffffe00079b4d94:      	stur	x9, [x29, #-8]
fffffe00079b4d98:      	cbz	x0, 0xfffffe00079b4e88 <_nwl_caps+0x110>
fffffe00079b4d9c:      	cmp	x1, #64
fffffe00079b4da0:      	b.lo	0xfffffe00079b4e88 <_nwl_caps+0x110>
fffffe00079b4da4:      	adrp	x8, 0xfffffe0007031000 <_decode+0xc50>
fffffe00079b4da8:      	add	x8, x8, #348
fffffe00079b4dac:      	ldp	q0, q1, [x8]
fffffe00079b4db0:      	ldp	q2, q3, [x8, #32]
fffffe00079b4db4:      	adrp	x8, 0xfffffe0008547000 <_release_done+0xa8>
fffffe00079b4db8:      	add	x8, x8, #276
fffffe00079b4dbc:      	stp	q0, q1, [sp]
fffffe00079b4dc0:      	stp	q2, q3, [sp, #32]
fffffe00079b4dc4:      	ldar	w8, [x8]
fffffe00079b4dc8:      	cmp	w8, #1
fffffe00079b4dcc:      	cset	w8, eq
fffffe00079b4dd0:      	str	w8, [sp, #36]
fffffe00079b4dd4:      	b.ne	0xfffffe00079b4e18 <_nwl_caps+0xa0>
fffffe00079b4dd8:      	adrp	x8, 0xfffffe0008547000 <_release_done+0xcc>
fffffe00079b4ddc:      	add	x8, x8, #280
fffffe00079b4de0:      	add	x10, x8, #4
fffffe00079b4de4:      	ldar	w11, [x8]
fffffe00079b4de8:      	ldar	w9, [x10]
fffffe00079b4dec:      	add	x10, x8, #8
fffffe00079b4df0:      	stp	w11, w9, [sp, #28]
fffffe00079b4df4:      	ldar	w11, [x10]
fffffe00079b4df8:      	add	x10, x8, #12
fffffe00079b4dfc:      	ldar	w9, [x10]
fffffe00079b4e00:      	add	x10, x8, #16
fffffe00079b4e04:      	add	x8, x8, #20
fffffe00079b4e08:      	stp	w11, w9, [sp, #40]
fffffe00079b4e0c:      	ldar	w9, [x10]
fffffe00079b4e10:      	ldar	w8, [x8]
fffffe00079b4e14:      	stp	w9, w8, [sp, #48]
fffffe00079b4e18:      	ldp	q1, q0, [sp, #32]
fffffe00079b4e1c:      	mov	w8, #64
fffffe00079b4e20:      	xtn.4h	v6, v1
fffffe00079b4e24:      	ldp	q3, q2, [sp]
fffffe00079b4e28:      	xtn.4h	v7, v0
fffffe00079b4e2c:      	ushr.4h	v6, v6, #8
fffffe00079b4e30:      	uzp1.8h	v4, v1, v0
fffffe00079b4e34:      	ushr.4h	v7, v7, #8
fffffe00079b4e38:      	xtn.4h	v16, v3
fffffe00079b4e3c:      	mov.d	v6[1], v7[0]
fffffe00079b4e40:      	xtn.4h	v17, v2
fffffe00079b4e44:      	ushr.4h	v16, v16, #8
fffffe00079b4e48:      	ushr.4h	v17, v17, #8
fffffe00079b4e4c:      	uzp1.8h	v5, v3, v2
fffffe00079b4e50:      	mov.d	v16[1], v17[0]
fffffe00079b4e54:      	uzp2.8h	v7, v1, v0
fffffe00079b4e58:      	uzp2.8h	v17, v3, v2
fffffe00079b4e5c:      	ushr.4s	v0, v0, #24
fffffe00079b4e60:      	ushr.4s	v1, v1, #24
fffffe00079b4e64:      	ushr.4s	v2, v2, #24
fffffe00079b4e68:      	ushr.4s	v3, v3, #24
fffffe00079b4e6c:      	uzp1.8h	v0, v1, v0
fffffe00079b4e70:      	uzp1.8h	v1, v3, v2
fffffe00079b4e74:      	uzp1.16b	v2, v5, v4
fffffe00079b4e78:      	uzp1.16b	v3, v16, v6
fffffe00079b4e7c:      	uzp1.16b	v4, v17, v7
fffffe00079b4e80:      	uzp1.16b	v5, v1, v0
fffffe00079b4e84:      	st4.16b	{ v2, v3, v4, v5 }, [x0]
fffffe00079b4e88:      	adrp	x9, 0xfffffe0007c9c000 <_stop+0x128>
fffffe00079b4e8c:      	nop
fffffe00079b4e90:      	ldur	x10, [x29, #-8]
fffffe00079b4e94:      	ldr	x9, [x9, #3440]
fffffe00079b4e98:      	cmp	x9, x10
fffffe00079b4e9c:      	b.ne	0xfffffe00079b4eb0 <_nwl_caps+0x138>
fffffe00079b4ea0:      	ldp	x29, x30, [sp, #80]
fffffe00079b4ea4:      	mov	x0, x8
fffffe00079b4ea8:      	add	sp, sp, #96
fffffe00079b4eac:      	ret
fffffe00079b4eb0:      	bl	0xfffffe0007a09274 <___stack_chk_fail>
