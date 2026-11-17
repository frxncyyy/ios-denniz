
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079ab784 <_nst_prepare>:
fffffe00079ab784:      	stp	x28, x27, [sp, #-96]!
fffffe00079ab788:      	stp	x26, x25, [sp, #16]
fffffe00079ab78c:      	stp	x24, x23, [sp, #32]
fffffe00079ab790:      	stp	x22, x21, [sp, #48]
fffffe00079ab794:      	stp	x20, x19, [sp, #64]
fffffe00079ab798:      	stp	x29, x30, [sp, #80]
fffffe00079ab79c:      	add	x29, sp, #80
fffffe00079ab7a0:      	cbz	x0, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab7a4:      	ldp	x28, x27, [x29, #16]
fffffe00079ab7a8:      	mov	x19, x0
fffffe00079ab7ac:      	mov	w0, #-1
fffffe00079ab7b0:      	mov	w9, #34560
fffffe00079ab7b4:      	movk	w9, #915, lsl #16
fffffe00079ab7b8:      	sub	x8, x27, x28
fffffe00079ab7bc:      	cmp	x8, x9
fffffe00079ab7c0:      	b.hi	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7c4:      	cmp	x27, x28
fffffe00079ab7c8:      	b.ls	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7cc:      	mov	x21, x7
fffffe00079ab7d0:      	cbz	x7, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7d4:      	mov	x23, x6
fffffe00079ab7d8:      	cbz	x6, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7dc:      	mov	x24, x5
fffffe00079ab7e0:      	cbz	x5, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7e4:      	mov	x26, x4
fffffe00079ab7e8:      	cbz	x4, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7ec:      	mov	x20, x3
fffffe00079ab7f0:      	cbz	x3, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7f4:      	mov	x25, x2
fffffe00079ab7f8:      	cbz	x2, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab7fc:      	mov	x22, x1
fffffe00079ab800:      	cbz	x1, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab804:      	ldr	w8, [x19, #28]
fffffe00079ab808:      	cbnz	w8, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab80c:      	mov	x0, x25
fffffe00079ab810:      	bl	0xfffffe000799afb0 <_nct_done_receipt>
fffffe00079ab814:      	cbz	w0, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab818:      	ldr	w8, [x25, #108]
fffffe00079ab81c:      	cbz	w8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab820:      	ldr	x8, [x25, #16]
fffffe00079ab824:      	cmp	x8, x22
fffffe00079ab828:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab82c:      	ldr	w9, [x25, #128]
fffffe00079ab830:      	ldr	w8, [x25, #68]
fffffe00079ab834:      	ldr	w10, [x22, #2872]
fffffe00079ab838:      	add	w9, w8, w9
fffffe00079ab83c:      	and	w9, w9, #0x3ff
fffffe00079ab840:      	cmp	w10, w9
fffffe00079ab844:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab848:      	ldr	w9, [x25, #132]
fffffe00079ab84c:      	ldr	w10, [x22, #1928]
fffffe00079ab850:      	add	w9, w9, w8
fffffe00079ab854:      	cmp	w10, w9
fffffe00079ab858:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab85c:      	ldrb	w9, [x25, #136]
fffffe00079ab860:      	ldrb	w10, [x22, #2884]
fffffe00079ab864:      	add	w8, w9, w8
fffffe00079ab868:      	cmp	w10, w8, uxtb
fffffe00079ab86c:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab870:      	ldr	x8, [x20]
fffffe00079ab874:      	cmp	x8, x22
fffffe00079ab878:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab87c:      	ldr	x8, [x20, #8]
fffffe00079ab880:      	cmp	x8, x25
fffffe00079ab884:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab888:      	ldr	w8, [x20, #24]
fffffe00079ab88c:      	cbnz	w8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab890:      	ldr	w8, [x20, #36]
fffffe00079ab894:      	cbnz	w8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab898:      	ldr	w8, [x20, #40]
fffffe00079ab89c:      	cbz	w8, 0xfffffe00079ab8c0 <_nst_prepare+0x13c>
fffffe00079ab8a0:      	mov	w0, #-1
fffffe00079ab8a4:      	ldp	x29, x30, [sp, #80]
fffffe00079ab8a8:      	ldp	x20, x19, [sp, #64]
fffffe00079ab8ac:      	ldp	x22, x21, [sp, #48]
fffffe00079ab8b0:      	ldp	x24, x23, [sp, #32]
fffffe00079ab8b4:      	ldp	x26, x25, [sp, #16]
fffffe00079ab8b8:      	ldp	x28, x27, [sp], #96
fffffe00079ab8bc:      	ret
fffffe00079ab8c0:      	add	x0, x20, #1856
fffffe00079ab8c4:      	bl	0xfffffe00079ab6d0 <_nst_scan_admissible>
fffffe00079ab8c8:      	cbz	w0, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab8cc:      	ldr	w8, [x22, #1916]
fffffe00079ab8d0:      	cbnz	w8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab8d4:      	mov	x0, x24
fffffe00079ab8d8:      	bl	0xfffffe00079abbc0 <_mac>
fffffe00079ab8dc:      	cbz	w0, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab8e0:      	add	x0, x24, #6
fffffe00079ab8e4:      	bl	0xfffffe00079abbc0 <_mac>
fffffe00079ab8e8:      	cbz	w0, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab8ec:      	ldrb	w8, [x24, #12]
fffffe00079ab8f0:      	cbnz	w8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab8f4:      	ldrb	w8, [x24, #14]
fffffe00079ab8f8:      	cbnz	w8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab8fc:      	ldrb	w8, [x24, #13]
fffffe00079ab900:      	ldrb	w9, [x26, #4]
fffffe00079ab904:      	cmp	w8, w9
fffffe00079ab908:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab90c:      	ldrb	w10, [x24, #16]
fffffe00079ab910:      	ldrb	w9, [x26, #5]
fffffe00079ab914:      	cmp	w10, w9
fffffe00079ab918:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab91c:      	ldrb	w9, [x24, #15]
fffffe00079ab920:      	cmp	w9, w10
fffffe00079ab924:      	b.ls	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab928:      	ldrb	w10, [x26, #1]
fffffe00079ab92c:      	mov	w0, #-1
fffffe00079ab930:      	sub	w11, w10, #1
fffffe00079ab934:      	cmp	w11, w9
fffffe00079ab938:      	b.le	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab93c:      	cmp	w10, #7
fffffe00079ab940:      	b.lo	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab944:      	ldrb	w10, [x23, #1]
fffffe00079ab948:      	cmp	w10, w8
fffffe00079ab94c:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab950:      	ldrb	w8, [x23]
fffffe00079ab954:      	cmp	w8, w9
fffffe00079ab958:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab95c:      	ldrh	w8, [x23, #8]
fffffe00079ab960:      	cmp	w8, #8
fffffe00079ab964:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab968:      	ldrb	w8, [x23, #2]
fffffe00079ab96c:      	cmp	w8, #1
fffffe00079ab970:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab974:      	ldrb	w8, [x24, #17]
fffffe00079ab978:      	cmp	w8, #2
fffffe00079ab97c:      	b.eq	0xfffffe00079ab99c <_nst_prepare+0x218>
fffffe00079ab980:      	cmp	w8, #1
fffffe00079ab984:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab988:      	ldrb	w8, [x24, #18]
fffffe00079ab98c:      	sub	w8, w8, #1
fffffe00079ab990:      	cmp	w8, #14
fffffe00079ab994:      	b.hs	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab998:      	b	0xfffffe00079ab9a8 <_nst_prepare+0x224>
fffffe00079ab99c:      	ldrb	w8, [x24, #18]
fffffe00079ab9a0:      	cmp	w8, #15
fffffe00079ab9a4:      	b.lo	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab9a8:      	ldr	w8, [x22, #104]
fffffe00079ab9ac:      	mov	w9, #21248
fffffe00079ab9b0:      	movk	w9, #6146, lsl #16
fffffe00079ab9b4:      	cmp	w8, w9
fffffe00079ab9b8:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab9bc:      	ldr	w8, [x22, #108]
fffffe00079ab9c0:      	cmp	w8, #1024
fffffe00079ab9c4:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab9c8:      	ldr	w8, [x22, #116]
fffffe00079ab9cc:      	cbnz	w8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab9d0:      	ldr	x8, [x22, #88]
fffffe00079ab9d4:      	cbz	x8, 0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab9d8:      	ldr	x10, [x22, #408]
fffffe00079ab9dc:      	subs	x9, x8, x10
fffffe00079ab9e0:      	b.lo	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079ab9e4:      	ldr	x8, [x22, #416]
fffffe00079ab9e8:      	mov	w0, #-1
fffffe00079ab9ec:      	subs	x9, x8, x9
fffffe00079ab9f0:      	b.lo	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab9f4:      	cmp	x9, #4, lsl #12
fffffe00079ab9f8:      	b.lo	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079ab9fc:      	ldr	x9, [x22, #392]
fffffe00079aba00:      	mov	w0, #-1
fffffe00079aba04:      	cbz	x9, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079aba08:      	and	x11, x9, #0xfff
fffffe00079aba0c:      	cbnz	x11, 0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079aba10:      	subs	x10, x9, x10
fffffe00079aba14:      	b.lo	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079aba18:      	subs	x8, x8, x10
fffffe00079aba1c:      	mov	w0, #-1
fffffe00079aba20:      	b.lo	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079aba24:      	cmp	x8, #1, lsl #12
fffffe00079aba28:      	b.lo	0xfffffe00079ab8a4 <_nst_prepare+0x120>
fffffe00079aba2c:      	ldr	x8, [x22, #384]
fffffe00079aba30:      	add	x9, x9, #1, lsl #12
fffffe00079aba34:      	cmp	x8, x9
fffffe00079aba38:      	b.ne	0xfffffe00079ab8a0 <_nst_prepare+0x11c>
fffffe00079aba3c:      	mov	w8, #1
fffffe00079aba40:      	str	w8, [x19, #10284]
fffffe00079aba44:      	ldr	x9, [x22, #88]
fffffe00079aba48:      	str	x9, [x19, #10232]
fffffe00079aba4c:      	ldr	x9, [x22, #392]
fffffe00079aba50:      	str	x9, [x19, #10224]
fffffe00079aba54:      	ldrb	w9, [x24, #16]
fffffe00079aba58:      	cmp	w9, #4
fffffe00079aba5c:      	b.ne	0xfffffe00079abae4 <_nst_prepare+0x360>
fffffe00079aba60:      	ldrb	w9, [x24, #15]
fffffe00079aba64:      	cmp	w9, #5
fffffe00079aba68:      	b.ne	0xfffffe00079abae4 <_nst_prepare+0x360>
fffffe00079aba6c:      	ldrb	w9, [x26, #1]
fffffe00079aba70:      	sub	w9, w9, #1
fffffe00079aba74:      	cmp	w9, #7
fffffe00079aba78:      	b.lo	0xfffffe00079aba90 <_nst_prepare+0x30c>
fffffe00079aba7c:      	mov	w9, #6
fffffe00079aba80:      	strb	w9, [x19, #1500]
fffffe00079aba84:      	ldrb	w9, [x24, #16]
fffffe00079aba88:      	cmp	w9, #4
fffffe00079aba8c:      	b.ne	0xfffffe00079abae4 <_nst_prepare+0x360>
fffffe00079aba90:      	ldrb	w9, [x24, #15]
fffffe00079aba94:      	cmp	w9, #5
fffffe00079aba98:      	b.ne	0xfffffe00079abae4 <_nst_prepare+0x360>
fffffe00079aba9c:      	ldrb	w9, [x26, #1]
fffffe00079abaa0:      	sub	w9, w9, #1
fffffe00079abaa4:      	tst	x9, #0xfffffff8
fffffe00079abaa8:      	b.eq	0xfffffe00079abac0 <_nst_prepare+0x33c>
fffffe00079abaac:      	mov	w9, #7
fffffe00079abab0:      	strb	w9, [x19, #1501]
fffffe00079abab4:      	ldrb	w9, [x24, #16]
fffffe00079abab8:      	cmp	w9, #4
fffffe00079ababc:      	b.ne	0xfffffe00079abae4 <_nst_prepare+0x360>
fffffe00079abac0:      	ldrb	w9, [x24, #15]
fffffe00079abac4:      	cmp	w9, #5
fffffe00079abac8:      	b.ne	0xfffffe00079abae4 <_nst_prepare+0x360>
fffffe00079abacc:      	ldrb	w9, [x26, #1]
fffffe00079abad0:      	sub	w9, w9, #1
fffffe00079abad4:      	cmp	w9, #9
fffffe00079abad8:      	b.lo	0xfffffe00079abae4 <_nst_prepare+0x360>
fffffe00079abadc:      	mov	w9, #8
fffffe00079abae0:      	strb	w9, [x19, #1502]
fffffe00079abae4:      	add	x9, x25, #108
fffffe00079abae8:      	mov	w0, wzr
fffffe00079abaec:      	ld1r.2s	{ v0 }, [x9]
fffffe00079abaf0:      	stp	x22, x25, [x19]
fffffe00079abaf4:      	str	x20, [x19, #16]
fffffe00079abaf8:      	str	d0, [x19, #64]
fffffe00079abafc:      	ldur	w9, [x24, #15]
fffffe00079abb00:      	ldr	q0, [x24]
fffffe00079abb04:      	stur	w9, [x19, #207]
fffffe00079abb08:      	str	q0, [x19, #192]
fffffe00079abb0c:      	ldr	q0, [x23]
fffffe00079abb10:      	ldr	w9, [x23, #16]
fffffe00079abb14:      	str	x21, [x19, #152]
fffffe00079abb18:      	stp	x28, x27, [x19, #168]
fffffe00079abb1c:      	stur	q0, [x19, #212]
fffffe00079abb20:      	str	w9, [x19, #228]
fffffe00079abb24:      	ldr	w9, [x22, #2872]
fffffe00079abb28:      	str	w9, [x19, #84]
fffffe00079abb2c:      	ldr	w9, [x22, #1928]
fffffe00079abb30:      	str	w8, [x19, #104]
fffffe00079abb34:      	str	w9, [x19, #88]
fffffe00079abb38:      	add	x9, x19, #232
fffffe00079abb3c:      	ldr	q0, [x20, #144]
fffffe00079abb40:      	stur	q0, [x19, #232]
fffffe00079abb44:      	ldp	q1, q0, [x20, #192]
fffffe00079abb48:      	ldp	q3, q2, [x20, #160]
fffffe00079abb4c:      	stp	q1, q0, [x9, #48]
fffffe00079abb50:      	stur	q3, [x19, #248]
fffffe00079abb54:      	str	q2, [x9, #32]
fffffe00079abb58:      	ldp	q1, q0, [x20, #272]
fffffe00079abb5c:      	ldp	q2, q3, [x20, #240]
fffffe00079abb60:      	stp	q1, q0, [x9, #128]
fffffe00079abb64:      	stp	q2, q3, [x9, #96]
fffffe00079abb68:      	ldr	q0, [x20, #224]
fffffe00079abb6c:      	str	q0, [x9, #80]
fffffe00079abb70:      	ldp	q1, q0, [x20, #352]
fffffe00079abb74:      	ldp	q2, q3, [x20, #320]
fffffe00079abb78:      	stp	q1, q0, [x9, #208]
fffffe00079abb7c:      	stp	q2, q3, [x9, #176]
fffffe00079abb80:      	ldr	q0, [x20, #304]
fffffe00079abb84:      	str	q0, [x9, #160]
fffffe00079abb88:      	ldp	q1, q0, [x20, #432]
fffffe00079abb8c:      	ldp	q2, q3, [x20, #400]
fffffe00079abb90:      	stp	q1, q0, [x9, #288]
fffffe00079abb94:      	stp	q2, q3, [x9, #256]
fffffe00079abb98:      	ldr	q0, [x20, #384]
fffffe00079abb9c:      	str	q0, [x9, #240]
fffffe00079abba0:      	ldr	q0, [x20, #464]
fffffe00079abba4:      	str	q0, [x9, #320]
fffffe00079abba8:      	ldp	q1, q0, [x20, #512]
fffffe00079abbac:      	ldp	q3, q2, [x20, #480]
fffffe00079abbb0:      	stp	q1, q0, [x9, #368]
fffffe00079abbb4:      	str	w8, [x19, #28]
fffffe00079abbb8:      	stp	q3, q2, [x9, #336]
fffffe00079abbbc:      	b	0xfffffe00079ab8a4 <_nst_prepare+0x120>
