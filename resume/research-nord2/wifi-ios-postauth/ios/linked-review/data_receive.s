
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079afda8 <_nsdd_receive>:
fffffe00079afda8:      	sub	sp, sp, #128
fffffe00079afdac:      	stp	x20, x19, [sp, #96]
fffffe00079afdb0:      	stp	x29, x30, [sp, #112]
fffffe00079afdb4:      	add	x29, sp, #112
fffffe00079afdb8:      	cbz	x0, 0xfffffe00079aff10 <_nsdd_receive+0x168>
fffffe00079afdbc:      	mov	x20, x0
fffffe00079afdc0:      	ldr	w8, [x0, #192]
fffffe00079afdc4:      	cbnz	w8, 0xfffffe00079aff10 <_nsdd_receive+0x168>
fffffe00079afdc8:      	mov	x19, x1
fffffe00079afdcc:      	sub	x0, x29, #40
fffffe00079afdd0:      	mov	x1, x2
fffffe00079afdd4:      	mov	x2, x3
fffffe00079afdd8:      	stp	xzr, xzr, [x29, #-40]
fffffe00079afddc:      	stur	wzr, [x29, #-24]
fffffe00079afde0:      	bl	0xfffffe00079af84c <_nsdd_classify>
fffffe00079afde4:      	cbnz	w0, 0xfffffe00079aff10 <_nsdd_receive+0x168>
fffffe00079afde8:      	ldur	w8, [x29, #-40]
fffffe00079afdec:      	cmp	w8, #2048
fffffe00079afdf0:      	b.eq	0xfffffe00079afe10 <_nsdd_receive+0x68>
fffffe00079afdf4:      	cmp	w8, #2054
fffffe00079afdf8:      	b.eq	0xfffffe00079afe18 <_nsdd_receive+0x70>
fffffe00079afdfc:      	mov	w9, #34525
fffffe00079afe00:      	cmp	w8, w9
fffffe00079afe04:      	b.ne	0xfffffe00079afe20 <_nsdd_receive+0x78>
fffffe00079afe08:      	mov	w9, #2
fffffe00079afe0c:      	b	0xfffffe00079afe30 <_nsdd_receive+0x88>
fffffe00079afe10:      	mov	x9, xzr
fffffe00079afe14:      	b	0xfffffe00079afe30 <_nsdd_receive+0x88>
fffffe00079afe18:      	mov	w9, #1
fffffe00079afe1c:      	b	0xfffffe00079afe30 <_nsdd_receive+0x88>
fffffe00079afe20:      	mov	w9, #34958
fffffe00079afe24:      	cmp	w8, w9
fffffe00079afe28:      	mov	w9, #3
fffffe00079afe2c:      	cinc	x9, x9, ne
fffffe00079afe30:      	add	x9, x20, x9, lsl #2
fffffe00079afe34:      	ldr	w10, [x9, #52]!
fffffe00079afe38:      	cmn	w10, #1
fffffe00079afe3c:      	b.eq	0xfffffe00079afe48 <_nsdd_receive+0xa0>
fffffe00079afe40:      	add	w10, w10, #1
fffffe00079afe44:      	str	w10, [x9]
fffffe00079afe48:      	ldur	w9, [x29, #-32]
fffffe00079afe4c:      	mov	w10, #12
fffffe00079afe50:      	mov	w11, #8
fffffe00079afe54:      	cmp	w9, #0
fffffe00079afe58:      	csel	x10, x11, x10, eq
fffffe00079afe5c:      	ldr	w11, [x20, x10]
fffffe00079afe60:      	cmn	w11, #1
fffffe00079afe64:      	b.eq	0xfffffe00079afe70 <_nsdd_receive+0xc8>
fffffe00079afe68:      	add	w11, w11, #1
fffffe00079afe6c:      	str	w11, [x20, x10]
fffffe00079afe70:      	ldur	w10, [x29, #-28]
fffffe00079afe74:      	cbz	w10, 0xfffffe00079afeac <_nsdd_receive+0x104>
fffffe00079afe78:      	ldr	w11, [x20, #20]
fffffe00079afe7c:      	cmn	w11, #1
fffffe00079afe80:      	b.eq	0xfffffe00079afe8c <_nsdd_receive+0xe4>
fffffe00079afe84:      	add	w11, w11, #1
fffffe00079afe88:      	str	w11, [x20, #20]
fffffe00079afe8c:      	add	x12, x20, x10, lsl #2
fffffe00079afe90:      	ldr	w11, [x12, #108]!
fffffe00079afe94:      	cmn	w11, #1
fffffe00079afe98:      	b.eq	0xfffffe00079afeb4 <_nsdd_receive+0x10c>
fffffe00079afe9c:      	add	w13, w11, #1
fffffe00079afea0:      	mov	w11, #12
fffffe00079afea4:      	str	w13, [x12]
fffffe00079afea8:      	b	0xfffffe00079afeb8 <_nsdd_receive+0x110>
fffffe00079afeac:      	mov	w11, #4
fffffe00079afeb0:      	b	0xfffffe00079afeb8 <_nsdd_receive+0x110>
fffffe00079afeb4:      	mov	w11, #12
fffffe00079afeb8:      	cmp	w10, #0
fffffe00079afebc:      	mov	w12, #176
fffffe00079afec0:      	mov	w13, #168
fffffe00079afec4:      	csel	x12, x13, x12, eq
fffffe00079afec8:      	ldr	w13, [x20, x12]
fffffe00079afecc:      	cmp	w13, w11
fffffe00079afed0:      	b.hs	0xfffffe00079aff10 <_nsdd_receive+0x168>
fffffe00079afed4:      	ldr	w11, [x20, #188]
fffffe00079afed8:      	add	w13, w13, #1
fffffe00079afedc:      	ldur	w14, [x29, #-36]
fffffe00079afee0:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x400>
fffffe00079afee4:      	add	x0, x0, #3006
fffffe00079afee8:      	ldur	w15, [x29, #-24]
fffffe00079afeec:      	add	w11, w11, #1
fffffe00079afef0:      	str	w13, [x20, x12]
fffffe00079afef4:      	str	w11, [x20, #188]
fffffe00079afef8:      	stp	x8, x14, [sp, #32]
fffffe00079afefc:      	mov	w8, #3
fffffe00079aff00:      	stp	x10, x15, [sp, #48]
fffffe00079aff04:      	stp	x11, x19, [sp]
fffffe00079aff08:      	stp	x8, x9, [sp, #16]
fffffe00079aff0c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079aff10:      	ldp	x29, x30, [sp, #112]
fffffe00079aff14:      	ldp	x20, x19, [sp, #96]
fffffe00079aff18:      	add	sp, sp, #128
fffffe00079aff1c:      	ret
