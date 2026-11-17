
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aa314 <_nstd_txs>:
fffffe00079aa314:      	movi.2d	v0, #0000000000000000
fffffe00079aa318:      	adrp	x8, 0xfffffe000702d000 <_receive+0x29c>
fffffe00079aa31c:      	mov	w9, #4
fffffe00079aa320:      	stp	x1, x2, [x0]
fffffe00079aa324:      	stur	q0, [x0, #40]
fffffe00079aa328:      	stur	q0, [x0, #24]
fffffe00079aa32c:      	ldr	q0, [x8, #2688]
fffffe00079aa330:      	stp	w9, w4, [x0, #16]
fffffe00079aa334:      	str	q0, [x0, #32]
fffffe00079aa338:      	cbz	x3, 0xfffffe00079aa3b0 <_nstd_txs+0x9c>
fffffe00079aa33c:      	cmp	x4, #2
fffffe00079aa340:      	b.lo	0xfffffe00079aa3b0 <_nstd_txs+0x9c>
fffffe00079aa344:      	ldrh	w8, [x3]
fffffe00079aa348:      	cmp	x4, #26
fffffe00079aa34c:      	str	w8, [x0, #24]
fffffe00079aa350:      	b.lo	0xfffffe00079aa3b0 <_nstd_txs+0x9c>
fffffe00079aa354:      	ldrh	w8, [x3, #24]
fffffe00079aa358:      	cmp	x4, #30
fffffe00079aa35c:      	str	w8, [x0, #28]
fffffe00079aa360:      	b.lo	0xfffffe00079aa3b0 <_nstd_txs+0x9c>
fffffe00079aa364:      	ldrb	w9, [x3, #29]
fffffe00079aa368:      	subs	x8, x4, #36
fffffe00079aa36c:      	str	w9, [x0, #48]
fffffe00079aa370:      	b.lo	0xfffffe00079aa3b0 <_nstd_txs+0x9c>
fffffe00079aa374:      	b.eq	0xfffffe00079aa3a8 <_nstd_txs+0x94>
fffffe00079aa378:      	ldrb	w9, [x3, #36]
fffffe00079aa37c:      	cmp	x8, #1
fffffe00079aa380:      	str	w9, [x0, #32]
fffffe00079aa384:      	b.eq	0xfffffe00079aa3a8 <_nstd_txs+0x94>
fffffe00079aa388:      	ldrb	w9, [x3, #37]
fffffe00079aa38c:      	cmp	x8, #5
fffffe00079aa390:      	str	w9, [x0, #40]
fffffe00079aa394:      	b.lo	0xfffffe00079aa3a8 <_nstd_txs+0x94>
fffffe00079aa398:      	ldrb	w9, [x3, #40]
fffffe00079aa39c:      	cmp	x8, #88
fffffe00079aa3a0:      	str	w9, [x0, #36]
fffffe00079aa3a4:      	b.hs	0xfffffe00079aa3b4 <_nstd_txs+0xa0>
fffffe00079aa3a8:      	mov	w8, #2
fffffe00079aa3ac:      	str	w8, [x0, #44]
fffffe00079aa3b0:      	ret
fffffe00079aa3b4:      	cbz	w5, 0xfffffe00079aa3e4 <_nstd_txs+0xd0>
fffffe00079aa3b8:      	ldrb	w8, [x3, #36]
fffffe00079aa3bc:      	cmp	w8, w6
fffffe00079aa3c0:      	b.ne	0xfffffe00079aa3ec <_nstd_txs+0xd8>
fffffe00079aa3c4:      	ldrb	w8, [x3, #40]
fffffe00079aa3c8:      	cmp	w8, w7
fffffe00079aa3cc:      	b.ne	0xfffffe00079aa3f4 <_nstd_txs+0xe0>
fffffe00079aa3d0:      	ldr	w8, [sp]
fffffe00079aa3d4:      	cmp	w8, #0
fffffe00079aa3d8:      	mov	w8, #6
fffffe00079aa3dc:      	cinc	w8, w8, eq
fffffe00079aa3e0:      	b	0xfffffe00079aa3ac <_nstd_txs+0x98>
fffffe00079aa3e4:      	mov	w8, #3
fffffe00079aa3e8:      	b	0xfffffe00079aa3ac <_nstd_txs+0x98>
fffffe00079aa3ec:      	mov	w8, #4
fffffe00079aa3f0:      	b	0xfffffe00079aa3ac <_nstd_txs+0x98>
fffffe00079aa3f4:      	mov	w8, #5
fffffe00079aa3f8:      	b	0xfffffe00079aa3ac <_nstd_txs+0x98>
