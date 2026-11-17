
$NORD2_REPO/research/ci-39804982005/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007ae4b24 <IO80211Controller::start(IOService*)>:
fffffe0007ae4b24:      	stp	x28, x27, [sp, #-96]!
fffffe0007ae4b28:      	stp	x26, x25, [sp, #16]
fffffe0007ae4b2c:      	stp	x24, x23, [sp, #32]
fffffe0007ae4b30:      	stp	x22, x21, [sp, #48]
fffffe0007ae4b34:      	stp	x20, x19, [sp, #64]
fffffe0007ae4b38:      	stp	x29, x30, [sp, #80]
fffffe0007ae4b3c:      	add	x29, sp, #80
fffffe0007ae4b40:      	sub	sp, sp, #512
fffffe0007ae4b44:      	adrp	x8, 0xfffffe0007c8c000 <IO80211Controller::start(IOService*)+0x6c0>
fffffe0007ae4b48:      	mov	x20, x1
fffffe0007ae4b4c:      	mov	x19, x0
fffffe0007ae4b50:      	mov	x21, x0
fffffe0007ae4b54:      	adrp	x0, 0xfffffe0007096000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0xe8>
fffffe0007ae4b58:      	add	x0, x0, #746
fffffe0007ae4b5c:      	nop
fffffe0007ae4b60:      	sub	x1, x29, #116
fffffe0007ae4b64:      	mov	w2, #4
fffffe0007ae4b68:      	ldr	x8, [x8, #3376]
fffffe0007ae4b6c:      	stur	x8, [x29, #-88]
fffffe0007ae4b70:      	stp	xzr, xzr, [x21, #136]!
fffffe0007ae4b74:      	str	xzr, [x21, #16]
fffffe0007ae4b78:      	stur	wzr, [x29, #-116]
fffffe0007ae4b7c:      	bl	0xfffffe0007ae72c0 <_PE_parse_boot_argn>
fffffe0007ae4b80:      	ldr	x8, [x20]
fffffe0007ae4b84:      	adrp	x1, 0xfffffe0007130000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x380>
fffffe0007ae4b88:      	add	x1, x1, #3491
fffffe0007ae4b8c:      	mov	x0, x20
fffffe0007ae4b90:      	ldr	x8, [x8, #728]
fffffe0007ae4b94:      	blr	x8
fffffe0007ae4b98:      	adrp	x8, 0xfffffe000724e000 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x70>
fffffe0007ae4b9c:      	nop
fffffe0007ae4ba0:      	ldr	x23, [x8, #2136]
fffffe0007ae4ba4:      	mov	x1, x23
fffffe0007ae4ba8:      	bl	0xfffffe00079b6860 <OSMetaClassBase::safeMetaCast(OSMetaClassBase const*, OSMetaClass const*)>
fffffe0007ae4bac:      	ldur	w8, [x29, #-116]
fffffe0007ae4bb0:      	mov	w22, wzr
fffffe0007ae4bb4:      	cmp	w8, #1
fffffe0007ae4bb8:      	b.ne	0xfffffe0007ae4c04 <IO80211Controller::start(IOService*)+0xe0>
fffffe0007ae4bbc:      	mov	x24, x0
fffffe0007ae4bc0:      	cbz	x0, 0xfffffe0007ae4c04 <IO80211Controller::start(IOService*)+0xe0>
fffffe0007ae4bc4:      	ldr	x8, [x24]
fffffe0007ae4bc8:      	mov	x0, x24
fffffe0007ae4bcc:      	ldr	x8, [x8, #352]
fffffe0007ae4bd0:      	blr	x8
fffffe0007ae4bd4:      	cmp	w0, #18
fffffe0007ae4bd8:      	b.ne	0xfffffe0007ae4c00 <IO80211Controller::start(IOService*)+0xdc>
fffffe0007ae4bdc:      	ldr	x8, [x24]
fffffe0007ae4be0:      	mov	x0, x24
fffffe0007ae4be4:      	ldr	x8, [x8, #408]
fffffe0007ae4be8:      	blr	x8
fffffe0007ae4bec:      	adrp	x1, 0xfffffe0007036000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)>
fffffe0007ae4bf0:      	add	x1, x1, #2960
fffffe0007ae4bf4:      	mov	w2, #18
fffffe0007ae4bf8:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe0007ae4bfc:      	cbz	w0, 0xfffffe0007ae4c40 <IO80211Controller::start(IOService*)+0x11c>
fffffe0007ae4c00:      	mov	w22, wzr
fffffe0007ae4c04:      	adrp	x8, 0xfffffe0007c8c000 <IO80211Controller::start(IOService*)+0x780>
fffffe0007ae4c08:      	nop
fffffe0007ae4c0c:      	ldur	x9, [x29, #-88]
fffffe0007ae4c10:      	ldr	x8, [x8, #3376]
fffffe0007ae4c14:      	cmp	x8, x9
fffffe0007ae4c18:      	b.ne	0xfffffe0007ae541c <IO80211Controller::start(IOService*)+0x8f8>
fffffe0007ae4c1c:      	mov	w0, w22
fffffe0007ae4c20:      	add	sp, sp, #512
fffffe0007ae4c24:      	ldp	x29, x30, [sp, #80]
fffffe0007ae4c28:      	ldp	x20, x19, [sp, #64]
fffffe0007ae4c2c:      	ldp	x22, x21, [sp, #48]
fffffe0007ae4c30:      	ldp	x24, x23, [sp, #32]
fffffe0007ae4c34:      	ldp	x26, x25, [sp, #16]
fffffe0007ae4c38:      	ldp	x28, x27, [sp], #96
fffffe0007ae4c3c:      	ret
fffffe0007ae4c40:      	movi.2d	v0, #0000000000000000
fffffe0007ae4c44:      	sub	x8, x29, #224
fffffe0007ae4c48:      	sub	x0, x29, #224
fffffe0007ae4c4c:      	stur	wzr, [x29, #-128]
fffffe0007ae4c50:      	stp	q0, q0, [x8]
fffffe0007ae4c54:      	stp	q0, q0, [x8, #32]
fffffe0007ae4c58:      	stp	q0, q0, [x8, #64]
fffffe0007ae4c5c:      	bl	0xfffffe000750dacc <_nord2_wifi_scan_info>
fffffe0007ae4c60:      	mov	w22, wzr
fffffe0007ae4c64:      	cbnz	w0, 0xfffffe0007ae4c04 <IO80211Controller::start(IOService*)+0xe0>
fffffe0007ae4c68:      	ldur	w8, [x29, #-212]
fffffe0007ae4c6c:      	cbz	w8, 0xfffffe0007ae4c04 <IO80211Controller::start(IOService*)+0xe0>
fffffe0007ae4c70:      	adrp	x26, 0xfffffe0007254000 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x160>
fffffe0007ae4c74:      	mov	x0, x19
fffffe0007ae4c78:      	mov	x1, x20
fffffe0007ae4c7c:      	add	x26, x26, #1224
fffffe0007ae4c80:      	ldr	x8, [x26, #1536]
fffffe0007ae4c84:      	blr	x8
fffffe0007ae4c88:      	cbz	w0, 0xfffffe0007ae4c00 <IO80211Controller::start(IOService*)+0xdc>
fffffe0007ae4c8c:      	ldurb	w8, [x29, #-224]
fffffe0007ae4c90:      	mov	w25, #1
fffffe0007ae4c94:      	tbnz	w8, #0, 0xfffffe0007ae4e94 <IO80211Controller::start(IOService*)+0x370>
fffffe0007ae4c98:      	ldurb	w9, [x29, #-223]
fffffe0007ae4c9c:      	ldurb	w10, [x29, #-222]
fffffe0007ae4ca0:      	ldurb	w11, [x29, #-221]
fffffe0007ae4ca4:      	orr	w13, w9, w8
fffffe0007ae4ca8:      	ldurb	w12, [x29, #-220]
fffffe0007ae4cac:      	orr	w14, w13, w10
fffffe0007ae4cb0:      	ldurb	w13, [x29, #-219]
fffffe0007ae4cb4:      	orr	w14, w14, w11
fffffe0007ae4cb8:      	orr	w14, w14, w12
fffffe0007ae4cbc:      	orr	w14, w14, w13
fffffe0007ae4cc0:      	cbz	w14, 0xfffffe0007ae4e94 <IO80211Controller::start(IOService*)+0x370>
fffffe0007ae4cc4:      	eor	w8, w8, #0xff
fffffe0007ae4cc8:      	eor	w9, w9, #0xff
fffffe0007ae4ccc:      	orr	w8, w9, w8
fffffe0007ae4cd0:      	eor	w9, w10, #0xff
fffffe0007ae4cd4:      	orr	w8, w8, w9
fffffe0007ae4cd8:      	eor	w9, w11, #0xff
fffffe0007ae4cdc:      	orr	w8, w8, w9
fffffe0007ae4ce0:      	eor	w9, w12, #0xff
fffffe0007ae4ce4:      	orr	w8, w8, w9
fffffe0007ae4ce8:      	eor	w9, w13, #0xff
fffffe0007ae4cec:      	orr	w8, w8, w9
fffffe0007ae4cf0:      	cbz	w8, 0xfffffe0007ae4e94 <IO80211Controller::start(IOService*)+0x370>
fffffe0007ae4cf4:      	adrp	x0, 0xfffffe000713a000 <NiwlLifecycleClient::outputLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x38>
fffffe0007ae4cf8:      	add	x0, x0, #1961
fffffe0007ae4cfc:      	mov	x1, xzr
fffffe0007ae4d00:      	mov	x2, xzr
fffffe0007ae4d04:      	mov	x3, xzr
fffffe0007ae4d08:      	mov	x4, xzr
fffffe0007ae4d0c:      	bl	0xfffffe0007a1926c <IORegistryEntry::fromPath(char const*, IORegistryPlane const*, char*, int*, IORegistryEntry*)>
fffffe0007ae4d10:      	cbz	x0, 0xfffffe0007ae4d84 <IO80211Controller::start(IOService*)+0x260>
fffffe0007ae4d14:      	ldr	x8, [x0]
fffffe0007ae4d18:      	adrp	x1, 0xfffffe0007036000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x12c>
fffffe0007ae4d1c:      	add	x1, x1, #2984
fffffe0007ae4d20:      	mov	x22, x0
fffffe0007ae4d24:      	ldr	x8, [x8, #776]
fffffe0007ae4d28:      	blr	x8
fffffe0007ae4d2c:      	cbz	x0, 0xfffffe0007ae4d8c <IO80211Controller::start(IOService*)+0x268>
fffffe0007ae4d30:      	mov	x1, x23
fffffe0007ae4d34:      	mov	x24, x0
fffffe0007ae4d38:      	bl	0xfffffe00079b6860 <OSMetaClassBase::safeMetaCast(OSMetaClassBase const*, OSMetaClass const*)>
fffffe0007ae4d3c:      	cbz	x0, 0xfffffe0007ae4e44 <IO80211Controller::start(IOService*)+0x320>
fffffe0007ae4d40:      	ldr	x8, [x0]
fffffe0007ae4d44:      	mov	x23, x0
fffffe0007ae4d48:      	ldr	x8, [x8, #352]
fffffe0007ae4d4c:      	blr	x8
fffffe0007ae4d50:      	mov	w25, #3
fffffe0007ae4d54:      	cmp	w0, #6
fffffe0007ae4d58:      	b.ne	0xfffffe0007ae4e70 <IO80211Controller::start(IOService*)+0x34c>
fffffe0007ae4d5c:      	ldr	x8, [x23]
fffffe0007ae4d60:      	mov	x0, x23
fffffe0007ae4d64:      	ldr	x8, [x8, #408]
fffffe0007ae4d68:      	blr	x8
fffffe0007ae4d6c:      	sub	x1, x29, #224
fffffe0007ae4d70:      	mov	w2, #6
fffffe0007ae4d74:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe0007ae4d78:      	cmp	w0, #0
fffffe0007ae4d7c:      	csel	w25, wzr, w25, eq
fffffe0007ae4d80:      	b	0xfffffe0007ae4e70 <IO80211Controller::start(IOService*)+0x34c>
fffffe0007ae4d84:      	mov	w25, #2
fffffe0007ae4d88:      	b	0xfffffe0007ae4e94 <IO80211Controller::start(IOService*)+0x370>
fffffe0007ae4d8c:      	sub	x0, x29, #224
fffffe0007ae4d90:      	mov	w1, #6
fffffe0007ae4d94:      	bl	0xfffffe00079bbc38 <OSData::withBytes(void const*, unsigned int)>
fffffe0007ae4d98:      	ldr	x8, [x22]
fffffe0007ae4d9c:      	cbz	x0, 0xfffffe0007ae4e4c <IO80211Controller::start(IOService*)+0x328>
fffffe0007ae4da0:      	mov	x24, x0
fffffe0007ae4da4:      	ldr	x8, [x8, #648]
fffffe0007ae4da8:      	adrp	x1, 0xfffffe0007036000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x1bc>
fffffe0007ae4dac:      	add	x1, x1, #2984
fffffe0007ae4db0:      	mov	x0, x22
fffffe0007ae4db4:      	mov	x2, x24
fffffe0007ae4db8:      	blr	x8
fffffe0007ae4dbc:      	ldr	x8, [x24]
fffffe0007ae4dc0:      	mov	w25, w0
fffffe0007ae4dc4:      	mov	x0, x24
fffffe0007ae4dc8:      	ldr	x8, [x8, #40]
fffffe0007ae4dcc:      	blr	x8
fffffe0007ae4dd0:      	ldr	x8, [x22]
fffffe0007ae4dd4:      	cbz	w25, 0xfffffe0007ae4e54 <IO80211Controller::start(IOService*)+0x330>
fffffe0007ae4dd8:      	ldr	x8, [x8, #776]
fffffe0007ae4ddc:      	adrp	x1, 0xfffffe0007036000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x1f0>
fffffe0007ae4de0:      	add	x1, x1, #2984
fffffe0007ae4de4:      	mov	x0, x22
fffffe0007ae4de8:      	blr	x8
fffffe0007ae4dec:      	mov	x1, x23
fffffe0007ae4df0:      	mov	x24, x0
fffffe0007ae4df4:      	bl	0xfffffe00079b6860 <OSMetaClassBase::safeMetaCast(OSMetaClassBase const*, OSMetaClass const*)>
fffffe0007ae4df8:      	cbz	x0, 0xfffffe0007ae4e68 <IO80211Controller::start(IOService*)+0x344>
fffffe0007ae4dfc:      	ldr	x8, [x0]
fffffe0007ae4e00:      	mov	x23, x0
fffffe0007ae4e04:      	ldr	x8, [x8, #352]
fffffe0007ae4e08:      	blr	x8
fffffe0007ae4e0c:      	mov	w27, #6
fffffe0007ae4e10:      	cmp	w0, #6
fffffe0007ae4e14:      	mov	w25, w27
fffffe0007ae4e18:      	b.ne	0xfffffe0007ae4e6c <IO80211Controller::start(IOService*)+0x348>
fffffe0007ae4e1c:      	ldr	x8, [x23]
fffffe0007ae4e20:      	mov	x0, x23
fffffe0007ae4e24:      	ldr	x8, [x8, #408]
fffffe0007ae4e28:      	blr	x8
fffffe0007ae4e2c:      	sub	x1, x29, #224
fffffe0007ae4e30:      	mov	w2, #6
fffffe0007ae4e34:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe0007ae4e38:      	cmp	w0, #0
fffffe0007ae4e3c:      	csel	w25, wzr, w27, eq
fffffe0007ae4e40:      	b	0xfffffe0007ae4e6c <IO80211Controller::start(IOService*)+0x348>
fffffe0007ae4e44:      	mov	w25, #3
fffffe0007ae4e48:      	b	0xfffffe0007ae4e70 <IO80211Controller::start(IOService*)+0x34c>
fffffe0007ae4e4c:      	mov	w25, #4
fffffe0007ae4e50:      	b	0xfffffe0007ae4e58 <IO80211Controller::start(IOService*)+0x334>
fffffe0007ae4e54:      	mov	w25, #5
fffffe0007ae4e58:      	ldr	x8, [x8, #40]
fffffe0007ae4e5c:      	mov	x0, x22
fffffe0007ae4e60:      	blr	x8
fffffe0007ae4e64:      	b	0xfffffe0007ae4e94 <IO80211Controller::start(IOService*)+0x370>
fffffe0007ae4e68:      	mov	w25, #6
fffffe0007ae4e6c:      	cbz	x24, 0xfffffe0007ae4e80 <IO80211Controller::start(IOService*)+0x35c>
fffffe0007ae4e70:      	ldr	x8, [x24]
fffffe0007ae4e74:      	mov	x0, x24
fffffe0007ae4e78:      	ldr	x8, [x8, #40]
fffffe0007ae4e7c:      	blr	x8
fffffe0007ae4e80:      	ldr	x8, [x22]
fffffe0007ae4e84:      	mov	x0, x22
fffffe0007ae4e88:      	ldr	x8, [x8, #40]
fffffe0007ae4e8c:      	blr	x8
fffffe0007ae4e90:      	cbz	w25, 0xfffffe0007ae4eb8 <IO80211Controller::start(IOService*)+0x394>
fffffe0007ae4e94:      	adrp	x0, 0xfffffe0007151000 <NiwlLifecycleClient::outputLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x234>
fffffe0007ae4e98:      	add	x0, x0, #2180
fffffe0007ae4e9c:      	str	x25, [sp]
fffffe0007ae4ea0:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae4ea4:      	ldr	x8, [x26, #1544]
fffffe0007ae4ea8:      	mov	x0, x19
fffffe0007ae4eac:      	mov	x1, x20
fffffe0007ae4eb0:      	blr	x8
fffffe0007ae4eb4:      	b	0xfffffe0007ae4c00 <IO80211Controller::start(IOService*)+0xdc>
fffffe0007ae4eb8:      	adrp	x0, 0xfffffe0007151000 <NiwlLifecycleClient::outputLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x258>
fffffe0007ae4ebc:      	add	x0, x0, #2229
fffffe0007ae4ec0:      	add	x23, sp, #24
fffffe0007ae4ec4:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae4ec8:      	add	x8, sp, #24
fffffe0007ae4ecc:      	adrp	x11, 0xfffffe00075c3000 <IO80211Interface::~IO80211Interface()+0x4>
fffffe0007ae4ed0:      	movi.2d	v0, #0000000000000000
fffffe0007ae4ed4:      	add	x8, x8, #8
fffffe0007ae4ed8:      	mov	w12, #1
fffffe0007ae4edc:      	adr	x9, #1560
fffffe0007ae4ee0:      	nop
fffffe0007ae4ee4:      	adrp	x13, 0xfffffe00075c3000 <IO80211Interface::~IO80211Interface()+0x1c>
fffffe0007ae4ee8:      	mov	w22, wzr
fffffe0007ae4eec:      	adr	x10, #2044
fffffe0007ae4ef0:      	nop
fffffe0007ae4ef4:      	stp	q0, q0, [x8, #272]
fffffe0007ae4ef8:      	stp	q0, q0, [x8, #304]
fffffe0007ae4efc:      	mov	w27, #4628
fffffe0007ae4f00:      	str	q0, [x8, #256]
fffffe0007ae4f04:      	adrp	x8, 0xfffffe00070a0000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x4c0>
fffffe0007ae4f08:      	add	x8, x8, #2930
fffffe0007ae4f0c:      	stur	q0, [x23, #248]
fffffe0007ae4f10:      	stur	q0, [x23, #232]
fffffe0007ae4f14:      	stur	q0, [x23, #216]
fffffe0007ae4f18:      	stur	q0, [x23, #200]
fffffe0007ae4f1c:      	stur	q0, [x23, #184]
fffffe0007ae4f20:      	stur	q0, [x23, #168]
fffffe0007ae4f24:      	stur	q0, [x23, #152]
fffffe0007ae4f28:      	stur	q0, [x23, #136]
fffffe0007ae4f2c:      	mov	w23, #6
fffffe0007ae4f30:      	add	x11, x11, #1432
fffffe0007ae4f34:      	stp	q0, q0, [sp, #32]
fffffe0007ae4f38:      	str	w12, [sp, #32]
fffffe0007ae4f3c:      	mov	w12, #3
fffffe0007ae4f40:      	str	x8, [sp, #56]
fffffe0007ae4f44:      	adrp	x8, 0xfffffe00075c3000 <IO80211Interface::MetaClass::MetaClass()+0x2c>
fffffe0007ae4f48:      	stur	q0, [sp, #144]
fffffe0007ae4f4c:      	add	x8, x8, #384
fffffe0007ae4f50:      	add	x13, x13, #196
fffffe0007ae4f54:      	stur	q0, [sp, #80]
fffffe0007ae4f58:      	str	w12, [sp, #344]
fffffe0007ae4f5c:      	adrp	x12, 0xfffffe00075c3000 <IO80211Interface::MetaClass::alloc() const+0x10>
fffffe0007ae4f60:      	str	x9, [sp, #80]
fffffe0007ae4f64:      	adrp	x9, 0xfffffe00075c3000 <IO80211Interface::MetaClass::alloc() const+0x18>
fffffe0007ae4f68:      	stur	q0, [sp, #128]
fffffe0007ae4f6c:      	add	x9, x9, #2572
fffffe0007ae4f70:      	add	x12, x12, #2196
fffffe0007ae4f74:      	stp	x11, x8, [sp, #240]
fffffe0007ae4f78:      	adrp	x11, 0xfffffe0007019000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x318>
fffffe0007ae4f7c:      	adr	x8, #2340
fffffe0007ae4f80:      	nop
fffffe0007ae4f84:      	stp	q0, q0, [sp, #96]
fffffe0007ae4f88:      	stur	q0, [sp, #64]
fffffe0007ae4f8c:      	stp	x13, x9, [sp, #256]
fffffe0007ae4f90:      	ldr	d0, [x11, #2136]
fffffe0007ae4f94:      	adrp	x9, 0xfffffe0007008000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x2f0>
fffffe0007ae4f98:      	str	x8, [sp, #304]
fffffe0007ae4f9c:      	sub	x8, x29, #112
fffffe0007ae4fa0:      	adrp	x11, 0xfffffe0007036000 <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)+0x3b4>
fffffe0007ae4fa4:      	add	x11, x11, #2978
fffffe0007ae4fa8:      	str	d0, [sp, #24]
fffffe0007ae4fac:      	ldr	d0, [x9, #3888]
fffffe0007ae4fb0:      	add	x24, x8, #8
fffffe0007ae4fb4:      	orr	x25, x8, #0x2
fffffe0007ae4fb8:      	str	x12, [sp, #336]
fffffe0007ae4fbc:      	str	x10, [sp, #288]
fffffe0007ae4fc0:      	str	x11, [sp, #320]
fffffe0007ae4fc4:      	stur	d0, [sp, #68]
fffffe0007ae4fc8:      	str	w23, [sp, #328]
fffffe0007ae4fcc:      	b	0xfffffe0007ae4fe8 <IO80211Controller::start(IOService*)+0x4c4>
fffffe0007ae4fd0:      	ldr	x0, [x21]
fffffe0007ae4fd4:      	bl	0xfffffe00075f6b2c <_ifnet_release>
fffffe0007ae4fd8:      	str	xzr, [x21]
fffffe0007ae4fdc:      	add	w22, w22, #1
fffffe0007ae4fe0:      	cmp	w22, #32
fffffe0007ae4fe4:      	b.eq	0xfffffe0007ae5044 <IO80211Controller::start(IOService*)+0x520>
fffffe0007ae4fe8:      	add	x0, sp, #24
fffffe0007ae4fec:      	mov	x1, x21
fffffe0007ae4ff0:      	str	w22, [sp, #64]
fffffe0007ae4ff4:      	bl	0xfffffe00075f6254 <_ifnet_allocate_extended>
fffffe0007ae4ff8:      	cbnz	w0, 0xfffffe0007ae4fdc <IO80211Controller::start(IOService*)+0x4b8>
fffffe0007ae4ffc:      	ldr	x0, [x21]
fffffe0007ae5000:      	mov	w1, #6
fffffe0007ae5004:      	bl	0xfffffe00075f7764 <_ifnet_set_addrlen>
fffffe0007ae5008:      	ldr	x0, [x21]
fffffe0007ae500c:      	mov	w1, #14
fffffe0007ae5010:      	bl	0xfffffe00075f7788 <_ifnet_set_hdrlen>
fffffe0007ae5014:      	ldur	w8, [x29, #-224]
fffffe0007ae5018:      	sub	x1, x29, #112
fffffe0007ae501c:      	ldurh	w9, [x29, #-220]
fffffe0007ae5020:      	strh	wzr, [x25, #16]
fffffe0007ae5024:      	ldr	x0, [x21]
fffffe0007ae5028:      	stp	xzr, xzr, [x25]
fffffe0007ae502c:      	sturh	w27, [x29, #-112]
fffffe0007ae5030:      	sturb	w23, [x29, #-106]
fffffe0007ae5034:      	str	w8, [x24]
fffffe0007ae5038:      	strh	w9, [x24, #4]
fffffe0007ae503c:      	bl	0xfffffe00075bb394 <_ifnet_attach>
fffffe0007ae5040:      	cbnz	w0, 0xfffffe0007ae4fd0 <IO80211Controller::start(IOService*)+0x4ac>
fffffe0007ae5044:      	ldr	x0, [x21]
fffffe0007ae5048:      	cbz	x0, 0xfffffe0007ae5068 <IO80211Controller::start(IOService*)+0x544>
fffffe0007ae504c:      	sub	x1, x29, #224
fffffe0007ae5050:      	mov	w2, #6
fffffe0007ae5054:      	bl	0xfffffe00075f8aa0 <_ifnet_set_lladdr>
fffffe0007ae5058:      	cbz	w0, 0xfffffe0007ae5074 <IO80211Controller::start(IOService*)+0x550>
fffffe0007ae505c:      	adrp	x0, 0xfffffe0007151000 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x13c>
fffffe0007ae5060:      	add	x0, x0, #2333
fffffe0007ae5064:      	b	0xfffffe0007ae50d4 <IO80211Controller::start(IOService*)+0x5b0>
fffffe0007ae5068:      	adrp	x0, 0xfffffe0007151000 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x148>
fffffe0007ae506c:      	add	x0, x0, #2284
fffffe0007ae5070:      	b	0xfffffe0007ae4ea0 <IO80211Controller::start(IOService*)+0x37c>
fffffe0007ae5074:      	ldr	x0, [x21]
fffffe0007ae5078:      	stp	xzr, xzr, [x29, #-112]
fffffe0007ae507c:      	bl	0xfffffe00075f6b58 <_ifnet_name>
fffffe0007ae5080:      	mov	x22, x0
fffffe0007ae5084:      	ldr	x0, [x21]
fffffe0007ae5088:      	bl	0xfffffe00075f6b7c <_ifnet_unit>
fffffe0007ae508c:      	mov	w8, w0
fffffe0007ae5090:      	adrp	x2, 0xfffffe0007057000 <NiwlLifecycleClient::outputLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x44>
fffffe0007ae5094:      	add	x2, x2, #2814
fffffe0007ae5098:      	sub	x0, x29, #112
fffffe0007ae509c:      	mov	w1, #16
fffffe0007ae50a0:      	stp	x22, x8, [sp]
fffffe0007ae50a4:      	bl	0xfffffe000786d2c8 <_snprintf>
fffffe0007ae50a8:      	ldr	x0, [x21]
fffffe0007ae50ac:      	mov	w1, #1500
fffffe0007ae50b0:      	bl	0xfffffe00075f7730 <_ifnet_set_mtu>
fffffe0007ae50b4:      	cbnz	w0, 0xfffffe0007ae50cc <IO80211Controller::start(IOService*)+0x5a8>
fffffe0007ae50b8:      	ldr	x0, [x21]
fffffe0007ae50bc:      	mov	w1, #34818
fffffe0007ae50c0:      	mov	w2, #34883
fffffe0007ae50c4:      	bl	0xfffffe00075f6ba4 <_ifnet_set_flags>
fffffe0007ae50c8:      	cbz	w0, 0xfffffe0007ae50ec <IO80211Controller::start(IOService*)+0x5c8>
fffffe0007ae50cc:      	adrp	x0, 0xfffffe0007151000 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1ac>
fffffe0007ae50d0:      	add	x0, x0, #2382
fffffe0007ae50d4:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae50d8:      	ldr	x0, [x19, #136]
fffffe0007ae50dc:      	bl	0xfffffe00075bca8c <_ifnet_detach>
fffffe0007ae50e0:      	ldr	x8, [x26, #1544]
fffffe0007ae50e4:      	str	xzr, [x19, #136]
fffffe0007ae50e8:      	b	0xfffffe0007ae4ea8 <IO80211Controller::start(IOService*)+0x384>
fffffe0007ae50ec:      	ldr	x8, [x19]
fffffe0007ae50f0:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1d0>
fffffe0007ae50f4:      	add	x1, x1, #2431
fffffe0007ae50f8:      	sub	x2, x29, #224
fffffe0007ae50fc:      	mov	x0, x19
fffffe0007ae5100:      	mov	w3, #6
fffffe0007ae5104:      	ldr	x8, [x8, #680]
fffffe0007ae5108:      	blr	x8
fffffe0007ae510c:      	cbz	w0, 0xfffffe0007ae5388 <IO80211Controller::start(IOService*)+0x864>
fffffe0007ae5110:      	ldr	x8, [x19]
fffffe0007ae5114:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)>
fffffe0007ae5118:      	add	x1, x1, #2444
fffffe0007ae511c:      	mov	x0, x19
fffffe0007ae5120:      	mov	w2, #1
fffffe0007ae5124:      	ldr	x8, [x8, #664]
fffffe0007ae5128:      	blr	x8
fffffe0007ae512c:      	tbz	w0, #0, 0xfffffe0007ae5388 <IO80211Controller::start(IOService*)+0x864>
fffffe0007ae5130:      	adrp	x0, 0xfffffe00072d8000 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x1c4>
fffffe0007ae5134:      	add	x0, x0, #0
fffffe0007ae5138:      	mov	w1, #136
fffffe0007ae513c:      	bl	0xfffffe00079b8dc4 <_OSObject_typed_operator_new>
fffffe0007ae5140:      	bl	0xfffffe0007ae3d18 <IO80211SkywalkInterface::IO80211SkywalkInterface()>
fffffe0007ae5144:      	str	x0, [x19, #144]
fffffe0007ae5148:      	cbz	x0, 0xfffffe0007ae5394 <IO80211Controller::start(IOService*)+0x870>
fffffe0007ae514c:      	ldr	x8, [x0]
fffffe0007ae5150:      	mov	x1, xzr
fffffe0007ae5154:      	ldr	x8, [x8, #616]
fffffe0007ae5158:      	blr	x8
fffffe0007ae515c:      	tbz	w0, #0, 0xfffffe0007ae5394 <IO80211Controller::start(IOService*)+0x870>
fffffe0007ae5160:      	ldr	x0, [x19, #144]
fffffe0007ae5164:      	mov	x1, x19
fffffe0007ae5168:      	ldr	x8, [x0]
fffffe0007ae516c:      	ldr	x8, [x8, #1696]
fffffe0007ae5170:      	blr	x8
fffffe0007ae5174:      	tbz	w0, #0, 0xfffffe0007ae53ac <IO80211Controller::start(IOService*)+0x888>
fffffe0007ae5178:      	ldr	x0, [x19, #144]
fffffe0007ae517c:      	mov	x1, x19
fffffe0007ae5180:      	ldr	x8, [x0]
fffffe0007ae5184:      	ldr	x8, [x8, #1520]
fffffe0007ae5188:      	blr	x8
fffffe0007ae518c:      	tbz	w0, #0, 0xfffffe0007ae53bc <IO80211Controller::start(IOService*)+0x898>
fffffe0007ae5190:      	ldr	x0, [x19, #144]
fffffe0007ae5194:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)+0x80>
fffffe0007ae5198:      	add	x1, x1, #295
fffffe0007ae519c:      	sub	x2, x29, #112
fffffe0007ae51a0:      	ldr	x8, [x0]
fffffe0007ae51a4:      	ldr	x8, [x8, #656]
fffffe0007ae51a8:      	blr	x8
fffffe0007ae51ac:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae51b0:      	ldr	x0, [x19, #144]
fffffe0007ae51b4:      	adrp	x1, 0xfffffe000713b000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)+0x48>
fffffe0007ae51b8:      	add	x1, x1, #2096
fffffe0007ae51bc:      	sub	x2, x29, #112
fffffe0007ae51c0:      	ldr	x8, [x0]
fffffe0007ae51c4:      	ldr	x8, [x8, #656]
fffffe0007ae51c8:      	blr	x8
fffffe0007ae51cc:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae51d0:      	ldp	x0, x22, [x19, #136]
fffffe0007ae51d4:      	bl	0xfffffe00075f6b7c <_ifnet_unit>
fffffe0007ae51d8:      	ldr	x8, [x22]
fffffe0007ae51dc:      	mov	w2, w0
fffffe0007ae51e0:      	adrp	x1, 0xfffffe000714a000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)+0xb0>
fffffe0007ae51e4:      	add	x1, x1, #363
fffffe0007ae51e8:      	mov	x0, x22
fffffe0007ae51ec:      	mov	w3, #32
fffffe0007ae51f0:      	ldr	x8, [x8, #672]
fffffe0007ae51f4:      	blr	x8
fffffe0007ae51f8:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae51fc:      	ldr	x0, [x19, #144]
fffffe0007ae5200:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)+0xec>
fffffe0007ae5204:      	add	x1, x1, #2650
fffffe0007ae5208:      	mov	w2, #6
fffffe0007ae520c:      	mov	w3, #32
fffffe0007ae5210:      	ldr	x8, [x0]
fffffe0007ae5214:      	ldr	x8, [x8, #672]
fffffe0007ae5218:      	blr	x8
fffffe0007ae521c:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae5220:      	ldr	x0, [x19, #144]
fffffe0007ae5224:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)+0x110>
fffffe0007ae5228:      	add	x1, x1, #2666
fffffe0007ae522c:      	adrp	x2, 0xfffffe0007151000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)+0x118>
fffffe0007ae5230:      	add	x2, x2, #2687
fffffe0007ae5234:      	ldr	x8, [x0]
fffffe0007ae5238:      	ldr	x8, [x8, #656]
fffffe0007ae523c:      	blr	x8
fffffe0007ae5240:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae5244:      	ldr	x0, [x19, #144]
fffffe0007ae5248:      	adrp	x1, 0xfffffe0007149000 <NiwlLifecycleClient::clientMemoryForType(unsigned int, unsigned int*, IOMemoryDescriptor**)+0x114>
fffffe0007ae524c:      	add	x1, x1, #2934
fffffe0007ae5250:      	adrp	x2, 0xfffffe00070a0000 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x6c>
fffffe0007ae5254:      	add	x2, x2, #2930
fffffe0007ae5258:      	ldr	x8, [x0]
fffffe0007ae525c:      	ldr	x8, [x8, #656]
fffffe0007ae5260:      	blr	x8
fffffe0007ae5264:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae5268:      	ldr	x0, [x19, #144]
fffffe0007ae526c:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::clientClose()+0x10>
fffffe0007ae5270:      	add	x1, x1, #2444
fffffe0007ae5274:      	mov	w2, #1
fffffe0007ae5278:      	ldr	x8, [x0]
fffffe0007ae527c:      	ldr	x8, [x8, #664]
fffffe0007ae5280:      	blr	x8
fffffe0007ae5284:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae5288:      	ldr	x0, [x19, #144]
fffffe0007ae528c:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::clientClose()+0x30>
fffffe0007ae5290:      	add	x1, x1, #2702
fffffe0007ae5294:      	mov	w2, #1
fffffe0007ae5298:      	ldr	x8, [x0]
fffffe0007ae529c:      	ldr	x8, [x8, #664]
fffffe0007ae52a0:      	blr	x8
fffffe0007ae52a4:      	cbz	w0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae52a8:      	ldr	x0, [x19, #144]
fffffe0007ae52ac:      	adrp	x1, 0xfffffe0007151000 <NiwlLifecycleClient::clientDied()+0x18>
fffffe0007ae52b0:      	add	x1, x1, #2721
fffffe0007ae52b4:      	mov	w2, #1
fffffe0007ae52b8:      	mov	w3, #32
fffffe0007ae52bc:      	ldr	x8, [x0]
fffffe0007ae52c0:      	ldr	x8, [x8, #672]
fffffe0007ae52c4:      	blr	x8
fffffe0007ae52c8:      	tbz	w0, #0, 0xfffffe0007ae53cc <IO80211Controller::start(IOService*)+0x8a8>
fffffe0007ae52cc:      	bl	0xfffffe0007a207f8 <IOService::resources()>
fffffe0007ae52d0:      	mov	x22, x0
fffffe0007ae52d4:      	adrp	x0, 0xfffffe00072d8000 <NiwlLifecycleClient::eventLocked(unsigned int)+0x140>
fffffe0007ae52d8:      	add	x0, x0, #64
fffffe0007ae52dc:      	mov	w1, #200
fffffe0007ae52e0:      	bl	0xfffffe00079b8dc4 <_OSObject_typed_operator_new>
fffffe0007ae52e4:      	bl	0xfffffe0007ae3f58 <IONetworkStack::IONetworkStack()>
fffffe0007ae52e8:      	str	x0, [x19, #152]
fffffe0007ae52ec:      	cbz	x0, 0xfffffe0007ae5424 <IO80211Controller::start(IOService*)+0x900>
fffffe0007ae52f0:      	cbz	x22, 0xfffffe0007ae5424 <IO80211Controller::start(IOService*)+0x900>
fffffe0007ae52f4:      	ldp	x2, x1, [x19, #136]
fffffe0007ae52f8:      	bl	0xfffffe0007ae58a4 <IONetworkStack::initOwned(IOService*, __ifnet*)>
fffffe0007ae52fc:      	cbz	w0, 0xfffffe0007ae5420 <IO80211Controller::start(IOService*)+0x8fc>
fffffe0007ae5300:      	ldr	x0, [x19, #152]
fffffe0007ae5304:      	mov	x1, x22
fffffe0007ae5308:      	ldr	x8, [x0]
fffffe0007ae530c:      	ldr	x8, [x8, #1696]
fffffe0007ae5310:      	blr	x8
fffffe0007ae5314:      	cbz	w0, 0xfffffe0007ae5420 <IO80211Controller::start(IOService*)+0x8fc>
fffffe0007ae5318:      	ldr	x0, [x19, #152]
fffffe0007ae531c:      	mov	x1, x22
fffffe0007ae5320:      	ldr	x8, [x0]
fffffe0007ae5324:      	ldr	x8, [x8, #1520]
fffffe0007ae5328:      	blr	x8
fffffe0007ae532c:      	tbz	w0, #0, 0xfffffe0007ae543c <IO80211Controller::start(IOService*)+0x918>
fffffe0007ae5330:      	ldr	x0, [x21]
fffffe0007ae5334:      	bl	0xfffffe0007ae59cc <nwil_bind(__ifnet*)>
fffffe0007ae5338:      	cbz	w0, 0xfffffe0007ae545c <IO80211Controller::start(IOService*)+0x938>
fffffe0007ae533c:      	ldr	x0, [x19, #152]
fffffe0007ae5340:      	bl	0xfffffe0007ae5a7c <IONetworkStack::deactivate()>
fffffe0007ae5344:      	ldr	x0, [x19, #152]
fffffe0007ae5348:      	mov	w1, wzr
fffffe0007ae534c:      	ldr	x8, [x0]
fffffe0007ae5350:      	ldr	x8, [x8, #1584]
fffffe0007ae5354:      	blr	x8
fffffe0007ae5358:      	ldr	x0, [x19, #152]
fffffe0007ae535c:      	mov	x1, x22
fffffe0007ae5360:      	ldr	x8, [x0]
fffffe0007ae5364:      	ldr	x8, [x8, #1704]
fffffe0007ae5368:      	blr	x8
fffffe0007ae536c:      	ldr	x0, [x19, #152]
fffffe0007ae5370:      	ldr	x8, [x0]
fffffe0007ae5374:      	ldr	x8, [x8, #40]
fffffe0007ae5378:      	blr	x8
fffffe0007ae537c:      	ldr	x0, [x19, #144]
fffffe0007ae5380:      	str	xzr, [x19, #152]
fffffe0007ae5384:      	b	0xfffffe0007ae53dc <IO80211Controller::start(IOService*)+0x8b8>
fffffe0007ae5388:      	adrp	x0, 0xfffffe0007151000 <NiwlLifecycleClient::free()+0x84>
fffffe0007ae538c:      	add	x0, x0, #2454
fffffe0007ae5390:      	b	0xfffffe0007ae50d4 <IO80211Controller::start(IOService*)+0x5b0>
fffffe0007ae5394:      	adrp	x0, 0xfffffe0007151000 <NiwlLifecycleClient::bindInterface(char const*)+0x4>
fffffe0007ae5398:      	add	x0, x0, #2503
fffffe0007ae539c:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae53a0:      	ldr	x0, [x19, #144]
fffffe0007ae53a4:      	cbnz	x0, 0xfffffe0007ae5404 <IO80211Controller::start(IOService*)+0x8e0>
fffffe0007ae53a8:      	b	0xfffffe0007ae5410 <IO80211Controller::start(IOService*)+0x8ec>
fffffe0007ae53ac:      	adrp	x0, 0xfffffe0007151000 <_niwl_create_client+0xc>
fffffe0007ae53b0:      	add	x0, x0, #2552
fffffe0007ae53b4:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae53b8:      	b	0xfffffe0007ae5400 <IO80211Controller::start(IOService*)+0x8dc>
fffffe0007ae53bc:      	adrp	x0, 0xfffffe0007151000 <_niwl_create_client+0x1c>
fffffe0007ae53c0:      	add	x0, x0, #2601
fffffe0007ae53c4:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae53c8:      	b	0xfffffe0007ae53ec <IO80211Controller::start(IOService*)+0x8c8>
fffffe0007ae53cc:      	adrp	x0, 0xfffffe0007151000 <_niwl_create_client+0x2c>
fffffe0007ae53d0:      	add	x0, x0, #2738
fffffe0007ae53d4:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae53d8:      	ldr	x0, [x19, #144]
fffffe0007ae53dc:      	ldr	x8, [x0]
fffffe0007ae53e0:      	mov	x1, x19
fffffe0007ae53e4:      	ldr	x8, [x8, #1528]
fffffe0007ae53e8:      	blr	x8
fffffe0007ae53ec:      	ldr	x0, [x19, #144]
fffffe0007ae53f0:      	mov	x1, x19
fffffe0007ae53f4:      	ldr	x8, [x0]
fffffe0007ae53f8:      	ldr	x8, [x8, #1704]
fffffe0007ae53fc:      	blr	x8
fffffe0007ae5400:      	ldr	x0, [x19, #144]
fffffe0007ae5404:      	ldr	x8, [x0]
fffffe0007ae5408:      	ldr	x8, [x8, #40]
fffffe0007ae540c:      	blr	x8
fffffe0007ae5410:      	ldr	x0, [x19, #136]
fffffe0007ae5414:      	str	xzr, [x19, #144]
fffffe0007ae5418:      	b	0xfffffe0007ae50dc <IO80211Controller::start(IOService*)+0x5b8>
fffffe0007ae541c:      	bl	0xfffffe00079fc274 <___stack_chk_fail>
fffffe0007ae5420:      	ldr	x0, [x19, #152]
fffffe0007ae5424:      	cbz	x0, 0xfffffe0007ae53d8 <IO80211Controller::start(IOService*)+0x8b4>
fffffe0007ae5428:      	ldr	x8, [x0]
fffffe0007ae542c:      	ldr	x8, [x8, #40]
fffffe0007ae5430:      	blr	x8
fffffe0007ae5434:      	str	xzr, [x19, #152]
fffffe0007ae5438:      	b	0xfffffe0007ae53d8 <IO80211Controller::start(IOService*)+0x8b4>
fffffe0007ae543c:      	ldr	x0, [x19, #152]
fffffe0007ae5440:      	cbz	x0, 0xfffffe0007ae53d8 <IO80211Controller::start(IOService*)+0x8b4>
fffffe0007ae5444:      	ldr	x8, [x0]
fffffe0007ae5448:      	mov	x1, x22
fffffe0007ae544c:      	ldr	x8, [x8, #1704]
fffffe0007ae5450:      	blr	x8
fffffe0007ae5454:      	ldr	x0, [x19, #152]
fffffe0007ae5458:      	b	0xfffffe0007ae5428 <IO80211Controller::start(IOService*)+0x904>
fffffe0007ae545c:      	ldr	x0, [x19, #136]
fffffe0007ae5460:      	mov	w1, #1
fffffe0007ae5464:      	mov	w2, #14
fffffe0007ae5468:      	mov	w22, #1
fffffe0007ae546c:      	bl	0xfffffe0007595ee0 <_bpfattach>
fffffe0007ae5470:      	ldr	x0, [x19, #144]
fffffe0007ae5474:      	mov	w1, wzr
fffffe0007ae5478:      	ldr	x8, [x0]
fffffe0007ae547c:      	ldr	x8, [x8, #1504]
fffffe0007ae5480:      	blr	x8
fffffe0007ae5484:      	ldr	x0, [x19, #152]
fffffe0007ae5488:      	mov	w1, wzr
fffffe0007ae548c:      	ldr	x8, [x0]
fffffe0007ae5490:      	ldr	x8, [x8, #1504]
fffffe0007ae5494:      	blr	x8
fffffe0007ae5498:      	ldr	x8, [x19]
fffffe0007ae549c:      	mov	x0, x19
fffffe0007ae54a0:      	mov	w1, wzr
fffffe0007ae54a4:      	ldr	x8, [x8, #1504]
fffffe0007ae54a8:      	blr	x8
fffffe0007ae54ac:      	ldr	x0, [x19, #136]
fffffe0007ae54b0:      	bl	0xfffffe00075f6b7c <_ifnet_unit>
fffffe0007ae54b4:      	mov	w20, w0
fffffe0007ae54b8:      	bl	0xfffffe0007ae5ab8 <niws_ms()>
fffffe0007ae54bc:      	mov	x8, x0
fffffe0007ae54c0:      	adrp	x0, 0xfffffe0007151000 <_niwl_create_client+0x120>
fffffe0007ae54c4:      	add	x0, x0, #2787
fffffe0007ae54c8:      	stp	x20, x8, [sp]
fffffe0007ae54cc:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae54d0:      	ldr	x0, [x19, #136]
fffffe0007ae54d4:      	bl	0xfffffe00075f6b7c <_ifnet_unit>
fffffe0007ae54d8:      	mov	w8, w0
fffffe0007ae54dc:      	mov	w9, #128
fffffe0007ae54e0:      	adrp	x0, 0xfffffe0007151000 <_niwl_create_client+0x140>
fffffe0007ae54e4:      	add	x0, x0, #2851
fffffe0007ae54e8:      	stp	x8, x9, [sp]
fffffe0007ae54ec:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae54f0:      	b	0xfffffe0007ae4c04 <IO80211Controller::start(IOService*)+0xe0>
