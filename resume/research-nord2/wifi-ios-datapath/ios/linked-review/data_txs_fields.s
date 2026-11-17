
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b2160 <_nsdd_event>:
fffffe00079b2160:      	sub	sp, sp, #192
fffffe00079b2164:      	stp	x28, x27, [sp, #96]
fffffe00079b2168:      	stp	x26, x25, [sp, #112]
fffffe00079b216c:      	stp	x24, x23, [sp, #128]
fffffe00079b2170:      	stp	x22, x21, [sp, #144]
fffffe00079b2174:      	stp	x20, x19, [sp, #160]
fffffe00079b2178:      	stp	x29, x30, [sp, #176]
fffffe00079b217c:      	add	x29, sp, #176
fffffe00079b2180:      	cbz	x0, 0xfffffe00079b22fc <_nsdd_event+0x19c>
fffffe00079b2184:      	cmp	w4, #1
fffffe00079b2188:      	b.hi	0xfffffe00079b22fc <_nsdd_event+0x19c>
fffffe00079b218c:      	mov	x21, x0
fffffe00079b2190:      	ldr	w8, [x0, #440]
fffffe00079b2194:      	cbnz	w8, 0xfffffe00079b22fc <_nsdd_event+0x19c>
fffffe00079b2198:      	mov	x20, x2
fffffe00079b219c:      	cbz	x2, 0xfffffe00079b22fc <_nsdd_event+0x19c>
fffffe00079b21a0:      	mov	x8, #-4097
fffffe00079b21a4:      	add	x8, x3, x8
fffffe00079b21a8:      	cmn	x8, #4009
fffffe00079b21ac:      	b.lo	0xfffffe00079b22fc <_nsdd_event+0x19c>
fffffe00079b21b0:      	mov	x19, x1
fffffe00079b21b4:      	ldrb	w23, [x20, #8]
fffffe00079b21b8:      	ldrb	w26, [x20, #1]
fffffe00079b21bc:      	tbnz	w23, #0, 0xfffffe00079b21d8 <_nsdd_event+0x78>
fffffe00079b21c0:      	mov	w13, wzr
fffffe00079b21c4:      	mov	w28, wzr
fffffe00079b21c8:      	mov	w24, wzr
fffffe00079b21cc:      	mov	w22, wzr
fffffe00079b21d0:      	mov	w27, wzr
fffffe00079b21d4:      	b	0xfffffe00079b21ec <_nsdd_event+0x8c>
fffffe00079b21d8:      	ldrh	w13, [x20, #2]
fffffe00079b21dc:      	ldrb	w28, [x20, #5]
fffffe00079b21e0:      	ldrh	w24, [x20, #6]
fffffe00079b21e4:      	ldrb	w22, [x20, #9]
fffffe00079b21e8:      	ldrb	w27, [x20, #14]
fffffe00079b21ec:      	ldrb	w25, [x20]
fffffe00079b21f0:      	mov	x9, x21
fffffe00079b21f4:      	ldrb	w8, [x20, #4]
fffffe00079b21f8:      	cbz	w26, 0xfffffe00079b231c <_nsdd_event+0x1bc>
fffffe00079b21fc:      	ldr	w10, [x9, #148]!
fffffe00079b2200:      	cmn	w10, #1
fffffe00079b2204:      	b.eq	0xfffffe00079b2210 <_nsdd_event+0xb0>
fffffe00079b2208:      	add	w10, w10, #1
fffffe00079b220c:      	str	w10, [x9]
fffffe00079b2210:      	cmp	w25, #127
fffffe00079b2214:      	b.ne	0xfffffe00079b222c <_nsdd_event+0xcc>
fffffe00079b2218:      	ldr	w9, [x21, #152]
fffffe00079b221c:      	cmn	w9, #1
fffffe00079b2220:      	b.eq	0xfffffe00079b222c <_nsdd_event+0xcc>
fffffe00079b2224:      	add	w9, w9, #1
fffffe00079b2228:      	str	w9, [x21, #152]
fffffe00079b222c:      	cmp	w4, #0
fffffe00079b2230:      	mov	w9, #432
fffffe00079b2234:      	mov	w10, #428
fffffe00079b2238:      	mov	w11, #16
fffffe00079b223c:      	csel	x9, x10, x9, eq
fffffe00079b2240:      	mov	w12, #8
fffffe00079b2244:      	csel	w11, w12, w11, eq
fffffe00079b2248:      	ldr	w10, [x21, x9]
fffffe00079b224c:      	cmp	w10, w11
fffffe00079b2250:      	b.hs	0xfffffe00079b22fc <_nsdd_event+0x19c>
fffffe00079b2254:      	add	w10, w10, #1
fffffe00079b2258:      	cmp	w25, #127
fffffe00079b225c:      	cset	w11, eq
fffffe00079b2260:      	adrp	x0, 0xfffffe0007134000 <_mr+0x44>
fffffe00079b2264:      	add	x0, x0, #2187
fffffe00079b2268:      	str	w10, [x21, x9]
fffffe00079b226c:      	and	w10, w23, #0x1
fffffe00079b2270:      	ldr	w9, [x21, #436]
fffffe00079b2274:      	stp	x13, x10, [sp, #80]
fffffe00079b2278:      	add	w9, w9, #1
fffffe00079b227c:      	ldp	w10, w12, [x21, #156]
fffffe00079b2280:      	str	w9, [x21, #436]
fffffe00079b2284:      	stp	x26, x11, [sp, #32]
fffffe00079b2288:      	stp	x10, x12, [sp, #48]
fffffe00079b228c:      	stp	x25, x8, [sp, #16]
fffffe00079b2290:      	stp	x9, x19, [sp]
fffffe00079b2294:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2298:      	ldr	w8, [x21, #436]
fffffe00079b229c:      	stp	x22, x27, [sp, #48]
fffffe00079b22a0:      	ldp	x9, x22, [sp, #80]
fffffe00079b22a4:      	adrp	x0, 0xfffffe0007134000 <_mr+0x88>
fffffe00079b22a8:      	add	x0, x0, #2290
fffffe00079b22ac:      	stp	x28, x24, [sp, #16]
fffffe00079b22b0:      	stp	x8, x9, [sp]
fffffe00079b22b4:      	stp	x23, x22, [sp, #32]
fffffe00079b22b8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b22bc:      	cbz	w22, 0xfffffe00079b22fc <_nsdd_event+0x19c>
fffffe00079b22c0:      	ldrb	w8, [x20, #10]
fffffe00079b22c4:      	adrp	x0, 0xfffffe0007134000 <_mw+0x14>
fffffe00079b22c8:      	add	x0, x0, #2382
fffffe00079b22cc:      	ldrb	w9, [x20, #11]
fffffe00079b22d0:      	ldrb	w10, [x20, #12]
fffffe00079b22d4:      	ldrb	w11, [x20, #13]
fffffe00079b22d8:      	ldr	w12, [x21, #436]
fffffe00079b22dc:      	ldr	w13, [x20, #16]
fffffe00079b22e0:      	ldr	w14, [x20, #24]
fffffe00079b22e4:      	stp	x10, x11, [sp, #48]
fffffe00079b22e8:      	stp	x8, x9, [sp, #32]
fffffe00079b22ec:      	stp	x25, x26, [sp, #16]
fffffe00079b22f0:      	stp	x13, x14, [sp, #64]
fffffe00079b22f4:      	stp	x19, x12, [sp]
fffffe00079b22f8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b22fc:      	ldp	x29, x30, [sp, #176]
fffffe00079b2300:      	ldp	x20, x19, [sp, #160]
fffffe00079b2304:      	ldp	x22, x21, [sp, #144]
fffffe00079b2308:      	ldp	x24, x23, [sp, #128]
fffffe00079b230c:      	ldp	x26, x25, [sp, #112]
fffffe00079b2310:      	ldp	x28, x27, [sp, #96]
fffffe00079b2314:      	add	sp, sp, #192
fffffe00079b2318:      	ret
fffffe00079b231c:      	ldr	w10, [x9, #144]!
fffffe00079b2320:      	cmn	w10, #1
fffffe00079b2324:      	b.ne	0xfffffe00079b2208 <_nsdd_event+0xa8>
fffffe00079b2328:      	b	0xfffffe00079b2210 <_nsdd_event+0xb0>
