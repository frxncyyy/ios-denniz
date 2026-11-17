
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4c70 <_nwpd_gate_observe>:
fffffe00079b4c70:      	sub	sp, sp, #64
fffffe00079b4c74:      	stp	x29, x30, [sp, #48]
fffffe00079b4c78:      	add	x29, sp, #48
fffffe00079b4c7c:      	adrp	x9, 0xfffffe0008530000 <_sendsig+0xc0>
fffffe00079b4c80:      	ldrb	w10, [x9]
fffffe00079b4c84:      	tbnz	w10, #0, 0xfffffe00079b4ce4 <_nwpd_gate_observe+0x74>
fffffe00079b4c88:      	mov	x8, x0
fffffe00079b4c8c:      	cbz	x0, 0xfffffe00079b4ce4 <_nwpd_gate_observe+0x74>
fffffe00079b4c90:      	cmp	w2, #5
fffffe00079b4c94:      	b.hi	0xfffffe00079b4ce4 <_nwpd_gate_observe+0x74>
fffffe00079b4c98:      	cmp	w3, #1
fffffe00079b4c9c:      	b.hi	0xfffffe00079b4ce4 <_nwpd_gate_observe+0x74>
fffffe00079b4ca0:      	mov	w10, #1
fffffe00079b4ca4:      	cbz	w3, 0xfffffe00079b4cbc <_nwpd_gate_observe+0x4c>
fffffe00079b4ca8:      	cbz	x4, 0xfffffe00079b4cc4 <_nwpd_gate_observe+0x54>
fffffe00079b4cac:      	cmp	x4, #641
fffffe00079b4cb0:      	b.lo	0xfffffe00079b4ce4 <_nwpd_gate_observe+0x74>
fffffe00079b4cb4:      	mov	w11, #3
fffffe00079b4cb8:      	b	0xfffffe00079b4cc8 <_nwpd_gate_observe+0x58>
fffffe00079b4cbc:      	mov	w11, #1
fffffe00079b4cc0:      	b	0xfffffe00079b4cc8 <_nwpd_gate_observe+0x58>
fffffe00079b4cc4:      	mov	w11, #2
fffffe00079b4cc8:      	adrp	x0, 0xfffffe0007134000 <_nwpc_unwrap+0xec>
fffffe00079b4ccc:      	add	x0, x0, #3245
fffffe00079b4cd0:      	strb	w10, [x9]
fffffe00079b4cd4:      	stp	x4, x11, [sp, #32]
fffffe00079b4cd8:      	stp	x2, x3, [sp, #16]
fffffe00079b4cdc:      	stp	x8, x1, [sp]
fffffe00079b4ce0:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4ce4:      	ldp	x29, x30, [sp, #48]
fffffe00079b4ce8:      	add	sp, sp, #64
fffffe00079b4cec:      	ret
