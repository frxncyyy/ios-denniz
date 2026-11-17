
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4aa4 <_nwl_initialize>:
fffffe00079b4aa4:      	cbz	x0, 0xfffffe00079b4ab8 <_nwl_initialize+0x14>
fffffe00079b4aa8:      	adrp	x9, 0xfffffe0008547000 <_stc_link_lost+0x60>
fffffe00079b4aac:      	add	x9, x9, #276
fffffe00079b4ab0:      	ldar	w10, [x9]
fffffe00079b4ab4:      	cbz	w10, 0xfffffe00079b4ac0 <_nwl_initialize+0x1c>
fffffe00079b4ab8:      	mov	w0, #-1
fffffe00079b4abc:      	ret
fffffe00079b4ac0:      	mov	x8, x0
fffffe00079b4ac4:      	mov	w0, wzr
fffffe00079b4ac8:      	add	x10, x9, #4
fffffe00079b4acc:      	lsr	x11, x8, #32
fffffe00079b4ad0:      	add	x12, x9, #8
fffffe00079b4ad4:      	mov	w13, #1
fffffe00079b4ad8:      	stlr	w8, [x10]
fffffe00079b4adc:      	stlr	w11, [x12]
fffffe00079b4ae0:      	stlr	w13, [x9]
fffffe00079b4ae4:      	ret
