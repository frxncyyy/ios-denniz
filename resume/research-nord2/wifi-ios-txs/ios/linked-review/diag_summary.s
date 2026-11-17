
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aa634 <_nstd_summary>:
fffffe00079aa634:      	movi.2d	v0, #0000000000000000
fffffe00079aa638:      	mov	w8, #8
fffffe00079aa63c:      	adrp	x9, 0xfffffe0008527000 <_nwp_ap_rsn+0x160>
fffffe00079aa640:      	add	x9, x9, #2708
fffffe00079aa644:      	stp	x1, x2, [x0]
fffffe00079aa648:      	adrp	x13, 0xfffffe0008527000 <_nwp_ap_rsn+0x16c>
fffffe00079aa64c:      	add	x13, x13, #2688
fffffe00079aa650:      	str	w8, [x0, #16]
fffffe00079aa654:      	stur	q0, [x0, #40]
fffffe00079aa658:      	ldr	w10, [x9, #8]
fffffe00079aa65c:      	stur	q0, [x0, #24]
fffffe00079aa660:      	ldr	w8, [x9]
fffffe00079aa664:      	ldr	w11, [x9, #28]
fffffe00079aa668:      	ldr	q0, [x13]
fffffe00079aa66c:      	str	w8, [x0, #40]
fffffe00079aa670:      	stp	w11, w10, [x0, #44]
fffffe00079aa674:      	ldr	w12, [x9, #132]
fffffe00079aa678:      	adds	w8, w8, w12
fffffe00079aa67c:      	csinv	w8, w8, wzr, lo
fffffe00079aa680:      	str	w8, [x0, #40]
fffffe00079aa684:      	ldr	w12, [x9, #160]
fffffe00079aa688:      	adds	w11, w11, w12
fffffe00079aa68c:      	csinv	w11, w11, wzr, lo
fffffe00079aa690:      	str	w11, [x0, #44]
fffffe00079aa694:      	ldr	w12, [x9, #140]
fffffe00079aa698:      	adds	w10, w10, w12
fffffe00079aa69c:      	csinv	w10, w10, wzr, lo
fffffe00079aa6a0:      	str	w10, [x0, #48]
fffffe00079aa6a4:      	ldr	w12, [x9, #264]
fffffe00079aa6a8:      	adds	w8, w8, w12
fffffe00079aa6ac:      	csinv	w8, w8, wzr, lo
fffffe00079aa6b0:      	str	w8, [x0, #40]
fffffe00079aa6b4:      	ldr	w12, [x9, #292]
fffffe00079aa6b8:      	adds	w11, w11, w12
fffffe00079aa6bc:      	csinv	w11, w11, wzr, lo
fffffe00079aa6c0:      	str	w11, [x0, #44]
fffffe00079aa6c4:      	ldr	w12, [x9, #272]
fffffe00079aa6c8:      	stur	q0, [x0, #20]
fffffe00079aa6cc:      	adds	w10, w10, w12
fffffe00079aa6d0:      	csinv	w10, w10, wzr, lo
fffffe00079aa6d4:      	str	w10, [x0, #48]
fffffe00079aa6d8:      	ldr	w12, [x9, #396]
fffffe00079aa6dc:      	adds	w8, w8, w12
fffffe00079aa6e0:      	csinv	w8, w8, wzr, lo
fffffe00079aa6e4:      	str	w8, [x0, #40]
fffffe00079aa6e8:      	ldr	w12, [x9, #424]
fffffe00079aa6ec:      	adds	w11, w11, w12
fffffe00079aa6f0:      	csinv	w11, w11, wzr, lo
fffffe00079aa6f4:      	str	w11, [x0, #44]
fffffe00079aa6f8:      	ldr	w12, [x9, #404]
fffffe00079aa6fc:      	adds	w10, w10, w12
fffffe00079aa700:      	ldr	w12, [x13, #16]
fffffe00079aa704:      	csinv	w10, w10, wzr, lo
fffffe00079aa708:      	str	w12, [x0, #36]
fffffe00079aa70c:      	str	w10, [x0, #48]
fffffe00079aa710:      	ldr	w12, [x9, #528]
fffffe00079aa714:      	adds	w8, w8, w12
fffffe00079aa718:      	csinv	w8, w8, wzr, lo
fffffe00079aa71c:      	str	w8, [x0, #40]
fffffe00079aa720:      	ldr	w8, [x9, #556]
fffffe00079aa724:      	adds	w8, w11, w8
fffffe00079aa728:      	csinv	w8, w8, wzr, lo
fffffe00079aa72c:      	str	w8, [x0, #44]
fffffe00079aa730:      	ldr	w8, [x9, #536]
fffffe00079aa734:      	adds	w8, w10, w8
fffffe00079aa738:      	csinv	w8, w8, wzr, lo
fffffe00079aa73c:      	str	w8, [x0, #48]
fffffe00079aa740:      	ret
