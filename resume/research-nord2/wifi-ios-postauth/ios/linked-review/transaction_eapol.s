
$NORD2_REPO/research/ci-39850764280/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b17dc <_stc_eapol>:
fffffe00079b17dc:      	sub	sp, sp, #384
fffffe00079b17e0:      	stp	x28, x27, [sp, #288]
fffffe00079b17e4:      	stp	x26, x25, [sp, #304]
fffffe00079b17e8:      	stp	x24, x23, [sp, #320]
fffffe00079b17ec:      	stp	x22, x21, [sp, #336]
fffffe00079b17f0:      	stp	x20, x19, [sp, #352]
fffffe00079b17f4:      	stp	x29, x30, [sp, #368]
fffffe00079b17f8:      	add	x29, sp, #368
fffffe00079b17fc:      	adrp	x8, 0xfffffe0007c94000 <_nwpc_hmac+0x350>
fffffe00079b1800:      	nop
fffffe00079b1804:      	ldr	x8, [x8, #3376]
fffffe00079b1808:      	stur	x8, [x29, #-88]
fffffe00079b180c:      	cbz	x0, 0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1810:      	ldr	x8, [x0, #1240]
fffffe00079b1814:      	mov	x19, x0
fffffe00079b1818:      	mov	x22, x1
fffffe00079b181c:      	cmp	x8, x1
fffffe00079b1820:      	b.ne	0xfffffe00079b1a70 <_stc_eapol+0x294>
fffffe00079b1824:      	ldr	w8, [x19]
fffffe00079b1828:      	cbz	w8, 0xfffffe00079b1a70 <_stc_eapol+0x294>
fffffe00079b182c:      	ldr	w26, [x19, #4]
fffffe00079b1830:      	sub	w8, w26, #21
fffffe00079b1834:      	cmp	w8, #2
fffffe00079b1838:      	b.lo	0xfffffe00079b1a70 <_stc_eapol+0x294>
fffffe00079b183c:      	mov	x0, x19
fffffe00079b1840:      	mov	x1, x4
fffffe00079b1844:      	mov	x20, x3
fffffe00079b1848:      	mov	x21, x2
fffffe00079b184c:      	bl	0xfffffe00079b0260 <_stc_tick>
fffffe00079b1850:      	tbnz	w0, #31, 0xfffffe00079b1a70 <_stc_eapol+0x294>
fffffe00079b1854:      	ldr	w8, [x19, #4]
fffffe00079b1858:      	mov	w0, wzr
fffffe00079b185c:      	cmp	w8, w26
fffffe00079b1860:      	b.ne	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1864:      	and	w8, w26, #0xfffffff8
fffffe00079b1868:      	cmp	w8, #8
fffffe00079b186c:      	b.ne	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1870:      	cbz	x21, 0xfffffe00079b19a4 <_stc_eapol+0x1c8>
fffffe00079b1874:      	sub	x8, x20, #1
fffffe00079b1878:      	cmp	x8, #640
fffffe00079b187c:      	b.hs	0xfffffe00079b19a4 <_stc_eapol+0x1c8>
fffffe00079b1880:      	cmp	w26, #15
fffffe00079b1884:      	b.hi	0xfffffe00079b1a24 <_stc_eapol+0x248>
fffffe00079b1888:      	mov	w8, #1
fffffe00079b188c:      	mov	w9, #37376
fffffe00079b1890:      	lsl	w8, w8, w26
fffffe00079b1894:      	tst	w8, w9
fffffe00079b1898:      	b.eq	0xfffffe00079b1a24 <_stc_eapol+0x248>
fffffe00079b189c:      	movi.2d	v0, #0000000000000000
fffffe00079b18a0:      	add	x23, x19, #1432
fffffe00079b18a4:      	mov	x3, sp
fffffe00079b18a8:      	mov	x0, x23
fffffe00079b18ac:      	mov	x1, x21
fffffe00079b18b0:      	mov	x2, x20
fffffe00079b18b4:      	ldr	w25, [x19, #1464]
fffffe00079b18b8:      	str	xzr, [sp, #272]
fffffe00079b18bc:      	stp	q0, q0, [sp, #16]
fffffe00079b18c0:      	mov	x27, sp
fffffe00079b18c4:      	stp	q0, q0, [sp, #48]
fffffe00079b18c8:      	stp	q0, q0, [sp, #80]
fffffe00079b18cc:      	stp	q0, q0, [sp, #112]
fffffe00079b18d0:      	stp	q0, q0, [sp, #144]
fffffe00079b18d4:      	stp	q0, q0, [sp, #176]
fffffe00079b18d8:      	stp	q0, q0, [sp, #208]
fffffe00079b18dc:      	stp	q0, q0, [sp, #240]
fffffe00079b18e0:      	str	q0, [sp]
fffffe00079b18e4:      	bl	0xfffffe00079b3514 <_nwp_receive>
fffffe00079b18e8:      	mov	w24, w0
fffffe00079b18ec:      	ldr	x1, [x19, #2872]
fffffe00079b18f0:      	ldr	w6, [sp, #8]
fffffe00079b18f4:      	mov	x0, x22
fffffe00079b18f8:      	mov	w2, w25
fffffe00079b18fc:      	mov	x3, x21
fffffe00079b1900:      	mov	x4, x20
fffffe00079b1904:      	mov	w5, w24
fffffe00079b1908:      	bl	0xfffffe00079b4a84 <_nwpd_observe>
fffffe00079b190c:      	tbnz	w24, #31, 0xfffffe00079b1a00 <_stc_eapol+0x224>
fffffe00079b1910:      	cmp	w24, #1
fffffe00079b1914:      	b.ne	0xfffffe00079b1a58 <_stc_eapol+0x27c>
fffffe00079b1918:      	mov	x8, xzr
fffffe00079b191c:      	add	x9, x19, #2864
fffffe00079b1920:      	strb	wzr, [x9, x8]
fffffe00079b1924:      	add	x8, x8, #1
fffffe00079b1928:      	cmp	x8, #664
fffffe00079b192c:      	b.ne	0xfffffe00079b1920 <_stc_eapol+0x144>
fffffe00079b1930:      	ldr	x8, [x19, #1248]
fffffe00079b1934:      	mov	w9, #10
fffffe00079b1938:      	cmn	x8, #1
fffffe00079b193c:      	str	w9, [x19, #4]
fffffe00079b1940:      	b.eq	0xfffffe00079b1ad0 <_stc_eapol+0x2f4>
fffffe00079b1944:      	add	x8, x8, #1
fffffe00079b1948:      	mov	x20, sp
fffffe00079b194c:      	ldr	x9, [x19, #1240]
fffffe00079b1950:      	mov	w10, #6
fffffe00079b1954:      	add	x0, x19, #2888
fffffe00079b1958:      	add	x1, x20, #24
fffffe00079b195c:      	str	x8, [x19, #1248]
fffffe00079b1960:      	ldr	w2, [sp, #4]
fffffe00079b1964:      	str	x8, [x19, #2872]
fffffe00079b1968:      	ldr	x8, [sp, #16]
fffffe00079b196c:      	str	x9, [x19, #2864]
fffffe00079b1970:      	str	w10, [x19, #2880]
fffffe00079b1974:      	str	w26, [x19, #572]
fffffe00079b1978:      	str	x8, [x19, #1280]
fffffe00079b197c:      	bl	0xfffffe0007ba96b0 <_memmove>
fffffe00079b1980:      	ldr	w9, [sp, #4]
fffffe00079b1984:      	mov	x8, xzr
fffffe00079b1988:      	str	w9, [x19, #2884]
fffffe00079b198c:      	strb	wzr, [x20, x8]
fffffe00079b1990:      	add	x8, x8, #1
fffffe00079b1994:      	cmp	x8, #280
fffffe00079b1998:      	b.ne	0xfffffe00079b198c <_stc_eapol+0x1b0>
fffffe00079b199c:      	mov	w0, #1
fffffe00079b19a0:      	b	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b19a4:      	cmp	x21, #0
fffffe00079b19a8:      	ldr	x1, [x19, #2872]
fffffe00079b19ac:      	cset	w3, ne
fffffe00079b19b0:      	ldr	w2, [x19, #1464]
fffffe00079b19b4:      	mov	x0, x22
fffffe00079b19b8:      	mov	x4, x20
fffffe00079b19bc:      	bl	0xfffffe00079b4c70 <_nwpd_gate_observe>
fffffe00079b19c0:      	adrp	x8, 0xfffffe0007c94000 <_nwpc_pmk+0x13c>
fffffe00079b19c4:      	nop
fffffe00079b19c8:      	ldur	x9, [x29, #-88]
fffffe00079b19cc:      	ldr	x8, [x8, #3376]
fffffe00079b19d0:      	cmp	x8, x9
fffffe00079b19d4:      	b.ne	0xfffffe00079b1b08 <_stc_eapol+0x32c>
fffffe00079b19d8:      	mov	x0, x19
fffffe00079b19dc:      	mov	w1, #2
fffffe00079b19e0:      	ldp	x29, x30, [sp, #368]
fffffe00079b19e4:      	ldp	x20, x19, [sp, #352]
fffffe00079b19e8:      	ldp	x22, x21, [sp, #336]
fffffe00079b19ec:      	ldp	x24, x23, [sp, #320]
fffffe00079b19f0:      	ldp	x26, x25, [sp, #304]
fffffe00079b19f4:      	ldp	x28, x27, [sp, #288]
fffffe00079b19f8:      	add	sp, sp, #384
fffffe00079b19fc:      	b	0xfffffe00079b0498 <_stop>
fffffe00079b1a00:      	mov	x8, xzr
fffffe00079b1a04:      	strb	wzr, [x27, x8]
fffffe00079b1a08:      	add	x8, x8, #1
fffffe00079b1a0c:      	cmp	x8, #280
fffffe00079b1a10:      	b.ne	0xfffffe00079b1a04 <_stc_eapol+0x228>
fffffe00079b1a14:      	mov	x0, x19
fffffe00079b1a18:      	mov	w1, #2
fffffe00079b1a1c:      	bl	0xfffffe00079b0498 <_stop>
fffffe00079b1a20:      	b	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1a24:      	ldr	w8, [x19, #576]
fffffe00079b1a28:      	cbz	w8, 0xfffffe00079b1aac <_stc_eapol+0x2d0>
fffffe00079b1a2c:      	ldr	w8, [x19, #580]
fffffe00079b1a30:      	cmp	x8, x20
fffffe00079b1a34:      	b.ne	0xfffffe00079b1b00 <_stc_eapol+0x324>
fffffe00079b1a38:      	add	x0, x19, #584
fffffe00079b1a3c:      	mov	x1, x21
fffffe00079b1a40:      	mov	x2, x20
fffffe00079b1a44:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b1a48:      	cmp	w0, #0
fffffe00079b1a4c:      	cset	w8, ne
fffffe00079b1a50:      	lsl	w0, w8, #1
fffffe00079b1a54:      	b	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1a58:      	mov	x8, xzr
fffffe00079b1a5c:      	mov	x9, sp
fffffe00079b1a60:      	strb	wzr, [x9, x8]
fffffe00079b1a64:      	add	x8, x8, #1
fffffe00079b1a68:      	cmp	x8, #280
fffffe00079b1a6c:      	b.ne	0xfffffe00079b1a60 <_stc_eapol+0x284>
fffffe00079b1a70:      	mov	w0, wzr
fffffe00079b1a74:      	adrp	x8, 0xfffffe0007c94000 <_nwpc_pmk+0x1f0>
fffffe00079b1a78:      	nop
fffffe00079b1a7c:      	ldur	x9, [x29, #-88]
fffffe00079b1a80:      	ldr	x8, [x8, #3376]
fffffe00079b1a84:      	cmp	x8, x9
fffffe00079b1a88:      	b.ne	0xfffffe00079b1b08 <_stc_eapol+0x32c>
fffffe00079b1a8c:      	ldp	x29, x30, [sp, #368]
fffffe00079b1a90:      	ldp	x20, x19, [sp, #352]
fffffe00079b1a94:      	ldp	x22, x21, [sp, #336]
fffffe00079b1a98:      	ldp	x24, x23, [sp, #320]
fffffe00079b1a9c:      	ldp	x26, x25, [sp, #304]
fffffe00079b1aa0:      	ldp	x28, x27, [sp, #288]
fffffe00079b1aa4:      	add	sp, sp, #384
fffffe00079b1aa8:      	ret
fffffe00079b1aac:      	add	x0, x19, #584
fffffe00079b1ab0:      	mov	x1, x21
fffffe00079b1ab4:      	mov	x2, x20
fffffe00079b1ab8:      	mov	w3, #640
fffffe00079b1abc:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b1ac0:      	mov	w0, #1
fffffe00079b1ac4:      	str	w20, [x19, #580]
fffffe00079b1ac8:      	str	w0, [x19, #576]
fffffe00079b1acc:      	b	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1ad0:      	mov	w8, #22
fffffe00079b1ad4:      	mov	x0, x23
fffffe00079b1ad8:      	str	w8, [x19, #4]
fffffe00079b1adc:      	bl	0xfffffe00079b3140 <_nwp_cancel>
fffffe00079b1ae0:      	mov	x8, xzr
fffffe00079b1ae4:      	mov	x9, sp
fffffe00079b1ae8:      	strb	wzr, [x9, x8]
fffffe00079b1aec:      	add	x8, x8, #1
fffffe00079b1af0:      	cmp	x8, #280
fffffe00079b1af4:      	b.ne	0xfffffe00079b1ae8 <_stc_eapol+0x30c>
fffffe00079b1af8:      	mov	w0, #-1
fffffe00079b1afc:      	b	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1b00:      	mov	w0, #2
fffffe00079b1b04:      	b	0xfffffe00079b1a74 <_stc_eapol+0x298>
fffffe00079b1b08:      	bl	0xfffffe0007a024b4 <___stack_chk_fail>
