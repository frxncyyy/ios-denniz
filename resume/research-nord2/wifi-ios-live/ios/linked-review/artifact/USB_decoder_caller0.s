
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe000753042c <_nord2_usb_enumerate>:
fffffe000753042c:      	stp	d9, d8, [sp, #-112]!
fffffe0007530430:      	stp	x28, x27, [sp, #16]
fffffe0007530434:      	stp	x26, x25, [sp, #32]
fffffe0007530438:      	stp	x24, x23, [sp, #48]
fffffe000753043c:      	stp	x22, x21, [sp, #64]
fffffe0007530440:      	stp	x20, x19, [sp, #80]
fffffe0007530444:      	stp	x29, x30, [sp, #96]
fffffe0007530448:      	add	x29, sp, #96
fffffe000753044c:      	sub	sp, sp, #1200
fffffe0007530450:      	adrp	x8, 0xfffffe0007c9c000 <_munge_wwwwwlll+0x40>
fffffe0007530454:      	mov	w21, w1
fffffe0007530458:      	mov	x20, x0
fffffe000753045c:      	add	x0, sp, #128
fffffe0007530460:      	mov	w1, #1024
fffffe0007530464:      	mov	w19, w2
fffffe0007530468:      	nop
fffffe000753046c:      	ldr	x8, [x8, #3440]
fffffe0007530470:      	stur	x8, [x29, #-112]
fffffe0007530474:      	bl	0xfffffe0007bb02c0 <_bzero>
fffffe0007530478:      	adrp	x26, 0xfffffe0008532000 <_getattrlistat+0x7c>
fffffe000753047c:      	str	xzr, [sp, #120]
fffffe0007530480:      	str	wzr, [sp, #116]
fffffe0007530484:      	str	x20, [x26, #3024]
fffffe0007530488:      	ldr	w9, [x20, #3088]
fffffe000753048c:      	dmb	oshld
fffffe0007530490:      	and	x8, x9, #0x1f
fffffe0007530494:      	sub	w10, w8, #1
fffffe0007530498:      	cmp	w10, #14
fffffe000753049c:      	b.hi	0xfffffe0007530534 <_nord2_usb_enumerate+0x108>
fffffe00075304a0:      	ubfx	x9, x9, #8, #5
fffffe00075304a4:      	cmp	w9, #15
fffffe00075304a8:      	b.hi	0xfffffe0007530534 <_nord2_usb_enumerate+0x108>
fffffe00075304ac:      	cbz	w9, 0xfffffe0007530534 <_nord2_usb_enumerate+0x108>
fffffe00075304b0:      	cmp	w8, w9
fffffe00075304b4:      	mov	x10, xzr
fffffe00075304b8:      	sub	x27, x29, #144
fffffe00075304bc:      	csel	w11, w8, w9, hi
fffffe00075304c0:      	mov	w12, #1552
fffffe00075304c4:      	b	0xfffffe00075304d8 <_nord2_usb_enumerate+0xac>
fffffe00075304c8:      	add	x12, x12, #16
fffffe00075304cc:      	add	x10, x10, #1
fffffe00075304d0:      	cmp	x11, x10
fffffe00075304d4:      	b.eq	0xfffffe0007530578 <_nord2_usb_enumerate+0x14c>
fffffe00075304d8:      	add	x13, x10, #1
fffffe00075304dc:      	cmp	x13, x8
fffffe00075304e0:      	b.hi	0xfffffe00075304f8 <_nord2_usb_enumerate+0xcc>
fffffe00075304e4:      	ldr	x14, [x26, #3024]
fffffe00075304e8:      	add	x14, x12, x14
fffffe00075304ec:      	ldur	w14, [x14, #-256]
fffffe00075304f0:      	dmb	oshld
fffffe00075304f4:      	tbnz	w14, #15, 0xfffffe0007530510 <_nord2_usb_enumerate+0xe4>
fffffe00075304f8:      	cmp	x13, x9
fffffe00075304fc:      	b.hi	0xfffffe00075304c8 <_nord2_usb_enumerate+0x9c>
fffffe0007530500:      	ldr	x13, [x26, #3024]
fffffe0007530504:      	ldr	w13, [x12, x13]
fffffe0007530508:      	dmb	oshld
fffffe000753050c:      	tbz	w13, #15, 0xfffffe00075304c8 <_nord2_usb_enumerate+0x9c>
fffffe0007530510:      	adrp	x8, 0xfffffe0007c9c000 <_munge_wwwwwwll+0x3c>
fffffe0007530514:      	nop
fffffe0007530518:      	ldur	x9, [x29, #-112]
fffffe000753051c:      	ldr	x8, [x8, #3440]
fffffe0007530520:      	cmp	x8, x9
fffffe0007530524:      	b.ne	0xfffffe0007531560 <_nord2_usb_enumerate+0x1134>
fffffe0007530528:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x5e8>
fffffe000753052c:      	add	x0, x0, #2697
fffffe0007530530:      	b	0xfffffe0007530554 <_nord2_usb_enumerate+0x128>
fffffe0007530534:      	adrp	x8, 0xfffffe0007c9c000 <_munge_wws>
fffffe0007530538:      	nop
fffffe000753053c:      	ldur	x9, [x29, #-112]
fffffe0007530540:      	ldr	x8, [x8, #3440]
fffffe0007530544:      	cmp	x8, x9
fffffe0007530548:      	b.ne	0xfffffe0007531560 <_nord2_usb_enumerate+0x1134>
fffffe000753054c:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x60c>
fffffe0007530550:      	add	x0, x0, #2657
fffffe0007530554:      	add	sp, sp, #1200
fffffe0007530558:      	ldp	x29, x30, [sp, #96]
fffffe000753055c:      	ldp	x20, x19, [sp, #80]
fffffe0007530560:      	ldp	x22, x21, [sp, #64]
fffffe0007530564:      	ldp	x24, x23, [sp, #48]
fffffe0007530568:      	ldp	x26, x25, [sp, #32]
fffffe000753056c:      	ldp	x28, x27, [sp, #16]
fffffe0007530570:      	ldp	d9, d8, [sp], #112
fffffe0007530574:      	b	0xfffffe0007361ae4 <_printf>
fffffe0007530578:      	ldr	x8, [x26, #3024]
fffffe000753057c:      	adrp	x23, 0xfffffe0008532000 <_getattrlistbulk>
fffffe0007530580:      	ldr	w8, [x8, #9220]
fffffe0007530584:      	dmb	oshld
fffffe0007530588:      	str	w8, [x23, #3040]
fffffe000753058c:      	tbnz	w8, #6, 0xfffffe0007530a98 <_nord2_usb_enumerate+0x66c>
fffffe0007530590:      	mov	w0, #28672
fffffe0007530594:      	mov	w1, #4096
fffffe0007530598:      	movk	w0, #4096, lsl #16
fffffe000753059c:      	bl	0xfffffe00074af50c <_ml_io_map>
fffffe00075305a0:      	mrs	x22, CNTFRQ_EL0
fffffe00075305a4:      	cbz	x0, 0xfffffe0007530abc <_nord2_usb_enumerate+0x690>
fffffe00075305a8:      	cbz	x22, 0xfffffe0007530abc <_nord2_usb_enumerate+0x690>
fffffe00075305ac:      	mov	w8, #51713
fffffe00075305b0:      	movk	w8, #15258, lsl #16
fffffe00075305b4:      	cmp	x22, x8
fffffe00075305b8:      	b.hs	0xfffffe0007530abc <_nord2_usb_enumerate+0x690>
fffffe00075305bc:      	mov	x20, x0
fffffe00075305c0:      	cbz	w21, 0xfffffe0007530748 <_nord2_usb_enumerate+0x31c>
fffffe00075305c4:      	mov	w0, #300154880
fffffe00075305c8:      	mov	w1, #1792
fffffe00075305cc:      	bl	0xfffffe00074af50c <_ml_io_map>
fffffe00075305d0:      	cbz	x0, 0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe00075305d4:      	add	x8, x0, #768
fffffe00075305d8:      	adrp	x24, 0xfffffe0008532000 <_getattrlistbulk+0x5c>
fffffe00075305dc:      	adrp	x25, 0xfffffe0008532000 <_getattrlistbulk+0x60>
fffffe00075305e0:      	mov	w9, #768
fffffe00075305e4:      	str	x8, [x24, #3128]
fffffe00075305e8:      	ldr	w8, [x0, #768]
fffffe00075305ec:      	dmb	oshld
fffffe00075305f0:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x6b0>
fffffe00075305f4:      	add	x0, x0, #3318
fffffe00075305f8:      	str	w8, [x25, #3144]
fffffe00075305fc:      	stp	x9, x8, [sp]
fffffe0007530600:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530604:      	ldr	w8, [x25, #3144]
fffffe0007530608:      	cmn	w8, #1
fffffe000753060c:      	b.eq	0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe0007530610:      	ldr	x8, [x24, #3128]
fffffe0007530614:      	adrp	x25, 0xfffffe0008532000 <_getattrlistbulk+0x98>
fffffe0007530618:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x6d8>
fffffe000753061c:      	add	x0, x0, #3318
fffffe0007530620:      	mov	w9, #788
fffffe0007530624:      	ldr	w8, [x8, #20]
fffffe0007530628:      	dmb	oshld
fffffe000753062c:      	str	w8, [x25, #3152]
fffffe0007530630:      	stp	x9, x8, [sp]
fffffe0007530634:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530638:      	ldr	w8, [x25, #3152]
fffffe000753063c:      	cmn	w8, #1
fffffe0007530640:      	b.eq	0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe0007530644:      	ldr	x8, [x24, #3128]
fffffe0007530648:      	adrp	x25, 0xfffffe0008532000 <_getattrlistbulk+0xcc>
fffffe000753064c:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x70c>
fffffe0007530650:      	add	x0, x0, #3318
fffffe0007530654:      	mov	w9, #792
fffffe0007530658:      	ldr	w8, [x8, #24]
fffffe000753065c:      	dmb	oshld
fffffe0007530660:      	str	w8, [x25, #3160]
fffffe0007530664:      	stp	x9, x8, [sp]
fffffe0007530668:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000753066c:      	ldr	w8, [x25, #3160]
fffffe0007530670:      	cmn	w8, #1
fffffe0007530674:      	b.eq	0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe0007530678:      	ldr	x8, [x24, #3128]
fffffe000753067c:      	adrp	x25, 0xfffffe0008532000 <_getattrlistbulk+0x100>
fffffe0007530680:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x740>
fffffe0007530684:      	add	x0, x0, #3318
fffffe0007530688:      	mov	w9, #796
fffffe000753068c:      	ldr	w8, [x8, #28]
fffffe0007530690:      	dmb	oshld
fffffe0007530694:      	str	w8, [x25, #3168]
fffffe0007530698:      	stp	x9, x8, [sp]
fffffe000753069c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00075306a0:      	ldr	w8, [x25, #3168]
fffffe00075306a4:      	cmn	w8, #1
fffffe00075306a8:      	b.eq	0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe00075306ac:      	ldr	x8, [x24, #3128]
fffffe00075306b0:      	adrp	x25, 0xfffffe0008532000 <_getattrlistbulk+0x134>
fffffe00075306b4:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x774>
fffffe00075306b8:      	add	x0, x0, #3318
fffffe00075306bc:      	mov	w9, #800
fffffe00075306c0:      	ldr	w8, [x8, #32]
fffffe00075306c4:      	dmb	oshld
fffffe00075306c8:      	str	w8, [x25, #3176]
fffffe00075306cc:      	stp	x9, x8, [sp]
fffffe00075306d0:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00075306d4:      	ldr	w8, [x25, #3176]
fffffe00075306d8:      	cmn	w8, #1
fffffe00075306dc:      	b.eq	0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe00075306e0:      	ldr	x8, [x24, #3128]
fffffe00075306e4:      	adrp	x25, 0xfffffe0008532000 <_getattrlistbulk+0x168>
fffffe00075306e8:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x7a8>
fffffe00075306ec:      	add	x0, x0, #3318
fffffe00075306f0:      	mov	w9, #872
fffffe00075306f4:      	ldr	w8, [x8, #104]
fffffe00075306f8:      	dmb	oshld
fffffe00075306fc:      	str	w8, [x25, #3184]
fffffe0007530700:      	stp	x9, x8, [sp]
fffffe0007530704:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530708:      	ldr	w8, [x25, #3184]
fffffe000753070c:      	cmn	w8, #1
fffffe0007530710:      	b.eq	0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe0007530714:      	ldr	x8, [x24, #3128]
fffffe0007530718:      	adrp	x24, 0xfffffe0008532000 <_getattrlistbulk+0x19c>
fffffe000753071c:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x7dc>
fffffe0007530720:      	add	x0, x0, #3318
fffffe0007530724:      	mov	w9, #876
fffffe0007530728:      	ldr	w8, [x8, #108]
fffffe000753072c:      	dmb	oshld
fffffe0007530730:      	str	w8, [x24, #3192]
fffffe0007530734:      	stp	x9, x8, [sp]
fffffe0007530738:      	bl	0xfffffe0007361ae4 <_printf>
fffffe000753073c:      	ldr	w8, [x24, #3192]
fffffe0007530740:      	cmn	w8, #1
fffffe0007530744:      	b.eq	0xfffffe0007530ae0 <_nord2_usb_enumerate+0x6b4>
fffffe0007530748:      	ldr	x8, [x26, #3024]
fffffe000753074c:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x80c>
fffffe0007530750:      	add	x0, x0, #2873
fffffe0007530754:      	ldr	w9, [x23, #3040]
fffffe0007530758:      	mov	w25, #65537
fffffe000753075c:      	ldr	w8, [x8, #2048]
fffffe0007530760:      	dmb	oshld
fffffe0007530764:      	ldr	x10, [x26, #3024]
fffffe0007530768:      	ldr	w10, [x10, #9236]
fffffe000753076c:      	dmb	oshld
fffffe0007530770:      	ldr	x11, [x26, #3024]
fffffe0007530774:      	ldr	w11, [x11, #5148]
fffffe0007530778:      	dmb	oshld
fffffe000753077c:      	stp	x9, x8, [sp]
fffffe0007530780:      	stp	x10, x11, [sp, #16]
fffffe0007530784:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530788:      	ldr	x8, [x26, #3024]
fffffe000753078c:      	adrp	x9, 0xfffffe0008532000 <_getattrlistbulk+0x210>
fffffe0007530790:      	adrp	x11, 0xfffffe0008532000 <_getattrlistbulk+0x214>
fffffe0007530794:      	adrp	x15, 0xfffffe0008532000 <_getattrlistbulk+0x218>
fffffe0007530798:      	adrp	x14, 0xfffffe0008532000 <_getattrlistbulk+0x21c>
fffffe000753079c:      	adrp	x12, 0xfffffe0008532000 <_getattrlistbulk+0x220>
fffffe00075307a0:      	ldr	w8, [x8, #4]
fffffe00075307a4:      	dmb	oshld
fffffe00075307a8:      	ldr	x10, [x26, #3024]
fffffe00075307ac:      	adrp	x13, 0xfffffe0008532000 <_getattrlistbulk+0x230>
fffffe00075307b0:      	str	w8, [x9, #3048]
fffffe00075307b4:      	ldr	w8, [x10, #132]
fffffe00075307b8:      	dmb	oshld
fffffe00075307bc:      	ldr	x9, [x26, #3024]
fffffe00075307c0:      	adrp	x10, 0xfffffe0008532000 <_getattrlistbulk+0x244>
fffffe00075307c4:      	str	w8, [x11, #3056]
fffffe00075307c8:      	nop
fffffe00075307cc:      	ldr	w8, [x9, #9240]
fffffe00075307d0:      	dmb	oshld
fffffe00075307d4:      	ldr	x9, [x26, #3024]
fffffe00075307d8:      	str	w8, [x10, #3064]
fffffe00075307dc:      	ldr	w8, [x9, #2128]
fffffe00075307e0:      	dmb	oshld
fffffe00075307e4:      	ldr	x10, [x26, #3024]
fffffe00075307e8:      	adrp	x9, 0xfffffe0008532000 <_getattrlistbulk+0x26c>
fffffe00075307ec:      	str	w8, [x11, #3072]
fffffe00075307f0:      	adrp	x11, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x8b0>
fffffe00075307f4:      	add	x11, x11, #2992
fffffe00075307f8:      	ldr	w8, [x10, #2048]
fffffe00075307fc:      	dmb	oshld
fffffe0007530800:      	strb	wzr, [x14, #3116]
fffffe0007530804:      	ldr	w10, [x15, #3012]
fffffe0007530808:      	nop
fffffe000753080c:      	str	w8, [x9, #3080]
fffffe0007530810:      	adrp	x9, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0x8d0>
fffffe0007530814:      	add	x9, x9, #2937
fffffe0007530818:      	str	wzr, [x12, #3112]
fffffe000753081c:      	cmp	w10, #23
fffffe0007530820:      	adrp	x10, 0xfffffe0008532000 <_getattrlistbulk+0x2a4>
fffffe0007530824:      	cset	w8, eq
fffffe0007530828:      	str	wzr, [x13, #3104]
fffffe000753082c:      	tst	w19, w8
fffffe0007530830:      	mov	x28, x15
fffffe0007530834:      	csel	x0, x9, x11, ne
fffffe0007530838:      	str	wzr, [x15, #3096]
fffffe000753083c:      	str	wzr, [x10, #3088]
fffffe0007530840:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530844:      	mrs	x8, DAIF
fffffe0007530848:      	msr	DAIFSet, #3
fffffe000753084c:      	str	x8, [sp, #88]
fffffe0007530850:      	adrp	x9, 0xfffffe0008532000 <_getattrlistbulk+0x2d4>
fffffe0007530854:      	mov	w8, #1
fffffe0007530858:      	ldr	x10, [x26, #3024]
fffffe000753085c:      	mov	w11, #-1
fffffe0007530860:      	strb	w8, [x9, #3032]
fffffe0007530864:      	str	w11, [x10, #12]
fffffe0007530868:      	mov	w10, #64
fffffe000753086c:      	dmb	oshst
fffffe0007530870:      	ldr	x9, [x26, #3024]
fffffe0007530874:      	str	w11, [x9, #140]
fffffe0007530878:      	dmb	oshst
fffffe000753087c:      	ldr	x9, [x26, #3024]
fffffe0007530880:      	str	w11, [x9, #128]
fffffe0007530884:      	dmb	oshst
fffffe0007530888:      	ldr	x9, [x26, #3024]
fffffe000753088c:      	str	w11, [x9, #9244]
fffffe0007530890:      	dmb	oshst
fffffe0007530894:      	ldr	x9, [x26, #3024]
fffffe0007530898:      	str	w11, [x9, #2132]
fffffe000753089c:      	dmb	oshst
fffffe00075308a0:      	ldr	x9, [x26, #3024]
fffffe00075308a4:      	str	wzr, [x9, #2048]
fffffe00075308a8:      	dmb	oshst
fffffe00075308ac:      	ldr	x9, [x26, #3024]
fffffe00075308b0:      	str	w10, [x9, #256]
fffffe00075308b4:      	dmb	oshst
fffffe00075308b8:      	ldr	x9, [x26, #3024]
fffffe00075308bc:      	str	w25, [x9, #136]
fffffe00075308c0:      	dmb	oshst
fffffe00075308c4:      	cbz	w21, 0xfffffe0007530a34 <_nord2_usb_enumerate+0x608>
fffffe00075308c8:      	adrp	x24, 0xfffffe0008532000 <_getattrlistbulk+0x34c>
fffffe00075308cc:      	adrp	x9, 0xfffffe0008532000 <_getattrlistbulk+0x350>
fffffe00075308d0:      	mov	w27, #65527
fffffe00075308d4:      	mov	w0, #30
fffffe00075308d8:      	movk	w27, #15355, lsl #16
fffffe00075308dc:      	ldr	x10, [x24, #3128]
fffffe00075308e0:      	strb	w8, [x9, #3136]
fffffe00075308e4:      	mov	w8, #1006632959
fffffe00075308e8:      	ldr	w25, [x10, #104]
fffffe00075308ec:      	dmb	oshld
fffffe00075308f0:      	ldr	x10, [x24, #3128]
fffffe00075308f4:      	and	w8, w25, w8
fffffe00075308f8:      	orr	w9, w8, #0x40000
fffffe00075308fc:      	str	w9, [x10, #104]
fffffe0007530900:      	orr	w10, w27, #0x40000
fffffe0007530904:      	dmb	oshst
fffffe0007530908:      	and	w9, w9, w10
fffffe000753090c:      	ldr	x10, [x24, #3128]
fffffe0007530910:      	str	w9, [x10, #104]
fffffe0007530914:      	mov	w9, #8
fffffe0007530918:      	dmb	oshst
fffffe000753091c:      	movk	w9, #4, lsl #16
fffffe0007530920:      	orr	w8, w8, w9
fffffe0007530924:      	ldr	x9, [x24, #3128]
fffffe0007530928:      	str	w8, [x9, #104]
fffffe000753092c:      	dmb	oshst
fffffe0007530930:      	bl	0xfffffe0007337470 <_delay>
fffffe0007530934:      	and	w8, w25, w27
fffffe0007530938:      	ldr	x9, [x24, #3128]
fffffe000753093c:      	orr	w8, w8, #0x8
fffffe0007530940:      	mov	w0, #800
fffffe0007530944:      	sub	x27, x29, #144
fffffe0007530948:      	adrp	x28, 0xfffffe0008532000 <_getattrlistbulk+0x3cc>
fffffe000753094c:      	str	w8, [x9, #104]
fffffe0007530950:      	mov	w8, #49923
fffffe0007530954:      	dmb	oshst
fffffe0007530958:      	movk	w8, #15169, lsl #16
fffffe000753095c:      	and	w8, w25, w8
fffffe0007530960:      	ldr	x9, [x24, #3128]
fffffe0007530964:      	mov	w25, #65537
fffffe0007530968:      	str	w8, [x9, #104]
fffffe000753096c:      	dmb	oshst
fffffe0007530970:      	ldr	x8, [x24, #3128]
fffffe0007530974:      	ldr	w8, [x8]
fffffe0007530978:      	dmb	oshld
fffffe000753097c:      	ldr	x9, [x24, #3128]
fffffe0007530980:      	orr	w8, w8, #0x20
fffffe0007530984:      	str	w8, [x9]
fffffe0007530988:      	dmb	oshst
fffffe000753098c:      	ldr	x8, [x24, #3128]
fffffe0007530990:      	ldr	w8, [x8, #20]
fffffe0007530994:      	dmb	oshld
fffffe0007530998:      	ldr	x9, [x24, #3128]
fffffe000753099c:      	and	w8, w8, #0xfffff7ff
fffffe00075309a0:      	str	w8, [x9, #20]
fffffe00075309a4:      	dmb	oshst
fffffe00075309a8:      	ldr	x8, [x24, #3128]
fffffe00075309ac:      	ldr	w8, [x8, #32]
fffffe00075309b0:      	dmb	oshld
fffffe00075309b4:      	ldr	x9, [x24, #3128]
fffffe00075309b8:      	and	w8, w8, #0xfffffcff
fffffe00075309bc:      	str	w8, [x9, #32]
fffffe00075309c0:      	mov	w9, #-4609
fffffe00075309c4:      	dmb	oshst
fffffe00075309c8:      	ldr	x8, [x24, #3128]
fffffe00075309cc:      	ldr	w8, [x8, #28]
fffffe00075309d0:      	dmb	oshld
fffffe00075309d4:      	and	w8, w8, w9
fffffe00075309d8:      	ldr	x9, [x24, #3128]
fffffe00075309dc:      	str	w8, [x9, #28]
fffffe00075309e0:      	dmb	oshst
fffffe00075309e4:      	ldr	x8, [x24, #3128]
fffffe00075309e8:      	ldr	w8, [x8, #24]
fffffe00075309ec:      	dmb	oshld
fffffe00075309f0:      	ldr	x9, [x24, #3128]
fffffe00075309f4:      	and	w8, w8, #0xff7fffff
fffffe00075309f8:      	orr	w8, w8, #0x100000
fffffe00075309fc:      	str	w8, [x9, #24]
fffffe0007530a00:      	mov	w9, #64969
fffffe0007530a04:      	dmb	oshst
fffffe0007530a08:      	movk	w9, #65534, lsl #16
fffffe0007530a0c:      	ldr	x8, [x24, #3128]
fffffe0007530a10:      	ldr	w8, [x8, #108]
fffffe0007530a14:      	dmb	oshld
fffffe0007530a18:      	and	w8, w8, w9
fffffe0007530a1c:      	mov	w9, #550
fffffe0007530a20:      	orr	w8, w8, w9
fffffe0007530a24:      	ldr	x9, [x24, #3128]
fffffe0007530a28:      	str	w8, [x9, #108]
fffffe0007530a2c:      	dmb	oshst
fffffe0007530a30:      	bl	0xfffffe0007337470 <_delay>
fffffe0007530a34:      	ldr	w8, [x23, #3040]
fffffe0007530a38:      	mov	w9, #97
fffffe0007530a3c:      	orr	w8, w8, w9
fffffe0007530a40:      	ldr	x9, [x26, #3024]
fffffe0007530a44:      	str	w8, [x9, #9220]
fffffe0007530a48:      	dmb	oshst
fffffe0007530a4c:      	ldr	x8, [x26, #3024]
fffffe0007530a50:      	ldr	w8, [x8, #9220]
fffffe0007530a54:      	dmb	oshld
fffffe0007530a58:      	str	x8, [sp, #64]
fffffe0007530a5c:      	cbz	w21, 0xfffffe0007530b04 <_nord2_usb_enumerate+0x6d8>
fffffe0007530a60:      	adrp	x8, 0xfffffe0008532000 <_readdirattr+0x7c>
fffffe0007530a64:      	ldr	x9, [x8, #3128]
fffffe0007530a68:      	ldr	w9, [x9, #104]
fffffe0007530a6c:      	str	x9, [sp, #56]
fffffe0007530a70:      	dmb	oshld
fffffe0007530a74:      	ldr	x9, [x8, #3128]
fffffe0007530a78:      	ldr	w9, [x9, #108]
fffffe0007530a7c:      	str	x9, [sp, #48]
fffffe0007530a80:      	dmb	oshld
fffffe0007530a84:      	ldr	x8, [x8, #3128]
fffffe0007530a88:      	ldr	w8, [x8, #24]
fffffe0007530a8c:      	str	x8, [sp, #40]
fffffe0007530a90:      	dmb	oshld
fffffe0007530a94:      	b	0xfffffe0007530b0c <_nord2_usb_enumerate+0x6e0>
fffffe0007530a98:      	adrp	x8, 0xfffffe0007c9c000 <_bsd_mbuf_cluster_reserve+0xb8>
fffffe0007530a9c:      	nop
fffffe0007530aa0:      	ldur	x9, [x29, #-112]
fffffe0007530aa4:      	ldr	x8, [x8, #3440]
fffffe0007530aa8:      	cmp	x8, x9
fffffe0007530aac:      	b.ne	0xfffffe0007531560 <_nord2_usb_enumerate+0x1134>
fffffe0007530ab0:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0xb70>
fffffe0007530ab4:      	add	x0, x0, #2739
fffffe0007530ab8:      	b	0xfffffe0007530554 <_nord2_usb_enumerate+0x128>
fffffe0007530abc:      	adrp	x8, 0xfffffe0007c9c000 <_bsd_mbuf_cluster_reserve+0xdc>
fffffe0007530ac0:      	nop
fffffe0007530ac4:      	ldur	x9, [x29, #-112]
fffffe0007530ac8:      	ldr	x8, [x8, #3440]
fffffe0007530acc:      	cmp	x8, x9
fffffe0007530ad0:      	b.ne	0xfffffe0007531560 <_nord2_usb_enumerate+0x1134>
fffffe0007530ad4:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0xb94>
fffffe0007530ad8:      	add	x0, x0, #2788
fffffe0007530adc:      	b	0xfffffe0007530554 <_nord2_usb_enumerate+0x128>
fffffe0007530ae0:      	adrp	x8, 0xfffffe0007c9c000 <_bsd_mbuf_cluster_reserve+0x100>
fffffe0007530ae4:      	nop
fffffe0007530ae8:      	ldur	x9, [x29, #-112]
fffffe0007530aec:      	ldr	x8, [x8, #3440]
fffffe0007530af0:      	cmp	x8, x9
fffffe0007530af4:      	b.ne	0xfffffe0007531560 <_nord2_usb_enumerate+0x1134>
fffffe0007530af8:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0xbb8>
fffffe0007530afc:      	add	x0, x0, #2827
fffffe0007530b00:      	b	0xfffffe0007530554 <_nord2_usb_enumerate+0x128>
fffffe0007530b04:      	stp	xzr, xzr, [sp, #48]
fffffe0007530b08:      	str	xzr, [sp, #40]
fffffe0007530b0c:      	mov	w8, #45
fffffe0007530b10:      	mrs	x9, CNTVCT_EL0
fffffe0007530b14:      	cbz	w19, 0xfffffe0007530b30 <_nord2_usb_enumerate+0x704>
fffffe0007530b18:      	mov	w10, #1
fffffe0007530b1c:      	adrp	x11, 0xfffffe0008532000 <_readdirattr+0x138>
fffffe0007530b20:      	add	x11, x11, #3016
fffffe0007530b24:      	stlr	w10, [x11]
fffffe0007530b28:      	ldr	x10, [sp, #88]
fffffe0007530b2c:      	msr	DAIF, x10
fffffe0007530b30:      	adrp	x10, 0xfffffe0008532000 <_readdirattr+0x14c>
fffffe0007530b34:      	madd	x8, x22, x8, x9
fffffe0007530b38:      	mov	x22, x28
fffffe0007530b3c:      	ldr	w11, [x10, #3012]
fffffe0007530b40:      	mrs	x10, CNTVCT_EL0
fffffe0007530b44:      	str	x8, [sp, #104]
fffffe0007530b48:      	ldr	x24, [sp, #88]
fffffe0007530b4c:      	cmp	w11, #23
fffffe0007530b50:      	cset	w11, eq
fffffe0007530b54:      	and	w8, w11, w19
fffffe0007530b58:      	tbnz	w8, #0, 0xfffffe0007530c58 <_nord2_usb_enumerate+0x82c>
fffffe0007530b5c:      	ldr	x8, [sp, #104]
fffffe0007530b60:      	cmp	x10, x8
fffffe0007530b64:      	b.lo	0xfffffe0007530c58 <_nord2_usb_enumerate+0x82c>
fffffe0007530b68:      	mov	x23, x22
fffffe0007530b6c:      	cbz	w19, 0xfffffe0007530b74 <_nord2_usb_enumerate+0x748>
fffffe0007530b70:      	msr	DAIFSet, #3
fffffe0007530b74:      	ldr	x8, [x26, #3024]
fffffe0007530b78:      	ldr	w19, [x8, #9220]
fffffe0007530b7c:      	dmb	oshld
fffffe0007530b80:      	ldr	x8, [x26, #3024]
fffffe0007530b84:      	ldr	w20, [x8, #2048]
fffffe0007530b88:      	dmb	oshld
fffffe0007530b8c:      	ldr	x8, [x26, #3024]
fffffe0007530b90:      	ldr	w21, [x8, #9312]
fffffe0007530b94:      	dmb	oshld
fffffe0007530b98:      	ldr	x8, [x26, #3024]
fffffe0007530b9c:      	ldr	w22, [x8, #256]
fffffe0007530ba0:      	dmb	oshld
fffffe0007530ba4:      	bl	0xfffffe000753160c <_nord2_usb_prepare_return>
fffffe0007530ba8:      	tbnz	w0, #31, 0xfffffe0007531564 <_nord2_usb_enumerate+0x1138>
fffffe0007530bac:      	ldr	x8, [sp, #88]
fffffe0007530bb0:      	msr	DAIF, x8
fffffe0007530bb4:      	ldr	x9, [sp, #40]
fffffe0007530bb8:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0xc78>
fffffe0007530bbc:      	add	x0, x0, #3108
fffffe0007530bc0:      	ldr	x8, [sp, #48]
fffffe0007530bc4:      	stp	x8, x9, [sp, #16]
fffffe0007530bc8:      	ldr	x9, [sp, #56]
fffffe0007530bcc:      	ldr	x8, [sp, #64]
fffffe0007530bd0:      	stp	x8, x9, [sp]
fffffe0007530bd4:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530bd8:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0xc98>
fffffe0007530bdc:      	add	x0, x0, #3180
fffffe0007530be0:      	stp	x21, x22, [sp, #16]
fffffe0007530be4:      	stp	x19, x20, [sp]
fffffe0007530be8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530bec:      	adrp	x9, 0xfffffe0008532000 <_readdirattr+0x208>
fffffe0007530bf0:      	adrp	x10, 0xfffffe0008532000 <_readdirattr+0x20c>
fffffe0007530bf4:      	adrp	x11, 0xfffffe0008532000 <_readdirattr+0x210>
fffffe0007530bf8:      	ldr	w8, [x23, #3096]
fffffe0007530bfc:      	adrp	x0, 0xfffffe000709b000 <_sweet_rpmh_vreg_enable+0xcbc>
fffffe0007530c00:      	add	x0, x0, #3240
fffffe0007530c04:      	ldr	w9, [x9, #3088]
fffffe0007530c08:      	ldr	w10, [x10, #3104]
fffffe0007530c0c:      	ldr	w11, [x11, #3112]
fffffe0007530c10:      	stp	x8, x9, [sp]
fffffe0007530c14:      	stp	x10, x11, [sp, #16]
fffffe0007530c18:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007530c1c:      	adrp	x8, 0xfffffe0007c9c000 <_bsd_scale_setup+0x80>
fffffe0007530c20:      	nop
fffffe0007530c24:      	ldur	x9, [x29, #-112]
fffffe0007530c28:      	ldr	x8, [x8, #3440]
fffffe0007530c2c:      	cmp	x8, x9
fffffe0007530c30:      	b.ne	0xfffffe0007531560 <_nord2_usb_enumerate+0x1134>
fffffe0007530c34:      	add	sp, sp, #1200
fffffe0007530c38:      	ldp	x29, x30, [sp, #96]
fffffe0007530c3c:      	ldp	x20, x19, [sp, #80]
fffffe0007530c40:      	ldp	x22, x21, [sp, #64]
fffffe0007530c44:      	ldp	x24, x23, [sp, #48]
fffffe0007530c48:      	ldp	x26, x25, [sp, #32]
fffffe0007530c4c:      	ldp	x28, x27, [sp, #16]
fffffe0007530c50:      	ldp	d9, d8, [sp], #112
fffffe0007530c54:      	ret
fffffe0007530c58:      	add	x8, sp, #120
fffffe0007530c5c:      	mov	w28, wzr
fffffe0007530c60:      	orr	x8, x8, #0x4
fffffe0007530c64:      	mov	w21, wzr
fffffe0007530c68:      	str	wzr, [sp, #100]
fffffe0007530c6c:      	str	x8, [sp, #72]
fffffe0007530c70:      	adrp	x8, 0xfffffe000700e000 <_sweet_rpmh_vreg_enable+0xafc>
fffffe0007530c74:      	ldr	d8, [x8, #2688]
fffffe0007530c78:      	cbz	w19, 0xfffffe0007530c80 <_nord2_usb_enumerate+0x854>
fffffe0007530c7c:      	msr	DAIFSet, #3
fffffe0007530c80:      	mov	w8, #6513
fffffe0007530c84:      	str	w8, [x20, #8]
fffffe0007530c88:      	ldr	x8, [x26, #3024]
fffffe0007530c8c:      	ldr	w8, [x8, #9244]
fffffe0007530c90:      	dmb	oshld
fffffe0007530c94:      	cbz	w8, 0xfffffe0007530cec <_nord2_usb_enumerate+0x8c0>
fffffe0007530c98:      	ldr	x9, [x26, #3024]
fffffe0007530c9c:      	str	w8, [x9, #9244]
fffffe0007530ca0:      	dmb	oshst
fffffe0007530ca4:      	tbz	w8, #2, 0xfffffe0007530cec <_nord2_usb_enumerate+0x8c0>
fffffe0007530ca8:      	ldr	w8, [x22, #3096]
fffffe0007530cac:      	mov	w21, wzr
fffffe0007530cb0:      	ldr	x9, [x26, #3024]
fffffe0007530cb4:      	add	w8, w8, #1
fffffe0007530cb8:      	str	w8, [x22, #3096]
fffffe0007530cbc:      	adrp	x8, 0xfffffe0008532000 <_readdirattr+0x2d8>
fffffe0007530cc0:      	strb	wzr, [x8, #3116]
fffffe0007530cc4:      	ldr	w8, [x9, #2048]
fffffe0007530cc8:      	dmb	oshld
fffffe0007530ccc:      	ldr	x9, [x26, #3024]
fffffe0007530cd0:      	and	w8, w8, #0x80ffffff
fffffe0007530cd4:      	str	w8, [x9, #2048]
fffffe0007530cd8:      	mov	w9, #64
fffffe0007530cdc:      	dmb	oshst
fffffe0007530ce0:      	ldr	x8, [x26, #3024]
fffffe0007530ce4:      	str	w9, [x8, #256]
fffffe0007530ce8:      	dmb	oshst
fffffe0007530cec:      	ldr	x8, [x26, #3024]
fffffe0007530cf0:      	ldr	w8, [x8, #128]
fffffe0007530cf4:      	dmb	oshld
fffffe0007530cf8:      	ands	w9, w8, w25
fffffe0007530cfc:      	b.eq	0xfffffe000753146c <_nord2_usb_enumerate+0x1040>
fffffe0007530d00:      	ldr	x10, [x26, #3024]
fffffe0007530d04:      	tst	w8, #0x10000
fffffe0007530d08:      	cset	w8, eq
fffffe0007530d0c:      	str	w9, [x10, #128]
fffffe0007530d10:      	dmb	oshst
fffffe0007530d14:      	ldr	x9, [x26, #3024]
fffffe0007530d18:      	ldr	w23, [x9, #256]
fffffe0007530d1c:      	dmb	oshld
fffffe0007530d20:      	tbnz	w23, #22, 0xfffffe0007530e3c <_nord2_usb_enumerate+0xa10>
fffffe0007530d24:      	and	w21, w8, w21
fffffe0007530d28:      	tbz	w23, #17, 0xfffffe0007530e68 <_nord2_usb_enumerate+0xa3c>
fffffe0007530d2c:      	ldr	x8, [x26, #3024]
fffffe0007530d30:      	ldr	w8, [x8, #264]
fffffe0007530d34:      	dmb	oshld
fffffe0007530d38:      	cmp	w8, #8
fffffe0007530d3c:      	b.ne	0xfffffe0007530eb8 <_nord2_usb_enumerate+0xa8c>
fffffe0007530d40:      	ldr	x8, [x26, #3024]
fffffe0007530d44:      	str	x28, [sp, #80]
fffffe0007530d48:      	stur	wzr, [x29, #-144]
fffffe0007530d4c:      	add	x0, sp, #120
fffffe0007530d50:      	sub	x1, x29, #144
fffffe0007530d54:      	mov	w2, #4
fffffe0007530d58:      	ldr	w8, [x8, #768]
fffffe0007530d5c:      	dmb	oshld
fffffe0007530d60:      	mov	w3, #8
fffffe0007530d64:      	mov	x22, x20
fffffe0007530d68:      	stur	w8, [x29, #-144]
fffffe0007530d6c:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe0007530d70:      	ldr	x8, [x26, #3024]
fffffe0007530d74:      	adrp	x10, 0xfffffe0008532000 <_readdirattr+0x390>
fffffe0007530d78:      	stur	wzr, [x29, #-144]
fffffe0007530d7c:      	add	x0, sp, #120
fffffe0007530d80:      	add	x1, sp, #128
fffffe0007530d84:      	add	x3, sp, #116
fffffe0007530d88:      	ldr	w8, [x8, #768]
fffffe0007530d8c:      	dmb	oshld
fffffe0007530d90:      	ldr	w9, [x10, #3088]
fffffe0007530d94:      	mov	w2, #1024
fffffe0007530d98:      	ldr	x11, [sp, #72]
fffffe0007530d9c:      	str	wzr, [sp, #116]
fffffe0007530da0:      	ldrb	w27, [sp, #120]
fffffe0007530da4:      	ldrb	w21, [sp, #121]
fffffe0007530da8:      	str	w8, [x11]
fffffe0007530dac:      	add	w8, w9, #1
fffffe0007530db0:      	ldrb	w28, [sp, #122]
fffffe0007530db4:      	ldrb	w24, [sp, #123]
fffffe0007530db8:      	ldrh	w20, [sp, #124]
fffffe0007530dbc:      	str	w8, [x10, #3088]
fffffe0007530dc0:      	ldrh	w25, [sp, #126]
fffffe0007530dc4:      	bl	0xfffffe00079b515c <_nwl_usb>
fffffe0007530dc8:      	tbnz	w0, #31, 0xfffffe0007530f0c <_nord2_usb_enumerate+0xae0>
fffffe0007530dcc:      	mov	w8, #-4390913
fffffe0007530dd0:      	sub	x27, x29, #144
fffffe0007530dd4:      	and	w8, w23, w8
fffffe0007530dd8:      	cmp	w0, #2
fffffe0007530ddc:      	b.eq	0xfffffe0007530ffc <_nord2_usb_enumerate+0xbd0>
fffffe0007530de0:      	ldr	x24, [sp, #88]
fffffe0007530de4:      	add	x12, sp, #128
fffffe0007530de8:      	mov	x20, x22
fffffe0007530dec:      	cbnz	w0, 0xfffffe00075310e8 <_nord2_usb_enumerate+0xcbc>
fffffe0007530df0:      	ldrh	w9, [sp, #126]
fffffe0007530df4:      	ldr	x28, [sp, #80]
fffffe0007530df8:      	cbz	w9, 0xfffffe00075311d0 <_nord2_usb_enumerate+0xda4>
fffffe0007530dfc:      	mov	w8, #-5439489
fffffe0007530e00:      	ldr	x9, [x26, #3024]
fffffe0007530e04:      	and	w8, w23, w8
fffffe0007530e08:      	orr	w8, w8, #0x20000
fffffe0007530e0c:      	str	w8, [x9, #256]
fffffe0007530e10:      	mov	w9, #-37945345
fffffe0007530e14:      	dmb	oshst
fffffe0007530e18:      	ldr	x8, [x26, #3024]
fffffe0007530e1c:      	ldr	w8, [x8, #256]
fffffe0007530e20:      	dmb	oshld
fffffe0007530e24:      	and	w8, w8, w9
fffffe0007530e28:      	ldr	x9, [x26, #3024]
fffffe0007530e2c:      	orr	w8, w8, #0x2000000
fffffe0007530e30:      	str	w8, [x9, #256]
fffffe0007530e34:      	dmb	oshst
fffffe0007530e38:      	b	0xfffffe00075311e4 <_nord2_usb_enumerate+0xdb8>
fffffe0007530e3c:      	mov	w8, #-37945345
fffffe0007530e40:      	ldr	x9, [x26, #3024]
fffffe0007530e44:      	and	w8, w23, w8
fffffe0007530e48:      	mov	w21, wzr
fffffe0007530e4c:      	orr	w8, w8, #0x400000
fffffe0007530e50:      	str	w8, [x9, #256]
fffffe0007530e54:      	dmb	oshst
fffffe0007530e58:      	ldr	x8, [x26, #3024]
fffffe0007530e5c:      	ldr	w23, [x8, #256]
fffffe0007530e60:      	dmb	oshld
fffffe0007530e64:      	tbnz	w23, #17, 0xfffffe0007530d2c <_nord2_usb_enumerate+0x900>
fffffe0007530e68:      	tst	w23, #0x40000
fffffe0007530e6c:      	cset	w8, eq
fffffe0007530e70:      	and	w8, w21, w8
fffffe0007530e74:      	cmp	w8, #1
fffffe0007530e78:      	b.ne	0xfffffe000753146c <_nord2_usb_enumerate+0x1040>
fffffe0007530e7c:      	ldr	w8, [sp, #116]
fffffe0007530e80:      	ldr	w13, [sp, #100]
fffffe0007530e84:      	subs	w8, w8, w28
fffffe0007530e88:      	cset	w9, hi
fffffe0007530e8c:      	orr	w9, w9, w13
fffffe0007530e90:      	tbz	w9, #0, 0xfffffe0007530ee8 <_nord2_usb_enumerate+0xabc>
fffffe0007530e94:      	cmp	w8, #64
fffffe0007530e98:      	mov	w9, #64
fffffe0007530e9c:      	csel	w8, w8, w9, lo
fffffe0007530ea0:      	add	x9, sp, #128
fffffe0007530ea4:      	add	x9, x9, w28, uxtw
fffffe0007530ea8:      	cmp	w8, #4
fffffe0007530eac:      	b.hs	0xfffffe0007530f70 <_nord2_usb_enumerate+0xb44>
fffffe0007530eb0:      	mov	w10, wzr
fffffe0007530eb4:      	b	0xfffffe0007530f94 <_nord2_usb_enumerate+0xb68>
fffffe0007530eb8:      	adrp	x11, 0xfffffe0008532000 <_setattrlist+0x3c>
fffffe0007530ebc:      	mov	w9, #-37945345
fffffe0007530ec0:      	and	w9, w23, w9
fffffe0007530ec4:      	mov	w12, #33685504
fffffe0007530ec8:      	ldr	x10, [x26, #3024]
fffffe0007530ecc:      	orr	w9, w9, w12
fffffe0007530ed0:      	ldr	w8, [x11, #3112]
fffffe0007530ed4:      	add	w8, w8, #1
fffffe0007530ed8:      	str	w8, [x11, #3112]
fffffe0007530edc:      	str	w9, [x10, #256]
fffffe0007530ee0:      	dmb	oshst
fffffe0007530ee4:      	b	0xfffffe0007530f04 <_nord2_usb_enumerate+0xad8>
fffffe0007530ee8:      	mov	w8, #-4915201
fffffe0007530eec:      	ldr	x9, [x26, #3024]
fffffe0007530ef0:      	and	w8, w23, w8
fffffe0007530ef4:      	str	wzr, [sp, #100]
fffffe0007530ef8:      	orr	w8, w8, #0x80000
fffffe0007530efc:      	str	w8, [x9, #256]
fffffe0007530f00:      	dmb	oshst
fffffe0007530f04:      	mov	w21, wzr
fffffe0007530f08:      	b	0xfffffe000753146c <_nord2_usb_enumerate+0x1040>
fffffe0007530f0c:      	mov	w0, w28
fffffe0007530f10:      	mov	w9, w20
fffffe0007530f14:      	bfi	w0, w24, #8, #8
fffffe0007530f18:      	cmp	w27, #192
fffffe0007530f1c:      	str	w25, [sp, #36]
fffffe0007530f20:      	b.ne	0xfffffe0007531010 <_nord2_usb_enumerate+0xbe4>
fffffe0007530f24:      	cmp	w21, #81
fffffe0007530f28:      	b.ne	0xfffffe0007531010 <_nord2_usb_enumerate+0xbe4>
fffffe0007530f2c:      	cbnz	w9, 0xfffffe0007531010 <_nord2_usb_enumerate+0xbe4>
fffffe0007530f30:      	ldr	w21, [sp, #36]
fffffe0007530f34:      	mov	w8, #1024
fffffe0007530f38:      	add	x1, sp, #128
fffffe0007530f3c:      	cmp	w21, #1024
fffffe0007530f40:      	csel	w2, w21, w8, lo
fffffe0007530f44:      	bl	0xfffffe000749e13c <_nord2_log_read>
fffffe0007530f48:      	adrp	x9, 0xfffffe0008532000 <_setattrlist+0xcc>
fffffe0007530f4c:      	ldr	x24, [sp, #88]
fffffe0007530f50:      	mov	w10, w21
fffffe0007530f54:      	mov	w25, w0
fffffe0007530f58:      	sub	x27, x29, #144
fffffe0007530f5c:      	mov	x20, x22
fffffe0007530f60:      	ldr	w8, [x9, #3104]
fffffe0007530f64:      	add	w8, w8, #1
fffffe0007530f68:      	str	w8, [x9, #3104]
fffffe0007530f6c:      	b	0xfffffe00075310cc <_nord2_usb_enumerate+0xca0>
fffffe0007530f70:      	mov	x10, xzr
fffffe0007530f74:      	ldr	w11, [x9, x10]
fffffe0007530f78:      	ldr	x12, [x26, #3024]
fffffe0007530f7c:      	str	w11, [x12, #768]
fffffe0007530f80:      	add	x11, x10, #8
fffffe0007530f84:      	dmb	oshst
fffffe0007530f88:      	add	x10, x10, #4
fffffe0007530f8c:      	cmp	x11, x8
fffffe0007530f90:      	b.ls	0xfffffe0007530f74 <_nord2_usb_enumerate+0xb48>
fffffe0007530f94:      	sub	w11, w8, w10
fffffe0007530f98:      	cmp	w11, #2
fffffe0007530f9c:      	b.lo	0xfffffe0007530fb0 <_nord2_usb_enumerate+0xb84>
fffffe0007530fa0:      	ldrh	w11, [x9, w10, uxtw]
fffffe0007530fa4:      	orr	w10, w10, #0x2
fffffe0007530fa8:      	ldr	x12, [x26, #3024]
fffffe0007530fac:      	strh	w11, [x12, #768]
fffffe0007530fb0:      	cmp	w10, w8
fffffe0007530fb4:      	b.hs	0xfffffe0007530fc4 <_nord2_usb_enumerate+0xb98>
fffffe0007530fb8:      	ldrb	w9, [x9, w10, uxtw]
fffffe0007530fbc:      	ldr	x10, [x26, #3024]
fffffe0007530fc0:      	strb	w9, [x10, #768]
fffffe0007530fc4:      	mov	w9, #-4653057
fffffe0007530fc8:      	dmb	oshst
fffffe0007530fcc:      	and	w9, w23, w9
fffffe0007530fd0:      	ldr	x10, [x26, #3024]
fffffe0007530fd4:      	orr	w9, w9, #0x40000
fffffe0007530fd8:      	cmp	w8, #0
fffffe0007530fdc:      	add	w28, w8, w28
fffffe0007530fe0:      	cset	w8, ne
fffffe0007530fe4:      	and	w13, w8, w13
fffffe0007530fe8:      	mov	w21, #1
fffffe0007530fec:      	str	w9, [x10, #256]
fffffe0007530ff0:      	dmb	oshst
fffffe0007530ff4:      	str	w13, [sp, #100]
fffffe0007530ff8:      	b	0xfffffe000753146c <_nord2_usb_enumerate+0x1040>
fffffe0007530ffc:      	ldr	x24, [sp, #88]
fffffe0007531000:      	adrp	x9, 0xfffffe0008532000 <_setattrlist_internal+0x24>
fffffe0007531004:      	mov	x20, x22
fffffe0007531008:      	mov	x22, x9
fffffe000753100c:      	b	0xfffffe000753144c <_nord2_usb_enumerate+0x1020>
fffffe0007531010:      	mov	x20, x22
fffffe0007531014:      	cmp	w27, #192
fffffe0007531018:      	b.ne	0xfffffe0007531084 <_nord2_usb_enumerate+0xc58>
fffffe000753101c:      	cmp	w21, #82
fffffe0007531020:      	b.ne	0xfffffe0007531084 <_nord2_usb_enumerate+0xc58>
fffffe0007531024:      	cbnz	w0, 0xfffffe0007531084 <_nord2_usb_enumerate+0xc58>
fffffe0007531028:      	cbnz	w9, 0xfffffe0007531084 <_nord2_usb_enumerate+0xc58>
fffffe000753102c:      	adrp	x8, 0xfffffe0008532000 <_setattrlist_internal+0x50>
fffffe0007531030:      	adrp	x9, 0xfffffe0008532000 <_setattrlist_internal+0x54>
fffffe0007531034:      	adrp	x10, 0xfffffe0008532000 <_setattrlist_internal+0x58>
fffffe0007531038:      	adrp	x11, 0xfffffe0008532000 <_setattrlist_internal+0x5c>
fffffe000753103c:      	sub	x27, x29, #144
fffffe0007531040:      	add	x0, sp, #128
fffffe0007531044:      	ldr	w8, [x8, #3088]
fffffe0007531048:      	sub	x1, x29, #144
fffffe000753104c:      	ldr	w9, [x9, #3096]
fffffe0007531050:      	mov	w2, #28
fffffe0007531054:      	ldr	w10, [x10, #3104]
fffffe0007531058:      	mov	w3, #1024
fffffe000753105c:      	ldr	w11, [x11, #3112]
fffffe0007531060:      	str	d8, [x27]
fffffe0007531064:      	stp	w8, w9, [x29, #-136]
fffffe0007531068:      	adrp	x8, 0xfffffe0008532000 <_setattrlist_internal+0x8c>
fffffe000753106c:      	mov	w25, #28
fffffe0007531070:      	stp	w10, w11, [x29, #-128]
fffffe0007531074:      	ldrb	w8, [x8, #3116]
fffffe0007531078:      	stur	w8, [x29, #-120]
fffffe000753107c:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe0007531080:      	b	0xfffffe00075310c4 <_nord2_usb_enumerate+0xc98>
fffffe0007531084:      	cmp	w27, #192
fffffe0007531088:      	b.ne	0xfffffe0007531204 <_nord2_usb_enumerate+0xdd8>
fffffe000753108c:      	cmp	w21, #83
fffffe0007531090:      	b.ne	0xfffffe0007531204 <_nord2_usb_enumerate+0xdd8>
fffffe0007531094:      	cbnz	w0, 0xfffffe0007531204 <_nord2_usb_enumerate+0xdd8>
fffffe0007531098:      	cbnz	w9, 0xfffffe0007531204 <_nord2_usb_enumerate+0xdd8>
fffffe000753109c:      	sub	x27, x29, #144
fffffe00075310a0:      	sub	x0, x29, #144
fffffe00075310a4:      	str	xzr, [x27]
fffffe00075310a8:      	bl	0xfffffe000749e1b4 <_nord2_log_stream_info>
fffffe00075310ac:      	add	x0, sp, #128
fffffe00075310b0:      	sub	x1, x29, #144
fffffe00075310b4:      	mov	w2, #8
fffffe00075310b8:      	mov	w3, #1024
fffffe00075310bc:      	mov	w25, #8
fffffe00075310c0:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00075310c4:      	ldr	x24, [sp, #88]
fffffe00075310c8:      	ldr	w10, [sp, #36]
fffffe00075310cc:      	cmp	w25, w10
fffffe00075310d0:      	csel	w8, w25, w10, lo
fffffe00075310d4:      	str	w8, [sp, #116]
fffffe00075310d8:      	cbz	w10, 0xfffffe0007531440 <_nord2_usb_enumerate+0x1014>
fffffe00075310dc:      	mov	w8, #-4390913
fffffe00075310e0:      	add	x12, sp, #128
fffffe00075310e4:      	and	w8, w23, w8
fffffe00075310e8:      	mov	w9, #1179648
fffffe00075310ec:      	mov	w10, #64
fffffe00075310f0:      	orr	w8, w8, w9
fffffe00075310f4:      	ldr	x9, [x26, #3024]
fffffe00075310f8:      	str	w8, [x9, #256]
fffffe00075310fc:      	dmb	oshst
fffffe0007531100:      	ldr	w9, [sp, #116]
fffffe0007531104:      	ldrh	w8, [sp, #126]
fffffe0007531108:      	tst	w9, #0x3f
fffffe000753110c:      	ccmp	w9, w8, #2, eq
fffffe0007531110:      	cset	w8, lo
fffffe0007531114:      	cmp	w9, #64
fffffe0007531118:      	csel	w28, w9, w10, lo
fffffe000753111c:      	cmp	w28, #4
fffffe0007531120:      	b.hs	0xfffffe0007531134 <_nord2_usb_enumerate+0xd08>
fffffe0007531124:      	mov	w9, wzr
fffffe0007531128:      	mov	w25, #65537
fffffe000753112c:      	adrp	x22, 0xfffffe0008532000 <_setattrlist_internal+0x150>
fffffe0007531130:      	b	0xfffffe0007531160 <_nord2_usb_enumerate+0xd34>
fffffe0007531134:      	mov	x9, xzr
fffffe0007531138:      	adrp	x22, 0xfffffe0008532000 <_setattrlist_internal+0x15c>
fffffe000753113c:      	ldr	w10, [x12, x9]
fffffe0007531140:      	ldr	x11, [x26, #3024]
fffffe0007531144:      	str	w10, [x11, #768]
fffffe0007531148:      	add	x10, x9, #8
fffffe000753114c:      	dmb	oshst
fffffe0007531150:      	add	x9, x9, #4
fffffe0007531154:      	cmp	x10, x28
fffffe0007531158:      	b.ls	0xfffffe000753113c <_nord2_usb_enumerate+0xd10>
fffffe000753115c:      	mov	w25, #65537
fffffe0007531160:      	sub	w10, w28, w9
fffffe0007531164:      	cmp	w10, #2
fffffe0007531168:      	b.lo	0xfffffe000753117c <_nord2_usb_enumerate+0xd50>
fffffe000753116c:      	ldrh	w10, [x12, w9, uxtw]
fffffe0007531170:      	orr	w9, w9, #0x2
fffffe0007531174:      	ldr	x11, [x26, #3024]
fffffe0007531178:      	strh	w10, [x11, #768]
fffffe000753117c:      	cmp	w9, w28
fffffe0007531180:      	b.hs	0xfffffe0007531190 <_nord2_usb_enumerate+0xd64>
fffffe0007531184:      	ldrb	w9, [x12, w9, uxtw]
fffffe0007531188:      	ldr	x10, [x26, #3024]
fffffe000753118c:      	strb	w9, [x10, #768]
fffffe0007531190:      	dmb	oshst
fffffe0007531194:      	mov	w11, #-4653057
fffffe0007531198:      	ldr	x9, [x26, #3024]
fffffe000753119c:      	cmp	w28, #0
fffffe00075311a0:      	mov	w21, #1
fffffe00075311a4:      	ldr	w9, [x9, #256]
fffffe00075311a8:      	dmb	oshld
fffffe00075311ac:      	ldr	x10, [x26, #3024]
fffffe00075311b0:      	and	w9, w9, w11
fffffe00075311b4:      	orr	w9, w9, #0x40000
fffffe00075311b8:      	str	w9, [x10, #256]
fffffe00075311bc:      	cset	w9, ne
fffffe00075311c0:      	dmb	oshst
fffffe00075311c4:      	and	w8, w9, w8
fffffe00075311c8:      	str	w8, [sp, #100]
fffffe00075311cc:      	b	0xfffffe000753146c <_nord2_usb_enumerate+0x1040>
fffffe00075311d0:      	mov	w9, #33685504
fffffe00075311d4:      	orr	w8, w8, w9
fffffe00075311d8:      	ldr	x9, [x26, #3024]
fffffe00075311dc:      	str	w8, [x9, #256]
fffffe00075311e0:      	dmb	oshst
fffffe00075311e4:      	adrp	x9, 0xfffffe0008532000 <_setattrlist_internal+0x208>
fffffe00075311e8:      	mov	w25, #65537
fffffe00075311ec:      	adrp	x22, 0xfffffe0008532000 <_setattrlist_internal+0x210>
fffffe00075311f0:      	mov	w21, wzr
fffffe00075311f4:      	ldr	w8, [x9, #3112]
fffffe00075311f8:      	add	w8, w8, #1
fffffe00075311fc:      	str	w8, [x9, #3112]
fffffe0007531200:      	b	0xfffffe000753146c <_nord2_usb_enumerate+0x1040>
fffffe0007531204:      	ldr	w10, [sp, #36]
fffffe0007531208:      	cmp	w27, #192
fffffe000753120c:      	b.ne	0xfffffe0007531248 <_nord2_usb_enumerate+0xe1c>
fffffe0007531210:      	cmp	w21, #84
fffffe0007531214:      	b.ne	0xfffffe0007531248 <_nord2_usb_enumerate+0xe1c>
fffffe0007531218:      	cmp	w10, #1024
fffffe000753121c:      	mov	w8, #1024
fffffe0007531220:      	bfi	w0, w9, #16, #16
fffffe0007531224:      	csel	w2, w10, w8, lo
fffffe0007531228:      	add	x1, sp, #128
fffffe000753122c:      	bl	0xfffffe000749e1d0 <_nord2_log_stream_read>
fffffe0007531230:      	adrp	x9, 0xfffffe0008532000 <_setattrlist_internal+0x254>
fffffe0007531234:      	ldr	w10, [sp, #36]
fffffe0007531238:      	ldr	x24, [sp, #88]
fffffe000753123c:      	mov	w25, w0
fffffe0007531240:      	sub	x27, x29, #144
fffffe0007531244:      	b	0xfffffe0007530f60 <_nord2_usb_enumerate+0xb34>
fffffe0007531248:      	cmp	w27, #128
fffffe000753124c:      	b.ne	0xfffffe0007531294 <_nord2_usb_enumerate+0xe68>
fffffe0007531250:      	cmp	w21, #6
fffffe0007531254:      	b.ne	0xfffffe0007531294 <_nord2_usb_enumerate+0xe68>
fffffe0007531258:      	lsr	w8, w0, #8
fffffe000753125c:      	and	w0, w0, #0xff
fffffe0007531260:      	cmp	w8, #3
fffffe0007531264:      	b.ne	0xfffffe00075312e8 <_nord2_usb_enumerate+0xebc>
fffffe0007531268:      	sub	x27, x29, #144
fffffe000753126c:      	ldr	x24, [sp, #88]
fffffe0007531270:      	cmp	w9, #1033
fffffe0007531274:      	b.eq	0xfffffe000753127c <_nord2_usb_enumerate+0xe50>
fffffe0007531278:      	cbnz	w9, 0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe000753127c:      	add	x1, sp, #128
fffffe0007531280:      	bl	0xfffffe00075317dc <_nusb_string>
fffffe0007531284:      	ldr	w10, [sp, #36]
fffffe0007531288:      	mov	w25, w0
fffffe000753128c:      	cbnz	w0, 0xfffffe00075310cc <_nord2_usb_enumerate+0xca0>
fffffe0007531290:      	b	0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe0007531294:      	cbnz	w27, 0xfffffe0007531304 <_nord2_usb_enumerate+0xed8>
fffffe0007531298:      	cmp	w21, #5
fffffe000753129c:      	b.ne	0xfffffe0007531304 <_nord2_usb_enumerate+0xed8>
fffffe00075312a0:      	cmp	w0, #127
fffffe00075312a4:      	b.hi	0xfffffe0007531304 <_nord2_usb_enumerate+0xed8>
fffffe00075312a8:      	cbnz	w9, 0xfffffe0007531304 <_nord2_usb_enumerate+0xed8>
fffffe00075312ac:      	cbnz	w10, 0xfffffe0007531304 <_nord2_usb_enumerate+0xed8>
fffffe00075312b0:      	ldr	x8, [x26, #3024]
fffffe00075312b4:      	sub	x27, x29, #144
fffffe00075312b8:      	adrp	x22, 0xfffffe0008532000 <_setattrlist_internal+0x2dc>
fffffe00075312bc:      	ldr	w8, [x8, #2048]
fffffe00075312c0:      	dmb	oshld
fffffe00075312c4:      	ldr	x9, [x26, #3024]
fffffe00075312c8:      	and	w8, w8, #0x80ffffff
fffffe00075312cc:      	orr	w8, w8, w0, lsl #24
fffffe00075312d0:      	str	w8, [x9, #2048]
fffffe00075312d4:      	mov	w8, #-4390913
fffffe00075312d8:      	dmb	oshst
fffffe00075312dc:      	and	w8, w23, w8
fffffe00075312e0:      	ldr	x24, [sp, #88]
fffffe00075312e4:      	b	0xfffffe000753144c <_nord2_usb_enumerate+0x1020>
fffffe00075312e8:      	orr	w9, w9, w0
fffffe00075312ec:      	sub	x27, x29, #144
fffffe00075312f0:      	ldr	x24, [sp, #88]
fffffe00075312f4:      	cbz	w9, 0xfffffe0007531340 <_nord2_usb_enumerate+0xf14>
fffffe00075312f8:      	mov	w8, #-4390913
fffffe00075312fc:      	and	w8, w23, w8
fffffe0007531300:      	b	0xfffffe0007530df0 <_nord2_usb_enumerate+0x9c4>
fffffe0007531304:      	adrp	x22, 0xfffffe0008532000 <_setattrlist_internal+0x328>
fffffe0007531308:      	cbnz	w27, 0xfffffe0007531374 <_nord2_usb_enumerate+0xf48>
fffffe000753130c:      	cmp	w21, #9
fffffe0007531310:      	b.ne	0xfffffe0007531374 <_nord2_usb_enumerate+0xf48>
fffffe0007531314:      	cmp	w0, #1
fffffe0007531318:      	b.hi	0xfffffe0007531374 <_nord2_usb_enumerate+0xf48>
fffffe000753131c:      	cbnz	w9, 0xfffffe0007531374 <_nord2_usb_enumerate+0xf48>
fffffe0007531320:      	cbnz	w10, 0xfffffe0007531374 <_nord2_usb_enumerate+0xf48>
fffffe0007531324:      	mov	w8, #-4390913
fffffe0007531328:      	adrp	x9, 0xfffffe0008532000 <_setattrlist_internal+0x34c>
fffffe000753132c:      	and	w8, w23, w8
fffffe0007531330:      	sub	x27, x29, #144
fffffe0007531334:      	ldr	x24, [sp, #88]
fffffe0007531338:      	strb	w28, [x9, #3116]
fffffe000753133c:      	b	0xfffffe000753144c <_nord2_usb_enumerate+0x1020>
fffffe0007531340:      	sub	w8, w8, #1
fffffe0007531344:      	cmp	w8, #6
fffffe0007531348:      	b.hi	0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe000753134c:      	adr	x11, #540
fffffe0007531350:      	nop
fffffe0007531354:      	adr	x9, #0
fffffe0007531358:      	ldrsw	x10, [x11, x8, lsl #2]
fffffe000753135c:      	add	x9, x9, x10
fffffe0007531360:      	br	x9
fffffe0007531364:      	add	x0, sp, #128
fffffe0007531368:      	adrp	x1, 0xfffffe000700e000 <_sweet_spmi_read+0x80>
fffffe000753136c:      	add	x1, x1, #2794
fffffe0007531370:      	b	0xfffffe00075314e0 <_nord2_usb_enumerate+0x10b4>
fffffe0007531374:      	cmp	w27, #128
fffffe0007531378:      	b.ne	0xfffffe00075313a8 <_nord2_usb_enumerate+0xf7c>
fffffe000753137c:      	cmp	w21, #8
fffffe0007531380:      	b.ne	0xfffffe00075313a8 <_nord2_usb_enumerate+0xf7c>
fffffe0007531384:      	cbnz	w0, 0xfffffe00075313a8 <_nord2_usb_enumerate+0xf7c>
fffffe0007531388:      	cbnz	w9, 0xfffffe00075313a8 <_nord2_usb_enumerate+0xf7c>
fffffe000753138c:      	cmp	w10, #1
fffffe0007531390:      	b.ne	0xfffffe00075313a8 <_nord2_usb_enumerate+0xf7c>
fffffe0007531394:      	adrp	x8, 0xfffffe0008532000 <_setattrlist_internal+0x3b8>
fffffe0007531398:      	ldrb	w9, [x8, #3116]
fffffe000753139c:      	mov	w8, #1
fffffe00075313a0:      	strb	w9, [sp, #128]
fffffe00075313a4:      	b	0xfffffe0007531548 <_nord2_usb_enumerate+0x111c>
fffffe00075313a8:      	mov	w8, #-126
fffffe00075313ac:      	cmp	w8, w27, sxtb
fffffe00075313b0:      	b.lt	0xfffffe00075313dc <_nord2_usb_enumerate+0xfb0>
fffffe00075313b4:      	cbnz	w21, 0xfffffe00075313dc <_nord2_usb_enumerate+0xfb0>
fffffe00075313b8:      	cbnz	w0, 0xfffffe00075313dc <_nord2_usb_enumerate+0xfb0>
fffffe00075313bc:      	cmp	w10, #2
fffffe00075313c0:      	b.ne	0xfffffe00075313dc <_nord2_usb_enumerate+0xfb0>
fffffe00075313c4:      	cmp	w27, #130
fffffe00075313c8:      	mov	w8, #128
fffffe00075313cc:      	ccmp	w9, w8, #0, eq
fffffe00075313d0:      	cset	w8, eq
fffffe00075313d4:      	cbz	w9, 0xfffffe0007531540 <_nord2_usb_enumerate+0x1114>
fffffe00075313d8:      	cbnz	w8, 0xfffffe0007531540 <_nord2_usb_enumerate+0x1114>
fffffe00075313dc:      	adrp	x8, 0xfffffe0008532000 <_setattrlist_internal+0x400>
fffffe00075313e0:      	cmp	w27, #129
fffffe00075313e4:      	ldrb	w8, [x8, #3116]
fffffe00075313e8:      	b.ne	0xfffffe0007531414 <_nord2_usb_enumerate+0xfe8>
fffffe00075313ec:      	cmp	w21, #10
fffffe00075313f0:      	b.ne	0xfffffe0007531414 <_nord2_usb_enumerate+0xfe8>
fffffe00075313f4:      	cbnz	w0, 0xfffffe0007531414 <_nord2_usb_enumerate+0xfe8>
fffffe00075313f8:      	cbnz	w9, 0xfffffe0007531414 <_nord2_usb_enumerate+0xfe8>
fffffe00075313fc:      	cmp	w10, #1
fffffe0007531400:      	b.ne	0xfffffe0007531414 <_nord2_usb_enumerate+0xfe8>
fffffe0007531404:      	cmp	w8, #1
fffffe0007531408:      	b.ne	0xfffffe0007531414 <_nord2_usb_enumerate+0xfe8>
fffffe000753140c:      	strb	wzr, [sp, #128]
fffffe0007531410:      	b	0xfffffe0007531548 <_nord2_usb_enumerate+0x111c>
fffffe0007531414:      	cmp	w27, #1
fffffe0007531418:      	sub	x27, x29, #144
fffffe000753141c:      	ldr	x24, [sp, #88]
fffffe0007531420:      	b.ne	0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe0007531424:      	cmp	w21, #11
fffffe0007531428:      	b.ne	0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe000753142c:      	cbnz	w0, 0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe0007531430:      	cbnz	w9, 0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe0007531434:      	cbnz	w10, 0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe0007531438:      	cmp	w8, #1
fffffe000753143c:      	b.ne	0xfffffe00075312f8 <_nord2_usb_enumerate+0xecc>
fffffe0007531440:      	mov	w8, #-4390913
fffffe0007531444:      	adrp	x22, 0xfffffe0008532000 <_setattrlist_internal+0x468>
fffffe0007531448:      	and	w8, w23, w8
fffffe000753144c:      	mov	w9, #655360
fffffe0007531450:      	mov	w21, wzr
fffffe0007531454:      	orr	w8, w8, w9
fffffe0007531458:      	ldr	x9, [x26, #3024]
fffffe000753145c:      	mov	w25, #65537
fffffe0007531460:      	str	w8, [x9, #256]
fffffe0007531464:      	dmb	oshst
fffffe0007531468:      	ldr	x28, [sp, #80]
fffffe000753146c:      	cbz	w19, 0xfffffe00075314a8 <_nord2_usb_enumerate+0x107c>
fffffe0007531470:      	mov	w1, #16960
fffffe0007531474:      	msr	DAIF, x24
fffffe0007531478:      	sub	x2, x29, #144
fffffe000753147c:      	mov	w0, #1
fffffe0007531480:      	movk	w1, #15, lsl #16
fffffe0007531484:      	str	xzr, [x27]
fffffe0007531488:      	bl	0xfffffe0007337480 <_clock_interval_to_deadline>
fffffe000753148c:      	ldr	x2, [x27]
fffffe0007531490:      	adrp	x0, 0xfffffe0008532000 <_setattrlist_internal+0x4b4>
fffffe0007531494:      	add	x0, x0, #3120
fffffe0007531498:      	mov	w1, wzr
fffffe000753149c:      	bl	0xfffffe000736ca44 <_assert_wait_deadline>
fffffe00075314a0:      	mov	x0, xzr
fffffe00075314a4:      	bl	0xfffffe000736d140 <_thread_block>
fffffe00075314a8:      	adrp	x8, 0xfffffe0008532000 <_setattrlist_internal+0x4cc>
fffffe00075314ac:      	ldr	w9, [x8, #3012]
fffffe00075314b0:      	mrs	x8, CNTVCT_EL0
fffffe00075314b4:      	cmp	w9, #23
fffffe00075314b8:      	cset	w9, eq
fffffe00075314bc:      	and	w9, w9, w19
fffffe00075314c0:      	tbnz	w9, #0, 0xfffffe0007530c78 <_nord2_usb_enumerate+0x84c>
fffffe00075314c4:      	ldr	x9, [sp, #104]
fffffe00075314c8:      	cmp	x8, x9
fffffe00075314cc:      	b.lo	0xfffffe0007530c78 <_nord2_usb_enumerate+0x84c>
fffffe00075314d0:      	b	0xfffffe0007530b68 <_nord2_usb_enumerate+0x73c>
fffffe00075314d4:      	add	x0, sp, #128
fffffe00075314d8:      	adrp	x1, 0xfffffe000700e000 <_nord2_usb_probe+0x18>
fffffe00075314dc:      	add	x1, x1, #2812
fffffe00075314e0:      	mov	w2, #18
fffffe00075314e4:      	mov	w3, #1024
fffffe00075314e8:      	mov	w25, #18
fffffe00075314ec:      	b	0xfffffe0007531508 <_nord2_usb_enumerate+0x10dc>
fffffe00075314f0:      	add	x0, sp, #128
fffffe00075314f4:      	adrp	x1, 0xfffffe000700e000 <_nord2_usb_probe+0x34>
fffffe00075314f8:      	add	x1, x1, #2830
fffffe00075314fc:      	mov	w2, #10
fffffe0007531500:      	mov	w3, #1024
fffffe0007531504:      	mov	w25, #10
fffffe0007531508:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe000753150c:      	ldr	w10, [sp, #36]
fffffe0007531510:      	b	0xfffffe00075310cc <_nord2_usb_enumerate+0xca0>
fffffe0007531514:      	add	x0, sp, #128
fffffe0007531518:      	adrp	x1, 0xfffffe000700e000 <_nord2_usb_probe+0x58>
fffffe000753151c:      	add	x1, x1, #2812
fffffe0007531520:      	mov	w2, #18
fffffe0007531524:      	mov	w3, #1024
fffffe0007531528:      	mov	w25, #18
fffffe000753152c:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe0007531530:      	ldr	w10, [sp, #36]
fffffe0007531534:      	mov	w8, #7
fffffe0007531538:      	strb	w8, [sp, #129]
fffffe000753153c:      	b	0xfffffe00075310cc <_nord2_usb_enumerate+0xca0>
fffffe0007531540:      	mov	w8, #2
fffffe0007531544:      	strh	wzr, [sp, #128]
fffffe0007531548:      	cmp	w8, w10
fffffe000753154c:      	ldr	x24, [sp, #88]
fffffe0007531550:      	csel	w8, w8, w10, lo
fffffe0007531554:      	sub	x27, x29, #144
fffffe0007531558:      	str	w8, [sp, #116]
fffffe000753155c:      	b	0xfffffe00075310dc <_nord2_usb_enumerate+0xcb0>
fffffe0007531560:      	bl	0xfffffe0007a09274 <___stack_chk_fail>
fffffe0007531564:      	bl	0xfffffe0007b55244 <_nord2_usb_enumerate.cold.1>
