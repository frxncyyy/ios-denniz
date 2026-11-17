
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b6230 <_nwp_group_ready>:
fffffe00079b6230:      	cbz	x4, 0xfffffe00079b62d0 <_nwp_group_ready+0xa0>
fffffe00079b6234:      	movi.2d	v0, #0000000000000000
fffffe00079b6238:      	mov	w8, wzr
fffffe00079b623c:      	str	xzr, [x4, #272]
fffffe00079b6240:      	stp	q0, q0, [x4, #16]
fffffe00079b6244:      	stp	q0, q0, [x4, #48]
fffffe00079b6248:      	stp	q0, q0, [x4, #80]
fffffe00079b624c:      	stp	q0, q0, [x4, #112]
fffffe00079b6250:      	stp	q0, q0, [x4, #144]
fffffe00079b6254:      	stp	q0, q0, [x4, #176]
fffffe00079b6258:      	stp	q0, q0, [x4, #208]
fffffe00079b625c:      	stp	q0, q0, [x4, #240]
fffffe00079b6260:      	str	q0, [x4]
fffffe00079b6264:      	cbz	x0, 0xfffffe00079b6314 <_nwp_group_ready+0xe4>
fffffe00079b6268:      	cbz	x2, 0xfffffe00079b6314 <_nwp_group_ready+0xe4>
fffffe00079b626c:      	ldr	x9, [x0]
fffffe00079b6270:      	cmp	x9, x1
fffffe00079b6274:      	b.ne	0xfffffe00079b6314 <_nwp_group_ready+0xe4>
fffffe00079b6278:      	ldr	x8, [x0, #240]
fffffe00079b627c:      	cmp	x8, x2
fffffe00079b6280:      	b.ne	0xfffffe00079b62d8 <_nwp_group_ready+0xa8>
fffffe00079b6284:      	ldr	x8, [x0, #16]
fffffe00079b6288:      	cmp	x8, x2
fffffe00079b628c:      	b.ne	0xfffffe00079b62d8 <_nwp_group_ready+0xa8>
fffffe00079b6290:      	ldr	w8, [x0, #228]
fffffe00079b6294:      	cmp	w8, #1
fffffe00079b6298:      	b.ne	0xfffffe00079b62d8 <_nwp_group_ready+0xa8>
fffffe00079b629c:      	ldr	w8, [x0, #32]
fffffe00079b62a0:      	cmp	w8, #5
fffffe00079b62a4:      	b.ne	0xfffffe00079b62d8 <_nwp_group_ready+0xa8>
fffffe00079b62a8:      	cmp	w3, #1
fffffe00079b62ac:      	b.ne	0xfffffe00079b62e0 <_nwp_group_ready+0xb0>
fffffe00079b62b0:      	add	x1, x0, #256
fffffe00079b62b4:      	mov	w8, #2
fffffe00079b62b8:      	mov	w2, #4
fffffe00079b62bc:      	mov	x3, x4
fffffe00079b62c0:      	str	xzr, [x0, #16]
fffffe00079b62c4:      	str	xzr, [x0, #240]
fffffe00079b62c8:      	str	w8, [x0, #228]
fffffe00079b62cc:      	b	0xfffffe00079b631c <_reply>
fffffe00079b62d0:      	mov	w0, #-1
fffffe00079b62d4:      	ret
fffffe00079b62d8:      	mov	w0, wzr
fffffe00079b62dc:      	ret
fffffe00079b62e0:      	mov	x8, xzr
fffffe00079b62e4:      	strb	wzr, [x0, x8]
fffffe00079b62e8:      	add	x8, x8, #1
fffffe00079b62ec:      	cmp	x8, #2616
fffffe00079b62f0:      	b.ne	0xfffffe00079b62e4 <_nwp_group_ready+0xb4>
fffffe00079b62f4:      	mov	x8, xzr
fffffe00079b62f8:      	strb	wzr, [x4, x8]
fffffe00079b62fc:      	add	x8, x8, #1
fffffe00079b6300:      	cmp	x8, #280
fffffe00079b6304:      	b.ne	0xfffffe00079b62f8 <_nwp_group_ready+0xc8>
fffffe00079b6308:      	mov	w9, #1
fffffe00079b630c:      	mov	w8, #-1
fffffe00079b6310:      	str	w9, [x4, #8]
fffffe00079b6314:      	mov	w0, w8
fffffe00079b6318:      	ret
