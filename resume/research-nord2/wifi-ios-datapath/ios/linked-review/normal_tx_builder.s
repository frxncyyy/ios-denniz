
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aad5c <_nstx_build>:
fffffe00079aad5c:      	stp	x28, x27, [sp, #-96]!
fffffe00079aad60:      	stp	x26, x25, [sp, #16]
fffffe00079aad64:      	stp	x24, x23, [sp, #32]
fffffe00079aad68:      	stp	x22, x21, [sp, #48]
fffffe00079aad6c:      	stp	x20, x19, [sp, #64]
fffffe00079aad70:      	stp	x29, x30, [sp, #80]
fffffe00079aad74:      	add	x29, sp, #80
fffffe00079aad78:      	sub	sp, sp, #1616
fffffe00079aad7c:      	adrp	x8, 0xfffffe0007c98000 <_nst_prepare+0x1b0>
fffffe00079aad80:      	mov	w24, w7
fffffe00079aad84:      	mov	w26, w6
fffffe00079aad88:      	mov	x19, x2
fffffe00079aad8c:      	mov	x20, x5
fffffe00079aad90:      	mov	x23, x4
fffffe00079aad94:      	nop
fffffe00079aad98:      	mov	x25, x3
fffffe00079aad9c:      	mov	x22, x1
fffffe00079aada0:      	mov	x21, x0
fffffe00079aada4:      	ldr	x8, [x8, #3440]
fffffe00079aada8:      	stur	x8, [x29, #-88]
fffffe00079aadac:      	cbz	x2, 0xfffffe00079aadb4 <_nstx_build+0x58>
fffffe00079aadb0:      	str	xzr, [x19]
fffffe00079aadb4:      	mov	w0, #-1
fffffe00079aadb8:      	cmp	w26, #1
fffffe00079aadbc:      	b.hi	0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadc0:      	cbz	x19, 0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadc4:      	cbz	x21, 0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadc8:      	cmp	x22, #1, lsl #12
fffffe00079aadcc:      	b.ne	0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadd0:      	cbz	x25, 0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadd4:      	cbz	x23, 0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadd8:      	cmp	w24, #2047
fffffe00079aaddc:      	b.hi	0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aade0:      	ldr	x27, [x29, #16]
fffffe00079aade4:      	mov	w8, #-4097
fffffe00079aade8:      	cmp	x27, x8
fffffe00079aadec:      	b.hi	0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadf0:      	and	x8, x27, #0xfff
fffffe00079aadf4:      	cbnz	x8, 0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aadf8:      	add	x0, sp, #8
fffffe00079aadfc:      	mov	w1, #1600
fffffe00079aae00:      	bl	0xfffffe0007bad720 <_bzero>
fffffe00079aae04:      	add	x0, sp, #8
fffffe00079aae08:      	mov	x2, sp
fffffe00079aae0c:      	mov	w1, #32
fffffe00079aae10:      	mov	x3, x25
fffffe00079aae14:      	mov	x4, x23
fffffe00079aae18:      	mov	x5, x20
fffffe00079aae1c:      	str	xzr, [sp]
fffffe00079aae20:      	cbz	w26, 0xfffffe00079aae2c <_nstx_build+0xd0>
fffffe00079aae24:      	bl	0xfffffe00079aad4c <_sw_security_txd>
fffffe00079aae28:      	b	0xfffffe00079aae30 <_nstx_build+0xd4>
fffffe00079aae2c:      	bl	0xfffffe00079aad54 <_sw_data_txd>
fffffe00079aae30:      	mov	w8, w0
fffffe00079aae34:      	mov	w0, #-1
fffffe00079aae38:      	cmp	x20, #1536
fffffe00079aae3c:      	b.hi	0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aae40:      	cbnz	w8, 0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aae44:      	ldr	x8, [sp]
fffffe00079aae48:      	cmp	x8, #32
fffffe00079aae4c:      	b.ne	0xfffffe00079aaf88 <_nstx_build+0x22c>
fffffe00079aae50:      	ldrb	w8, [sp, #10]
fffffe00079aae54:      	mov	w10, #2
fffffe00079aae58:      	ldrb	w9, [sp, #13]
fffffe00079aae5c:      	ldrh	w11, [x23, #12]
fffffe00079aae60:      	and	w12, w8, #0x7f
fffffe00079aae64:      	strb	w10, [sp, #11]
fffffe00079aae68:      	orr	w9, w9, #0x4
fffffe00079aae6c:      	rev16	w8, w11
fffffe00079aae70:      	cmp	w8, #2054
fffffe00079aae74:      	strb	w12, [sp, #10]
fffffe00079aae78:      	strb	w9, [sp, #13]
fffffe00079aae7c:      	b.eq	0xfffffe00079aae8c <_nstx_build+0x130>
fffffe00079aae80:      	mov	w9, #34958
fffffe00079aae84:      	cmp	w8, w9
fffffe00079aae88:      	b.ne	0xfffffe00079aae9c <_nstx_build+0x140>
fffffe00079aae8c:      	ldrb	w8, [sp, #23]
fffffe00079aae90:      	orr	w8, w8, #0x10
fffffe00079aae94:      	strb	w8, [sp, #23]
fffffe00079aae98:      	b	0xfffffe00079aaf08 <_nstx_build+0x1ac>
fffffe00079aae9c:      	cmp	x20, #34
fffffe00079aaea0:      	b.lo	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaea4:      	cmp	w8, #2048
fffffe00079aaea8:      	b.ne	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaeac:      	mov	x8, x23
fffffe00079aaeb0:      	ldrb	w9, [x8, #14]!
fffffe00079aaeb4:      	and	w10, w9, #0xf0
fffffe00079aaeb8:      	cmp	w10, #64
fffffe00079aaebc:      	b.ne	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaec0:      	lsl	w9, w9, #2
fffffe00079aaec4:      	and	x9, x9, #0x3c
fffffe00079aaec8:      	cmp	w9, #20
fffffe00079aaecc:      	b.lo	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaed0:      	sub	x10, x20, #14
fffffe00079aaed4:      	cmp	x10, x9
fffffe00079aaed8:      	b.lo	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaedc:      	ldrb	w11, [x23, #23]
fffffe00079aaee0:      	cmp	w11, #17
fffffe00079aaee4:      	b.ne	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaee8:      	ldrb	w11, [x23, #20]
fffffe00079aaeec:      	ldrb	w12, [x23, #21]
fffffe00079aaef0:      	bfi	w12, w11, #8, #6
fffffe00079aaef4:      	cbz	w12, 0xfffffe00079aafc0 <_nstx_build+0x264>
fffffe00079aaef8:      	ldrb	w8, [sp, #19]
fffffe00079aaefc:      	str	wzr, [sp, #32]
fffffe00079aaf00:      	and	w8, w8, #0x3f
fffffe00079aaf04:      	strb	w8, [sp, #19]
fffffe00079aaf08:      	lsr	w8, w24, #8
fffffe00079aaf0c:      	add	w9, w27, #64
fffffe00079aaf10:      	orr	w8, w8, #0x80
fffffe00079aaf14:      	lsr	w10, w9, #8
fffffe00079aaf18:      	mov	x1, x23
fffffe00079aaf1c:      	mov	x2, x20
fffffe00079aaf20:      	strb	w9, [sp, #48]
fffffe00079aaf24:      	mov	w3, #1536
fffffe00079aaf28:      	strb	w8, [sp, #41]
fffffe00079aaf2c:      	lsr	w8, w9, #16
fffffe00079aaf30:      	strb	w10, [sp, #49]
fffffe00079aaf34:      	add	x10, sp, #8
fffffe00079aaf38:      	lsr	w9, w9, #24
fffffe00079aaf3c:      	add	x0, x10, #64
fffffe00079aaf40:      	strb	w8, [sp, #50]
fffffe00079aaf44:      	ubfx	x8, x20, #8, #24
fffffe00079aaf48:      	orr	w8, w8, #0x80
fffffe00079aaf4c:      	strb	w24, [sp, #40]
fffffe00079aaf50:      	strb	w9, [sp, #51]
fffffe00079aaf54:      	strb	w20, [sp, #52]
fffffe00079aaf58:      	strb	w8, [sp, #53]
fffffe00079aaf5c:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079aaf60:      	mov	x0, x21
fffffe00079aaf64:      	mov	x1, x22
fffffe00079aaf68:      	bl	0xfffffe0007bad720 <_bzero>
fffffe00079aaf6c:      	add	x20, x20, #64
fffffe00079aaf70:      	add	x1, sp, #8
fffffe00079aaf74:      	mov	x0, x21
fffffe00079aaf78:      	mov	x2, x20
fffffe00079aaf7c:      	bl	0xfffffe0007bad570 <_memmove>
fffffe00079aaf80:      	mov	w0, wzr
fffffe00079aaf84:      	str	x20, [x19]
fffffe00079aaf88:      	adrp	x8, 0xfffffe0007c98000 <_nst_prepare+0x3bc>
fffffe00079aaf8c:      	nop
fffffe00079aaf90:      	ldur	x9, [x29, #-88]
fffffe00079aaf94:      	ldr	x8, [x8, #3440]
fffffe00079aaf98:      	cmp	x8, x9
fffffe00079aaf9c:      	b.ne	0xfffffe00079ab044 <_nstx_build+0x2e8>
fffffe00079aafa0:      	add	sp, sp, #1616
fffffe00079aafa4:      	ldp	x29, x30, [sp, #80]
fffffe00079aafa8:      	ldp	x20, x19, [sp, #64]
fffffe00079aafac:      	ldp	x22, x21, [sp, #48]
fffffe00079aafb0:      	ldp	x24, x23, [sp, #32]
fffffe00079aafb4:      	ldp	x26, x25, [sp, #16]
fffffe00079aafb8:      	ldp	x28, x27, [sp], #96
fffffe00079aafbc:      	ret
fffffe00079aafc0:      	ldrh	w11, [x23, #16]
fffffe00079aafc4:      	add	w12, w9, #8
fffffe00079aafc8:      	rev16	w11, w11
fffffe00079aafcc:      	cmp	w11, w12
fffffe00079aafd0:      	b.lo	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aafd4:      	cmp	x10, x11
fffffe00079aafd8:      	b.lo	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aafdc:      	add	x8, x8, x9
fffffe00079aafe0:      	ldrh	w10, [x8, #4]
fffffe00079aafe4:      	rev16	w10, w10
fffffe00079aafe8:      	cmp	w10, #248
fffffe00079aafec:      	b.lo	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaff0:      	sub	w9, w11, w9
fffffe00079aaff4:      	cmp	w10, w9
fffffe00079aaff8:      	b.hi	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079aaffc:      	ldrh	w9, [x8, #2]
fffffe00079ab000:      	rev16	w9, w9
fffffe00079ab004:      	sub	w9, w9, #69
fffffe00079ab008:      	cmn	w9, #2
fffffe00079ab00c:      	b.lo	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079ab010:      	ldrb	w9, [x8, #244]
fffffe00079ab014:      	cmp	w9, #99
fffffe00079ab018:      	b.ne	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079ab01c:      	ldrb	w9, [x8, #245]
fffffe00079ab020:      	cmp	w9, #130
fffffe00079ab024:      	b.ne	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079ab028:      	ldrb	w9, [x8, #246]
fffffe00079ab02c:      	cmp	w9, #83
fffffe00079ab030:      	b.ne	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079ab034:      	ldrb	w8, [x8, #247]
fffffe00079ab038:      	cmp	w8, #99
fffffe00079ab03c:      	b.eq	0xfffffe00079aae8c <_nstx_build+0x130>
fffffe00079ab040:      	b	0xfffffe00079aaef8 <_nstx_build+0x19c>
fffffe00079ab044:      	bl	0xfffffe0007a06354 <___stack_chk_fail>
