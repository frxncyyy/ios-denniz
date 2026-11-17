
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4bc8 <_nwl_take>:
fffffe00079b4bc8:      	cbz	x0, 0xfffffe00079b4c28 <_nwl_take+0x60>
fffffe00079b4bcc:      	adrp	x8, 0xfffffe0008547000 <_stc_channel_released+0x90>
fffffe00079b4bd0:      	add	x8, x8, #276
fffffe00079b4bd4:      	ldar	w8, [x8]
fffffe00079b4bd8:      	cmp	w8, #1
fffffe00079b4bdc:      	b.ne	0xfffffe00079b4c28 <_nwl_take+0x60>
fffffe00079b4be0:      	adrp	x9, 0xfffffe0008547000 <_abort_channel+0x10>
fffffe00079b4be4:      	add	x9, x9, #288
fffffe00079b4be8:      	ldar	w8, [x9]
fffffe00079b4bec:      	cmp	w8, #1
fffffe00079b4bf0:      	b.ne	0xfffffe00079b4c28 <_nwl_take+0x60>
fffffe00079b4bf4:      	add	x8, x9, #4
fffffe00079b4bf8:      	add	x10, x9, #8
fffffe00079b4bfc:      	add	x11, x9, #12
fffffe00079b4c00:      	mov	w12, #2
fffffe00079b4c04:      	ldar	w8, [x8]
fffffe00079b4c08:      	ldar	w10, [x10]
fffffe00079b4c0c:      	ldar	w11, [x11]
fffffe00079b4c10:      	stp	w8, w10, [x0]
fffffe00079b4c14:      	mov	w8, #1
fffffe00079b4c18:      	str	w11, [x0, #8]
fffffe00079b4c1c:      	mov	w0, w8
fffffe00079b4c20:      	stlr	w12, [x9]
fffffe00079b4c24:      	ret
fffffe00079b4c28:      	mov	w0, wzr
fffffe00079b4c2c:      	ret
