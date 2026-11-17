
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7084 <_niwa_snapshot_bss_filter>:
fffffe00079a7084:      	mov	w8, #-1
fffffe00079a7088:      	cbz	x0, 0xfffffe00079a7340 <_niwa_snapshot_bss_filter+0x2bc>
fffffe00079a708c:      	sub	w9, w1, #4
fffffe00079a7090:      	cmn	w9, #2
fffffe00079a7094:      	b.lo	0xfffffe00079a7340 <_niwa_snapshot_bss_filter+0x2bc>
fffffe00079a7098:      	ldr	w8, [x0, #4]
fffffe00079a709c:      	cbz	w8, 0xfffffe00079a70f8 <_niwa_snapshot_bss_filter+0x74>
fffffe00079a70a0:      	ldr	w8, [x0]
fffffe00079a70a4:      	cmp	w8, #4
fffffe00079a70a8:      	b.ne	0xfffffe00079a70f8 <_niwa_snapshot_bss_filter+0x74>
fffffe00079a70ac:      	ldr	w8, [x0, #8]
fffffe00079a70b0:      	cmp	w8, #16
fffffe00079a70b4:      	b.hi	0xfffffe00079a70f8 <_niwa_snapshot_bss_filter+0x74>
fffffe00079a70b8:      	ldr	w9, [x0, #40]
fffffe00079a70bc:      	cmp	w9, #2
fffffe00079a70c0:      	b.ne	0xfffffe00079a70f8 <_niwa_snapshot_bss_filter+0x74>
fffffe00079a70c4:      	ldr	w9, [x0, #68]
fffffe00079a70c8:      	cbnz	w9, 0xfffffe00079a70f8 <_niwa_snapshot_bss_filter+0x74>
fffffe00079a70cc:      	ldr	x9, [x0, #48]
fffffe00079a70d0:      	ldr	x10, [x0, #32]
fffffe00079a70d4:      	cmp	x9, x10
fffffe00079a70d8:      	b.ne	0xfffffe00079a70f8 <_niwa_snapshot_bss_filter+0x74>
fffffe00079a70dc:      	ldr	w9, [x0, #64]
fffffe00079a70e0:      	cmp	w9, w8
fffffe00079a70e4:      	b.ne	0xfffffe00079a70f8 <_niwa_snapshot_bss_filter+0x74>
fffffe00079a70e8:      	ldr	x9, [x0, #56]
fffffe00079a70ec:      	add	x10, x0, #72
fffffe00079a70f0:      	cmp	x9, x10
fffffe00079a70f4:      	b.eq	0xfffffe00079a7100 <_niwa_snapshot_bss_filter+0x7c>
fffffe00079a70f8:      	mov	w0, #-1
fffffe00079a70fc:      	ret
fffffe00079a7100:      	cmp	w1, #3
fffffe00079a7104:      	b.ne	0xfffffe00079a7110 <_niwa_snapshot_bss_filter+0x8c>
fffffe00079a7108:      	mov	w0, wzr
fffffe00079a710c:      	ret
fffffe00079a7110:      	cbz	w8, 0xfffffe00079a71fc <_niwa_snapshot_bss_filter+0x178>
fffffe00079a7114:      	movi.2s	v0, #1
fffffe00079a7118:      	mov	x10, xzr
fffffe00079a711c:      	mov	w9, wzr
fffffe00079a7120:      	add	x11, x0, #72
fffffe00079a7124:      	mov	w12, #1164
fffffe00079a7128:      	mov	x13, x0
fffffe00079a712c:      	b	0xfffffe00079a7144 <_niwa_snapshot_bss_filter+0xc0>
fffffe00079a7130:      	add	w9, w9, #1
fffffe00079a7134:      	add	x10, x10, #1
fffffe00079a7138:      	add	x13, x13, #1164
fffffe00079a713c:      	cmp	x10, x8
fffffe00079a7140:      	b.eq	0xfffffe00079a7200 <_niwa_snapshot_bss_filter+0x17c>
fffffe00079a7144:      	mul	x16, x10, x12
fffffe00079a7148:      	add	x14, x0, x16
fffffe00079a714c:      	ldrb	w14, [x14, #98]
fffffe00079a7150:      	and	w14, w14, #0x3
fffffe00079a7154:      	cmp	w14, #1
fffffe00079a7158:      	b.ne	0xfffffe00079a7188 <_niwa_snapshot_bss_filter+0x104>
fffffe00079a715c:      	mov	w15, w9
fffffe00079a7160:      	cmp	x10, x15
fffffe00079a7164:      	b.eq	0xfffffe00079a7130 <_niwa_snapshot_bss_filter+0xac>
fffffe00079a7168:      	mul	x14, x15, x12
fffffe00079a716c:      	add	x16, x11, x16
fffffe00079a7170:      	add	x17, x11, x14
fffffe00079a7174:      	sub	x16, x17, x16
fffffe00079a7178:      	cmp	x16, #32
fffffe00079a717c:      	b.hs	0xfffffe00079a71a0 <_niwa_snapshot_bss_filter+0x11c>
fffffe00079a7180:      	mov	x15, xzr
fffffe00079a7184:      	b	0xfffffe00079a71dc <_niwa_snapshot_bss_filter+0x158>
fffffe00079a7188:      	ldur	d1, [x0, #20]
fffffe00079a718c:      	add.2s	v2, v1, v0
fffffe00079a7190:      	orr.2s	v1, #32
fffffe00079a7194:      	mov.s	v2[1], v1[1]
fffffe00079a7198:      	stur	d2, [x0, #20]
fffffe00079a719c:      	b	0xfffffe00079a7134 <_niwa_snapshot_bss_filter+0xb0>
fffffe00079a71a0:      	madd	x16, x15, x12, x0
fffffe00079a71a4:      	mov	w17, #88
fffffe00079a71a8:      	add	x1, x13, x17
fffffe00079a71ac:      	add	x2, x16, x17
fffffe00079a71b0:      	add	x17, x17, #32
fffffe00079a71b4:      	cmp	x17, #1240
fffffe00079a71b8:      	ldp	q1, q2, [x1, #-16]
fffffe00079a71bc:      	stp	q1, q2, [x2, #-16]
fffffe00079a71c0:      	b.ne	0xfffffe00079a71a8 <_niwa_snapshot_bss_filter+0x124>
fffffe00079a71c4:      	add	x16, x0, #72
fffffe00079a71c8:      	madd	x17, x10, x12, x16
fffffe00079a71cc:      	madd	x16, x15, x12, x16
fffffe00079a71d0:      	mov	w15, #1160
fffffe00079a71d4:      	ldr	d1, [x17, #1152]
fffffe00079a71d8:      	str	d1, [x16, #1152]
fffffe00079a71dc:      	add	x15, x15, #72
fffffe00079a71e0:      	add	x14, x0, x14
fffffe00079a71e4:      	ldrb	w16, [x13, x15]
fffffe00079a71e8:      	strb	w16, [x14, x15]
fffffe00079a71ec:      	add	x15, x15, #1
fffffe00079a71f0:      	cmp	x15, #1236
fffffe00079a71f4:      	b.ne	0xfffffe00079a71e4 <_niwa_snapshot_bss_filter+0x160>
fffffe00079a71f8:      	b	0xfffffe00079a7130 <_niwa_snapshot_bss_filter+0xac>
fffffe00079a71fc:      	mov	w9, wzr
fffffe00079a7200:      	cmp	w9, w8
fffffe00079a7204:      	b.hs	0xfffffe00079a7334 <_niwa_snapshot_bss_filter+0x2b0>
fffffe00079a7208:      	mov	w10, #1164
fffffe00079a720c:      	sub	w8, w8, w9
fffffe00079a7210:      	movi.2d	v0, #0000000000000000
fffffe00079a7214:      	umaddl	x10, w9, w10, x0
fffffe00079a7218:      	add	x10, x10, #632
fffffe00079a721c:      	sub	x11, x10, #560
fffffe00079a7220:      	sub	x12, x10, #544
fffffe00079a7224:      	sub	x13, x10, #528
fffffe00079a7228:      	stp	q0, q0, [x10, #-256]
fffffe00079a722c:      	stp	q0, q0, [x10, #-224]
fffffe00079a7230:      	subs	w8, w8, #1
fffffe00079a7234:      	str	q0, [x11]
fffffe00079a7238:      	sub	x11, x10, #512
fffffe00079a723c:      	str	q0, [x12]
fffffe00079a7240:      	sub	x12, x10, #496
fffffe00079a7244:      	str	q0, [x13]
fffffe00079a7248:      	sub	x13, x10, #480
fffffe00079a724c:      	str	q0, [x11]
fffffe00079a7250:      	sub	x11, x10, #464
fffffe00079a7254:      	str	q0, [x12]
fffffe00079a7258:      	sub	x12, x10, #448
fffffe00079a725c:      	str	q0, [x13]
fffffe00079a7260:      	sub	x13, x10, #432
fffffe00079a7264:      	str	q0, [x11]
fffffe00079a7268:      	sub	x11, x10, #416
fffffe00079a726c:      	str	q0, [x12]
fffffe00079a7270:      	sub	x12, x10, #400
fffffe00079a7274:      	str	q0, [x13]
fffffe00079a7278:      	sub	x13, x10, #384
fffffe00079a727c:      	str	q0, [x11]
fffffe00079a7280:      	sub	x11, x10, #368
fffffe00079a7284:      	str	q0, [x12]
fffffe00079a7288:      	sub	x12, x10, #352
fffffe00079a728c:      	str	q0, [x13]
fffffe00079a7290:      	sub	x13, x10, #336
fffffe00079a7294:      	str	q0, [x11]
fffffe00079a7298:      	sub	x11, x10, #320
fffffe00079a729c:      	str	q0, [x12]
fffffe00079a72a0:      	sub	x12, x10, #304
fffffe00079a72a4:      	stp	q0, q0, [x10, #-192]
fffffe00079a72a8:      	str	q0, [x11]
fffffe00079a72ac:      	sub	x11, x10, #288
fffffe00079a72b0:      	str	q0, [x12]
fffffe00079a72b4:      	sub	x12, x10, #272
fffffe00079a72b8:      	stp	q0, q0, [x10, #-160]
fffffe00079a72bc:      	stp	q0, q0, [x10, #-128]
fffffe00079a72c0:      	stp	q0, q0, [x10, #-96]
fffffe00079a72c4:      	stp	q0, q0, [x10, #-64]
fffffe00079a72c8:      	stp	q0, q0, [x10, #-32]
fffffe00079a72cc:      	stp	q0, q0, [x10]
fffffe00079a72d0:      	stp	q0, q0, [x10, #32]
fffffe00079a72d4:      	stp	q0, q0, [x10, #64]
fffffe00079a72d8:      	stp	q0, q0, [x10, #96]
fffffe00079a72dc:      	stp	q0, q0, [x10, #128]
fffffe00079a72e0:      	stp	q0, q0, [x10, #160]
fffffe00079a72e4:      	stp	q0, q0, [x10, #192]
fffffe00079a72e8:      	stp	q0, q0, [x10, #224]
fffffe00079a72ec:      	stp	q0, q0, [x10, #256]
fffffe00079a72f0:      	stp	q0, q0, [x10, #288]
fffffe00079a72f4:      	stp	q0, q0, [x10, #320]
fffffe00079a72f8:      	stp	q0, q0, [x10, #352]
fffffe00079a72fc:      	stp	q0, q0, [x10, #384]
fffffe00079a7300:      	stp	q0, q0, [x10, #416]
fffffe00079a7304:      	stp	q0, q0, [x10, #448]
fffffe00079a7308:      	stp	q0, q0, [x10, #480]
fffffe00079a730c:      	stp	q0, q0, [x10, #512]
fffffe00079a7310:      	stp	q0, q0, [x10, #544]
fffffe00079a7314:      	str	q0, [x10, #576]
fffffe00079a7318:      	str	d0, [x10, #592]
fffffe00079a731c:      	str	wzr, [x10, #600]
fffffe00079a7320:      	add	x10, x10, #1164
fffffe00079a7324:      	str	q0, [x13]
fffffe00079a7328:      	str	q0, [x11]
fffffe00079a732c:      	str	q0, [x12]
fffffe00079a7330:      	b.ne	0xfffffe00079a721c <_niwa_snapshot_bss_filter+0x198>
fffffe00079a7334:      	mov	w8, wzr
fffffe00079a7338:      	str	w9, [x0, #8]
fffffe00079a733c:      	str	w9, [x0, #64]
fffffe00079a7340:      	mov	w0, w8
fffffe00079a7344:      	ret
