
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a5420 <_niwi_scan_admit>:
fffffe00079a5420:      	sub	sp, sp, #112
fffffe00079a5424:      	stp	x20, x19, [sp, #96]
fffffe00079a5428:      	mov	w8, #-1
fffffe00079a542c:      	cbz	x0, 0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a5430:      	mov	w9, #5456
fffffe00079a5434:      	cmp	x1, x9
fffffe00079a5438:      	b.ne	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a543c:      	cbz	x2, 0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a5440:      	cbz	x3, 0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a5444:      	ldr	w9, [x2, #12]
fffffe00079a5448:      	sub	x8, x9, #40
fffffe00079a544c:      	cmn	x8, #39
fffffe00079a5450:      	b.hs	0xfffffe00079a545c <_niwi_scan_admit+0x3c>
fffffe00079a5454:      	mov	w8, #-3
fffffe00079a5458:      	b	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a545c:      	ldr	w8, [x0]
fffffe00079a5460:      	cmp	w8, #1
fffffe00079a5464:      	b.ne	0xfffffe00079a54d4 <_niwi_scan_admit+0xb4>
fffffe00079a5468:      	ldrb	w8, [x0, #4]
fffffe00079a546c:      	ldrb	w10, [x0, #5]
fffffe00079a5470:      	ldrb	w11, [x0, #6]
fffffe00079a5474:      	ldrb	w12, [x0, #7]
fffffe00079a5478:      	and	w8, w8, #0xfe
fffffe00079a547c:      	bfi	w8, w10, #8, #8
fffffe00079a5480:      	bfi	w8, w11, #16, #8
fffffe00079a5484:      	bfi	w8, w12, #24, #8
fffffe00079a5488:      	cmp	w8, #2
fffffe00079a548c:      	b.ne	0xfffffe00079a54d4 <_niwi_scan_admit+0xb4>
fffffe00079a5490:      	ldr	w10, [x0, #56]
fffffe00079a5494:      	mov	w8, #-2
fffffe00079a5498:      	cmp	w10, #1
fffffe00079a549c:      	b.ne	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a54a0:      	ldr	w10, [x0, #68]
fffffe00079a54a4:      	cmp	w10, #39
fffffe00079a54a8:      	b.hi	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a54ac:      	ldr	w11, [x0, #52]
fffffe00079a54b0:      	sub	w11, w11, #4
fffffe00079a54b4:      	cmn	w11, #3
fffffe00079a54b8:      	b.lo	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a54bc:      	mov	w8, #4872
fffffe00079a54c0:      	mov	x11, xzr
fffffe00079a54c4:      	add	x8, x0, x8
fffffe00079a54c8:      	add	x13, x2, #17
fffffe00079a54cc:      	ldr	w12, [x8]
fffffe00079a54d0:      	b	0xfffffe00079a54f4 <_niwi_scan_admit+0xd4>
fffffe00079a54d4:      	mov	w8, #-2
fffffe00079a54d8:      	ldp	x20, x19, [sp, #96]
fffffe00079a54dc:      	mov	w0, w8
fffffe00079a54e0:      	add	sp, sp, #112
fffffe00079a54e4:      	ret
fffffe00079a54e8:      	add	x11, x11, #1
fffffe00079a54ec:      	cmp	x9, w11, uxtw
fffffe00079a54f0:      	b.eq	0xfffffe00079a5550 <_niwi_scan_admit+0x130>
fffffe00079a54f4:      	add	x14, x2, w11, uxtw #1
fffffe00079a54f8:      	ldrb	w14, [x14, #16]
fffffe00079a54fc:      	sub	w15, w14, #1
fffffe00079a5500:      	cmp	w15, #1
fffffe00079a5504:      	b.hi	0xfffffe00079a5454 <_niwi_scan_admit+0x34>
fffffe00079a5508:      	and	x16, x11, #0xffffffff
fffffe00079a550c:      	add	x15, x2, x16, lsl #1
fffffe00079a5510:      	ldrb	w15, [x15, #17]
fffffe00079a5514:      	cbz	w15, 0xfffffe00079a5454 <_niwi_scan_admit+0x34>
fffffe00079a5518:      	cbz	w16, 0xfffffe00079a54e8 <_niwi_scan_admit+0xc8>
fffffe00079a551c:      	mov	x16, x11
fffffe00079a5520:      	mov	x17, x13
fffffe00079a5524:      	b	0xfffffe00079a5534 <_niwi_scan_admit+0x114>
fffffe00079a5528:      	add	x17, x17, #2
fffffe00079a552c:      	subs	x16, x16, #1
fffffe00079a5530:      	b.eq	0xfffffe00079a54e8 <_niwi_scan_admit+0xc8>
fffffe00079a5534:      	ldurb	w1, [x17, #-1]
fffffe00079a5538:      	cmp	w14, w1
fffffe00079a553c:      	b.ne	0xfffffe00079a5528 <_niwi_scan_admit+0x108>
fffffe00079a5540:      	ldrb	w1, [x17]
fffffe00079a5544:      	cmp	w15, w1
fffffe00079a5548:      	b.ne	0xfffffe00079a5528 <_niwi_scan_admit+0x108>
fffffe00079a554c:      	b	0xfffffe00079a5454 <_niwi_scan_admit+0x34>
fffffe00079a5550:      	ldr	w11, [x0, #16]
fffffe00079a5554:      	cbz	w11, 0xfffffe00079a5560 <_niwi_scan_admit+0x140>
fffffe00079a5558:      	mov	w8, #-4
fffffe00079a555c:      	b	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a5560:      	ldrb	w11, [x0, #60]
fffffe00079a5564:      	ldrb	w14, [x0, #61]
fffffe00079a5568:      	mov	w13, w11
fffffe00079a556c:      	bfi	w13, w14, #8, #8
fffffe00079a5570:      	cmp	w13, #110
fffffe00079a5574:      	b.eq	0xfffffe00079a557c <_niwi_scan_admit+0x15c>
fffffe00079a5578:      	cbnz	w13, 0xfffffe00079a5558 <_niwi_scan_admit+0x138>
fffffe00079a557c:      	ldr	w13, [x0, #64]
fffffe00079a5580:      	cbnz	w13, 0xfffffe00079a5558 <_niwi_scan_admit+0x138>
fffffe00079a5584:      	ldr	w13, [x8, #8]
fffffe00079a5588:      	mov	w14, #-2147483648
fffffe00079a558c:      	orr	w13, w13, #0x80000000
fffffe00079a5590:      	cmp	w13, w14
fffffe00079a5594:      	b.ne	0xfffffe00079a5558 <_niwi_scan_admit+0x138>
fffffe00079a5598:      	ldr	w13, [x8, #12]
fffffe00079a559c:      	cbnz	w13, 0xfffffe00079a5558 <_niwi_scan_admit+0x138>
fffffe00079a55a0:      	ldr	w13, [x8, #576]
fffffe00079a55a4:      	cbnz	w13, 0xfffffe00079a5558 <_niwi_scan_admit+0x138>
fffffe00079a55a8:      	ldr	w8, [x8, #4]
fffffe00079a55ac:      	cbz	w8, 0xfffffe00079a55b8 <_niwi_scan_admit+0x198>
fffffe00079a55b0:      	mov	w8, #-5
fffffe00079a55b4:      	b	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a55b8:      	mov	w8, #-5
fffffe00079a55bc:      	cmp	w12, #32
fffffe00079a55c0:      	b.hi	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a55c4:      	mov	w13, #1
fffffe00079a55c8:      	lsl	x12, x13, x12
fffffe00079a55cc:      	mov	x13, #5
fffffe00079a55d0:      	movk	x13, #1, lsl #32
fffffe00079a55d4:      	tst	x12, x13
fffffe00079a55d8:      	b.eq	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a55dc:      	ldrb	w8, [x0, #12]
fffffe00079a55e0:      	ldrb	w12, [x0, #13]
fffffe00079a55e4:      	ldrb	w13, [x0, #11]
fffffe00079a55e8:      	ldrb	w14, [x0, #10]
fffffe00079a55ec:      	orr	w15, w12, w8
fffffe00079a55f0:      	and	w8, w12, w8
fffffe00079a55f4:      	ldrb	w12, [x0, #9]
fffffe00079a55f8:      	orr	w15, w15, w13
fffffe00079a55fc:      	ldrb	w16, [x0, #8]
fffffe00079a5600:      	and	w8, w8, w13
fffffe00079a5604:      	orr	w13, w15, w14
fffffe00079a5608:      	and	w8, w8, w14
fffffe00079a560c:      	orr	w13, w13, w12
fffffe00079a5610:      	and	w8, w8, w12
fffffe00079a5614:      	orr	w12, w13, w16
fffffe00079a5618:      	and	w8, w8, w16
fffffe00079a561c:      	mov	w13, #255
fffffe00079a5620:      	and	w12, w12, #0xff
fffffe00079a5624:      	bics	wzr, w13, w8
fffffe00079a5628:      	cset	w8, eq
fffffe00079a562c:      	cbz	w12, 0xfffffe00079a563c <_niwi_scan_admit+0x21c>
fffffe00079a5630:      	cbnz	w8, 0xfffffe00079a563c <_niwi_scan_admit+0x21c>
fffffe00079a5634:      	mov	w8, #-6
fffffe00079a5638:      	b	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a563c:      	movi.2d	v0, #0000000000000000
fffffe00079a5640:      	str	wzr, [sp, #80]
fffffe00079a5644:      	stp	q0, q0, [sp, #16]
fffffe00079a5648:      	stp	q0, q0, [sp, #48]
fffffe00079a564c:      	str	q0, [sp]
fffffe00079a5650:      	cbz	w10, 0xfffffe00079a57b8 <_niwi_scan_admit+0x398>
fffffe00079a5654:      	mov	w8, #5
fffffe00079a5658:      	mov	x14, sp
fffffe00079a565c:      	mov	x12, xzr
fffffe00079a5660:      	add	x13, x0, #72
fffffe00079a5664:      	add	x15, x2, #17
fffffe00079a5668:      	orr	x16, x14, x8
fffffe00079a566c:      	mov	w0, #1
fffffe00079a5670:      	mov	w17, #125
fffffe00079a5674:      	b	0xfffffe00079a5698 <_niwi_scan_admit+0x278>
fffffe00079a5678:      	add	x8, x2, x8, lsl #1
fffffe00079a567c:      	add	x1, x14, x12, lsl #1
fffffe00079a5680:      	add	x12, x12, #1
fffffe00079a5684:      	cmp	x12, x10
fffffe00079a5688:      	ldrh	w8, [x8, #16]
fffffe00079a568c:      	cset	w0, lo
fffffe00079a5690:      	strh	w8, [x1, #4]
fffffe00079a5694:      	b.eq	0xfffffe00079a57b8 <_niwi_scan_admit+0x398>
fffffe00079a5698:      	add	w8, w12, w12, lsl #1
fffffe00079a569c:      	lsl	w8, w8, #2
fffffe00079a56a0:      	add	x4, x13, x8
fffffe00079a56a4:      	ldp	w8, w1, [x4]
fffffe00079a56a8:      	cmp	w8, #1
fffffe00079a56ac:      	mov	w8, #-7
fffffe00079a56b0:      	ccmp	w1, #0, #4, eq
fffffe00079a56b4:      	b.eq	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a56b8:      	cmp	w1, #255
fffffe00079a56bc:      	b.hi	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a56c0:      	ldrb	w8, [x4, #8]
fffffe00079a56c4:      	ldrb	w5, [x4, #9]
fffffe00079a56c8:      	ldrb	w6, [x4, #10]
fffffe00079a56cc:      	ldrb	w4, [x4, #11]
fffffe00079a56d0:      	and	w8, w8, w17
fffffe00079a56d4:      	bfi	w8, w5, #8, #8
fffffe00079a56d8:      	bfi	w8, w6, #16, #8
fffffe00079a56dc:      	bfi	w8, w4, #24, #8
fffffe00079a56e0:      	cbz	w8, 0xfffffe00079a5700 <_niwi_scan_admit+0x2e0>
fffffe00079a56e4:      	cmp	w8, #8
fffffe00079a56e8:      	b.eq	0xfffffe00079a570c <_niwi_scan_admit+0x2ec>
fffffe00079a56ec:      	cmp	w8, #16
fffffe00079a56f0:      	b.ne	0xfffffe00079a57f4 <_niwi_scan_admit+0x3d4>
fffffe00079a56f4:      	mov	w5, wzr
fffffe00079a56f8:      	mov	w4, #2
fffffe00079a56fc:      	b	0xfffffe00079a5714 <_niwi_scan_admit+0x2f4>
fffffe00079a5700:      	mov	w4, wzr
fffffe00079a5704:      	mov	w5, #1
fffffe00079a5708:      	b	0xfffffe00079a5714 <_niwi_scan_admit+0x2f4>
fffffe00079a570c:      	mov	w5, wzr
fffffe00079a5710:      	mov	w4, #1
fffffe00079a5714:      	mov	x6, xzr
fffffe00079a5718:      	mov	x7, x15
fffffe00079a571c:      	mov	x8, x9
fffffe00079a5720:      	b	0xfffffe00079a5740 <_niwi_scan_admit+0x320>
fffffe00079a5724:      	cmp	x8, x9
fffffe00079a5728:      	b.ne	0xfffffe00079a57ac <_niwi_scan_admit+0x38c>
fffffe00079a572c:      	and	x8, x6, #0xffffffff
fffffe00079a5730:      	add	x6, x6, #1
fffffe00079a5734:      	add	x7, x7, #2
fffffe00079a5738:      	cmp	x9, x6
fffffe00079a573c:      	b.eq	0xfffffe00079a5760 <_niwi_scan_admit+0x340>
fffffe00079a5740:      	ldrb	w19, [x7]
fffffe00079a5744:      	cmp	w1, w19
fffffe00079a5748:      	b.ne	0xfffffe00079a5730 <_niwi_scan_admit+0x310>
fffffe00079a574c:      	tbnz	w5, #0, 0xfffffe00079a5724 <_niwi_scan_admit+0x304>
fffffe00079a5750:      	ldurb	w19, [x7, #-1]
fffffe00079a5754:      	cmp	w4, w19
fffffe00079a5758:      	b.eq	0xfffffe00079a5724 <_niwi_scan_admit+0x304>
fffffe00079a575c:      	b	0xfffffe00079a5730 <_niwi_scan_admit+0x310>
fffffe00079a5760:      	cmp	x8, x9
fffffe00079a5764:      	b.eq	0xfffffe00079a57ac <_niwi_scan_admit+0x38c>
fffffe00079a5768:      	cbz	x12, 0xfffffe00079a5678 <_niwi_scan_admit+0x258>
fffffe00079a576c:      	add	x4, x2, x8, lsl #1
fffffe00079a5770:      	mov	x5, x12
fffffe00079a5774:      	add	x1, x4, #17
fffffe00079a5778:      	mov	x6, x16
fffffe00079a577c:      	ldrb	w4, [x4, #16]
fffffe00079a5780:      	b	0xfffffe00079a5790 <_niwi_scan_admit+0x370>
fffffe00079a5784:      	add	x6, x6, #2
fffffe00079a5788:      	subs	x5, x5, #1
fffffe00079a578c:      	b.eq	0xfffffe00079a5678 <_niwi_scan_admit+0x258>
fffffe00079a5790:      	ldurb	w7, [x6, #-1]
fffffe00079a5794:      	cmp	w7, w4
fffffe00079a5798:      	b.ne	0xfffffe00079a5784 <_niwi_scan_admit+0x364>
fffffe00079a579c:      	ldrb	w7, [x6]
fffffe00079a57a0:      	ldrb	w19, [x1]
fffffe00079a57a4:      	cmp	w7, w19
fffffe00079a57a8:      	b.ne	0xfffffe00079a5784 <_niwi_scan_admit+0x364>
fffffe00079a57ac:      	tbz	w0, #0, 0xfffffe00079a57b8 <_niwi_scan_admit+0x398>
fffffe00079a57b0:      	mov	w8, #-9
fffffe00079a57b4:      	b	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a57b8:      	ldp	q0, q1, [sp, #32]
fffffe00079a57bc:      	cmp	w11, #0
fffffe00079a57c0:      	mov	w9, #108
fffffe00079a57c4:      	csel	w9, wzr, w9, eq
fffffe00079a57c8:      	str	w10, [sp]
fffffe00079a57cc:      	mov	w8, wzr
fffffe00079a57d0:      	strh	w9, [sp, #82]
fffffe00079a57d4:      	stp	q0, q1, [x3, #32]
fffffe00079a57d8:      	ldr	q2, [sp, #64]
fffffe00079a57dc:      	ldp	q0, q3, [sp]
fffffe00079a57e0:      	str	q2, [x3, #64]
fffffe00079a57e4:      	ldr	w9, [sp, #80]
fffffe00079a57e8:      	stp	q0, q3, [x3]
fffffe00079a57ec:      	str	w9, [x3, #80]
fffffe00079a57f0:      	b	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
fffffe00079a57f4:      	mov	w8, #-8
fffffe00079a57f8:      	b	0xfffffe00079a54d8 <_niwi_scan_admit+0xb8>
