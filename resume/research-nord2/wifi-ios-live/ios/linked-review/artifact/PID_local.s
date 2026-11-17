
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b3670 <_nsdd_pid_local>:
fffffe00079b3670:      	cmp	w2, #254
fffffe00079b3674:      	b.hi	0xfffffe00079b36d0 <_nsdd_pid_local+0x60>
fffffe00079b3678:      	cbz	x0, 0xfffffe00079b36d0 <_nsdd_pid_local+0x60>
fffffe00079b367c:      	sub	w8, w1, #127
fffffe00079b3680:      	cmn	w8, #126
fffffe00079b3684:      	b.lo	0xfffffe00079b36d0 <_nsdd_pid_local+0x60>
fffffe00079b3688:      	mov	w8, #72
fffffe00079b368c:      	umull	x9, w1, w8
fffffe00079b3690:      	adrp	x8, 0xfffffe0008544000 <_stc_receipt+0x3b8>
fffffe00079b3694:      	add	x8, x8, #3392
fffffe00079b3698:      	ldr	x9, [x8, x9]
fffffe00079b369c:      	cmp	x9, #0
fffffe00079b36a0:      	ccmp	x9, x0, #0, ne
fffffe00079b36a4:      	b.ne	0xfffffe00079b36d0 <_nsdd_pid_local+0x60>
fffffe00079b36a8:      	mov	w9, w1
fffffe00079b36ac:      	mov	w10, #72
fffffe00079b36b0:      	madd	x10, x9, x10, x8
fffffe00079b36b4:      	ldr	w10, [x10, #12]
fffffe00079b36b8:      	cmp	w10, w2
fffffe00079b36bc:      	b.ne	0xfffffe00079b36d0 <_nsdd_pid_local+0x60>
fffffe00079b36c0:      	mov	w10, #72
fffffe00079b36c4:      	madd	x10, x9, x10, x8
fffffe00079b36c8:      	ldr	w10, [x10, #68]
fffffe00079b36cc:      	cbz	w10, 0xfffffe00079b36d4 <_nsdd_pid_local+0x64>
fffffe00079b36d0:      	ret
fffffe00079b36d4:      	mov	w10, #72
fffffe00079b36d8:      	madd	x10, x9, x10, x8
fffffe00079b36dc:      	ldr	w10, [x10, #48]
fffffe00079b36e0:      	cbz	w10, 0xfffffe00079b36d0 <_nsdd_pid_local+0x60>
fffffe00079b36e4:      	mov	w10, #72
fffffe00079b36e8:      	madd	x8, x9, x10, x8
fffffe00079b36ec:      	mov	w9, #1
fffffe00079b36f0:      	str	w9, [x8, #56]
fffffe00079b36f4:      	ret
