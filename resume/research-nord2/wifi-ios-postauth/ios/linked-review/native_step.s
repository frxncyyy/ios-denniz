
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079abdd4 <_nst_step>:
fffffe00079abdd4:      	sub	sp, sp, #160
fffffe00079abdd8:      	stp	x28, x27, [sp, #64]
fffffe00079abddc:      	stp	x26, x25, [sp, #80]
fffffe00079abde0:      	stp	x24, x23, [sp, #96]
fffffe00079abde4:      	stp	x22, x21, [sp, #112]
fffffe00079abde8:      	stp	x20, x19, [sp, #128]
fffffe00079abdec:      	stp	x29, x30, [sp, #144]
fffffe00079abdf0:      	add	x29, sp, #144
fffffe00079abdf4:      	cbz	x0, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abdf8:      	mov	x19, x0
fffffe00079abdfc:      	ldr	w8, [x0, #28]
fffffe00079abe00:      	cbz	w8, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe04:      	ldr	x8, [x19]
fffffe00079abe08:      	cbz	x8, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe0c:      	ldr	x9, [x19, #8]
fffffe00079abe10:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe14:      	ldr	x9, [x19, #16]
fffffe00079abe18:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe1c:      	ldr	w9, [x19, #44]
fffffe00079abe20:      	cbnz	w9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe24:      	ldr	w9, [x8, #12]
fffffe00079abe28:      	cbnz	w9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe2c:      	ldr	w8, [x8, #1876]
fffffe00079abe30:      	cbnz	w8, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe34:      	ldr	w8, [x19, #36]
fffffe00079abe38:      	cbz	w8, 0xfffffe00079abe60 <_nst_step+0x8c>
fffffe00079abe3c:      	mov	w0, #-1
fffffe00079abe40:      	ldp	x29, x30, [sp, #144]
fffffe00079abe44:      	ldp	x20, x19, [sp, #128]
fffffe00079abe48:      	ldp	x22, x21, [sp, #112]
fffffe00079abe4c:      	ldp	x24, x23, [sp, #96]
fffffe00079abe50:      	ldp	x26, x25, [sp, #80]
fffffe00079abe54:      	ldp	x28, x27, [sp, #64]
fffffe00079abe58:      	add	sp, sp, #160
fffffe00079abe5c:      	ret
fffffe00079abe60:      	mov	x24, x1
fffffe00079abe64:      	mov	w0, #-1
fffffe00079abe68:      	cbz	x1, 0xfffffe00079abe40 <_nst_step+0x6c>
fffffe00079abe6c:      	ldr	w8, [x19, #148]
fffffe00079abe70:      	cbnz	w8, 0xfffffe00079abe40 <_nst_step+0x6c>
fffffe00079abe74:      	ldr	x8, [x24]
fffffe00079abe78:      	cbz	x8, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe7c:      	ldr	x9, [x24, #8]
fffffe00079abe80:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe84:      	ldr	x9, [x8]
fffffe00079abe88:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe8c:      	ldr	x9, [x8, #8]
fffffe00079abe90:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe94:      	ldr	x9, [x8, #16]
fffffe00079abe98:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abe9c:      	ldr	x9, [x8, #24]
fffffe00079abea0:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abea4:      	ldr	x9, [x8, #32]
fffffe00079abea8:      	cbz	x9, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abeac:      	mov	w22, w3
fffffe00079abeb0:      	mov	w0, #-1
fffffe00079abeb4:      	sub	w9, w3, #17
fffffe00079abeb8:      	cmn	w9, #16
fffffe00079abebc:      	b.lo	0xfffffe00079abe40 <_nst_step+0x6c>
fffffe00079abec0:      	ldr	x8, [x8, #40]
fffffe00079abec4:      	cbz	x8, 0xfffffe00079abe40 <_nst_step+0x6c>
fffffe00079abec8:      	movi.2s	v0, #1
fffffe00079abecc:      	mov	x0, x19
fffffe00079abed0:      	mov	x1, x24
fffffe00079abed4:      	mov	x20, x5
fffffe00079abed8:      	mov	x21, x4
fffffe00079abedc:      	mov	x23, x2
fffffe00079abee0:      	str	wzr, [x19, #40]
fffffe00079abee4:      	str	d0, [x19, #32]
fffffe00079abee8:      	bl	0xfffffe00079ac068 <_before>
fffffe00079abeec:      	cbnz	w0, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abef0:      	mov	x0, x19
fffffe00079abef4:      	mov	x1, x24
fffffe00079abef8:      	mov	x2, x23
fffffe00079abefc:      	bl	0xfffffe00079ac180 <_coherent>
fffffe00079abf00:      	cbnz	w0, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abf04:      	mov	x0, x19
fffffe00079abf08:      	mov	x1, x24
fffffe00079abf0c:      	mov	x2, x23
fffffe00079abf10:      	mov	x3, x21
fffffe00079abf14:      	mov	x4, x20
fffffe00079abf18:      	bl	0xfffffe00079ac254 <_transmit>
fffffe00079abf1c:      	cbnz	w0, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abf20:      	mov	w25, wzr
fffffe00079abf24:      	adrp	x27, 0xfffffe0007031000 <_rate_ok+0x18>
fffffe00079abf28:      	add	x27, x27, #1692
fffffe00079abf2c:      	b	0xfffffe00079abf4c <_nst_step+0x178>
fffffe00079abf30:      	mov	w8, #1
fffffe00079abf34:      	cmp	w28, #0
fffffe00079abf38:      	mov	w9, #3
fffffe00079abf3c:      	csel	w9, w9, wzr, eq
fffffe00079abf40:      	cmp	w0, #0
fffffe00079abf44:      	csel	w8, w8, w9, lt
fffffe00079abf48:      	cbnz	w8, 0xfffffe00079abfa4 <_nst_step+0x1d0>
fffffe00079abf4c:      	cmp	w25, w22
fffffe00079abf50:      	b.hs	0xfffffe00079abfac <_nst_step+0x1d8>
fffffe00079abf54:      	mov	x26, xzr
fffffe00079abf58:      	mov	w28, wzr
fffffe00079abf5c:      	ldr	w3, [x27, x26, lsl #2]
fffffe00079abf60:      	mov	x0, x19
fffffe00079abf64:      	mov	x1, x24
fffffe00079abf68:      	mov	x2, x23
fffffe00079abf6c:      	mov	x4, x21
fffffe00079abf70:      	mov	x5, x20
fffffe00079abf74:      	bl	0xfffffe00079acb28 <_receive>
fffffe00079abf78:      	tbnz	w0, #31, 0xfffffe00079abf30 <_nst_step+0x15c>
fffffe00079abf7c:      	cmp	w0, #0
fffffe00079abf80:      	mov	w8, #4
fffffe00079abf84:      	cinc	w25, w25, ne
fffffe00079abf88:      	cinc	w28, w28, ne
fffffe00079abf8c:      	cmp	x26, #3
fffffe00079abf90:      	b.hi	0xfffffe00079abf34 <_nst_step+0x160>
fffffe00079abf94:      	add	x26, x26, #1
fffffe00079abf98:      	cmp	w25, w22
fffffe00079abf9c:      	b.lo	0xfffffe00079abf5c <_nst_step+0x188>
fffffe00079abfa0:      	b	0xfffffe00079abf34 <_nst_step+0x160>
fffffe00079abfa4:      	cmp	w8, #3
fffffe00079abfa8:      	b.ne	0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079abfac:      	ldr	w8, [x19, #60]
fffffe00079abfb0:      	cbz	w8, 0xfffffe00079ac044 <_nst_step+0x270>
fffffe00079abfb4:      	ldr	w8, [x19, #136]
fffffe00079abfb8:      	cbz	w8, 0xfffffe00079ac044 <_nst_step+0x270>
fffffe00079abfbc:      	ldr	w8, [x19, #140]
fffffe00079abfc0:      	cbz	w8, 0xfffffe00079ac028 <_nst_step+0x254>
fffffe00079abfc4:      	ldr	w8, [x19, #144]
fffffe00079abfc8:      	ldr	x9, [x19, #160]
fffffe00079abfcc:      	cmp	w8, #256
fffffe00079abfd0:      	stp	xzr, xzr, [sp, #40]
fffffe00079abfd4:      	strh	wzr, [sp, #32]
fffffe00079abfd8:      	stp	xzr, x9, [sp, #16]
fffffe00079abfdc:      	ldp	w10, w9, [x19, #120]
fffffe00079abfe0:      	str	wzr, [sp, #36]
fffffe00079abfe4:      	strb	wzr, [sp, #41]
fffffe00079abfe8:      	str	xzr, [sp, #56]
fffffe00079abfec:      	strb	w10, [sp, #34]
fffffe00079abff0:      	mov	w10, #2
fffffe00079abff4:      	strb	w9, [sp, #35]
fffffe00079abff8:      	str	wzr, [x19, #60]
fffffe00079abffc:      	stp	w10, w8, [sp, #8]
fffffe00079ac000:      	b.ne	0xfffffe00079ac00c <_nst_step+0x238>
fffffe00079ac004:      	mov	x0, x19
fffffe00079ac008:      	bl	0xfffffe00079aa740 <_nst_close_port>
fffffe00079ac00c:      	add	x3, sp, #8
fffffe00079ac010:      	mov	x0, x19
fffffe00079ac014:      	mov	x1, x21
fffffe00079ac018:      	mov	x2, x20
fffffe00079ac01c:      	bl	0xfffffe00079acf50 <_emit>
fffffe00079ac020:      	cbnz	w0, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079ac024:      	b	0xfffffe00079ac044 <_nst_step+0x270>
fffffe00079ac028:      	mov	x0, x19
fffffe00079ac02c:      	bl	0xfffffe00079acf0c <_retired_air_failure>
fffffe00079ac030:      	cbz	w0, 0xfffffe00079ac044 <_nst_step+0x270>
fffffe00079ac034:      	ldr	x8, [x19, #168]
fffffe00079ac038:      	ldr	x9, [x19, #184]
fffffe00079ac03c:      	cmp	x8, x9
fffffe00079ac040:      	b.hs	0xfffffe00079ac060 <_nst_step+0x28c>
fffffe00079ac044:      	mov	x0, x19
fffffe00079ac048:      	mov	x1, x21
fffffe00079ac04c:      	mov	x2, x20
fffffe00079ac050:      	bl	0xfffffe00079acfb8 <_peer_progress>
fffffe00079ac054:      	cbnz	w0, 0xfffffe00079abe3c <_nst_step+0x68>
fffffe00079ac058:      	stp	wzr, wzr, [x19, #36]
fffffe00079ac05c:      	b	0xfffffe00079abe40 <_nst_step+0x6c>
fffffe00079ac060:      	mov	w8, #256
fffffe00079ac064:      	b	0xfffffe00079abfc8 <_nst_step+0x1f4>
