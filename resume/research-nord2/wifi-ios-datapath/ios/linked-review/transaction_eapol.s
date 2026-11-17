
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4444 <_stc_eapol>:
fffffe00079b4444:      	sub	sp, sp, #400
fffffe00079b4448:      	stp	x28, x27, [sp, #304]
fffffe00079b444c:      	stp	x26, x25, [sp, #320]
fffffe00079b4450:      	stp	x24, x23, [sp, #336]
fffffe00079b4454:      	stp	x22, x21, [sp, #352]
fffffe00079b4458:      	stp	x20, x19, [sp, #368]
fffffe00079b445c:      	stp	x29, x30, [sp, #384]
fffffe00079b4460:      	add	x29, sp, #384
fffffe00079b4464:      	adrp	x8, 0xfffffe0007c98000 <_nwpc_hmac+0x2a4>
fffffe00079b4468:      	nop
fffffe00079b446c:      	ldr	x8, [x8, #3440]
fffffe00079b4470:      	stur	x8, [x29, #-96]
fffffe00079b4474:      	cbz	x0, 0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b4478:      	ldr	x8, [x0, #1272]
fffffe00079b447c:      	mov	x19, x0
fffffe00079b4480:      	mov	x23, x1
fffffe00079b4484:      	cmp	x8, x1
fffffe00079b4488:      	b.ne	0xfffffe00079b4724 <_stc_eapol+0x2e0>
fffffe00079b448c:      	ldr	w8, [x19]
fffffe00079b4490:      	cbz	w8, 0xfffffe00079b4724 <_stc_eapol+0x2e0>
fffffe00079b4494:      	ldr	w27, [x19, #4]
fffffe00079b4498:      	sub	w8, w27, #21
fffffe00079b449c:      	cmp	w8, #2
fffffe00079b44a0:      	b.lo	0xfffffe00079b4724 <_stc_eapol+0x2e0>
fffffe00079b44a4:      	mov	x0, x19
fffffe00079b44a8:      	mov	x1, x4
fffffe00079b44ac:      	mov	x21, x4
fffffe00079b44b0:      	mov	x20, x3
fffffe00079b44b4:      	mov	x22, x2
fffffe00079b44b8:      	bl	0xfffffe00079b2798 <_stc_tick>
fffffe00079b44bc:      	tbnz	w0, #31, 0xfffffe00079b4724 <_stc_eapol+0x2e0>
fffffe00079b44c0:      	ldr	w8, [x19, #4]
fffffe00079b44c4:      	mov	w0, wzr
fffffe00079b44c8:      	cmp	w8, w27
fffffe00079b44cc:      	b.ne	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b44d0:      	and	w8, w27, #0xfffffff8
fffffe00079b44d4:      	cmp	w8, #8
fffffe00079b44d8:      	b.ne	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b44dc:      	cbz	x22, 0xfffffe00079b45c0 <_stc_eapol+0x17c>
fffffe00079b44e0:      	sub	x8, x20, #1
fffffe00079b44e4:      	cmp	x8, #640
fffffe00079b44e8:      	b.hs	0xfffffe00079b45c0 <_stc_eapol+0x17c>
fffffe00079b44ec:      	cmp	w27, #15
fffffe00079b44f0:      	b.hi	0xfffffe00079b46d8 <_stc_eapol+0x294>
fffffe00079b44f4:      	mov	w8, #1
fffffe00079b44f8:      	mov	w9, #37376
fffffe00079b44fc:      	lsl	w8, w8, w27
fffffe00079b4500:      	tst	w8, w9
fffffe00079b4504:      	b.eq	0xfffffe00079b46d8 <_stc_eapol+0x294>
fffffe00079b4508:      	movi.2d	v0, #0000000000000000
fffffe00079b450c:      	add	x24, x19, #1464
fffffe00079b4510:      	mov	x3, sp
fffffe00079b4514:      	mov	x0, x24
fffffe00079b4518:      	mov	x1, x22
fffffe00079b451c:      	mov	x2, x20
fffffe00079b4520:      	ldr	w26, [x19, #1496]
fffffe00079b4524:      	str	xzr, [sp, #272]
fffffe00079b4528:      	stp	q0, q0, [sp, #16]
fffffe00079b452c:      	mov	x28, sp
fffffe00079b4530:      	stp	q0, q0, [sp, #48]
fffffe00079b4534:      	stp	q0, q0, [sp, #80]
fffffe00079b4538:      	stp	q0, q0, [sp, #112]
fffffe00079b453c:      	stp	q0, q0, [sp, #144]
fffffe00079b4540:      	stp	q0, q0, [sp, #176]
fffffe00079b4544:      	stp	q0, q0, [sp, #208]
fffffe00079b4548:      	stp	q0, q0, [sp, #240]
fffffe00079b454c:      	str	q0, [sp]
fffffe00079b4550:      	bl	0xfffffe00079b6544 <_nwp_receive>
fffffe00079b4554:      	mov	w25, w0
fffffe00079b4558:      	ldr	x1, [x19, #4088]
fffffe00079b455c:      	ldr	w6, [sp, #8]
fffffe00079b4560:      	mov	x0, x23
fffffe00079b4564:      	mov	w2, w26
fffffe00079b4568:      	mov	x3, x22
fffffe00079b456c:      	mov	x4, x20
fffffe00079b4570:      	mov	w5, w25
fffffe00079b4574:      	bl	0xfffffe00079b8870 <_nwpd_observe>
fffffe00079b4578:      	tbnz	w25, #31, 0xfffffe00079b46b4 <_stc_eapol+0x270>
fffffe00079b457c:      	sub	w8, w25, #1
fffffe00079b4580:      	cmp	w8, #1
fffffe00079b4584:      	b.hi	0xfffffe00079b461c <_stc_eapol+0x1d8>
fffffe00079b4588:      	ldr	w8, [x19, #1692]
fffffe00079b458c:      	cbz	w8, 0xfffffe00079b461c <_stc_eapol+0x1d8>
fffffe00079b4590:      	ldr	w8, [x19, #1240]
fffffe00079b4594:      	cbnz	w8, 0xfffffe00079b47d0 <_stc_eapol+0x38c>
fffffe00079b4598:      	mov	x8, #-10000
fffffe00079b459c:      	cmp	x21, x8
fffffe00079b45a0:      	b.lo	0xfffffe00079b47bc <_stc_eapol+0x378>
fffffe00079b45a4:      	mov	x8, xzr
fffffe00079b45a8:      	mov	x9, sp
fffffe00079b45ac:      	strb	wzr, [x9, x8]
fffffe00079b45b0:      	add	x8, x8, #1
fffffe00079b45b4:      	cmp	x8, #280
fffffe00079b45b8:      	b.ne	0xfffffe00079b45ac <_stc_eapol+0x168>
fffffe00079b45bc:      	b	0xfffffe00079b46c8 <_stc_eapol+0x284>
fffffe00079b45c0:      	cmp	x22, #0
fffffe00079b45c4:      	ldr	x1, [x19, #4088]
fffffe00079b45c8:      	cset	w3, ne
fffffe00079b45cc:      	ldr	w2, [x19, #1496]
fffffe00079b45d0:      	mov	x0, x23
fffffe00079b45d4:      	mov	x4, x20
fffffe00079b45d8:      	bl	0xfffffe00079b8a5c <_nwpd_gate_observe>
fffffe00079b45dc:      	adrp	x8, 0xfffffe0007c98000 <_nwpc_pmk+0x44>
fffffe00079b45e0:      	nop
fffffe00079b45e4:      	ldur	x9, [x29, #-96]
fffffe00079b45e8:      	ldr	x8, [x8, #3440]
fffffe00079b45ec:      	cmp	x8, x9
fffffe00079b45f0:      	b.ne	0xfffffe00079b481c <_stc_eapol+0x3d8>
fffffe00079b45f4:      	mov	x0, x19
fffffe00079b45f8:      	mov	w1, #2
fffffe00079b45fc:      	ldp	x29, x30, [sp, #384]
fffffe00079b4600:      	ldp	x20, x19, [sp, #368]
fffffe00079b4604:      	ldp	x22, x21, [sp, #352]
fffffe00079b4608:      	ldp	x24, x23, [sp, #336]
fffffe00079b460c:      	ldp	x26, x25, [sp, #320]
fffffe00079b4610:      	ldp	x28, x27, [sp, #304]
fffffe00079b4614:      	add	sp, sp, #400
fffffe00079b4618:      	b	0xfffffe00079b29e0 <_stop>
fffffe00079b461c:      	cmp	w25, #1
fffffe00079b4620:      	b.ne	0xfffffe00079b470c <_stc_eapol+0x2c8>
fffffe00079b4624:      	mov	x8, xzr
fffffe00079b4628:      	add	x9, x19, #4080
fffffe00079b462c:      	strb	wzr, [x9, x8]
fffffe00079b4630:      	add	x8, x8, #1
fffffe00079b4634:      	cmp	x8, #664
fffffe00079b4638:      	b.ne	0xfffffe00079b462c <_stc_eapol+0x1e8>
fffffe00079b463c:      	ldr	x8, [x19, #1280]
fffffe00079b4640:      	mov	w9, #10
fffffe00079b4644:      	cmn	x8, #1
fffffe00079b4648:      	str	w9, [x19, #4]
fffffe00079b464c:      	b.eq	0xfffffe00079b478c <_stc_eapol+0x348>
fffffe00079b4650:      	add	x8, x8, #1
fffffe00079b4654:      	ldr	x9, [x19, #1272]
fffffe00079b4658:      	mov	x20, sp
fffffe00079b465c:      	mov	w10, #6
fffffe00079b4660:      	add	x1, x20, #24
fffffe00079b4664:      	ldr	w2, [sp, #4]
fffffe00079b4668:      	str	x8, [x19, #1280]
fffffe00079b466c:      	str	x8, [x19, #4088]
fffffe00079b4670:      	mov	w8, #4104
fffffe00079b4674:      	str	x9, [x19, #4080]
fffffe00079b4678:      	ldr	x9, [sp, #16]
fffffe00079b467c:      	add	x0, x19, x8
fffffe00079b4680:      	str	w10, [x19, #4096]
fffffe00079b4684:      	str	w27, [x19, #572]
fffffe00079b4688:      	str	x9, [x19, #1312]
fffffe00079b468c:      	bl	0xfffffe0007bad570 <_memmove>
fffffe00079b4690:      	ldr	w9, [sp, #4]
fffffe00079b4694:      	mov	x8, xzr
fffffe00079b4698:      	str	w9, [x19, #4100]
fffffe00079b469c:      	strb	wzr, [x20, x8]
fffffe00079b46a0:      	add	x8, x8, #1
fffffe00079b46a4:      	cmp	x8, #280
fffffe00079b46a8:      	b.ne	0xfffffe00079b469c <_stc_eapol+0x258>
fffffe00079b46ac:      	mov	w0, #1
fffffe00079b46b0:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b46b4:      	mov	x8, xzr
fffffe00079b46b8:      	strb	wzr, [x28, x8]
fffffe00079b46bc:      	add	x8, x8, #1
fffffe00079b46c0:      	cmp	x8, #280
fffffe00079b46c4:      	b.ne	0xfffffe00079b46b8 <_stc_eapol+0x274>
fffffe00079b46c8:      	mov	x0, x19
fffffe00079b46cc:      	mov	w1, #2
fffffe00079b46d0:      	bl	0xfffffe00079b29e0 <_stop>
fffffe00079b46d4:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b46d8:      	ldr	w8, [x19, #576]
fffffe00079b46dc:      	cbz	w8, 0xfffffe00079b4760 <_stc_eapol+0x31c>
fffffe00079b46e0:      	ldr	w8, [x19, #580]
fffffe00079b46e4:      	cmp	x8, x20
fffffe00079b46e8:      	b.ne	0xfffffe00079b4784 <_stc_eapol+0x340>
fffffe00079b46ec:      	add	x0, x19, #584
fffffe00079b46f0:      	mov	x1, x22
fffffe00079b46f4:      	mov	x2, x20
fffffe00079b46f8:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b46fc:      	cmp	w0, #0
fffffe00079b4700:      	cset	w8, ne
fffffe00079b4704:      	lsl	w0, w8, #1
fffffe00079b4708:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b470c:      	mov	x8, xzr
fffffe00079b4710:      	mov	x9, sp
fffffe00079b4714:      	strb	wzr, [x9, x8]
fffffe00079b4718:      	add	x8, x8, #1
fffffe00079b471c:      	cmp	x8, #280
fffffe00079b4720:      	b.ne	0xfffffe00079b4714 <_stc_eapol+0x2d0>
fffffe00079b4724:      	mov	w0, wzr
fffffe00079b4728:      	adrp	x8, 0xfffffe0007c98000 <_nwpc_pmk+0x190>
fffffe00079b472c:      	nop
fffffe00079b4730:      	ldur	x9, [x29, #-96]
fffffe00079b4734:      	ldr	x8, [x8, #3440]
fffffe00079b4738:      	cmp	x8, x9
fffffe00079b473c:      	b.ne	0xfffffe00079b481c <_stc_eapol+0x3d8>
fffffe00079b4740:      	ldp	x29, x30, [sp, #384]
fffffe00079b4744:      	ldp	x20, x19, [sp, #368]
fffffe00079b4748:      	ldp	x22, x21, [sp, #352]
fffffe00079b474c:      	ldp	x24, x23, [sp, #336]
fffffe00079b4750:      	ldp	x26, x25, [sp, #320]
fffffe00079b4754:      	ldp	x28, x27, [sp, #304]
fffffe00079b4758:      	add	sp, sp, #400
fffffe00079b475c:      	ret
fffffe00079b4760:      	add	x0, x19, #584
fffffe00079b4764:      	mov	x1, x22
fffffe00079b4768:      	mov	x2, x20
fffffe00079b476c:      	mov	w3, #640
fffffe00079b4770:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b4774:      	mov	w0, #1
fffffe00079b4778:      	str	w20, [x19, #580]
fffffe00079b477c:      	str	w0, [x19, #576]
fffffe00079b4780:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b4784:      	mov	w0, #2
fffffe00079b4788:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b478c:      	mov	w8, #22
fffffe00079b4790:      	mov	x0, x24
fffffe00079b4794:      	str	w8, [x19, #4]
fffffe00079b4798:      	bl	0xfffffe00079b5e58 <_nwp_cancel>
fffffe00079b479c:      	mov	x8, xzr
fffffe00079b47a0:      	mov	x9, sp
fffffe00079b47a4:      	strb	wzr, [x9, x8]
fffffe00079b47a8:      	add	x8, x8, #1
fffffe00079b47ac:      	cmp	x8, #280
fffffe00079b47b0:      	b.ne	0xfffffe00079b47a4 <_stc_eapol+0x360>
fffffe00079b47b4:      	mov	w0, #-1
fffffe00079b47b8:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b47bc:      	mov	w9, #10000
fffffe00079b47c0:      	mov	w8, #1
fffffe00079b47c4:      	add	x9, x21, x9
fffffe00079b47c8:      	str	w8, [x19, #1240]
fffffe00079b47cc:      	str	x9, [x19, #1256]
fffffe00079b47d0:      	cmp	w25, #2
fffffe00079b47d4:      	b.ne	0xfffffe00079b480c <_stc_eapol+0x3c8>
fffffe00079b47d8:      	mov	x8, xzr
fffffe00079b47dc:      	ldr	w10, [x19, #2872]
fffffe00079b47e0:      	mov	x9, sp
fffffe00079b47e4:      	strb	w10, [x19, #1362]
fffffe00079b47e8:      	strb	wzr, [x9, x8]
fffffe00079b47ec:      	add	x8, x8, #1
fffffe00079b47f0:      	cmp	x8, #280
fffffe00079b47f4:      	b.ne	0xfffffe00079b47e8 <_stc_eapol+0x3a4>
fffffe00079b47f8:      	mov	x0, x19
fffffe00079b47fc:      	mov	w1, #1
fffffe00079b4800:      	mov	w2, wzr
fffffe00079b4804:      	bl	0xfffffe00079b3db0 <_command_key>
fffffe00079b4808:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b480c:      	mov	x1, sp
fffffe00079b4810:      	mov	x0, x19
fffffe00079b4814:      	bl	0xfffffe00079b4114 <_group_tx>
fffffe00079b4818:      	b	0xfffffe00079b4728 <_stc_eapol+0x2e4>
fffffe00079b481c:      	bl	0xfffffe0007a06354 <___stack_chk_fail>
