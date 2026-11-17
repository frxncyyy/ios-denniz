
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4424 <_nsdd_event>:
fffffe00079b4424:      	sub	sp, sp, #176
fffffe00079b4428:      	stp	x22, x21, [sp, #128]
fffffe00079b442c:      	stp	x20, x19, [sp, #144]
fffffe00079b4430:      	stp	x29, x30, [sp, #160]
fffffe00079b4434:      	add	x29, sp, #160
fffffe00079b4438:      	cbz	x0, 0xfffffe00079b45f0 <_nsdd_event+0x1cc>
fffffe00079b443c:      	mov	w22, w4
fffffe00079b4440:      	cmp	w4, #1
fffffe00079b4444:      	b.hi	0xfffffe00079b45f0 <_nsdd_event+0x1cc>
fffffe00079b4448:      	mov	x21, x0
fffffe00079b444c:      	ldr	w8, [x0, #460]
fffffe00079b4450:      	cbnz	w8, 0xfffffe00079b45f0 <_nsdd_event+0x1cc>
fffffe00079b4454:      	mov	x20, x2
fffffe00079b4458:      	stp	xzr, xzr, [sp, #96]
fffffe00079b445c:      	str	wzr, [sp, #120]
fffffe00079b4460:      	str	xzr, [sp, #112]
fffffe00079b4464:      	cbz	x2, 0xfffffe00079b45f0 <_nsdd_event+0x1cc>
fffffe00079b4468:      	mov	x8, #-4097
fffffe00079b446c:      	add	x8, x3, x8
fffffe00079b4470:      	cmn	x8, #4009
fffffe00079b4474:      	b.lo	0xfffffe00079b45f0 <_nsdd_event+0x1cc>
fffffe00079b4478:      	ldrb	w8, [x20]
fffffe00079b447c:      	mov	x19, x1
fffffe00079b4480:      	ldrb	w10, [x20, #8]
fffffe00079b4484:      	ldrb	w9, [x20, #1]
fffffe00079b4488:      	ldrb	w11, [x20, #4]
fffffe00079b448c:      	stp	w3, w8, [sp, #80]
fffffe00079b4490:      	and	w8, w10, #0x1
fffffe00079b4494:      	stp	w11, w9, [sp, #88]
fffffe00079b4498:      	stp	w10, w8, [sp, #108]
fffffe00079b449c:      	tbz	w10, #0, 0xfffffe00079b44c0 <_nsdd_event+0x9c>
fffffe00079b44a0:      	ldrh	w8, [x20, #2]
fffffe00079b44a4:      	ldrb	w9, [x20, #5]
fffffe00079b44a8:      	ldrh	w10, [x20, #6]
fffffe00079b44ac:      	ldrb	w11, [x20, #9]
fffffe00079b44b0:      	stp	w8, w9, [sp, #96]
fffffe00079b44b4:      	ldrb	w8, [x20, #14]
fffffe00079b44b8:      	str	w10, [sp, #104]
fffffe00079b44bc:      	stp	w11, w8, [sp, #116]
fffffe00079b44c0:      	add	x1, sp, #80
fffffe00079b44c4:      	mov	x0, x19
fffffe00079b44c8:      	bl	0xfffffe00079b36f8 <_nsdd_pid_status>
fffffe00079b44cc:      	ldr	w8, [sp, #92]
fffffe00079b44d0:      	cbz	w8, 0xfffffe00079b4604 <_nsdd_event+0x1e0>
fffffe00079b44d4:      	mov	x9, x21
fffffe00079b44d8:      	ldr	w10, [x9, #148]!
fffffe00079b44dc:      	cmn	w10, #1
fffffe00079b44e0:      	b.eq	0xfffffe00079b44ec <_nsdd_event+0xc8>
fffffe00079b44e4:      	add	w10, w10, #1
fffffe00079b44e8:      	str	w10, [x9]
fffffe00079b44ec:      	ldr	w9, [sp, #84]
fffffe00079b44f0:      	cmp	w9, #127
fffffe00079b44f4:      	b.ne	0xfffffe00079b450c <_nsdd_event+0xe8>
fffffe00079b44f8:      	ldr	w10, [x21, #152]
fffffe00079b44fc:      	cmn	w10, #1
fffffe00079b4500:      	b.eq	0xfffffe00079b450c <_nsdd_event+0xe8>
fffffe00079b4504:      	add	w10, w10, #1
fffffe00079b4508:      	str	w10, [x21, #152]
fffffe00079b450c:      	cmp	w22, #0
fffffe00079b4510:      	mov	w10, #452
fffffe00079b4514:      	mov	w11, #448
fffffe00079b4518:      	mov	w12, #16
fffffe00079b451c:      	csel	x10, x11, x10, eq
fffffe00079b4520:      	mov	w13, #8
fffffe00079b4524:      	csel	w12, w13, w12, eq
fffffe00079b4528:      	ldr	w11, [x21, x10]
fffffe00079b452c:      	cmp	w11, w12
fffffe00079b4530:      	b.hs	0xfffffe00079b45f0 <_nsdd_event+0x1cc>
fffffe00079b4534:      	ldr	w12, [x21, #456]
fffffe00079b4538:      	cmp	w9, #127
fffffe00079b453c:      	ldp	w14, w16, [x21, #156]
fffffe00079b4540:      	ldr	w13, [sp, #88]
fffffe00079b4544:      	cset	w15, eq
fffffe00079b4548:      	add	w12, w12, #1
fffffe00079b454c:      	adrp	x0, 0xfffffe0007134000 <_decode+0x804>
fffffe00079b4550:      	add	x0, x0, #2002
fffffe00079b4554:      	add	w11, w11, #1
fffffe00079b4558:      	str	w11, [x21, x10]
fffffe00079b455c:      	str	w12, [x21, #456]
fffffe00079b4560:      	stp	x14, x16, [sp, #48]
fffffe00079b4564:      	stp	x8, x15, [sp, #32]
fffffe00079b4568:      	stp	x9, x13, [sp, #16]
fffffe00079b456c:      	stp	x12, x19, [sp]
fffffe00079b4570:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4574:      	ldp	w9, w10, [sp, #96]
fffffe00079b4578:      	ldp	w11, w12, [sp, #104]
fffffe00079b457c:      	ldp	w13, w14, [sp, #112]
fffffe00079b4580:      	ldr	w8, [x21, #456]
fffffe00079b4584:      	adrp	x0, 0xfffffe0007134000 <_decode+0x83c>
fffffe00079b4588:      	add	x0, x0, #2105
fffffe00079b458c:      	ldr	w15, [sp, #120]
fffffe00079b4590:      	stp	x10, x11, [sp, #16]
fffffe00079b4594:      	stp	x12, x13, [sp, #32]
fffffe00079b4598:      	stp	x14, x15, [sp, #48]
fffffe00079b459c:      	stp	x8, x9, [sp]
fffffe00079b45a0:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b45a4:      	ldr	w8, [sp, #112]
fffffe00079b45a8:      	cbz	w8, 0xfffffe00079b45f0 <_nsdd_event+0x1cc>
fffffe00079b45ac:      	ldrb	w8, [x20, #10]
fffffe00079b45b0:      	adrp	x0, 0xfffffe0007134000 <_decode+0x868>
fffffe00079b45b4:      	add	x0, x0, #2197
fffffe00079b45b8:      	ldrb	w9, [x20, #11]
fffffe00079b45bc:      	ldrb	w10, [x20, #12]
fffffe00079b45c0:      	ldrb	w11, [x20, #13]
fffffe00079b45c4:      	ldr	w12, [x21, #456]
fffffe00079b45c8:      	ldr	w13, [sp, #84]
fffffe00079b45cc:      	ldr	w14, [sp, #92]
fffffe00079b45d0:      	ldr	w15, [x20, #16]
fffffe00079b45d4:      	ldr	w16, [x20, #24]
fffffe00079b45d8:      	stp	x10, x11, [sp, #48]
fffffe00079b45dc:      	stp	x8, x9, [sp, #32]
fffffe00079b45e0:      	stp	x13, x14, [sp, #16]
fffffe00079b45e4:      	stp	x15, x16, [sp, #64]
fffffe00079b45e8:      	stp	x19, x12, [sp]
fffffe00079b45ec:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b45f0:      	ldp	x29, x30, [sp, #160]
fffffe00079b45f4:      	ldp	x20, x19, [sp, #144]
fffffe00079b45f8:      	ldp	x22, x21, [sp, #128]
fffffe00079b45fc:      	add	sp, sp, #176
fffffe00079b4600:      	ret
fffffe00079b4604:      	mov	x9, x21
fffffe00079b4608:      	ldr	w10, [x9, #144]!
fffffe00079b460c:      	cmn	w10, #1
fffffe00079b4610:      	b.ne	0xfffffe00079b44e4 <_nsdd_event+0xc0>
fffffe00079b4614:      	b	0xfffffe00079b44ec <_nsdd_event+0xc8>
