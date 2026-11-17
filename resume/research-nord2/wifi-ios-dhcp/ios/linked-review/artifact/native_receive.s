
$NORD2_REPO/research/ci-39865241324/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079add6c <_receive>:
fffffe00079add6c:      	sub	sp, sp, #160
fffffe00079add70:      	stp	x28, x27, [sp, #64]
fffffe00079add74:      	stp	x26, x25, [sp, #80]
fffffe00079add78:      	stp	x24, x23, [sp, #96]
fffffe00079add7c:      	stp	x22, x21, [sp, #112]
fffffe00079add80:      	stp	x20, x19, [sp, #128]
fffffe00079add84:      	stp	x29, x30, [sp, #144]
fffffe00079add88:      	add	x29, sp, #144
fffffe00079add8c:      	mov	w8, #80
fffffe00079add90:      	mov	x20, x5
fffffe00079add94:      	mov	x21, x4
fffffe00079add98:      	mov	w22, w3
fffffe00079add9c:      	umaddl	x25, w3, w8, x0
fffffe00079adda0:      	mov	x19, x0
fffffe00079adda4:      	mov	x23, x2
fffffe00079adda8:      	mov	x24, x1
fffffe00079addac:      	str	wzr, [sp, #60]
fffffe00079addb0:      	str	w3, [x0, #100]
fffffe00079addb4:      	ldr	w26, [x25, #248]
fffffe00079addb8:      	str	wzr, [x0, #40]
fffffe00079addbc:      	bl	0xfffffe00079ad1e4 <_before>
fffffe00079addc0:      	cbnz	w0, 0xfffffe00079adde0 <_receive+0x74>
fffffe00079addc4:      	ldr	x8, [x24]
fffffe00079addc8:      	add	w1, w26, #12
fffffe00079addcc:      	add	x2, sp, #60
fffffe00079addd0:      	mov	x0, x23
fffffe00079addd4:      	ldr	x8, [x8, #8]
fffffe00079addd8:      	blr	x8
fffffe00079adddc:      	cbz	w0, 0xfffffe00079ade10 <_receive+0xa4>
fffffe00079adde0:      	mov	x0, x19
fffffe00079adde4:      	mov	w1, #5
fffffe00079adde8:      	bl	0xfffffe00079afe2c <_fail>
fffffe00079addec:      	mov	w0, #-1
fffffe00079addf0:      	ldp	x29, x30, [sp, #144]
fffffe00079addf4:      	ldp	x20, x19, [sp, #128]
fffffe00079addf8:      	ldp	x22, x21, [sp, #112]
fffffe00079addfc:      	ldp	x24, x23, [sp, #96]
fffffe00079ade00:      	ldp	x26, x25, [sp, #80]
fffffe00079ade04:      	ldp	x28, x27, [sp, #64]
fffffe00079ade08:      	add	sp, sp, #160
fffffe00079ade0c:      	ret
fffffe00079ade10:      	add	x25, x25, #232
fffffe00079ade14:      	ldr	w8, [sp, #60]
fffffe00079ade18:      	ldr	w9, [x25, #20]
fffffe00079ade1c:      	cmp	w8, w9
fffffe00079ade20:      	b.hs	0xfffffe00079ade44 <_receive+0xd8>
fffffe00079ade24:      	mov	w10, w22
fffffe00079ade28:      	mov	w9, #80
fffffe00079ade2c:      	madd	x28, x10, x9, x19
fffffe00079ade30:      	ldr	w27, [x28, #264]
fffffe00079ade34:      	cmp	w8, w27
fffffe00079ade38:      	b.ne	0xfffffe00079ade50 <_receive+0xe4>
fffffe00079ade3c:      	mov	w0, wzr
fffffe00079ade40:      	b	0xfffffe00079addf0 <_receive+0x84>
fffffe00079ade44:      	mov	x0, x19
fffffe00079ade48:      	mov	w1, #20
fffffe00079ade4c:      	b	0xfffffe00079adde8 <_receive+0x7c>
fffffe00079ade50:      	ldp	x26, x9, [x25]
fffffe00079ade54:      	mov	w8, #5
fffffe00079ade58:      	mov	x0, x19
fffffe00079ade5c:      	mov	x1, x24
fffffe00079ade60:      	mov	x2, x23
fffffe00079ade64:      	str	w8, [x19, #40]
fffffe00079ade68:      	stp	x9, x10, [sp, #40]
fffffe00079ade6c:      	bl	0xfffffe00079ad1e4 <_before>
fffffe00079ade70:      	cbz	w0, 0xfffffe00079ade80 <_receive+0x114>
fffffe00079ade74:      	mov	x0, x19
fffffe00079ade78:      	mov	w1, #9
fffffe00079ade7c:      	b	0xfffffe00079adde8 <_receive+0x7c>
fffffe00079ade80:      	lsl	w8, w27, #4
fffffe00079ade84:      	mov	x0, x19
fffffe00079ade88:      	add	x26, x26, x8
fffffe00079ade8c:      	mov	w2, #16
fffffe00079ade90:      	mov	x1, x26
fffffe00079ade94:      	mov	w3, wzr
fffffe00079ade98:      	bl	0xfffffe00079ac72c <_nst_memory_range>
fffffe00079ade9c:      	cbz	w0, 0xfffffe00079ade74 <_receive+0x108>
fffffe00079adea0:      	ldr	x8, [x24]
fffffe00079adea4:      	mov	x0, x23
fffffe00079adea8:      	mov	x1, x26
fffffe00079adeac:      	mov	w2, #16
fffffe00079adeb0:      	mov	w3, #2
fffffe00079adeb4:      	ldr	x8, [x8, #40]
fffffe00079adeb8:      	blr	x8
fffffe00079adebc:      	cbnz	w0, 0xfffffe00079ade74 <_receive+0x108>
fffffe00079adec0:      	mov	x0, x19
fffffe00079adec4:      	mov	x1, x24
fffffe00079adec8:      	mov	x2, x23
fffffe00079adecc:      	bl	0xfffffe00079ad1e4 <_before>
fffffe00079aded0:      	cbz	w0, 0xfffffe00079adee0 <_receive+0x174>
fffffe00079aded4:      	mov	x0, x19
fffffe00079aded8:      	mov	w1, #7
fffffe00079adedc:      	b	0xfffffe00079adde8 <_receive+0x7c>
fffffe00079adee0:      	mov	x0, x19
fffffe00079adee4:      	mov	x1, x26
fffffe00079adee8:      	mov	w2, #16
fffffe00079adeec:      	mov	w3, wzr
fffffe00079adef0:      	bl	0xfffffe00079ac72c <_nst_memory_range>
fffffe00079adef4:      	cbz	w0, 0xfffffe00079aded4 <_receive+0x168>
fffffe00079adef8:      	ldr	x8, [x24]
fffffe00079adefc:      	mov	w9, #5857
fffffe00079adf00:      	add	x2, x19, x9
fffffe00079adf04:      	mov	x0, x23
fffffe00079adf08:      	mov	x1, x26
fffffe00079adf0c:      	mov	w3, #16
fffffe00079adf10:      	ldr	x8, [x8, #24]
fffffe00079adf14:      	str	x2, [sp, #32]
fffffe00079adf18:      	blr	x8
fffffe00079adf1c:      	cbnz	w0, 0xfffffe00079aded4 <_receive+0x168>
fffffe00079adf20:      	mov	w8, #5861
fffffe00079adf24:      	add	x9, x19, x8
fffffe00079adf28:      	ldrb	w10, [x9, #2]
fffffe00079adf2c:      	ldrh	w8, [x9]
fffffe00079adf30:      	ldrb	w9, [x9, #3]
fffffe00079adf34:      	bfi	w8, w10, #16, #8
fffffe00079adf38:      	bfi	w8, w9, #24, #8
fffffe00079adf3c:      	tbz	w8, #31, 0xfffffe00079ade3c <_receive+0xd0>
fffffe00079adf40:      	ldr	x11, [sp, #48]
fffffe00079adf44:      	mov	w10, #80
fffffe00079adf48:      	ands	w9, w9, #0x40
fffffe00079adf4c:      	ubfx	w4, w8, #16, #14
fffffe00079adf50:      	mov	w8, #9720
fffffe00079adf54:      	stp	x25, x26, [sp, #16]
fffffe00079adf58:      	madd	x10, x11, x10, x19
fffffe00079adf5c:      	cset	w11, eq
fffffe00079adf60:      	movi.2d	v0, #0000000000000000
fffffe00079adf64:      	add	x26, x19, x8
fffffe00079adf68:      	lsl	w8, w27, #12
fffffe00079adf6c:      	str	x28, [sp, #8]
fffffe00079adf70:      	str	w4, [x19, #9716]
fffffe00079adf74:      	ldr	w12, [x10, #268]
fffffe00079adf78:      	str	w11, [x10, #268]
fffffe00079adf7c:      	ldr	x10, [sp, #40]
fffffe00079adf80:      	str	xzr, [x19, #9768]
fffffe00079adf84:      	stp	q0, q0, [x26]
fffffe00079adf88:      	str	q0, [x26, #32]
fffffe00079adf8c:      	add	x8, x10, x8
fffffe00079adf90:      	cbnz	w12, 0xfffffe00079ae024 <_receive+0x2b8>
fffffe00079adf94:      	cbz	w9, 0xfffffe00079ae024 <_receive+0x2b8>
fffffe00079adf98:      	cbz	w4, 0xfffffe00079ae024 <_receive+0x2b8>
fffffe00079adf9c:      	cmp	w4, #3840
fffffe00079adfa0:      	b.hi	0xfffffe00079ae024 <_receive+0x2b8>
fffffe00079adfa4:      	mov	w9, #6
fffffe00079adfa8:      	mov	x0, x19
fffffe00079adfac:      	mov	x1, x24
fffffe00079adfb0:      	mov	x2, x23
fffffe00079adfb4:      	mov	x3, x8
fffffe00079adfb8:      	mov	w5, #2
fffffe00079adfbc:      	str	w9, [x19, #40]
fffffe00079adfc0:      	str	x8, [sp, #40]
fffffe00079adfc4:      	bl	0xfffffe00079aff94 <_sy>
fffffe00079adfc8:      	cbnz	w0, 0xfffffe00079addec <_receive+0x80>
fffffe00079adfcc:      	mov	w8, #5873
fffffe00079adfd0:      	ldr	w5, [x19, #9716]
fffffe00079adfd4:      	add	x27, x19, x8
fffffe00079adfd8:      	mov	x0, x19
fffffe00079adfdc:      	mov	x1, x24
fffffe00079adfe0:      	mov	x2, x23
fffffe00079adfe4:      	ldr	x3, [sp, #40]
fffffe00079adfe8:      	mov	x4, x27
fffffe00079adfec:      	bl	0xfffffe00079b002c <_mr>
fffffe00079adff0:      	cbnz	w0, 0xfffffe00079addec <_receive+0x80>
fffffe00079adff4:      	ldp	x0, x1, [x19, #152]
fffffe00079adff8:      	mov	w2, w22
fffffe00079adffc:      	mov	x3, x27
fffffe00079ae000:      	ldr	w4, [x19, #9716]
fffffe00079ae004:      	bl	0xfffffe00079b1350 <_nstd_rx>
fffffe00079ae008:      	mov	x0, x19
fffffe00079ae00c:      	bl	0xfffffe00079b01f4 <_decode>
fffffe00079ae010:      	ldr	x8, [sp, #40]
fffffe00079ae014:      	cbz	w0, 0xfffffe00079ae024 <_receive+0x2b8>
fffffe00079ae018:      	mov	x0, x19
fffffe00079ae01c:      	mov	w1, #21
fffffe00079ae020:      	b	0xfffffe00079adde8 <_receive+0x7c>
fffffe00079ae024:      	ldr	x0, [sp, #32]
fffffe00079ae028:      	mov	x1, x8
fffffe00079ae02c:      	mov	w2, #3840
fffffe00079ae030:      	bl	0xfffffe00079950c0 <_nwt_rx_desc>
fffffe00079ae034:      	cbz	w0, 0xfffffe00079ae044 <_receive+0x2d8>
fffffe00079ae038:      	mov	x0, x19
fffffe00079ae03c:      	mov	w1, #22
fffffe00079ae040:      	b	0xfffffe00079adde8 <_receive+0x7c>
fffffe00079ae044:      	ldp	x3, x4, [sp, #24]
fffffe00079ae048:      	mov	w8, #7
fffffe00079ae04c:      	mov	x0, x19
fffffe00079ae050:      	mov	x1, x24
fffffe00079ae054:      	mov	x2, x23
fffffe00079ae058:      	mov	w5, #16
fffffe00079ae05c:      	str	w8, [x19, #40]
fffffe00079ae060:      	bl	0xfffffe00079b00c0 <_mw>
fffffe00079ae064:      	cbnz	w0, 0xfffffe00079addec <_receive+0x80>
fffffe00079ae068:      	mov	x0, x19
fffffe00079ae06c:      	mov	x1, x24
fffffe00079ae070:      	mov	x2, x23
fffffe00079ae074:      	ldr	x3, [sp, #24]
fffffe00079ae078:      	mov	w4, #16
fffffe00079ae07c:      	mov	w5, #1
fffffe00079ae080:      	bl	0xfffffe00079aff94 <_sy>
fffffe00079ae084:      	cbnz	w0, 0xfffffe00079addec <_receive+0x80>
fffffe00079ae088:      	ldr	x8, [sp, #8]
fffffe00079ae08c:      	mov	w9, #8
fffffe00079ae090:      	mov	x0, x19
fffffe00079ae094:      	mov	x1, x24
fffffe00079ae098:      	mov	x2, x23
fffffe00079ae09c:      	add	x25, x8, #264
fffffe00079ae0a0:      	ldr	x8, [sp, #16]
fffffe00079ae0a4:      	str	w9, [x19, #40]
fffffe00079ae0a8:      	ldr	w8, [x8, #16]
fffffe00079ae0ac:      	ldr	w4, [x25]
fffffe00079ae0b0:      	add	w3, w8, #8
fffffe00079ae0b4:      	bl	0xfffffe00079b0164 <_rw>
fffffe00079ae0b8:      	cbnz	w0, 0xfffffe00079addec <_receive+0x80>
fffffe00079ae0bc:      	ldr	x8, [sp, #16]
fffffe00079ae0c0:      	cmp	w22, #1
fffffe00079ae0c4:      	ldr	w9, [x25]
fffffe00079ae0c8:      	ldr	w8, [x8, #20]
fffffe00079ae0cc:      	add	w9, w9, #1
fffffe00079ae0d0:      	udiv	w10, w9, w8
fffffe00079ae0d4:      	msub	w8, w10, w8, w9
fffffe00079ae0d8:      	str	w8, [x25]
fffffe00079ae0dc:      	b.ne	0xfffffe00079ae0e8 <_receive+0x37c>
fffffe00079ae0e0:      	ldr	x9, [x19]
fffffe00079ae0e4:      	str	w8, [x9, #2876]
fffffe00079ae0e8:      	ldr	x8, [sp, #48]
fffffe00079ae0ec:      	mov	w9, #80
fffffe00079ae0f0:      	str	wzr, [x19, #40]
fffffe00079ae0f4:      	madd	x9, x8, x9, x19
fffffe00079ae0f8:      	ldr	w8, [x19, #9720]
fffffe00079ae0fc:      	cmp	w8, #8
fffffe00079ae100:      	ldr	w10, [x9, #276]
fffffe00079ae104:      	add	w10, w10, #1
fffffe00079ae108:      	str	w10, [x9, #276]
fffffe00079ae10c:      	b.ne	0xfffffe00079ae12c <_receive+0x3c0>
fffffe00079ae110:      	mov	w8, #9776
fffffe00079ae114:      	ldr	x1, [x19, #152]
fffffe00079ae118:      	ldr	x2, [x19, #9760]
fffffe00079ae11c:      	add	x0, x19, x8
fffffe00079ae120:      	ldr	x3, [x19, #9768]
fffffe00079ae124:      	bl	0xfffffe00079b213c <_nsdd_receive>
fffffe00079ae128:      	ldr	w8, [x19, #9720]
fffffe00079ae12c:      	cbz	w8, 0xfffffe00079ae148 <_receive+0x3dc>
fffffe00079ae130:      	mov	x0, x19
fffffe00079ae134:      	mov	x1, x21
fffffe00079ae138:      	mov	x2, x20
fffffe00079ae13c:      	mov	x3, x26
fffffe00079ae140:      	bl	0xfffffe00079ae194 <_emit>
fffffe00079ae144:      	cbnz	w0, 0xfffffe00079addec <_receive+0x80>
fffffe00079ae148:      	mov	w0, #1
fffffe00079ae14c:      	b	0xfffffe00079addf0 <_receive+0x84>
