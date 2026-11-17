
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ae7e8 <_nst_step>:
fffffe00079ae7e8:      	sub	sp, sp, #160
fffffe00079ae7ec:      	stp	x28, x27, [sp, #64]
fffffe00079ae7f0:      	stp	x26, x25, [sp, #80]
fffffe00079ae7f4:      	stp	x24, x23, [sp, #96]
fffffe00079ae7f8:      	stp	x22, x21, [sp, #112]
fffffe00079ae7fc:      	stp	x20, x19, [sp, #128]
fffffe00079ae800:      	stp	x29, x30, [sp, #144]
fffffe00079ae804:      	add	x29, sp, #144
fffffe00079ae808:      	cbz	x0, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae80c:      	mov	x19, x0
fffffe00079ae810:      	ldr	w8, [x0, #28]
fffffe00079ae814:      	cbz	w8, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae818:      	ldr	x8, [x19]
fffffe00079ae81c:      	cbz	x8, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae820:      	ldr	x9, [x19, #8]
fffffe00079ae824:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae828:      	ldr	x9, [x19, #16]
fffffe00079ae82c:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae830:      	ldr	w9, [x19, #44]
fffffe00079ae834:      	cbnz	w9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae838:      	ldr	w9, [x8, #12]
fffffe00079ae83c:      	cbnz	w9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae840:      	ldr	w8, [x8, #1884]
fffffe00079ae844:      	cbnz	w8, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae848:      	ldr	w8, [x19, #36]
fffffe00079ae84c:      	cbz	w8, 0xfffffe00079ae874 <_nst_step+0x8c>
fffffe00079ae850:      	mov	w0, #-1
fffffe00079ae854:      	ldp	x29, x30, [sp, #144]
fffffe00079ae858:      	ldp	x20, x19, [sp, #128]
fffffe00079ae85c:      	ldp	x22, x21, [sp, #112]
fffffe00079ae860:      	ldp	x24, x23, [sp, #96]
fffffe00079ae864:      	ldp	x26, x25, [sp, #80]
fffffe00079ae868:      	ldp	x28, x27, [sp, #64]
fffffe00079ae86c:      	add	sp, sp, #160
fffffe00079ae870:      	ret
fffffe00079ae874:      	mov	x23, x1
fffffe00079ae878:      	mov	w0, #-1
fffffe00079ae87c:      	cbz	x1, 0xfffffe00079ae854 <_nst_step+0x6c>
fffffe00079ae880:      	ldr	w8, [x19, #152]
fffffe00079ae884:      	cbnz	w8, 0xfffffe00079ae854 <_nst_step+0x6c>
fffffe00079ae888:      	ldr	x8, [x23]
fffffe00079ae88c:      	cbz	x8, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae890:      	ldr	x9, [x23, #8]
fffffe00079ae894:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae898:      	ldr	x9, [x8]
fffffe00079ae89c:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae8a0:      	ldr	x9, [x8, #8]
fffffe00079ae8a4:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae8a8:      	ldr	x9, [x8, #16]
fffffe00079ae8ac:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae8b0:      	ldr	x9, [x8, #24]
fffffe00079ae8b4:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae8b8:      	ldr	x9, [x8, #32]
fffffe00079ae8bc:      	cbz	x9, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae8c0:      	mov	w24, w3
fffffe00079ae8c4:      	mov	w0, #-1
fffffe00079ae8c8:      	sub	w9, w3, #17
fffffe00079ae8cc:      	cmn	w9, #16
fffffe00079ae8d0:      	b.lo	0xfffffe00079ae854 <_nst_step+0x6c>
fffffe00079ae8d4:      	ldr	x8, [x8, #40]
fffffe00079ae8d8:      	cbz	x8, 0xfffffe00079ae854 <_nst_step+0x6c>
fffffe00079ae8dc:      	movi.2s	v0, #1
fffffe00079ae8e0:      	mov	x0, x19
fffffe00079ae8e4:      	mov	x1, x23
fffffe00079ae8e8:      	mov	x20, x5
fffffe00079ae8ec:      	mov	x21, x4
fffffe00079ae8f0:      	mov	x22, x2
fffffe00079ae8f4:      	str	wzr, [x19, #40]
fffffe00079ae8f8:      	str	d0, [x19, #32]
fffffe00079ae8fc:      	bl	0xfffffe00079aeab8 <_before>
fffffe00079ae900:      	cbnz	w0, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae904:      	mov	x0, x19
fffffe00079ae908:      	mov	x1, x23
fffffe00079ae90c:      	mov	x2, x22
fffffe00079ae910:      	bl	0xfffffe00079aebd0 <_coherent>
fffffe00079ae914:      	cbnz	w0, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae918:      	mov	x0, x19
fffffe00079ae91c:      	mov	x1, x23
fffffe00079ae920:      	mov	x2, x22
fffffe00079ae924:      	mov	x3, x21
fffffe00079ae928:      	mov	x4, x20
fffffe00079ae92c:      	bl	0xfffffe00079aeca4 <_transmit>
fffffe00079ae930:      	cbnz	w0, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae934:      	mov	w25, wzr
fffffe00079ae938:      	adrp	x27, 0xfffffe0007031000 <_txd+0xf4>
fffffe00079ae93c:      	add	x27, x27, #252
fffffe00079ae940:      	b	0xfffffe00079ae960 <_nst_step+0x178>
fffffe00079ae944:      	mov	w8, #1
fffffe00079ae948:      	cmp	w28, #0
fffffe00079ae94c:      	mov	w9, #3
fffffe00079ae950:      	csel	w9, w9, wzr, eq
fffffe00079ae954:      	cmp	w0, #0
fffffe00079ae958:      	csel	w8, w8, w9, lt
fffffe00079ae95c:      	cbnz	w8, 0xfffffe00079ae9b8 <_nst_step+0x1d0>
fffffe00079ae960:      	cmp	w25, w24
fffffe00079ae964:      	b.hs	0xfffffe00079ae9c0 <_nst_step+0x1d8>
fffffe00079ae968:      	mov	x26, xzr
fffffe00079ae96c:      	mov	w28, wzr
fffffe00079ae970:      	ldr	w3, [x27, x26, lsl #2]
fffffe00079ae974:      	mov	x0, x19
fffffe00079ae978:      	mov	x1, x23
fffffe00079ae97c:      	mov	x2, x22
fffffe00079ae980:      	mov	x4, x21
fffffe00079ae984:      	mov	x5, x20
fffffe00079ae988:      	bl	0xfffffe00079af684 <_receive>
fffffe00079ae98c:      	tbnz	w0, #31, 0xfffffe00079ae944 <_nst_step+0x15c>
fffffe00079ae990:      	cmp	w0, #0
fffffe00079ae994:      	mov	w8, #4
fffffe00079ae998:      	cinc	w25, w25, ne
fffffe00079ae99c:      	cinc	w28, w28, ne
fffffe00079ae9a0:      	cmp	x26, #3
fffffe00079ae9a4:      	b.hi	0xfffffe00079ae948 <_nst_step+0x160>
fffffe00079ae9a8:      	add	x26, x26, #1
fffffe00079ae9ac:      	cmp	w25, w24
fffffe00079ae9b0:      	b.lo	0xfffffe00079ae970 <_nst_step+0x188>
fffffe00079ae9b4:      	b	0xfffffe00079ae948 <_nst_step+0x160>
fffffe00079ae9b8:      	cmp	w8, #3
fffffe00079ae9bc:      	b.ne	0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079ae9c0:      	ldr	w8, [x19, #60]
fffffe00079ae9c4:      	cbz	w8, 0xfffffe00079aea58 <_nst_step+0x270>
fffffe00079ae9c8:      	ldr	w8, [x19, #140]
fffffe00079ae9cc:      	cbz	w8, 0xfffffe00079aea58 <_nst_step+0x270>
fffffe00079ae9d0:      	ldr	w8, [x19, #144]
fffffe00079ae9d4:      	cbz	w8, 0xfffffe00079aea3c <_nst_step+0x254>
fffffe00079ae9d8:      	ldr	w8, [x19, #148]
fffffe00079ae9dc:      	ldr	x9, [x19, #168]
fffffe00079ae9e0:      	cmp	w8, #256
fffffe00079ae9e4:      	stp	xzr, xzr, [sp, #40]
fffffe00079ae9e8:      	strh	wzr, [sp, #32]
fffffe00079ae9ec:      	stp	xzr, x9, [sp, #16]
fffffe00079ae9f0:      	ldp	w10, w9, [x19, #124]
fffffe00079ae9f4:      	str	wzr, [sp, #36]
fffffe00079ae9f8:      	strb	wzr, [sp, #41]
fffffe00079ae9fc:      	str	xzr, [sp, #56]
fffffe00079aea00:      	strb	w10, [sp, #34]
fffffe00079aea04:      	mov	w10, #2
fffffe00079aea08:      	strb	w9, [sp, #35]
fffffe00079aea0c:      	str	wzr, [x19, #60]
fffffe00079aea10:      	stp	w10, w8, [sp, #8]
fffffe00079aea14:      	b.ne	0xfffffe00079aea20 <_nst_step+0x238>
fffffe00079aea18:      	mov	x0, x19
fffffe00079aea1c:      	bl	0xfffffe00079acb88 <_nst_close_port>
fffffe00079aea20:      	add	x3, sp, #8
fffffe00079aea24:      	mov	x0, x19
fffffe00079aea28:      	mov	x1, x21
fffffe00079aea2c:      	mov	x2, x20
fffffe00079aea30:      	bl	0xfffffe00079afaac <_emit>
fffffe00079aea34:      	cbnz	w0, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079aea38:      	b	0xfffffe00079aea58 <_nst_step+0x270>
fffffe00079aea3c:      	mov	x0, x19
fffffe00079aea40:      	bl	0xfffffe00079afa68 <_retired_air_failure>
fffffe00079aea44:      	cbz	w0, 0xfffffe00079aea58 <_nst_step+0x270>
fffffe00079aea48:      	ldr	x8, [x19, #176]
fffffe00079aea4c:      	ldr	x9, [x19, #192]
fffffe00079aea50:      	cmp	x8, x9
fffffe00079aea54:      	b.hs	0xfffffe00079aeab0 <_nst_step+0x2c8>
fffffe00079aea58:      	ldr	w8, [x19, #56]
fffffe00079aea5c:      	cbz	w8, 0xfffffe00079aea94 <_nst_step+0x2ac>
fffffe00079aea60:      	ldr	w8, [x19, #10276]
fffffe00079aea64:      	cbz	w8, 0xfffffe00079aea94 <_nst_step+0x2ac>
fffffe00079aea68:      	ldr	w8, [x19, #132]
fffffe00079aea6c:      	and	w8, w8, #0xfffffffe
fffffe00079aea70:      	cmp	w8, #2
fffffe00079aea74:      	b.ne	0xfffffe00079aea94 <_nst_step+0x2ac>
fffffe00079aea78:      	mov	x0, x19
fffffe00079aea7c:      	mov	x1, x23
fffffe00079aea80:      	mov	x2, x22
fffffe00079aea84:      	mov	x3, x21
fffffe00079aea88:      	mov	x4, x20
fffffe00079aea8c:      	bl	0xfffffe00079afb14 <_data_transmit>
fffffe00079aea90:      	cbnz	w0, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079aea94:      	mov	x0, x19
fffffe00079aea98:      	mov	x1, x21
fffffe00079aea9c:      	mov	x2, x20
fffffe00079aeaa0:      	bl	0xfffffe00079b0408 <_peer_progress>
fffffe00079aeaa4:      	cbnz	w0, 0xfffffe00079ae850 <_nst_step+0x68>
fffffe00079aeaa8:      	stp	wzr, wzr, [x19, #36]
fffffe00079aeaac:      	b	0xfffffe00079ae854 <_nst_step+0x6c>
fffffe00079aeab0:      	mov	w8, #256
fffffe00079aeab4:      	b	0xfffffe00079ae9dc <_nst_step+0x1f4>
