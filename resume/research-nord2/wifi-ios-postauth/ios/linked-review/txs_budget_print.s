
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aff20 <_nsdd_event>:
fffffe00079aff20:      	sub	sp, sp, #144
fffffe00079aff24:      	stp	x26, x25, [sp, #64]
fffffe00079aff28:      	stp	x24, x23, [sp, #80]
fffffe00079aff2c:      	stp	x22, x21, [sp, #96]
fffffe00079aff30:      	stp	x20, x19, [sp, #112]
fffffe00079aff34:      	stp	x29, x30, [sp, #128]
fffffe00079aff38:      	add	x29, sp, #128
fffffe00079aff3c:      	cbz	x0, 0xfffffe00079b0068 <_nsdd_event+0x148>
fffffe00079aff40:      	cmp	w4, #1
fffffe00079aff44:      	b.hi	0xfffffe00079b0068 <_nsdd_event+0x148>
fffffe00079aff48:      	mov	x19, x0
fffffe00079aff4c:      	ldr	w8, [x0, #192]
fffffe00079aff50:      	cbnz	w8, 0xfffffe00079b0068 <_nsdd_event+0x148>
fffffe00079aff54:      	cbz	x2, 0xfffffe00079b0068 <_nsdd_event+0x148>
fffffe00079aff58:      	mov	x8, #-4097
fffffe00079aff5c:      	add	x8, x3, x8
fffffe00079aff60:      	cmn	x8, #4009
fffffe00079aff64:      	b.lo	0xfffffe00079b0068 <_nsdd_event+0x148>
fffffe00079aff68:      	ldrb	w22, [x2, #8]
fffffe00079aff6c:      	ldrb	w8, [x2, #1]
fffffe00079aff70:      	tbnz	w22, #0, 0xfffffe00079aff8c <_nsdd_event+0x6c>
fffffe00079aff74:      	mov	w20, wzr
fffffe00079aff78:      	mov	w21, wzr
fffffe00079aff7c:      	mov	w23, wzr
fffffe00079aff80:      	mov	w24, wzr
fffffe00079aff84:      	mov	w25, wzr
fffffe00079aff88:      	b	0xfffffe00079affa0 <_nsdd_event+0x80>
fffffe00079aff8c:      	ldrh	w20, [x2, #2]
fffffe00079aff90:      	ldrb	w21, [x2, #5]
fffffe00079aff94:      	ldrh	w23, [x2, #6]
fffffe00079aff98:      	ldrb	w24, [x2, #9]
fffffe00079aff9c:      	ldrb	w25, [x2, #14]
fffffe00079affa0:      	ldrb	w9, [x2]
fffffe00079affa4:      	mov	x11, x19
fffffe00079affa8:      	ldrb	w10, [x2, #4]
fffffe00079affac:      	cbz	w8, 0xfffffe00079b0084 <_nsdd_event+0x164>
fffffe00079affb0:      	ldr	w12, [x11, #148]!
fffffe00079affb4:      	cmn	w12, #1
fffffe00079affb8:      	b.eq	0xfffffe00079affc4 <_nsdd_event+0xa4>
fffffe00079affbc:      	add	w12, w12, #1
fffffe00079affc0:      	str	w12, [x11]
fffffe00079affc4:      	cmp	w9, #127
fffffe00079affc8:      	b.ne	0xfffffe00079affe0 <_nsdd_event+0xc0>
fffffe00079affcc:      	ldr	w11, [x19, #152]
fffffe00079affd0:      	cmn	w11, #1
fffffe00079affd4:      	b.eq	0xfffffe00079affe0 <_nsdd_event+0xc0>
fffffe00079affd8:      	add	w11, w11, #1
fffffe00079affdc:      	str	w11, [x19, #152]
fffffe00079affe0:      	cmp	w4, #0
fffffe00079affe4:      	mov	w11, #184
fffffe00079affe8:      	mov	w12, #180
fffffe00079affec:      	mov	w13, #16
fffffe00079afff0:      	csel	x11, x12, x11, eq
fffffe00079afff4:      	mov	w14, #8
fffffe00079afff8:      	csel	w13, w14, w13, eq
fffffe00079afffc:      	ldr	w12, [x19, x11]
fffffe00079b0000:      	cmp	w12, w13
fffffe00079b0004:      	b.hs	0xfffffe00079b0068 <_nsdd_event+0x148>
fffffe00079b0008:      	add	w12, w12, #1
fffffe00079b000c:      	cmp	w9, #127
fffffe00079b0010:      	cset	w13, eq
fffffe00079b0014:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x530>
fffffe00079b0018:      	add	x0, x0, #2492
fffffe00079b001c:      	str	w12, [x19, x11]
fffffe00079b0020:      	ldr	w11, [x19, #188]
fffffe00079b0024:      	and	w26, w22, #0x1
fffffe00079b0028:      	ldp	w12, w14, [x19, #156]
fffffe00079b002c:      	add	w11, w11, #1
fffffe00079b0030:      	str	w11, [x19, #188]
fffffe00079b0034:      	stp	x12, x14, [sp, #48]
fffffe00079b0038:      	stp	x8, x13, [sp, #32]
fffffe00079b003c:      	stp	x9, x10, [sp, #16]
fffffe00079b0040:      	stp	x11, x1, [sp]
fffffe00079b0044:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b0048:      	ldr	w8, [x19, #188]
fffffe00079b004c:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x568>
fffffe00079b0050:      	add	x0, x0, #2595
fffffe00079b0054:      	stp	x24, x25, [sp, #48]
fffffe00079b0058:      	stp	x22, x26, [sp, #32]
fffffe00079b005c:      	stp	x21, x23, [sp, #16]
fffffe00079b0060:      	stp	x8, x20, [sp]
fffffe00079b0064:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b0068:      	ldp	x29, x30, [sp, #128]
fffffe00079b006c:      	ldp	x20, x19, [sp, #112]
fffffe00079b0070:      	ldp	x22, x21, [sp, #96]
fffffe00079b0074:      	ldp	x24, x23, [sp, #80]
fffffe00079b0078:      	ldp	x26, x25, [sp, #64]
fffffe00079b007c:      	add	sp, sp, #144
fffffe00079b0080:      	ret
fffffe00079b0084:      	ldr	w12, [x11, #144]!
fffffe00079b0088:      	cmn	w12, #1
fffffe00079b008c:      	b.ne	0xfffffe00079affbc <_nsdd_event+0x9c>
fffffe00079b0090:      	b	0xfffffe00079affc4 <_nsdd_event+0xa4>
