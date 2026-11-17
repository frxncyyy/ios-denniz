
$NORD2_REPO/research/ci-39804982005/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007acc840 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)>:
fffffe0007acc840:      	sub	sp, sp, #368
fffffe0007acc844:      	stp	x28, x27, [sp, #272]
fffffe0007acc848:      	stp	x26, x25, [sp, #288]
fffffe0007acc84c:      	stp	x24, x23, [sp, #304]
fffffe0007acc850:      	stp	x22, x21, [sp, #320]
fffffe0007acc854:      	stp	x20, x19, [sp, #336]
fffffe0007acc858:      	stp	x29, x30, [sp, #352]
fffffe0007acc85c:      	add	x29, sp, #352
fffffe0007acc860:      	adrp	x8, 0xfffffe0007c8c000 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x720>
fffffe0007acc864:      	mov	x25, x2
fffffe0007acc868:      	mov	w20, w1
fffffe0007acc86c:      	and	w9, w1, #0xfffffffe
fffffe0007acc870:      	cmp	w9, #10
fffffe0007acc874:      	nop
fffffe0007acc878:      	ldr	x8, [x8, #3376]
fffffe0007acc87c:      	stur	x8, [x29, #-96]
fffffe0007acc880:      	b.ne	0xfffffe0007acca50 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x210>
fffffe0007acc884:      	mov	w22, #706
fffffe0007acc888:      	strb	wzr, [sp, #128]
fffffe0007acc88c:      	movk	w22, #57344, lsl #16
fffffe0007acc890:      	strb	wzr, [sp, #129]
fffffe0007acc894:      	strb	wzr, [sp, #130]
fffffe0007acc898:      	strb	wzr, [sp, #131]
fffffe0007acc89c:      	strb	wzr, [sp, #132]
fffffe0007acc8a0:      	strb	wzr, [sp, #133]
fffffe0007acc8a4:      	strb	wzr, [sp, #134]
fffffe0007acc8a8:      	strb	wzr, [sp, #135]
fffffe0007acc8ac:      	strb	wzr, [sp, #136]
fffffe0007acc8b0:      	strb	wzr, [sp, #137]
fffffe0007acc8b4:      	strb	wzr, [sp, #138]
fffffe0007acc8b8:      	strb	wzr, [sp, #139]
fffffe0007acc8bc:      	strb	wzr, [sp, #140]
fffffe0007acc8c0:      	strb	wzr, [sp, #141]
fffffe0007acc8c4:      	strb	wzr, [sp, #142]
fffffe0007acc8c8:      	strb	wzr, [sp, #143]
fffffe0007acc8cc:      	strb	wzr, [sp, #144]
fffffe0007acc8d0:      	strb	wzr, [sp, #145]
fffffe0007acc8d4:      	strb	wzr, [sp, #146]
fffffe0007acc8d8:      	strb	wzr, [sp, #147]
fffffe0007acc8dc:      	strb	wzr, [sp, #148]
fffffe0007acc8e0:      	strb	wzr, [sp, #149]
fffffe0007acc8e4:      	strb	wzr, [sp, #150]
fffffe0007acc8e8:      	strb	wzr, [sp, #151]
fffffe0007acc8ec:      	strb	wzr, [sp, #152]
fffffe0007acc8f0:      	strb	wzr, [sp, #153]
fffffe0007acc8f4:      	strb	wzr, [sp, #154]
fffffe0007acc8f8:      	strb	wzr, [sp, #155]
fffffe0007acc8fc:      	strb	wzr, [sp, #156]
fffffe0007acc900:      	strb	wzr, [sp, #157]
fffffe0007acc904:      	strb	wzr, [sp, #158]
fffffe0007acc908:      	strb	wzr, [sp, #159]
fffffe0007acc90c:      	strb	wzr, [sp, #160]
fffffe0007acc910:      	strb	wzr, [sp, #161]
fffffe0007acc914:      	strb	wzr, [sp, #162]
fffffe0007acc918:      	strb	wzr, [sp, #163]
fffffe0007acc91c:      	strb	wzr, [sp, #164]
fffffe0007acc920:      	strb	wzr, [sp, #165]
fffffe0007acc924:      	strb	wzr, [sp, #166]
fffffe0007acc928:      	strb	wzr, [sp, #167]
fffffe0007acc92c:      	strb	wzr, [sp, #168]
fffffe0007acc930:      	strb	wzr, [sp, #169]
fffffe0007acc934:      	strb	wzr, [sp, #170]
fffffe0007acc938:      	strb	wzr, [sp, #171]
fffffe0007acc93c:      	strb	wzr, [sp, #172]
fffffe0007acc940:      	strb	wzr, [sp, #173]
fffffe0007acc944:      	strb	wzr, [sp, #174]
fffffe0007acc948:      	strb	wzr, [sp, #175]
fffffe0007acc94c:      	strb	wzr, [sp, #176]
fffffe0007acc950:      	strb	wzr, [sp, #177]
fffffe0007acc954:      	strb	wzr, [sp, #178]
fffffe0007acc958:      	strb	wzr, [sp, #179]
fffffe0007acc95c:      	strb	wzr, [sp, #180]
fffffe0007acc960:      	strb	wzr, [sp, #181]
fffffe0007acc964:      	strb	wzr, [sp, #182]
fffffe0007acc968:      	strb	wzr, [sp, #183]
fffffe0007acc96c:      	strb	wzr, [sp, #184]
fffffe0007acc970:      	strb	wzr, [sp, #185]
fffffe0007acc974:      	strb	wzr, [sp, #186]
fffffe0007acc978:      	strb	wzr, [sp, #187]
fffffe0007acc97c:      	strb	wzr, [sp, #188]
fffffe0007acc980:      	strb	wzr, [sp, #189]
fffffe0007acc984:      	strb	wzr, [sp, #190]
fffffe0007acc988:      	strb	wzr, [sp, #191]
fffffe0007acc98c:      	strb	wzr, [sp, #192]
fffffe0007acc990:      	strb	wzr, [sp, #193]
fffffe0007acc994:      	strb	wzr, [sp, #194]
fffffe0007acc998:      	strb	wzr, [sp, #195]
fffffe0007acc99c:      	strb	wzr, [sp, #196]
fffffe0007acc9a0:      	strb	wzr, [sp, #197]
fffffe0007acc9a4:      	strb	wzr, [sp, #198]
fffffe0007acc9a8:      	strb	wzr, [sp, #199]
fffffe0007acc9ac:      	cbz	x25, 0xfffffe0007acca70 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x230>
fffffe0007acc9b0:      	ldr	w8, [x25]
fffffe0007acc9b4:      	cmp	w8, #2
fffffe0007acc9b8:      	b.ne	0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acc9bc:      	ldr	w8, [x25, #4]
fffffe0007acc9c0:      	cmp	w8, w20
fffffe0007acc9c4:      	b.ne	0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acc9c8:      	ldr	w8, [x25, #40]
fffffe0007acc9cc:      	cmp	w8, #2
fffffe0007acc9d0:      	b.ne	0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acc9d4:      	ldr	x8, [x25, #32]
fffffe0007acc9d8:      	cbz	x8, 0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acc9dc:      	ldr	x8, [x8]
fffffe0007acc9e0:      	cbnz	x8, 0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acc9e4:      	ldr	w8, [x25, #80]
fffffe0007acc9e8:      	cmp	w20, #10
fffffe0007acc9ec:      	cset	w9, eq
fffffe0007acc9f0:      	cmp	w8, w9
fffffe0007acc9f4:      	b.hi	0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acc9f8:      	cbz	w8, 0xfffffe0007acca04 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1c4>
fffffe0007acc9fc:      	ldr	x8, [x25, #72]
fffffe0007acca00:      	cbz	x8, 0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acca04:      	ldr	w8, [x25, #24]
fffffe0007acca08:      	cbnz	w8, 0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acca0c:      	ldr	x8, [x25, #8]
fffffe0007acca10:      	cbnz	x8, 0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acca14:      	ldr	x8, [x25, #120]
fffffe0007acca18:      	cbnz	x8, 0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007acca1c:      	ldr	x8, [x25, #64]
fffffe0007acca20:      	cbz	x8, 0xfffffe0007accd58 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x518>
fffffe0007acca24:      	mov	w8, #706
fffffe0007acca28:      	movk	w8, #57344, lsl #16
fffffe0007acca2c:      	ldr	x9, [x25, #104]
fffffe0007acca30:      	mov	w10, #96
fffffe0007acca34:      	mov	w22, w8
fffffe0007acca38:      	cmp	x9, #0
fffffe0007acca3c:      	mov	w9, #112
fffffe0007acca40:      	csel	x9, x10, x9, eq
fffffe0007acca44:      	ldr	w10, [x25, x9]
fffffe0007acca48:      	ldr	w9, [x25, #56]
fffffe0007acca4c:      	b	0xfffffe0007acca78 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x238>
fffffe0007acca50:      	ldr	x0, [x25, #64]
fffffe0007acca54:      	ldr	w23, [x25, #40]
fffffe0007acca58:      	cbz	x0, 0xfffffe0007acca94 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x254>
fffffe0007acca5c:      	ldr	x8, [x0]
fffffe0007acca60:      	ldr	x8, [x8, #496]
fffffe0007acca64:      	blr	x8
fffffe0007acca68:      	mov	w24, w0
fffffe0007acca6c:      	b	0xfffffe0007acca98 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x258>
fffffe0007acca70:      	mov	x9, xzr
fffffe0007acca74:      	mov	x10, xzr
fffffe0007acca78:      	mov	w8, w20
fffffe0007acca7c:      	adrp	x0, 0xfffffe000714f000 <sysctl_sleepwaketime(sysctl_oid*, void*, int, sysctl_req*)+0x7c>
fffffe0007acca80:      	add	x0, x0, #2426
fffffe0007acca84:      	stp	x10, x22, [sp, #16]
fffffe0007acca88:      	stp	x8, x9, [sp]
fffffe0007acca8c:      	bl	0xfffffe0007a11850 <_IOLog>
fffffe0007acca90:      	b	0xfffffe0007accb40 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x300>
fffffe0007acca94:      	ldr	w24, [x25, #56]
fffffe0007acca98:      	ldr	x0, [x25, #104]
fffffe0007acca9c:      	ldr	w26, [x25, #80]
fffffe0007accaa0:      	cbz	x0, 0xfffffe0007accac4 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x284>
fffffe0007accaa4:      	ldr	x8, [x0]
fffffe0007accaa8:      	ldr	x8, [x8, #496]
fffffe0007accaac:      	blr	x8
fffffe0007accab0:      	mov	w27, w0
fffffe0007accab4:      	cbz	w23, 0xfffffe0007accacc <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x28c>
fffffe0007accab8:      	ldr	x8, [x25, #32]
fffffe0007accabc:      	ldr	x28, [x8]
fffffe0007accac0:      	b	0xfffffe0007accad0 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x290>
fffffe0007accac4:      	ldr	w27, [x25, #96]
fffffe0007accac8:      	cbnz	w23, 0xfffffe0007accab8 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x278>
fffffe0007accacc:      	mov	x28, xzr
fffffe0007accad0:      	mov	w9, #706
fffffe0007accad4:      	mov	w8, #5
fffffe0007accad8:      	movk	w9, #57344, lsl #16
fffffe0007accadc:      	cmp	w20, #103
fffffe0007accae0:      	orr	w22, w9, w8
fffffe0007accae4:      	b.hi	0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007accae8:      	mov	w8, w20
fffffe0007accaec:      	adrp	x9, 0xfffffe0007035000 <IORootParent::initialize()+0x700>
fffffe0007accaf0:      	add	x9, x9, #3448
fffffe0007accaf4:      	adr	x10, #16
fffffe0007accaf8:      	ldrb	w11, [x9, x8]
fffffe0007accafc:      	add	x10, x10, x11, lsl #2
fffffe0007accb00:      	br	x10
fffffe0007accb04:      	mov	w22, wzr
fffffe0007accb08:      	str	wzr, [x25, #80]
fffffe0007accb0c:      	movi.2d	v0, #0000000000000000
fffffe0007accb10:      	add	x0, sp, #128
fffffe0007accb14:      	mov	w1, #32
fffffe0007accb18:      	add	x19, sp, #128
fffffe0007accb1c:      	stp	q0, q0, [sp, #128]
fffffe0007accb20:      	bl	0xfffffe000783c84c <_proc_selfname>
fffffe0007accb24:      	adrp	x0, 0xfffffe000714f000 <sysctl_willshutdown(sysctl_oid*, void*, int, sysctl_req*)+0x7c>
fffffe0007accb28:      	add	x0, x0, #2493
fffffe0007accb2c:      	stp	x27, x22, [sp, #48]
fffffe0007accb30:      	stp	x24, x26, [sp, #32]
fffffe0007accb34:      	stp	x28, x23, [sp, #16]
fffffe0007accb38:      	stp	x19, x20, [sp]
fffffe0007accb3c:      	bl	0xfffffe0007a11850 <_IOLog>
fffffe0007accb40:      	adrp	x8, 0xfffffe0007c8c000 <IOWatchdog::IOWatchdog(OSMetaClass const*)+0x8>
fffffe0007accb44:      	nop
fffffe0007accb48:      	ldur	x9, [x29, #-96]
fffffe0007accb4c:      	ldr	x8, [x8, #3376]
fffffe0007accb50:      	cmp	x8, x9
fffffe0007accb54:      	b.ne	0xfffffe0007accec8 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x688>
fffffe0007accb58:      	mov	w0, w22
fffffe0007accb5c:      	ldp	x29, x30, [sp, #352]
fffffe0007accb60:      	ldp	x20, x19, [sp, #336]
fffffe0007accb64:      	ldp	x22, x21, [sp, #320]
fffffe0007accb68:      	ldp	x24, x23, [sp, #304]
fffffe0007accb6c:      	ldp	x26, x25, [sp, #288]
fffffe0007accb70:      	ldp	x28, x27, [sp, #272]
fffffe0007accb74:      	add	sp, sp, #368
fffffe0007accb78:      	ret
fffffe0007accb7c:      	mov	w22, #706
fffffe0007accb80:      	movk	w22, #57344, lsl #16
fffffe0007accb84:      	cbz	w26, 0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007accb88:      	ldr	x8, [x25, #72]
fffffe0007accb8c:      	mov	w9, #1
fffffe0007accb90:      	mov	w22, wzr
fffffe0007accb94:      	str	xzr, [x8]
fffffe0007accb98:      	str	w9, [x25, #80]
fffffe0007accb9c:      	b	0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007accba0:      	movi.2d	v0, #0000000000000000
fffffe0007accba4:      	stp	x28, x27, [sp, #72]
fffffe0007accba8:      	stp	x23, x20, [sp, #104]
fffffe0007accbac:      	mov	x28, xzr
fffffe0007accbb0:      	mov	w20, #1
fffffe0007accbb4:      	mov	w21, #2
fffffe0007accbb8:      	adrp	x27, 0xfffffe00072ae000 <OSValueObject<PMAssertStruct>::init()+0x54>
fffffe0007accbbc:      	add	x27, x27, #184
fffffe0007accbc0:      	stp	x26, x24, [sp, #88]
fffffe0007accbc4:      	str	x25, [sp, #120]
fffffe0007accbc8:      	str	wzr, [x25, #80]
fffffe0007accbcc:      	stp	q0, q0, [sp, #128]
fffffe0007accbd0:      	stp	q0, q0, [sp, #160]
fffffe0007accbd4:      	stp	q0, q0, [sp, #192]
fffffe0007accbd8:      	stp	q0, q0, [sp, #224]
fffffe0007accbdc:      	add	x8, x27, x28
fffffe0007accbe0:      	mov	w26, w21
fffffe0007accbe4:      	add	x9, sp, #128
fffffe0007accbe8:      	add	x25, x9, x26
fffffe0007accbec:      	ldr	x22, [x8]
fffffe0007accbf0:      	ldrb	w24, [x8, #8]
fffffe0007accbf4:      	mov	x0, x22
fffffe0007accbf8:      	bl	0xfffffe00074ad380 <_strlen>
fffffe0007accbfc:      	mov	w8, #48
fffffe0007accc00:      	mov	x23, x0
fffffe0007accc04:      	cmp	w21, #124
fffffe0007accc08:      	add	w19, w23, #5
fffffe0007accc0c:      	add	x0, x25, #4
fffffe0007accc10:      	and	x2, x23, #0xffffffff
fffffe0007accc14:      	strb	w8, [x25]
fffffe0007accc18:      	mov	w8, #12
fffffe0007accc1c:      	mov	x1, x22
fffffe0007accc20:      	strb	w19, [x25, #1]
fffffe0007accc24:      	strb	w23, [x25, #3]
fffffe0007accc28:      	strb	w8, [x25, #2]
fffffe0007accc2c:      	mov	w8, #124
fffffe0007accc30:      	sub	x8, x8, x26
fffffe0007accc34:      	csel	x3, xzr, x8, hi
fffffe0007accc38:      	bl	0xfffffe00074672a4 <___memcpy_chk>
fffffe0007accc3c:      	add	w10, w21, w23
fffffe0007accc40:      	add	w8, w23, #4
fffffe0007accc44:      	add	w9, w23, #6
fffffe0007accc48:      	add	x28, x28, #16
fffffe0007accc4c:      	add	w21, w10, #7
fffffe0007accc50:      	mov	w10, #2
fffffe0007accc54:      	cmp	x28, #128
fffffe0007accc58:      	strb	w10, [x25, w8, uxtw]
fffffe0007accc5c:      	strb	w20, [x25, w19, uxtw]
fffffe0007accc60:      	strb	w24, [x25, w9, uxtw]
fffffe0007accc64:      	b.ne	0xfffffe0007accbdc <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x39c>
fffffe0007accc68:      	ldr	x19, [sp, #120]
fffffe0007accc6c:      	mov	w8, #49
fffffe0007accc70:      	sub	w9, w21, #2
fffffe0007accc74:      	ldr	x0, [x19, #104]
fffffe0007accc78:      	strb	w8, [sp, #128]
fffffe0007accc7c:      	strb	w9, [sp, #129]
fffffe0007accc80:      	cbz	x0, 0xfffffe0007acccd8 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x498>
fffffe0007accc84:      	ldr	x8, [x0]
fffffe0007accc88:      	ldr	x8, [x8, #496]
fffffe0007accc8c:      	blr	x8
fffffe0007accc90:      	ldp	x28, x27, [sp, #72]
fffffe0007accc94:      	cmp	x0, x21
fffffe0007accc98:      	ldr	x20, [sp, #112]
fffffe0007accc9c:      	b.hs	0xfffffe0007accd24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x4e4>
fffffe0007accca0:      	mov	w8, #706
fffffe0007accca4:      	movk	w8, #57344, lsl #16
fffffe0007accca8:      	add	w22, w8, #25
fffffe0007acccac:      	b	0xfffffe0007accd18 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x4d8>
fffffe0007acccb0:      	mov	w22, #706
fffffe0007acccb4:      	movk	w22, #57344, lsl #16
fffffe0007acccb8:      	cbz	w26, 0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007acccbc:      	ldr	x8, [x25, #72]
fffffe0007acccc0:      	mov	w9, #4
fffffe0007acccc4:      	mov	w10, #1
fffffe0007acccc8:      	mov	w22, wzr
fffffe0007accccc:      	str	x9, [x8]
fffffe0007acccd0:      	str	w10, [x25, #80]
fffffe0007acccd4:      	b	0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007acccd8:      	mov	w8, #706
fffffe0007acccdc:      	ldr	x0, [x19, #88]
fffffe0007accce0:      	ldp	x28, x27, [sp, #72]
fffffe0007accce4:      	movk	w8, #57344, lsl #16
fffffe0007accce8:      	add	w22, w8, #25
fffffe0007acccec:      	ldr	x20, [sp, #112]
fffffe0007acccf0:      	cbz	x0, 0xfffffe0007accd18 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x4d8>
fffffe0007acccf4:      	ldp	x24, x23, [sp, #96]
fffffe0007acccf8:      	ldr	w8, [x19, #96]!
fffffe0007acccfc:      	ldr	x26, [sp, #88]
fffffe0007accd00:      	cmp	w8, w21
fffffe0007accd04:      	b.lo	0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007accd08:      	add	x1, sp, #128
fffffe0007accd0c:      	mov	x2, x21
fffffe0007accd10:      	bl	0xfffffe0007ba2d10 <_memmove>
fffffe0007accd14:      	b	0xfffffe0007accd4c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x50c>
fffffe0007accd18:      	ldp	x24, x23, [sp, #96]
fffffe0007accd1c:      	ldr	x26, [sp, #88]
fffffe0007accd20:      	b	0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007accd24:      	ldr	x0, [x19, #104]
fffffe0007accd28:      	add	x2, sp, #128
fffffe0007accd2c:      	mov	x1, xzr
fffffe0007accd30:      	mov	x3, x21
fffffe0007accd34:      	ldr	x8, [x0]
fffffe0007accd38:      	ldr	x8, [x8, #528]
fffffe0007accd3c:      	blr	x8
fffffe0007accd40:      	ldp	x24, x23, [sp, #96]
fffffe0007accd44:      	add	x19, x19, #112
fffffe0007accd48:      	ldr	x26, [sp, #88]
fffffe0007accd4c:      	mov	w22, wzr
fffffe0007accd50:      	str	w21, [x19]
fffffe0007accd54:      	b	0xfffffe0007accb0c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2cc>
fffffe0007accd58:      	mov	w26, #706
fffffe0007accd5c:      	ldr	x24, [x25, #48]
fffffe0007accd60:      	movk	w26, #57344, lsl #16
fffffe0007accd64:      	cbz	x24, 0xfffffe0007acca24 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1e4>
fffffe0007accd68:      	mov	x28, x25
fffffe0007accd6c:      	ldr	w22, [x25, #56]
fffffe0007accd70:      	mov	x27, x20
fffffe0007accd74:      	cmp	w20, #10
fffffe0007accd78:      	b.ne	0xfffffe0007accde0 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5a0>
fffffe0007accd7c:      	sub	w8, w22, #65
fffffe0007accd80:      	cmn	w8, #64
fffffe0007accd84:      	b.lo	0xfffffe0007acce30 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f0>
fffffe0007accd88:      	mov	w8, #8
fffffe0007accd8c:      	ldr	x23, [x28, #104]
fffffe0007accd90:      	add	x21, x8, x22
fffffe0007accd94:      	cbz	x23, 0xfffffe0007acce44 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x604>
fffffe0007accd98:      	ldr	w8, [x28, #96]
fffffe0007accd9c:      	cbnz	w8, 0xfffffe0007acce30 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f0>
fffffe0007accda0:      	ldr	x8, [x23]
fffffe0007accda4:      	mov	x0, x23
fffffe0007accda8:      	ldr	w19, [x28, #112]
fffffe0007accdac:      	mov	x25, x28
fffffe0007accdb0:      	ldr	x8, [x8, #496]
fffffe0007accdb4:      	blr	x8
fffffe0007accdb8:      	mov	w8, #706
fffffe0007accdbc:      	mov	x20, x27
fffffe0007accdc0:      	movk	w8, #57344, lsl #16
fffffe0007accdc4:      	cmp	x0, x19
fffffe0007accdc8:      	b.ne	0xfffffe0007acca2c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1ec>
fffffe0007accdcc:      	ldr	w8, [x25, #112]
fffffe0007accdd0:      	cmp	x21, x8
fffffe0007accdd4:      	b.ls	0xfffffe0007acce7c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x63c>
fffffe0007accdd8:      	add	w8, w26, #25
fffffe0007accddc:      	b	0xfffffe0007acca2c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1ec>
fffffe0007accde0:      	sub	w8, w22, #73
fffffe0007accde4:      	cmn	w8, #64
fffffe0007accde8:      	b.lo	0xfffffe0007acce30 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f0>
fffffe0007accdec:      	adrp	x1, 0xfffffe000714f000 <invocation function for block in update_aotmode(unsigned int)+0x2c>
fffffe0007accdf0:      	add	x1, x1, #2552
fffffe0007accdf4:      	mov	x0, x24
fffffe0007accdf8:      	mov	w2, #4
fffffe0007accdfc:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe0007acce00:      	cbnz	w0, 0xfffffe0007acce30 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f0>
fffffe0007acce04:      	ldr	x8, [x28, #32]
fffffe0007acce08:      	ldrb	w9, [x24, #4]
fffffe0007acce0c:      	ldrb	w8, [x8, #8]
fffffe0007acce10:      	cmp	w9, w8
fffffe0007acce14:      	b.ne	0xfffffe0007acce30 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f0>
fffffe0007acce18:      	ldrb	w8, [x24, #5]
fffffe0007acce1c:      	cbnz	w8, 0xfffffe0007acce30 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f0>
fffffe0007acce20:      	ldrb	w8, [x24, #6]
fffffe0007acce24:      	cbnz	w8, 0xfffffe0007acce30 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f0>
fffffe0007acce28:      	ldrb	w8, [x24, #7]
fffffe0007acce2c:      	cbz	w8, 0xfffffe0007acd04c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x80c>
fffffe0007acce30:      	mov	w8, #706
fffffe0007acce34:      	movk	w8, #57344, lsl #16
fffffe0007acce38:      	mov	x20, x27
fffffe0007acce3c:      	mov	x25, x28
fffffe0007acce40:      	b	0xfffffe0007acca2c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1ec>
fffffe0007acce44:      	mov	w8, #706
fffffe0007acce48:      	mov	x25, x28
fffffe0007acce4c:      	ldr	w9, [x28, #112]
fffffe0007acce50:      	movk	w8, #57344, lsl #16
fffffe0007acce54:      	mov	x20, x27
fffffe0007acce58:      	cbnz	w9, 0xfffffe0007acca2c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1ec>
fffffe0007acce5c:      	ldr	x9, [x25, #88]
fffffe0007acce60:      	add	w8, w26, #25
fffffe0007acce64:      	cbz	x9, 0xfffffe0007acce38 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f8>
fffffe0007acce68:      	ldr	w9, [x28, #96]
fffffe0007acce6c:      	mov	x25, x28
fffffe0007acce70:      	mov	x20, x27
fffffe0007acce74:      	cmp	x21, x9
fffffe0007acce78:      	b.hi	0xfffffe0007acca2c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1ec>
fffffe0007acce7c:      	cmp	w20, #10
fffffe0007acce80:      	b.ne	0xfffffe0007accecc <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x68c>
fffffe0007acce84:      	adrp	x1, 0xfffffe000714f000 <invocation function for block in update_aotmode(unsigned int)+0xc4>
fffffe0007acce88:      	add	x1, x1, #2552
fffffe0007acce8c:      	add	x0, sp, #128
fffffe0007acce90:      	mov	w2, #4
fffffe0007acce94:      	mov	w3, #72
fffffe0007acce98:      	add	x8, sp, #128
fffffe0007acce9c:      	add	x25, x8, #8
fffffe0007accea0:      	bl	0xfffffe00074672a4 <___memcpy_chk>
fffffe0007accea4:      	ldr	x8, [x28, #32]
fffffe0007accea8:      	mov	x0, x25
fffffe0007acceac:      	mov	x1, x24
fffffe0007acceb0:      	mov	x2, x22
fffffe0007acceb4:      	mov	w3, #64
fffffe0007acceb8:      	ldr	x8, [x8, #8]
fffffe0007accebc:      	strb	w8, [sp, #132]
fffffe0007accec0:      	bl	0xfffffe00074672a4 <___memcpy_chk>
fffffe0007accec4:      	b	0xfffffe0007accee4 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x6a4>
fffffe0007accec8:      	bl	0xfffffe00079fc274 <___stack_chk_fail>
fffffe0007accecc:      	add	x1, x24, #8
fffffe0007acced0:      	add	x0, sp, #128
fffffe0007acced4:      	mov	x2, x21
fffffe0007acced8:      	mov	w3, #72
fffffe0007accedc:      	bl	0xfffffe00074672a4 <___memcpy_chk>
fffffe0007accee0:      	mov	x22, x21
fffffe0007accee4:      	cbz	x22, 0xfffffe0007accf7c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x73c>
fffffe0007accee8:      	cmp	w27, #10
fffffe0007acceec:      	cset	w8, eq
fffffe0007accef0:      	cmp	x22, #16
fffffe0007accef4:      	ubfiz	x9, x8, #3, #32
fffffe0007accef8:      	b.hs	0xfffffe0007accf04 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x6c4>
fffffe0007accefc:      	mov	x8, xzr
fffffe0007accf00:      	b	0xfffffe0007accf50 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x710>
fffffe0007accf04:      	adrp	x8, 0xfffffe0007006000 <OSValueObject<PMAssertStruct>::MetaClass::alloc() const+0x34>
fffffe0007accf08:      	mov	x10, xzr
fffffe0007accf0c:      	movi.16b	v0, #37
fffffe0007accf10:      	add	x11, sp, #128
fffffe0007accf14:      	movi.16b	v1, #16
fffffe0007accf18:      	ldr	q2, [x8, #3872]
fffffe0007accf1c:      	and	x8, x22, #0xfffffffffffffff0
fffffe0007accf20:      	movi.16b	v3, #90
fffffe0007accf24:      	orr	x12, x10, x9
fffffe0007accf28:      	add	x10, x10, #16
fffffe0007accf2c:      	cmp	x8, x10
fffffe0007accf30:      	ldr	q4, [x11, x12]
fffffe0007accf34:      	mla.16b	v3, v2, v0
fffffe0007accf38:      	add.16b	v2, v2, v1
fffffe0007accf3c:      	eor.16b	v3, v3, v4
fffffe0007accf40:      	str	q3, [x11, x12]
fffffe0007accf44:      	b.ne	0xfffffe0007accf20 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x6e0>
fffffe0007accf48:      	cmp	x22, x8
fffffe0007accf4c:      	b.eq	0xfffffe0007accf7c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x73c>
fffffe0007accf50:      	add	x10, sp, #128
fffffe0007accf54:      	mov	w11, #90
fffffe0007accf58:      	add	x9, x10, x9
fffffe0007accf5c:      	mov	w10, #37
fffffe0007accf60:      	ldrb	w12, [x9, x8]
fffffe0007accf64:      	madd	w13, w8, w10, w11
fffffe0007accf68:      	eor	w12, w13, w12
fffffe0007accf6c:      	strb	w12, [x9, x8]
fffffe0007accf70:      	add	x8, x8, #1
fffffe0007accf74:      	cmp	x22, x8
fffffe0007accf78:      	b.ne	0xfffffe0007accf60 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x720>
fffffe0007accf7c:      	cbz	x23, 0xfffffe0007accff0 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x7b0>
fffffe0007accf80:      	ldr	x8, [x23]
fffffe0007accf84:      	mov	x0, x23
fffffe0007accf88:      	mov	w1, #1
fffffe0007accf8c:      	ldr	x8, [x8, #536]
fffffe0007accf90:      	blr	x8
fffffe0007accf94:      	mov	w19, w0
fffffe0007accf98:      	cbnz	w0, 0xfffffe0007acd058 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x818>
fffffe0007accf9c:      	ldr	x8, [x23]
fffffe0007accfa0:      	add	x2, sp, #128
fffffe0007accfa4:      	mov	x0, x23
fffffe0007accfa8:      	mov	x1, xzr
fffffe0007accfac:      	mov	x3, x21
fffffe0007accfb0:      	ldr	x8, [x8, #528]
fffffe0007accfb4:      	blr	x8
fffffe0007accfb8:      	ldr	x8, [x23]
fffffe0007accfbc:      	mov	x22, x0
fffffe0007accfc0:      	mov	x0, x23
fffffe0007accfc4:      	mov	w1, #1
fffffe0007accfc8:      	ldr	x8, [x8, #544]
fffffe0007accfcc:      	blr	x8
fffffe0007accfd0:      	mov	w19, w0
fffffe0007accfd4:      	cbnz	w0, 0xfffffe0007acd058 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x818>
fffffe0007accfd8:      	cmp	x22, x21
fffffe0007accfdc:      	b.ne	0xfffffe0007acd054 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x814>
fffffe0007accfe0:      	add	x0, sp, #128
fffffe0007accfe4:      	bl	0xfffffe0007acd078 <sweet_aks_compat_wipe(void*, unsigned long)>
fffffe0007accfe8:      	str	w21, [x28, #112]
fffffe0007accfec:      	b	0xfffffe0007acd010 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x7d0>
fffffe0007accff0:      	ldr	x0, [x28, #88]
fffffe0007accff4:      	add	x1, sp, #128
fffffe0007accff8:      	mov	x2, x21
fffffe0007accffc:      	mov	x19, x28
fffffe0007acd000:      	bl	0xfffffe0007ba2d10 <_memmove>
fffffe0007acd004:      	add	x0, sp, #128
fffffe0007acd008:      	bl	0xfffffe0007acd078 <sweet_aks_compat_wipe(void*, unsigned long)>
fffffe0007acd00c:      	str	w21, [x28, #96]
fffffe0007acd010:      	cmp	w27, #10
fffffe0007acd014:      	b.ne	0xfffffe0007acd044 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x804>
fffffe0007acd018:      	ldr	w8, [x28, #80]
fffffe0007acd01c:      	cmp	w8, #1
fffffe0007acd020:      	b.ne	0xfffffe0007acd044 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x804>
fffffe0007acd024:      	ldr	x8, [x28, #32]
fffffe0007acd028:      	mov	x25, x28
fffffe0007acd02c:      	ldr	x10, [x28, #72]
fffffe0007acd030:      	mov	x20, x27
fffffe0007acd034:      	ldr	x9, [x8, #8]
fffffe0007acd038:      	mov	w8, wzr
fffffe0007acd03c:      	str	x9, [x10]
fffffe0007acd040:      	b	0xfffffe0007acca2c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1ec>
fffffe0007acd044:      	mov	w8, wzr
fffffe0007acd048:      	b	0xfffffe0007acce38 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f8>
fffffe0007acd04c:      	mov	x8, #-8
fffffe0007acd050:      	b	0xfffffe0007accd8c <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x54c>
fffffe0007acd054:      	add	w19, w26, #8
fffffe0007acd058:      	add	x0, sp, #128
fffffe0007acd05c:      	bl	0xfffffe0007acd078 <sweet_aks_compat_wipe(void*, unsigned long)>
fffffe0007acd060:      	mov	w8, w19
fffffe0007acd064:      	b	0xfffffe0007acce38 <AppleKeyStoreUserClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x5f8>
