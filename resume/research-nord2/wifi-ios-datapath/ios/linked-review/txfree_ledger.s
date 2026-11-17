
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ab228 <_nstx_report>:
fffffe00079ab228:      	cbz	x0, 0xfffffe00079ab288 <_nstx_report+0x60>
fffffe00079ab22c:      	sub	sp, sp, #176
fffffe00079ab230:      	stp	x20, x19, [sp, #144]
fffffe00079ab234:      	stp	x29, x30, [sp, #160]
fffffe00079ab238:      	add	x29, sp, #160
fffffe00079ab23c:      	movi.2d	v0, #0000000000000000
fffffe00079ab240:      	mov	x19, x0
fffffe00079ab244:      	mov	x0, sp
fffffe00079ab248:      	str	xzr, [sp, #128]
fffffe00079ab24c:      	stp	q0, q0, [sp]
fffffe00079ab250:      	stp	q0, q0, [sp, #32]
fffffe00079ab254:      	stp	q0, q0, [sp, #64]
fffffe00079ab258:      	stp	q0, q0, [sp, #96]
fffffe00079ab25c:      	bl	0xfffffe00079ab048 <_nstx_decode_report>
fffffe00079ab260:      	cbz	w0, 0xfffffe00079ab28c <_nstx_report+0x64>
fffffe00079ab264:      	ldr	w8, [x19, #56]!
fffffe00079ab268:      	mov	w0, wzr
fffffe00079ab26c:      	cmn	w8, #1
fffffe00079ab270:      	b.eq	0xfffffe00079ab27c <_nstx_report+0x54>
fffffe00079ab274:      	add	w8, w8, #1
fffffe00079ab278:      	str	w8, [x19]
fffffe00079ab27c:      	ldp	x20, x19, [sp, #144]
fffffe00079ab280:      	ldp	x29, x30, [sp, #160]
fffffe00079ab284:      	add	sp, sp, #176
fffffe00079ab288:      	ret
fffffe00079ab28c:      	ldr	w8, [x19, #28]
fffffe00079ab290:      	cbz	w8, 0xfffffe00079ab264 <_nstx_report+0x3c>
fffffe00079ab294:      	ldr	w8, [x19, #36]
fffffe00079ab298:      	cbnz	w8, 0xfffffe00079ab264 <_nstx_report+0x3c>
fffffe00079ab29c:      	ldr	w8, [sp, #128]
fffffe00079ab2a0:      	cmp	w8, #1
fffffe00079ab2a4:      	b.ne	0xfffffe00079ab264 <_nstx_report+0x3c>
fffffe00079ab2a8:      	ldrh	w8, [sp]
fffffe00079ab2ac:      	ldr	w9, [x19, #24]
fffffe00079ab2b0:      	cmp	w9, w8
fffffe00079ab2b4:      	b.ne	0xfffffe00079ab264 <_nstx_report+0x3c>
fffffe00079ab2b8:      	ldr	w8, [x19, #52]!
fffffe00079ab2bc:      	mov	w0, #1
fffffe00079ab2c0:      	cmn	w8, #1
fffffe00079ab2c4:      	stur	w0, [x19, #-16]
fffffe00079ab2c8:      	b.ne	0xfffffe00079ab274 <_nstx_report+0x4c>
fffffe00079ab2cc:      	b	0xfffffe00079ab27c <_nstx_report+0x54>
