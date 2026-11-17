
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b96e4 <_submit_native>:
fffffe00079b96e4:      	stp	x28, x27, [sp, #-64]!
fffffe00079b96e8:      	stp	x22, x21, [sp, #16]
fffffe00079b96ec:      	stp	x20, x19, [sp, #32]
fffffe00079b96f0:      	stp	x29, x30, [sp, #48]
fffffe00079b96f4:      	add	x29, sp, #48
fffffe00079b96f8:      	sub	sp, sp, #672
fffffe00079b96fc:      	adrp	x8, 0xfffffe0007c98000 <_cnread+0x10>
fffffe00079b9700:      	mov	x22, x2
fffffe00079b9704:      	mov	x19, x1
fffffe00079b9708:      	mov	x21, x0
fffffe00079b970c:      	nop
fffffe00079b9710:      	ldr	x8, [x8, #3440]
fffffe00079b9714:      	stur	x8, [x29, #-56]
fffffe00079b9718:      	ldr	w8, [x0, #4]
fffffe00079b971c:      	ldr	x20, [x0, #4800]
fffffe00079b9720:      	cmp	w8, #16
fffffe00079b9724:      	b.lo	0xfffffe00079b9730 <_submit_native+0x4c>
fffffe00079b9728:      	ldr	w8, [x21, #4840]
fffffe00079b972c:      	cbz	w8, 0xfffffe00079b9748 <_submit_native+0x64>
fffffe00079b9730:      	ldr	w8, [x20, #56]
fffffe00079b9734:      	cbnz	w8, 0xfffffe00079b9740 <_submit_native+0x5c>
fffffe00079b9738:      	ldr	w8, [x20, #60]
fffffe00079b973c:      	cbz	w8, 0xfffffe00079b97c8 <_submit_native+0xe4>
fffffe00079b9740:      	mov	w0, #1
fffffe00079b9744:      	b	0xfffffe00079b9798 <_submit_native+0xb4>
fffffe00079b9748:      	mov	x10, #42991
fffffe00079b974c:      	ldr	x8, [x21, #1304]
fffffe00079b9750:      	movk	x10, #19398, lsl #16
fffffe00079b9754:      	movk	x10, #35127, lsl #32
fffffe00079b9758:      	movk	x10, #65, lsl #48
fffffe00079b975c:      	cmp	x8, x10
fffffe00079b9760:      	b.hi	0xfffffe00079b9794 <_submit_native+0xb0>
fffffe00079b9764:      	ldr	x9, [x21, #4848]
fffffe00079b9768:      	cmp	x9, x10
fffffe00079b976c:      	b.hi	0xfffffe00079b9794 <_submit_native+0xb0>
fffffe00079b9770:      	mov	w10, #1000
fffffe00079b9774:      	ldr	x11, [x20, #168]
fffffe00079b9778:      	mov	x0, x20
fffffe00079b977c:      	mul	x9, x9, x10
fffffe00079b9780:      	mul	x2, x8, x10
fffffe00079b9784:      	cmp	x9, x11
fffffe00079b9788:      	csel	x1, x9, x11, hi
fffffe00079b978c:      	bl	0xfffffe00079abc00 <_nst_cleanup_begin>
fffffe00079b9790:      	cbz	w0, 0xfffffe00079b98b8 <_submit_native+0x1d4>
fffffe00079b9794:      	mov	w0, #-1
fffffe00079b9798:      	adrp	x8, 0xfffffe0007c98000 <_cnioctl+0x34>
fffffe00079b979c:      	nop
fffffe00079b97a0:      	ldur	x9, [x29, #-56]
fffffe00079b97a4:      	ldr	x8, [x8, #3440]
fffffe00079b97a8:      	cmp	x8, x9
fffffe00079b97ac:      	b.ne	0xfffffe00079b99b4 <_submit_native+0x2d0>
fffffe00079b97b0:      	add	sp, sp, #672
fffffe00079b97b4:      	ldp	x29, x30, [sp, #48]
fffffe00079b97b8:      	ldp	x20, x19, [sp, #32]
fffffe00079b97bc:      	ldp	x22, x21, [sp, #16]
fffffe00079b97c0:      	ldp	x28, x27, [sp], #64
fffffe00079b97c4:      	ret
fffffe00079b97c8:      	ldr	w8, [x19, #16]
fffffe00079b97cc:      	sub	w8, w8, #1
fffffe00079b97d0:      	cmp	w8, #15
fffffe00079b97d4:      	b.hi	0xfffffe00079b9794 <_submit_native+0xb0>
fffffe00079b97d8:      	adrp	x9, 0xfffffe0007031000 <_group_receive+0x438>
fffffe00079b97dc:      	add	x9, x9, #1376
fffffe00079b97e0:      	adr	x10, #16
fffffe00079b97e4:      	ldrb	w11, [x9, x8]
fffffe00079b97e8:      	add	x10, x10, x11, lsl #2
fffffe00079b97ec:      	br	x10
fffffe00079b97f0:      	ldr	x1, [x19, #8]
fffffe00079b97f4:      	add	x3, x19, #24
fffffe00079b97f8:      	ldr	w4, [x19, #20]
fffffe00079b97fc:      	mov	x0, x20
fffffe00079b9800:      	mov	w2, #7
fffffe00079b9804:      	bl	0xfffffe00079abcd4 <_nst_command>
fffffe00079b9808:      	cbnz	w0, 0xfffffe00079b9798 <_submit_native+0xb4>
fffffe00079b980c:      	mov	w8, #4824
fffffe00079b9810:      	ldr	x9, [x22]
fffffe00079b9814:      	add	x8, x21, x8
fffffe00079b9818:      	ldrb	w10, [x22, #8]
fffffe00079b981c:      	str	x9, [x21, #4824]
fffffe00079b9820:      	strb	w10, [x8, #8]
fffffe00079b9824:      	ldr	w8, [x19, #16]
fffffe00079b9828:      	cmp	w8, #7
fffffe00079b982c:      	cset	w8, eq
fffffe00079b9830:      	str	w8, [x21, #4836]
fffffe00079b9834:      	b	0xfffffe00079b9798 <_submit_native+0xb4>
fffffe00079b9838:      	adrp	x8, 0xfffffe0007c98000 <_kminit+0x18>
fffffe00079b983c:      	ldr	x3, [x21, #4808]
fffffe00079b9840:      	nop
fffffe00079b9844:      	ldr	x0, [x21, #4816]
fffffe00079b9848:      	ldur	x9, [x29, #-56]
fffffe00079b984c:      	ldr	x8, [x8, #3440]
fffffe00079b9850:      	cmp	x8, x9
fffffe00079b9854:      	b.ne	0xfffffe00079b99b4 <_submit_native+0x2d0>
fffffe00079b9858:      	mov	x1, x19
fffffe00079b985c:      	mov	x2, x21
fffffe00079b9860:      	add	sp, sp, #672
fffffe00079b9864:      	ldp	x29, x30, [sp, #48]
fffffe00079b9868:      	ldp	x20, x19, [sp, #32]
fffffe00079b986c:      	ldp	x22, x21, [sp, #16]
fffffe00079b9870:      	ldp	x28, x27, [sp], #64
fffffe00079b9874:      	br	x3
fffffe00079b9878:      	adrp	x8, 0xfffffe0007c98000 <_kmopen+0x28>
fffffe00079b987c:      	ldr	x1, [x19, #8]
fffffe00079b9880:      	nop
fffffe00079b9884:      	ldr	w3, [x19, #20]
fffffe00079b9888:      	ldur	x9, [x29, #-56]
fffffe00079b988c:      	ldr	x8, [x8, #3440]
fffffe00079b9890:      	cmp	x8, x9
fffffe00079b9894:      	b.ne	0xfffffe00079b99b4 <_submit_native+0x2d0>
fffffe00079b9898:      	add	x2, x19, #24
fffffe00079b989c:      	mov	x0, x20
fffffe00079b98a0:      	add	sp, sp, #672
fffffe00079b98a4:      	ldp	x29, x30, [sp, #48]
fffffe00079b98a8:      	ldp	x20, x19, [sp, #32]
fffffe00079b98ac:      	ldp	x22, x21, [sp, #16]
fffffe00079b98b0:      	ldp	x28, x27, [sp], #64
fffffe00079b98b4:      	b	0xfffffe00079ac1b0 <_nst_management>
fffffe00079b98b8:      	mov	w8, #1
fffffe00079b98bc:      	str	w8, [x21, #4840]
fffffe00079b98c0:      	ldr	w8, [x20, #56]
fffffe00079b98c4:      	cbnz	w8, 0xfffffe00079b9740 <_submit_native+0x5c>
fffffe00079b98c8:      	b	0xfffffe00079b9738 <_submit_native+0x54>
fffffe00079b98cc:      	adrp	x8, 0xfffffe0007c98000 <_kmopen+0x7c>
fffffe00079b98d0:      	ldr	x1, [x19, #8]
fffffe00079b98d4:      	nop
fffffe00079b98d8:      	ldr	w4, [x19, #20]
fffffe00079b98dc:      	ldur	x9, [x29, #-56]
fffffe00079b98e0:      	ldr	x8, [x8, #3440]
fffffe00079b98e4:      	cmp	x8, x9
fffffe00079b98e8:      	b.ne	0xfffffe00079b99b4 <_submit_native+0x2d0>
fffffe00079b98ec:      	add	x3, x19, #24
fffffe00079b98f0:      	mov	x0, x20
fffffe00079b98f4:      	mov	w2, #28
fffffe00079b98f8:      	add	sp, sp, #672
fffffe00079b98fc:      	ldp	x29, x30, [sp, #48]
fffffe00079b9900:      	ldp	x20, x19, [sp, #32]
fffffe00079b9904:      	ldp	x22, x21, [sp, #16]
fffffe00079b9908:      	ldp	x28, x27, [sp], #64
fffffe00079b990c:      	b	0xfffffe00079abcd4 <_nst_command>
fffffe00079b9910:      	add	x22, sp, #8
fffffe00079b9914:      	mov	w1, #640
fffffe00079b9918:      	add	x21, x22, #14
fffffe00079b991c:      	mov	x0, x21
fffffe00079b9920:      	bl	0xfffffe0007bad720 <_bzero>
fffffe00079b9924:      	ldr	w2, [x19, #20]
fffffe00079b9928:      	cmp	w2, #640
fffffe00079b992c:      	b.hi	0xfffffe00079b9794 <_submit_native+0xb0>
fffffe00079b9930:      	ldur	w8, [x20, #198]
fffffe00079b9934:      	add	x1, x19, #24
fffffe00079b9938:      	ldrh	w9, [x20, #202]
fffffe00079b993c:      	mov	x0, x21
fffffe00079b9940:      	ldr	w10, [x20, #192]
fffffe00079b9944:      	mov	w3, #640
fffffe00079b9948:      	ldrh	w11, [x20, #196]
fffffe00079b994c:      	str	w8, [sp, #8]
fffffe00079b9950:      	mov	w8, #36488
fffffe00079b9954:      	strh	w9, [sp, #12]
fffffe00079b9958:      	stur	w10, [sp, #14]
fffffe00079b995c:      	strh	w11, [sp, #18]
fffffe00079b9960:      	strh	w8, [sp, #20]
fffffe00079b9964:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b9968:      	ldp	w8, w9, [x19, #16]
fffffe00079b996c:      	ldr	x1, [x19, #8]
fffffe00079b9970:      	add	x2, sp, #8
fffffe00079b9974:      	mov	x0, x20
fffffe00079b9978:      	cmp	w8, #15
fffffe00079b997c:      	add	w3, w9, #14
fffffe00079b9980:      	cset	w4, eq
fffffe00079b9984:      	bl	0xfffffe00079ac6cc <_nst_eapol>
fffffe00079b9988:      	mov	x8, xzr
fffffe00079b998c:      	strb	wzr, [x22, x8]
fffffe00079b9990:      	add	x8, x8, #1
fffffe00079b9994:      	cmp	x8, #654
fffffe00079b9998:      	b.ne	0xfffffe00079b998c <_submit_native+0x2a8>
fffffe00079b999c:      	b	0xfffffe00079b9798 <_submit_native+0xb4>
fffffe00079b99a0:      	mov	x0, x20
fffffe00079b99a4:      	bl	0xfffffe00079ace68 <_nst_authorize>
fffffe00079b99a8:      	cmp	w0, #0
fffffe00079b99ac:      	csetm	w0, ne
fffffe00079b99b0:      	b	0xfffffe00079b9798 <_submit_native+0xb4>
fffffe00079b99b4:      	bl	0xfffffe0007a06354 <___stack_chk_fail>
