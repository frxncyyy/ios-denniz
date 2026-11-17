
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4c0c <_nwpd_gate_record>:
fffffe00079b4c0c:      	mov	w8, wzr
fffffe00079b4c10:      	cbz	x0, 0xfffffe00079b4c60 <_nwpd_gate_record+0x54>
fffffe00079b4c14:      	cbz	x1, 0xfffffe00079b4c60 <_nwpd_gate_record+0x54>
fffffe00079b4c18:      	cmp	w3, #5
fffffe00079b4c1c:      	b.hi	0xfffffe00079b4c60 <_nwpd_gate_record+0x54>
fffffe00079b4c20:      	cmp	w4, #1
fffffe00079b4c24:      	b.hi	0xfffffe00079b4c60 <_nwpd_gate_record+0x54>
fffffe00079b4c28:      	cbz	w4, 0xfffffe00079b4c40 <_nwpd_gate_record+0x34>
fffffe00079b4c2c:      	cbz	x5, 0xfffffe00079b4c48 <_nwpd_gate_record+0x3c>
fffffe00079b4c30:      	cmp	x5, #641
fffffe00079b4c34:      	b.lo	0xfffffe00079b4c68 <_nwpd_gate_record+0x5c>
fffffe00079b4c38:      	mov	w9, #3
fffffe00079b4c3c:      	b	0xfffffe00079b4c4c <_nwpd_gate_record+0x40>
fffffe00079b4c40:      	mov	w9, #1
fffffe00079b4c44:      	b	0xfffffe00079b4c4c <_nwpd_gate_record+0x40>
fffffe00079b4c48:      	mov	w9, #2
fffffe00079b4c4c:      	mov	w8, #1
fffffe00079b4c50:      	stp	x1, x2, [x0]
fffffe00079b4c54:      	str	x5, [x0, #16]
fffffe00079b4c58:      	stp	w3, w4, [x0, #24]
fffffe00079b4c5c:      	str	w9, [x0, #32]
fffffe00079b4c60:      	mov	w0, w8
fffffe00079b4c64:      	ret
fffffe00079b4c68:      	mov	w0, wzr
fffffe00079b4c6c:      	ret
