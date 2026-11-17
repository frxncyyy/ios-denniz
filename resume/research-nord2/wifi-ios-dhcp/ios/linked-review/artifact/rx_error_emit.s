
$NORD2_REPO/research/ci-39865241324/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b1ae4 <_nsdd_rx_error>:
fffffe00079b1ae4:      	sub	sp, sp, #192
fffffe00079b1ae8:      	stp	x29, x30, [sp, #176]
fffffe00079b1aec:      	add	x29, sp, #176
fffffe00079b1af0:      	movi.2d	v0, #0000000000000000
fffffe00079b1af4:      	mov	x4, x3
fffffe00079b1af8:      	mov	x3, x2
fffffe00079b1afc:      	mov	x2, x1
fffffe00079b1b00:      	sub	x1, x29, #64
fffffe00079b1b04:      	stp	q0, q0, [x29, #-64]
fffffe00079b1b08:      	stp	q0, q0, [x29, #-32]
fffffe00079b1b0c:      	bl	0xfffffe00079b1998 <_nsdd_rx_error_take>
fffffe00079b1b10:      	cbz	w0, 0xfffffe00079b1b5c <_nsdd_rx_error+0x78>
fffffe00079b1b14:      	ldp	w17, w0, [x29, #-24]
fffffe00079b1b18:      	ldp	w1, w2, [x29, #-16]
fffffe00079b1b1c:      	ldp	w9, w10, [x29, #-56]
fffffe00079b1b20:      	ldp	w11, w12, [x29, #-48]
fffffe00079b1b24:      	ldp	w13, w14, [x29, #-40]
fffffe00079b1b28:      	ldp	w15, w16, [x29, #-32]
fffffe00079b1b2c:      	ldur	x8, [x29, #-64]
fffffe00079b1b30:      	stp	x0, x1, [sp, #80]
fffffe00079b1b34:      	ldur	w3, [x29, #-8]
fffffe00079b1b38:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x540>
fffffe00079b1b3c:      	add	x0, x0, #1913
fffffe00079b1b40:      	stp	x12, x13, [sp, #32]
fffffe00079b1b44:      	stp	x16, x17, [sp, #64]
fffffe00079b1b48:      	stp	x2, x3, [sp, #96]
fffffe00079b1b4c:      	stp	x14, x15, [sp, #48]
fffffe00079b1b50:      	stp	x10, x11, [sp, #16]
fffffe00079b1b54:      	stp	x8, x9, [sp]
fffffe00079b1b58:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b1b5c:      	ldp	x29, x30, [sp, #176]
fffffe00079b1b60:      	add	sp, sp, #192
fffffe00079b1b64:      	ret
