
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b350c <_nsdd_pid_attempt>:
fffffe00079b350c:      	cmp	w2, #254
fffffe00079b3510:      	b.hi	0xfffffe00079b356c <_nsdd_pid_attempt+0x60>
fffffe00079b3514:      	cbz	x0, 0xfffffe00079b356c <_nsdd_pid_attempt+0x60>
fffffe00079b3518:      	sub	w8, w1, #127
fffffe00079b351c:      	cmn	w8, #126
fffffe00079b3520:      	b.lo	0xfffffe00079b356c <_nsdd_pid_attempt+0x60>
fffffe00079b3524:      	mov	w8, #72
fffffe00079b3528:      	umull	x9, w1, w8
fffffe00079b352c:      	adrp	x8, 0xfffffe0008544000 <_stc_receipt+0x254>
fffffe00079b3530:      	add	x8, x8, #3392
fffffe00079b3534:      	ldr	x9, [x8, x9]
fffffe00079b3538:      	cmp	x9, #0
fffffe00079b353c:      	ccmp	x9, x0, #0, ne
fffffe00079b3540:      	b.ne	0xfffffe00079b356c <_nsdd_pid_attempt+0x60>
fffffe00079b3544:      	mov	w9, w1
fffffe00079b3548:      	mov	w10, #72
fffffe00079b354c:      	madd	x10, x9, x10, x8
fffffe00079b3550:      	ldr	w10, [x10, #12]
fffffe00079b3554:      	cmp	w10, w2
fffffe00079b3558:      	b.ne	0xfffffe00079b356c <_nsdd_pid_attempt+0x60>
fffffe00079b355c:      	mov	w10, #72
fffffe00079b3560:      	madd	x10, x9, x10, x8
fffffe00079b3564:      	ldr	w10, [x10, #68]
fffffe00079b3568:      	cbz	w10, 0xfffffe00079b3570 <_nsdd_pid_attempt+0x64>
fffffe00079b356c:      	ret
fffffe00079b3570:      	mov	w10, #72
fffffe00079b3574:      	madd	x8, x9, x10, x8
fffffe00079b3578:      	mov	w9, #1
fffffe00079b357c:      	str	w9, [x8, #44]
fffffe00079b3580:      	ret
