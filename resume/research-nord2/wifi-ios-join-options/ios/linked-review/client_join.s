
$NORD2_REPO/research/ci-39823157589/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007ae20f8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)>:
fffffe0007ae20f8:      	sub	sp, sp, #416
fffffe0007ae20fc:      	stp	x28, x27, [sp, #320]
fffffe0007ae2100:      	stp	x26, x25, [sp, #336]
fffffe0007ae2104:      	stp	x24, x23, [sp, #352]
fffffe0007ae2108:      	stp	x22, x21, [sp, #368]
fffffe0007ae210c:      	stp	x20, x19, [sp, #384]
fffffe0007ae2110:      	stp	x29, x30, [sp, #400]
fffffe0007ae2114:      	add	x29, sp, #400
fffffe0007ae2118:      	ldr	w8, [x0, #232]
fffffe0007ae211c:      	mov	w19, #728
fffffe0007ae2120:      	movk	w19, #57344, lsl #16
fffffe0007ae2124:      	cmp	w8, #3
fffffe0007ae2128:      	b.ne	0xfffffe0007ae2144 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x4c>
fffffe0007ae212c:      	mov	x21, x0
fffffe0007ae2130:      	ldrb	w8, [x0, #320]
fffffe0007ae2134:      	cbz	w8, 0xfffffe0007ae2144 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x4c>
fffffe0007ae2138:      	ldr	x8, [x21, #960]
fffffe0007ae213c:      	cbz	x8, 0xfffffe0007ae2168 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x70>
fffffe0007ae2140:      	mov	w19, #16
fffffe0007ae2144:      	mov	w0, w19
fffffe0007ae2148:      	ldp	x29, x30, [sp, #400]
fffffe0007ae214c:      	ldp	x20, x19, [sp, #384]
fffffe0007ae2150:      	ldp	x22, x21, [sp, #368]
fffffe0007ae2154:      	ldp	x24, x23, [sp, #352]
fffffe0007ae2158:      	ldp	x26, x25, [sp, #336]
fffffe0007ae215c:      	ldp	x28, x27, [sp, #320]
fffffe0007ae2160:      	add	sp, sp, #416
fffffe0007ae2164:      	ret
fffffe0007ae2168:      	adrp	x0, 0xfffffe000717e000 <_nbg_i2c_read+0x6b0>
fffffe0007ae216c:      	mov	x23, x2
fffffe0007ae2170:      	mov	x22, x1
fffffe0007ae2174:      	mov	w1, #104
fffffe0007ae2178:      	mov	w2, wzr
fffffe0007ae217c:      	add	x0, x0, #3608
fffffe0007ae2180:      	bl	0xfffffe0007a10658 <_IOMalloc_internal>
fffffe0007ae2184:      	cbz	x0, 0xfffffe0007ae2214 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x11c>
fffffe0007ae2188:      	movi.2d	v0, #0000000000000000
fffffe0007ae218c:      	mov	x20, x0
fffffe0007ae2190:      	add	x8, sp, #192
fffffe0007ae2194:      	add	x0, sp, #192
fffffe0007ae2198:      	str	wzr, [sp, #288]
fffffe0007ae219c:      	stp	q0, q0, [x8, #64]
fffffe0007ae21a0:      	str	q0, [x8, #48]
fffffe0007ae21a4:      	stp	q0, q0, [sp, #208]
fffffe0007ae21a8:      	str	q0, [sp, #192]
fffffe0007ae21ac:      	bl	0xfffffe000750dacc <_nord2_wifi_scan_info>
fffffe0007ae21b0:      	cbz	w0, 0xfffffe0007ae221c <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x124>
fffffe0007ae21b4:      	ldr	w8, [x21, #976]
fffffe0007ae21b8:      	add	w9, w8, #1
fffffe0007ae21bc:      	cmp	w8, #31
fffffe0007ae21c0:      	str	w9, [x21, #976]
fffffe0007ae21c4:      	b.hi	0xfffffe0007ae22b8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x1c0>
fffffe0007ae21c8:      	adrp	x8, 0xfffffe0008534000 <_nord2_wifi_ifnet_receive+0xcc>
fffffe0007ae21cc:      	ldr	x11, [x8, #2992]
fffffe0007ae21d0:      	cmn	x11, #1
fffffe0007ae21d4:      	b.eq	0xfffffe0007ae22b8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x1c0>
fffffe0007ae21d8:      	mov	w8, wzr
fffffe0007ae21dc:      	mov	x9, x11
fffffe0007ae21e0:      	adrp	x10, 0xfffffe0008534000 <_nord2_wifi_ifnet_receive+0xe4>
fffffe0007ae21e4:      	add	x10, x10, #2992
fffffe0007ae21e8:      	add	x12, x11, #1
fffffe0007ae21ec:      	cas	x9, x12, [x10]
fffffe0007ae21f0:      	cmp	x9, x11
fffffe0007ae21f4:      	b.eq	0xfffffe0007ae22a8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x1b0>
fffffe0007ae21f8:      	cmp	w8, #14
fffffe0007ae21fc:      	b.hi	0xfffffe0007ae22b8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x1c0>
fffffe0007ae2200:      	add	w8, w8, #1
fffffe0007ae2204:      	mov	x11, x9
fffffe0007ae2208:      	cmn	x9, #1
fffffe0007ae220c:      	b.ne	0xfffffe0007ae21e8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0xf0>
fffffe0007ae2210:      	b	0xfffffe0007ae22b8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x1c0>
fffffe0007ae2214:      	sub	w19, w19, #27
fffffe0007ae2218:      	b	0xfffffe0007ae2144 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x4c>
fffffe0007ae221c:      	movi.2d	v0, #0000000000000000
fffffe0007ae2220:      	add	x2, sp, #192
fffffe0007ae2224:      	add	x4, sp, #112
fffffe0007ae2228:      	mov	x0, x22
fffffe0007ae222c:      	mov	x1, x23
fffffe0007ae2230:      	mov	x3, x20
fffffe0007ae2234:      	stur	q0, [sp, #172]
fffffe0007ae2238:      	stp	q0, q0, [sp, #112]
fffffe0007ae223c:      	stp	q0, q0, [sp, #144]
fffffe0007ae2240:      	bl	0xfffffe00079a1d1c <_niwj_decode_identity>
fffffe0007ae2244:      	ldr	w8, [x21, #976]
fffffe0007ae2248:      	mov	w22, w0
fffffe0007ae224c:      	add	w9, w8, #1
fffffe0007ae2250:      	cmp	w8, #31
fffffe0007ae2254:      	str	w9, [x21, #976]
fffffe0007ae2258:      	b.hi	0xfffffe0007ae236c <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x274>
fffffe0007ae225c:      	adrp	x8, 0xfffffe0008534000 <_nord2_wifi_ifnet_receive+0x160>
fffffe0007ae2260:      	ldr	x11, [x8, #2992]
fffffe0007ae2264:      	cmn	x11, #1
fffffe0007ae2268:      	b.eq	0xfffffe0007ae236c <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x274>
fffffe0007ae226c:      	mov	w8, wzr
fffffe0007ae2270:      	mov	x9, x11
fffffe0007ae2274:      	adrp	x10, 0xfffffe0008534000 <_nord2_wifi_ifnet_receive+0x178>
fffffe0007ae2278:      	add	x10, x10, #2992
fffffe0007ae227c:      	add	x24, x11, #1
fffffe0007ae2280:      	cas	x9, x24, [x10]
fffffe0007ae2284:      	cmp	x9, x11
fffffe0007ae2288:      	b.eq	0xfffffe0007ae22d4 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x1dc>
fffffe0007ae228c:      	cmp	w8, #14
fffffe0007ae2290:      	b.hi	0xfffffe0007ae236c <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x274>
fffffe0007ae2294:      	add	w8, w8, #1
fffffe0007ae2298:      	mov	x11, x9
fffffe0007ae229c:      	cmn	x9, #1
fffffe0007ae22a0:      	b.ne	0xfffffe0007ae227c <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x184>
fffffe0007ae22a4:      	b	0xfffffe0007ae236c <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x274>
fffffe0007ae22a8:      	adrp	x0, 0xfffffe0007151000 <_nbg_i2c_read+0x73c>
fffffe0007ae22ac:      	add	x0, x0, #1098
fffffe0007ae22b0:      	stp	x12, x19, [sp]
fffffe0007ae22b4:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae22b8:      	mov	x0, x20
fffffe0007ae22bc:      	mov	w1, #104
fffffe0007ae22c0:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe0007ae22c4:      	mov	x0, x20
fffffe0007ae22c8:      	mov	w1, #104
fffffe0007ae22cc:      	bl	0xfffffe0007a10838 <_IOFree>
fffffe0007ae22d0:      	b	0xfffffe0007ae2144 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x4c>
fffffe0007ae22d4:      	ldp	w8, w27, [sp, #116]
fffffe0007ae22d8:      	ldr	w9, [sp, #112]
fffffe0007ae22dc:      	ldp	w25, w26, [sp, #132]
fffffe0007ae22e0:      	str	x8, [sp, #80]
fffffe0007ae22e4:      	ldp	w28, w8, [sp, #124]
fffffe0007ae22e8:      	stp	x8, x9, [sp, #88]
fffffe0007ae22ec:      	bl	0xfffffe0007ae2430 <niw_ms()>
fffffe0007ae22f0:      	ldr	x8, [sp, #80]
fffffe0007ae22f4:      	stp	x26, x0, [sp, #64]
fffffe0007ae22f8:      	adrp	x0, 0xfffffe0007151000 <_nbg_i2c_read+0x78c>
fffffe0007ae22fc:      	add	x0, x0, #1155
fffffe0007ae2300:      	ldr	x9, [sp, #96]
fffffe0007ae2304:      	stp	x28, x25, [sp, #48]
fffffe0007ae2308:      	stp	x8, x27, [sp, #32]
fffffe0007ae230c:      	ldr	x8, [sp, #88]
fffffe0007ae2310:      	stp	x24, x23, [sp]
fffffe0007ae2314:      	stp	x8, x9, [sp, #16]
fffffe0007ae2318:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae231c:      	ldp	w8, w9, [sp, #140]
fffffe0007ae2320:      	ldp	w10, w11, [sp, #148]
fffffe0007ae2324:      	ldp	w12, w13, [sp, #156]
fffffe0007ae2328:      	ldr	w14, [sp, #164]
fffffe0007ae232c:      	adrp	x0, 0xfffffe0007151000 <_nbg_i2c_read+0x7c0>
fffffe0007ae2330:      	add	x0, x0, #1294
fffffe0007ae2334:      	stp	x24, x8, [sp]
fffffe0007ae2338:      	stp	x9, x10, [sp, #16]
fffffe0007ae233c:      	stp	x13, x14, [sp, #48]
fffffe0007ae2340:      	stp	x11, x12, [sp, #32]
fffffe0007ae2344:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae2348:      	ldp	w8, w9, [sp, #176]
fffffe0007ae234c:      	ldp	w11, w12, [sp, #168]
fffffe0007ae2350:      	ldr	w10, [sp, #184]
fffffe0007ae2354:      	adrp	x0, 0xfffffe0007151000 <_nbg_i2c_read+0x7e8>
fffffe0007ae2358:      	add	x0, x0, #1456
fffffe0007ae235c:      	stp	x24, x8, [sp]
fffffe0007ae2360:      	stp	x11, x12, [sp, #32]
fffffe0007ae2364:      	stp	x9, x10, [sp, #16]
fffffe0007ae2368:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae236c:      	cbz	w22, 0xfffffe0007ae239c <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x2a4>
fffffe0007ae2370:      	mov	x0, x20
fffffe0007ae2374:      	mov	w1, #104
fffffe0007ae2378:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe0007ae237c:      	mov	x0, x20
fffffe0007ae2380:      	mov	w1, #104
fffffe0007ae2384:      	bl	0xfffffe0007a10838 <_IOFree>
fffffe0007ae2388:      	sub	w8, w19, #22
fffffe0007ae238c:      	sub	w9, w19, #17
fffffe0007ae2390:      	cmn	w22, #1
fffffe0007ae2394:      	csel	w19, w8, w9, eq
fffffe0007ae2398:      	b	0xfffffe0007ae2144 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x4c>
fffffe0007ae239c:      	ldr	x8, [x21]
fffffe0007ae23a0:      	mov	x0, x21
fffffe0007ae23a4:      	ldr	x8, [x8, #32]
fffffe0007ae23a8:      	blr	x8
fffffe0007ae23ac:      	ldr	x2, [x21, #328]
fffffe0007ae23b0:      	adr	x3, #4816
fffffe0007ae23b4:      	nop
fffffe0007ae23b8:      	add	x4, sp, #104
fffffe0007ae23bc:      	mov	x0, x20
fffffe0007ae23c0:      	mov	x1, x21
fffffe0007ae23c4:      	str	xzr, [sp, #104]
fffffe0007ae23c8:      	bl	0xfffffe000750e1a8 <_nord2_wifi_join_enqueue>
fffffe0007ae23cc:      	mov	w22, w0
fffffe0007ae23d0:      	mov	x0, x20
fffffe0007ae23d4:      	mov	w1, #104
fffffe0007ae23d8:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe0007ae23dc:      	mov	x0, x20
fffffe0007ae23e0:      	mov	w1, #104
fffffe0007ae23e4:      	bl	0xfffffe0007a10838 <_IOFree>
fffffe0007ae23e8:      	cbz	w22, 0xfffffe0007ae2418 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x320>
fffffe0007ae23ec:      	ldr	x8, [x21]
fffffe0007ae23f0:      	mov	x0, x21
fffffe0007ae23f4:      	ldr	x8, [x8, #40]
fffffe0007ae23f8:      	blr	x8
fffffe0007ae23fc:      	sub	w8, w19, #22
fffffe0007ae2400:      	cmp	w22, #0
fffffe0007ae2404:      	csel	w8, w8, w19, lt
fffffe0007ae2408:      	cmp	w22, #2
fffffe0007ae240c:      	mov	w9, #16
fffffe0007ae2410:      	csel	w19, w9, w8, eq
fffffe0007ae2414:      	b	0xfffffe0007ae2144 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x4c>
fffffe0007ae2418:      	mov	w19, wzr
fffffe0007ae241c:      	ldr	x8, [sp, #104]
fffffe0007ae2420:      	ldr	x9, [x21, #328]
fffffe0007ae2424:      	str	x8, [x21, #960]
fffffe0007ae2428:      	str	x9, [x21, #968]
fffffe0007ae242c:      	b	0xfffffe0007ae2144 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)+0x4c>
