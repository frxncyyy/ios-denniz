
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b0094 <_nsdd_summary>:
fffffe00079b0094:      	sub	sp, sp, #144
fffffe00079b0098:      	stp	x24, x23, [sp, #80]
fffffe00079b009c:      	stp	x22, x21, [sp, #96]
fffffe00079b00a0:      	stp	x20, x19, [sp, #112]
fffffe00079b00a4:      	stp	x29, x30, [sp, #128]
fffffe00079b00a8:      	add	x29, sp, #128
fffffe00079b00ac:      	cbz	x0, 0xfffffe00079b01dc <_nsdd_summary+0x148>
fffffe00079b00b0:      	mov	x20, x0
fffffe00079b00b4:      	ldr	w8, [x0, #192]
fffffe00079b00b8:      	cbnz	w8, 0xfffffe00079b01dc <_nsdd_summary+0x148>
fffffe00079b00bc:      	ldp	w8, w9, [x20]
fffffe00079b00c0:      	ldp	w10, w11, [x20, #8]
fffffe00079b00c4:      	ldp	w12, w13, [x20, #16]
fffffe00079b00c8:      	ldp	w14, w15, [x20, #144]
fffffe00079b00cc:      	mov	w23, #1
fffffe00079b00d0:      	ldr	w16, [x20, #152]
fffffe00079b00d4:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x5f0>
fffffe00079b00d8:      	add	x0, x0, #2687
fffffe00079b00dc:      	mov	x19, x1
fffffe00079b00e0:      	str	w23, [x20, #192]
fffffe00079b00e4:      	stp	x15, x16, [sp, #64]
fffffe00079b00e8:      	stp	x13, x14, [sp, #48]
fffffe00079b00ec:      	stp	x11, x12, [sp, #32]
fffffe00079b00f0:      	stp	x9, x10, [sp, #16]
fffffe00079b00f4:      	stp	x1, x8, [sp]
fffffe00079b00f8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b00fc:      	ldp	w8, w9, [x20, #24]
fffffe00079b0100:      	ldp	w10, w11, [x20, #32]
fffffe00079b0104:      	ldp	w12, w13, [x20, #40]
fffffe00079b0108:      	adrp	x21, 0xfffffe0007134000 <_nst_peer_begin+0x624>
fffffe00079b010c:      	add	x21, x21, #2817
fffffe00079b0110:      	ldr	w14, [x20, #48]
fffffe00079b0114:      	mov	x0, x21
fffffe00079b0118:      	stp	x9, x10, [sp, #24]
fffffe00079b011c:      	stp	x11, x12, [sp, #40]
fffffe00079b0120:      	stp	x13, x14, [sp, #56]
fffffe00079b0124:      	stp	x23, x8, [sp, #8]
fffffe00079b0128:      	str	x19, [sp]
fffffe00079b012c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b0130:      	ldp	w11, w8, [x20, #84]
fffffe00079b0134:      	ldp	w9, w10, [x20, #100]
fffffe00079b0138:      	mov	w24, #-1
fffffe00079b013c:      	adrp	x22, 0xfffffe0007134000 <_nst_peer_begin+0x658>
fffffe00079b0140:      	add	x22, x22, #2915
fffffe00079b0144:      	ldp	w12, w13, [x20, #92]
fffffe00079b0148:      	add	x8, x9, x8
fffffe00079b014c:      	mov	x0, x22
fffffe00079b0150:      	add	x8, x8, x10
fffffe00079b0154:      	ldp	w9, w10, [x20, #76]
fffffe00079b0158:      	cmp	x8, x24
fffffe00079b015c:      	stp	x11, x12, [sp, #32]
fffffe00079b0160:      	csel	x8, x8, x24, lo
fffffe00079b0164:      	stp	x19, x23, [sp]
fffffe00079b0168:      	stp	x9, x10, [sp, #16]
fffffe00079b016c:      	stp	x13, x8, [sp, #48]
fffffe00079b0170:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b0174:      	ldp	w8, w9, [x20, #8]
fffffe00079b0178:      	ldp	w10, w11, [x20, #52]
fffffe00079b017c:      	ldp	w12, w13, [x20, #60]
fffffe00079b0180:      	ldr	w14, [x20, #68]
fffffe00079b0184:      	mov	w23, #2
fffffe00079b0188:      	mov	x0, x21
fffffe00079b018c:      	str	x19, [sp]
fffffe00079b0190:      	stp	x9, x10, [sp, #24]
fffffe00079b0194:      	stp	x13, x14, [sp, #56]
fffffe00079b0198:      	stp	x11, x12, [sp, #40]
fffffe00079b019c:      	stp	x23, x8, [sp, #8]
fffffe00079b01a0:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b01a4:      	ldp	w11, w8, [x20, #120]
fffffe00079b01a8:      	ldp	w9, w10, [x20, #136]
fffffe00079b01ac:      	ldp	w12, w13, [x20, #128]
fffffe00079b01b0:      	mov	x0, x22
fffffe00079b01b4:      	add	x8, x9, x8
fffffe00079b01b8:      	add	x8, x8, x10
fffffe00079b01bc:      	ldp	w9, w10, [x20, #112]
fffffe00079b01c0:      	cmp	x8, x24
fffffe00079b01c4:      	stp	x11, x12, [sp, #32]
fffffe00079b01c8:      	csel	x8, x8, x24, lo
fffffe00079b01cc:      	stp	x19, x23, [sp]
fffffe00079b01d0:      	stp	x9, x10, [sp, #16]
fffffe00079b01d4:      	stp	x13, x8, [sp, #48]
fffffe00079b01d8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b01dc:      	ldp	x29, x30, [sp, #128]
fffffe00079b01e0:      	ldp	x20, x19, [sp, #112]
fffffe00079b01e4:      	ldp	x22, x21, [sp, #96]
fffffe00079b01e8:      	ldp	x24, x23, [sp, #80]
fffffe00079b01ec:      	add	sp, sp, #144
fffffe00079b01f0:      	ret
