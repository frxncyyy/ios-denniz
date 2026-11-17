
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b3584 <_nsdd_pid_publish>:
fffffe00079b3584:      	sub	sp, sp, #112
fffffe00079b3588:      	stp	x29, x30, [sp, #96]
fffffe00079b358c:      	add	x29, sp, #96
fffffe00079b3590:      	cmp	w2, #254
fffffe00079b3594:      	b.hi	0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b3598:      	mov	x8, x0
fffffe00079b359c:      	cbz	x0, 0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b35a0:      	sub	w9, w1, #127
fffffe00079b35a4:      	cmn	w9, #126
fffffe00079b35a8:      	b.lo	0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b35ac:      	mov	w9, #72
fffffe00079b35b0:      	umull	x10, w1, w9
fffffe00079b35b4:      	adrp	x9, 0xfffffe0008544000 <_stc_receipt+0x2dc>
fffffe00079b35b8:      	add	x9, x9, #3392
fffffe00079b35bc:      	ldr	x10, [x9, x10]
fffffe00079b35c0:      	cmp	x10, #0
fffffe00079b35c4:      	ccmp	x10, x8, #0, ne
fffffe00079b35c8:      	b.ne	0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b35cc:      	mov	w10, w1
fffffe00079b35d0:      	mov	w11, #72
fffffe00079b35d4:      	madd	x11, x10, x11, x9
fffffe00079b35d8:      	ldr	w11, [x11, #12]
fffffe00079b35dc:      	cmp	w11, w2
fffffe00079b35e0:      	b.ne	0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b35e4:      	mov	w11, #72
fffffe00079b35e8:      	madd	x11, x10, x11, x9
fffffe00079b35ec:      	ldr	w11, [x11, #68]
fffffe00079b35f0:      	cbz	w11, 0xfffffe00079b3600 <_nsdd_pid_publish+0x7c>
fffffe00079b35f4:      	ldp	x29, x30, [sp, #96]
fffffe00079b35f8:      	add	sp, sp, #112
fffffe00079b35fc:      	ret
fffffe00079b3600:      	mov	w11, #72
fffffe00079b3604:      	madd	x11, x10, x11, x9
fffffe00079b3608:      	ldr	w11, [x11, #44]
fffffe00079b360c:      	cbz	w11, 0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b3610:      	cbz	w3, 0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b3614:      	mov	w11, #72
fffffe00079b3618:      	madd	x11, x10, x11, x9
fffffe00079b361c:      	ldr	w12, [x11, #48]!
fffffe00079b3620:      	cbnz	w12, 0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
fffffe00079b3624:      	mov	w12, #72
fffffe00079b3628:      	mov	w15, #1
fffffe00079b362c:      	stp	x1, x2, [sp, #8]
fffffe00079b3630:      	madd	x9, x10, x12, x9
fffffe00079b3634:      	str	x8, [sp]
fffffe00079b3638:      	str	w15, [x11]
fffffe00079b363c:      	ldr	w0, [x9, #40]
fffffe00079b3640:      	str	w3, [x9, #52]
fffffe00079b3644:      	ldp	w10, w12, [x9, #16]
fffffe00079b3648:      	ldp	w13, w14, [x9, #24]
fffffe00079b364c:      	ldp	w16, w17, [x9, #32]
fffffe00079b3650:      	stp	x0, x3, [sp, #72]
fffffe00079b3654:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x708>
fffffe00079b3658:      	add	x0, x0, #1420
fffffe00079b365c:      	stp	x10, x12, [sp, #24]
fffffe00079b3660:      	stp	x13, x14, [sp, #40]
fffffe00079b3664:      	stp	x16, x17, [sp, #56]
fffffe00079b3668:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b366c:      	b	0xfffffe00079b35f4 <_nsdd_pid_publish+0x70>
