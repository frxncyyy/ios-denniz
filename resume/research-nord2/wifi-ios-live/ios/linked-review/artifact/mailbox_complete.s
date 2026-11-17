
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4c30 <_nwl_complete>:
fffffe00079b4c30:      	mov	x8, x0
fffffe00079b4c34:      	mov	w0, #-1
fffffe00079b4c38:      	cmp	w1, #4
fffffe00079b4c3c:      	b.hi	0xfffffe00079b4d6c <_nwl_complete+0x13c>
fffffe00079b4c40:      	cbz	x8, 0xfffffe00079b4d6c <_nwl_complete+0x13c>
fffffe00079b4c44:      	cbz	x2, 0xfffffe00079b4d6c <_nwl_complete+0x13c>
fffffe00079b4c48:      	adrp	x9, 0xfffffe0008547000 <_abort_channel+0x78>
fffffe00079b4c4c:      	add	x9, x9, #288
fffffe00079b4c50:      	ldar	w9, [x9]
fffffe00079b4c54:      	cmp	w9, #2
fffffe00079b4c58:      	b.ne	0xfffffe00079b4d70 <_nwl_complete+0x140>
fffffe00079b4c5c:      	adrp	x10, 0xfffffe0008547000 <_abort_channel+0x8c>
fffffe00079b4c60:      	add	x10, x10, #292
fffffe00079b4c64:      	ldr	w9, [x8]
fffffe00079b4c68:      	ldar	w10, [x10]
fffffe00079b4c6c:      	cmp	w9, w10
fffffe00079b4c70:      	b.ne	0xfffffe00079b4d70 <_nwl_complete+0x140>
fffffe00079b4c74:      	adrp	x10, 0xfffffe0008547000 <_abort_channel+0xa4>
fffffe00079b4c78:      	add	x10, x10, #296
fffffe00079b4c7c:      	ldr	w9, [x8, #4]
fffffe00079b4c80:      	ldar	w10, [x10]
fffffe00079b4c84:      	cmp	w9, w10
fffffe00079b4c88:      	b.ne	0xfffffe00079b4d70 <_nwl_complete+0x140>
fffffe00079b4c8c:      	adrp	x10, 0xfffffe0008547000 <_abort_channel+0xbc>
fffffe00079b4c90:      	add	x10, x10, #300
fffffe00079b4c94:      	ldr	w9, [x8, #8]
fffffe00079b4c98:      	ldar	w10, [x10]
fffffe00079b4c9c:      	cmp	w9, w10
fffffe00079b4ca0:      	b.ne	0xfffffe00079b4d70 <_nwl_complete+0x140>
fffffe00079b4ca4:      	adrp	x11, 0xfffffe0008547000 <_abort_channel+0xd4>
fffffe00079b4ca8:      	add	x11, x11, #304
fffffe00079b4cac:      	mov	x10, xzr
fffffe00079b4cb0:      	ldar	w9, [x11]
fffffe00079b4cb4:      	add	w12, w9, #1
fffffe00079b4cb8:      	stlr	w12, [x11]
fffffe00079b4cbc:      	adrp	x11, 0xfffffe0008547000 <_abort_channel+0xec>
fffffe00079b4cc0:      	add	x11, x11, #276
fffffe00079b4cc4:      	add	x12, x11, x10
fffffe00079b4cc8:      	ldr	w13, [x2, x10]
fffffe00079b4ccc:      	add	x12, x12, #32
fffffe00079b4cd0:      	add	x10, x10, #4
fffffe00079b4cd4:      	cmp	x10, #256
fffffe00079b4cd8:      	stlr	w13, [x12]
fffffe00079b4cdc:      	b.ne	0xfffffe00079b4cc4 <_nwl_complete+0x94>
fffffe00079b4ce0:      	mov	w10, #17486
fffffe00079b4ce4:      	adrp	x11, 0xfffffe0008547000 <_abort_channel+0x114>
fffffe00079b4ce8:      	add	x11, x11, #280
fffffe00079b4cec:      	movk	w10, #19543, lsl #16
fffffe00079b4cf0:      	add	x12, x11, #4
fffffe00079b4cf4:      	add	x16, x11, #28
fffffe00079b4cf8:      	add	x17, x11, #32
fffffe00079b4cfc:      	mov	w2, #1
fffffe00079b4d00:      	ldar	w13, [x11]
fffffe00079b4d04:      	add	x3, x11, #36
fffffe00079b4d08:      	ldar	w12, [x12]
fffffe00079b4d0c:      	mov	w4, #256
fffffe00079b4d10:      	ldp	w14, w15, [x8]
fffffe00079b4d14:      	ldr	w8, [x8, #8]
fffffe00079b4d18:      	stlr	w10, [x16]
fffffe00079b4d1c:      	add	x10, x11, #40
fffffe00079b4d20:      	add	x16, x11, #44
fffffe00079b4d24:      	stlr	w2, [x17]
fffffe00079b4d28:      	add	x17, x11, #48
fffffe00079b4d2c:      	stlr	w4, [x3]
fffffe00079b4d30:      	mov	w0, wzr
fffffe00079b4d34:      	stlr	w13, [x10]
fffffe00079b4d38:      	add	x10, x11, #52
fffffe00079b4d3c:      	stlr	w12, [x16]
fffffe00079b4d40:      	add	x12, x11, #56
fffffe00079b4d44:      	stlr	w14, [x17]
fffffe00079b4d48:      	add	w9, w9, #2
fffffe00079b4d4c:      	stlr	w15, [x10]
fffffe00079b4d50:      	add	x10, x11, #60
fffffe00079b4d54:      	add	x13, x11, #24
fffffe00079b4d58:      	stlr	w8, [x12]
fffffe00079b4d5c:      	add	x8, x11, #8
fffffe00079b4d60:      	stlr	w1, [x10]
fffffe00079b4d64:      	stlr	w9, [x13]
fffffe00079b4d68:      	stlr	wzr, [x8]
fffffe00079b4d6c:      	ret
fffffe00079b4d70:      	mov	w0, #-1
fffffe00079b4d74:      	ret
