
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007518f58 <_nss_worker_continue>:
fffffe0007518f58:      	stp	d9, d8, [sp, #-112]!
fffffe0007518f5c:      	stp	x28, x27, [sp, #16]
fffffe0007518f60:      	stp	x26, x25, [sp, #32]
fffffe0007518f64:      	stp	x24, x23, [sp, #48]
fffffe0007518f68:      	stp	x22, x21, [sp, #64]
fffffe0007518f6c:      	stp	x20, x19, [sp, #80]
fffffe0007518f70:      	stp	x29, x30, [sp, #96]
fffffe0007518f74:      	add	x29, sp, #96
fffffe0007518f78:      	sub	sp, sp, #768
fffffe0007518f7c:      	adrp	x8, 0xfffffe0007c90000 <_nss_worker_continue+0x1e04>
fffffe0007518f80:      	adrp	x0, 0xfffffe0007096000 <_nppa_source_owner+0x244>
fffffe0007518f84:      	add	x0, x0, #83
fffffe0007518f88:      	add	x1, sp, #536
fffffe0007518f8c:      	mov	w2, #4
fffffe0007518f90:      	nop
fffffe0007518f94:      	ldr	x8, [x8, #3376]
fffffe0007518f98:      	stur	x8, [x29, #-120]
fffffe0007518f9c:      	str	wzr, [sp, #536]
fffffe0007518fa0:      	bl	0xfffffe0007ae89bc <_PE_parse_boot_argn>
fffffe0007518fa4:      	ldr	w8, [sp, #536]
fffffe0007518fa8:      	cmp	w8, #1
fffffe0007518fac:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007518fb0:      	adrp	x24, 0xfffffe00084d1000 <_njs_event+0x274>
fffffe0007518fb4:      	add	x27, sp, #544
fffffe0007518fb8:      	movi.2d	v0, #0000000000000000
fffffe0007518fbc:      	str	wzr, [sp, #512]
fffffe0007518fc0:      	str	xzr, [sp, #504]
fffffe0007518fc4:      	ldr	x8, [x24, #2456]
fffffe0007518fc8:      	stur	wzr, [x27, #191]
fffffe0007518fcc:      	str	xzr, [sp, #480]
fffffe0007518fd0:      	stp	q0, q0, [x27, #64]
fffffe0007518fd4:      	stp	q0, q0, [x27, #96]
fffffe0007518fd8:      	stp	q0, q0, [x27, #128]
fffffe0007518fdc:      	stp	q0, q0, [x27, #160]
fffffe0007518fe0:      	stp	q0, q0, [sp, #544]
fffffe0007518fe4:      	stp	q0, q0, [sp, #576]
fffffe0007518fe8:      	cbz	x8, 0xfffffe0007519028 <_nss_worker_continue+0xd0>
fffffe0007518fec:      	adrp	x8, 0xfffffe0007c90000 <_nss_worker_continue+0x1e74>
fffffe0007518ff0:      	nop
fffffe0007518ff4:      	ldur	x9, [x29, #-120]
fffffe0007518ff8:      	ldr	x8, [x8, #3376]
fffffe0007518ffc:      	cmp	x8, x9
fffffe0007519000:      	b.ne	0xfffffe000751bc54 <_nss_worker_continue+0x2cfc>
fffffe0007519004:      	add	sp, sp, #768
fffffe0007519008:      	ldp	x29, x30, [sp, #96]
fffffe000751900c:      	ldp	x20, x19, [sp, #80]
fffffe0007519010:      	ldp	x22, x21, [sp, #64]
fffffe0007519014:      	ldp	x24, x23, [sp, #48]
fffffe0007519018:      	ldp	x26, x25, [sp, #32]
fffffe000751901c:      	ldp	x28, x27, [sp, #16]
fffffe0007519020:      	ldp	d9, d8, [sp], #112
fffffe0007519024:      	ret
fffffe0007519028:      	adrp	x25, 0xfffffe000850d000 <_njs_snapshot_locked+0xf0>
fffffe000751902c:      	add	x25, x25, #3176
fffffe0007519030:      	ldr	x28, [x25]
fffffe0007519034:      	bl	0xfffffe00074aa6dc <_current_thread>
fffffe0007519038:      	adrp	x23, 0xfffffe0008510000 <_njs_snapshot_locked+0x10c>
fffffe000751903c:      	add	x23, x23, #184
fffffe0007519040:      	ldr	d0, [x25, #16]
fffffe0007519044:      	ldr	d1, [x23]
fffffe0007519048:      	mov.d	v0[1], v1[0]
fffffe000751904c:      	cmtst.4s	v1, v0, v0
fffffe0007519050:      	cmeq.4s	v0, v0, #0
fffffe0007519054:      	xtn.4h	v0, v0
fffffe0007519058:      	xtn.4h	v1, v1
fffffe000751905c:      	mov.h	v0[2], v1[2]
fffffe0007519060:      	umov.h	w8, v0[1]
fffffe0007519064:      	umov.h	w9, v0[2]
fffffe0007519068:      	umov.h	w10, v0[0]
fffffe000751906c:      	umov.h	w11, v0[3]
fffffe0007519070:      	and	w8, w8, #0x1
fffffe0007519074:      	and	w9, w9, #0x1
fffffe0007519078:      	bfi	w10, w8, #1, #1
fffffe000751907c:      	mov	w8, #15
fffffe0007519080:      	bfi	w10, w9, #2, #1
fffffe0007519084:      	bfi	w10, w11, #3, #29
fffffe0007519088:      	bics	wzr, w8, w10
fffffe000751908c:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519090:      	ldr	w8, [x25, #12]
fffffe0007519094:      	cbz	w8, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519098:      	ldr	w8, [x23, #6164]
fffffe000751909c:      	cbz	w8, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075190a0:      	adrp	x8, 0xfffffe000850f000 <_njs_snapshot_locked+0x170>
fffffe00075190a4:      	add	x8, x8, #3752
fffffe00075190a8:      	ldr	w9, [x8, #4]
fffffe00075190ac:      	cbnz	w9, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075190b0:      	cmp	x28, x0
fffffe00075190b4:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075190b8:      	ldr	w8, [x8]
fffffe00075190bc:      	cbz	w8, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075190c0:      	adrp	x19, 0xfffffe000850d000 <_njs_snapshot_locked+0x188>
fffffe00075190c4:      	add	x19, x19, #3208
fffffe00075190c8:      	mov	x0, x19
fffffe00075190cc:      	bl	0xfffffe0007996cac <_nct_done_receipt>
fffffe00075190d0:      	ldr	d0, [x19, #48]
fffffe00075190d4:      	add	x8, x19, #56
fffffe00075190d8:      	adrp	x9, 0xfffffe000700a000 <_nppa_source_owner+0x168>
fffffe00075190dc:      	cmp	w0, #0
fffffe00075190e0:      	cset	w10, ne
fffffe00075190e4:      	ld1.s	{ v0 }[2], [x8]
fffffe00075190e8:      	add	x8, x19, #68
fffffe00075190ec:      	ldr	q1, [x9, #1632]
fffffe00075190f0:      	ldr	w9, [x19, #72]
fffffe00075190f4:      	ld1.s	{ v0 }[3], [x8]
fffffe00075190f8:      	ldr	w8, [x19, #44]
fffffe00075190fc:      	cmeq.4s	v0, v0, v1
fffffe0007519100:      	cmp	w8, #7
fffffe0007519104:      	cset	w8, eq
fffffe0007519108:      	cmp	w9, #26
fffffe000751910c:      	mvn.16b	v0, v0
fffffe0007519110:      	cset	w9, eq
fffffe0007519114:      	xtn.4h	v0, v0
fffffe0007519118:      	umaxv.4h	h0, v0
fffffe000751911c:      	fmov	w11, s0
fffffe0007519120:      	bic	w8, w8, w11
fffffe0007519124:      	and	w8, w8, w10
fffffe0007519128:      	and	w8, w8, w9
fffffe000751912c:      	cmp	w8, #1
fffffe0007519130:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519134:      	ldr	w8, [x19, #64]
fffffe0007519138:      	cmp	w8, #26
fffffe000751913c:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519140:      	ldr	x8, [x19]
fffffe0007519144:      	adrp	x9, 0xfffffe000850b000 <_njs_notice+0x78>
fffffe0007519148:      	add	x9, x9, #1744
fffffe000751914c:      	cmp	x8, x9
fffffe0007519150:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519154:      	ldr	x8, [x19, #8]
fffffe0007519158:      	adrp	x9, 0xfffffe000850c000 <_njs_notice+0x90>
fffffe000751915c:      	add	x9, x9, #2120
fffffe0007519160:      	cmp	x8, x9
fffffe0007519164:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519168:      	ldr	x8, [x19, #16]
fffffe000751916c:      	adrp	x9, 0xfffffe0007c53000 <_nss_worker_continue+0x1efc>
fffffe0007519170:      	add	x9, x9, #1624
fffffe0007519174:      	cmp	x8, x9
fffffe0007519178:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe000751917c:      	adrp	x15, 0xfffffe0007c90000 <_nss_worker_continue+0x2000>
fffffe0007519180:      	adrp	x8, 0xfffffe000850c000 <_njs_rng+0x8>
fffffe0007519184:      	add	x8, x8, #2144
fffffe0007519188:      	adrp	x11, 0xfffffe0007c53000 <_nss_worker_continue+0x1f18>
fffffe000751918c:      	add	x11, x11, #1636
fffffe0007519190:      	adrp	x16, 0xfffffe0008512000 <_njs_peer>
fffffe0007519194:      	add	x16, x16, #136
fffffe0007519198:      	add	x15, x15, #584
fffffe000751919c:      	ldur	d0, [x8, #4]
fffffe00075191a0:      	ldr	s3, [x11, #1864]
fffffe00075191a4:      	add	x10, x15, #16
fffffe00075191a8:      	add	x9, x15, #300
fffffe00075191ac:      	ldr	q2, [x11, #3408]
fffffe00075191b0:      	ldur	d4, [x16, #12]
fffffe00075191b4:      	ld1.s	{ v0 }[1], [x10]
fffffe00075191b8:      	add	x10, x11, #3400
fffffe00075191bc:      	ldr	d1, [x9]
fffffe00075191c0:      	add	x9, x15, #60
fffffe00075191c4:      	ld1.s	{ v3 }[1], [x10]
fffffe00075191c8:      	adrp	x10, 0xfffffe000700a000 <_nppa_source_owner+0x258>
fffffe00075191cc:      	ld1.s	{ v1 }[2], [x11]
fffffe00075191d0:      	add	x11, x11, #32
fffffe00075191d4:      	ld1.s	{ v0 }[2], [x9]
fffffe00075191d8:      	add	x9, x15, #280
fffffe00075191dc:      	mov.d	v3[1], v2[0]
fffffe00075191e0:      	ld1.s	{ v1 }[3], [x11]
fffffe00075191e4:      	ld1.s	{ v0 }[3], [x9]
fffffe00075191e8:      	adrp	x9, 0xfffffe000700a000 <_nppa_source_owner+0x278>
fffffe00075191ec:      	ext.16b	v2, v2, v4, #8
fffffe00075191f0:      	ldr	q4, [x10, #1648]
fffffe00075191f4:      	cmeq.4s	v5, v1, #0
fffffe00075191f8:      	cmeq.4s	v0, v0, #0
fffffe00075191fc:      	cmeq.4s	v3, v3, v4
fffffe0007519200:      	cmeq.4s	v4, v2, #0
fffffe0007519204:      	cmtst.4s	v2, v2, v2
fffffe0007519208:      	uzp1.8h	v0, v0, v5
fffffe000751920c:      	uzp1.8h	v4, v3, v4
fffffe0007519210:      	mvn.16b	v3, v3
fffffe0007519214:      	cmtst.4s	v1, v1, v1
fffffe0007519218:      	uzp1.8h	v2, v3, v2
fffffe000751921c:      	uzp1.16b	v3, v0, v4
fffffe0007519220:      	ldr	q0, [x9, #1664]
fffffe0007519224:      	xtn2.8h	v1, v1
fffffe0007519228:      	uzp1.16b	v4, v1, v2
fffffe000751922c:      	tbl.16b	v0, { v3, v4 }, v0
fffffe0007519230:      	umov.b	w9, v0[1]
fffffe0007519234:      	umov.b	w11, v0[2]
fffffe0007519238:      	umov.b	w10, v0[0]
fffffe000751923c:      	umov.b	w12, v0[3]
fffffe0007519240:      	umov.b	w13, v0[4]
fffffe0007519244:      	umov.b	w14, v0[5]
fffffe0007519248:      	and	w9, w9, #0x1
fffffe000751924c:      	and	w11, w11, #0x1
fffffe0007519250:      	and	w10, w10, #0x1
fffffe0007519254:      	and	w12, w12, #0x1
fffffe0007519258:      	and	w13, w13, #0x1
fffffe000751925c:      	and	w14, w14, #0x1
fffffe0007519260:      	bfi	w10, w9, #1, #1
fffffe0007519264:      	umov.b	w9, v0[6]
fffffe0007519268:      	bfi	w10, w11, #2, #1
fffffe000751926c:      	umov.b	w11, v0[7]
fffffe0007519270:      	bfi	w10, w12, #3, #1
fffffe0007519274:      	umov.b	w12, v0[8]
fffffe0007519278:      	bfi	w10, w13, #4, #1
fffffe000751927c:      	umov.b	w13, v0[9]
fffffe0007519280:      	and	w9, w9, #0x1
fffffe0007519284:      	bfi	w10, w14, #5, #1
fffffe0007519288:      	umov.b	w14, v0[10]
fffffe000751928c:      	and	w11, w11, #0x1
fffffe0007519290:      	orr	w9, w10, w9, lsl #6
fffffe0007519294:      	umov.b	w10, v0[11]
fffffe0007519298:      	and	w12, w12, #0x1
fffffe000751929c:      	orr	w9, w9, w11, lsl #7
fffffe00075192a0:      	umov.b	w11, v0[12]
fffffe00075192a4:      	and	w13, w13, #0x1
fffffe00075192a8:      	orr	w9, w9, w12, lsl #8
fffffe00075192ac:      	umov.b	w12, v0[13]
fffffe00075192b0:      	and	w14, w14, #0x1
fffffe00075192b4:      	orr	w9, w9, w13, lsl #9
fffffe00075192b8:      	umov.b	w13, v0[14]
fffffe00075192bc:      	and	w10, w10, #0x1
fffffe00075192c0:      	orr	w9, w9, w14, lsl #10
fffffe00075192c4:      	and	w11, w11, #0x1
fffffe00075192c8:      	orr	w9, w9, w10, lsl #11
fffffe00075192cc:      	and	w10, w12, #0x1
fffffe00075192d0:      	umov.b	w12, v0[15]
fffffe00075192d4:      	orr	w9, w9, w11, lsl #12
fffffe00075192d8:      	and	w11, w13, #0x1
fffffe00075192dc:      	orr	w9, w9, w10, lsl #13
fffffe00075192e0:      	mov	w10, #65535
fffffe00075192e4:      	orr	w9, w9, w11, lsl #14
fffffe00075192e8:      	orr	w9, w9, w12, lsl #15
fffffe00075192ec:      	bics	wzr, w10, w9
fffffe00075192f0:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075192f4:      	add	x9, x16, #1832
fffffe00075192f8:      	ldr	q0, [x9]
fffffe00075192fc:      	cmtst.4s	v1, v0, v0
fffffe0007519300:      	cmeq.4s	v0, v0, #0
fffffe0007519304:      	xtn.4h	v1, v1
fffffe0007519308:      	xtn.4h	v0, v0
fffffe000751930c:      	rev32.4h	v1, v1
fffffe0007519310:      	trn2.4h	v0, v1, v0
fffffe0007519314:      	umov.h	w9, v0[1]
fffffe0007519318:      	umov.h	w10, v0[2]
fffffe000751931c:      	umov.h	w11, v0[0]
fffffe0007519320:      	umov.h	w12, v0[3]
fffffe0007519324:      	and	w9, w9, #0x1
fffffe0007519328:      	and	w10, w10, #0x1
fffffe000751932c:      	bfi	w11, w9, #1, #1
fffffe0007519330:      	bfi	w11, w10, #2, #1
fffffe0007519334:      	bfi	w11, w12, #3, #29
fffffe0007519338:      	and	w9, w11, #0xf
fffffe000751933c:      	cmp	w9, #15
fffffe0007519340:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519344:      	adrp	x9, 0xfffffe000850b000 <_ncc_cold_read+0x14>
fffffe0007519348:      	add	x9, x9, #1784
fffffe000751934c:      	ldr	w8, [x8]
fffffe0007519350:      	ldp	w10, w9, [x9]
fffffe0007519354:      	orr	w9, w10, w9
fffffe0007519358:      	orr	w8, w9, w8
fffffe000751935c:      	cbnz	w8, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519360:      	adrp	x8, 0xfffffe000850d000 <_ncc_cold_read+0x38>
fffffe0007519364:      	ldr	x8, [x8, #3232]
fffffe0007519368:      	cmp	x8, x15
fffffe000751936c:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519370:      	ldr	w8, [x16]
fffffe0007519374:      	cbnz	w8, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519378:      	ldrb	w8, [x16, #1897]
fffffe000751937c:      	cmp	w8, #7
fffffe0007519380:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519384:      	adrp	x8, 0xfffffe0008512000 <_ncc_cold_read+0x70>
fffffe0007519388:      	add	x8, x8, #2000
fffffe000751938c:      	ldrb	w9, [x8, #32]
fffffe0007519390:      	ldrb	w8, [x8]
fffffe0007519394:      	cmp	w9, w8
fffffe0007519398:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe000751939c:      	adrp	x8, 0xfffffe0008512000 <_ncc_cold_read+0x88>
fffffe00075193a0:      	ldr	w8, [x8, #1988]
fffffe00075193a4:      	cmp	w8, #16
fffffe00075193a8:      	b.hi	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075193ac:      	adrp	x9, 0xfffffe0008512000 <_ncc_cold_read+0x98>
fffffe00075193b0:      	ldr	w9, [x9, #1996]
fffffe00075193b4:      	subs	w10, w9, w8
fffffe00075193b8:      	b.lo	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075193bc:      	adrp	x9, 0xfffffe0008512000 <_ncc_cold_read+0xa8>
fffffe00075193c0:      	ldr	w9, [x9, #1992]
fffffe00075193c4:      	cmp	w9, w10
fffffe00075193c8:      	b.hi	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075193cc:      	adrp	x11, 0xfffffe0008512000 <_ncc_cold_read+0xb8>
fffffe00075193d0:      	add	x11, x11, #2008
fffffe00075193d4:      	ldp	x11, x10, [x11]
fffffe00075193d8:      	cmp	x10, x11
fffffe00075193dc:      	b.lo	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075193e0:      	adrp	x12, 0xfffffe0008512000 <_ncc_cold_read+0xcc>
fffffe00075193e4:      	ldr	x12, [x12, #2024]
fffffe00075193e8:      	cmp	x12, x11
fffffe00075193ec:      	b.ls	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075193f0:      	cmp	x10, x12
fffffe00075193f4:      	b.hs	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe00075193f8:      	adrp	x10, 0xfffffe0008512000 <_ncc_cold_read+0xe4>
fffffe00075193fc:      	ldr	w10, [x10, #1984]
fffffe0007519400:      	cbz	w10, 0xfffffe0007519410 <_nss_worker_continue+0x4b8>
fffffe0007519404:      	cmp	w9, #0
fffffe0007519408:      	ccmp	w8, #16, #0, ne
fffffe000751940c:      	b	0xfffffe0007519414 <_nss_worker_continue+0x4bc>
fffffe0007519410:      	cmp	w9, #0
fffffe0007519414:      	cset	w8, eq
fffffe0007519418:      	cbz	w8, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe000751941c:      	adrp	x8, 0xfffffe0007c53000 <_nss_worker_continue+0x21ac>
fffffe0007519420:      	ldr	w8, [x8, #3544]
fffffe0007519424:      	cmp	w8, #31
fffffe0007519428:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe000751942c:      	adrp	x26, 0xfffffe0008511000 <_ncc_cold_read+0x114>
fffffe0007519430:      	add	x26, x26, #2264
fffffe0007519434:      	add	x1, x26, #40
fffffe0007519438:      	add	x2, sp, #504
fffffe000751943c:      	mov	x0, x26
fffffe0007519440:      	bl	0xfffffe000799c070 <_ncfg_address>
fffffe0007519444:      	cbnz	w0, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519448:      	ldr	w8, [sp, #512]
fffffe000751944c:      	cmp	w8, #1
fffffe0007519450:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519454:      	add	x1, x26, #40
fffffe0007519458:      	add	x2, sp, #544
fffffe000751945c:      	add	x4, sp, #480
fffffe0007519460:      	mov	x0, x26
fffffe0007519464:      	mov	w3, #39
fffffe0007519468:      	bl	0xfffffe000799cff0 <_ncfg_channels>
fffffe000751946c:      	cbnz	w0, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe0007519470:      	add	x8, sp, #251
fffffe0007519474:      	adrp	x11, 0xfffffe00084d1000 <_ncc_cold_read+0x5c>
fffffe0007519478:      	add	x11, x11, #2484
fffffe000751947c:      	ldrh	w10, [sp, #504]
fffffe0007519480:      	ldr	x9, [sp, #480]
fffffe0007519484:      	ldur	s0, [x8, #255]
fffffe0007519488:      	adrp	x8, 0xfffffe0008511000 <_ncc_native_check+0x40>
fffffe000751948c:      	add	x8, x8, #2293
fffffe0007519490:      	strh	w10, [x11]
fffffe0007519494:      	ld1.s	{ v0 }[1], [x8]
fffffe0007519498:      	ldr	w8, [x11, #12]
fffffe000751949c:      	stur	d0, [x11, #2]
fffffe00075194a0:      	cbz	x9, 0xfffffe0007519508 <_nss_worker_continue+0x5b0>
fffffe00075194a4:      	mov	x10, xzr
fffffe00075194a8:      	add	x11, sp, #544
fffffe00075194ac:      	adrp	x16, 0xfffffe00084d1000 <_ncc_cold_read+0x94>
fffffe00075194b0:      	add	x16, x16, #2484
fffffe00075194b4:      	b	0xfffffe00075194c4 <_nss_worker_continue+0x56c>
fffffe00075194b8:      	add	w10, w10, #1
fffffe00075194bc:      	cmp	x9, x10
fffffe00075194c0:      	b.ls	0xfffffe0007519508 <_nss_worker_continue+0x5b0>
fffffe00075194c4:      	add	x12, x10, x10, lsl #2
fffffe00075194c8:      	add	x12, x11, x12
fffffe00075194cc:      	ldrb	w13, [x12, #2]
fffffe00075194d0:      	cbz	w13, 0xfffffe00075194b8 <_nss_worker_continue+0x560>
fffffe00075194d4:      	ldrb	w13, [x12, #3]
fffffe00075194d8:      	cbnz	w13, 0xfffffe00075194b8 <_nss_worker_continue+0x560>
fffffe00075194dc:      	add	x13, x10, x10, lsl #2
fffffe00075194e0:      	add	w14, w8, #1
fffffe00075194e4:      	add	x13, x11, x13
fffffe00075194e8:      	add	x15, x16, w8, uxtw #1
fffffe00075194ec:      	ldrb	w12, [x12]
fffffe00075194f0:      	mov	w8, w14
fffffe00075194f4:      	str	w14, [x16, #12]
fffffe00075194f8:      	ldrb	w13, [x13, #1]
fffffe00075194fc:      	strb	w12, [x15, #16]
fffffe0007519500:      	strb	w13, [x15, #17]
fffffe0007519504:      	b	0xfffffe00075194b8 <_nss_worker_continue+0x560>
fffffe0007519508:      	cbz	w8, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe000751950c:      	adrp	x9, 0xfffffe0008511000 <_ncc_native_check+0xc4>
fffffe0007519510:      	ldr	w9, [x9, #2256]
fffffe0007519514:      	cmp	w8, w9
fffffe0007519518:      	b.ne	0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe000751951c:      	adrp	x8, 0xfffffe0008512000 <_ncc_native_check+0xd8>
fffffe0007519520:      	adrp	x20, 0xfffffe00084d1000 <_ncc_cold_read+0x108>
fffffe0007519524:      	add	x20, x20, #2456
fffffe0007519528:      	ldrb	w8, [x8, #2000]
fffffe000751952c:      	strb	w8, [x20, #24]
fffffe0007519530:      	bl	0xfffffe0007a131a4 <_IOLockAlloc>
fffffe0007519534:      	str	x0, [x20]
fffffe0007519538:      	cbz	x0, 0xfffffe0007518fec <_nss_worker_continue+0x94>
fffffe000751953c:      	bl	0xfffffe0007a131a4 <_IOLockAlloc>
fffffe0007519540:      	adrp	x20, 0xfffffe00084ec000 <_ncc_native_check+0x64>
fffffe0007519544:      	add	x20, x20, #544
fffffe0007519548:      	str	x0, [x20]
fffffe000751954c:      	adrp	x0, 0xfffffe0008512000 <_ncc_native_check+0x108>
fffffe0007519550:      	add	x0, x0, #1968
fffffe0007519554:      	bl	0xfffffe000751c934 <_njs_cache_scan_locked>
fffffe0007519558:      	mov	w8, #1
fffffe000751955c:      	adrp	x9, 0xfffffe00084d1000 <_ncc_native_check+0x14>
fffffe0007519560:      	add	x9, x9, #2464
fffffe0007519564:      	adrp	x0, 0xfffffe0007096000 <_nppa_xnu_word+0x8>
fffffe0007519568:      	add	x0, x0, #3144
fffffe000751956c:      	str	w8, [x20, #3272]
fffffe0007519570:      	stlr	w8, [x9]
fffffe0007519574:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007519578:      	bl	0xfffffe0007ae75c0 <_nord2_wifi_ios_backend_ready>
fffffe000751957c:      	add	x8, sp, #480
fffffe0007519580:      	add	x9, sp, #504
fffffe0007519584:      	orr	x11, x8, #0x6
fffffe0007519588:      	orr	x8, x9, #0x4
fffffe000751958c:      	add	x10, sp, #544
fffffe0007519590:      	adrp	x12, 0xfffffe00084ec000 <_ncc_native_check+0xb4>
fffffe0007519594:      	add	x12, x12, #904
fffffe0007519598:      	adrp	x9, 0xfffffe00084ec000 <_ncc_native_check+0xbc>
fffffe000751959c:      	add	x9, x9, #560
fffffe00075195a0:      	stp	x8, x11, [sp, #104]
fffffe00075195a4:      	add	x8, x10, #16
fffffe00075195a8:      	adrp	x11, 0xfffffe00084ec000 <_ncc_native_check+0xcc>
fffffe00075195ac:      	add	x11, x11, #552
fffffe00075195b0:      	adrp	x13, 0xfffffe00084ec000 <_ncc_native_check+0xd4>
fffffe00075195b4:      	add	x13, x13, #596
fffffe00075195b8:      	add	x11, x11, #19, lsl #12
fffffe00075195bc:      	str	x8, [sp, #88]
fffffe00075195c0:      	add	x8, x10, #24
fffffe00075195c4:      	adrp	x10, 0xfffffe00084ec000 <_ncc_native_check+0xe8>
fffffe00075195c8:      	add	x10, x10, #572
fffffe00075195cc:      	movi.2s	v8, #1
fffffe00075195d0:      	str	x8, [sp, #80]
fffffe00075195d4:      	mov	w8, #4344
fffffe00075195d8:      	add	x8, x12, x8
fffffe00075195dc:      	str	x8, [sp, #264]
fffffe00075195e0:      	add	x8, x10, #19, lsl #12
fffffe00075195e4:      	add	x8, x8, #3876
fffffe00075195e8:      	str	x8, [sp, #96]
fffffe00075195ec:      	add	x8, x20, #19, lsl #12
fffffe00075195f0:      	add	x10, x8, #3904
fffffe00075195f4:      	add	x8, x20, #19, lsl #12
fffffe00075195f8:      	add	x8, x8, #240
fffffe00075195fc:      	stp	x8, x10, [sp, #64]
fffffe0007519600:      	add	x8, x20, #19, lsl #12
fffffe0007519604:      	add	x8, x8, #3888
fffffe0007519608:      	adrp	x10, 0xfffffe00084ec000 <_ncc_native_check+0x12c>
fffffe000751960c:      	add	x10, x10, #604
fffffe0007519610:      	str	x8, [sp, #56]
fffffe0007519614:      	add	x8, x10, #19, lsl #12
fffffe0007519618:      	add	x8, x8, #180
fffffe000751961c:      	str	x8, [sp, #344]
fffffe0007519620:      	add	x8, x10, #19, lsl #12
fffffe0007519624:      	add	x8, x8, #3908
fffffe0007519628:      	str	x8, [sp, #336]
fffffe000751962c:      	add	x8, x10, #22, lsl #12
fffffe0007519630:      	add	x8, x8, #3732
fffffe0007519634:      	str	x8, [sp, #328]
fffffe0007519638:      	add	x8, x9, #19, lsl #12
fffffe000751963c:      	add	x8, x8, #224
fffffe0007519640:      	adrp	x9, 0xfffffe00084ec000 <_ncc_native_check+0x164>
fffffe0007519644:      	add	x9, x9, #556
fffffe0007519648:      	str	x8, [sp, #392]
fffffe000751964c:      	add	x8, x9, #19, lsl #12
fffffe0007519650:      	add	x10, x8, #1660
fffffe0007519654:      	add	x8, x9, #19, lsl #12
fffffe0007519658:      	add	x8, x8, #3956
fffffe000751965c:      	add	x9, x9, #22, lsl #12
fffffe0007519660:      	add	x9, x9, #3780
fffffe0007519664:      	stp	x8, x10, [sp, #304]
fffffe0007519668:      	adrp	x8, 0xfffffe000700a000 <_nppa_xnu_smc+0x18c>
fffffe000751966c:      	adrp	x10, 0xfffffe000700a000 <_nppa_xnu_smc+0x190>
fffffe0007519670:      	str	x9, [sp, #296]
fffffe0007519674:      	adrp	x9, 0xfffffe0007009000 <_nppa_xnu_smc+0x194>
fffffe0007519678:      	ldr	q0, [x8, #1712]
fffffe000751967c:      	nop
fffffe0007519680:      	str	q0, [sp, #192]
fffffe0007519684:      	ldr	q0, [x10, #1728]
fffffe0007519688:      	nop
fffffe000751968c:      	str	q0, [sp, #176]
fffffe0007519690:      	ldr	q0, [x9, #944]
fffffe0007519694:      	adrp	x9, 0xfffffe000700a000 <_nppa_xnu_map+0x8>
fffffe0007519698:      	str	q0, [sp, #160]
fffffe000751969c:      	ldr	q0, [x8, #1744]
fffffe00075196a0:      	add	x8, x11, #232
fffffe00075196a4:      	ldr	d9, [x9, #1528]
fffffe00075196a8:      	adrp	x9, 0xfffffe00084ec000 <_ncc_native_read+0x30>
fffffe00075196ac:      	add	x9, x9, #608
fffffe00075196b0:      	add	x9, x9, #19, lsl #12
fffffe00075196b4:      	str	q0, [sp, #144]
fffffe00075196b8:      	str	x8, [sp, #272]
fffffe00075196bc:      	add	x8, x13, #19, lsl #12
fffffe00075196c0:      	add	x8, x8, #188
fffffe00075196c4:      	ldr	q0, [x10, #1536]
fffffe00075196c8:      	add	x9, x9, #176
fffffe00075196cc:      	nop
fffffe00075196d0:      	str	x8, [sp, #240]
fffffe00075196d4:      	adrp	x8, 0xfffffe000700a000 <_nppa_xnu_map+0x48>
fffffe00075196d8:      	str	q0, [sp, #128]
fffffe00075196dc:      	str	x9, [sp, #320]
fffffe00075196e0:      	adrp	x9, 0xfffffe000850d000 <_ncc_native_read+0xec>
fffffe00075196e4:      	add	x9, x9, #3252
fffffe00075196e8:      	ldr	q0, [x8, #1680]
fffffe00075196ec:      	mov	w8, #8696
fffffe00075196f0:      	add	x8, x9, x8
fffffe00075196f4:      	adrp	x9, 0xfffffe00084e3000 <_ncc_native_read+0x58>
fffffe00075196f8:      	add	x9, x9, #2776
fffffe00075196fc:      	str	q0, [sp, #368]
fffffe0007519700:      	ldr	q0, [x10, #1696]
fffffe0007519704:      	adrp	x10, 0xfffffe00084d1000 <_ncc_native_read+0x20>
fffffe0007519708:      	add	x10, x10, #2480
fffffe000751970c:      	str	x8, [sp, #360]
fffffe0007519710:      	add	x8, x10, #26, lsl #12
fffffe0007519714:      	adrp	x10, 0xfffffe0008510000 <_ncc_native_read+0x12c>
fffffe0007519718:      	add	x10, x10, #176
fffffe000751971c:      	add	x8, x8, #792
fffffe0007519720:      	str	q0, [sp, #224]
fffffe0007519724:      	str	x8, [sp, #352]
fffffe0007519728:      	mov	w8, #33264
fffffe000751972c:      	add	x8, x9, x8
fffffe0007519730:      	mov	w9, #59352
fffffe0007519734:      	str	x8, [sp, #216]
fffffe0007519738:      	mov	w8, #8128
fffffe000751973c:      	add	x8, x10, x8
fffffe0007519740:      	str	x8, [sp, #120]
fffffe0007519744:      	mov	w8, #22680
fffffe0007519748:      	add	x8, x12, x8
fffffe000751974c:      	str	x8, [sp, #208]
fffffe0007519750:      	mov	w8, #41016
fffffe0007519754:      	add	x8, x12, x8
fffffe0007519758:      	str	x8, [sp, #248]
fffffe000751975c:      	add	x8, x12, x9
fffffe0007519760:      	mov	w9, #28316
fffffe0007519764:      	movk	w9, #1, lsl #16
fffffe0007519768:      	str	x8, [sp, #256]
fffffe000751976c:      	mov	w8, #16204
fffffe0007519770:      	movk	w8, #1, lsl #16
fffffe0007519774:      	add	x8, x13, x8
fffffe0007519778:      	str	x8, [sp, #288]
fffffe000751977c:      	add	x8, x13, x9
fffffe0007519780:      	str	x8, [sp, #280]
fffffe0007519784:      	b	0xfffffe0007519798 <_nss_worker_continue+0x840>
fffffe0007519788:      	mov	w26, #3
fffffe000751978c:      	mov	x0, x22
fffffe0007519790:      	mov	w1, w26
fffffe0007519794:      	bl	0xfffffe000751ca0c <_nss_deliver>
fffffe0007519798:      	ldr	x0, [x24, #2456]
fffffe000751979c:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe00075197a0:      	adrp	x10, 0xfffffe00084ec000 <_ncc_native_read+0x128>
fffffe00075197a4:      	add	x10, x10, #552
fffffe00075197a8:      	mov	w22, wzr
fffffe00075197ac:      	ldp	w8, w9, [x10, #8]
fffffe00075197b0:      	ldp	w20, w26, [x10]
fffffe00075197b4:      	orr	w8, w8, w9
fffffe00075197b8:      	cmp	w8, #0
fffffe00075197bc:      	cset	w8, eq
fffffe00075197c0:      	str	w8, [sp, #456]
fffffe00075197c4:      	cbz	w20, 0xfffffe0007519814 <_nss_worker_continue+0x8bc>
fffffe00075197c8:      	cbnz	w26, 0xfffffe0007519814 <_nss_worker_continue+0x8bc>
fffffe00075197cc:      	mov	w8, #55440
fffffe00075197d0:      	adrp	x11, 0xfffffe00084d1000 <_ncc_native_read+0xec>
fffffe00075197d4:      	add	x11, x11, #2584
fffffe00075197d8:      	mov	w9, #36960
fffffe00075197dc:      	mov	w10, #18480
fffffe00075197e0:      	ldr	w8, [x11, x8]
fffffe00075197e4:      	ldr	w9, [x11, x9]
fffffe00075197e8:      	ldr	w10, [x11, x10]
fffffe00075197ec:      	sub	w8, w8, #1
fffffe00075197f0:      	sub	w9, w9, #1
fffffe00075197f4:      	cmp	w8, #2
fffffe00075197f8:      	ccmp	w9, #2, #0, hs
fffffe00075197fc:      	ldr	w8, [x11]
fffffe0007519800:      	sub	w9, w10, #1
fffffe0007519804:      	ccmp	w9, #2, #0, hs
fffffe0007519808:      	sub	w8, w8, #1
fffffe000751980c:      	ccmp	w8, #2, #0, hs
fffffe0007519810:      	cset	w22, lo
fffffe0007519814:      	adrp	x8, 0xfffffe00084ec000 <_ncc_native_write+0x68>
fffffe0007519818:      	add	x8, x8, #712
fffffe000751981c:      	ldr	x0, [x24, #2456]
fffffe0007519820:      	ldp	x9, x21, [x8]
fffffe0007519824:      	str	x9, [sp, #448]
fffffe0007519828:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751982c:      	orr	w8, w20, w26
fffffe0007519830:      	cbz	w8, 0xfffffe000751989c <_nss_worker_continue+0x944>
fffffe0007519834:      	cbz	w20, 0xfffffe0007519924 <_nss_worker_continue+0x9cc>
fffffe0007519838:      	add	x0, sp, #544
fffffe000751983c:      	str	xzr, [sp, #544]
fffffe0007519840:      	str	xzr, [sp, #504]
fffffe0007519844:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe0007519848:      	ldr	x0, [sp, #544]
fffffe000751984c:      	add	x1, sp, #504
fffffe0007519850:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe0007519854:      	ldr	x8, [sp, #504]
fffffe0007519858:      	mov	x9, #63439
fffffe000751985c:      	movk	x9, #58195, lsl #16
fffffe0007519860:      	ldr	w10, [sp, #456]
fffffe0007519864:      	movk	x9, #39845, lsl #32
fffffe0007519868:      	lsr	x8, x8, #3
fffffe000751986c:      	movk	x9, #8388, lsl #48
fffffe0007519870:      	umulh	x8, x8, x9
fffffe0007519874:      	ldr	x9, [sp, #448]
fffffe0007519878:      	lsr	x8, x8, #4
fffffe000751987c:      	cmp	x8, x9
fffffe0007519880:      	cset	w9, hs
fffffe0007519884:      	and	w9, w10, w9
fffffe0007519888:      	cmp	w9, #1
fffffe000751988c:      	b.ne	0xfffffe0007519a64 <_nss_worker_continue+0xb0c>
fffffe0007519890:      	cmp	x8, x21
fffffe0007519894:      	b.hs	0xfffffe0007519a64 <_nss_worker_continue+0xb0c>
fffffe0007519898:      	cbz	w22, 0xfffffe0007519924 <_nss_worker_continue+0x9cc>
fffffe000751989c:      	adrp	x20, 0xfffffe00084d1000 <_ncc_native_write+0x84>
fffffe00075198a0:      	add	x20, x20, #2456
fffffe00075198a4:      	ldr	x0, [x20]
fffffe00075198a8:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe00075198ac:      	mov	w8, #18608
fffffe00075198b0:      	ldr	w9, [x20, #128]
fffffe00075198b4:      	ldr	w8, [x20, x8]
fffffe00075198b8:      	cmp	w9, #1
fffffe00075198bc:      	b.ne	0xfffffe00075198ec <_nss_worker_continue+0x994>
fffffe00075198c0:      	adrp	x22, 0xfffffe00084d1000 <_ncc_native_write+0xa8>
fffffe00075198c4:      	add	x22, x22, #2584
fffffe00075198c8:      	cmp	w8, #1
fffffe00075198cc:      	b.ne	0xfffffe00075198fc <_nss_worker_continue+0x9a4>
fffffe00075198d0:      	adrp	x22, 0xfffffe00084d1000 <_ncc_native_write+0xb8>
fffffe00075198d4:      	add	x22, x22, #2584
fffffe00075198d8:      	ldr	x8, [x22, #18496]
fffffe00075198dc:      	ldr	x9, [x22, #16]
fffffe00075198e0:      	cmp	x8, x9
fffffe00075198e4:      	b.lo	0xfffffe00075198f4 <_nss_worker_continue+0x99c>
fffffe00075198e8:      	b	0xfffffe00075198fc <_nss_worker_continue+0x9a4>
fffffe00075198ec:      	cmp	w8, #1
fffffe00075198f0:      	b.ne	0xfffffe0007519a84 <_nss_worker_continue+0xb2c>
fffffe00075198f4:      	adrp	x22, 0xfffffe00084d6000 <_ncc_native_write+0xf0>
fffffe00075198f8:      	add	x22, x22, #584
fffffe00075198fc:      	adrp	x8, 0xfffffe00084da000 <_ncc_native_write+0x108>
fffffe0007519900:      	ldr	w8, [x8, #2680]
fffffe0007519904:      	cmp	w8, #1
fffffe0007519908:      	b.ne	0xfffffe0007519ae0 <_nss_worker_continue+0xb88>
fffffe000751990c:      	adrp	x8, 0xfffffe00084da000 <_ncc_native_write+0x118>
fffffe0007519910:      	ldr	x9, [x22, #16]
fffffe0007519914:      	ldr	x8, [x8, #2696]
fffffe0007519918:      	cmp	x8, x9
fffffe000751991c:      	b.lo	0xfffffe0007519a94 <_nss_worker_continue+0xb3c>
fffffe0007519920:      	b	0xfffffe0007519ae0 <_nss_worker_continue+0xb88>
fffffe0007519924:      	ldr	w8, [x25, #12]
fffffe0007519928:      	str	xzr, [sp, #544]
fffffe000751992c:      	str	xzr, [sp, #504]
fffffe0007519930:      	cbz	w8, 0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
fffffe0007519934:      	ldr	w8, [x25, #20]
fffffe0007519938:      	cbnz	w8, 0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
fffffe000751993c:      	ldr	x8, [x25]
fffffe0007519940:      	cmp	x8, x28
fffffe0007519944:      	b.ne	0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
fffffe0007519948:      	add	x0, sp, #544
fffffe000751994c:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe0007519950:      	ldr	x0, [sp, #544]
fffffe0007519954:      	add	x1, sp, #504
fffffe0007519958:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe000751995c:      	ldr	x8, [sp, #504]
fffffe0007519960:      	mov	x9, #63439
fffffe0007519964:      	movk	x9, #58195, lsl #16
fffffe0007519968:      	movk	x9, #39845, lsl #32
fffffe000751996c:      	lsr	x8, x8, #3
fffffe0007519970:      	movk	x9, #8388, lsl #48
fffffe0007519974:      	umulh	x8, x8, x9
fffffe0007519978:      	lsr	x21, x8, #4
fffffe000751997c:      	cbz	w20, 0xfffffe000751a3fc <_nss_worker_continue+0x14a4>
fffffe0007519980:      	ldr	x0, [x24, #2456]
fffffe0007519984:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe0007519988:      	adrp	x11, 0xfffffe00084ec000 <_nis_scope_or_common+0x38>
fffffe000751998c:      	add	x11, x11, #560
fffffe0007519990:      	ldp	w8, w9, [x11]
fffffe0007519994:      	orr	w10, w8, w9
fffffe0007519998:      	ldr	x9, [x11, #160]
fffffe000751999c:      	cmp	w10, #0
fffffe00075199a0:      	cset	w8, ne
fffffe00075199a4:      	cbnz	w10, 0xfffffe0007519ab0 <_nss_worker_continue+0xb58>
fffffe00075199a8:      	ldr	x10, [x11, #152]
fffffe00075199ac:      	cmp	x10, x21
fffffe00075199b0:      	b.hi	0xfffffe0007519ab0 <_nss_worker_continue+0xb58>
fffffe00075199b4:      	cmp	x9, x21
fffffe00075199b8:      	b.ls	0xfffffe0007519ab0 <_nss_worker_continue+0xb58>
fffffe00075199bc:      	adrp	x0, 0xfffffe00084ec000 <_nis_scope_or_common+0x6c>
fffffe00075199c0:      	add	x0, x0, #904
fffffe00075199c4:      	add	x26, x0, #1496
fffffe00075199c8:      	ldr	x1, [sp, #264]
fffffe00075199cc:      	adrp	x2, 0xfffffe00084d1000 <_nis_scope_or_common+0x10>
fffffe00075199d0:      	add	x2, x2, #2484
fffffe00075199d4:      	mov	x3, x21
fffffe00075199d8:      	mov	x4, x26
fffffe00075199dc:      	bl	0xfffffe00079a37d0 <_niwj_select>
fffffe00075199e0:      	cbz	w0, 0xfffffe0007519d60 <_nss_worker_continue+0xe08>
fffffe00075199e4:      	adrp	x0, 0xfffffe00084ec000 <_nis_scope_or_common+0x94>
fffffe00075199e8:      	add	x0, x0, #904
fffffe00075199ec:      	mov	w8, #22680
fffffe00075199f0:      	adrp	x2, 0xfffffe00084d1000 <_nis_scope_or_common+0x34>
fffffe00075199f4:      	add	x2, x2, #2484
fffffe00075199f8:      	add	x1, x0, x8
fffffe00075199fc:      	mov	x3, x21
fffffe0007519a00:      	mov	x4, x26
fffffe0007519a04:      	bl	0xfffffe00079a37d0 <_niwj_select>
fffffe0007519a08:      	cbz	w0, 0xfffffe0007519db0 <_nss_worker_continue+0xe58>
fffffe0007519a0c:      	adrp	x0, 0xfffffe00084ec000 <_nis_scope_or_common+0xbc>
fffffe0007519a10:      	add	x0, x0, #904
fffffe0007519a14:      	mov	w8, #41016
fffffe0007519a18:      	add	x26, x0, #1496
fffffe0007519a1c:      	add	x1, x0, x8
fffffe0007519a20:      	adrp	x2, 0xfffffe00084d1000 <_nis_scope_or_common+0x64>
fffffe0007519a24:      	add	x2, x2, #2484
fffffe0007519a28:      	mov	x3, x21
fffffe0007519a2c:      	mov	x4, x26
fffffe0007519a30:      	bl	0xfffffe00079a37d0 <_niwj_select>
fffffe0007519a34:      	cbz	w0, 0xfffffe0007519e00 <_nss_worker_continue+0xea8>
fffffe0007519a38:      	adrp	x0, 0xfffffe00084ec000 <_nis_scope_or_common+0xe8>
fffffe0007519a3c:      	add	x0, x0, #904
fffffe0007519a40:      	mov	w8, #59352
fffffe0007519a44:      	adrp	x2, 0xfffffe00084d1000 <_nis_scope_or_common+0x88>
fffffe0007519a48:      	add	x2, x2, #2484
fffffe0007519a4c:      	add	x1, x0, x8
fffffe0007519a50:      	mov	x3, x21
fffffe0007519a54:      	mov	x4, x26
fffffe0007519a58:      	bl	0xfffffe00079a37d0 <_niwj_select>
fffffe0007519a5c:      	cbnz	w0, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519a60:      	b	0xfffffe0007519e50 <_nss_worker_continue+0xef8>
fffffe0007519a64:      	cmp	x8, x21
fffffe0007519a68:      	mov	w9, #9
fffffe0007519a6c:      	cset	w8, hs
fffffe0007519a70:      	tst	w10, w8
fffffe0007519a74:      	mov	w8, #2
fffffe0007519a78:      	csel	w0, w9, w8, ne
fffffe0007519a7c:      	bl	0xfffffe000751cb80 <_njs_early_finish>
fffffe0007519a80:      	b	0xfffffe0007519798 <_nss_worker_continue+0x840>
fffffe0007519a84:      	adrp	x8, 0xfffffe00084da000 <_nis_scope_or_common+0xec>
fffffe0007519a88:      	ldr	w8, [x8, #2680]
fffffe0007519a8c:      	cmp	w8, #1
fffffe0007519a90:      	b.ne	0xfffffe0007519adc <_nss_worker_continue+0xb84>
fffffe0007519a94:      	mov	w8, #18480
fffffe0007519a98:      	adrp	x22, 0xfffffe00084da000 <_nis_scope_or_common+0x100>
fffffe0007519a9c:      	add	x22, x22, #2680
fffffe0007519aa0:      	ldr	w8, [x22, x8]
fffffe0007519aa4:      	cmp	w8, #1
fffffe0007519aa8:      	b.eq	0xfffffe0007519af4 <_nss_worker_continue+0xb9c>
fffffe0007519aac:      	b	0xfffffe0007519b18 <_nss_worker_continue+0xbc0>
fffffe0007519ab0:      	cmp	x9, x21
fffffe0007519ab4:      	ldr	x0, [x24, #2456]
fffffe0007519ab8:      	cset	w9, hi
fffffe0007519abc:      	orr	w8, w8, w9
fffffe0007519ac0:      	mov	w9, #9
fffffe0007519ac4:      	cmp	w8, #0
fffffe0007519ac8:      	mov	w8, #2
fffffe0007519acc:      	csel	w26, w8, w9, ne
fffffe0007519ad0:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe0007519ad4:      	mov	w0, w26
fffffe0007519ad8:      	b	0xfffffe000751a384 <_nss_worker_continue+0x142c>
fffffe0007519adc:      	mov	x22, xzr
fffffe0007519ae0:      	adrp	x8, 0xfffffe00084df000 <_nis_scope_or_common+0x15c>
fffffe0007519ae4:      	ldr	w8, [x8, #680]
fffffe0007519ae8:      	cmp	w8, #1
fffffe0007519aec:      	b.ne	0xfffffe0007519b14 <_nss_worker_continue+0xbbc>
fffffe0007519af0:      	cbz	x22, 0xfffffe0007519b08 <_nss_worker_continue+0xbb0>
fffffe0007519af4:      	adrp	x8, 0xfffffe00084df000 <_nis_scope_or_common+0x170>
fffffe0007519af8:      	ldr	x9, [x22, #16]
fffffe0007519afc:      	ldr	x8, [x8, #696]
fffffe0007519b00:      	cmp	x8, x9
fffffe0007519b04:      	b.hs	0xfffffe0007519b18 <_nss_worker_continue+0xbc0>
fffffe0007519b08:      	adrp	x22, 0xfffffe00084df000 <_nis_scope_or_common+0x184>
fffffe0007519b0c:      	add	x22, x22, #680
fffffe0007519b10:      	b	0xfffffe0007519b18 <_nss_worker_continue+0xbc0>
fffffe0007519b14:      	cbz	x22, 0xfffffe0007519d24 <_nss_worker_continue+0xdcc>
fffffe0007519b18:      	mov	w26, #2
fffffe0007519b1c:      	ldr	w20, [x22, #4]
fffffe0007519b20:      	str	w26, [x22]
fffffe0007519b24:      	ldr	x0, [x24, #2456]
fffffe0007519b28:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe0007519b2c:      	cbnz	w20, 0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe0007519b30:      	ldr	x8, [x25]
fffffe0007519b34:      	mov	w26, #3
fffffe0007519b38:      	cmp	x8, x28
fffffe0007519b3c:      	b.ne	0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe0007519b40:      	ldur	d0, [x25, #20]
fffffe0007519b44:      	ldr	x8, [sp, #360]
fffffe0007519b48:      	ld1.s	{ v0 }[1], [x8]
fffffe0007519b4c:      	adrp	x8, 0xfffffe000850d000 <_nis_native_check+0x14>
fffffe0007519b50:      	add	x8, x8, #3252
fffffe0007519b54:      	ldr	d1, [x8]
fffffe0007519b58:      	mov.d	v0[1], v1[0]
fffffe0007519b5c:      	ldr	q1, [sp, #368]
fffffe0007519b60:      	cmeq.4s	v0, v0, v1
fffffe0007519b64:      	mvn.16b	v0, v0
fffffe0007519b68:      	xtn.4h	v0, v0
fffffe0007519b6c:      	umov.h	w8, v0[1]
fffffe0007519b70:      	umov.h	w9, v0[2]
fffffe0007519b74:      	umov.h	w10, v0[0]
fffffe0007519b78:      	umov.h	w11, v0[3]
fffffe0007519b7c:      	and	w8, w8, #0x1
fffffe0007519b80:      	and	w9, w9, #0x1
fffffe0007519b84:      	bfi	w10, w8, #1, #1
fffffe0007519b88:      	bfi	w10, w9, #2, #1
fffffe0007519b8c:      	bfi	w10, w11, #3, #29
fffffe0007519b90:      	and	w8, w10, #0xf
fffffe0007519b94:      	cbnz	w8, 0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe0007519b98:      	ldr	w8, [x25, #12]
fffffe0007519b9c:      	cbz	w8, 0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe0007519ba0:      	adrp	x9, 0xfffffe0008512000 <_nis_native_check+0x7c>
fffffe0007519ba4:      	add	x9, x9, #136
fffffe0007519ba8:      	ldr	w8, [x9]
fffffe0007519bac:      	cbnz	w8, 0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe0007519bb0:      	ldr	w8, [x9, #12]
fffffe0007519bb4:      	cbnz	w8, 0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe0007519bb8:      	adrp	x14, 0xfffffe000850c000 <_nis_native_check+0x7c>
fffffe0007519bbc:      	add	x14, x14, #2120
fffffe0007519bc0:      	mov	w13, #60
fffffe0007519bc4:      	adrp	x15, 0xfffffe00084d1000 <_nis_scope_or_common+0x208>
fffffe0007519bc8:      	add	x15, x15, #2480
fffffe0007519bcc:      	ldr	w9, [x22, #60]
fffffe0007519bd0:      	ldr	w8, [x14, #44]
fffffe0007519bd4:      	add	x10, x22, #64
fffffe0007519bd8:      	add	x11, x15, #20
fffffe0007519bdc:      	add	x3, x15, #4
fffffe0007519be0:      	ldr	w12, [x15, #16]
fffffe0007519be4:      	cmp	w9, #0
fffffe0007519be8:      	sub	w8, w8, #1
fffffe0007519bec:      	csel	x4, x11, x10, eq
fffffe0007519bf0:      	ldr	x1, [x14, #4048]
fffffe0007519bf4:      	add	x0, x14, #201
fffffe0007519bf8:      	umaddl	x8, w8, w13, x14
fffffe0007519bfc:      	ldrb	w13, [x15]
fffffe0007519c00:      	csel	w5, w12, w9, eq
fffffe0007519c04:      	mov	w6, #1
fffffe0007519c08:      	mov	w7, #2
fffffe0007519c0c:      	add	w26, w13, #1
fffffe0007519c10:      	ldrb	w2, [x8, #4071]
fffffe0007519c14:      	ldr	x8, [sp, #352]
fffffe0007519c18:      	strb	w26, [sp]
fffffe0007519c1c:      	str	x8, [sp, #16]
fffffe0007519c20:      	mov	w8, #1
fffffe0007519c24:      	stur	x8, [sp, #4]
fffffe0007519c28:      	bl	0xfffffe000799b828 <_ncs_build>
fffffe0007519c2c:      	cbnz	w0, 0xfffffe0007519788 <_nss_worker_continue+0x830>
fffffe0007519c30:      	adrp	x20, 0xfffffe00084e3000 <_nis_native_check+0x50>
fffffe0007519c34:      	add	x20, x20, #2776
fffffe0007519c38:      	mov	x0, x20
fffffe0007519c3c:      	mov	x1, x19
fffffe0007519c40:      	mov	w2, #8736
fffffe0007519c44:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe0007519c48:      	mov	w8, #8736
fffffe0007519c4c:      	adrp	x21, 0xfffffe0008512000 <_nis_native_check+0x128>
fffffe0007519c50:      	add	x21, x21, #112
fffffe0007519c54:      	add	x0, x20, x8
fffffe0007519c58:      	mov	x1, x21
fffffe0007519c5c:      	mov	w2, #24528
fffffe0007519c60:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe0007519c64:      	mov	x0, x19
fffffe0007519c68:      	mov	w1, wzr
fffffe0007519c6c:      	mov	w2, #8736
fffffe0007519c70:      	mov	w3, #8744
fffffe0007519c74:      	str	x20, [x20, #8744]
fffffe0007519c78:      	bl	0xfffffe000746732c <___memset_chk>
fffffe0007519c7c:      	mov	x0, x21
fffffe0007519c80:      	mov	w1, #24528
fffffe0007519c84:      	bl	0xfffffe0007ba4940 <_bzero>
fffffe0007519c88:      	adrp	x9, 0xfffffe0008510000 <_nis_native_check+0x15c>
fffffe0007519c8c:      	add	x9, x9, #132
fffffe0007519c90:      	ldr	q0, [sp, #224]
fffffe0007519c94:      	add	x2, sp, #544
fffffe0007519c98:      	adrp	x0, 0xfffffe000850b000 <_nis_native_check+0x158>
fffffe0007519c9c:      	add	x0, x0, #1744
fffffe0007519ca0:      	ldp	w8, w9, [x9]
fffffe0007519ca4:      	str	q0, [sp, #576]
fffffe0007519ca8:      	cmp	w8, #0
fffffe0007519cac:      	mov	w8, #1
fffffe0007519cb0:      	ccmp	w9, #0, #0, ne
fffffe0007519cb4:      	str	x8, [sp, #552]
fffffe0007519cb8:      	adrp	x8, 0xfffffe000700a000 <_ncd_worker+0x54>
fffffe0007519cbc:      	add	x8, x8, #2484
fffffe0007519cc0:      	cset	w20, eq
fffffe0007519cc4:      	str	x8, [sp, #544]
fffffe0007519cc8:      	adr	x8, #13624
fffffe0007519ccc:      	nop
fffffe0007519cd0:      	str	x8, [sp, #560]
fffffe0007519cd4:      	ldr	x8, [sp, #216]
fffffe0007519cd8:      	str	x8, [sp, #568]
fffffe0007519cdc:      	b.ne	0xfffffe0007519fec <_nss_worker_continue+0x1094>
fffffe0007519ce0:      	adrp	x1, 0xfffffe000850f000 <_nis_read+0x30>
fffffe0007519ce4:      	add	x1, x1, #3760
fffffe0007519ce8:      	mov	x3, x19
fffffe0007519cec:      	bl	0xfffffe000799704c <_nct_scan_return_prepare>
fffffe0007519cf0:      	cbnz	w0, 0xfffffe000751a030 <_nss_worker_continue+0x10d8>
fffffe0007519cf4:      	mov	w5, #40832
fffffe0007519cf8:      	and	w4, w26, #0xff
fffffe0007519cfc:      	adrp	x0, 0xfffffe0008512000 <_nis_read+0x58>
fffffe0007519d00:      	add	x0, x0, #112
fffffe0007519d04:      	adrp	x1, 0xfffffe0007c53000 <_nss_worker_continue+0x2a94>
fffffe0007519d08:      	add	x1, x1, #1624
fffffe0007519d0c:      	mov	x2, x19
fffffe0007519d10:      	adrp	x3, 0xfffffe000850f000 <_nis_read+0x60>
fffffe0007519d14:      	add	x3, x3, #3824
fffffe0007519d18:      	movk	w5, #213, lsl #16
fffffe0007519d1c:      	bl	0xfffffe0007999258 <_ncr_scan_return_prepare>
fffffe0007519d20:      	b	0xfffffe000751a02c <_nss_worker_continue+0x10d4>
fffffe0007519d24:      	adrp	x9, 0xfffffe00084ec000 <_nis_native_check+0x168>
fffffe0007519d28:      	add	x9, x9, #552
fffffe0007519d2c:      	ldp	w8, w9, [x9]
fffffe0007519d30:      	orr	w8, w8, w9
fffffe0007519d34:      	cbnz	w8, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe0007519d38:      	adrp	x20, 0xfffffe00084d1000 <_nis_native_check+0x110>
fffffe0007519d3c:      	add	x20, x20, #2456
fffffe0007519d40:      	adrp	x8, 0xfffffe0008518000 <_nis_read+0xb4>
fffffe0007519d44:      	add	x8, x8, #680
fffffe0007519d48:      	mov	w9, #6
fffffe0007519d4c:      	mov	x0, x20
fffffe0007519d50:      	mov	w1, wzr
fffffe0007519d54:      	stlr	w9, [x8]
fffffe0007519d58:      	bl	0xfffffe000736868c <_assert_wait>
fffffe0007519d5c:      	b	0xfffffe000751abb4 <_nss_worker_continue+0x1c5c>
fffffe0007519d60:      	adrp	x20, 0xfffffe00084ec000 <_nis_read+0x24>
fffffe0007519d64:      	add	x20, x20, #904
fffffe0007519d68:      	add	x0, x20, #104
fffffe0007519d6c:      	mov	x1, x26
fffffe0007519d70:      	mov	w2, #1392
fffffe0007519d74:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe0007519d78:      	mov	x0, x20
fffffe0007519d7c:      	ldr	x1, [sp, #208]
fffffe0007519d80:      	adrp	x2, 0xfffffe00084d1000 <_nis_native_check+0x158>
fffffe0007519d84:      	add	x2, x2, #2484
fffffe0007519d88:      	mov	x3, x21
fffffe0007519d8c:      	mov	x4, x26
fffffe0007519d90:      	bl	0xfffffe00079a37d0 <_niwj_select>
fffffe0007519d94:      	cbnz	w0, 0xfffffe0007519dc4 <_nss_worker_continue+0xe6c>
fffffe0007519d98:      	adrp	x9, 0xfffffe00084ec000 <_nis_read+0x5c>
fffffe0007519d9c:      	add	x9, x9, #2396
fffffe0007519da0:      	ldrsh	w8, [x9, #1392]
fffffe0007519da4:      	ldrsh	w9, [x9]
fffffe0007519da8:      	cmp	w8, w9
fffffe0007519dac:      	b.le	0xfffffe0007519dc4 <_nss_worker_continue+0xe6c>
fffffe0007519db0:      	adrp	x0, 0xfffffe00084ec000 <_nis_read+0x74>
fffffe0007519db4:      	add	x0, x0, #1008
fffffe0007519db8:      	add	x1, x0, #1392
fffffe0007519dbc:      	mov	w2, #1392
fffffe0007519dc0:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe0007519dc4:      	adrp	x0, 0xfffffe00084ec000 <_nis_read+0x88>
fffffe0007519dc8:      	add	x0, x0, #904
fffffe0007519dcc:      	add	x4, x0, #1496
fffffe0007519dd0:      	ldr	x1, [sp, #248]
fffffe0007519dd4:      	adrp	x2, 0xfffffe00084d1000 <_nis_read+0x2c>
fffffe0007519dd8:      	add	x2, x2, #2484
fffffe0007519ddc:      	mov	x3, x21
fffffe0007519de0:      	bl	0xfffffe00079a37d0 <_niwj_select>
fffffe0007519de4:      	cbnz	w0, 0xfffffe0007519e14 <_nss_worker_continue+0xebc>
fffffe0007519de8:      	adrp	x9, 0xfffffe00084ec000 <_nis_read+0xac>
fffffe0007519dec:      	add	x9, x9, #2396
fffffe0007519df0:      	ldrsh	w8, [x9, #1392]
fffffe0007519df4:      	ldrsh	w9, [x9]
fffffe0007519df8:      	cmp	w8, w9
fffffe0007519dfc:      	b.le	0xfffffe0007519e14 <_nss_worker_continue+0xebc>
fffffe0007519e00:      	adrp	x0, 0xfffffe00084ec000 <_nis_write_mask+0xc>
fffffe0007519e04:      	add	x0, x0, #1008
fffffe0007519e08:      	add	x1, x0, #1392
fffffe0007519e0c:      	mov	w2, #1392
fffffe0007519e10:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe0007519e14:      	adrp	x0, 0xfffffe00084ec000 <_nis_write_mask+0x20>
fffffe0007519e18:      	add	x0, x0, #904
fffffe0007519e1c:      	add	x4, x0, #1496
fffffe0007519e20:      	ldr	x1, [sp, #256]
fffffe0007519e24:      	adrp	x2, 0xfffffe00084d1000 <_nis_read+0x7c>
fffffe0007519e28:      	add	x2, x2, #2484
fffffe0007519e2c:      	mov	x3, x21
fffffe0007519e30:      	bl	0xfffffe00079a37d0 <_niwj_select>
fffffe0007519e34:      	cbnz	w0, 0xfffffe0007519e64 <_nss_worker_continue+0xf0c>
fffffe0007519e38:      	adrp	x9, 0xfffffe00084ec000 <_nis_write_mask+0x44>
fffffe0007519e3c:      	add	x9, x9, #2396
fffffe0007519e40:      	ldrsh	w8, [x9, #1392]
fffffe0007519e44:      	ldrsh	w9, [x9]
fffffe0007519e48:      	cmp	w8, w9
fffffe0007519e4c:      	b.le	0xfffffe0007519e64 <_nss_worker_continue+0xf0c>
fffffe0007519e50:      	adrp	x0, 0xfffffe00084ec000 <_nis_write_mask+0x5c>
fffffe0007519e54:      	add	x0, x0, #1008
fffffe0007519e58:      	add	x1, x0, #1392
fffffe0007519e5c:      	mov	w2, #1392
fffffe0007519e60:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe0007519e64:      	adrp	x8, 0xfffffe00084ec000 <_nis_write_mask+0x70>
fffffe0007519e68:      	add	x8, x8, #720
fffffe0007519e6c:      	ldr	x26, [x8, #32]
fffffe0007519e70:      	cbz	x26, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519e74:      	adrp	x8, 0xfffffe000850b000 <_nis_write_mask+0xfc>
fffffe0007519e78:      	ldr	w8, [x8, #968]
fffffe0007519e7c:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519e80:      	ldr	x8, [x25]
fffffe0007519e84:      	cmp	x8, x28
fffffe0007519e88:      	b.ne	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519e8c:      	ldr	w8, [x25, #12]
fffffe0007519e90:      	cbz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519e94:      	ldr	w8, [x25, #16]
fffffe0007519e98:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519e9c:      	ldr	w8, [x25, #20]
fffffe0007519ea0:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519ea4:      	adrp	x8, 0xfffffe000850f000 <_nis_write_mask+0x13c>
fffffe0007519ea8:      	ldr	w8, [x8, #3756]
fffffe0007519eac:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519eb0:      	adrp	x8, 0xfffffe00084ec000 <_nis_write_mask+0xbc>
fffffe0007519eb4:      	nop
fffffe0007519eb8:      	ldr	x20, [x8, #720]
fffffe0007519ebc:      	adrp	x8, 0xfffffe00084d1000 <_nis_write_mask+0x5c>
fffffe0007519ec0:      	add	x8, x8, #2464
fffffe0007519ec4:      	ldar	w8, [x8]
fffffe0007519ec8:      	cmp	w8, #1
fffffe0007519ecc:      	b.ne	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519ed0:      	adrp	x8, 0xfffffe00084d1000 <_nis_write_mask+0x70>
fffffe0007519ed4:      	add	x8, x8, #2464
fffffe0007519ed8:      	ldr	w8, [x8, #4]
fffffe0007519edc:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519ee0:      	ldr	w8, [x23]
fffffe0007519ee4:      	cbz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519ee8:      	ldr	w8, [x23, #8144]
fffffe0007519eec:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519ef0:      	adrp	x0, 0xfffffe0008512000 <_nis_write_mask+0x194>
fffffe0007519ef4:      	add	x0, x0, #1968
fffffe0007519ef8:      	bl	0xfffffe00079a6a24 <_nst_scan_admissible>
fffffe0007519efc:      	cbz	w0, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519f00:      	mov	x0, x19
fffffe0007519f04:      	bl	0xfffffe0007996cac <_nct_done_receipt>
fffffe0007519f08:      	cbz	w0, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519f0c:      	adrp	x8, 0xfffffe00084d1000 <_nis_write_mask+0xac>
fffffe0007519f10:      	ldr	w8, [x8, #2584]
fffffe0007519f14:      	sub	w8, w8, #1
fffffe0007519f18:      	cmp	w8, #2
fffffe0007519f1c:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519f20:      	adrp	x8, 0xfffffe00084d6000 <_nis_write_mask+0xd4>
fffffe0007519f24:      	ldr	w8, [x8, #584]
fffffe0007519f28:      	sub	w8, w8, #1
fffffe0007519f2c:      	cmp	w8, #2
fffffe0007519f30:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519f34:      	adrp	x8, 0xfffffe00084da000 <_nis_write_mask+0xf8>
fffffe0007519f38:      	ldr	w8, [x8, #2680]
fffffe0007519f3c:      	sub	w8, w8, #1
fffffe0007519f40:      	cmp	w8, #2
fffffe0007519f44:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519f48:      	adrp	x8, 0xfffffe00084df000 <_nis_write_mask+0x120>
fffffe0007519f4c:      	ldr	w8, [x8, #680]
fffffe0007519f50:      	sub	w8, w8, #1
fffffe0007519f54:      	cmp	w8, #2
fffffe0007519f58:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519f5c:      	adrp	x8, 0xfffffe000850c000 <_nis_id_check+0x1c>
fffffe0007519f60:      	str	xzr, [sp, #552]
fffffe0007519f64:      	str	xzr, [sp, #544]
fffffe0007519f68:      	str	xzr, [sp, #560]
fffffe0007519f6c:      	ldr	w8, [x8, #2164]
fffffe0007519f70:      	sub	w9, w8, #17
fffffe0007519f74:      	cmn	w9, #16
fffffe0007519f78:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519f7c:      	sub	w8, w8, #1
fffffe0007519f80:      	mov	w10, #60
fffffe0007519f84:      	adrp	x9, 0xfffffe000850c000 <_nis_id_check+0x44>
fffffe0007519f88:      	add	x9, x9, #2120
fffffe0007519f8c:      	umaddl	x8, w8, w10, x9
fffffe0007519f90:      	add	x0, x9, #201
fffffe0007519f94:      	add	x3, sp, #544
fffffe0007519f98:      	ldr	x1, [x9, #4048]
fffffe0007519f9c:      	ldrb	w2, [x8, #4071]
fffffe0007519fa0:      	bl	0xfffffe000799b6c8 <_ncs_cap_mac>
fffffe0007519fa4:      	cbnz	w0, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519fa8:      	ldrb	w8, [sp, #549]
fffffe0007519fac:      	cmp	w8, #4
fffffe0007519fb0:      	b.ne	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519fb4:      	ldrb	w8, [sp, #545]
fffffe0007519fb8:      	cmp	w8, #7
fffffe0007519fbc:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe0007519fc0:      	adrp	x8, 0xfffffe00084d1000 <_nis_write_mask+0x160>
fffffe0007519fc4:      	ldr	w8, [x8, #2496]
fffffe0007519fc8:      	cbz	w8, 0xfffffe000751a2c8 <_nss_worker_continue+0x1370>
fffffe0007519fcc:      	adrp	x11, 0xfffffe00084ec000 <_nis_id_check+0xc>
fffffe0007519fd0:      	add	x11, x11, #2120
fffffe0007519fd4:      	mov	w9, wzr
fffffe0007519fd8:      	adrp	x12, 0xfffffe00084d1000 <_nis_write_mask+0x178>
fffffe0007519fdc:      	add	x12, x12, #2501
fffffe0007519fe0:      	ldrb	w10, [x11]
fffffe0007519fe4:      	ldrb	w11, [x11, #1]
fffffe0007519fe8:      	b	0xfffffe000751a29c <_nss_worker_continue+0x1344>
fffffe0007519fec:      	adrp	x1, 0xfffffe00084e3000 <_nis_id_check+0x8>
fffffe0007519ff0:      	add	x1, x1, #2776
fffffe0007519ff4:      	mov	x3, x19
fffffe0007519ff8:      	bl	0xfffffe0007996e34 <_nct_continue_prepare>
fffffe0007519ffc:      	cbnz	w0, 0xfffffe000751a030 <_nss_worker_continue+0x10d8>
fffffe000751a000:      	mov	w5, #40832
fffffe000751a004:      	and	w4, w26, #0xff
fffffe000751a008:      	adrp	x0, 0xfffffe0008512000 <_nis_id_check+0xdc>
fffffe000751a00c:      	add	x0, x0, #112
fffffe000751a010:      	adrp	x1, 0xfffffe0007c53000 <_nbc_report+0x58>
fffffe000751a014:      	add	x1, x1, #1624
fffffe000751a018:      	mov	x2, x19
fffffe000751a01c:      	adrp	x3, 0xfffffe00084e5000 <_nis_id_check+0x3c>
fffffe000751a020:      	add	x3, x3, #3320
fffffe000751a024:      	movk	w5, #213, lsl #16
fffffe000751a028:      	bl	0xfffffe0007998f84 <_ncr_continue_prepare>
fffffe000751a02c:      	cbz	w0, 0xfffffe000751a070 <_nss_worker_continue+0x1118>
fffffe000751a030:      	adrp	x20, 0xfffffe00084e3000 <_nis_id_check+0x48>
fffffe000751a034:      	add	x20, x20, #2776
fffffe000751a038:      	mov	x0, x19
fffffe000751a03c:      	mov	x1, x20
fffffe000751a040:      	mov	w2, #8736
fffffe000751a044:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe000751a048:      	mov	w8, #8736
fffffe000751a04c:      	mov	w2, #24528
fffffe000751a050:      	add	x1, x20, x8
fffffe000751a054:      	adrp	x20, 0xfffffe0008512000 <_nis_acquire+0x40>
fffffe000751a058:      	add	x20, x20, #112
fffffe000751a05c:      	mov	x0, x20
fffffe000751a060:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe000751a064:      	mov	w26, #3
fffffe000751a068:      	str	x19, [x20, #8]
fffffe000751a06c:      	b	0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe000751a070:      	mov	w8, #1
fffffe000751a074:      	adrp	x9, 0xfffffe0008518000 <_nis_acquire+0x78>
fffffe000751a078:      	add	x9, x9, #680
fffffe000751a07c:      	add	x2, sp, #504
fffffe000751a080:      	adrp	x0, 0xfffffe0007217000 <_nss_worker_continue+0x51c>
fffffe000751a084:      	add	x0, x0, #1232
fffffe000751a088:      	str	w8, [x19, #8740]
fffffe000751a08c:      	mov	w8, #5
fffffe000751a090:      	adrp	x1, 0xfffffe000850c000 <_nis_acquire+0x64>
fffffe000751a094:      	add	x1, x1, #2120
fffffe000751a098:      	mov	x3, x19
fffffe000751a09c:      	stlr	w8, [x9]
fffffe000751a0a0:      	adrp	x8, 0xfffffe0007999000 <_nss_worker_continue+0x2344>
fffffe000751a0a4:      	add	x9, x8, #1552
fffffe000751a0a8:      	ldr	x8, [sp, #120]
fffffe000751a0ac:      	stp	x9, x8, [sp, #504]
fffffe000751a0b0:      	adr	x8, #9500
fffffe000751a0b4:      	nop
fffffe000751a0b8:      	str	x8, [sp, #520]
fffffe000751a0bc:      	adrp	x8, 0xfffffe0008510000 <_nis_acquire+0xa0>
fffffe000751a0c0:      	add	x8, x8, #176
fffffe000751a0c4:      	str	x8, [sp, #528]
fffffe000751a0c8:      	bl	0xfffffe00079978fc <_nct_run>
fffffe000751a0cc:      	cmp	w0, #1
fffffe000751a0d0:      	str	wzr, [x19, #8740]
fffffe000751a0d4:      	b.ne	0xfffffe000751a188 <_nss_worker_continue+0x1230>
fffffe000751a0d8:      	ldr	w8, [x19, #4572]
fffffe000751a0dc:      	cbz	w8, 0xfffffe000751a188 <_nss_worker_continue+0x1230>
fffffe000751a0e0:      	cbz	w20, 0xfffffe000751a15c <_nss_worker_continue+0x1204>
fffffe000751a0e4:      	adrp	x9, 0xfffffe0008512000 <_nis_acquire+0xd0>
fffffe000751a0e8:      	add	x9, x9, #256
fffffe000751a0ec:      	adrp	x8, 0xfffffe000850f000 <_nis_acquire+0xcc>
fffffe000751a0f0:      	add	x8, x8, #3824
fffffe000751a0f4:      	ldp	q0, q1, [x9, #32]
fffffe000751a0f8:      	ldp	q3, q2, [x9]
fffffe000751a0fc:      	stp	q0, q1, [x8, #32]
fffffe000751a100:      	ldp	q0, q1, [x9, #96]
fffffe000751a104:      	stp	q3, q2, [x8]
fffffe000751a108:      	ldp	q2, q3, [x9, #128]
fffffe000751a10c:      	stp	q0, q1, [x8, #96]
fffffe000751a110:      	ldp	q0, q1, [x9, #64]
fffffe000751a114:      	stp	q2, q3, [x8, #128]
fffffe000751a118:      	ldp	q3, q2, [x9, #192]
fffffe000751a11c:      	stp	q0, q1, [x8, #64]
fffffe000751a120:      	ldp	q0, q1, [x9, #160]
fffffe000751a124:      	stp	q3, q2, [x8, #192]
fffffe000751a128:      	ldp	q3, q2, [x9, #288]
fffffe000751a12c:      	stp	q0, q1, [x8, #160]
fffffe000751a130:      	ldp	q1, q0, [x9, #256]
fffffe000751a134:      	stp	q3, q2, [x8, #288]
fffffe000751a138:      	ldp	q2, q3, [x9, #224]
fffffe000751a13c:      	stp	q1, q0, [x8, #256]
fffffe000751a140:      	ldp	q1, q0, [x9, #368]
fffffe000751a144:      	stp	q2, q3, [x8, #224]
fffffe000751a148:      	stp	q1, q0, [x8, #368]
fffffe000751a14c:      	ldr	q3, [x9, #320]
fffffe000751a150:      	ldp	q0, q2, [x9, #336]
fffffe000751a154:      	str	q3, [x8, #320]
fffffe000751a158:      	stp	q0, q2, [x8, #336]
fffffe000751a15c:      	mov	x0, x19
fffffe000751a160:      	adrp	x1, 0xfffffe00084e3000 <_nis_acquire+0x90>
fffffe000751a164:      	add	x1, x1, #2776
fffffe000751a168:      	mov	w2, #8736
fffffe000751a16c:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe000751a170:      	adrp	x8, 0xfffffe0008512000 <_nis_release+0x8>
fffffe000751a174:      	add	x8, x8, #120
fffffe000751a178:      	mov	w26, #3
fffffe000751a17c:      	str	x19, [x8]
fffffe000751a180:      	str	xzr, [x8, #16]
fffffe000751a184:      	b	0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe000751a188:      	cbz	w20, 0xfffffe000751a198 <_nss_worker_continue+0x1240>
fffffe000751a18c:      	mov	w8, #1
fffffe000751a190:      	adrp	x9, 0xfffffe0008510000 <_nis_release+0x20>
fffffe000751a194:      	str	w8, [x9, #136]
fffffe000751a198:      	adrp	x8, 0xfffffe00084d1000 <_nis_acquire+0x80>
fffffe000751a19c:      	strb	w26, [x8, #2480]
fffffe000751a1a0:      	tbnz	w0, #31, 0xfffffe000751bc6c <_nss_worker_continue+0x2d14>
fffffe000751a1a4:      	adrp	x8, 0xfffffe0008512000 <_nis_release+0x3c>
fffffe000751a1a8:      	add	x8, x8, #220
fffffe000751a1ac:      	ldr	w8, [x8, #1756]
fffffe000751a1b0:      	cbnz	w8, 0xfffffe000751a1f0 <_nss_worker_continue+0x1298>
fffffe000751a1b4:      	adrp	x8, 0xfffffe0008512000 <_nis_release+0x4c>
fffffe000751a1b8:      	nop
fffffe000751a1bc:      	ldr	w8, [x8, #220]
fffffe000751a1c0:      	cbz	w8, 0xfffffe000751a1f0 <_nss_worker_continue+0x1298>
fffffe000751a1c4:      	adrp	x8, 0xfffffe000850d000 <_nis_release+0x48>
fffffe000751a1c8:      	adrp	x10, 0xfffffe0008512000 <_nis_id_read+0x8>
fffffe000751a1cc:      	add	x10, x10, #1972
fffffe000751a1d0:      	mov	w9, #1
fffffe000751a1d4:      	ldr	w8, [x8, #3300]
fffffe000751a1d8:      	str	wzr, [x10]
fffffe000751a1dc:      	str	w9, [x10, #12]
fffffe000751a1e0:      	cbz	w8, 0xfffffe000751a1f0 <_nss_worker_continue+0x1298>
fffffe000751a1e4:      	mov	w8, #1
fffffe000751a1e8:      	adrp	x9, 0xfffffe0008512000 <_nis_id_read+0x28>
fffffe000751a1ec:      	str	w8, [x9, #1980]
fffffe000751a1f0:      	ldr	w8, [x25, #12]
fffffe000751a1f4:      	str	xzr, [sp, #480]
fffffe000751a1f8:      	str	xzr, [sp, #536]
fffffe000751a1fc:      	cbz	w8, 0xfffffe000751bc74 <_nss_worker_continue+0x2d1c>
fffffe000751a200:      	ldr	w8, [x25, #20]
fffffe000751a204:      	cbnz	w8, 0xfffffe000751bc74 <_nss_worker_continue+0x2d1c>
fffffe000751a208:      	ldr	x8, [x25]
fffffe000751a20c:      	cmp	x8, x28
fffffe000751a210:      	b.ne	0xfffffe000751bc74 <_nss_worker_continue+0x2d1c>
fffffe000751a214:      	add	x0, sp, #480
fffffe000751a218:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe000751a21c:      	ldr	x0, [sp, #480]
fffffe000751a220:      	add	x1, sp, #536
fffffe000751a224:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe000751a228:      	ldr	x8, [sp, #536]
fffffe000751a22c:      	mov	x9, #63439
fffffe000751a230:      	movk	x9, #58195, lsl #16
fffffe000751a234:      	movk	x9, #39845, lsl #32
fffffe000751a238:      	lsr	x8, x8, #3
fffffe000751a23c:      	movk	x9, #8388, lsl #48
fffffe000751a240:      	umulh	x8, x8, x9
fffffe000751a244:      	adrp	x9, 0xfffffe0008512000 <_nis_id_write+0x4>
fffffe000751a248:      	lsr	x20, x8, #4
fffffe000751a24c:      	ldr	x9, [x9, #2016]
fffffe000751a250:      	cmp	x20, x9
fffffe000751a254:      	b.lo	0xfffffe000751bc74 <_nss_worker_continue+0x2d1c>
fffffe000751a258:      	add	x0, x22, #144
fffffe000751a25c:      	adrp	x1, 0xfffffe0008512000 <_nis_id_write+0x1c>
fffffe000751a260:      	add	x1, x1, #1968
fffffe000751a264:      	mov	w2, #18336
fffffe000751a268:      	bl	0xfffffe0007ba4790 <_memmove>
fffffe000751a26c:      	ldr	w8, [x22, #152]
fffffe000751a270:      	str	x20, [x22, #32]
fffffe000751a274:      	cbz	w8, 0xfffffe000751a288 <_nss_worker_continue+0x1330>
fffffe000751a278:      	ldr	w8, [x22, #160]
fffffe000751a27c:      	cbnz	w8, 0xfffffe000751a288 <_nss_worker_continue+0x1330>
fffffe000751a280:      	ldr	w8, [x22, #156]
fffffe000751a284:      	cbz	w8, 0xfffffe000751a2b8 <_nss_worker_continue+0x1360>
fffffe000751a288:      	mov	w26, #1
fffffe000751a28c:      	b	0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe000751a290:      	add	x12, x12, #2
fffffe000751a294:      	subs	x8, x8, #1
fffffe000751a298:      	b.eq	0xfffffe000751a2cc <_nss_worker_continue+0x1374>
fffffe000751a29c:      	ldurb	w13, [x12, #-1]
fffffe000751a2a0:      	cmp	w13, w10
fffffe000751a2a4:      	b.ne	0xfffffe000751a290 <_nss_worker_continue+0x1338>
fffffe000751a2a8:      	ldrb	w13, [x12]
fffffe000751a2ac:      	cmp	w13, w11
fffffe000751a2b0:      	csinc	w9, w9, wzr, ne
fffffe000751a2b4:      	b	0xfffffe000751a290 <_nss_worker_continue+0x1338>
fffffe000751a2b8:      	ldr	w8, [x22, #168]
fffffe000751a2bc:      	cmp	w8, #0
fffffe000751a2c0:      	cset	w26, ne
fffffe000751a2c4:      	b	0xfffffe000751978c <_nss_worker_continue+0x834>
fffffe000751a2c8:      	mov	w9, wzr
fffffe000751a2cc:      	cbz	w9, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a2d0:      	adrp	x8, 0xfffffe00084ec000 <_nis_id_read+0x78>
fffffe000751a2d4:      	add	x8, x8, #1060
fffffe000751a2d8:      	ldrb	w8, [x8]
fffffe000751a2dc:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a2e0:      	adrp	x8, 0xfffffe00084ec000 <_nis_id_write+0x8>
fffffe000751a2e4:      	add	x8, x8, #1060
fffffe000751a2e8:      	ldr	w8, [x8, #24]
fffffe000751a2ec:      	cbnz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a2f0:      	adrp	x9, 0xfffffe00084ec000 <_nis_id_write+0x18>
fffffe000751a2f4:      	add	x9, x9, #1053
fffffe000751a2f8:      	ldurh	w8, [x9, #25]
fffffe000751a2fc:      	ldurh	w9, [x9, #27]
fffffe000751a300:      	cmp	w8, w9
fffffe000751a304:      	b.ne	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a308:      	adrp	x8, 0xfffffe00084ec000 <_nis_id_write+0x30>
fffffe000751a30c:      	add	x8, x8, #1053
fffffe000751a310:      	ldrb	w8, [x8]
fffffe000751a314:      	cbz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a318:      	adrp	x8, 0xfffffe00084ec000 <_nis_id_write+0x40>
fffffe000751a31c:      	ldrb	w8, [x8, #1052]
fffffe000751a320:      	sub	w8, w8, #33
fffffe000751a324:      	cmn	w8, #32
fffffe000751a328:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a32c:      	adrp	x8, 0xfffffe00084ec000 <_nis_id_write+0x54>
fffffe000751a330:      	add	x8, x8, #1074
fffffe000751a334:      	ldrb	w1, [x8, #1048]
fffffe000751a338:      	sub	w8, w1, #13
fffffe000751a33c:      	cmn	w8, #12
fffffe000751a340:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a344:      	adrp	x8, 0xfffffe00084ec000 <_nis_id_write+0x6c>
fffffe000751a348:      	add	x8, x8, #1074
fffffe000751a34c:      	ldrh	w8, [x8]
fffffe000751a350:      	cbz	w8, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751a354:      	add	x2, sp, #536
fffffe000751a358:      	add	x3, sp, #478
fffffe000751a35c:      	add	x4, sp, #476
fffffe000751a360:      	adrp	x0, 0xfffffe00084ec000 <_nis_id_write+0x88>
fffffe000751a364:      	add	x0, x0, #2123
fffffe000751a368:      	strh	wzr, [sp, #536]
fffffe000751a36c:      	str	wzr, [sp, #476]
fffffe000751a370:      	bl	0xfffffe00079a95ec <_nst_legacy_rates>
fffffe000751a374:      	cbz	w0, 0xfffffe000751b76c <_nss_worker_continue+0x2814>
fffffe000751a378:      	ldr	x0, [x24, #2456]
fffffe000751a37c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a380:      	mov	w0, #2
fffffe000751a384:      	bl	0xfffffe000751cb80 <_njs_early_finish>
fffffe000751a388:      	ldr	x0, [x24, #2456]
fffffe000751a38c:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a390:      	adrp	x8, 0xfffffe00084ec000 <_nis_delay+0x28>
fffffe000751a394:      	ldr	x0, [x24, #2456]
fffffe000751a398:      	ldr	w20, [x8, #556]
fffffe000751a39c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a3a0:      	cbz	w20, 0xfffffe0007519798 <_nss_worker_continue+0x840>
fffffe000751a3a4:      	ldr	w8, [x25, #12]
fffffe000751a3a8:      	str	xzr, [sp, #544]
fffffe000751a3ac:      	str	xzr, [sp, #504]
fffffe000751a3b0:      	cbz	w8, 0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
fffffe000751a3b4:      	ldr	w8, [x25, #20]
fffffe000751a3b8:      	cbnz	w8, 0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
fffffe000751a3bc:      	ldr	x8, [x25]
fffffe000751a3c0:      	cmp	x8, x28
fffffe000751a3c4:      	b.ne	0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
fffffe000751a3c8:      	add	x0, sp, #544
fffffe000751a3cc:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe000751a3d0:      	ldr	x0, [sp, #544]
fffffe000751a3d4:      	add	x1, sp, #504
fffffe000751a3d8:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe000751a3dc:      	ldr	x8, [sp, #504]
fffffe000751a3e0:      	mov	x9, #63439
fffffe000751a3e4:      	movk	x9, #58195, lsl #16
fffffe000751a3e8:      	movk	x9, #39845, lsl #32
fffffe000751a3ec:      	lsr	x8, x8, #3
fffffe000751a3f0:      	movk	x9, #8388, lsl #48
fffffe000751a3f4:      	umulh	x8, x8, x9
fffffe000751a3f8:      	lsr	x21, x8, #4
fffffe000751a3fc:      	ldr	x0, [x24, #2456]
fffffe000751a400:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a404:      	adrp	x8, 0xfffffe00084ec000 <_nbc_prepare+0x18>
fffffe000751a408:      	ldr	w10, [x8, #568]
fffffe000751a40c:      	cbz	w10, 0xfffffe000751a418 <_nss_worker_continue+0x14c0>
fffffe000751a410:      	adrp	x8, 0xfffffe00084ec000 <_nbc_prepare+0x24>
fffffe000751a414:      	str	wzr, [x8, #568]
fffffe000751a418:      	adrp	x9, 0xfffffe00084ec000 <_nbc_prepare+0x2c>
fffffe000751a41c:      	add	x9, x9, #560
fffffe000751a420:      	cmp	w10, #0
fffffe000751a424:      	str	w10, [sp, #448]
fffffe000751a428:      	ldp	w8, w9, [x9]
fffffe000751a42c:      	orr	w9, w8, w9
fffffe000751a430:      	cset	w8, ne
fffffe000751a434:      	str	w8, [sp, #408]
fffffe000751a438:      	lsr	x8, x21, #3
fffffe000751a43c:      	cbz	w9, 0xfffffe000751a4a0 <_nss_worker_continue+0x1548>
fffffe000751a440:      	mov	x9, #63439
fffffe000751a444:      	adrp	x20, 0xfffffe00084ec000 <_nbc_prepare+0x58>
fffffe000751a448:      	add	x20, x20, #604
fffffe000751a44c:      	movk	x9, #58195, lsl #16
fffffe000751a450:      	movk	x9, #39845, lsl #32
fffffe000751a454:      	ldr	x0, [sp, #344]
fffffe000751a458:      	movk	x9, #8388, lsl #48
fffffe000751a45c:      	ldur	x1, [x20, #148]
fffffe000751a460:      	umulh	x8, x8, x9
fffffe000751a464:      	lsr	x21, x8, #4
fffffe000751a468:      	mov	x2, x21
fffffe000751a46c:      	bl	0xfffffe00079b0e24 <_nwn_cancel>
fffffe000751a470:      	ldr	w8, [x20, #4]
fffffe000751a474:      	mov	w1, #12112
fffffe000751a478:      	ldr	w9, [x20, #72]
fffffe000751a47c:      	ldr	x0, [sp, #336]
fffffe000751a480:      	add	w8, w9, w8
fffffe000751a484:      	str	w8, [x20, #72]
fffffe000751a488:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751a48c:      	movi.2d	v0, #0000000000000000
fffffe000751a490:      	ldr	x8, [sp, #328]
fffffe000751a494:      	str	xzr, [x20]
fffffe000751a498:      	stp	q0, q0, [x8]
fffffe000751a49c:      	b	0xfffffe000751a4b8 <_nss_worker_continue+0x1560>
fffffe000751a4a0:      	mov	x9, #63439
fffffe000751a4a4:      	movk	x9, #58195, lsl #16
fffffe000751a4a8:      	movk	x9, #39845, lsl #32
fffffe000751a4ac:      	movk	x9, #8388, lsl #48
fffffe000751a4b0:      	umulh	x8, x8, x9
fffffe000751a4b4:      	lsr	x21, x8, #4
fffffe000751a4b8:      	adrp	x0, 0xfffffe00084ff000 <_nbc_prepare+0x118>
fffffe000751a4bc:      	add	x0, x0, #784
fffffe000751a4c0:      	mov	x1, x21
fffffe000751a4c4:      	str	x21, [sp, #400]
fffffe000751a4c8:      	bl	0xfffffe00079b0c64 <_nwn_pump>
fffffe000751a4cc:      	tbnz	w0, #31, 0xfffffe000751bc60 <_nss_worker_continue+0x2d08>
fffffe000751a4d0:      	adrp	x9, 0xfffffe00084ec000 <_nbc_prepare+0xe4>
fffffe000751a4d4:      	add	x9, x9, #560
fffffe000751a4d8:      	adrp	x21, 0xfffffe00084ec000 <_nbc_prepare+0xec>
fffffe000751a4dc:      	add	x21, x21, #544
fffffe000751a4e0:      	ldp	w8, w9, [x9]
fffffe000751a4e4:      	orr	w8, w8, w9
fffffe000751a4e8:      	cbnz	w8, 0xfffffe000751a5b8 <_nss_worker_continue+0x1660>
fffffe000751a4ec:      	ldr	x0, [sp, #320]
fffffe000751a4f0:      	bl	0xfffffe00079aff14 <_nwo_authorized>
fffffe000751a4f4:      	cbz	w0, 0xfffffe000751a5b8 <_nss_worker_continue+0x1660>
fffffe000751a4f8:      	adrp	x8, 0xfffffe0008509000 <_cap_receipt+0x20>
fffffe000751a4fc:      	nop
fffffe000751a500:      	ldr	w8, [x8, #40]
fffffe000751a504:      	cbnz	w8, 0xfffffe000751a5b8 <_nss_worker_continue+0x1660>
fffffe000751a508:      	adrp	x8, 0xfffffe0008509000 <_cap_receipt+0x30>
fffffe000751a50c:      	add	x8, x8, #40
fffffe000751a510:      	ldr	w8, [x8, #4]
fffffe000751a514:      	cbnz	w8, 0xfffffe000751a5b8 <_nss_worker_continue+0x1660>
fffffe000751a518:      	adrp	x8, 0xfffffe00084ec000 <_nbc_prepare+0x12c>
fffffe000751a51c:      	nop
fffffe000751a520:      	ldr	w8, [x8, #608]
fffffe000751a524:      	cbz	w8, 0xfffffe000751a5b8 <_nss_worker_continue+0x1660>
fffffe000751a528:      	ldr	w20, [x21, #60]
fffffe000751a52c:      	mov	w9, #28368
fffffe000751a530:      	movk	w9, #1, lsl #16
fffffe000751a534:      	adrp	x0, 0xfffffe0008508000 <_cap_receipt+0x58>
fffffe000751a538:      	add	x0, x0, #4080
fffffe000751a53c:      	mov	x1, #-1
fffffe000751a540:      	add	x8, x21, x20, lsl #2
fffffe000751a544:      	add	x22, x8, x9
fffffe000751a548:      	mov	w8, #1514
fffffe000751a54c:      	mov	w9, #16256
fffffe000751a550:      	madd	x8, x20, x8, x21
fffffe000751a554:      	movk	w9, #1, lsl #16
fffffe000751a558:      	ldr	w3, [x22]
fffffe000751a55c:      	add	x26, x8, x9
fffffe000751a560:      	mov	x2, x26
fffffe000751a564:      	bl	0xfffffe00079a7874 <_nst_data>
fffffe000751a568:      	cmp	w0, #0
fffffe000751a56c:      	b.gt	0xfffffe000751a5b8 <_nss_worker_continue+0x1660>
fffffe000751a570:      	tbz	w0, #31, 0xfffffe000751a584 <_nss_worker_continue+0x162c>
fffffe000751a574:      	adrp	x9, 0xfffffe00084ec000 <_cap_receipt+0x28>
fffffe000751a578:      	ldr	w8, [x9, #676]
fffffe000751a57c:      	add	w8, w8, #1
fffffe000751a580:      	str	w8, [x9, #676]
fffffe000751a584:      	mov	x0, x26
fffffe000751a588:      	mov	w1, #1514
fffffe000751a58c:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751a590:      	adrp	x10, 0xfffffe00084ec000 <_cap_receipt+0x44>
fffffe000751a594:      	add	x10, x10, #604
fffffe000751a598:      	add	w9, w20, #1
fffffe000751a59c:      	adrp	x21, 0xfffffe00084ec000 <_cap_receipt+0x50>
fffffe000751a5a0:      	add	x21, x21, #544
fffffe000751a5a4:      	and	w9, w9, #0x7
fffffe000751a5a8:      	ldr	w8, [x10, #4]
fffffe000751a5ac:      	str	wzr, [x22]
fffffe000751a5b0:      	sub	w8, w8, #1
fffffe000751a5b4:      	stp	w9, w8, [x10]
fffffe000751a5b8:      	adrp	x8, 0xfffffe0008509000 <_cap_receipt+0xe0>
fffffe000751a5bc:      	ldr	w8, [x8, #28]
fffffe000751a5c0:      	cbnz	w8, 0xfffffe000751bc58 <_nss_worker_continue+0x2d00>
fffffe000751a5c4:      	adrp	x0, 0xfffffe0008508000 <_cap_receipt+0xe8>
fffffe000751a5c8:      	add	x0, x0, #4080
fffffe000751a5cc:      	adrp	x1, 0xfffffe0007217000 <_nss_worker_continue+0xa68>
fffffe000751a5d0:      	add	x1, x1, #1232
fffffe000751a5d4:      	adrp	x2, 0xfffffe000850c000 <_cap_receipt+0x108>
fffffe000751a5d8:      	add	x2, x2, #2120
fffffe000751a5dc:      	mov	w3, #16
fffffe000751a5e0:      	adr	x4, #9792
fffffe000751a5e4:      	nop
fffffe000751a5e8:      	mov	x5, x21
fffffe000751a5ec:      	bl	0xfffffe00079a7dd8 <_nst_step>
fffffe000751a5f0:      	tbnz	w0, #31, 0xfffffe000751bc58 <_nss_worker_continue+0x2d00>
fffffe000751a5f4:      	bl	0xfffffe000751cf08 <_njs_snapshot_locked>
fffffe000751a5f8:      	mov	w0, wzr
fffffe000751a5fc:      	bl	0xfffffe000750e938 <_njs_trace_locked>
fffffe000751a600:      	ldr	x0, [sp, #392]
fffffe000751a604:      	bl	0xfffffe00079aff14 <_nwo_authorized>
fffffe000751a608:      	adrp	x8, 0xfffffe0008509000 <_cap_receipt+0x130>
fffffe000751a60c:      	adrp	x11, 0xfffffe00084ec000 <_cap_receipt+0xc0>
fffffe000751a610:      	add	x11, x11, #560
fffffe000751a614:      	cmp	w0, #0
fffffe000751a618:      	ldr	w8, [x8, #904]
fffffe000751a61c:      	ldr	w10, [x11, #36]
fffffe000751a620:      	ccmp	w8, #0, #4, ne
fffffe000751a624:      	ldp	w8, w9, [x11]
fffffe000751a628:      	cset	w12, eq
fffffe000751a62c:      	cmp	w8, #0
fffffe000751a630:      	ldr	w8, [x11, #3276]
fffffe000751a634:      	cset	w11, ne
fffffe000751a638:      	cmp	w9, #0
fffffe000751a63c:      	cset	w9, ne
fffffe000751a640:      	cmp	w10, #0
fffffe000751a644:      	cset	w10, ne
fffffe000751a648:      	cmp	w8, #0
fffffe000751a64c:      	cset	w8, eq
fffffe000751a650:      	mov	w22, w11
fffffe000751a654:      	orr	w8, w8, w11
fffffe000751a658:      	str	w12, [sp, #456]
fffffe000751a65c:      	orr	w8, w8, w9
fffffe000751a660:      	str	w9, [sp, #424]
fffffe000751a664:      	and	w20, w8, w10
fffffe000751a668:      	str	w10, [sp, #416]
fffffe000751a66c:      	cmp	w20, #1
fffffe000751a670:      	b.ne	0xfffffe000751a6b0 <_nss_worker_continue+0x1758>
fffffe000751a674:      	adrp	x21, 0xfffffe00084ec000 <_cap_receipt+0x128>
fffffe000751a678:      	add	x21, x21, #596
fffffe000751a67c:      	movi.2d	v0, #0000000000000000
fffffe000751a680:      	ldr	x0, [sp, #288]
fffffe000751a684:      	mov	w1, #12112
fffffe000751a688:      	ldr	w8, [x21, #12]
fffffe000751a68c:      	ldr	w9, [x21, #80]
fffffe000751a690:      	str	d0, [x21]
fffffe000751a694:      	add	w8, w9, w8
fffffe000751a698:      	str	w8, [x21, #80]
fffffe000751a69c:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751a6a0:      	movi.2d	v0, #0000000000000000
fffffe000751a6a4:      	ldr	x8, [sp, #280]
fffffe000751a6a8:      	stp	q0, q0, [x8]
fffffe000751a6ac:      	str	d0, [x21, #8]
fffffe000751a6b0:      	adrp	x8, 0xfffffe00084ff000 <_nbc_lease_window+0x30>
fffffe000751a6b4:      	ldr	w8, [x8, #788]
fffffe000751a6b8:      	sub	w8, w8, #21
fffffe000751a6bc:      	cmp	w8, #1
fffffe000751a6c0:      	str	w8, [sp, #444]
fffffe000751a6c4:      	b.hi	0xfffffe000751a6dc <_nss_worker_continue+0x1784>
fffffe000751a6c8:      	adrp	x8, 0xfffffe00084ec000 <_cap_receipt+0x17c>
fffffe000751a6cc:      	ldr	w8, [x8, #580]
fffffe000751a6d0:      	cbz	w8, 0xfffffe000751a6e4 <_nss_worker_continue+0x178c>
fffffe000751a6d4:      	str	wzr, [sp, #432]
fffffe000751a6d8:      	b	0xfffffe000751a6f8 <_nss_worker_continue+0x17a0>
fffffe000751a6dc:      	str	wzr, [sp, #432]
fffffe000751a6e0:      	b	0xfffffe000751a744 <_nss_worker_continue+0x17ec>
fffffe000751a6e4:      	mov	w10, #1
fffffe000751a6e8:      	adrp	x8, 0xfffffe00084ec000 <_nbc_lease_window+0x1c>
fffffe000751a6ec:      	mov	w9, #1
fffffe000751a6f0:      	str	w10, [x8, #580]
fffffe000751a6f4:      	str	w9, [sp, #432]
fffffe000751a6f8:      	adrp	x21, 0xfffffe00084ec000 <_nbc_lease_window+0x2c>
fffffe000751a6fc:      	add	x21, x21, #556
fffffe000751a700:      	mov	w8, #1
fffffe000751a704:      	ldr	x0, [sp, #312]
fffffe000751a708:      	mov	w1, #1432
fffffe000751a70c:      	str	wzr, [x21]
fffffe000751a710:      	str	w8, [x21, #28]
fffffe000751a714:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751a718:      	ldr	w8, [x21, #52]
fffffe000751a71c:      	mov	w1, #12112
fffffe000751a720:      	ldr	w9, [x21, #120]
fffffe000751a724:      	ldr	x0, [sp, #304]
fffffe000751a728:      	add	w8, w9, w8
fffffe000751a72c:      	str	w8, [x21, #120]
fffffe000751a730:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751a734:      	movi.2d	v0, #0000000000000000
fffffe000751a738:      	ldr	x8, [sp, #296]
fffffe000751a73c:      	str	xzr, [x21, #48]
fffffe000751a740:      	stp	q0, q0, [x8]
fffffe000751a744:      	ldr	x0, [x24, #2456]
fffffe000751a748:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a74c:      	cbz	w20, 0xfffffe000751a7d0 <_nss_worker_continue+0x1878>
fffffe000751a750:      	adrp	x20, 0xfffffe00084ec000 <_nbc_lease_window+0x84>
fffffe000751a754:      	add	x20, x20, #544
fffffe000751a758:      	ldr	x0, [x20]
fffffe000751a75c:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a760:      	ldr	x0, [x20, #208]
fffffe000751a764:      	mov	w1, wzr
fffffe000751a768:      	bl	0xfffffe0007ae5b4c <_nord2_wifi_ifnet_link>
fffffe000751a76c:      	mov	w26, w0
fffffe000751a770:      	ldr	x0, [x20]
fffffe000751a774:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a778:      	cbz	w26, 0xfffffe000751a7d0 <_nss_worker_continue+0x1878>
fffffe000751a77c:      	ldr	x0, [x24, #2456]
fffffe000751a780:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a784:      	adrp	x8, 0xfffffe00084ec000 <_nbc_lease_window+0xb8>
fffffe000751a788:      	ldr	w8, [x8, #616]
fffffe000751a78c:      	cmp	w8, #111
fffffe000751a790:      	b.hi	0xfffffe000751a7c8 <_nss_worker_continue+0x1870>
fffffe000751a794:      	add	w8, w8, #1
fffffe000751a798:      	ldr	x0, [x24, #2456]
fffffe000751a79c:      	adrp	x20, 0xfffffe00084ec000 <_nbc_lease_window+0xd0>
fffffe000751a7a0:      	add	x20, x20, #616
fffffe000751a7a4:      	str	w8, [x20]
fffffe000751a7a8:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a7ac:      	ldr	x8, [x20, #136]
fffffe000751a7b0:      	adrp	x0, 0xfffffe0007096000 <_nss_worker_continue+0x648>
fffffe000751a7b4:      	add	x0, x0, #3500
fffffe000751a7b8:      	stp	xzr, x26, [sp, #8]
fffffe000751a7bc:      	str	x8, [sp]
fffffe000751a7c0:      	bl	0xfffffe000735dae4 <_printf>
fffffe000751a7c4:      	b	0xfffffe000751a7d0 <_nss_worker_continue+0x1878>
fffffe000751a7c8:      	ldr	x0, [x24, #2456]
fffffe000751a7cc:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a7d0:      	ldr	w8, [sp, #456]
fffffe000751a7d4:      	ldr	w9, [sp, #424]
fffffe000751a7d8:      	orr	w8, w8, w22
fffffe000751a7dc:      	orr	w8, w8, w9
fffffe000751a7e0:      	ldr	w9, [sp, #416]
fffffe000751a7e4:      	orr	w8, w8, w9
fffffe000751a7e8:      	tbz	w8, #0, 0xfffffe000751a7f4 <_nss_worker_continue+0x189c>
fffffe000751a7ec:      	str	wzr, [sp, #424]
fffffe000751a7f0:      	b	0xfffffe000751a9bc <_nss_worker_continue+0x1a64>
fffffe000751a7f4:      	adrp	x20, 0xfffffe00084ec000 <_nbc_lease_window+0x128>
fffffe000751a7f8:      	add	x20, x20, #544
fffffe000751a7fc:      	ldr	x0, [x20]
fffffe000751a800:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a804:      	ldr	x0, [x24, #2456]
fffffe000751a808:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a80c:      	ldp	w8, w9, [x20, #16]
fffffe000751a810:      	orr	w8, w8, w9
fffffe000751a814:      	cbnz	w8, 0xfffffe000751a854 <_nss_worker_continue+0x18fc>
fffffe000751a818:      	adrp	x0, 0xfffffe00084ff000 <_nbc_run+0x38>
fffffe000751a81c:      	add	x0, x0, #784
fffffe000751a820:      	bl	0xfffffe00079aff14 <_nwo_authorized>
fffffe000751a824:      	cbz	w0, 0xfffffe000751a854 <_nss_worker_continue+0x18fc>
fffffe000751a828:      	mov	w26, #1
fffffe000751a82c:      	ldr	x0, [x24, #2456]
fffffe000751a830:      	adrp	x20, 0xfffffe00084ec000 <_nbc_run+0x4>
fffffe000751a834:      	add	x20, x20, #600
fffffe000751a838:      	str	w26, [x20]
fffffe000751a83c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a840:      	ldr	x0, [x20, #152]
fffffe000751a844:      	mov	w1, #1
fffffe000751a848:      	bl	0xfffffe0007ae5b4c <_nord2_wifi_ifnet_link>
fffffe000751a84c:      	mov	w21, w0
fffffe000751a850:      	b	0xfffffe000751a864 <_nss_worker_continue+0x190c>
fffffe000751a854:      	ldr	x0, [x24, #2456]
fffffe000751a858:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a85c:      	mov	w26, wzr
fffffe000751a860:      	mov	w21, wzr
fffffe000751a864:      	ldr	x0, [x24, #2456]
fffffe000751a868:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a86c:      	adrp	x9, 0xfffffe00084ec000 <_nbc_run+0x40>
fffffe000751a870:      	add	x9, x9, #560
fffffe000751a874:      	str	wzr, [x9, #40]
fffffe000751a878:      	cbz	w26, 0xfffffe000751a8b4 <_nss_worker_continue+0x195c>
fffffe000751a87c:      	cbnz	w21, 0xfffffe000751a8b4 <_nss_worker_continue+0x195c>
fffffe000751a880:      	ldr	w8, [x9]
fffffe000751a884:      	cbnz	w8, 0xfffffe000751a8b4 <_nss_worker_continue+0x195c>
fffffe000751a888:      	ldr	w8, [x9, #4]
fffffe000751a88c:      	cbnz	w8, 0xfffffe000751a8b4 <_nss_worker_continue+0x195c>
fffffe000751a890:      	mov	w8, #1
fffffe000751a894:      	adrp	x20, 0xfffffe00084ec000 <_nbc_run+0x68>
fffffe000751a898:      	add	x20, x20, #576
fffffe000751a89c:      	str	w8, [x20, #20]
fffffe000751a8a0:      	bl	0xfffffe000751cf08 <_njs_snapshot_locked>
fffffe000751a8a4:      	ldr	w8, [x20]
fffffe000751a8a8:      	cbz	w8, 0xfffffe000751a918 <_nss_worker_continue+0x19c0>
fffffe000751a8ac:      	str	wzr, [sp, #424]
fffffe000751a8b0:      	b	0xfffffe000751a92c <_nss_worker_continue+0x19d4>
fffffe000751a8b4:      	adrp	x20, 0xfffffe00084ec000 <_nbc_run+0x88>
fffffe000751a8b8:      	add	x20, x20, #596
fffffe000751a8bc:      	ldr	x0, [sp, #240]
fffffe000751a8c0:      	ldr	x2, [sp, #400]
fffffe000751a8c4:      	ldur	x1, [x20, #156]
fffffe000751a8c8:      	str	wzr, [x20]
fffffe000751a8cc:      	bl	0xfffffe00079b0e24 <_nwn_cancel>
fffffe000751a8d0:      	ldr	w8, [x20, #12]
fffffe000751a8d4:      	mov	w1, #12112
fffffe000751a8d8:      	ldr	w9, [x20, #80]
fffffe000751a8dc:      	add	w8, w9, w8
fffffe000751a8e0:      	mov	w9, #16204
fffffe000751a8e4:      	movk	w9, #1, lsl #16
fffffe000751a8e8:      	add	x0, x20, x9
fffffe000751a8ec:      	str	w8, [x20, #80]
fffffe000751a8f0:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751a8f4:      	mov	w8, #28316
fffffe000751a8f8:      	str	xzr, [x20, #8]
fffffe000751a8fc:      	movk	w8, #1, lsl #16
fffffe000751a900:      	movi.2d	v0, #0000000000000000
fffffe000751a904:      	add	x8, x20, x8
fffffe000751a908:      	stp	q0, q0, [x8]
fffffe000751a90c:      	bl	0xfffffe000751cf08 <_njs_snapshot_locked>
fffffe000751a910:      	str	wzr, [sp, #424]
fffffe000751a914:      	b	0xfffffe000751a92c <_nss_worker_continue+0x19d4>
fffffe000751a918:      	mov	w10, #1
fffffe000751a91c:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0xf0>
fffffe000751a920:      	mov	w9, #1
fffffe000751a924:      	str	w10, [x8, #576]
fffffe000751a928:      	str	w9, [sp, #424]
fffffe000751a92c:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x100>
fffffe000751a930:      	ldr	x0, [x24, #2456]
fffffe000751a934:      	ldr	w20, [x8, #596]
fffffe000751a938:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a93c:      	cbz	w26, 0xfffffe000751a954 <_nss_worker_continue+0x19fc>
fffffe000751a940:      	cbnz	w20, 0xfffffe000751a954 <_nss_worker_continue+0x19fc>
fffffe000751a944:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x118>
fffffe000751a948:      	mov	w1, wzr
fffffe000751a94c:      	ldr	x0, [x8, #752]
fffffe000751a950:      	bl	0xfffffe0007ae5b4c <_nord2_wifi_ifnet_link>
fffffe000751a954:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x128>
fffffe000751a958:      	ldr	x0, [x8, #544]
fffffe000751a95c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a960:      	cbz	w21, 0xfffffe000751a9bc <_nss_worker_continue+0x1a64>
fffffe000751a964:      	ldr	x0, [x24, #2456]
fffffe000751a968:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751a96c:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x140>
fffffe000751a970:      	ldr	w8, [x8, #616]
fffffe000751a974:      	cmp	w8, #111
fffffe000751a978:      	b.hi	0xfffffe000751a9b4 <_nss_worker_continue+0x1a5c>
fffffe000751a97c:      	add	w8, w8, #1
fffffe000751a980:      	ldr	x0, [x24, #2456]
fffffe000751a984:      	adrp	x20, 0xfffffe00084ec000 <_nbc_run+0x158>
fffffe000751a988:      	add	x20, x20, #616
fffffe000751a98c:      	str	w8, [x20]
fffffe000751a990:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a994:      	ldr	x8, [x20, #136]
fffffe000751a998:      	adrp	x0, 0xfffffe0007096000 <_nss_worker_continue+0x830>
fffffe000751a99c:      	add	x0, x0, #3500
fffffe000751a9a0:      	mov	w9, #1
fffffe000751a9a4:      	str	x8, [sp]
fffffe000751a9a8:      	stp	x9, x21, [sp, #8]
fffffe000751a9ac:      	bl	0xfffffe000735dae4 <_printf>
fffffe000751a9b0:      	b	0xfffffe000751a9bc <_nss_worker_continue+0x1a64>
fffffe000751a9b4:      	ldr	x0, [x24, #2456]
fffffe000751a9b8:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a9bc:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x190>
fffffe000751a9c0:      	adrp	x21, 0xfffffe00084ec000 <_nbc_run+0x194>
fffffe000751a9c4:      	add	x21, x21, #544
fffffe000751a9c8:      	adrp	x22, 0xfffffe00084ec000 <_nbc_run+0x19c>
fffffe000751a9cc:      	add	x22, x22, #560
fffffe000751a9d0:      	ldr	w8, [x8, #612]
fffffe000751a9d4:      	cbz	w8, 0xfffffe000751aad8 <_nss_worker_continue+0x1b80>
fffffe000751a9d8:      	mov	x20, xzr
fffffe000751a9dc:      	adrp	x26, 0xfffffe0008503000 <_nbc_run+0x20c>
fffffe000751a9e0:      	add	x26, x26, #272
fffffe000751a9e4:      	b	0xfffffe000751aa20 <_nss_worker_continue+0x1ac8>
fffffe000751a9e8:      	ldr	x0, [x24, #2456]
fffffe000751a9ec:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751a9f0:      	mov	x0, x26
fffffe000751a9f4:      	mov	w1, #1514
fffffe000751a9f8:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751a9fc:      	ldr	w8, [x21, #68]
fffffe000751aa00:      	add	x9, x21, x20, lsl #2
fffffe000751aa04:      	add	x20, x20, #1
fffffe000751aa08:      	add	x26, x26, #1514
fffffe000751aa0c:      	cmp	x20, x8
fffffe000751aa10:      	mov	w8, #52624
fffffe000751aa14:      	movk	w8, #1, lsl #16
fffffe000751aa18:      	str	wzr, [x9, x8]
fffffe000751aa1c:      	b.hs	0xfffffe000751aad8 <_nss_worker_continue+0x1b80>
fffffe000751aa20:      	ldr	x0, [x24, #2456]
fffffe000751aa24:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751aa28:      	ldr	w8, [x22, #36]
fffffe000751aa2c:      	cbz	w8, 0xfffffe000751a9e8 <_nss_worker_continue+0x1a90>
fffffe000751aa30:      	ldr	w8, [x22]
fffffe000751aa34:      	cbnz	w8, 0xfffffe000751a9e8 <_nss_worker_continue+0x1a90>
fffffe000751aa38:      	ldr	w8, [x22, #4]
fffffe000751aa3c:      	cbnz	w8, 0xfffffe000751a9e8 <_nss_worker_continue+0x1a90>
fffffe000751aa40:      	add	x9, x21, x20, lsl #3
fffffe000751aa44:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x218>
fffffe000751aa48:      	add	x8, x8, #544
fffffe000751aa4c:      	ldr	x0, [x24, #2456]
fffffe000751aa50:      	ldr	x22, [x9, #216]
fffffe000751aa54:      	str	x9, [sp, #456]
fffffe000751aa58:      	ldr	x21, [x8, #208]
fffffe000751aa5c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751aa60:      	cmp	x22, x21
fffffe000751aa64:      	adrp	x22, 0xfffffe00084ec000 <_nbc_run+0x238>
fffffe000751aa68:      	add	x22, x22, #560
fffffe000751aa6c:      	adrp	x21, 0xfffffe00084ec000 <_nbc_run+0x240>
fffffe000751aa70:      	add	x21, x21, #544
fffffe000751aa74:      	b.ne	0xfffffe000751a9f0 <_nss_worker_continue+0x1a98>
fffffe000751aa78:      	mov	w9, #52624
fffffe000751aa7c:      	add	x8, x21, x20, lsl #2
fffffe000751aa80:      	movk	w9, #1, lsl #16
fffffe000751aa84:      	mov	x1, x26
fffffe000751aa88:      	ldr	w2, [x8, x9]
fffffe000751aa8c:      	ldr	x8, [sp, #456]
fffffe000751aa90:      	ldr	x0, [x8, #216]
fffffe000751aa94:      	bl	0xfffffe0007ae5df8 <_nord2_wifi_ifnet_receive>
fffffe000751aa98:      	cbnz	w0, 0xfffffe000751a9f0 <_nss_worker_continue+0x1a98>
fffffe000751aa9c:      	ldr	x0, [x24, #2456]
fffffe000751aaa0:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751aaa4:      	adrp	x10, 0xfffffe00084ec000 <_nbc_run+0x278>
fffffe000751aaa8:      	add	x10, x10, #680
fffffe000751aaac:      	ldp	w9, w8, [x10]
fffffe000751aab0:      	add	w9, w9, #1
fffffe000751aab4:      	cmp	w8, #7
fffffe000751aab8:      	str	w9, [x10]
fffffe000751aabc:      	b.hi	0xfffffe000751a9e8 <_nss_worker_continue+0x1a90>
fffffe000751aac0:      	add	w8, w8, #1
fffffe000751aac4:      	adrp	x9, 0xfffffe00084ec000 <_nbc_run+0x298>
fffffe000751aac8:      	mov	w0, #1
fffffe000751aacc:      	str	w8, [x9, #684]
fffffe000751aad0:      	bl	0xfffffe000750e938 <_njs_trace_locked>
fffffe000751aad4:      	b	0xfffffe000751a9e8 <_nss_worker_continue+0x1a90>
fffffe000751aad8:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x2ac>
fffffe000751aadc:      	str	wzr, [x8, #612]
fffffe000751aae0:      	ldr	w8, [sp, #448]
fffffe000751aae4:      	cbz	w8, 0xfffffe000751aaf4 <_nss_worker_continue+0x1b9c>
fffffe000751aae8:      	mov	w0, #4
fffffe000751aaec:      	bl	0xfffffe000751d094 <_njs_notice>
fffffe000751aaf0:      	b	0xfffffe000751ab0c <_nss_worker_continue+0x1bb4>
fffffe000751aaf4:      	ldr	w8, [sp, #424]
fffffe000751aaf8:      	ldr	w9, [sp, #408]
fffffe000751aafc:      	cmp	w8, #0
fffffe000751ab00:      	cset	w8, eq
fffffe000751ab04:      	orr	w8, w8, w9
fffffe000751ab08:      	tbz	w8, #0, 0xfffffe000751abc8 <_nss_worker_continue+0x1c70>
fffffe000751ab0c:      	ldr	w8, [sp, #432]
fffffe000751ab10:      	cbz	w8, 0xfffffe000751ab2c <_nss_worker_continue+0x1bd4>
fffffe000751ab14:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x2e8>
fffffe000751ab18:      	ldr	w8, [x8, #576]
fffffe000751ab1c:      	cmp	w8, #0
fffffe000751ab20:      	mov	w8, #2
fffffe000751ab24:      	cinc	w0, w8, ne
fffffe000751ab28:      	bl	0xfffffe000751d094 <_njs_notice>
fffffe000751ab2c:      	ldr	w8, [sp, #444]
fffffe000751ab30:      	cmp	w8, #1
fffffe000751ab34:      	b.hi	0xfffffe000751ab80 <_nss_worker_continue+0x1c28>
fffffe000751ab38:      	mov	w0, #4
fffffe000751ab3c:      	bl	0xfffffe000751d094 <_njs_notice>
fffffe000751ab40:      	adrp	x8, 0xfffffe00084ff000 <_nbc_run+0x360>
fffffe000751ab44:      	ldr	w8, [x8, #788]
fffffe000751ab48:      	cmp	w8, #21
fffffe000751ab4c:      	b.ne	0xfffffe0007519798 <_nss_worker_continue+0x840>
fffffe000751ab50:      	adrp	x8, 0xfffffe000850d000 <_nbc_run+0x3a8>
fffffe000751ab54:      	ldr	x8, [x8, #3176]
fffffe000751ab58:      	cmp	x8, x28
fffffe000751ab5c:      	b.ne	0xfffffe0007519798 <_nss_worker_continue+0x840>
fffffe000751ab60:      	ldr	x0, [x24, #2456]
fffffe000751ab64:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751ab68:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x33c>
fffffe000751ab6c:      	ldr	w8, [x8, #592]
fffffe000751ab70:      	cbz	w8, 0xfffffe000751abfc <_nss_worker_continue+0x1ca4>
fffffe000751ab74:      	ldr	x0, [x24, #2456]
fffffe000751ab78:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751ab7c:      	b	0xfffffe0007519798 <_nss_worker_continue+0x840>
fffffe000751ab80:      	add	x2, sp, #544
fffffe000751ab84:      	mov	w0, #1000
fffffe000751ab88:      	mov	w1, #1000
fffffe000751ab8c:      	str	xzr, [sp, #544]
fffffe000751ab90:      	bl	0xfffffe0007333480 <_clock_interval_to_deadline>
fffffe000751ab94:      	adrp	x20, 0xfffffe00084d1000 <_nbc_run+0x2fc>
fffffe000751ab98:      	add	x20, x20, #2456
fffffe000751ab9c:      	ldr	x0, [x20]
fffffe000751aba0:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751aba4:      	ldr	x2, [sp, #544]
fffffe000751aba8:      	mov	x0, x20
fffffe000751abac:      	mov	w1, wzr
fffffe000751abb0:      	bl	0xfffffe0007368a44 <_assert_wait_deadline>
fffffe000751abb4:      	ldr	x0, [x20]
fffffe000751abb8:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751abbc:      	mov	x0, xzr
fffffe000751abc0:      	bl	0xfffffe0007369140 <_thread_block>
fffffe000751abc4:      	b	0xfffffe0007519798 <_nss_worker_continue+0x840>
fffffe000751abc8:      	ldr	x0, [x24, #2456]
fffffe000751abcc:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751abd0:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x3a4>
fffffe000751abd4:      	add	x8, x8, #560
fffffe000751abd8:      	ldr	x0, [x24, #2456]
fffffe000751abdc:      	ldp	w22, w20, [x8]
fffffe000751abe0:      	ldr	w21, [x8, #36]
fffffe000751abe4:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751abe8:      	cbz	w21, 0xfffffe000751ab0c <_nss_worker_continue+0x1bb4>
fffffe000751abec:      	cbnz	w22, 0xfffffe000751ab0c <_nss_worker_continue+0x1bb4>
fffffe000751abf0:      	cbnz	w20, 0xfffffe000751ab0c <_nss_worker_continue+0x1bb4>
fffffe000751abf4:      	mov	w0, #1
fffffe000751abf8:      	b	0xfffffe000751aaec <_nss_worker_continue+0x1b94>
fffffe000751abfc:      	adrp	x8, 0xfffffe000850f000 <_nbc_run+0x45c>
fffffe000751ac00:      	adrp	x11, 0xfffffe000850d000 <_nbc_run+0x458>
fffffe000751ac04:      	add	x11, x11, #3188
fffffe000751ac08:      	adrp	x15, 0xfffffe0007c54000 <_nct_native_report+0x150>
fffffe000751ac0c:      	add	x15, x15, #940
fffffe000751ac10:      	adrp	x14, 0xfffffe0007c90000 <_njs_cache_scan_locked+0xb4>
fffffe000751ac14:      	ldr	s0, [x8, #3756]
fffffe000751ac18:      	adrp	x8, 0xfffffe0008510000 <_nbc_run+0x47c>
fffffe000751ac1c:      	add	x8, x8, #128
fffffe000751ac20:      	ldp	w10, w9, [x8]
fffffe000751ac24:      	adrp	x8, 0xfffffe00084d1000 <_nbc_run+0x38c>
fffffe000751ac28:      	ldr	w8, [x8, #2468]
fffffe000751ac2c:      	cmp	w10, #0
fffffe000751ac30:      	ld1.s	{ v0 }[1], [x11], #4
fffffe000751ac34:      	ldur	q2, [x15, #8]
fffffe000751ac38:      	cset	w10, ne
fffffe000751ac3c:      	add	x14, x14, #584
fffffe000751ac40:      	cmp	w9, #0
fffffe000751ac44:      	ldr	s3, [x15]
fffffe000751ac48:      	adrp	x15, 0xfffffe00084ec000 <_nbc_run+0x41c>
fffffe000751ac4c:      	add	x15, x15, #552
fffffe000751ac50:      	ldr	d5, [x11]
fffffe000751ac54:      	add	x12, x14, #300
fffffe000751ac58:      	add	x13, x14, #16
fffffe000751ac5c:      	ext.16b	v3, v3, v3, #4
fffffe000751ac60:      	add	x11, x14, #60
fffffe000751ac64:      	dup.4s	v4, v2[3]
fffffe000751ac68:      	ldur	d6, [x15, #20]
fffffe000751ac6c:      	ldr	d1, [x12]
fffffe000751ac70:      	add	x12, x14, #280
fffffe000751ac74:      	mov	w14, #16092
fffffe000751ac78:      	ld1.s	{ v4 }[1], [x13]
fffffe000751ac7c:      	movk	w14, #1, lsl #16
fffffe000751ac80:      	ldr	q17, [sp, #192]
fffffe000751ac84:      	mov.d	v0[1], v5[0]
fffffe000751ac88:      	ldr	d7, [x15, #32]
fffffe000751ac8c:      	ext.16b	v2, v3, v2, #12
fffffe000751ac90:      	add	x13, x15, #3264
fffffe000751ac94:      	ldr	d5, [x15, x14]
fffffe000751ac98:      	cset	w9, ne
fffffe000751ac9c:      	ldr	x14, [sp, #272]
fffffe000751aca0:      	cmp	w8, #0
fffffe000751aca4:      	cmtst.4s	v16, v0, v0
fffffe000751aca8:      	ld1.s	{ v4 }[2], [x11]
fffffe000751acac:      	cmeq.4s	v2, v2, v17
fffffe000751acb0:      	add	x11, x15, #140
fffffe000751acb4:      	mov.d	v1[1], v6[0]
fffffe000751acb8:      	ldr	s6, [x14, #2880]
fffffe000751acbc:      	mvn.16b	v17, v2
fffffe000751acc0:      	ldur	d3, [x15, #44]
fffffe000751acc4:      	uzp1.8h	v16, v16, v17
fffffe000751acc8:      	ldr	d17, [x15, #56]
fffffe000751accc:      	uzp1.4s	v6, v6, v5
fffffe000751acd0:      	ld1.s	{ v7 }[1], [x11]
fffffe000751acd4:      	add	x11, x15, #3272
fffffe000751acd8:      	ld1.s	{ v4 }[3], [x12]
fffffe000751acdc:      	ld1.s	{ v17 }[2], [x13]
fffffe000751ace0:      	add	x12, x14, #4
fffffe000751ace4:      	ldr	q18, [sp, #160]
fffffe000751ace8:      	mov.s	v6[1], v5[1]
fffffe000751acec:      	ldr	q5, [x11]
fffffe000751acf0:      	mov.d	v7[1], v3[0]
fffffe000751acf4:      	add	x11, x14, #3568
fffffe000751acf8:      	cmtst.4s	v3, v1, v1
fffffe000751acfc:      	mov.s	v17[3], v5[3]
fffffe000751ad00:      	ld1.s	{ v5 }[3], [x12]
fffffe000751ad04:      	cmtst.4s	v4, v4, v4
fffffe000751ad08:      	ld1.s	{ v6 }[3], [x11]
fffffe000751ad0c:      	cmeq.4s	v0, v0, #0
fffffe000751ad10:      	cmeq.4s	v1, v1, #0
fffffe000751ad14:      	uzp1.8h	v3, v4, v3
fffffe000751ad18:      	cmtst.4s	v4, v7, v7
fffffe000751ad1c:      	cmeq.4s	v17, v17, v18
fffffe000751ad20:      	uzp1.8h	v0, v0, v2
fffffe000751ad24:      	mvn.16b	v2, v17
fffffe000751ad28:      	uzp1.8h	v2, v4, v2
fffffe000751ad2c:      	cmtst.4s	v4, v6, v6
fffffe000751ad30:      	ldr	q6, [sp, #144]
fffffe000751ad34:      	xtn2.8h	v1, v1
fffffe000751ad38:      	cmeq.4s	v5, v5, v6
fffffe000751ad3c:      	mvn.16b	v5, v5
fffffe000751ad40:      	uzp1.8h	v4, v5, v4
fffffe000751ad44:      	uzp1.16b	v5, v16, v3
fffffe000751ad48:      	uzp1.16b	v6, v0, v1
fffffe000751ad4c:      	cmeq.4s	v0, v7, #0
fffffe000751ad50:      	uzp1.16b	v1, v2, v4
fffffe000751ad54:      	ldr	q2, [sp, #176]
fffffe000751ad58:      	xtn.4h	v0, v0
fffffe000751ad5c:      	xtn.8b	v0, v0
fffffe000751ad60:      	tbl.16b	v2, { v5, v6 }, v2
fffffe000751ad64:      	mov.b	v1[1], v0[1]
fffffe000751ad68:      	orr.16b	v0, v2, v1
fffffe000751ad6c:      	umaxv.16b	b0, v0
fffffe000751ad70:      	fmov	w11, s0
fffffe000751ad74:      	orr	w8, w11, w9
fffffe000751ad78:      	cset	w9, ne
fffffe000751ad7c:      	orr	w8, w8, w10
fffffe000751ad80:      	orr	w8, w8, w9
fffffe000751ad84:      	tbnz	w8, #0, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751ad88:      	adrp	x9, 0xfffffe00084ec000 <_nbc_run+0x55c>
fffffe000751ad8c:      	add	x9, x9, #552
fffffe000751ad90:      	ldp	w8, w9, [x9]
fffffe000751ad94:      	orr	w8, w8, w9
fffffe000751ad98:      	cbnz	w8, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751ad9c:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x570>
fffffe000751ada0:      	add	x8, x8, #552
fffffe000751ada4:      	ldr	w8, [x8, #28]
fffffe000751ada8:      	cbz	w8, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751adac:      	ldr	x8, [sp, #272]
fffffe000751adb0:      	ldr	w8, [x8, #3580]
fffffe000751adb4:      	cbz	w8, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751adb8:      	adrp	x8, 0xfffffe00084ec000 <_nbc_run+0x58c>
fffffe000751adbc:      	add	x8, x8, #552
fffffe000751adc0:      	ldr	x8, [x8, #200]
fffffe000751adc4:      	cbz	x8, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751adc8:      	ldr	x9, [sp, #272]
fffffe000751adcc:      	ldr	x9, [x9, #1240]
fffffe000751add0:      	cmp	x9, x8
fffffe000751add4:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751add8:      	ldr	x9, [sp, #272]
fffffe000751addc:      	ldr	x9, [x9, #3560]
fffffe000751ade0:      	cbnz	x9, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751ade4:      	adrp	x9, 0xfffffe00084ec000 <_nbc_run+0x5b8>
fffffe000751ade8:      	add	x9, x9, #552
fffffe000751adec:      	ldr	x9, [x9, #336]
fffffe000751adf0:      	cbnz	x9, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751adf4:      	adrp	x9, 0xfffffe00084ec000 <_nbc_run+0x5c8>
fffffe000751adf8:      	add	x9, x9, #552
fffffe000751adfc:      	ldr	x9, [x9, #344]
fffffe000751ae00:      	cbnz	x9, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751ae04:      	ldr	x9, [sp, #272]
fffffe000751ae08:      	adrp	x10, 0xfffffe0008508000 <_nbc_run+0x64c>
fffffe000751ae0c:      	add	x10, x10, #4080
fffffe000751ae10:      	ldr	x9, [x9, #3584]
fffffe000751ae14:      	cmp	x9, x10
fffffe000751ae18:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751ae1c:      	adrp	x0, 0xfffffe00084ff000 <_nbc_run+0x63c>
fffffe000751ae20:      	add	x0, x0, #2080
fffffe000751ae24:      	adrp	x17, 0xfffffe0008508000 <_nbc_run+0x668>
fffffe000751ae28:      	add	x17, x17, #4080
fffffe000751ae2c:      	add	x9, x17, #28
fffffe000751ae30:      	adrp	x14, 0xfffffe0008512000 <_rr+0x28>
fffffe000751ae34:      	add	x14, x14, #112
fffffe000751ae38:      	ldr	d0, [x0, #2352]
fffffe000751ae3c:      	ldr	d1, [x17, #9176]
fffffe000751ae40:      	ldur	d2, [x17, #44]
fffffe000751ae44:      	ld1.s	{ v0 }[2], [x9]
fffffe000751ae48:      	add	x9, x17, #36
fffffe000751ae4c:      	ldr	w11, [x17, #148]
fffffe000751ae50:      	mov.d	v2[1], v1[0]
fffffe000751ae54:      	ldr	q1, [sp, #128]
fffffe000751ae58:      	ld1.s	{ v0 }[3], [x9]
fffffe000751ae5c:      	ldr	x9, [x17, #16]
fffffe000751ae60:      	cmeq.4s	v1, v2, v1
fffffe000751ae64:      	cmeq.4s	v2, v0, #0
fffffe000751ae68:      	cmp	x9, x14
fffffe000751ae6c:      	cmtst.4s	v0, v0, v0
fffffe000751ae70:      	cset	w9, eq
fffffe000751ae74:      	uzp1.8h	v1, v2, v1
fffffe000751ae78:      	cmp	w11, #0
fffffe000751ae7c:      	xtn.4h	v0, v0
fffffe000751ae80:      	uzp1.16b	v0, v1, v0
fffffe000751ae84:      	tbl.8b	v0, { v0 }, v9
fffffe000751ae88:      	umov.b	w10, v0[1]
fffffe000751ae8c:      	umov.b	w12, v0[0]
fffffe000751ae90:      	umov.b	w13, v0[2]
fffffe000751ae94:      	umov.b	w14, v0[3]
fffffe000751ae98:      	umov.b	w15, v0[4]
fffffe000751ae9c:      	umov.b	w16, v0[5]
fffffe000751aea0:      	and	w10, w10, #0x1
fffffe000751aea4:      	and	w12, w12, #0x1
fffffe000751aea8:      	and	w13, w13, #0x1
fffffe000751aeac:      	and	w14, w14, #0x1
fffffe000751aeb0:      	and	w15, w15, #0x1
fffffe000751aeb4:      	and	w11, w16, #0x1
fffffe000751aeb8:      	bfi	w12, w10, #1, #1
fffffe000751aebc:      	umov.b	w10, v0[6]
fffffe000751aec0:      	bfi	w12, w13, #2, #1
fffffe000751aec4:      	umov.b	w13, v0[7]
fffffe000751aec8:      	bfi	w12, w14, #3, #1
fffffe000751aecc:      	ldr	x14, [x0]
fffffe000751aed0:      	bfi	w12, w15, #4, #1
fffffe000751aed4:      	and	w10, w10, #0x1
fffffe000751aed8:      	bfi	w12, w11, #5, #1
fffffe000751aedc:      	add	x11, x0, #2352
fffffe000751aee0:      	orr	w10, w12, w10, lsl #6
fffffe000751aee4:      	ldr	w12, [x17, #40]
fffffe000751aee8:      	ccmp	x14, x11, #0, eq
fffffe000751aeec:      	orr	w10, w10, w13, lsl #7
fffffe000751aef0:      	mov	w11, #255
fffffe000751aef4:      	bic	w10, w11, w10
fffffe000751aef8:      	orr	w10, w10, w12
fffffe000751aefc:      	ccmp	w10, #0, #0, eq
fffffe000751af00:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af04:      	adrp	x10, 0xfffffe0008508000 <_coherent+0x20>
fffffe000751af08:      	add	x10, x10, #4080
fffffe000751af0c:      	adrp	x11, 0xfffffe0007c53000 <_njs_early_finish+0x70>
fffffe000751af10:      	add	x11, x11, #1624
fffffe000751af14:      	ldr	x10, [x10]
fffffe000751af18:      	cmp	x10, x11
fffffe000751af1c:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af20:      	adrp	x10, 0xfffffe0008508000 <_coherent+0x3c>
fffffe000751af24:      	add	x10, x10, #4080
fffffe000751af28:      	ldr	x10, [x10, #8]
fffffe000751af2c:      	cmp	x10, x19
fffffe000751af30:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af34:      	tbz	w9, #0, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af38:      	adrp	x9, 0xfffffe00084ff000 <_coherent+0x30>
fffffe000751af3c:      	add	x9, x9, #2080
fffffe000751af40:      	ldr	x9, [x9, #2360]
fffffe000751af44:      	cbnz	x9, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af48:      	adrp	x9, 0xfffffe0008508000 <_coherent+0x64>
fffffe000751af4c:      	add	x9, x9, #4080
fffffe000751af50:      	ldr	x9, [x9, #152]
fffffe000751af54:      	cmp	x9, x8
fffffe000751af58:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af5c:      	mov	x0, x19
fffffe000751af60:      	bl	0xfffffe0007996cac <_nct_done_receipt>
fffffe000751af64:      	cbz	w0, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af68:      	adrp	x8, 0xfffffe0008508000 <_coherent+0x84>
fffffe000751af6c:      	adrp	x10, 0xfffffe000850b000 <_coherent+0x94>
fffffe000751af70:      	add	x10, x10, #1744
fffffe000751af74:      	ldr	x8, [x8, #4088]
fffffe000751af78:      	ldr	x9, [x8]
fffffe000751af7c:      	cmp	x9, x10
fffffe000751af80:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af84:      	ldr	x9, [x8, #8]
fffffe000751af88:      	adrp	x10, 0xfffffe000850c000 <_coherent+0xb4>
fffffe000751af8c:      	add	x10, x10, #2120
fffffe000751af90:      	cmp	x9, x10
fffffe000751af94:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751af98:      	ldr	x9, [x8, #16]
fffffe000751af9c:      	adrp	x10, 0xfffffe0007c53000 <_njs_event+0x60>
fffffe000751afa0:      	add	x10, x10, #1624
fffffe000751afa4:      	cmp	x9, x10
fffffe000751afa8:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751afac:      	adrp	x10, 0xfffffe0007c90000 <_njs_event+0x164>
fffffe000751afb0:      	ldr	x9, [x8, #24]
fffffe000751afb4:      	add	x10, x10, #584
fffffe000751afb8:      	cmp	x9, x10
fffffe000751afbc:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751afc0:      	adrp	x9, 0xfffffe0008509000 <_coherent+0xe0>
fffffe000751afc4:      	add	x9, x9, #8
fffffe000751afc8:      	ldr	w10, [x8, #132]
fffffe000751afcc:      	ldr	w11, [x8, #68]
fffffe000751afd0:      	ldr	w9, [x9, #64]
fffffe000751afd4:      	add	w10, w11, w10
fffffe000751afd8:      	cmp	w9, w10
fffffe000751afdc:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751afe0:      	adrp	x10, 0xfffffe0008509000 <_coherent+0x100>
fffffe000751afe4:      	nop
fffffe000751afe8:      	ldr	w10, [x10, #8]
fffffe000751afec:      	cbz	w10, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751aff0:      	adrp	x10, 0xfffffe0008509000 <_coherent+0x110>
fffffe000751aff4:      	add	x10, x10, #8
fffffe000751aff8:      	ldr	w10, [x10, #88]
fffffe000751affc:      	cmp	w10, #23
fffffe000751b000:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b004:      	adrp	x10, 0xfffffe0008509000 <_coherent+0x120>
fffffe000751b008:      	add	x10, x10, #8
fffffe000751b00c:      	ldr	w10, [x10, #104]
fffffe000751b010:      	cbnz	w10, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b014:      	adrp	x13, 0xfffffe0008509000 <_coherent+0x130>
fffffe000751b018:      	add	x13, x13, #40
fffffe000751b01c:      	add	x10, x13, #868
fffffe000751b020:      	add	x11, x13, #896
fffffe000751b024:      	ldr	d0, [x13]
fffffe000751b028:      	ldr	d1, [x10]
fffffe000751b02c:      	add	x10, x13, #864
fffffe000751b030:      	ldr	w12, [x13, #900]
fffffe000751b034:      	ld1.s	{ v0 }[2], [x11]
fffffe000751b038:      	add	x11, x13, #888
fffffe000751b03c:      	ld1.s	{ v1 }[2], [x10]
fffffe000751b040:      	add	x10, x13, #576
fffffe000751b044:      	ld1.s	{ v0 }[3], [x11]
fffffe000751b048:      	adrp	x11, 0xfffffe0007c54000 <_njs_event+0x10c>
fffffe000751b04c:      	ld1.s	{ v1 }[3], [x10]
fffffe000751b050:      	ldr	w10, [x13, #60]
fffffe000751b054:      	ldrb	w11, [x11, #404]
fffffe000751b058:      	cmtst.4s	v0, v0, v0
fffffe000751b05c:      	cmtst.4s	v1, v1, v1
fffffe000751b060:      	sub	w11, w11, #1
fffffe000751b064:      	cmp	w10, w11, uxtb
fffffe000751b068:      	ldr	w10, [x13, #80]
fffffe000751b06c:      	uzp1.8h	v0, v0, v1
fffffe000751b070:      	cset	w11, eq
fffffe000751b074:      	cmp	w12, #3
fffffe000751b078:      	cset	w12, eq
fffffe000751b07c:      	cmp	w10, #0
fffffe000751b080:      	xtn.8b	v0, v0
fffffe000751b084:      	umaxv.8b	b0, v0
fffffe000751b088:      	fmov	w13, s0
fffffe000751b08c:      	bic	w10, w12, w13
fffffe000751b090:      	cset	w12, ne
fffffe000751b094:      	and	w10, w10, w11
fffffe000751b098:      	and	w10, w10, w12
fffffe000751b09c:      	cmp	w10, #1
fffffe000751b0a0:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b0a4:      	adrp	x10, 0xfffffe0008509000 <_sy+0x78>
fffffe000751b0a8:      	add	x10, x10, #40
fffffe000751b0ac:      	ldr	w10, [x10, #720]
fffffe000751b0b0:      	cbnz	w10, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b0b4:      	adrp	x10, 0xfffffe0008509000 <_sy+0x88>
fffffe000751b0b8:      	add	x10, x10, #40
fffffe000751b0bc:      	ldrb	w10, [x10, #929]
fffffe000751b0c0:      	cbnz	w10, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b0c4:      	adrp	x10, 0xfffffe0008509000 <_sy+0x98>
fffffe000751b0c8:      	ldr	w10, [x10, #76]
fffffe000751b0cc:      	cbz	w10, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b0d0:      	adrp	x11, 0xfffffe0008509000 <_sy+0xa4>
fffffe000751b0d4:      	ldr	w12, [x11, #80]
fffffe000751b0d8:      	cmn	w12, w9
fffffe000751b0dc:      	cset	w11, hs
fffffe000751b0e0:      	cmp	w10, w12
fffffe000751b0e4:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b0e8:      	tbnz	w11, #0, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b0ec:      	adrp	x11, 0xfffffe0007c53000 <_njs_event+0x1ac>
fffffe000751b0f0:      	add	w9, w10, w9
fffffe000751b0f4:      	ldr	w11, [x11, #3544]
fffffe000751b0f8:      	cmp	w11, w9
fffffe000751b0fc:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b100:      	cmn	w11, #1
fffffe000751b104:      	b.eq	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b108:      	adrp	x10, 0xfffffe0007c53000 <_njs_event+0x1c8>
fffffe000751b10c:      	add	x10, x10, #3532
fffffe000751b110:      	movi.2d	v1, #0000000000000000
fffffe000751b114:      	ldr	w9, [x10, #956]
fffffe000751b118:      	fmov	s0, w9
fffffe000751b11c:      	ld1.s	{ v0 }[1], [x10], #4
fffffe000751b120:      	ldr	d2, [x10]
fffffe000751b124:      	adrp	x10, 0xfffffe0008509000 <_mr+0x3c>
fffffe000751b128:      	add	x10, x10, #68
fffffe000751b12c:      	mov.d	v0[1], v2[0]
fffffe000751b130:      	ld1.s	{ v1 }[0], [x10]
fffffe000751b134:      	cmeq.4s	v0, v0, v1
fffffe000751b138:      	xtn.4h	v0, v0
fffffe000751b13c:      	umov.h	w10, v0[1]
fffffe000751b140:      	umov.h	w11, v0[2]
fffffe000751b144:      	umov.h	w12, v0[0]
fffffe000751b148:      	umov.h	w13, v0[3]
fffffe000751b14c:      	and	w10, w10, #0x1
fffffe000751b150:      	and	w11, w11, #0x1
fffffe000751b154:      	bfi	w12, w10, #1, #1
fffffe000751b158:      	mov	w10, #15
fffffe000751b15c:      	bfi	w12, w11, #2, #1
fffffe000751b160:      	bfi	w12, w13, #3, #29
fffffe000751b164:      	bics	wzr, w10, w12
fffffe000751b168:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b16c:      	cmp	w9, #1023
fffffe000751b170:      	b.hi	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b174:      	adrp	x9, 0xfffffe0007c53000 <_njs_event+0x234>
fffffe000751b178:      	add	x9, x9, #1636
fffffe000751b17c:      	ldr	w9, [x9, #1864]
fffffe000751b180:      	cbnz	w9, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b184:      	adrp	x9, 0xfffffe0007c53000 <_njs_event+0x244>
fffffe000751b188:      	nop
fffffe000751b18c:      	ldr	w9, [x9, #1636]
fffffe000751b190:      	cbnz	w9, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b194:      	adrp	x9, 0xfffffe0008509000 <_mr+0xac>
fffffe000751b198:      	ldr	w9, [x9, #56]
fffffe000751b19c:      	cbnz	w9, 0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b1a0:      	adrp	x10, 0xfffffe0008509000 <_mr+0xb8>
fffffe000751b1a4:      	add	x10, x10, #48
fffffe000751b1a8:      	ldp	w10, w9, [x10]
fffffe000751b1ac:      	cmp	w9, w10
fffffe000751b1b0:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b1b4:      	adrp	x11, 0xfffffe0008509000 <_mw+0x10>
fffffe000751b1b8:      	add	x11, x11, #60
fffffe000751b1bc:      	ldp	w10, w11, [x11]
fffffe000751b1c0:      	cmp	w10, w11
fffffe000751b1c4:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b1c8:      	ldr	w8, [x8, #108]
fffffe000751b1cc:      	cmp	w9, w8
fffffe000751b1d0:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b1d4:      	adrp	x8, 0xfffffe000850b000 <_mw+0x38>
fffffe000751b1d8:      	ldr	w8, [x8, #2176]
fffffe000751b1dc:      	sub	w8, w8, #1
fffffe000751b1e0:      	cmp	w9, w8
fffffe000751b1e4:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b1e8:      	adrp	x8, 0xfffffe0007c54000 <_njs_event+0x2ac>
fffffe000751b1ec:      	adrp	x9, 0xfffffe0008509000 <_mw+0x48>
fffffe000751b1f0:      	ldr	w8, [x8, #396]
fffffe000751b1f4:      	ldr	w9, [x9, #328]
fffffe000751b1f8:      	cmp	w8, w9
fffffe000751b1fc:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b200:      	mov	x8, xzr
fffffe000751b204:      	adrp	x9, 0xfffffe0008509000 <_mw+0x60>
fffffe000751b208:      	add	x9, x9, #252
fffffe000751b20c:      	adrp	x10, 0xfffffe0007c53000 <_njs_event+0x2cc>
fffffe000751b210:      	add	x10, x10, #1624
fffffe000751b214:      	add	x10, x10, x8
fffffe000751b218:      	ldur	x11, [x9, #-36]
fffffe000751b21c:      	ldr	x12, [x10, #216]
fffffe000751b220:      	cmp	x11, x12
fffffe000751b224:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b228:      	ldur	x11, [x9, #-28]
fffffe000751b22c:      	ldr	x10, [x10, #224]
fffffe000751b230:      	cmp	x11, x10
fffffe000751b234:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b238:      	adrp	x10, 0xfffffe0007c53000 <_njs_snapshot_locked+0x10>
fffffe000751b23c:      	add	x10, x10, #1624
fffffe000751b240:      	add	x11, x10, x8
fffffe000751b244:      	ldur	w10, [x9, #-20]
fffffe000751b248:      	ldr	w12, [x11, #232]
fffffe000751b24c:      	cmp	w10, w12
fffffe000751b250:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b254:      	ldur	w10, [x9, #-16]
fffffe000751b258:      	ldr	w11, [x11, #236]
fffffe000751b25c:      	cmp	w10, w11
fffffe000751b260:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b264:      	adrp	x11, 0xfffffe0007c53000 <_njs_snapshot_locked+0x3c>
fffffe000751b268:      	add	x11, x11, #1624
fffffe000751b26c:      	add	x11, x11, x8
fffffe000751b270:      	ldur	w12, [x9, #-12]
fffffe000751b274:      	ldr	w13, [x11, #240]
fffffe000751b278:      	cmp	w12, w13
fffffe000751b27c:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b280:      	ldur	w12, [x9, #-8]
fffffe000751b284:      	ldr	w11, [x11, #244]
fffffe000751b288:      	cmp	w12, w11
fffffe000751b28c:      	b.ne	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b290:      	ldur	w11, [x9, #-4]
fffffe000751b294:      	cmp	w11, w10
fffffe000751b298:      	b.hs	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b29c:      	ldr	w10, [x9]
fffffe000751b2a0:      	cmp	w10, #2
fffffe000751b2a4:      	b.hs	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b2a8:      	add	x8, x8, #32
fffffe000751b2ac:      	add	x9, x9, #80
fffffe000751b2b0:      	cmp	x8, #160
fffffe000751b2b4:      	b.ne	0xfffffe000751b20c <_nss_worker_continue+0x22b4>
fffffe000751b2b8:      	adrp	x9, 0xfffffe0008509000 <_rw+0x50>
fffffe000751b2bc:      	add	x9, x9, #20
fffffe000751b2c0:      	mov	w8, #1
fffffe000751b2c4:      	adrp	x10, 0xfffffe0008510000 <_rw+0x78>
fffffe000751b2c8:      	adrp	x0, 0xfffffe000850c000 <_rw+0x6c>
fffffe000751b2cc:      	add	x0, x0, #2120
fffffe000751b2d0:      	ldp	w21, w22, [x9]
fffffe000751b2d4:      	str	w8, [x10, #128]
fffffe000751b2d8:      	str	wzr, [sp, #480]
fffffe000751b2dc:      	str	d8, [x9]
fffffe000751b2e0:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b2e4:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b2e8:      	ldr	w8, [x25, #12]
fffffe000751b2ec:      	str	xzr, [sp, #544]
fffffe000751b2f0:      	str	xzr, [sp, #504]
fffffe000751b2f4:      	cbz	w8, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b2f8:      	ldr	w8, [x25, #20]
fffffe000751b2fc:      	cbnz	w8, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b300:      	ldr	x8, [x25]
fffffe000751b304:      	cmp	x8, x28
fffffe000751b308:      	b.ne	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b30c:      	add	x0, sp, #544
fffffe000751b310:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe000751b314:      	ldr	x0, [sp, #544]
fffffe000751b318:      	add	x1, sp, #504
fffffe000751b31c:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe000751b320:      	ldr	x8, [sp, #504]
fffffe000751b324:      	mov	x9, #63439
fffffe000751b328:      	movk	x9, #58195, lsl #16
fffffe000751b32c:      	movk	x9, #39845, lsl #32
fffffe000751b330:      	lsr	x8, x8, #3
fffffe000751b334:      	movk	x9, #8388, lsl #48
fffffe000751b338:      	umulh	x8, x8, x9
fffffe000751b33c:      	adrp	x9, 0xfffffe0008509000 <_rw+0xd4>
fffffe000751b340:      	add	x9, x9, #152
fffffe000751b344:      	lsr	x8, x8, #4
fffffe000751b348:      	ldr	x9, [x9]
fffffe000751b34c:      	cmp	x8, x9
fffffe000751b350:      	b.lo	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b354:      	adrp	x9, 0xfffffe0008509000 <_drain+0x14>
fffffe000751b358:      	add	x9, x9, #152
fffffe000751b35c:      	ldr	x9, [x9, #8]
fffffe000751b360:      	cmp	x8, x9
fffffe000751b364:      	b.hs	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b368:      	adrp	x9, 0xfffffe0008509000 <_drain+0x28>
fffffe000751b36c:      	adrp	x0, 0xfffffe000850c000 <_drain+0x38>
fffffe000751b370:      	add	x0, x0, #2120
fffffe000751b374:      	str	x8, [x9, #152]
fffffe000751b378:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b37c:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b380:      	adrp	x8, 0xfffffe000850b000 <_drain+0x48>
fffffe000751b384:      	mov	w9, #3
fffffe000751b388:      	ldr	x8, [x8, #1256]
fffffe000751b38c:      	ldr	w8, [x8, #240]
fffffe000751b390:      	dmb	oshld
fffffe000751b394:      	bics	wzr, w9, w8
fffffe000751b398:      	str	w8, [sp, #480]
fffffe000751b39c:      	b.ne	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b3a0:      	adrp	x0, 0xfffffe000850c000 <_drain+0x6c>
fffffe000751b3a4:      	add	x0, x0, #2120
fffffe000751b3a8:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b3ac:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b3b0:      	adrp	x8, 0xfffffe0007c54000 <_njs_notice>
fffffe000751b3b4:      	ldr	w8, [x8, #948]
fffffe000751b3b8:      	sub	w8, w8, #4
fffffe000751b3bc:      	cmp	w8, #3
fffffe000751b3c0:      	cset	w0, lo
fffffe000751b3c4:      	bl	0xfffffe000750a674 <_nwfn_guard>
fffffe000751b3c8:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b3cc:      	adrp	x8, 0xfffffe0008510000 <_drain+0xa8>
fffffe000751b3d0:      	ldr	w8, [x8, #184]
fffffe000751b3d4:      	cbz	w8, 0xfffffe000751b3e4 <_nss_worker_continue+0x248c>
fffffe000751b3d8:      	adrp	x8, 0xfffffe000850f000 <_drain+0xb0>
fffffe000751b3dc:      	ldr	w8, [x8, #3756]
fffffe000751b3e0:      	cbnz	w8, 0xfffffe000751b3f0 <_nss_worker_continue+0x2498>
fffffe000751b3e4:      	adrp	x0, 0xfffffe0007c53000 <_njs_notice+0x30>
fffffe000751b3e8:      	add	x0, x0, #1624
fffffe000751b3ec:      	bl	0xfffffe0007512418 <_nst_native_rx_active>
fffffe000751b3f0:      	adrp	x8, 0xfffffe00084d1000 <_rw+0xa8>
fffffe000751b3f4:      	mov	w9, #16896
fffffe000751b3f8:      	movk	w9, #6146, lsl #16
fffffe000751b3fc:      	adrp	x10, 0xfffffe0007c54000 <_njs_notice+0x4c>
fffffe000751b400:      	add	x10, x10, #1036
fffffe000751b404:      	ldr	x8, [x8, #1664]
fffffe000751b408:      	ldr	w8, [x8, #512]
fffffe000751b40c:      	dmb	oshld
fffffe000751b410:      	stp	w9, w8, [x10]
fffffe000751b414:      	tst	w8, #0x300000
fffffe000751b418:      	str	w8, [sp, #480]
fffffe000751b41c:      	mov	w8, #1
fffffe000751b420:      	str	w8, [x10, #8]
fffffe000751b424:      	b.ne	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b428:      	adrp	x0, 0xfffffe000850c000 <_drain+0xf4>
fffffe000751b42c:      	add	x0, x0, #2120
fffffe000751b430:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b434:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b438:      	adrp	x8, 0xfffffe0007c54000 <_njs_notice+0x88>
fffffe000751b43c:      	ldr	w8, [x8, #948]
fffffe000751b440:      	sub	w8, w8, #4
fffffe000751b444:      	cmp	w8, #3
fffffe000751b448:      	cset	w0, lo
fffffe000751b44c:      	bl	0xfffffe000750a674 <_nwfn_guard>
fffffe000751b450:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b454:      	adrp	x8, 0xfffffe0008510000 <_drain+0x130>
fffffe000751b458:      	ldr	w8, [x8, #184]
fffffe000751b45c:      	cbz	w8, 0xfffffe000751b46c <_nss_worker_continue+0x2514>
fffffe000751b460:      	adrp	x8, 0xfffffe000850f000 <_drain+0x138>
fffffe000751b464:      	ldr	w8, [x8, #3756]
fffffe000751b468:      	cbnz	w8, 0xfffffe000751b478 <_nss_worker_continue+0x2520>
fffffe000751b46c:      	adrp	x0, 0xfffffe0007c53000 <_njs_rng+0x8>
fffffe000751b470:      	add	x0, x0, #1624
fffffe000751b474:      	bl	0xfffffe0007512418 <_nst_native_rx_active>
fffffe000751b478:      	adrp	x8, 0xfffffe00084d1000 <_drain+0x58>
fffffe000751b47c:      	mov	w9, #16896
fffffe000751b480:      	movk	w9, #6146, lsl #16
fffffe000751b484:      	adrp	x10, 0xfffffe0007c54000 <_njs_rng+0x24>
fffffe000751b488:      	add	x10, x10, #1036
fffffe000751b48c:      	add	w9, w9, #1, lsl #12
fffffe000751b490:      	ldr	x8, [x8, #1664]
fffffe000751b494:      	ldr	w8, [x8, #4608]
fffffe000751b498:      	dmb	oshld
fffffe000751b49c:      	str	w8, [sp, #480]
fffffe000751b4a0:      	tst	w8, #0x300000
fffffe000751b4a4:      	stp	w9, w8, [x10]
fffffe000751b4a8:      	mov	w8, #1
fffffe000751b4ac:      	str	w8, [x10, #8]
fffffe000751b4b0:      	b.ne	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b4b4:      	adrp	x0, 0xfffffe000850c000 <_drain+0x180>
fffffe000751b4b8:      	add	x0, x0, #2120
fffffe000751b4bc:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b4c0:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b4c4:      	mov	w1, #21528
fffffe000751b4c8:      	add	x2, sp, #480
fffffe000751b4cc:      	adrp	x0, 0xfffffe0007c53000 <_njs_peer+0x38>
fffffe000751b4d0:      	add	x0, x0, #1624
fffffe000751b4d4:      	movk	w1, #6146, lsl #16
fffffe000751b4d8:      	bl	0xfffffe000750be10 <_nwfn_read>
fffffe000751b4dc:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b4e0:      	adrp	x9, 0xfffffe0008509000 <_drain+0x1a0>
fffffe000751b4e4:      	ldr	w8, [sp, #480]
fffffe000751b4e8:      	ldr	w9, [x9, #68]
fffffe000751b4ec:      	cmp	w8, w9
fffffe000751b4f0:      	b.ne	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b4f4:      	adrp	x0, 0xfffffe000850c000 <_drain+0x1c0>
fffffe000751b4f8:      	add	x0, x0, #2120
fffffe000751b4fc:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b500:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b504:      	mov	w8, #21528
fffffe000751b508:      	add	x2, sp, #480
fffffe000751b50c:      	movk	w8, #6146, lsl #16
fffffe000751b510:      	adrp	x0, 0xfffffe0007c53000 <_njs_peer+0x7c>
fffffe000751b514:      	add	x0, x0, #1624
fffffe000751b518:      	add	w1, w8, #4
fffffe000751b51c:      	bl	0xfffffe000750be10 <_nwfn_read>
fffffe000751b520:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b524:      	adrp	x8, 0xfffffe0008509000 <_drain+0x1e4>
fffffe000751b528:      	ldr	w20, [sp, #480]
fffffe000751b52c:      	ldr	w8, [x8, #68]
fffffe000751b530:      	cmp	w20, w8
fffffe000751b534:      	b.ne	0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b538:      	adrp	x8, 0xfffffe0007c54000 <_nss_build+0x1c>
fffffe000751b53c:      	adrp	x0, 0xfffffe000850c000 <_drain+0x208>
fffffe000751b540:      	add	x0, x0, #2120
fffffe000751b544:      	ldr	x26, [x8, #360]
fffffe000751b548:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b54c:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b550:      	adrp	x8, 0xfffffe0007c54000 <_nss_build+0x34>
fffffe000751b554:      	ldr	w8, [x8, #948]
fffffe000751b558:      	sub	w8, w8, #4
fffffe000751b55c:      	cmp	w8, #3
fffffe000751b560:      	cset	w0, lo
fffffe000751b564:      	bl	0xfffffe000750a674 <_nwfn_guard>
fffffe000751b568:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b56c:      	lsl	w8, w20, #4
fffffe000751b570:      	adrp	x0, 0xfffffe0007c53000 <_nss_build+0x50>
fffffe000751b574:      	add	x0, x0, #1624
fffffe000751b578:      	add	x26, x26, x8
fffffe000751b57c:      	mov	x1, x26
fffffe000751b580:      	mov	w2, #16
fffffe000751b584:      	mov	w3, wzr
fffffe000751b588:      	bl	0xfffffe0007512560 <_nwfn_memory_range>
fffffe000751b58c:      	cbnz	w0, 0xfffffe000751b5ac <_nss_worker_continue+0x2654>
fffffe000751b590:      	adrp	x0, 0xfffffe0007c53000 <_ncc_ap_report+0x8>
fffffe000751b594:      	add	x0, x0, #1624
fffffe000751b598:      	mov	x1, x26
fffffe000751b59c:      	mov	w2, #16
fffffe000751b5a0:      	mov	w3, #1
fffffe000751b5a4:      	bl	0xfffffe0007512560 <_nwfn_memory_range>
fffffe000751b5a8:      	cbz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b5ac:      	dsb	sy
fffffe000751b5b0:      	adrp	x0, 0xfffffe000850c000 <_drain+0x27c>
fffffe000751b5b4:      	add	x0, x0, #2120
fffffe000751b5b8:      	bl	0xfffffe0007511ffc <_ncd_native_owner>
fffffe000751b5bc:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b5c0:      	adrp	x8, 0xfffffe0007c54000 <_ncc_ap_report+0x3c>
fffffe000751b5c4:      	ldr	w8, [x8, #948]
fffffe000751b5c8:      	sub	w8, w8, #4
fffffe000751b5cc:      	cmp	w8, #3
fffffe000751b5d0:      	cset	w0, lo
fffffe000751b5d4:      	bl	0xfffffe000750a674 <_nwfn_guard>
fffffe000751b5d8:      	cbnz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b5dc:      	adrp	x0, 0xfffffe0007c53000 <_ncc_ap_report+0x54>
fffffe000751b5e0:      	add	x0, x0, #1624
fffffe000751b5e4:      	mov	x1, x26
fffffe000751b5e8:      	mov	w2, #16
fffffe000751b5ec:      	mov	w3, wzr
fffffe000751b5f0:      	bl	0xfffffe0007512560 <_nwfn_memory_range>
fffffe000751b5f4:      	cbz	w0, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b5f8:      	adrp	x9, 0xfffffe0007c54000 <_ncc_ap_report+0x74>
fffffe000751b5fc:      	add	x9, x9, #584
fffffe000751b600:      	ldr	x8, [x9, #312]
fffffe000751b604:      	ldr	x9, [x9]
fffffe000751b608:      	add	x8, x8, x26
fffffe000751b60c:      	sub	x8, x8, x9
fffffe000751b610:      	ldrb	wzr, [x8]
fffffe000751b614:      	ldrb	wzr, [x8, #1]
fffffe000751b618:      	ldrb	wzr, [x8, #2]
fffffe000751b61c:      	ldrb	wzr, [x8, #3]
fffffe000751b620:      	ldrb	wzr, [x8, #4]
fffffe000751b624:      	ldrb	wzr, [x8, #5]
fffffe000751b628:      	ldrb	wzr, [x8, #6]
fffffe000751b62c:      	ldrsb	w9, [x8, #7]
fffffe000751b630:      	ldrb	wzr, [x8, #8]
fffffe000751b634:      	ldrb	wzr, [x8, #9]
fffffe000751b638:      	ldrb	wzr, [x8, #10]
fffffe000751b63c:      	ldrb	wzr, [x8, #11]
fffffe000751b640:      	ldrb	wzr, [x8, #12]
fffffe000751b644:      	ldrb	wzr, [x8, #13]
fffffe000751b648:      	ldrb	wzr, [x8, #14]
fffffe000751b64c:      	ldrb	wzr, [x8, #15]
fffffe000751b650:      	tbz	w9, #31, 0xfffffe000751bc84 <_nss_worker_continue+0x2d2c>
fffffe000751b654:      	adrp	x15, 0xfffffe0008509000 <_drain+0x314>
fffffe000751b658:      	add	x15, x15, #20
fffffe000751b65c:      	adrp	x10, 0xfffffe000850b000 <_drain+0x324>
fffffe000751b660:      	add	x10, x10, #1744
fffffe000751b664:      	adrp	x16, 0xfffffe000850f000 <_drain+0x33c>
fffffe000751b668:      	add	x16, x16, #3760
fffffe000751b66c:      	adrp	x8, 0xfffffe000850c000 <_drain+0x338>
fffffe000751b670:      	add	x8, x8, #2120
fffffe000751b674:      	adrp	x13, 0xfffffe0007c53000 <_ncc_cold_read+0x5c>
fffffe000751b678:      	add	x13, x13, #1624
fffffe000751b67c:      	adrp	x9, 0xfffffe0007c90000 <_ncc_native_check+0x28>
fffffe000751b680:      	stp	w21, w22, [x15]
fffffe000751b684:      	stp	x10, x8, [x16]
fffffe000751b688:      	mov	w14, #1
fffffe000751b68c:      	str	x13, [x16, #16]
fffffe000751b690:      	ldr	w8, [x13, #1920]
fffffe000751b694:      	add	x9, x9, #584
fffffe000751b698:      	adrp	x0, 0xfffffe0007096000 <_nss_worker_continue+0x152c>
fffffe000751b69c:      	add	x0, x0, #3553
fffffe000751b6a0:      	ldr	w12, [x15, #28]
fffffe000751b6a4:      	ldur	q0, [x15, #244]
fffffe000751b6a8:      	stp	w14, w8, [x16, #32]
fffffe000751b6ac:      	ldur	q1, [x15, #228]
fffffe000751b6b0:      	str	x9, [x16, #24]
fffffe000751b6b4:      	str	w12, [x16, #48]
fffffe000751b6b8:      	add	x12, x15, #196
fffffe000751b6bc:      	ldur	q2, [x15, #196]
fffffe000751b6c0:      	str	wzr, [x15, #1096]
fffffe000751b6c4:      	ldur	q3, [x15, #212]
fffffe000751b6c8:      	stp	q1, q0, [x16, #96]
fffffe000751b6cc:      	ldp	q0, q1, [x12, #64]
fffffe000751b6d0:      	str	wzr, [x15, #9144]
fffffe000751b6d4:      	stp	q2, q3, [x16, #64]
fffffe000751b6d8:      	str	w14, [x16, #468]
fffffe000751b6dc:      	ldp	q3, q2, [x12, #128]
fffffe000751b6e0:      	stp	q0, q1, [x16, #128]
fffffe000751b6e4:      	ldp	q1, q0, [x12, #96]
fffffe000751b6e8:      	stp	q3, q2, [x16, #192]
fffffe000751b6ec:      	ldp	q3, q2, [x12, #192]
fffffe000751b6f0:      	stp	q1, q0, [x16, #160]
fffffe000751b6f4:      	ldp	q0, q1, [x12, #160]
fffffe000751b6f8:      	stp	q3, q2, [x16, #256]
fffffe000751b6fc:      	ldp	q2, q3, [x12, #224]
fffffe000751b700:      	stp	q0, q1, [x16, #224]
fffffe000751b704:      	ldp	q1, q0, [x12, #288]
fffffe000751b708:      	stp	q2, q3, [x16, #288]
fffffe000751b70c:      	ldp	q3, q2, [x12, #256]
fffffe000751b710:      	stp	q1, q0, [x16, #352]
fffffe000751b714:      	ldp	q1, q0, [x12, #368]
fffffe000751b718:      	stp	q3, q2, [x16, #320]
fffffe000751b71c:      	ldr	w9, [x13, #2864]
fffffe000751b720:      	stp	q1, q0, [x16, #432]
fffffe000751b724:      	ldr	w10, [x10, #432]
fffffe000751b728:      	ldp	q3, q2, [x12, #336]
fffffe000751b72c:      	stp	x8, x9, [sp, #8]
fffffe000751b730:      	stp	w9, w10, [x16, #40]
fffffe000751b734:      	ldrb	w11, [x13, #2876]
fffffe000751b738:      	stp	q3, q2, [x16, #400]
fffffe000751b73c:      	ldur	x10, [x15, #116]
fffffe000751b740:      	ldr	q0, [x12, #320]
fffffe000751b744:      	ldr	w12, [x13, #2868]
fffffe000751b748:      	adrp	x13, 0xfffffe00084ec000 <_drain+0x394>
fffffe000751b74c:      	strb	w11, [x16, #52]
fffffe000751b750:      	str	x10, [x16, #56]
fffffe000751b754:      	str	q0, [x16, #384]
fffffe000751b758:      	str	w14, [x13, #592]
fffffe000751b75c:      	stp	x11, x12, [sp, #24]
fffffe000751b760:      	str	x10, [sp]
fffffe000751b764:      	bl	0xfffffe000735dae4 <_printf>
fffffe000751b768:      	b	0xfffffe000751ab74 <_nss_worker_continue+0x1c1c>
fffffe000751b76c:      	adrp	x12, 0xfffffe00084ec000 <_drain+0x3b8>
fffffe000751b770:      	add	x12, x12, #1008
fffffe000751b774:      	adrp	x11, 0xfffffe00084d1000 <_drain+0x354>
fffffe000751b778:      	add	x11, x11, #2484
fffffe000751b77c:      	ldrb	w8, [sp, #549]
fffffe000751b780:      	strb	wzr, [sp, #492]
fffffe000751b784:      	ldrb	w9, [x12, #1113]
fffffe000751b788:      	ldr	w10, [x11]
fffffe000751b78c:      	strb	w8, [sp, #496]
fffffe000751b790:      	ldrb	w8, [sp, #548]
fffffe000751b794:      	strb	w9, [sp, #498]
fffffe000751b798:      	ldrh	w9, [x11, #4]
fffffe000751b79c:      	str	w10, [sp, #480]
fffffe000751b7a0:      	ldrh	w10, [x12, #4]
fffffe000751b7a4:      	ldr	x11, [sp, #112]
fffffe000751b7a8:      	strb	w8, [sp, #493]
fffffe000751b7ac:      	strh	w9, [sp, #484]
fffffe000751b7b0:      	ldrb	w9, [x12, #1112]
fffffe000751b7b4:      	strh	w10, [x11, #4]
fffffe000751b7b8:      	ldr	w10, [x12]
fffffe000751b7bc:      	mov	w12, #1280
fffffe000751b7c0:      	cmp	w9, #1
fffffe000751b7c4:      	strb	w9, [sp, #497]
fffffe000751b7c8:      	str	w10, [x11]
fffffe000751b7cc:      	strh	w12, [sp, #494]
fffffe000751b7d0:      	b.ne	0xfffffe000751b7fc <_nss_worker_continue+0x28a4>
fffffe000751b7d4:      	adrp	x10, 0xfffffe00084ec000 <_drain+0x420>
fffffe000751b7d8:      	ldrh	w9, [sp, #536]
fffffe000751b7dc:      	mov	w11, #17
fffffe000751b7e0:      	ldrh	w10, [x10, #1076]
fffffe000751b7e4:      	tst	w9, #0x3fc0
fffffe000751b7e8:      	and	w9, w10, #0x20
fffffe000751b7ec:      	mov	w10, #1041
fffffe000751b7f0:      	csel	w10, w11, w10, eq
fffffe000751b7f4:      	orr	w22, w10, w9
fffffe000751b7f8:      	b	0xfffffe000751b800 <_nss_worker_continue+0x28a8>
fffffe000751b7fc:      	mov	w22, #17
fffffe000751b800:      	adrp	x9, 0xfffffe00084ec000 <_drain+0x44c>
fffffe000751b804:      	add	x9, x9, #1062
fffffe000751b808:      	ldrb	w9, [x9]
fffffe000751b80c:      	cbz	w9, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751b810:      	adrp	x9, 0xfffffe00084ec000 <_drain+0x45c>
fffffe000751b814:      	ldrb	w9, [x9, #1061]
fffffe000751b818:      	tbz	w9, #2, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751b81c:      	adrp	x9, 0xfffffe00084d1000 <_drain+0x3fc>
fffffe000751b820:      	add	x9, x9, #2484
fffffe000751b824:      	ldrh	w11, [x9, #4]
fffffe000751b828:      	ldr	w9, [x9]
fffffe000751b82c:      	str	x9, [sp, #448]
fffffe000751b830:      	adrp	x9, 0xfffffe00084ec000 <_drain+0x47c>
fffffe000751b834:      	add	x9, x9, #1008
fffffe000751b838:      	ldrh	w10, [x9]
fffffe000751b83c:      	str	x10, [sp, #400]
fffffe000751b840:      	ldur	w10, [x9, #2]
fffffe000751b844:      	str	x10, [sp, #432]
fffffe000751b848:      	ldrb	w10, [x9, #44]
fffffe000751b84c:      	ldrb	w9, [x9, #1114]
fffffe000751b850:      	stp	x10, x11, [sp, #408]
fffffe000751b854:      	adrp	x11, 0xfffffe00084ec000 <_drain+0x4a0>
fffffe000751b858:      	add	x11, x11, #1067
fffffe000751b85c:      	str	x9, [sp, #424]
fffffe000751b860:      	adrp	x9, 0xfffffe00084ec000 <_drain+0x4ac>
fffffe000751b864:      	add	x9, x9, #1062
fffffe000751b868:      	ldrb	w10, [x11]
fffffe000751b86c:      	ldrh	w9, [x9, #12]
fffffe000751b870:      	cmp	w10, #255
fffffe000751b874:      	str	w9, [sp, #444]
fffffe000751b878:      	ldrb	w9, [x11, #1]
fffffe000751b87c:      	b.ne	0xfffffe000751b888 <_nss_worker_continue+0x2930>
fffffe000751b880:      	cmp	w9, #255
fffffe000751b884:      	b.eq	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751b888:      	cmp	w10, #255
fffffe000751b88c:      	mov	w11, #220
fffffe000751b890:      	csel	w10, wzr, w10, eq
fffffe000751b894:      	cmp	w9, #255
fffffe000751b898:      	csel	w9, wzr, w9, eq
fffffe000751b89c:      	cmp	w10, w9
fffffe000751b8a0:      	csel	w10, w10, w9, hi
fffffe000751b8a4:      	adrp	x9, 0xfffffe00084ec000 <_drain+0x4f0>
fffffe000751b8a8:      	cmp	w10, #220
fffffe000751b8ac:      	csel	w10, w10, w11, lo
fffffe000751b8b0:      	ldrh	w9, [x9, #1078]
fffffe000751b8b4:      	str	w10, [sp, #52]
fffffe000751b8b8:      	cbz	x9, 0xfffffe000751b944 <_nss_worker_continue+0x29ec>
fffffe000751b8bc:      	mov	x11, xzr
fffffe000751b8c0:      	str	wzr, [sp, #456]
fffffe000751b8c4:      	b	0xfffffe000751b8d4 <_nss_worker_continue+0x297c>
fffffe000751b8c8:      	add	x11, x11, x10
fffffe000751b8cc:      	cmp	x11, x9
fffffe000751b8d0:      	b.hs	0xfffffe000751b948 <_nss_worker_continue+0x29f0>
fffffe000751b8d4:      	sub	x10, x9, x11
fffffe000751b8d8:      	subs	x12, x10, #2
fffffe000751b8dc:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751b8e0:      	adrp	x10, 0xfffffe00084ec000 <_before+0x8>
fffffe000751b8e4:      	add	x10, x10, #544
fffffe000751b8e8:      	add	x10, x11, x10
fffffe000751b8ec:      	ldrb	w10, [x10, #553]
fffffe000751b8f0:      	cmp	x12, x10
fffffe000751b8f4:      	b.lo	0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751b8f8:      	adrp	x12, 0xfffffe00084ec000 <_before+0x20>
fffffe000751b8fc:      	add	x12, x12, #544
fffffe000751b900:      	add	x12, x12, x11
fffffe000751b904:      	add	x11, x11, #2
fffffe000751b908:      	ldrb	w12, [x12, #552]
fffffe000751b90c:      	cmp	w12, #42
fffffe000751b910:      	b.ne	0xfffffe000751b8c8 <_nss_worker_continue+0x2970>
fffffe000751b914:      	cmp	w10, #1
fffffe000751b918:      	b.ne	0xfffffe000751b8c8 <_nss_worker_continue+0x2970>
fffffe000751b91c:      	adrp	x12, 0xfffffe00084ec000 <_before+0x44>
fffffe000751b920:      	add	x12, x12, #544
fffffe000751b924:      	add	x12, x12, x11
fffffe000751b928:      	and	w13, w22, #0xffffffdf
fffffe000751b92c:      	ldrb	w12, [x12, #552]
fffffe000751b930:      	tst	w12, #0x4
fffffe000751b934:      	ubfx	w12, w12, #1, #1
fffffe000751b938:      	csel	w22, w22, w13, eq
fffffe000751b93c:      	str	w12, [sp, #456]
fffffe000751b940:      	b	0xfffffe000751b8c8 <_nss_worker_continue+0x2970>
fffffe000751b944:      	str	wzr, [sp, #456]
fffffe000751b948:      	ldr	x9, [sp, #104]
fffffe000751b94c:      	strb	w8, [sp, #505]
fffffe000751b950:      	ldrh	w8, [sp, #476]
fffffe000751b954:      	add	x4, sp, #544
fffffe000751b958:      	add	x5, sp, #480
fffffe000751b95c:      	add	x6, sp, #504
fffffe000751b960:      	stp	xzr, xzr, [x9]
fffffe000751b964:      	mov	w9, #5
fffffe000751b968:      	strh	w8, [sp, #510]
fffffe000751b96c:      	mov	w8, #8
fffffe000751b970:      	adrp	x0, 0xfffffe0008508000 <_before+0x108>
fffffe000751b974:      	add	x0, x0, #4080
fffffe000751b978:      	strb	w9, [sp, #504]
fffffe000751b97c:      	mov	w9, #257
fffffe000751b980:      	strh	w8, [sp, #512]
fffffe000751b984:      	mov	w8, #2000
fffffe000751b988:      	adrp	x1, 0xfffffe0007c53000 <_ncc_native_read+0xa4>
fffffe000751b98c:      	add	x1, x1, #1624
fffffe000751b990:      	strh	w9, [sp, #506]
fffffe000751b994:      	mov	w9, #30
fffffe000751b998:      	mov	x2, x19
fffffe000751b99c:      	adrp	x3, 0xfffffe0008512000 <_nnr_plan>
fffffe000751b9a0:      	add	x3, x3, #112
fffffe000751b9a4:      	mov	x7, x26
fffffe000751b9a8:      	strb	w9, [sp, #508]
fffffe000751b9ac:      	str	w8, [sp, #516]
fffffe000751b9b0:      	stp	x21, x20, [sp]
fffffe000751b9b4:      	bl	0xfffffe00079a6ad8 <_nst_prepare>
fffffe000751b9b8:      	cbnz	w0, 0xfffffe000751a378 <_nss_worker_continue+0x1420>
fffffe000751b9bc:      	ldp	x12, x10, [sp, #408]
fffffe000751b9c0:      	adrp	x9, 0xfffffe00084ec000 <_before+0xe8>
fffffe000751b9c4:      	add	x9, x9, #572
fffffe000751b9c8:      	ldr	x11, [sp, #448]
fffffe000751b9cc:      	add	x8, x9, #448
fffffe000751b9d0:      	add	x0, sp, #544
fffffe000751b9d4:      	str	xzr, [sp, #544]
fffffe000751b9d8:      	str	xzr, [sp, #504]
fffffe000751b9dc:      	bfi	x11, x10, #32, #16
fffffe000751b9e0:      	adrp	x10, 0xfffffe000850b000 <_nnr_plan+0x28>
fffffe000751b9e4:      	str	d8, [x10, #968]
fffffe000751b9e8:      	ldr	x10, [sp, #400]
fffffe000751b9ec:      	bfi	x11, x10, #48, #16
fffffe000751b9f0:      	ldr	x10, [sp, #96]
fffffe000751b9f4:      	stp	x8, x12, [x10, #16]
fffffe000751b9f8:      	add	x8, x9, #1551
fffffe000751b9fc:      	str	x11, [x10]
fffffe000751ba00:      	ldr	x11, [sp, #432]
fffffe000751ba04:      	strh	w22, [x10, #48]
fffffe000751ba08:      	str	x8, [x10, #32]
fffffe000751ba0c:      	ldr	x8, [sp, #424]
fffffe000751ba10:      	str	x11, [x10, #8]
fffffe000751ba14:      	ldr	w11, [sp, #456]
fffffe000751ba18:      	str	wzr, [x10, #52]
fffffe000751ba1c:      	str	x8, [x10, #40]
fffffe000751ba20:      	mov	w8, #10
fffffe000751ba24:      	strb	w11, [x10, #60]
fffffe000751ba28:      	sturh	wzr, [x10, #61]
fffffe000751ba2c:      	strh	w8, [x10, #50]
fffffe000751ba30:      	ldr	w8, [sp, #444]
fffffe000751ba34:      	strb	wzr, [x10, #63]
fffffe000751ba38:      	strh	w8, [x10, #56]
fffffe000751ba3c:      	ldr	w8, [sp, #52]
fffffe000751ba40:      	strb	w8, [x10, #58]
fffffe000751ba44:      	mov	w8, #1
fffffe000751ba48:      	strb	w8, [x10, #59]
fffffe000751ba4c:      	str	w8, [x9]
fffffe000751ba50:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe000751ba54:      	ldr	x0, [sp, #544]
fffffe000751ba58:      	add	x1, sp, #504
fffffe000751ba5c:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe000751ba60:      	ldr	x8, [sp, #504]
fffffe000751ba64:      	mov	x10, #63439
fffffe000751ba68:      	movk	x10, #58195, lsl #16
fffffe000751ba6c:      	movk	x10, #39845, lsl #32
fffffe000751ba70:      	lsr	x9, x8, #3
fffffe000751ba74:      	movk	x10, #8388, lsl #48
fffffe000751ba78:      	umulh	x9, x9, x10
fffffe000751ba7c:      	lsr	x20, x9, #4
fffffe000751ba80:      	cmp	x20, x21
fffffe000751ba84:      	b.lo	0xfffffe000751bc04 <_nss_worker_continue+0x2cac>
fffffe000751ba88:      	adrp	x9, 0xfffffe00084ec000 <_nnr_plan+0x54>
fffffe000751ba8c:      	add	x9, x9, #572
fffffe000751ba90:      	ldur	x9, [x9, #148]
fffffe000751ba94:      	cmp	x20, x9
fffffe000751ba98:      	b.hs	0xfffffe000751bc04 <_nss_worker_continue+0x2cac>
fffffe000751ba9c:      	mov	x11, #63439
fffffe000751baa0:      	lsr	x9, x9, #3
fffffe000751baa4:      	movk	x11, #58195, lsl #16
fffffe000751baa8:      	ldr	x10, [sp, #88]
fffffe000751baac:      	movk	x11, #39845, lsl #32
fffffe000751bab0:      	adrp	x20, 0xfffffe00084ec000 <_nnr_plan+0x7c>
fffffe000751bab4:      	add	x20, x20, #544
fffffe000751bab8:      	movk	x11, #8388, lsl #48
fffffe000751babc:      	movi.2d	v2, #0000000000000000
fffffe000751bac0:      	adrp	x1, 0xfffffe0008508000 <_nnr_begin+0x5c>
fffffe000751bac4:      	add	x1, x1, #4080
fffffe000751bac8:      	umulh	x9, x9, x11
fffffe000751bacc:      	ldp	x12, x11, [sp, #72]
fffffe000751bad0:      	lsr	x9, x9, #4
fffffe000751bad4:      	add	x3, sp, #544
fffffe000751bad8:      	stp	q2, q2, [x10, #16]
fffffe000751badc:      	adr	x4, #5784
fffffe000751bae0:      	nop
fffffe000751bae4:      	stp	q2, q2, [x10, #48]
fffffe000751bae8:      	ldp	q0, q1, [x12]
fffffe000751baec:      	stp	q2, q2, [x10, #80]
fffffe000751baf0:      	mov	x5, x20
fffffe000751baf4:      	stp	q2, q2, [x10, #112]
fffffe000751baf8:      	add	x21, x20, #360
fffffe000751bafc:      	str	q2, [x10]
fffffe000751bb00:      	str	x9, [sp, #552]
fffffe000751bb04:      	ldr	x10, [x20, #208]
fffffe000751bb08:      	stp	q0, q1, [x11]
fffffe000751bb0c:      	ldr	x9, [x12, #48]
fffffe000751bb10:      	ldr	q0, [x12, #32]
fffffe000751bb14:      	str	x10, [sp, #544]
fffffe000751bb18:      	mov	w10, #30000
fffffe000751bb1c:      	str	x9, [x11, #48]
fffffe000751bb20:      	ldrh	w9, [x20, #1576]
fffffe000751bb24:      	str	q0, [x11, #32]
fffffe000751bb28:      	mov	w11, #4000
fffffe000751bb2c:      	str	w10, [sp, #560]
fffffe000751bb30:      	ldrb	w10, [x1, #204]
fffffe000751bb34:      	sturh	w9, [x27, #83]
fffffe000751bb38:      	ldrb	w9, [x1, #207]
fffffe000751bb3c:      	str	w11, [sp, #632]
fffffe000751bb40:      	add	x11, x20, #400
fffffe000751bb44:      	strb	w10, [sp, #624]
fffffe000751bb48:      	mov	w10, #1
fffffe000751bb4c:      	strb	w9, [sp, #636]
fffffe000751bb50:      	add	x9, x20, #432
fffffe000751bb54:      	ldrh	w12, [x20, #1592]
fffffe000751bb58:      	strb	w10, [sp, #625]
fffffe000751bb5c:      	mov	w10, #4
fffffe000751bb60:      	stp	x11, x9, [x27, #96]
fffffe000751bb64:      	mov	x11, #13531
fffffe000751bb68:      	movk	x11, #55222, lsl #16
fffffe000751bb6c:      	add	x9, x20, #552
fffffe000751bb70:      	strb	w10, [sp, #629]
fffffe000751bb74:      	ldrb	w10, [x1, #208]
fffffe000751bb78:      	movk	x11, #56962, lsl #32
fffffe000751bb7c:      	movk	x11, #17179, lsl #48
fffffe000751bb80:      	str	x9, [x27, #128]
fffffe000751bb84:      	strb	w10, [sp, #637]
fffffe000751bb88:      	add	x10, x20, #1594
fffffe000751bb8c:      	umulh	x8, x8, x11
fffffe000751bb90:      	adr	x11, #5556
fffffe000751bb94:      	nop
fffffe000751bb98:      	ldp	x2, x0, [sp, #56]
fffffe000751bb9c:      	stp	x10, x12, [x27, #112]
fffffe000751bba0:      	lsr	x6, x8, #18
fffffe000751bba4:      	ldrh	w10, [x20, #534]
fffffe000751bba8:      	stp	x10, x11, [x27, #136]
fffffe000751bbac:      	bl	0xfffffe00079b0844 <_nwn_begin>
fffffe000751bbb0:      	mov	w26, w0
fffffe000751bbb4:      	add	x0, sp, #544
fffffe000751bbb8:      	mov	w1, #160
fffffe000751bbbc:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751bbc0:      	mov	x0, x21
fffffe000751bbc4:      	mov	w1, #104
fffffe000751bbc8:      	bl	0xfffffe00079a2d30 <_niwj_clear>
fffffe000751bbcc:      	cmp	w26, #1
fffffe000751bbd0:      	cset	w8, eq
fffffe000751bbd4:      	stp	wzr, w8, [x20, #8]
fffffe000751bbd8:      	b.ne	0xfffffe000751bc28 <_nss_worker_continue+0x2cd0>
fffffe000751bbdc:      	bl	0xfffffe000751cf08 <_njs_snapshot_locked>
fffffe000751bbe0:      	ldr	x0, [x24, #2456]
fffffe000751bbe4:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751bbe8:      	ldr	x0, [x24, #2456]
fffffe000751bbec:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe000751bbf0:      	mov	w0, #1
fffffe000751bbf4:      	bl	0xfffffe000750e938 <_njs_trace_locked>
fffffe000751bbf8:      	ldr	x0, [x24, #2456]
fffffe000751bbfc:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751bc00:      	b	0xfffffe000751a388 <_nss_worker_continue+0x1430>
fffffe000751bc04:      	ldr	x0, [x24, #2456]
fffffe000751bc08:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751bc0c:      	adrp	x8, 0xfffffe00084ec000 <_nbc_caps_decode+0x28>
fffffe000751bc10:      	mov	w9, #9
fffffe000751bc14:      	ldr	x8, [x8, #720]
fffffe000751bc18:      	cmp	x20, x8
fffffe000751bc1c:      	mov	w8, #2
fffffe000751bc20:      	csel	w0, w8, w9, lo
fffffe000751bc24:      	b	0xfffffe000751a384 <_nss_worker_continue+0x142c>
fffffe000751bc28:      	ldr	x0, [x24, #2456]
fffffe000751bc2c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751bc30:      	mov	w0, #2
fffffe000751bc34:      	bl	0xfffffe000751cb80 <_njs_early_finish>
fffffe000751bc38:      	adrp	x9, 0xfffffe0008509000 <_nbc_caps_decode+0xc8>
fffffe000751bc3c:      	add	x9, x9, #36
fffffe000751bc40:      	ldr	w8, [x9]
fffffe000751bc44:      	ldr	w9, [x9, #40]
fffffe000751bc48:      	orr	w8, w8, w9
fffffe000751bc4c:      	cbz	w8, 0xfffffe000751a388 <_nss_worker_continue+0x1430>
fffffe000751bc50:      	b	0xfffffe000751bc68 <_nss_worker_continue+0x2d10>
fffffe000751bc54:      	bl	0xfffffe00079fd594 <___stack_chk_fail>
fffffe000751bc58:      	bl	0xfffffe0007b4953c <_nss_worker_continue.cold.1>
fffffe000751bc5c:      	b	0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
fffffe000751bc60:      	bl	0xfffffe0007b49558 <_nss_worker_continue.cold.2>
fffffe000751bc64:      	bl	0xfffffe0007b49574 <_nss_worker_continue.cold.3>
fffffe000751bc68:      	bl	0xfffffe0007b49704 <_nss_worker_continue.cold.4>
fffffe000751bc6c:      	mov	x0, x22
fffffe000751bc70:      	bl	0xfffffe0007b49778 <_nss_worker_continue.cold.5>
fffffe000751bc74:      	add	x0, sp, #504
fffffe000751bc78:      	add	x1, sp, #544
fffffe000751bc7c:      	mov	x2, x22
fffffe000751bc80:      	bl	0xfffffe0007b497b4 <_nss_worker_continue.cold.6>
fffffe000751bc84:      	ldr	x0, [x24, #2456]
fffffe000751bc88:      	adrp	x8, 0xfffffe0008509000 <_nbc_caps_decode+0x118>
fffffe000751bc8c:      	add	x8, x8, #20
fffffe000751bc90:      	stp	w21, w22, [x8]
fffffe000751bc94:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe000751bc98:      	b	0xfffffe000751bc64 <_nss_worker_continue+0x2d0c>
