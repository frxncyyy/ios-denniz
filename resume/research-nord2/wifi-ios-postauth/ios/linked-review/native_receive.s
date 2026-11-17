
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079acb28 <_receive>:
fffffe00079acb28:      	sub	sp, sp, #160
fffffe00079acb2c:      	stp	x28, x27, [sp, #64]
fffffe00079acb30:      	stp	x26, x25, [sp, #80]
fffffe00079acb34:      	stp	x24, x23, [sp, #96]
fffffe00079acb38:      	stp	x22, x21, [sp, #112]
fffffe00079acb3c:      	stp	x20, x19, [sp, #128]
fffffe00079acb40:      	stp	x29, x30, [sp, #144]
fffffe00079acb44:      	add	x29, sp, #144
fffffe00079acb48:      	mov	w8, #80
fffffe00079acb4c:      	mov	x20, x5
fffffe00079acb50:      	mov	x21, x4
fffffe00079acb54:      	mov	w22, w3
fffffe00079acb58:      	umaddl	x25, w3, w8, x0
fffffe00079acb5c:      	mov	x19, x0
fffffe00079acb60:      	mov	x23, x2
fffffe00079acb64:      	mov	x24, x1
fffffe00079acb68:      	str	wzr, [sp, #60]
fffffe00079acb6c:      	str	w3, [x0, #100]
fffffe00079acb70:      	ldr	w26, [x25, #248]
fffffe00079acb74:      	str	wzr, [x0, #40]
fffffe00079acb78:      	bl	0xfffffe00079ac068 <_before>
fffffe00079acb7c:      	cbnz	w0, 0xfffffe00079acb9c <_receive+0x74>
fffffe00079acb80:      	ldr	x8, [x24]
fffffe00079acb84:      	add	w1, w26, #12
fffffe00079acb88:      	add	x2, sp, #60
fffffe00079acb8c:      	mov	x0, x23
fffffe00079acb90:      	ldr	x8, [x8, #8]
fffffe00079acb94:      	blr	x8
fffffe00079acb98:      	cbz	w0, 0xfffffe00079acbcc <_receive+0xa4>
fffffe00079acb9c:      	mov	x0, x19
fffffe00079acba0:      	mov	w1, #5
fffffe00079acba4:      	bl	0xfffffe00079ae1c8 <_fail>
fffffe00079acba8:      	mov	w0, #-1
fffffe00079acbac:      	ldp	x29, x30, [sp, #144]
fffffe00079acbb0:      	ldp	x20, x19, [sp, #128]
fffffe00079acbb4:      	ldp	x22, x21, [sp, #112]
fffffe00079acbb8:      	ldp	x24, x23, [sp, #96]
fffffe00079acbbc:      	ldp	x26, x25, [sp, #80]
fffffe00079acbc0:      	ldp	x28, x27, [sp, #64]
fffffe00079acbc4:      	add	sp, sp, #160
fffffe00079acbc8:      	ret
fffffe00079acbcc:      	add	x26, x25, #232
fffffe00079acbd0:      	ldr	w8, [sp, #60]
fffffe00079acbd4:      	ldr	w9, [x26, #20]
fffffe00079acbd8:      	cmp	w8, w9
fffffe00079acbdc:      	b.hs	0xfffffe00079acc00 <_receive+0xd8>
fffffe00079acbe0:      	mov	w10, w22
fffffe00079acbe4:      	mov	w9, #80
fffffe00079acbe8:      	madd	x25, x10, x9, x19
fffffe00079acbec:      	ldr	w28, [x25, #264]
fffffe00079acbf0:      	cmp	w8, w28
fffffe00079acbf4:      	b.ne	0xfffffe00079acc0c <_receive+0xe4>
fffffe00079acbf8:      	mov	w0, wzr
fffffe00079acbfc:      	b	0xfffffe00079acbac <_receive+0x84>
fffffe00079acc00:      	mov	x0, x19
fffffe00079acc04:      	mov	w1, #20
fffffe00079acc08:      	b	0xfffffe00079acba4 <_receive+0x7c>
fffffe00079acc0c:      	ldp	x27, x9, [x26]
fffffe00079acc10:      	mov	w8, #5
fffffe00079acc14:      	mov	x0, x19
fffffe00079acc18:      	mov	x1, x24
fffffe00079acc1c:      	mov	x2, x23
fffffe00079acc20:      	str	w8, [x19, #40]
fffffe00079acc24:      	stp	x9, x10, [sp, #40]
fffffe00079acc28:      	bl	0xfffffe00079ac068 <_before>
fffffe00079acc2c:      	cbz	w0, 0xfffffe00079acc3c <_receive+0x114>
fffffe00079acc30:      	mov	x0, x19
fffffe00079acc34:      	mov	w1, #9
fffffe00079acc38:      	b	0xfffffe00079acba4 <_receive+0x7c>
fffffe00079acc3c:      	lsl	w8, w28, #4
fffffe00079acc40:      	mov	x0, x19
fffffe00079acc44:      	add	x27, x27, x8
fffffe00079acc48:      	mov	w2, #16
fffffe00079acc4c:      	mov	x1, x27
fffffe00079acc50:      	mov	w3, wzr
fffffe00079acc54:      	bl	0xfffffe00079ab898 <_nst_memory_range>
fffffe00079acc58:      	cbz	w0, 0xfffffe00079acc30 <_receive+0x108>
fffffe00079acc5c:      	ldr	x8, [x24]
fffffe00079acc60:      	mov	x0, x23
fffffe00079acc64:      	mov	x1, x27
fffffe00079acc68:      	mov	w2, #16
fffffe00079acc6c:      	mov	w3, #2
fffffe00079acc70:      	ldr	x8, [x8, #40]
fffffe00079acc74:      	blr	x8
fffffe00079acc78:      	cbnz	w0, 0xfffffe00079acc30 <_receive+0x108>
fffffe00079acc7c:      	mov	x0, x19
fffffe00079acc80:      	mov	x1, x24
fffffe00079acc84:      	mov	x2, x23
fffffe00079acc88:      	bl	0xfffffe00079ac068 <_before>
fffffe00079acc8c:      	cbz	w0, 0xfffffe00079acc9c <_receive+0x174>
fffffe00079acc90:      	mov	x0, x19
fffffe00079acc94:      	mov	w1, #7
fffffe00079acc98:      	b	0xfffffe00079acba4 <_receive+0x7c>
fffffe00079acc9c:      	mov	x0, x19
fffffe00079acca0:      	mov	x1, x27
fffffe00079acca4:      	mov	w2, #16
fffffe00079acca8:      	mov	w3, wzr
fffffe00079accac:      	bl	0xfffffe00079ab898 <_nst_memory_range>
fffffe00079accb0:      	cbz	w0, 0xfffffe00079acc90 <_receive+0x168>
fffffe00079accb4:      	ldr	x8, [x24]
fffffe00079accb8:      	mov	w9, #5257
fffffe00079accbc:      	add	x2, x19, x9
fffffe00079accc0:      	mov	x0, x23
fffffe00079accc4:      	mov	x1, x27
fffffe00079accc8:      	mov	w3, #16
fffffe00079acccc:      	ldr	x8, [x8, #24]
fffffe00079accd0:      	str	x2, [sp, #32]
fffffe00079accd4:      	blr	x8
fffffe00079accd8:      	cbnz	w0, 0xfffffe00079acc90 <_receive+0x168>
fffffe00079accdc:      	mov	w8, #5261
fffffe00079acce0:      	add	x9, x19, x8
fffffe00079acce4:      	ldrb	w10, [x9, #2]
fffffe00079acce8:      	ldrh	w8, [x9]
fffffe00079accec:      	ldrb	w9, [x9, #3]
fffffe00079accf0:      	bfi	w8, w10, #16, #8
fffffe00079accf4:      	bfi	w8, w9, #24, #8
fffffe00079accf8:      	tbz	w8, #31, 0xfffffe00079acbf8 <_receive+0xd0>
fffffe00079accfc:      	ldr	x11, [sp, #48]
fffffe00079acd00:      	mov	w10, #80
fffffe00079acd04:      	ands	w9, w9, #0x40
fffffe00079acd08:      	ubfx	w4, w8, #16, #14
fffffe00079acd0c:      	cset	w8, eq
fffffe00079acd10:      	stp	x26, x27, [sp, #16]
fffffe00079acd14:      	madd	x10, x11, x10, x19
fffffe00079acd18:      	str	x25, [sp, #8]
fffffe00079acd1c:      	movi.2d	v0, #0000000000000000
fffffe00079acd20:      	str	w4, [x19, #9116]
fffffe00079acd24:      	ldr	w11, [x10, #268]
fffffe00079acd28:      	str	w8, [x10, #268]
fffffe00079acd2c:      	ldr	x10, [sp, #40]
fffffe00079acd30:      	lsl	w8, w28, #12
fffffe00079acd34:      	str	q0, [x19, #9120]
fffffe00079acd38:      	str	xzr, [x19, #9168]
fffffe00079acd3c:      	add	x8, x10, x8
fffffe00079acd40:      	str	q0, [x19, #9152]
fffffe00079acd44:      	str	q0, [x19, #9136]
fffffe00079acd48:      	cbnz	w11, 0xfffffe00079acddc <_receive+0x2b4>
fffffe00079acd4c:      	cbz	w9, 0xfffffe00079acddc <_receive+0x2b4>
fffffe00079acd50:      	cbz	w4, 0xfffffe00079acddc <_receive+0x2b4>
fffffe00079acd54:      	cmp	w4, #3840
fffffe00079acd58:      	b.hi	0xfffffe00079acddc <_receive+0x2b4>
fffffe00079acd5c:      	mov	w9, #6
fffffe00079acd60:      	mov	x0, x19
fffffe00079acd64:      	mov	x1, x24
fffffe00079acd68:      	mov	x2, x23
fffffe00079acd6c:      	mov	x3, x8
fffffe00079acd70:      	mov	w5, #2
fffffe00079acd74:      	str	w9, [x19, #40]
fffffe00079acd78:      	str	x8, [sp, #40]
fffffe00079acd7c:      	bl	0xfffffe00079ae330 <_sy>
fffffe00079acd80:      	cbnz	w0, 0xfffffe00079acba8 <_receive+0x80>
fffffe00079acd84:      	mov	w8, #5273
fffffe00079acd88:      	ldr	w5, [x19, #9116]
fffffe00079acd8c:      	add	x27, x19, x8
fffffe00079acd90:      	mov	x0, x19
fffffe00079acd94:      	mov	x1, x24
fffffe00079acd98:      	mov	x2, x23
fffffe00079acd9c:      	ldr	x3, [sp, #40]
fffffe00079acda0:      	mov	x4, x27
fffffe00079acda4:      	bl	0xfffffe00079ae3c8 <_mr>
fffffe00079acda8:      	cbnz	w0, 0xfffffe00079acba8 <_receive+0x80>
fffffe00079acdac:      	ldp	x0, x1, [x19, #152]
fffffe00079acdb0:      	mov	w2, w22
fffffe00079acdb4:      	mov	x3, x27
fffffe00079acdb8:      	ldr	w4, [x19, #9116]
fffffe00079acdbc:      	bl	0xfffffe00079af58c <_nstd_rx>
fffffe00079acdc0:      	mov	x0, x19
fffffe00079acdc4:      	bl	0xfffffe00079ae590 <_decode>
fffffe00079acdc8:      	ldr	x8, [sp, #40]
fffffe00079acdcc:      	cbz	w0, 0xfffffe00079acddc <_receive+0x2b4>
fffffe00079acdd0:      	mov	x0, x19
fffffe00079acdd4:      	mov	w1, #21
fffffe00079acdd8:      	b	0xfffffe00079acba4 <_receive+0x7c>
fffffe00079acddc:      	ldr	x0, [sp, #32]
fffffe00079acde0:      	mov	x1, x8
fffffe00079acde4:      	mov	w2, #3840
fffffe00079acde8:      	bl	0xfffffe0007994dc4 <_nwt_rx_desc>
fffffe00079acdec:      	cbz	w0, 0xfffffe00079acdfc <_receive+0x2d4>
fffffe00079acdf0:      	mov	x0, x19
fffffe00079acdf4:      	mov	w1, #22
fffffe00079acdf8:      	b	0xfffffe00079acba4 <_receive+0x7c>
fffffe00079acdfc:      	ldp	x3, x4, [sp, #24]
fffffe00079ace00:      	mov	w8, #7
fffffe00079ace04:      	mov	x0, x19
fffffe00079ace08:      	mov	x1, x24
fffffe00079ace0c:      	mov	x2, x23
fffffe00079ace10:      	mov	w5, #16
fffffe00079ace14:      	str	w8, [x19, #40]
fffffe00079ace18:      	bl	0xfffffe00079ae45c <_mw>
fffffe00079ace1c:      	cbnz	w0, 0xfffffe00079acba8 <_receive+0x80>
fffffe00079ace20:      	mov	x0, x19
fffffe00079ace24:      	mov	x1, x24
fffffe00079ace28:      	mov	x2, x23
fffffe00079ace2c:      	ldr	x3, [sp, #24]
fffffe00079ace30:      	mov	w4, #16
fffffe00079ace34:      	mov	w5, #1
fffffe00079ace38:      	bl	0xfffffe00079ae330 <_sy>
fffffe00079ace3c:      	cbnz	w0, 0xfffffe00079acba8 <_receive+0x80>
fffffe00079ace40:      	ldr	x8, [sp, #8]
fffffe00079ace44:      	mov	w9, #8
fffffe00079ace48:      	mov	x0, x19
fffffe00079ace4c:      	mov	x1, x24
fffffe00079ace50:      	mov	x2, x23
fffffe00079ace54:      	add	x25, x8, #264
fffffe00079ace58:      	ldr	x8, [sp, #16]
fffffe00079ace5c:      	str	w9, [x19, #40]
fffffe00079ace60:      	ldr	w8, [x8, #16]
fffffe00079ace64:      	ldr	w4, [x25]
fffffe00079ace68:      	add	w3, w8, #8
fffffe00079ace6c:      	bl	0xfffffe00079ae500 <_rw>
fffffe00079ace70:      	cbnz	w0, 0xfffffe00079acba8 <_receive+0x80>
fffffe00079ace74:      	ldr	x8, [sp, #16]
fffffe00079ace78:      	cmp	w22, #1
fffffe00079ace7c:      	ldr	w9, [x25]
fffffe00079ace80:      	ldr	w8, [x8, #20]
fffffe00079ace84:      	add	w9, w9, #1
fffffe00079ace88:      	udiv	w10, w9, w8
fffffe00079ace8c:      	msub	w8, w10, w8, w9
fffffe00079ace90:      	str	w8, [x25]
fffffe00079ace94:      	b.ne	0xfffffe00079acea0 <_receive+0x378>
fffffe00079ace98:      	ldr	x9, [x19]
fffffe00079ace9c:      	str	w8, [x9, #2868]
fffffe00079acea0:      	ldr	x8, [sp, #48]
fffffe00079acea4:      	mov	w9, #80
fffffe00079acea8:      	str	wzr, [x19, #40]
fffffe00079aceac:      	madd	x9, x8, x9, x19
fffffe00079aceb0:      	ldr	w8, [x19, #9120]
fffffe00079aceb4:      	cmp	w8, #8
fffffe00079aceb8:      	ldr	w10, [x9, #276]
fffffe00079acebc:      	add	w10, w10, #1
fffffe00079acec0:      	str	w10, [x9, #276]
fffffe00079acec4:      	b.ne	0xfffffe00079acee4 <_receive+0x3bc>
fffffe00079acec8:      	mov	w8, #9176
fffffe00079acecc:      	ldr	x1, [x19, #152]
fffffe00079aced0:      	ldr	x2, [x19, #9160]
fffffe00079aced4:      	add	x0, x19, x8
fffffe00079aced8:      	ldr	x3, [x19, #9168]
fffffe00079acedc:      	bl	0xfffffe00079afda8 <_nsdd_receive>
fffffe00079acee0:      	ldr	w8, [x19, #9120]
fffffe00079acee4:      	cbz	w8, 0xfffffe00079acf04 <_receive+0x3dc>
fffffe00079acee8:      	mov	w8, #9120
fffffe00079aceec:      	mov	x0, x19
fffffe00079acef0:      	add	x3, x19, x8
fffffe00079acef4:      	mov	x1, x21
fffffe00079acef8:      	mov	x2, x20
fffffe00079acefc:      	bl	0xfffffe00079acf50 <_emit>
fffffe00079acf00:      	cbnz	w0, 0xfffffe00079acba8 <_receive+0x80>
fffffe00079acf04:      	mov	w0, #1
fffffe00079acf08:      	b	0xfffffe00079acbac <_receive+0x84>
