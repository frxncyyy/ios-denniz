1000089a4:     	sub	sp, sp, #0xa0
1000089a8:     	stp	d11, d10, [sp, #0x30]
1000089ac:     	stp	d9, d8, [sp, #0x40]
1000089b0:     	stp	x26, x25, [sp, #0x50]
1000089b4:     	stp	x24, x23, [sp, #0x60]
1000089b8:     	stp	x22, x21, [sp, #0x70]
1000089bc:     	stp	x20, x19, [sp, #0x80]
1000089c0:     	stp	x29, x30, [sp, #0x90]
1000089c4:     	add	x29, sp, #0x90
1000089c8:     	mov	x20, x3
1000089cc:     	mov	x22, x0
1000089d0:     	nop
1000089d4:     	ldr	x8, 0x1000fc508 
1000089d8:     	ldr	x8, [x8]
1000089dc:     	str	x8, [sp, #0x28]
1000089e0:     	bl	0x1000abe70 
1000089e4:     	mov	x19, x0
1000089e8:     	bl	0x1000abe7c 
1000089ec:     	mov	x20, x0
1000089f0:     	bl	0x1000b3780    ; sel isUnavailable
1000089f4:     	cbz	w0, 0x100008a1c <__text+0x29cc>
1000089f8:     	adrp	x8, 0x100137000 
1000089fc:     	ldrb	w8, [x8, #0xc78]
100008a00:     	cmp	w8, #0x0
100008a04:     	mov	x8, #0x7ff0000000000000 ; =9218868437227405312
100008a08:     	fmov	d0, x8
100008a0c:     	nop
100008a10:     	ldr	d1, 0x1000c6368 
100008a14:     	fcsel	d8, d1, d0, ne
100008a18:     	b	0x100008cdc <__text+0x2c8c>
100008a1c:     	bl	0x1000abca8 
100008a20:     	mov	x21, x0
100008a24:     	ldr	x23, [x22, #0x60]
100008a28:     	mov	x0, x23
100008a2c:     	mov	w1, #0x0                ; =0
100008a30:     	bl	0x1000abf9c 
100008a34:     	cbz	w0, 0x100008a6c <__text+0x2a1c>
100008a38:     	mov	w8, #0x102              ; =258
100008a3c:     	movk	w8, #0x840, lsl #16
100008a40:     	str	w8, [sp]
100008a44:     	stur	x20, [sp, #0x4]
100008a48:     	adr	x0, 0x100000000    ; "����"
100008a4c:     	nop
100008a50:     	adr	x3, 0x1000ec700    ; "Modulation Timeline: %@"
100008a54:     	nop
100008a58:     	mov	x4, sp
100008a5c:     	mov	x1, x23
100008a60:     	mov	w2, #0x0                ; =0
100008a64:     	mov	w5, #0xc                ; =12
100008a68:     	bl	0x1000ab978 
100008a6c:     	mov	x0, x20
100008a70:     	bl	0x1000ba240    ; sel startDate
100008a74:     	bl	0x1000abccc 
100008a78:     	mov	x23, x0
100008a7c:     	mov	x0, x20
100008a80:     	mov	x2, x23
100008a84:     	bl	0x1000bbaa0    ; sel valueAtDate:
100008a88:     	bl	0x1000abccc 
100008a8c:     	mov	x24, x0
100008a90:     	bl	0x1000b05a0    ; sel doubleValue
100008a94:     	fmov	d8, d0
100008a98:     	bl	0x1000abdc8 
100008a9c:     	bl	0x1000abdbc 
100008aa0:     	mov	x0, x20
100008aa4:     	bl	0x1000ba240    ; sel startDate
100008aa8:     	bl	0x1000abccc 
100008aac:     	mov	x23, x0
100008ab0:     	nop
100008ab4:     	ldr	d9, 0x1000c6358 
100008ab8:     	fmov	d0, d9
100008abc:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100008ac0:     	bl	0x1000abccc 
100008ac4:     	mov	x24, x0
100008ac8:     	bl	0x1000abdbc 
100008acc:     	mov	x0, x20
100008ad0:     	bl	0x1000b0800    ; sel endDate
100008ad4:     	bl	0x1000abccc 
100008ad8:     	mov	x23, x0
100008adc:     	mov	x0, x24
100008ae0:     	mov	x2, x23
100008ae4:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
100008ae8:     	fmov	d10, d0
100008aec:     	bl	0x1000abdbc 
100008af0:     	fcmp	d10, #0.0
100008af4:     	b.pl	0x100008b64 <__text+0x2b14>
100008af8:     	mov	x0, x20
100008afc:     	mov	x2, x24
100008b00:     	bl	0x1000bbaa0    ; sel valueAtDate:
100008b04:     	bl	0x1000abccc 
100008b08:     	mov	x23, x0
100008b0c:     	bl	0x1000b05a0    ; sel doubleValue
100008b10:     	fadd	d8, d8, d0
100008b14:     	bl	0x1000abdbc 
100008b18:     	mov	x0, x24
100008b1c:     	fmov	d0, d9
100008b20:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100008b24:     	bl	0x1000abccc 
100008b28:     	mov	x23, x0
100008b2c:     	bl	0x1000abdc8 
100008b30:     	mov	x0, x20
100008b34:     	bl	0x1000b0800    ; sel endDate
100008b38:     	bl	0x1000abccc 
100008b3c:     	mov	x24, x0
100008b40:     	mov	x0, x23
100008b44:     	mov	x2, x24
100008b48:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
100008b4c:     	fmov	d10, d0
100008b50:     	bl	0x1000abdc8 
100008b54:     	fcmp	d10, #0.0
100008b58:     	mov	x24, x23
100008b5c:     	b.mi	0x100008af8 <__text+0x2aa8>
100008b60:     	b	0x100008b68 <__text+0x2b18>
100008b64:     	mov	x23, x24
100008b68:     	mov	x0, x20
100008b6c:     	bl	0x1000ba240    ; sel startDate
100008b70:     	bl	0x1000abccc 
100008b74:     	mov	x24, x0
100008b78:     	mov	x0, x19
100008b7c:     	mov	x2, x24
100008b80:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
100008b84:     	fmov	d9, d0
100008b88:     	bl	0x1000abdc8 
100008b8c:     	fcmp	d9, #0.0
100008b90:     	b.pl	0x100008be4 <__text+0x2b94>
100008b94:     	nop
100008b98:     	ldr	d9, 0x1000c6360 
100008b9c:     	mov	x24, x19
100008ba0:     	mov	x0, x19
100008ba4:     	fmov	d0, d9
100008ba8:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100008bac:     	bl	0x1000abccc 
100008bb0:     	mov	x19, x0
100008bb4:     	bl	0x1000abdc8 
100008bb8:     	mov	x0, x20
100008bbc:     	bl	0x1000ba240    ; sel startDate
100008bc0:     	bl	0x1000abccc 
100008bc4:     	mov	x24, x0
100008bc8:     	mov	x0, x19
100008bcc:     	mov	x2, x24
100008bd0:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
100008bd4:     	fmov	d10, d0
100008bd8:     	bl	0x1000abdc8 
100008bdc:     	fcmp	d10, #0.0
100008be0:     	b.mi	0x100008b9c <__text+0x2b4c>
100008be4:     	ldr	x22, [x22, #0x60]
100008be8:     	mov	x0, x22
100008bec:     	mov	w1, #0x1                ; =1
100008bf0:     	bl	0x1000abf9c 
100008bf4:     	cbz	w0, 0x100008c84 <__text+0x2c34>
100008bf8:     	adrp	x8, 0x100132000 
100008bfc:     	ldr	x24, [x8, #0xab8]
100008c00:     	bl	0x1000abe94 
100008c04:     	mov	x22, x0
100008c08:     	mov	x0, x24
100008c0c:     	fmov	d0, d8
100008c10:     	bl	0x1000b4f80    ; sel numberWithDouble:
100008c14:     	bl	0x1000abccc 
100008c18:     	mov	x24, x0
100008c1c:     	mov	x0, x20
100008c20:     	mov	x2, x19
100008c24:     	bl	0x1000bbaa0    ; sel valueAtDate:
100008c28:     	bl	0x1000abccc 
100008c2c:     	mov	x25, x0
100008c30:     	mov	w8, #0x302              ; =770
100008c34:     	movk	w8, #0x840, lsl #16
100008c38:     	str	w8, [sp]
100008c3c:     	stur	x24, [sp, #0x4]
100008c40:     	mov	w8, #0x840              ; =2112
100008c44:     	strh	w8, [sp, #0xc]
100008c48:     	stur	x19, [sp, #0xe]
100008c4c:     	strh	w8, [sp, #0x16]
100008c50:     	str	x0, [sp, #0x18]
100008c54:     	adr	x0, 0x100000000    ; "����"
100008c58:     	nop
100008c5c:     	adr	x3, 0x1000ec718    ; "All Likelihoods: %@, Value at %@: %@"
100008c60:     	nop
100008c64:     	mov	x4, sp
100008c68:     	mov	x1, x22
100008c6c:     	mov	w2, #0x1                ; =1
100008c70:     	mov	w5, #0x20               ; =32
100008c74:     	bl	0x1000ab978 
100008c78:     	bl	0x1000abdc8 
100008c7c:     	bl	0x1000abdd4 
100008c80:     	bl	0x1000abdb0 
100008c84:     	fcmp	d8, #0.0
100008c88:     	b.ne	0x100008cb0 <__text+0x2c60>
100008c8c:     	adrp	x8, 0x100137000 
100008c90:     	ldrb	w8, [x8, #0xc78]
100008c94:     	cmp	w8, #0x0
100008c98:     	mov	x8, #0x7ff0000000000000 ; =9218868437227405312
100008c9c:     	fmov	d0, x8
100008ca0:     	nop
100008ca4:     	ldr	d1, 0x1000c6368 
100008ca8:     	fcsel	d8, d1, d0, ne
100008cac:     	b	0x100008cd0 <__text+0x2c80>
100008cb0:     	mov	x0, x20
100008cb4:     	mov	x2, x19
100008cb8:     	bl	0x1000bbaa0    ; sel valueAtDate:
100008cbc:     	bl	0x1000abccc 
100008cc0:     	mov	x22, x0
100008cc4:     	bl	0x1000b05a0    ; sel doubleValue
100008cc8:     	fdiv	d8, d0, d8
100008ccc:     	bl	0x1000abdb0 
100008cd0:     	bl	0x1000abdbc 
100008cd4:     	mov	x0, x21
100008cd8:     	bl	0x1000abc9c 
100008cdc:     	bl	0x1000abd98 
100008ce0:     	bl	0x1000abd8c 
100008ce4:     	ldr	x8, [sp, #0x28]
100008ce8:     	nop
100008cec:     	ldr	x9, 0x1000fc508 
100008cf0:     	ldr	x9, [x9]
100008cf4:     	cmp	x9, x8
100008cf8:     	b.ne	0x100008d24 <__text+0x2cd4>
100008cfc:     	fmov	d0, d8
100008d00:     	ldp	x29, x30, [sp, #0x90]
100008d04:     	ldp	x20, x19, [sp, #0x80]
100008d08:     	ldp	x22, x21, [sp, #0x70]
100008d0c:     	ldp	x24, x23, [sp, #0x60]
100008d10:     	ldp	x26, x25, [sp, #0x50]
100008d14:     	ldp	d9, d8, [sp, #0x40]
100008d18:     	ldp	d11, d10, [sp, #0x30]
100008d1c:     	add	sp, sp, #0xa0
100008d20:     	ret
100008d24:     	bl	0x1000ab90c 
