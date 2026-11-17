
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4ae8 <_nwl_submit>:
fffffe00079b4ae8:      	adrp	x9, 0xfffffe0008547000 <_stc_cancel+0x24>
fffffe00079b4aec:      	add	x9, x9, #276
fffffe00079b4af0:      	orr	w10, w1, w0
fffffe00079b4af4:      	lsr	w10, w10, #16
fffffe00079b4af8:      	ldar	w9, [x9]
fffffe00079b4afc:      	cmp	w10, #0
fffffe00079b4b00:      	ccmp	w9, #1, #0, eq
fffffe00079b4b04:      	b.eq	0xfffffe00079b4b10 <_nwl_submit+0x28>
fffffe00079b4b08:      	mov	w0, wzr
fffffe00079b4b0c:      	ret
fffffe00079b4b10:      	mov	w8, w0
fffffe00079b4b14:      	and	w9, w0, #0xfff
fffffe00079b4b18:      	mov	w0, wzr
fffffe00079b4b1c:      	cbz	w9, 0xfffffe00079b4bc4 <_nwl_submit+0xdc>
fffffe00079b4b20:      	sub	w10, w8, #8, lsl #12
fffffe00079b4b24:      	cmn	w10, #7, lsl #12
fffffe00079b4b28:      	b.lo	0xfffffe00079b4bc4 <_nwl_submit+0xdc>
fffffe00079b4b2c:      	lsr	w8, w8, #12
fffffe00079b4b30:      	cmp	w8, #1
fffffe00079b4b34:      	b.ne	0xfffffe00079b4b3c <_nwl_submit+0x54>
fffffe00079b4b38:      	cbnz	w1, 0xfffffe00079b4b08 <_nwl_submit+0x20>
fffffe00079b4b3c:      	adrp	x10, 0xfffffe0008547000 <_stc_channel_released>
fffffe00079b4b40:      	add	x10, x10, #292
fffffe00079b4b44:      	ldar	w10, [x10]
fffffe00079b4b48:      	cmp	w9, w10
fffffe00079b4b4c:      	b.ne	0xfffffe00079b4b7c <_nwl_submit+0x94>
fffffe00079b4b50:      	adrp	x9, 0xfffffe0008547000 <_stc_channel_released+0x14>
fffffe00079b4b54:      	add	x9, x9, #296
fffffe00079b4b58:      	ldar	w9, [x9]
fffffe00079b4b5c:      	cmp	w8, w9
fffffe00079b4b60:      	b.ne	0xfffffe00079b4b08 <_nwl_submit+0x20>
fffffe00079b4b64:      	adrp	x8, 0xfffffe0008547000 <_stc_channel_released+0x28>
fffffe00079b4b68:      	add	x8, x8, #300
fffffe00079b4b6c:      	ldar	w8, [x8]
fffffe00079b4b70:      	cmp	w8, w1
fffffe00079b4b74:      	cset	w0, eq
fffffe00079b4b78:      	ret
fffffe00079b4b7c:      	mov	w0, wzr
fffffe00079b4b80:      	cmp	w10, #4095
fffffe00079b4b84:      	b.eq	0xfffffe00079b4bc4 <_nwl_submit+0xdc>
fffffe00079b4b88:      	add	w10, w10, #1
fffffe00079b4b8c:      	cmp	w9, w10
fffffe00079b4b90:      	b.ne	0xfffffe00079b4bc4 <_nwl_submit+0xdc>
fffffe00079b4b94:      	adrp	x10, 0xfffffe0008547000 <_stc_channel_released+0x58>
fffffe00079b4b98:      	add	x10, x10, #288
fffffe00079b4b9c:      	ldar	w11, [x10]
fffffe00079b4ba0:      	cbnz	w11, 0xfffffe00079b4b08 <_nwl_submit+0x20>
fffffe00079b4ba4:      	add	x11, x10, #8
fffffe00079b4ba8:      	add	x12, x10, #12
fffffe00079b4bac:      	add	x13, x10, #4
fffffe00079b4bb0:      	mov	w0, #1
fffffe00079b4bb4:      	stlr	w8, [x11]
fffffe00079b4bb8:      	stlr	w1, [x12]
fffffe00079b4bbc:      	stlr	w9, [x13]
fffffe00079b4bc0:      	stlr	w0, [x10]
fffffe00079b4bc4:      	ret
