
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b4618 <_nsdd_summary>:
fffffe00079b4618:      	sub	sp, sp, #144
fffffe00079b461c:      	stp	x24, x23, [sp, #80]
fffffe00079b4620:      	stp	x22, x21, [sp, #96]
fffffe00079b4624:      	stp	x20, x19, [sp, #112]
fffffe00079b4628:      	stp	x29, x30, [sp, #128]
fffffe00079b462c:      	add	x29, sp, #128
fffffe00079b4630:      	cbz	x0, 0xfffffe00079b4a74 <_nsdd_summary+0x45c>
fffffe00079b4634:      	mov	x20, x0
fffffe00079b4638:      	ldr	w8, [x0, #460]
fffffe00079b463c:      	cbnz	w8, 0xfffffe00079b4a74 <_nsdd_summary+0x45c>
fffffe00079b4640:      	mov	x19, x1
fffffe00079b4644:      	mov	w22, #1
fffffe00079b4648:      	str	w22, [x20, #460]
fffffe00079b464c:      	cbz	x1, 0xfffffe00079b46c0 <_nsdd_summary+0xa8>
fffffe00079b4650:      	mov	x23, #-126
fffffe00079b4654:      	adrp	x24, 0xfffffe0008544000 <_stc_eapol+0x130>
fffffe00079b4658:      	add	x24, x24, #3532
fffffe00079b465c:      	adrp	x21, 0xfffffe0007134000 <_decode+0x914>
fffffe00079b4660:      	add	x21, x21, #1643
fffffe00079b4664:      	b	0xfffffe00079b4674 <_nsdd_summary+0x5c>
fffffe00079b4668:      	adds	x23, x23, #1
fffffe00079b466c:      	add	x24, x24, #72
fffffe00079b4670:      	b.hs	0xfffffe00079b46c0 <_nsdd_summary+0xa8>
fffffe00079b4674:      	ldur	x8, [x24, #-68]
fffffe00079b4678:      	cmp	x8, x19
fffffe00079b467c:      	b.ne	0xfffffe00079b4668 <_nsdd_summary+0x50>
fffffe00079b4680:      	ldr	w8, [x24]
fffffe00079b4684:      	cbnz	w8, 0xfffffe00079b4668 <_nsdd_summary+0x50>
fffffe00079b4688:      	ldp	w8, w9, [x24, #-56]
fffffe00079b468c:      	ldp	w11, w12, [x24, #-16]
fffffe00079b4690:      	ldp	w13, w15, [x24, #-8]
fffffe00079b4694:      	ldur	w10, [x24, #-24]
fffffe00079b4698:      	add	x14, x23, #127
fffffe00079b469c:      	mov	x0, x21
fffffe00079b46a0:      	str	w22, [x24]
fffffe00079b46a4:      	stp	x11, x12, [sp, #40]
fffffe00079b46a8:      	stp	x13, x15, [sp, #56]
fffffe00079b46ac:      	stp	x9, x10, [sp, #24]
fffffe00079b46b0:      	stp	x14, x8, [sp, #8]
fffffe00079b46b4:      	str	x19, [sp]
fffffe00079b46b8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b46bc:      	b	0xfffffe00079b4668 <_nsdd_summary+0x50>
fffffe00079b46c0:      	ldr	w8, [x20, #168]
fffffe00079b46c4:      	cbz	w8, 0xfffffe00079b46e0 <_nsdd_summary+0xc8>
fffffe00079b46c8:      	mov	w9, #1
fffffe00079b46cc:      	adrp	x0, 0xfffffe0007134000 <_decode+0x984>
fffffe00079b46d0:      	add	x0, x0, #2304
fffffe00079b46d4:      	str	x19, [sp]
fffffe00079b46d8:      	stp	x9, x8, [sp, #8]
fffffe00079b46dc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b46e0:      	ldr	w8, [x20, #172]
fffffe00079b46e4:      	cbz	w8, 0xfffffe00079b4700 <_nsdd_summary+0xe8>
fffffe00079b46e8:      	mov	w9, #2
fffffe00079b46ec:      	adrp	x0, 0xfffffe0007134000 <_decode+0x9a4>
fffffe00079b46f0:      	add	x0, x0, #2304
fffffe00079b46f4:      	str	x19, [sp]
fffffe00079b46f8:      	stp	x9, x8, [sp, #8]
fffffe00079b46fc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4700:      	ldr	w8, [x20, #176]
fffffe00079b4704:      	cbz	w8, 0xfffffe00079b4720 <_nsdd_summary+0x108>
fffffe00079b4708:      	mov	w9, #3
fffffe00079b470c:      	adrp	x0, 0xfffffe0007134000 <_decode+0x9c4>
fffffe00079b4710:      	add	x0, x0, #2304
fffffe00079b4714:      	str	x19, [sp]
fffffe00079b4718:      	stp	x9, x8, [sp, #8]
fffffe00079b471c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4720:      	ldr	w8, [x20, #180]
fffffe00079b4724:      	cbz	w8, 0xfffffe00079b4740 <_nsdd_summary+0x128>
fffffe00079b4728:      	mov	w9, #4
fffffe00079b472c:      	adrp	x0, 0xfffffe0007134000 <_decode+0x9e4>
fffffe00079b4730:      	add	x0, x0, #2304
fffffe00079b4734:      	str	x19, [sp]
fffffe00079b4738:      	stp	x9, x8, [sp, #8]
fffffe00079b473c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4740:      	ldr	w8, [x20, #184]
fffffe00079b4744:      	cbz	w8, 0xfffffe00079b4760 <_nsdd_summary+0x148>
fffffe00079b4748:      	mov	w9, #5
fffffe00079b474c:      	adrp	x0, 0xfffffe0007134000 <_decode+0xa04>
fffffe00079b4750:      	add	x0, x0, #2304
fffffe00079b4754:      	str	x19, [sp]
fffffe00079b4758:      	stp	x9, x8, [sp, #8]
fffffe00079b475c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4760:      	ldr	w8, [x20, #188]
fffffe00079b4764:      	cbz	w8, 0xfffffe00079b4780 <_nsdd_summary+0x168>
fffffe00079b4768:      	mov	w9, #6
fffffe00079b476c:      	adrp	x0, 0xfffffe0007134000 <_decode+0xa24>
fffffe00079b4770:      	add	x0, x0, #2304
fffffe00079b4774:      	str	x19, [sp]
fffffe00079b4778:      	stp	x9, x8, [sp, #8]
fffffe00079b477c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4780:      	ldr	w8, [x20, #192]
fffffe00079b4784:      	cbz	w8, 0xfffffe00079b47a0 <_nsdd_summary+0x188>
fffffe00079b4788:      	mov	w9, #7
fffffe00079b478c:      	adrp	x0, 0xfffffe0007134000 <_decode+0xa44>
fffffe00079b4790:      	add	x0, x0, #2304
fffffe00079b4794:      	str	x19, [sp]
fffffe00079b4798:      	stp	x9, x8, [sp, #8]
fffffe00079b479c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b47a0:      	ldr	w8, [x20, #196]
fffffe00079b47a4:      	cbz	w8, 0xfffffe00079b47c0 <_nsdd_summary+0x1a8>
fffffe00079b47a8:      	mov	w9, #8
fffffe00079b47ac:      	adrp	x0, 0xfffffe0007134000 <_decode+0xa64>
fffffe00079b47b0:      	add	x0, x0, #2304
fffffe00079b47b4:      	str	x19, [sp]
fffffe00079b47b8:      	stp	x9, x8, [sp, #8]
fffffe00079b47bc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b47c0:      	ldr	w8, [x20, #200]
fffffe00079b47c4:      	cbz	w8, 0xfffffe00079b47e0 <_nsdd_summary+0x1c8>
fffffe00079b47c8:      	mov	w9, #9
fffffe00079b47cc:      	adrp	x0, 0xfffffe0007134000 <_decode+0xa84>
fffffe00079b47d0:      	add	x0, x0, #2304
fffffe00079b47d4:      	str	x19, [sp]
fffffe00079b47d8:      	stp	x9, x8, [sp, #8]
fffffe00079b47dc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b47e0:      	ldr	w8, [x20, #204]
fffffe00079b47e4:      	cbz	w8, 0xfffffe00079b4800 <_nsdd_summary+0x1e8>
fffffe00079b47e8:      	mov	w9, #10
fffffe00079b47ec:      	adrp	x0, 0xfffffe0007134000 <_decode+0xaa4>
fffffe00079b47f0:      	add	x0, x0, #2304
fffffe00079b47f4:      	str	x19, [sp]
fffffe00079b47f8:      	stp	x9, x8, [sp, #8]
fffffe00079b47fc:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4800:      	ldr	w8, [x20, #208]
fffffe00079b4804:      	cbz	w8, 0xfffffe00079b4820 <_nsdd_summary+0x208>
fffffe00079b4808:      	mov	w9, #11
fffffe00079b480c:      	adrp	x0, 0xfffffe0007134000 <_decode+0xac4>
fffffe00079b4810:      	add	x0, x0, #2304
fffffe00079b4814:      	str	x19, [sp]
fffffe00079b4818:      	stp	x9, x8, [sp, #8]
fffffe00079b481c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4820:      	ldr	w8, [x20, #212]
fffffe00079b4824:      	cbz	w8, 0xfffffe00079b4840 <_nsdd_summary+0x228>
fffffe00079b4828:      	mov	w9, #12
fffffe00079b482c:      	adrp	x0, 0xfffffe0007134000 <_decode+0xae4>
fffffe00079b4830:      	add	x0, x0, #2304
fffffe00079b4834:      	str	x19, [sp]
fffffe00079b4838:      	stp	x9, x8, [sp, #8]
fffffe00079b483c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4840:      	ldr	w8, [x20, #216]
fffffe00079b4844:      	cbz	w8, 0xfffffe00079b4860 <_nsdd_summary+0x248>
fffffe00079b4848:      	mov	w9, #13
fffffe00079b484c:      	adrp	x0, 0xfffffe0007134000 <_decode+0xb04>
fffffe00079b4850:      	add	x0, x0, #2304
fffffe00079b4854:      	str	x19, [sp]
fffffe00079b4858:      	stp	x9, x8, [sp, #8]
fffffe00079b485c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4860:      	ldr	w8, [x20, #292]
fffffe00079b4864:      	adrp	x0, 0xfffffe0007134000 <_decode+0xb1c>
fffffe00079b4868:      	add	x0, x0, #2351
fffffe00079b486c:      	ldr	w9, [x20, #304]
fffffe00079b4870:      	ldr	w10, [x20, #308]
fffffe00079b4874:      	ldr	w11, [x20, #328]
fffffe00079b4878:      	ldr	w12, [x20, #340]
fffffe00079b487c:      	ldr	w13, [x20, #344]
fffffe00079b4880:      	stp	x8, x9, [sp, #8]
fffffe00079b4884:      	stp	x10, x11, [sp, #24]
fffffe00079b4888:      	str	x19, [sp]
fffffe00079b488c:      	stp	x12, x13, [sp, #40]
fffffe00079b4890:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4894:      	strb	wzr, [x20, #364]
fffffe00079b4898:      	adrp	x0, 0xfffffe0007134000 <_decode+0xb50>
fffffe00079b489c:      	add	x0, x0, #2445
fffffe00079b48a0:      	strb	wzr, [x20, #365]
fffffe00079b48a4:      	strb	wzr, [x20, #366]
fffffe00079b48a8:      	strb	wzr, [x20, #367]
fffffe00079b48ac:      	strb	wzr, [x20, #368]
fffffe00079b48b0:      	strb	wzr, [x20, #369]
fffffe00079b48b4:      	strb	wzr, [x20, #370]
fffffe00079b48b8:      	strb	wzr, [x20, #371]
fffffe00079b48bc:      	strb	wzr, [x20, #372]
fffffe00079b48c0:      	strb	wzr, [x20, #373]
fffffe00079b48c4:      	strb	wzr, [x20, #374]
fffffe00079b48c8:      	strb	wzr, [x20, #375]
fffffe00079b48cc:      	strb	wzr, [x20, #376]
fffffe00079b48d0:      	strb	wzr, [x20, #377]
fffffe00079b48d4:      	strb	wzr, [x20, #378]
fffffe00079b48d8:      	strb	wzr, [x20, #379]
fffffe00079b48dc:      	strb	wzr, [x20, #380]
fffffe00079b48e0:      	strb	wzr, [x20, #381]
fffffe00079b48e4:      	strb	wzr, [x20, #382]
fffffe00079b48e8:      	strb	wzr, [x20, #383]
fffffe00079b48ec:      	strb	wzr, [x20, #384]
fffffe00079b48f0:      	strb	wzr, [x20, #385]
fffffe00079b48f4:      	strb	wzr, [x20, #386]
fffffe00079b48f8:      	strb	wzr, [x20, #387]
fffffe00079b48fc:      	strb	wzr, [x20, #388]
fffffe00079b4900:      	strb	wzr, [x20, #389]
fffffe00079b4904:      	strb	wzr, [x20, #390]
fffffe00079b4908:      	strb	wzr, [x20, #391]
fffffe00079b490c:      	strb	wzr, [x20, #392]
fffffe00079b4910:      	strb	wzr, [x20, #393]
fffffe00079b4914:      	strb	wzr, [x20, #394]
fffffe00079b4918:      	strb	wzr, [x20, #395]
fffffe00079b491c:      	strb	wzr, [x20, #396]
fffffe00079b4920:      	strb	wzr, [x20, #397]
fffffe00079b4924:      	strb	wzr, [x20, #398]
fffffe00079b4928:      	strb	wzr, [x20, #399]
fffffe00079b492c:      	strb	wzr, [x20, #400]
fffffe00079b4930:      	strb	wzr, [x20, #401]
fffffe00079b4934:      	strb	wzr, [x20, #402]
fffffe00079b4938:      	strb	wzr, [x20, #403]
fffffe00079b493c:      	strb	wzr, [x20, #404]
fffffe00079b4940:      	strb	wzr, [x20, #405]
fffffe00079b4944:      	strb	wzr, [x20, #406]
fffffe00079b4948:      	strb	wzr, [x20, #407]
fffffe00079b494c:      	strb	wzr, [x20, #408]
fffffe00079b4950:      	strb	wzr, [x20, #409]
fffffe00079b4954:      	strb	wzr, [x20, #410]
fffffe00079b4958:      	strb	wzr, [x20, #411]
fffffe00079b495c:      	ldp	w8, w9, [x20]
fffffe00079b4960:      	ldp	w10, w11, [x20, #8]
fffffe00079b4964:      	ldp	w12, w13, [x20, #16]
fffffe00079b4968:      	ldp	w14, w15, [x20, #144]
fffffe00079b496c:      	ldr	w16, [x20, #152]
fffffe00079b4970:      	str	wzr, [x20, #280]
fffffe00079b4974:      	str	wzr, [x20, #276]
fffffe00079b4978:      	stp	x11, x12, [sp, #32]
fffffe00079b497c:      	stp	x15, x16, [sp, #64]
fffffe00079b4980:      	stp	x13, x14, [sp, #48]
fffffe00079b4984:      	stp	x9, x10, [sp, #16]
fffffe00079b4988:      	stp	x19, x8, [sp]
fffffe00079b498c:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4990:      	ldp	w8, w9, [x20, #24]
fffffe00079b4994:      	ldp	w10, w11, [x20, #32]
fffffe00079b4998:      	ldp	w12, w13, [x20, #40]
fffffe00079b499c:      	adrp	x21, 0xfffffe0007134000 <_decode+0xc54>
fffffe00079b49a0:      	add	x21, x21, #2575
fffffe00079b49a4:      	ldr	w14, [x20, #48]
fffffe00079b49a8:      	mov	w24, #1
fffffe00079b49ac:      	mov	x0, x21
fffffe00079b49b0:      	stp	x9, x10, [sp, #24]
fffffe00079b49b4:      	stp	x11, x12, [sp, #40]
fffffe00079b49b8:      	stp	x13, x14, [sp, #56]
fffffe00079b49bc:      	stp	x24, x8, [sp, #8]
fffffe00079b49c0:      	str	x19, [sp]
fffffe00079b49c4:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b49c8:      	ldp	w11, w8, [x20, #84]
fffffe00079b49cc:      	ldp	w9, w10, [x20, #100]
fffffe00079b49d0:      	mov	w23, #-1
fffffe00079b49d4:      	adrp	x22, 0xfffffe0007134000 <_decode+0xc8c>
fffffe00079b49d8:      	add	x22, x22, #2673
fffffe00079b49dc:      	ldp	w12, w13, [x20, #92]
fffffe00079b49e0:      	add	x8, x9, x8
fffffe00079b49e4:      	mov	x0, x22
fffffe00079b49e8:      	add	x8, x8, x10
fffffe00079b49ec:      	ldp	w9, w10, [x20, #76]
fffffe00079b49f0:      	cmp	x8, x23
fffffe00079b49f4:      	stp	x11, x12, [sp, #32]
fffffe00079b49f8:      	csel	x8, x8, x23, lo
fffffe00079b49fc:      	stp	x19, x24, [sp]
fffffe00079b4a00:      	stp	x9, x10, [sp, #16]
fffffe00079b4a04:      	stp	x13, x8, [sp, #48]
fffffe00079b4a08:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4a0c:      	ldp	w8, w9, [x20, #8]
fffffe00079b4a10:      	ldp	w10, w11, [x20, #52]
fffffe00079b4a14:      	ldp	w12, w13, [x20, #60]
fffffe00079b4a18:      	ldr	w14, [x20, #68]
fffffe00079b4a1c:      	mov	w24, #2
fffffe00079b4a20:      	mov	x0, x21
fffffe00079b4a24:      	str	x19, [sp]
fffffe00079b4a28:      	stp	x9, x10, [sp, #24]
fffffe00079b4a2c:      	stp	x13, x14, [sp, #56]
fffffe00079b4a30:      	stp	x11, x12, [sp, #40]
fffffe00079b4a34:      	stp	x24, x8, [sp, #8]
fffffe00079b4a38:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4a3c:      	ldp	w11, w8, [x20, #120]
fffffe00079b4a40:      	ldp	w9, w10, [x20, #136]
fffffe00079b4a44:      	ldp	w12, w13, [x20, #128]
fffffe00079b4a48:      	mov	x0, x22
fffffe00079b4a4c:      	add	x8, x9, x8
fffffe00079b4a50:      	add	x8, x8, x10
fffffe00079b4a54:      	ldp	w9, w10, [x20, #112]
fffffe00079b4a58:      	cmp	x8, x23
fffffe00079b4a5c:      	stp	x11, x12, [sp, #32]
fffffe00079b4a60:      	csel	x8, x8, x23, lo
fffffe00079b4a64:      	stp	x19, x24, [sp]
fffffe00079b4a68:      	stp	x9, x10, [sp, #16]
fffffe00079b4a6c:      	stp	x13, x8, [sp, #48]
fffffe00079b4a70:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b4a74:      	ldp	x29, x30, [sp, #128]
fffffe00079b4a78:      	ldp	x20, x19, [sp, #112]
fffffe00079b4a7c:      	ldp	x22, x21, [sp, #96]
fffffe00079b4a80:      	ldp	x24, x23, [sp, #80]
fffffe00079b4a84:      	add	sp, sp, #144
fffffe00079b4a88:      	ret
