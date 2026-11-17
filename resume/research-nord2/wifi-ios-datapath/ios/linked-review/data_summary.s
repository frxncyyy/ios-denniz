
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b232c <_nsdd_summary>:
fffffe00079b232c:      	sub	sp, sp, #144
fffffe00079b2330:      	stp	x24, x23, [sp, #80]
fffffe00079b2334:      	stp	x22, x21, [sp, #96]
fffffe00079b2338:      	stp	x20, x19, [sp, #112]
fffffe00079b233c:      	stp	x29, x30, [sp, #128]
fffffe00079b2340:      	add	x29, sp, #128
fffffe00079b2344:      	cbz	x0, 0xfffffe00079b2714 <_nsdd_summary+0x3e8>
fffffe00079b2348:      	mov	x20, x0
fffffe00079b234c:      	ldr	w8, [x0, #440]
fffffe00079b2350:      	cbnz	w8, 0xfffffe00079b2714 <_nsdd_summary+0x3e8>
fffffe00079b2354:      	mov	x19, x1
fffffe00079b2358:      	mov	w9, #1
fffffe00079b235c:      	ldr	w8, [x20, #168]
fffffe00079b2360:      	str	w9, [x20, #440]
fffffe00079b2364:      	cbz	w8, 0xfffffe00079b2380 <_nsdd_summary+0x54>
fffffe00079b2368:      	mov	w9, #1
fffffe00079b236c:      	adrp	x0, 0xfffffe0007134000 <_rw+0x18>
fffffe00079b2370:      	add	x0, x0, #2489
fffffe00079b2374:      	str	x19, [sp]
fffffe00079b2378:      	stp	x9, x8, [sp, #8]
fffffe00079b237c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2380:      	ldr	w8, [x20, #172]
fffffe00079b2384:      	cbz	w8, 0xfffffe00079b23a0 <_nsdd_summary+0x74>
fffffe00079b2388:      	mov	w9, #2
fffffe00079b238c:      	adrp	x0, 0xfffffe0007134000 <_rw+0x38>
fffffe00079b2390:      	add	x0, x0, #2489
fffffe00079b2394:      	str	x19, [sp]
fffffe00079b2398:      	stp	x9, x8, [sp, #8]
fffffe00079b239c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b23a0:      	ldr	w8, [x20, #176]
fffffe00079b23a4:      	cbz	w8, 0xfffffe00079b23c0 <_nsdd_summary+0x94>
fffffe00079b23a8:      	mov	w9, #3
fffffe00079b23ac:      	adrp	x0, 0xfffffe0007134000 <_rw+0x58>
fffffe00079b23b0:      	add	x0, x0, #2489
fffffe00079b23b4:      	str	x19, [sp]
fffffe00079b23b8:      	stp	x9, x8, [sp, #8]
fffffe00079b23bc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b23c0:      	ldr	w8, [x20, #180]
fffffe00079b23c4:      	cbz	w8, 0xfffffe00079b23e0 <_nsdd_summary+0xb4>
fffffe00079b23c8:      	mov	w9, #4
fffffe00079b23cc:      	adrp	x0, 0xfffffe0007134000 <_rw+0x78>
fffffe00079b23d0:      	add	x0, x0, #2489
fffffe00079b23d4:      	str	x19, [sp]
fffffe00079b23d8:      	stp	x9, x8, [sp, #8]
fffffe00079b23dc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b23e0:      	ldr	w8, [x20, #184]
fffffe00079b23e4:      	cbz	w8, 0xfffffe00079b2400 <_nsdd_summary+0xd4>
fffffe00079b23e8:      	mov	w9, #5
fffffe00079b23ec:      	adrp	x0, 0xfffffe0007134000 <_decode+0x8>
fffffe00079b23f0:      	add	x0, x0, #2489
fffffe00079b23f4:      	str	x19, [sp]
fffffe00079b23f8:      	stp	x9, x8, [sp, #8]
fffffe00079b23fc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2400:      	ldr	w8, [x20, #188]
fffffe00079b2404:      	cbz	w8, 0xfffffe00079b2420 <_nsdd_summary+0xf4>
fffffe00079b2408:      	mov	w9, #6
fffffe00079b240c:      	adrp	x0, 0xfffffe0007134000 <_decode+0x28>
fffffe00079b2410:      	add	x0, x0, #2489
fffffe00079b2414:      	str	x19, [sp]
fffffe00079b2418:      	stp	x9, x8, [sp, #8]
fffffe00079b241c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2420:      	ldr	w8, [x20, #192]
fffffe00079b2424:      	cbz	w8, 0xfffffe00079b2440 <_nsdd_summary+0x114>
fffffe00079b2428:      	mov	w9, #7
fffffe00079b242c:      	adrp	x0, 0xfffffe0007134000 <_decode+0x48>
fffffe00079b2430:      	add	x0, x0, #2489
fffffe00079b2434:      	str	x19, [sp]
fffffe00079b2438:      	stp	x9, x8, [sp, #8]
fffffe00079b243c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2440:      	ldr	w8, [x20, #196]
fffffe00079b2444:      	cbz	w8, 0xfffffe00079b2460 <_nsdd_summary+0x134>
fffffe00079b2448:      	mov	w9, #8
fffffe00079b244c:      	adrp	x0, 0xfffffe0007134000 <_decode+0x68>
fffffe00079b2450:      	add	x0, x0, #2489
fffffe00079b2454:      	str	x19, [sp]
fffffe00079b2458:      	stp	x9, x8, [sp, #8]
fffffe00079b245c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2460:      	ldr	w8, [x20, #200]
fffffe00079b2464:      	cbz	w8, 0xfffffe00079b2480 <_nsdd_summary+0x154>
fffffe00079b2468:      	mov	w9, #9
fffffe00079b246c:      	adrp	x0, 0xfffffe0007134000 <_decode+0x88>
fffffe00079b2470:      	add	x0, x0, #2489
fffffe00079b2474:      	str	x19, [sp]
fffffe00079b2478:      	stp	x9, x8, [sp, #8]
fffffe00079b247c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2480:      	ldr	w8, [x20, #204]
fffffe00079b2484:      	cbz	w8, 0xfffffe00079b24a0 <_nsdd_summary+0x174>
fffffe00079b2488:      	mov	w9, #10
fffffe00079b248c:      	adrp	x0, 0xfffffe0007134000 <_decode+0xa8>
fffffe00079b2490:      	add	x0, x0, #2489
fffffe00079b2494:      	str	x19, [sp]
fffffe00079b2498:      	stp	x9, x8, [sp, #8]
fffffe00079b249c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b24a0:      	ldr	w8, [x20, #208]
fffffe00079b24a4:      	cbz	w8, 0xfffffe00079b24c0 <_nsdd_summary+0x194>
fffffe00079b24a8:      	mov	w9, #11
fffffe00079b24ac:      	adrp	x0, 0xfffffe0007134000 <_decode+0xc8>
fffffe00079b24b0:      	add	x0, x0, #2489
fffffe00079b24b4:      	str	x19, [sp]
fffffe00079b24b8:      	stp	x9, x8, [sp, #8]
fffffe00079b24bc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b24c0:      	ldr	w8, [x20, #212]
fffffe00079b24c4:      	cbz	w8, 0xfffffe00079b24e0 <_nsdd_summary+0x1b4>
fffffe00079b24c8:      	mov	w9, #12
fffffe00079b24cc:      	adrp	x0, 0xfffffe0007134000 <_decode+0xe8>
fffffe00079b24d0:      	add	x0, x0, #2489
fffffe00079b24d4:      	str	x19, [sp]
fffffe00079b24d8:      	stp	x9, x8, [sp, #8]
fffffe00079b24dc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b24e0:      	ldr	w8, [x20, #216]
fffffe00079b24e4:      	cbz	w8, 0xfffffe00079b2500 <_nsdd_summary+0x1d4>
fffffe00079b24e8:      	mov	w9, #13
fffffe00079b24ec:      	adrp	x0, 0xfffffe0007134000 <_decode+0x108>
fffffe00079b24f0:      	add	x0, x0, #2489
fffffe00079b24f4:      	str	x19, [sp]
fffffe00079b24f8:      	stp	x9, x8, [sp, #8]
fffffe00079b24fc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2500:      	ldr	w8, [x20, #292]
fffffe00079b2504:      	adrp	x0, 0xfffffe0007134000 <_decode+0x120>
fffffe00079b2508:      	add	x0, x0, #2536
fffffe00079b250c:      	ldr	w9, [x20, #304]
fffffe00079b2510:      	ldr	w10, [x20, #308]
fffffe00079b2514:      	ldr	w11, [x20, #328]
fffffe00079b2518:      	ldr	w12, [x20, #340]
fffffe00079b251c:      	ldr	w13, [x20, #344]
fffffe00079b2520:      	stp	x8, x9, [sp, #8]
fffffe00079b2524:      	stp	x10, x11, [sp, #24]
fffffe00079b2528:      	str	x19, [sp]
fffffe00079b252c:      	stp	x12, x13, [sp, #40]
fffffe00079b2530:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2534:      	strb	wzr, [x20, #364]
fffffe00079b2538:      	adrp	x0, 0xfffffe0007134000 <_decode+0x154>
fffffe00079b253c:      	add	x0, x0, #2630
fffffe00079b2540:      	strb	wzr, [x20, #365]
fffffe00079b2544:      	strb	wzr, [x20, #366]
fffffe00079b2548:      	strb	wzr, [x20, #367]
fffffe00079b254c:      	strb	wzr, [x20, #368]
fffffe00079b2550:      	strb	wzr, [x20, #369]
fffffe00079b2554:      	strb	wzr, [x20, #370]
fffffe00079b2558:      	strb	wzr, [x20, #371]
fffffe00079b255c:      	strb	wzr, [x20, #372]
fffffe00079b2560:      	strb	wzr, [x20, #373]
fffffe00079b2564:      	strb	wzr, [x20, #374]
fffffe00079b2568:      	strb	wzr, [x20, #375]
fffffe00079b256c:      	strb	wzr, [x20, #376]
fffffe00079b2570:      	strb	wzr, [x20, #377]
fffffe00079b2574:      	strb	wzr, [x20, #378]
fffffe00079b2578:      	strb	wzr, [x20, #379]
fffffe00079b257c:      	strb	wzr, [x20, #380]
fffffe00079b2580:      	strb	wzr, [x20, #381]
fffffe00079b2584:      	strb	wzr, [x20, #382]
fffffe00079b2588:      	strb	wzr, [x20, #383]
fffffe00079b258c:      	strb	wzr, [x20, #384]
fffffe00079b2590:      	strb	wzr, [x20, #385]
fffffe00079b2594:      	strb	wzr, [x20, #386]
fffffe00079b2598:      	strb	wzr, [x20, #387]
fffffe00079b259c:      	strb	wzr, [x20, #388]
fffffe00079b25a0:      	strb	wzr, [x20, #389]
fffffe00079b25a4:      	strb	wzr, [x20, #390]
fffffe00079b25a8:      	strb	wzr, [x20, #391]
fffffe00079b25ac:      	strb	wzr, [x20, #392]
fffffe00079b25b0:      	strb	wzr, [x20, #393]
fffffe00079b25b4:      	strb	wzr, [x20, #394]
fffffe00079b25b8:      	strb	wzr, [x20, #395]
fffffe00079b25bc:      	strb	wzr, [x20, #396]
fffffe00079b25c0:      	strb	wzr, [x20, #397]
fffffe00079b25c4:      	strb	wzr, [x20, #398]
fffffe00079b25c8:      	strb	wzr, [x20, #399]
fffffe00079b25cc:      	strb	wzr, [x20, #400]
fffffe00079b25d0:      	strb	wzr, [x20, #401]
fffffe00079b25d4:      	strb	wzr, [x20, #402]
fffffe00079b25d8:      	strb	wzr, [x20, #403]
fffffe00079b25dc:      	strb	wzr, [x20, #404]
fffffe00079b25e0:      	strb	wzr, [x20, #405]
fffffe00079b25e4:      	strb	wzr, [x20, #406]
fffffe00079b25e8:      	strb	wzr, [x20, #407]
fffffe00079b25ec:      	strb	wzr, [x20, #408]
fffffe00079b25f0:      	strb	wzr, [x20, #409]
fffffe00079b25f4:      	strb	wzr, [x20, #410]
fffffe00079b25f8:      	strb	wzr, [x20, #411]
fffffe00079b25fc:      	ldp	w8, w9, [x20]
fffffe00079b2600:      	ldp	w10, w11, [x20, #8]
fffffe00079b2604:      	ldp	w12, w13, [x20, #16]
fffffe00079b2608:      	ldp	w14, w15, [x20, #144]
fffffe00079b260c:      	ldr	w16, [x20, #152]
fffffe00079b2610:      	str	wzr, [x20, #280]
fffffe00079b2614:      	str	wzr, [x20, #276]
fffffe00079b2618:      	stp	x11, x12, [sp, #32]
fffffe00079b261c:      	stp	x15, x16, [sp, #64]
fffffe00079b2620:      	stp	x13, x14, [sp, #48]
fffffe00079b2624:      	stp	x9, x10, [sp, #16]
fffffe00079b2628:      	stp	x19, x8, [sp]
fffffe00079b262c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2630:      	ldp	w8, w9, [x20, #24]
fffffe00079b2634:      	ldp	w10, w11, [x20, #32]
fffffe00079b2638:      	ldp	w12, w13, [x20, #40]
fffffe00079b263c:      	adrp	x21, 0xfffffe0007134000 <_decode+0x258>
fffffe00079b2640:      	add	x21, x21, #2760
fffffe00079b2644:      	ldr	w14, [x20, #48]
fffffe00079b2648:      	mov	w24, #1
fffffe00079b264c:      	mov	x0, x21
fffffe00079b2650:      	stp	x9, x10, [sp, #24]
fffffe00079b2654:      	stp	x11, x12, [sp, #40]
fffffe00079b2658:      	stp	x13, x14, [sp, #56]
fffffe00079b265c:      	stp	x24, x8, [sp, #8]
fffffe00079b2660:      	str	x19, [sp]
fffffe00079b2664:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2668:      	ldp	w11, w8, [x20, #84]
fffffe00079b266c:      	ldp	w9, w10, [x20, #100]
fffffe00079b2670:      	mov	w23, #-1
fffffe00079b2674:      	adrp	x22, 0xfffffe0007134000 <_decode+0x290>
fffffe00079b2678:      	add	x22, x22, #2858
fffffe00079b267c:      	ldp	w12, w13, [x20, #92]
fffffe00079b2680:      	add	x8, x9, x8
fffffe00079b2684:      	mov	x0, x22
fffffe00079b2688:      	add	x8, x8, x10
fffffe00079b268c:      	ldp	w9, w10, [x20, #76]
fffffe00079b2690:      	cmp	x8, x23
fffffe00079b2694:      	stp	x11, x12, [sp, #32]
fffffe00079b2698:      	csel	x8, x8, x23, lo
fffffe00079b269c:      	stp	x19, x24, [sp]
fffffe00079b26a0:      	stp	x9, x10, [sp, #16]
fffffe00079b26a4:      	stp	x13, x8, [sp, #48]
fffffe00079b26a8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b26ac:      	ldp	w8, w9, [x20, #8]
fffffe00079b26b0:      	ldp	w10, w11, [x20, #52]
fffffe00079b26b4:      	ldp	w12, w13, [x20, #60]
fffffe00079b26b8:      	ldr	w14, [x20, #68]
fffffe00079b26bc:      	mov	w24, #2
fffffe00079b26c0:      	mov	x0, x21
fffffe00079b26c4:      	str	x19, [sp]
fffffe00079b26c8:      	stp	x9, x10, [sp, #24]
fffffe00079b26cc:      	stp	x13, x14, [sp, #56]
fffffe00079b26d0:      	stp	x11, x12, [sp, #40]
fffffe00079b26d4:      	stp	x24, x8, [sp, #8]
fffffe00079b26d8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b26dc:      	ldp	w11, w8, [x20, #120]
fffffe00079b26e0:      	ldp	w9, w10, [x20, #136]
fffffe00079b26e4:      	ldp	w12, w13, [x20, #128]
fffffe00079b26e8:      	mov	x0, x22
fffffe00079b26ec:      	add	x8, x9, x8
fffffe00079b26f0:      	add	x8, x8, x10
fffffe00079b26f4:      	ldp	w9, w10, [x20, #112]
fffffe00079b26f8:      	cmp	x8, x23
fffffe00079b26fc:      	stp	x11, x12, [sp, #32]
fffffe00079b2700:      	csel	x8, x8, x23, lo
fffffe00079b2704:      	stp	x19, x24, [sp]
fffffe00079b2708:      	stp	x9, x10, [sp, #16]
fffffe00079b270c:      	stp	x13, x8, [sp, #48]
fffffe00079b2710:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b2714:      	ldp	x29, x30, [sp, #128]
fffffe00079b2718:      	ldp	x20, x19, [sp, #112]
fffffe00079b271c:      	ldp	x22, x21, [sp, #96]
fffffe00079b2720:      	ldp	x24, x23, [sp, #80]
fffffe00079b2724:      	add	sp, sp, #144
fffffe00079b2728:      	ret
