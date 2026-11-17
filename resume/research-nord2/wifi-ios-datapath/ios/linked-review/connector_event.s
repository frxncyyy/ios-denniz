
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b9c7c <_nwn_event>:
fffffe00079b9c7c:      	sub	sp, sp, #144
fffffe00079b9c80:      	stp	x26, x25, [sp, #64]
fffffe00079b9c84:      	stp	x24, x23, [sp, #80]
fffffe00079b9c88:      	stp	x22, x21, [sp, #96]
fffffe00079b9c8c:      	stp	x20, x19, [sp, #112]
fffffe00079b9c90:      	stp	x29, x30, [sp, #128]
fffffe00079b9c94:      	add	x29, sp, #128
fffffe00079b9c98:      	cbz	x0, 0xfffffe00079b9ea0 <_nwn_event+0x224>
fffffe00079b9c9c:      	mov	x20, x2
fffffe00079b9ca0:      	mov	x19, x0
fffffe00079b9ca4:      	mov	x22, x1
fffffe00079b9ca8:      	str	x2, [x0, #4848]
fffffe00079b9cac:      	cbz	x1, 0xfffffe00079b9e3c <_nwn_event+0x1c0>
fffffe00079b9cb0:      	ldr	x1, [x22, #8]
fffffe00079b9cb4:      	ldr	x8, [x19, #1272]
fffffe00079b9cb8:      	cmp	x1, x8
fffffe00079b9cbc:      	b.ne	0xfffffe00079b9e3c <_nwn_event+0x1c0>
fffffe00079b9cc0:      	ldr	w8, [x22]
fffffe00079b9cc4:      	mov	w21, wzr
fffffe00079b9cc8:      	sub	w8, w8, #1
fffffe00079b9ccc:      	cmp	w8, #14
fffffe00079b9cd0:      	b.hi	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9cd4:      	mov	w9, #4824
fffffe00079b9cd8:      	add	x24, x19, x9
fffffe00079b9cdc:      	adrp	x9, 0xfffffe0007031000 <_group_receive+0x93c>
fffffe00079b9ce0:      	add	x9, x9, #1392
fffffe00079b9ce4:      	adr	x10, #16
fffffe00079b9ce8:      	ldrb	w11, [x9, x8]
fffffe00079b9cec:      	add	x10, x10, x11, lsl #2
fffffe00079b9cf0:      	br	x10
fffffe00079b9cf4:      	ldr	x2, [x22, #16]
fffffe00079b9cf8:      	ldr	x8, [x19, #4776]
fffffe00079b9cfc:      	cmp	x2, x8
fffffe00079b9d00:      	b.ne	0xfffffe00079b9e3c <_nwn_event+0x1c0>
fffffe00079b9d04:      	ldr	w26, [x22, #4]
fffffe00079b9d08:      	ldr	w25, [x19, #4784]
fffffe00079b9d0c:      	cmp	w26, #0
fffffe00079b9d10:      	cset	w23, eq
fffffe00079b9d14:      	cmp	w25, #15
fffffe00079b9d18:      	b.hi	0xfffffe00079b9ffc <_nwn_event+0x380>
fffffe00079b9d1c:      	mov	w8, #1
fffffe00079b9d20:      	mov	w9, #1048
fffffe00079b9d24:      	lsl	w8, w8, w25
fffffe00079b9d28:      	tst	w8, w9
fffffe00079b9d2c:      	b.eq	0xfffffe00079b9ff0 <_nwn_event+0x374>
fffffe00079b9d30:      	cbz	w26, 0xfffffe00079b9e3c <_nwn_event+0x1c0>
fffffe00079b9d34:      	mov	x0, x19
fffffe00079b9d38:      	mov	w3, #3
fffffe00079b9d3c:      	mov	w4, wzr
fffffe00079b9d40:      	b	0xfffffe00079b9fb8 <_nwn_event+0x33c>
fffffe00079b9d44:      	ldp	x2, x3, [x22, #40]
fffffe00079b9d48:      	mov	x0, x19
fffffe00079b9d4c:      	mov	x4, x20
fffffe00079b9d50:      	bl	0xfffffe00079b9314 <_nwo_management>
fffffe00079b9d54:      	b	0xfffffe00079b9fc0 <_nwn_event+0x344>
fffffe00079b9d58:      	ldr	w8, [x22, #4]
fffffe00079b9d5c:      	mov	x0, x19
fffffe00079b9d60:      	ldr	x2, [x22, #16]
fffffe00079b9d64:      	mov	w3, #3
fffffe00079b9d68:      	b	0xfffffe00079b9fb0 <_nwn_event+0x334>
fffffe00079b9d6c:      	ldr	w8, [x22, #4]
fffffe00079b9d70:      	mov	x0, x19
fffffe00079b9d74:      	ldrb	w2, [x22, #28]
fffffe00079b9d78:      	mov	x7, x20
fffffe00079b9d7c:      	ldrb	w3, [x22, #30]
fffffe00079b9d80:      	cmp	w8, #0
fffffe00079b9d84:      	ldrb	w4, [x22, #31]
fffffe00079b9d88:      	ldrb	w5, [x22, #32]
fffffe00079b9d8c:      	cset	w6, eq
fffffe00079b9d90:      	bl	0xfffffe00079b93dc <_nwo_grant>
fffffe00079b9d94:      	mov	w21, w0
fffffe00079b9d98:      	cmp	w0, #1
fffffe00079b9d9c:      	b.lt	0xfffffe00079b9fc4 <_nwn_event+0x348>
fffffe00079b9da0:      	ldr	w8, [x22, #36]
fffffe00079b9da4:      	subs	w8, w8, #10
fffffe00079b9da8:      	csel	w8, wzr, w8, lo
fffffe00079b9dac:      	cmn	x8, x20
fffffe00079b9db0:      	b.lo	0xfffffe00079b9fd8 <_nwn_event+0x35c>
fffffe00079b9db4:      	ldr	x1, [x22, #8]
fffffe00079b9db8:      	mov	x0, x19
fffffe00079b9dbc:      	mov	x2, x20
fffffe00079b9dc0:      	bl	0xfffffe00079b90a4 <_nwo_link_lost>
fffffe00079b9dc4:      	b	0xfffffe00079b9fc0 <_nwn_event+0x344>
fffffe00079b9dc8:      	ldp	x2, x3, [x22, #40]
fffffe00079b9dcc:      	mov	x0, x19
fffffe00079b9dd0:      	mov	x4, x20
fffffe00079b9dd4:      	bl	0xfffffe00079b94a4 <_nwo_key24>
fffffe00079b9dd8:      	b	0xfffffe00079b9fc0 <_nwn_event+0x344>
fffffe00079b9ddc:      	ldr	x24, [x22, #40]
fffffe00079b9de0:      	mov	w21, wzr
fffffe00079b9de4:      	cbz	x24, 0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9de8:      	ldr	x8, [x22, #48]
fffffe00079b9dec:      	subs	x23, x8, #14
fffffe00079b9df0:      	b.lo	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9df4:      	ldrb	w8, [x24, #12]
fffffe00079b9df8:      	cmp	w8, #136
fffffe00079b9dfc:      	b.ne	0xfffffe00079b9e3c <_nwn_event+0x1c0>
fffffe00079b9e00:      	ldrb	w8, [x24, #13]
fffffe00079b9e04:      	cmp	w8, #142
fffffe00079b9e08:      	b.ne	0xfffffe00079b9e3c <_nwn_event+0x1c0>
fffffe00079b9e0c:      	ldr	x8, [x19, #4800]
fffffe00079b9e10:      	mov	x0, x24
fffffe00079b9e14:      	mov	w2, #6
fffffe00079b9e18:      	add	x1, x8, #192
fffffe00079b9e1c:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b9e20:      	cbnz	w0, 0xfffffe00079b9e3c <_nwn_event+0x1c0>
fffffe00079b9e24:      	ldr	x8, [x19, #4800]
fffffe00079b9e28:      	add	x0, x24, #6
fffffe00079b9e2c:      	mov	w2, #6
fffffe00079b9e30:      	add	x1, x8, #198
fffffe00079b9e34:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b9e38:      	cbz	w0, 0xfffffe00079ba050 <_nwn_event+0x3d4>
fffffe00079b9e3c:      	mov	w21, wzr
fffffe00079b9e40:      	ldr	w8, [x19, #4]
fffffe00079b9e44:      	cmp	w8, #16
fffffe00079b9e48:      	b.lo	0xfffffe00079b9ea4 <_nwn_event+0x228>
fffffe00079b9e4c:      	ldr	w8, [x19, #4840]
fffffe00079b9e50:      	cbnz	w8, 0xfffffe00079b9ea4 <_nwn_event+0x228>
fffffe00079b9e54:      	mov	x10, #42991
fffffe00079b9e58:      	ldr	x8, [x19, #1304]
fffffe00079b9e5c:      	movk	x10, #19398, lsl #16
fffffe00079b9e60:      	movk	x10, #35127, lsl #32
fffffe00079b9e64:      	movk	x10, #65, lsl #48
fffffe00079b9e68:      	cmp	x8, x10
fffffe00079b9e6c:      	b.hi	0xfffffe00079b9ea0 <_nwn_event+0x224>
fffffe00079b9e70:      	ldr	x9, [x19, #4848]
fffffe00079b9e74:      	cmp	x9, x10
fffffe00079b9e78:      	b.hi	0xfffffe00079b9ea0 <_nwn_event+0x224>
fffffe00079b9e7c:      	ldr	x0, [x19, #4800]
fffffe00079b9e80:      	mov	w10, #1000
fffffe00079b9e84:      	mul	x9, x9, x10
fffffe00079b9e88:      	ldr	x11, [x0, #168]
fffffe00079b9e8c:      	mul	x2, x8, x10
fffffe00079b9e90:      	cmp	x9, x11
fffffe00079b9e94:      	csel	x1, x9, x11, hi
fffffe00079b9e98:      	bl	0xfffffe00079abc00 <_nst_cleanup_begin>
fffffe00079b9e9c:      	cbz	w0, 0xfffffe00079b9fcc <_nwn_event+0x350>
fffffe00079b9ea0:      	mov	w21, #-1
fffffe00079b9ea4:      	mov	w0, w21
fffffe00079b9ea8:      	ldp	x29, x30, [sp, #128]
fffffe00079b9eac:      	ldp	x20, x19, [sp, #112]
fffffe00079b9eb0:      	ldp	x22, x21, [sp, #96]
fffffe00079b9eb4:      	ldp	x24, x23, [sp, #80]
fffffe00079b9eb8:      	ldp	x26, x25, [sp, #64]
fffffe00079b9ebc:      	add	sp, sp, #144
fffffe00079b9ec0:      	ret
fffffe00079b9ec4:      	ldrb	w2, [x22, #30]
fffffe00079b9ec8:      	mov	x0, x19
fffffe00079b9ecc:      	mov	x3, x20
fffffe00079b9ed0:      	bl	0xfffffe00079b4a68 <_stc_channel_released>
fffffe00079b9ed4:      	mov	w21, w0
fffffe00079b9ed8:      	cmp	w0, #1
fffffe00079b9edc:      	b.lt	0xfffffe00079b9fc4 <_nwn_event+0x348>
fffffe00079b9ee0:      	ldr	w8, [x19, #4]
fffffe00079b9ee4:      	cmp	w8, #15
fffffe00079b9ee8:      	b.hi	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9eec:      	ldr	x8, [x19, #4856]
fffffe00079b9ef0:      	str	x8, [x19, #1296]
fffffe00079b9ef4:      	b	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9ef8:      	mov	x0, x19
fffffe00079b9efc:      	mov	x2, x20
fffffe00079b9f00:      	strb	wzr, [x24]
fffffe00079b9f04:      	strb	wzr, [x24, #1]
fffffe00079b9f08:      	strb	wzr, [x24, #2]
fffffe00079b9f0c:      	strb	wzr, [x24, #3]
fffffe00079b9f10:      	strb	wzr, [x24, #4]
fffffe00079b9f14:      	strb	wzr, [x24, #5]
fffffe00079b9f18:      	strb	wzr, [x24, #6]
fffffe00079b9f1c:      	strb	wzr, [x24, #7]
fffffe00079b9f20:      	strb	wzr, [x24, #8]
fffffe00079b9f24:      	str	x20, [x19, #4848]
fffffe00079b9f28:      	bl	0xfffffe00079b9178 <_nwo_cancel>
fffffe00079b9f2c:      	mov	w21, w0
fffffe00079b9f30:      	tbnz	w0, #31, 0xfffffe00079b9ea4 <_nwn_event+0x228>
fffffe00079b9f34:      	ldr	w8, [x19, #4]
fffffe00079b9f38:      	cmp	w8, #16
fffffe00079b9f3c:      	b.lo	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9f40:      	ldr	w8, [x19, #4840]
fffffe00079b9f44:      	cbnz	w8, 0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9f48:      	mov	x10, #42991
fffffe00079b9f4c:      	ldr	x8, [x19, #1304]
fffffe00079b9f50:      	movk	x10, #19398, lsl #16
fffffe00079b9f54:      	movk	x10, #35127, lsl #32
fffffe00079b9f58:      	movk	x10, #65, lsl #48
fffffe00079b9f5c:      	cmp	x8, x10
fffffe00079b9f60:      	b.hi	0xfffffe00079b9ea0 <_nwn_event+0x224>
fffffe00079b9f64:      	ldr	x9, [x19, #4848]
fffffe00079b9f68:      	cmp	x9, x10
fffffe00079b9f6c:      	b.hi	0xfffffe00079b9ea0 <_nwn_event+0x224>
fffffe00079b9f70:      	ldr	x0, [x19, #4800]
fffffe00079b9f74:      	mov	w10, #1000
fffffe00079b9f78:      	mul	x9, x9, x10
fffffe00079b9f7c:      	ldr	x11, [x0, #168]
fffffe00079b9f80:      	mul	x2, x8, x10
fffffe00079b9f84:      	cmp	x9, x11
fffffe00079b9f88:      	csel	x1, x9, x11, hi
fffffe00079b9f8c:      	bl	0xfffffe00079abc00 <_nst_cleanup_begin>
fffffe00079b9f90:      	cbnz	w0, 0xfffffe00079b9ea0 <_nwn_event+0x224>
fffffe00079b9f94:      	mov	w8, #1
fffffe00079b9f98:      	str	w8, [x19, #4840]
fffffe00079b9f9c:      	b	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9fa0:      	ldr	w8, [x22, #4]
fffffe00079b9fa4:      	mov	x0, x19
fffffe00079b9fa8:      	ldr	x2, [x22, #16]
fffffe00079b9fac:      	mov	w3, #8
fffffe00079b9fb0:      	cmp	w8, #0
fffffe00079b9fb4:      	cset	w4, eq
fffffe00079b9fb8:      	mov	x5, x20
fffffe00079b9fbc:      	bl	0xfffffe00079b8f74 <_nwo_complete>
fffffe00079b9fc0:      	mov	w21, w0
fffffe00079b9fc4:      	tbz	w21, #31, 0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9fc8:      	b	0xfffffe00079b9ea4 <_nwn_event+0x228>
fffffe00079b9fcc:      	mov	w8, #1
fffffe00079b9fd0:      	str	w8, [x19, #4840]
fffffe00079b9fd4:      	b	0xfffffe00079b9ea4 <_nwn_event+0x228>
fffffe00079b9fd8:      	ldr	x9, [x19, #1296]
fffffe00079b9fdc:      	add	x8, x8, x20
fffffe00079b9fe0:      	cmp	x8, x9
fffffe00079b9fe4:      	b.hs	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9fe8:      	str	x8, [x19, #1296]
fffffe00079b9fec:      	b	0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079b9ff0:      	mov	w9, #32832
fffffe00079b9ff4:      	tst	w8, w9
fffffe00079b9ff8:      	b.ne	0xfffffe00079b9ea0 <_nwn_event+0x224>
fffffe00079b9ffc:      	mov	x0, x19
fffffe00079ba000:      	sub	w8, w25, #8
fffffe00079ba004:      	tst	w8, #0xfffffff7
fffffe00079ba008:      	mov	w8, #2
fffffe00079ba00c:      	mov	w9, #5
fffffe00079ba010:      	csel	w21, w9, w8, eq
fffffe00079ba014:      	bl	0xfffffe00079b3164 <_stc_next>
fffffe00079ba018:      	cbnz	w26, 0xfffffe00079ba0e0 <_nwn_event+0x464>
fffffe00079ba01c:      	cbz	x0, 0xfffffe00079ba0e0 <_nwn_event+0x464>
fffffe00079ba020:      	ldr	x8, [x0, #8]
fffffe00079ba024:      	ldr	x9, [x22, #16]
fffffe00079ba028:      	cmp	x8, x9
fffffe00079ba02c:      	b.ne	0xfffffe00079ba0c0 <_nwn_event+0x444>
fffffe00079ba030:      	sub	w8, w25, #7
fffffe00079ba034:      	cmp	w8, #2
fffffe00079ba038:      	b.hs	0xfffffe00079ba0a0 <_nwn_event+0x424>
fffffe00079ba03c:      	ldr	w1, [x19, #4836]
fffffe00079ba040:      	ldr	x0, [x19, #4800]
fffffe00079ba044:      	cbz	w1, 0xfffffe00079ba0c8 <_nwn_event+0x44c>
fffffe00079ba048:      	mov	w2, wzr
fffffe00079ba04c:      	b	0xfffffe00079ba0cc <_nwn_event+0x450>
fffffe00079ba050:      	movi.2d	v0, #0000000000000000
fffffe00079ba054:      	add	x21, x24, #14
fffffe00079ba058:      	ldr	x1, [x22, #8]
fffffe00079ba05c:      	mov	x0, sp
fffffe00079ba060:      	ldr	x2, [x19, #4776]
fffffe00079ba064:      	mov	x3, x21
fffffe00079ba068:      	mov	x4, x23
fffffe00079ba06c:      	str	xzr, [sp, #48]
fffffe00079ba070:      	stp	q0, q0, [sp, #16]
fffffe00079ba074:      	str	q0, [sp]
fffffe00079ba078:      	bl	0xfffffe00079b12a8 <_nstd_eapol>
fffffe00079ba07c:      	mov	x0, sp
fffffe00079ba080:      	bl	0xfffffe00079b143c <_nstd_log>
fffffe00079ba084:      	ldr	x1, [x22, #8]
fffffe00079ba088:      	mov	x0, x19
fffffe00079ba08c:      	mov	x2, x21
fffffe00079ba090:      	mov	x3, x23
fffffe00079ba094:      	mov	x4, x20
fffffe00079ba098:      	bl	0xfffffe00079b924c <_nwo_eapol>
fffffe00079ba09c:      	b	0xfffffe00079b9fc0 <_nwn_event+0x344>
fffffe00079ba0a0:      	cmp	w25, #16
fffffe00079ba0a4:      	b.ne	0xfffffe00079ba0c0 <_nwn_event+0x444>
fffffe00079ba0a8:      	mov	w8, #4825
fffffe00079ba0ac:      	ldr	x0, [x19, #4800]
fffffe00079ba0b0:      	ldrb	w1, [x24]
fffffe00079ba0b4:      	add	x2, x19, x8
fffffe00079ba0b8:      	bl	0xfffffe00079ace60 <_nst_replay_replace_group>
fffffe00079ba0bc:      	b	0xfffffe00079ba0d8 <_nwn_event+0x45c>
fffffe00079ba0c0:      	mov	w23, #1
fffffe00079ba0c4:      	b	0xfffffe00079ba0e0 <_nwn_event+0x464>
fffffe00079ba0c8:      	ldrb	w2, [x24]
fffffe00079ba0cc:      	mov	w8, #4825
fffffe00079ba0d0:      	add	x3, x19, x8
fffffe00079ba0d4:      	bl	0xfffffe00079ac9cc <_nst_replay_install>
fffffe00079ba0d8:      	cmp	w0, #0
fffffe00079ba0dc:      	cset	w23, eq
fffffe00079ba0e0:      	ldp	x1, x2, [x22, #8]
fffffe00079ba0e4:      	mov	x0, x19
fffffe00079ba0e8:      	mov	w3, w21
fffffe00079ba0ec:      	mov	w4, w23
fffffe00079ba0f0:      	mov	x5, x20
fffffe00079ba0f4:      	bl	0xfffffe00079b8f74 <_nwo_complete>
fffffe00079ba0f8:      	mov	w21, w0
fffffe00079ba0fc:      	strb	wzr, [x24]
fffffe00079ba100:      	strb	wzr, [x24, #1]
fffffe00079ba104:      	strb	wzr, [x24, #2]
fffffe00079ba108:      	strb	wzr, [x24, #3]
fffffe00079ba10c:      	strb	wzr, [x24, #4]
fffffe00079ba110:      	strb	wzr, [x24, #5]
fffffe00079ba114:      	strb	wzr, [x24, #6]
fffffe00079ba118:      	strb	wzr, [x24, #7]
fffffe00079ba11c:      	strb	wzr, [x24, #8]
fffffe00079ba120:      	tbz	w21, #31, 0xfffffe00079b9e40 <_nwn_event+0x1c4>
fffffe00079ba124:      	b	0xfffffe00079b9ea4 <_nwn_event+0x228>
