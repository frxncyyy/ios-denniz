
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe000751cfd8 <_nss_worker_continue>:
fffffe000751cfd8:      	stp	d9, d8, [sp, #-112]!
fffffe000751cfdc:      	stp	x28, x27, [sp, #16]
fffffe000751cfe0:      	stp	x26, x25, [sp, #32]
fffffe000751cfe4:      	stp	x24, x23, [sp, #48]
fffffe000751cfe8:      	stp	x22, x21, [sp, #64]
fffffe000751cfec:      	stp	x20, x19, [sp, #80]
fffffe000751cff0:      	stp	x29, x30, [sp, #96]
fffffe000751cff4:      	add	x29, sp, #96
fffffe000751cff8:      	sub	sp, sp, #816
fffffe000751cffc:      	adrp	x8, 0xfffffe0007c98000 <_nss_worker_continue+0x1e14>
fffffe000751d000:      	adrp	x0, 0xfffffe0007099000 <_nppa_source_owner+0x23c>
fffffe000751d004:      	add	x0, x0, #2819
fffffe000751d008:      	add	x1, sp, #584
fffffe000751d00c:      	mov	w2, #4
fffffe000751d010:      	nop
fffffe000751d014:      	ldr	x8, [x8, #3440]
fffffe000751d018:      	stur	x8, [x29, #-120]
fffffe000751d01c:      	str	wzr, [sp, #584]
fffffe000751d020:      	bl	0xfffffe0007af17a4 <_PE_parse_boot_argn>
fffffe000751d024:      	ldr	w8, [sp, #584]
fffffe000751d028:      	cmp	w8, #1
fffffe000751d02c:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d030:      	adrp	x24, 0xfffffe00084d9000 <_njs_event+0x8>
fffffe000751d034:      	add	x27, sp, #592
fffffe000751d038:      	movi.2d	v0, #0000000000000000
fffffe000751d03c:      	str	wzr, [sp, #560]
fffffe000751d040:      	str	xzr, [sp, #552]
fffffe000751d044:      	ldr	x8, [x24, #2456]
fffffe000751d048:      	stur	wzr, [x27, #191]
fffffe000751d04c:      	str	xzr, [sp, #528]
fffffe000751d050:      	stp	q0, q0, [x27, #64]
fffffe000751d054:      	stp	q0, q0, [x27, #96]
fffffe000751d058:      	stp	q0, q0, [x27, #128]
fffffe000751d05c:      	stp	q0, q0, [x27, #160]
fffffe000751d060:      	stp	q0, q0, [sp, #592]
fffffe000751d064:      	stp	q0, q0, [sp, #624]
fffffe000751d068:      	cbz	x8, 0xfffffe000751d0a8 <_nss_worker_continue+0xd0>
fffffe000751d06c:      	adrp	x8, 0xfffffe0007c98000 <_nss_worker_continue+0x1e80>
fffffe000751d070:      	nop
fffffe000751d074:      	ldur	x9, [x29, #-120]
fffffe000751d078:      	ldr	x8, [x8, #3440]
fffffe000751d07c:      	cmp	x8, x9
fffffe000751d080:      	b.ne	0xfffffe000751ff4c <_nss_worker_continue+0x2f74>
fffffe000751d084:      	add	sp, sp, #816
fffffe000751d088:      	ldp	x29, x30, [sp, #96]
fffffe000751d08c:      	ldp	x20, x19, [sp, #80]
fffffe000751d090:      	ldp	x22, x21, [sp, #64]
fffffe000751d094:      	ldp	x24, x23, [sp, #48]
fffffe000751d098:      	ldp	x26, x25, [sp, #32]
fffffe000751d09c:      	ldp	x28, x27, [sp, #16]
fffffe000751d0a0:      	ldp	d9, d8, [sp], #112
fffffe000751d0a4:      	ret
fffffe000751d0a8:      	adrp	x25, 0xfffffe0008518000 <_njs_event+0x17c>
fffffe000751d0ac:      	add	x25, x25, #1432
fffffe000751d0b0:      	ldr	x28, [x25]
fffffe000751d0b4:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe000751d0b8:      	adrp	x23, 0xfffffe000851a000 <_njs_event+0x194>
fffffe000751d0bc:      	add	x23, x23, #2536
fffffe000751d0c0:      	ldr	d0, [x25, #16]
fffffe000751d0c4:      	ldr	d1, [x23]
fffffe000751d0c8:      	mov.d	v0[1], v1[0]
fffffe000751d0cc:      	cmtst.4s	v1, v0, v0
fffffe000751d0d0:      	cmeq.4s	v0, v0, #0
fffffe000751d0d4:      	xtn.4h	v0, v0
fffffe000751d0d8:      	xtn.4h	v1, v1
fffffe000751d0dc:      	mov.h	v0[2], v1[2]
fffffe000751d0e0:      	umov.h	w8, v0[1]
fffffe000751d0e4:      	umov.h	w9, v0[2]
fffffe000751d0e8:      	umov.h	w10, v0[0]
fffffe000751d0ec:      	umov.h	w11, v0[3]
fffffe000751d0f0:      	and	w8, w8, #0x1
fffffe000751d0f4:      	and	w9, w9, #0x1
fffffe000751d0f8:      	bfi	w10, w8, #1, #1
fffffe000751d0fc:      	mov	w8, #15
fffffe000751d100:      	bfi	w10, w9, #2, #1
fffffe000751d104:      	bfi	w10, w11, #3, #29
fffffe000751d108:      	bics	wzr, w8, w10
fffffe000751d10c:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d110:      	ldr	w8, [x25, #12]
fffffe000751d114:      	cbz	w8, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d118:      	ldr	w8, [x23, #6164]
fffffe000751d11c:      	cbz	w8, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d120:      	adrp	x8, 0xfffffe000851a000 <_njs_event+0x1fc>
fffffe000751d124:      	add	x8, x8, #2008
fffffe000751d128:      	ldr	w9, [x8, #4]
fffffe000751d12c:      	cbnz	w9, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d130:      	cmp	x28, x0
fffffe000751d134:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d138:      	ldr	w8, [x8]
fffffe000751d13c:      	cbz	w8, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d140:      	adrp	x19, 0xfffffe0008518000 <_njs_event+0x214>
fffffe000751d144:      	add	x19, x19, #1464
fffffe000751d148:      	mov	x0, x19
fffffe000751d14c:      	bl	0xfffffe000799afb0 <_nct_done_receipt>
fffffe000751d150:      	ldr	d0, [x19, #48]
fffffe000751d154:      	add	x8, x19, #56
fffffe000751d158:      	adrp	x9, 0xfffffe000700e000 <_nppa_source_owner+0x168>
fffffe000751d15c:      	cmp	w0, #0
fffffe000751d160:      	cset	w10, ne
fffffe000751d164:      	ld1.s	{ v0 }[2], [x8]
fffffe000751d168:      	add	x8, x19, #68
fffffe000751d16c:      	ldr	q1, [x9, #256]
fffffe000751d170:      	ldr	w9, [x19, #72]
fffffe000751d174:      	ld1.s	{ v0 }[3], [x8]
fffffe000751d178:      	ldr	w8, [x19, #44]
fffffe000751d17c:      	cmeq.4s	v0, v0, v1
fffffe000751d180:      	cmp	w8, #7
fffffe000751d184:      	cset	w8, eq
fffffe000751d188:      	cmp	w9, #26
fffffe000751d18c:      	mvn.16b	v0, v0
fffffe000751d190:      	cset	w9, eq
fffffe000751d194:      	xtn.4h	v0, v0
fffffe000751d198:      	umaxv.4h	h0, v0
fffffe000751d19c:      	fmov	w11, s0
fffffe000751d1a0:      	bic	w8, w8, w11
fffffe000751d1a4:      	and	w8, w8, w10
fffffe000751d1a8:      	and	w8, w8, w9
fffffe000751d1ac:      	cmp	w8, #1
fffffe000751d1b0:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d1b4:      	ldr	w8, [x19, #64]
fffffe000751d1b8:      	cmp	w8, #26
fffffe000751d1bc:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d1c0:      	ldr	x8, [x19]
fffffe000751d1c4:      	adrp	x9, 0xfffffe0008516000 <_njs_event+0x290>
fffffe000751d1c8:      	add	x9, x9, #0
fffffe000751d1cc:      	cmp	x8, x9
fffffe000751d1d0:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d1d4:      	ldr	x8, [x19, #8]
fffffe000751d1d8:      	adrp	x9, 0xfffffe0008517000 <_njs_event+0x2a8>
fffffe000751d1dc:      	add	x9, x9, #376
fffffe000751d1e0:      	cmp	x8, x9
fffffe000751d1e4:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d1e8:      	ldr	x8, [x19, #16]
fffffe000751d1ec:      	adrp	x9, 0xfffffe0007c5b000 <_nss_worker_continue+0x1f0c>
fffffe000751d1f0:      	add	x9, x9, #1624
fffffe000751d1f4:      	cmp	x8, x9
fffffe000751d1f8:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d1fc:      	adrp	x15, 0xfffffe0007c98000 <_nss_worker_continue+0x2010>
fffffe000751d200:      	adrp	x8, 0xfffffe0008517000 <_njs_event+0x2d0>
fffffe000751d204:      	add	x8, x8, #400
fffffe000751d208:      	adrp	x12, 0xfffffe0007c5b000 <_nss_worker_continue+0x1f28>
fffffe000751d20c:      	add	x12, x12, #1636
fffffe000751d210:      	adrp	x16, 0xfffffe000851c000 <_njs_snapshot_locked+0xc>
fffffe000751d214:      	add	x16, x16, #2488
fffffe000751d218:      	add	x15, x15, #648
fffffe000751d21c:      	ldur	d0, [x8, #4]
fffffe000751d220:      	add	x9, x12, #3416
fffffe000751d224:      	ldr	s4, [x12, #1872]
fffffe000751d228:      	add	x11, x15, #16
fffffe000751d22c:      	add	x10, x15, #300
fffffe000751d230:      	ldr	q1, [x9]
fffffe000751d234:      	add	x9, x15, #60
fffffe000751d238:      	ldur	d3, [x16, #12]
fffffe000751d23c:      	ld1.s	{ v0 }[1], [x11]
fffffe000751d240:      	adrp	x11, 0xfffffe000700e000 <_nppa_source_owner+0x250>
fffffe000751d244:      	ldr	d2, [x10]
fffffe000751d248:      	add	x10, x12, #3408
fffffe000751d24c:      	ldr	q5, [x11, #272]
fffffe000751d250:      	ld1.s	{ v2 }[2], [x12]
fffffe000751d254:      	add	x12, x12, #32
fffffe000751d258:      	ld1.s	{ v0 }[2], [x9]
fffffe000751d25c:      	add	x9, x15, #280
fffffe000751d260:      	ld1.s	{ v4 }[1], [x10]
fffffe000751d264:      	ld1.s	{ v2 }[3], [x12]
fffffe000751d268:      	ld1.s	{ v0 }[3], [x9]
fffffe000751d26c:      	adrp	x9, 0xfffffe000700e000 <_nppa_source_owner+0x27c>
fffffe000751d270:      	mov.d	v4[1], v1[0]
fffffe000751d274:      	ext.16b	v1, v1, v3, #8
fffffe000751d278:      	cmeq.4s	v3, v2, #0
fffffe000751d27c:      	cmeq.4s	v0, v0, #0
fffffe000751d280:      	cmeq.4s	v4, v4, v5
fffffe000751d284:      	uzp1.8h	v0, v0, v3
fffffe000751d288:      	cmeq.4s	v3, v1, #0
fffffe000751d28c:      	cmtst.4s	v1, v1, v1
fffffe000751d290:      	uzp1.8h	v3, v4, v3
fffffe000751d294:      	mvn.16b	v4, v4
fffffe000751d298:      	cmtst.4s	v2, v2, v2
fffffe000751d29c:      	uzp1.8h	v1, v4, v1
fffffe000751d2a0:      	uzp1.16b	v3, v0, v3
fffffe000751d2a4:      	ldr	q0, [x9, #288]
fffffe000751d2a8:      	xtn2.8h	v2, v2
fffffe000751d2ac:      	uzp1.16b	v4, v2, v1
fffffe000751d2b0:      	tbl.16b	v0, { v3, v4 }, v0
fffffe000751d2b4:      	umov.b	w9, v0[1]
fffffe000751d2b8:      	umov.b	w11, v0[2]
fffffe000751d2bc:      	umov.b	w10, v0[0]
fffffe000751d2c0:      	umov.b	w12, v0[3]
fffffe000751d2c4:      	umov.b	w13, v0[4]
fffffe000751d2c8:      	umov.b	w14, v0[5]
fffffe000751d2cc:      	and	w9, w9, #0x1
fffffe000751d2d0:      	and	w11, w11, #0x1
fffffe000751d2d4:      	and	w10, w10, #0x1
fffffe000751d2d8:      	and	w12, w12, #0x1
fffffe000751d2dc:      	and	w13, w13, #0x1
fffffe000751d2e0:      	and	w14, w14, #0x1
fffffe000751d2e4:      	bfi	w10, w9, #1, #1
fffffe000751d2e8:      	umov.b	w9, v0[6]
fffffe000751d2ec:      	bfi	w10, w11, #2, #1
fffffe000751d2f0:      	umov.b	w11, v0[7]
fffffe000751d2f4:      	bfi	w10, w12, #3, #1
fffffe000751d2f8:      	umov.b	w12, v0[8]
fffffe000751d2fc:      	bfi	w10, w13, #4, #1
fffffe000751d300:      	umov.b	w13, v0[9]
fffffe000751d304:      	and	w9, w9, #0x1
fffffe000751d308:      	bfi	w10, w14, #5, #1
fffffe000751d30c:      	umov.b	w14, v0[10]
fffffe000751d310:      	and	w11, w11, #0x1
fffffe000751d314:      	orr	w9, w10, w9, lsl #6
fffffe000751d318:      	umov.b	w10, v0[11]
fffffe000751d31c:      	and	w12, w12, #0x1
fffffe000751d320:      	orr	w9, w9, w11, lsl #7
fffffe000751d324:      	umov.b	w11, v0[12]
fffffe000751d328:      	and	w13, w13, #0x1
fffffe000751d32c:      	orr	w9, w9, w12, lsl #8
fffffe000751d330:      	umov.b	w12, v0[13]
fffffe000751d334:      	and	w14, w14, #0x1
fffffe000751d338:      	orr	w9, w9, w13, lsl #9
fffffe000751d33c:      	umov.b	w13, v0[14]
fffffe000751d340:      	and	w10, w10, #0x1
fffffe000751d344:      	orr	w9, w9, w14, lsl #10
fffffe000751d348:      	and	w11, w11, #0x1
fffffe000751d34c:      	orr	w9, w9, w10, lsl #11
fffffe000751d350:      	and	w10, w12, #0x1
fffffe000751d354:      	umov.b	w12, v0[15]
fffffe000751d358:      	orr	w9, w9, w11, lsl #12
fffffe000751d35c:      	and	w11, w13, #0x1
fffffe000751d360:      	orr	w9, w9, w10, lsl #13
fffffe000751d364:      	mov	w10, #65535
fffffe000751d368:      	orr	w9, w9, w11, lsl #14
fffffe000751d36c:      	orr	w9, w9, w12, lsl #15
fffffe000751d370:      	bics	wzr, w10, w9
fffffe000751d374:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d378:      	add	x9, x16, #1832
fffffe000751d37c:      	ldr	q0, [x9]
fffffe000751d380:      	cmtst.4s	v1, v0, v0
fffffe000751d384:      	cmeq.4s	v0, v0, #0
fffffe000751d388:      	xtn.4h	v1, v1
fffffe000751d38c:      	xtn.4h	v0, v0
fffffe000751d390:      	rev32.4h	v1, v1
fffffe000751d394:      	trn2.4h	v0, v1, v0
fffffe000751d398:      	umov.h	w9, v0[1]
fffffe000751d39c:      	umov.h	w10, v0[2]
fffffe000751d3a0:      	umov.h	w11, v0[0]
fffffe000751d3a4:      	umov.h	w12, v0[3]
fffffe000751d3a8:      	and	w9, w9, #0x1
fffffe000751d3ac:      	and	w10, w10, #0x1
fffffe000751d3b0:      	bfi	w11, w9, #1, #1
fffffe000751d3b4:      	bfi	w11, w10, #2, #1
fffffe000751d3b8:      	bfi	w11, w12, #3, #29
fffffe000751d3bc:      	and	w9, w11, #0xf
fffffe000751d3c0:      	cmp	w9, #15
fffffe000751d3c4:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d3c8:      	adrp	x9, 0xfffffe0008516000 <_njs_notice+0x20>
fffffe000751d3cc:      	add	x9, x9, #40
fffffe000751d3d0:      	ldr	w8, [x8]
fffffe000751d3d4:      	ldp	w10, w9, [x9]
fffffe000751d3d8:      	orr	w9, w10, w9
fffffe000751d3dc:      	orr	w8, w9, w8
fffffe000751d3e0:      	cbnz	w8, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d3e4:      	adrp	x8, 0xfffffe0008518000 <_njs_notice+0x44>
fffffe000751d3e8:      	ldr	x8, [x8, #1488]
fffffe000751d3ec:      	cmp	x8, x15
fffffe000751d3f0:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d3f4:      	ldr	w8, [x16]
fffffe000751d3f8:      	cbnz	w8, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d3fc:      	ldrb	w8, [x16, #1897]
fffffe000751d400:      	cmp	w8, #7
fffffe000751d404:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d408:      	adrp	x8, 0xfffffe000851d000 <_njs_notice+0x7c>
fffffe000751d40c:      	add	x8, x8, #256
fffffe000751d410:      	ldrb	w9, [x8, #32]
fffffe000751d414:      	ldrb	w8, [x8]
fffffe000751d418:      	cmp	w9, w8
fffffe000751d41c:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d420:      	adrp	x8, 0xfffffe000851d000 <_njs_notice+0x94>
fffffe000751d424:      	ldr	w8, [x8, #244]
fffffe000751d428:      	cmp	w8, #16
fffffe000751d42c:      	b.hi	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d430:      	adrp	x9, 0xfffffe000851d000 <_njs_notice+0xa4>
fffffe000751d434:      	ldr	w9, [x9, #252]
fffffe000751d438:      	subs	w10, w9, w8
fffffe000751d43c:      	b.lo	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d440:      	adrp	x9, 0xfffffe000851d000 <_njs_rng+0x4>
fffffe000751d444:      	ldr	w9, [x9, #248]
fffffe000751d448:      	cmp	w9, w10
fffffe000751d44c:      	b.hi	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d450:      	adrp	x11, 0xfffffe000851d000 <_njs_rng+0x14>
fffffe000751d454:      	add	x11, x11, #264
fffffe000751d458:      	ldp	x11, x10, [x11]
fffffe000751d45c:      	cmp	x10, x11
fffffe000751d460:      	b.lo	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d464:      	adrp	x12, 0xfffffe000851d000 <_njs_rng+0x28>
fffffe000751d468:      	ldr	x12, [x12, #280]
fffffe000751d46c:      	cmp	x12, x11
fffffe000751d470:      	b.ls	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d474:      	cmp	x10, x12
fffffe000751d478:      	b.hs	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d47c:      	adrp	x10, 0xfffffe000851d000 <_njs_peer+0x10>
fffffe000751d480:      	ldr	w10, [x10, #240]
fffffe000751d484:      	cbz	w10, 0xfffffe000751d494 <_nss_worker_continue+0x4bc>
fffffe000751d488:      	cmp	w9, #0
fffffe000751d48c:      	ccmp	w8, #16, #0, ne
fffffe000751d490:      	b	0xfffffe000751d498 <_nss_worker_continue+0x4c0>
fffffe000751d494:      	cmp	w9, #0
fffffe000751d498:      	cset	w8, eq
fffffe000751d49c:      	cbz	w8, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d4a0:      	adrp	x8, 0xfffffe0007c5b000 <_nss_worker_continue+0x21c0>
fffffe000751d4a4:      	ldr	w8, [x8, #3552]
fffffe000751d4a8:      	cmp	w8, #31
fffffe000751d4ac:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d4b0:      	adrp	x26, 0xfffffe000851c000 <_njs_peer+0x40>
fffffe000751d4b4:      	add	x26, x26, #520
fffffe000751d4b8:      	add	x1, x26, #40
fffffe000751d4bc:      	add	x2, sp, #552
fffffe000751d4c0:      	mov	x0, x26
fffffe000751d4c4:      	bl	0xfffffe00079a0384 <_ncfg_address>
fffffe000751d4c8:      	cbnz	w0, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d4cc:      	ldr	w8, [sp, #560]
fffffe000751d4d0:      	cmp	w8, #1
fffffe000751d4d4:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d4d8:      	add	x1, x26, #40
fffffe000751d4dc:      	add	x2, sp, #592
fffffe000751d4e0:      	add	x4, sp, #528
fffffe000751d4e4:      	mov	x0, x26
fffffe000751d4e8:      	mov	w3, #39
fffffe000751d4ec:      	bl	0xfffffe00079a1304 <_ncfg_channels>
fffffe000751d4f0:      	cbnz	w0, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d4f4:      	add	x8, sp, #299
fffffe000751d4f8:      	adrp	x11, 0xfffffe00084d9000 <_njs_notice+0x5c>
fffffe000751d4fc:      	add	x11, x11, #2484
fffffe000751d500:      	ldrh	w10, [sp, #552]
fffffe000751d504:      	ldr	x9, [sp, #528]
fffffe000751d508:      	ldur	s0, [x8, #255]
fffffe000751d50c:      	adrp	x8, 0xfffffe000851c000 <_nss_build+0x10>
fffffe000751d510:      	add	x8, x8, #549
fffffe000751d514:      	strh	w10, [x11]
fffffe000751d518:      	ld1.s	{ v0 }[1], [x8]
fffffe000751d51c:      	ldr	w8, [x11, #12]
fffffe000751d520:      	stur	d0, [x11, #2]
fffffe000751d524:      	cbz	x9, 0xfffffe000751d58c <_nss_worker_continue+0x5b4>
fffffe000751d528:      	mov	x10, xzr
fffffe000751d52c:      	add	x11, sp, #592
fffffe000751d530:      	adrp	x16, 0xfffffe00084d9000 <_njs_notice+0x94>
fffffe000751d534:      	add	x16, x16, #2484
fffffe000751d538:      	b	0xfffffe000751d548 <_nss_worker_continue+0x570>
fffffe000751d53c:      	add	w10, w10, #1
fffffe000751d540:      	cmp	x9, x10
fffffe000751d544:      	b.ls	0xfffffe000751d58c <_nss_worker_continue+0x5b4>
fffffe000751d548:      	add	x12, x10, x10, lsl #2
fffffe000751d54c:      	add	x12, x11, x12
fffffe000751d550:      	ldrb	w13, [x12, #2]
fffffe000751d554:      	cbz	w13, 0xfffffe000751d53c <_nss_worker_continue+0x564>
fffffe000751d558:      	ldrb	w13, [x12, #3]
fffffe000751d55c:      	cbnz	w13, 0xfffffe000751d53c <_nss_worker_continue+0x564>
fffffe000751d560:      	add	x13, x10, x10, lsl #2
fffffe000751d564:      	add	w14, w8, #1
fffffe000751d568:      	add	x13, x11, x13
fffffe000751d56c:      	add	x15, x16, w8, uxtw #1
fffffe000751d570:      	ldrb	w12, [x12]
fffffe000751d574:      	mov	w8, w14
fffffe000751d578:      	str	w14, [x16, #12]
fffffe000751d57c:      	ldrb	w13, [x13, #1]
fffffe000751d580:      	strb	w12, [x15, #16]
fffffe000751d584:      	strb	w13, [x15, #17]
fffffe000751d588:      	b	0xfffffe000751d53c <_nss_worker_continue+0x564>
fffffe000751d58c:      	cbz	w8, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d590:      	adrp	x9, 0xfffffe000851c000 <_ncc_ap_report+0x2c>
fffffe000751d594:      	ldr	w9, [x9, #512]
fffffe000751d598:      	cmp	w8, w9
fffffe000751d59c:      	b.ne	0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d5a0:      	adrp	x8, 0xfffffe000851d000 <_ncc_ap_report+0x40>
fffffe000751d5a4:      	adrp	x20, 0xfffffe00084d9000 <_njs_peer+0x28>
fffffe000751d5a8:      	add	x20, x20, #2456
fffffe000751d5ac:      	ldrb	w8, [x8, #256]
fffffe000751d5b0:      	strb	w8, [x20, #24]
fffffe000751d5b4:      	bl	0xfffffe0007a1bf64 <_IOLockAlloc>
fffffe000751d5b8:      	str	x0, [x20]
fffffe000751d5bc:      	cbz	x0, 0xfffffe000751d06c <_nss_worker_continue+0x94>
fffffe000751d5c0:      	bl	0xfffffe0007a1bf64 <_IOLockAlloc>
fffffe000751d5c4:      	adrp	x20, 0xfffffe00084f4000 <_nss_build+0x28>
fffffe000751d5c8:      	add	x20, x20, #544
fffffe000751d5cc:      	str	x0, [x20]
fffffe000751d5d0:      	adrp	x0, 0xfffffe000851d000 <_ncc_ap_report+0x70>
fffffe000751d5d4:      	add	x0, x0, #224
fffffe000751d5d8:      	bl	0xfffffe0007520c2c <_njs_cache_scan_locked>
fffffe000751d5dc:      	mov	w8, #1
fffffe000751d5e0:      	adrp	x9, 0xfffffe00084d9000 <_njs_peer+0x64>
fffffe000751d5e4:      	add	x9, x9, #2464
fffffe000751d5e8:      	adrp	x0, 0xfffffe000709a000 <_nppa_xnu_word+0xc>
fffffe000751d5ec:      	add	x0, x0, #1784
fffffe000751d5f0:      	str	w8, [x20, #3272]
fffffe000751d5f4:      	stlr	w8, [x9]
fffffe000751d5f8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751d5fc:      	bl	0xfffffe0007af03a8 <_nord2_wifi_ios_backend_ready>
fffffe000751d600:      	add	x8, sp, #528
fffffe000751d604:      	add	x9, sp, #552
fffffe000751d608:      	orr	x10, x8, #0x6
fffffe000751d60c:      	orr	x8, x9, #0x4
fffffe000751d610:      	adrp	x12, 0xfffffe00084f4000 <_ncc_ap_report+0xc>
fffffe000751d614:      	add	x12, x12, #904
fffffe000751d618:      	adrp	x14, 0xfffffe00084f4000 <_ncc_ap_report+0x14>
fffffe000751d61c:      	add	x14, x14, #556
fffffe000751d620:      	stp	x8, x10, [sp, #136]
fffffe000751d624:      	add	x8, sp, #592
fffffe000751d628:      	add	x10, x8, #16
fffffe000751d62c:      	add	x9, x8, #24
fffffe000751d630:      	add	x8, x8, #92
fffffe000751d634:      	adrp	x11, 0xfffffe00084f4000 <_ncc_ap_report+0x30>
fffffe000751d638:      	add	x11, x11, #552
fffffe000751d63c:      	adrp	x13, 0xfffffe00084f4000 <_ncc_ap_report+0x38>
fffffe000751d640:      	add	x13, x13, #596
fffffe000751d644:      	stp	x9, x10, [sp, #112]
fffffe000751d648:      	str	x8, [sp, #104]
fffffe000751d64c:      	mov	w8, #4344
fffffe000751d650:      	add	x8, x12, x8
fffffe000751d654:      	adrp	x10, 0xfffffe00084f4000 <_ncc_ap_report+0x50>
fffffe000751d658:      	add	x10, x10, #572
fffffe000751d65c:      	adrp	x9, 0xfffffe00084f4000 <_ncc_ap_report+0x58>
fffffe000751d660:      	add	x9, x9, #560
fffffe000751d664:      	movi.2s	v8, #1
fffffe000751d668:      	str	x8, [sp, #312]
fffffe000751d66c:      	add	x8, x10, #20, lsl #12
fffffe000751d670:      	add	x8, x8, #996
fffffe000751d674:      	str	x8, [sp, #128]
fffffe000751d678:      	add	x8, x20, #20, lsl #12
fffffe000751d67c:      	add	x10, x8, #1024
fffffe000751d680:      	add	x8, x20, #19, lsl #12
fffffe000751d684:      	add	x8, x8, #240
fffffe000751d688:      	stp	x8, x10, [sp, #88]
fffffe000751d68c:      	add	x8, x20, #20, lsl #12
fffffe000751d690:      	add	x8, x8, #1008
fffffe000751d694:      	adrp	x10, 0xfffffe00084f4000 <_ncc_cold_read>
fffffe000751d698:      	add	x10, x10, #604
fffffe000751d69c:      	str	x8, [sp, #80]
fffffe000751d6a0:      	add	x8, x10, #19, lsl #12
fffffe000751d6a4:      	add	x8, x8, #180
fffffe000751d6a8:      	str	x8, [sp, #392]
fffffe000751d6ac:      	add	x8, x10, #20, lsl #12
fffffe000751d6b0:      	add	x8, x8, #1028
fffffe000751d6b4:      	str	x8, [sp, #384]
fffffe000751d6b8:      	add	x8, x10, #23, lsl #12
fffffe000751d6bc:      	add	x8, x8, #852
fffffe000751d6c0:      	adrp	x10, 0xfffffe000700e000 <_nppa_xnu_smc+0x164>
fffffe000751d6c4:      	str	x8, [sp, #376]
fffffe000751d6c8:      	add	x8, x9, #19, lsl #12
fffffe000751d6cc:      	add	x8, x8, #224
fffffe000751d6d0:      	add	x9, x14, #20, lsl #12
fffffe000751d6d4:      	add	x9, x9, #1076
fffffe000751d6d8:      	str	x8, [sp, #440]
fffffe000751d6dc:      	add	x8, x14, #19, lsl #12
fffffe000751d6e0:      	add	x8, x8, #1692
fffffe000751d6e4:      	str	x9, [sp, #352]
fffffe000751d6e8:      	add	x9, x14, #23, lsl #12
fffffe000751d6ec:      	add	x9, x9, #900
fffffe000751d6f0:      	str	x8, [sp, #360]
fffffe000751d6f4:      	adrp	x8, 0xfffffe000700e000 <_nppa_xnu_smc+0x198>
fffffe000751d6f8:      	str	x9, [sp, #344]
fffffe000751d6fc:      	add	x9, x11, #19, lsl #12
fffffe000751d700:      	ldr	q0, [x8, #336]
fffffe000751d704:      	adrp	x8, 0xfffffe000700c000 <_nppa_xnu_smc+0x1a0>
fffffe000751d708:      	adrp	x11, 0xfffffe000700e000 <_nppa_xnu_smc+0x1ac>
fffffe000751d70c:      	add	x14, x9, #232
fffffe000751d710:      	mov	w9, #4784
fffffe000751d714:      	str	q0, [sp, #240]
fffffe000751d718:      	ldr	q0, [x10, #352]
fffffe000751d71c:      	nop
fffffe000751d720:      	add	x9, x14, x9
fffffe000751d724:      	str	x14, [sp, #320]
fffffe000751d728:      	str	q0, [sp, #224]
fffffe000751d72c:      	ldr	q0, [x8, #3664]
fffffe000751d730:      	adrp	x8, 0xfffffe000700e000 <_nppa_xnu_map+0x24>
fffffe000751d734:      	str	x9, [sp, #184]
fffffe000751d738:      	adrp	x9, 0xfffffe000700e000 <_nppa_xnu_map+0x2c>
fffffe000751d73c:      	str	q0, [sp, #208]
fffffe000751d740:      	ldr	q0, [x10, #368]
fffffe000751d744:      	ldr	d9, [x8, #152]
fffffe000751d748:      	add	x8, x13, #19, lsl #12
fffffe000751d74c:      	add	x8, x8, #188
fffffe000751d750:      	nop
fffffe000751d754:      	str	q0, [sp, #192]
fffffe000751d758:      	ldr	q0, [x11, #160]
fffffe000751d75c:      	str	x8, [sp, #288]
fffffe000751d760:      	adrp	x8, 0xfffffe00084f4000 <_ncc_cold_read+0xcc>
fffffe000751d764:      	add	x8, x8, #608
fffffe000751d768:      	str	q0, [sp, #160]
fffffe000751d76c:      	add	x8, x8, #19, lsl #12
fffffe000751d770:      	ldr	q0, [x10, #304]
fffffe000751d774:      	add	x8, x8, #176
fffffe000751d778:      	adrp	x10, 0xfffffe00084d9000 <_ncc_cold_read+0x78>
fffffe000751d77c:      	add	x10, x10, #2480
fffffe000751d780:      	str	q0, [sp, #416]
fffffe000751d784:      	ldr	q0, [x9, #320]
fffffe000751d788:      	adrp	x9, 0xfffffe0008518000 <_ncc_native_check+0x54>
fffffe000751d78c:      	add	x9, x9, #1508
fffffe000751d790:      	str	x8, [sp, #368]
fffffe000751d794:      	mov	w8, #8696
fffffe000751d798:      	add	x8, x9, x8
fffffe000751d79c:      	adrp	x9, 0xfffffe00084eb000 <_ncc_cold_read+0xe4>
fffffe000751d7a0:      	add	x9, x9, #2776
fffffe000751d7a4:      	str	q0, [sp, #272]
fffffe000751d7a8:      	str	x8, [sp, #408]
fffffe000751d7ac:      	add	x8, x10, #26, lsl #12
fffffe000751d7b0:      	add	x8, x8, #792
fffffe000751d7b4:      	adrp	x10, 0xfffffe000851a000 <_ncc_native_check+0x88>
fffffe000751d7b8:      	add	x10, x10, #2528
fffffe000751d7bc:      	str	x8, [sp, #400]
fffffe000751d7c0:      	mov	w8, #33264
fffffe000751d7c4:      	add	x8, x9, x8
fffffe000751d7c8:      	mov	w9, #59352
fffffe000751d7cc:      	str	x8, [sp, #264]
fffffe000751d7d0:      	mov	w8, #8128
fffffe000751d7d4:      	add	x8, x10, x8
fffffe000751d7d8:      	str	x8, [sp, #152]
fffffe000751d7dc:      	mov	w8, #22680
fffffe000751d7e0:      	add	x8, x12, x8
fffffe000751d7e4:      	str	x8, [sp, #256]
fffffe000751d7e8:      	mov	w8, #41016
fffffe000751d7ec:      	add	x8, x12, x8
fffffe000751d7f0:      	str	x8, [sp, #296]
fffffe000751d7f4:      	add	x8, x12, x9
fffffe000751d7f8:      	str	x8, [sp, #304]
fffffe000751d7fc:      	mov	w8, #17420
fffffe000751d800:      	movk	w8, #1, lsl #16
fffffe000751d804:      	add	x8, x13, x8
fffffe000751d808:      	str	x8, [sp, #336]
fffffe000751d80c:      	mov	w8, #29532
fffffe000751d810:      	movk	w8, #1, lsl #16
fffffe000751d814:      	add	x8, x13, x8
fffffe000751d818:      	str	x8, [sp, #328]
fffffe000751d81c:      	b	0xfffffe000751d830 <_nss_worker_continue+0x858>
fffffe000751d820:      	mov	w26, #3
fffffe000751d824:      	mov	x0, x22
fffffe000751d828:      	mov	w1, w26
fffffe000751d82c:      	bl	0xfffffe0007520d04 <_nss_deliver>
fffffe000751d830:      	ldr	x0, [x24, #2456]
fffffe000751d834:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751d838:      	adrp	x10, 0xfffffe00084f4000 <_ncc_native_check+0x74>
fffffe000751d83c:      	add	x10, x10, #552
fffffe000751d840:      	mov	w22, wzr
fffffe000751d844:      	ldp	w8, w9, [x10, #8]
fffffe000751d848:      	ldp	w20, w26, [x10]
fffffe000751d84c:      	orr	w8, w8, w9
fffffe000751d850:      	cmp	w8, #0
fffffe000751d854:      	cset	w8, eq
fffffe000751d858:      	str	w8, [sp, #504]
fffffe000751d85c:      	cbz	w20, 0xfffffe000751d8ac <_nss_worker_continue+0x8d4>
fffffe000751d860:      	cbnz	w26, 0xfffffe000751d8ac <_nss_worker_continue+0x8d4>
fffffe000751d864:      	mov	w8, #55440
fffffe000751d868:      	adrp	x11, 0xfffffe00084d9000 <_ncc_native_check+0x38>
fffffe000751d86c:      	add	x11, x11, #2584
fffffe000751d870:      	mov	w9, #36960
fffffe000751d874:      	mov	w10, #18480
fffffe000751d878:      	ldr	w8, [x11, x8]
fffffe000751d87c:      	ldr	w9, [x11, x9]
fffffe000751d880:      	ldr	w10, [x11, x10]
fffffe000751d884:      	sub	w8, w8, #1
fffffe000751d888:      	sub	w9, w9, #1
fffffe000751d88c:      	cmp	w8, #2
fffffe000751d890:      	ccmp	w9, #2, #0, hs
fffffe000751d894:      	ldr	w8, [x11]
fffffe000751d898:      	sub	w9, w10, #1
fffffe000751d89c:      	ccmp	w9, #2, #0, hs
fffffe000751d8a0:      	sub	w8, w8, #1
fffffe000751d8a4:      	ccmp	w8, #2, #0, hs
fffffe000751d8a8:      	cset	w22, lo
fffffe000751d8ac:      	adrp	x8, 0xfffffe00084f4000 <_ncc_native_check+0xe8>
fffffe000751d8b0:      	add	x8, x8, #712
fffffe000751d8b4:      	ldr	x0, [x24, #2456]
fffffe000751d8b8:      	ldp	x9, x21, [x8]
fffffe000751d8bc:      	str	x9, [sp, #496]
fffffe000751d8c0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751d8c4:      	orr	w8, w20, w26
fffffe000751d8c8:      	cbz	w8, 0xfffffe000751d934 <_nss_worker_continue+0x95c>
fffffe000751d8cc:      	cbz	w20, 0xfffffe000751d9bc <_nss_worker_continue+0x9e4>
fffffe000751d8d0:      	add	x0, sp, #592
fffffe000751d8d4:      	str	xzr, [sp, #592]
fffffe000751d8d8:      	str	xzr, [sp, #552]
fffffe000751d8dc:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751d8e0:      	ldr	x0, [sp, #592]
fffffe000751d8e4:      	add	x1, sp, #552
fffffe000751d8e8:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751d8ec:      	ldr	x8, [sp, #552]
fffffe000751d8f0:      	mov	x9, #63439
fffffe000751d8f4:      	movk	x9, #58195, lsl #16
fffffe000751d8f8:      	ldr	w10, [sp, #504]
fffffe000751d8fc:      	movk	x9, #39845, lsl #32
fffffe000751d900:      	lsr	x8, x8, #3
fffffe000751d904:      	movk	x9, #8388, lsl #48
fffffe000751d908:      	umulh	x8, x8, x9
fffffe000751d90c:      	ldr	x9, [sp, #496]
fffffe000751d910:      	lsr	x8, x8, #4
fffffe000751d914:      	cmp	x8, x9
fffffe000751d918:      	cset	w9, hs
fffffe000751d91c:      	and	w9, w10, w9
fffffe000751d920:      	cmp	w9, #1
fffffe000751d924:      	b.ne	0xfffffe000751dafc <_nss_worker_continue+0xb24>
fffffe000751d928:      	cmp	x8, x21
fffffe000751d92c:      	b.hs	0xfffffe000751dafc <_nss_worker_continue+0xb24>
fffffe000751d930:      	cbz	w22, 0xfffffe000751d9bc <_nss_worker_continue+0x9e4>
fffffe000751d934:      	adrp	x20, 0xfffffe00084d9000 <_ncc_native_check+0x104>
fffffe000751d938:      	add	x20, x20, #2456
fffffe000751d93c:      	ldr	x0, [x20]
fffffe000751d940:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751d944:      	mov	w8, #18608
fffffe000751d948:      	ldr	w9, [x20, #128]
fffffe000751d94c:      	ldr	w8, [x20, x8]
fffffe000751d950:      	cmp	w9, #1
fffffe000751d954:      	b.ne	0xfffffe000751d984 <_nss_worker_continue+0x9ac>
fffffe000751d958:      	adrp	x22, 0xfffffe00084d9000 <_ncc_native_check+0x128>
fffffe000751d95c:      	add	x22, x22, #2584
fffffe000751d960:      	cmp	w8, #1
fffffe000751d964:      	b.ne	0xfffffe000751d994 <_nss_worker_continue+0x9bc>
fffffe000751d968:      	adrp	x22, 0xfffffe00084d9000 <_ncc_native_check+0x138>
fffffe000751d96c:      	add	x22, x22, #2584
fffffe000751d970:      	ldr	x8, [x22, #18496]
fffffe000751d974:      	ldr	x9, [x22, #16]
fffffe000751d978:      	cmp	x8, x9
fffffe000751d97c:      	b.lo	0xfffffe000751d98c <_nss_worker_continue+0x9b4>
fffffe000751d980:      	b	0xfffffe000751d994 <_nss_worker_continue+0x9bc>
fffffe000751d984:      	cmp	w8, #1
fffffe000751d988:      	b.ne	0xfffffe000751db1c <_nss_worker_continue+0xb44>
fffffe000751d98c:      	adrp	x22, 0xfffffe00084de000 <_ncc_native_check+0x170>
fffffe000751d990:      	add	x22, x22, #584
fffffe000751d994:      	adrp	x8, 0xfffffe00084e2000 <_ncc_native_check+0x188>
fffffe000751d998:      	ldr	w8, [x8, #2680]
fffffe000751d99c:      	cmp	w8, #1
fffffe000751d9a0:      	b.ne	0xfffffe000751db78 <_nss_worker_continue+0xba0>
fffffe000751d9a4:      	adrp	x8, 0xfffffe00084e2000 <_ncc_native_check+0x198>
fffffe000751d9a8:      	ldr	x9, [x22, #16]
fffffe000751d9ac:      	ldr	x8, [x8, #2696]
fffffe000751d9b0:      	cmp	x8, x9
fffffe000751d9b4:      	b.lo	0xfffffe000751db2c <_nss_worker_continue+0xb54>
fffffe000751d9b8:      	b	0xfffffe000751db78 <_nss_worker_continue+0xba0>
fffffe000751d9bc:      	ldr	w8, [x25, #12]
fffffe000751d9c0:      	str	xzr, [sp, #592]
fffffe000751d9c4:      	str	xzr, [sp, #552]
fffffe000751d9c8:      	cbz	w8, 0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
fffffe000751d9cc:      	ldr	w8, [x25, #20]
fffffe000751d9d0:      	cbnz	w8, 0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
fffffe000751d9d4:      	ldr	x8, [x25]
fffffe000751d9d8:      	cmp	x8, x28
fffffe000751d9dc:      	b.ne	0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
fffffe000751d9e0:      	add	x0, sp, #592
fffffe000751d9e4:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751d9e8:      	ldr	x0, [sp, #592]
fffffe000751d9ec:      	add	x1, sp, #552
fffffe000751d9f0:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751d9f4:      	ldr	x8, [sp, #552]
fffffe000751d9f8:      	mov	x9, #63439
fffffe000751d9fc:      	movk	x9, #58195, lsl #16
fffffe000751da00:      	movk	x9, #39845, lsl #32
fffffe000751da04:      	lsr	x8, x8, #3
fffffe000751da08:      	movk	x9, #8388, lsl #48
fffffe000751da0c:      	umulh	x8, x8, x9
fffffe000751da10:      	lsr	x22, x8, #4
fffffe000751da14:      	cbz	w20, 0xfffffe000751e0b0 <_nss_worker_continue+0x10d8>
fffffe000751da18:      	ldr	x0, [x24, #2456]
fffffe000751da1c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751da20:      	adrp	x11, 0xfffffe00084f4000 <_ncc_native_read+0xc0>
fffffe000751da24:      	add	x11, x11, #560
fffffe000751da28:      	ldp	w8, w9, [x11]
fffffe000751da2c:      	orr	w10, w8, w9
fffffe000751da30:      	ldr	x9, [x11, #160]
fffffe000751da34:      	cmp	w10, #0
fffffe000751da38:      	cset	w8, ne
fffffe000751da3c:      	cbnz	w10, 0xfffffe000751db48 <_nss_worker_continue+0xb70>
fffffe000751da40:      	ldr	x10, [x11, #152]
fffffe000751da44:      	cmp	x10, x22
fffffe000751da48:      	b.hi	0xfffffe000751db48 <_nss_worker_continue+0xb70>
fffffe000751da4c:      	cmp	x9, x22
fffffe000751da50:      	b.ls	0xfffffe000751db48 <_nss_worker_continue+0xb70>
fffffe000751da54:      	adrp	x0, 0xfffffe00084f4000 <_ncc_native_read+0xf4>
fffffe000751da58:      	add	x0, x0, #904
fffffe000751da5c:      	add	x26, x0, #1496
fffffe000751da60:      	ldr	x1, [sp, #312]
fffffe000751da64:      	adrp	x2, 0xfffffe00084d9000 <_ncc_native_read+0x98>
fffffe000751da68:      	add	x2, x2, #2484
fffffe000751da6c:      	mov	x3, x22
fffffe000751da70:      	mov	x4, x26
fffffe000751da74:      	bl	0xfffffe00079a7de8 <_niwj_select>
fffffe000751da78:      	cbz	w0, 0xfffffe000751de04 <_nss_worker_continue+0xe2c>
fffffe000751da7c:      	adrp	x0, 0xfffffe00084f4000 <_ncc_native_read+0x11c>
fffffe000751da80:      	add	x0, x0, #904
fffffe000751da84:      	mov	w8, #22680
fffffe000751da88:      	adrp	x2, 0xfffffe00084d9000 <_ncc_native_read+0xbc>
fffffe000751da8c:      	add	x2, x2, #2484
fffffe000751da90:      	add	x1, x0, x8
fffffe000751da94:      	mov	x3, x22
fffffe000751da98:      	mov	x4, x26
fffffe000751da9c:      	bl	0xfffffe00079a7de8 <_niwj_select>
fffffe000751daa0:      	cbz	w0, 0xfffffe000751de54 <_nss_worker_continue+0xe7c>
fffffe000751daa4:      	adrp	x0, 0xfffffe00084f4000 <_ncc_native_write+0x10>
fffffe000751daa8:      	add	x0, x0, #904
fffffe000751daac:      	mov	w8, #41016
fffffe000751dab0:      	add	x26, x0, #1496
fffffe000751dab4:      	add	x1, x0, x8
fffffe000751dab8:      	adrp	x2, 0xfffffe00084d9000 <_ncc_native_read+0xec>
fffffe000751dabc:      	add	x2, x2, #2484
fffffe000751dac0:      	mov	x3, x22
fffffe000751dac4:      	mov	x4, x26
fffffe000751dac8:      	bl	0xfffffe00079a7de8 <_niwj_select>
fffffe000751dacc:      	cbz	w0, 0xfffffe000751dea4 <_nss_worker_continue+0xecc>
fffffe000751dad0:      	adrp	x0, 0xfffffe00084f4000 <_ncc_native_write+0x3c>
fffffe000751dad4:      	add	x0, x0, #904
fffffe000751dad8:      	mov	w8, #59352
fffffe000751dadc:      	adrp	x2, 0xfffffe00084d9000 <_ncc_native_read+0x110>
fffffe000751dae0:      	add	x2, x2, #2484
fffffe000751dae4:      	add	x1, x0, x8
fffffe000751dae8:      	mov	x3, x22
fffffe000751daec:      	mov	x4, x26
fffffe000751daf0:      	bl	0xfffffe00079a7de8 <_niwj_select>
fffffe000751daf4:      	cbnz	w0, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751daf8:      	b	0xfffffe000751def4 <_nss_worker_continue+0xf1c>
fffffe000751dafc:      	cmp	x8, x21
fffffe000751db00:      	mov	w9, #9
fffffe000751db04:      	cset	w8, hs
fffffe000751db08:      	tst	w10, w8
fffffe000751db0c:      	mov	w8, #2
fffffe000751db10:      	csel	w0, w9, w8, ne
fffffe000751db14:      	bl	0xfffffe0007520e78 <_njs_early_finish>
fffffe000751db18:      	b	0xfffffe000751d830 <_nss_worker_continue+0x858>
fffffe000751db1c:      	adrp	x8, 0xfffffe00084e2000 <_ncc_native_write+0x40>
fffffe000751db20:      	ldr	w8, [x8, #2680]
fffffe000751db24:      	cmp	w8, #1
fffffe000751db28:      	b.ne	0xfffffe000751db74 <_nss_worker_continue+0xb9c>
fffffe000751db2c:      	mov	w8, #18480
fffffe000751db30:      	adrp	x22, 0xfffffe00084e2000 <_ncc_native_write+0x54>
fffffe000751db34:      	add	x22, x22, #2680
fffffe000751db38:      	ldr	w8, [x22, x8]
fffffe000751db3c:      	cmp	w8, #1
fffffe000751db40:      	b.eq	0xfffffe000751db8c <_nss_worker_continue+0xbb4>
fffffe000751db44:      	b	0xfffffe000751dbb0 <_nss_worker_continue+0xbd8>
fffffe000751db48:      	cmp	x9, x22
fffffe000751db4c:      	ldr	x0, [x24, #2456]
fffffe000751db50:      	cset	w9, hi
fffffe000751db54:      	orr	w8, w8, w9
fffffe000751db58:      	mov	w9, #9
fffffe000751db5c:      	cmp	w8, #0
fffffe000751db60:      	mov	w8, #2
fffffe000751db64:      	csel	w26, w8, w9, ne
fffffe000751db68:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751db6c:      	mov	w0, w26
fffffe000751db70:      	b	0xfffffe000751e038 <_nss_worker_continue+0x1060>
fffffe000751db74:      	mov	x22, xzr
fffffe000751db78:      	adrp	x8, 0xfffffe00084e7000 <_ncc_native_write+0xb0>
fffffe000751db7c:      	ldr	w8, [x8, #680]
fffffe000751db80:      	cmp	w8, #1
fffffe000751db84:      	b.ne	0xfffffe000751dbac <_nss_worker_continue+0xbd4>
fffffe000751db88:      	cbz	x22, 0xfffffe000751dba0 <_nss_worker_continue+0xbc8>
fffffe000751db8c:      	adrp	x8, 0xfffffe00084e7000 <_ncc_native_write+0xc4>
fffffe000751db90:      	ldr	x9, [x22, #16]
fffffe000751db94:      	ldr	x8, [x8, #696]
fffffe000751db98:      	cmp	x8, x9
fffffe000751db9c:      	b.hs	0xfffffe000751dbb0 <_nss_worker_continue+0xbd8>
fffffe000751dba0:      	adrp	x22, 0xfffffe00084e7000 <_ncc_native_write+0xd8>
fffffe000751dba4:      	add	x22, x22, #680
fffffe000751dba8:      	b	0xfffffe000751dbb0 <_nss_worker_continue+0xbd8>
fffffe000751dbac:      	cbz	x22, 0xfffffe000751ddc8 <_nss_worker_continue+0xdf0>
fffffe000751dbb0:      	mov	w26, #2
fffffe000751dbb4:      	ldr	w20, [x22, #4]
fffffe000751dbb8:      	str	w26, [x22]
fffffe000751dbbc:      	ldr	x0, [x24, #2456]
fffffe000751dbc0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751dbc4:      	cbnz	w20, 0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751dbc8:      	ldr	x8, [x25]
fffffe000751dbcc:      	mov	w26, #3
fffffe000751dbd0:      	cmp	x8, x28
fffffe000751dbd4:      	b.ne	0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751dbd8:      	ldur	d0, [x25, #20]
fffffe000751dbdc:      	ldr	x8, [sp, #408]
fffffe000751dbe0:      	ld1.s	{ v0 }[1], [x8]
fffffe000751dbe4:      	adrp	x8, 0xfffffe0008518000 <_nis_scope_or_common+0x3c>
fffffe000751dbe8:      	add	x8, x8, #1508
fffffe000751dbec:      	ldr	d1, [x8]
fffffe000751dbf0:      	mov.d	v0[1], v1[0]
fffffe000751dbf4:      	ldr	q1, [sp, #416]
fffffe000751dbf8:      	cmeq.4s	v0, v0, v1
fffffe000751dbfc:      	mvn.16b	v0, v0
fffffe000751dc00:      	xtn.4h	v0, v0
fffffe000751dc04:      	umov.h	w8, v0[1]
fffffe000751dc08:      	umov.h	w9, v0[2]
fffffe000751dc0c:      	umov.h	w10, v0[0]
fffffe000751dc10:      	umov.h	w11, v0[3]
fffffe000751dc14:      	and	w8, w8, #0x1
fffffe000751dc18:      	and	w9, w9, #0x1
fffffe000751dc1c:      	bfi	w10, w8, #1, #1
fffffe000751dc20:      	bfi	w10, w9, #2, #1
fffffe000751dc24:      	bfi	w10, w11, #3, #29
fffffe000751dc28:      	and	w8, w10, #0xf
fffffe000751dc2c:      	cbnz	w8, 0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751dc30:      	ldr	w8, [x25, #12]
fffffe000751dc34:      	cbz	w8, 0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751dc38:      	adrp	x9, 0xfffffe000851c000 <_nis_scope_or_common+0xa0>
fffffe000751dc3c:      	add	x9, x9, #2488
fffffe000751dc40:      	ldr	w8, [x9]
fffffe000751dc44:      	cbnz	w8, 0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751dc48:      	ldr	w8, [x9, #12]
fffffe000751dc4c:      	cbnz	w8, 0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751dc50:      	adrp	x14, 0xfffffe0008517000 <_nis_scope_or_common+0xa4>
fffffe000751dc54:      	add	x14, x14, #376
fffffe000751dc58:      	mov	w12, #60
fffffe000751dc5c:      	adrp	x13, 0xfffffe00084d9000 <_ncc_native_write+0x15c>
fffffe000751dc60:      	add	x13, x13, #2480
fffffe000751dc64:      	ldr	w9, [x22, #60]
fffffe000751dc68:      	ldr	w8, [x14, #44]
fffffe000751dc6c:      	add	x11, x13, #20
fffffe000751dc70:      	add	x3, x13, #4
fffffe000751dc74:      	add	x10, x22, #64
fffffe000751dc78:      	cmp	w9, #0
fffffe000751dc7c:      	ldr	x1, [x14, #4048]
fffffe000751dc80:      	sub	w8, w8, #1
fffffe000751dc84:      	csel	x4, x11, x10, eq
fffffe000751dc88:      	add	x0, x14, #201
fffffe000751dc8c:      	mov	w6, #1
fffffe000751dc90:      	umaddl	x8, w8, w12, x14
fffffe000751dc94:      	ldr	w12, [x13, #16]
fffffe000751dc98:      	ldrb	w13, [x13]
fffffe000751dc9c:      	mov	w7, #2
fffffe000751dca0:      	csel	w5, w12, w9, eq
fffffe000751dca4:      	ldr	x9, [sp, #400]
fffffe000751dca8:      	ldrb	w2, [x8, #4071]
fffffe000751dcac:      	add	w26, w13, #1
fffffe000751dcb0:      	ldrh	w8, [x22, #142]
fffffe000751dcb4:      	strh	wzr, [sp, #10]
fffffe000751dcb8:      	str	x9, [sp, #16]
fffffe000751dcbc:      	strb	w26, [sp]
fffffe000751dcc0:      	strh	w8, [sp, #8]
fffffe000751dcc4:      	mov	w8, #1
fffffe000751dcc8:      	str	w8, [sp, #4]
fffffe000751dccc:      	bl	0xfffffe000799fb2c <_ncs_build>
fffffe000751dcd0:      	cbnz	w0, 0xfffffe000751d820 <_nss_worker_continue+0x848>
fffffe000751dcd4:      	adrp	x20, 0xfffffe00084eb000 <_nis_scope_or_common+0x78>
fffffe000751dcd8:      	add	x20, x20, #2776
fffffe000751dcdc:      	mov	x0, x20
fffffe000751dce0:      	mov	x1, x19
fffffe000751dce4:      	mov	w2, #8736
fffffe000751dce8:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751dcec:      	mov	w8, #8736
fffffe000751dcf0:      	adrp	x21, 0xfffffe000851c000 <_nis_scope_or_common+0x158>
fffffe000751dcf4:      	add	x21, x21, #2464
fffffe000751dcf8:      	add	x0, x20, x8
fffffe000751dcfc:      	mov	x1, x21
fffffe000751dd00:      	mov	w2, #24528
fffffe000751dd04:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751dd08:      	mov	x0, x19
fffffe000751dd0c:      	mov	w1, wzr
fffffe000751dd10:      	mov	w2, #8736
fffffe000751dd14:      	mov	w3, #8744
fffffe000751dd18:      	str	x20, [x20, #8744]
fffffe000751dd1c:      	bl	0xfffffe000746b32c <___memset_chk>
fffffe000751dd20:      	mov	x0, x21
fffffe000751dd24:      	mov	w1, #24528
fffffe000751dd28:      	bl	0xfffffe0007bad720 <_bzero>
fffffe000751dd2c:      	adrp	x9, 0xfffffe000851a000 <_nis_scope_or_common+0x18c>
fffffe000751dd30:      	add	x9, x9, #2484
fffffe000751dd34:      	ldr	q0, [sp, #272]
fffffe000751dd38:      	add	x2, sp, #592
fffffe000751dd3c:      	adrp	x0, 0xfffffe0008516000 <_nis_scope_or_common+0x18c>
fffffe000751dd40:      	add	x0, x0, #0
fffffe000751dd44:      	ldp	w8, w9, [x9]
fffffe000751dd48:      	str	q0, [sp, #624]
fffffe000751dd4c:      	cmp	w8, #0
fffffe000751dd50:      	mov	w8, #1
fffffe000751dd54:      	ccmp	w9, #0, #0, ne
fffffe000751dd58:      	str	x8, [sp, #600]
fffffe000751dd5c:      	adrp	x8, 0xfffffe000700e000 <_ncd_worker+0x78>
fffffe000751dd60:      	add	x8, x8, #1108
fffffe000751dd64:      	cset	w20, eq
fffffe000751dd68:      	str	x8, [sp, #592]
fffffe000751dd6c:      	adr	x8, #14220
fffffe000751dd70:      	nop
fffffe000751dd74:      	str	x8, [sp, #608]
fffffe000751dd78:      	ldr	x8, [sp, #264]
fffffe000751dd7c:      	str	x8, [sp, #616]
fffffe000751dd80:      	b.ne	0xfffffe000751e5d0 <_nss_worker_continue+0x15f8>
fffffe000751dd84:      	adrp	x1, 0xfffffe000851a000 <_nis_scope_or_common+0x1e4>
fffffe000751dd88:      	add	x1, x1, #2016
fffffe000751dd8c:      	mov	x3, x19
fffffe000751dd90:      	bl	0xfffffe000799b350 <_nct_scan_return_prepare>
fffffe000751dd94:      	cbnz	w0, 0xfffffe000751e614 <_nss_worker_continue+0x163c>
fffffe000751dd98:      	mov	w5, #40832
fffffe000751dd9c:      	and	w4, w26, #0xff
fffffe000751dda0:      	adrp	x0, 0xfffffe000851c000 <_nis_scope_or_common+0x208>
fffffe000751dda4:      	add	x0, x0, #2464
fffffe000751dda8:      	adrp	x1, 0xfffffe0007c5b000 <_nss_worker_continue+0x2ac8>
fffffe000751ddac:      	add	x1, x1, #1624
fffffe000751ddb0:      	mov	x2, x19
fffffe000751ddb4:      	adrp	x3, 0xfffffe000851a000 <_nis_inner+0x4>
fffffe000751ddb8:      	add	x3, x3, #2080
fffffe000751ddbc:      	movk	w5, #213, lsl #16
fffffe000751ddc0:      	bl	0xfffffe000799d55c <_ncr_scan_return_prepare>
fffffe000751ddc4:      	b	0xfffffe000751e610 <_nss_worker_continue+0x1638>
fffffe000751ddc8:      	adrp	x9, 0xfffffe00084f4000 <_nis_scope_or_common+0x190>
fffffe000751ddcc:      	add	x9, x9, #552
fffffe000751ddd0:      	ldp	w8, w9, [x9]
fffffe000751ddd4:      	orr	w8, w8, w9
fffffe000751ddd8:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751dddc:      	adrp	x20, 0xfffffe00084d9000 <_nis_scope_or_common+0x138>
fffffe000751dde0:      	add	x20, x20, #2456
fffffe000751dde4:      	adrp	x8, 0xfffffe0008522000 <_nis_inner+0x54>
fffffe000751dde8:      	add	x8, x8, #3032
fffffe000751ddec:      	mov	w9, #6
fffffe000751ddf0:      	mov	x0, x20
fffffe000751ddf4:      	mov	w1, wzr
fffffe000751ddf8:      	stlr	w9, [x8]
fffffe000751ddfc:      	bl	0xfffffe000736c68c <_assert_wait>
fffffe000751de00:      	b	0xfffffe000751e8f0 <_nss_worker_continue+0x1918>
fffffe000751de04:      	adrp	x20, 0xfffffe00084f4000 <_nis_scope_or_common+0x1cc>
fffffe000751de08:      	add	x20, x20, #904
fffffe000751de0c:      	add	x0, x20, #104
fffffe000751de10:      	mov	x1, x26
fffffe000751de14:      	mov	w2, #1392
fffffe000751de18:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751de1c:      	mov	x0, x20
fffffe000751de20:      	ldr	x1, [sp, #256]
fffffe000751de24:      	adrp	x2, 0xfffffe00084d9000 <_nis_scope_or_common+0x180>
fffffe000751de28:      	add	x2, x2, #2484
fffffe000751de2c:      	mov	x3, x22
fffffe000751de30:      	mov	x4, x26
fffffe000751de34:      	bl	0xfffffe00079a7de8 <_niwj_select>
fffffe000751de38:      	cbnz	w0, 0xfffffe000751de68 <_nss_worker_continue+0xe90>
fffffe000751de3c:      	adrp	x9, 0xfffffe00084f4000 <_nis_scope_or_common+0x204>
fffffe000751de40:      	add	x9, x9, #2396
fffffe000751de44:      	ldrsh	w8, [x9, #1392]
fffffe000751de48:      	ldrsh	w9, [x9]
fffffe000751de4c:      	cmp	w8, w9
fffffe000751de50:      	b.le	0xfffffe000751de68 <_nss_worker_continue+0xe90>
fffffe000751de54:      	adrp	x0, 0xfffffe00084f4000 <_nis_inner+0xc>
fffffe000751de58:      	add	x0, x0, #1008
fffffe000751de5c:      	add	x1, x0, #1392
fffffe000751de60:      	mov	w2, #1392
fffffe000751de64:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751de68:      	adrp	x0, 0xfffffe00084f4000 <_nis_inner+0x20>
fffffe000751de6c:      	add	x0, x0, #904
fffffe000751de70:      	add	x4, x0, #1496
fffffe000751de74:      	ldr	x1, [sp, #296]
fffffe000751de78:      	adrp	x2, 0xfffffe00084d9000 <_nis_scope_or_common+0x1d4>
fffffe000751de7c:      	add	x2, x2, #2484
fffffe000751de80:      	mov	x3, x22
fffffe000751de84:      	bl	0xfffffe00079a7de8 <_niwj_select>
fffffe000751de88:      	cbnz	w0, 0xfffffe000751deb8 <_nss_worker_continue+0xee0>
fffffe000751de8c:      	adrp	x9, 0xfffffe00084f4000 <_nis_inner+0x44>
fffffe000751de90:      	add	x9, x9, #2396
fffffe000751de94:      	ldrsh	w8, [x9, #1392]
fffffe000751de98:      	ldrsh	w9, [x9]
fffffe000751de9c:      	cmp	w8, w9
fffffe000751dea0:      	b.le	0xfffffe000751deb8 <_nss_worker_continue+0xee0>
fffffe000751dea4:      	adrp	x0, 0xfffffe00084f4000 <_nis_native_check>
fffffe000751dea8:      	add	x0, x0, #1008
fffffe000751deac:      	add	x1, x0, #1392
fffffe000751deb0:      	mov	w2, #1392
fffffe000751deb4:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751deb8:      	adrp	x0, 0xfffffe00084f4000 <_nis_native_check+0x14>
fffffe000751debc:      	add	x0, x0, #904
fffffe000751dec0:      	add	x4, x0, #1496
fffffe000751dec4:      	ldr	x1, [sp, #304]
fffffe000751dec8:      	adrp	x2, 0xfffffe00084d9000 <_nis_inner+0x14>
fffffe000751decc:      	add	x2, x2, #2484
fffffe000751ded0:      	mov	x3, x22
fffffe000751ded4:      	bl	0xfffffe00079a7de8 <_niwj_select>
fffffe000751ded8:      	cbnz	w0, 0xfffffe000751df08 <_nss_worker_continue+0xf30>
fffffe000751dedc:      	adrp	x9, 0xfffffe00084f4000 <_nis_native_check+0x38>
fffffe000751dee0:      	add	x9, x9, #2396
fffffe000751dee4:      	ldrsh	w8, [x9, #1392]
fffffe000751dee8:      	ldrsh	w9, [x9]
fffffe000751deec:      	cmp	w8, w9
fffffe000751def0:      	b.le	0xfffffe000751df08 <_nss_worker_continue+0xf30>
fffffe000751def4:      	adrp	x0, 0xfffffe00084f4000 <_nis_native_check+0x50>
fffffe000751def8:      	add	x0, x0, #1008
fffffe000751defc:      	add	x1, x0, #1392
fffffe000751df00:      	mov	w2, #1392
fffffe000751df04:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751df08:      	adrp	x8, 0xfffffe00084f4000 <_nis_native_check+0x64>
fffffe000751df0c:      	add	x8, x8, #720
fffffe000751df10:      	ldr	x26, [x8, #32]
fffffe000751df14:      	cbz	x26, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df18:      	adrp	x8, 0xfffffe0008515000 <_nis_native_check+0xf8>
fffffe000751df1c:      	ldr	w8, [x8, #3328]
fffffe000751df20:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df24:      	ldr	x8, [x25]
fffffe000751df28:      	cmp	x8, x28
fffffe000751df2c:      	b.ne	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df30:      	ldr	w8, [x25, #12]
fffffe000751df34:      	cbz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df38:      	ldr	w8, [x25, #16]
fffffe000751df3c:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df40:      	ldr	w8, [x25, #20]
fffffe000751df44:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df48:      	adrp	x8, 0xfffffe000851a000 <_nis_native_check+0x13c>
fffffe000751df4c:      	ldr	w8, [x8, #2012]
fffffe000751df50:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df54:      	adrp	x8, 0xfffffe00084f4000 <_nis_native_check+0xb0>
fffffe000751df58:      	nop
fffffe000751df5c:      	ldr	x20, [x8, #720]
fffffe000751df60:      	adrp	x8, 0xfffffe00084d9000 <_nis_native_check+0x50>
fffffe000751df64:      	add	x8, x8, #2464
fffffe000751df68:      	ldar	w8, [x8]
fffffe000751df6c:      	cmp	w8, #1
fffffe000751df70:      	b.ne	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df74:      	adrp	x8, 0xfffffe00084d9000 <_nis_native_check+0x64>
fffffe000751df78:      	add	x8, x8, #2464
fffffe000751df7c:      	ldr	w8, [x8, #4]
fffffe000751df80:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df84:      	ldr	w8, [x23]
fffffe000751df88:      	cbz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df8c:      	adrp	x8, 0xfffffe0008516000 <_nis_native_check+0x170>
fffffe000751df90:      	ldr	w8, [x8, #428]
fffffe000751df94:      	cbz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751df98:      	ldr	w8, [x23, #8144]
fffffe000751df9c:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751dfa0:      	adrp	x0, 0xfffffe000851d000 <_nis_read+0x20>
fffffe000751dfa4:      	add	x0, x0, #224
fffffe000751dfa8:      	bl	0xfffffe00079ab6d0 <_nst_scan_admissible>
fffffe000751dfac:      	cbz	w0, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751dfb0:      	mov	x0, x19
fffffe000751dfb4:      	bl	0xfffffe000799afb0 <_nct_done_receipt>
fffffe000751dfb8:      	cbz	w0, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751dfbc:      	adrp	x8, 0xfffffe00084d9000 <_nis_native_check+0xac>
fffffe000751dfc0:      	ldr	w8, [x8, #2584]
fffffe000751dfc4:      	sub	w8, w8, #1
fffffe000751dfc8:      	cmp	w8, #2
fffffe000751dfcc:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751dfd0:      	adrp	x8, 0xfffffe00084de000 <_nis_native_check+0xd4>
fffffe000751dfd4:      	ldr	w8, [x8, #584]
fffffe000751dfd8:      	sub	w8, w8, #1
fffffe000751dfdc:      	cmp	w8, #2
fffffe000751dfe0:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751dfe4:      	adrp	x8, 0xfffffe00084e2000 <_nis_native_check+0xf8>
fffffe000751dfe8:      	ldr	w8, [x8, #2680]
fffffe000751dfec:      	sub	w8, w8, #1
fffffe000751dff0:      	cmp	w8, #2
fffffe000751dff4:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751dff8:      	adrp	x8, 0xfffffe00084e7000 <_nis_native_check+0x120>
fffffe000751dffc:      	ldr	w8, [x8, #680]
fffffe000751e000:      	sub	w8, w8, #1
fffffe000751e004:      	cmp	w8, #2
fffffe000751e008:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751e00c:      	adrp	x8, 0xfffffe0008517000 <_nis_read+0x70>
fffffe000751e010:      	str	xzr, [sp, #600]
fffffe000751e014:      	str	xzr, [sp, #592]
fffffe000751e018:      	str	xzr, [sp, #608]
fffffe000751e01c:      	ldr	w8, [x8, #420]
fffffe000751e020:      	sub	w9, w8, #17
fffffe000751e024:      	cmn	w9, #16
fffffe000751e028:      	b.hs	0xfffffe000751f8bc <_nss_worker_continue+0x28e4>
fffffe000751e02c:      	ldr	x0, [x24, #2456]
fffffe000751e030:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e034:      	mov	w0, #2
fffffe000751e038:      	bl	0xfffffe0007520e78 <_njs_early_finish>
fffffe000751e03c:      	ldr	x0, [x24, #2456]
fffffe000751e040:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e044:      	adrp	x8, 0xfffffe00084f4000 <_nis_read+0x1c>
fffffe000751e048:      	ldr	x0, [x24, #2456]
fffffe000751e04c:      	ldr	w20, [x8, #556]
fffffe000751e050:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e054:      	cbz	w20, 0xfffffe000751d830 <_nss_worker_continue+0x858>
fffffe000751e058:      	ldr	w8, [x25, #12]
fffffe000751e05c:      	str	xzr, [sp, #592]
fffffe000751e060:      	str	xzr, [sp, #552]
fffffe000751e064:      	cbz	w8, 0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
fffffe000751e068:      	ldr	w8, [x25, #20]
fffffe000751e06c:      	cbnz	w8, 0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
fffffe000751e070:      	ldr	x8, [x25]
fffffe000751e074:      	cmp	x8, x28
fffffe000751e078:      	b.ne	0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
fffffe000751e07c:      	add	x0, sp, #592
fffffe000751e080:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751e084:      	ldr	x0, [sp, #592]
fffffe000751e088:      	add	x1, sp, #552
fffffe000751e08c:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751e090:      	ldr	x8, [sp, #552]
fffffe000751e094:      	mov	x9, #63439
fffffe000751e098:      	movk	x9, #58195, lsl #16
fffffe000751e09c:      	movk	x9, #39845, lsl #32
fffffe000751e0a0:      	lsr	x8, x8, #3
fffffe000751e0a4:      	movk	x9, #8388, lsl #48
fffffe000751e0a8:      	umulh	x8, x8, x9
fffffe000751e0ac:      	lsr	x22, x8, #4
fffffe000751e0b0:      	ldr	x0, [x24, #2456]
fffffe000751e0b4:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e0b8:      	adrp	x8, 0xfffffe00084f4000 <_nis_read+0x90>
fffffe000751e0bc:      	ldr	w10, [x8, #568]
fffffe000751e0c0:      	cbz	w10, 0xfffffe000751e0cc <_nss_worker_continue+0x10f4>
fffffe000751e0c4:      	adrp	x8, 0xfffffe00084f4000 <_nis_read+0x9c>
fffffe000751e0c8:      	str	wzr, [x8, #568]
fffffe000751e0cc:      	adrp	x9, 0xfffffe00084f4000 <_nis_read+0xa4>
fffffe000751e0d0:      	add	x9, x9, #560
fffffe000751e0d4:      	cmp	w10, #0
fffffe000751e0d8:      	str	w10, [sp, #496]
fffffe000751e0dc:      	ldp	w8, w9, [x9]
fffffe000751e0e0:      	orr	w8, w8, w9
fffffe000751e0e4:      	cset	w9, ne
fffffe000751e0e8:      	str	w9, [sp, #456]
fffffe000751e0ec:      	cbz	w8, 0xfffffe000751e154 <_nss_worker_continue+0x117c>
fffffe000751e0f0:      	mov	x9, #63439
fffffe000751e0f4:      	lsr	x8, x22, #3
fffffe000751e0f8:      	movk	x9, #58195, lsl #16
fffffe000751e0fc:      	adrp	x20, 0xfffffe00084f4000 <_nis_write_mask+0x1c>
fffffe000751e100:      	add	x20, x20, #604
fffffe000751e104:      	movk	x9, #39845, lsl #32
fffffe000751e108:      	movk	x9, #8388, lsl #48
fffffe000751e10c:      	ldr	x0, [sp, #392]
fffffe000751e110:      	umulh	x8, x8, x9
fffffe000751e114:      	ldur	x1, [x20, #148]
fffffe000751e118:      	lsr	x21, x8, #4
fffffe000751e11c:      	mov	x2, x21
fffffe000751e120:      	bl	0xfffffe00079b9bac <_nwn_cancel>
fffffe000751e124:      	ldr	w8, [x20, #4]
fffffe000751e128:      	mov	w1, #12112
fffffe000751e12c:      	ldr	w9, [x20, #72]
fffffe000751e130:      	ldr	x0, [sp, #384]
fffffe000751e134:      	add	w8, w9, w8
fffffe000751e138:      	str	w8, [x20, #72]
fffffe000751e13c:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751e140:      	movi.2d	v0, #0000000000000000
fffffe000751e144:      	ldr	x8, [sp, #376]
fffffe000751e148:      	str	xzr, [x20]
fffffe000751e14c:      	stp	q0, q0, [x8]
fffffe000751e150:      	b	0xfffffe000751e170 <_nss_worker_continue+0x1198>
fffffe000751e154:      	mov	x9, #63439
fffffe000751e158:      	lsr	x8, x22, #3
fffffe000751e15c:      	movk	x9, #58195, lsl #16
fffffe000751e160:      	movk	x9, #39845, lsl #32
fffffe000751e164:      	movk	x9, #8388, lsl #48
fffffe000751e168:      	umulh	x8, x8, x9
fffffe000751e16c:      	lsr	x21, x8, #4
fffffe000751e170:      	adrp	x0, 0xfffffe0008507000 <_nis_write_mask+0xdc>
fffffe000751e174:      	add	x0, x0, #784
fffffe000751e178:      	mov	x1, x21
fffffe000751e17c:      	str	x21, [sp, #448]
fffffe000751e180:      	bl	0xfffffe00079b99ec <_nwn_pump>
fffffe000751e184:      	tbnz	w0, #31, 0xfffffe000751ff58 <_nss_worker_continue+0x2f80>
fffffe000751e188:      	adrp	x9, 0xfffffe00084f4000 <_nis_write_mask+0xa8>
fffffe000751e18c:      	add	x9, x9, #560
fffffe000751e190:      	adrp	x21, 0xfffffe00084f4000 <_nis_write_mask+0xb0>
fffffe000751e194:      	add	x21, x21, #544
fffffe000751e198:      	ldp	w8, w9, [x9]
fffffe000751e19c:      	orr	w8, w8, w9
fffffe000751e1a0:      	cbnz	w8, 0xfffffe000751e270 <_nss_worker_continue+0x1298>
fffffe000751e1a4:      	ldr	x0, [sp, #368]
fffffe000751e1a8:      	bl	0xfffffe00079b8c30 <_nwo_authorized>
fffffe000751e1ac:      	cbz	w0, 0xfffffe000751e270 <_nss_worker_continue+0x1298>
fffffe000751e1b0:      	adrp	x8, 0xfffffe0008511000 <_nis_write_mask+0x144>
fffffe000751e1b4:      	nop
fffffe000751e1b8:      	ldr	w8, [x8, #1256]
fffffe000751e1bc:      	cbnz	w8, 0xfffffe000751e270 <_nss_worker_continue+0x1298>
fffffe000751e1c0:      	adrp	x8, 0xfffffe0008511000 <_nis_write_mask+0x154>
fffffe000751e1c4:      	add	x8, x8, #1256
fffffe000751e1c8:      	ldr	w8, [x8, #4]
fffffe000751e1cc:      	cbnz	w8, 0xfffffe000751e270 <_nss_worker_continue+0x1298>
fffffe000751e1d0:      	adrp	x8, 0xfffffe00084f4000 <_nis_write_mask+0xf0>
fffffe000751e1d4:      	nop
fffffe000751e1d8:      	ldr	w8, [x8, #608]
fffffe000751e1dc:      	cbz	w8, 0xfffffe000751e270 <_nss_worker_continue+0x1298>
fffffe000751e1e0:      	ldr	w20, [x21, #60]
fffffe000751e1e4:      	mov	w9, #29584
fffffe000751e1e8:      	movk	w9, #1, lsl #16
fffffe000751e1ec:      	adrp	x0, 0xfffffe0008511000 <_nis_write_mask+0x180>
fffffe000751e1f0:      	add	x0, x0, #1200
fffffe000751e1f4:      	mov	x1, #-1
fffffe000751e1f8:      	add	x8, x21, x20, lsl #2
fffffe000751e1fc:      	add	x22, x8, x9
fffffe000751e200:      	mov	w8, #1514
fffffe000751e204:      	mov	w9, #17472
fffffe000751e208:      	madd	x8, x20, x8, x21
fffffe000751e20c:      	movk	w9, #1, lsl #16
fffffe000751e210:      	ldr	w3, [x22]
fffffe000751e214:      	add	x26, x8, x9
fffffe000751e218:      	mov	x2, x26
fffffe000751e21c:      	bl	0xfffffe00079ac6f8 <_nst_data>
fffffe000751e220:      	cmp	w0, #0
fffffe000751e224:      	b.gt	0xfffffe000751e270 <_nss_worker_continue+0x1298>
fffffe000751e228:      	tbz	w0, #31, 0xfffffe000751e23c <_nss_worker_continue+0x1264>
fffffe000751e22c:      	adrp	x9, 0xfffffe00084f4000 <_nis_write_mask+0x14c>
fffffe000751e230:      	ldr	w8, [x9, #676]
fffffe000751e234:      	add	w8, w8, #1
fffffe000751e238:      	str	w8, [x9, #676]
fffffe000751e23c:      	mov	x0, x26
fffffe000751e240:      	mov	w1, #1514
fffffe000751e244:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751e248:      	adrp	x10, 0xfffffe00084f4000 <_nis_write_mask+0x168>
fffffe000751e24c:      	add	x10, x10, #604
fffffe000751e250:      	add	w9, w20, #1
fffffe000751e254:      	adrp	x21, 0xfffffe00084f4000 <_nis_write_mask+0x174>
fffffe000751e258:      	add	x21, x21, #544
fffffe000751e25c:      	and	w9, w9, #0x7
fffffe000751e260:      	ldr	w8, [x10, #4]
fffffe000751e264:      	str	wzr, [x22]
fffffe000751e268:      	sub	w8, w8, #1
fffffe000751e26c:      	stp	w9, w8, [x10]
fffffe000751e270:      	adrp	x8, 0xfffffe0008511000 <_nis_id_check+0x38>
fffffe000751e274:      	ldr	w8, [x8, #1244]
fffffe000751e278:      	cbnz	w8, 0xfffffe000751ff50 <_nss_worker_continue+0x2f78>
fffffe000751e27c:      	adrp	x0, 0xfffffe0008511000 <_nis_id_check+0x44>
fffffe000751e280:      	add	x0, x0, #1200
fffffe000751e284:      	adrp	x1, 0xfffffe000721b000 <_nss_worker_continue+0x6a0>
fffffe000751e288:      	add	x1, x1, #1232
fffffe000751e28c:      	adrp	x2, 0xfffffe0008517000 <_nis_id_check+0x6c>
fffffe000751e290:      	add	x2, x2, #376
fffffe000751e294:      	mov	w3, #16
fffffe000751e298:      	adr	x4, #11392
fffffe000751e29c:      	nop
fffffe000751e2a0:      	mov	x5, x21
fffffe000751e2a4:      	bl	0xfffffe00079acf08 <_nst_step>
fffffe000751e2a8:      	tbnz	w0, #31, 0xfffffe000751ff50 <_nss_worker_continue+0x2f78>
fffffe000751e2ac:      	bl	0xfffffe0007521200 <_njs_snapshot_locked>
fffffe000751e2b0:      	mov	w0, wzr
fffffe000751e2b4:      	bl	0xfffffe00075129a8 <_njs_trace_locked>
fffffe000751e2b8:      	ldr	x0, [sp, #440]
fffffe000751e2bc:      	bl	0xfffffe00079b8c30 <_nwo_authorized>
fffffe000751e2c0:      	adrp	x8, 0xfffffe0008511000 <_nis_id_check+0x88>
fffffe000751e2c4:      	adrp	x11, 0xfffffe00084f4000 <_nis_id_check+0x18>
fffffe000751e2c8:      	add	x11, x11, #560
fffffe000751e2cc:      	cmp	w0, #0
fffffe000751e2d0:      	ldr	w8, [x8, #2720]
fffffe000751e2d4:      	ldr	w10, [x11, #36]
fffffe000751e2d8:      	ccmp	w8, #0, #4, ne
fffffe000751e2dc:      	ldp	w8, w9, [x11]
fffffe000751e2e0:      	cset	w12, eq
fffffe000751e2e4:      	cmp	w8, #0
fffffe000751e2e8:      	ldr	w8, [x11, #3276]
fffffe000751e2ec:      	cset	w11, ne
fffffe000751e2f0:      	cmp	w9, #0
fffffe000751e2f4:      	cset	w9, ne
fffffe000751e2f8:      	cmp	w10, #0
fffffe000751e2fc:      	cset	w10, ne
fffffe000751e300:      	cmp	w8, #0
fffffe000751e304:      	cset	w8, eq
fffffe000751e308:      	mov	w22, w11
fffffe000751e30c:      	orr	w8, w8, w11
fffffe000751e310:      	str	w12, [sp, #504]
fffffe000751e314:      	orr	w8, w8, w9
fffffe000751e318:      	str	w9, [sp, #472]
fffffe000751e31c:      	and	w20, w8, w10
fffffe000751e320:      	str	w10, [sp, #464]
fffffe000751e324:      	cmp	w20, #1
fffffe000751e328:      	b.ne	0xfffffe000751e368 <_nss_worker_continue+0x1390>
fffffe000751e32c:      	adrp	x21, 0xfffffe00084f4000 <_nis_id_check+0x80>
fffffe000751e330:      	add	x21, x21, #596
fffffe000751e334:      	movi.2d	v0, #0000000000000000
fffffe000751e338:      	ldr	x0, [sp, #336]
fffffe000751e33c:      	mov	w1, #12112
fffffe000751e340:      	ldr	w8, [x21, #12]
fffffe000751e344:      	ldr	w9, [x21, #80]
fffffe000751e348:      	str	d0, [x21]
fffffe000751e34c:      	add	w8, w9, w8
fffffe000751e350:      	str	w8, [x21, #80]
fffffe000751e354:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751e358:      	movi.2d	v0, #0000000000000000
fffffe000751e35c:      	ldr	x8, [sp, #328]
fffffe000751e360:      	stp	q0, q0, [x8]
fffffe000751e364:      	str	d0, [x21, #8]
fffffe000751e368:      	adrp	x8, 0xfffffe0008507000 <_nis_acquire+0x20>
fffffe000751e36c:      	ldr	w8, [x8, #788]
fffffe000751e370:      	sub	w8, w8, #21
fffffe000751e374:      	cmp	w8, #1
fffffe000751e378:      	str	w8, [sp, #488]
fffffe000751e37c:      	b.hi	0xfffffe000751e394 <_nss_worker_continue+0x13bc>
fffffe000751e380:      	adrp	x8, 0xfffffe00084f4000 <_nis_id_check+0xd4>
fffffe000751e384:      	ldr	w8, [x8, #580]
fffffe000751e388:      	cbz	w8, 0xfffffe000751e39c <_nss_worker_continue+0x13c4>
fffffe000751e38c:      	str	wzr, [sp, #480]
fffffe000751e390:      	b	0xfffffe000751e3b0 <_nss_worker_continue+0x13d8>
fffffe000751e394:      	str	wzr, [sp, #480]
fffffe000751e398:      	b	0xfffffe000751e3fc <_nss_worker_continue+0x1424>
fffffe000751e39c:      	mov	w10, #1
fffffe000751e3a0:      	adrp	x8, 0xfffffe00084f4000 <_nis_acquire+0xc>
fffffe000751e3a4:      	mov	w9, #1
fffffe000751e3a8:      	str	w10, [x8, #580]
fffffe000751e3ac:      	str	w9, [sp, #480]
fffffe000751e3b0:      	adrp	x21, 0xfffffe00084f4000 <_nis_acquire+0x1c>
fffffe000751e3b4:      	add	x21, x21, #556
fffffe000751e3b8:      	mov	w8, #1
fffffe000751e3bc:      	ldr	x0, [sp, #360]
fffffe000751e3c0:      	mov	w1, #2616
fffffe000751e3c4:      	str	wzr, [x21]
fffffe000751e3c8:      	str	w8, [x21, #28]
fffffe000751e3cc:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751e3d0:      	ldr	w8, [x21, #52]
fffffe000751e3d4:      	mov	w1, #12112
fffffe000751e3d8:      	ldr	w9, [x21, #120]
fffffe000751e3dc:      	ldr	x0, [sp, #352]
fffffe000751e3e0:      	add	w8, w9, w8
fffffe000751e3e4:      	str	w8, [x21, #120]
fffffe000751e3e8:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751e3ec:      	movi.2d	v0, #0000000000000000
fffffe000751e3f0:      	ldr	x8, [sp, #344]
fffffe000751e3f4:      	str	xzr, [x21, #48]
fffffe000751e3f8:      	stp	q0, q0, [x8]
fffffe000751e3fc:      	ldr	x0, [x24, #2456]
fffffe000751e400:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e404:      	cbz	w20, 0xfffffe000751e488 <_nss_worker_continue+0x14b0>
fffffe000751e408:      	adrp	x20, 0xfffffe00084f4000 <_nis_acquire+0x74>
fffffe000751e40c:      	add	x20, x20, #544
fffffe000751e410:      	ldr	x0, [x20]
fffffe000751e414:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e418:      	ldr	x0, [x20, #208]
fffffe000751e41c:      	mov	w1, wzr
fffffe000751e420:      	bl	0xfffffe0007aee934 <_nord2_wifi_ifnet_link>
fffffe000751e424:      	mov	w26, w0
fffffe000751e428:      	ldr	x0, [x20]
fffffe000751e42c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e430:      	cbz	w26, 0xfffffe000751e488 <_nss_worker_continue+0x14b0>
fffffe000751e434:      	ldr	x0, [x24, #2456]
fffffe000751e438:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e43c:      	adrp	x8, 0xfffffe00084f4000 <_nis_acquire+0xa8>
fffffe000751e440:      	ldr	w8, [x8, #616]
fffffe000751e444:      	cmp	w8, #111
fffffe000751e448:      	b.hi	0xfffffe000751e480 <_nss_worker_continue+0x14a8>
fffffe000751e44c:      	add	w8, w8, #1
fffffe000751e450:      	ldr	x0, [x24, #2456]
fffffe000751e454:      	adrp	x20, 0xfffffe00084f4000 <_nis_acquire+0xc0>
fffffe000751e458:      	add	x20, x20, #616
fffffe000751e45c:      	str	w8, [x20]
fffffe000751e460:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e464:      	ldr	x8, [x20, #136]
fffffe000751e468:      	adrp	x0, 0xfffffe000709a000 <_nss_worker_continue+0x280>
fffffe000751e46c:      	add	x0, x0, #2249
fffffe000751e470:      	stp	xzr, x26, [sp, #8]
fffffe000751e474:      	str	x8, [sp]
fffffe000751e478:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751e47c:      	b	0xfffffe000751e488 <_nss_worker_continue+0x14b0>
fffffe000751e480:      	ldr	x0, [x24, #2456]
fffffe000751e484:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e488:      	ldr	w8, [sp, #504]
fffffe000751e48c:      	ldr	w9, [sp, #472]
fffffe000751e490:      	orr	w8, w8, w22
fffffe000751e494:      	orr	w8, w8, w9
fffffe000751e498:      	ldr	w9, [sp, #464]
fffffe000751e49c:      	orr	w8, w8, w9
fffffe000751e4a0:      	tbz	w8, #0, 0xfffffe000751e4ac <_nss_worker_continue+0x14d4>
fffffe000751e4a4:      	str	wzr, [sp, #472]
fffffe000751e4a8:      	b	0xfffffe000751e6f8 <_nss_worker_continue+0x1720>
fffffe000751e4ac:      	adrp	x20, 0xfffffe00084f4000 <_nis_acquire+0x118>
fffffe000751e4b0:      	add	x20, x20, #544
fffffe000751e4b4:      	ldr	x0, [x20]
fffffe000751e4b8:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e4bc:      	ldr	x0, [x24, #2456]
fffffe000751e4c0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e4c4:      	ldp	w8, w9, [x20, #16]
fffffe000751e4c8:      	orr	w8, w8, w9
fffffe000751e4cc:      	cbnz	w8, 0xfffffe000751e50c <_nss_worker_continue+0x1534>
fffffe000751e4d0:      	adrp	x0, 0xfffffe0008507000 <_nis_release+0x34>
fffffe000751e4d4:      	add	x0, x0, #784
fffffe000751e4d8:      	bl	0xfffffe00079b8c30 <_nwo_authorized>
fffffe000751e4dc:      	cbz	w0, 0xfffffe000751e50c <_nss_worker_continue+0x1534>
fffffe000751e4e0:      	mov	w26, #1
fffffe000751e4e4:      	ldr	x0, [x24, #2456]
fffffe000751e4e8:      	adrp	x20, 0xfffffe00084f4000 <_nis_release>
fffffe000751e4ec:      	add	x20, x20, #600
fffffe000751e4f0:      	str	w26, [x20]
fffffe000751e4f4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e4f8:      	ldr	x0, [x20, #152]
fffffe000751e4fc:      	mov	w1, #1
fffffe000751e500:      	bl	0xfffffe0007aee934 <_nord2_wifi_ifnet_link>
fffffe000751e504:      	mov	w21, w0
fffffe000751e508:      	b	0xfffffe000751e51c <_nss_worker_continue+0x1544>
fffffe000751e50c:      	ldr	x0, [x24, #2456]
fffffe000751e510:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e514:      	mov	w26, wzr
fffffe000751e518:      	mov	w21, wzr
fffffe000751e51c:      	ldr	x0, [x24, #2456]
fffffe000751e520:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e524:      	adrp	x9, 0xfffffe00084f4000 <_nis_release+0x3c>
fffffe000751e528:      	add	x9, x9, #560
fffffe000751e52c:      	str	wzr, [x9, #40]
fffffe000751e530:      	cbz	w26, 0xfffffe000751e56c <_nss_worker_continue+0x1594>
fffffe000751e534:      	cbnz	w21, 0xfffffe000751e56c <_nss_worker_continue+0x1594>
fffffe000751e538:      	ldr	w8, [x9]
fffffe000751e53c:      	cbnz	w8, 0xfffffe000751e56c <_nss_worker_continue+0x1594>
fffffe000751e540:      	ldr	w8, [x9, #4]
fffffe000751e544:      	cbnz	w8, 0xfffffe000751e56c <_nss_worker_continue+0x1594>
fffffe000751e548:      	mov	w8, #1
fffffe000751e54c:      	adrp	x20, 0xfffffe00084f4000 <_nis_id_read+0xc>
fffffe000751e550:      	add	x20, x20, #576
fffffe000751e554:      	str	w8, [x20, #20]
fffffe000751e558:      	bl	0xfffffe0007521200 <_njs_snapshot_locked>
fffffe000751e55c:      	ldr	w8, [x20]
fffffe000751e560:      	cbz	w8, 0xfffffe000751e654 <_nss_worker_continue+0x167c>
fffffe000751e564:      	str	wzr, [sp, #472]
fffffe000751e568:      	b	0xfffffe000751e668 <_nss_worker_continue+0x1690>
fffffe000751e56c:      	adrp	x20, 0xfffffe00084f4000 <_nis_id_read+0x2c>
fffffe000751e570:      	add	x20, x20, #596
fffffe000751e574:      	ldr	x0, [sp, #288]
fffffe000751e578:      	ldr	x2, [sp, #448]
fffffe000751e57c:      	ldur	x1, [x20, #156]
fffffe000751e580:      	str	wzr, [x20]
fffffe000751e584:      	bl	0xfffffe00079b9bac <_nwn_cancel>
fffffe000751e588:      	ldr	w8, [x20, #12]
fffffe000751e58c:      	mov	w1, #12112
fffffe000751e590:      	ldr	w9, [x20, #80]
fffffe000751e594:      	add	w8, w9, w8
fffffe000751e598:      	mov	w9, #17420
fffffe000751e59c:      	movk	w9, #1, lsl #16
fffffe000751e5a0:      	add	x0, x20, x9
fffffe000751e5a4:      	str	w8, [x20, #80]
fffffe000751e5a8:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751e5ac:      	mov	w8, #29532
fffffe000751e5b0:      	str	xzr, [x20, #8]
fffffe000751e5b4:      	movk	w8, #1, lsl #16
fffffe000751e5b8:      	movi.2d	v0, #0000000000000000
fffffe000751e5bc:      	add	x8, x20, x8
fffffe000751e5c0:      	stp	q0, q0, [x8]
fffffe000751e5c4:      	bl	0xfffffe0007521200 <_njs_snapshot_locked>
fffffe000751e5c8:      	str	wzr, [sp, #472]
fffffe000751e5cc:      	b	0xfffffe000751e668 <_nss_worker_continue+0x1690>
fffffe000751e5d0:      	adrp	x1, 0xfffffe00084eb000 <_nis_id_read+0x6c>
fffffe000751e5d4:      	add	x1, x1, #2776
fffffe000751e5d8:      	mov	x3, x19
fffffe000751e5dc:      	bl	0xfffffe000799b138 <_nct_continue_prepare>
fffffe000751e5e0:      	cbnz	w0, 0xfffffe000751e614 <_nss_worker_continue+0x163c>
fffffe000751e5e4:      	mov	w5, #40832
fffffe000751e5e8:      	and	w4, w26, #0xff
fffffe000751e5ec:      	adrp	x0, 0xfffffe000851c000 <_OUTLINED_FUNCTION_0+0x4>
fffffe000751e5f0:      	add	x0, x0, #2464
fffffe000751e5f4:      	adrp	x1, 0xfffffe0007c5b000 <_ncb_report+0x54>
fffffe000751e5f8:      	add	x1, x1, #1624
fffffe000751e5fc:      	mov	x2, x19
fffffe000751e600:      	adrp	x3, 0xfffffe00084ed000 <_nis_id_write+0x24>
fffffe000751e604:      	add	x3, x3, #3320
fffffe000751e608:      	movk	w5, #213, lsl #16
fffffe000751e60c:      	bl	0xfffffe000799d288 <_ncr_continue_prepare>
fffffe000751e610:      	cbz	w0, 0xfffffe000751f688 <_nss_worker_continue+0x26b0>
fffffe000751e614:      	adrp	x20, 0xfffffe00084eb000 <_nis_id_write+0x30>
fffffe000751e618:      	add	x20, x20, #2776
fffffe000751e61c:      	mov	x0, x19
fffffe000751e620:      	mov	x1, x20
fffffe000751e624:      	mov	w2, #8736
fffffe000751e628:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751e62c:      	mov	w8, #8736
fffffe000751e630:      	mov	w2, #24528
fffffe000751e634:      	add	x1, x20, x8
fffffe000751e638:      	adrp	x20, 0xfffffe000851c000 <_nbc_prepare+0x4>
fffffe000751e63c:      	add	x20, x20, #2464
fffffe000751e640:      	mov	x0, x20
fffffe000751e644:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751e648:      	mov	w26, #3
fffffe000751e64c:      	str	x19, [x20, #8]
fffffe000751e650:      	b	0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751e654:      	mov	w10, #1
fffffe000751e658:      	adrp	x8, 0xfffffe00084f4000 <_nis_delay+0x8>
fffffe000751e65c:      	mov	w9, #1
fffffe000751e660:      	str	w10, [x8, #576]
fffffe000751e664:      	str	w9, [sp, #472]
fffffe000751e668:      	adrp	x8, 0xfffffe00084f4000 <_nis_delay+0x18>
fffffe000751e66c:      	ldr	x0, [x24, #2456]
fffffe000751e670:      	ldr	w20, [x8, #596]
fffffe000751e674:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e678:      	cbz	w26, 0xfffffe000751e690 <_nss_worker_continue+0x16b8>
fffffe000751e67c:      	cbnz	w20, 0xfffffe000751e690 <_nss_worker_continue+0x16b8>
fffffe000751e680:      	adrp	x8, 0xfffffe00084f4000 <_nis_delay+0x30>
fffffe000751e684:      	mov	w1, wzr
fffffe000751e688:      	ldr	x0, [x8, #752]
fffffe000751e68c:      	bl	0xfffffe0007aee934 <_nord2_wifi_ifnet_link>
fffffe000751e690:      	adrp	x8, 0xfffffe00084f4000 <_OUTLINED_FUNCTION_1>
fffffe000751e694:      	ldr	x0, [x8, #544]
fffffe000751e698:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e69c:      	cbz	w21, 0xfffffe000751e6f8 <_nss_worker_continue+0x1720>
fffffe000751e6a0:      	ldr	x0, [x24, #2456]
fffffe000751e6a4:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e6a8:      	adrp	x8, 0xfffffe00084f4000 <_OUTLINED_FUNCTION_2+0xc>
fffffe000751e6ac:      	ldr	w8, [x8, #616]
fffffe000751e6b0:      	cmp	w8, #111
fffffe000751e6b4:      	b.hi	0xfffffe000751e6f0 <_nss_worker_continue+0x1718>
fffffe000751e6b8:      	add	w8, w8, #1
fffffe000751e6bc:      	ldr	x0, [x24, #2456]
fffffe000751e6c0:      	adrp	x20, 0xfffffe00084f4000 <_OUTLINED_FUNCTION_5>
fffffe000751e6c4:      	add	x20, x20, #616
fffffe000751e6c8:      	str	w8, [x20]
fffffe000751e6cc:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e6d0:      	ldr	x8, [x20, #136]
fffffe000751e6d4:      	adrp	x0, 0xfffffe000709a000 <_nss_worker_continue+0x4ec>
fffffe000751e6d8:      	add	x0, x0, #2249
fffffe000751e6dc:      	mov	w9, #1
fffffe000751e6e0:      	str	x8, [sp]
fffffe000751e6e4:      	stp	x9, x21, [sp, #8]
fffffe000751e6e8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751e6ec:      	b	0xfffffe000751e6f8 <_nss_worker_continue+0x1720>
fffffe000751e6f0:      	ldr	x0, [x24, #2456]
fffffe000751e6f4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e6f8:      	adrp	x8, 0xfffffe00084f4000 <_nbc_prepare+0x24>
fffffe000751e6fc:      	adrp	x21, 0xfffffe00084f4000 <_nbc_prepare+0x28>
fffffe000751e700:      	add	x21, x21, #544
fffffe000751e704:      	adrp	x22, 0xfffffe00084f4000 <_nbc_prepare+0x30>
fffffe000751e708:      	add	x22, x22, #560
fffffe000751e70c:      	ldr	w8, [x8, #612]
fffffe000751e710:      	cbz	w8, 0xfffffe000751e814 <_nss_worker_continue+0x183c>
fffffe000751e714:      	mov	x20, xzr
fffffe000751e718:      	adrp	x26, 0xfffffe000850b000 <_nbc_prepare+0xa0>
fffffe000751e71c:      	add	x26, x26, #1488
fffffe000751e720:      	b	0xfffffe000751e75c <_nss_worker_continue+0x1784>
fffffe000751e724:      	ldr	x0, [x24, #2456]
fffffe000751e728:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e72c:      	mov	x0, x26
fffffe000751e730:      	mov	w1, #1514
fffffe000751e734:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751e738:      	ldr	w8, [x21, #68]
fffffe000751e73c:      	add	x9, x21, x20, lsl #2
fffffe000751e740:      	add	x20, x20, #1
fffffe000751e744:      	add	x26, x26, #1514
fffffe000751e748:      	cmp	x20, x8
fffffe000751e74c:      	mov	w8, #53840
fffffe000751e750:      	movk	w8, #1, lsl #16
fffffe000751e754:      	str	wzr, [x9, x8]
fffffe000751e758:      	b.hs	0xfffffe000751e814 <_nss_worker_continue+0x183c>
fffffe000751e75c:      	ldr	x0, [x24, #2456]
fffffe000751e760:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e764:      	ldr	w8, [x22, #36]
fffffe000751e768:      	cbz	w8, 0xfffffe000751e724 <_nss_worker_continue+0x174c>
fffffe000751e76c:      	ldr	w8, [x22]
fffffe000751e770:      	cbnz	w8, 0xfffffe000751e724 <_nss_worker_continue+0x174c>
fffffe000751e774:      	ldr	w8, [x22, #4]
fffffe000751e778:      	cbnz	w8, 0xfffffe000751e724 <_nss_worker_continue+0x174c>
fffffe000751e77c:      	add	x9, x21, x20, lsl #3
fffffe000751e780:      	adrp	x8, 0xfffffe00084f4000 <_nbc_prepare+0xac>
fffffe000751e784:      	add	x8, x8, #544
fffffe000751e788:      	ldr	x0, [x24, #2456]
fffffe000751e78c:      	ldr	x22, [x9, #216]
fffffe000751e790:      	str	x9, [sp, #504]
fffffe000751e794:      	ldr	x21, [x8, #208]
fffffe000751e798:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e79c:      	cmp	x22, x21
fffffe000751e7a0:      	adrp	x22, 0xfffffe00084f4000 <_nbc_prepare+0xcc>
fffffe000751e7a4:      	add	x22, x22, #560
fffffe000751e7a8:      	adrp	x21, 0xfffffe00084f4000 <_nbc_prepare+0xd4>
fffffe000751e7ac:      	add	x21, x21, #544
fffffe000751e7b0:      	b.ne	0xfffffe000751e72c <_nss_worker_continue+0x1754>
fffffe000751e7b4:      	mov	w9, #53840
fffffe000751e7b8:      	add	x8, x21, x20, lsl #2
fffffe000751e7bc:      	movk	w9, #1, lsl #16
fffffe000751e7c0:      	mov	x1, x26
fffffe000751e7c4:      	ldr	w2, [x8, x9]
fffffe000751e7c8:      	ldr	x8, [sp, #504]
fffffe000751e7cc:      	ldr	x0, [x8, #216]
fffffe000751e7d0:      	bl	0xfffffe0007aeebe0 <_nord2_wifi_ifnet_receive>
fffffe000751e7d4:      	cbnz	w0, 0xfffffe000751e72c <_nss_worker_continue+0x1754>
fffffe000751e7d8:      	ldr	x0, [x24, #2456]
fffffe000751e7dc:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e7e0:      	adrp	x10, 0xfffffe00084f4000 <_nbc_prepare+0x10c>
fffffe000751e7e4:      	add	x10, x10, #680
fffffe000751e7e8:      	ldp	w9, w8, [x10]
fffffe000751e7ec:      	add	w9, w9, #1
fffffe000751e7f0:      	cmp	w8, #7
fffffe000751e7f4:      	str	w9, [x10]
fffffe000751e7f8:      	b.hi	0xfffffe000751e724 <_nss_worker_continue+0x174c>
fffffe000751e7fc:      	add	w8, w8, #1
fffffe000751e800:      	adrp	x9, 0xfffffe00084f4000 <_nbc_prepare+0x12c>
fffffe000751e804:      	mov	w0, #1
fffffe000751e808:      	str	w8, [x9, #684]
fffffe000751e80c:      	bl	0xfffffe00075129a8 <_njs_trace_locked>
fffffe000751e810:      	b	0xfffffe000751e724 <_nss_worker_continue+0x174c>
fffffe000751e814:      	adrp	x8, 0xfffffe00084f4000 <_nbc_prepare+0x140>
fffffe000751e818:      	str	wzr, [x8, #612]
fffffe000751e81c:      	ldr	w8, [sp, #496]
fffffe000751e820:      	cbz	w8, 0xfffffe000751e830 <_nss_worker_continue+0x1858>
fffffe000751e824:      	mov	w0, #4
fffffe000751e828:      	bl	0xfffffe000752138c <_njs_notice>
fffffe000751e82c:      	b	0xfffffe000751e848 <_nss_worker_continue+0x1870>
fffffe000751e830:      	ldr	w8, [sp, #472]
fffffe000751e834:      	ldr	w9, [sp, #456]
fffffe000751e838:      	cmp	w8, #0
fffffe000751e83c:      	cset	w8, eq
fffffe000751e840:      	orr	w8, w8, w9
fffffe000751e844:      	tbz	w8, #0, 0xfffffe000751e904 <_nss_worker_continue+0x192c>
fffffe000751e848:      	ldr	w8, [sp, #480]
fffffe000751e84c:      	cbz	w8, 0xfffffe000751e868 <_nss_worker_continue+0x1890>
fffffe000751e850:      	adrp	x8, 0xfffffe00084f4000 <_cap_receipt+0x1c>
fffffe000751e854:      	ldr	w8, [x8, #576]
fffffe000751e858:      	cmp	w8, #0
fffffe000751e85c:      	mov	w8, #2
fffffe000751e860:      	cinc	w0, w8, ne
fffffe000751e864:      	bl	0xfffffe000752138c <_njs_notice>
fffffe000751e868:      	ldr	w8, [sp, #488]
fffffe000751e86c:      	cmp	w8, #1
fffffe000751e870:      	b.hi	0xfffffe000751e8bc <_nss_worker_continue+0x18e4>
fffffe000751e874:      	mov	w0, #4
fffffe000751e878:      	bl	0xfffffe000752138c <_njs_notice>
fffffe000751e87c:      	adrp	x8, 0xfffffe0008507000 <_cap_receipt+0x94>
fffffe000751e880:      	ldr	w8, [x8, #788]
fffffe000751e884:      	cmp	w8, #21
fffffe000751e888:      	b.ne	0xfffffe000751d830 <_nss_worker_continue+0x858>
fffffe000751e88c:      	adrp	x8, 0xfffffe0008518000 <_cap_receipt+0xe8>
fffffe000751e890:      	ldr	x8, [x8, #1432]
fffffe000751e894:      	cmp	x8, x28
fffffe000751e898:      	b.ne	0xfffffe000751d830 <_nss_worker_continue+0x858>
fffffe000751e89c:      	ldr	x0, [x24, #2456]
fffffe000751e8a0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e8a4:      	adrp	x8, 0xfffffe00084f4000 <_cap_receipt+0x70>
fffffe000751e8a8:      	ldr	w8, [x8, #592]
fffffe000751e8ac:      	cbz	w8, 0xfffffe000751e938 <_nss_worker_continue+0x1960>
fffffe000751e8b0:      	ldr	x0, [x24, #2456]
fffffe000751e8b4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e8b8:      	b	0xfffffe000751d830 <_nss_worker_continue+0x858>
fffffe000751e8bc:      	add	x2, sp, #592
fffffe000751e8c0:      	mov	w0, #1000
fffffe000751e8c4:      	mov	w1, #1000
fffffe000751e8c8:      	str	xzr, [sp, #592]
fffffe000751e8cc:      	bl	0xfffffe0007337480 <_clock_interval_to_deadline>
fffffe000751e8d0:      	adrp	x20, 0xfffffe00084d9000 <_cap_receipt+0x30>
fffffe000751e8d4:      	add	x20, x20, #2456
fffffe000751e8d8:      	ldr	x0, [x20]
fffffe000751e8dc:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e8e0:      	ldr	x2, [sp, #592]
fffffe000751e8e4:      	mov	x0, x20
fffffe000751e8e8:      	mov	w1, wzr
fffffe000751e8ec:      	bl	0xfffffe000736ca44 <_assert_wait_deadline>
fffffe000751e8f0:      	ldr	x0, [x20]
fffffe000751e8f4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e8f8:      	mov	x0, xzr
fffffe000751e8fc:      	bl	0xfffffe000736d140 <_thread_block>
fffffe000751e900:      	b	0xfffffe000751d830 <_nss_worker_continue+0x858>
fffffe000751e904:      	ldr	x0, [x24, #2456]
fffffe000751e908:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e90c:      	adrp	x8, 0xfffffe00084f4000 <_cap_receipt+0xd8>
fffffe000751e910:      	add	x8, x8, #560
fffffe000751e914:      	ldr	x0, [x24, #2456]
fffffe000751e918:      	ldp	w22, w20, [x8]
fffffe000751e91c:      	ldr	w21, [x8, #36]
fffffe000751e920:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e924:      	cbz	w21, 0xfffffe000751e848 <_nss_worker_continue+0x1870>
fffffe000751e928:      	cbnz	w22, 0xfffffe000751e848 <_nss_worker_continue+0x1870>
fffffe000751e92c:      	cbnz	w20, 0xfffffe000751e848 <_nss_worker_continue+0x1870>
fffffe000751e930:      	mov	w0, #1
fffffe000751e934:      	b	0xfffffe000751e828 <_nss_worker_continue+0x1850>
fffffe000751e938:      	adrp	x8, 0xfffffe0008518000 <_nbc_lease_window+0x14>
fffffe000751e93c:      	add	x8, x8, #1444
fffffe000751e940:      	adrp	x11, 0xfffffe0007c98000 <_ncb_report+0x494>
fffffe000751e944:      	adrp	x12, 0xfffffe00084f4000 <_cap_receipt+0x110>
fffffe000751e948:      	add	x12, x12, #552
fffffe000751e94c:      	adrp	x13, 0xfffffe0007c5c000 <_ncb_report+0x3b0>
fffffe000751e950:      	add	x13, x13, #948
fffffe000751e954:      	ldur	d2, [x8, #4]
fffffe000751e958:      	add	x11, x11, #648
fffffe000751e95c:      	add	x10, x12, #20
fffffe000751e960:      	ldr	d1, [x12]
fffffe000751e964:      	add	x9, x13, #20
fffffe000751e968:      	ldur	d0, [x13, #12]
fffffe000751e96c:      	add	x8, x11, #300
fffffe000751e970:      	ldr	s4, [x11, #60]
fffffe000751e974:      	ld1.s	{ v1 }[2], [x10]
fffffe000751e978:      	add	x10, x12, #3272
fffffe000751e97c:      	ld1.s	{ v0 }[2], [x9]
fffffe000751e980:      	add	x9, x11, #16
fffffe000751e984:      	ldr	d3, [x8]
fffffe000751e988:      	add	x8, x11, #280
fffffe000751e98c:      	ld1.s	{ v2 }[2], [x13]
fffffe000751e990:      	add	x11, x13, #8
fffffe000751e994:      	ldr	q5, [sp, #240]
fffffe000751e998:      	ld1.s	{ v4 }[1], [x8]
fffffe000751e99c:      	add	x8, x12, #28
fffffe000751e9a0:      	ld1.s	{ v0 }[3], [x9]
fffffe000751e9a4:      	add	x9, x12, #140
fffffe000751e9a8:      	ld1.s	{ v2 }[3], [x11]
fffffe000751e9ac:      	ld1.s	{ v1 }[3], [x8]
fffffe000751e9b0:      	mov	w8, #17308
fffffe000751e9b4:      	mov.d	v4[1], v3[0]
fffffe000751e9b8:      	movk	w8, #1, lsl #16
fffffe000751e9bc:      	ldr	x11, [sp, #320]
fffffe000751e9c0:      	ldr	d18, [x12, #56]
fffffe000751e9c4:      	ldr	d6, [x12, x8]
fffffe000751e9c8:      	add	x8, x12, #3264
fffffe000751e9cc:      	cmtst.4s	v3, v0, v0
fffffe000751e9d0:      	ldr	s16, [x11, #4096]
fffffe000751e9d4:      	cmeq.4s	v2, v2, v5
fffffe000751e9d8:      	ldr	d17, [x12, #32]
fffffe000751e9dc:      	cmtst.4s	v5, v4, v4
fffffe000751e9e0:      	ld1.s	{ v18 }[2], [x8]
fffffe000751e9e4:      	cmtst.4s	v7, v1, v1
fffffe000751e9e8:      	ldur	d19, [x12, #44]
fffffe000751e9ec:      	mvn.16b	v2, v2
fffffe000751e9f0:      	ld1.s	{ v17 }[1], [x9]
fffffe000751e9f4:      	uzp1.4s	v16, v16, v6
fffffe000751e9f8:      	add	x8, x11, #4
fffffe000751e9fc:      	uzp1.8h	v2, v2, v3
fffffe000751ea00:      	ldr	x9, [sp, #184]
fffffe000751ea04:      	uzp1.8h	v3, v5, v7
fffffe000751ea08:      	ldr	q5, [x10]
fffffe000751ea0c:      	ldr	q7, [sp, #208]
fffffe000751ea10:      	mov.s	v16[1], v6[1]
fffffe000751ea14:      	mov.s	v18[3], v5[3]
fffffe000751ea18:      	ld1.s	{ v5 }[3], [x8]
fffffe000751ea1c:      	mov.d	v17[1], v19[0]
fffffe000751ea20:      	cmeq.4s	v4, v4, #0
fffffe000751ea24:      	cmeq.4s	v1, v1, #0
fffffe000751ea28:      	ld1.s	{ v16 }[3], [x9]
fffffe000751ea2c:      	cmeq.4s	v7, v18, v7
fffffe000751ea30:      	adrp	x9, 0xfffffe000851a000 <_nbc_lease_window+0x114>
fffffe000751ea34:      	add	x9, x9, #2480
fffffe000751ea38:      	cmtst.4s	v6, v17, v17
fffffe000751ea3c:      	uzp1.8h	v1, v4, v1
fffffe000751ea40:      	mvn.16b	v4, v7
fffffe000751ea44:      	ldr	q7, [sp, #192]
fffffe000751ea48:      	uzp1.8h	v4, v6, v4
fffffe000751ea4c:      	cmtst.4s	v6, v16, v16
fffffe000751ea50:      	cmeq.4s	v5, v5, v7
fffffe000751ea54:      	cmeq.4s	v0, v0, #0
fffffe000751ea58:      	mvn.16b	v5, v5
fffffe000751ea5c:      	uzp1.8h	v5, v5, v6
fffffe000751ea60:      	xtn2.8h	v0, v0
fffffe000751ea64:      	uzp1.16b	v2, v2, v3
fffffe000751ea68:      	uzp1.16b	v3, v0, v1
fffffe000751ea6c:      	cmeq.4s	v0, v17, #0
fffffe000751ea70:      	uzp1.16b	v1, v4, v5
fffffe000751ea74:      	ldr	q4, [sp, #224]
fffffe000751ea78:      	xtn.4h	v0, v0
fffffe000751ea7c:      	ldp	w8, w9, [x9]
fffffe000751ea80:      	xtn.8b	v0, v0
fffffe000751ea84:      	tbl.16b	v2, { v2, v3 }, v4
fffffe000751ea88:      	mov.b	v1[1], v0[1]
fffffe000751ea8c:      	cmp	w8, #0
fffffe000751ea90:      	adrp	x8, 0xfffffe00084d9000 <_nbc_lease_window+0x70>
fffffe000751ea94:      	cset	w10, ne
fffffe000751ea98:      	cmp	w9, #0
fffffe000751ea9c:      	cset	w9, ne
fffffe000751eaa0:      	orr.16b	v0, v2, v1
fffffe000751eaa4:      	ldr	w8, [x8, #2468]
fffffe000751eaa8:      	umaxv.16b	b0, v0
fffffe000751eaac:      	cmp	w8, #0
fffffe000751eab0:      	fmov	w11, s0
fffffe000751eab4:      	orr	w8, w11, w9
fffffe000751eab8:      	cset	w9, ne
fffffe000751eabc:      	orr	w8, w8, w10
fffffe000751eac0:      	orr	w8, w8, w9
fffffe000751eac4:      	tbnz	w8, #0, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eac8:      	adrp	x8, 0xfffffe000851a000 <_nbc_run+0x4c>
fffffe000751eacc:      	ldr	w8, [x8, #2012]
fffffe000751ead0:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ead4:      	adrp	x8, 0xfffffe0008518000 <_nbc_run+0x50>
fffffe000751ead8:      	nop
fffffe000751eadc:      	ldr	w8, [x8, #1444]
fffffe000751eae0:      	cbz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eae4:      	ldr	x8, [sp, #320]
fffffe000751eae8:      	ldr	w8, [x8, #4796]
fffffe000751eaec:      	cbz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eaf0:      	adrp	x8, 0xfffffe00084f4000 <_nbc_lease_window+0x13c>
fffffe000751eaf4:      	add	x8, x8, #552
fffffe000751eaf8:      	ldr	x8, [x8, #200]
fffffe000751eafc:      	cbz	x8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eb00:      	ldr	x9, [sp, #320]
fffffe000751eb04:      	ldr	x9, [x9, #1272]
fffffe000751eb08:      	cmp	x9, x8
fffffe000751eb0c:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eb10:      	ldr	x9, [sp, #320]
fffffe000751eb14:      	ldr	x9, [x9, #4776]
fffffe000751eb18:      	cbnz	x9, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eb1c:      	adrp	x9, 0xfffffe00084f4000 <_nbc_run+0x8>
fffffe000751eb20:      	add	x9, x9, #552
fffffe000751eb24:      	ldr	x9, [x9, #336]
fffffe000751eb28:      	cbnz	x9, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eb2c:      	adrp	x9, 0xfffffe00084f4000 <_nbc_run+0x18>
fffffe000751eb30:      	add	x9, x9, #552
fffffe000751eb34:      	ldr	x9, [x9, #344]
fffffe000751eb38:      	cbnz	x9, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eb3c:      	ldr	x9, [sp, #320]
fffffe000751eb40:      	adrp	x10, 0xfffffe0008511000 <_nbc_run+0xa0>
fffffe000751eb44:      	add	x10, x10, #1200
fffffe000751eb48:      	ldr	x9, [x9, #4800]
fffffe000751eb4c:      	cmp	x9, x10
fffffe000751eb50:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eb54:      	adrp	x0, 0xfffffe0008507000 <_nbc_run+0x8c>
fffffe000751eb58:      	add	x0, x0, #2112
fffffe000751eb5c:      	adrp	x17, 0xfffffe0008511000 <_nbc_run+0xbc>
fffffe000751eb60:      	add	x17, x17, #1200
fffffe000751eb64:      	add	x9, x17, #28
fffffe000751eb68:      	adrp	x14, 0xfffffe000851c000 <_nbc_run+0xf4>
fffffe000751eb6c:      	add	x14, x14, #2464
fffffe000751eb70:      	ldr	d0, [x0, #3536]
fffffe000751eb74:      	ldr	d1, [x17, #18512]
fffffe000751eb78:      	ldur	d2, [x17, #44]
fffffe000751eb7c:      	ld1.s	{ v0 }[2], [x9]
fffffe000751eb80:      	add	x9, x17, #36
fffffe000751eb84:      	ldr	w11, [x17, #148]
fffffe000751eb88:      	mov.d	v2[1], v1[0]
fffffe000751eb8c:      	ldr	q1, [sp, #160]
fffffe000751eb90:      	ld1.s	{ v0 }[3], [x9]
fffffe000751eb94:      	ldr	x9, [x17, #16]
fffffe000751eb98:      	cmeq.4s	v1, v2, v1
fffffe000751eb9c:      	cmeq.4s	v2, v0, #0
fffffe000751eba0:      	cmp	x9, x14
fffffe000751eba4:      	cmtst.4s	v0, v0, v0
fffffe000751eba8:      	cset	w9, eq
fffffe000751ebac:      	uzp1.8h	v1, v2, v1
fffffe000751ebb0:      	cmp	w11, #0
fffffe000751ebb4:      	xtn.4h	v0, v0
fffffe000751ebb8:      	uzp1.16b	v0, v1, v0
fffffe000751ebbc:      	tbl.8b	v0, { v0 }, v9
fffffe000751ebc0:      	umov.b	w10, v0[1]
fffffe000751ebc4:      	umov.b	w12, v0[0]
fffffe000751ebc8:      	umov.b	w13, v0[2]
fffffe000751ebcc:      	umov.b	w14, v0[3]
fffffe000751ebd0:      	umov.b	w15, v0[4]
fffffe000751ebd4:      	umov.b	w16, v0[5]
fffffe000751ebd8:      	and	w10, w10, #0x1
fffffe000751ebdc:      	and	w12, w12, #0x1
fffffe000751ebe0:      	and	w13, w13, #0x1
fffffe000751ebe4:      	and	w14, w14, #0x1
fffffe000751ebe8:      	and	w15, w15, #0x1
fffffe000751ebec:      	and	w11, w16, #0x1
fffffe000751ebf0:      	bfi	w12, w10, #1, #1
fffffe000751ebf4:      	umov.b	w10, v0[6]
fffffe000751ebf8:      	bfi	w12, w13, #2, #1
fffffe000751ebfc:      	umov.b	w13, v0[7]
fffffe000751ec00:      	bfi	w12, w14, #3, #1
fffffe000751ec04:      	ldr	x14, [x0]
fffffe000751ec08:      	bfi	w12, w15, #4, #1
fffffe000751ec0c:      	and	w10, w10, #0x1
fffffe000751ec10:      	bfi	w12, w11, #5, #1
fffffe000751ec14:      	add	x11, x0, #3536
fffffe000751ec18:      	orr	w10, w12, w10, lsl #6
fffffe000751ec1c:      	ldr	w12, [x17, #40]
fffffe000751ec20:      	ccmp	x14, x11, #0, eq
fffffe000751ec24:      	orr	w10, w10, w13, lsl #7
fffffe000751ec28:      	mov	w11, #255
fffffe000751ec2c:      	bic	w10, w11, w10
fffffe000751ec30:      	orr	w10, w10, w12
fffffe000751ec34:      	ccmp	w10, #0, #0, eq
fffffe000751ec38:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ec3c:      	adrp	x10, 0xfffffe0008511000 <_nbc_run+0x19c>
fffffe000751ec40:      	add	x10, x10, #1200
fffffe000751ec44:      	adrp	x11, 0xfffffe0007c5b000 <_ncb_sleep+0x74>
fffffe000751ec48:      	add	x11, x11, #1624
fffffe000751ec4c:      	ldr	x10, [x10]
fffffe000751ec50:      	cmp	x10, x11
fffffe000751ec54:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ec58:      	adrp	x10, 0xfffffe0008511000 <_nbc_run+0x1b8>
fffffe000751ec5c:      	add	x10, x10, #1200
fffffe000751ec60:      	ldr	x10, [x10, #8]
fffffe000751ec64:      	cmp	x10, x19
fffffe000751ec68:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ec6c:      	tbz	w9, #0, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ec70:      	adrp	x9, 0xfffffe0008507000 <_nbc_run+0x1a8>
fffffe000751ec74:      	add	x9, x9, #2112
fffffe000751ec78:      	ldr	x9, [x9, #3544]
fffffe000751ec7c:      	cbnz	x9, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ec80:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x1e0>
fffffe000751ec84:      	add	x9, x9, #1200
fffffe000751ec88:      	ldr	x9, [x9, #152]
fffffe000751ec8c:      	cmp	x9, x8
fffffe000751ec90:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ec94:      	mov	x0, x19
fffffe000751ec98:      	bl	0xfffffe000799afb0 <_nct_done_receipt>
fffffe000751ec9c:      	cbz	w0, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eca0:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x200>
fffffe000751eca4:      	adrp	x10, 0xfffffe0008516000 <_nbc_run+0x218>
fffffe000751eca8:      	add	x10, x10, #0
fffffe000751ecac:      	ldr	x8, [x8, #1208]
fffffe000751ecb0:      	ldr	x9, [x8]
fffffe000751ecb4:      	cmp	x9, x10
fffffe000751ecb8:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ecbc:      	ldr	x9, [x8, #8]
fffffe000751ecc0:      	adrp	x10, 0xfffffe0008517000 <_nbc_run+0x238>
fffffe000751ecc4:      	add	x10, x10, #376
fffffe000751ecc8:      	cmp	x9, x10
fffffe000751eccc:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ecd0:      	ldr	x9, [x8, #16]
fffffe000751ecd4:      	adrp	x10, 0xfffffe0007c5b000 <_nct_native_run+0x60>
fffffe000751ecd8:      	add	x10, x10, #1624
fffffe000751ecdc:      	cmp	x9, x10
fffffe000751ece0:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ece4:      	adrp	x10, 0xfffffe0007c98000 <_nct_native_report+0x34>
fffffe000751ece8:      	ldr	x9, [x8, #24]
fffffe000751ecec:      	add	x10, x10, #648
fffffe000751ecf0:      	cmp	x9, x10
fffffe000751ecf4:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ecf8:      	adrp	x10, 0xfffffe0008511000 <_nbc_run+0x258>
fffffe000751ecfc:      	add	x10, x10, #1224
fffffe000751ed00:      	ldr	w9, [x8, #132]
fffffe000751ed04:      	ldr	w8, [x8, #68]
fffffe000751ed08:      	ldr	w10, [x10, #64]
fffffe000751ed0c:      	add	w8, w8, w9
fffffe000751ed10:      	cmp	w10, w8
fffffe000751ed14:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ed18:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x278>
fffffe000751ed1c:      	nop
fffffe000751ed20:      	ldr	w8, [x8, #1224]
fffffe000751ed24:      	cbz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ed28:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x288>
fffffe000751ed2c:      	add	x8, x8, #1224
fffffe000751ed30:      	ldr	w8, [x8, #88]
fffffe000751ed34:      	cmp	w8, #23
fffffe000751ed38:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ed3c:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x29c>
fffffe000751ed40:      	add	x8, x8, #1224
fffffe000751ed44:      	ldr	w8, [x8, #104]
fffffe000751ed48:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ed4c:      	adrp	x11, 0xfffffe0008511000 <_nbc_run+0x2ac>
fffffe000751ed50:      	add	x11, x11, #1256
fffffe000751ed54:      	add	x8, x11, #1468
fffffe000751ed58:      	add	x9, x11, #1496
fffffe000751ed5c:      	ldr	d0, [x11]
fffffe000751ed60:      	ldr	d1, [x8]
fffffe000751ed64:      	add	x8, x11, #1464
fffffe000751ed68:      	ldr	w10, [x11, #1500]
fffffe000751ed6c:      	ld1.s	{ v0 }[2], [x9]
fffffe000751ed70:      	add	x9, x11, #1488
fffffe000751ed74:      	ld1.s	{ v1 }[2], [x8]
fffffe000751ed78:      	add	x8, x11, #576
fffffe000751ed7c:      	ld1.s	{ v0 }[3], [x9]
fffffe000751ed80:      	adrp	x9, 0xfffffe0007c5c000 <_nct_native_run+0x110>
fffffe000751ed84:      	ld1.s	{ v1 }[3], [x8]
fffffe000751ed88:      	ldr	w8, [x11, #60]
fffffe000751ed8c:      	ldrb	w9, [x9, #412]
fffffe000751ed90:      	cmtst.4s	v0, v0, v0
fffffe000751ed94:      	cmtst.4s	v1, v1, v1
fffffe000751ed98:      	sub	w9, w9, #1
fffffe000751ed9c:      	cmp	w8, w9, uxtb
fffffe000751eda0:      	ldr	w8, [x11, #80]
fffffe000751eda4:      	uzp1.8h	v0, v0, v1
fffffe000751eda8:      	cset	w9, eq
fffffe000751edac:      	cmp	w10, #3
fffffe000751edb0:      	cset	w10, eq
fffffe000751edb4:      	cmp	w8, #0
fffffe000751edb8:      	xtn.8b	v0, v0
fffffe000751edbc:      	umaxv.8b	b0, v0
fffffe000751edc0:      	fmov	w11, s0
fffffe000751edc4:      	bic	w8, w10, w11
fffffe000751edc8:      	cset	w10, ne
fffffe000751edcc:      	and	w8, w8, w9
fffffe000751edd0:      	and	w8, w8, w10
fffffe000751edd4:      	cmp	w8, #1
fffffe000751edd8:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eddc:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x33c>
fffffe000751ede0:      	add	x8, x8, #1256
fffffe000751ede4:      	ldr	w8, [x8, #720]
fffffe000751ede8:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751edec:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x34c>
fffffe000751edf0:      	add	x8, x8, #1256
fffffe000751edf4:      	ldrb	w8, [x8, #1529]
fffffe000751edf8:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751edfc:      	adrp	x0, 0xfffffe0008513000 <_nbc_run+0x364>
fffffe000751ee00:      	add	x0, x0, #3232
fffffe000751ee04:      	bl	0xfffffe00079ab2d0 <_nstx_idle>
fffffe000751ee08:      	cbz	w0, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ee0c:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x36c>
fffffe000751ee10:      	nop
fffffe000751ee14:      	ldr	w8, [x8, #1292]
fffffe000751ee18:      	cbz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ee1c:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x37c>
fffffe000751ee20:      	add	x9, x9, #1292
fffffe000751ee24:      	ldr	w9, [x9, #4]
fffffe000751ee28:      	cmp	w8, w9
fffffe000751ee2c:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ee30:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x390>
fffffe000751ee34:      	ldr	w9, [x9, #1288]
fffffe000751ee38:      	cmn	w8, w9
fffffe000751ee3c:      	b.hs	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ee40:      	adrp	x10, 0xfffffe0007c5b000 <_nct_native_report+0x9c>
fffffe000751ee44:      	ldr	w10, [x10, #3552]
fffffe000751ee48:      	cmn	w10, #1
fffffe000751ee4c:      	b.eq	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ee50:      	adrp	x11, 0xfffffe0008513000 <_nbc_run+0x3b8>
fffffe000751ee54:      	add	w8, w9, w8
fffffe000751ee58:      	ldr	w11, [x11, #3276]
fffffe000751ee5c:      	sub	w8, w8, w11
fffffe000751ee60:      	cmp	w10, w8
fffffe000751ee64:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ee68:      	adrp	x9, 0xfffffe0007c5b000 <_nct_native_report+0xc4>
fffffe000751ee6c:      	add	x9, x9, #3540
fffffe000751ee70:      	movi.2d	v1, #0000000000000000
fffffe000751ee74:      	ldr	w8, [x9, #956]
fffffe000751ee78:      	fmov	s0, w8
fffffe000751ee7c:      	ld1.s	{ v0 }[1], [x9], #4
fffffe000751ee80:      	ldr	d2, [x9]
fffffe000751ee84:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x3e4>
fffffe000751ee88:      	add	x9, x9, #1284
fffffe000751ee8c:      	mov.d	v0[1], v2[0]
fffffe000751ee90:      	ld1.s	{ v1 }[0], [x9]
fffffe000751ee94:      	cmeq.4s	v0, v0, v1
fffffe000751ee98:      	xtn.4h	v0, v0
fffffe000751ee9c:      	umov.h	w9, v0[1]
fffffe000751eea0:      	umov.h	w10, v0[2]
fffffe000751eea4:      	umov.h	w11, v0[0]
fffffe000751eea8:      	umov.h	w12, v0[3]
fffffe000751eeac:      	and	w9, w9, #0x1
fffffe000751eeb0:      	and	w10, w10, #0x1
fffffe000751eeb4:      	bfi	w11, w9, #1, #1
fffffe000751eeb8:      	mov	w9, #15
fffffe000751eebc:      	bfi	w11, w10, #2, #1
fffffe000751eec0:      	bfi	w11, w12, #3, #29
fffffe000751eec4:      	bics	wzr, w9, w11
fffffe000751eec8:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eecc:      	cmp	w8, #1023
fffffe000751eed0:      	b.hi	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eed4:      	adrp	x8, 0xfffffe0007c5b000 <_nct_native_report+0x130>
fffffe000751eed8:      	add	x8, x8, #1636
fffffe000751eedc:      	ldr	w8, [x8, #1872]
fffffe000751eee0:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eee4:      	adrp	x8, 0xfffffe0007c5b000 <_nct_native_report+0x140>
fffffe000751eee8:      	nop
fffffe000751eeec:      	ldr	w8, [x8, #1636]
fffffe000751eef0:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eef4:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x454>
fffffe000751eef8:      	ldr	w8, [x8, #1272]
fffffe000751eefc:      	cbnz	w8, 0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ef00:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x460>
fffffe000751ef04:      	add	x9, x9, #1264
fffffe000751ef08:      	ldp	w9, w8, [x9]
fffffe000751ef0c:      	cmp	w8, w9
fffffe000751ef10:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ef14:      	adrp	x10, 0xfffffe0008511000 <_nbc_run+0x474>
fffffe000751ef18:      	add	x10, x10, #1276
fffffe000751ef1c:      	ldp	w9, w10, [x10]
fffffe000751ef20:      	cmp	w9, w10
fffffe000751ef24:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ef28:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x488>
fffffe000751ef2c:      	ldr	x9, [x9, #1208]
fffffe000751ef30:      	ldr	w9, [x9, #108]
fffffe000751ef34:      	cmp	w8, w9
fffffe000751ef38:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ef3c:      	adrp	x9, 0xfffffe0008516000 <_nbc_run+0x4b0>
fffffe000751ef40:      	ldr	w9, [x9, #432]
fffffe000751ef44:      	sub	w9, w9, #1
fffffe000751ef48:      	cmp	w8, w9
fffffe000751ef4c:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ef50:      	adrp	x8, 0xfffffe0007c5c000 <_njs_cache_scan_locked+0x1c>
fffffe000751ef54:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x4b4>
fffffe000751ef58:      	ldr	w8, [x8, #404]
fffffe000751ef5c:      	ldr	w9, [x9, #1544]
fffffe000751ef60:      	cmp	w8, w9
fffffe000751ef64:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ef68:      	mov	x8, xzr
fffffe000751ef6c:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x4cc>
fffffe000751ef70:      	add	x9, x9, #1468
fffffe000751ef74:      	adrp	x10, 0xfffffe0007c5b000 <_njs_cache_scan_locked+0x3c>
fffffe000751ef78:      	add	x10, x10, #1624
fffffe000751ef7c:      	add	x10, x10, x8
fffffe000751ef80:      	ldur	x11, [x9, #-36]
fffffe000751ef84:      	ldr	x12, [x10, #216]
fffffe000751ef88:      	cmp	x11, x12
fffffe000751ef8c:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751ef90:      	ldur	x11, [x9, #-28]
fffffe000751ef94:      	ldr	x10, [x10, #224]
fffffe000751ef98:      	cmp	x11, x10
fffffe000751ef9c:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751efa0:      	adrp	x10, 0xfffffe0007c5b000 <_njs_cache_scan_locked+0x68>
fffffe000751efa4:      	add	x10, x10, #1624
fffffe000751efa8:      	add	x11, x10, x8
fffffe000751efac:      	ldur	w10, [x9, #-20]
fffffe000751efb0:      	ldr	w12, [x11, #232]
fffffe000751efb4:      	cmp	w10, w12
fffffe000751efb8:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751efbc:      	ldur	w10, [x9, #-16]
fffffe000751efc0:      	ldr	w11, [x11, #236]
fffffe000751efc4:      	cmp	w10, w11
fffffe000751efc8:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751efcc:      	adrp	x11, 0xfffffe0007c5b000 <_njs_cache_scan_locked+0x94>
fffffe000751efd0:      	add	x11, x11, #1624
fffffe000751efd4:      	add	x11, x11, x8
fffffe000751efd8:      	ldur	w12, [x9, #-12]
fffffe000751efdc:      	ldr	w13, [x11, #240]
fffffe000751efe0:      	cmp	w12, w13
fffffe000751efe4:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751efe8:      	ldur	w12, [x9, #-8]
fffffe000751efec:      	ldr	w11, [x11, #244]
fffffe000751eff0:      	cmp	w12, w11
fffffe000751eff4:      	b.ne	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751eff8:      	ldur	w11, [x9, #-4]
fffffe000751effc:      	cmp	w11, w10
fffffe000751f000:      	b.hs	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751f004:      	ldr	w10, [x9]
fffffe000751f008:      	cmp	w10, #2
fffffe000751f00c:      	b.hs	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751f010:      	add	x8, x8, #32
fffffe000751f014:      	add	x9, x9, #80
fffffe000751f018:      	cmp	x8, #160
fffffe000751f01c:      	b.ne	0xfffffe000751ef74 <_nss_worker_continue+0x1f9c>
fffffe000751f020:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x57c>
fffffe000751f024:      	add	x9, x9, #1236
fffffe000751f028:      	mov	w8, #1
fffffe000751f02c:      	adrp	x10, 0xfffffe000851a000 <_nbc_run+0x5ac>
fffffe000751f030:      	adrp	x0, 0xfffffe0008517000 <_nbc_run+0x5a4>
fffffe000751f034:      	add	x0, x0, #376
fffffe000751f038:      	ldp	w21, w22, [x9]
fffffe000751f03c:      	str	w8, [x10, #2480]
fffffe000751f040:      	str	wzr, [sp, #528]
fffffe000751f044:      	str	d8, [x9]
fffffe000751f048:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f04c:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f050:      	ldr	w8, [x25, #12]
fffffe000751f054:      	str	xzr, [sp, #592]
fffffe000751f058:      	str	xzr, [sp, #552]
fffffe000751f05c:      	cbz	w8, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f060:      	ldr	w8, [x25, #20]
fffffe000751f064:      	cbnz	w8, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f068:      	ldr	x8, [x25]
fffffe000751f06c:      	cmp	x8, x28
fffffe000751f070:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f074:      	add	x0, sp, #592
fffffe000751f078:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751f07c:      	ldr	x0, [sp, #592]
fffffe000751f080:      	add	x1, sp, #552
fffffe000751f084:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751f088:      	ldr	x8, [sp, #552]
fffffe000751f08c:      	mov	x9, #63439
fffffe000751f090:      	movk	x9, #58195, lsl #16
fffffe000751f094:      	movk	x9, #39845, lsl #32
fffffe000751f098:      	lsr	x8, x8, #3
fffffe000751f09c:      	movk	x9, #8388, lsl #48
fffffe000751f0a0:      	umulh	x8, x8, x9
fffffe000751f0a4:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x600>
fffffe000751f0a8:      	add	x9, x9, #1368
fffffe000751f0ac:      	lsr	x8, x8, #4
fffffe000751f0b0:      	ldr	x9, [x9]
fffffe000751f0b4:      	cmp	x8, x9
fffffe000751f0b8:      	b.lo	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f0bc:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x618>
fffffe000751f0c0:      	add	x9, x9, #1368
fffffe000751f0c4:      	ldr	x9, [x9, #8]
fffffe000751f0c8:      	cmp	x8, x9
fffffe000751f0cc:      	b.hs	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f0d0:      	adrp	x9, 0xfffffe0008511000 <_nbc_run+0x62c>
fffffe000751f0d4:      	adrp	x0, 0xfffffe0008517000 <_nbc_run+0x648>
fffffe000751f0d8:      	add	x0, x0, #376
fffffe000751f0dc:      	str	x8, [x9, #1368]
fffffe000751f0e0:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f0e4:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f0e8:      	adrp	x8, 0xfffffe0008515000 <_nbc_run+0x654>
fffffe000751f0ec:      	mov	w9, #3
fffffe000751f0f0:      	ldr	x8, [x8, #3616]
fffffe000751f0f4:      	ldr	w8, [x8, #240]
fffffe000751f0f8:      	dmb	oshld
fffffe000751f0fc:      	bics	wzr, w9, w8
fffffe000751f100:      	str	w8, [sp, #528]
fffffe000751f104:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f108:      	adrp	x0, 0xfffffe0008517000 <_rr+0x8>
fffffe000751f10c:      	add	x0, x0, #376
fffffe000751f110:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f114:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f118:      	adrp	x8, 0xfffffe0007c5c000 <_nss_deliver+0x108>
fffffe000751f11c:      	ldr	w8, [x8, #956]
fffffe000751f120:      	sub	w8, w8, #4
fffffe000751f124:      	cmp	w8, #3
fffffe000751f128:      	cset	w0, lo
fffffe000751f12c:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f130:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f134:      	adrp	x8, 0xfffffe000851a000 <_rr+0x40>
fffffe000751f138:      	ldr	w8, [x8, #2536]
fffffe000751f13c:      	cbz	w8, 0xfffffe000751f14c <_nss_worker_continue+0x2174>
fffffe000751f140:      	adrp	x8, 0xfffffe000851a000 <_rr+0x4c>
fffffe000751f144:      	ldr	w8, [x8, #2012]
fffffe000751f148:      	cbnz	w8, 0xfffffe000751f158 <_nss_worker_continue+0x2180>
fffffe000751f14c:      	adrp	x0, 0xfffffe0007c5b000 <_nss_deliver+0x138>
fffffe000751f150:      	add	x0, x0, #1624
fffffe000751f154:      	bl	0xfffffe000751648c <_nst_native_rx_active>
fffffe000751f158:      	adrp	x8, 0xfffffe00084d9000 <_nbc_run+0x5d4>
fffffe000751f15c:      	mov	w9, #16896
fffffe000751f160:      	movk	w9, #6146, lsl #16
fffffe000751f164:      	adrp	x10, 0xfffffe0007c5c000 <_nss_deliver+0x154>
fffffe000751f168:      	add	x10, x10, #1044
fffffe000751f16c:      	ldr	x8, [x8, #1664]
fffffe000751f170:      	ldr	w8, [x8, #512]
fffffe000751f174:      	dmb	oshld
fffffe000751f178:      	stp	w9, w8, [x10]
fffffe000751f17c:      	tst	w8, #0x300000
fffffe000751f180:      	str	w8, [sp, #528]
fffffe000751f184:      	mov	w8, #1
fffffe000751f188:      	str	w8, [x10, #8]
fffffe000751f18c:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f190:      	adrp	x0, 0xfffffe0008517000 <_rr+0x90>
fffffe000751f194:      	add	x0, x0, #376
fffffe000751f198:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f19c:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f1a0:      	adrp	x8, 0xfffffe0007c5c000 <_njs_early_finish+0x1c>
fffffe000751f1a4:      	ldr	w8, [x8, #956]
fffffe000751f1a8:      	sub	w8, w8, #4
fffffe000751f1ac:      	cmp	w8, #3
fffffe000751f1b0:      	cset	w0, lo
fffffe000751f1b4:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f1b8:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f1bc:      	adrp	x8, 0xfffffe000851a000 <_coherent+0x14>
fffffe000751f1c0:      	ldr	w8, [x8, #2536]
fffffe000751f1c4:      	cbz	w8, 0xfffffe000751f1d4 <_nss_worker_continue+0x21fc>
fffffe000751f1c8:      	adrp	x8, 0xfffffe000851a000 <_coherent+0x20>
fffffe000751f1cc:      	ldr	w8, [x8, #2012]
fffffe000751f1d0:      	cbnz	w8, 0xfffffe000751f1e0 <_nss_worker_continue+0x2208>
fffffe000751f1d4:      	adrp	x0, 0xfffffe0007c5b000 <_njs_early_finish+0x4c>
fffffe000751f1d8:      	add	x0, x0, #1624
fffffe000751f1dc:      	bl	0xfffffe000751648c <_nst_native_rx_active>
fffffe000751f1e0:      	adrp	x8, 0xfffffe00084d9000 <_nbc_run+0x65c>
fffffe000751f1e4:      	mov	w9, #16896
fffffe000751f1e8:      	movk	w9, #6146, lsl #16
fffffe000751f1ec:      	adrp	x10, 0xfffffe0007c5c000 <_njs_early_finish+0x68>
fffffe000751f1f0:      	add	x10, x10, #1044
fffffe000751f1f4:      	add	w9, w9, #1, lsl #12
fffffe000751f1f8:      	ldr	x8, [x8, #1664]
fffffe000751f1fc:      	ldr	w8, [x8, #4608]
fffffe000751f200:      	dmb	oshld
fffffe000751f204:      	str	w8, [sp, #528]
fffffe000751f208:      	tst	w8, #0x300000
fffffe000751f20c:      	stp	w9, w8, [x10]
fffffe000751f210:      	mov	w8, #1
fffffe000751f214:      	str	w8, [x10, #8]
fffffe000751f218:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f21c:      	adrp	x0, 0xfffffe0008517000 <_coherent+0x68>
fffffe000751f220:      	add	x0, x0, #376
fffffe000751f224:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f228:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f22c:      	mov	w8, #21256
fffffe000751f230:      	add	x2, sp, #528
fffffe000751f234:      	movk	w8, #6146, lsl #16
fffffe000751f238:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x10>
fffffe000751f23c:      	add	x0, x0, #1624
fffffe000751f240:      	add	w1, w8, #272
fffffe000751f244:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe000751f248:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f24c:      	adrp	x9, 0xfffffe0008511000 <_coherent+0x80>
fffffe000751f250:      	ldr	w8, [sp, #528]
fffffe000751f254:      	ldr	w9, [x9, #1284]
fffffe000751f258:      	cmp	w8, w9
fffffe000751f25c:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f260:      	adrp	x0, 0xfffffe0008517000 <_coherent+0xac>
fffffe000751f264:      	add	x0, x0, #376
fffffe000751f268:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f26c:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f270:      	mov	w8, #21256
fffffe000751f274:      	add	x2, sp, #528
fffffe000751f278:      	movk	w8, #6146, lsl #16
fffffe000751f27c:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x54>
fffffe000751f280:      	add	x0, x0, #1624
fffffe000751f284:      	add	w1, w8, #276
fffffe000751f288:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe000751f28c:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f290:      	adrp	x8, 0xfffffe0008511000 <_coherent+0xc4>
fffffe000751f294:      	ldr	w20, [sp, #528]
fffffe000751f298:      	ldr	w8, [x8, #1284]
fffffe000751f29c:      	cmp	w20, w8
fffffe000751f2a0:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f2a4:      	adrp	x8, 0xfffffe0007c5c000 <_njs_event+0x80>
fffffe000751f2a8:      	adrp	x0, 0xfffffe0008517000 <_coherent+0xf4>
fffffe000751f2ac:      	add	x0, x0, #376
fffffe000751f2b0:      	ldr	x26, [x8, #368]
fffffe000751f2b4:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f2b8:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f2bc:      	adrp	x8, 0xfffffe0007c5c000 <_njs_event+0x98>
fffffe000751f2c0:      	ldr	w8, [x8, #956]
fffffe000751f2c4:      	sub	w8, w8, #4
fffffe000751f2c8:      	cmp	w8, #3
fffffe000751f2cc:      	cset	w0, lo
fffffe000751f2d0:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f2d4:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f2d8:      	lsl	w8, w20, #4
fffffe000751f2dc:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0xb4>
fffffe000751f2e0:      	add	x0, x0, #1624
fffffe000751f2e4:      	add	x26, x26, x8
fffffe000751f2e8:      	mov	x1, x26
fffffe000751f2ec:      	mov	w2, #16
fffffe000751f2f0:      	mov	w3, wzr
fffffe000751f2f4:      	bl	0xfffffe00075165e0 <_nwfn_memory_range>
fffffe000751f2f8:      	cbnz	w0, 0xfffffe000751f318 <_nss_worker_continue+0x2340>
fffffe000751f2fc:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0xd4>
fffffe000751f300:      	add	x0, x0, #1624
fffffe000751f304:      	mov	x1, x26
fffffe000751f308:      	mov	w2, #16
fffffe000751f30c:      	mov	w3, #1
fffffe000751f310:      	bl	0xfffffe00075165e0 <_nwfn_memory_range>
fffffe000751f314:      	cbz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f318:      	dsb	sy
fffffe000751f31c:      	adrp	x0, 0xfffffe0008517000 <_sy+0x20>
fffffe000751f320:      	add	x0, x0, #376
fffffe000751f324:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f328:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f32c:      	adrp	x8, 0xfffffe0007c5c000 <_njs_event+0x108>
fffffe000751f330:      	ldr	w8, [x8, #956]
fffffe000751f334:      	sub	w8, w8, #4
fffffe000751f338:      	cmp	w8, #3
fffffe000751f33c:      	cset	w0, lo
fffffe000751f340:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f344:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f348:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x120>
fffffe000751f34c:      	add	x0, x0, #1624
fffffe000751f350:      	mov	x1, x26
fffffe000751f354:      	mov	w2, #16
fffffe000751f358:      	mov	w3, wzr
fffffe000751f35c:      	bl	0xfffffe00075165e0 <_nwfn_memory_range>
fffffe000751f360:      	cbz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f364:      	adrp	x9, 0xfffffe0007c5c000 <_njs_event+0x140>
fffffe000751f368:      	add	x9, x9, #592
fffffe000751f36c:      	ldr	x8, [x9, #312]
fffffe000751f370:      	ldr	x9, [x9]
fffffe000751f374:      	add	x8, x8, x26
fffffe000751f378:      	sub	x8, x8, x9
fffffe000751f37c:      	ldrb	wzr, [x8]
fffffe000751f380:      	ldrb	wzr, [x8, #1]
fffffe000751f384:      	ldrb	wzr, [x8, #2]
fffffe000751f388:      	ldrb	wzr, [x8, #3]
fffffe000751f38c:      	ldrb	wzr, [x8, #4]
fffffe000751f390:      	ldrb	wzr, [x8, #5]
fffffe000751f394:      	ldrb	wzr, [x8, #6]
fffffe000751f398:      	ldrsb	w9, [x8, #7]
fffffe000751f39c:      	ldrb	wzr, [x8, #8]
fffffe000751f3a0:      	ldrb	wzr, [x8, #9]
fffffe000751f3a4:      	ldrb	wzr, [x8, #10]
fffffe000751f3a8:      	ldrb	wzr, [x8, #11]
fffffe000751f3ac:      	ldrb	wzr, [x8, #12]
fffffe000751f3b0:      	ldrb	wzr, [x8, #13]
fffffe000751f3b4:      	ldrb	wzr, [x8, #14]
fffffe000751f3b8:      	ldrb	wzr, [x8, #15]
fffffe000751f3bc:      	tbz	w9, #31, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f3c0:      	mov	w8, #10
fffffe000751f3c4:      	adrp	x9, 0xfffffe0008511000 <_sy+0xb0>
fffffe000751f3c8:      	adrp	x0, 0xfffffe0008517000 <_mr+0x10>
fffffe000751f3cc:      	add	x0, x0, #376
fffffe000751f3d0:      	str	w8, [x9, #1240]
fffffe000751f3d4:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f3d8:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f3dc:      	mov	w1, #21256
fffffe000751f3e0:      	add	x2, sp, #528
fffffe000751f3e4:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x1bc>
fffffe000751f3e8:      	add	x0, x0, #1624
fffffe000751f3ec:      	movk	w1, #6146, lsl #16
fffffe000751f3f0:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe000751f3f4:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f3f8:      	adrp	x9, 0xfffffe0008513000 <_mr+0x30>
fffffe000751f3fc:      	ldr	w8, [sp, #528]
fffffe000751f400:      	ldr	w9, [x9, #3248]
fffffe000751f404:      	cmp	w8, w9
fffffe000751f408:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f40c:      	adrp	x0, 0xfffffe0008517000 <_mr+0x54>
fffffe000751f410:      	add	x0, x0, #376
fffffe000751f414:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f418:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f41c:      	mov	w8, #21256
fffffe000751f420:      	add	x2, sp, #528
fffffe000751f424:      	movk	w8, #6146, lsl #16
fffffe000751f428:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x200>
fffffe000751f42c:      	add	x0, x0, #1624
fffffe000751f430:      	add	w1, w8, #4
fffffe000751f434:      	bl	0xfffffe000750fe78 <_nwfn_read>
fffffe000751f438:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f43c:      	adrp	x8, 0xfffffe0008513000 <_mr+0x74>
fffffe000751f440:      	ldr	w20, [sp, #528]
fffffe000751f444:      	ldr	w8, [x8, #3248]
fffffe000751f448:      	cmp	w20, w8
fffffe000751f44c:      	b.ne	0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f450:      	adrp	x8, 0xfffffe0008513000 <_mr+0x88>
fffffe000751f454:      	adrp	x0, 0xfffffe0008517000 <_mr+0x9c>
fffffe000751f458:      	add	x0, x0, #376
fffffe000751f45c:      	ldr	x26, [x8, #3240]
fffffe000751f460:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f464:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f468:      	adrp	x8, 0xfffffe0007c5c000 <_njs_event+0x244>
fffffe000751f46c:      	ldr	w8, [x8, #956]
fffffe000751f470:      	sub	w8, w8, #4
fffffe000751f474:      	cmp	w8, #3
fffffe000751f478:      	cset	w0, lo
fffffe000751f47c:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f480:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f484:      	lsl	w8, w20, #4
fffffe000751f488:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x260>
fffffe000751f48c:      	add	x0, x0, #1624
fffffe000751f490:      	add	x26, x26, x8
fffffe000751f494:      	mov	x1, x26
fffffe000751f498:      	mov	w2, #16
fffffe000751f49c:      	mov	w3, wzr
fffffe000751f4a0:      	bl	0xfffffe00075165e0 <_nwfn_memory_range>
fffffe000751f4a4:      	cbnz	w0, 0xfffffe000751f4c4 <_nss_worker_continue+0x24ec>
fffffe000751f4a8:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x280>
fffffe000751f4ac:      	add	x0, x0, #1624
fffffe000751f4b0:      	mov	x1, x26
fffffe000751f4b4:      	mov	w2, #16
fffffe000751f4b8:      	mov	w3, #1
fffffe000751f4bc:      	bl	0xfffffe00075165e0 <_nwfn_memory_range>
fffffe000751f4c0:      	cbz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f4c4:      	dsb	sy
fffffe000751f4c8:      	adrp	x0, 0xfffffe0008517000 <_mw+0x54>
fffffe000751f4cc:      	add	x0, x0, #376
fffffe000751f4d0:      	bl	0xfffffe0007516070 <_ncd_native_owner>
fffffe000751f4d4:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f4d8:      	adrp	x8, 0xfffffe0007c5c000 <_njs_event+0x2b4>
fffffe000751f4dc:      	ldr	w8, [x8, #956]
fffffe000751f4e0:      	sub	w8, w8, #4
fffffe000751f4e4:      	cmp	w8, #3
fffffe000751f4e8:      	cset	w0, lo
fffffe000751f4ec:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f4f0:      	cbnz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f4f4:      	adrp	x0, 0xfffffe0007c5b000 <_njs_event+0x2cc>
fffffe000751f4f8:      	add	x0, x0, #1624
fffffe000751f4fc:      	mov	x1, x26
fffffe000751f500:      	mov	w2, #16
fffffe000751f504:      	mov	w3, wzr
fffffe000751f508:      	bl	0xfffffe00075165e0 <_nwfn_memory_range>
fffffe000751f50c:      	cbz	w0, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f510:      	adrp	x9, 0xfffffe0007c5c000 <_njs_snapshot_locked+0x4>
fffffe000751f514:      	add	x9, x9, #592
fffffe000751f518:      	ldr	x8, [x9, #312]
fffffe000751f51c:      	ldr	x9, [x9]
fffffe000751f520:      	add	x8, x8, x26
fffffe000751f524:      	sub	x8, x8, x9
fffffe000751f528:      	ldrb	wzr, [x8]
fffffe000751f52c:      	ldrb	wzr, [x8, #1]
fffffe000751f530:      	ldrb	wzr, [x8, #2]
fffffe000751f534:      	ldrb	wzr, [x8, #3]
fffffe000751f538:      	ldrb	wzr, [x8, #4]
fffffe000751f53c:      	ldrb	wzr, [x8, #5]
fffffe000751f540:      	ldrb	wzr, [x8, #6]
fffffe000751f544:      	ldrsb	w9, [x8, #7]
fffffe000751f548:      	ldrb	wzr, [x8, #8]
fffffe000751f54c:      	ldrb	wzr, [x8, #9]
fffffe000751f550:      	ldrb	wzr, [x8, #10]
fffffe000751f554:      	ldrb	wzr, [x8, #11]
fffffe000751f558:      	ldrb	wzr, [x8, #12]
fffffe000751f55c:      	ldrb	wzr, [x8, #13]
fffffe000751f560:      	ldrb	wzr, [x8, #14]
fffffe000751f564:      	ldrb	wzr, [x8, #15]
fffffe000751f568:      	tbz	w9, #31, 0xfffffe000751ff7c <_nss_worker_continue+0x2fa4>
fffffe000751f56c:      	adrp	x15, 0xfffffe0008511000 <_rw+0x1c>
fffffe000751f570:      	add	x15, x15, #1236
fffffe000751f574:      	adrp	x10, 0xfffffe0008516000 <_rw+0x38>
fffffe000751f578:      	add	x10, x10, #0
fffffe000751f57c:      	adrp	x16, 0xfffffe000851a000 <_rw+0x50>
fffffe000751f580:      	add	x16, x16, #2016
fffffe000751f584:      	adrp	x8, 0xfffffe0008517000 <_rw+0x4c>
fffffe000751f588:      	add	x8, x8, #376
fffffe000751f58c:      	adrp	x13, 0xfffffe0007c5b000 <_njs_snapshot_locked+0x7c>
fffffe000751f590:      	add	x13, x13, #1624
fffffe000751f594:      	adrp	x9, 0xfffffe0007c98000 <_njs_snapshot_locked+0x178>
fffffe000751f598:      	stp	w21, w22, [x15]
fffffe000751f59c:      	stp	x10, x8, [x16]
fffffe000751f5a0:      	mov	w14, #1
fffffe000751f5a4:      	str	x13, [x16, #16]
fffffe000751f5a8:      	ldr	w8, [x13, #1928]
fffffe000751f5ac:      	add	x9, x9, #648
fffffe000751f5b0:      	adrp	x0, 0xfffffe000709a000 <_nss_worker_continue+0x13c4>
fffffe000751f5b4:      	add	x0, x0, #2302
fffffe000751f5b8:      	ldr	w12, [x15, #28]
fffffe000751f5bc:      	ldur	q0, [x15, #244]
fffffe000751f5c0:      	stp	w14, w8, [x16, #32]
fffffe000751f5c4:      	ldur	q1, [x15, #228]
fffffe000751f5c8:      	str	x9, [x16, #24]
fffffe000751f5cc:      	str	w12, [x16, #48]
fffffe000751f5d0:      	add	x12, x15, #196
fffffe000751f5d4:      	ldur	q2, [x15, #196]
fffffe000751f5d8:      	str	wzr, [x15, #1696]
fffffe000751f5dc:      	stp	q1, q0, [x16, #96]
fffffe000751f5e0:      	ldur	q3, [x15, #212]
fffffe000751f5e4:      	ldp	q0, q1, [x12, #64]
fffffe000751f5e8:      	str	w14, [x16, #468]
fffffe000751f5ec:      	stp	q2, q3, [x16, #64]
fffffe000751f5f0:      	ldp	q3, q2, [x12, #128]
fffffe000751f5f4:      	stp	q0, q1, [x16, #128]
fffffe000751f5f8:      	ldp	q1, q0, [x12, #96]
fffffe000751f5fc:      	stp	q3, q2, [x16, #192]
fffffe000751f600:      	ldp	q3, q2, [x12, #192]
fffffe000751f604:      	stp	q1, q0, [x16, #160]
fffffe000751f608:      	ldp	q0, q1, [x12, #160]
fffffe000751f60c:      	stp	q3, q2, [x16, #256]
fffffe000751f610:      	ldp	q2, q3, [x12, #224]
fffffe000751f614:      	stp	q0, q1, [x16, #224]
fffffe000751f618:      	ldp	q1, q0, [x12, #288]
fffffe000751f61c:      	stp	q2, q3, [x16, #288]
fffffe000751f620:      	ldp	q3, q2, [x12, #256]
fffffe000751f624:      	stp	q1, q0, [x16, #352]
fffffe000751f628:      	ldp	q1, q0, [x12, #368]
fffffe000751f62c:      	stp	q3, q2, [x16, #320]
fffffe000751f630:      	ldr	w9, [x13, #2872]
fffffe000751f634:      	stp	q1, q0, [x16, #432]
fffffe000751f638:      	ldr	w10, [x10, #432]
fffffe000751f63c:      	ldp	q3, q2, [x12, #336]
fffffe000751f640:      	stp	x8, x9, [sp, #8]
fffffe000751f644:      	stp	w9, w10, [x16, #40]
fffffe000751f648:      	ldr	q0, [x12, #320]
fffffe000751f64c:      	mov	w12, #18480
fffffe000751f650:      	ldrb	w11, [x13, #2884]
fffffe000751f654:      	stp	q3, q2, [x16, #400]
fffffe000751f658:      	ldur	x10, [x15, #116]
fffffe000751f65c:      	str	wzr, [x15, x12]
fffffe000751f660:      	ldr	w12, [x13, #2876]
fffffe000751f664:      	adrp	x13, 0xfffffe00084f4000 <_rw+0xa0>
fffffe000751f668:      	strb	w11, [x16, #52]
fffffe000751f66c:      	str	x10, [x16, #56]
fffffe000751f670:      	str	q0, [x16, #384]
fffffe000751f674:      	str	w14, [x13, #592]
fffffe000751f678:      	stp	x11, x12, [sp, #24]
fffffe000751f67c:      	str	x10, [sp]
fffffe000751f680:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751f684:      	b	0xfffffe000751e8b0 <_nss_worker_continue+0x18d8>
fffffe000751f688:      	mov	w8, #1
fffffe000751f68c:      	adrp	x9, 0xfffffe0008522000 <_drain+0xa8>
fffffe000751f690:      	add	x9, x9, #3032
fffffe000751f694:      	add	x2, sp, #552
fffffe000751f698:      	adrp	x0, 0xfffffe000721b000 <_nss_worker_continue+0x1ab0>
fffffe000751f69c:      	add	x0, x0, #1232
fffffe000751f6a0:      	str	w8, [x19, #8740]
fffffe000751f6a4:      	mov	w8, #5
fffffe000751f6a8:      	adrp	x1, 0xfffffe0008517000 <_drain+0x98>
fffffe000751f6ac:      	add	x1, x1, #376
fffffe000751f6b0:      	mov	x3, x19
fffffe000751f6b4:      	stlr	w8, [x9]
fffffe000751f6b8:      	adrp	x8, 0xfffffe000799d000 <_ncb_report+0x61c>
fffffe000751f6bc:      	add	x8, x8, #2324
fffffe000751f6c0:      	str	x8, [sp, #552]
fffffe000751f6c4:      	ldr	x8, [sp, #152]
fffffe000751f6c8:      	str	x8, [sp, #560]
fffffe000751f6cc:      	adr	x8, #4600
fffffe000751f6d0:      	nop
fffffe000751f6d4:      	str	x8, [sp, #568]
fffffe000751f6d8:      	adrp	x8, 0xfffffe000851a000 <_drain+0xd4>
fffffe000751f6dc:      	add	x8, x8, #2528
fffffe000751f6e0:      	str	x8, [sp, #576]
fffffe000751f6e4:      	bl	0xfffffe000799bc00 <_nct_run>
fffffe000751f6e8:      	cmp	w0, #1
fffffe000751f6ec:      	str	wzr, [x19, #8740]
fffffe000751f6f0:      	b.ne	0xfffffe000751f7a4 <_nss_worker_continue+0x27cc>
fffffe000751f6f4:      	ldr	w8, [x19, #4572]
fffffe000751f6f8:      	cbz	w8, 0xfffffe000751f7a4 <_nss_worker_continue+0x27cc>
fffffe000751f6fc:      	cbz	w20, 0xfffffe000751f778 <_nss_worker_continue+0x27a0>
fffffe000751f700:      	adrp	x9, 0xfffffe000851c000 <_drain+0x104>
fffffe000751f704:      	add	x9, x9, #2608
fffffe000751f708:      	adrp	x8, 0xfffffe000851a000 <_drain+0x104>
fffffe000751f70c:      	add	x8, x8, #2080
fffffe000751f710:      	ldp	q0, q1, [x9, #32]
fffffe000751f714:      	ldp	q3, q2, [x9]
fffffe000751f718:      	stp	q0, q1, [x8, #32]
fffffe000751f71c:      	ldp	q0, q1, [x9, #96]
fffffe000751f720:      	stp	q3, q2, [x8]
fffffe000751f724:      	ldp	q2, q3, [x9, #128]
fffffe000751f728:      	stp	q0, q1, [x8, #96]
fffffe000751f72c:      	ldp	q0, q1, [x9, #64]
fffffe000751f730:      	stp	q2, q3, [x8, #128]
fffffe000751f734:      	ldp	q3, q2, [x9, #192]
fffffe000751f738:      	stp	q0, q1, [x8, #64]
fffffe000751f73c:      	ldp	q0, q1, [x9, #160]
fffffe000751f740:      	stp	q3, q2, [x8, #192]
fffffe000751f744:      	ldp	q3, q2, [x9, #288]
fffffe000751f748:      	stp	q0, q1, [x8, #160]
fffffe000751f74c:      	ldp	q1, q0, [x9, #256]
fffffe000751f750:      	stp	q3, q2, [x8, #288]
fffffe000751f754:      	ldp	q2, q3, [x9, #224]
fffffe000751f758:      	stp	q1, q0, [x8, #256]
fffffe000751f75c:      	ldp	q1, q0, [x9, #368]
fffffe000751f760:      	stp	q2, q3, [x8, #224]
fffffe000751f764:      	stp	q1, q0, [x8, #368]
fffffe000751f768:      	ldr	q3, [x9, #320]
fffffe000751f76c:      	ldp	q0, q2, [x9, #336]
fffffe000751f770:      	str	q3, [x8, #320]
fffffe000751f774:      	stp	q0, q2, [x8, #336]
fffffe000751f778:      	mov	x0, x19
fffffe000751f77c:      	adrp	x1, 0xfffffe00084eb000 <_drain+0xbc>
fffffe000751f780:      	add	x1, x1, #2776
fffffe000751f784:      	mov	w2, #8736
fffffe000751f788:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751f78c:      	adrp	x8, 0xfffffe000851c000 <_drain+0x190>
fffffe000751f790:      	add	x8, x8, #2472
fffffe000751f794:      	mov	w26, #3
fffffe000751f798:      	str	x19, [x8]
fffffe000751f79c:      	str	xzr, [x8, #16]
fffffe000751f7a0:      	b	0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751f7a4:      	cbz	w20, 0xfffffe000751f7b4 <_nss_worker_continue+0x27dc>
fffffe000751f7a8:      	mov	w8, #1
fffffe000751f7ac:      	adrp	x9, 0xfffffe000851a000 <_drain+0x1a8>
fffffe000751f7b0:      	str	w8, [x9, #2488]
fffffe000751f7b4:      	adrp	x8, 0xfffffe00084d9000 <_drain+0xac>
fffffe000751f7b8:      	strb	w26, [x8, #2480]
fffffe000751f7bc:      	tbnz	w0, #31, 0xfffffe000751ff64 <_nss_worker_continue+0x2f8c>
fffffe000751f7c0:      	adrp	x8, 0xfffffe000851c000 <_drain+0x1c4>
fffffe000751f7c4:      	add	x8, x8, #2572
fffffe000751f7c8:      	ldr	w8, [x8, #1756]
fffffe000751f7cc:      	cbnz	w8, 0xfffffe000751f80c <_nss_worker_continue+0x2834>
fffffe000751f7d0:      	adrp	x8, 0xfffffe000851c000 <_drain+0x1d4>
fffffe000751f7d4:      	nop
fffffe000751f7d8:      	ldr	w8, [x8, #2572]
fffffe000751f7dc:      	cbz	w8, 0xfffffe000751f80c <_nss_worker_continue+0x2834>
fffffe000751f7e0:      	adrp	x8, 0xfffffe0008518000 <_drain+0x1d4>
fffffe000751f7e4:      	adrp	x10, 0xfffffe000851d000 <_drain+0x1ec>
fffffe000751f7e8:      	add	x10, x10, #228
fffffe000751f7ec:      	mov	w9, #1
fffffe000751f7f0:      	ldr	w8, [x8, #1556]
fffffe000751f7f4:      	str	wzr, [x10]
fffffe000751f7f8:      	str	w9, [x10, #12]
fffffe000751f7fc:      	cbz	w8, 0xfffffe000751f80c <_nss_worker_continue+0x2834>
fffffe000751f800:      	mov	w8, #1
fffffe000751f804:      	adrp	x9, 0xfffffe000851d000 <_drain+0x20c>
fffffe000751f808:      	str	w8, [x9, #236]
fffffe000751f80c:      	ldr	w8, [x25, #12]
fffffe000751f810:      	str	xzr, [sp, #528]
fffffe000751f814:      	str	xzr, [sp, #584]
fffffe000751f818:      	cbz	w8, 0xfffffe000751ff6c <_nss_worker_continue+0x2f94>
fffffe000751f81c:      	ldr	w8, [x25, #20]
fffffe000751f820:      	cbnz	w8, 0xfffffe000751ff6c <_nss_worker_continue+0x2f94>
fffffe000751f824:      	ldr	x8, [x25]
fffffe000751f828:      	cmp	x8, x28
fffffe000751f82c:      	b.ne	0xfffffe000751ff6c <_nss_worker_continue+0x2f94>
fffffe000751f830:      	add	x0, sp, #528
fffffe000751f834:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751f838:      	ldr	x0, [sp, #528]
fffffe000751f83c:      	add	x1, sp, #584
fffffe000751f840:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751f844:      	ldr	x8, [sp, #584]
fffffe000751f848:      	mov	x9, #63439
fffffe000751f84c:      	movk	x9, #58195, lsl #16
fffffe000751f850:      	movk	x9, #39845, lsl #32
fffffe000751f854:      	lsr	x8, x8, #3
fffffe000751f858:      	movk	x9, #8388, lsl #48
fffffe000751f85c:      	umulh	x8, x8, x9
fffffe000751f860:      	adrp	x9, 0xfffffe000851d000 <_drain+0x268>
fffffe000751f864:      	lsr	x20, x8, #4
fffffe000751f868:      	ldr	x9, [x9, #272]
fffffe000751f86c:      	cmp	x20, x9
fffffe000751f870:      	b.lo	0xfffffe000751ff6c <_nss_worker_continue+0x2f94>
fffffe000751f874:      	add	x0, x22, #144
fffffe000751f878:      	adrp	x1, 0xfffffe000851d000 <_drain+0x280>
fffffe000751f87c:      	add	x1, x1, #224
fffffe000751f880:      	mov	w2, #18336
fffffe000751f884:      	bl	0xfffffe0007bad570 <_memmove>
fffffe000751f888:      	ldr	w8, [x22, #152]
fffffe000751f88c:      	str	x20, [x22, #32]
fffffe000751f890:      	cbz	w8, 0xfffffe000751f8a4 <_nss_worker_continue+0x28cc>
fffffe000751f894:      	ldr	w8, [x22, #160]
fffffe000751f898:      	cbnz	w8, 0xfffffe000751f8a4 <_nss_worker_continue+0x28cc>
fffffe000751f89c:      	ldr	w8, [x22, #156]
fffffe000751f8a0:      	cbz	w8, 0xfffffe000751f8ac <_nss_worker_continue+0x28d4>
fffffe000751f8a4:      	mov	w26, #1
fffffe000751f8a8:      	b	0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751f8ac:      	ldr	w8, [x22, #168]
fffffe000751f8b0:      	cmp	w8, #0
fffffe000751f8b4:      	cset	w26, ne
fffffe000751f8b8:      	b	0xfffffe000751d824 <_nss_worker_continue+0x84c>
fffffe000751f8bc:      	sub	w8, w8, #1
fffffe000751f8c0:      	mov	w10, #60
fffffe000751f8c4:      	adrp	x9, 0xfffffe0008517000 <_drain+0x2b4>
fffffe000751f8c8:      	add	x9, x9, #376
fffffe000751f8cc:      	umaddl	x8, w8, w10, x9
fffffe000751f8d0:      	add	x0, x9, #201
fffffe000751f8d4:      	add	x3, sp, #592
fffffe000751f8d8:      	ldr	x1, [x9, #4048]
fffffe000751f8dc:      	ldrb	w2, [x8, #4071]
fffffe000751f8e0:      	bl	0xfffffe000799f9cc <_ncs_cap_mac>
fffffe000751f8e4:      	cbnz	w0, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f8e8:      	ldrb	w8, [sp, #597]
fffffe000751f8ec:      	cmp	w8, #4
fffffe000751f8f0:      	b.ne	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f8f4:      	ldrb	w8, [sp, #593]
fffffe000751f8f8:      	cmp	w8, #7
fffffe000751f8fc:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f900:      	adrp	x8, 0xfffffe00084d9000 <_drain+0x1f8>
fffffe000751f904:      	ldr	w8, [x8, #2496]
fffffe000751f908:      	cbz	w8, 0xfffffe000751f954 <_nss_worker_continue+0x297c>
fffffe000751f90c:      	adrp	x11, 0xfffffe00084f4000 <_drain+0x270>
fffffe000751f910:      	add	x11, x11, #2120
fffffe000751f914:      	mov	w9, wzr
fffffe000751f918:      	adrp	x12, 0xfffffe00084d9000 <_drain+0x210>
fffffe000751f91c:      	add	x12, x12, #2501
fffffe000751f920:      	ldrb	w10, [x11]
fffffe000751f924:      	ldrb	w11, [x11, #1]
fffffe000751f928:      	b	0xfffffe000751f938 <_nss_worker_continue+0x2960>
fffffe000751f92c:      	add	x12, x12, #2
fffffe000751f930:      	subs	x8, x8, #1
fffffe000751f934:      	b.eq	0xfffffe000751f958 <_nss_worker_continue+0x2980>
fffffe000751f938:      	ldurb	w13, [x12, #-1]
fffffe000751f93c:      	cmp	w13, w10
fffffe000751f940:      	b.ne	0xfffffe000751f92c <_nss_worker_continue+0x2954>
fffffe000751f944:      	ldrb	w13, [x12]
fffffe000751f948:      	cmp	w13, w11
fffffe000751f94c:      	csinc	w9, w9, wzr, ne
fffffe000751f950:      	b	0xfffffe000751f92c <_nss_worker_continue+0x2954>
fffffe000751f954:      	mov	w9, wzr
fffffe000751f958:      	cbz	w9, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f95c:      	adrp	x8, 0xfffffe00084f4000 <_drain+0x2c0>
fffffe000751f960:      	add	x8, x8, #1060
fffffe000751f964:      	ldrb	w8, [x8]
fffffe000751f968:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f96c:      	adrp	x8, 0xfffffe00084f4000 <_drain+0x2d0>
fffffe000751f970:      	add	x8, x8, #1060
fffffe000751f974:      	ldr	w8, [x8, #24]
fffffe000751f978:      	cbnz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f97c:      	adrp	x9, 0xfffffe00084f4000 <_drain+0x2e0>
fffffe000751f980:      	add	x9, x9, #1053
fffffe000751f984:      	ldurh	w8, [x9, #25]
fffffe000751f988:      	ldurh	w9, [x9, #27]
fffffe000751f98c:      	cmp	w8, w9
fffffe000751f990:      	b.ne	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f994:      	adrp	x8, 0xfffffe00084f4000 <_drain+0x2f8>
fffffe000751f998:      	add	x8, x8, #1053
fffffe000751f99c:      	ldrb	w8, [x8]
fffffe000751f9a0:      	cbz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f9a4:      	adrp	x8, 0xfffffe00084f4000 <_drain+0x308>
fffffe000751f9a8:      	ldrb	w8, [x8, #1052]
fffffe000751f9ac:      	sub	w8, w8, #33
fffffe000751f9b0:      	cmn	w8, #32
fffffe000751f9b4:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f9b8:      	adrp	x8, 0xfffffe00084f4000 <_drain+0x31c>
fffffe000751f9bc:      	add	x8, x8, #1074
fffffe000751f9c0:      	ldrb	w1, [x8, #1048]
fffffe000751f9c4:      	sub	w8, w1, #13
fffffe000751f9c8:      	cmn	w8, #12
fffffe000751f9cc:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f9d0:      	adrp	x8, 0xfffffe00084f4000 <_drain+0x334>
fffffe000751f9d4:      	add	x8, x8, #1074
fffffe000751f9d8:      	ldrh	w8, [x8]
fffffe000751f9dc:      	cbz	w8, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751f9e0:      	add	x2, sp, #584
fffffe000751f9e4:      	add	x3, sp, #526
fffffe000751f9e8:      	add	x4, sp, #524
fffffe000751f9ec:      	adrp	x0, 0xfffffe00084f4000 <_drain+0x350>
fffffe000751f9f0:      	add	x0, x0, #2123
fffffe000751f9f4:      	strh	wzr, [sp, #584]
fffffe000751f9f8:      	str	wzr, [sp, #524]
fffffe000751f9fc:      	bl	0xfffffe00079af144 <_nst_legacy_rates>
fffffe000751fa00:      	cbnz	w0, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751fa04:      	adrp	x12, 0xfffffe00084f4000 <_drain+0x368>
fffffe000751fa08:      	add	x12, x12, #1008
fffffe000751fa0c:      	adrp	x11, 0xfffffe00084d9000 <_drain+0x304>
fffffe000751fa10:      	add	x11, x11, #2484
fffffe000751fa14:      	ldrb	w8, [sp, #597]
fffffe000751fa18:      	strb	wzr, [sp, #540]
fffffe000751fa1c:      	ldrb	w9, [x12, #1113]
fffffe000751fa20:      	ldr	w10, [x11]
fffffe000751fa24:      	strb	w8, [sp, #544]
fffffe000751fa28:      	ldrb	w8, [sp, #596]
fffffe000751fa2c:      	strb	w9, [sp, #546]
fffffe000751fa30:      	ldrh	w9, [x11, #4]
fffffe000751fa34:      	str	w10, [sp, #528]
fffffe000751fa38:      	ldrh	w10, [x12, #4]
fffffe000751fa3c:      	ldr	x11, [sp, #144]
fffffe000751fa40:      	strb	w8, [sp, #541]
fffffe000751fa44:      	strh	w9, [sp, #532]
fffffe000751fa48:      	ldrb	w9, [x12, #1112]
fffffe000751fa4c:      	strh	w10, [x11, #4]
fffffe000751fa50:      	ldr	w10, [x12]
fffffe000751fa54:      	mov	w12, #1280
fffffe000751fa58:      	cmp	w9, #1
fffffe000751fa5c:      	strb	w9, [sp, #545]
fffffe000751fa60:      	str	w10, [x11]
fffffe000751fa64:      	strh	w12, [sp, #542]
fffffe000751fa68:      	b.ne	0xfffffe000751fa94 <_nss_worker_continue+0x2abc>
fffffe000751fa6c:      	adrp	x10, 0xfffffe00084f4000 <_drain+0x3d0>
fffffe000751fa70:      	ldrh	w9, [sp, #584]
fffffe000751fa74:      	mov	w11, #17
fffffe000751fa78:      	ldrh	w10, [x10, #1076]
fffffe000751fa7c:      	tst	w9, #0x3fc0
fffffe000751fa80:      	and	w9, w10, #0x20
fffffe000751fa84:      	mov	w10, #1041
fffffe000751fa88:      	csel	w10, w11, w10, eq
fffffe000751fa8c:      	orr	w21, w10, w9
fffffe000751fa90:      	b	0xfffffe000751fa98 <_nss_worker_continue+0x2ac0>
fffffe000751fa94:      	mov	w21, #17
fffffe000751fa98:      	adrp	x9, 0xfffffe00084f4000 <_drain+0x3fc>
fffffe000751fa9c:      	add	x9, x9, #1062
fffffe000751faa0:      	ldrb	w9, [x9]
fffffe000751faa4:      	cbz	w9, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751faa8:      	adrp	x9, 0xfffffe00084f4000 <_drain+0x40c>
fffffe000751faac:      	ldrb	w9, [x9, #1061]
fffffe000751fab0:      	tbz	w9, #2, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751fab4:      	adrp	x9, 0xfffffe00084d9000 <_drain+0x3ac>
fffffe000751fab8:      	add	x9, x9, #2484
fffffe000751fabc:      	ldrh	w11, [x9, #4]
fffffe000751fac0:      	ldr	w9, [x9]
fffffe000751fac4:      	str	x9, [sp, #488]
fffffe000751fac8:      	adrp	x9, 0xfffffe00084f4000 <_drain+0x42c>
fffffe000751facc:      	add	x9, x9, #1008
fffffe000751fad0:      	ldrh	w10, [x9]
fffffe000751fad4:      	str	x10, [sp, #448]
fffffe000751fad8:      	ldur	w10, [x9, #2]
fffffe000751fadc:      	str	x10, [sp, #480]
fffffe000751fae0:      	ldrb	w10, [x9, #44]
fffffe000751fae4:      	ldrb	w9, [x9, #1114]
fffffe000751fae8:      	stp	x10, x11, [sp, #456]
fffffe000751faec:      	adrp	x11, 0xfffffe00084f4000 <_drain+0x450>
fffffe000751faf0:      	add	x11, x11, #1067
fffffe000751faf4:      	str	x9, [sp, #472]
fffffe000751faf8:      	adrp	x9, 0xfffffe00084f4000 <_drain+0x45c>
fffffe000751fafc:      	add	x9, x9, #1062
fffffe000751fb00:      	ldrb	w10, [x11]
fffffe000751fb04:      	ldrh	w9, [x9, #12]
fffffe000751fb08:      	cmp	w10, #255
fffffe000751fb0c:      	str	w9, [sp, #496]
fffffe000751fb10:      	ldrb	w9, [x11, #1]
fffffe000751fb14:      	b.ne	0xfffffe000751fb20 <_nss_worker_continue+0x2b48>
fffffe000751fb18:      	cmp	w9, #255
fffffe000751fb1c:      	b.eq	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751fb20:      	cmp	w10, #255
fffffe000751fb24:      	mov	w11, #220
fffffe000751fb28:      	csel	w10, wzr, w10, eq
fffffe000751fb2c:      	cmp	w9, #255
fffffe000751fb30:      	csel	w9, wzr, w9, eq
fffffe000751fb34:      	cmp	w10, w9
fffffe000751fb38:      	csel	w10, w10, w9, hi
fffffe000751fb3c:      	adrp	x9, 0xfffffe00084f4000 <_drain+0x4a0>
fffffe000751fb40:      	cmp	w10, #220
fffffe000751fb44:      	csel	w10, w10, w11, lo
fffffe000751fb48:      	ldrh	w9, [x9, #1078]
fffffe000751fb4c:      	str	w10, [sp, #76]
fffffe000751fb50:      	cbz	x9, 0xfffffe000751fbdc <_nss_worker_continue+0x2c04>
fffffe000751fb54:      	mov	x11, xzr
fffffe000751fb58:      	str	wzr, [sp, #504]
fffffe000751fb5c:      	b	0xfffffe000751fb6c <_nss_worker_continue+0x2b94>
fffffe000751fb60:      	add	x11, x11, x10
fffffe000751fb64:      	cmp	x11, x9
fffffe000751fb68:      	b.hs	0xfffffe000751fbe0 <_nss_worker_continue+0x2c08>
fffffe000751fb6c:      	sub	x10, x9, x11
fffffe000751fb70:      	subs	x12, x10, #2
fffffe000751fb74:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751fb78:      	adrp	x10, 0xfffffe00084f4000 <_drain+0x4dc>
fffffe000751fb7c:      	add	x10, x10, #544
fffffe000751fb80:      	add	x10, x11, x10
fffffe000751fb84:      	ldrb	w10, [x10, #553]
fffffe000751fb88:      	cmp	x12, x10
fffffe000751fb8c:      	b.lo	0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751fb90:      	adrp	x12, 0xfffffe00084f4000 <_drain+0x4f4>
fffffe000751fb94:      	add	x12, x12, #544
fffffe000751fb98:      	add	x12, x12, x11
fffffe000751fb9c:      	add	x11, x11, #2
fffffe000751fba0:      	ldrb	w12, [x12, #552]
fffffe000751fba4:      	cmp	w12, #42
fffffe000751fba8:      	b.ne	0xfffffe000751fb60 <_nss_worker_continue+0x2b88>
fffffe000751fbac:      	cmp	w10, #1
fffffe000751fbb0:      	b.ne	0xfffffe000751fb60 <_nss_worker_continue+0x2b88>
fffffe000751fbb4:      	adrp	x12, 0xfffffe00084f4000 <_drain+0x518>
fffffe000751fbb8:      	add	x12, x12, #544
fffffe000751fbbc:      	add	x12, x12, x11
fffffe000751fbc0:      	and	w13, w21, #0xffffffdf
fffffe000751fbc4:      	ldrb	w12, [x12, #552]
fffffe000751fbc8:      	tst	w12, #0x4
fffffe000751fbcc:      	ubfx	w12, w12, #1, #1
fffffe000751fbd0:      	csel	w21, w21, w13, eq
fffffe000751fbd4:      	str	w12, [sp, #504]
fffffe000751fbd8:      	b	0xfffffe000751fb60 <_nss_worker_continue+0x2b88>
fffffe000751fbdc:      	str	wzr, [sp, #504]
fffffe000751fbe0:      	ldr	x9, [sp, #136]
fffffe000751fbe4:      	strb	w8, [sp, #553]
fffffe000751fbe8:      	ldrh	w8, [sp, #524]
fffffe000751fbec:      	add	x4, sp, #592
fffffe000751fbf0:      	add	x5, sp, #528
fffffe000751fbf4:      	add	x6, sp, #552
fffffe000751fbf8:      	stp	xzr, xzr, [x9]
fffffe000751fbfc:      	mov	w9, #5
fffffe000751fc00:      	strh	w8, [sp, #558]
fffffe000751fc04:      	mov	w8, #8
fffffe000751fc08:      	adrp	x0, 0xfffffe0008511000 <_before+0xbc>
fffffe000751fc0c:      	add	x0, x0, #1200
fffffe000751fc10:      	strb	w9, [sp, #552]
fffffe000751fc14:      	mov	w9, #257
fffffe000751fc18:      	strh	w8, [sp, #560]
fffffe000751fc1c:      	mov	w8, #2000
fffffe000751fc20:      	adrp	x1, 0xfffffe0007c5b000 <_ncc_native_read+0x54>
fffffe000751fc24:      	add	x1, x1, #1624
fffffe000751fc28:      	strh	w9, [sp, #554]
fffffe000751fc2c:      	mov	w9, #30
fffffe000751fc30:      	mov	x2, x19
fffffe000751fc34:      	adrp	x3, 0xfffffe000851c000 <_before+0x114>
fffffe000751fc38:      	add	x3, x3, #2464
fffffe000751fc3c:      	mov	x7, x26
fffffe000751fc40:      	strb	w9, [sp, #556]
fffffe000751fc44:      	str	w8, [sp, #564]
fffffe000751fc48:      	stp	x22, x20, [sp]
fffffe000751fc4c:      	bl	0xfffffe00079ab784 <_nst_prepare>
fffffe000751fc50:      	cbnz	w0, 0xfffffe000751e02c <_nss_worker_continue+0x1054>
fffffe000751fc54:      	ldr	x10, [sp, #488]
fffffe000751fc58:      	adrp	x11, 0xfffffe00084f4000 <_before+0x98>
fffffe000751fc5c:      	add	x11, x11, #572
fffffe000751fc60:      	ldr	x9, [sp, #464]
fffffe000751fc64:      	add	x8, x11, #448
fffffe000751fc68:      	ldr	x16, [sp, #128]
fffffe000751fc6c:      	ldr	w12, [sp, #496]
fffffe000751fc70:      	mov	w20, #1
fffffe000751fc74:      	bfi	x10, x9, #32, #16
fffffe000751fc78:      	adrp	x9, 0xfffffe0008515000 <_before+0x13c>
fffffe000751fc7c:      	str	x8, [x16, #16]
fffffe000751fc80:      	add	x8, x11, #1551
fffffe000751fc84:      	ldr	w13, [sp, #76]
fffffe000751fc88:      	strh	w12, [x16, #56]
fffffe000751fc8c:      	str	d8, [x9, #3328]
fffffe000751fc90:      	ldr	x9, [sp, #448]
fffffe000751fc94:      	ldr	w15, [sp, #504]
fffffe000751fc98:      	adrp	x0, 0xfffffe000709a000 <_nss_worker_continue+0x1aac>
fffffe000751fc9c:      	add	x0, x0, #2140
fffffe000751fca0:      	ldrh	w11, [sp, #526]
fffffe000751fca4:      	bfi	x10, x9, #48, #16
fffffe000751fca8:      	ldr	x9, [sp, #456]
fffffe000751fcac:      	ldrh	w12, [sp, #524]
fffffe000751fcb0:      	strb	w13, [x16, #58]
fffffe000751fcb4:      	ldrb	w13, [sp, #541]
fffffe000751fcb8:      	strb	w15, [x16, #60]
fffffe000751fcbc:      	stp	x9, x8, [x16, #24]
fffffe000751fcc0:      	ldrb	w14, [sp, #543]
fffffe000751fcc4:      	ldp	x8, x9, [sp, #472]
fffffe000751fcc8:      	strh	w21, [x16, #48]
fffffe000751fccc:      	ldrb	w15, [sp, #544]
fffffe000751fcd0:      	str	wzr, [x16, #52]
fffffe000751fcd4:      	strb	w20, [x16, #59]
fffffe000751fcd8:      	str	x8, [x16, #40]
fffffe000751fcdc:      	ldrb	w8, [sp, #545]
fffffe000751fce0:      	stp	x10, x9, [x16]
fffffe000751fce4:      	mov	w10, #10
fffffe000751fce8:      	ldrb	w9, [sp, #546]
fffffe000751fcec:      	sturh	wzr, [x16, #61]
fffffe000751fcf0:      	strb	wzr, [x16, #63]
fffffe000751fcf4:      	strh	w10, [x16, #50]
fffffe000751fcf8:      	ldrh	w10, [sp, #584]
fffffe000751fcfc:      	stp	x14, x15, [sp, #56]
fffffe000751fd00:      	stp	x12, x13, [sp, #40]
fffffe000751fd04:      	stp	x10, x11, [sp, #24]
fffffe000751fd08:      	stp	x8, x9, [sp, #8]
fffffe000751fd0c:      	str	x26, [sp]
fffffe000751fd10:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751fd14:      	adrp	x8, 0xfffffe00084f4000 <_before+0x154>
fffffe000751fd18:      	nop
fffffe000751fd1c:      	add	x0, sp, #592
fffffe000751fd20:      	str	xzr, [sp, #592]
fffffe000751fd24:      	str	xzr, [sp, #552]
fffffe000751fd28:      	str	w20, [x8, #572]
fffffe000751fd2c:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751fd30:      	ldr	x0, [sp, #592]
fffffe000751fd34:      	add	x1, sp, #552
fffffe000751fd38:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751fd3c:      	ldr	x8, [sp, #552]
fffffe000751fd40:      	mov	x10, #63439
fffffe000751fd44:      	movk	x10, #58195, lsl #16
fffffe000751fd48:      	movk	x10, #39845, lsl #32
fffffe000751fd4c:      	lsr	x9, x8, #3
fffffe000751fd50:      	movk	x10, #8388, lsl #48
fffffe000751fd54:      	umulh	x9, x9, x10
fffffe000751fd58:      	lsr	x20, x9, #4
fffffe000751fd5c:      	cmp	x20, x22
fffffe000751fd60:      	b.lo	0xfffffe000751fefc <_nss_worker_continue+0x2f24>
fffffe000751fd64:      	adrp	x9, 0xfffffe00084f4000 <_nnr_plan+0x48>
fffffe000751fd68:      	add	x9, x9, #572
fffffe000751fd6c:      	ldur	x9, [x9, #148]
fffffe000751fd70:      	cmp	x20, x9
fffffe000751fd74:      	b.hs	0xfffffe000751fefc <_nss_worker_continue+0x2f24>
fffffe000751fd78:      	movi.2d	v2, #0000000000000000
fffffe000751fd7c:      	mov	x11, #63439
fffffe000751fd80:      	ldr	x10, [sp, #120]
fffffe000751fd84:      	movk	x11, #58195, lsl #16
fffffe000751fd88:      	adrp	x20, 0xfffffe00084f4000 <_nnr_plan+0x6c>
fffffe000751fd8c:      	add	x20, x20, #544
fffffe000751fd90:      	movk	x11, #39845, lsl #32
fffffe000751fd94:      	lsr	x9, x9, #3
fffffe000751fd98:      	movk	x11, #8388, lsl #48
fffffe000751fd9c:      	str	xzr, [x10, #144]
fffffe000751fda0:      	stp	q2, q2, [x10, #16]
fffffe000751fda4:      	ldr	x12, [sp, #96]
fffffe000751fda8:      	stp	q2, q2, [x10, #48]
fffffe000751fdac:      	umulh	x9, x9, x11
fffffe000751fdb0:      	stp	q2, q2, [x10, #80]
fffffe000751fdb4:      	adrp	x1, 0xfffffe0008511000 <_nnr_begin+0x6c>
fffffe000751fdb8:      	add	x1, x1, #1200
fffffe000751fdbc:      	stp	q2, q2, [x10, #112]
fffffe000751fdc0:      	str	q2, [x10]
fffffe000751fdc4:      	ldr	x10, [x20, #208]
fffffe000751fdc8:      	lsr	x9, x9, #4
fffffe000751fdcc:      	ldr	x11, [sp, #112]
fffffe000751fdd0:      	ldp	q0, q1, [x12]
fffffe000751fdd4:      	str	x10, [sp, #592]
fffffe000751fdd8:      	mov	w10, #30000
fffffe000751fddc:      	str	x9, [sp, #600]
fffffe000751fde0:      	add	x3, sp, #592
fffffe000751fde4:      	adr	x4, #5768
fffffe000751fde8:      	nop
fffffe000751fdec:      	str	w10, [sp, #608]
fffffe000751fdf0:      	mov	x5, x20
fffffe000751fdf4:      	add	x21, x20, #360
fffffe000751fdf8:      	ldr	x9, [x12, #48]
fffffe000751fdfc:      	stp	q0, q1, [x11]
fffffe000751fe00:      	ldrb	w10, [x1, #204]
fffffe000751fe04:      	ldr	q0, [x12, #32]
fffffe000751fe08:      	str	x9, [x11, #48]
fffffe000751fe0c:      	ldrh	w9, [x20, #1576]
fffffe000751fe10:      	strb	w10, [sp, #672]
fffffe000751fe14:      	mov	w10, #1
fffffe000751fe18:      	ldr	x12, [sp, #104]
fffffe000751fe1c:      	str	q0, [x11, #32]
fffffe000751fe20:      	sturh	w9, [x27, #83]
fffffe000751fe24:      	ldrb	w9, [x1, #1502]
fffffe000751fe28:      	strb	w10, [sp, #673]
fffffe000751fe2c:      	mov	w10, #4
fffffe000751fe30:      	mov	w11, #4000
fffffe000751fe34:      	strb	w9, [x12, #2]
fffffe000751fe38:      	ldrb	w9, [x1, #208]
fffffe000751fe3c:      	strb	w10, [sp, #677]
fffffe000751fe40:      	ldrh	w10, [x1, #1500]
fffffe000751fe44:      	str	w11, [sp, #680]
fffffe000751fe48:      	ldrb	w11, [x1, #207]
fffffe000751fe4c:      	strb	w9, [sp, #688]
fffffe000751fe50:      	add	x9, x20, #432
fffffe000751fe54:      	strh	w10, [x12]
fffffe000751fe58:      	add	x10, x20, #400
fffffe000751fe5c:      	strb	w11, [sp, #687]
fffffe000751fe60:      	add	x11, x20, #1594
fffffe000751fe64:      	ldrh	w12, [x20, #1592]
fffffe000751fe68:      	stp	x10, x9, [x27, #104]
fffffe000751fe6c:      	mov	x10, #13531
fffffe000751fe70:      	movk	x10, #55222, lsl #16
fffffe000751fe74:      	add	x9, x20, #552
fffffe000751fe78:      	movk	x10, #56962, lsl #32
fffffe000751fe7c:      	stp	x11, x12, [x27, #120]
fffffe000751fe80:      	movk	x10, #17179, lsl #48
fffffe000751fe84:      	adr	x11, #5560
fffffe000751fe88:      	nop
fffffe000751fe8c:      	str	x9, [x27, #136]
fffffe000751fe90:      	umulh	x8, x8, x10
fffffe000751fe94:      	ldrh	w10, [x20, #534]
fffffe000751fe98:      	ldp	x2, x0, [sp, #80]
fffffe000751fe9c:      	lsr	x6, x8, #18
fffffe000751fea0:      	stp	x10, x11, [x27, #144]
fffffe000751fea4:      	bl	0xfffffe00079b9584 <_nwn_begin>
fffffe000751fea8:      	mov	w26, w0
fffffe000751feac:      	add	x0, sp, #592
fffffe000751feb0:      	mov	w1, #168
fffffe000751feb4:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751feb8:      	mov	x0, x21
fffffe000751febc:      	mov	w1, #104
fffffe000751fec0:      	bl	0xfffffe00079a7348 <_niwj_clear>
fffffe000751fec4:      	cmp	w26, #1
fffffe000751fec8:      	cset	w8, eq
fffffe000751fecc:      	stp	wzr, w8, [x20, #8]
fffffe000751fed0:      	b.ne	0xfffffe000751ff20 <_nss_worker_continue+0x2f48>
fffffe000751fed4:      	bl	0xfffffe0007521200 <_njs_snapshot_locked>
fffffe000751fed8:      	ldr	x0, [x24, #2456]
fffffe000751fedc:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fee0:      	ldr	x0, [x24, #2456]
fffffe000751fee4:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751fee8:      	mov	w0, #1
fffffe000751feec:      	bl	0xfffffe00075129a8 <_njs_trace_locked>
fffffe000751fef0:      	ldr	x0, [x24, #2456]
fffffe000751fef4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fef8:      	b	0xfffffe000751e03c <_nss_worker_continue+0x1064>
fffffe000751fefc:      	ldr	x0, [x24, #2456]
fffffe000751ff00:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ff04:      	adrp	x8, 0xfffffe00084f4000 <_nbc_caps_decode+0x38>
fffffe000751ff08:      	mov	w9, #9
fffffe000751ff0c:      	ldr	x8, [x8, #720]
fffffe000751ff10:      	cmp	x20, x8
fffffe000751ff14:      	mov	w8, #2
fffffe000751ff18:      	csel	w0, w8, w9, lo
fffffe000751ff1c:      	b	0xfffffe000751e038 <_nss_worker_continue+0x1060>
fffffe000751ff20:      	ldr	x0, [x24, #2456]
fffffe000751ff24:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ff28:      	mov	w0, #2
fffffe000751ff2c:      	bl	0xfffffe0007520e78 <_njs_early_finish>
fffffe000751ff30:      	adrp	x9, 0xfffffe0008511000 <_nbc_caps_decode+0xd8>
fffffe000751ff34:      	add	x9, x9, #1252
fffffe000751ff38:      	ldr	w8, [x9]
fffffe000751ff3c:      	ldr	w9, [x9, #40]
fffffe000751ff40:      	orr	w8, w8, w9
fffffe000751ff44:      	cbz	w8, 0xfffffe000751e03c <_nss_worker_continue+0x1064>
fffffe000751ff48:      	b	0xfffffe000751ff60 <_nss_worker_continue+0x2f88>
fffffe000751ff4c:      	bl	0xfffffe0007a06354 <___stack_chk_fail>
fffffe000751ff50:      	bl	0xfffffe0007b52324 <_nss_worker_continue.cold.1>
fffffe000751ff54:      	b	0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
fffffe000751ff58:      	bl	0xfffffe0007b52340 <_nss_worker_continue.cold.2>
fffffe000751ff5c:      	bl	0xfffffe0007b5235c <_nss_worker_continue.cold.3>
fffffe000751ff60:      	bl	0xfffffe0007b524e8 <_nss_worker_continue.cold.4>
fffffe000751ff64:      	mov	x0, x22
fffffe000751ff68:      	bl	0xfffffe0007b5255c <_nss_worker_continue.cold.5>
fffffe000751ff6c:      	add	x0, sp, #552
fffffe000751ff70:      	add	x1, sp, #592
fffffe000751ff74:      	mov	x2, x22
fffffe000751ff78:      	bl	0xfffffe0007b52598 <_nss_worker_continue.cold.6>
fffffe000751ff7c:      	ldr	x0, [x24, #2456]
fffffe000751ff80:      	adrp	x8, 0xfffffe0008511000 <_nbc_caps_decode+0x128>
fffffe000751ff84:      	add	x8, x8, #1236
fffffe000751ff88:      	stp	w21, w22, [x8]
fffffe000751ff8c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ff90:      	b	0xfffffe000751ff5c <_nss_worker_continue+0x2f84>
