
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007512468 <_njs_rejoin_ready_locked>:
fffffe0007512468:      	sub	sp, sp, #32
fffffe000751246c:      	stp	x29, x30, [sp, #16]
fffffe0007512470:      	add	x29, sp, #16
fffffe0007512474:      	adrp	x9, 0xfffffe0008516000 <_ncd_native_write+0xc>
fffffe0007512478:      	add	x9, x9, #2040
fffffe000751247c:      	adrp	x8, 0xfffffe00084f8000 <_ncd_native_read+0x14>
fffffe0007512480:      	add	x8, x8, #572
fffffe0007512484:      	add	x10, x8, #20
fffffe0007512488:      	mov	w11, #55656
fffffe000751248c:      	ldr	s0, [x9, #16]
fffffe0007512490:      	mov	w0, wzr
fffffe0007512494:      	ldr	q1, [x9]
fffffe0007512498:      	adrp	x9, 0xfffffe000700d000 <_nwfn_start+0x3d0>
fffffe000751249c:      	ld1.s	{ v0 }[1], [x8]
fffffe00075124a0:      	adrp	x8, 0xfffffe0008516000 <_ncd_native_write+0x38>
fffffe00075124a4:      	add	x8, x8, #2116
fffffe00075124a8:      	cmeq.4s	v2, v1, #0
fffffe00075124ac:      	cmtst.4s	v1, v1, v1
fffffe00075124b0:      	ld1.s	{ v0 }[2], [x10]
fffffe00075124b4:      	add	x10, x8, x11
fffffe00075124b8:      	ld1.s	{ v0 }[3], [x10]
fffffe00075124bc:      	cmeq.4s	v3, v0, #0
fffffe00075124c0:      	cmtst.4s	v0, v0, v0
fffffe00075124c4:      	uzp1.8h	v2, v2, v3
fffffe00075124c8:      	uzp1.8h	v0, v1, v0
fffffe00075124cc:      	ldr	d1, [x9, #3312]
fffffe00075124d0:      	uzp1.16b	v0, v2, v0
fffffe00075124d4:      	tbl.8b	v0, { v0 }, v1
fffffe00075124d8:      	umaxv.8b	b0, v0
fffffe00075124dc:      	fmov	w9, s0
fffffe00075124e0:      	tbnz	w9, #0, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe00075124e4:      	ldr	d0, [x8]
fffffe00075124e8:      	add	x9, x8, #108
fffffe00075124ec:      	ld1.s	{ v0 }[2], [x9]
fffffe00075124f0:      	add	x9, x8, #12
fffffe00075124f4:      	ldr	w8, [x8, #16]
fffffe00075124f8:      	ld1.s	{ v0 }[3], [x9]
fffffe00075124fc:      	cmtst.4s	v0, v0, v0
fffffe0007512500:      	xtn.4h	v0, v0
fffffe0007512504:      	umov.h	w9, v0[1]
fffffe0007512508:      	umov.h	w10, v0[2]
fffffe000751250c:      	umov.h	w11, v0[0]
fffffe0007512510:      	umov.h	w12, v0[3]
fffffe0007512514:      	and	w9, w9, #0x1
fffffe0007512518:      	and	w10, w10, #0x1
fffffe000751251c:      	bfi	w11, w9, #1, #1
fffffe0007512520:      	bfi	w11, w10, #2, #1
fffffe0007512524:      	bfi	w11, w12, #3, #29
fffffe0007512528:      	and	w9, w11, #0xf
fffffe000751252c:      	cmp	w9, #0
fffffe0007512530:      	ccmp	w8, #0, #0, eq
fffffe0007512534:      	b.ne	0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512538:      	adrp	x0, 0xfffffe0008519000 <_ncd_native_mread+0x94>
fffffe000751253c:      	add	x0, x0, #32
fffffe0007512540:      	bl	0xfffffe00079acb40 <_nstx_idle>
fffffe0007512544:      	cbz	w0, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512548:      	adrp	x8, 0xfffffe0008516000 <_ncd_native_mread+0x98>
fffffe000751254c:      	add	x8, x8, #2164
fffffe0007512550:      	mov	w0, wzr
fffffe0007512554:      	ldp	w9, w8, [x8]
fffffe0007512558:      	cmp	w9, w8
fffffe000751255c:      	b.ne	0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512560:      	adrp	x8, 0xfffffe00084dd000 <_ncd_native_write+0x14>
fffffe0007512564:      	ldr	w8, [x8, #2468]
fffffe0007512568:      	cbnz	w8, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe000751256c:      	adrp	x8, 0xfffffe0008526000 <_ncd_native_mwrite+0x48>
fffffe0007512570:      	ldr	w8, [x8, #2668]
fffffe0007512574:      	cbnz	w8, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512578:      	adrp	x8, 0xfffffe0007c60000 <_ni_thread+0x7fc>
fffffe000751257c:      	ldr	w8, [x8, #948]
fffffe0007512580:      	cbnz	w8, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512584:      	adrp	x8, 0xfffffe0008528000 <_ncd_native_mwrite+0x68>
fffffe0007512588:      	ldr	w8, [x8, #3228]
fffffe000751258c:      	cbnz	w8, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512590:      	adrp	x8, 0xfffffe00084f8000 <_ncd_native_mread+0x68>
fffffe0007512594:      	add	x8, x8, #556
fffffe0007512598:      	ldr	w9, [x8]
fffffe000751259c:      	cbnz	w9, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe00075125a0:      	ldr	w9, [x8, #28]
fffffe00075125a4:      	cbnz	w9, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe00075125a8:      	add	x9, x8, #332
fffffe00075125ac:      	ldr	x9, [x9]
fffffe00075125b0:      	cbnz	x9, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe00075125b4:      	add	x9, x8, #340
fffffe00075125b8:      	ldr	x9, [x9]
fffffe00075125bc:      	cbnz	x9, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe00075125c0:      	ldr	w8, [x8, #136]
fffffe00075125c4:      	cbz	w8, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe00075125c8:      	adrp	x8, 0xfffffe0008516000 <_ncd_native_mwrite+0x64>
fffffe00075125cc:      	adrp	x9, 0xfffffe0008528000 <_ncd_native_mwrite+0xb0>
fffffe00075125d0:      	ldr	x8, [x8, #1984]
fffffe00075125d4:      	ldr	x9, [x9, #3288]
fffffe00075125d8:      	cmp	x8, x9
fffffe00075125dc:      	b.ne	0xfffffe000751265c <_njs_rejoin_ready_locked+0x1f4>
fffffe00075125e0:      	adrp	x0, 0xfffffe0008526000 <_ncd_native_sync+0x8>
fffffe00075125e4:      	add	x0, x0, #2680
fffffe00075125e8:      	bl	0xfffffe000799c814 <_nct_done_receipt>
fffffe00075125ec:      	cbz	w0, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe00075125f0:      	adrp	x0, 0xfffffe000852b000 <_ncd_native_sync+0x2c>
fffffe00075125f4:      	add	x0, x0, #1440
fffffe00075125f8:      	bl	0xfffffe00079acf40 <_nst_scan_admissible>
fffffe00075125fc:      	mov	w8, w0
fffffe0007512600:      	mov	w0, wzr
fffffe0007512604:      	cbz	w8, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512608:      	adrp	x8, 0xfffffe00084dd000 <_ncd_native_mread+0x74>
fffffe000751260c:      	add	x8, x8, #2592
fffffe0007512610:      	ldr	w9, [x8]
fffffe0007512614:      	cbnz	w9, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512618:      	mov	w9, #18480
fffffe000751261c:      	ldr	w9, [x8, x9]
fffffe0007512620:      	cbnz	w9, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512624:      	mov	w9, #36960
fffffe0007512628:      	ldr	w9, [x8, x9]
fffffe000751262c:      	cbnz	w9, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe0007512630:      	mov	w9, #55440
fffffe0007512634:      	ldr	w8, [x8, x9]
fffffe0007512638:      	cbnz	w8, 0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe000751263c:      	adrp	x8, 0xfffffe0008516000 <_ncd_native_sync+0x24>
fffffe0007512640:      	add	x8, x8, #1984
fffffe0007512644:      	ldr	x9, [x8]
fffffe0007512648:      	ldr	w8, [x8, #48]
fffffe000751264c:      	cmp	x9, #255
fffffe0007512650:      	ccmp	w8, #28, #2, lo
fffffe0007512654:      	cset	w0, lo
fffffe0007512658:      	b	0xfffffe0007512660 <_njs_rejoin_ready_locked+0x1f8>
fffffe000751265c:      	mov	w0, wzr
fffffe0007512660:      	ldp	x29, x30, [sp, #16]
fffffe0007512664:      	add	sp, sp, #32
fffffe0007512668:      	ret
