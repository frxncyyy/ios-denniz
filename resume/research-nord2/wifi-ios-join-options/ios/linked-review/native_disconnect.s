
$NORD2_REPO/research/ci-39823157589/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe000750e528 <_nord2_wifi_join_disconnect>:
fffffe000750e528:      	stp	x22, x21, [sp, #-48]!
fffffe000750e52c:      	stp	x20, x19, [sp, #16]
fffffe000750e530:      	stp	x29, x30, [sp, #32]
fffffe000750e534:      	add	x29, sp, #32
fffffe000750e538:      	adrp	x8, 0xfffffe00084cd000 <_nst_native_rx_active+0x138>
fffffe000750e53c:      	add	x8, x8, #2464
fffffe000750e540:      	mov	w19, #1
fffffe000750e544:      	ldar	w8, [x8]
fffffe000750e548:      	cmp	w8, #1
fffffe000750e54c:      	b.ne	0xfffffe000750e6c4 <_nord2_wifi_join_disconnect+0x19c>
fffffe000750e550:      	adrp	x8, 0xfffffe00084e8000 <_nwfn_memory_range+0x74>
fffffe000750e554:      	ldr	x0, [x8, #536]
fffffe000750e558:      	cbz	x0, 0xfffffe000750e6c4 <_nord2_wifi_join_disconnect+0x19c>
fffffe000750e55c:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000750e560:      	adrp	x8, 0xfffffe00084cd000 <_nwfn_memory_range+0x18>
fffffe000750e564:      	ldr	x0, [x8, #2456]
fffffe000750e568:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000750e56c:      	adrp	x8, 0xfffffe00084e8000 <_nwfn_memory_range+0x90>
fffffe000750e570:      	add	x8, x8, #544
fffffe000750e574:      	ldp	w9, w8, [x8]
fffffe000750e578:      	orr	w9, w9, w8
fffffe000750e57c:      	cbz	w9, 0xfffffe000750e634 <_nord2_wifi_join_disconnect+0x10c>
fffffe000750e580:      	adrp	x9, 0xfffffe00084e8000 <_nwfn_memory_range+0xa4>
fffffe000750e584:      	add	x9, x9, #556
fffffe000750e588:      	mov	w10, #1
fffffe000750e58c:      	ldur	x19, [x9, #172]
fffffe000750e590:      	str	w10, [x9]
fffffe000750e594:      	cbz	w8, 0xfffffe000750e5a4 <_nord2_wifi_join_disconnect+0x7c>
fffffe000750e598:      	adrp	x0, 0xfffffe0008504000 <_nwfn_memory_range+0x12c>
fffffe000750e59c:      	add	x0, x0, #4056
fffffe000750e5a0:      	bl	0xfffffe00079a5718 <_nst_close_port>
fffffe000750e5a4:      	adrp	x21, 0xfffffe00084e8000 <_nwfn_memory_range+0xc8>
fffffe000750e5a8:      	add	x21, x21, #536
fffffe000750e5ac:      	mov	w1, #12112
fffffe000750e5b0:      	ldr	w8, [x21, #60]
fffffe000750e5b4:      	ldr	w9, [x21, #124]
fffffe000750e5b8:      	add	w8, w9, w8
fffffe000750e5bc:      	add	x9, x21, #19, lsl #12
fffffe000750e5c0:      	add	x0, x9, #3952
fffffe000750e5c4:      	str	w8, [x21, #124]
fffffe000750e5c8:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe000750e5cc:      	adrp	x20, 0xfffffe00084cd000 <_nwfn_memory_range+0x84>
fffffe000750e5d0:      	add	x20, x20, #2456
fffffe000750e5d4:      	add	x8, x21, #22, lsl #12
fffffe000750e5d8:      	mov	w9, #7
fffffe000750e5dc:      	movi.2d	v0, #0000000000000000
fffffe000750e5e0:      	add	x8, x8, #3776
fffffe000750e5e4:      	ldr	x0, [x20]
fffffe000750e5e8:      	stp	xzr, xzr, [x21, #48]
fffffe000750e5ec:      	str	xzr, [x21, #3272]
fffffe000750e5f0:      	str	xzr, [x21, #3264]
fffffe000750e5f4:      	stp	q0, q0, [x8]
fffffe000750e5f8:      	str	w9, [x21, #3256]
fffffe000750e5fc:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000750e600:      	mov	x0, x19
fffffe000750e604:      	mov	w1, wzr
fffffe000750e608:      	bl	0xfffffe0007ae4798 <_nord2_wifi_ifnet_link>
fffffe000750e60c:      	ldr	x8, [x21]
fffffe000750e610:      	cmp	w0, #0
fffffe000750e614:      	csetm	w19, ne
fffffe000750e618:      	mov	x0, x8
fffffe000750e61c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000750e620:      	mov	x0, x20
fffffe000750e624:      	mov	w1, wzr
fffffe000750e628:      	mov	w2, wzr
fffffe000750e62c:      	bl	0xfffffe0007369248 <_thread_wakeup_prim>
fffffe000750e630:      	b	0xfffffe000750e6c4 <_nord2_wifi_join_disconnect+0x19c>
fffffe000750e634:      	adrp	x20, 0xfffffe00084e8000 <_nwfn_memory_range+0x158>
fffffe000750e638:      	add	x20, x20, #536
fffffe000750e63c:      	adrp	x8, 0xfffffe00084cd000 <_nwfn_memory_range+0xf4>
fffffe000750e640:      	add	x8, x8, #2456
fffffe000750e644:      	add	x9, x20, #40
fffffe000750e648:      	add	x10, x8, #12
fffffe000750e64c:      	ldur	d0, [x20, #28]
fffffe000750e650:      	adrp	x11, 0xfffffe0008507000 <_nwfn_memory_range+0x1f0>
fffffe000750e654:      	ldr	s1, [x20, #3256]
fffffe000750e658:      	ldr	d2, [x20, #48]
fffffe000750e65c:      	ld1.s	{ v0 }[1], [x9]
fffffe000750e660:      	adrp	x9, 0xfffffe0007008000 <_nwfn_fwrite+0xa4>
fffffe000750e664:      	ld1.s	{ v1 }[1], [x10]
fffffe000750e668:      	ldr	d3, [x11, #944]
fffffe000750e66c:      	ldr	x0, [x8]
fffffe000750e670:      	mov.d	v0[1], v2[0]
fffffe000750e674:      	ldr	q2, [x9, #2816]
fffffe000750e678:      	mov.d	v1[1], v3[0]
fffffe000750e67c:      	ldp	x10, x9, [x20, #328]
fffffe000750e680:      	cmtst.4s	v0, v0, v0
fffffe000750e684:      	cmeq.4s	v1, v1, v2
fffffe000750e688:      	mvn.16b	v1, v1
fffffe000750e68c:      	cmp	x9, #0
fffffe000750e690:      	uzp1.8h	v0, v0, v1
fffffe000750e694:      	cset	w9, ne
fffffe000750e698:      	cmp	x10, #0
fffffe000750e69c:      	cset	w11, ne
fffffe000750e6a0:      	xtn.8b	v0, v0
fffffe000750e6a4:      	umaxv.8b	b0, v0
fffffe000750e6a8:      	fmov	w10, s0
fffffe000750e6ac:      	orr	w9, w10, w9
fffffe000750e6b0:      	orr	w9, w9, w11
fffffe000750e6b4:      	and	w19, w9, #0x1
fffffe000750e6b8:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000750e6bc:      	ldr	x0, [x20]
fffffe000750e6c0:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000750e6c4:      	mov	w0, w19
fffffe000750e6c8:      	ldp	x29, x30, [sp, #32]
fffffe000750e6cc:      	ldp	x20, x19, [sp, #16]
fffffe000750e6d0:      	ldp	x22, x21, [sp], #48
fffffe000750e6d4:      	ret
