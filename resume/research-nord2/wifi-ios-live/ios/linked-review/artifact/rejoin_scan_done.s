
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007522980 <_njs_rejoin_scan_done>:
fffffe0007522980:      	sub	sp, sp, #96
fffffe0007522984:      	stp	x24, x23, [sp, #32]
fffffe0007522988:      	stp	x22, x21, [sp, #48]
fffffe000752298c:      	stp	x20, x19, [sp, #64]
fffffe0007522990:      	stp	x29, x30, [sp, #80]
fffffe0007522994:      	add	x29, sp, #80
fffffe0007522998:      	movi.2d	v0, #0000000000000000
fffffe000752299c:      	mov	x20, x1
fffffe00075229a0:      	mov	x22, x0
fffffe00075229a4:      	mov	x1, sp
fffffe00075229a8:      	mov	x0, x2
fffffe00075229ac:      	mov	w21, w3
fffffe00075229b0:      	mov	x19, x2
fffffe00075229b4:      	stp	q0, q0, [sp]
fffffe00075229b8:      	bl	0xfffffe0007511f20 <_nord2_wifi_scan_view>
fffffe00075229bc:      	mov	w24, wzr
fffffe00075229c0:      	cbnz	w0, 0xfffffe0007522a3c <_njs_rejoin_scan_done+0xbc>
fffffe00075229c4:      	ldr	x8, [sp]
fffffe00075229c8:      	cbz	x8, 0xfffffe0007522a3c <_njs_rejoin_scan_done+0xbc>
fffffe00075229cc:      	ldr	w9, [x8]
fffffe00075229d0:      	cbz	w9, 0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe00075229d4:      	ldr	w9, [x8, #8]
fffffe00075229d8:      	cbz	w9, 0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe00075229dc:      	ldr	w9, [x8, #4]
fffffe00075229e0:      	cbnz	w9, 0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe00075229e4:      	ldr	w9, [x8, #12]
fffffe00075229e8:      	cbnz	w9, 0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe00075229ec:      	ldrb	w9, [x8, #65]
fffffe00075229f0:      	cmp	w9, #7
fffffe00075229f4:      	b.ne	0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe00075229f8:      	ldrb	w9, [x8, #64]
fffffe00075229fc:      	ldrb	w10, [x8, #32]
fffffe0007522a00:      	cmp	w9, w10
fffffe0007522a04:      	b.ne	0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe0007522a08:      	ldr	w9, [x8, #20]
fffffe0007522a0c:      	cmp	w9, #16
fffffe0007522a10:      	b.hi	0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe0007522a14:      	ldr	w10, [x8, #28]
fffffe0007522a18:      	subs	w11, w10, w9
fffffe0007522a1c:      	b.lo	0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe0007522a20:      	ldr	w10, [x8, #24]
fffffe0007522a24:      	cmp	w10, w11
fffffe0007522a28:      	b.hi	0xfffffe0007522a38 <_njs_rejoin_scan_done+0xb8>
fffffe0007522a2c:      	ldp	x12, x11, [x8, #40]
fffffe0007522a30:      	cmp	x11, x12
fffffe0007522a34:      	b.hs	0xfffffe0007522b5c <_njs_rejoin_scan_done+0x1dc>
fffffe0007522a38:      	mov	w24, wzr
fffffe0007522a3c:      	adrp	x23, 0xfffffe00084dd000 <_receipt+0x280>
fffffe0007522a40:      	ldr	x0, [x23, #2456]
fffffe0007522a44:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007522a48:      	adrp	x8, 0xfffffe0008516000 <_rr+0x94>
fffffe0007522a4c:      	add	x8, x8, #1984
fffffe0007522a50:      	cmp	x22, x8
fffffe0007522a54:      	b.ne	0xfffffe0007522ac8 <_njs_rejoin_scan_done+0x148>
fffffe0007522a58:      	ldr	w9, [x8, #52]
fffffe0007522a5c:      	cmp	w9, #2
fffffe0007522a60:      	b.ne	0xfffffe0007522ac8 <_njs_rejoin_scan_done+0x148>
fffffe0007522a64:      	ldr	x9, [x8]
fffffe0007522a68:      	cmp	x9, x20
fffffe0007522a6c:      	b.ne	0xfffffe0007522ac8 <_njs_rejoin_scan_done+0x148>
fffffe0007522a70:      	ldr	x8, [x8, #16]
fffffe0007522a74:      	cmp	x8, x19
fffffe0007522a78:      	b.ne	0xfffffe0007522ac8 <_njs_rejoin_scan_done+0x148>
fffffe0007522a7c:      	cmp	w21, #1
fffffe0007522a80:      	eor	w9, w24, #0x1
fffffe0007522a84:      	cset	w8, hi
fffffe0007522a88:      	orr	w9, w8, w9
fffffe0007522a8c:      	mov	w8, wzr
fffffe0007522a90:      	tbnz	w9, #0, 0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522a94:      	ldr	x9, [sp, #16]
fffffe0007522a98:      	cmp	x9, x20
fffffe0007522a9c:      	b.ne	0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522aa0:      	adrp	x8, 0xfffffe0008516000 <_coherent+0x44>
fffffe0007522aa4:      	ldr	x10, [sp, #8]
fffffe0007522aa8:      	ldr	x9, [x8, #1992]
fffffe0007522aac:      	cmp	x10, x9
fffffe0007522ab0:      	b.hs	0xfffffe0007522af0 <_njs_rejoin_scan_done+0x170>
fffffe0007522ab4:      	mov	w8, wzr
fffffe0007522ab8:      	mov	w9, #3
fffffe0007522abc:      	adrp	x10, 0xfffffe0008516000 <_coherent+0x60>
fffffe0007522ac0:      	add	x10, x10, #2036
fffffe0007522ac4:      	stp	w9, w8, [x10]
fffffe0007522ac8:      	ldr	x0, [x23, #2456]
fffffe0007522acc:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007522ad0:      	mov	x0, x19
fffffe0007522ad4:      	bl	0xfffffe000751203c <_nord2_wifi_scan_release>
fffffe0007522ad8:      	ldp	x29, x30, [sp, #80]
fffffe0007522adc:      	ldp	x20, x19, [sp, #64]
fffffe0007522ae0:      	ldp	x22, x21, [sp, #48]
fffffe0007522ae4:      	ldp	x24, x23, [sp, #32]
fffffe0007522ae8:      	add	sp, sp, #96
fffffe0007522aec:      	ret
fffffe0007522af0:      	ldr	x8, [sp]
fffffe0007522af4:      	ldr	x10, [x8, #40]
fffffe0007522af8:      	mov	w8, wzr
fffffe0007522afc:      	cmp	x10, x9
fffffe0007522b00:      	b.lo	0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522b04:      	adrp	x9, 0xfffffe0008528000 <_coherent+0xf0>
fffffe0007522b08:      	ldr	w9, [x9, #3704]
fffffe0007522b0c:      	cbz	w9, 0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522b10:      	adrp	x9, 0xfffffe0008528000 <_coherent+0xfc>
fffffe0007522b14:      	ldr	w9, [x9, #3228]
fffffe0007522b18:      	cbnz	w9, 0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522b1c:      	adrp	x0, 0xfffffe0008526000 <_coherent+0x100>
fffffe0007522b20:      	add	x0, x0, #2680
fffffe0007522b24:      	bl	0xfffffe000799c814 <_nct_done_receipt>
fffffe0007522b28:      	mov	w8, wzr
fffffe0007522b2c:      	cbz	w0, 0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522b30:      	adrp	x9, 0xfffffe000852a000 <_coherent+0x124>
fffffe0007522b34:      	add	x9, x9, #3704
fffffe0007522b38:      	ldr	w10, [x9]
fffffe0007522b3c:      	cbnz	w10, 0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522b40:      	ldr	w9, [x9, #12]
fffffe0007522b44:      	cbnz	w9, 0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522b48:      	adrp	x8, 0xfffffe000852a000 <_syncmem+0x14>
fffffe0007522b4c:      	ldr	w8, [x8, #3720]
fffffe0007522b50:      	cmp	w8, #0
fffffe0007522b54:      	cset	w8, eq
fffffe0007522b58:      	b	0xfffffe0007522ab8 <_njs_rejoin_scan_done+0x138>
fffffe0007522b5c:      	ldr	x13, [x8, #56]
fffffe0007522b60:      	mov	w24, wzr
fffffe0007522b64:      	cmp	x13, x12
fffffe0007522b68:      	b.ls	0xfffffe0007522a3c <_njs_rejoin_scan_done+0xbc>
fffffe0007522b6c:      	cmp	x11, x13
fffffe0007522b70:      	b.hs	0xfffffe0007522a3c <_njs_rejoin_scan_done+0xbc>
fffffe0007522b74:      	ldr	w8, [x8, #16]
fffffe0007522b78:      	cbz	w8, 0xfffffe0007522b88 <_njs_rejoin_scan_done+0x208>
fffffe0007522b7c:      	cmp	w10, #0
fffffe0007522b80:      	ccmp	w9, #16, #0, ne
fffffe0007522b84:      	b	0xfffffe0007522b8c <_njs_rejoin_scan_done+0x20c>
fffffe0007522b88:      	cmp	w10, #0
fffffe0007522b8c:      	cset	w24, eq
fffffe0007522b90:      	b	0xfffffe0007522a3c <_njs_rejoin_scan_done+0xbc>
