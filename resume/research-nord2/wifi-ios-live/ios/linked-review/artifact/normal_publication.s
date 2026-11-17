
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079afb14 <_data_transmit>:
fffffe00079afb14:      	sub	sp, sp, #224
fffffe00079afb18:      	stp	x28, x27, [sp, #128]
fffffe00079afb1c:      	stp	x26, x25, [sp, #144]
fffffe00079afb20:      	stp	x24, x23, [sp, #160]
fffffe00079afb24:      	stp	x22, x21, [sp, #176]
fffffe00079afb28:      	stp	x20, x19, [sp, #192]
fffffe00079afb2c:      	stp	x29, x30, [sp, #208]
fffffe00079afb30:      	add	x29, sp, #208
fffffe00079afb34:      	mov	w8, #10276
fffffe00079afb38:      	ldr	x9, [x0, #10256]
fffffe00079afb3c:      	add	x28, x0, x8
fffffe00079afb40:      	ldr	w8, [x0, #10264]
fffffe00079afb44:      	mov	x20, x4
fffffe00079afb48:      	mov	x21, x3
fffffe00079afb4c:      	mov	x19, x0
fffffe00079afb50:      	mov	x22, x2
fffffe00079afb54:      	lsl	w8, w8, #4
fffffe00079afb58:      	mov	x23, x1
fffffe00079afb5c:      	ldr	w10, [x0, #136]
fffffe00079afb60:      	add	x24, x9, x8
fffffe00079afb64:      	stur	wzr, [x29, #-84]
fffffe00079afb68:      	cbz	w10, 0xfffffe00079afbe8 <_data_transmit+0xd4>
fffffe00079afb6c:      	ldr	w8, [x19, #10280]
fffffe00079afb70:      	cbz	w8, 0xfffffe00079b0274 <_data_transmit+0x760>
fffffe00079afb74:      	ldr	w8, [x19, #10284]
fffffe00079afb78:      	cbz	w8, 0xfffffe00079b02f4 <_data_transmit+0x7e0>
fffffe00079afb7c:      	mov	w9, #10340
fffffe00079afb80:      	mov	x8, xzr
fffffe00079afb84:      	add	x24, x19, x9
fffffe00079afb88:      	strb	wzr, [x24, x8]
fffffe00079afb8c:      	add	x8, x8, #1
fffffe00079afb90:      	cmp	x8, #1, lsl #12
fffffe00079afb94:      	b.ne	0xfffffe00079afb88 <_data_transmit+0x74>
fffffe00079afb98:      	mov	w8, #1769
fffffe00079afb9c:      	mov	w9, #5865
fffffe00079afba0:      	strb	wzr, [x19, x8]
fffffe00079afba4:      	add	x8, x8, #1
fffffe00079afba8:      	cmp	x8, x9
fffffe00079afbac:      	b.ne	0xfffffe00079afba0 <_data_transmit+0x8c>
fffffe00079afbb0:      	mov	w8, #15
fffffe00079afbb4:      	mov	x0, x19
fffffe00079afbb8:      	mov	x1, x23
fffffe00079afbbc:      	mov	x2, x22
fffffe00079afbc0:      	ldr	x25, [x19, #10248]
fffffe00079afbc4:      	str	w8, [x19, #40]
fffffe00079afbc8:      	bl	0xfffffe00079aeab8 <_before>
fffffe00079afbcc:      	cbz	w0, 0xfffffe00079afc48 <_data_transmit+0x134>
fffffe00079afbd0:      	mov	w1, #8
fffffe00079afbd4:      	mov	x0, x19
fffffe00079afbd8:      	bl	0xfffffe00079b1780 <_fail>
fffffe00079afbdc:      	mov	x0, x19
fffffe00079afbe0:      	mov	w1, #46
fffffe00079afbe4:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079afbe8:      	mov	w8, #10248
fffffe00079afbec:      	add	x25, x19, x8
fffffe00079afbf0:      	mov	x0, x25
fffffe00079afbf4:      	bl	0xfffffe00079acb40 <_nstx_idle>
fffffe00079afbf8:      	cbz	w0, 0xfffffe00079afc3c <_data_transmit+0x128>
fffffe00079afbfc:      	mov	x0, x19
fffffe00079afc00:      	mov	x1, x23
fffffe00079afc04:      	mov	x2, x22
fffffe00079afc08:      	bl	0xfffffe00079aeab8 <_before>
fffffe00079afc0c:      	cbnz	w0, 0xfffffe00079afc30 <_data_transmit+0x11c>
fffffe00079afc10:      	ldr	x8, [x23]
fffffe00079afc14:      	mov	w1, #240
fffffe00079afc18:      	sub	x2, x29, #84
fffffe00079afc1c:      	mov	x0, x22
fffffe00079afc20:      	movk	w1, #6150, lsl #16
fffffe00079afc24:      	ldr	x8, [x8, #8]
fffffe00079afc28:      	blr	x8
fffffe00079afc2c:      	cbz	w0, 0xfffffe00079afca8 <_data_transmit+0x194>
fffffe00079afc30:      	mov	x0, x19
fffffe00079afc34:      	mov	w1, #5
fffffe00079afc38:      	bl	0xfffffe00079b1780 <_fail>
fffffe00079afc3c:      	mov	x0, x19
fffffe00079afc40:      	mov	w1, #40
fffffe00079afc44:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079afc48:      	mov	x0, x19
fffffe00079afc4c:      	mov	x1, x25
fffffe00079afc50:      	mov	w2, #4096
fffffe00079afc54:      	mov	w3, #1
fffffe00079afc58:      	mov	w26, #1
fffffe00079afc5c:      	bl	0xfffffe00079ae004 <_nst_memory_range>
fffffe00079afc60:      	cbz	w0, 0xfffffe00079afbd0 <_data_transmit+0xbc>
fffffe00079afc64:      	str	w26, [x19, #52]
fffffe00079afc68:      	mov	x0, x22
fffffe00079afc6c:      	ldr	x8, [x23]
fffffe00079afc70:      	mov	x1, x25
fffffe00079afc74:      	mov	x2, x24
fffffe00079afc78:      	mov	w3, #4096
fffffe00079afc7c:      	ldr	x8, [x8, #32]
fffffe00079afc80:      	blr	x8
fffffe00079afc84:      	cbnz	w0, 0xfffffe00079afbd0 <_data_transmit+0xbc>
fffffe00079afc88:      	mov	x0, x19
fffffe00079afc8c:      	mov	x1, x23
fffffe00079afc90:      	mov	x2, x22
fffffe00079afc94:      	ldr	x25, [x19, #10248]
fffffe00079afc98:      	bl	0xfffffe00079aeab8 <_before>
fffffe00079afc9c:      	cbz	w0, 0xfffffe00079afd94 <_data_transmit+0x280>
fffffe00079afca0:      	mov	w1, #9
fffffe00079afca4:      	b	0xfffffe00079afbd4 <_data_transmit+0xc0>
fffffe00079afca8:      	ldur	w8, [x29, #-84]
fffffe00079afcac:      	mov	w9, #3
fffffe00079afcb0:      	bics	wzr, w9, w8
fffffe00079afcb4:      	b.ne	0xfffffe00079afc3c <_data_transmit+0x128>
fffffe00079afcb8:      	mov	x0, x19
fffffe00079afcbc:      	mov	x1, x23
fffffe00079afcc0:      	mov	x2, x22
fffffe00079afcc4:      	bl	0xfffffe00079aebd0 <_coherent>
fffffe00079afcc8:      	cbnz	w0, 0xfffffe00079afc3c <_data_transmit+0x128>
fffffe00079afccc:      	mov	w3, #21256
fffffe00079afcd0:      	sub	x4, x29, #84
fffffe00079afcd4:      	mov	x0, x19
fffffe00079afcd8:      	mov	x1, x23
fffffe00079afcdc:      	mov	x2, x22
fffffe00079afce0:      	movk	w3, #6146, lsl #16
fffffe00079afce4:      	mov	w26, #21256
fffffe00079afce8:      	movk	w26, #6146, lsl #16
fffffe00079afcec:      	bl	0xfffffe00079b1878 <_rr>
fffffe00079afcf0:      	cbnz	w0, 0xfffffe00079afc3c <_data_transmit+0x128>
fffffe00079afcf4:      	ldur	w8, [x29, #-84]
fffffe00079afcf8:      	ldr	w9, [x19, #10264]
fffffe00079afcfc:      	cmp	w8, w9
fffffe00079afd00:      	b.ne	0xfffffe00079afc3c <_data_transmit+0x128>
fffffe00079afd04:      	add	w3, w26, #4
fffffe00079afd08:      	sub	x4, x29, #84
fffffe00079afd0c:      	mov	x0, x19
fffffe00079afd10:      	mov	x1, x23
fffffe00079afd14:      	mov	x2, x22
fffffe00079afd18:      	bl	0xfffffe00079b1878 <_rr>
fffffe00079afd1c:      	cbnz	w0, 0xfffffe00079afc3c <_data_transmit+0x128>
fffffe00079afd20:      	ldur	w8, [x29, #-84]
fffffe00079afd24:      	ldr	w9, [x19, #10264]
fffffe00079afd28:      	cmp	w8, w9
fffffe00079afd2c:      	b.ne	0xfffffe00079afc3c <_data_transmit+0x128>
fffffe00079afd30:      	mov	w8, #10
fffffe00079afd34:      	mov	x0, x19
fffffe00079afd38:      	mov	x1, x23
fffffe00079afd3c:      	mov	x2, x22
fffffe00079afd40:      	mov	x3, x24
fffffe00079afd44:      	mov	w4, #16
fffffe00079afd48:      	mov	w5, #2
fffffe00079afd4c:      	str	w8, [x19, #40]
fffffe00079afd50:      	bl	0xfffffe00079b18e8 <_sy>
fffffe00079afd54:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079afd58:      	mov	w8, #10324
fffffe00079afd5c:      	mov	x0, x19
fffffe00079afd60:      	add	x27, x19, x8
fffffe00079afd64:      	mov	x1, x23
fffffe00079afd68:      	mov	x2, x22
fffffe00079afd6c:      	mov	x3, x24
fffffe00079afd70:      	mov	x4, x27
fffffe00079afd74:      	mov	w5, #16
fffffe00079afd78:      	bl	0xfffffe00079b1980 <_mr>
fffffe00079afd7c:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079afd80:      	ldrsb	w8, [x28, #55]
fffffe00079afd84:      	tbnz	w8, #31, 0xfffffe00079aff1c <_data_transmit+0x408>
fffffe00079afd88:      	mov	x0, x19
fffffe00079afd8c:      	mov	w1, #41
fffffe00079afd90:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079afd94:      	mov	x0, x19
fffffe00079afd98:      	mov	x1, x25
fffffe00079afd9c:      	mov	w2, #4096
fffffe00079afda0:      	mov	w3, #1
fffffe00079afda4:      	bl	0xfffffe00079ae004 <_nst_memory_range>
fffffe00079afda8:      	cbz	w0, 0xfffffe00079afca0 <_data_transmit+0x18c>
fffffe00079afdac:      	ldr	x8, [x23]
fffffe00079afdb0:      	mov	x0, x22
fffffe00079afdb4:      	mov	x1, x25
fffffe00079afdb8:      	mov	w2, #4096
fffffe00079afdbc:      	mov	w3, #1
fffffe00079afdc0:      	ldr	x8, [x8, #40]
fffffe00079afdc4:      	blr	x8
fffffe00079afdc8:      	cbnz	w0, 0xfffffe00079afca0 <_data_transmit+0x18c>
fffffe00079afdcc:      	mov	x0, x19
fffffe00079afdd0:      	mov	x1, x23
fffffe00079afdd4:      	mov	x2, x22
fffffe00079afdd8:      	ldr	x25, [x19, #10248]
fffffe00079afddc:      	bl	0xfffffe00079aeab8 <_before>
fffffe00079afde0:      	cbnz	w0, 0xfffffe00079afca0 <_data_transmit+0x18c>
fffffe00079afde4:      	mov	x0, x19
fffffe00079afde8:      	mov	x1, x25
fffffe00079afdec:      	mov	w2, #4096
fffffe00079afdf0:      	mov	w3, wzr
fffffe00079afdf4:      	bl	0xfffffe00079ae004 <_nst_memory_range>
fffffe00079afdf8:      	cbz	w0, 0xfffffe00079afca0 <_data_transmit+0x18c>
fffffe00079afdfc:      	ldr	x8, [x23]
fffffe00079afe00:      	mov	x0, x22
fffffe00079afe04:      	mov	x1, x25
fffffe00079afe08:      	mov	w2, #4096
fffffe00079afe0c:      	mov	w3, #2
fffffe00079afe10:      	ldr	x8, [x8, #40]
fffffe00079afe14:      	blr	x8
fffffe00079afe18:      	cbnz	w0, 0xfffffe00079afca0 <_data_transmit+0x18c>
fffffe00079afe1c:      	mov	w8, #14440
fffffe00079afe20:      	ldr	x3, [x19, #10248]
fffffe00079afe24:      	add	x25, x19, x8
fffffe00079afe28:      	mov	x0, x19
fffffe00079afe2c:      	mov	x1, x23
fffffe00079afe30:      	mov	x2, x22
fffffe00079afe34:      	mov	x4, x25
fffffe00079afe38:      	mov	w5, #4096
fffffe00079afe3c:      	bl	0xfffffe00079b1980 <_mr>
fffffe00079afe40:      	cbnz	w0, 0xfffffe00079afbdc <_data_transmit+0xc8>
fffffe00079afe44:      	mov	x8, xzr
fffffe00079afe48:      	mov	w9, #4100
fffffe00079afe4c:      	cmp	x8, #1, lsl #12
fffffe00079afe50:      	b.eq	0xfffffe00079afe70 <_data_transmit+0x35c>
fffffe00079afe54:      	add	x10, x24, x8
fffffe00079afe58:      	add	x8, x8, #1
fffffe00079afe5c:      	ldrb	w11, [x10, x9]
fffffe00079afe60:      	ldrb	w10, [x10]
fffffe00079afe64:      	cmp	w10, w11
fffffe00079afe68:      	b.eq	0xfffffe00079afe4c <_data_transmit+0x338>
fffffe00079afe6c:      	b	0xfffffe00079afbdc <_data_transmit+0xc8>
fffffe00079afe70:      	mov	x8, xzr
fffffe00079afe74:      	strb	wzr, [x25, x8]
fffffe00079afe78:      	add	x8, x8, #1
fffffe00079afe7c:      	cmp	x8, #1, lsl #12
fffffe00079afe80:      	b.ne	0xfffffe00079afe74 <_data_transmit+0x360>
fffffe00079afe84:      	ldr	w8, [x19, #10264]
fffffe00079afe88:      	mov	w10, #1
fffffe00079afe8c:      	ldr	w9, [x19, #10296]
fffffe00079afe90:      	str	xzr, [x28]
fffffe00079afe94:      	ldr	x0, [x19, #160]
fffffe00079afe98:      	str	wzr, [x19, #10284]
fffffe00079afe9c:      	add	w8, w8, #1
fffffe00079afea0:      	ldr	w1, [x19, #124]
fffffe00079afea4:      	add	w9, w9, #1
fffffe00079afea8:      	and	w8, w8, #0x3ff
fffffe00079afeac:      	ldrb	w2, [x19, #215]
fffffe00079afeb0:      	str	w10, [x19, #140]
fffffe00079afeb4:      	str	wzr, [x19, #56]
fffffe00079afeb8:      	str	w9, [x19, #10296]
fffffe00079afebc:      	str	w8, [x19, #10264]
fffffe00079afec0:      	str	wzr, [x19, #40]
fffffe00079afec4:      	bl	0xfffffe00079b3670 <_nsdd_pid_local>
fffffe00079afec8:      	ldr	w8, [x19, #132]
fffffe00079afecc:      	mov	w9, #15
fffffe00079afed0:      	movi.2d	v0, #0000000000000000
fffffe00079afed4:      	ldr	x10, [x19, #168]
fffffe00079afed8:      	add	x3, sp, #16
fffffe00079afedc:      	mov	x0, x19
fffffe00079afee0:      	cmp	w8, #2
fffffe00079afee4:      	ldr	w8, [x19, #124]
fffffe00079afee8:      	cinc	w9, w9, ne
fffffe00079afeec:      	mov	x1, x21
fffffe00079afef0:      	stp	q0, q0, [sp, #16]
fffffe00079afef4:      	mov	x2, x20
fffffe00079afef8:      	strb	w8, [sp, #42]
fffffe00079afefc:      	ldrb	w8, [x19, #215]
fffffe00079aff00:      	str	x10, [sp, #32]
fffffe00079aff04:      	str	w9, [sp, #16]
fffffe00079aff08:      	str	xzr, [sp, #64]
fffffe00079aff0c:      	str	q0, [sp, #48]
fffffe00079aff10:      	strb	w8, [sp, #43]
fffffe00079aff14:      	bl	0xfffffe00079afaac <_emit>
fffffe00079aff18:      	b	0xfffffe00079b02c4 <_data_transmit+0x7b0>
fffffe00079aff1c:      	ldr	w8, [x19, #236]
fffffe00079aff20:      	mov	w10, #10340
fffffe00079aff24:      	ldrb	w9, [x19, #1781]
fffffe00079aff28:      	add	x0, x19, x10
fffffe00079aff2c:      	ldr	w10, [x19, #112]
fffffe00079aff30:      	add	x26, x19, #1801
fffffe00079aff34:      	str	w8, [sp, #96]
fffffe00079aff38:      	ldr	w8, [x19, #132]
fffffe00079aff3c:      	ldur	q0, [x19, #220]
fffffe00079aff40:      	ubfx	w9, w9, #1, #1
fffffe00079aff44:      	ldr	w11, [x19, #124]
fffffe00079aff48:      	sub	w5, w10, #32
fffffe00079aff4c:      	cmp	w8, #2
fffffe00079aff50:      	ldr	w7, [x19, #10268]
fffffe00079aff54:      	cset	w6, eq
fffffe00079aff58:      	ldr	x8, [x19, #10248]
fffffe00079aff5c:      	sub	x2, x29, #96
fffffe00079aff60:      	add	x3, sp, #80
fffffe00079aff64:      	mov	w1, #4096
fffffe00079aff68:      	mov	x4, x26
fffffe00079aff6c:      	str	q0, [sp, #80]
fffffe00079aff70:      	strb	w11, [sp, #83]
fffffe00079aff74:      	strb	w9, [sp, #96]
fffffe00079aff78:      	stur	xzr, [x29, #-96]
fffffe00079aff7c:      	str	w7, [x19, #10272]
fffffe00079aff80:      	stp	x8, x0, [sp]
fffffe00079aff84:      	bl	0xfffffe00079ac5cc <_nstx_build>
fffffe00079aff88:      	cbnz	w0, 0xfffffe00079affb4 <_data_transmit+0x4a0>
fffffe00079aff8c:      	ldr	w8, [x19, #112]
fffffe00079aff90:      	ldur	x9, [x29, #-96]
fffffe00079aff94:      	add	w8, w8, #32
fffffe00079aff98:      	cmp	x9, x8
fffffe00079aff9c:      	b.ne	0xfffffe00079affb4 <_data_transmit+0x4a0>
fffffe00079affa0:      	ldr	x1, [x25]
fffffe00079affa4:      	mov	x0, x27
fffffe00079affa8:      	mov	w2, #64
fffffe00079affac:      	bl	0xfffffe000799689c <_nwt_tx_desc>
fffffe00079affb0:      	cbz	w0, 0xfffffe00079affc0 <_data_transmit+0x4ac>
fffffe00079affb4:      	mov	x0, x19
fffffe00079affb8:      	mov	w1, #42
fffffe00079affbc:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079affc0:      	mov	w8, #11
fffffe00079affc4:      	ldr	x3, [x19, #10248]
fffffe00079affc8:      	mov	x0, x19
fffffe00079affcc:      	mov	x1, x23
fffffe00079affd0:      	mov	x2, x22
fffffe00079affd4:      	ldr	x4, [sp, #8]
fffffe00079affd8:      	mov	w5, #4096
fffffe00079affdc:      	str	w8, [x19, #40]
fffffe00079affe0:      	bl	0xfffffe00079b1a14 <_mw>
fffffe00079affe4:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079affe8:      	ldr	x3, [x19, #10248]
fffffe00079affec:      	mov	x0, x19
fffffe00079afff0:      	mov	x1, x23
fffffe00079afff4:      	mov	x2, x22
fffffe00079afff8:      	mov	w4, #4096
fffffe00079afffc:      	mov	w5, #1
fffffe00079b0000:      	bl	0xfffffe00079b18e8 <_sy>
fffffe00079b0004:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079b0008:      	mov	w8, #12
fffffe00079b000c:      	ldr	x3, [x19, #10248]
fffffe00079b0010:      	mov	x0, x19
fffffe00079b0014:      	mov	x1, x23
fffffe00079b0018:      	mov	x2, x22
fffffe00079b001c:      	mov	w4, #4096
fffffe00079b0020:      	mov	w5, #2
fffffe00079b0024:      	str	w8, [x19, #40]
fffffe00079b0028:      	bl	0xfffffe00079b18e8 <_sy>
fffffe00079b002c:      	cbnz	w0, 0xfffffe00079b0084 <_data_transmit+0x570>
fffffe00079b0030:      	mov	w8, #14440
fffffe00079b0034:      	ldr	x3, [x19, #10248]
fffffe00079b0038:      	add	x25, x19, x8
fffffe00079b003c:      	mov	x0, x19
fffffe00079b0040:      	mov	x1, x23
fffffe00079b0044:      	mov	x2, x22
fffffe00079b0048:      	mov	x4, x25
fffffe00079b004c:      	mov	w5, #4096
fffffe00079b0050:      	bl	0xfffffe00079b1980 <_mr>
fffffe00079b0054:      	cbnz	w0, 0xfffffe00079b0084 <_data_transmit+0x570>
fffffe00079b0058:      	mov	x8, xzr
fffffe00079b005c:      	mov	w9, #4100
fffffe00079b0060:      	cmp	x8, #1, lsl #12
fffffe00079b0064:      	b.eq	0xfffffe00079b0090 <_data_transmit+0x57c>
fffffe00079b0068:      	ldr	x10, [sp, #8]
fffffe00079b006c:      	add	x10, x10, x8
fffffe00079b0070:      	add	x8, x8, #1
fffffe00079b0074:      	ldrb	w11, [x10, x9]
fffffe00079b0078:      	ldrb	w10, [x10]
fffffe00079b007c:      	cmp	w10, w11
fffffe00079b0080:      	b.eq	0xfffffe00079b0060 <_data_transmit+0x54c>
fffffe00079b0084:      	mov	x0, x19
fffffe00079b0088:      	mov	w1, #43
fffffe00079b008c:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079b0090:      	mov	x8, xzr
fffffe00079b0094:      	strb	wzr, [x25, x8]
fffffe00079b0098:      	add	x8, x8, #1
fffffe00079b009c:      	cmp	x8, #1, lsl #12
fffffe00079b00a0:      	b.ne	0xfffffe00079b0094 <_data_transmit+0x580>
fffffe00079b00a4:      	mov	w8, #11
fffffe00079b00a8:      	mov	x0, x19
fffffe00079b00ac:      	mov	x1, x23
fffffe00079b00b0:      	mov	x2, x22
fffffe00079b00b4:      	mov	x3, x24
fffffe00079b00b8:      	mov	x4, x27
fffffe00079b00bc:      	mov	w5, #16
fffffe00079b00c0:      	str	w8, [x19, #40]
fffffe00079b00c4:      	bl	0xfffffe00079b1a14 <_mw>
fffffe00079b00c8:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079b00cc:      	mov	x0, x19
fffffe00079b00d0:      	mov	x1, x23
fffffe00079b00d4:      	mov	x2, x22
fffffe00079b00d8:      	mov	x3, x24
fffffe00079b00dc:      	mov	w4, #16
fffffe00079b00e0:      	mov	w5, #1
fffffe00079b00e4:      	bl	0xfffffe00079b18e8 <_sy>
fffffe00079b00e8:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079b00ec:      	ldr	x8, [x19, #176]
fffffe00079b00f0:      	mov	x9, #-38528
fffffe00079b00f4:      	movk	x9, #65383, lsl #16
fffffe00079b00f8:      	cmp	x8, x9
fffffe00079b00fc:      	b.lo	0xfffffe00079b010c <_data_transmit+0x5f8>
fffffe00079b0100:      	mov	x0, x19
fffffe00079b0104:      	mov	w1, #44
fffffe00079b0108:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079b010c:      	add	x8, x8, #2441, lsl #12
fffffe00079b0110:      	mov	w10, #13
fffffe00079b0114:      	add	x9, x8, #1664
fffffe00079b0118:      	ldr	w8, [x19, #10308]
fffffe00079b011c:      	str	w10, [x19, #40]
fffffe00079b0120:      	str	x9, [x19, #192]
fffffe00079b0124:      	cbz	w8, 0xfffffe00079b0130 <_data_transmit+0x61c>
fffffe00079b0128:      	ldr	w9, [x19, #10312]
fffffe00079b012c:      	cbz	w9, 0xfffffe00079b013c <_data_transmit+0x628>
fffffe00079b0130:      	mov	x0, x19
fffffe00079b0134:      	mov	w1, #47
fffffe00079b0138:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079b013c:      	ldr	w9, [x19, #10316]
fffffe00079b0140:      	sub	w8, w8, #1
fffffe00079b0144:      	mov	w25, #1
fffffe00079b0148:      	ldr	x0, [x19, #160]
fffffe00079b014c:      	ldr	w1, [x19, #124]
fffffe00079b0150:      	add	w9, w9, #1
fffffe00079b0154:      	ldrb	w2, [x19, #215]
fffffe00079b0158:      	str	w8, [x19, #10308]
fffffe00079b015c:      	str	w25, [x19, #10312]
fffffe00079b0160:      	str	w9, [x19, #10316]
fffffe00079b0164:      	bl	0xfffffe00079b350c <_nsdd_pid_attempt>
fffffe00079b0168:      	ldr	w8, [x19, #10264]
fffffe00079b016c:      	mov	w3, #21256
fffffe00079b0170:      	mov	x0, x19
fffffe00079b0174:      	mov	x1, x23
fffffe00079b0178:      	mov	x2, x22
fffffe00079b017c:      	movk	w3, #6146, lsl #16
fffffe00079b0180:      	add	w8, w8, #1
fffffe00079b0184:      	and	w4, w8, #0x3ff
fffffe00079b0188:      	bl	0xfffffe00079b1ab8 <_rw>
fffffe00079b018c:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079b0190:      	ldr	w8, [x19, #10308]
fffffe00079b0194:      	str	w25, [x19, #10276]
fffffe00079b0198:      	ldr	w9, [x19, #10320]
fffffe00079b019c:      	str	w25, [x19, #136]
fffffe00079b01a0:      	ldr	w10, [x19, #10288]
fffffe00079b01a4:      	str	wzr, [x19, #10312]
fffffe00079b01a8:      	add	w8, w8, #1
fffffe00079b01ac:      	ldr	w11, [x19, #10268]
fffffe00079b01b0:      	ldr	x0, [x19, #160]
fffffe00079b01b4:      	ldr	w1, [x19, #124]
fffffe00079b01b8:      	str	w8, [x19, #10308]
fffffe00079b01bc:      	add	w8, w9, #1
fffffe00079b01c0:      	add	w9, w10, #1
fffffe00079b01c4:      	add	w10, w11, #1
fffffe00079b01c8:      	and	w10, w10, #0x7ff
fffffe00079b01cc:      	ldrb	w2, [x19, #215]
fffffe00079b01d0:      	str	w8, [x19, #10320]
fffffe00079b01d4:      	ldr	w8, [x19, #92]
fffffe00079b01d8:      	str	w9, [x19, #10288]
fffffe00079b01dc:      	str	w10, [x19, #10268]
fffffe00079b01e0:      	add	w3, w8, #1
fffffe00079b01e4:      	str	w3, [x19, #92]
fffffe00079b01e8:      	bl	0xfffffe00079b3584 <_nsdd_pid_publish>
fffffe00079b01ec:      	ldr	w8, [x19, #112]
fffffe00079b01f0:      	mov	w9, #9784
fffffe00079b01f4:      	ldr	x1, [x19, #160]
fffffe00079b01f8:      	add	x0, x19, x9
fffffe00079b01fc:      	ldr	w2, [x19, #92]
fffffe00079b0200:      	mov	x4, x26
fffffe00079b0204:      	ldr	w3, [x19, #132]
fffffe00079b0208:      	sub	w5, w8, #32
fffffe00079b020c:      	bl	0xfffffe00079b3cd4 <_nsdd_publish>
fffffe00079b0210:      	ldr	x25, [sp, #8]
fffffe00079b0214:      	add	x0, sp, #16
fffffe00079b0218:      	ldp	x1, x2, [x19, #160]
fffffe00079b021c:      	movi.2d	v0, #0000000000000000
fffffe00079b0220:      	mov	w6, wzr
fffffe00079b0224:      	ldr	w4, [x19, #112]
fffffe00079b0228:      	mov	x3, x25
fffffe00079b022c:      	ldr	w5, [x19, #132]
fffffe00079b0230:      	str	xzr, [sp, #64]
fffffe00079b0234:      	stp	q0, q0, [sp, #32]
fffffe00079b0238:      	str	q0, [sp, #16]
fffffe00079b023c:      	bl	0xfffffe00079b2a98 <_nstd_tx>
fffffe00079b0240:      	add	x0, sp, #16
fffffe00079b0244:      	bl	0xfffffe00079b2ddc <_nstd_log>
fffffe00079b0248:      	ldp	x1, x2, [x19, #160]
fffffe00079b024c:      	add	x0, sp, #16
fffffe00079b0250:      	mov	x3, x25
fffffe00079b0254:      	ldr	w4, [x19, #112]
fffffe00079b0258:      	mov	w6, #1
fffffe00079b025c:      	ldr	w5, [x19, #132]
fffffe00079b0260:      	bl	0xfffffe00079b2a98 <_nstd_tx>
fffffe00079b0264:      	add	x0, sp, #16
fffffe00079b0268:      	bl	0xfffffe00079b2ddc <_nstd_log>
fffffe00079b026c:      	ldr	w8, [x19, #10280]
fffffe00079b0270:      	cbnz	w8, 0xfffffe00079afb74 <_data_transmit+0x60>
fffffe00079b0274:      	mov	w8, #14
fffffe00079b0278:      	mov	x0, x19
fffffe00079b027c:      	mov	x1, x23
fffffe00079b0280:      	mov	x2, x22
fffffe00079b0284:      	str	w8, [x19, #40]
fffffe00079b0288:      	bl	0xfffffe00079aeab8 <_before>
fffffe00079b028c:      	cbnz	w0, 0xfffffe00079b02b4 <_data_transmit+0x7a0>
fffffe00079b0290:      	ldr	x8, [x23]
fffffe00079b0294:      	mov	w9, #21256
fffffe00079b0298:      	movk	w9, #6146, lsl #16
fffffe00079b029c:      	sub	x2, x29, #84
fffffe00079b02a0:      	orr	w1, w9, #0x4
fffffe00079b02a4:      	mov	x0, x22
fffffe00079b02a8:      	ldr	x8, [x8, #8]
fffffe00079b02ac:      	blr	x8
fffffe00079b02b0:      	cbz	w0, 0xfffffe00079b02e4 <_data_transmit+0x7d0>
fffffe00079b02b4:      	mov	x0, x19
fffffe00079b02b8:      	mov	w1, #5
fffffe00079b02bc:      	bl	0xfffffe00079b1780 <_fail>
fffffe00079b02c0:      	mov	w0, #-1
fffffe00079b02c4:      	ldp	x29, x30, [sp, #208]
fffffe00079b02c8:      	ldp	x20, x19, [sp, #192]
fffffe00079b02cc:      	ldp	x22, x21, [sp, #176]
fffffe00079b02d0:      	ldp	x24, x23, [sp, #160]
fffffe00079b02d4:      	ldp	x26, x25, [sp, #144]
fffffe00079b02d8:      	ldp	x28, x27, [sp, #128]
fffffe00079b02dc:      	add	sp, sp, #224
fffffe00079b02e0:      	ret
fffffe00079b02e4:      	ldur	w8, [x29, #-84]
fffffe00079b02e8:      	ldr	w9, [x19, #10264]
fffffe00079b02ec:      	cmp	w8, w9
fffffe00079b02f0:      	b.ne	0xfffffe00079b02fc <_data_transmit+0x7e8>
fffffe00079b02f4:      	mov	w0, wzr
fffffe00079b02f8:      	b	0xfffffe00079b02c4 <_data_transmit+0x7b0>
fffffe00079b02fc:      	add	w9, w9, #1
fffffe00079b0300:      	and	w9, w9, #0x3ff
fffffe00079b0304:      	cmp	w8, w9
fffffe00079b0308:      	b.ne	0xfffffe00079b0334 <_data_transmit+0x820>
fffffe00079b030c:      	mov	w8, #10
fffffe00079b0310:      	mov	x0, x19
fffffe00079b0314:      	mov	x1, x23
fffffe00079b0318:      	mov	x2, x22
fffffe00079b031c:      	str	w8, [x19, #40]
fffffe00079b0320:      	bl	0xfffffe00079aeab8 <_before>
fffffe00079b0324:      	cbz	w0, 0xfffffe00079b0340 <_data_transmit+0x82c>
fffffe00079b0328:      	mov	x0, x19
fffffe00079b032c:      	mov	w1, #9
fffffe00079b0330:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079b0334:      	mov	x0, x19
fffffe00079b0338:      	mov	w1, #45
fffffe00079b033c:      	b	0xfffffe00079b02bc <_data_transmit+0x7a8>
fffffe00079b0340:      	mov	x0, x19
fffffe00079b0344:      	mov	x1, x24
fffffe00079b0348:      	mov	w2, #16
fffffe00079b034c:      	mov	w3, wzr
fffffe00079b0350:      	bl	0xfffffe00079ae004 <_nst_memory_range>
fffffe00079b0354:      	cbz	w0, 0xfffffe00079b0328 <_data_transmit+0x814>
fffffe00079b0358:      	ldr	x8, [x23]
fffffe00079b035c:      	mov	x0, x22
fffffe00079b0360:      	mov	x1, x24
fffffe00079b0364:      	mov	w2, #16
fffffe00079b0368:      	mov	w3, #2
fffffe00079b036c:      	ldr	x8, [x8, #40]
fffffe00079b0370:      	blr	x8
fffffe00079b0374:      	cbnz	w0, 0xfffffe00079b0328 <_data_transmit+0x814>
fffffe00079b0378:      	mov	w8, #10324
fffffe00079b037c:      	mov	x0, x19
fffffe00079b0380:      	add	x4, x19, x8
fffffe00079b0384:      	mov	x1, x23
fffffe00079b0388:      	mov	x2, x22
fffffe00079b038c:      	mov	x3, x24
fffffe00079b0390:      	mov	w5, #16
fffffe00079b0394:      	bl	0xfffffe00079b1980 <_mr>
fffffe00079b0398:      	cbnz	w0, 0xfffffe00079b02c0 <_data_transmit+0x7ac>
fffffe00079b039c:      	ldrsb	w8, [x28, #55]
fffffe00079b03a0:      	tbz	w8, #31, 0xfffffe00079b02f4 <_data_transmit+0x7e0>
fffffe00079b03a4:      	mov	w9, #1
fffffe00079b03a8:      	ldr	w10, [x19, #96]
fffffe00079b03ac:      	ldr	w8, [x19, #10292]
fffffe00079b03b0:      	add	x0, sp, #16
fffffe00079b03b4:      	ldp	x1, x2, [x19, #160]
fffffe00079b03b8:      	movi.2d	v0, #0000000000000000
fffffe00079b03bc:      	str	w9, [x19, #10280]
fffffe00079b03c0:      	add	w9, w10, #1
fffffe00079b03c4:      	mov	w10, #10340
fffffe00079b03c8:      	add	w8, w8, #1
fffffe00079b03cc:      	ldr	w4, [x19, #112]
fffffe00079b03d0:      	add	x3, x19, x10
fffffe00079b03d4:      	ldr	w5, [x19, #132]
fffffe00079b03d8:      	mov	w6, #2
fffffe00079b03dc:      	str	xzr, [sp, #64]
fffffe00079b03e0:      	stp	q0, q0, [sp, #32]
fffffe00079b03e4:      	str	w8, [x19, #10292]
fffffe00079b03e8:      	str	w9, [x19, #96]
fffffe00079b03ec:      	str	q0, [sp, #16]
fffffe00079b03f0:      	bl	0xfffffe00079b2a98 <_nstd_tx>
fffffe00079b03f4:      	add	x0, sp, #16
fffffe00079b03f8:      	bl	0xfffffe00079b2ddc <_nstd_log>
fffffe00079b03fc:      	ldr	w8, [x19, #10284]
fffffe00079b0400:      	cbnz	w8, 0xfffffe00079afb7c <_data_transmit+0x68>
fffffe00079b0404:      	b	0xfffffe00079b02f4 <_data_transmit+0x7e0>
