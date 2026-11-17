
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079afb34 <_nsdd_decode_txs>:
fffffe00079afb34:      	cbz	x0, 0xfffffe00079afb98 <_nsdd_decode_txs+0x64>
fffffe00079afb38:      	movi.2d	v0, #0000000000000000
fffffe00079afb3c:      	mov	w8, #-1
fffffe00079afb40:      	stur	q0, [x0, #28]
fffffe00079afb44:      	stp	q0, q0, [x0]
fffffe00079afb48:      	cbz	x1, 0xfffffe00079afbcc <_nsdd_decode_txs+0x98>
fffffe00079afb4c:      	mov	x9, #-4097
fffffe00079afb50:      	add	x9, x2, x9
fffffe00079afb54:      	cmn	x9, #4009
fffffe00079afb58:      	b.lo	0xfffffe00079afbcc <_nsdd_decode_txs+0x98>
fffffe00079afb5c:      	str	w2, [x0]
fffffe00079afb60:      	ldrb	w8, [x1]
fffffe00079afb64:      	str	w8, [x0, #4]
fffffe00079afb68:      	ldrb	w8, [x1, #1]
fffffe00079afb6c:      	str	w8, [x0, #12]
fffffe00079afb70:      	ldrb	w8, [x1, #4]
fffffe00079afb74:      	str	w8, [x0, #8]
fffffe00079afb78:      	ldrb	w8, [x1, #8]
fffffe00079afb7c:      	str	w8, [x0, #28]
fffffe00079afb80:      	ldrb	w8, [x1, #8]
fffffe00079afb84:      	and	w9, w8, #0x1
fffffe00079afb88:      	str	w9, [x0, #32]
fffffe00079afb8c:      	tbnz	w8, #0, 0xfffffe00079afba0 <_nsdd_decode_txs+0x6c>
fffffe00079afb90:      	mov	w0, wzr
fffffe00079afb94:      	ret
fffffe00079afb98:      	mov	w0, #-1
fffffe00079afb9c:      	ret
fffffe00079afba0:      	ldrh	w8, [x1, #2]
fffffe00079afba4:      	str	w8, [x0, #16]
fffffe00079afba8:      	ldrb	w8, [x1, #5]
fffffe00079afbac:      	str	w8, [x0, #20]
fffffe00079afbb0:      	ldrh	w8, [x1, #6]
fffffe00079afbb4:      	str	w8, [x0, #24]
fffffe00079afbb8:      	ldrb	w8, [x1, #9]
fffffe00079afbbc:      	str	w8, [x0, #36]
fffffe00079afbc0:      	mov	w8, wzr
fffffe00079afbc4:      	ldrb	w9, [x1, #14]
fffffe00079afbc8:      	str	w9, [x0, #40]
fffffe00079afbcc:      	mov	w0, w8
fffffe00079afbd0:      	ret
