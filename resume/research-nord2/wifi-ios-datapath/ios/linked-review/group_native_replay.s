
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079acc08 <_group_replay>:
fffffe00079acc08:      	stp	x22, x21, [sp, #-48]!
fffffe00079acc0c:      	stp	x20, x19, [sp, #16]
fffffe00079acc10:      	stp	x29, x30, [sp, #32]
fffffe00079acc14:      	add	x29, sp, #32
fffffe00079acc18:      	cbz	x0, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc1c:      	ldr	w8, [x0, #28]
fffffe00079acc20:      	cbz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc24:      	ldr	x8, [x0]
fffffe00079acc28:      	cbz	x8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc2c:      	ldr	x9, [x0, #8]
fffffe00079acc30:      	cbz	x9, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc34:      	ldr	x9, [x0, #16]
fffffe00079acc38:      	cbz	x9, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc3c:      	ldr	w9, [x0, #44]
fffffe00079acc40:      	cbnz	w9, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc44:      	ldr	w9, [x8, #12]
fffffe00079acc48:      	cbnz	w9, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc4c:      	ldr	w8, [x8, #1884]
fffffe00079acc50:      	cbnz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc54:      	ldr	w8, [x0, #24]
fffffe00079acc58:      	cbnz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc5c:      	ldr	w8, [x0, #1708]
fffffe00079acc60:      	cbnz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc64:      	ldr	w8, [x0, #128]
fffffe00079acc68:      	cbnz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc6c:      	ldr	w8, [x0, #112]
fffffe00079acc70:      	cmp	w8, #7
fffffe00079acc74:      	b.ne	0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc78:      	ldr	w8, [x0, #136]
fffffe00079acc7c:      	cbz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acc80:      	mov	x20, x0
fffffe00079acc84:      	mov	w0, #-1
fffffe00079acc88:      	cbz	x2, 0xfffffe00079accb4 <_group_replay+0xac>
fffffe00079acc8c:      	cmp	w1, #3
fffffe00079acc90:      	b.hi	0xfffffe00079accb4 <_group_replay+0xac>
fffffe00079acc94:      	ldr	w8, [x20, #1516]
fffffe00079acc98:      	cbz	w8, 0xfffffe00079accb4 <_group_replay+0xac>
fffffe00079acc9c:      	mov	x19, x2
fffffe00079acca0:      	ldrb	w8, [x2, #6]
fffffe00079acca4:      	cbnz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acca8:      	ldrb	w8, [x19, #7]
fffffe00079accac:      	cbz	w8, 0xfffffe00079accc4 <_group_replay+0xbc>
fffffe00079accb0:      	mov	w0, #-1
fffffe00079accb4:      	ldp	x29, x30, [sp, #32]
fffffe00079accb8:      	ldp	x20, x19, [sp, #16]
fffffe00079accbc:      	ldp	x22, x21, [sp], #48
fffffe00079accc0:      	ret
fffffe00079accc4:      	ldr	w8, [x20, #1540]
fffffe00079accc8:      	cbz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acccc:      	ldrb	w8, [x20, #1744]
fffffe00079accd0:      	cmp	w8, w1
fffffe00079accd4:      	b.ne	0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079accd8:      	ldr	w8, [x20, #1504]
fffffe00079accdc:      	lsr	w8, w8, w1
fffffe00079acce0:      	tbz	w8, #0, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acce4:      	ldr	w8, [x20, #56]
fffffe00079acce8:      	cbnz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079accec:      	ldr	w8, [x20, #60]
fffffe00079accf0:      	cbnz	w8, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079accf4:      	mov	x8, x20
fffffe00079accf8:      	ldr	w9, [x20, #1508]
fffffe00079accfc:      	ldr	w8, [x20, #1512]
fffffe00079acd00:      	cmp	w9, w8
fffffe00079acd04:      	b.ls	0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acd08:      	mov	w8, #144
fffffe00079acd0c:      	umaddl	x8, w1, w8, x20
fffffe00079acd10:      	cbnz	w3, 0xfffffe00079acd1c <_group_replay+0x114>
fffffe00079acd14:      	ldr	w9, [x8, #920]
fffffe00079acd18:      	cbnz	w9, 0xfffffe00079accb0 <_group_replay+0xa8>
fffffe00079acd1c:      	mov	w9, w1
fffffe00079acd20:      	mov	w10, #144
fffffe00079acd24:      	add	x21, x8, #920
fffffe00079acd28:      	mov	w8, #1
fffffe00079acd2c:      	madd	x22, x9, x10, x20
fffffe00079acd30:      	mov	x0, x19
fffffe00079acd34:      	str	w8, [x21]
fffffe00079acd38:      	strb	w1, [x22, #924]
fffffe00079acd3c:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd40:      	str	x0, [x22, #928]
fffffe00079acd44:      	mov	x0, x19
fffffe00079acd48:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd4c:      	str	x0, [x22, #936]
fffffe00079acd50:      	mov	x0, x19
fffffe00079acd54:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd58:      	str	x0, [x22, #944]
fffffe00079acd5c:      	mov	x0, x19
fffffe00079acd60:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd64:      	str	x0, [x22, #952]
fffffe00079acd68:      	mov	x0, x19
fffffe00079acd6c:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd70:      	str	x0, [x22, #960]
fffffe00079acd74:      	mov	x0, x19
fffffe00079acd78:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd7c:      	str	x0, [x22, #968]
fffffe00079acd80:      	mov	x0, x19
fffffe00079acd84:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd88:      	str	x0, [x22, #976]
fffffe00079acd8c:      	mov	x0, x19
fffffe00079acd90:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acd94:      	str	x0, [x22, #984]
fffffe00079acd98:      	mov	x0, x19
fffffe00079acd9c:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acda0:      	str	x0, [x22, #992]
fffffe00079acda4:      	mov	x0, x19
fffffe00079acda8:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acdac:      	str	x0, [x22, #1000]
fffffe00079acdb0:      	mov	x0, x19
fffffe00079acdb4:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acdb8:      	str	x0, [x22, #1008]
fffffe00079acdbc:      	mov	x0, x19
fffffe00079acdc0:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acdc4:      	str	x0, [x22, #1016]
fffffe00079acdc8:      	mov	x0, x19
fffffe00079acdcc:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acdd0:      	str	x0, [x22, #1024]
fffffe00079acdd4:      	mov	x0, x19
fffffe00079acdd8:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acddc:      	str	x0, [x22, #1032]
fffffe00079acde0:      	mov	x0, x19
fffffe00079acde4:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acde8:      	str	x0, [x22, #1040]
fffffe00079acdec:      	mov	x0, x19
fffffe00079acdf0:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079acdf4:      	str	x0, [x22, #1048]
fffffe00079acdf8:      	mov	x0, x19
fffffe00079acdfc:      	bl	0xfffffe00079ace44 <_pn48>
fffffe00079ace00:      	ldp	q0, q1, [x21, #96]
fffffe00079ace04:      	add	x9, x20, #776
fffffe00079ace08:      	mov	x8, x0
fffffe00079ace0c:      	mov	w0, wzr
fffffe00079ace10:      	str	x8, [x22, #1056]
fffffe00079ace14:      	ldp	q2, q3, [x21, #32]
fffffe00079ace18:      	stp	q0, q1, [x9, #96]
fffffe00079ace1c:      	ldp	q1, q0, [x21, #64]
fffffe00079ace20:      	stp	q2, q3, [x9, #32]
fffffe00079ace24:      	ldp	q3, q2, [x21]
fffffe00079ace28:      	stp	q1, q0, [x9, #64]
fffffe00079ace2c:      	ldr	q0, [x21, #128]
fffffe00079ace30:      	stp	q3, q2, [x9]
fffffe00079ace34:      	ldr	w8, [x20, #1508]
fffffe00079ace38:      	str	q0, [x9, #128]
fffffe00079ace3c:      	str	w8, [x20, #1512]
fffffe00079ace40:      	b	0xfffffe00079accb4 <_group_replay+0xac>
