
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079add60 <_receive>:
fffffe00079add60:      	sub	sp, sp, #160
fffffe00079add64:      	stp	x28, x27, [sp, #64]
fffffe00079add68:      	stp	x26, x25, [sp, #80]
fffffe00079add6c:      	stp	x24, x23, [sp, #96]
fffffe00079add70:      	stp	x22, x21, [sp, #112]
fffffe00079add74:      	stp	x20, x19, [sp, #128]
fffffe00079add78:      	stp	x29, x30, [sp, #144]
fffffe00079add7c:      	add	x29, sp, #144
fffffe00079add80:      	mov	w8, #80
fffffe00079add84:      	mov	x20, x5
fffffe00079add88:      	mov	x21, x4
fffffe00079add8c:      	mov	w22, w3
fffffe00079add90:      	umaddl	x25, w3, w8, x0
fffffe00079add94:      	mov	x19, x0
fffffe00079add98:      	mov	x23, x2
fffffe00079add9c:      	mov	x24, x1
fffffe00079adda0:      	str	wzr, [sp, #60]
fffffe00079adda4:      	str	w3, [x0, #100]
fffffe00079adda8:      	ldr	w26, [x25, #248]
fffffe00079addac:      	str	wzr, [x0, #40]
fffffe00079addb0:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079addb4:      	cbnz	w0, 0xfffffe00079addd4 <_receive+0x74>
fffffe00079addb8:      	ldr	x8, [x24]
fffffe00079addbc:      	add	w1, w26, #12
fffffe00079addc0:      	add	x2, sp, #60
fffffe00079addc4:      	mov	x0, x23
fffffe00079addc8:      	ldr	x8, [x8, #8]
fffffe00079addcc:      	blr	x8
fffffe00079addd0:      	cbz	w0, 0xfffffe00079ade04 <_receive+0xa4>
fffffe00079addd4:      	mov	x0, x19
fffffe00079addd8:      	mov	w1, #5
fffffe00079adddc:      	bl	0xfffffe00079afe24 <_fail>
fffffe00079adde0:      	mov	w0, #-1
fffffe00079adde4:      	ldp	x29, x30, [sp, #144]
fffffe00079adde8:      	ldp	x20, x19, [sp, #128]
fffffe00079addec:      	ldp	x22, x21, [sp, #112]
fffffe00079addf0:      	ldp	x24, x23, [sp, #96]
fffffe00079addf4:      	ldp	x26, x25, [sp, #80]
fffffe00079addf8:      	ldp	x28, x27, [sp, #64]
fffffe00079addfc:      	add	sp, sp, #160
fffffe00079ade00:      	ret
fffffe00079ade04:      	add	x25, x25, #232
fffffe00079ade08:      	ldr	w8, [sp, #60]
fffffe00079ade0c:      	ldr	w9, [x25, #20]
fffffe00079ade10:      	cmp	w8, w9
fffffe00079ade14:      	b.hs	0xfffffe00079ade38 <_receive+0xd8>
fffffe00079ade18:      	mov	w10, w22
fffffe00079ade1c:      	mov	w9, #80
fffffe00079ade20:      	madd	x28, x10, x9, x19
fffffe00079ade24:      	ldr	w27, [x28, #264]
fffffe00079ade28:      	cmp	w8, w27
fffffe00079ade2c:      	b.ne	0xfffffe00079ade44 <_receive+0xe4>
fffffe00079ade30:      	mov	w0, wzr
fffffe00079ade34:      	b	0xfffffe00079adde4 <_receive+0x84>
fffffe00079ade38:      	mov	x0, x19
fffffe00079ade3c:      	mov	w1, #20
fffffe00079ade40:      	b	0xfffffe00079adddc <_receive+0x7c>
fffffe00079ade44:      	ldp	x26, x9, [x25]
fffffe00079ade48:      	mov	w8, #5
fffffe00079ade4c:      	mov	x0, x19
fffffe00079ade50:      	mov	x1, x24
fffffe00079ade54:      	mov	x2, x23
fffffe00079ade58:      	str	w8, [x19, #40]
fffffe00079ade5c:      	stp	x9, x10, [sp, #40]
fffffe00079ade60:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ade64:      	cbz	w0, 0xfffffe00079ade74 <_receive+0x114>
fffffe00079ade68:      	mov	x0, x19
fffffe00079ade6c:      	mov	w1, #9
fffffe00079ade70:      	b	0xfffffe00079adddc <_receive+0x7c>
fffffe00079ade74:      	lsl	w8, w27, #4
fffffe00079ade78:      	mov	x0, x19
fffffe00079ade7c:      	add	x26, x26, x8
fffffe00079ade80:      	mov	w2, #16
fffffe00079ade84:      	mov	x1, x26
fffffe00079ade88:      	mov	w3, wzr
fffffe00079ade8c:      	bl	0xfffffe00079ac720 <_nst_memory_range>
fffffe00079ade90:      	cbz	w0, 0xfffffe00079ade68 <_receive+0x108>
fffffe00079ade94:      	ldr	x8, [x24]
fffffe00079ade98:      	mov	x0, x23
fffffe00079ade9c:      	mov	x1, x26
fffffe00079adea0:      	mov	w2, #16
fffffe00079adea4:      	mov	w3, #2
fffffe00079adea8:      	ldr	x8, [x8, #40]
fffffe00079adeac:      	blr	x8
fffffe00079adeb0:      	cbnz	w0, 0xfffffe00079ade68 <_receive+0x108>
fffffe00079adeb4:      	mov	x0, x19
fffffe00079adeb8:      	mov	x1, x24
fffffe00079adebc:      	mov	x2, x23
fffffe00079adec0:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079adec4:      	cbz	w0, 0xfffffe00079aded4 <_receive+0x174>
fffffe00079adec8:      	mov	x0, x19
fffffe00079adecc:      	mov	w1, #7
fffffe00079aded0:      	b	0xfffffe00079adddc <_receive+0x7c>
fffffe00079aded4:      	mov	x0, x19
fffffe00079aded8:      	mov	x1, x26
fffffe00079adedc:      	mov	w2, #16
fffffe00079adee0:      	mov	w3, wzr
fffffe00079adee4:      	bl	0xfffffe00079ac720 <_nst_memory_range>
fffffe00079adee8:      	cbz	w0, 0xfffffe00079adec8 <_receive+0x168>
fffffe00079adeec:      	ldr	x8, [x24]
fffffe00079adef0:      	mov	w9, #5857
fffffe00079adef4:      	add	x2, x19, x9
fffffe00079adef8:      	mov	x0, x23
fffffe00079adefc:      	mov	x1, x26
fffffe00079adf00:      	mov	w3, #16
fffffe00079adf04:      	ldr	x8, [x8, #24]
fffffe00079adf08:      	str	x2, [sp, #32]
fffffe00079adf0c:      	blr	x8
fffffe00079adf10:      	cbnz	w0, 0xfffffe00079adec8 <_receive+0x168>
fffffe00079adf14:      	mov	w8, #5861
fffffe00079adf18:      	add	x9, x19, x8
fffffe00079adf1c:      	ldrb	w10, [x9, #2]
fffffe00079adf20:      	ldrh	w8, [x9]
fffffe00079adf24:      	ldrb	w9, [x9, #3]
fffffe00079adf28:      	bfi	w8, w10, #16, #8
fffffe00079adf2c:      	bfi	w8, w9, #24, #8
fffffe00079adf30:      	tbz	w8, #31, 0xfffffe00079ade30 <_receive+0xd0>
fffffe00079adf34:      	ldr	x11, [sp, #48]
fffffe00079adf38:      	mov	w10, #80
fffffe00079adf3c:      	ands	w9, w9, #0x40
fffffe00079adf40:      	ubfx	w4, w8, #16, #14
fffffe00079adf44:      	mov	w8, #9720
fffffe00079adf48:      	stp	x25, x26, [sp, #16]
fffffe00079adf4c:      	madd	x10, x11, x10, x19
fffffe00079adf50:      	cset	w11, eq
fffffe00079adf54:      	movi.2d	v0, #0000000000000000
fffffe00079adf58:      	add	x26, x19, x8
fffffe00079adf5c:      	lsl	w8, w27, #12
fffffe00079adf60:      	str	x28, [sp, #8]
fffffe00079adf64:      	str	w4, [x19, #9716]
fffffe00079adf68:      	ldr	w12, [x10, #268]
fffffe00079adf6c:      	str	w11, [x10, #268]
fffffe00079adf70:      	ldr	x10, [sp, #40]
fffffe00079adf74:      	str	xzr, [x19, #9768]
fffffe00079adf78:      	stp	q0, q0, [x26]
fffffe00079adf7c:      	str	q0, [x26, #32]
fffffe00079adf80:      	add	x8, x10, x8
fffffe00079adf84:      	cbnz	w12, 0xfffffe00079ae018 <_receive+0x2b8>
fffffe00079adf88:      	cbz	w9, 0xfffffe00079ae018 <_receive+0x2b8>
fffffe00079adf8c:      	cbz	w4, 0xfffffe00079ae018 <_receive+0x2b8>
fffffe00079adf90:      	cmp	w4, #3840
fffffe00079adf94:      	b.hi	0xfffffe00079ae018 <_receive+0x2b8>
fffffe00079adf98:      	mov	w9, #6
fffffe00079adf9c:      	mov	x0, x19
fffffe00079adfa0:      	mov	x1, x24
fffffe00079adfa4:      	mov	x2, x23
fffffe00079adfa8:      	mov	x3, x8
fffffe00079adfac:      	mov	w5, #2
fffffe00079adfb0:      	str	w9, [x19, #40]
fffffe00079adfb4:      	str	x8, [sp, #40]
fffffe00079adfb8:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079adfbc:      	cbnz	w0, 0xfffffe00079adde0 <_receive+0x80>
fffffe00079adfc0:      	mov	w8, #5873
fffffe00079adfc4:      	ldr	w5, [x19, #9716]
fffffe00079adfc8:      	add	x27, x19, x8
fffffe00079adfcc:      	mov	x0, x19
fffffe00079adfd0:      	mov	x1, x24
fffffe00079adfd4:      	mov	x2, x23
fffffe00079adfd8:      	ldr	x3, [sp, #40]
fffffe00079adfdc:      	mov	x4, x27
fffffe00079adfe0:      	bl	0xfffffe00079b0024 <_mr>
fffffe00079adfe4:      	cbnz	w0, 0xfffffe00079adde0 <_receive+0x80>
fffffe00079adfe8:      	ldp	x0, x1, [x19, #152]
fffffe00079adfec:      	mov	w2, w22
fffffe00079adff0:      	mov	x3, x27
fffffe00079adff4:      	ldr	w4, [x19, #9716]
fffffe00079adff8:      	bl	0xfffffe00079b1330 <_nstd_rx>
fffffe00079adffc:      	mov	x0, x19
fffffe00079ae000:      	bl	0xfffffe00079b01ec <_decode>
fffffe00079ae004:      	ldr	x8, [sp, #40]
fffffe00079ae008:      	cbz	w0, 0xfffffe00079ae018 <_receive+0x2b8>
fffffe00079ae00c:      	mov	x0, x19
fffffe00079ae010:      	mov	w1, #21
fffffe00079ae014:      	b	0xfffffe00079adddc <_receive+0x7c>
fffffe00079ae018:      	ldr	x0, [sp, #32]
fffffe00079ae01c:      	mov	x1, x8
fffffe00079ae020:      	mov	w2, #3840
fffffe00079ae024:      	bl	0xfffffe00079950c0 <_nwt_rx_desc>
fffffe00079ae028:      	cbz	w0, 0xfffffe00079ae038 <_receive+0x2d8>
fffffe00079ae02c:      	mov	x0, x19
fffffe00079ae030:      	mov	w1, #22
fffffe00079ae034:      	b	0xfffffe00079adddc <_receive+0x7c>
fffffe00079ae038:      	ldp	x3, x4, [sp, #24]
fffffe00079ae03c:      	mov	w8, #7
fffffe00079ae040:      	mov	x0, x19
fffffe00079ae044:      	mov	x1, x24
fffffe00079ae048:      	mov	x2, x23
fffffe00079ae04c:      	mov	w5, #16
fffffe00079ae050:      	str	w8, [x19, #40]
fffffe00079ae054:      	bl	0xfffffe00079b00b8 <_mw>
fffffe00079ae058:      	cbnz	w0, 0xfffffe00079adde0 <_receive+0x80>
fffffe00079ae05c:      	mov	x0, x19
fffffe00079ae060:      	mov	x1, x24
fffffe00079ae064:      	mov	x2, x23
fffffe00079ae068:      	ldr	x3, [sp, #24]
fffffe00079ae06c:      	mov	w4, #16
fffffe00079ae070:      	mov	w5, #1
fffffe00079ae074:      	bl	0xfffffe00079aff8c <_sy>
fffffe00079ae078:      	cbnz	w0, 0xfffffe00079adde0 <_receive+0x80>
fffffe00079ae07c:      	ldr	x8, [sp, #8]
fffffe00079ae080:      	mov	w9, #8
fffffe00079ae084:      	mov	x0, x19
fffffe00079ae088:      	mov	x1, x24
fffffe00079ae08c:      	mov	x2, x23
fffffe00079ae090:      	add	x25, x8, #264
fffffe00079ae094:      	ldr	x8, [sp, #16]
fffffe00079ae098:      	str	w9, [x19, #40]
fffffe00079ae09c:      	ldr	w8, [x8, #16]
fffffe00079ae0a0:      	ldr	w4, [x25]
fffffe00079ae0a4:      	add	w3, w8, #8
fffffe00079ae0a8:      	bl	0xfffffe00079b015c <_rw>
fffffe00079ae0ac:      	cbnz	w0, 0xfffffe00079adde0 <_receive+0x80>
fffffe00079ae0b0:      	ldr	x8, [sp, #16]
fffffe00079ae0b4:      	cmp	w22, #1
fffffe00079ae0b8:      	ldr	w9, [x25]
fffffe00079ae0bc:      	ldr	w8, [x8, #20]
fffffe00079ae0c0:      	add	w9, w9, #1
fffffe00079ae0c4:      	udiv	w10, w9, w8
fffffe00079ae0c8:      	msub	w8, w10, w8, w9
fffffe00079ae0cc:      	str	w8, [x25]
fffffe00079ae0d0:      	b.ne	0xfffffe00079ae0dc <_receive+0x37c>
fffffe00079ae0d4:      	ldr	x9, [x19]
fffffe00079ae0d8:      	str	w8, [x9, #2876]
fffffe00079ae0dc:      	ldr	x8, [sp, #48]
fffffe00079ae0e0:      	mov	w9, #80
fffffe00079ae0e4:      	str	wzr, [x19, #40]
fffffe00079ae0e8:      	madd	x9, x8, x9, x19
fffffe00079ae0ec:      	ldr	w8, [x19, #9720]
fffffe00079ae0f0:      	cmp	w8, #8
fffffe00079ae0f4:      	ldr	w10, [x9, #276]
fffffe00079ae0f8:      	add	w10, w10, #1
fffffe00079ae0fc:      	str	w10, [x9, #276]
fffffe00079ae100:      	b.ne	0xfffffe00079ae120 <_receive+0x3c0>
fffffe00079ae104:      	mov	w8, #9776
fffffe00079ae108:      	ldr	x1, [x19, #152]
fffffe00079ae10c:      	ldr	x2, [x19, #9760]
fffffe00079ae110:      	add	x0, x19, x8
fffffe00079ae114:      	ldr	x3, [x19, #9768]
fffffe00079ae118:      	bl	0xfffffe00079b1f4c <_nsdd_receive>
fffffe00079ae11c:      	ldr	w8, [x19, #9720]
fffffe00079ae120:      	cbz	w8, 0xfffffe00079ae13c <_receive+0x3dc>
fffffe00079ae124:      	mov	x0, x19
fffffe00079ae128:      	mov	x1, x21
fffffe00079ae12c:      	mov	x2, x20
fffffe00079ae130:      	mov	x3, x26
fffffe00079ae134:      	bl	0xfffffe00079ae188 <_emit>
fffffe00079ae138:      	cbnz	w0, 0xfffffe00079adde0 <_receive+0x80>
fffffe00079ae13c:      	mov	w0, #1
fffffe00079ae140:      	b	0xfffffe00079adde4 <_receive+0x84>
