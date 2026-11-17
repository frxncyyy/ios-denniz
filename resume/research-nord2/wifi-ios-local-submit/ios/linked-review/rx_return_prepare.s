
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007999258 <_ncr_scan_return_prepare>:
fffffe0007999258:      	stp	x22, x21, [sp, #-48]!
fffffe000799925c:      	stp	x20, x19, [sp, #16]
fffffe0007999260:      	stp	x29, x30, [sp, #32]
fffffe0007999264:      	add	x29, sp, #32
fffffe0007999268:      	mov	x19, x0
fffffe000799926c:      	mov	w0, #-1
fffffe0007999270:      	cbz	x2, 0xfffffe00079992ac <_ncr_scan_return_prepare+0x54>
fffffe0007999274:      	cbz	x1, 0xfffffe00079992ac <_ncr_scan_return_prepare+0x54>
fffffe0007999278:      	mov	x20, x3
fffffe000799927c:      	cbz	x3, 0xfffffe00079992ac <_ncr_scan_return_prepare+0x54>
fffffe0007999280:      	ldr	w8, [x2, #4576]
fffffe0007999284:      	cmp	w8, #2
fffffe0007999288:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe000799928c:      	ldr	x8, [x2, #4592]
fffffe0007999290:      	cbz	x8, 0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999294:      	ldr	w8, [x8, #32]
fffffe0007999298:      	cbz	w8, 0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe000799929c:      	ldr	x8, [x2, #16]
fffffe00079992a0:      	cmp	x8, x1
fffffe00079992a4:      	b.eq	0xfffffe00079992bc <_ncr_scan_return_prepare+0x64>
fffffe00079992a8:      	mov	w0, #-1
fffffe00079992ac:      	ldp	x29, x30, [sp, #32]
fffffe00079992b0:      	ldp	x20, x19, [sp, #16]
fffffe00079992b4:      	ldp	x22, x21, [sp], #48
fffffe00079992b8:      	ret
fffffe00079992bc:      	mov	x8, xzr
fffffe00079992c0:      	add	x21, x19, #280
fffffe00079992c4:      	add	x9, x1, #244
fffffe00079992c8:      	ldr	x10, [x20, x8]
fffffe00079992cc:      	ldur	x11, [x9, #-28]
fffffe00079992d0:      	cmp	x10, x11
fffffe00079992d4:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe00079992d8:      	add	x10, x20, x8
fffffe00079992dc:      	ldur	x12, [x9, #-20]
fffffe00079992e0:      	ldr	x11, [x10, #8]
fffffe00079992e4:      	cmp	x11, x12
fffffe00079992e8:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe00079992ec:      	ldr	w10, [x10, #16]
fffffe00079992f0:      	ldur	w11, [x9, #-12]
fffffe00079992f4:      	cmp	w10, w11
fffffe00079992f8:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe00079992fc:      	add	x11, x20, x8
fffffe0007999300:      	ldur	w12, [x9, #-8]
fffffe0007999304:      	ldr	w10, [x11, #20]
fffffe0007999308:      	cmp	w10, w12
fffffe000799930c:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999310:      	ldr	w11, [x11, #24]
fffffe0007999314:      	ldur	w12, [x9, #-4]
fffffe0007999318:      	cmp	w11, w12
fffffe000799931c:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999320:      	add	x11, x20, x8
fffffe0007999324:      	ldr	w13, [x9]
fffffe0007999328:      	ldr	w12, [x11, #28]
fffffe000799932c:      	cmp	w12, w13
fffffe0007999330:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999334:      	ldr	w11, [x11, #32]
fffffe0007999338:      	cmp	w11, w10
fffffe000799933c:      	b.hs	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999340:      	add	x10, x20, x8
fffffe0007999344:      	ldr	w10, [x10, #36]
fffffe0007999348:      	cmp	w10, #1
fffffe000799934c:      	b.hi	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999350:      	add	x9, x9, #32
fffffe0007999354:      	add	x8, x8, #80
fffffe0007999358:      	cmp	x8, #400
fffffe000799935c:      	b.ne	0xfffffe00079992c8 <_ncr_scan_return_prepare+0x70>
fffffe0007999360:      	ldr	w8, [x20, #112]
fffffe0007999364:      	ldr	w9, [x1, #2868]
fffffe0007999368:      	cmp	w8, w9
fffffe000799936c:      	b.ne	0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999370:      	ldr	w6, [x2, #132]
fffffe0007999374:      	mov	x0, x19
fffffe0007999378:      	mov	w3, wzr
fffffe000799937c:      	bl	0xfffffe0007998de8 <_prepare>
fffffe0007999380:      	cbnz	w0, 0xfffffe00079992a8 <_ncr_scan_return_prepare+0x50>
fffffe0007999384:      	ldp	q0, q1, [x20, #48]
fffffe0007999388:      	movi.2d	v3, #0000000000000000
fffffe000799938c:      	mov	x0, x19
fffffe0007999390:      	ldp	q4, q2, [x20, #16]
fffffe0007999394:      	stp	q0, q1, [x19, #192]
fffffe0007999398:      	ldr	q0, [x20]
fffffe000799939c:      	stp	q4, q2, [x19, #160]
fffffe00079993a0:      	stur	q3, [x19, #200]
fffffe00079993a4:      	str	wzr, [x19, #216]
fffffe00079993a8:      	str	q0, [x19, #144]
fffffe00079993ac:      	ldp	q1, q0, [x20, #128]
fffffe00079993b0:      	ldp	q4, q2, [x20, #96]
fffffe00079993b4:      	stp	q1, q0, [x19, #272]
fffffe00079993b8:      	ldr	q0, [x20, #80]
fffffe00079993bc:      	stp	q4, q2, [x19, #240]
fffffe00079993c0:      	str	q3, [x21]
fffffe00079993c4:      	str	wzr, [x19, #296]
fffffe00079993c8:      	str	q0, [x19, #224]
fffffe00079993cc:      	ldp	q1, q0, [x20, #208]
fffffe00079993d0:      	ldp	q4, q2, [x20, #176]
fffffe00079993d4:      	stp	q1, q0, [x19, #352]
fffffe00079993d8:      	ldr	q0, [x20, #160]
fffffe00079993dc:      	stp	q4, q2, [x19, #320]
fffffe00079993e0:      	str	q3, [x21, #80]
fffffe00079993e4:      	str	wzr, [x19, #376]
fffffe00079993e8:      	str	q0, [x19, #304]
fffffe00079993ec:      	ldp	q1, q0, [x20, #288]
fffffe00079993f0:      	ldp	q4, q2, [x20, #256]
fffffe00079993f4:      	stp	q1, q0, [x19, #432]
fffffe00079993f8:      	ldr	q0, [x20, #240]
fffffe00079993fc:      	stp	q4, q2, [x19, #400]
fffffe0007999400:      	str	q3, [x21, #160]
fffffe0007999404:      	str	wzr, [x19, #456]
fffffe0007999408:      	str	q0, [x19, #384]
fffffe000799940c:      	ldp	q1, q0, [x20, #368]
fffffe0007999410:      	ldp	q2, q4, [x20, #336]
fffffe0007999414:      	stp	q1, q0, [x19, #512]
fffffe0007999418:      	ldr	q0, [x20, #320]
fffffe000799941c:      	stp	q2, q4, [x19, #480]
fffffe0007999420:      	str	q3, [x21, #240]
fffffe0007999424:      	str	wzr, [x19, #536]
fffffe0007999428:      	str	q0, [x19, #464]
fffffe000799942c:      	bl	0xfffffe00079990ec <_source>
fffffe0007999430:      	cmp	w0, #0
fffffe0007999434:      	csetm	w0, eq
fffffe0007999438:      	b	0xfffffe00079992ac <_ncr_scan_return_prepare+0x54>
