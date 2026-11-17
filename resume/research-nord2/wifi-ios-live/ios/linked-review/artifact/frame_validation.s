
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ada2c <_frame>:
fffffe00079ada2c:      	sub	sp, sp, #176
fffffe00079ada30:      	stp	x26, x25, [sp, #96]
fffffe00079ada34:      	stp	x24, x23, [sp, #112]
fffffe00079ada38:      	stp	x22, x21, [sp, #128]
fffffe00079ada3c:      	stp	x20, x19, [sp, #144]
fffffe00079ada40:      	stp	x29, x30, [sp, #160]
fffffe00079ada44:      	add	x29, sp, #160
fffffe00079ada48:      	adrp	x8, 0xfffffe0007c9c000 <_group_replay+0x118>
fffffe00079ada4c:      	mov	w24, w5
fffffe00079ada50:      	mov	w20, w4
fffffe00079ada54:      	mov	x19, x0
fffffe00079ada58:      	mov	x21, x3
fffffe00079ada5c:      	mov	x23, x2
fffffe00079ada60:      	nop
fffffe00079ada64:      	mov	x22, x1
fffffe00079ada68:      	ldr	x8, [x8, #3440]
fffffe00079ada6c:      	stur	x8, [x29, #-72]
fffffe00079ada70:      	cbz	x0, 0xfffffe00079adaac <_frame+0x80>
fffffe00079ada74:      	ldr	w8, [x19, #28]
fffffe00079ada78:      	cbz	w8, 0xfffffe00079adab0 <_frame+0x84>
fffffe00079ada7c:      	ldr	x8, [x19]
fffffe00079ada80:      	cbz	x8, 0xfffffe00079adab0 <_frame+0x84>
fffffe00079ada84:      	ldr	x9, [x19, #8]
fffffe00079ada88:      	cbz	x9, 0xfffffe00079adaac <_frame+0x80>
fffffe00079ada8c:      	ldr	x9, [x19, #16]
fffffe00079ada90:      	cbz	x9, 0xfffffe00079adaac <_frame+0x80>
fffffe00079ada94:      	ldr	w9, [x19, #44]
fffffe00079ada98:      	cbnz	w9, 0xfffffe00079adaac <_frame+0x80>
fffffe00079ada9c:      	ldr	w9, [x8, #12]
fffffe00079adaa0:      	cbnz	w9, 0xfffffe00079adaac <_frame+0x80>
fffffe00079adaa4:      	ldr	w8, [x8, #1884]
fffffe00079adaa8:      	cbz	w8, 0xfffffe00079adcdc <_frame+0x2b0>
fffffe00079adaac:      	mov	w8, wzr
fffffe00079adab0:      	mov	w0, #-1
fffffe00079adab4:      	sub	x9, x21, #2
fffffe00079adab8:      	cmp	x9, #1566
fffffe00079adabc:      	b.hi	0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adac0:      	cbz	x23, 0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adac4:      	cbz	x22, 0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adac8:      	cbz	w8, 0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adacc:      	ldr	w8, [x19, #56]
fffffe00079adad0:      	cbnz	w8, 0xfffffe00079adae4 <_frame+0xb8>
fffffe00079adad4:      	ldr	w8, [x19, #60]
fffffe00079adad8:      	cbnz	w8, 0xfffffe00079adae4 <_frame+0xb8>
fffffe00079adadc:      	ldr	w8, [x19, #1560]
fffffe00079adae0:      	cbz	w8, 0xfffffe00079adb1c <_frame+0xf0>
fffffe00079adae4:      	mov	w0, #1
fffffe00079adae8:      	adrp	x8, 0xfffffe0007c9c000 <_group_replay+0x1b8>
fffffe00079adaec:      	nop
fffffe00079adaf0:      	ldur	x9, [x29, #-72]
fffffe00079adaf4:      	ldr	x8, [x8, #3440]
fffffe00079adaf8:      	cmp	x8, x9
fffffe00079adafc:      	b.ne	0xfffffe00079adf9c <_frame+0x570>
fffffe00079adb00:      	ldp	x29, x30, [sp, #160]
fffffe00079adb04:      	ldp	x20, x19, [sp, #144]
fffffe00079adb08:      	ldp	x22, x21, [sp, #128]
fffffe00079adb0c:      	ldp	x24, x23, [sp, #112]
fffffe00079adb10:      	ldp	x26, x25, [sp, #96]
fffffe00079adb14:      	add	sp, sp, #176
fffffe00079adb18:      	ret
fffffe00079adb1c:      	ldr	w8, [x19, #24]
fffffe00079adb20:      	cbz	w8, 0xfffffe00079adb3c <_frame+0x110>
fffffe00079adb24:      	cmp	w20, #1
fffffe00079adb28:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adb2c:      	ldrh	w8, [x23]
fffffe00079adb30:      	cmp	w8, #192
fffffe00079adb34:      	b.eq	0xfffffe00079adb44 <_frame+0x118>
fffffe00079adb38:      	b	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adb3c:      	cmp	w20, #1
fffffe00079adb40:      	b.ne	0xfffffe00079adcec <_frame+0x2c0>
fffffe00079adb44:      	ldr	w8, [x19, #104]
fffffe00079adb48:      	sub	w9, w8, #32
fffffe00079adb4c:      	cmn	w9, #31
fffffe00079adb50:      	b.lo	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adb54:      	ldrh	w9, [x23]
fffffe00079adb58:      	ldur	q0, [x19, #220]
fffffe00079adb5c:      	ldr	w10, [x19, #236]
fffffe00079adb60:      	cmp	w9, #176
fffffe00079adb64:      	str	q0, [sp, #16]
fffffe00079adb68:      	str	w10, [sp, #32]
fffffe00079adb6c:      	b.eq	0xfffffe00079adb74 <_frame+0x148>
fffffe00079adb70:      	cbnz	w9, 0xfffffe00079adb7c <_frame+0x150>
fffffe00079adb74:      	mov	w9, #16
fffffe00079adb78:      	strb	w9, [sp, #18]
fffffe00079adb7c:      	movi.2d	v0, #0000000000000000
fffffe00079adb80:      	add	x0, sp, #48
fffffe00079adb84:      	add	x2, sp, #8
fffffe00079adb88:      	add	x3, sp, #16
fffffe00079adb8c:      	mov	w1, #32
fffffe00079adb90:      	mov	x4, x23
fffffe00079adb94:      	mov	x5, x21
fffffe00079adb98:      	strb	w8, [sp, #19]
fffffe00079adb9c:      	strb	w24, [sp, #32]
fffffe00079adba0:      	str	xzr, [sp, #8]
fffffe00079adba4:      	stp	q0, q0, [sp, #48]
fffffe00079adba8:      	bl	0xfffffe00079ac248 <_sw_management_txd>
fffffe00079adbac:      	mov	w8, w0
fffffe00079adbb0:      	mov	w0, #-1
fffffe00079adbb4:      	cmp	x21, #24
fffffe00079adbb8:      	b.lo	0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adbbc:      	cbnz	w8, 0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adbc0:      	ldr	x8, [sp, #8]
fffffe00079adbc4:      	cmp	x8, #32
fffffe00079adbc8:      	b.ne	0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adbcc:      	ldrb	w8, [x23, #4]
fffffe00079adbd0:      	ldrb	w9, [x19, #206]
fffffe00079adbd4:      	cmp	w8, w9
fffffe00079adbd8:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adbdc:      	ldrb	w9, [x23, #5]
fffffe00079adbe0:      	ldrb	w10, [x19, #207]
fffffe00079adbe4:      	cmp	w9, w10
fffffe00079adbe8:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adbec:      	ldrb	w10, [x23, #6]
fffffe00079adbf0:      	ldrb	w11, [x19, #208]
fffffe00079adbf4:      	cmp	w10, w11
fffffe00079adbf8:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adbfc:      	ldrb	w11, [x23, #7]
fffffe00079adc00:      	ldrb	w12, [x19, #209]
fffffe00079adc04:      	cmp	w11, w12
fffffe00079adc08:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc0c:      	ldrb	w12, [x23, #8]
fffffe00079adc10:      	ldrb	w13, [x19, #210]
fffffe00079adc14:      	cmp	w12, w13
fffffe00079adc18:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc1c:      	ldrb	w13, [x23, #9]
fffffe00079adc20:      	ldrb	w14, [x19, #211]
fffffe00079adc24:      	cmp	w13, w14
fffffe00079adc28:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc2c:      	ldrb	w14, [x23, #10]
fffffe00079adc30:      	ldrb	w15, [x19, #200]
fffffe00079adc34:      	cmp	w14, w15
fffffe00079adc38:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc3c:      	ldrb	w14, [x23, #11]
fffffe00079adc40:      	ldrb	w15, [x19, #201]
fffffe00079adc44:      	cmp	w14, w15
fffffe00079adc48:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc4c:      	ldrb	w14, [x23, #12]
fffffe00079adc50:      	ldrb	w15, [x19, #202]
fffffe00079adc54:      	cmp	w14, w15
fffffe00079adc58:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc5c:      	ldrb	w14, [x23, #13]
fffffe00079adc60:      	ldrb	w15, [x19, #203]
fffffe00079adc64:      	cmp	w14, w15
fffffe00079adc68:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc6c:      	ldrb	w14, [x23, #14]
fffffe00079adc70:      	ldrb	w15, [x19, #204]
fffffe00079adc74:      	cmp	w14, w15
fffffe00079adc78:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc7c:      	ldrb	w14, [x23, #15]
fffffe00079adc80:      	ldrb	w15, [x19, #205]
fffffe00079adc84:      	cmp	w14, w15
fffffe00079adc88:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc8c:      	ldrb	w14, [x23, #16]
fffffe00079adc90:      	cmp	w14, w8
fffffe00079adc94:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adc98:      	ldrb	w8, [x23, #17]
fffffe00079adc9c:      	cmp	w8, w9
fffffe00079adca0:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adca4:      	ldrb	w8, [x23, #18]
fffffe00079adca8:      	cmp	w8, w10
fffffe00079adcac:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adcb0:      	ldrb	w8, [x23, #19]
fffffe00079adcb4:      	cmp	w8, w11
fffffe00079adcb8:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adcbc:      	ldrb	w8, [x23, #20]
fffffe00079adcc0:      	cmp	w8, w12
fffffe00079adcc4:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adcc8:      	ldrb	w8, [x23, #21]
fffffe00079adccc:      	cmp	w8, w13
fffffe00079adcd0:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adcd4:      	mov	w25, #1
fffffe00079adcd8:      	b	0xfffffe00079ade24 <_frame+0x3f8>
fffffe00079adcdc:      	ldr	w8, [x19, #152]
fffffe00079adce0:      	cmp	w8, #0
fffffe00079adce4:      	cset	w8, eq
fffffe00079adce8:      	b	0xfffffe00079adab0 <_frame+0x84>
fffffe00079adcec:      	ldur	q0, [x19, #220]
fffffe00079adcf0:      	mov	w9, #127
fffffe00079adcf4:      	ldr	w8, [x19, #236]
fffffe00079adcf8:      	add	x0, sp, #48
fffffe00079adcfc:      	add	x2, sp, #8
fffffe00079add00:      	add	x3, sp, #16
fffffe00079add04:      	str	q0, [sp, #16]
fffffe00079add08:      	mov	w1, #32
fffffe00079add0c:      	movi.2d	v0, #0000000000000000
fffffe00079add10:      	mov	x4, x23
fffffe00079add14:      	mov	x5, x21
fffffe00079add18:      	cmp	w20, #2
fffffe00079add1c:      	str	w8, [sp, #32]
fffffe00079add20:      	strb	w9, [sp, #19]
fffffe00079add24:      	strb	w24, [sp, #32]
fffffe00079add28:      	str	xzr, [sp, #8]
fffffe00079add2c:      	stp	q0, q0, [sp, #48]
fffffe00079add30:      	b.ne	0xfffffe00079add3c <_frame+0x310>
fffffe00079add34:      	bl	0xfffffe00079ac5bc <_sw_security_txd>
fffffe00079add38:      	b	0xfffffe00079add40 <_frame+0x314>
fffffe00079add3c:      	bl	0xfffffe00079ac5c4 <_sw_data_txd>
fffffe00079add40:      	mov	w8, w0
fffffe00079add44:      	mov	w0, #-1
fffffe00079add48:      	cbnz	w8, 0xfffffe00079adae8 <_frame+0xbc>
fffffe00079add4c:      	ldr	x8, [sp, #8]
fffffe00079add50:      	cmp	x8, #32
fffffe00079add54:      	b.ne	0xfffffe00079adae8 <_frame+0xbc>
fffffe00079add58:      	ldrb	w8, [x23, #6]
fffffe00079add5c:      	ldrb	w9, [x19, #200]
fffffe00079add60:      	cmp	w8, w9
fffffe00079add64:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079add68:      	ldrb	w8, [x23, #7]
fffffe00079add6c:      	ldrb	w9, [x19, #201]
fffffe00079add70:      	cmp	w8, w9
fffffe00079add74:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079add78:      	ldrb	w8, [x23, #8]
fffffe00079add7c:      	ldrb	w9, [x19, #202]
fffffe00079add80:      	cmp	w8, w9
fffffe00079add84:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079add88:      	ldrb	w8, [x23, #9]
fffffe00079add8c:      	ldrb	w9, [x19, #203]
fffffe00079add90:      	cmp	w8, w9
fffffe00079add94:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079add98:      	ldrb	w8, [x23, #10]
fffffe00079add9c:      	ldrb	w9, [x19, #204]
fffffe00079adda0:      	cmp	w8, w9
fffffe00079adda4:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adda8:      	ldrb	w8, [x23, #11]
fffffe00079addac:      	ldrb	w9, [x19, #205]
fffffe00079addb0:      	cmp	w8, w9
fffffe00079addb4:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079addb8:      	cmp	w20, #2
fffffe00079addbc:      	b.ne	0xfffffe00079ade20 <_frame+0x3f4>
fffffe00079addc0:      	ldrb	w8, [x23]
fffffe00079addc4:      	ldrb	w9, [x19, #206]
fffffe00079addc8:      	cmp	w8, w9
fffffe00079addcc:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079addd0:      	ldrb	w8, [x23, #1]
fffffe00079addd4:      	ldrb	w9, [x19, #207]
fffffe00079addd8:      	cmp	w8, w9
fffffe00079adddc:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079adde0:      	ldrb	w8, [x23, #2]
fffffe00079adde4:      	ldrb	w9, [x19, #208]
fffffe00079adde8:      	cmp	w8, w9
fffffe00079addec:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079addf0:      	ldrb	w8, [x23, #3]
fffffe00079addf4:      	ldrb	w9, [x19, #209]
fffffe00079addf8:      	cmp	w8, w9
fffffe00079addfc:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079ade00:      	ldrb	w8, [x23, #4]
fffffe00079ade04:      	ldrb	w9, [x19, #210]
fffffe00079ade08:      	cmp	w8, w9
fffffe00079ade0c:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079ade10:      	ldrb	w8, [x23, #5]
fffffe00079ade14:      	ldrb	w9, [x19, #211]
fffffe00079ade18:      	cmp	w8, w9
fffffe00079ade1c:      	b.ne	0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079ade20:      	mov	w25, wzr
fffffe00079ade24:      	ldr	x0, [x19, #160]
fffffe00079ade28:      	mov	w2, w20
fffffe00079ade2c:      	ldrb	w1, [sp, #16]
fffffe00079ade30:      	mov	w3, w24
fffffe00079ade34:      	mov	x4, x23
fffffe00079ade38:      	mov	x5, x21
fffffe00079ade3c:      	bl	0xfffffe00079b3318 <_nsdd_pid_reserve>
fffffe00079ade40:      	cbz	w0, 0xfffffe00079adf10 <_frame+0x4e4>
fffffe00079ade44:      	ldrb	w9, [sp, #68]
fffffe00079ade48:      	mov	w8, w0
fffffe00079ade4c:      	ldrb	w10, [sp, #69]
fffffe00079ade50:      	strb	w0, [sp, #19]
fffffe00079ade54:      	ldrb	w11, [sp, #70]
fffffe00079ade58:      	ldrb	w12, [sp, #71]
fffffe00079ade5c:      	and	w9, w9, #0x80
fffffe00079ade60:      	bfi	w9, w10, #8, #8
fffffe00079ade64:      	add	x10, x19, #1769
fffffe00079ade68:      	bfi	w9, w11, #16, #8
fffffe00079ade6c:      	bfi	w9, w12, #24, #8
fffffe00079ade70:      	orr	w9, w9, w0
fffffe00079ade74:      	str	w9, [sp, #68]
fffffe00079ade78:      	cbz	w25, 0xfffffe00079ade84 <_frame+0x458>
fffffe00079ade7c:      	add	w9, w8, #1
fffffe00079ade80:      	str	w9, [x19, #104]
fffffe00079ade84:      	mov	x9, xzr
fffffe00079ade88:      	strb	wzr, [x10, x9]
fffffe00079ade8c:      	add	x9, x9, #1
fffffe00079ade90:      	cmp	x9, #1, lsl #12
fffffe00079ade94:      	b.ne	0xfffffe00079ade88 <_frame+0x45c>
fffffe00079ade98:      	ldp	q0, q1, [sp, #48]
fffffe00079ade9c:      	stp	q0, q1, [x10]
fffffe00079adea0:      	cbz	x21, 0xfffffe00079aded8 <_frame+0x4ac>
fffffe00079adea4:      	add	x9, x19, #1801
fffffe00079adea8:      	cmp	x21, #8
fffffe00079adeac:      	b.lo	0xfffffe00079adec0 <_frame+0x494>
fffffe00079adeb0:      	sub	x11, x19, x23
fffffe00079adeb4:      	add	x11, x11, #1801
fffffe00079adeb8:      	cmp	x11, #32
fffffe00079adebc:      	b.hs	0xfffffe00079adf18 <_frame+0x4ec>
fffffe00079adec0:      	mov	x10, x23
fffffe00079adec4:      	mov	x11, x21
fffffe00079adec8:      	ldrb	w12, [x10], #1
fffffe00079adecc:      	subs	x11, x11, #1
fffffe00079aded0:      	strb	w12, [x9], #1
fffffe00079aded4:      	b.ne	0xfffffe00079adec8 <_frame+0x49c>
fffffe00079aded8:      	add	w9, w21, #32
fffffe00079adedc:      	mov	w0, wzr
fffffe00079adee0:      	and	w8, w8, #0xff
fffffe00079adee4:      	ldrb	w10, [sp, #16]
fffffe00079adee8:      	str	x22, [x19, #168]
fffffe00079adeec:      	stp	w9, wzr, [x19, #112]
fffffe00079adef0:      	mov	w9, #1
fffffe00079adef4:      	stp	w8, w10, [x19, #124]
fffffe00079adef8:      	str	wzr, [x19, #144]
fffffe00079adefc:      	str	w9, [x19, #56]
fffffe00079adf00:      	str	xzr, [x19, #136]
fffffe00079adf04:      	str	w20, [x19, #132]
fffffe00079adf08:      	str	wzr, [x19, #1568]
fffffe00079adf0c:      	b	0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adf10:      	mov	w0, #-1
fffffe00079adf14:      	b	0xfffffe00079adae8 <_frame+0xbc>
fffffe00079adf18:      	cmp	x21, #32
fffffe00079adf1c:      	b.hs	0xfffffe00079adf28 <_frame+0x4fc>
fffffe00079adf20:      	mov	x12, xzr
fffffe00079adf24:      	b	0xfffffe00079adf60 <_frame+0x534>
fffffe00079adf28:      	and	x12, x21, #0xffffffffffffffe0
fffffe00079adf2c:      	add	x10, x10, #48
fffffe00079adf30:      	add	x11, x23, #16
fffffe00079adf34:      	mov	x13, x12
fffffe00079adf38:      	ldp	q0, q1, [x11, #-16]
fffffe00079adf3c:      	add	x11, x11, #32
fffffe00079adf40:      	subs	x13, x13, #32
fffffe00079adf44:      	stp	q0, q1, [x10, #-16]
fffffe00079adf48:      	add	x10, x10, #32
fffffe00079adf4c:      	b.ne	0xfffffe00079adf38 <_frame+0x50c>
fffffe00079adf50:      	cmp	x12, x21
fffffe00079adf54:      	b.eq	0xfffffe00079aded8 <_frame+0x4ac>
fffffe00079adf58:      	tst	x21, #0x18
fffffe00079adf5c:      	b.eq	0xfffffe00079adfa0 <_frame+0x574>
fffffe00079adf60:      	and	x13, x21, #0xfffffffffffffff8
fffffe00079adf64:      	add	x14, x12, x19
fffffe00079adf68:      	add	x10, x23, x13
fffffe00079adf6c:      	add	x9, x9, x13
fffffe00079adf70:      	and	x11, x21, #0x7
fffffe00079adf74:      	add	x14, x14, #1801
fffffe00079adf78:      	add	x15, x23, x12
fffffe00079adf7c:      	sub	x12, x12, x13
fffffe00079adf80:      	ldr	d0, [x15], #8
fffffe00079adf84:      	adds	x12, x12, #8
fffffe00079adf88:      	str	d0, [x14], #8
fffffe00079adf8c:      	b.ne	0xfffffe00079adf80 <_frame+0x554>
fffffe00079adf90:      	cmp	x13, x21
fffffe00079adf94:      	b.ne	0xfffffe00079adec8 <_frame+0x49c>
fffffe00079adf98:      	b	0xfffffe00079aded8 <_frame+0x4ac>
fffffe00079adf9c:      	bl	0xfffffe0007a09274 <___stack_chk_fail>
fffffe00079adfa0:      	and	x11, x21, #0x1f
fffffe00079adfa4:      	add	x9, x9, x12
fffffe00079adfa8:      	add	x10, x23, x12
fffffe00079adfac:      	b	0xfffffe00079adec8 <_frame+0x49c>
