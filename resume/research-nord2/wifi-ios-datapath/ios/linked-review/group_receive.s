
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b6d80 <_group_receive>:
fffffe00079b6d80:      	stp	x28, x27, [sp, #-96]!
fffffe00079b6d84:      	stp	x26, x25, [sp, #16]
fffffe00079b6d88:      	stp	x24, x23, [sp, #32]
fffffe00079b6d8c:      	stp	x22, x21, [sp, #48]
fffffe00079b6d90:      	stp	x20, x19, [sp, #64]
fffffe00079b6d94:      	stp	x29, x30, [sp, #80]
fffffe00079b6d98:      	add	x29, sp, #80
fffffe00079b6d9c:      	sub	sp, sp, #752
fffffe00079b6da0:      	adrp	x8, 0xfffffe0007c98000 <_derive_keys+0x18>
fffffe00079b6da4:      	mov	x19, x3
fffffe00079b6da8:      	mov	x20, x0
fffffe00079b6dac:      	nop
fffffe00079b6db0:      	ldr	x8, [x8, #3440]
fffffe00079b6db4:      	stur	x8, [x29, #-104]
fffffe00079b6db8:      	ldr	w8, [x0, #32]
fffffe00079b6dbc:      	cmp	w8, #5
fffffe00079b6dc0:      	b.ne	0xfffffe00079b6f54 <_group_receive+0x1d4>
fffffe00079b6dc4:      	ldr	w8, [x20, #40]
fffffe00079b6dc8:      	cbz	w8, 0xfffffe00079b6f54 <_group_receive+0x1d4>
fffffe00079b6dcc:      	ldr	w8, [x20, #44]
fffffe00079b6dd0:      	cmp	w8, #7
fffffe00079b6dd4:      	b.ne	0xfffffe00079b6f54 <_group_receive+0x1d4>
fffffe00079b6dd8:      	sub	x24, x29, #176
fffffe00079b6ddc:      	adrp	x8, 0xfffffe0007031000 <_stc_eapol+0x384>
fffffe00079b6de0:      	add	x8, x8, #1296
fffffe00079b6de4:      	adrp	x10, 0xfffffe0007031000 <_stc_eapol+0x38c>
fffffe00079b6de8:      	mov	w22, w2
fffffe00079b6dec:      	mov	x21, x1
fffffe00079b6df0:      	add	x9, x1, #97
fffffe00079b6df4:      	add	x0, x20, #156
fffffe00079b6df8:      	stp	x1, x8, [x24, #48]
fffffe00079b6dfc:      	ldr	q0, [x10, #1280]
fffffe00079b6e00:      	sub	w8, w2, #97
fffffe00079b6e04:      	sub	x2, x29, #128
fffffe00079b6e08:      	add	x3, sp, #16
fffffe00079b6e0c:      	sub	x5, x29, #152
fffffe00079b6e10:      	mov	w1, #16
fffffe00079b6e14:      	mov	w4, #3
fffffe00079b6e18:      	stp	xzr, xzr, [x24]
fffffe00079b6e1c:      	str	x9, [x24, #64]
fffffe00079b6e20:      	stp	xzr, xzr, [x24, #24]
fffffe00079b6e24:      	stur	wzr, [x29, #-136]
fffffe00079b6e28:      	str	q0, [sp, #16]
fffffe00079b6e2c:      	stp	x8, xzr, [sp, #32]
fffffe00079b6e30:      	bl	0xfffffe00079b4d50 <_nwpc_hmac>
fffffe00079b6e34:      	ldur	q0, [x24, #24]
fffffe00079b6e38:      	str	q0, [x24]
fffffe00079b6e3c:      	sturb	wzr, [x29, #-152]
fffffe00079b6e40:      	sturb	wzr, [x29, #-151]
fffffe00079b6e44:      	sturb	wzr, [x29, #-150]
fffffe00079b6e48:      	sturb	wzr, [x29, #-149]
fffffe00079b6e4c:      	sturb	wzr, [x29, #-148]
fffffe00079b6e50:      	sturb	wzr, [x29, #-147]
fffffe00079b6e54:      	sturb	wzr, [x29, #-146]
fffffe00079b6e58:      	sturb	wzr, [x29, #-145]
fffffe00079b6e5c:      	sturb	wzr, [x29, #-144]
fffffe00079b6e60:      	sturb	wzr, [x29, #-143]
fffffe00079b6e64:      	sturb	wzr, [x29, #-142]
fffffe00079b6e68:      	sturb	wzr, [x29, #-141]
fffffe00079b6e6c:      	sturb	wzr, [x29, #-140]
fffffe00079b6e70:      	sturb	wzr, [x29, #-139]
fffffe00079b6e74:      	sturb	wzr, [x29, #-138]
fffffe00079b6e78:      	sturb	wzr, [x29, #-137]
fffffe00079b6e7c:      	sturb	wzr, [x29, #-136]
fffffe00079b6e80:      	sturb	wzr, [x29, #-135]
fffffe00079b6e84:      	sturb	wzr, [x29, #-134]
fffffe00079b6e88:      	sturb	wzr, [x29, #-133]
fffffe00079b6e8c:      	ldr	q0, [x24]
fffffe00079b6e90:      	ldur	q1, [x21, #81]
fffffe00079b6e94:      	sturb	wzr, [x29, #-176]
fffffe00079b6e98:      	sturb	wzr, [x29, #-175]
fffffe00079b6e9c:      	sturb	wzr, [x29, #-174]
fffffe00079b6ea0:      	eor.16b	v0, v1, v0
fffffe00079b6ea4:      	sturb	wzr, [x29, #-173]
fffffe00079b6ea8:      	ext.16b	v1, v0, v0, #8
fffffe00079b6eac:      	sturb	wzr, [x29, #-172]
fffffe00079b6eb0:      	sturb	wzr, [x29, #-171]
fffffe00079b6eb4:      	sturb	wzr, [x29, #-170]
fffffe00079b6eb8:      	sturb	wzr, [x29, #-169]
fffffe00079b6ebc:      	orr.8b	v0, v0, v1
fffffe00079b6ec0:      	sturb	wzr, [x29, #-168]
fffffe00079b6ec4:      	umov.b	w8, v0[1]
fffffe00079b6ec8:      	umov.b	w9, v0[0]
fffffe00079b6ecc:      	umov.b	w10, v0[2]
fffffe00079b6ed0:      	umov.b	w11, v0[3]
fffffe00079b6ed4:      	umov.b	w12, v0[7]
fffffe00079b6ed8:      	sturb	wzr, [x29, #-167]
fffffe00079b6edc:      	sturb	wzr, [x29, #-166]
fffffe00079b6ee0:      	orr	w8, w9, w8
fffffe00079b6ee4:      	umov.b	w9, v0[4]
fffffe00079b6ee8:      	orr	w8, w8, w10
fffffe00079b6eec:      	umov.b	w10, v0[5]
fffffe00079b6ef0:      	orr	w8, w8, w11
fffffe00079b6ef4:      	umov.b	w11, v0[6]
fffffe00079b6ef8:      	sturb	wzr, [x29, #-165]
fffffe00079b6efc:      	orr	w8, w8, w9
fffffe00079b6f00:      	sturb	wzr, [x29, #-164]
fffffe00079b6f04:      	orr	w8, w8, w10
fffffe00079b6f08:      	sturb	wzr, [x29, #-163]
fffffe00079b6f0c:      	orr	w8, w8, w11
fffffe00079b6f10:      	sturb	wzr, [x29, #-162]
fffffe00079b6f14:      	orr	w8, w8, w12
fffffe00079b6f18:      	sturb	wzr, [x29, #-161]
fffffe00079b6f1c:      	tst	w8, #0xff
fffffe00079b6f20:      	b.eq	0xfffffe00079b6fc0 <_group_receive+0x240>
fffffe00079b6f24:      	mov	x8, xzr
fffffe00079b6f28:      	strb	wzr, [x20, x8]
fffffe00079b6f2c:      	add	x8, x8, #1
fffffe00079b6f30:      	cmp	x8, #2616
fffffe00079b6f34:      	b.ne	0xfffffe00079b6f28 <_group_receive+0x1a8>
fffffe00079b6f38:      	mov	x8, xzr
fffffe00079b6f3c:      	strb	wzr, [x19, x8]
fffffe00079b6f40:      	add	x8, x8, #1
fffffe00079b6f44:      	cmp	x8, #280
fffffe00079b6f48:      	b.ne	0xfffffe00079b6f3c <_group_receive+0x1bc>
fffffe00079b6f4c:      	mov	w8, #8
fffffe00079b6f50:      	b	0xfffffe00079b6f80 <_group_receive+0x200>
fffffe00079b6f54:      	mov	x8, xzr
fffffe00079b6f58:      	strb	wzr, [x20, x8]
fffffe00079b6f5c:      	add	x8, x8, #1
fffffe00079b6f60:      	cmp	x8, #2616
fffffe00079b6f64:      	b.ne	0xfffffe00079b6f58 <_group_receive+0x1d8>
fffffe00079b6f68:      	mov	x8, xzr
fffffe00079b6f6c:      	strb	wzr, [x19, x8]
fffffe00079b6f70:      	add	x8, x8, #1
fffffe00079b6f74:      	cmp	x8, #280
fffffe00079b6f78:      	b.ne	0xfffffe00079b6f6c <_group_receive+0x1ec>
fffffe00079b6f7c:      	mov	w8, #7
fffffe00079b6f80:      	mov	w0, #-1
fffffe00079b6f84:      	str	w8, [x19, #8]
fffffe00079b6f88:      	adrp	x8, 0xfffffe0007c98000 <_derive_keys+0x200>
fffffe00079b6f8c:      	nop
fffffe00079b6f90:      	ldur	x9, [x29, #-104]
fffffe00079b6f94:      	ldr	x8, [x8, #3440]
fffffe00079b6f98:      	cmp	x8, x9
fffffe00079b6f9c:      	b.ne	0xfffffe00079b7824 <_group_receive+0xaa4>
fffffe00079b6fa0:      	add	sp, sp, #752
fffffe00079b6fa4:      	ldp	x29, x30, [sp, #80]
fffffe00079b6fa8:      	ldp	x20, x19, [sp, #64]
fffffe00079b6fac:      	ldp	x22, x21, [sp, #48]
fffffe00079b6fb0:      	ldp	x24, x23, [sp, #32]
fffffe00079b6fb4:      	ldp	x26, x25, [sp, #16]
fffffe00079b6fb8:      	ldp	x28, x27, [sp], #96
fffffe00079b6fbc:      	ret
fffffe00079b6fc0:      	add	x0, x21, #9
fffffe00079b6fc4:      	add	x1, x20, #248
fffffe00079b6fc8:      	mov	w2, #8
fffffe00079b6fcc:      	bl	0xfffffe000746acfc <_memcmp>
fffffe00079b6fd0:      	tbz	w0, #31, 0xfffffe00079b7004 <_group_receive+0x284>
fffffe00079b6fd4:      	mov	x8, xzr
fffffe00079b6fd8:      	strb	wzr, [x20, x8]
fffffe00079b6fdc:      	add	x8, x8, #1
fffffe00079b6fe0:      	cmp	x8, #2616
fffffe00079b6fe4:      	b.ne	0xfffffe00079b6fd8 <_group_receive+0x258>
fffffe00079b6fe8:      	mov	x8, xzr
fffffe00079b6fec:      	strb	wzr, [x19, x8]
fffffe00079b6ff0:      	add	x8, x8, #1
fffffe00079b6ff4:      	cmp	x8, #280
fffffe00079b6ff8:      	b.ne	0xfffffe00079b6fec <_group_receive+0x26c>
fffffe00079b6ffc:      	mov	w8, #11
fffffe00079b7000:      	b	0xfffffe00079b6f80 <_group_receive+0x200>
fffffe00079b7004:      	cbz	w0, 0xfffffe00079b70b4 <_group_receive+0x334>
fffffe00079b7008:      	ldr	x8, [x20, #16]
fffffe00079b700c:      	cbnz	x8, 0xfffffe00079b72c0 <_group_receive+0x540>
fffffe00079b7010:      	ldr	w8, [x20, #228]
fffffe00079b7014:      	cbnz	w8, 0xfffffe00079b72c0 <_group_receive+0x540>
fffffe00079b7018:      	add	x0, sp, #16
fffffe00079b701c:      	mov	w1, #640
fffffe00079b7020:      	ldurh	w23, [x21, #97]
fffffe00079b7024:      	str	wzr, [sp, #12]
fffffe00079b7028:      	add	x25, sp, #16
fffffe00079b702c:      	bl	0xfffffe0007bad720 <_bzero>
fffffe00079b7030:      	ldrb	w8, [x21, #71]
fffffe00079b7034:      	stp	xzr, xzr, [x24, #48]
fffffe00079b7038:      	cbnz	w8, 0xfffffe00079b706c <_group_receive+0x2ec>
fffffe00079b703c:      	ldrb	w8, [x21, #72]
fffffe00079b7040:      	cbnz	w8, 0xfffffe00079b706c <_group_receive+0x2ec>
fffffe00079b7044:      	rev16	w23, w23
fffffe00079b7048:      	add	x0, x20, #172
fffffe00079b704c:      	add	x1, x21, #99
fffffe00079b7050:      	add	x3, sp, #16
fffffe00079b7054:      	sub	x5, x29, #152
fffffe00079b7058:      	mov	x2, x23
fffffe00079b705c:      	mov	w4, #640
fffffe00079b7060:      	str	xzr, [x24, #24]
fffffe00079b7064:      	bl	0xfffffe00079b56f4 <_nwpc_unwrap>
fffffe00079b7068:      	cbz	w0, 0xfffffe00079b72e8 <_group_receive+0x568>
fffffe00079b706c:      	mov	x8, xzr
fffffe00079b7070:      	strb	wzr, [x25, x8]
fffffe00079b7074:      	add	x8, x8, #1
fffffe00079b7078:      	cmp	x8, #640
fffffe00079b707c:      	b.ne	0xfffffe00079b7070 <_group_receive+0x2f0>
fffffe00079b7080:      	cbz	x20, 0xfffffe00079b7098 <_group_receive+0x318>
fffffe00079b7084:      	mov	x8, xzr
fffffe00079b7088:      	strb	wzr, [x20, x8]
fffffe00079b708c:      	add	x8, x8, #1
fffffe00079b7090:      	cmp	x8, #2616
fffffe00079b7094:      	b.ne	0xfffffe00079b7088 <_group_receive+0x308>
fffffe00079b7098:      	mov	x8, xzr
fffffe00079b709c:      	strb	wzr, [x19, x8]
fffffe00079b70a0:      	add	x8, x8, #1
fffffe00079b70a4:      	cmp	x8, #280
fffffe00079b70a8:      	b.ne	0xfffffe00079b709c <_group_receive+0x31c>
fffffe00079b70ac:      	mov	w8, #12
fffffe00079b70b0:      	b	0xfffffe00079b6f80 <_group_receive+0x200>
fffffe00079b70b4:      	ldr	w8, [x20, #232]
fffffe00079b70b8:      	cbz	w8, 0xfffffe00079b7280 <_group_receive+0x500>
fffffe00079b70bc:      	cmp	w8, w22
fffffe00079b70c0:      	b.ne	0xfffffe00079b7280 <_group_receive+0x500>
fffffe00079b70c4:      	cbz	w22, 0xfffffe00079b72b0 <_group_receive+0x530>
fffffe00079b70c8:      	mov	w8, w22
fffffe00079b70cc:      	cmp	w22, #8
fffffe00079b70d0:      	b.hs	0xfffffe00079b70e0 <_group_receive+0x360>
fffffe00079b70d4:      	mov	x9, xzr
fffffe00079b70d8:      	mov	w12, wzr
fffffe00079b70dc:      	b	0xfffffe00079b7254 <_group_receive+0x4d4>
fffffe00079b70e0:      	adrp	x10, 0xfffffe000700b000 <_stc_cancel+0x40>
fffffe00079b70e4:      	adrp	x11, 0xfffffe000700b000 <_stc_cancel+0x44>
fffffe00079b70e8:      	cmp	w22, #32
fffffe00079b70ec:      	b.hs	0xfffffe00079b70fc <_group_receive+0x37c>
fffffe00079b70f0:      	mov	w12, wzr
fffffe00079b70f4:      	mov	x9, xzr
fffffe00079b70f8:      	b	0xfffffe00079b71e4 <_group_receive+0x464>
fffffe00079b70fc:      	adrp	x14, 0xfffffe000700b000 <_stc_cancel+0x5c>
fffffe00079b7100:      	and	x9, x8, #0xffffffe0
fffffe00079b7104:      	movi.2d	v0, #0000000000000000
fffffe00079b7108:      	add	x12, x21, #16
fffffe00079b710c:      	movi.2d	v5, #0000000000000000
fffffe00079b7110:      	add	x13, x20, #272
fffffe00079b7114:      	ldr	q3, [x14, #3040]
fffffe00079b7118:      	nop
fffffe00079b711c:      	movi.2d	v4, #0000000000000000
fffffe00079b7120:      	ldr	q1, [x10, #3072]
fffffe00079b7124:      	movi.2d	v6, #0000000000000000
fffffe00079b7128:      	ldr	q2, [x11, #3024]
fffffe00079b712c:      	movi.2d	v7, #0000000000000000
fffffe00079b7130:      	ldr	q19, [x14, #3056]
fffffe00079b7134:      	movi.2d	v17, #0000000000000000
fffffe00079b7138:      	mov	x14, x9
fffffe00079b713c:      	movi.2d	v16, #0000000000000000
fffffe00079b7140:      	movi.2d	v18, #0000000000000000
fffffe00079b7144:      	ldp	q20, q21, [x12, #-16]
fffffe00079b7148:      	add	x12, x12, #32
fffffe00079b714c:      	subs	x14, x14, #32
fffffe00079b7150:      	ldp	q22, q23, [x13, #-16]
fffffe00079b7154:      	add	x13, x13, #32
fffffe00079b7158:      	eor.16b	v20, v22, v20
fffffe00079b715c:      	tbl.16b	v22, { v20 }, v1
fffffe00079b7160:      	eor.16b	v21, v23, v21
fffffe00079b7164:      	tbl.16b	v23, { v20 }, v2
fffffe00079b7168:      	tbl.16b	v24, { v20 }, v3
fffffe00079b716c:      	tbl.16b	v20, { v20 }, v19
fffffe00079b7170:      	tbl.16b	v25, { v21 }, v1
fffffe00079b7174:      	tbl.16b	v26, { v21 }, v2
fffffe00079b7178:      	tbl.16b	v27, { v21 }, v3
fffffe00079b717c:      	tbl.16b	v21, { v21 }, v19
fffffe00079b7180:      	orr.16b	v6, v6, v20
fffffe00079b7184:      	orr.16b	v4, v4, v24
fffffe00079b7188:      	orr.16b	v5, v5, v23
fffffe00079b718c:      	orr.16b	v0, v0, v22
fffffe00079b7190:      	orr.16b	v18, v18, v21
fffffe00079b7194:      	orr.16b	v16, v16, v27
fffffe00079b7198:      	orr.16b	v17, v17, v26
fffffe00079b719c:      	orr.16b	v7, v7, v25
fffffe00079b71a0:      	b.ne	0xfffffe00079b7144 <_group_receive+0x3c4>
fffffe00079b71a4:      	orr.16b	v1, v17, v5
fffffe00079b71a8:      	cmp	x9, x8
fffffe00079b71ac:      	orr.16b	v2, v18, v6
fffffe00079b71b0:      	orr.16b	v0, v7, v0
fffffe00079b71b4:      	orr.16b	v3, v16, v4
fffffe00079b71b8:      	orr.16b	v0, v0, v3
fffffe00079b71bc:      	orr.16b	v1, v1, v2
fffffe00079b71c0:      	orr.16b	v0, v0, v1
fffffe00079b71c4:      	ext.16b	v1, v0, v0, #8
fffffe00079b71c8:      	orr.8b	v0, v0, v1
fffffe00079b71cc:      	mov.s	w12, v0[1]
fffffe00079b71d0:      	fmov	w13, s0
fffffe00079b71d4:      	orr	w12, w13, w12
fffffe00079b71d8:      	b.eq	0xfffffe00079b727c <_group_receive+0x4fc>
fffffe00079b71dc:      	tst	x8, #0x18
fffffe00079b71e0:      	b.eq	0xfffffe00079b7254 <_group_receive+0x4d4>
fffffe00079b71e4:      	movi.2d	v0, #0000000000000000
fffffe00079b71e8:      	mov	x14, x9
fffffe00079b71ec:      	and	x9, x8, #0xfffffff8
fffffe00079b71f0:      	add	x15, x14, x20
fffffe00079b71f4:      	movi.2d	v1, #0000000000000000
fffffe00079b71f8:      	add	x13, x21, x14
fffffe00079b71fc:      	ldr	q2, [x10, #3072]
fffffe00079b7200:      	sub	x10, x14, x9
fffffe00079b7204:      	mov.s	v0[0], w12
fffffe00079b7208:      	add	x12, x15, #256
fffffe00079b720c:      	ldr	q3, [x11, #3024]
fffffe00079b7210:      	ldr	d4, [x13], #8
fffffe00079b7214:      	ldr	d5, [x12], #8
fffffe00079b7218:      	adds	x10, x10, #8
fffffe00079b721c:      	eor.8b	v4, v5, v4
fffffe00079b7220:      	tbl.16b	v5, { v4 }, v3
fffffe00079b7224:      	tbl.16b	v4, { v4 }, v2
fffffe00079b7228:      	orr.16b	v1, v1, v5
fffffe00079b722c:      	orr.16b	v0, v0, v4
fffffe00079b7230:      	b.ne	0xfffffe00079b7210 <_group_receive+0x490>
fffffe00079b7234:      	orr.16b	v0, v0, v1
fffffe00079b7238:      	cmp	x9, x8
fffffe00079b723c:      	ext.16b	v1, v0, v0, #8
fffffe00079b7240:      	orr.8b	v0, v0, v1
fffffe00079b7244:      	mov.s	w10, v0[1]
fffffe00079b7248:      	fmov	w11, s0
fffffe00079b724c:      	orr	w12, w11, w10
fffffe00079b7250:      	b.eq	0xfffffe00079b727c <_group_receive+0x4fc>
fffffe00079b7254:      	add	x10, x9, x20
fffffe00079b7258:      	sub	x8, x8, x9
fffffe00079b725c:      	add	x10, x10, #256
fffffe00079b7260:      	add	x9, x21, x9
fffffe00079b7264:      	ldrb	w11, [x9], #1
fffffe00079b7268:      	ldrb	w13, [x10], #1
fffffe00079b726c:      	subs	x8, x8, #1
fffffe00079b7270:      	eor	w11, w13, w11
fffffe00079b7274:      	orr	w12, w12, w11
fffffe00079b7278:      	b.ne	0xfffffe00079b7264 <_group_receive+0x4e4>
fffffe00079b727c:      	cbz	w12, 0xfffffe00079b72b0 <_group_receive+0x530>
fffffe00079b7280:      	mov	x8, xzr
fffffe00079b7284:      	strb	wzr, [x20, x8]
fffffe00079b7288:      	add	x8, x8, #1
fffffe00079b728c:      	cmp	x8, #2616
fffffe00079b7290:      	b.ne	0xfffffe00079b7284 <_group_receive+0x504>
fffffe00079b7294:      	mov	x8, xzr
fffffe00079b7298:      	strb	wzr, [x19, x8]
fffffe00079b729c:      	add	x8, x8, #1
fffffe00079b72a0:      	cmp	x8, #280
fffffe00079b72a4:      	b.ne	0xfffffe00079b7298 <_group_receive+0x518>
fffffe00079b72a8:      	mov	w8, #10
fffffe00079b72ac:      	b	0xfffffe00079b6f80 <_group_receive+0x200>
fffffe00079b72b0:      	ldr	x8, [x20, #16]
fffffe00079b72b4:      	cbnz	x8, 0xfffffe00079b72c0 <_group_receive+0x540>
fffffe00079b72b8:      	ldr	w8, [x20, #228]
fffffe00079b72bc:      	cbz	w8, 0xfffffe00079b72c8 <_group_receive+0x548>
fffffe00079b72c0:      	mov	w0, #4
fffffe00079b72c4:      	b	0xfffffe00079b6f88 <_group_receive+0x208>
fffffe00079b72c8:      	mov	w8, #2
fffffe00079b72cc:      	mov	x0, x20
fffffe00079b72d0:      	mov	x1, x21
fffffe00079b72d4:      	mov	w2, #4
fffffe00079b72d8:      	mov	x3, x19
fffffe00079b72dc:      	str	w8, [x20, #228]
fffffe00079b72e0:      	bl	0xfffffe00079b631c <_reply>
fffffe00079b72e4:      	b	0xfffffe00079b6f88 <_group_receive+0x208>
fffffe00079b72e8:      	sub	w2, w23, #8
fffffe00079b72ec:      	add	x1, sp, #16
fffffe00079b72f0:      	sub	x3, x29, #128
fffffe00079b72f4:      	add	x4, sp, #12
fffffe00079b72f8:      	mov	x0, x20
fffffe00079b72fc:      	add	x23, sp, #16
fffffe00079b7300:      	bl	0xfffffe00079b82b8 <_group_kde>
fffffe00079b7304:      	mov	x8, xzr
fffffe00079b7308:      	cbz	w0, 0xfffffe00079b7390 <_group_receive+0x610>
fffffe00079b730c:      	sturb	wzr, [x29, #-128]
fffffe00079b7310:      	sturb	wzr, [x29, #-127]
fffffe00079b7314:      	sturb	wzr, [x29, #-126]
fffffe00079b7318:      	sturb	wzr, [x29, #-125]
fffffe00079b731c:      	sturb	wzr, [x29, #-124]
fffffe00079b7320:      	sturb	wzr, [x29, #-123]
fffffe00079b7324:      	sturb	wzr, [x29, #-122]
fffffe00079b7328:      	sturb	wzr, [x29, #-121]
fffffe00079b732c:      	sturb	wzr, [x29, #-120]
fffffe00079b7330:      	sturb	wzr, [x29, #-119]
fffffe00079b7334:      	sturb	wzr, [x29, #-118]
fffffe00079b7338:      	sturb	wzr, [x29, #-117]
fffffe00079b733c:      	sturb	wzr, [x29, #-116]
fffffe00079b7340:      	sturb	wzr, [x29, #-115]
fffffe00079b7344:      	sturb	wzr, [x29, #-114]
fffffe00079b7348:      	sturb	wzr, [x29, #-113]
fffffe00079b734c:      	strb	wzr, [x23, x8]
fffffe00079b7350:      	add	x8, x8, #1
fffffe00079b7354:      	cmp	x8, #640
fffffe00079b7358:      	b.ne	0xfffffe00079b734c <_group_receive+0x5cc>
fffffe00079b735c:      	cbz	x20, 0xfffffe00079b7374 <_group_receive+0x5f4>
fffffe00079b7360:      	mov	x8, xzr
fffffe00079b7364:      	strb	wzr, [x20, x8]
fffffe00079b7368:      	add	x8, x8, #1
fffffe00079b736c:      	cmp	x8, #2616
fffffe00079b7370:      	b.ne	0xfffffe00079b7364 <_group_receive+0x5e4>
fffffe00079b7374:      	mov	x8, xzr
fffffe00079b7378:      	strb	wzr, [x19, x8]
fffffe00079b737c:      	add	x8, x8, #1
fffffe00079b7380:      	cmp	x8, #280
fffffe00079b7384:      	b.ne	0xfffffe00079b7378 <_group_receive+0x5f8>
fffffe00079b7388:      	mov	w8, #14
fffffe00079b738c:      	b	0xfffffe00079b6f80 <_group_receive+0x200>
fffffe00079b7390:      	strb	wzr, [x23, x8]
fffffe00079b7394:      	add	x8, x8, #1
fffffe00079b7398:      	cmp	x8, #640
fffffe00079b739c:      	b.ne	0xfffffe00079b7390 <_group_receive+0x610>
fffffe00079b73a0:      	mov	x23, x20
fffffe00079b73a4:      	ldurb	w8, [x29, #-128]
fffffe00079b73a8:      	ldurb	w9, [x29, #-127]
fffffe00079b73ac:      	ldurb	w10, [x29, #-126]
fffffe00079b73b0:      	ldrb	w12, [x23, #204]!
fffffe00079b73b4:      	ldrb	w13, [x23, #1]
fffffe00079b73b8:      	str	w8, [sp, #8]
fffffe00079b73bc:      	ldrb	w14, [x23, #2]
fffffe00079b73c0:      	ldurb	w11, [x29, #-125]
fffffe00079b73c4:      	eor	w16, w12, w8
fffffe00079b73c8:      	ldrb	w15, [x23, #3]
fffffe00079b73cc:      	eor	w13, w13, w9
fffffe00079b73d0:      	ldurb	w12, [x29, #-124]
fffffe00079b73d4:      	orr	w16, w16, w13
fffffe00079b73d8:      	ldrb	w17, [x23, #4]
fffffe00079b73dc:      	eor	w14, w14, w10
fffffe00079b73e0:      	ldurb	w13, [x29, #-123]
fffffe00079b73e4:      	orr	w16, w16, w14
fffffe00079b73e8:      	ldrb	w0, [x23, #5]
fffffe00079b73ec:      	eor	w15, w15, w11
fffffe00079b73f0:      	ldurb	w14, [x29, #-122]
fffffe00079b73f4:      	orr	w16, w16, w15
fffffe00079b73f8:      	ldrb	w1, [x23, #6]
fffffe00079b73fc:      	eor	w17, w17, w12
fffffe00079b7400:      	ldurb	w15, [x29, #-121]
fffffe00079b7404:      	orr	w17, w16, w17
fffffe00079b7408:      	ldrb	w2, [x23, #7]
fffffe00079b740c:      	eor	w0, w0, w13
fffffe00079b7410:      	ldurb	w16, [x29, #-120]
fffffe00079b7414:      	orr	w0, w17, w0
fffffe00079b7418:      	ldrb	w3, [x23, #8]
fffffe00079b741c:      	eor	w1, w1, w14
fffffe00079b7420:      	ldurb	w17, [x29, #-119]
fffffe00079b7424:      	orr	w1, w0, w1
fffffe00079b7428:      	ldrb	w4, [x23, #9]
fffffe00079b742c:      	eor	w2, w2, w15
fffffe00079b7430:      	ldurb	w0, [x29, #-118]
fffffe00079b7434:      	orr	w2, w1, w2
fffffe00079b7438:      	ldrb	w5, [x23, #10]
fffffe00079b743c:      	eor	w3, w3, w16
fffffe00079b7440:      	ldurb	w1, [x29, #-117]
fffffe00079b7444:      	orr	w3, w2, w3
fffffe00079b7448:      	ldrb	w6, [x23, #11]
fffffe00079b744c:      	eor	w4, w4, w17
fffffe00079b7450:      	ldurb	w2, [x29, #-116]
fffffe00079b7454:      	orr	w4, w3, w4
fffffe00079b7458:      	ldrb	w7, [x23, #12]
fffffe00079b745c:      	eor	w5, w5, w0
fffffe00079b7460:      	ldurb	w3, [x29, #-115]
fffffe00079b7464:      	orr	w4, w4, w5
fffffe00079b7468:      	ldrb	w25, [x23, #13]
fffffe00079b746c:      	eor	w6, w6, w1
fffffe00079b7470:      	ldurb	w5, [x29, #-114]
fffffe00079b7474:      	orr	w4, w4, w6
fffffe00079b7478:      	ldrb	w26, [x23, #14]
fffffe00079b747c:      	eor	w7, w7, w2
fffffe00079b7480:      	ldurb	w6, [x29, #-113]
fffffe00079b7484:      	orr	w4, w4, w7
fffffe00079b7488:      	ldrb	w27, [x23, #15]
fffffe00079b748c:      	eor	w7, w25, w3
fffffe00079b7490:      	orr	w4, w4, w7
fffffe00079b7494:      	eor	w7, w26, w5
fffffe00079b7498:      	orr	w4, w4, w7
fffffe00079b749c:      	eor	w7, w27, w6
fffffe00079b74a0:      	orr	w4, w4, w7
fffffe00079b74a4:      	tst	w4, #0xff
fffffe00079b74a8:      	b.eq	0xfffffe00079b7660 <_group_receive+0x8e0>
fffffe00079b74ac:      	ldr	w8, [x20, #236]
fffffe00079b74b0:      	str	x8, [sp]
fffffe00079b74b4:      	cbz	w8, 0xfffffe00079b759c <_group_receive+0x81c>
fffffe00079b74b8:      	ldr	x25, [sp]
fffffe00079b74bc:      	add	x7, x20, #911
fffffe00079b74c0:      	ldurb	w26, [x7, #-15]
fffffe00079b74c4:      	ldurb	w27, [x7, #-14]
fffffe00079b74c8:      	ldr	w8, [sp, #8]
fffffe00079b74cc:      	ldurb	w28, [x7, #-13]
fffffe00079b74d0:      	ldurb	w4, [x7, #-12]
fffffe00079b74d4:      	eor	w27, w27, w9
fffffe00079b74d8:      	eor	w26, w26, w8
fffffe00079b74dc:      	ldurb	w8, [x7, #-11]
fffffe00079b74e0:      	eor	w28, w28, w10
fffffe00079b74e4:      	orr	w26, w26, w27
fffffe00079b74e8:      	ldurb	w27, [x7, #-10]
fffffe00079b74ec:      	eor	w4, w4, w11
fffffe00079b74f0:      	orr	w26, w26, w28
fffffe00079b74f4:      	eor	w8, w8, w12
fffffe00079b74f8:      	orr	w4, w26, w4
fffffe00079b74fc:      	ldurb	w26, [x7, #-9]
fffffe00079b7500:      	orr	w8, w4, w8
fffffe00079b7504:      	eor	w4, w27, w13
fffffe00079b7508:      	ldurb	w27, [x7, #-8]
fffffe00079b750c:      	orr	w8, w8, w4
fffffe00079b7510:      	eor	w4, w26, w14
fffffe00079b7514:      	ldurb	w26, [x7, #-7]
fffffe00079b7518:      	orr	w8, w8, w4
fffffe00079b751c:      	ldrb	w28, [x7]
fffffe00079b7520:      	eor	w4, w27, w15
fffffe00079b7524:      	ldurb	w27, [x7, #-5]
fffffe00079b7528:      	orr	w8, w8, w4
fffffe00079b752c:      	ldurb	w4, [x7, #-6]
fffffe00079b7530:      	eor	w26, w26, w16
fffffe00079b7534:      	orr	w8, w8, w26
fffffe00079b7538:      	ldurb	w26, [x7, #-4]
fffffe00079b753c:      	eor	w4, w4, w17
fffffe00079b7540:      	orr	w8, w8, w4
fffffe00079b7544:      	eor	w4, w27, w0
fffffe00079b7548:      	orr	w8, w8, w4
fffffe00079b754c:      	ldurb	w4, [x7, #-3]
fffffe00079b7550:      	eor	w26, w26, w1
fffffe00079b7554:      	ldurb	w27, [x7, #-2]
fffffe00079b7558:      	orr	w8, w8, w26
fffffe00079b755c:      	ldurb	w26, [x7, #-1]
fffffe00079b7560:      	eor	w4, w4, w2
fffffe00079b7564:      	orr	w8, w8, w4
fffffe00079b7568:      	eor	w4, w27, w3
fffffe00079b756c:      	orr	w8, w8, w4
fffffe00079b7570:      	eor	w4, w26, w5
fffffe00079b7574:      	orr	w8, w8, w4
fffffe00079b7578:      	eor	w4, w28, w6
fffffe00079b757c:      	orr	w8, w8, w4
fffffe00079b7580:      	cbz	w8, 0xfffffe00079b774c <_group_receive+0x9cc>
fffffe00079b7584:      	add	x7, x7, #16
fffffe00079b7588:      	subs	x25, x25, #1
fffffe00079b758c:      	b.ne	0xfffffe00079b74c0 <_group_receive+0x740>
fffffe00079b7590:      	ldr	x8, [sp]
fffffe00079b7594:      	cmp	w8, #31
fffffe00079b7598:      	b.hi	0xfffffe00079b76d8 <_group_receive+0x958>
fffffe00079b759c:      	ldr	x8, [x20, #8]
fffffe00079b75a0:      	cmn	x8, #1
fffffe00079b75a4:      	b.eq	0xfffffe00079b76d8 <_group_receive+0x958>
fffffe00079b75a8:      	ldr	x9, [sp]
fffffe00079b75ac:      	add	x0, x20, #256
fffffe00079b75b0:      	ldr	q0, [x24, #48]
fffffe00079b75b4:      	mov	w2, w22
fffffe00079b75b8:      	mov	x1, x21
fffffe00079b75bc:      	mov	w3, #640
fffffe00079b75c0:      	add	w8, w9, #1
fffffe00079b75c4:      	add	x9, x20, x9, lsl #4
fffffe00079b75c8:      	str	w8, [x20, #236]
fffffe00079b75cc:      	str	q0, [x9, #896]
fffffe00079b75d0:      	ldur	x8, [x21, #9]
fffffe00079b75d4:      	str	x8, [x20, #248]
fffffe00079b75d8:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b75dc:      	ldr	q0, [x24, #48]
fffffe00079b75e0:      	str	w22, [x20, #232]
fffffe00079b75e4:      	mov	w10, #1
fffffe00079b75e8:      	mov	w0, #2
fffffe00079b75ec:      	str	q0, [x23]
fffffe00079b75f0:      	sturb	wzr, [x29, #-128]
fffffe00079b75f4:      	sturb	wzr, [x29, #-127]
fffffe00079b75f8:      	sturb	wzr, [x29, #-126]
fffffe00079b75fc:      	sturb	wzr, [x29, #-125]
fffffe00079b7600:      	sturb	wzr, [x29, #-124]
fffffe00079b7604:      	sturb	wzr, [x29, #-123]
fffffe00079b7608:      	sturb	wzr, [x29, #-122]
fffffe00079b760c:      	sturb	wzr, [x29, #-121]
fffffe00079b7610:      	sturb	wzr, [x29, #-120]
fffffe00079b7614:      	sturb	wzr, [x29, #-119]
fffffe00079b7618:      	sturb	wzr, [x29, #-118]
fffffe00079b761c:      	sturb	wzr, [x29, #-117]
fffffe00079b7620:      	sturb	wzr, [x29, #-116]
fffffe00079b7624:      	sturb	wzr, [x29, #-115]
fffffe00079b7628:      	sturb	wzr, [x29, #-114]
fffffe00079b762c:      	sturb	wzr, [x29, #-113]
fffffe00079b7630:      	ldr	w8, [sp, #12]
fffffe00079b7634:      	str	w8, [x20, #1408]
fffffe00079b7638:      	ldr	x8, [x20, #8]
fffffe00079b763c:      	ldur	x9, [x21, #65]
fffffe00079b7640:      	str	w10, [x20, #228]
fffffe00079b7644:      	add	x8, x8, #1
fffffe00079b7648:      	stur	x9, [x20, #220]
fffffe00079b764c:      	str	x8, [x20, #240]
fffffe00079b7650:      	stp	x8, x8, [x20, #8]
fffffe00079b7654:      	str	x8, [x19, #16]
fffffe00079b7658:      	str	w0, [x19]
fffffe00079b765c:      	b	0xfffffe00079b6f88 <_group_receive+0x208>
fffffe00079b7660:      	ldr	w8, [sp, #12]
fffffe00079b7664:      	ldr	w9, [x20, #1408]
fffffe00079b7668:      	cmp	w8, w9
fffffe00079b766c:      	b.ne	0xfffffe00079b77b8 <_group_receive+0xa38>
fffffe00079b7670:      	ldur	x8, [x21, #9]
fffffe00079b7674:      	add	x0, x20, #256
fffffe00079b7678:      	mov	w2, w22
fffffe00079b767c:      	mov	x1, x21
fffffe00079b7680:      	mov	w3, #640
fffffe00079b7684:      	str	x8, [x20, #248]
fffffe00079b7688:      	bl	0xfffffe000746b2a4 <___memcpy_chk>
fffffe00079b768c:      	str	w22, [x20, #232]
fffffe00079b7690:      	mov	w8, #2
fffffe00079b7694:      	sturb	wzr, [x29, #-128]
fffffe00079b7698:      	sturb	wzr, [x29, #-127]
fffffe00079b769c:      	sturb	wzr, [x29, #-126]
fffffe00079b76a0:      	sturb	wzr, [x29, #-125]
fffffe00079b76a4:      	sturb	wzr, [x29, #-124]
fffffe00079b76a8:      	sturb	wzr, [x29, #-123]
fffffe00079b76ac:      	sturb	wzr, [x29, #-122]
fffffe00079b76b0:      	sturb	wzr, [x29, #-121]
fffffe00079b76b4:      	sturb	wzr, [x29, #-120]
fffffe00079b76b8:      	sturb	wzr, [x29, #-119]
fffffe00079b76bc:      	sturb	wzr, [x29, #-118]
fffffe00079b76c0:      	sturb	wzr, [x29, #-117]
fffffe00079b76c4:      	sturb	wzr, [x29, #-116]
fffffe00079b76c8:      	sturb	wzr, [x29, #-115]
fffffe00079b76cc:      	sturb	wzr, [x29, #-114]
fffffe00079b76d0:      	sturb	wzr, [x29, #-113]
fffffe00079b76d4:      	b	0xfffffe00079b72cc <_group_receive+0x54c>
fffffe00079b76d8:      	sturb	wzr, [x29, #-128]
fffffe00079b76dc:      	sturb	wzr, [x29, #-127]
fffffe00079b76e0:      	sturb	wzr, [x29, #-126]
fffffe00079b76e4:      	sturb	wzr, [x29, #-125]
fffffe00079b76e8:      	sturb	wzr, [x29, #-124]
fffffe00079b76ec:      	sturb	wzr, [x29, #-123]
fffffe00079b76f0:      	sturb	wzr, [x29, #-122]
fffffe00079b76f4:      	sturb	wzr, [x29, #-121]
fffffe00079b76f8:      	sturb	wzr, [x29, #-120]
fffffe00079b76fc:      	sturb	wzr, [x29, #-119]
fffffe00079b7700:      	sturb	wzr, [x29, #-118]
fffffe00079b7704:      	sturb	wzr, [x29, #-117]
fffffe00079b7708:      	sturb	wzr, [x29, #-116]
fffffe00079b770c:      	sturb	wzr, [x29, #-115]
fffffe00079b7710:      	sturb	wzr, [x29, #-114]
fffffe00079b7714:      	sturb	wzr, [x29, #-113]
fffffe00079b7718:      	cbz	x20, 0xfffffe00079b7730 <_group_receive+0x9b0>
fffffe00079b771c:      	mov	x8, xzr
fffffe00079b7720:      	strb	wzr, [x20, x8]
fffffe00079b7724:      	add	x8, x8, #1
fffffe00079b7728:      	cmp	x8, #2616
fffffe00079b772c:      	b.ne	0xfffffe00079b7720 <_group_receive+0x9a0>
fffffe00079b7730:      	mov	x8, xzr
fffffe00079b7734:      	strb	wzr, [x19, x8]
fffffe00079b7738:      	add	x8, x8, #1
fffffe00079b773c:      	cmp	x8, #280
fffffe00079b7740:      	b.ne	0xfffffe00079b7734 <_group_receive+0x9b4>
fffffe00079b7744:      	mov	w8, #1
fffffe00079b7748:      	b	0xfffffe00079b6f80 <_group_receive+0x200>
fffffe00079b774c:      	mov	x8, xzr
fffffe00079b7750:      	sturb	wzr, [x29, #-128]
fffffe00079b7754:      	sturb	wzr, [x29, #-127]
fffffe00079b7758:      	sturb	wzr, [x29, #-126]
fffffe00079b775c:      	sturb	wzr, [x29, #-125]
fffffe00079b7760:      	sturb	wzr, [x29, #-124]
fffffe00079b7764:      	sturb	wzr, [x29, #-123]
fffffe00079b7768:      	sturb	wzr, [x29, #-122]
fffffe00079b776c:      	sturb	wzr, [x29, #-121]
fffffe00079b7770:      	sturb	wzr, [x29, #-120]
fffffe00079b7774:      	sturb	wzr, [x29, #-119]
fffffe00079b7778:      	sturb	wzr, [x29, #-118]
fffffe00079b777c:      	sturb	wzr, [x29, #-117]
fffffe00079b7780:      	sturb	wzr, [x29, #-116]
fffffe00079b7784:      	sturb	wzr, [x29, #-115]
fffffe00079b7788:      	sturb	wzr, [x29, #-114]
fffffe00079b778c:      	sturb	wzr, [x29, #-113]
fffffe00079b7790:      	strb	wzr, [x20, x8]
fffffe00079b7794:      	add	x8, x8, #1
fffffe00079b7798:      	cmp	x8, #2616
fffffe00079b779c:      	b.ne	0xfffffe00079b7790 <_group_receive+0xa10>
fffffe00079b77a0:      	mov	x8, xzr
fffffe00079b77a4:      	strb	wzr, [x19, x8]
fffffe00079b77a8:      	add	x8, x8, #1
fffffe00079b77ac:      	cmp	x8, #280
fffffe00079b77b0:      	b.ne	0xfffffe00079b77a4 <_group_receive+0xa24>
fffffe00079b77b4:      	b	0xfffffe00079b7388 <_group_receive+0x608>
fffffe00079b77b8:      	mov	x8, xzr
fffffe00079b77bc:      	sturb	wzr, [x29, #-128]
fffffe00079b77c0:      	sturb	wzr, [x29, #-127]
fffffe00079b77c4:      	sturb	wzr, [x29, #-126]
fffffe00079b77c8:      	sturb	wzr, [x29, #-125]
fffffe00079b77cc:      	sturb	wzr, [x29, #-124]
fffffe00079b77d0:      	sturb	wzr, [x29, #-123]
fffffe00079b77d4:      	sturb	wzr, [x29, #-122]
fffffe00079b77d8:      	sturb	wzr, [x29, #-121]
fffffe00079b77dc:      	sturb	wzr, [x29, #-120]
fffffe00079b77e0:      	sturb	wzr, [x29, #-119]
fffffe00079b77e4:      	sturb	wzr, [x29, #-118]
fffffe00079b77e8:      	sturb	wzr, [x29, #-117]
fffffe00079b77ec:      	sturb	wzr, [x29, #-116]
fffffe00079b77f0:      	sturb	wzr, [x29, #-115]
fffffe00079b77f4:      	sturb	wzr, [x29, #-114]
fffffe00079b77f8:      	sturb	wzr, [x29, #-113]
fffffe00079b77fc:      	strb	wzr, [x20, x8]
fffffe00079b7800:      	add	x8, x8, #1
fffffe00079b7804:      	cmp	x8, #2616
fffffe00079b7808:      	b.ne	0xfffffe00079b77fc <_group_receive+0xa7c>
fffffe00079b780c:      	mov	x8, xzr
fffffe00079b7810:      	strb	wzr, [x19, x8]
fffffe00079b7814:      	add	x8, x8, #1
fffffe00079b7818:      	cmp	x8, #280
fffffe00079b781c:      	b.ne	0xfffffe00079b7810 <_group_receive+0xa90>
fffffe00079b7820:      	b	0xfffffe00079b7388 <_group_receive+0x608>
fffffe00079b7824:      	bl	0xfffffe0007a06354 <___stack_chk_fail>
