
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe000751cf58 <_nss_worker_continue>:
fffffe000751cf58:      	stp	d9, d8, [sp, #-112]!
fffffe000751cf5c:      	stp	x28, x27, [sp, #16]
fffffe000751cf60:      	stp	x26, x25, [sp, #32]
fffffe000751cf64:      	stp	x24, x23, [sp, #48]
fffffe000751cf68:      	stp	x22, x21, [sp, #64]
fffffe000751cf6c:      	stp	x20, x19, [sp, #80]
fffffe000751cf70:      	stp	x29, x30, [sp, #96]
fffffe000751cf74:      	add	x29, sp, #96
fffffe000751cf78:      	sub	sp, sp, #768
fffffe000751cf7c:      	adrp	x8, 0xfffffe0007c94000 <_nss_worker_continue+0x1e04>
fffffe000751cf80:      	adrp	x0, 0xfffffe0007099000 <_nppa_source_owner+0x240>
fffffe000751cf84:      	add	x0, x0, #3379
fffffe000751cf88:      	add	x1, sp, #536
fffffe000751cf8c:      	mov	w2, #4
fffffe000751cf90:      	nop
fffffe000751cf94:      	ldr	x8, [x8, #3376]
fffffe000751cf98:      	stur	x8, [x29, #-120]
fffffe000751cf9c:      	str	wzr, [sp, #536]
fffffe000751cfa0:      	bl	0xfffffe0007aed8dc <_PE_parse_boot_argn>
fffffe000751cfa4:      	ldr	w8, [sp, #536]
fffffe000751cfa8:      	cmp	w8, #1
fffffe000751cfac:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751cfb0:      	adrp	x24, 0xfffffe00084d5000 <_njs_event+0x278>
fffffe000751cfb4:      	add	x27, sp, #544
fffffe000751cfb8:      	movi.2d	v0, #0000000000000000
fffffe000751cfbc:      	str	wzr, [sp, #512]
fffffe000751cfc0:      	str	xzr, [sp, #504]
fffffe000751cfc4:      	ldr	x8, [x24, #2456]
fffffe000751cfc8:      	stur	wzr, [x27, #191]
fffffe000751cfcc:      	str	xzr, [sp, #480]
fffffe000751cfd0:      	stp	q0, q0, [x27, #64]
fffffe000751cfd4:      	stp	q0, q0, [x27, #96]
fffffe000751cfd8:      	stp	q0, q0, [x27, #128]
fffffe000751cfdc:      	stp	q0, q0, [x27, #160]
fffffe000751cfe0:      	stp	q0, q0, [sp, #544]
fffffe000751cfe4:      	stp	q0, q0, [sp, #576]
fffffe000751cfe8:      	cbz	x8, 0xfffffe000751d028 <_nss_worker_continue+0xd0>
fffffe000751cfec:      	adrp	x8, 0xfffffe0007c94000 <_nss_worker_continue+0x1e74>
fffffe000751cff0:      	nop
fffffe000751cff4:      	ldur	x9, [x29, #-120]
fffffe000751cff8:      	ldr	x8, [x8, #3376]
fffffe000751cffc:      	cmp	x8, x9
fffffe000751d000:      	b.ne	0xfffffe000751fc50 <_nss_worker_continue+0x2cf8>
fffffe000751d004:      	add	sp, sp, #768
fffffe000751d008:      	ldp	x29, x30, [sp, #96]
fffffe000751d00c:      	ldp	x20, x19, [sp, #80]
fffffe000751d010:      	ldp	x22, x21, [sp, #64]
fffffe000751d014:      	ldp	x24, x23, [sp, #48]
fffffe000751d018:      	ldp	x26, x25, [sp, #32]
fffffe000751d01c:      	ldp	x28, x27, [sp, #16]
fffffe000751d020:      	ldp	d9, d8, [sp], #112
fffffe000751d024:      	ret
fffffe000751d028:      	adrp	x25, 0xfffffe0008511000 <_njs_snapshot_locked+0xf4>
fffffe000751d02c:      	add	x25, x25, #3368
fffffe000751d030:      	ldr	x28, [x25]
fffffe000751d034:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe000751d038:      	adrp	x23, 0xfffffe0008514000 <_njs_snapshot_locked+0x110>
fffffe000751d03c:      	add	x23, x23, #376
fffffe000751d040:      	ldr	d0, [x25, #16]
fffffe000751d044:      	ldr	d1, [x23]
fffffe000751d048:      	mov.d	v0[1], v1[0]
fffffe000751d04c:      	cmtst.4s	v1, v0, v0
fffffe000751d050:      	cmeq.4s	v0, v0, #0
fffffe000751d054:      	xtn.4h	v0, v0
fffffe000751d058:      	xtn.4h	v1, v1
fffffe000751d05c:      	mov.h	v0[2], v1[2]
fffffe000751d060:      	umov.h	w8, v0[1]
fffffe000751d064:      	umov.h	w9, v0[2]
fffffe000751d068:      	umov.h	w10, v0[0]
fffffe000751d06c:      	umov.h	w11, v0[3]
fffffe000751d070:      	and	w8, w8, #0x1
fffffe000751d074:      	and	w9, w9, #0x1
fffffe000751d078:      	bfi	w10, w8, #1, #1
fffffe000751d07c:      	mov	w8, #15
fffffe000751d080:      	bfi	w10, w9, #2, #1
fffffe000751d084:      	bfi	w10, w11, #3, #29
fffffe000751d088:      	bics	wzr, w8, w10
fffffe000751d08c:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d090:      	ldr	w8, [x25, #12]
fffffe000751d094:      	cbz	w8, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d098:      	ldr	w8, [x23, #6164]
fffffe000751d09c:      	cbz	w8, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d0a0:      	adrp	x8, 0xfffffe0008513000 <_njs_snapshot_locked+0x174>
fffffe000751d0a4:      	add	x8, x8, #3944
fffffe000751d0a8:      	ldr	w9, [x8, #4]
fffffe000751d0ac:      	cbnz	w9, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d0b0:      	cmp	x28, x0
fffffe000751d0b4:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d0b8:      	ldr	w8, [x8]
fffffe000751d0bc:      	cbz	w8, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d0c0:      	adrp	x19, 0xfffffe0008511000 <_njs_notice>
fffffe000751d0c4:      	add	x19, x19, #3400
fffffe000751d0c8:      	mov	x0, x19
fffffe000751d0cc:      	bl	0xfffffe000799aca8 <_nct_done_receipt>
fffffe000751d0d0:      	ldr	d0, [x19, #48]
fffffe000751d0d4:      	add	x8, x19, #56
fffffe000751d0d8:      	adrp	x9, 0xfffffe000700e000 <_nppa_source_owner+0x168>
fffffe000751d0dc:      	cmp	w0, #0
fffffe000751d0e0:      	cset	w10, ne
fffffe000751d0e4:      	ld1.s	{ v0 }[2], [x8]
fffffe000751d0e8:      	add	x8, x19, #68
fffffe000751d0ec:      	ldr	q1, [x9, #832]
fffffe000751d0f0:      	ldr	w9, [x19, #72]
fffffe000751d0f4:      	ld1.s	{ v0 }[3], [x8]
fffffe000751d0f8:      	ldr	w8, [x19, #44]
fffffe000751d0fc:      	cmeq.4s	v0, v0, v1
fffffe000751d100:      	cmp	w8, #7
fffffe000751d104:      	cset	w8, eq
fffffe000751d108:      	cmp	w9, #26
fffffe000751d10c:      	mvn.16b	v0, v0
fffffe000751d110:      	cset	w9, eq
fffffe000751d114:      	xtn.4h	v0, v0
fffffe000751d118:      	umaxv.4h	h0, v0
fffffe000751d11c:      	fmov	w11, s0
fffffe000751d120:      	bic	w8, w8, w11
fffffe000751d124:      	and	w8, w8, w10
fffffe000751d128:      	and	w8, w8, w9
fffffe000751d12c:      	cmp	w8, #1
fffffe000751d130:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d134:      	ldr	w8, [x19, #64]
fffffe000751d138:      	cmp	w8, #26
fffffe000751d13c:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d140:      	ldr	x8, [x19]
fffffe000751d144:      	adrp	x9, 0xfffffe000850f000 <_njs_notice+0x7c>
fffffe000751d148:      	add	x9, x9, #1936
fffffe000751d14c:      	cmp	x8, x9
fffffe000751d150:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d154:      	ldr	x8, [x19, #8]
fffffe000751d158:      	adrp	x9, 0xfffffe0008510000 <_njs_notice+0x94>
fffffe000751d15c:      	add	x9, x9, #2312
fffffe000751d160:      	cmp	x8, x9
fffffe000751d164:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d168:      	ldr	x8, [x19, #16]
fffffe000751d16c:      	adrp	x9, 0xfffffe0007c57000 <_nss_worker_continue+0x1efc>
fffffe000751d170:      	add	x9, x9, #1624
fffffe000751d174:      	cmp	x8, x9
fffffe000751d178:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d17c:      	adrp	x15, 0xfffffe0007c94000 <_nss_worker_continue+0x2000>
fffffe000751d180:      	adrp	x8, 0xfffffe0008510000 <_njs_rng+0xc>
fffffe000751d184:      	add	x8, x8, #2336
fffffe000751d188:      	adrp	x11, 0xfffffe0007c57000 <_nss_worker_continue+0x1f18>
fffffe000751d18c:      	add	x11, x11, #1636
fffffe000751d190:      	adrp	x16, 0xfffffe0008516000 <_njs_peer+0x4>
fffffe000751d194:      	add	x16, x16, #328
fffffe000751d198:      	add	x15, x15, #584
fffffe000751d19c:      	ldur	d0, [x8, #4]
fffffe000751d1a0:      	ldr	s3, [x11, #1864]
fffffe000751d1a4:      	add	x10, x15, #16
fffffe000751d1a8:      	add	x9, x15, #300
fffffe000751d1ac:      	ldr	q2, [x11, #3408]
fffffe000751d1b0:      	ldur	d4, [x16, #12]
fffffe000751d1b4:      	ld1.s	{ v0 }[1], [x10]
fffffe000751d1b8:      	add	x10, x11, #3400
fffffe000751d1bc:      	ldr	d1, [x9]
fffffe000751d1c0:      	add	x9, x15, #60
fffffe000751d1c4:      	ld1.s	{ v3 }[1], [x10]
fffffe000751d1c8:      	adrp	x10, 0xfffffe000700e000 <_nppa_source_owner+0x258>
fffffe000751d1cc:      	ld1.s	{ v1 }[2], [x11]
fffffe000751d1d0:      	add	x11, x11, #32
fffffe000751d1d4:      	ld1.s	{ v0 }[2], [x9]
fffffe000751d1d8:      	add	x9, x15, #280
fffffe000751d1dc:      	mov.d	v3[1], v2[0]
fffffe000751d1e0:      	ld1.s	{ v1 }[3], [x11]
fffffe000751d1e4:      	ld1.s	{ v0 }[3], [x9]
fffffe000751d1e8:      	adrp	x9, 0xfffffe000700e000 <_nppa_source_owner+0x278>
fffffe000751d1ec:      	ext.16b	v2, v2, v4, #8
fffffe000751d1f0:      	ldr	q4, [x10, #848]
fffffe000751d1f4:      	cmeq.4s	v5, v1, #0
fffffe000751d1f8:      	cmeq.4s	v0, v0, #0
fffffe000751d1fc:      	cmeq.4s	v3, v3, v4
fffffe000751d200:      	cmeq.4s	v4, v2, #0
fffffe000751d204:      	cmtst.4s	v2, v2, v2
fffffe000751d208:      	uzp1.8h	v0, v0, v5
fffffe000751d20c:      	uzp1.8h	v4, v3, v4
fffffe000751d210:      	mvn.16b	v3, v3
fffffe000751d214:      	cmtst.4s	v1, v1, v1
fffffe000751d218:      	uzp1.8h	v2, v3, v2
fffffe000751d21c:      	uzp1.16b	v3, v0, v4
fffffe000751d220:      	ldr	q0, [x9, #864]
fffffe000751d224:      	xtn2.8h	v1, v1
fffffe000751d228:      	uzp1.16b	v4, v1, v2
fffffe000751d22c:      	tbl.16b	v0, { v3, v4 }, v0
fffffe000751d230:      	umov.b	w9, v0[1]
fffffe000751d234:      	umov.b	w11, v0[2]
fffffe000751d238:      	umov.b	w10, v0[0]
fffffe000751d23c:      	umov.b	w12, v0[3]
fffffe000751d240:      	umov.b	w13, v0[4]
fffffe000751d244:      	umov.b	w14, v0[5]
fffffe000751d248:      	and	w9, w9, #0x1
fffffe000751d24c:      	and	w11, w11, #0x1
fffffe000751d250:      	and	w10, w10, #0x1
fffffe000751d254:      	and	w12, w12, #0x1
fffffe000751d258:      	and	w13, w13, #0x1
fffffe000751d25c:      	and	w14, w14, #0x1
fffffe000751d260:      	bfi	w10, w9, #1, #1
fffffe000751d264:      	umov.b	w9, v0[6]
fffffe000751d268:      	bfi	w10, w11, #2, #1
fffffe000751d26c:      	umov.b	w11, v0[7]
fffffe000751d270:      	bfi	w10, w12, #3, #1
fffffe000751d274:      	umov.b	w12, v0[8]
fffffe000751d278:      	bfi	w10, w13, #4, #1
fffffe000751d27c:      	umov.b	w13, v0[9]
fffffe000751d280:      	and	w9, w9, #0x1
fffffe000751d284:      	bfi	w10, w14, #5, #1
fffffe000751d288:      	umov.b	w14, v0[10]
fffffe000751d28c:      	and	w11, w11, #0x1
fffffe000751d290:      	orr	w9, w10, w9, lsl #6
fffffe000751d294:      	umov.b	w10, v0[11]
fffffe000751d298:      	and	w12, w12, #0x1
fffffe000751d29c:      	orr	w9, w9, w11, lsl #7
fffffe000751d2a0:      	umov.b	w11, v0[12]
fffffe000751d2a4:      	and	w13, w13, #0x1
fffffe000751d2a8:      	orr	w9, w9, w12, lsl #8
fffffe000751d2ac:      	umov.b	w12, v0[13]
fffffe000751d2b0:      	and	w14, w14, #0x1
fffffe000751d2b4:      	orr	w9, w9, w13, lsl #9
fffffe000751d2b8:      	umov.b	w13, v0[14]
fffffe000751d2bc:      	and	w10, w10, #0x1
fffffe000751d2c0:      	orr	w9, w9, w14, lsl #10
fffffe000751d2c4:      	and	w11, w11, #0x1
fffffe000751d2c8:      	orr	w9, w9, w10, lsl #11
fffffe000751d2cc:      	and	w10, w12, #0x1
fffffe000751d2d0:      	umov.b	w12, v0[15]
fffffe000751d2d4:      	orr	w9, w9, w11, lsl #12
fffffe000751d2d8:      	and	w11, w13, #0x1
fffffe000751d2dc:      	orr	w9, w9, w10, lsl #13
fffffe000751d2e0:      	mov	w10, #65535
fffffe000751d2e4:      	orr	w9, w9, w11, lsl #14
fffffe000751d2e8:      	orr	w9, w9, w12, lsl #15
fffffe000751d2ec:      	bics	wzr, w10, w9
fffffe000751d2f0:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d2f4:      	add	x9, x16, #1832
fffffe000751d2f8:      	ldr	q0, [x9]
fffffe000751d2fc:      	cmtst.4s	v1, v0, v0
fffffe000751d300:      	cmeq.4s	v0, v0, #0
fffffe000751d304:      	xtn.4h	v1, v1
fffffe000751d308:      	xtn.4h	v0, v0
fffffe000751d30c:      	rev32.4h	v1, v1
fffffe000751d310:      	trn2.4h	v0, v1, v0
fffffe000751d314:      	umov.h	w9, v0[1]
fffffe000751d318:      	umov.h	w10, v0[2]
fffffe000751d31c:      	umov.h	w11, v0[0]
fffffe000751d320:      	umov.h	w12, v0[3]
fffffe000751d324:      	and	w9, w9, #0x1
fffffe000751d328:      	and	w10, w10, #0x1
fffffe000751d32c:      	bfi	w11, w9, #1, #1
fffffe000751d330:      	bfi	w11, w10, #2, #1
fffffe000751d334:      	bfi	w11, w12, #3, #29
fffffe000751d338:      	and	w9, w11, #0xf
fffffe000751d33c:      	cmp	w9, #15
fffffe000751d340:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d344:      	adrp	x9, 0xfffffe000850f000 <_ncc_cold_read+0x18>
fffffe000751d348:      	add	x9, x9, #1976
fffffe000751d34c:      	ldr	w8, [x8]
fffffe000751d350:      	ldp	w10, w9, [x9]
fffffe000751d354:      	orr	w9, w10, w9
fffffe000751d358:      	orr	w8, w9, w8
fffffe000751d35c:      	cbnz	w8, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d360:      	adrp	x8, 0xfffffe0008511000 <_ncc_cold_read+0x3c>
fffffe000751d364:      	ldr	x8, [x8, #3424]
fffffe000751d368:      	cmp	x8, x15
fffffe000751d36c:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d370:      	ldr	w8, [x16]
fffffe000751d374:      	cbnz	w8, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d378:      	ldrb	w8, [x16, #1897]
fffffe000751d37c:      	cmp	w8, #7
fffffe000751d380:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d384:      	adrp	x8, 0xfffffe0008516000 <_ncc_cold_read+0x74>
fffffe000751d388:      	add	x8, x8, #2192
fffffe000751d38c:      	ldrb	w9, [x8, #32]
fffffe000751d390:      	ldrb	w8, [x8]
fffffe000751d394:      	cmp	w9, w8
fffffe000751d398:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d39c:      	adrp	x8, 0xfffffe0008516000 <_ncc_cold_read+0x8c>
fffffe000751d3a0:      	ldr	w8, [x8, #2180]
fffffe000751d3a4:      	cmp	w8, #16
fffffe000751d3a8:      	b.hi	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d3ac:      	adrp	x9, 0xfffffe0008516000 <_ncc_cold_read+0x9c>
fffffe000751d3b0:      	ldr	w9, [x9, #2188]
fffffe000751d3b4:      	subs	w10, w9, w8
fffffe000751d3b8:      	b.lo	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d3bc:      	adrp	x9, 0xfffffe0008516000 <_ncc_cold_read+0xac>
fffffe000751d3c0:      	ldr	w9, [x9, #2184]
fffffe000751d3c4:      	cmp	w9, w10
fffffe000751d3c8:      	b.hi	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d3cc:      	adrp	x11, 0xfffffe0008516000 <_ncc_cold_read+0xbc>
fffffe000751d3d0:      	add	x11, x11, #2200
fffffe000751d3d4:      	ldp	x11, x10, [x11]
fffffe000751d3d8:      	cmp	x10, x11
fffffe000751d3dc:      	b.lo	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d3e0:      	adrp	x12, 0xfffffe0008516000 <_ncc_cold_read+0xd0>
fffffe000751d3e4:      	ldr	x12, [x12, #2216]
fffffe000751d3e8:      	cmp	x12, x11
fffffe000751d3ec:      	b.ls	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d3f0:      	cmp	x10, x12
fffffe000751d3f4:      	b.hs	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d3f8:      	adrp	x10, 0xfffffe0008516000 <_ncc_cold_read+0xe8>
fffffe000751d3fc:      	ldr	w10, [x10, #2176]
fffffe000751d400:      	cbz	w10, 0xfffffe000751d410 <_nss_worker_continue+0x4b8>
fffffe000751d404:      	cmp	w9, #0
fffffe000751d408:      	ccmp	w8, #16, #0, ne
fffffe000751d40c:      	b	0xfffffe000751d414 <_nss_worker_continue+0x4bc>
fffffe000751d410:      	cmp	w9, #0
fffffe000751d414:      	cset	w8, eq
fffffe000751d418:      	cbz	w8, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d41c:      	adrp	x8, 0xfffffe0007c57000 <_nss_worker_continue+0x21ac>
fffffe000751d420:      	ldr	w8, [x8, #3544]
fffffe000751d424:      	cmp	w8, #31
fffffe000751d428:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d42c:      	adrp	x26, 0xfffffe0008515000 <_ncc_cold_read+0x118>
fffffe000751d430:      	add	x26, x26, #2456
fffffe000751d434:      	add	x1, x26, #40
fffffe000751d438:      	add	x2, sp, #504
fffffe000751d43c:      	mov	x0, x26
fffffe000751d440:      	bl	0xfffffe00079a006c <_ncfg_address>
fffffe000751d444:      	cbnz	w0, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d448:      	ldr	w8, [sp, #512]
fffffe000751d44c:      	cmp	w8, #1
fffffe000751d450:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d454:      	add	x1, x26, #40
fffffe000751d458:      	add	x2, sp, #544
fffffe000751d45c:      	add	x4, sp, #480
fffffe000751d460:      	mov	x0, x26
fffffe000751d464:      	mov	w3, #39
fffffe000751d468:      	bl	0xfffffe00079a0fec <_ncfg_channels>
fffffe000751d46c:      	cbnz	w0, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d470:      	add	x8, sp, #251
fffffe000751d474:      	adrp	x11, 0xfffffe00084d5000 <_ncc_cold_read+0x60>
fffffe000751d478:      	add	x11, x11, #2484
fffffe000751d47c:      	ldrh	w10, [sp, #504]
fffffe000751d480:      	ldr	x9, [sp, #480]
fffffe000751d484:      	ldur	s0, [x8, #255]
fffffe000751d488:      	adrp	x8, 0xfffffe0008515000 <_ncc_native_check+0x44>
fffffe000751d48c:      	add	x8, x8, #2485
fffffe000751d490:      	strh	w10, [x11]
fffffe000751d494:      	ld1.s	{ v0 }[1], [x8]
fffffe000751d498:      	ldr	w8, [x11, #12]
fffffe000751d49c:      	stur	d0, [x11, #2]
fffffe000751d4a0:      	cbz	x9, 0xfffffe000751d508 <_nss_worker_continue+0x5b0>
fffffe000751d4a4:      	mov	x10, xzr
fffffe000751d4a8:      	add	x11, sp, #544
fffffe000751d4ac:      	adrp	x16, 0xfffffe00084d5000 <_ncc_cold_read+0x98>
fffffe000751d4b0:      	add	x16, x16, #2484
fffffe000751d4b4:      	b	0xfffffe000751d4c4 <_nss_worker_continue+0x56c>
fffffe000751d4b8:      	add	w10, w10, #1
fffffe000751d4bc:      	cmp	x9, x10
fffffe000751d4c0:      	b.ls	0xfffffe000751d508 <_nss_worker_continue+0x5b0>
fffffe000751d4c4:      	add	x12, x10, x10, lsl #2
fffffe000751d4c8:      	add	x12, x11, x12
fffffe000751d4cc:      	ldrb	w13, [x12, #2]
fffffe000751d4d0:      	cbz	w13, 0xfffffe000751d4b8 <_nss_worker_continue+0x560>
fffffe000751d4d4:      	ldrb	w13, [x12, #3]
fffffe000751d4d8:      	cbnz	w13, 0xfffffe000751d4b8 <_nss_worker_continue+0x560>
fffffe000751d4dc:      	add	x13, x10, x10, lsl #2
fffffe000751d4e0:      	add	w14, w8, #1
fffffe000751d4e4:      	add	x13, x11, x13
fffffe000751d4e8:      	add	x15, x16, w8, uxtw #1
fffffe000751d4ec:      	ldrb	w12, [x12]
fffffe000751d4f0:      	mov	w8, w14
fffffe000751d4f4:      	str	w14, [x16, #12]
fffffe000751d4f8:      	ldrb	w13, [x13, #1]
fffffe000751d4fc:      	strb	w12, [x15, #16]
fffffe000751d500:      	strb	w13, [x15, #17]
fffffe000751d504:      	b	0xfffffe000751d4b8 <_nss_worker_continue+0x560>
fffffe000751d508:      	cbz	w8, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d50c:      	adrp	x9, 0xfffffe0008515000 <_ncc_native_check+0xc8>
fffffe000751d510:      	ldr	w9, [x9, #2448]
fffffe000751d514:      	cmp	w8, w9
fffffe000751d518:      	b.ne	0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d51c:      	adrp	x8, 0xfffffe0008516000 <_ncc_native_check+0xdc>
fffffe000751d520:      	adrp	x20, 0xfffffe00084d5000 <_ncc_cold_read+0x10c>
fffffe000751d524:      	add	x20, x20, #2456
fffffe000751d528:      	ldrb	w8, [x8, #2192]
fffffe000751d52c:      	strb	w8, [x20, #24]
fffffe000751d530:      	bl	0xfffffe0007a180c4 <_IOLockAlloc>
fffffe000751d534:      	str	x0, [x20]
fffffe000751d538:      	cbz	x0, 0xfffffe000751cfec <_nss_worker_continue+0x94>
fffffe000751d53c:      	bl	0xfffffe0007a180c4 <_IOLockAlloc>
fffffe000751d540:      	adrp	x20, 0xfffffe00084f0000 <_ncc_native_check+0x68>
fffffe000751d544:      	add	x20, x20, #544
fffffe000751d548:      	str	x0, [x20]
fffffe000751d54c:      	adrp	x0, 0xfffffe0008516000 <_ncc_native_check+0x10c>
fffffe000751d550:      	add	x0, x0, #2160
fffffe000751d554:      	bl	0xfffffe0007520930 <_njs_cache_scan_locked>
fffffe000751d558:      	mov	w8, #1
fffffe000751d55c:      	adrp	x9, 0xfffffe00084d5000 <_ncc_native_check+0x18>
fffffe000751d560:      	add	x9, x9, #2464
fffffe000751d564:      	adrp	x0, 0xfffffe000709a000 <_nppa_xnu_word+0x8>
fffffe000751d568:      	add	x0, x0, #2344
fffffe000751d56c:      	str	w8, [x20, #3272]
fffffe000751d570:      	stlr	w8, [x9]
fffffe000751d574:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751d578:      	bl	0xfffffe0007aec4e0 <_nord2_wifi_ios_backend_ready>
fffffe000751d57c:      	add	x8, sp, #480
fffffe000751d580:      	add	x9, sp, #504
fffffe000751d584:      	orr	x11, x8, #0x6
fffffe000751d588:      	orr	x8, x9, #0x4
fffffe000751d58c:      	add	x10, sp, #544
fffffe000751d590:      	adrp	x12, 0xfffffe00084f0000 <_ncc_native_check+0xb8>
fffffe000751d594:      	add	x12, x12, #904
fffffe000751d598:      	adrp	x9, 0xfffffe00084f0000 <_ncc_native_check+0xc0>
fffffe000751d59c:      	add	x9, x9, #560
fffffe000751d5a0:      	stp	x8, x11, [sp, #104]
fffffe000751d5a4:      	add	x8, x10, #16
fffffe000751d5a8:      	adrp	x11, 0xfffffe00084f0000 <_ncc_native_check+0xd0>
fffffe000751d5ac:      	add	x11, x11, #552
fffffe000751d5b0:      	adrp	x13, 0xfffffe00084f0000 <_ncc_native_check+0xd8>
fffffe000751d5b4:      	add	x13, x13, #596
fffffe000751d5b8:      	add	x11, x11, #19, lsl #12
fffffe000751d5bc:      	str	x8, [sp, #88]
fffffe000751d5c0:      	add	x8, x10, #24
fffffe000751d5c4:      	adrp	x10, 0xfffffe00084f0000 <_ncc_native_check+0xec>
fffffe000751d5c8:      	add	x10, x10, #572
fffffe000751d5cc:      	movi.2s	v8, #1
fffffe000751d5d0:      	str	x8, [sp, #80]
fffffe000751d5d4:      	mov	w8, #4344
fffffe000751d5d8:      	add	x8, x12, x8
fffffe000751d5dc:      	str	x8, [sp, #264]
fffffe000751d5e0:      	add	x8, x10, #19, lsl #12
fffffe000751d5e4:      	add	x8, x8, #3876
fffffe000751d5e8:      	str	x8, [sp, #96]
fffffe000751d5ec:      	add	x8, x20, #19, lsl #12
fffffe000751d5f0:      	add	x10, x8, #3904
fffffe000751d5f4:      	add	x8, x20, #19, lsl #12
fffffe000751d5f8:      	add	x8, x8, #240
fffffe000751d5fc:      	stp	x8, x10, [sp, #64]
fffffe000751d600:      	add	x8, x20, #19, lsl #12
fffffe000751d604:      	add	x8, x8, #3888
fffffe000751d608:      	adrp	x10, 0xfffffe00084f0000 <_ncc_native_check+0x130>
fffffe000751d60c:      	add	x10, x10, #604
fffffe000751d610:      	str	x8, [sp, #56]
fffffe000751d614:      	add	x8, x10, #19, lsl #12
fffffe000751d618:      	add	x8, x8, #180
fffffe000751d61c:      	str	x8, [sp, #344]
fffffe000751d620:      	add	x8, x10, #19, lsl #12
fffffe000751d624:      	add	x8, x8, #3908
fffffe000751d628:      	str	x8, [sp, #336]
fffffe000751d62c:      	add	x8, x10, #22, lsl #12
fffffe000751d630:      	add	x8, x8, #3732
fffffe000751d634:      	str	x8, [sp, #328]
fffffe000751d638:      	add	x8, x9, #19, lsl #12
fffffe000751d63c:      	add	x8, x8, #224
fffffe000751d640:      	adrp	x9, 0xfffffe00084f0000 <_ncc_native_check+0x168>
fffffe000751d644:      	add	x9, x9, #556
fffffe000751d648:      	str	x8, [sp, #392]
fffffe000751d64c:      	add	x8, x9, #19, lsl #12
fffffe000751d650:      	add	x10, x8, #1660
fffffe000751d654:      	add	x8, x9, #19, lsl #12
fffffe000751d658:      	add	x8, x8, #3956
fffffe000751d65c:      	add	x9, x9, #22, lsl #12
fffffe000751d660:      	add	x9, x9, #3780
fffffe000751d664:      	stp	x8, x10, [sp, #304]
fffffe000751d668:      	adrp	x8, 0xfffffe000700e000 <_nppa_xnu_smc+0x18c>
fffffe000751d66c:      	adrp	x10, 0xfffffe000700e000 <_nppa_xnu_smc+0x190>
fffffe000751d670:      	str	x9, [sp, #296]
fffffe000751d674:      	adrp	x9, 0xfffffe000700d000 <_nppa_xnu_smc+0x194>
fffffe000751d678:      	ldr	q0, [x8, #912]
fffffe000751d67c:      	nop
fffffe000751d680:      	str	q0, [sp, #192]
fffffe000751d684:      	ldr	q0, [x10, #928]
fffffe000751d688:      	nop
fffffe000751d68c:      	str	q0, [sp, #176]
fffffe000751d690:      	ldr	q0, [x9, #144]
fffffe000751d694:      	adrp	x9, 0xfffffe000700e000 <_nppa_xnu_map+0x8>
fffffe000751d698:      	str	q0, [sp, #160]
fffffe000751d69c:      	ldr	q0, [x8, #944]
fffffe000751d6a0:      	add	x8, x11, #232
fffffe000751d6a4:      	ldr	d9, [x9, #728]
fffffe000751d6a8:      	adrp	x9, 0xfffffe00084f0000 <_ncc_native_read+0x34>
fffffe000751d6ac:      	add	x9, x9, #608
fffffe000751d6b0:      	add	x9, x9, #19, lsl #12
fffffe000751d6b4:      	str	q0, [sp, #144]
fffffe000751d6b8:      	str	x8, [sp, #272]
fffffe000751d6bc:      	add	x8, x13, #19, lsl #12
fffffe000751d6c0:      	add	x8, x8, #188
fffffe000751d6c4:      	ldr	q0, [x10, #736]
fffffe000751d6c8:      	add	x9, x9, #176
fffffe000751d6cc:      	nop
fffffe000751d6d0:      	str	x8, [sp, #240]
fffffe000751d6d4:      	adrp	x8, 0xfffffe000700e000 <_nppa_xnu_map+0x48>
fffffe000751d6d8:      	str	q0, [sp, #128]
fffffe000751d6dc:      	str	x9, [sp, #320]
fffffe000751d6e0:      	adrp	x9, 0xfffffe0008511000 <_ncc_native_read+0xf0>
fffffe000751d6e4:      	add	x9, x9, #3444
fffffe000751d6e8:      	ldr	q0, [x8, #880]
fffffe000751d6ec:      	mov	w8, #8696
fffffe000751d6f0:      	add	x8, x9, x8
fffffe000751d6f4:      	adrp	x9, 0xfffffe00084e7000 <_ncc_native_read+0x5c>
fffffe000751d6f8:      	add	x9, x9, #2776
fffffe000751d6fc:      	str	q0, [sp, #368]
fffffe000751d700:      	ldr	q0, [x10, #896]
fffffe000751d704:      	adrp	x10, 0xfffffe00084d5000 <_ncc_native_read+0x24>
fffffe000751d708:      	add	x10, x10, #2480
fffffe000751d70c:      	str	x8, [sp, #360]
fffffe000751d710:      	add	x8, x10, #26, lsl #12
fffffe000751d714:      	adrp	x10, 0xfffffe0008514000 <_ncc_native_read+0x130>
fffffe000751d718:      	add	x10, x10, #368
fffffe000751d71c:      	add	x8, x8, #792
fffffe000751d720:      	str	q0, [sp, #224]
fffffe000751d724:      	str	x8, [sp, #352]
fffffe000751d728:      	mov	w8, #33264
fffffe000751d72c:      	add	x8, x9, x8
fffffe000751d730:      	mov	w9, #59352
fffffe000751d734:      	str	x8, [sp, #216]
fffffe000751d738:      	mov	w8, #8128
fffffe000751d73c:      	add	x8, x10, x8
fffffe000751d740:      	str	x8, [sp, #120]
fffffe000751d744:      	mov	w8, #22680
fffffe000751d748:      	add	x8, x12, x8
fffffe000751d74c:      	str	x8, [sp, #208]
fffffe000751d750:      	mov	w8, #41016
fffffe000751d754:      	add	x8, x12, x8
fffffe000751d758:      	str	x8, [sp, #248]
fffffe000751d75c:      	add	x8, x12, x9
fffffe000751d760:      	mov	w9, #28316
fffffe000751d764:      	movk	w9, #1, lsl #16
fffffe000751d768:      	str	x8, [sp, #256]
fffffe000751d76c:      	mov	w8, #16204
fffffe000751d770:      	movk	w8, #1, lsl #16
fffffe000751d774:      	add	x8, x13, x8
fffffe000751d778:      	str	x8, [sp, #288]
fffffe000751d77c:      	add	x8, x13, x9
fffffe000751d780:      	str	x8, [sp, #280]
fffffe000751d784:      	b	0xfffffe000751d798 <_nss_worker_continue+0x840>
fffffe000751d788:      	mov	w26, #3
fffffe000751d78c:      	mov	x0, x22
fffffe000751d790:      	mov	w1, w26
fffffe000751d794:      	bl	0xfffffe0007520a08 <_nss_deliver>
fffffe000751d798:      	ldr	x0, [x24, #2456]
fffffe000751d79c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751d7a0:      	adrp	x10, 0xfffffe00084f0000 <_ncc_native_read+0x12c>
fffffe000751d7a4:      	add	x10, x10, #552
fffffe000751d7a8:      	mov	w22, wzr
fffffe000751d7ac:      	ldp	w8, w9, [x10, #8]
fffffe000751d7b0:      	ldp	w20, w26, [x10]
fffffe000751d7b4:      	orr	w8, w8, w9
fffffe000751d7b8:      	cmp	w8, #0
fffffe000751d7bc:      	cset	w8, eq
fffffe000751d7c0:      	str	w8, [sp, #456]
fffffe000751d7c4:      	cbz	w20, 0xfffffe000751d814 <_nss_worker_continue+0x8bc>
fffffe000751d7c8:      	cbnz	w26, 0xfffffe000751d814 <_nss_worker_continue+0x8bc>
fffffe000751d7cc:      	mov	w8, #55440
fffffe000751d7d0:      	adrp	x11, 0xfffffe00084d5000 <_ncc_native_read+0xf0>
fffffe000751d7d4:      	add	x11, x11, #2584
fffffe000751d7d8:      	mov	w9, #36960
fffffe000751d7dc:      	mov	w10, #18480
fffffe000751d7e0:      	ldr	w8, [x11, x8]
fffffe000751d7e4:      	ldr	w9, [x11, x9]
fffffe000751d7e8:      	ldr	w10, [x11, x10]
fffffe000751d7ec:      	sub	w8, w8, #1
fffffe000751d7f0:      	sub	w9, w9, #1
fffffe000751d7f4:      	cmp	w8, #2
fffffe000751d7f8:      	ccmp	w9, #2, #0, hs
fffffe000751d7fc:      	ldr	w8, [x11]
fffffe000751d800:      	sub	w9, w10, #1
fffffe000751d804:      	ccmp	w9, #2, #0, hs
fffffe000751d808:      	sub	w8, w8, #1
fffffe000751d80c:      	ccmp	w8, #2, #0, hs
fffffe000751d810:      	cset	w22, lo
fffffe000751d814:      	adrp	x8, 0xfffffe00084f0000 <_ncc_native_write+0x6c>
fffffe000751d818:      	add	x8, x8, #712
fffffe000751d81c:      	ldr	x0, [x24, #2456]
fffffe000751d820:      	ldp	x9, x21, [x8]
fffffe000751d824:      	str	x9, [sp, #448]
fffffe000751d828:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751d82c:      	orr	w8, w20, w26
fffffe000751d830:      	cbz	w8, 0xfffffe000751d89c <_nss_worker_continue+0x944>
fffffe000751d834:      	cbz	w20, 0xfffffe000751d924 <_nss_worker_continue+0x9cc>
fffffe000751d838:      	add	x0, sp, #544
fffffe000751d83c:      	str	xzr, [sp, #544]
fffffe000751d840:      	str	xzr, [sp, #504]
fffffe000751d844:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751d848:      	ldr	x0, [sp, #544]
fffffe000751d84c:      	add	x1, sp, #504
fffffe000751d850:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751d854:      	ldr	x8, [sp, #504]
fffffe000751d858:      	mov	x9, #63439
fffffe000751d85c:      	movk	x9, #58195, lsl #16
fffffe000751d860:      	ldr	w10, [sp, #456]
fffffe000751d864:      	movk	x9, #39845, lsl #32
fffffe000751d868:      	lsr	x8, x8, #3
fffffe000751d86c:      	movk	x9, #8388, lsl #48
fffffe000751d870:      	umulh	x8, x8, x9
fffffe000751d874:      	ldr	x9, [sp, #448]
fffffe000751d878:      	lsr	x8, x8, #4
fffffe000751d87c:      	cmp	x8, x9
fffffe000751d880:      	cset	w9, hs
fffffe000751d884:      	and	w9, w10, w9
fffffe000751d888:      	cmp	w9, #1
fffffe000751d88c:      	b.ne	0xfffffe000751da64 <_nss_worker_continue+0xb0c>
fffffe000751d890:      	cmp	x8, x21
fffffe000751d894:      	b.hs	0xfffffe000751da64 <_nss_worker_continue+0xb0c>
fffffe000751d898:      	cbz	w22, 0xfffffe000751d924 <_nss_worker_continue+0x9cc>
fffffe000751d89c:      	adrp	x20, 0xfffffe00084d5000 <_ncc_native_write+0x88>
fffffe000751d8a0:      	add	x20, x20, #2456
fffffe000751d8a4:      	ldr	x0, [x20]
fffffe000751d8a8:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751d8ac:      	mov	w8, #18608
fffffe000751d8b0:      	ldr	w9, [x20, #128]
fffffe000751d8b4:      	ldr	w8, [x20, x8]
fffffe000751d8b8:      	cmp	w9, #1
fffffe000751d8bc:      	b.ne	0xfffffe000751d8ec <_nss_worker_continue+0x994>
fffffe000751d8c0:      	adrp	x22, 0xfffffe00084d5000 <_ncc_native_write+0xac>
fffffe000751d8c4:      	add	x22, x22, #2584
fffffe000751d8c8:      	cmp	w8, #1
fffffe000751d8cc:      	b.ne	0xfffffe000751d8fc <_nss_worker_continue+0x9a4>
fffffe000751d8d0:      	adrp	x22, 0xfffffe00084d5000 <_ncc_native_write+0xbc>
fffffe000751d8d4:      	add	x22, x22, #2584
fffffe000751d8d8:      	ldr	x8, [x22, #18496]
fffffe000751d8dc:      	ldr	x9, [x22, #16]
fffffe000751d8e0:      	cmp	x8, x9
fffffe000751d8e4:      	b.lo	0xfffffe000751d8f4 <_nss_worker_continue+0x99c>
fffffe000751d8e8:      	b	0xfffffe000751d8fc <_nss_worker_continue+0x9a4>
fffffe000751d8ec:      	cmp	w8, #1
fffffe000751d8f0:      	b.ne	0xfffffe000751da84 <_nss_worker_continue+0xb2c>
fffffe000751d8f4:      	adrp	x22, 0xfffffe00084da000 <_ncc_native_write+0xf4>
fffffe000751d8f8:      	add	x22, x22, #584
fffffe000751d8fc:      	adrp	x8, 0xfffffe00084de000 <_ncc_native_write+0x10c>
fffffe000751d900:      	ldr	w8, [x8, #2680]
fffffe000751d904:      	cmp	w8, #1
fffffe000751d908:      	b.ne	0xfffffe000751dae0 <_nss_worker_continue+0xb88>
fffffe000751d90c:      	adrp	x8, 0xfffffe00084de000 <_ncc_native_write+0x11c>
fffffe000751d910:      	ldr	x9, [x22, #16]
fffffe000751d914:      	ldr	x8, [x8, #2696]
fffffe000751d918:      	cmp	x8, x9
fffffe000751d91c:      	b.lo	0xfffffe000751da94 <_nss_worker_continue+0xb3c>
fffffe000751d920:      	b	0xfffffe000751dae0 <_nss_worker_continue+0xb88>
fffffe000751d924:      	ldr	w8, [x25, #12]
fffffe000751d928:      	str	xzr, [sp, #544]
fffffe000751d92c:      	str	xzr, [sp, #504]
fffffe000751d930:      	cbz	w8, 0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
fffffe000751d934:      	ldr	w8, [x25, #20]
fffffe000751d938:      	cbnz	w8, 0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
fffffe000751d93c:      	ldr	x8, [x25]
fffffe000751d940:      	cmp	x8, x28
fffffe000751d944:      	b.ne	0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
fffffe000751d948:      	add	x0, sp, #544
fffffe000751d94c:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751d950:      	ldr	x0, [sp, #544]
fffffe000751d954:      	add	x1, sp, #504
fffffe000751d958:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751d95c:      	ldr	x8, [sp, #504]
fffffe000751d960:      	mov	x9, #63439
fffffe000751d964:      	movk	x9, #58195, lsl #16
fffffe000751d968:      	movk	x9, #39845, lsl #32
fffffe000751d96c:      	lsr	x8, x8, #3
fffffe000751d970:      	movk	x9, #8388, lsl #48
fffffe000751d974:      	umulh	x8, x8, x9
fffffe000751d978:      	lsr	x21, x8, #4
fffffe000751d97c:      	cbz	w20, 0xfffffe000751e3fc <_nss_worker_continue+0x14a4>
fffffe000751d980:      	ldr	x0, [x24, #2456]
fffffe000751d984:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751d988:      	adrp	x11, 0xfffffe00084f0000 <_nis_scope_or_common+0x3c>
fffffe000751d98c:      	add	x11, x11, #560
fffffe000751d990:      	ldp	w8, w9, [x11]
fffffe000751d994:      	orr	w10, w8, w9
fffffe000751d998:      	ldr	x9, [x11, #160]
fffffe000751d99c:      	cmp	w10, #0
fffffe000751d9a0:      	cset	w8, ne
fffffe000751d9a4:      	cbnz	w10, 0xfffffe000751dab0 <_nss_worker_continue+0xb58>
fffffe000751d9a8:      	ldr	x10, [x11, #152]
fffffe000751d9ac:      	cmp	x10, x21
fffffe000751d9b0:      	b.hi	0xfffffe000751dab0 <_nss_worker_continue+0xb58>
fffffe000751d9b4:      	cmp	x9, x21
fffffe000751d9b8:      	b.ls	0xfffffe000751dab0 <_nss_worker_continue+0xb58>
fffffe000751d9bc:      	adrp	x0, 0xfffffe00084f0000 <_nis_scope_or_common+0x70>
fffffe000751d9c0:      	add	x0, x0, #904
fffffe000751d9c4:      	add	x26, x0, #1496
fffffe000751d9c8:      	ldr	x1, [sp, #264]
fffffe000751d9cc:      	adrp	x2, 0xfffffe00084d5000 <_nis_scope_or_common+0x14>
fffffe000751d9d0:      	add	x2, x2, #2484
fffffe000751d9d4:      	mov	x3, x21
fffffe000751d9d8:      	mov	x4, x26
fffffe000751d9dc:      	bl	0xfffffe00079a77cc <_niwj_select>
fffffe000751d9e0:      	cbz	w0, 0xfffffe000751dd60 <_nss_worker_continue+0xe08>
fffffe000751d9e4:      	adrp	x0, 0xfffffe00084f0000 <_nis_scope_or_common+0x98>
fffffe000751d9e8:      	add	x0, x0, #904
fffffe000751d9ec:      	mov	w8, #22680
fffffe000751d9f0:      	adrp	x2, 0xfffffe00084d5000 <_nis_scope_or_common+0x38>
fffffe000751d9f4:      	add	x2, x2, #2484
fffffe000751d9f8:      	add	x1, x0, x8
fffffe000751d9fc:      	mov	x3, x21
fffffe000751da00:      	mov	x4, x26
fffffe000751da04:      	bl	0xfffffe00079a77cc <_niwj_select>
fffffe000751da08:      	cbz	w0, 0xfffffe000751ddb0 <_nss_worker_continue+0xe58>
fffffe000751da0c:      	adrp	x0, 0xfffffe00084f0000 <_nis_scope_or_common+0xc0>
fffffe000751da10:      	add	x0, x0, #904
fffffe000751da14:      	mov	w8, #41016
fffffe000751da18:      	add	x26, x0, #1496
fffffe000751da1c:      	add	x1, x0, x8
fffffe000751da20:      	adrp	x2, 0xfffffe00084d5000 <_nis_scope_or_common+0x68>
fffffe000751da24:      	add	x2, x2, #2484
fffffe000751da28:      	mov	x3, x21
fffffe000751da2c:      	mov	x4, x26
fffffe000751da30:      	bl	0xfffffe00079a77cc <_niwj_select>
fffffe000751da34:      	cbz	w0, 0xfffffe000751de00 <_nss_worker_continue+0xea8>
fffffe000751da38:      	adrp	x0, 0xfffffe00084f0000 <_nis_scope_or_common+0xec>
fffffe000751da3c:      	add	x0, x0, #904
fffffe000751da40:      	mov	w8, #59352
fffffe000751da44:      	adrp	x2, 0xfffffe00084d5000 <_nis_scope_or_common+0x8c>
fffffe000751da48:      	add	x2, x2, #2484
fffffe000751da4c:      	add	x1, x0, x8
fffffe000751da50:      	mov	x3, x21
fffffe000751da54:      	mov	x4, x26
fffffe000751da58:      	bl	0xfffffe00079a77cc <_niwj_select>
fffffe000751da5c:      	cbnz	w0, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751da60:      	b	0xfffffe000751de50 <_nss_worker_continue+0xef8>
fffffe000751da64:      	cmp	x8, x21
fffffe000751da68:      	mov	w9, #9
fffffe000751da6c:      	cset	w8, hs
fffffe000751da70:      	tst	w10, w8
fffffe000751da74:      	mov	w8, #2
fffffe000751da78:      	csel	w0, w9, w8, ne
fffffe000751da7c:      	bl	0xfffffe0007520b7c <_njs_early_finish>
fffffe000751da80:      	b	0xfffffe000751d798 <_nss_worker_continue+0x840>
fffffe000751da84:      	adrp	x8, 0xfffffe00084de000 <_nis_scope_or_common+0xf0>
fffffe000751da88:      	ldr	w8, [x8, #2680]
fffffe000751da8c:      	cmp	w8, #1
fffffe000751da90:      	b.ne	0xfffffe000751dadc <_nss_worker_continue+0xb84>
fffffe000751da94:      	mov	w8, #18480
fffffe000751da98:      	adrp	x22, 0xfffffe00084de000 <_nis_scope_or_common+0x104>
fffffe000751da9c:      	add	x22, x22, #2680
fffffe000751daa0:      	ldr	w8, [x22, x8]
fffffe000751daa4:      	cmp	w8, #1
fffffe000751daa8:      	b.eq	0xfffffe000751daf4 <_nss_worker_continue+0xb9c>
fffffe000751daac:      	b	0xfffffe000751db18 <_nss_worker_continue+0xbc0>
fffffe000751dab0:      	cmp	x9, x21
fffffe000751dab4:      	ldr	x0, [x24, #2456]
fffffe000751dab8:      	cset	w9, hi
fffffe000751dabc:      	orr	w8, w8, w9
fffffe000751dac0:      	mov	w9, #9
fffffe000751dac4:      	cmp	w8, #0
fffffe000751dac8:      	mov	w8, #2
fffffe000751dacc:      	csel	w26, w8, w9, ne
fffffe000751dad0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751dad4:      	mov	w0, w26
fffffe000751dad8:      	b	0xfffffe000751e384 <_nss_worker_continue+0x142c>
fffffe000751dadc:      	mov	x22, xzr
fffffe000751dae0:      	adrp	x8, 0xfffffe00084e3000 <_nis_scope_or_common+0x160>
fffffe000751dae4:      	ldr	w8, [x8, #680]
fffffe000751dae8:      	cmp	w8, #1
fffffe000751daec:      	b.ne	0xfffffe000751db14 <_nss_worker_continue+0xbbc>
fffffe000751daf0:      	cbz	x22, 0xfffffe000751db08 <_nss_worker_continue+0xbb0>
fffffe000751daf4:      	adrp	x8, 0xfffffe00084e3000 <_nis_scope_or_common+0x174>
fffffe000751daf8:      	ldr	x9, [x22, #16]
fffffe000751dafc:      	ldr	x8, [x8, #696]
fffffe000751db00:      	cmp	x8, x9
fffffe000751db04:      	b.hs	0xfffffe000751db18 <_nss_worker_continue+0xbc0>
fffffe000751db08:      	adrp	x22, 0xfffffe00084e3000 <_nis_scope_or_common+0x188>
fffffe000751db0c:      	add	x22, x22, #680
fffffe000751db10:      	b	0xfffffe000751db18 <_nss_worker_continue+0xbc0>
fffffe000751db14:      	cbz	x22, 0xfffffe000751dd24 <_nss_worker_continue+0xdcc>
fffffe000751db18:      	mov	w26, #2
fffffe000751db1c:      	ldr	w20, [x22, #4]
fffffe000751db20:      	str	w26, [x22]
fffffe000751db24:      	ldr	x0, [x24, #2456]
fffffe000751db28:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751db2c:      	cbnz	w20, 0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751db30:      	ldr	x8, [x25]
fffffe000751db34:      	mov	w26, #3
fffffe000751db38:      	cmp	x8, x28
fffffe000751db3c:      	b.ne	0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751db40:      	ldur	d0, [x25, #20]
fffffe000751db44:      	ldr	x8, [sp, #360]
fffffe000751db48:      	ld1.s	{ v0 }[1], [x8]
fffffe000751db4c:      	adrp	x8, 0xfffffe0008511000 <_nis_native_check+0x18>
fffffe000751db50:      	add	x8, x8, #3444
fffffe000751db54:      	ldr	d1, [x8]
fffffe000751db58:      	mov.d	v0[1], v1[0]
fffffe000751db5c:      	ldr	q1, [sp, #368]
fffffe000751db60:      	cmeq.4s	v0, v0, v1
fffffe000751db64:      	mvn.16b	v0, v0
fffffe000751db68:      	xtn.4h	v0, v0
fffffe000751db6c:      	umov.h	w8, v0[1]
fffffe000751db70:      	umov.h	w9, v0[2]
fffffe000751db74:      	umov.h	w10, v0[0]
fffffe000751db78:      	umov.h	w11, v0[3]
fffffe000751db7c:      	and	w8, w8, #0x1
fffffe000751db80:      	and	w9, w9, #0x1
fffffe000751db84:      	bfi	w10, w8, #1, #1
fffffe000751db88:      	bfi	w10, w9, #2, #1
fffffe000751db8c:      	bfi	w10, w11, #3, #29
fffffe000751db90:      	and	w8, w10, #0xf
fffffe000751db94:      	cbnz	w8, 0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751db98:      	ldr	w8, [x25, #12]
fffffe000751db9c:      	cbz	w8, 0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751dba0:      	adrp	x9, 0xfffffe0008516000 <_nis_native_check+0x80>
fffffe000751dba4:      	add	x9, x9, #328
fffffe000751dba8:      	ldr	w8, [x9]
fffffe000751dbac:      	cbnz	w8, 0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751dbb0:      	ldr	w8, [x9, #12]
fffffe000751dbb4:      	cbnz	w8, 0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751dbb8:      	adrp	x14, 0xfffffe0008510000 <_nis_native_check+0x80>
fffffe000751dbbc:      	add	x14, x14, #2312
fffffe000751dbc0:      	mov	w13, #60
fffffe000751dbc4:      	adrp	x15, 0xfffffe00084d5000 <_nis_scope_or_common+0x20c>
fffffe000751dbc8:      	add	x15, x15, #2480
fffffe000751dbcc:      	ldr	w9, [x22, #60]
fffffe000751dbd0:      	ldr	w8, [x14, #44]
fffffe000751dbd4:      	add	x10, x22, #64
fffffe000751dbd8:      	add	x11, x15, #20
fffffe000751dbdc:      	add	x3, x15, #4
fffffe000751dbe0:      	ldr	w12, [x15, #16]
fffffe000751dbe4:      	cmp	w9, #0
fffffe000751dbe8:      	sub	w8, w8, #1
fffffe000751dbec:      	csel	x4, x11, x10, eq
fffffe000751dbf0:      	ldr	x1, [x14, #4048]
fffffe000751dbf4:      	add	x0, x14, #201
fffffe000751dbf8:      	umaddl	x8, w8, w13, x14
fffffe000751dbfc:      	ldrb	w13, [x15]
fffffe000751dc00:      	csel	w5, w12, w9, eq
fffffe000751dc04:      	mov	w6, #1
fffffe000751dc08:      	mov	w7, #2
fffffe000751dc0c:      	add	w26, w13, #1
fffffe000751dc10:      	ldrb	w2, [x8, #4071]
fffffe000751dc14:      	ldr	x8, [sp, #352]
fffffe000751dc18:      	strb	w26, [sp]
fffffe000751dc1c:      	str	x8, [sp, #16]
fffffe000751dc20:      	mov	w8, #1
fffffe000751dc24:      	stur	x8, [sp, #4]
fffffe000751dc28:      	bl	0xfffffe000799f824 <_ncs_build>
fffffe000751dc2c:      	cbnz	w0, 0xfffffe000751d788 <_nss_worker_continue+0x830>
fffffe000751dc30:      	adrp	x20, 0xfffffe00084e7000 <_nis_native_check+0x54>
fffffe000751dc34:      	add	x20, x20, #2776
fffffe000751dc38:      	mov	x0, x20
fffffe000751dc3c:      	mov	x1, x19
fffffe000751dc40:      	mov	w2, #8736
fffffe000751dc44:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751dc48:      	mov	w8, #8736
fffffe000751dc4c:      	adrp	x21, 0xfffffe0008516000 <_nis_native_check+0x12c>
fffffe000751dc50:      	add	x21, x21, #304
fffffe000751dc54:      	add	x0, x20, x8
fffffe000751dc58:      	mov	x1, x21
fffffe000751dc5c:      	mov	w2, #24528
fffffe000751dc60:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751dc64:      	mov	x0, x19
fffffe000751dc68:      	mov	w1, wzr
fffffe000751dc6c:      	mov	w2, #8736
fffffe000751dc70:      	mov	w3, #8744
fffffe000751dc74:      	str	x20, [x20, #8744]
fffffe000751dc78:      	bl	0xfffffe000746b32c <___memset_chk>
fffffe000751dc7c:      	mov	x0, x21
fffffe000751dc80:      	mov	w1, #24528
fffffe000751dc84:      	bl	0xfffffe0007ba9860 <_bzero>
fffffe000751dc88:      	adrp	x9, 0xfffffe0008514000 <_nis_native_check+0x160>
fffffe000751dc8c:      	add	x9, x9, #324
fffffe000751dc90:      	ldr	q0, [sp, #224]
fffffe000751dc94:      	add	x2, sp, #544
fffffe000751dc98:      	adrp	x0, 0xfffffe000850f000 <_nis_native_check+0x15c>
fffffe000751dc9c:      	add	x0, x0, #1936
fffffe000751dca0:      	ldp	w8, w9, [x9]
fffffe000751dca4:      	str	q0, [sp, #576]
fffffe000751dca8:      	cmp	w8, #0
fffffe000751dcac:      	mov	w8, #1
fffffe000751dcb0:      	ccmp	w9, #0, #0, ne
fffffe000751dcb4:      	str	x8, [sp, #552]
fffffe000751dcb8:      	adrp	x8, 0xfffffe000700e000 <_ncd_worker+0x54>
fffffe000751dcbc:      	add	x8, x8, #1684
fffffe000751dcc0:      	cset	w20, eq
fffffe000751dcc4:      	str	x8, [sp, #544]
fffffe000751dcc8:      	adr	x8, #13620
fffffe000751dccc:      	nop
fffffe000751dcd0:      	str	x8, [sp, #560]
fffffe000751dcd4:      	ldr	x8, [sp, #216]
fffffe000751dcd8:      	str	x8, [sp, #568]
fffffe000751dcdc:      	b.ne	0xfffffe000751dfec <_nss_worker_continue+0x1094>
fffffe000751dce0:      	adrp	x1, 0xfffffe0008513000 <_nis_read+0x34>
fffffe000751dce4:      	add	x1, x1, #3952
fffffe000751dce8:      	mov	x3, x19
fffffe000751dcec:      	bl	0xfffffe000799b048 <_nct_scan_return_prepare>
fffffe000751dcf0:      	cbnz	w0, 0xfffffe000751e030 <_nss_worker_continue+0x10d8>
fffffe000751dcf4:      	mov	w5, #40832
fffffe000751dcf8:      	and	w4, w26, #0xff
fffffe000751dcfc:      	adrp	x0, 0xfffffe0008516000 <_nis_read+0x5c>
fffffe000751dd00:      	add	x0, x0, #304
fffffe000751dd04:      	adrp	x1, 0xfffffe0007c57000 <_nss_worker_continue+0x2a94>
fffffe000751dd08:      	add	x1, x1, #1624
fffffe000751dd0c:      	mov	x2, x19
fffffe000751dd10:      	adrp	x3, 0xfffffe0008513000 <_nis_read+0x64>
fffffe000751dd14:      	add	x3, x3, #4016
fffffe000751dd18:      	movk	w5, #213, lsl #16
fffffe000751dd1c:      	bl	0xfffffe000799d254 <_ncr_scan_return_prepare>
fffffe000751dd20:      	b	0xfffffe000751e02c <_nss_worker_continue+0x10d4>
fffffe000751dd24:      	adrp	x9, 0xfffffe00084f0000 <_nis_native_check+0x16c>
fffffe000751dd28:      	add	x9, x9, #552
fffffe000751dd2c:      	ldp	w8, w9, [x9]
fffffe000751dd30:      	orr	w8, w8, w9
fffffe000751dd34:      	cbnz	w8, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751dd38:      	adrp	x20, 0xfffffe00084d5000 <_nis_native_check+0x114>
fffffe000751dd3c:      	add	x20, x20, #2456
fffffe000751dd40:      	adrp	x8, 0xfffffe000851c000 <_nis_write_mask>
fffffe000751dd44:      	add	x8, x8, #872
fffffe000751dd48:      	mov	w9, #6
fffffe000751dd4c:      	mov	x0, x20
fffffe000751dd50:      	mov	w1, wzr
fffffe000751dd54:      	stlr	w9, [x8]
fffffe000751dd58:      	bl	0xfffffe000736c68c <_assert_wait>
fffffe000751dd5c:      	b	0xfffffe000751ebb4 <_nss_worker_continue+0x1c5c>
fffffe000751dd60:      	adrp	x20, 0xfffffe00084f0000 <_nis_read+0x28>
fffffe000751dd64:      	add	x20, x20, #904
fffffe000751dd68:      	add	x0, x20, #104
fffffe000751dd6c:      	mov	x1, x26
fffffe000751dd70:      	mov	w2, #1392
fffffe000751dd74:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751dd78:      	mov	x0, x20
fffffe000751dd7c:      	ldr	x1, [sp, #208]
fffffe000751dd80:      	adrp	x2, 0xfffffe00084d5000 <_nis_native_check+0x15c>
fffffe000751dd84:      	add	x2, x2, #2484
fffffe000751dd88:      	mov	x3, x21
fffffe000751dd8c:      	mov	x4, x26
fffffe000751dd90:      	bl	0xfffffe00079a77cc <_niwj_select>
fffffe000751dd94:      	cbnz	w0, 0xfffffe000751ddc4 <_nss_worker_continue+0xe6c>
fffffe000751dd98:      	adrp	x9, 0xfffffe00084f0000 <_nis_read+0x60>
fffffe000751dd9c:      	add	x9, x9, #2396
fffffe000751dda0:      	ldrsh	w8, [x9, #1392]
fffffe000751dda4:      	ldrsh	w9, [x9]
fffffe000751dda8:      	cmp	w8, w9
fffffe000751ddac:      	b.le	0xfffffe000751ddc4 <_nss_worker_continue+0xe6c>
fffffe000751ddb0:      	adrp	x0, 0xfffffe00084f0000 <_nis_read+0x78>
fffffe000751ddb4:      	add	x0, x0, #1008
fffffe000751ddb8:      	add	x1, x0, #1392
fffffe000751ddbc:      	mov	w2, #1392
fffffe000751ddc0:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751ddc4:      	adrp	x0, 0xfffffe00084f0000 <_nis_read+0x8c>
fffffe000751ddc8:      	add	x0, x0, #904
fffffe000751ddcc:      	add	x4, x0, #1496
fffffe000751ddd0:      	ldr	x1, [sp, #248]
fffffe000751ddd4:      	adrp	x2, 0xfffffe00084d5000 <_nis_read+0x30>
fffffe000751ddd8:      	add	x2, x2, #2484
fffffe000751dddc:      	mov	x3, x21
fffffe000751dde0:      	bl	0xfffffe00079a77cc <_niwj_select>
fffffe000751dde4:      	cbnz	w0, 0xfffffe000751de14 <_nss_worker_continue+0xebc>
fffffe000751dde8:      	adrp	x9, 0xfffffe00084f0000 <_nis_read+0xb0>
fffffe000751ddec:      	add	x9, x9, #2396
fffffe000751ddf0:      	ldrsh	w8, [x9, #1392]
fffffe000751ddf4:      	ldrsh	w9, [x9]
fffffe000751ddf8:      	cmp	w8, w9
fffffe000751ddfc:      	b.le	0xfffffe000751de14 <_nss_worker_continue+0xebc>
fffffe000751de00:      	adrp	x0, 0xfffffe00084f0000 <_nis_write_mask+0x10>
fffffe000751de04:      	add	x0, x0, #1008
fffffe000751de08:      	add	x1, x0, #1392
fffffe000751de0c:      	mov	w2, #1392
fffffe000751de10:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751de14:      	adrp	x0, 0xfffffe00084f0000 <_nis_write_mask+0x24>
fffffe000751de18:      	add	x0, x0, #904
fffffe000751de1c:      	add	x4, x0, #1496
fffffe000751de20:      	ldr	x1, [sp, #256]
fffffe000751de24:      	adrp	x2, 0xfffffe00084d5000 <_nis_read+0x80>
fffffe000751de28:      	add	x2, x2, #2484
fffffe000751de2c:      	mov	x3, x21
fffffe000751de30:      	bl	0xfffffe00079a77cc <_niwj_select>
fffffe000751de34:      	cbnz	w0, 0xfffffe000751de64 <_nss_worker_continue+0xf0c>
fffffe000751de38:      	adrp	x9, 0xfffffe00084f0000 <_nis_write_mask+0x48>
fffffe000751de3c:      	add	x9, x9, #2396
fffffe000751de40:      	ldrsh	w8, [x9, #1392]
fffffe000751de44:      	ldrsh	w9, [x9]
fffffe000751de48:      	cmp	w8, w9
fffffe000751de4c:      	b.le	0xfffffe000751de64 <_nss_worker_continue+0xf0c>
fffffe000751de50:      	adrp	x0, 0xfffffe00084f0000 <_nis_write_mask+0x60>
fffffe000751de54:      	add	x0, x0, #1008
fffffe000751de58:      	add	x1, x0, #1392
fffffe000751de5c:      	mov	w2, #1392
fffffe000751de60:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751de64:      	adrp	x8, 0xfffffe00084f0000 <_nis_write_mask+0x74>
fffffe000751de68:      	add	x8, x8, #720
fffffe000751de6c:      	ldr	x26, [x8, #32]
fffffe000751de70:      	cbz	x26, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751de74:      	adrp	x8, 0xfffffe000850f000 <_nis_write_mask+0x100>
fffffe000751de78:      	ldr	w8, [x8, #1168]
fffffe000751de7c:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751de80:      	ldr	x8, [x25]
fffffe000751de84:      	cmp	x8, x28
fffffe000751de88:      	b.ne	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751de8c:      	ldr	w8, [x25, #12]
fffffe000751de90:      	cbz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751de94:      	ldr	w8, [x25, #16]
fffffe000751de98:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751de9c:      	ldr	w8, [x25, #20]
fffffe000751dea0:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751dea4:      	adrp	x8, 0xfffffe0008513000 <_nis_write_mask+0x140>
fffffe000751dea8:      	ldr	w8, [x8, #3948]
fffffe000751deac:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751deb0:      	adrp	x8, 0xfffffe00084f0000 <_nis_write_mask+0xc0>
fffffe000751deb4:      	nop
fffffe000751deb8:      	ldr	x20, [x8, #720]
fffffe000751debc:      	adrp	x8, 0xfffffe00084d5000 <_nis_write_mask+0x60>
fffffe000751dec0:      	add	x8, x8, #2464
fffffe000751dec4:      	ldar	w8, [x8]
fffffe000751dec8:      	cmp	w8, #1
fffffe000751decc:      	b.ne	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751ded0:      	adrp	x8, 0xfffffe00084d5000 <_nis_write_mask+0x74>
fffffe000751ded4:      	add	x8, x8, #2464
fffffe000751ded8:      	ldr	w8, [x8, #4]
fffffe000751dedc:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751dee0:      	ldr	w8, [x23]
fffffe000751dee4:      	cbz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751dee8:      	ldr	w8, [x23, #8144]
fffffe000751deec:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751def0:      	adrp	x0, 0xfffffe0008516000 <_nis_write_mask+0x198>
fffffe000751def4:      	add	x0, x0, #2160
fffffe000751def8:      	bl	0xfffffe00079aaa20 <_nst_scan_admissible>
fffffe000751defc:      	cbz	w0, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751df00:      	mov	x0, x19
fffffe000751df04:      	bl	0xfffffe000799aca8 <_nct_done_receipt>
fffffe000751df08:      	cbz	w0, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751df0c:      	adrp	x8, 0xfffffe00084d5000 <_nis_write_mask+0xb0>
fffffe000751df10:      	ldr	w8, [x8, #2584]
fffffe000751df14:      	sub	w8, w8, #1
fffffe000751df18:      	cmp	w8, #2
fffffe000751df1c:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751df20:      	adrp	x8, 0xfffffe00084da000 <_nis_write_mask+0xd8>
fffffe000751df24:      	ldr	w8, [x8, #584]
fffffe000751df28:      	sub	w8, w8, #1
fffffe000751df2c:      	cmp	w8, #2
fffffe000751df30:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751df34:      	adrp	x8, 0xfffffe00084de000 <_nis_write_mask+0xfc>
fffffe000751df38:      	ldr	w8, [x8, #2680]
fffffe000751df3c:      	sub	w8, w8, #1
fffffe000751df40:      	cmp	w8, #2
fffffe000751df44:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751df48:      	adrp	x8, 0xfffffe00084e3000 <_nis_write_mask+0x124>
fffffe000751df4c:      	ldr	w8, [x8, #680]
fffffe000751df50:      	sub	w8, w8, #1
fffffe000751df54:      	cmp	w8, #2
fffffe000751df58:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751df5c:      	adrp	x8, 0xfffffe0008510000 <_nis_id_check+0x20>
fffffe000751df60:      	str	xzr, [sp, #552]
fffffe000751df64:      	str	xzr, [sp, #544]
fffffe000751df68:      	str	xzr, [sp, #560]
fffffe000751df6c:      	ldr	w8, [x8, #2356]
fffffe000751df70:      	sub	w9, w8, #17
fffffe000751df74:      	cmn	w9, #16
fffffe000751df78:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751df7c:      	sub	w8, w8, #1
fffffe000751df80:      	mov	w10, #60
fffffe000751df84:      	adrp	x9, 0xfffffe0008510000 <_nis_id_check+0x48>
fffffe000751df88:      	add	x9, x9, #2312
fffffe000751df8c:      	umaddl	x8, w8, w10, x9
fffffe000751df90:      	add	x0, x9, #201
fffffe000751df94:      	add	x3, sp, #544
fffffe000751df98:      	ldr	x1, [x9, #4048]
fffffe000751df9c:      	ldrb	w2, [x8, #4071]
fffffe000751dfa0:      	bl	0xfffffe000799f6c4 <_ncs_cap_mac>
fffffe000751dfa4:      	cbnz	w0, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751dfa8:      	ldrb	w8, [sp, #549]
fffffe000751dfac:      	cmp	w8, #4
fffffe000751dfb0:      	b.ne	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751dfb4:      	ldrb	w8, [sp, #545]
fffffe000751dfb8:      	cmp	w8, #7
fffffe000751dfbc:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751dfc0:      	adrp	x8, 0xfffffe00084d5000 <_nis_write_mask+0x164>
fffffe000751dfc4:      	ldr	w8, [x8, #2496]
fffffe000751dfc8:      	cbz	w8, 0xfffffe000751e2c8 <_nss_worker_continue+0x1370>
fffffe000751dfcc:      	adrp	x11, 0xfffffe00084f0000 <_nis_id_check+0x10>
fffffe000751dfd0:      	add	x11, x11, #2120
fffffe000751dfd4:      	mov	w9, wzr
fffffe000751dfd8:      	adrp	x12, 0xfffffe00084d5000 <_nis_write_mask+0x17c>
fffffe000751dfdc:      	add	x12, x12, #2501
fffffe000751dfe0:      	ldrb	w10, [x11]
fffffe000751dfe4:      	ldrb	w11, [x11, #1]
fffffe000751dfe8:      	b	0xfffffe000751e29c <_nss_worker_continue+0x1344>
fffffe000751dfec:      	adrp	x1, 0xfffffe00084e7000 <_nis_id_check+0xc>
fffffe000751dff0:      	add	x1, x1, #2776
fffffe000751dff4:      	mov	x3, x19
fffffe000751dff8:      	bl	0xfffffe000799ae30 <_nct_continue_prepare>
fffffe000751dffc:      	cbnz	w0, 0xfffffe000751e030 <_nss_worker_continue+0x10d8>
fffffe000751e000:      	mov	w5, #40832
fffffe000751e004:      	and	w4, w26, #0xff
fffffe000751e008:      	adrp	x0, 0xfffffe0008516000 <_nis_id_check+0xe0>
fffffe000751e00c:      	add	x0, x0, #304
fffffe000751e010:      	adrp	x1, 0xfffffe0007c57000 <_nbc_report+0x5c>
fffffe000751e014:      	add	x1, x1, #1624
fffffe000751e018:      	mov	x2, x19
fffffe000751e01c:      	adrp	x3, 0xfffffe00084e9000 <_nis_id_check+0x40>
fffffe000751e020:      	add	x3, x3, #3320
fffffe000751e024:      	movk	w5, #213, lsl #16
fffffe000751e028:      	bl	0xfffffe000799cf80 <_ncr_continue_prepare>
fffffe000751e02c:      	cbz	w0, 0xfffffe000751e070 <_nss_worker_continue+0x1118>
fffffe000751e030:      	adrp	x20, 0xfffffe00084e7000 <_nis_id_check+0x4c>
fffffe000751e034:      	add	x20, x20, #2776
fffffe000751e038:      	mov	x0, x19
fffffe000751e03c:      	mov	x1, x20
fffffe000751e040:      	mov	w2, #8736
fffffe000751e044:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751e048:      	mov	w8, #8736
fffffe000751e04c:      	mov	w2, #24528
fffffe000751e050:      	add	x1, x20, x8
fffffe000751e054:      	adrp	x20, 0xfffffe0008516000 <_nis_acquire+0x44>
fffffe000751e058:      	add	x20, x20, #304
fffffe000751e05c:      	mov	x0, x20
fffffe000751e060:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751e064:      	mov	w26, #3
fffffe000751e068:      	str	x19, [x20, #8]
fffffe000751e06c:      	b	0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751e070:      	mov	w8, #1
fffffe000751e074:      	adrp	x9, 0xfffffe000851c000 <_nis_acquire+0x7c>
fffffe000751e078:      	add	x9, x9, #872
fffffe000751e07c:      	add	x2, sp, #504
fffffe000751e080:      	adrp	x0, 0xfffffe000721b000 <_nss_worker_continue+0x51c>
fffffe000751e084:      	add	x0, x0, #1232
fffffe000751e088:      	str	w8, [x19, #8740]
fffffe000751e08c:      	mov	w8, #5
fffffe000751e090:      	adrp	x1, 0xfffffe0008510000 <_nis_acquire+0x68>
fffffe000751e094:      	add	x1, x1, #2312
fffffe000751e098:      	mov	x3, x19
fffffe000751e09c:      	stlr	w8, [x9]
fffffe000751e0a0:      	adrp	x8, 0xfffffe000799d000 <_nss_worker_continue+0x2344>
fffffe000751e0a4:      	add	x9, x8, #1548
fffffe000751e0a8:      	ldr	x8, [sp, #120]
fffffe000751e0ac:      	stp	x9, x8, [sp, #504]
fffffe000751e0b0:      	adr	x8, #9496
fffffe000751e0b4:      	nop
fffffe000751e0b8:      	str	x8, [sp, #520]
fffffe000751e0bc:      	adrp	x8, 0xfffffe0008514000 <_nis_acquire+0xa4>
fffffe000751e0c0:      	add	x8, x8, #368
fffffe000751e0c4:      	str	x8, [sp, #528]
fffffe000751e0c8:      	bl	0xfffffe000799b8f8 <_nct_run>
fffffe000751e0cc:      	cmp	w0, #1
fffffe000751e0d0:      	str	wzr, [x19, #8740]
fffffe000751e0d4:      	b.ne	0xfffffe000751e188 <_nss_worker_continue+0x1230>
fffffe000751e0d8:      	ldr	w8, [x19, #4572]
fffffe000751e0dc:      	cbz	w8, 0xfffffe000751e188 <_nss_worker_continue+0x1230>
fffffe000751e0e0:      	cbz	w20, 0xfffffe000751e15c <_nss_worker_continue+0x1204>
fffffe000751e0e4:      	adrp	x9, 0xfffffe0008516000 <_nis_acquire+0xd4>
fffffe000751e0e8:      	add	x9, x9, #448
fffffe000751e0ec:      	adrp	x8, 0xfffffe0008513000 <_nis_acquire+0xd0>
fffffe000751e0f0:      	add	x8, x8, #4016
fffffe000751e0f4:      	ldp	q0, q1, [x9, #32]
fffffe000751e0f8:      	ldp	q3, q2, [x9]
fffffe000751e0fc:      	stp	q0, q1, [x8, #32]
fffffe000751e100:      	ldp	q0, q1, [x9, #96]
fffffe000751e104:      	stp	q3, q2, [x8]
fffffe000751e108:      	ldp	q2, q3, [x9, #128]
fffffe000751e10c:      	stp	q0, q1, [x8, #96]
fffffe000751e110:      	ldp	q0, q1, [x9, #64]
fffffe000751e114:      	stp	q2, q3, [x8, #128]
fffffe000751e118:      	ldp	q3, q2, [x9, #192]
fffffe000751e11c:      	stp	q0, q1, [x8, #64]
fffffe000751e120:      	ldp	q0, q1, [x9, #160]
fffffe000751e124:      	stp	q3, q2, [x8, #192]
fffffe000751e128:      	ldp	q3, q2, [x9, #288]
fffffe000751e12c:      	stp	q0, q1, [x8, #160]
fffffe000751e130:      	ldp	q1, q0, [x9, #256]
fffffe000751e134:      	stp	q3, q2, [x8, #288]
fffffe000751e138:      	ldp	q2, q3, [x9, #224]
fffffe000751e13c:      	stp	q1, q0, [x8, #256]
fffffe000751e140:      	ldp	q1, q0, [x9, #368]
fffffe000751e144:      	stp	q2, q3, [x8, #224]
fffffe000751e148:      	stp	q1, q0, [x8, #368]
fffffe000751e14c:      	ldr	q3, [x9, #320]
fffffe000751e150:      	ldp	q0, q2, [x9, #336]
fffffe000751e154:      	str	q3, [x8, #320]
fffffe000751e158:      	stp	q0, q2, [x8, #336]
fffffe000751e15c:      	mov	x0, x19
fffffe000751e160:      	adrp	x1, 0xfffffe00084e7000 <_nis_acquire+0x94>
fffffe000751e164:      	add	x1, x1, #2776
fffffe000751e168:      	mov	w2, #8736
fffffe000751e16c:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751e170:      	adrp	x8, 0xfffffe0008516000 <_nis_release+0xc>
fffffe000751e174:      	add	x8, x8, #312
fffffe000751e178:      	mov	w26, #3
fffffe000751e17c:      	str	x19, [x8]
fffffe000751e180:      	str	xzr, [x8, #16]
fffffe000751e184:      	b	0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751e188:      	cbz	w20, 0xfffffe000751e198 <_nss_worker_continue+0x1240>
fffffe000751e18c:      	mov	w8, #1
fffffe000751e190:      	adrp	x9, 0xfffffe0008514000 <_nis_release+0x24>
fffffe000751e194:      	str	w8, [x9, #328]
fffffe000751e198:      	adrp	x8, 0xfffffe00084d5000 <_nis_acquire+0x84>
fffffe000751e19c:      	strb	w26, [x8, #2480]
fffffe000751e1a0:      	tbnz	w0, #31, 0xfffffe000751fc68 <_nss_worker_continue+0x2d10>
fffffe000751e1a4:      	adrp	x8, 0xfffffe0008516000 <_nis_release+0x40>
fffffe000751e1a8:      	add	x8, x8, #412
fffffe000751e1ac:      	ldr	w8, [x8, #1756]
fffffe000751e1b0:      	cbnz	w8, 0xfffffe000751e1f0 <_nss_worker_continue+0x1298>
fffffe000751e1b4:      	adrp	x8, 0xfffffe0008516000 <_nis_release+0x50>
fffffe000751e1b8:      	nop
fffffe000751e1bc:      	ldr	w8, [x8, #412]
fffffe000751e1c0:      	cbz	w8, 0xfffffe000751e1f0 <_nss_worker_continue+0x1298>
fffffe000751e1c4:      	adrp	x8, 0xfffffe0008511000 <_nis_release+0x4c>
fffffe000751e1c8:      	adrp	x10, 0xfffffe0008516000 <_nis_id_read+0xc>
fffffe000751e1cc:      	add	x10, x10, #2164
fffffe000751e1d0:      	mov	w9, #1
fffffe000751e1d4:      	ldr	w8, [x8, #3492]
fffffe000751e1d8:      	str	wzr, [x10]
fffffe000751e1dc:      	str	w9, [x10, #12]
fffffe000751e1e0:      	cbz	w8, 0xfffffe000751e1f0 <_nss_worker_continue+0x1298>
fffffe000751e1e4:      	mov	w8, #1
fffffe000751e1e8:      	adrp	x9, 0xfffffe0008516000 <_nis_id_read+0x2c>
fffffe000751e1ec:      	str	w8, [x9, #2172]
fffffe000751e1f0:      	ldr	w8, [x25, #12]
fffffe000751e1f4:      	str	xzr, [sp, #480]
fffffe000751e1f8:      	str	xzr, [sp, #536]
fffffe000751e1fc:      	cbz	w8, 0xfffffe000751fc70 <_nss_worker_continue+0x2d18>
fffffe000751e200:      	ldr	w8, [x25, #20]
fffffe000751e204:      	cbnz	w8, 0xfffffe000751fc70 <_nss_worker_continue+0x2d18>
fffffe000751e208:      	ldr	x8, [x25]
fffffe000751e20c:      	cmp	x8, x28
fffffe000751e210:      	b.ne	0xfffffe000751fc70 <_nss_worker_continue+0x2d18>
fffffe000751e214:      	add	x0, sp, #480
fffffe000751e218:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751e21c:      	ldr	x0, [sp, #480]
fffffe000751e220:      	add	x1, sp, #536
fffffe000751e224:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751e228:      	ldr	x8, [sp, #536]
fffffe000751e22c:      	mov	x9, #63439
fffffe000751e230:      	movk	x9, #58195, lsl #16
fffffe000751e234:      	movk	x9, #39845, lsl #32
fffffe000751e238:      	lsr	x8, x8, #3
fffffe000751e23c:      	movk	x9, #8388, lsl #48
fffffe000751e240:      	umulh	x8, x8, x9
fffffe000751e244:      	adrp	x9, 0xfffffe0008516000 <_nis_id_write+0x8>
fffffe000751e248:      	lsr	x20, x8, #4
fffffe000751e24c:      	ldr	x9, [x9, #2208]
fffffe000751e250:      	cmp	x20, x9
fffffe000751e254:      	b.lo	0xfffffe000751fc70 <_nss_worker_continue+0x2d18>
fffffe000751e258:      	add	x0, x22, #144
fffffe000751e25c:      	adrp	x1, 0xfffffe0008516000 <_nis_id_write+0x20>
fffffe000751e260:      	add	x1, x1, #2160
fffffe000751e264:      	mov	w2, #18336
fffffe000751e268:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe000751e26c:      	ldr	w8, [x22, #152]
fffffe000751e270:      	str	x20, [x22, #32]
fffffe000751e274:      	cbz	w8, 0xfffffe000751e288 <_nss_worker_continue+0x1330>
fffffe000751e278:      	ldr	w8, [x22, #160]
fffffe000751e27c:      	cbnz	w8, 0xfffffe000751e288 <_nss_worker_continue+0x1330>
fffffe000751e280:      	ldr	w8, [x22, #156]
fffffe000751e284:      	cbz	w8, 0xfffffe000751e2b8 <_nss_worker_continue+0x1360>
fffffe000751e288:      	mov	w26, #1
fffffe000751e28c:      	b	0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751e290:      	add	x12, x12, #2
fffffe000751e294:      	subs	x8, x8, #1
fffffe000751e298:      	b.eq	0xfffffe000751e2cc <_nss_worker_continue+0x1374>
fffffe000751e29c:      	ldurb	w13, [x12, #-1]
fffffe000751e2a0:      	cmp	w13, w10
fffffe000751e2a4:      	b.ne	0xfffffe000751e290 <_nss_worker_continue+0x1338>
fffffe000751e2a8:      	ldrb	w13, [x12]
fffffe000751e2ac:      	cmp	w13, w11
fffffe000751e2b0:      	csinc	w9, w9, wzr, ne
fffffe000751e2b4:      	b	0xfffffe000751e290 <_nss_worker_continue+0x1338>
fffffe000751e2b8:      	ldr	w8, [x22, #168]
fffffe000751e2bc:      	cmp	w8, #0
fffffe000751e2c0:      	cset	w26, ne
fffffe000751e2c4:      	b	0xfffffe000751d78c <_nss_worker_continue+0x834>
fffffe000751e2c8:      	mov	w9, wzr
fffffe000751e2cc:      	cbz	w9, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e2d0:      	adrp	x8, 0xfffffe00084f0000 <_nis_id_read+0x7c>
fffffe000751e2d4:      	add	x8, x8, #1060
fffffe000751e2d8:      	ldrb	w8, [x8]
fffffe000751e2dc:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e2e0:      	adrp	x8, 0xfffffe00084f0000 <_nis_id_write+0xc>
fffffe000751e2e4:      	add	x8, x8, #1060
fffffe000751e2e8:      	ldr	w8, [x8, #24]
fffffe000751e2ec:      	cbnz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e2f0:      	adrp	x9, 0xfffffe00084f0000 <_nis_id_write+0x1c>
fffffe000751e2f4:      	add	x9, x9, #1053
fffffe000751e2f8:      	ldurh	w8, [x9, #25]
fffffe000751e2fc:      	ldurh	w9, [x9, #27]
fffffe000751e300:      	cmp	w8, w9
fffffe000751e304:      	b.ne	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e308:      	adrp	x8, 0xfffffe00084f0000 <_nis_id_write+0x34>
fffffe000751e30c:      	add	x8, x8, #1053
fffffe000751e310:      	ldrb	w8, [x8]
fffffe000751e314:      	cbz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e318:      	adrp	x8, 0xfffffe00084f0000 <_nis_id_write+0x44>
fffffe000751e31c:      	ldrb	w8, [x8, #1052]
fffffe000751e320:      	sub	w8, w8, #33
fffffe000751e324:      	cmn	w8, #32
fffffe000751e328:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e32c:      	adrp	x8, 0xfffffe00084f0000 <_nis_id_write+0x58>
fffffe000751e330:      	add	x8, x8, #1074
fffffe000751e334:      	ldrb	w1, [x8, #1048]
fffffe000751e338:      	sub	w8, w1, #13
fffffe000751e33c:      	cmn	w8, #12
fffffe000751e340:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e344:      	adrp	x8, 0xfffffe00084f0000 <_nis_id_write+0x70>
fffffe000751e348:      	add	x8, x8, #1074
fffffe000751e34c:      	ldrh	w8, [x8]
fffffe000751e350:      	cbz	w8, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751e354:      	add	x2, sp, #536
fffffe000751e358:      	add	x3, sp, #478
fffffe000751e35c:      	add	x4, sp, #476
fffffe000751e360:      	adrp	x0, 0xfffffe00084f0000 <_nis_id_write+0x8c>
fffffe000751e364:      	add	x0, x0, #2123
fffffe000751e368:      	strh	wzr, [sp, #536]
fffffe000751e36c:      	str	wzr, [sp, #476]
fffffe000751e370:      	bl	0xfffffe00079ad63c <_nst_legacy_rates>
fffffe000751e374:      	cbz	w0, 0xfffffe000751f768 <_nss_worker_continue+0x2810>
fffffe000751e378:      	ldr	x0, [x24, #2456]
fffffe000751e37c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e380:      	mov	w0, #2
fffffe000751e384:      	bl	0xfffffe0007520b7c <_njs_early_finish>
fffffe000751e388:      	ldr	x0, [x24, #2456]
fffffe000751e38c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e390:      	adrp	x8, 0xfffffe00084f0000 <_nis_delay+0x2c>
fffffe000751e394:      	ldr	x0, [x24, #2456]
fffffe000751e398:      	ldr	w20, [x8, #556]
fffffe000751e39c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e3a0:      	cbz	w20, 0xfffffe000751d798 <_nss_worker_continue+0x840>
fffffe000751e3a4:      	ldr	w8, [x25, #12]
fffffe000751e3a8:      	str	xzr, [sp, #544]
fffffe000751e3ac:      	str	xzr, [sp, #504]
fffffe000751e3b0:      	cbz	w8, 0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
fffffe000751e3b4:      	ldr	w8, [x25, #20]
fffffe000751e3b8:      	cbnz	w8, 0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
fffffe000751e3bc:      	ldr	x8, [x25]
fffffe000751e3c0:      	cmp	x8, x28
fffffe000751e3c4:      	b.ne	0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
fffffe000751e3c8:      	add	x0, sp, #544
fffffe000751e3cc:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751e3d0:      	ldr	x0, [sp, #544]
fffffe000751e3d4:      	add	x1, sp, #504
fffffe000751e3d8:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751e3dc:      	ldr	x8, [sp, #504]
fffffe000751e3e0:      	mov	x9, #63439
fffffe000751e3e4:      	movk	x9, #58195, lsl #16
fffffe000751e3e8:      	movk	x9, #39845, lsl #32
fffffe000751e3ec:      	lsr	x8, x8, #3
fffffe000751e3f0:      	movk	x9, #8388, lsl #48
fffffe000751e3f4:      	umulh	x8, x8, x9
fffffe000751e3f8:      	lsr	x21, x8, #4
fffffe000751e3fc:      	ldr	x0, [x24, #2456]
fffffe000751e400:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e404:      	adrp	x8, 0xfffffe00084f0000 <_nbc_prepare+0x1c>
fffffe000751e408:      	ldr	w10, [x8, #568]
fffffe000751e40c:      	cbz	w10, 0xfffffe000751e418 <_nss_worker_continue+0x14c0>
fffffe000751e410:      	adrp	x8, 0xfffffe00084f0000 <_nbc_prepare+0x28>
fffffe000751e414:      	str	wzr, [x8, #568]
fffffe000751e418:      	adrp	x9, 0xfffffe00084f0000 <_nbc_prepare+0x30>
fffffe000751e41c:      	add	x9, x9, #560
fffffe000751e420:      	cmp	w10, #0
fffffe000751e424:      	str	w10, [sp, #448]
fffffe000751e428:      	ldp	w8, w9, [x9]
fffffe000751e42c:      	orr	w9, w8, w9
fffffe000751e430:      	cset	w8, ne
fffffe000751e434:      	str	w8, [sp, #408]
fffffe000751e438:      	lsr	x8, x21, #3
fffffe000751e43c:      	cbz	w9, 0xfffffe000751e4a0 <_nss_worker_continue+0x1548>
fffffe000751e440:      	mov	x9, #63439
fffffe000751e444:      	adrp	x20, 0xfffffe00084f0000 <_nbc_prepare+0x5c>
fffffe000751e448:      	add	x20, x20, #604
fffffe000751e44c:      	movk	x9, #58195, lsl #16
fffffe000751e450:      	movk	x9, #39845, lsl #32
fffffe000751e454:      	ldr	x0, [sp, #344]
fffffe000751e458:      	movk	x9, #8388, lsl #48
fffffe000751e45c:      	ldur	x1, [x20, #148]
fffffe000751e460:      	umulh	x8, x8, x9
fffffe000751e464:      	lsr	x21, x8, #4
fffffe000751e468:      	mov	x2, x21
fffffe000751e46c:      	bl	0xfffffe00079b5d54 <_nwn_cancel>
fffffe000751e470:      	ldr	w8, [x20, #4]
fffffe000751e474:      	mov	w1, #12112
fffffe000751e478:      	ldr	w9, [x20, #72]
fffffe000751e47c:      	ldr	x0, [sp, #336]
fffffe000751e480:      	add	w8, w9, w8
fffffe000751e484:      	str	w8, [x20, #72]
fffffe000751e488:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751e48c:      	movi.2d	v0, #0000000000000000
fffffe000751e490:      	ldr	x8, [sp, #328]
fffffe000751e494:      	str	xzr, [x20]
fffffe000751e498:      	stp	q0, q0, [x8]
fffffe000751e49c:      	b	0xfffffe000751e4b8 <_nss_worker_continue+0x1560>
fffffe000751e4a0:      	mov	x9, #63439
fffffe000751e4a4:      	movk	x9, #58195, lsl #16
fffffe000751e4a8:      	movk	x9, #39845, lsl #32
fffffe000751e4ac:      	movk	x9, #8388, lsl #48
fffffe000751e4b0:      	umulh	x8, x8, x9
fffffe000751e4b4:      	lsr	x21, x8, #4
fffffe000751e4b8:      	adrp	x0, 0xfffffe0008503000 <_nbc_prepare+0x11c>
fffffe000751e4bc:      	add	x0, x0, #784
fffffe000751e4c0:      	mov	x1, x21
fffffe000751e4c4:      	str	x21, [sp, #400]
fffffe000751e4c8:      	bl	0xfffffe00079b5b94 <_nwn_pump>
fffffe000751e4cc:      	tbnz	w0, #31, 0xfffffe000751fc5c <_nss_worker_continue+0x2d04>
fffffe000751e4d0:      	adrp	x9, 0xfffffe00084f0000 <_nbc_prepare+0xe8>
fffffe000751e4d4:      	add	x9, x9, #560
fffffe000751e4d8:      	adrp	x21, 0xfffffe00084f0000 <_nbc_prepare+0xf0>
fffffe000751e4dc:      	add	x21, x21, #544
fffffe000751e4e0:      	ldp	w8, w9, [x9]
fffffe000751e4e4:      	orr	w8, w8, w9
fffffe000751e4e8:      	cbnz	w8, 0xfffffe000751e5b8 <_nss_worker_continue+0x1660>
fffffe000751e4ec:      	ldr	x0, [sp, #320]
fffffe000751e4f0:      	bl	0xfffffe00079b4e44 <_nwo_authorized>
fffffe000751e4f4:      	cbz	w0, 0xfffffe000751e5b8 <_nss_worker_continue+0x1660>
fffffe000751e4f8:      	adrp	x8, 0xfffffe000850d000 <_cap_receipt+0x24>
fffffe000751e4fc:      	nop
fffffe000751e500:      	ldr	w8, [x8, #40]
fffffe000751e504:      	cbnz	w8, 0xfffffe000751e5b8 <_nss_worker_continue+0x1660>
fffffe000751e508:      	adrp	x8, 0xfffffe000850d000 <_cap_receipt+0x34>
fffffe000751e50c:      	add	x8, x8, #40
fffffe000751e510:      	ldr	w8, [x8, #4]
fffffe000751e514:      	cbnz	w8, 0xfffffe000751e5b8 <_nss_worker_continue+0x1660>
fffffe000751e518:      	adrp	x8, 0xfffffe00084f0000 <_nbc_prepare+0x130>
fffffe000751e51c:      	nop
fffffe000751e520:      	ldr	w8, [x8, #608]
fffffe000751e524:      	cbz	w8, 0xfffffe000751e5b8 <_nss_worker_continue+0x1660>
fffffe000751e528:      	ldr	w20, [x21, #60]
fffffe000751e52c:      	mov	w9, #28368
fffffe000751e530:      	movk	w9, #1, lsl #16
fffffe000751e534:      	adrp	x0, 0xfffffe000850c000 <_cap_receipt+0x5c>
fffffe000751e538:      	add	x0, x0, #4080
fffffe000751e53c:      	mov	x1, #-1
fffffe000751e540:      	add	x8, x21, x20, lsl #2
fffffe000751e544:      	add	x22, x8, x9
fffffe000751e548:      	mov	w8, #1514
fffffe000751e54c:      	mov	w9, #16256
fffffe000751e550:      	madd	x8, x20, x8, x21
fffffe000751e554:      	movk	w9, #1, lsl #16
fffffe000751e558:      	ldr	w3, [x22]
fffffe000751e55c:      	add	x26, x8, x9
fffffe000751e560:      	mov	x2, x26
fffffe000751e564:      	bl	0xfffffe00079ab870 <_nst_data>
fffffe000751e568:      	cmp	w0, #0
fffffe000751e56c:      	b.gt	0xfffffe000751e5b8 <_nss_worker_continue+0x1660>
fffffe000751e570:      	tbz	w0, #31, 0xfffffe000751e584 <_nss_worker_continue+0x162c>
fffffe000751e574:      	adrp	x9, 0xfffffe00084f0000 <_cap_receipt+0x2c>
fffffe000751e578:      	ldr	w8, [x9, #676]
fffffe000751e57c:      	add	w8, w8, #1
fffffe000751e580:      	str	w8, [x9, #676]
fffffe000751e584:      	mov	x0, x26
fffffe000751e588:      	mov	w1, #1514
fffffe000751e58c:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751e590:      	adrp	x10, 0xfffffe00084f0000 <_cap_receipt+0x48>
fffffe000751e594:      	add	x10, x10, #604
fffffe000751e598:      	add	w9, w20, #1
fffffe000751e59c:      	adrp	x21, 0xfffffe00084f0000 <_cap_receipt+0x54>
fffffe000751e5a0:      	add	x21, x21, #544
fffffe000751e5a4:      	and	w9, w9, #0x7
fffffe000751e5a8:      	ldr	w8, [x10, #4]
fffffe000751e5ac:      	str	wzr, [x22]
fffffe000751e5b0:      	sub	w8, w8, #1
fffffe000751e5b4:      	stp	w9, w8, [x10]
fffffe000751e5b8:      	adrp	x8, 0xfffffe000850d000 <_cap_receipt+0xe4>
fffffe000751e5bc:      	ldr	w8, [x8, #28]
fffffe000751e5c0:      	cbnz	w8, 0xfffffe000751fc54 <_nss_worker_continue+0x2cfc>
fffffe000751e5c4:      	adrp	x0, 0xfffffe000850c000 <_cap_receipt+0xec>
fffffe000751e5c8:      	add	x0, x0, #4080
fffffe000751e5cc:      	adrp	x1, 0xfffffe000721b000 <_nss_worker_continue+0xa68>
fffffe000751e5d0:      	add	x1, x1, #1232
fffffe000751e5d4:      	adrp	x2, 0xfffffe0008510000 <_cap_receipt+0x10c>
fffffe000751e5d8:      	add	x2, x2, #2312
fffffe000751e5dc:      	mov	w3, #16
fffffe000751e5e0:      	adr	x4, #9788
fffffe000751e5e4:      	nop
fffffe000751e5e8:      	mov	x5, x21
fffffe000751e5ec:      	bl	0xfffffe00079abdd4 <_nst_step>
fffffe000751e5f0:      	tbnz	w0, #31, 0xfffffe000751fc54 <_nss_worker_continue+0x2cfc>
fffffe000751e5f4:      	bl	0xfffffe0007520f04 <_njs_snapshot_locked>
fffffe000751e5f8:      	mov	w0, wzr
fffffe000751e5fc:      	bl	0xfffffe0007512938 <_njs_trace_locked>
fffffe000751e600:      	ldr	x0, [sp, #392]
fffffe000751e604:      	bl	0xfffffe00079b4e44 <_nwo_authorized>
fffffe000751e608:      	adrp	x8, 0xfffffe000850d000 <_cap_receipt+0x134>
fffffe000751e60c:      	adrp	x11, 0xfffffe00084f0000 <_cap_receipt+0xc4>
fffffe000751e610:      	add	x11, x11, #560
fffffe000751e614:      	cmp	w0, #0
fffffe000751e618:      	ldr	w8, [x8, #904]
fffffe000751e61c:      	ldr	w10, [x11, #36]
fffffe000751e620:      	ccmp	w8, #0, #4, ne
fffffe000751e624:      	ldp	w8, w9, [x11]
fffffe000751e628:      	cset	w12, eq
fffffe000751e62c:      	cmp	w8, #0
fffffe000751e630:      	ldr	w8, [x11, #3276]
fffffe000751e634:      	cset	w11, ne
fffffe000751e638:      	cmp	w9, #0
fffffe000751e63c:      	cset	w9, ne
fffffe000751e640:      	cmp	w10, #0
fffffe000751e644:      	cset	w10, ne
fffffe000751e648:      	cmp	w8, #0
fffffe000751e64c:      	cset	w8, eq
fffffe000751e650:      	mov	w22, w11
fffffe000751e654:      	orr	w8, w8, w11
fffffe000751e658:      	str	w12, [sp, #456]
fffffe000751e65c:      	orr	w8, w8, w9
fffffe000751e660:      	str	w9, [sp, #424]
fffffe000751e664:      	and	w20, w8, w10
fffffe000751e668:      	str	w10, [sp, #416]
fffffe000751e66c:      	cmp	w20, #1
fffffe000751e670:      	b.ne	0xfffffe000751e6b0 <_nss_worker_continue+0x1758>
fffffe000751e674:      	adrp	x21, 0xfffffe00084f0000 <_cap_receipt+0x12c>
fffffe000751e678:      	add	x21, x21, #596
fffffe000751e67c:      	movi.2d	v0, #0000000000000000
fffffe000751e680:      	ldr	x0, [sp, #288]
fffffe000751e684:      	mov	w1, #12112
fffffe000751e688:      	ldr	w8, [x21, #12]
fffffe000751e68c:      	ldr	w9, [x21, #80]
fffffe000751e690:      	str	d0, [x21]
fffffe000751e694:      	add	w8, w9, w8
fffffe000751e698:      	str	w8, [x21, #80]
fffffe000751e69c:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751e6a0:      	movi.2d	v0, #0000000000000000
fffffe000751e6a4:      	ldr	x8, [sp, #280]
fffffe000751e6a8:      	stp	q0, q0, [x8]
fffffe000751e6ac:      	str	d0, [x21, #8]
fffffe000751e6b0:      	adrp	x8, 0xfffffe0008503000 <_nbc_lease_window+0x34>
fffffe000751e6b4:      	ldr	w8, [x8, #788]
fffffe000751e6b8:      	sub	w8, w8, #21
fffffe000751e6bc:      	cmp	w8, #1
fffffe000751e6c0:      	str	w8, [sp, #444]
fffffe000751e6c4:      	b.hi	0xfffffe000751e6dc <_nss_worker_continue+0x1784>
fffffe000751e6c8:      	adrp	x8, 0xfffffe00084f0000 <_nbc_lease_window>
fffffe000751e6cc:      	ldr	w8, [x8, #580]
fffffe000751e6d0:      	cbz	w8, 0xfffffe000751e6e4 <_nss_worker_continue+0x178c>
fffffe000751e6d4:      	str	wzr, [sp, #432]
fffffe000751e6d8:      	b	0xfffffe000751e6f8 <_nss_worker_continue+0x17a0>
fffffe000751e6dc:      	str	wzr, [sp, #432]
fffffe000751e6e0:      	b	0xfffffe000751e744 <_nss_worker_continue+0x17ec>
fffffe000751e6e4:      	mov	w10, #1
fffffe000751e6e8:      	adrp	x8, 0xfffffe00084f0000 <_nbc_lease_window+0x20>
fffffe000751e6ec:      	mov	w9, #1
fffffe000751e6f0:      	str	w10, [x8, #580]
fffffe000751e6f4:      	str	w9, [sp, #432]
fffffe000751e6f8:      	adrp	x21, 0xfffffe00084f0000 <_nbc_lease_window+0x30>
fffffe000751e6fc:      	add	x21, x21, #556
fffffe000751e700:      	mov	w8, #1
fffffe000751e704:      	ldr	x0, [sp, #312]
fffffe000751e708:      	mov	w1, #1432
fffffe000751e70c:      	str	wzr, [x21]
fffffe000751e710:      	str	w8, [x21, #28]
fffffe000751e714:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751e718:      	ldr	w8, [x21, #52]
fffffe000751e71c:      	mov	w1, #12112
fffffe000751e720:      	ldr	w9, [x21, #120]
fffffe000751e724:      	ldr	x0, [sp, #304]
fffffe000751e728:      	add	w8, w9, w8
fffffe000751e72c:      	str	w8, [x21, #120]
fffffe000751e730:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751e734:      	movi.2d	v0, #0000000000000000
fffffe000751e738:      	ldr	x8, [sp, #296]
fffffe000751e73c:      	str	xzr, [x21, #48]
fffffe000751e740:      	stp	q0, q0, [x8]
fffffe000751e744:      	ldr	x0, [x24, #2456]
fffffe000751e748:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e74c:      	cbz	w20, 0xfffffe000751e7d0 <_nss_worker_continue+0x1878>
fffffe000751e750:      	adrp	x20, 0xfffffe00084f0000 <_nbc_lease_window+0x88>
fffffe000751e754:      	add	x20, x20, #544
fffffe000751e758:      	ldr	x0, [x20]
fffffe000751e75c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e760:      	ldr	x0, [x20, #208]
fffffe000751e764:      	mov	w1, wzr
fffffe000751e768:      	bl	0xfffffe0007aeaa6c <_nord2_wifi_ifnet_link>
fffffe000751e76c:      	mov	w26, w0
fffffe000751e770:      	ldr	x0, [x20]
fffffe000751e774:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e778:      	cbz	w26, 0xfffffe000751e7d0 <_nss_worker_continue+0x1878>
fffffe000751e77c:      	ldr	x0, [x24, #2456]
fffffe000751e780:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e784:      	adrp	x8, 0xfffffe00084f0000 <_nbc_lease_window+0xbc>
fffffe000751e788:      	ldr	w8, [x8, #616]
fffffe000751e78c:      	cmp	w8, #111
fffffe000751e790:      	b.hi	0xfffffe000751e7c8 <_nss_worker_continue+0x1870>
fffffe000751e794:      	add	w8, w8, #1
fffffe000751e798:      	ldr	x0, [x24, #2456]
fffffe000751e79c:      	adrp	x20, 0xfffffe00084f0000 <_nbc_lease_window+0xd4>
fffffe000751e7a0:      	add	x20, x20, #616
fffffe000751e7a4:      	str	w8, [x20]
fffffe000751e7a8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e7ac:      	ldr	x8, [x20, #136]
fffffe000751e7b0:      	adrp	x0, 0xfffffe000709a000 <_nss_worker_continue+0x648>
fffffe000751e7b4:      	add	x0, x0, #2700
fffffe000751e7b8:      	stp	xzr, x26, [sp, #8]
fffffe000751e7bc:      	str	x8, [sp]
fffffe000751e7c0:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751e7c4:      	b	0xfffffe000751e7d0 <_nss_worker_continue+0x1878>
fffffe000751e7c8:      	ldr	x0, [x24, #2456]
fffffe000751e7cc:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e7d0:      	ldr	w8, [sp, #456]
fffffe000751e7d4:      	ldr	w9, [sp, #424]
fffffe000751e7d8:      	orr	w8, w8, w22
fffffe000751e7dc:      	orr	w8, w8, w9
fffffe000751e7e0:      	ldr	w9, [sp, #416]
fffffe000751e7e4:      	orr	w8, w8, w9
fffffe000751e7e8:      	tbz	w8, #0, 0xfffffe000751e7f4 <_nss_worker_continue+0x189c>
fffffe000751e7ec:      	str	wzr, [sp, #424]
fffffe000751e7f0:      	b	0xfffffe000751e9bc <_nss_worker_continue+0x1a64>
fffffe000751e7f4:      	adrp	x20, 0xfffffe00084f0000 <_nbc_lease_window+0x12c>
fffffe000751e7f8:      	add	x20, x20, #544
fffffe000751e7fc:      	ldr	x0, [x20]
fffffe000751e800:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e804:      	ldr	x0, [x24, #2456]
fffffe000751e808:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e80c:      	ldp	w8, w9, [x20, #16]
fffffe000751e810:      	orr	w8, w8, w9
fffffe000751e814:      	cbnz	w8, 0xfffffe000751e854 <_nss_worker_continue+0x18fc>
fffffe000751e818:      	adrp	x0, 0xfffffe0008503000 <_nbc_run+0x3c>
fffffe000751e81c:      	add	x0, x0, #784
fffffe000751e820:      	bl	0xfffffe00079b4e44 <_nwo_authorized>
fffffe000751e824:      	cbz	w0, 0xfffffe000751e854 <_nss_worker_continue+0x18fc>
fffffe000751e828:      	mov	w26, #1
fffffe000751e82c:      	ldr	x0, [x24, #2456]
fffffe000751e830:      	adrp	x20, 0xfffffe00084f0000 <_nbc_run+0x8>
fffffe000751e834:      	add	x20, x20, #600
fffffe000751e838:      	str	w26, [x20]
fffffe000751e83c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e840:      	ldr	x0, [x20, #152]
fffffe000751e844:      	mov	w1, #1
fffffe000751e848:      	bl	0xfffffe0007aeaa6c <_nord2_wifi_ifnet_link>
fffffe000751e84c:      	mov	w21, w0
fffffe000751e850:      	b	0xfffffe000751e864 <_nss_worker_continue+0x190c>
fffffe000751e854:      	ldr	x0, [x24, #2456]
fffffe000751e858:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e85c:      	mov	w26, wzr
fffffe000751e860:      	mov	w21, wzr
fffffe000751e864:      	ldr	x0, [x24, #2456]
fffffe000751e868:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e86c:      	adrp	x9, 0xfffffe00084f0000 <_nbc_run+0x44>
fffffe000751e870:      	add	x9, x9, #560
fffffe000751e874:      	str	wzr, [x9, #40]
fffffe000751e878:      	cbz	w26, 0xfffffe000751e8b4 <_nss_worker_continue+0x195c>
fffffe000751e87c:      	cbnz	w21, 0xfffffe000751e8b4 <_nss_worker_continue+0x195c>
fffffe000751e880:      	ldr	w8, [x9]
fffffe000751e884:      	cbnz	w8, 0xfffffe000751e8b4 <_nss_worker_continue+0x195c>
fffffe000751e888:      	ldr	w8, [x9, #4]
fffffe000751e88c:      	cbnz	w8, 0xfffffe000751e8b4 <_nss_worker_continue+0x195c>
fffffe000751e890:      	mov	w8, #1
fffffe000751e894:      	adrp	x20, 0xfffffe00084f0000 <_nbc_run+0x6c>
fffffe000751e898:      	add	x20, x20, #576
fffffe000751e89c:      	str	w8, [x20, #20]
fffffe000751e8a0:      	bl	0xfffffe0007520f04 <_njs_snapshot_locked>
fffffe000751e8a4:      	ldr	w8, [x20]
fffffe000751e8a8:      	cbz	w8, 0xfffffe000751e918 <_nss_worker_continue+0x19c0>
fffffe000751e8ac:      	str	wzr, [sp, #424]
fffffe000751e8b0:      	b	0xfffffe000751e92c <_nss_worker_continue+0x19d4>
fffffe000751e8b4:      	adrp	x20, 0xfffffe00084f0000 <_nbc_run+0x8c>
fffffe000751e8b8:      	add	x20, x20, #596
fffffe000751e8bc:      	ldr	x0, [sp, #240]
fffffe000751e8c0:      	ldr	x2, [sp, #400]
fffffe000751e8c4:      	ldur	x1, [x20, #156]
fffffe000751e8c8:      	str	wzr, [x20]
fffffe000751e8cc:      	bl	0xfffffe00079b5d54 <_nwn_cancel>
fffffe000751e8d0:      	ldr	w8, [x20, #12]
fffffe000751e8d4:      	mov	w1, #12112
fffffe000751e8d8:      	ldr	w9, [x20, #80]
fffffe000751e8dc:      	add	w8, w9, w8
fffffe000751e8e0:      	mov	w9, #16204
fffffe000751e8e4:      	movk	w9, #1, lsl #16
fffffe000751e8e8:      	add	x0, x20, x9
fffffe000751e8ec:      	str	w8, [x20, #80]
fffffe000751e8f0:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751e8f4:      	mov	w8, #28316
fffffe000751e8f8:      	str	xzr, [x20, #8]
fffffe000751e8fc:      	movk	w8, #1, lsl #16
fffffe000751e900:      	movi.2d	v0, #0000000000000000
fffffe000751e904:      	add	x8, x20, x8
fffffe000751e908:      	stp	q0, q0, [x8]
fffffe000751e90c:      	bl	0xfffffe0007520f04 <_njs_snapshot_locked>
fffffe000751e910:      	str	wzr, [sp, #424]
fffffe000751e914:      	b	0xfffffe000751e92c <_nss_worker_continue+0x19d4>
fffffe000751e918:      	mov	w10, #1
fffffe000751e91c:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0xf4>
fffffe000751e920:      	mov	w9, #1
fffffe000751e924:      	str	w10, [x8, #576]
fffffe000751e928:      	str	w9, [sp, #424]
fffffe000751e92c:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x104>
fffffe000751e930:      	ldr	x0, [x24, #2456]
fffffe000751e934:      	ldr	w20, [x8, #596]
fffffe000751e938:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e93c:      	cbz	w26, 0xfffffe000751e954 <_nss_worker_continue+0x19fc>
fffffe000751e940:      	cbnz	w20, 0xfffffe000751e954 <_nss_worker_continue+0x19fc>
fffffe000751e944:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x11c>
fffffe000751e948:      	mov	w1, wzr
fffffe000751e94c:      	ldr	x0, [x8, #752]
fffffe000751e950:      	bl	0xfffffe0007aeaa6c <_nord2_wifi_ifnet_link>
fffffe000751e954:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x12c>
fffffe000751e958:      	ldr	x0, [x8, #544]
fffffe000751e95c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e960:      	cbz	w21, 0xfffffe000751e9bc <_nss_worker_continue+0x1a64>
fffffe000751e964:      	ldr	x0, [x24, #2456]
fffffe000751e968:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751e96c:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x144>
fffffe000751e970:      	ldr	w8, [x8, #616]
fffffe000751e974:      	cmp	w8, #111
fffffe000751e978:      	b.hi	0xfffffe000751e9b4 <_nss_worker_continue+0x1a5c>
fffffe000751e97c:      	add	w8, w8, #1
fffffe000751e980:      	ldr	x0, [x24, #2456]
fffffe000751e984:      	adrp	x20, 0xfffffe00084f0000 <_nbc_run+0x15c>
fffffe000751e988:      	add	x20, x20, #616
fffffe000751e98c:      	str	w8, [x20]
fffffe000751e990:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e994:      	ldr	x8, [x20, #136]
fffffe000751e998:      	adrp	x0, 0xfffffe000709a000 <_nss_worker_continue+0x830>
fffffe000751e99c:      	add	x0, x0, #2700
fffffe000751e9a0:      	mov	w9, #1
fffffe000751e9a4:      	str	x8, [sp]
fffffe000751e9a8:      	stp	x9, x21, [sp, #8]
fffffe000751e9ac:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751e9b0:      	b	0xfffffe000751e9bc <_nss_worker_continue+0x1a64>
fffffe000751e9b4:      	ldr	x0, [x24, #2456]
fffffe000751e9b8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e9bc:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x194>
fffffe000751e9c0:      	adrp	x21, 0xfffffe00084f0000 <_nbc_run+0x198>
fffffe000751e9c4:      	add	x21, x21, #544
fffffe000751e9c8:      	adrp	x22, 0xfffffe00084f0000 <_nbc_run+0x1a0>
fffffe000751e9cc:      	add	x22, x22, #560
fffffe000751e9d0:      	ldr	w8, [x8, #612]
fffffe000751e9d4:      	cbz	w8, 0xfffffe000751ead8 <_nss_worker_continue+0x1b80>
fffffe000751e9d8:      	mov	x20, xzr
fffffe000751e9dc:      	adrp	x26, 0xfffffe0008507000 <_nbc_run+0x210>
fffffe000751e9e0:      	add	x26, x26, #272
fffffe000751e9e4:      	b	0xfffffe000751ea20 <_nss_worker_continue+0x1ac8>
fffffe000751e9e8:      	ldr	x0, [x24, #2456]
fffffe000751e9ec:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751e9f0:      	mov	x0, x26
fffffe000751e9f4:      	mov	w1, #1514
fffffe000751e9f8:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751e9fc:      	ldr	w8, [x21, #68]
fffffe000751ea00:      	add	x9, x21, x20, lsl #2
fffffe000751ea04:      	add	x20, x20, #1
fffffe000751ea08:      	add	x26, x26, #1514
fffffe000751ea0c:      	cmp	x20, x8
fffffe000751ea10:      	mov	w8, #52624
fffffe000751ea14:      	movk	w8, #1, lsl #16
fffffe000751ea18:      	str	wzr, [x9, x8]
fffffe000751ea1c:      	b.hs	0xfffffe000751ead8 <_nss_worker_continue+0x1b80>
fffffe000751ea20:      	ldr	x0, [x24, #2456]
fffffe000751ea24:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751ea28:      	ldr	w8, [x22, #36]
fffffe000751ea2c:      	cbz	w8, 0xfffffe000751e9e8 <_nss_worker_continue+0x1a90>
fffffe000751ea30:      	ldr	w8, [x22]
fffffe000751ea34:      	cbnz	w8, 0xfffffe000751e9e8 <_nss_worker_continue+0x1a90>
fffffe000751ea38:      	ldr	w8, [x22, #4]
fffffe000751ea3c:      	cbnz	w8, 0xfffffe000751e9e8 <_nss_worker_continue+0x1a90>
fffffe000751ea40:      	add	x9, x21, x20, lsl #3
fffffe000751ea44:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x21c>
fffffe000751ea48:      	add	x8, x8, #544
fffffe000751ea4c:      	ldr	x0, [x24, #2456]
fffffe000751ea50:      	ldr	x22, [x9, #216]
fffffe000751ea54:      	str	x9, [sp, #456]
fffffe000751ea58:      	ldr	x21, [x8, #208]
fffffe000751ea5c:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ea60:      	cmp	x22, x21
fffffe000751ea64:      	adrp	x22, 0xfffffe00084f0000 <_nbc_run+0x23c>
fffffe000751ea68:      	add	x22, x22, #560
fffffe000751ea6c:      	adrp	x21, 0xfffffe00084f0000 <_nbc_run+0x244>
fffffe000751ea70:      	add	x21, x21, #544
fffffe000751ea74:      	b.ne	0xfffffe000751e9f0 <_nss_worker_continue+0x1a98>
fffffe000751ea78:      	mov	w9, #52624
fffffe000751ea7c:      	add	x8, x21, x20, lsl #2
fffffe000751ea80:      	movk	w9, #1, lsl #16
fffffe000751ea84:      	mov	x1, x26
fffffe000751ea88:      	ldr	w2, [x8, x9]
fffffe000751ea8c:      	ldr	x8, [sp, #456]
fffffe000751ea90:      	ldr	x0, [x8, #216]
fffffe000751ea94:      	bl	0xfffffe0007aead18 <_nord2_wifi_ifnet_receive>
fffffe000751ea98:      	cbnz	w0, 0xfffffe000751e9f0 <_nss_worker_continue+0x1a98>
fffffe000751ea9c:      	ldr	x0, [x24, #2456]
fffffe000751eaa0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751eaa4:      	adrp	x10, 0xfffffe00084f0000 <_nbc_run+0x27c>
fffffe000751eaa8:      	add	x10, x10, #680
fffffe000751eaac:      	ldp	w9, w8, [x10]
fffffe000751eab0:      	add	w9, w9, #1
fffffe000751eab4:      	cmp	w8, #7
fffffe000751eab8:      	str	w9, [x10]
fffffe000751eabc:      	b.hi	0xfffffe000751e9e8 <_nss_worker_continue+0x1a90>
fffffe000751eac0:      	add	w8, w8, #1
fffffe000751eac4:      	adrp	x9, 0xfffffe00084f0000 <_nbc_run+0x29c>
fffffe000751eac8:      	mov	w0, #1
fffffe000751eacc:      	str	w8, [x9, #684]
fffffe000751ead0:      	bl	0xfffffe0007512938 <_njs_trace_locked>
fffffe000751ead4:      	b	0xfffffe000751e9e8 <_nss_worker_continue+0x1a90>
fffffe000751ead8:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x2b0>
fffffe000751eadc:      	str	wzr, [x8, #612]
fffffe000751eae0:      	ldr	w8, [sp, #448]
fffffe000751eae4:      	cbz	w8, 0xfffffe000751eaf4 <_nss_worker_continue+0x1b9c>
fffffe000751eae8:      	mov	w0, #4
fffffe000751eaec:      	bl	0xfffffe0007521090 <_njs_notice>
fffffe000751eaf0:      	b	0xfffffe000751eb0c <_nss_worker_continue+0x1bb4>
fffffe000751eaf4:      	ldr	w8, [sp, #424]
fffffe000751eaf8:      	ldr	w9, [sp, #408]
fffffe000751eafc:      	cmp	w8, #0
fffffe000751eb00:      	cset	w8, eq
fffffe000751eb04:      	orr	w8, w8, w9
fffffe000751eb08:      	tbz	w8, #0, 0xfffffe000751ebc8 <_nss_worker_continue+0x1c70>
fffffe000751eb0c:      	ldr	w8, [sp, #432]
fffffe000751eb10:      	cbz	w8, 0xfffffe000751eb2c <_nss_worker_continue+0x1bd4>
fffffe000751eb14:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x2ec>
fffffe000751eb18:      	ldr	w8, [x8, #576]
fffffe000751eb1c:      	cmp	w8, #0
fffffe000751eb20:      	mov	w8, #2
fffffe000751eb24:      	cinc	w0, w8, ne
fffffe000751eb28:      	bl	0xfffffe0007521090 <_njs_notice>
fffffe000751eb2c:      	ldr	w8, [sp, #444]
fffffe000751eb30:      	cmp	w8, #1
fffffe000751eb34:      	b.hi	0xfffffe000751eb80 <_nss_worker_continue+0x1c28>
fffffe000751eb38:      	mov	w0, #4
fffffe000751eb3c:      	bl	0xfffffe0007521090 <_njs_notice>
fffffe000751eb40:      	adrp	x8, 0xfffffe0008503000 <_nbc_run+0x364>
fffffe000751eb44:      	ldr	w8, [x8, #788]
fffffe000751eb48:      	cmp	w8, #21
fffffe000751eb4c:      	b.ne	0xfffffe000751d798 <_nss_worker_continue+0x840>
fffffe000751eb50:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x3ac>
fffffe000751eb54:      	ldr	x8, [x8, #3368]
fffffe000751eb58:      	cmp	x8, x28
fffffe000751eb5c:      	b.ne	0xfffffe000751d798 <_nss_worker_continue+0x840>
fffffe000751eb60:      	ldr	x0, [x24, #2456]
fffffe000751eb64:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751eb68:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x340>
fffffe000751eb6c:      	ldr	w8, [x8, #592]
fffffe000751eb70:      	cbz	w8, 0xfffffe000751ebfc <_nss_worker_continue+0x1ca4>
fffffe000751eb74:      	ldr	x0, [x24, #2456]
fffffe000751eb78:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751eb7c:      	b	0xfffffe000751d798 <_nss_worker_continue+0x840>
fffffe000751eb80:      	add	x2, sp, #544
fffffe000751eb84:      	mov	w0, #1000
fffffe000751eb88:      	mov	w1, #1000
fffffe000751eb8c:      	str	xzr, [sp, #544]
fffffe000751eb90:      	bl	0xfffffe0007337480 <_clock_interval_to_deadline>
fffffe000751eb94:      	adrp	x20, 0xfffffe00084d5000 <_nbc_run+0x300>
fffffe000751eb98:      	add	x20, x20, #2456
fffffe000751eb9c:      	ldr	x0, [x20]
fffffe000751eba0:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751eba4:      	ldr	x2, [sp, #544]
fffffe000751eba8:      	mov	x0, x20
fffffe000751ebac:      	mov	w1, wzr
fffffe000751ebb0:      	bl	0xfffffe000736ca44 <_assert_wait_deadline>
fffffe000751ebb4:      	ldr	x0, [x20]
fffffe000751ebb8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ebbc:      	mov	x0, xzr
fffffe000751ebc0:      	bl	0xfffffe000736d140 <_thread_block>
fffffe000751ebc4:      	b	0xfffffe000751d798 <_nss_worker_continue+0x840>
fffffe000751ebc8:      	ldr	x0, [x24, #2456]
fffffe000751ebcc:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751ebd0:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x3a8>
fffffe000751ebd4:      	add	x8, x8, #560
fffffe000751ebd8:      	ldr	x0, [x24, #2456]
fffffe000751ebdc:      	ldp	w22, w20, [x8]
fffffe000751ebe0:      	ldr	w21, [x8, #36]
fffffe000751ebe4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751ebe8:      	cbz	w21, 0xfffffe000751eb0c <_nss_worker_continue+0x1bb4>
fffffe000751ebec:      	cbnz	w22, 0xfffffe000751eb0c <_nss_worker_continue+0x1bb4>
fffffe000751ebf0:      	cbnz	w20, 0xfffffe000751eb0c <_nss_worker_continue+0x1bb4>
fffffe000751ebf4:      	mov	w0, #1
fffffe000751ebf8:      	b	0xfffffe000751eaec <_nss_worker_continue+0x1b94>
fffffe000751ebfc:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x458>
fffffe000751ec00:      	add	x8, x8, #3380
fffffe000751ec04:      	adrp	x11, 0xfffffe0007c94000 <_njs_cache_scan_locked+0xac>
fffffe000751ec08:      	adrp	x12, 0xfffffe00084f0000 <_nbc_run+0x3e0>
fffffe000751ec0c:      	add	x12, x12, #552
fffffe000751ec10:      	adrp	x13, 0xfffffe0007c58000 <_nct_native_report+0x15c>
fffffe000751ec14:      	add	x13, x13, #940
fffffe000751ec18:      	ldur	d2, [x8, #4]
fffffe000751ec1c:      	add	x11, x11, #584
fffffe000751ec20:      	add	x10, x12, #20
fffffe000751ec24:      	ldr	d1, [x12]
fffffe000751ec28:      	add	x9, x13, #20
fffffe000751ec2c:      	ldur	d0, [x13, #12]
fffffe000751ec30:      	add	x8, x11, #300
fffffe000751ec34:      	ldr	s4, [x11, #60]
fffffe000751ec38:      	ld1.s	{ v1 }[2], [x10]
fffffe000751ec3c:      	add	x10, x12, #3272
fffffe000751ec40:      	ld1.s	{ v0 }[2], [x9]
fffffe000751ec44:      	add	x9, x11, #16
fffffe000751ec48:      	ldr	d3, [x8]
fffffe000751ec4c:      	add	x8, x11, #280
fffffe000751ec50:      	ld1.s	{ v2 }[2], [x13]
fffffe000751ec54:      	add	x11, x13, #8
fffffe000751ec58:      	ldr	q5, [sp, #192]
fffffe000751ec5c:      	ld1.s	{ v4 }[1], [x8]
fffffe000751ec60:      	add	x8, x12, #28
fffffe000751ec64:      	ld1.s	{ v0 }[3], [x9]
fffffe000751ec68:      	add	x9, x12, #140
fffffe000751ec6c:      	ld1.s	{ v2 }[3], [x11]
fffffe000751ec70:      	ld1.s	{ v1 }[3], [x8]
fffffe000751ec74:      	mov	w8, #16092
fffffe000751ec78:      	mov.d	v4[1], v3[0]
fffffe000751ec7c:      	movk	w8, #1, lsl #16
fffffe000751ec80:      	ldr	x11, [sp, #272]
fffffe000751ec84:      	ldr	d18, [x12, #56]
fffffe000751ec88:      	ldr	d6, [x12, x8]
fffffe000751ec8c:      	add	x8, x12, #3264
fffffe000751ec90:      	cmtst.4s	v3, v0, v0
fffffe000751ec94:      	ldr	s16, [x11, #2880]
fffffe000751ec98:      	cmeq.4s	v2, v2, v5
fffffe000751ec9c:      	ldr	d17, [x12, #32]
fffffe000751eca0:      	cmtst.4s	v5, v4, v4
fffffe000751eca4:      	ld1.s	{ v18 }[2], [x8]
fffffe000751eca8:      	cmtst.4s	v7, v1, v1
fffffe000751ecac:      	ldur	d19, [x12, #44]
fffffe000751ecb0:      	mvn.16b	v2, v2
fffffe000751ecb4:      	ld1.s	{ v17 }[1], [x9]
fffffe000751ecb8:      	uzp1.4s	v16, v16, v6
fffffe000751ecbc:      	add	x8, x11, #3568
fffffe000751ecc0:      	uzp1.8h	v2, v2, v3
fffffe000751ecc4:      	add	x9, x11, #4
fffffe000751ecc8:      	uzp1.8h	v3, v5, v7
fffffe000751eccc:      	ldr	q5, [x10]
fffffe000751ecd0:      	ldr	q7, [sp, #160]
fffffe000751ecd4:      	mov.s	v16[1], v6[1]
fffffe000751ecd8:      	mov.s	v18[3], v5[3]
fffffe000751ecdc:      	ld1.s	{ v5 }[3], [x9]
fffffe000751ece0:      	mov.d	v17[1], v19[0]
fffffe000751ece4:      	adrp	x9, 0xfffffe0008514000 <_nbc_run+0x54c>
fffffe000751ece8:      	add	x9, x9, #320
fffffe000751ecec:      	cmeq.4s	v4, v4, #0
fffffe000751ecf0:      	ld1.s	{ v16 }[3], [x8]
fffffe000751ecf4:      	cmeq.4s	v1, v1, #0
fffffe000751ecf8:      	cmeq.4s	v7, v18, v7
fffffe000751ecfc:      	cmtst.4s	v6, v17, v17
fffffe000751ed00:      	uzp1.8h	v1, v4, v1
fffffe000751ed04:      	mvn.16b	v4, v7
fffffe000751ed08:      	ldr	q7, [sp, #144]
fffffe000751ed0c:      	uzp1.8h	v4, v6, v4
fffffe000751ed10:      	cmtst.4s	v6, v16, v16
fffffe000751ed14:      	cmeq.4s	v5, v5, v7
fffffe000751ed18:      	cmeq.4s	v0, v0, #0
fffffe000751ed1c:      	mvn.16b	v5, v5
fffffe000751ed20:      	uzp1.8h	v5, v5, v6
fffffe000751ed24:      	xtn2.8h	v0, v0
fffffe000751ed28:      	uzp1.16b	v2, v2, v3
fffffe000751ed2c:      	uzp1.16b	v3, v0, v1
fffffe000751ed30:      	cmeq.4s	v0, v17, #0
fffffe000751ed34:      	uzp1.16b	v1, v4, v5
fffffe000751ed38:      	ldr	q4, [sp, #176]
fffffe000751ed3c:      	xtn.4h	v0, v0
fffffe000751ed40:      	ldp	w8, w9, [x9]
fffffe000751ed44:      	xtn.8b	v0, v0
fffffe000751ed48:      	tbl.16b	v2, { v2, v3 }, v4
fffffe000751ed4c:      	mov.b	v1[1], v0[1]
fffffe000751ed50:      	cmp	w8, #0
fffffe000751ed54:      	adrp	x8, 0xfffffe00084d5000 <_nbc_run+0x4c0>
fffffe000751ed58:      	cset	w10, ne
fffffe000751ed5c:      	cmp	w9, #0
fffffe000751ed60:      	cset	w9, ne
fffffe000751ed64:      	orr.16b	v0, v2, v1
fffffe000751ed68:      	ldr	w8, [x8, #2468]
fffffe000751ed6c:      	umaxv.16b	b0, v0
fffffe000751ed70:      	cmp	w8, #0
fffffe000751ed74:      	fmov	w11, s0
fffffe000751ed78:      	orr	w8, w11, w9
fffffe000751ed7c:      	cset	w9, ne
fffffe000751ed80:      	orr	w8, w8, w10
fffffe000751ed84:      	orr	w8, w8, w9
fffffe000751ed88:      	tbnz	w8, #0, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ed8c:      	adrp	x8, 0xfffffe0008513000 <_nbc_run+0x5f0>
fffffe000751ed90:      	ldr	w8, [x8, #3948]
fffffe000751ed94:      	cbnz	w8, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ed98:      	adrp	x8, 0xfffffe0008511000 <_nbc_run+0x5f4>
fffffe000751ed9c:      	nop
fffffe000751eda0:      	ldr	w8, [x8, #3380]
fffffe000751eda4:      	cbz	w8, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751eda8:      	ldr	x8, [sp, #272]
fffffe000751edac:      	ldr	w8, [x8, #3580]
fffffe000751edb0:      	cbz	w8, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751edb4:      	adrp	x8, 0xfffffe00084f0000 <_nbc_run+0x58c>
fffffe000751edb8:      	add	x8, x8, #552
fffffe000751edbc:      	ldr	x8, [x8, #200]
fffffe000751edc0:      	cbz	x8, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751edc4:      	ldr	x9, [sp, #272]
fffffe000751edc8:      	ldr	x9, [x9, #1240]
fffffe000751edcc:      	cmp	x9, x8
fffffe000751edd0:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751edd4:      	ldr	x9, [sp, #272]
fffffe000751edd8:      	ldr	x9, [x9, #3560]
fffffe000751eddc:      	cbnz	x9, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ede0:      	adrp	x9, 0xfffffe00084f0000 <_nbc_run+0x5b8>
fffffe000751ede4:      	add	x9, x9, #552
fffffe000751ede8:      	ldr	x9, [x9, #336]
fffffe000751edec:      	cbnz	x9, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751edf0:      	adrp	x9, 0xfffffe00084f0000 <_nbc_run+0x5c8>
fffffe000751edf4:      	add	x9, x9, #552
fffffe000751edf8:      	ldr	x9, [x9, #344]
fffffe000751edfc:      	cbnz	x9, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ee00:      	ldr	x9, [sp, #272]
fffffe000751ee04:      	adrp	x10, 0xfffffe000850c000 <_nbc_run+0x64c>
fffffe000751ee08:      	add	x10, x10, #4080
fffffe000751ee0c:      	ldr	x9, [x9, #3584]
fffffe000751ee10:      	cmp	x9, x10
fffffe000751ee14:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ee18:      	adrp	x0, 0xfffffe0008503000 <_nbc_run+0x63c>
fffffe000751ee1c:      	add	x0, x0, #2080
fffffe000751ee20:      	adrp	x17, 0xfffffe000850c000 <_nbc_run+0x668>
fffffe000751ee24:      	add	x17, x17, #4080
fffffe000751ee28:      	add	x9, x17, #28
fffffe000751ee2c:      	adrp	x14, 0xfffffe0008516000 <_rr+0x28>
fffffe000751ee30:      	add	x14, x14, #304
fffffe000751ee34:      	ldr	d0, [x0, #2352]
fffffe000751ee38:      	ldr	d1, [x17, #9376]
fffffe000751ee3c:      	ldur	d2, [x17, #44]
fffffe000751ee40:      	ld1.s	{ v0 }[2], [x9]
fffffe000751ee44:      	add	x9, x17, #36
fffffe000751ee48:      	ldr	w11, [x17, #148]
fffffe000751ee4c:      	mov.d	v2[1], v1[0]
fffffe000751ee50:      	ldr	q1, [sp, #128]
fffffe000751ee54:      	ld1.s	{ v0 }[3], [x9]
fffffe000751ee58:      	ldr	x9, [x17, #16]
fffffe000751ee5c:      	cmeq.4s	v1, v2, v1
fffffe000751ee60:      	cmeq.4s	v2, v0, #0
fffffe000751ee64:      	cmp	x9, x14
fffffe000751ee68:      	cmtst.4s	v0, v0, v0
fffffe000751ee6c:      	cset	w9, eq
fffffe000751ee70:      	uzp1.8h	v1, v2, v1
fffffe000751ee74:      	cmp	w11, #0
fffffe000751ee78:      	xtn.4h	v0, v0
fffffe000751ee7c:      	uzp1.16b	v0, v1, v0
fffffe000751ee80:      	tbl.8b	v0, { v0 }, v9
fffffe000751ee84:      	umov.b	w10, v0[1]
fffffe000751ee88:      	umov.b	w12, v0[0]
fffffe000751ee8c:      	umov.b	w13, v0[2]
fffffe000751ee90:      	umov.b	w14, v0[3]
fffffe000751ee94:      	umov.b	w15, v0[4]
fffffe000751ee98:      	umov.b	w16, v0[5]
fffffe000751ee9c:      	and	w10, w10, #0x1
fffffe000751eea0:      	and	w12, w12, #0x1
fffffe000751eea4:      	and	w13, w13, #0x1
fffffe000751eea8:      	and	w14, w14, #0x1
fffffe000751eeac:      	and	w15, w15, #0x1
fffffe000751eeb0:      	and	w11, w16, #0x1
fffffe000751eeb4:      	bfi	w12, w10, #1, #1
fffffe000751eeb8:      	umov.b	w10, v0[6]
fffffe000751eebc:      	bfi	w12, w13, #2, #1
fffffe000751eec0:      	umov.b	w13, v0[7]
fffffe000751eec4:      	bfi	w12, w14, #3, #1
fffffe000751eec8:      	ldr	x14, [x0]
fffffe000751eecc:      	bfi	w12, w15, #4, #1
fffffe000751eed0:      	and	w10, w10, #0x1
fffffe000751eed4:      	bfi	w12, w11, #5, #1
fffffe000751eed8:      	add	x11, x0, #2352
fffffe000751eedc:      	orr	w10, w12, w10, lsl #6
fffffe000751eee0:      	ldr	w12, [x17, #40]
fffffe000751eee4:      	ccmp	x14, x11, #0, eq
fffffe000751eee8:      	orr	w10, w10, w13, lsl #7
fffffe000751eeec:      	mov	w11, #255
fffffe000751eef0:      	bic	w10, w11, w10
fffffe000751eef4:      	orr	w10, w10, w12
fffffe000751eef8:      	ccmp	w10, #0, #0, eq
fffffe000751eefc:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef00:      	adrp	x10, 0xfffffe000850c000 <_coherent+0x20>
fffffe000751ef04:      	add	x10, x10, #4080
fffffe000751ef08:      	adrp	x11, 0xfffffe0007c57000 <_njs_early_finish+0x70>
fffffe000751ef0c:      	add	x11, x11, #1624
fffffe000751ef10:      	ldr	x10, [x10]
fffffe000751ef14:      	cmp	x10, x11
fffffe000751ef18:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef1c:      	adrp	x10, 0xfffffe000850c000 <_coherent+0x3c>
fffffe000751ef20:      	add	x10, x10, #4080
fffffe000751ef24:      	ldr	x10, [x10, #8]
fffffe000751ef28:      	cmp	x10, x19
fffffe000751ef2c:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef30:      	tbz	w9, #0, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef34:      	adrp	x9, 0xfffffe0008503000 <_coherent+0x30>
fffffe000751ef38:      	add	x9, x9, #2080
fffffe000751ef3c:      	ldr	x9, [x9, #2360]
fffffe000751ef40:      	cbnz	x9, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef44:      	adrp	x9, 0xfffffe000850c000 <_coherent+0x64>
fffffe000751ef48:      	add	x9, x9, #4080
fffffe000751ef4c:      	ldr	x9, [x9, #152]
fffffe000751ef50:      	cmp	x9, x8
fffffe000751ef54:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef58:      	mov	x0, x19
fffffe000751ef5c:      	bl	0xfffffe000799aca8 <_nct_done_receipt>
fffffe000751ef60:      	cbz	w0, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef64:      	adrp	x8, 0xfffffe000850c000 <_coherent+0x84>
fffffe000751ef68:      	adrp	x10, 0xfffffe000850f000 <_coherent+0x94>
fffffe000751ef6c:      	add	x10, x10, #1936
fffffe000751ef70:      	ldr	x8, [x8, #4088]
fffffe000751ef74:      	ldr	x9, [x8]
fffffe000751ef78:      	cmp	x9, x10
fffffe000751ef7c:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef80:      	ldr	x9, [x8, #8]
fffffe000751ef84:      	adrp	x10, 0xfffffe0008510000 <_coherent+0xb4>
fffffe000751ef88:      	add	x10, x10, #2312
fffffe000751ef8c:      	cmp	x9, x10
fffffe000751ef90:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751ef94:      	ldr	x9, [x8, #16]
fffffe000751ef98:      	adrp	x10, 0xfffffe0007c57000 <_njs_event+0x60>
fffffe000751ef9c:      	add	x10, x10, #1624
fffffe000751efa0:      	cmp	x9, x10
fffffe000751efa4:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751efa8:      	adrp	x10, 0xfffffe0007c94000 <_njs_event+0x164>
fffffe000751efac:      	ldr	x9, [x8, #24]
fffffe000751efb0:      	add	x10, x10, #584
fffffe000751efb4:      	cmp	x9, x10
fffffe000751efb8:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751efbc:      	adrp	x9, 0xfffffe000850d000 <_coherent+0xe0>
fffffe000751efc0:      	add	x9, x9, #8
fffffe000751efc4:      	ldr	w10, [x8, #132]
fffffe000751efc8:      	ldr	w11, [x8, #68]
fffffe000751efcc:      	ldr	w9, [x9, #64]
fffffe000751efd0:      	add	w10, w11, w10
fffffe000751efd4:      	cmp	w9, w10
fffffe000751efd8:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751efdc:      	adrp	x10, 0xfffffe000850d000 <_coherent+0x100>
fffffe000751efe0:      	nop
fffffe000751efe4:      	ldr	w10, [x10, #8]
fffffe000751efe8:      	cbz	w10, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751efec:      	adrp	x10, 0xfffffe000850d000 <_coherent+0x110>
fffffe000751eff0:      	add	x10, x10, #8
fffffe000751eff4:      	ldr	w10, [x10, #88]
fffffe000751eff8:      	cmp	w10, #23
fffffe000751effc:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f000:      	adrp	x10, 0xfffffe000850d000 <_coherent+0x120>
fffffe000751f004:      	add	x10, x10, #8
fffffe000751f008:      	ldr	w10, [x10, #104]
fffffe000751f00c:      	cbnz	w10, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f010:      	adrp	x13, 0xfffffe000850d000 <_coherent+0x130>
fffffe000751f014:      	add	x13, x13, #40
fffffe000751f018:      	add	x10, x13, #868
fffffe000751f01c:      	add	x11, x13, #896
fffffe000751f020:      	ldr	d0, [x13]
fffffe000751f024:      	ldr	d1, [x10]
fffffe000751f028:      	add	x10, x13, #864
fffffe000751f02c:      	ldr	w12, [x13, #900]
fffffe000751f030:      	ld1.s	{ v0 }[2], [x11]
fffffe000751f034:      	add	x11, x13, #888
fffffe000751f038:      	ld1.s	{ v1 }[2], [x10]
fffffe000751f03c:      	add	x10, x13, #576
fffffe000751f040:      	ld1.s	{ v0 }[3], [x11]
fffffe000751f044:      	adrp	x11, 0xfffffe0007c58000 <_njs_event+0x10c>
fffffe000751f048:      	ld1.s	{ v1 }[3], [x10]
fffffe000751f04c:      	ldr	w10, [x13, #60]
fffffe000751f050:      	ldrb	w11, [x11, #404]
fffffe000751f054:      	cmtst.4s	v0, v0, v0
fffffe000751f058:      	cmtst.4s	v1, v1, v1
fffffe000751f05c:      	sub	w11, w11, #1
fffffe000751f060:      	cmp	w10, w11, uxtb
fffffe000751f064:      	ldr	w10, [x13, #80]
fffffe000751f068:      	uzp1.8h	v0, v0, v1
fffffe000751f06c:      	cset	w11, eq
fffffe000751f070:      	cmp	w12, #3
fffffe000751f074:      	cset	w12, eq
fffffe000751f078:      	cmp	w10, #0
fffffe000751f07c:      	xtn.8b	v0, v0
fffffe000751f080:      	umaxv.8b	b0, v0
fffffe000751f084:      	fmov	w13, s0
fffffe000751f088:      	bic	w10, w12, w13
fffffe000751f08c:      	cset	w12, ne
fffffe000751f090:      	and	w10, w10, w11
fffffe000751f094:      	and	w10, w10, w12
fffffe000751f098:      	cmp	w10, #1
fffffe000751f09c:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f0a0:      	adrp	x10, 0xfffffe000850d000 <_sy+0x78>
fffffe000751f0a4:      	add	x10, x10, #40
fffffe000751f0a8:      	ldr	w10, [x10, #720]
fffffe000751f0ac:      	cbnz	w10, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f0b0:      	adrp	x10, 0xfffffe000850d000 <_sy+0x88>
fffffe000751f0b4:      	add	x10, x10, #40
fffffe000751f0b8:      	ldrb	w10, [x10, #929]
fffffe000751f0bc:      	cbnz	w10, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f0c0:      	adrp	x10, 0xfffffe000850d000 <_sy+0x98>
fffffe000751f0c4:      	ldr	w10, [x10, #76]
fffffe000751f0c8:      	cbz	w10, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f0cc:      	adrp	x11, 0xfffffe000850d000 <_sy+0xa4>
fffffe000751f0d0:      	ldr	w12, [x11, #80]
fffffe000751f0d4:      	cmn	w12, w9
fffffe000751f0d8:      	cset	w11, hs
fffffe000751f0dc:      	cmp	w10, w12
fffffe000751f0e0:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f0e4:      	tbnz	w11, #0, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f0e8:      	adrp	x11, 0xfffffe0007c57000 <_njs_event+0x1ac>
fffffe000751f0ec:      	add	w9, w10, w9
fffffe000751f0f0:      	ldr	w11, [x11, #3544]
fffffe000751f0f4:      	cmp	w11, w9
fffffe000751f0f8:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f0fc:      	cmn	w11, #1
fffffe000751f100:      	b.eq	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f104:      	adrp	x10, 0xfffffe0007c57000 <_njs_event+0x1c8>
fffffe000751f108:      	add	x10, x10, #3532
fffffe000751f10c:      	movi.2d	v1, #0000000000000000
fffffe000751f110:      	ldr	w9, [x10, #956]
fffffe000751f114:      	fmov	s0, w9
fffffe000751f118:      	ld1.s	{ v0 }[1], [x10], #4
fffffe000751f11c:      	ldr	d2, [x10]
fffffe000751f120:      	adrp	x10, 0xfffffe000850d000 <_mr+0x3c>
fffffe000751f124:      	add	x10, x10, #68
fffffe000751f128:      	mov.d	v0[1], v2[0]
fffffe000751f12c:      	ld1.s	{ v1 }[0], [x10]
fffffe000751f130:      	cmeq.4s	v0, v0, v1
fffffe000751f134:      	xtn.4h	v0, v0
fffffe000751f138:      	umov.h	w10, v0[1]
fffffe000751f13c:      	umov.h	w11, v0[2]
fffffe000751f140:      	umov.h	w12, v0[0]
fffffe000751f144:      	umov.h	w13, v0[3]
fffffe000751f148:      	and	w10, w10, #0x1
fffffe000751f14c:      	and	w11, w11, #0x1
fffffe000751f150:      	bfi	w12, w10, #1, #1
fffffe000751f154:      	mov	w10, #15
fffffe000751f158:      	bfi	w12, w11, #2, #1
fffffe000751f15c:      	bfi	w12, w13, #3, #29
fffffe000751f160:      	bics	wzr, w10, w12
fffffe000751f164:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f168:      	cmp	w9, #1023
fffffe000751f16c:      	b.hi	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f170:      	adrp	x9, 0xfffffe0007c57000 <_njs_event+0x234>
fffffe000751f174:      	add	x9, x9, #1636
fffffe000751f178:      	ldr	w9, [x9, #1864]
fffffe000751f17c:      	cbnz	w9, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f180:      	adrp	x9, 0xfffffe0007c57000 <_njs_event+0x244>
fffffe000751f184:      	nop
fffffe000751f188:      	ldr	w9, [x9, #1636]
fffffe000751f18c:      	cbnz	w9, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f190:      	adrp	x9, 0xfffffe000850d000 <_mr+0xac>
fffffe000751f194:      	ldr	w9, [x9, #56]
fffffe000751f198:      	cbnz	w9, 0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f19c:      	adrp	x10, 0xfffffe000850d000 <_mr+0xb8>
fffffe000751f1a0:      	add	x10, x10, #48
fffffe000751f1a4:      	ldp	w10, w9, [x10]
fffffe000751f1a8:      	cmp	w9, w10
fffffe000751f1ac:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f1b0:      	adrp	x11, 0xfffffe000850d000 <_mw+0x10>
fffffe000751f1b4:      	add	x11, x11, #60
fffffe000751f1b8:      	ldp	w10, w11, [x11]
fffffe000751f1bc:      	cmp	w10, w11
fffffe000751f1c0:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f1c4:      	ldr	w8, [x8, #108]
fffffe000751f1c8:      	cmp	w9, w8
fffffe000751f1cc:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f1d0:      	adrp	x8, 0xfffffe000850f000 <_mw+0x38>
fffffe000751f1d4:      	ldr	w8, [x8, #2368]
fffffe000751f1d8:      	sub	w8, w8, #1
fffffe000751f1dc:      	cmp	w9, w8
fffffe000751f1e0:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f1e4:      	adrp	x8, 0xfffffe0007c58000 <_njs_event+0x2ac>
fffffe000751f1e8:      	adrp	x9, 0xfffffe000850d000 <_mw+0x48>
fffffe000751f1ec:      	ldr	w8, [x8, #396]
fffffe000751f1f0:      	ldr	w9, [x9, #328]
fffffe000751f1f4:      	cmp	w8, w9
fffffe000751f1f8:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f1fc:      	mov	x8, xzr
fffffe000751f200:      	adrp	x9, 0xfffffe000850d000 <_mw+0x60>
fffffe000751f204:      	add	x9, x9, #252
fffffe000751f208:      	adrp	x10, 0xfffffe0007c57000 <_njs_event+0x2cc>
fffffe000751f20c:      	add	x10, x10, #1624
fffffe000751f210:      	add	x10, x10, x8
fffffe000751f214:      	ldur	x11, [x9, #-36]
fffffe000751f218:      	ldr	x12, [x10, #216]
fffffe000751f21c:      	cmp	x11, x12
fffffe000751f220:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f224:      	ldur	x11, [x9, #-28]
fffffe000751f228:      	ldr	x10, [x10, #224]
fffffe000751f22c:      	cmp	x11, x10
fffffe000751f230:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f234:      	adrp	x10, 0xfffffe0007c57000 <_njs_snapshot_locked+0x10>
fffffe000751f238:      	add	x10, x10, #1624
fffffe000751f23c:      	add	x11, x10, x8
fffffe000751f240:      	ldur	w10, [x9, #-20]
fffffe000751f244:      	ldr	w12, [x11, #232]
fffffe000751f248:      	cmp	w10, w12
fffffe000751f24c:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f250:      	ldur	w10, [x9, #-16]
fffffe000751f254:      	ldr	w11, [x11, #236]
fffffe000751f258:      	cmp	w10, w11
fffffe000751f25c:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f260:      	adrp	x11, 0xfffffe0007c57000 <_njs_snapshot_locked+0x3c>
fffffe000751f264:      	add	x11, x11, #1624
fffffe000751f268:      	add	x11, x11, x8
fffffe000751f26c:      	ldur	w12, [x9, #-12]
fffffe000751f270:      	ldr	w13, [x11, #240]
fffffe000751f274:      	cmp	w12, w13
fffffe000751f278:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f27c:      	ldur	w12, [x9, #-8]
fffffe000751f280:      	ldr	w11, [x11, #244]
fffffe000751f284:      	cmp	w12, w11
fffffe000751f288:      	b.ne	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f28c:      	ldur	w11, [x9, #-4]
fffffe000751f290:      	cmp	w11, w10
fffffe000751f294:      	b.hs	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f298:      	ldr	w10, [x9]
fffffe000751f29c:      	cmp	w10, #2
fffffe000751f2a0:      	b.hs	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f2a4:      	add	x8, x8, #32
fffffe000751f2a8:      	add	x9, x9, #80
fffffe000751f2ac:      	cmp	x8, #160
fffffe000751f2b0:      	b.ne	0xfffffe000751f208 <_nss_worker_continue+0x22b0>
fffffe000751f2b4:      	adrp	x9, 0xfffffe000850d000 <_rw+0x50>
fffffe000751f2b8:      	add	x9, x9, #20
fffffe000751f2bc:      	mov	w8, #1
fffffe000751f2c0:      	adrp	x10, 0xfffffe0008514000 <_rw+0x78>
fffffe000751f2c4:      	adrp	x0, 0xfffffe0008510000 <_rw+0x6c>
fffffe000751f2c8:      	add	x0, x0, #2312
fffffe000751f2cc:      	ldp	w21, w22, [x9]
fffffe000751f2d0:      	str	w8, [x10, #320]
fffffe000751f2d4:      	str	wzr, [sp, #480]
fffffe000751f2d8:      	str	d8, [x9]
fffffe000751f2dc:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f2e0:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f2e4:      	ldr	w8, [x25, #12]
fffffe000751f2e8:      	str	xzr, [sp, #544]
fffffe000751f2ec:      	str	xzr, [sp, #504]
fffffe000751f2f0:      	cbz	w8, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f2f4:      	ldr	w8, [x25, #20]
fffffe000751f2f8:      	cbnz	w8, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f2fc:      	ldr	x8, [x25]
fffffe000751f300:      	cmp	x8, x28
fffffe000751f304:      	b.ne	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f308:      	add	x0, sp, #544
fffffe000751f30c:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751f310:      	ldr	x0, [sp, #544]
fffffe000751f314:      	add	x1, sp, #504
fffffe000751f318:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751f31c:      	ldr	x8, [sp, #504]
fffffe000751f320:      	mov	x9, #63439
fffffe000751f324:      	movk	x9, #58195, lsl #16
fffffe000751f328:      	movk	x9, #39845, lsl #32
fffffe000751f32c:      	lsr	x8, x8, #3
fffffe000751f330:      	movk	x9, #8388, lsl #48
fffffe000751f334:      	umulh	x8, x8, x9
fffffe000751f338:      	adrp	x9, 0xfffffe000850d000 <_rw+0xd4>
fffffe000751f33c:      	add	x9, x9, #152
fffffe000751f340:      	lsr	x8, x8, #4
fffffe000751f344:      	ldr	x9, [x9]
fffffe000751f348:      	cmp	x8, x9
fffffe000751f34c:      	b.lo	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f350:      	adrp	x9, 0xfffffe000850d000 <_drain+0x14>
fffffe000751f354:      	add	x9, x9, #152
fffffe000751f358:      	ldr	x9, [x9, #8]
fffffe000751f35c:      	cmp	x8, x9
fffffe000751f360:      	b.hs	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f364:      	adrp	x9, 0xfffffe000850d000 <_drain+0x28>
fffffe000751f368:      	adrp	x0, 0xfffffe0008510000 <_drain+0x38>
fffffe000751f36c:      	add	x0, x0, #2312
fffffe000751f370:      	str	x8, [x9, #152]
fffffe000751f374:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f378:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f37c:      	adrp	x8, 0xfffffe000850f000 <_drain+0x48>
fffffe000751f380:      	mov	w9, #3
fffffe000751f384:      	ldr	x8, [x8, #1456]
fffffe000751f388:      	ldr	w8, [x8, #240]
fffffe000751f38c:      	dmb	oshld
fffffe000751f390:      	bics	wzr, w9, w8
fffffe000751f394:      	str	w8, [sp, #480]
fffffe000751f398:      	b.ne	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f39c:      	adrp	x0, 0xfffffe0008510000 <_drain+0x6c>
fffffe000751f3a0:      	add	x0, x0, #2312
fffffe000751f3a4:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f3a8:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f3ac:      	adrp	x8, 0xfffffe0007c58000 <_njs_notice>
fffffe000751f3b0:      	ldr	w8, [x8, #948]
fffffe000751f3b4:      	sub	w8, w8, #4
fffffe000751f3b8:      	cmp	w8, #3
fffffe000751f3bc:      	cset	w0, lo
fffffe000751f3c0:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f3c4:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f3c8:      	adrp	x8, 0xfffffe0008514000 <_drain+0xa8>
fffffe000751f3cc:      	ldr	w8, [x8, #376]
fffffe000751f3d0:      	cbz	w8, 0xfffffe000751f3e0 <_nss_worker_continue+0x2488>
fffffe000751f3d4:      	adrp	x8, 0xfffffe0008513000 <_drain+0xb0>
fffffe000751f3d8:      	ldr	w8, [x8, #3948]
fffffe000751f3dc:      	cbnz	w8, 0xfffffe000751f3ec <_nss_worker_continue+0x2494>
fffffe000751f3e0:      	adrp	x0, 0xfffffe0007c57000 <_njs_notice+0x30>
fffffe000751f3e4:      	add	x0, x0, #1624
fffffe000751f3e8:      	bl	0xfffffe0007516418 <_nst_native_rx_active>
fffffe000751f3ec:      	adrp	x8, 0xfffffe00084d5000 <_rw+0xa8>
fffffe000751f3f0:      	mov	w9, #16896
fffffe000751f3f4:      	movk	w9, #6146, lsl #16
fffffe000751f3f8:      	adrp	x10, 0xfffffe0007c58000 <_njs_notice+0x4c>
fffffe000751f3fc:      	add	x10, x10, #1036
fffffe000751f400:      	ldr	x8, [x8, #1664]
fffffe000751f404:      	ldr	w8, [x8, #512]
fffffe000751f408:      	dmb	oshld
fffffe000751f40c:      	stp	w9, w8, [x10]
fffffe000751f410:      	tst	w8, #0x300000
fffffe000751f414:      	str	w8, [sp, #480]
fffffe000751f418:      	mov	w8, #1
fffffe000751f41c:      	str	w8, [x10, #8]
fffffe000751f420:      	b.ne	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f424:      	adrp	x0, 0xfffffe0008510000 <_drain+0xf4>
fffffe000751f428:      	add	x0, x0, #2312
fffffe000751f42c:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f430:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f434:      	adrp	x8, 0xfffffe0007c58000 <_njs_notice+0x88>
fffffe000751f438:      	ldr	w8, [x8, #948]
fffffe000751f43c:      	sub	w8, w8, #4
fffffe000751f440:      	cmp	w8, #3
fffffe000751f444:      	cset	w0, lo
fffffe000751f448:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f44c:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f450:      	adrp	x8, 0xfffffe0008514000 <_drain+0x130>
fffffe000751f454:      	ldr	w8, [x8, #376]
fffffe000751f458:      	cbz	w8, 0xfffffe000751f468 <_nss_worker_continue+0x2510>
fffffe000751f45c:      	adrp	x8, 0xfffffe0008513000 <_drain+0x138>
fffffe000751f460:      	ldr	w8, [x8, #3948]
fffffe000751f464:      	cbnz	w8, 0xfffffe000751f474 <_nss_worker_continue+0x251c>
fffffe000751f468:      	adrp	x0, 0xfffffe0007c57000 <_njs_rng+0x8>
fffffe000751f46c:      	add	x0, x0, #1624
fffffe000751f470:      	bl	0xfffffe0007516418 <_nst_native_rx_active>
fffffe000751f474:      	adrp	x8, 0xfffffe00084d5000 <_drain+0x58>
fffffe000751f478:      	mov	w9, #16896
fffffe000751f47c:      	movk	w9, #6146, lsl #16
fffffe000751f480:      	adrp	x10, 0xfffffe0007c58000 <_njs_rng+0x24>
fffffe000751f484:      	add	x10, x10, #1036
fffffe000751f488:      	add	w9, w9, #1, lsl #12
fffffe000751f48c:      	ldr	x8, [x8, #1664]
fffffe000751f490:      	ldr	w8, [x8, #4608]
fffffe000751f494:      	dmb	oshld
fffffe000751f498:      	str	w8, [sp, #480]
fffffe000751f49c:      	tst	w8, #0x300000
fffffe000751f4a0:      	stp	w9, w8, [x10]
fffffe000751f4a4:      	mov	w8, #1
fffffe000751f4a8:      	str	w8, [x10, #8]
fffffe000751f4ac:      	b.ne	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f4b0:      	adrp	x0, 0xfffffe0008510000 <_drain+0x180>
fffffe000751f4b4:      	add	x0, x0, #2312
fffffe000751f4b8:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f4bc:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f4c0:      	mov	w1, #21528
fffffe000751f4c4:      	add	x2, sp, #480
fffffe000751f4c8:      	adrp	x0, 0xfffffe0007c57000 <_njs_peer+0x38>
fffffe000751f4cc:      	add	x0, x0, #1624
fffffe000751f4d0:      	movk	w1, #6146, lsl #16
fffffe000751f4d4:      	bl	0xfffffe000750fe10 <_nwfn_read>
fffffe000751f4d8:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f4dc:      	adrp	x9, 0xfffffe000850d000 <_drain+0x1a0>
fffffe000751f4e0:      	ldr	w8, [sp, #480]
fffffe000751f4e4:      	ldr	w9, [x9, #68]
fffffe000751f4e8:      	cmp	w8, w9
fffffe000751f4ec:      	b.ne	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f4f0:      	adrp	x0, 0xfffffe0008510000 <_drain+0x1c0>
fffffe000751f4f4:      	add	x0, x0, #2312
fffffe000751f4f8:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f4fc:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f500:      	mov	w8, #21528
fffffe000751f504:      	add	x2, sp, #480
fffffe000751f508:      	movk	w8, #6146, lsl #16
fffffe000751f50c:      	adrp	x0, 0xfffffe0007c57000 <_njs_peer+0x7c>
fffffe000751f510:      	add	x0, x0, #1624
fffffe000751f514:      	add	w1, w8, #4
fffffe000751f518:      	bl	0xfffffe000750fe10 <_nwfn_read>
fffffe000751f51c:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f520:      	adrp	x8, 0xfffffe000850d000 <_drain+0x1e4>
fffffe000751f524:      	ldr	w20, [sp, #480]
fffffe000751f528:      	ldr	w8, [x8, #68]
fffffe000751f52c:      	cmp	w20, w8
fffffe000751f530:      	b.ne	0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f534:      	adrp	x8, 0xfffffe0007c58000 <_nss_build+0x1c>
fffffe000751f538:      	adrp	x0, 0xfffffe0008510000 <_drain+0x208>
fffffe000751f53c:      	add	x0, x0, #2312
fffffe000751f540:      	ldr	x26, [x8, #360]
fffffe000751f544:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f548:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f54c:      	adrp	x8, 0xfffffe0007c58000 <_nss_build+0x34>
fffffe000751f550:      	ldr	w8, [x8, #948]
fffffe000751f554:      	sub	w8, w8, #4
fffffe000751f558:      	cmp	w8, #3
fffffe000751f55c:      	cset	w0, lo
fffffe000751f560:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f564:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f568:      	lsl	w8, w20, #4
fffffe000751f56c:      	adrp	x0, 0xfffffe0007c57000 <_nss_build+0x50>
fffffe000751f570:      	add	x0, x0, #1624
fffffe000751f574:      	add	x26, x26, x8
fffffe000751f578:      	mov	x1, x26
fffffe000751f57c:      	mov	w2, #16
fffffe000751f580:      	mov	w3, wzr
fffffe000751f584:      	bl	0xfffffe0007516560 <_nwfn_memory_range>
fffffe000751f588:      	cbnz	w0, 0xfffffe000751f5a8 <_nss_worker_continue+0x2650>
fffffe000751f58c:      	adrp	x0, 0xfffffe0007c57000 <_ncc_ap_report+0x8>
fffffe000751f590:      	add	x0, x0, #1624
fffffe000751f594:      	mov	x1, x26
fffffe000751f598:      	mov	w2, #16
fffffe000751f59c:      	mov	w3, #1
fffffe000751f5a0:      	bl	0xfffffe0007516560 <_nwfn_memory_range>
fffffe000751f5a4:      	cbz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f5a8:      	dsb	sy
fffffe000751f5ac:      	adrp	x0, 0xfffffe0008510000 <_drain+0x27c>
fffffe000751f5b0:      	add	x0, x0, #2312
fffffe000751f5b4:      	bl	0xfffffe0007515ffc <_ncd_native_owner>
fffffe000751f5b8:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f5bc:      	adrp	x8, 0xfffffe0007c58000 <_ncc_ap_report+0x3c>
fffffe000751f5c0:      	ldr	w8, [x8, #948]
fffffe000751f5c4:      	sub	w8, w8, #4
fffffe000751f5c8:      	cmp	w8, #3
fffffe000751f5cc:      	cset	w0, lo
fffffe000751f5d0:      	bl	0xfffffe000750e674 <_nwfn_guard>
fffffe000751f5d4:      	cbnz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f5d8:      	adrp	x0, 0xfffffe0007c57000 <_ncc_ap_report+0x54>
fffffe000751f5dc:      	add	x0, x0, #1624
fffffe000751f5e0:      	mov	x1, x26
fffffe000751f5e4:      	mov	w2, #16
fffffe000751f5e8:      	mov	w3, wzr
fffffe000751f5ec:      	bl	0xfffffe0007516560 <_nwfn_memory_range>
fffffe000751f5f0:      	cbz	w0, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f5f4:      	adrp	x9, 0xfffffe0007c58000 <_ncc_ap_report+0x74>
fffffe000751f5f8:      	add	x9, x9, #584
fffffe000751f5fc:      	ldr	x8, [x9, #312]
fffffe000751f600:      	ldr	x9, [x9]
fffffe000751f604:      	add	x8, x8, x26
fffffe000751f608:      	sub	x8, x8, x9
fffffe000751f60c:      	ldrb	wzr, [x8]
fffffe000751f610:      	ldrb	wzr, [x8, #1]
fffffe000751f614:      	ldrb	wzr, [x8, #2]
fffffe000751f618:      	ldrb	wzr, [x8, #3]
fffffe000751f61c:      	ldrb	wzr, [x8, #4]
fffffe000751f620:      	ldrb	wzr, [x8, #5]
fffffe000751f624:      	ldrb	wzr, [x8, #6]
fffffe000751f628:      	ldrsb	w9, [x8, #7]
fffffe000751f62c:      	ldrb	wzr, [x8, #8]
fffffe000751f630:      	ldrb	wzr, [x8, #9]
fffffe000751f634:      	ldrb	wzr, [x8, #10]
fffffe000751f638:      	ldrb	wzr, [x8, #11]
fffffe000751f63c:      	ldrb	wzr, [x8, #12]
fffffe000751f640:      	ldrb	wzr, [x8, #13]
fffffe000751f644:      	ldrb	wzr, [x8, #14]
fffffe000751f648:      	ldrb	wzr, [x8, #15]
fffffe000751f64c:      	tbz	w9, #31, 0xfffffe000751fc80 <_nss_worker_continue+0x2d28>
fffffe000751f650:      	adrp	x15, 0xfffffe000850d000 <_drain+0x314>
fffffe000751f654:      	add	x15, x15, #20
fffffe000751f658:      	adrp	x10, 0xfffffe000850f000 <_drain+0x324>
fffffe000751f65c:      	add	x10, x10, #1936
fffffe000751f660:      	adrp	x16, 0xfffffe0008513000 <_drain+0x33c>
fffffe000751f664:      	add	x16, x16, #3952
fffffe000751f668:      	adrp	x8, 0xfffffe0008510000 <_drain+0x338>
fffffe000751f66c:      	add	x8, x8, #2312
fffffe000751f670:      	adrp	x13, 0xfffffe0007c57000 <_ncc_cold_read+0x5c>
fffffe000751f674:      	add	x13, x13, #1624
fffffe000751f678:      	adrp	x9, 0xfffffe0007c94000 <_ncc_native_check+0x28>
fffffe000751f67c:      	stp	w21, w22, [x15]
fffffe000751f680:      	stp	x10, x8, [x16]
fffffe000751f684:      	mov	w14, #1
fffffe000751f688:      	str	x13, [x16, #16]
fffffe000751f68c:      	ldr	w8, [x13, #1920]
fffffe000751f690:      	add	x9, x9, #584
fffffe000751f694:      	adrp	x0, 0xfffffe000709a000 <_nss_worker_continue+0x1528>
fffffe000751f698:      	add	x0, x0, #2753
fffffe000751f69c:      	ldr	w12, [x15, #28]
fffffe000751f6a0:      	ldur	q0, [x15, #244]
fffffe000751f6a4:      	stp	w14, w8, [x16, #32]
fffffe000751f6a8:      	ldur	q1, [x15, #228]
fffffe000751f6ac:      	str	x9, [x16, #24]
fffffe000751f6b0:      	str	w12, [x16, #48]
fffffe000751f6b4:      	add	x12, x15, #196
fffffe000751f6b8:      	ldur	q2, [x15, #196]
fffffe000751f6bc:      	str	wzr, [x15, #1096]
fffffe000751f6c0:      	ldur	q3, [x15, #212]
fffffe000751f6c4:      	stp	q1, q0, [x16, #96]
fffffe000751f6c8:      	ldp	q0, q1, [x12, #64]
fffffe000751f6cc:      	str	wzr, [x15, #9344]
fffffe000751f6d0:      	stp	q2, q3, [x16, #64]
fffffe000751f6d4:      	str	w14, [x16, #468]
fffffe000751f6d8:      	ldp	q3, q2, [x12, #128]
fffffe000751f6dc:      	stp	q0, q1, [x16, #128]
fffffe000751f6e0:      	ldp	q1, q0, [x12, #96]
fffffe000751f6e4:      	stp	q3, q2, [x16, #192]
fffffe000751f6e8:      	ldp	q3, q2, [x12, #192]
fffffe000751f6ec:      	stp	q1, q0, [x16, #160]
fffffe000751f6f0:      	ldp	q0, q1, [x12, #160]
fffffe000751f6f4:      	stp	q3, q2, [x16, #256]
fffffe000751f6f8:      	ldp	q2, q3, [x12, #224]
fffffe000751f6fc:      	stp	q0, q1, [x16, #224]
fffffe000751f700:      	ldp	q1, q0, [x12, #288]
fffffe000751f704:      	stp	q2, q3, [x16, #288]
fffffe000751f708:      	ldp	q3, q2, [x12, #256]
fffffe000751f70c:      	stp	q1, q0, [x16, #352]
fffffe000751f710:      	ldp	q1, q0, [x12, #368]
fffffe000751f714:      	stp	q3, q2, [x16, #320]
fffffe000751f718:      	ldr	w9, [x13, #2864]
fffffe000751f71c:      	stp	q1, q0, [x16, #432]
fffffe000751f720:      	ldr	w10, [x10, #432]
fffffe000751f724:      	ldp	q3, q2, [x12, #336]
fffffe000751f728:      	stp	x8, x9, [sp, #8]
fffffe000751f72c:      	stp	w9, w10, [x16, #40]
fffffe000751f730:      	ldrb	w11, [x13, #2876]
fffffe000751f734:      	stp	q3, q2, [x16, #400]
fffffe000751f738:      	ldur	x10, [x15, #116]
fffffe000751f73c:      	ldr	q0, [x12, #320]
fffffe000751f740:      	ldr	w12, [x13, #2868]
fffffe000751f744:      	adrp	x13, 0xfffffe00084f0000 <_drain+0x394>
fffffe000751f748:      	strb	w11, [x16, #52]
fffffe000751f74c:      	str	x10, [x16, #56]
fffffe000751f750:      	str	q0, [x16, #384]
fffffe000751f754:      	str	w14, [x13, #592]
fffffe000751f758:      	stp	x11, x12, [sp, #24]
fffffe000751f75c:      	str	x10, [sp]
fffffe000751f760:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000751f764:      	b	0xfffffe000751eb74 <_nss_worker_continue+0x1c1c>
fffffe000751f768:      	adrp	x12, 0xfffffe00084f0000 <_drain+0x3b8>
fffffe000751f76c:      	add	x12, x12, #1008
fffffe000751f770:      	adrp	x11, 0xfffffe00084d5000 <_drain+0x354>
fffffe000751f774:      	add	x11, x11, #2484
fffffe000751f778:      	ldrb	w8, [sp, #549]
fffffe000751f77c:      	strb	wzr, [sp, #492]
fffffe000751f780:      	ldrb	w9, [x12, #1113]
fffffe000751f784:      	ldr	w10, [x11]
fffffe000751f788:      	strb	w8, [sp, #496]
fffffe000751f78c:      	ldrb	w8, [sp, #548]
fffffe000751f790:      	strb	w9, [sp, #498]
fffffe000751f794:      	ldrh	w9, [x11, #4]
fffffe000751f798:      	str	w10, [sp, #480]
fffffe000751f79c:      	ldrh	w10, [x12, #4]
fffffe000751f7a0:      	ldr	x11, [sp, #112]
fffffe000751f7a4:      	strb	w8, [sp, #493]
fffffe000751f7a8:      	strh	w9, [sp, #484]
fffffe000751f7ac:      	ldrb	w9, [x12, #1112]
fffffe000751f7b0:      	strh	w10, [x11, #4]
fffffe000751f7b4:      	ldr	w10, [x12]
fffffe000751f7b8:      	mov	w12, #1280
fffffe000751f7bc:      	cmp	w9, #1
fffffe000751f7c0:      	strb	w9, [sp, #497]
fffffe000751f7c4:      	str	w10, [x11]
fffffe000751f7c8:      	strh	w12, [sp, #494]
fffffe000751f7cc:      	b.ne	0xfffffe000751f7f8 <_nss_worker_continue+0x28a0>
fffffe000751f7d0:      	adrp	x10, 0xfffffe00084f0000 <_drain+0x420>
fffffe000751f7d4:      	ldrh	w9, [sp, #536]
fffffe000751f7d8:      	mov	w11, #17
fffffe000751f7dc:      	ldrh	w10, [x10, #1076]
fffffe000751f7e0:      	tst	w9, #0x3fc0
fffffe000751f7e4:      	and	w9, w10, #0x20
fffffe000751f7e8:      	mov	w10, #1041
fffffe000751f7ec:      	csel	w10, w11, w10, eq
fffffe000751f7f0:      	orr	w22, w10, w9
fffffe000751f7f4:      	b	0xfffffe000751f7fc <_nss_worker_continue+0x28a4>
fffffe000751f7f8:      	mov	w22, #17
fffffe000751f7fc:      	adrp	x9, 0xfffffe00084f0000 <_drain+0x44c>
fffffe000751f800:      	add	x9, x9, #1062
fffffe000751f804:      	ldrb	w9, [x9]
fffffe000751f808:      	cbz	w9, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751f80c:      	adrp	x9, 0xfffffe00084f0000 <_drain+0x45c>
fffffe000751f810:      	ldrb	w9, [x9, #1061]
fffffe000751f814:      	tbz	w9, #2, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751f818:      	adrp	x9, 0xfffffe00084d5000 <_drain+0x3fc>
fffffe000751f81c:      	add	x9, x9, #2484
fffffe000751f820:      	ldrh	w11, [x9, #4]
fffffe000751f824:      	ldr	w9, [x9]
fffffe000751f828:      	str	x9, [sp, #448]
fffffe000751f82c:      	adrp	x9, 0xfffffe00084f0000 <_drain+0x47c>
fffffe000751f830:      	add	x9, x9, #1008
fffffe000751f834:      	ldrh	w10, [x9]
fffffe000751f838:      	str	x10, [sp, #400]
fffffe000751f83c:      	ldur	w10, [x9, #2]
fffffe000751f840:      	str	x10, [sp, #432]
fffffe000751f844:      	ldrb	w10, [x9, #44]
fffffe000751f848:      	ldrb	w9, [x9, #1114]
fffffe000751f84c:      	stp	x10, x11, [sp, #408]
fffffe000751f850:      	adrp	x11, 0xfffffe00084f0000 <_drain+0x4a0>
fffffe000751f854:      	add	x11, x11, #1067
fffffe000751f858:      	str	x9, [sp, #424]
fffffe000751f85c:      	adrp	x9, 0xfffffe00084f0000 <_drain+0x4ac>
fffffe000751f860:      	add	x9, x9, #1062
fffffe000751f864:      	ldrb	w10, [x11]
fffffe000751f868:      	ldrh	w9, [x9, #12]
fffffe000751f86c:      	cmp	w10, #255
fffffe000751f870:      	str	w9, [sp, #444]
fffffe000751f874:      	ldrb	w9, [x11, #1]
fffffe000751f878:      	b.ne	0xfffffe000751f884 <_nss_worker_continue+0x292c>
fffffe000751f87c:      	cmp	w9, #255
fffffe000751f880:      	b.eq	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751f884:      	cmp	w10, #255
fffffe000751f888:      	mov	w11, #220
fffffe000751f88c:      	csel	w10, wzr, w10, eq
fffffe000751f890:      	cmp	w9, #255
fffffe000751f894:      	csel	w9, wzr, w9, eq
fffffe000751f898:      	cmp	w10, w9
fffffe000751f89c:      	csel	w10, w10, w9, hi
fffffe000751f8a0:      	adrp	x9, 0xfffffe00084f0000 <_drain+0x4f0>
fffffe000751f8a4:      	cmp	w10, #220
fffffe000751f8a8:      	csel	w10, w10, w11, lo
fffffe000751f8ac:      	ldrh	w9, [x9, #1078]
fffffe000751f8b0:      	str	w10, [sp, #52]
fffffe000751f8b4:      	cbz	x9, 0xfffffe000751f940 <_nss_worker_continue+0x29e8>
fffffe000751f8b8:      	mov	x11, xzr
fffffe000751f8bc:      	str	wzr, [sp, #456]
fffffe000751f8c0:      	b	0xfffffe000751f8d0 <_nss_worker_continue+0x2978>
fffffe000751f8c4:      	add	x11, x11, x10
fffffe000751f8c8:      	cmp	x11, x9
fffffe000751f8cc:      	b.hs	0xfffffe000751f944 <_nss_worker_continue+0x29ec>
fffffe000751f8d0:      	sub	x10, x9, x11
fffffe000751f8d4:      	subs	x12, x10, #2
fffffe000751f8d8:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751f8dc:      	adrp	x10, 0xfffffe00084f0000 <_before+0x8>
fffffe000751f8e0:      	add	x10, x10, #544
fffffe000751f8e4:      	add	x10, x11, x10
fffffe000751f8e8:      	ldrb	w10, [x10, #553]
fffffe000751f8ec:      	cmp	x12, x10
fffffe000751f8f0:      	b.lo	0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751f8f4:      	adrp	x12, 0xfffffe00084f0000 <_before+0x20>
fffffe000751f8f8:      	add	x12, x12, #544
fffffe000751f8fc:      	add	x12, x12, x11
fffffe000751f900:      	add	x11, x11, #2
fffffe000751f904:      	ldrb	w12, [x12, #552]
fffffe000751f908:      	cmp	w12, #42
fffffe000751f90c:      	b.ne	0xfffffe000751f8c4 <_nss_worker_continue+0x296c>
fffffe000751f910:      	cmp	w10, #1
fffffe000751f914:      	b.ne	0xfffffe000751f8c4 <_nss_worker_continue+0x296c>
fffffe000751f918:      	adrp	x12, 0xfffffe00084f0000 <_before+0x44>
fffffe000751f91c:      	add	x12, x12, #544
fffffe000751f920:      	add	x12, x12, x11
fffffe000751f924:      	and	w13, w22, #0xffffffdf
fffffe000751f928:      	ldrb	w12, [x12, #552]
fffffe000751f92c:      	tst	w12, #0x4
fffffe000751f930:      	ubfx	w12, w12, #1, #1
fffffe000751f934:      	csel	w22, w22, w13, eq
fffffe000751f938:      	str	w12, [sp, #456]
fffffe000751f93c:      	b	0xfffffe000751f8c4 <_nss_worker_continue+0x296c>
fffffe000751f940:      	str	wzr, [sp, #456]
fffffe000751f944:      	ldr	x9, [sp, #104]
fffffe000751f948:      	strb	w8, [sp, #505]
fffffe000751f94c:      	ldrh	w8, [sp, #476]
fffffe000751f950:      	add	x4, sp, #544
fffffe000751f954:      	add	x5, sp, #480
fffffe000751f958:      	add	x6, sp, #504
fffffe000751f95c:      	stp	xzr, xzr, [x9]
fffffe000751f960:      	mov	w9, #5
fffffe000751f964:      	strh	w8, [sp, #510]
fffffe000751f968:      	mov	w8, #8
fffffe000751f96c:      	adrp	x0, 0xfffffe000850c000 <_before+0x108>
fffffe000751f970:      	add	x0, x0, #4080
fffffe000751f974:      	strb	w9, [sp, #504]
fffffe000751f978:      	mov	w9, #257
fffffe000751f97c:      	strh	w8, [sp, #512]
fffffe000751f980:      	mov	w8, #2000
fffffe000751f984:      	adrp	x1, 0xfffffe0007c57000 <_ncc_native_read+0xa4>
fffffe000751f988:      	add	x1, x1, #1624
fffffe000751f98c:      	strh	w9, [sp, #506]
fffffe000751f990:      	mov	w9, #30
fffffe000751f994:      	mov	x2, x19
fffffe000751f998:      	adrp	x3, 0xfffffe0008516000 <_nnr_plan>
fffffe000751f99c:      	add	x3, x3, #304
fffffe000751f9a0:      	mov	x7, x26
fffffe000751f9a4:      	strb	w9, [sp, #508]
fffffe000751f9a8:      	str	w8, [sp, #516]
fffffe000751f9ac:      	stp	x21, x20, [sp]
fffffe000751f9b0:      	bl	0xfffffe00079aaad4 <_nst_prepare>
fffffe000751f9b4:      	cbnz	w0, 0xfffffe000751e378 <_nss_worker_continue+0x1420>
fffffe000751f9b8:      	ldp	x12, x10, [sp, #408]
fffffe000751f9bc:      	adrp	x9, 0xfffffe00084f0000 <_before+0xe8>
fffffe000751f9c0:      	add	x9, x9, #572
fffffe000751f9c4:      	ldr	x11, [sp, #448]
fffffe000751f9c8:      	add	x8, x9, #448
fffffe000751f9cc:      	add	x0, sp, #544
fffffe000751f9d0:      	str	xzr, [sp, #544]
fffffe000751f9d4:      	str	xzr, [sp, #504]
fffffe000751f9d8:      	bfi	x11, x10, #32, #16
fffffe000751f9dc:      	adrp	x10, 0xfffffe000850f000 <_nnr_plan+0x28>
fffffe000751f9e0:      	str	d8, [x10, #1168]
fffffe000751f9e4:      	ldr	x10, [sp, #400]
fffffe000751f9e8:      	bfi	x11, x10, #48, #16
fffffe000751f9ec:      	ldr	x10, [sp, #96]
fffffe000751f9f0:      	stp	x8, x12, [x10, #16]
fffffe000751f9f4:      	add	x8, x9, #1551
fffffe000751f9f8:      	str	x11, [x10]
fffffe000751f9fc:      	ldr	x11, [sp, #432]
fffffe000751fa00:      	strh	w22, [x10, #48]
fffffe000751fa04:      	str	x8, [x10, #32]
fffffe000751fa08:      	ldr	x8, [sp, #424]
fffffe000751fa0c:      	str	x11, [x10, #8]
fffffe000751fa10:      	ldr	w11, [sp, #456]
fffffe000751fa14:      	str	wzr, [x10, #52]
fffffe000751fa18:      	str	x8, [x10, #40]
fffffe000751fa1c:      	mov	w8, #10
fffffe000751fa20:      	strb	w11, [x10, #60]
fffffe000751fa24:      	sturh	wzr, [x10, #61]
fffffe000751fa28:      	strh	w8, [x10, #50]
fffffe000751fa2c:      	ldr	w8, [sp, #444]
fffffe000751fa30:      	strb	wzr, [x10, #63]
fffffe000751fa34:      	strh	w8, [x10, #56]
fffffe000751fa38:      	ldr	w8, [sp, #52]
fffffe000751fa3c:      	strb	w8, [x10, #58]
fffffe000751fa40:      	mov	w8, #1
fffffe000751fa44:      	strb	w8, [x10, #59]
fffffe000751fa48:      	str	w8, [x9]
fffffe000751fa4c:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe000751fa50:      	ldr	x0, [sp, #544]
fffffe000751fa54:      	add	x1, sp, #504
fffffe000751fa58:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000751fa5c:      	ldr	x8, [sp, #504]
fffffe000751fa60:      	mov	x10, #63439
fffffe000751fa64:      	movk	x10, #58195, lsl #16
fffffe000751fa68:      	movk	x10, #39845, lsl #32
fffffe000751fa6c:      	lsr	x9, x8, #3
fffffe000751fa70:      	movk	x10, #8388, lsl #48
fffffe000751fa74:      	umulh	x9, x9, x10
fffffe000751fa78:      	lsr	x20, x9, #4
fffffe000751fa7c:      	cmp	x20, x21
fffffe000751fa80:      	b.lo	0xfffffe000751fc00 <_nss_worker_continue+0x2ca8>
fffffe000751fa84:      	adrp	x9, 0xfffffe00084f0000 <_nnr_plan+0x54>
fffffe000751fa88:      	add	x9, x9, #572
fffffe000751fa8c:      	ldur	x9, [x9, #148]
fffffe000751fa90:      	cmp	x20, x9
fffffe000751fa94:      	b.hs	0xfffffe000751fc00 <_nss_worker_continue+0x2ca8>
fffffe000751fa98:      	mov	x11, #63439
fffffe000751fa9c:      	lsr	x9, x9, #3
fffffe000751faa0:      	movk	x11, #58195, lsl #16
fffffe000751faa4:      	ldr	x10, [sp, #88]
fffffe000751faa8:      	movk	x11, #39845, lsl #32
fffffe000751faac:      	adrp	x20, 0xfffffe00084f0000 <_nnr_plan+0x7c>
fffffe000751fab0:      	add	x20, x20, #544
fffffe000751fab4:      	movk	x11, #8388, lsl #48
fffffe000751fab8:      	movi.2d	v2, #0000000000000000
fffffe000751fabc:      	adrp	x1, 0xfffffe000850c000 <_nnr_begin+0x5c>
fffffe000751fac0:      	add	x1, x1, #4080
fffffe000751fac4:      	umulh	x9, x9, x11
fffffe000751fac8:      	ldp	x12, x11, [sp, #72]
fffffe000751facc:      	lsr	x9, x9, #4
fffffe000751fad0:      	add	x3, sp, #544
fffffe000751fad4:      	stp	q2, q2, [x10, #16]
fffffe000751fad8:      	adr	x4, #5784
fffffe000751fadc:      	nop
fffffe000751fae0:      	stp	q2, q2, [x10, #48]
fffffe000751fae4:      	ldp	q0, q1, [x12]
fffffe000751fae8:      	stp	q2, q2, [x10, #80]
fffffe000751faec:      	mov	x5, x20
fffffe000751faf0:      	stp	q2, q2, [x10, #112]
fffffe000751faf4:      	add	x21, x20, #360
fffffe000751faf8:      	str	q2, [x10]
fffffe000751fafc:      	str	x9, [sp, #552]
fffffe000751fb00:      	ldr	x10, [x20, #208]
fffffe000751fb04:      	stp	q0, q1, [x11]
fffffe000751fb08:      	ldr	x9, [x12, #48]
fffffe000751fb0c:      	ldr	q0, [x12, #32]
fffffe000751fb10:      	str	x10, [sp, #544]
fffffe000751fb14:      	mov	w10, #30000
fffffe000751fb18:      	str	x9, [x11, #48]
fffffe000751fb1c:      	ldrh	w9, [x20, #1576]
fffffe000751fb20:      	str	q0, [x11, #32]
fffffe000751fb24:      	mov	w11, #4000
fffffe000751fb28:      	str	w10, [sp, #560]
fffffe000751fb2c:      	ldrb	w10, [x1, #204]
fffffe000751fb30:      	sturh	w9, [x27, #83]
fffffe000751fb34:      	ldrb	w9, [x1, #207]
fffffe000751fb38:      	str	w11, [sp, #632]
fffffe000751fb3c:      	add	x11, x20, #400
fffffe000751fb40:      	strb	w10, [sp, #624]
fffffe000751fb44:      	mov	w10, #1
fffffe000751fb48:      	strb	w9, [sp, #636]
fffffe000751fb4c:      	add	x9, x20, #432
fffffe000751fb50:      	ldrh	w12, [x20, #1592]
fffffe000751fb54:      	strb	w10, [sp, #625]
fffffe000751fb58:      	mov	w10, #4
fffffe000751fb5c:      	stp	x11, x9, [x27, #96]
fffffe000751fb60:      	mov	x11, #13531
fffffe000751fb64:      	movk	x11, #55222, lsl #16
fffffe000751fb68:      	add	x9, x20, #552
fffffe000751fb6c:      	strb	w10, [sp, #629]
fffffe000751fb70:      	ldrb	w10, [x1, #208]
fffffe000751fb74:      	movk	x11, #56962, lsl #32
fffffe000751fb78:      	movk	x11, #17179, lsl #48
fffffe000751fb7c:      	str	x9, [x27, #128]
fffffe000751fb80:      	strb	w10, [sp, #637]
fffffe000751fb84:      	add	x10, x20, #1594
fffffe000751fb88:      	umulh	x8, x8, x11
fffffe000751fb8c:      	adr	x11, #5556
fffffe000751fb90:      	nop
fffffe000751fb94:      	ldp	x2, x0, [sp, #56]
fffffe000751fb98:      	stp	x10, x12, [x27, #112]
fffffe000751fb9c:      	lsr	x6, x8, #18
fffffe000751fba0:      	ldrh	w10, [x20, #534]
fffffe000751fba4:      	stp	x10, x11, [x27, #136]
fffffe000751fba8:      	bl	0xfffffe00079b5774 <_nwn_begin>
fffffe000751fbac:      	mov	w26, w0
fffffe000751fbb0:      	add	x0, sp, #544
fffffe000751fbb4:      	mov	w1, #160
fffffe000751fbb8:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751fbbc:      	mov	x0, x21
fffffe000751fbc0:      	mov	w1, #104
fffffe000751fbc4:      	bl	0xfffffe00079a6d2c <_niwj_clear>
fffffe000751fbc8:      	cmp	w26, #1
fffffe000751fbcc:      	cset	w8, eq
fffffe000751fbd0:      	stp	wzr, w8, [x20, #8]
fffffe000751fbd4:      	b.ne	0xfffffe000751fc24 <_nss_worker_continue+0x2ccc>
fffffe000751fbd8:      	bl	0xfffffe0007520f04 <_njs_snapshot_locked>
fffffe000751fbdc:      	ldr	x0, [x24, #2456]
fffffe000751fbe0:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fbe4:      	ldr	x0, [x24, #2456]
fffffe000751fbe8:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000751fbec:      	mov	w0, #1
fffffe000751fbf0:      	bl	0xfffffe0007512938 <_njs_trace_locked>
fffffe000751fbf4:      	ldr	x0, [x24, #2456]
fffffe000751fbf8:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fbfc:      	b	0xfffffe000751e388 <_nss_worker_continue+0x1430>
fffffe000751fc00:      	ldr	x0, [x24, #2456]
fffffe000751fc04:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fc08:      	adrp	x8, 0xfffffe00084f0000 <_nbc_caps_decode+0x28>
fffffe000751fc0c:      	mov	w9, #9
fffffe000751fc10:      	ldr	x8, [x8, #720]
fffffe000751fc14:      	cmp	x20, x8
fffffe000751fc18:      	mov	w8, #2
fffffe000751fc1c:      	csel	w0, w8, w9, lo
fffffe000751fc20:      	b	0xfffffe000751e384 <_nss_worker_continue+0x142c>
fffffe000751fc24:      	ldr	x0, [x24, #2456]
fffffe000751fc28:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fc2c:      	mov	w0, #2
fffffe000751fc30:      	bl	0xfffffe0007520b7c <_njs_early_finish>
fffffe000751fc34:      	adrp	x9, 0xfffffe000850d000 <_nbc_caps_decode+0xc8>
fffffe000751fc38:      	add	x9, x9, #36
fffffe000751fc3c:      	ldr	w8, [x9]
fffffe000751fc40:      	ldr	w9, [x9, #40]
fffffe000751fc44:      	orr	w8, w8, w9
fffffe000751fc48:      	cbz	w8, 0xfffffe000751e388 <_nss_worker_continue+0x1430>
fffffe000751fc4c:      	b	0xfffffe000751fc64 <_nss_worker_continue+0x2d0c>
fffffe000751fc50:      	bl	0xfffffe0007a024b4 <___stack_chk_fail>
fffffe000751fc54:      	bl	0xfffffe0007b4e45c <_nss_worker_continue.cold.1>
fffffe000751fc58:      	b	0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
fffffe000751fc5c:      	bl	0xfffffe0007b4e478 <_nss_worker_continue.cold.2>
fffffe000751fc60:      	bl	0xfffffe0007b4e494 <_nss_worker_continue.cold.3>
fffffe000751fc64:      	bl	0xfffffe0007b4e624 <_nss_worker_continue.cold.4>
fffffe000751fc68:      	mov	x0, x22
fffffe000751fc6c:      	bl	0xfffffe0007b4e698 <_nss_worker_continue.cold.5>
fffffe000751fc70:      	add	x0, sp, #504
fffffe000751fc74:      	add	x1, sp, #544
fffffe000751fc78:      	mov	x2, x22
fffffe000751fc7c:      	bl	0xfffffe0007b4e6d4 <_nss_worker_continue.cold.6>
fffffe000751fc80:      	ldr	x0, [x24, #2456]
fffffe000751fc84:      	adrp	x8, 0xfffffe000850d000 <_nbc_caps_decode+0x118>
fffffe000751fc88:      	add	x8, x8, #20
fffffe000751fc8c:      	stp	w21, w22, [x8]
fffffe000751fc90:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe000751fc94:      	b	0xfffffe000751fc60 <_nss_worker_continue+0x2d08>
