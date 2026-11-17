
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe000751216c <_nord2_wifi_join_enqueue>:
fffffe000751216c:      	sub	sp, sp, #96
fffffe0007512170:      	stp	x26, x25, [sp, #16]
fffffe0007512174:      	stp	x24, x23, [sp, #32]
fffffe0007512178:      	stp	x22, x21, [sp, #48]
fffffe000751217c:      	stp	x20, x19, [sp, #64]
fffffe0007512180:      	stp	x29, x30, [sp, #80]
fffffe0007512184:      	add	x29, sp, #80
fffffe0007512188:      	mov	x19, x4
fffffe000751218c:      	mov	x21, x3
fffffe0007512190:      	mov	x23, x0
fffffe0007512194:      	mov	x20, x2
fffffe0007512198:      	mov	x22, x1
fffffe000751219c:      	cbz	x0, 0xfffffe00075121fc <_nord2_wifi_join_enqueue+0x90>
fffffe00075121a0:      	ldrb	w8, [x23]
fffffe00075121a4:      	sub	w8, w8, #1
fffffe00075121a8:      	cmp	w8, #31
fffffe00075121ac:      	b.hi	0xfffffe00075121fc <_nord2_wifi_join_enqueue+0x90>
fffffe00075121b0:      	ldrb	w8, [x23, #33]
fffffe00075121b4:      	cmp	w8, #1
fffffe00075121b8:      	b.hi	0xfffffe00075121fc <_nord2_wifi_join_enqueue+0x90>
fffffe00075121bc:      	ldrb	w8, [x23, #94]
fffffe00075121c0:      	cmp	w8, #2
fffffe00075121c4:      	b.hi	0xfffffe00075121fc <_nord2_wifi_join_enqueue+0x90>
fffffe00075121c8:      	ldr	w8, [x23, #96]
fffffe00075121cc:      	cmp	w8, #1
fffffe00075121d0:      	b.ne	0xfffffe00075121fc <_nord2_wifi_join_enqueue+0x90>
fffffe00075121d4:      	ldr	w8, [x23, #100]
fffffe00075121d8:      	orr	w8, w8, #0x4
fffffe00075121dc:      	cmp	w8, #4
fffffe00075121e0:      	b.ne	0xfffffe00075121fc <_nord2_wifi_join_enqueue+0x90>
fffffe00075121e4:      	add	x0, x23, #72
fffffe00075121e8:      	adrp	x1, 0xfffffe000700e000 <_nwfn_start+0x124>
fffffe00075121ec:      	add	x1, x1, #160
fffffe00075121f0:      	mov	w2, #22
fffffe00075121f4:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00075121f8:      	cbz	w0, 0xfffffe000751241c <_nord2_wifi_join_enqueue+0x2b0>
fffffe00075121fc:      	mov	w8, wzr
fffffe0007512200:      	mov	w0, #-1
fffffe0007512204:      	cbz	x19, 0xfffffe00075123ec <_nord2_wifi_join_enqueue+0x280>
fffffe0007512208:      	cbz	x21, 0xfffffe00075123ec <_nord2_wifi_join_enqueue+0x280>
fffffe000751220c:      	cbz	x20, 0xfffffe00075123ec <_nord2_wifi_join_enqueue+0x280>
fffffe0007512210:      	cbz	x22, 0xfffffe00075123ec <_nord2_wifi_join_enqueue+0x280>
fffffe0007512214:      	cbz	w8, 0xfffffe00075123ec <_nord2_wifi_join_enqueue+0x280>
fffffe0007512218:      	adrp	x8, 0xfffffe00084dd000 <_nct_native_owner_window+0x1c4>
fffffe000751221c:      	add	x8, x8, #2464
fffffe0007512220:      	ldar	w8, [x8]
fffffe0007512224:      	cmp	w8, #1
fffffe0007512228:      	b.ne	0xfffffe00075123e8 <_nord2_wifi_join_enqueue+0x27c>
fffffe000751222c:      	add	x0, sp, #8
fffffe0007512230:      	stp	xzr, xzr, [sp]
fffffe0007512234:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe0007512238:      	ldr	x0, [sp, #8]
fffffe000751223c:      	mov	x1, sp
fffffe0007512240:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe0007512244:      	adrp	x24, 0xfffffe00084dd000 <_nct_native_owner_window+0x1f0>
fffffe0007512248:      	add	x24, x24, #2456
fffffe000751224c:      	ldr	x26, [sp]
fffffe0007512250:      	ldr	x0, [x24]
fffffe0007512254:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007512258:      	adrp	x8, 0xfffffe00084f8000 <_nct_native_owner_window+0x270>
fffffe000751225c:      	ldr	x8, [x8, #544]
fffffe0007512260:      	cbz	x8, 0xfffffe00075123dc <_nord2_wifi_join_enqueue+0x270>
fffffe0007512264:      	ldr	w8, [x24, #12]
fffffe0007512268:      	cbnz	w8, 0xfffffe00075123dc <_nord2_wifi_join_enqueue+0x270>
fffffe000751226c:      	adrp	x8, 0xfffffe00084f8000 <_nct_native_owner_window+0x284>
fffffe0007512270:      	ldr	w8, [x8, #572]
fffffe0007512274:      	cbz	w8, 0xfffffe00075123cc <_nord2_wifi_join_enqueue+0x260>
fffffe0007512278:      	bl	0xfffffe0007512468 <_njs_rejoin_ready_locked>
fffffe000751227c:      	cbz	w0, 0xfffffe00075123dc <_nord2_wifi_join_enqueue+0x270>
fffffe0007512280:      	adrp	x8, 0xfffffe00084f8000 <_nct_native_owner_window+0x298>
fffffe0007512284:      	ldr	x8, [x8, #728]
fffffe0007512288:      	cmp	x8, #254
fffffe000751228c:      	b.hi	0xfffffe00075123dc <_nord2_wifi_join_enqueue+0x270>
fffffe0007512290:      	adrp	x25, 0xfffffe00084f8000 <_nct_native_owner_window+0x2a8>
fffffe0007512294:      	add	x25, x25, #552
fffffe0007512298:      	ldr	w9, [x25, #4]
fffffe000751229c:      	cbnz	w9, 0xfffffe0007512408 <_nord2_wifi_join_enqueue+0x29c>
fffffe00075122a0:      	ldr	w9, [x25]
fffffe00075122a4:      	cbnz	w9, 0xfffffe0007512408 <_nord2_wifi_join_enqueue+0x29c>
fffffe00075122a8:      	ldr	w9, [x25, #32]
fffffe00075122ac:      	cbnz	w9, 0xfffffe0007512408 <_nord2_wifi_join_enqueue+0x29c>
fffffe00075122b0:      	ldr	x9, [x25, #344]
fffffe00075122b4:      	cbnz	x9, 0xfffffe0007512408 <_nord2_wifi_join_enqueue+0x29c>
fffffe00075122b8:      	mov	x10, #63439
fffffe00075122bc:      	lsr	x9, x26, #3
fffffe00075122c0:      	movk	x10, #58195, lsl #16
fffffe00075122c4:      	mov	w12, #4608
fffffe00075122c8:      	movk	x10, #39845, lsl #32
fffffe00075122cc:      	movk	w12, #122, lsl #16
fffffe00075122d0:      	movk	x10, #8388, lsl #48
fffffe00075122d4:      	add	x26, x8, #1
fffffe00075122d8:      	umulh	x9, x9, x10
fffffe00075122dc:      	mov	x10, #63439
fffffe00075122e0:      	movk	x10, #58195, lsl #16
fffffe00075122e4:      	lsr	x9, x9, #4
fffffe00075122e8:      	movk	x10, #39845, lsl #32
fffffe00075122ec:      	movk	x10, #8388, lsl #48
fffffe00075122f0:      	lsr	x11, x9, #3
fffffe00075122f4:      	umulh	x10, x11, x10
fffffe00075122f8:      	mov	w11, #1000
fffffe00075122fc:      	lsr	x10, x10, #4
fffffe0007512300:      	madd	x10, x10, x11, x12
fffffe0007512304:      	adrp	x11, 0xfffffe00084f8000 <_ncd_native_now+0x2c>
fffffe0007512308:      	add	x11, x11, #572
fffffe000751230c:      	add	x8, x11, #428
fffffe0007512310:      	str	wzr, [x11, #120]
fffffe0007512314:      	stur	x9, [x11, #140]
fffffe0007512318:      	add	x9, x11, #332
fffffe000751231c:      	stur	x10, [x11, #148]
fffffe0007512320:      	ldp	q1, q0, [x23, #16]
fffffe0007512324:      	ldr	q2, [x23]
fffffe0007512328:      	stp	q1, q0, [x9, #16]
fffffe000751232c:      	str	q2, [x9]
fffffe0007512330:      	ldp	q0, q1, [x23, #48]
fffffe0007512334:      	ldr	x10, [x23, #96]
fffffe0007512338:      	ldr	q2, [x23, #80]
fffffe000751233c:      	stur	x26, [x11, #156]
fffffe0007512340:      	stur	x26, [x11, #164]
fffffe0007512344:      	str	x10, [x8]
fffffe0007512348:      	ldr	w8, [x11]
fffffe000751234c:      	stp	q1, q2, [x9, #64]
fffffe0007512350:      	str	q0, [x9, #48]
fffffe0007512354:      	stur	x26, [x11, #180]
fffffe0007512358:      	cbz	w8, 0xfffffe0007512364 <_nord2_wifi_join_enqueue+0x1f8>
fffffe000751235c:      	adrp	x8, 0xfffffe0008516000 <_ncd_native_owner+0x50>
fffffe0007512360:      	str	x26, [x8, #2016]
fffffe0007512364:      	movi.2d	v0, #0000000000000000
fffffe0007512368:      	mov	w8, #1
fffffe000751236c:      	add	x0, x25, #3268
fffffe0007512370:      	mov	w1, #1428
fffffe0007512374:      	str	x20, [x25, #192]
fffffe0007512378:      	stp	x22, x21, [x25, #336]
fffffe000751237c:      	str	w8, [x25]
fffffe0007512380:      	str	wzr, [x25, #32]
fffffe0007512384:      	str	d0, [x25, #24]
fffffe0007512388:      	str	wzr, [x25, #16]
fffffe000751238c:      	str	d0, [x25, #8]
fffffe0007512390:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe0007512394:      	mov	w8, #2
fffffe0007512398:      	str	x26, [x25, #3240]
fffffe000751239c:      	str	x20, [x25, #3248]
fffffe00075123a0:      	str	x26, [x25, #3256]
fffffe00075123a4:      	str	w8, [x25, #3264]
fffffe00075123a8:      	str	x26, [x19]
fffffe00075123ac:      	ldr	x0, [x24]
fffffe00075123b0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe00075123b4:      	mov	x0, x24
fffffe00075123b8:      	mov	w1, wzr
fffffe00075123bc:      	mov	w2, wzr
fffffe00075123c0:      	bl	0xfffffe000736d248 <_thread_wakeup_prim>
fffffe00075123c4:      	mov	w0, wzr
fffffe00075123c8:      	b	0xfffffe00075123ec <_nord2_wifi_join_enqueue+0x280>
fffffe00075123cc:      	adrp	x8, 0xfffffe00084f8000 <_ncd_native_owner+0x48>
fffffe00075123d0:      	ldr	x8, [x8, #728]
fffffe00075123d4:      	cmp	x8, #255
fffffe00075123d8:      	b.lo	0xfffffe0007512290 <_nord2_wifi_join_enqueue+0x124>
fffffe00075123dc:      	adrp	x8, 0xfffffe00084dd000 <_ncd_native_now+0x98>
fffffe00075123e0:      	ldr	x0, [x8, #2456]
fffffe00075123e4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe00075123e8:      	mov	w0, #1
fffffe00075123ec:      	ldp	x29, x30, [sp, #80]
fffffe00075123f0:      	ldp	x20, x19, [sp, #64]
fffffe00075123f4:      	ldp	x22, x21, [sp, #48]
fffffe00075123f8:      	ldp	x24, x23, [sp, #32]
fffffe00075123fc:      	ldp	x26, x25, [sp, #16]
fffffe0007512400:      	add	sp, sp, #96
fffffe0007512404:      	ret
fffffe0007512408:      	adrp	x8, 0xfffffe00084dd000 <_ncd_native_owner+0x18>
fffffe000751240c:      	ldr	x0, [x8, #2456]
fffffe0007512410:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007512414:      	mov	w0, #2
fffffe0007512418:      	b	0xfffffe00075123ec <_nord2_wifi_join_enqueue+0x280>
fffffe000751241c:      	ldrb	w8, [x23, #33]
fffffe0007512420:      	cbz	w8, 0xfffffe0007512460 <_nord2_wifi_join_enqueue+0x2f4>
fffffe0007512424:      	ldrb	w8, [x23, #34]
fffffe0007512428:      	tbnz	w8, #0, 0xfffffe00075121fc <_nord2_wifi_join_enqueue+0x90>
fffffe000751242c:      	ldrb	w9, [x23, #35]
fffffe0007512430:      	ldrb	w10, [x23, #36]
fffffe0007512434:      	ldrb	w11, [x23, #37]
fffffe0007512438:      	ldrb	w12, [x23, #38]
fffffe000751243c:      	orr	w9, w9, w10
fffffe0007512440:      	ldrb	w10, [x23, #39]
fffffe0007512444:      	orr	w9, w9, w11
fffffe0007512448:      	orr	w9, w9, w12
fffffe000751244c:      	orr	w9, w9, w10
fffffe0007512450:      	orr	w8, w9, w8
fffffe0007512454:      	cmp	w8, #0
fffffe0007512458:      	cset	w8, ne
fffffe000751245c:      	b	0xfffffe0007512200 <_nord2_wifi_join_enqueue+0x94>
fffffe0007512460:      	mov	w8, #1
fffffe0007512464:      	b	0xfffffe0007512200 <_nord2_wifi_join_enqueue+0x94>
