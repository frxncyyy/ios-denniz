
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4eb4 <_nwl_reply>:
fffffe00079b4eb4:      	sub	sp, sp, #304
fffffe00079b4eb8:      	stp	x28, x27, [sp, #272]
fffffe00079b4ebc:      	stp	x29, x30, [sp, #288]
fffffe00079b4ec0:      	add	x29, sp, #288
fffffe00079b4ec4:      	adrp	x9, 0xfffffe0007c9c000 <_stop+0x164>
fffffe00079b4ec8:      	mov	x8, xzr
fffffe00079b4ecc:      	nop
fffffe00079b4ed0:      	ldr	x9, [x9, #3440]
fffffe00079b4ed4:      	stur	x9, [x29, #-24]
fffffe00079b4ed8:      	cbz	x0, 0xfffffe00079b512c <_nwl_reply+0x278>
fffffe00079b4edc:      	cmp	x1, #256
fffffe00079b4ee0:      	b.lo	0xfffffe00079b512c <_nwl_reply+0x278>
fffffe00079b4ee4:      	adrp	x9, 0xfffffe0008547000 <_nwpc_hmac+0xc0>
fffffe00079b4ee8:      	add	x9, x9, #276
fffffe00079b4eec:      	ldar	w8, [x9]
fffffe00079b4ef0:      	cmp	w8, #1
fffffe00079b4ef4:      	b.ne	0xfffffe00079b5128 <_nwl_reply+0x274>
fffffe00079b4ef8:      	adrp	x10, 0xfffffe0008547000 <_nwpc_hmac+0xd4>
fffffe00079b4efc:      	add	x10, x10, #304
fffffe00079b4f00:      	mov	x8, xzr
fffffe00079b4f04:      	ldar	w11, [x10]
fffffe00079b4f08:      	cbz	w11, 0xfffffe00079b512c <_nwl_reply+0x278>
fffffe00079b4f0c:      	tbnz	w11, #0, 0xfffffe00079b512c <_nwl_reply+0x278>
fffffe00079b4f10:      	movi.2d	v0, #0000000000000000
fffffe00079b4f14:      	mov	x8, xzr
fffffe00079b4f18:      	mov	x12, sp
fffffe00079b4f1c:      	stp	q0, q0, [sp]
fffffe00079b4f20:      	stp	q0, q0, [sp, #32]
fffffe00079b4f24:      	stp	q0, q0, [sp, #64]
fffffe00079b4f28:      	stp	q0, q0, [sp, #96]
fffffe00079b4f2c:      	stp	q0, q0, [sp, #128]
fffffe00079b4f30:      	stp	q0, q0, [sp, #160]
fffffe00079b4f34:      	stp	q0, q0, [sp, #192]
fffffe00079b4f38:      	stp	q0, q0, [sp, #224]
fffffe00079b4f3c:      	add	x13, x9, x8
fffffe00079b4f40:      	add	x13, x13, #32
fffffe00079b4f44:      	ldar	w13, [x13]
fffffe00079b4f48:      	str	w13, [x12, x8]
fffffe00079b4f4c:      	add	x8, x8, #4
fffffe00079b4f50:      	cmp	x8, #256
fffffe00079b4f54:      	b.ne	0xfffffe00079b4f3c <_nwl_reply+0x88>
fffffe00079b4f58:      	ldar	w8, [x10]
fffffe00079b4f5c:      	cmp	w11, w8
fffffe00079b4f60:      	b.ne	0xfffffe00079b5128 <_nwl_reply+0x274>
fffffe00079b4f64:      	ldp	q1, q0, [sp, #32]
fffffe00079b4f68:      	mov	x8, x0
fffffe00079b4f6c:      	add	x9, x0, #192
fffffe00079b4f70:      	xtn.4h	v6, v1
fffffe00079b4f74:      	ldp	q3, q2, [sp]
fffffe00079b4f78:      	xtn.4h	v7, v0
fffffe00079b4f7c:      	ushr.4h	v6, v6, #8
fffffe00079b4f80:      	uzp1.8h	v4, v1, v0
fffffe00079b4f84:      	ushr.4h	v7, v7, #8
fffffe00079b4f88:      	xtn.4h	v16, v3
fffffe00079b4f8c:      	mov.d	v6[1], v7[0]
fffffe00079b4f90:      	xtn.4h	v17, v2
fffffe00079b4f94:      	ushr.4h	v16, v16, #8
fffffe00079b4f98:      	ushr.4h	v17, v17, #8
fffffe00079b4f9c:      	uzp1.8h	v5, v3, v2
fffffe00079b4fa0:      	mov.d	v16[1], v17[0]
fffffe00079b4fa4:      	uzp2.8h	v7, v1, v0
fffffe00079b4fa8:      	uzp2.8h	v17, v3, v2
fffffe00079b4fac:      	ushr.4s	v0, v0, #24
fffffe00079b4fb0:      	ushr.4s	v1, v1, #24
fffffe00079b4fb4:      	ushr.4s	v2, v2, #24
fffffe00079b4fb8:      	ushr.4s	v3, v3, #24
fffffe00079b4fbc:      	uzp1.8h	v0, v1, v0
fffffe00079b4fc0:      	uzp1.8h	v1, v3, v2
fffffe00079b4fc4:      	uzp1.16b	v2, v5, v4
fffffe00079b4fc8:      	uzp1.16b	v3, v16, v6
fffffe00079b4fcc:      	uzp1.16b	v4, v17, v7
fffffe00079b4fd0:      	uzp1.16b	v5, v1, v0
fffffe00079b4fd4:      	st4.16b	{ v2, v3, v4, v5 }, [x8], #64
fffffe00079b4fd8:      	ldp	q1, q0, [sp, #96]
fffffe00079b4fdc:      	xtn.4h	v6, v1
fffffe00079b4fe0:      	ldp	q3, q2, [sp, #64]
fffffe00079b4fe4:      	xtn.4h	v7, v0
fffffe00079b4fe8:      	ushr.4h	v6, v6, #8
fffffe00079b4fec:      	uzp1.8h	v4, v1, v0
fffffe00079b4ff0:      	ushr.4h	v7, v7, #8
fffffe00079b4ff4:      	xtn.4h	v16, v3
fffffe00079b4ff8:      	mov.d	v6[1], v7[0]
fffffe00079b4ffc:      	xtn.4h	v17, v2
fffffe00079b5000:      	uzp1.8h	v5, v3, v2
fffffe00079b5004:      	ushr.4h	v16, v16, #8
fffffe00079b5008:      	ushr.4h	v17, v17, #8
fffffe00079b500c:      	uzp2.8h	v7, v1, v0
fffffe00079b5010:      	ushr.4s	v0, v0, #24
fffffe00079b5014:      	mov.d	v16[1], v17[0]
fffffe00079b5018:      	uzp2.8h	v17, v3, v2
fffffe00079b501c:      	ushr.4s	v1, v1, #24
fffffe00079b5020:      	ushr.4s	v2, v2, #24
fffffe00079b5024:      	ushr.4s	v3, v3, #24
fffffe00079b5028:      	uzp1.8h	v0, v1, v0
fffffe00079b502c:      	uzp1.8h	v1, v3, v2
fffffe00079b5030:      	ldp	q3, q2, [sp, #160]
fffffe00079b5034:      	uzp1.16b	v18, v5, v4
fffffe00079b5038:      	uzp1.16b	v19, v16, v6
fffffe00079b503c:      	uzp1.16b	v20, v17, v7
fffffe00079b5040:      	xtn.4h	v6, v3
fffffe00079b5044:      	uzp1.16b	v21, v1, v0
fffffe00079b5048:      	ldp	q5, q4, [sp, #128]
fffffe00079b504c:      	xtn.4h	v7, v2
fffffe00079b5050:      	ushr.4h	v6, v6, #8
fffffe00079b5054:      	uzp1.8h	v0, v3, v2
fffffe00079b5058:      	ushr.4h	v7, v7, #8
fffffe00079b505c:      	st4.16b	{ v18, v19, v20, v21 }, [x8]
fffffe00079b5060:      	xtn.4h	v16, v5
fffffe00079b5064:      	add	x8, x0, #128
fffffe00079b5068:      	mov.d	v6[1], v7[0]
fffffe00079b506c:      	xtn.4h	v17, v4
fffffe00079b5070:      	uzp1.8h	v1, v5, v4
fffffe00079b5074:      	ushr.4h	v16, v16, #8
fffffe00079b5078:      	ushr.4h	v17, v17, #8
fffffe00079b507c:      	uzp2.8h	v7, v3, v2
fffffe00079b5080:      	ushr.4s	v2, v2, #24
fffffe00079b5084:      	mov.d	v16[1], v17[0]
fffffe00079b5088:      	uzp2.8h	v17, v5, v4
fffffe00079b508c:      	ushr.4s	v3, v3, #24
fffffe00079b5090:      	ushr.4s	v4, v4, #24
fffffe00079b5094:      	ushr.4s	v5, v5, #24
fffffe00079b5098:      	uzp1.8h	v2, v3, v2
fffffe00079b509c:      	uzp1.8h	v3, v5, v4
fffffe00079b50a0:      	uzp1.16b	v22, v1, v0
fffffe00079b50a4:      	ldp	q0, q4, [sp, #224]
fffffe00079b50a8:      	uzp1.16b	v23, v16, v6
fffffe00079b50ac:      	uzp1.16b	v24, v17, v7
fffffe00079b50b0:      	uzp1.16b	v25, v3, v2
fffffe00079b50b4:      	xtn.4h	v6, v0
fffffe00079b50b8:      	ldp	q5, q1, [sp, #192]
fffffe00079b50bc:      	xtn.4h	v7, v4
fffffe00079b50c0:      	ushr.4h	v6, v6, #8
fffffe00079b50c4:      	uzp1.8h	v2, v0, v4
fffffe00079b50c8:      	st4.16b	{ v22, v23, v24, v25 }, [x8]
fffffe00079b50cc:      	ushr.4h	v7, v7, #8
fffffe00079b50d0:      	mov	w8, #256
fffffe00079b50d4:      	xtn.4h	v16, v5
fffffe00079b50d8:      	mov.d	v6[1], v7[0]
fffffe00079b50dc:      	xtn.4h	v17, v1
fffffe00079b50e0:      	ushr.4h	v16, v16, #8
fffffe00079b50e4:      	ushr.4h	v17, v17, #8
fffffe00079b50e8:      	uzp1.8h	v3, v5, v1
fffffe00079b50ec:      	mov.d	v16[1], v17[0]
fffffe00079b50f0:      	uzp2.8h	v7, v0, v4
fffffe00079b50f4:      	uzp2.8h	v17, v5, v1
fffffe00079b50f8:      	ushr.4s	v4, v4, #24
fffffe00079b50fc:      	ushr.4s	v0, v0, #24
fffffe00079b5100:      	ushr.4s	v1, v1, #24
fffffe00079b5104:      	ushr.4s	v5, v5, #24
fffffe00079b5108:      	uzp1.8h	v0, v0, v4
fffffe00079b510c:      	uzp1.8h	v1, v5, v1
fffffe00079b5110:      	uzp1.16b	v2, v3, v2
fffffe00079b5114:      	uzp1.16b	v3, v16, v6
fffffe00079b5118:      	uzp1.16b	v4, v17, v7
fffffe00079b511c:      	uzp1.16b	v5, v1, v0
fffffe00079b5120:      	st4.16b	{ v2, v3, v4, v5 }, [x9]
fffffe00079b5124:      	b	0xfffffe00079b512c <_nwl_reply+0x278>
fffffe00079b5128:      	mov	x8, xzr
fffffe00079b512c:      	adrp	x9, 0xfffffe0007c9c000 <_stc_begin+0x38>
fffffe00079b5130:      	nop
fffffe00079b5134:      	ldur	x10, [x29, #-24]
fffffe00079b5138:      	ldr	x9, [x9, #3440]
fffffe00079b513c:      	cmp	x9, x10
fffffe00079b5140:      	b.ne	0xfffffe00079b5158 <_nwl_reply+0x2a4>
fffffe00079b5144:      	ldp	x29, x30, [sp, #288]
fffffe00079b5148:      	mov	x0, x8
fffffe00079b514c:      	ldp	x28, x27, [sp, #272]
fffffe00079b5150:      	add	sp, sp, #304
fffffe00079b5154:      	ret
fffffe00079b5158:      	bl	0xfffffe0007a09274 <___stack_chk_fail>
