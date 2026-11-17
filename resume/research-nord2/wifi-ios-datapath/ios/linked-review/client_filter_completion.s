
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007aed40c <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)>:
fffffe0007aed40c:      	sub	sp, sp, #224
fffffe0007aed410:      	stp	x28, x27, [sp, #128]
fffffe0007aed414:      	stp	x26, x25, [sp, #144]
fffffe0007aed418:      	stp	x24, x23, [sp, #160]
fffffe0007aed41c:      	stp	x22, x21, [sp, #176]
fffffe0007aed420:      	stp	x20, x19, [sp, #192]
fffffe0007aed424:      	stp	x29, x30, [sp, #208]
fffffe0007aed428:      	add	x29, sp, #208
fffffe0007aed42c:      	mov	x19, x0
fffffe0007aed430:      	ldr	x0, [x0, #224]
fffffe0007aed434:      	mov	w21, w3
fffffe0007aed438:      	mov	x20, x2
fffffe0007aed43c:      	mov	x22, x1
fffffe0007aed440:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007aed444:      	movi.2d	v0, #0000000000000000
fffffe0007aed448:      	ldr	w8, [x19, #256]
fffffe0007aed44c:      	cmp	w8, #3
fffffe0007aed450:      	stp	q0, q0, [sp, #80]
fffffe0007aed454:      	b.ne	0xfffffe0007aed4bc <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xb0>
fffffe0007aed458:      	ldr	x8, [x19, #360]
fffffe0007aed45c:      	cmp	x8, x22
fffffe0007aed460:      	b.ne	0xfffffe0007aed4bc <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xb0>
fffffe0007aed464:      	ldr	x8, [x19, #368]
fffffe0007aed468:      	cmp	x8, x20
fffffe0007aed46c:      	b.ne	0xfffffe0007aed4bc <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xb0>
fffffe0007aed470:      	ldr	x8, [x19, #376]
fffffe0007aed474:      	ldr	x9, [x19, #352]
fffffe0007aed478:      	cmp	x8, x9
fffffe0007aed47c:      	b.ne	0xfffffe0007aed4bc <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xb0>
fffffe0007aed480:      	mov	w24, #702
fffffe0007aed484:      	movk	w24, #57344, lsl #16
fffffe0007aed488:      	cbz	w21, 0xfffffe0007aed5c8 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x1bc>
fffffe0007aed48c:      	add	x0, x19, #920
fffffe0007aed490:      	add	x1, x19, #960
fffffe0007aed494:      	mov	x2, xzr
fffffe0007aed498:      	bl	0xfffffe00079a4be8 <_nils_finish>
fffffe0007aed49c:      	add	w8, w24, #26
fffffe0007aed4a0:      	mov	w1, #5
fffffe0007aed4a4:      	mov	x0, x19
fffffe0007aed4a8:      	str	w8, [x19, #1028]
fffffe0007aed4ac:      	bl	0xfffffe0007aed644 <NiwlLifecycleClient::eventLocked(unsigned int)>
fffffe0007aed4b0:      	tbz	w0, #0, 0xfffffe0007aed5b4 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x1a8>
fffffe0007aed4b4:      	mov	w23, #1
fffffe0007aed4b8:      	b	0xfffffe0007aed4d8 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xcc>
fffffe0007aed4bc:      	ldr	w8, [x19, #940]
fffffe0007aed4c0:      	cbz	w8, 0xfffffe0007aed4d4 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xc8>
fffffe0007aed4c4:      	add	x0, x19, #920
fffffe0007aed4c8:      	add	x1, x19, #960
fffffe0007aed4cc:      	mov	x2, xzr
fffffe0007aed4d0:      	bl	0xfffffe00079a4be8 <_nils_finish>
fffffe0007aed4d4:      	mov	w23, wzr
fffffe0007aed4d8:      	ldr	w8, [x19, #404]
fffffe0007aed4dc:      	add	w9, w8, #1
fffffe0007aed4e0:      	cmp	w8, #63
fffffe0007aed4e4:      	str	w9, [x19, #404]
fffffe0007aed4e8:      	b.hi	0xfffffe0007aed564 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x158>
fffffe0007aed4ec:      	ldr	w24, [x19, #1028]
fffffe0007aed4f0:      	cbz	w23, 0xfffffe0007aed504 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xf8>
fffffe0007aed4f4:      	ldr	x8, [x19, #1016]
fffffe0007aed4f8:      	ldp	w26, w27, [x8, #20]
fffffe0007aed4fc:      	ldr	w25, [x8, #8]
fffffe0007aed500:      	b	0xfffffe0007aed510 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x104>
fffffe0007aed504:      	mov	w26, wzr
fffffe0007aed508:      	mov	w25, wzr
fffffe0007aed50c:      	mov	w27, wzr
fffffe0007aed510:      	sub	x0, x29, #88
fffffe0007aed514:      	stp	xzr, xzr, [x29, #-96]
fffffe0007aed518:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe0007aed51c:      	ldur	x0, [x29, #-88]
fffffe0007aed520:      	sub	x1, x29, #96
fffffe0007aed524:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe0007aed528:      	mov	x9, #13531
fffffe0007aed52c:      	ldur	x8, [x29, #-96]
fffffe0007aed530:      	movk	x9, #55222, lsl #16
fffffe0007aed534:      	adrp	x0, 0xfffffe0007155000 <NiwlLifecycleClient::initWithTask(task*, void*, unsigned int, OSDictionary*)+0x224>
fffffe0007aed538:      	add	x0, x0, #677
fffffe0007aed53c:      	movk	x9, #56962, lsl #32
fffffe0007aed540:      	movk	x9, #17179, lsl #48
fffffe0007aed544:      	stp	x25, x26, [sp, #40]
fffffe0007aed548:      	stp	x23, x24, [sp, #24]
fffffe0007aed54c:      	umulh	x8, x8, x9
fffffe0007aed550:      	stp	x20, x21, [sp, #8]
fffffe0007aed554:      	str	x22, [sp]
fffffe0007aed558:      	lsr	x8, x8, #18
fffffe0007aed55c:      	stp	x27, x8, [sp, #56]
fffffe0007aed560:      	bl	0xfffffe0007361ae4 <_printf>
fffffe0007aed564:      	ldr	x8, [x19, #368]
fffffe0007aed568:      	cmp	x8, x20
fffffe0007aed56c:      	b.ne	0xfffffe0007aed574 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x168>
fffffe0007aed570:      	str	xzr, [x19, #368]
fffffe0007aed574:      	ldr	x0, [x19, #224]
fffffe0007aed578:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007aed57c:      	mov	x0, x20
fffffe0007aed580:      	bl	0xfffffe000751203c <_nord2_wifi_scan_release>
fffffe0007aed584:      	ldr	x8, [x19]
fffffe0007aed588:      	mov	x0, x19
fffffe0007aed58c:      	ldr	x8, [x8, #40]
fffffe0007aed590:      	blr	x8
fffffe0007aed594:      	ldp	x29, x30, [sp, #208]
fffffe0007aed598:      	ldp	x20, x19, [sp, #192]
fffffe0007aed59c:      	ldp	x22, x21, [sp, #176]
fffffe0007aed5a0:      	ldp	x24, x23, [sp, #160]
fffffe0007aed5a4:      	ldp	x26, x25, [sp, #144]
fffffe0007aed5a8:      	ldp	x28, x27, [sp, #128]
fffffe0007aed5ac:      	add	sp, sp, #224
fffffe0007aed5b0:      	ret
fffffe0007aed5b4:      	add	x0, x19, #920
fffffe0007aed5b8:      	bl	0xfffffe00079a4cd4 <_nils_close>
fffffe0007aed5bc:      	mov	w23, #1
fffffe0007aed5c0:      	str	w24, [x19, #1028]
fffffe0007aed5c4:      	b	0xfffffe0007aed4d8 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0xcc>
fffffe0007aed5c8:      	add	x1, sp, #80
fffffe0007aed5cc:      	mov	x0, x20
fffffe0007aed5d0:      	bl	0xfffffe0007511f20 <_nord2_wifi_scan_view>
fffffe0007aed5d4:      	cbnz	w0, 0xfffffe0007aed48c <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x80>
fffffe0007aed5d8:      	ldr	x8, [sp, #96]
fffffe0007aed5dc:      	cmp	x8, x22
fffffe0007aed5e0:      	b.ne	0xfffffe0007aed48c <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x80>
fffffe0007aed5e4:      	sub	x0, x29, #88
fffffe0007aed5e8:      	stur	xzr, [x29, #-88]
fffffe0007aed5ec:      	bl	0xfffffe00079a57fc <_niwi_rate_map>
fffffe0007aed5f0:      	ldp	x1, x2, [sp, #80]
fffffe0007aed5f4:      	add	x23, x19, #960
fffffe0007aed5f8:      	mov	x4, x0
fffffe0007aed5fc:      	ldur	x5, [x29, #-88]
fffffe0007aed600:      	mov	x0, x23
fffffe0007aed604:      	ldr	x6, [x19, #1016]
fffffe0007aed608:      	mov	x3, x2
fffffe0007aed60c:      	bl	0xfffffe00079a4b70 <_nils_prepare>
fffffe0007aed610:      	cbnz	w0, 0xfffffe0007aed48c <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x80>
fffffe0007aed614:      	ldr	x0, [x19, #1016]
fffffe0007aed618:      	ldr	w1, [x19, #384]
fffffe0007aed61c:      	bl	0xfffffe00079a7084 <_niwa_snapshot_bss_filter>
fffffe0007aed620:      	cbnz	w0, 0xfffffe0007aed48c <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x80>
fffffe0007aed624:      	add	x0, x19, #920
fffffe0007aed628:      	ldr	x2, [x19, #1016]
fffffe0007aed62c:      	mov	x1, x23
fffffe0007aed630:      	bl	0xfffffe00079a4be8 <_nils_finish>
fffffe0007aed634:      	cbnz	w0, 0xfffffe0007aed48c <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x80>
fffffe0007aed638:      	mov	w8, wzr
fffffe0007aed63c:      	mov	w1, wzr
fffffe0007aed640:      	b	0xfffffe0007aed4a4 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x98>
