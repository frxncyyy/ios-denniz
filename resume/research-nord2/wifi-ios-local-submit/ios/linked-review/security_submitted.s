
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079afc90 <_nwp_security_submitted>:
fffffe00079afc90:      	mov	w8, wzr
fffffe00079afc94:      	cbz	x0, 0xfffffe00079afd20 <_nwp_security_submitted+0x90>
fffffe00079afc98:      	cbz	x2, 0xfffffe00079afd20 <_nwp_security_submitted+0x90>
fffffe00079afc9c:      	ldr	x8, [x0]
fffffe00079afca0:      	cmp	x8, x1
fffffe00079afca4:      	b.ne	0xfffffe00079afce4 <_nwp_security_submitted+0x54>
fffffe00079afca8:      	ldr	x8, [x0, #16]
fffffe00079afcac:      	cmp	x8, x2
fffffe00079afcb0:      	b.ne	0xfffffe00079afce4 <_nwp_security_submitted+0x54>
fffffe00079afcb4:      	ldr	w8, [x0, #36]
fffffe00079afcb8:      	str	xzr, [x0, #16]
fffffe00079afcbc:      	str	wzr, [x0, #36]
fffffe00079afcc0:      	cbz	w3, 0xfffffe00079afcec <_nwp_security_submitted+0x5c>
fffffe00079afcc4:      	and	w8, w8, #0xfffffffe
fffffe00079afcc8:      	cmp	w8, #2
fffffe00079afccc:      	b.ne	0xfffffe00079afce4 <_nwp_security_submitted+0x54>
fffffe00079afcd0:      	ldr	w8, [x0, #32]
fffffe00079afcd4:      	cmp	w8, #3
fffffe00079afcd8:      	b.ne	0xfffffe00079afce4 <_nwp_security_submitted+0x54>
fffffe00079afcdc:      	ldr	w8, [x0, #40]
fffffe00079afce0:      	cbz	w8, 0xfffffe00079afd08 <_nwp_security_submitted+0x78>
fffffe00079afce4:      	mov	w0, wzr
fffffe00079afce8:      	ret
fffffe00079afcec:      	mov	x8, xzr
fffffe00079afcf0:      	strb	wzr, [x0, x8]
fffffe00079afcf4:      	add	x8, x8, #1
fffffe00079afcf8:      	cmp	x8, #1432
fffffe00079afcfc:      	b.ne	0xfffffe00079afcf0 <_nwp_security_submitted+0x60>
fffffe00079afd00:      	mov	w0, #-1
fffffe00079afd04:      	ret
fffffe00079afd08:      	mov	w9, #1
fffffe00079afd0c:      	mov	w10, #4
fffffe00079afd10:      	mov	w8, #2
fffffe00079afd14:      	str	x2, [x0, #24]
fffffe00079afd18:      	str	w9, [x0, #40]
fffffe00079afd1c:      	str	w10, [x0, #32]
fffffe00079afd20:      	mov	w0, w8
fffffe00079afd24:      	ret
