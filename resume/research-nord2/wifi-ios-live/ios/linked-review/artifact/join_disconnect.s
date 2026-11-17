
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007512730 <_nord2_wifi_join_disconnect>:
fffffe0007512730:      	stp	x22, x21, [sp, #-48]!
fffffe0007512734:      	stp	x20, x19, [sp, #16]
fffffe0007512738:      	stp	x29, x30, [sp, #32]
fffffe000751273c:      	add	x29, sp, #32
fffffe0007512740:      	adrp	x8, 0xfffffe00084dd000 <_ncd_native_sync+0x44>
fffffe0007512744:      	add	x8, x8, #2464
fffffe0007512748:      	mov	w19, #1
fffffe000751274c:      	ldar	w8, [x8]
fffffe0007512750:      	cmp	w8, #1
fffffe0007512754:      	b.ne	0xfffffe00075128d0 <_nord2_wifi_join_disconnect+0x1a0>
fffffe0007512758:      	adrp	x8, 0xfffffe00084f8000 <_ncd_native_delay+0x1c>
fffffe000751275c:      	ldr	x0, [x8, #544]
fffffe0007512760:      	cbz	x0, 0xfffffe00075128d0 <_nord2_wifi_join_disconnect+0x1a0>
fffffe0007512764:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007512768:      	adrp	x8, 0xfffffe00084dd000 <_ncd_native_sync+0x6c>
fffffe000751276c:      	ldr	x0, [x8, #2456]
fffffe0007512770:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007512774:      	adrp	x8, 0xfffffe00084f8000 <_ncd_native_delay+0x38>
fffffe0007512778:      	add	x8, x8, #552
fffffe000751277c:      	ldp	w9, w8, [x8]
fffffe0007512780:      	orr	w9, w9, w8
fffffe0007512784:      	cbz	w9, 0xfffffe0007512840 <_nord2_wifi_join_disconnect+0x110>
fffffe0007512788:      	adrp	x9, 0xfffffe00084f8000 <_ncd_native_delay+0x4c>
fffffe000751278c:      	add	x9, x9, #564
fffffe0007512790:      	mov	w10, #1
fffffe0007512794:      	ldur	x19, [x9, #188]
fffffe0007512798:      	str	w10, [x9]
fffffe000751279c:      	cbz	w8, 0xfffffe00075127ac <_nord2_wifi_join_disconnect+0x7c>
fffffe00075127a0:      	adrp	x0, 0xfffffe0008516000 <_nst_native_rx_active+0x78>
fffffe00075127a4:      	add	x0, x0, #2072
fffffe00075127a8:      	bl	0xfffffe00079acb88 <_nst_close_port>
fffffe00075127ac:      	adrp	x21, 0xfffffe00084f8000 <_nst_native_rx_active+0xc>
fffffe00075127b0:      	add	x21, x21, #544
fffffe00075127b4:      	mov	w1, #12112
fffffe00075127b8:      	ldr	w8, [x21, #64]
fffffe00075127bc:      	ldr	w9, [x21, #132]
fffffe00075127c0:      	add	w8, w9, w8
fffffe00075127c4:      	add	x9, x21, #21, lsl #12
fffffe00075127c8:      	add	x0, x9, #1872
fffffe00075127cc:      	str	w8, [x21, #132]
fffffe00075127d0:      	bl	0xfffffe00079a8bac <_niwj_clear>
fffffe00075127d4:      	adrp	x20, 0xfffffe00084dd000 <_ncd_native_delay+0x2c>
fffffe00075127d8:      	add	x20, x20, #2456
fffffe00075127dc:      	add	x8, x21, #24, lsl #12
fffffe00075127e0:      	mov	w9, #7
fffffe00075127e4:      	movi.2d	v0, #0000000000000000
fffffe00075127e8:      	add	x8, x8, #1696
fffffe00075127ec:      	ldr	x0, [x20]
fffffe00075127f0:      	stur	xzr, [x21, #60]
fffffe00075127f4:      	stur	xzr, [x21, #52]
fffffe00075127f8:      	str	xzr, [x21, #3288]
fffffe00075127fc:      	stp	q0, q0, [x8]
fffffe0007512800:      	str	xzr, [x21, #3280]
fffffe0007512804:      	str	w9, [x21, #3272]
fffffe0007512808:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751280c:      	mov	x0, x19
fffffe0007512810:      	mov	w1, wzr
fffffe0007512814:      	bl	0xfffffe0007af1854 <_nord2_wifi_ifnet_link>
fffffe0007512818:      	ldr	x8, [x21]
fffffe000751281c:      	cmp	w0, #0
fffffe0007512820:      	csetm	w19, ne
fffffe0007512824:      	mov	x0, x8
fffffe0007512828:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751282c:      	mov	x0, x20
fffffe0007512830:      	mov	w1, wzr
fffffe0007512834:      	mov	w2, wzr
fffffe0007512838:      	bl	0xfffffe000736d248 <_thread_wakeup_prim>
fffffe000751283c:      	b	0xfffffe00075128d0 <_nord2_wifi_join_disconnect+0x1a0>
fffffe0007512840:      	adrp	x20, 0xfffffe00084f8000 <_nst_native_rx_active+0xa0>
fffffe0007512844:      	add	x20, x20, #544
fffffe0007512848:      	adrp	x8, 0xfffffe00084dd000 <_nst_native_rx_active+0x3c>
fffffe000751284c:      	add	x8, x8, #2456
fffffe0007512850:      	add	x9, x20, #40
fffffe0007512854:      	add	x10, x8, #12
fffffe0007512858:      	ldur	d0, [x20, #28]
fffffe000751285c:      	adrp	x11, 0xfffffe0008524000 <_nwfn_memory_range+0x18>
fffffe0007512860:      	ldr	s1, [x20, #3272]
fffffe0007512864:      	ldur	d2, [x20, #52]
fffffe0007512868:      	ld1.s	{ v0 }[1], [x9]
fffffe000751286c:      	adrp	x9, 0xfffffe000700b000 <_nwfn_readable+0xc4>
fffffe0007512870:      	ld1.s	{ v1 }[1], [x10]
fffffe0007512874:      	ldr	d3, [x11, #424]
fffffe0007512878:      	ldr	x0, [x8]
fffffe000751287c:      	mov.d	v0[1], v2[0]
fffffe0007512880:      	ldr	q2, [x9, #3936]
fffffe0007512884:      	mov.d	v1[1], v3[0]
fffffe0007512888:      	ldp	x10, x9, [x20, #344]
fffffe000751288c:      	cmtst.4s	v0, v0, v0
fffffe0007512890:      	cmeq.4s	v1, v1, v2
fffffe0007512894:      	mvn.16b	v1, v1
fffffe0007512898:      	cmp	x9, #0
fffffe000751289c:      	uzp1.8h	v0, v0, v1
fffffe00075128a0:      	cset	w9, ne
fffffe00075128a4:      	cmp	x10, #0
fffffe00075128a8:      	cset	w11, ne
fffffe00075128ac:      	xtn.8b	v0, v0
fffffe00075128b0:      	umaxv.8b	b0, v0
fffffe00075128b4:      	fmov	w10, s0
fffffe00075128b8:      	orr	w9, w10, w9
fffffe00075128bc:      	orr	w9, w9, w11
fffffe00075128c0:      	and	w19, w9, #0x1
fffffe00075128c4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe00075128c8:      	ldr	x0, [x20]
fffffe00075128cc:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe00075128d0:      	mov	w0, w19
fffffe00075128d4:      	ldp	x29, x30, [sp, #32]
fffffe00075128d8:      	ldp	x20, x19, [sp, #16]
fffffe00075128dc:      	ldp	x22, x21, [sp], #48
fffffe00075128e0:      	ret
