
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079afbd4 <_nsdd_publish>:
fffffe00079afbd4:      	sub	sp, sp, #144
fffffe00079afbd8:      	stp	x22, x21, [sp, #96]
fffffe00079afbdc:      	stp	x20, x19, [sp, #112]
fffffe00079afbe0:      	stp	x29, x30, [sp, #128]
fffffe00079afbe4:      	add	x29, sp, #128
fffffe00079afbe8:      	cbz	x0, 0xfffffe00079afd94 <_nsdd_publish+0x1c0>
fffffe00079afbec:      	mov	w20, w3
fffffe00079afbf0:      	sub	w8, w3, #4
fffffe00079afbf4:      	cmn	w8, #2
fffffe00079afbf8:      	b.lo	0xfffffe00079afd94 <_nsdd_publish+0x1c0>
fffffe00079afbfc:      	mov	x21, x0
fffffe00079afc00:      	ldr	w8, [x0, #192]
fffffe00079afc04:      	cbnz	w8, 0xfffffe00079afd94 <_nsdd_publish+0x1c0>
fffffe00079afc08:      	mov	w22, w2
fffffe00079afc0c:      	mov	x19, x1
fffffe00079afc10:      	sub	x0, x29, #56
fffffe00079afc14:      	mov	x1, x4
fffffe00079afc18:      	mov	x2, x5
fffffe00079afc1c:      	stp	xzr, xzr, [x29, #-56]
fffffe00079afc20:      	stur	wzr, [x29, #-40]
fffffe00079afc24:      	bl	0xfffffe00079af84c <_nsdd_classify>
fffffe00079afc28:      	cbnz	w0, 0xfffffe00079afd94 <_nsdd_publish+0x1c0>
fffffe00079afc2c:      	ldur	w8, [x29, #-48]
fffffe00079afc30:      	mov	w9, #28
fffffe00079afc34:      	mov	w10, #24
fffffe00079afc38:      	cmp	w8, #0
fffffe00079afc3c:      	csel	x9, x10, x9, eq
fffffe00079afc40:      	ldr	w10, [x21, x9]
fffffe00079afc44:      	cmn	w10, #1
fffffe00079afc48:      	b.eq	0xfffffe00079afc54 <_nsdd_publish+0x80>
fffffe00079afc4c:      	add	w10, w10, #1
fffffe00079afc50:      	str	w10, [x21, x9]
fffffe00079afc54:      	ldur	w9, [x29, #-56]
fffffe00079afc58:      	cmp	w9, #2048
fffffe00079afc5c:      	b.eq	0xfffffe00079afc7c <_nsdd_publish+0xa8>
fffffe00079afc60:      	cmp	w9, #2054
fffffe00079afc64:      	b.eq	0xfffffe00079afc84 <_nsdd_publish+0xb0>
fffffe00079afc68:      	mov	w10, #34525
fffffe00079afc6c:      	cmp	w9, w10
fffffe00079afc70:      	b.ne	0xfffffe00079afc8c <_nsdd_publish+0xb8>
fffffe00079afc74:      	mov	w10, #2
fffffe00079afc78:      	b	0xfffffe00079afc9c <_nsdd_publish+0xc8>
fffffe00079afc7c:      	mov	x10, xzr
fffffe00079afc80:      	b	0xfffffe00079afc9c <_nsdd_publish+0xc8>
fffffe00079afc84:      	mov	w10, #1
fffffe00079afc88:      	b	0xfffffe00079afc9c <_nsdd_publish+0xc8>
fffffe00079afc8c:      	mov	w10, #34958
fffffe00079afc90:      	cmp	w9, w10
fffffe00079afc94:      	mov	w10, #3
fffffe00079afc98:      	cinc	x10, x10, ne
fffffe00079afc9c:      	add	x10, x21, x10, lsl #2
fffffe00079afca0:      	ldr	w11, [x10, #32]!
fffffe00079afca4:      	cmn	w11, #1
fffffe00079afca8:      	b.eq	0xfffffe00079afcb4 <_nsdd_publish+0xe0>
fffffe00079afcac:      	add	w11, w11, #1
fffffe00079afcb0:      	str	w11, [x10]
fffffe00079afcb4:      	cmp	w20, #2
fffffe00079afcb8:      	stp	w20, w22, [x21, #156]
fffffe00079afcbc:      	b.ne	0xfffffe00079afcd4 <_nsdd_publish+0x100>
fffffe00079afcc0:      	ldr	w10, [x21]
fffffe00079afcc4:      	mov	x11, x21
fffffe00079afcc8:      	cmn	w10, #1
fffffe00079afccc:      	b.ne	0xfffffe00079afce4 <_nsdd_publish+0x110>
fffffe00079afcd0:      	b	0xfffffe00079afcec <_nsdd_publish+0x118>
fffffe00079afcd4:      	mov	x11, x21
fffffe00079afcd8:      	ldr	w10, [x11, #4]!
fffffe00079afcdc:      	cmn	w10, #1
fffffe00079afce0:      	b.eq	0xfffffe00079afcec <_nsdd_publish+0x118>
fffffe00079afce4:      	add	w10, w10, #1
fffffe00079afce8:      	str	w10, [x11]
fffffe00079afcec:      	ldur	w10, [x29, #-44]
fffffe00079afcf0:      	cbz	w10, 0xfffffe00079afd28 <_nsdd_publish+0x154>
fffffe00079afcf4:      	ldr	w11, [x21, #16]
fffffe00079afcf8:      	cmn	w11, #1
fffffe00079afcfc:      	b.eq	0xfffffe00079afd08 <_nsdd_publish+0x134>
fffffe00079afd00:      	add	w11, w11, #1
fffffe00079afd04:      	str	w11, [x21, #16]
fffffe00079afd08:      	add	x12, x21, x10, lsl #2
fffffe00079afd0c:      	ldr	w11, [x12, #72]!
fffffe00079afd10:      	cmn	w11, #1
fffffe00079afd14:      	b.eq	0xfffffe00079afd30 <_nsdd_publish+0x15c>
fffffe00079afd18:      	add	w13, w11, #1
fffffe00079afd1c:      	mov	w11, #12
fffffe00079afd20:      	str	w13, [x12]
fffffe00079afd24:      	b	0xfffffe00079afd34 <_nsdd_publish+0x160>
fffffe00079afd28:      	mov	w11, #4
fffffe00079afd2c:      	b	0xfffffe00079afd34 <_nsdd_publish+0x160>
fffffe00079afd30:      	mov	w11, #12
fffffe00079afd34:      	cmp	w10, #0
fffffe00079afd38:      	mov	w12, #172
fffffe00079afd3c:      	mov	w13, #164
fffffe00079afd40:      	csel	x12, x13, x12, eq
fffffe00079afd44:      	ldr	w13, [x21, x12]
fffffe00079afd48:      	cmp	w13, w11
fffffe00079afd4c:      	b.hs	0xfffffe00079afd94 <_nsdd_publish+0x1c0>
fffffe00079afd50:      	ldr	w11, [x21, #188]
fffffe00079afd54:      	add	w13, w13, #1
fffffe00079afd58:      	ldur	w15, [x29, #-40]
fffffe00079afd5c:      	cmp	w20, #2
fffffe00079afd60:      	ldur	w14, [x29, #-52]
fffffe00079afd64:      	str	w13, [x21, x12]
fffffe00079afd68:      	add	w11, w11, #1
fffffe00079afd6c:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x28c>
fffffe00079afd70:      	add	x0, x0, #3006
fffffe00079afd74:      	str	w11, [x21, #188]
fffffe00079afd78:      	stp	x10, x15, [sp, #48]
fffffe00079afd7c:      	mov	w10, #1
fffffe00079afd80:      	stp	x9, x14, [sp, #32]
fffffe00079afd84:      	cinc	x9, x10, ne
fffffe00079afd88:      	stp	x11, x19, [sp]
fffffe00079afd8c:      	stp	x9, x8, [sp, #16]
fffffe00079afd90:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079afd94:      	ldp	x29, x30, [sp, #128]
fffffe00079afd98:      	ldp	x20, x19, [sp, #112]
fffffe00079afd9c:      	ldp	x22, x21, [sp, #96]
fffffe00079afda0:      	add	sp, sp, #144
fffffe00079afda4:      	ret
