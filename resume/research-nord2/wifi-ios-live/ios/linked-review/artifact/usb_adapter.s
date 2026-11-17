
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b515c <_nwl_usb>:
fffffe00079b515c:      	sub	sp, sp, #304
fffffe00079b5160:      	stp	x28, x27, [sp, #272]
fffffe00079b5164:      	stp	x29, x30, [sp, #288]
fffffe00079b5168:      	add	x29, sp, #288
fffffe00079b516c:      	adrp	x9, 0xfffffe0007c9c000 <_stc_begin+0x78>
fffffe00079b5170:      	mov	x8, x0
fffffe00079b5174:      	mov	w0, wzr
fffffe00079b5178:      	nop
fffffe00079b517c:      	ldr	x9, [x9, #3440]
fffffe00079b5180:      	stur	x9, [x29, #-24]
fffffe00079b5184:      	cbz	x8, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b5188:      	cbz	x1, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b518c:      	cbz	x3, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b5190:      	ldrb	w13, [x8, #1]
fffffe00079b5194:      	sub	w9, w13, #99
fffffe00079b5198:      	cmn	w9, #3
fffffe00079b519c:      	b.hs	0xfffffe00079b51a8 <_nwl_usb+0x4c>
fffffe00079b51a0:      	mov	w0, #-1
fffffe00079b51a4:      	b	0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b51a8:      	str	wzr, [x3]
fffffe00079b51ac:      	cmp	w13, #97
fffffe00079b51b0:      	ldrb	w11, [x8, #2]
fffffe00079b51b4:      	ldrb	w12, [x8, #3]
fffffe00079b51b8:      	ldrh	w10, [x8, #4]
fffffe00079b51bc:      	ldrh	w9, [x8, #6]
fffffe00079b51c0:      	bfi	w11, w12, #8, #8
fffffe00079b51c4:      	ldrb	w8, [x8]
fffffe00079b51c8:      	b.ne	0xfffffe00079b525c <_nwl_usb+0x100>
fffffe00079b51cc:      	mov	w0, wzr
fffffe00079b51d0:      	cmp	w8, #64
fffffe00079b51d4:      	b.ne	0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b51d8:      	cbnz	w9, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b51dc:      	adrp	x8, 0xfffffe0008547000 <_nwpc_hmac+0x3b4>
fffffe00079b51e0:      	add	x8, x8, #276
fffffe00079b51e4:      	ldar	w8, [x8]
fffffe00079b51e8:      	cmp	w8, #1
fffffe00079b51ec:      	b.ne	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b51f0:      	lsl	w8, w12, #8
fffffe00079b51f4:      	sub	w8, w8, #8, lsl #12
fffffe00079b51f8:      	cmn	w8, #7, lsl #12
fffffe00079b51fc:      	b.lo	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b5200:      	and	w8, w11, #0xfff
fffffe00079b5204:      	cbz	w8, 0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b5208:      	lsr	w9, w12, #4
fffffe00079b520c:      	cmp	w9, #1
fffffe00079b5210:      	b.ne	0xfffffe00079b5218 <_nwl_usb+0xbc>
fffffe00079b5214:      	cbnz	w10, 0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b5218:      	adrp	x11, 0xfffffe0008547000 <_nwpc_pmk+0x18>
fffffe00079b521c:      	add	x11, x11, #292
fffffe00079b5220:      	ldar	w11, [x11]
fffffe00079b5224:      	cmp	w8, w11
fffffe00079b5228:      	b.ne	0xfffffe00079b55d0 <_nwl_usb+0x474>
fffffe00079b522c:      	adrp	x8, 0xfffffe0008547000 <_nwpc_pmk+0x2c>
fffffe00079b5230:      	add	x8, x8, #296
fffffe00079b5234:      	ldar	w8, [x8]
fffffe00079b5238:      	cmp	w9, w8
fffffe00079b523c:      	b.ne	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b5240:      	adrp	x8, 0xfffffe0008547000 <_nwpc_pmk+0x40>
fffffe00079b5244:      	add	x8, x8, #300
fffffe00079b5248:      	ldar	w8, [x8]
fffffe00079b524c:      	cmp	w8, w10
fffffe00079b5250:      	b.ne	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b5254:      	mov	w0, #2
fffffe00079b5258:      	b	0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b525c:      	mov	w0, wzr
fffffe00079b5260:      	cmp	w8, #192
fffffe00079b5264:      	b.ne	0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b5268:      	cbnz	w11, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b526c:      	cbnz	w10, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b5270:      	cbz	w9, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b5274:      	cmp	w13, #96
fffffe00079b5278:      	b.ne	0xfffffe00079b5384 <_nwl_usb+0x228>
fffffe00079b527c:      	cmp	x2, #64
fffffe00079b5280:      	b.lo	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b5284:      	adrp	x8, 0xfffffe0007031000 <_nstd_eapol+0x2c>
fffffe00079b5288:      	add	x8, x8, #348
fffffe00079b528c:      	ldp	q0, q1, [x8]
fffffe00079b5290:      	ldp	q2, q3, [x8, #32]
fffffe00079b5294:      	adrp	x8, 0xfffffe0008547000 <_nwpc_pmk+0x94>
fffffe00079b5298:      	add	x8, x8, #276
fffffe00079b529c:      	stp	q0, q1, [sp]
fffffe00079b52a0:      	stp	q2, q3, [sp, #32]
fffffe00079b52a4:      	ldar	w8, [x8]
fffffe00079b52a8:      	cmp	w8, #1
fffffe00079b52ac:      	cset	w8, eq
fffffe00079b52b0:      	str	w8, [sp, #36]
fffffe00079b52b4:      	b.ne	0xfffffe00079b5300 <_nwl_usb+0x1a4>
fffffe00079b52b8:      	adrp	x8, 0xfffffe0008547000 <_nwpc_pmk+0xb8>
fffffe00079b52bc:      	add	x8, x8, #280
fffffe00079b52c0:      	add	x11, x8, #4
fffffe00079b52c4:      	ldar	w10, [x8]
fffffe00079b52c8:      	str	w10, [sp, #28]
fffffe00079b52cc:      	ldar	w10, [x11]
fffffe00079b52d0:      	add	x11, x8, #8
fffffe00079b52d4:      	str	w10, [sp, #32]
fffffe00079b52d8:      	ldar	w10, [x11]
fffffe00079b52dc:      	add	x11, x8, #12
fffffe00079b52e0:      	str	w10, [sp, #40]
fffffe00079b52e4:      	ldar	w10, [x11]
fffffe00079b52e8:      	add	x11, x8, #16
fffffe00079b52ec:      	add	x8, x8, #20
fffffe00079b52f0:      	str	w10, [sp, #44]
fffffe00079b52f4:      	ldar	w10, [x11]
fffffe00079b52f8:      	ldar	w8, [x8]
fffffe00079b52fc:      	stp	w10, w8, [sp, #48]
fffffe00079b5300:      	ldp	q1, q0, [sp, #32]
fffffe00079b5304:      	mov	w8, #64
fffffe00079b5308:      	xtn.4h	v6, v1
fffffe00079b530c:      	ldp	q3, q2, [sp]
fffffe00079b5310:      	xtn.4h	v7, v0
fffffe00079b5314:      	ushr.4h	v6, v6, #8
fffffe00079b5318:      	uzp1.8h	v4, v1, v0
fffffe00079b531c:      	ushr.4h	v7, v7, #8
fffffe00079b5320:      	xtn.4h	v16, v3
fffffe00079b5324:      	mov.d	v6[1], v7[0]
fffffe00079b5328:      	xtn.4h	v17, v2
fffffe00079b532c:      	ushr.4h	v16, v16, #8
fffffe00079b5330:      	ushr.4h	v17, v17, #8
fffffe00079b5334:      	uzp1.8h	v5, v3, v2
fffffe00079b5338:      	mov.d	v16[1], v17[0]
fffffe00079b533c:      	uzp2.8h	v7, v1, v0
fffffe00079b5340:      	uzp2.8h	v17, v3, v2
fffffe00079b5344:      	ushr.4s	v0, v0, #24
fffffe00079b5348:      	ushr.4s	v1, v1, #24
fffffe00079b534c:      	ushr.4s	v2, v2, #24
fffffe00079b5350:      	ushr.4s	v3, v3, #24
fffffe00079b5354:      	uzp1.8h	v0, v1, v0
fffffe00079b5358:      	uzp1.8h	v1, v3, v2
fffffe00079b535c:      	uzp1.16b	v2, v5, v4
fffffe00079b5360:      	uzp1.16b	v3, v16, v6
fffffe00079b5364:      	uzp1.16b	v4, v17, v7
fffffe00079b5368:      	uzp1.16b	v5, v1, v0
fffffe00079b536c:      	st4.16b	{ v2, v3, v4, v5 }, [x1]
fffffe00079b5370:      	cmp	x8, x9
fffffe00079b5374:      	mov	w0, #1
fffffe00079b5378:      	csel	x8, x8, x9, lo
fffffe00079b537c:      	str	w8, [x3]
fffffe00079b5380:      	b	0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b5384:      	cmp	x2, #256
fffffe00079b5388:      	b.lo	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b538c:      	adrp	x8, 0xfffffe0008547000 <_nwpc_pmk+0x18c>
fffffe00079b5390:      	add	x8, x8, #276
fffffe00079b5394:      	ldar	w10, [x8]
fffffe00079b5398:      	cmp	w10, #1
fffffe00079b539c:      	b.ne	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b53a0:      	adrp	x10, 0xfffffe0008547000 <_nwpc_pmk+0x1a0>
fffffe00079b53a4:      	add	x10, x10, #304
fffffe00079b53a8:      	mov	w0, wzr
fffffe00079b53ac:      	ldar	w11, [x10]
fffffe00079b53b0:      	cbz	w11, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b53b4:      	tbnz	w11, #0, 0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b53b8:      	movi.2d	v0, #0000000000000000
fffffe00079b53bc:      	mov	x12, xzr
fffffe00079b53c0:      	mov	x13, sp
fffffe00079b53c4:      	stp	q0, q0, [sp]
fffffe00079b53c8:      	stp	q0, q0, [sp, #32]
fffffe00079b53cc:      	stp	q0, q0, [sp, #64]
fffffe00079b53d0:      	stp	q0, q0, [sp, #96]
fffffe00079b53d4:      	stp	q0, q0, [sp, #128]
fffffe00079b53d8:      	stp	q0, q0, [sp, #160]
fffffe00079b53dc:      	stp	q0, q0, [sp, #192]
fffffe00079b53e0:      	stp	q0, q0, [sp, #224]
fffffe00079b53e4:      	add	x14, x8, x12
fffffe00079b53e8:      	add	x14, x14, #32
fffffe00079b53ec:      	ldar	w14, [x14]
fffffe00079b53f0:      	str	w14, [x13, x12]
fffffe00079b53f4:      	add	x12, x12, #4
fffffe00079b53f8:      	cmp	x12, #256
fffffe00079b53fc:      	b.ne	0xfffffe00079b53e4 <_nwl_usb+0x288>
fffffe00079b5400:      	ldar	w8, [x10]
fffffe00079b5404:      	cmp	w11, w8
fffffe00079b5408:      	b.ne	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b540c:      	ldp	q1, q0, [sp, #32]
fffffe00079b5410:      	mov	x8, x1
fffffe00079b5414:      	add	x10, x1, #192
fffffe00079b5418:      	xtn.4h	v6, v1
fffffe00079b541c:      	ldp	q3, q2, [sp]
fffffe00079b5420:      	xtn.4h	v7, v0
fffffe00079b5424:      	ushr.4h	v6, v6, #8
fffffe00079b5428:      	uzp1.8h	v4, v1, v0
fffffe00079b542c:      	ushr.4h	v7, v7, #8
fffffe00079b5430:      	xtn.4h	v16, v3
fffffe00079b5434:      	mov.d	v6[1], v7[0]
fffffe00079b5438:      	xtn.4h	v17, v2
fffffe00079b543c:      	ushr.4h	v16, v16, #8
fffffe00079b5440:      	ushr.4h	v17, v17, #8
fffffe00079b5444:      	uzp1.8h	v5, v3, v2
fffffe00079b5448:      	mov.d	v16[1], v17[0]
fffffe00079b544c:      	uzp2.8h	v7, v1, v0
fffffe00079b5450:      	uzp2.8h	v17, v3, v2
fffffe00079b5454:      	ushr.4s	v0, v0, #24
fffffe00079b5458:      	ushr.4s	v1, v1, #24
fffffe00079b545c:      	ushr.4s	v2, v2, #24
fffffe00079b5460:      	ushr.4s	v3, v3, #24
fffffe00079b5464:      	uzp1.8h	v0, v1, v0
fffffe00079b5468:      	uzp1.8h	v1, v3, v2
fffffe00079b546c:      	uzp1.16b	v2, v5, v4
fffffe00079b5470:      	uzp1.16b	v3, v16, v6
fffffe00079b5474:      	uzp1.16b	v4, v17, v7
fffffe00079b5478:      	uzp1.16b	v5, v1, v0
fffffe00079b547c:      	st4.16b	{ v2, v3, v4, v5 }, [x8], #64
fffffe00079b5480:      	ldp	q1, q0, [sp, #96]
fffffe00079b5484:      	xtn.4h	v6, v1
fffffe00079b5488:      	ldp	q3, q2, [sp, #64]
fffffe00079b548c:      	xtn.4h	v7, v0
fffffe00079b5490:      	ushr.4h	v6, v6, #8
fffffe00079b5494:      	uzp1.8h	v4, v1, v0
fffffe00079b5498:      	ushr.4h	v7, v7, #8
fffffe00079b549c:      	xtn.4h	v16, v3
fffffe00079b54a0:      	mov.d	v6[1], v7[0]
fffffe00079b54a4:      	xtn.4h	v17, v2
fffffe00079b54a8:      	uzp1.8h	v5, v3, v2
fffffe00079b54ac:      	ushr.4h	v16, v16, #8
fffffe00079b54b0:      	ushr.4h	v17, v17, #8
fffffe00079b54b4:      	uzp2.8h	v7, v1, v0
fffffe00079b54b8:      	ushr.4s	v0, v0, #24
fffffe00079b54bc:      	mov.d	v16[1], v17[0]
fffffe00079b54c0:      	uzp2.8h	v17, v3, v2
fffffe00079b54c4:      	ushr.4s	v1, v1, #24
fffffe00079b54c8:      	ushr.4s	v2, v2, #24
fffffe00079b54cc:      	ushr.4s	v3, v3, #24
fffffe00079b54d0:      	uzp1.8h	v0, v1, v0
fffffe00079b54d4:      	uzp1.8h	v1, v3, v2
fffffe00079b54d8:      	ldp	q3, q2, [sp, #160]
fffffe00079b54dc:      	uzp1.16b	v18, v5, v4
fffffe00079b54e0:      	uzp1.16b	v19, v16, v6
fffffe00079b54e4:      	uzp1.16b	v20, v17, v7
fffffe00079b54e8:      	xtn.4h	v6, v3
fffffe00079b54ec:      	uzp1.16b	v21, v1, v0
fffffe00079b54f0:      	ldp	q5, q4, [sp, #128]
fffffe00079b54f4:      	xtn.4h	v7, v2
fffffe00079b54f8:      	ushr.4h	v6, v6, #8
fffffe00079b54fc:      	uzp1.8h	v0, v3, v2
fffffe00079b5500:      	ushr.4h	v7, v7, #8
fffffe00079b5504:      	st4.16b	{ v18, v19, v20, v21 }, [x8]
fffffe00079b5508:      	xtn.4h	v16, v5
fffffe00079b550c:      	add	x8, x1, #128
fffffe00079b5510:      	mov.d	v6[1], v7[0]
fffffe00079b5514:      	xtn.4h	v17, v4
fffffe00079b5518:      	uzp1.8h	v1, v5, v4
fffffe00079b551c:      	ushr.4h	v16, v16, #8
fffffe00079b5520:      	ushr.4h	v17, v17, #8
fffffe00079b5524:      	uzp2.8h	v7, v3, v2
fffffe00079b5528:      	ushr.4s	v2, v2, #24
fffffe00079b552c:      	mov.d	v16[1], v17[0]
fffffe00079b5530:      	uzp2.8h	v17, v5, v4
fffffe00079b5534:      	ushr.4s	v3, v3, #24
fffffe00079b5538:      	ushr.4s	v4, v4, #24
fffffe00079b553c:      	ushr.4s	v5, v5, #24
fffffe00079b5540:      	uzp1.8h	v2, v3, v2
fffffe00079b5544:      	uzp1.8h	v3, v5, v4
fffffe00079b5548:      	uzp1.16b	v22, v1, v0
fffffe00079b554c:      	ldp	q0, q4, [sp, #224]
fffffe00079b5550:      	uzp1.16b	v23, v16, v6
fffffe00079b5554:      	uzp1.16b	v24, v17, v7
fffffe00079b5558:      	uzp1.16b	v25, v3, v2
fffffe00079b555c:      	xtn.4h	v6, v0
fffffe00079b5560:      	ldp	q5, q1, [sp, #192]
fffffe00079b5564:      	xtn.4h	v7, v4
fffffe00079b5568:      	ushr.4h	v6, v6, #8
fffffe00079b556c:      	uzp1.8h	v2, v0, v4
fffffe00079b5570:      	st4.16b	{ v22, v23, v24, v25 }, [x8]
fffffe00079b5574:      	ushr.4h	v7, v7, #8
fffffe00079b5578:      	mov	w8, #256
fffffe00079b557c:      	xtn.4h	v16, v5
fffffe00079b5580:      	mov.d	v6[1], v7[0]
fffffe00079b5584:      	xtn.4h	v17, v1
fffffe00079b5588:      	ushr.4h	v16, v16, #8
fffffe00079b558c:      	ushr.4h	v17, v17, #8
fffffe00079b5590:      	uzp1.8h	v3, v5, v1
fffffe00079b5594:      	mov.d	v16[1], v17[0]
fffffe00079b5598:      	uzp2.8h	v7, v0, v4
fffffe00079b559c:      	uzp2.8h	v17, v5, v1
fffffe00079b55a0:      	ushr.4s	v4, v4, #24
fffffe00079b55a4:      	ushr.4s	v0, v0, #24
fffffe00079b55a8:      	ushr.4s	v1, v1, #24
fffffe00079b55ac:      	ushr.4s	v5, v5, #24
fffffe00079b55b0:      	uzp1.8h	v0, v0, v4
fffffe00079b55b4:      	uzp1.8h	v1, v5, v1
fffffe00079b55b8:      	uzp1.16b	v2, v3, v2
fffffe00079b55bc:      	uzp1.16b	v3, v16, v6
fffffe00079b55c0:      	uzp1.16b	v4, v17, v7
fffffe00079b55c4:      	uzp1.16b	v5, v1, v0
fffffe00079b55c8:      	st4.16b	{ v2, v3, v4, v5 }, [x10]
fffffe00079b55cc:      	b	0xfffffe00079b5370 <_nwl_usb+0x214>
fffffe00079b55d0:      	cmp	w11, #4095
fffffe00079b55d4:      	b.eq	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b55d8:      	add	w11, w11, #1
fffffe00079b55dc:      	cmp	w8, w11
fffffe00079b55e0:      	b.ne	0xfffffe00079b55f4 <_nwl_usb+0x498>
fffffe00079b55e4:      	adrp	x11, 0xfffffe0008547000 <_nwpc_ptk+0x4c>
fffffe00079b55e8:      	add	x11, x11, #288
fffffe00079b55ec:      	ldar	w12, [x11]
fffffe00079b55f0:      	cbz	w12, 0xfffffe00079b5620 <_nwl_usb+0x4c4>
fffffe00079b55f4:      	mov	w0, wzr
fffffe00079b55f8:      	adrp	x8, 0xfffffe0007c9c000 <_stc_receipt+0x78>
fffffe00079b55fc:      	nop
fffffe00079b5600:      	ldur	x9, [x29, #-24]
fffffe00079b5604:      	ldr	x8, [x8, #3440]
fffffe00079b5608:      	cmp	x8, x9
fffffe00079b560c:      	b.ne	0xfffffe00079b5648 <_nwl_usb+0x4ec>
fffffe00079b5610:      	ldp	x29, x30, [sp, #288]
fffffe00079b5614:      	ldp	x28, x27, [sp, #272]
fffffe00079b5618:      	add	sp, sp, #304
fffffe00079b561c:      	ret
fffffe00079b5620:      	add	x12, x11, #8
fffffe00079b5624:      	add	x13, x11, #12
fffffe00079b5628:      	add	x14, x11, #4
fffffe00079b562c:      	mov	w15, #1
fffffe00079b5630:      	mov	w0, #2
fffffe00079b5634:      	stlr	w9, [x12]
fffffe00079b5638:      	stlr	w10, [x13]
fffffe00079b563c:      	stlr	w8, [x14]
fffffe00079b5640:      	stlr	w15, [x11]
fffffe00079b5644:      	b	0xfffffe00079b55f8 <_nwl_usb+0x49c>
fffffe00079b5648:      	bl	0xfffffe0007a09274 <___stack_chk_fail>
