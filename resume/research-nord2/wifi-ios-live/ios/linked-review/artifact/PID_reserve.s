
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b3318 <_nsdd_pid_reserve>:
fffffe00079b3318:      	sub	sp, sp, #128
fffffe00079b331c:      	stp	x24, x23, [sp, #64]
fffffe00079b3320:      	stp	x22, x21, [sp, #80]
fffffe00079b3324:      	stp	x20, x19, [sp, #96]
fffffe00079b3328:      	stp	x29, x30, [sp, #112]
fffffe00079b332c:      	add	x29, sp, #112
fffffe00079b3330:      	mov	x20, x0
fffffe00079b3334:      	mov	w0, wzr
fffffe00079b3338:      	cbz	x4, 0xfffffe00079b34f4 <_nsdd_pid_reserve+0x1dc>
fffffe00079b333c:      	mov	w22, w3
fffffe00079b3340:      	cmp	w3, #1
fffffe00079b3344:      	b.hi	0xfffffe00079b34f4 <_nsdd_pid_reserve+0x1dc>
fffffe00079b3348:      	cbz	x20, 0xfffffe00079b34f4 <_nsdd_pid_reserve+0x1dc>
fffffe00079b334c:      	mov	w23, w1
fffffe00079b3350:      	cmp	w1, #254
fffffe00079b3354:      	b.hi	0xfffffe00079b34f4 <_nsdd_pid_reserve+0x1dc>
fffffe00079b3358:      	mov	w19, w2
fffffe00079b335c:      	sub	w8, w2, #4
fffffe00079b3360:      	cmn	w8, #3
fffffe00079b3364:      	b.lo	0xfffffe00079b34f4 <_nsdd_pid_reserve+0x1dc>
fffffe00079b3368:      	mov	x21, x5
fffffe00079b336c:      	mov	x24, x4
fffffe00079b3370:      	cmp	w19, #1
fffffe00079b3374:      	stp	xzr, xzr, [sp, #40]
fffffe00079b3378:      	str	wzr, [sp, #56]
fffffe00079b337c:      	b.ne	0xfffffe00079b339c <_nsdd_pid_reserve+0x84>
fffffe00079b3380:      	sub	x8, x21, #1569
fffffe00079b3384:      	cmn	x8, #1543
fffffe00079b3388:      	b.lo	0xfffffe00079b33b0 <_nsdd_pid_reserve+0x98>
fffffe00079b338c:      	ldrb	w8, [x24]
fffffe00079b3390:      	mov	w9, wzr
fffffe00079b3394:      	lsr	w8, w8, #4
fffffe00079b3398:      	b	0xfffffe00079b3428 <_nsdd_pid_reserve+0x110>
fffffe00079b339c:      	add	x0, sp, #40
fffffe00079b33a0:      	mov	x1, x24
fffffe00079b33a4:      	mov	x2, x21
fffffe00079b33a8:      	bl	0xfffffe00079b2f90 <_nsdd_classify>
fffffe00079b33ac:      	cbz	w0, 0xfffffe00079b33b8 <_nsdd_pid_reserve+0xa0>
fffffe00079b33b0:      	mov	w0, wzr
fffffe00079b33b4:      	b	0xfffffe00079b34f4 <_nsdd_pid_reserve+0x1dc>
fffffe00079b33b8:      	mov	w8, wzr
fffffe00079b33bc:      	ldr	w9, [sp, #40]
fffffe00079b33c0:      	cmp	x21, #21
fffffe00079b33c4:      	b.lo	0xfffffe00079b3428 <_nsdd_pid_reserve+0x110>
fffffe00079b33c8:      	cmp	w19, #2
fffffe00079b33cc:      	b.ne	0xfffffe00079b3428 <_nsdd_pid_reserve+0x110>
fffffe00079b33d0:      	mov	w10, #34958
fffffe00079b33d4:      	cmp	w9, w10
fffffe00079b33d8:      	b.ne	0xfffffe00079b3428 <_nsdd_pid_reserve+0x110>
fffffe00079b33dc:      	ldrb	w8, [x24, #15]
fffffe00079b33e0:      	cmp	w8, #3
fffffe00079b33e4:      	b.ne	0xfffffe00079b3408 <_nsdd_pid_reserve+0xf0>
fffffe00079b33e8:      	ldurh	w8, [x24, #19]
fffffe00079b33ec:      	rev16	w8, w8
fffffe00079b33f0:      	cmp	w8, #266
fffffe00079b33f4:      	b.eq	0xfffffe00079b3410 <_nsdd_pid_reserve+0xf8>
fffffe00079b33f8:      	cmp	w8, #778
fffffe00079b33fc:      	b.ne	0xfffffe00079b3418 <_nsdd_pid_reserve+0x100>
fffffe00079b3400:      	mov	w8, #2
fffffe00079b3404:      	b	0xfffffe00079b3424 <_nsdd_pid_reserve+0x10c>
fffffe00079b3408:      	mov	w8, wzr
fffffe00079b340c:      	b	0xfffffe00079b3424 <_nsdd_pid_reserve+0x10c>
fffffe00079b3410:      	mov	w8, #1
fffffe00079b3414:      	b	0xfffffe00079b3424 <_nsdd_pid_reserve+0x10c>
fffffe00079b3418:      	cmp	w8, #770
fffffe00079b341c:      	mov	w8, #3
fffffe00079b3420:      	cinc	w8, w8, ne
fffffe00079b3424:      	mov	w9, #34958
fffffe00079b3428:      	cmp	w19, #2
fffffe00079b342c:      	mov	w10, #126
fffffe00079b3430:      	mov	w11, #63
fffffe00079b3434:      	adrp	x12, 0xfffffe0007bfc000 <_nsdd_publish+0x84>
fffffe00079b3438:      	add	x12, x12, #3616
fffffe00079b343c:      	csel	w10, w11, w10, eq
fffffe00079b3440:      	subs	w11, w19, #1
fffffe00079b3444:      	mov	w13, #31
fffffe00079b3448:      	csel	w10, w13, w10, eq
fffffe00079b344c:      	ldr	w0, [x12, w11, uxtw #2]
fffffe00079b3450:      	cmp	w0, w10
fffffe00079b3454:      	b.ls	0xfffffe00079b34a8 <_nsdd_pid_reserve+0x190>
fffffe00079b3458:      	adrp	x9, 0xfffffe0008544000 <_stc_receipt+0x180>
fffffe00079b345c:      	add	x9, x9, #3376
fffffe00079b3460:      	ldr	w8, [x9, x11, lsl #2]
fffffe00079b3464:      	cmn	w8, #1
fffffe00079b3468:      	b.eq	0xfffffe00079b3498 <_nsdd_pid_reserve+0x180>
fffffe00079b346c:      	add	w12, w8, #1
fffffe00079b3470:      	str	w12, [x9, x11, lsl #2]
fffffe00079b3474:      	cbnz	w8, 0xfffffe00079b3498 <_nsdd_pid_reserve+0x180>
fffffe00079b3478:      	cmp	w19, #1
fffffe00079b347c:      	mov	w8, #127
fffffe00079b3480:      	csel	x8, xzr, x8, eq
fffffe00079b3484:      	adrp	x0, 0xfffffe0007134000 <_nst_peer_begin+0x538>
fffffe00079b3488:      	add	x0, x0, #1363
fffffe00079b348c:      	stp	x20, x19, [sp]
fffffe00079b3490:      	stp	x10, x8, [sp, #16]
fffffe00079b3494:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b3498:      	cmp	w19, #1
fffffe00079b349c:      	mov	w8, #127
fffffe00079b34a0:      	csel	w0, wzr, w8, eq
fffffe00079b34a4:      	b	0xfffffe00079b34f4 <_nsdd_pid_reserve+0x1dc>
fffffe00079b34a8:      	mov	w10, #72
fffffe00079b34ac:      	adrp	x13, 0xfffffe0008544000 <_stc_receipt+0x1d4>
fffffe00079b34b0:      	add	x13, x13, #3392
fffffe00079b34b4:      	ldur	d0, [sp, #52]
fffffe00079b34b8:      	madd	x10, x0, x10, x13
fffffe00079b34bc:      	add	w13, w0, #1
fffffe00079b34c0:      	str	w13, [x12, x11, lsl #2]
fffffe00079b34c4:      	str	w9, [x10, #24]
fffffe00079b34c8:      	adrp	x9, 0xfffffe0007031000 <_nst_peer_begin+0x170>
fffffe00079b34cc:      	stur	d0, [x10, #28]
fffffe00079b34d0:      	str	x20, [x10]
fffffe00079b34d4:      	ldr	d0, [x9, #320]
fffffe00079b34d8:      	stp	w0, w23, [x10, #8]
fffffe00079b34dc:      	stp	w19, w22, [x10, #16]
fffffe00079b34e0:      	stp	w21, w8, [x10, #36]
fffffe00079b34e4:      	stur	xzr, [x10, #52]
fffffe00079b34e8:      	stur	xzr, [x10, #44]
fffffe00079b34ec:      	str	wzr, [x10, #60]
fffffe00079b34f0:      	str	d0, [x10, #64]
fffffe00079b34f4:      	ldp	x29, x30, [sp, #112]
fffffe00079b34f8:      	ldp	x20, x19, [sp, #96]
fffffe00079b34fc:      	ldp	x22, x21, [sp, #80]
fffffe00079b3500:      	ldp	x24, x23, [sp, #64]
fffffe00079b3504:      	add	sp, sp, #128
fffffe00079b3508:      	ret
