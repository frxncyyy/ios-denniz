
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4a84 <_nwpd_observe>:
fffffe00079b4a84:      	sub	sp, sp, #128
fffffe00079b4a88:      	stp	x29, x30, [sp, #112]
fffffe00079b4a8c:      	add	x29, sp, #112
fffffe00079b4a90:      	cmp	x4, #640
fffffe00079b4a94:      	b.hi	0xfffffe00079b4c00 <_nwpd_observe+0x17c>
fffffe00079b4a98:      	cmp	x3, #0
fffffe00079b4a9c:      	ccmp	x4, #0, #4, eq
fffffe00079b4aa0:      	cset	w9, ne
fffffe00079b4aa4:      	cmp	w6, #14
fffffe00079b4aa8:      	b.hi	0xfffffe00079b4c00 <_nwpd_observe+0x17c>
fffffe00079b4aac:      	cmp	w2, #5
fffffe00079b4ab0:      	b.hi	0xfffffe00079b4c00 <_nwpd_observe+0x17c>
fffffe00079b4ab4:      	tbnz	w9, #0, 0xfffffe00079b4c00 <_nwpd_observe+0x17c>
fffffe00079b4ab8:      	sub	w9, w5, #5
fffffe00079b4abc:      	cmn	w9, #6
fffffe00079b4ac0:      	b.lo	0xfffffe00079b4c00 <_nwpd_observe+0x17c>
fffffe00079b4ac4:      	adrp	x15, 0xfffffe000852f000 <_sysctl_sysreg_ID_AA64ISAR1_EL1+0x4>
fffffe00079b4ac8:      	ldr	w10, [x15, #4092]
fffffe00079b4acc:      	cmp	w10, #39
fffffe00079b4ad0:      	b.hi	0xfffffe00079b4c00 <_nwpd_observe+0x17c>
fffffe00079b4ad4:      	cmp	w5, #0
fffffe00079b4ad8:      	mov	w9, #4
fffffe00079b4adc:      	cset	w14, ge
fffffe00079b4ae0:      	cmp	w2, #5
fffffe00079b4ae4:      	cset	w16, eq
fffffe00079b4ae8:      	mov	w17, #8
fffffe00079b4aec:      	tst	w16, w14
fffffe00079b4af0:      	adrp	x12, 0xfffffe000852f000 <_sysctl_sysreg_ID_AA64ISAR1_EL1+0x30>
fffffe00079b4af4:      	add	x12, x12, #4080
fffffe00079b4af8:      	csel	x9, x9, xzr, ne
fffffe00079b4afc:      	cmp	w5, #0
fffffe00079b4b00:      	csel	x11, x17, x9, lt
fffffe00079b4b04:      	mov	w9, #2
fffffe00079b4b08:      	csel	w9, w9, w16, lt
fffffe00079b4b0c:      	tst	w16, w14
fffffe00079b4b10:      	mov	w14, #24
fffffe00079b4b14:      	ldr	w13, [x12, x11]
fffffe00079b4b18:      	csel	w14, w14, w17, ne
fffffe00079b4b1c:      	cmp	w13, w14
fffffe00079b4b20:      	b.hs	0xfffffe00079b4c00 <_nwpd_observe+0x17c>
fffffe00079b4b24:      	mov	x8, x0
fffffe00079b4b28:      	add	w10, w10, #1
fffffe00079b4b2c:      	add	w14, w5, #1
fffffe00079b4b30:      	cmp	x4, #4
fffffe00079b4b34:      	str	w10, [x15, #4092]
fffffe00079b4b38:      	b.lo	0xfffffe00079b4b94 <_nwpd_observe+0x110>
fffffe00079b4b3c:      	ldrh	w15, [x3, #2]
fffffe00079b4b40:      	mov	w17, wzr
fffffe00079b4b44:      	ldrb	w16, [x3]
fffffe00079b4b48:      	cmp	x4, #7
fffffe00079b4b4c:      	ldrb	w0, [x3, #1]
fffffe00079b4b50:      	rev16	w15, w15
fffffe00079b4b54:      	b.lo	0xfffffe00079b4bac <_nwpd_observe+0x128>
fffffe00079b4b58:      	cmp	w0, #3
fffffe00079b4b5c:      	b.ne	0xfffffe00079b4bac <_nwpd_observe+0x128>
fffffe00079b4b60:      	ldrb	w0, [x3, #6]
fffffe00079b4b64:      	ldrb	w3, [x3, #5]
fffffe00079b4b68:      	mov	w17, w0
fffffe00079b4b6c:      	bfi	w17, w3, #8, #8
fffffe00079b4b70:      	cmp	w17, #138
fffffe00079b4b74:      	b.eq	0xfffffe00079b4bb4 <_nwpd_observe+0x130>
fffffe00079b4b78:      	mov	w3, #5066
fffffe00079b4b7c:      	cmp	w17, w3
fffffe00079b4b80:      	b.ne	0xfffffe00079b4bbc <_nwpd_observe+0x138>
fffffe00079b4b84:      	mov	w0, #3
fffffe00079b4b88:      	mov	w17, #5066
fffffe00079b4b8c:      	mov	w3, w0
fffffe00079b4b90:      	b	0xfffffe00079b4bcc <_nwpd_observe+0x148>
fffffe00079b4b94:      	mov	w15, wzr
fffffe00079b4b98:      	mov	w0, wzr
fffffe00079b4b9c:      	mov	w16, wzr
fffffe00079b4ba0:      	mov	w17, wzr
fffffe00079b4ba4:      	mov	w3, wzr
fffffe00079b4ba8:      	b	0xfffffe00079b4bcc <_nwpd_observe+0x148>
fffffe00079b4bac:      	mov	w3, w17
fffffe00079b4bb0:      	b	0xfffffe00079b4bcc <_nwpd_observe+0x148>
fffffe00079b4bb4:      	mov	w3, #1
fffffe00079b4bb8:      	b	0xfffffe00079b4bc8 <_nwpd_observe+0x144>
fffffe00079b4bbc:      	tst	w0, #0x8
fffffe00079b4bc0:      	mov	w0, #5
fffffe00079b4bc4:      	csel	w3, w0, wzr, eq
fffffe00079b4bc8:      	mov	w0, #3
fffffe00079b4bcc:      	add	w13, w13, #1
fffffe00079b4bd0:      	stp	x0, x15, [sp, #56]
fffffe00079b4bd4:      	adrp	x0, 0xfffffe0007134000 <_nwpc_ptk+0x22c>
fffffe00079b4bd8:      	add	x0, x0, #3109
fffffe00079b4bdc:      	str	w13, [x12, x11]
fffffe00079b4be0:      	mov	w11, w16
fffffe00079b4be4:      	stp	x14, x6, [sp, #88]
fffffe00079b4be8:      	stp	x17, x3, [sp, #72]
fffffe00079b4bec:      	stp	x4, x11, [sp, #40]
fffffe00079b4bf0:      	stp	x9, x2, [sp, #24]
fffffe00079b4bf4:      	stp	x8, x1, [sp, #8]
fffffe00079b4bf8:      	str	x10, [sp]
fffffe00079b4bfc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4c00:      	ldp	x29, x30, [sp, #112]
fffffe00079b4c04:      	add	sp, sp, #128
fffffe00079b4c08:      	ret
