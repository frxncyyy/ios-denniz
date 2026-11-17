
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007ae2ce0 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)>:
fffffe0007ae2ce0:      	sub	sp, sp, #352
fffffe0007ae2ce4:      	stp	x28, x27, [sp, #256]
fffffe0007ae2ce8:      	stp	x26, x25, [sp, #272]
fffffe0007ae2cec:      	stp	x24, x23, [sp, #288]
fffffe0007ae2cf0:      	stp	x22, x21, [sp, #304]
fffffe0007ae2cf4:      	stp	x20, x19, [sp, #320]
fffffe0007ae2cf8:      	stp	x29, x30, [sp, #336]
fffffe0007ae2cfc:      	add	x29, sp, #336
fffffe0007ae2d00:      	adrp	x8, 0xfffffe0007c8c000 <NiwlLifecycleClient::bindInterface(char const*)+0xc>
fffffe0007ae2d04:      	mov	x25, x5
fffffe0007ae2d08:      	movi.2d	v0, #0000000000000000
fffffe0007ae2d0c:      	mov	x26, x4
fffffe0007ae2d10:      	mov	x27, x3
fffffe0007ae2d14:      	mov	x21, x2
fffffe0007ae2d18:      	nop
fffffe0007ae2d1c:      	mov	w19, w1
fffffe0007ae2d20:      	mov	x20, x0
fffffe0007ae2d24:      	ldr	x8, [x8, #3376]
fffffe0007ae2d28:      	stur	x8, [x29, #-96]
fffffe0007ae2d2c:      	stp	q0, q0, [x29, #-160]
fffffe0007ae2d30:      	stp	q0, q0, [x29, #-128]
fffffe0007ae2d34:      	cbz	x2, 0xfffffe0007ae2d58 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x78>
fffffe0007ae2d38:      	ldr	x8, [x21, #16]
fffffe0007ae2d3c:      	cbz	x8, 0xfffffe0007ae2d58 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x78>
fffffe0007ae2d40:      	ldr	w9, [x21, #24]
fffffe0007ae2d44:      	cbz	w9, 0xfffffe0007ae2d58 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x78>
fffffe0007ae2d48:      	ldr	x8, [x8]
fffffe0007ae2d4c:      	mov	w12, #1
fffffe0007ae2d50:      	stur	x8, [x29, #-160]
fffffe0007ae2d54:      	b	0xfffffe0007ae2d5c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x7c>
fffffe0007ae2d58:      	mov	w12, wzr
fffffe0007ae2d5c:      	ldr	x28, [x20, #232]
fffffe0007ae2d60:      	strb	wzr, [sp, #159]
fffffe0007ae2d64:      	cbz	x28, 0xfffffe0007ae2dbc <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0xdc>
fffffe0007ae2d68:      	cmp	w19, #6
fffffe0007ae2d6c:      	b.eq	0xfffffe0007ae2d74 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x94>
fffffe0007ae2d70:      	cbnz	w19, 0xfffffe0007ae2dbc <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0xdc>
fffffe0007ae2d74:      	ldr	w10, [x20, #240]
fffffe0007ae2d78:      	cmn	w10, #1
fffffe0007ae2d7c:      	b.eq	0xfffffe0007ae2dbc <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0xdc>
fffffe0007ae2d80:      	mov	w8, wzr
fffffe0007ae2d84:      	mov	w9, w10
fffffe0007ae2d88:      	add	w23, w10, #1
fffffe0007ae2d8c:      	add	x11, x20, #240
fffffe0007ae2d90:      	cas	w9, w23, [x11]
fffffe0007ae2d94:      	cmp	w9, w10
fffffe0007ae2d98:      	b.eq	0xfffffe0007ae2dc0 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0xe0>
fffffe0007ae2d9c:      	mov	w23, wzr
fffffe0007ae2da0:      	cmp	w8, #14
fffffe0007ae2da4:      	b.hi	0xfffffe0007ae2dc0 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0xe0>
fffffe0007ae2da8:      	add	w8, w8, #1
fffffe0007ae2dac:      	mov	w10, w9
fffffe0007ae2db0:      	cmn	w9, #1
fffffe0007ae2db4:      	b.ne	0xfffffe0007ae2d88 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0xa8>
fffffe0007ae2db8:      	b	0xfffffe0007ae2dc0 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0xe0>
fffffe0007ae2dbc:      	mov	w23, wzr
fffffe0007ae2dc0:      	mov	w22, #706
fffffe0007ae2dc4:      	ldr	x0, [x20, #224]
fffffe0007ae2dc8:      	movk	w22, #57344, lsl #16
fffffe0007ae2dcc:      	cbz	x0, 0xfffffe0007ae2e38 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x158>
fffffe0007ae2dd0:      	mov	w24, #16960
fffffe0007ae2dd4:      	str	w12, [sp, #124]
fffffe0007ae2dd8:      	movk	w24, #15, lsl #16
fffffe0007ae2ddc:      	cbz	w23, 0xfffffe0007ae2e40 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x160>
fffffe0007ae2de0:      	add	x0, sp, #168
fffffe0007ae2de4:      	stp	xzr, xzr, [sp, #160]
fffffe0007ae2de8:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe0007ae2dec:      	ldr	x0, [sp, #168]
fffffe0007ae2df0:      	add	x1, sp, #160
fffffe0007ae2df4:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe0007ae2df8:      	ldr	x8, [sp, #160]
fffffe0007ae2dfc:      	ldr	x0, [x20, #224]
fffffe0007ae2e00:      	udiv	x8, x8, x24
fffffe0007ae2e04:      	str	x8, [sp, #144]
fffffe0007ae2e08:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe0007ae2e0c:      	add	x0, sp, #168
fffffe0007ae2e10:      	stp	xzr, xzr, [sp, #160]
fffffe0007ae2e14:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe0007ae2e18:      	ldr	x0, [sp, #168]
fffffe0007ae2e1c:      	add	x1, sp, #160
fffffe0007ae2e20:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe0007ae2e24:      	ldr	x8, [sp, #160]
fffffe0007ae2e28:      	mov	w9, #16960
fffffe0007ae2e2c:      	movk	w9, #15, lsl #16
fffffe0007ae2e30:      	udiv	x24, x8, x9
fffffe0007ae2e34:      	b	0xfffffe0007ae2e4c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x16c>
fffffe0007ae2e38:      	add	w19, w22, #22
fffffe0007ae2e3c:      	b	0xfffffe0007ae30b0 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x3d0>
fffffe0007ae2e40:      	bl	0xfffffe000734dcc4 <_lck_mtx_lock>
fffffe0007ae2e44:      	str	xzr, [sp, #144]
fffffe0007ae2e48:      	mov	x24, xzr
fffffe0007ae2e4c:      	cmp	w19, #4
fffffe0007ae2e50:      	str	wzr, [x20, #244]
fffffe0007ae2e54:      	b.ne	0xfffffe0007ae2e6c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x18c>
fffffe0007ae2e58:      	ldr	x9, [x20, #368]
fffffe0007ae2e5c:      	ldr	x8, [x20, #984]
fffffe0007ae2e60:      	stp	x9, x8, [sp, #128]
fffffe0007ae2e64:      	cbz	x27, 0xfffffe0007ae2e7c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x19c>
fffffe0007ae2e68:      	b	0xfffffe0007ae2e9c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1bc>
fffffe0007ae2e6c:      	mov	x9, xzr
fffffe0007ae2e70:      	mov	x8, xzr
fffffe0007ae2e74:      	stp	x9, x8, [sp, #128]
fffffe0007ae2e78:      	cbnz	x27, 0xfffffe0007ae2e9c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1bc>
fffffe0007ae2e7c:      	cbnz	x26, 0xfffffe0007ae2e9c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1bc>
fffffe0007ae2e80:      	cbnz	x25, 0xfffffe0007ae2e9c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x1bc>
fffffe0007ae2e84:      	add	x3, sp, #159
fffffe0007ae2e88:      	mov	x0, x20
fffffe0007ae2e8c:      	mov	w1, w19
fffffe0007ae2e90:      	mov	x2, x21
fffffe0007ae2e94:      	bl	0xfffffe0007ae2a24 <NiwlLifecycleClient::outputLocked(unsigned int, IOExternalMethodArguments*, bool*)>
fffffe0007ae2e98:      	mov	w22, w0
fffffe0007ae2e9c:      	str	x24, [sp, #104]
fffffe0007ae2ea0:      	cbz	w23, 0xfffffe0007ae2f60 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x280>
fffffe0007ae2ea4:      	add	x0, sp, #168
fffffe0007ae2ea8:      	stp	xzr, xzr, [sp, #160]
fffffe0007ae2eac:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe0007ae2eb0:      	ldr	x0, [sp, #168]
fffffe0007ae2eb4:      	add	x1, sp, #160
fffffe0007ae2eb8:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe0007ae2ebc:      	ldr	x8, [sp, #160]
fffffe0007ae2ec0:      	mov	w9, #16960
fffffe0007ae2ec4:      	movk	w9, #15, lsl #16
fffffe0007ae2ec8:      	ldr	w27, [x20, #244]
fffffe0007ae2ecc:      	udiv	x12, x8, x9
fffffe0007ae2ed0:      	ldr	x8, [sp, #144]
fffffe0007ae2ed4:      	subs	x8, x24, x8
fffffe0007ae2ed8:      	cset	w10, hs
fffffe0007ae2edc:      	subs	x9, x12, x24
fffffe0007ae2ee0:      	cset	w11, hs
fffffe0007ae2ee4:      	ands	w25, w10, w11
fffffe0007ae2ee8:      	str	x12, [sp, #112]
fffffe0007ae2eec:      	csel	x10, x8, xzr, ne
fffffe0007ae2ef0:      	csel	x11, x9, xzr, ne
fffffe0007ae2ef4:      	cmp	w25, #1
fffffe0007ae2ef8:      	b.ne	0xfffffe0007ae2f0c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x22c>
fffffe0007ae2efc:      	cmp	x10, #49
fffffe0007ae2f00:      	b.hi	0xfffffe0007ae2fb4 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2d4>
fffffe0007ae2f04:      	cmp	x11, #50
fffffe0007ae2f08:      	b.hs	0xfffffe0007ae2fb4 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2d4>
fffffe0007ae2f0c:      	stp	x11, x10, [sp, #88]
fffffe0007ae2f10:      	cbnz	w22, 0xfffffe0007ae2f40 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x260>
fffffe0007ae2f14:      	mov	w24, wzr
fffffe0007ae2f18:      	mov	w26, #1
fffffe0007ae2f1c:      	sub	w8, w27, #1
fffffe0007ae2f20:      	cmp	w8, #206
fffffe0007ae2f24:      	b.hi	0xfffffe0007ae2f78 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x298>
fffffe0007ae2f28:      	adrp	x9, 0xfffffe0007036000 <_nbg_i2c_read+0xef0>
fffffe0007ae2f2c:      	add	x9, x9, #1847
fffffe0007ae2f30:      	adr	x10, #16
fffffe0007ae2f34:      	ldrb	w11, [x9, x8]
fffffe0007ae2f38:      	add	x10, x10, x11, lsl #2
fffffe0007ae2f3c:      	br	x10
fffffe0007ae2f40:      	ldr	w8, [x20, #248]
fffffe0007ae2f44:      	cmp	w8, #63
fffffe0007ae2f48:      	b.hi	0xfffffe0007ae2f70 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x290>
fffffe0007ae2f4c:      	mov	w26, wzr
fffffe0007ae2f50:      	add	w8, w8, #1
fffffe0007ae2f54:      	mov	w24, #1
fffffe0007ae2f58:      	str	w8, [x20, #248]
fffffe0007ae2f5c:      	b	0xfffffe0007ae2f78 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x298>
fffffe0007ae2f60:      	str	xzr, [sp, #112]
fffffe0007ae2f64:      	mov	w27, wzr
fffffe0007ae2f68:      	stp	xzr, xzr, [sp, #88]
fffffe0007ae2f6c:      	mov	w25, wzr
fffffe0007ae2f70:      	mov	w24, wzr
fffffe0007ae2f74:      	mov	w26, #1
fffffe0007ae2f78:      	sub	w8, w19, #1
fffffe0007ae2f7c:      	cmp	w8, #4
fffffe0007ae2f80:      	b.hi	0xfffffe0007ae3028 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x348>
fffffe0007ae2f84:      	ldr	w8, [x20, #388]
fffffe0007ae2f88:      	add	w9, w8, #1
fffffe0007ae2f8c:      	cmp	w8, #31
fffffe0007ae2f90:      	str	w9, [x20, #388]
fffffe0007ae2f94:      	b.hi	0xfffffe0007ae3028 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x348>
fffffe0007ae2f98:      	ldr	w8, [x20, #256]
fffffe0007ae2f9c:      	str	x8, [sp, #80]
fffffe0007ae2fa0:      	cbz	x21, 0xfffffe0007ae2fdc <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x2fc>
fffffe0007ae2fa4:      	ldr	w8, [x21, #96]
fffffe0007ae2fa8:      	ldr	w21, [x21, #24]
fffffe0007ae2fac:      	str	x8, [sp, #72]
fffffe0007ae2fb0:      	b	0xfffffe0007ae2fe0 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x300>
fffffe0007ae2fb4:      	ldr	w10, [x20, #252]
fffffe0007ae2fb8:      	cmp	w10, #15
fffffe0007ae2fbc:      	b.hi	0xfffffe0007ae310c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x42c>
fffffe0007ae2fc0:      	add	w10, w10, #1
fffffe0007ae2fc4:      	mov	w26, wzr
fffffe0007ae2fc8:      	mov	w24, #2
fffffe0007ae2fcc:      	mov	w25, #1
fffffe0007ae2fd0:      	stp	x9, x8, [sp, #88]
fffffe0007ae2fd4:      	str	w10, [x20, #252]
fffffe0007ae2fd8:      	b	0xfffffe0007ae2f78 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x298>
fffffe0007ae2fdc:      	str	xzr, [sp, #72]
fffffe0007ae2fe0:      	add	x0, sp, #168
fffffe0007ae2fe4:      	stp	xzr, xzr, [sp, #160]
fffffe0007ae2fe8:      	bl	0xfffffe00073335cc <_clock_get_uptime>
fffffe0007ae2fec:      	ldr	x0, [sp, #168]
fffffe0007ae2ff0:      	add	x1, sp, #160
fffffe0007ae2ff4:      	bl	0xfffffe00074b0854 <_absolutetime_to_nanoseconds>
fffffe0007ae2ff8:      	ldr	x8, [sp, #160]
fffffe0007ae2ffc:      	mov	w9, #16960
fffffe0007ae3000:      	movk	w9, #15, lsl #16
fffffe0007ae3004:      	adrp	x0, 0xfffffe0007150000 <rd(void*, unsigned int, unsigned int, unsigned int*)+0x4c>
fffffe0007ae3008:      	add	x0, x0, #3848
fffffe0007ae300c:      	udiv	x8, x8, x9
fffffe0007ae3010:      	stp	x21, x8, [sp, #32]
fffffe0007ae3014:      	ldr	x8, [sp, #72]
fffffe0007ae3018:      	stp	x22, x8, [sp, #16]
fffffe0007ae301c:      	ldr	x8, [sp, #80]
fffffe0007ae3020:      	stp	x19, x8, [sp]
fffffe0007ae3024:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae3028:      	ldr	x0, [x20, #224]
fffffe0007ae302c:      	bl	0xfffffe000734dfb4 <_lck_mtx_unlock>
fffffe0007ae3030:      	tbnz	w26, #0, 0xfffffe0007ae3084 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x3a4>
fffffe0007ae3034:      	ldr	x9, [sp, #104]
fffffe0007ae3038:      	adrp	x0, 0xfffffe0007150000 <rd(void*, unsigned int, unsigned int, unsigned int*)+0x80>
fffffe0007ae303c:      	add	x0, x0, #3930
fffffe0007ae3040:      	ldr	x8, [sp, #144]
fffffe0007ae3044:      	and	w20, w25, #0x1
fffffe0007ae3048:      	stp	x23, x19, [sp, #8]
fffffe0007ae304c:      	str	x28, [sp]
fffffe0007ae3050:      	stp	x8, x9, [sp, #24]
fffffe0007ae3054:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae3058:      	ldp	x8, x9, [sp, #88]
fffffe0007ae305c:      	adrp	x0, 0xfffffe0007150000 <wr(void*, unsigned int, unsigned int, unsigned int)+0x10>
fffffe0007ae3060:      	add	x0, x0, #4021
fffffe0007ae3064:      	stp	x20, x24, [sp, #56]
fffffe0007ae3068:      	stp	x23, x27, [sp, #8]
fffffe0007ae306c:      	str	x28, [sp]
fffffe0007ae3070:      	stp	x9, x8, [sp, #40]
fffffe0007ae3074:      	mov	w8, w22
fffffe0007ae3078:      	ldr	x9, [sp, #112]
fffffe0007ae307c:      	stp	x8, x9, [sp, #24]
fffffe0007ae3080:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae3084:      	mov	w19, w22
fffffe0007ae3088:      	ldr	x0, [sp, #128]
fffffe0007ae308c:      	cbz	x0, 0xfffffe0007ae309c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x3bc>
fffffe0007ae3090:      	mov	w19, w22
fffffe0007ae3094:      	cbnz	w22, 0xfffffe0007ae309c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x3bc>
fffffe0007ae3098:      	bl	0xfffffe000750de74 <_nord2_wifi_scan_cancel>
fffffe0007ae309c:      	ldr	x0, [sp, #136]
fffffe0007ae30a0:      	cbz	x0, 0xfffffe0007ae30ac <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x3cc>
fffffe0007ae30a4:      	cbnz	w22, 0xfffffe0007ae30ac <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x3cc>
fffffe0007ae30a8:      	bl	0xfffffe000750e464 <_nord2_wifi_join_cancel>
fffffe0007ae30ac:      	ldr	w12, [sp, #124]
fffffe0007ae30b0:      	ldrb	w8, [sp, #159]
fffffe0007ae30b4:      	cmp	w8, #0
fffffe0007ae30b8:      	cset	w8, eq
fffffe0007ae30bc:      	and	w8, w12, w8
fffffe0007ae30c0:      	cmp	w8, #1
fffffe0007ae30c4:      	b.ne	0xfffffe0007ae30d0 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x3f0>
fffffe0007ae30c8:      	sub	x0, x29, #160
fffffe0007ae30cc:      	bl	0xfffffe0007a7810c <IOUserClient::releaseAsyncReference64(unsigned long long*)>
fffffe0007ae30d0:      	adrp	x8, 0xfffffe0007c8c000 <NiwlLifecycleClient::completed(void*, unsigned long long, unsigned long long, nss_status)+0x1e0>
fffffe0007ae30d4:      	nop
fffffe0007ae30d8:      	ldur	x9, [x29, #-96]
fffffe0007ae30dc:      	ldr	x8, [x8, #3376]
fffffe0007ae30e0:      	cmp	x8, x9
fffffe0007ae30e4:      	b.ne	0xfffffe0007ae311c <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x43c>
fffffe0007ae30e8:      	mov	w0, w19
fffffe0007ae30ec:      	ldp	x29, x30, [sp, #336]
fffffe0007ae30f0:      	ldp	x20, x19, [sp, #320]
fffffe0007ae30f4:      	ldp	x22, x21, [sp, #304]
fffffe0007ae30f8:      	ldp	x24, x23, [sp, #288]
fffffe0007ae30fc:      	ldp	x26, x25, [sp, #272]
fffffe0007ae3100:      	ldp	x28, x27, [sp, #256]
fffffe0007ae3104:      	add	sp, sp, #352
fffffe0007ae3108:      	ret
fffffe0007ae310c:      	mov	w24, wzr
fffffe0007ae3110:      	mov	w25, #1
fffffe0007ae3114:      	stp	x9, x8, [sp, #88]
fffffe0007ae3118:      	b	0xfffffe0007ae2f74 <NiwlLifecycleClient::externalMethod(unsigned int, IOExternalMethodArguments*, IOExternalMethodDispatch*, OSObject*, void*)+0x294>
fffffe0007ae311c:      	bl	0xfffffe00079fc4f4 <___stack_chk_fail>
