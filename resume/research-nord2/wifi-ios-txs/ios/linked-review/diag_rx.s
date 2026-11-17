
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aa484 <_nstd_rx>:
fffffe00079aa484:      	sub	sp, sp, #112
fffffe00079aa488:      	stp	x29, x30, [sp, #96]
fffffe00079aa48c:      	add	x29, sp, #96
fffffe00079aa490:      	cmp	w2, #4
fffffe00079aa494:      	b.hi	0xfffffe00079aa584 <_nstd_rx+0x100>
fffffe00079aa498:      	cbz	x3, 0xfffffe00079aa584 <_nstd_rx+0x100>
fffffe00079aa49c:      	mov	x8, x0
fffffe00079aa4a0:      	cmp	x4, #4
fffffe00079aa4a4:      	b.lo	0xfffffe00079aa4b4 <_nstd_rx+0x30>
fffffe00079aa4a8:      	ldrb	w9, [x3, #3]
fffffe00079aa4ac:      	lsr	w9, w9, #3
fffffe00079aa4b0:      	b	0xfffffe00079aa4b8 <_nstd_rx+0x34>
fffffe00079aa4b4:      	mov	w9, #32
fffffe00079aa4b8:      	adrp	x12, 0xfffffe0008527000 <_nwpc_unwrap+0x3e4>
fffffe00079aa4bc:      	add	x12, x12, #2688
fffffe00079aa4c0:      	mov	w11, w2
fffffe00079aa4c4:      	ldr	w10, [x12, w2, uxtw #2]
fffffe00079aa4c8:      	cmn	w10, #1
fffffe00079aa4cc:      	b.eq	0xfffffe00079aa4dc <_nstd_rx+0x58>
fffffe00079aa4d0:      	add	w10, w10, #1
fffffe00079aa4d4:      	str	w10, [x12, x11, lsl #2]
fffffe00079aa4d8:      	b	0xfffffe00079aa4e0 <_nstd_rx+0x5c>
fffffe00079aa4dc:      	mov	w10, #-1
fffffe00079aa4e0:      	mov	w12, #132
fffffe00079aa4e4:      	adrp	x13, 0xfffffe0008527000 <_nwp_ap_rsn+0x8>
fffffe00079aa4e8:      	add	x13, x13, #2708
fffffe00079aa4ec:      	madd	x12, x11, x12, x13
fffffe00079aa4f0:      	add	x13, x12, w9, uxtw #2
fffffe00079aa4f4:      	ldr	w11, [x13]
fffffe00079aa4f8:      	cmn	w11, #1
fffffe00079aa4fc:      	b.eq	0xfffffe00079aa514 <_nstd_rx+0x90>
fffffe00079aa500:      	add	w11, w11, #1
fffffe00079aa504:      	str	w11, [x13]
fffffe00079aa508:      	cmp	w10, #3
fffffe00079aa50c:      	b.hs	0xfffffe00079aa520 <_nstd_rx+0x9c>
fffffe00079aa510:      	b	0xfffffe00079aa530 <_nstd_rx+0xac>
fffffe00079aa514:      	mov	w11, #-1
fffffe00079aa518:      	cmp	w10, #3
fffffe00079aa51c:      	b.lo	0xfffffe00079aa530 <_nstd_rx+0xac>
fffffe00079aa520:      	cbnz	w9, 0xfffffe00079aa584 <_nstd_rx+0x100>
fffffe00079aa524:      	ldr	w12, [x12]
fffffe00079aa528:      	cmp	w12, #8
fffffe00079aa52c:      	b.hi	0xfffffe00079aa584 <_nstd_rx+0x100>
fffffe00079aa530:      	cmp	x4, #2
fffffe00079aa534:      	b.lo	0xfffffe00079aa540 <_nstd_rx+0xbc>
fffffe00079aa538:      	ldrh	w12, [x3]
fffffe00079aa53c:      	b	0xfffffe00079aa544 <_nstd_rx+0xc0>
fffffe00079aa540:      	mov	w12, wzr
fffffe00079aa544:      	adrp	x13, 0xfffffe0008527000 <_nwp_ap_rsn+0x68>
fffffe00079aa548:      	ldr	w14, [x13, #3368]
fffffe00079aa54c:      	cmp	w14, #94
fffffe00079aa550:      	b.hi	0xfffffe00079aa584 <_nstd_rx+0x100>
fffffe00079aa554:      	add	w14, w14, #1
fffffe00079aa558:      	stp	x10, x11, [sp, #48]
fffffe00079aa55c:      	mov	w10, #7
fffffe00079aa560:      	adrp	x0, 0xfffffe0007130000 <_peer_progress+0x474>
fffffe00079aa564:      	add	x0, x0, #3229
fffffe00079aa568:      	stp	xzr, xzr, [sp, #80]
fffffe00079aa56c:      	str	w14, [x13, #3368]
fffffe00079aa570:      	stp	x4, x12, [sp, #64]
fffffe00079aa574:      	stp	x2, x9, [sp, #32]
fffffe00079aa578:      	stp	x1, x10, [sp, #16]
fffffe00079aa57c:      	stp	x14, x8, [sp]
fffffe00079aa580:      	bl	0xfffffe000735dae4 <_printf>
fffffe00079aa584:      	ldp	x29, x30, [sp, #96]
fffffe00079aa588:      	add	sp, sp, #112
fffffe00079aa58c:      	ret
