
$NORD2_REPO/research/ci-39823157589/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a1d1c <_niwj_decode_identity>:
fffffe00079a1d1c:      	sub	sp, sp, #64
fffffe00079a1d20:      	stp	x29, x30, [sp, #48]
fffffe00079a1d24:      	add	x29, sp, #48
fffffe00079a1d28:      	cbz	x4, 0xfffffe00079a1d40 <_niwj_decode_identity+0x24>
fffffe00079a1d2c:      	mov	x8, xzr
fffffe00079a1d30:      	strb	wzr, [x4, x8]
fffffe00079a1d34:      	add	x8, x8, #1
fffffe00079a1d38:      	cmp	x8, #76
fffffe00079a1d3c:      	b.ne	0xfffffe00079a1d30 <_niwj_decode_identity+0x14>
fffffe00079a1d40:      	cbz	x3, 0xfffffe00079a1f1c <_niwj_decode_identity+0x200>
fffffe00079a1d44:      	mov	x8, xzr
fffffe00079a1d48:      	strb	wzr, [x3, x8]
fffffe00079a1d4c:      	add	x8, x8, #1
fffffe00079a1d50:      	cmp	x8, #104
fffffe00079a1d54:      	b.ne	0xfffffe00079a1d48 <_niwj_decode_identity+0x2c>
fffffe00079a1d58:      	cbz	x0, 0xfffffe00079a1f1c <_niwj_decode_identity+0x200>
fffffe00079a1d5c:      	cmp	x1, #592
fffffe00079a1d60:      	b.ne	0xfffffe00079a1f1c <_niwj_decode_identity+0x200>
fffffe00079a1d64:      	cbz	x4, 0xfffffe00079a1eac <_niwj_decode_identity+0x190>
fffffe00079a1d68:      	ldrh	w8, [x0, #4]
fffffe00079a1d6c:      	add	x9, x0, #463
fffffe00079a1d70:      	str	w8, [x4]
fffffe00079a1d74:      	ldrh	w8, [x0, #6]
fffffe00079a1d78:      	str	w8, [x4, #4]
fffffe00079a1d7c:      	ldrh	w8, [x0, #8]
fffffe00079a1d80:      	str	w8, [x4, #8]
fffffe00079a1d84:      	ldrh	w8, [x0, #204]
fffffe00079a1d88:      	str	w8, [x4, #12]
fffffe00079a1d8c:      	ldr	w8, [x0, #64]
fffffe00079a1d90:      	str	w8, [x4, #20]
fffffe00079a1d94:      	ldr	w8, [x0, #60]
fffffe00079a1d98:      	str	w8, [x4, #24]
fffffe00079a1d9c:      	ldr	w8, [x0, #504]
fffffe00079a1da0:      	str	w8, [x4, #28]
fffffe00079a1da4:      	ldrh	w8, [x9]
fffffe00079a1da8:      	str	w8, [x4, #32]
fffffe00079a1dac:      	ldrb	w8, [x0, #465]
fffffe00079a1db0:      	str	w8, [x4, #36]
fffffe00079a1db4:      	ldr	w8, [x0, #468]
fffffe00079a1db8:      	str	w8, [x4, #40]
fffffe00079a1dbc:      	ldrb	w8, [x0, #514]
fffffe00079a1dc0:      	str	w8, [x4, #44]
fffffe00079a1dc4:      	ldr	w8, [x0, #536]
fffffe00079a1dc8:      	str	w8, [x4, #48]
fffffe00079a1dcc:      	ldr	w8, [x0, #580]
fffffe00079a1dd0:      	str	w8, [x4, #52]
fffffe00079a1dd4:      	ldr	w8, [x0, #584]
fffffe00079a1dd8:      	str	w8, [x4, #56]
fffffe00079a1ddc:      	ldr	w8, [x0, #588]
fffffe00079a1de0:      	str	w8, [x4, #60]
fffffe00079a1de4:      	ldrb	w8, [x0, #464]
fffffe00079a1de8:      	ubfx	w8, w8, #3, #1
fffffe00079a1dec:      	str	w8, [x4, #64]
fffffe00079a1df0:      	cbz	x2, 0xfffffe00079a1ea0 <_niwj_decode_identity+0x184>
fffffe00079a1df4:      	ldrb	w9, [x2, #1]
fffffe00079a1df8:      	ldrb	w10, [x2, #2]
fffffe00079a1dfc:      	ldrb	w11, [x2, #3]
fffffe00079a1e00:      	ldrb	w12, [x2, #4]
fffffe00079a1e04:      	orr	w9, w9, w10
fffffe00079a1e08:      	ldrb	w10, [x2, #5]
fffffe00079a1e0c:      	orr	w11, w9, w11
fffffe00079a1e10:      	ldrb	w9, [x2]
fffffe00079a1e14:      	orr	w11, w11, w12
fffffe00079a1e18:      	orr	w10, w11, w10
fffffe00079a1e1c:      	orr	w10, w10, w9
fffffe00079a1e20:      	tst	w10, #0xff
fffffe00079a1e24:      	b.eq	0xfffffe00079a1ea0 <_niwj_decode_identity+0x184>
fffffe00079a1e28:      	and	w10, w9, #0x1
fffffe00079a1e2c:      	mov	w9, wzr
fffffe00079a1e30:      	eor	w11, w10, #0x1
fffffe00079a1e34:      	str	w11, [x4, #68]
fffffe00079a1e38:      	cbz	w8, 0xfffffe00079a1ea8 <_niwj_decode_identity+0x18c>
fffffe00079a1e3c:      	cbnz	w10, 0xfffffe00079a1ea8 <_niwj_decode_identity+0x18c>
fffffe00079a1e40:      	ldr	s0, [x0, #508]
fffffe00079a1e44:      	ldr	s1, [x2]
fffffe00079a1e48:      	ldrb	w12, [x0, #512]
fffffe00079a1e4c:      	ushll.8h	v0, v0, #0
fffffe00079a1e50:      	ldrb	w13, [x2, #4]
fffffe00079a1e54:      	ushll.8h	v1, v1, #0
fffffe00079a1e58:      	ldrb	w14, [x0, #513]
fffffe00079a1e5c:      	eor.8b	v0, v1, v0
fffffe00079a1e60:      	umov.h	w8, v0[1]
fffffe00079a1e64:      	umov.h	w9, v0[0]
fffffe00079a1e68:      	umov.h	w10, v0[2]
fffffe00079a1e6c:      	umov.h	w11, v0[3]
fffffe00079a1e70:      	orr	w8, w9, w8
fffffe00079a1e74:      	ldrb	w9, [x2, #5]
fffffe00079a1e78:      	orr	w8, w8, w10
fffffe00079a1e7c:      	eor	w10, w13, w12
fffffe00079a1e80:      	orr	w8, w8, w11
fffffe00079a1e84:      	and	w8, w8, #0xff
fffffe00079a1e88:      	eor	w9, w9, w14
fffffe00079a1e8c:      	orr	w8, w8, w10
fffffe00079a1e90:      	orr	w8, w8, w9
fffffe00079a1e94:      	cmp	w8, #0
fffffe00079a1e98:      	cset	w9, eq
fffffe00079a1e9c:      	b	0xfffffe00079a1ea8 <_niwj_decode_identity+0x18c>
fffffe00079a1ea0:      	mov	w9, wzr
fffffe00079a1ea4:      	str	wzr, [x4, #68]
fffffe00079a1ea8:      	str	w9, [x4, #72]
fffffe00079a1eac:      	ldr	w8, [x0]
fffffe00079a1eb0:      	cmp	w8, #1
fffffe00079a1eb4:      	b.ne	0xfffffe00079a1f1c <_niwj_decode_identity+0x200>
fffffe00079a1eb8:      	ldrb	w8, [x0, #12]
fffffe00079a1ebc:      	ldrb	w9, [x0, #13]
fffffe00079a1ec0:      	ldrb	w10, [x0, #14]
fffffe00079a1ec4:      	ldrb	w11, [x0, #15]
fffffe00079a1ec8:      	mov	w12, w8
fffffe00079a1ecc:      	bfi	w12, w9, #8, #8
fffffe00079a1ed0:      	bfi	w12, w10, #16, #8
fffffe00079a1ed4:      	bfi	w12, w11, #24, #8
fffffe00079a1ed8:      	sub	w9, w12, #33
fffffe00079a1edc:      	cmn	w9, #32
fffffe00079a1ee0:      	b.lo	0xfffffe00079a1f1c <_niwj_decode_identity+0x200>
fffffe00079a1ee4:      	ldrb	w9, [x0, #49]
fffffe00079a1ee8:      	ldrb	w10, [x0, #50]
fffffe00079a1eec:      	ldrb	w11, [x0, #51]
fffffe00079a1ef0:      	ldrb	w12, [x0, #52]
fffffe00079a1ef4:      	orr	w9, w9, w10
fffffe00079a1ef8:      	ldrb	w10, [x0, #53]
fffffe00079a1efc:      	orr	w9, w9, w11
fffffe00079a1f00:      	ldrb	w11, [x0, #48]
fffffe00079a1f04:      	orr	w9, w9, w12
fffffe00079a1f08:      	orr	w9, w9, w10
fffffe00079a1f0c:      	orr	w9, w9, w11
fffffe00079a1f10:      	and	w9, w9, #0xff
fffffe00079a1f14:      	tbz	w11, #0, 0xfffffe00079a1f40 <_niwj_decode_identity+0x224>
fffffe00079a1f18:      	cbz	w9, 0xfffffe00079a1f40 <_niwj_decode_identity+0x224>
fffffe00079a1f1c:      	cbz	x4, 0xfffffe00079a1f30 <_niwj_decode_identity+0x214>
fffffe00079a1f20:      	mov	w8, #1
fffffe00079a1f24:      	mov	w0, #-1
fffffe00079a1f28:      	str	w8, [x4, #16]
fffffe00079a1f2c:      	b	0xfffffe00079a1f34 <_niwj_decode_identity+0x218>
fffffe00079a1f30:      	mov	w0, #-1
fffffe00079a1f34:      	ldp	x29, x30, [sp, #48]
fffffe00079a1f38:      	add	sp, sp, #64
fffffe00079a1f3c:      	ret
fffffe00079a1f40:      	ldrh	w10, [x0, #4]
fffffe00079a1f44:      	cmp	w10, #2
fffffe00079a1f48:      	b.ne	0xfffffe00079a1fb0 <_niwj_decode_identity+0x294>
fffffe00079a1f4c:      	ldrh	w10, [x0, #6]
fffffe00079a1f50:      	cmp	w10, #1
fffffe00079a1f54:      	b.ne	0xfffffe00079a1fb0 <_niwj_decode_identity+0x294>
fffffe00079a1f58:      	ldrh	w10, [x0, #8]
fffffe00079a1f5c:      	cmp	w10, #8
fffffe00079a1f60:      	b.ne	0xfffffe00079a1fb0 <_niwj_decode_identity+0x294>
fffffe00079a1f64:      	ldr	w10, [x0, #56]
fffffe00079a1f68:      	cmp	w10, #1
fffffe00079a1f6c:      	b.ne	0xfffffe00079a1fa0 <_niwj_decode_identity+0x284>
fffffe00079a1f70:      	ldr	w10, [x0, #60]
fffffe00079a1f74:      	cmp	w10, #32
fffffe00079a1f78:      	b.ne	0xfffffe00079a1fa0 <_niwj_decode_identity+0x284>
fffffe00079a1f7c:      	ldr	w10, [x0, #64]
fffffe00079a1f80:      	cmp	w10, #6
fffffe00079a1f84:      	b.ne	0xfffffe00079a1fa0 <_niwj_decode_identity+0x284>
fffffe00079a1f88:      	ldrh	w10, [x0, #68]
fffffe00079a1f8c:      	cbnz	w10, 0xfffffe00079a1fa0 <_niwj_decode_identity+0x284>
fffffe00079a1f90:      	ldrh	w10, [x0, #70]
fffffe00079a1f94:      	cbnz	w10, 0xfffffe00079a1fa0 <_niwj_decode_identity+0x284>
fffffe00079a1f98:      	ldr	w10, [x0, #468]
fffffe00079a1f9c:      	cbz	w10, 0xfffffe00079a1fd0 <_niwj_decode_identity+0x2b4>
fffffe00079a1fa0:      	cbz	x4, 0xfffffe00079a1fc8 <_niwj_decode_identity+0x2ac>
fffffe00079a1fa4:      	mov	w8, #3
fffffe00079a1fa8:      	mov	w0, #-3
fffffe00079a1fac:      	b	0xfffffe00079a1f28 <_niwj_decode_identity+0x20c>
fffffe00079a1fb0:      	cbz	x4, 0xfffffe00079a1fc0 <_niwj_decode_identity+0x2a4>
fffffe00079a1fb4:      	mov	w8, #2
fffffe00079a1fb8:      	mov	w0, #-2
fffffe00079a1fbc:      	b	0xfffffe00079a1f28 <_niwj_decode_identity+0x20c>
fffffe00079a1fc0:      	mov	w0, #-2
fffffe00079a1fc4:      	b	0xfffffe00079a1f34 <_niwj_decode_identity+0x218>
fffffe00079a1fc8:      	mov	w0, #-3
fffffe00079a1fcc:      	b	0xfffffe00079a1f34 <_niwj_decode_identity+0x218>
fffffe00079a1fd0:      	str	x8, [sp]
fffffe00079a1fd4:      	add	x8, x0, #206
fffffe00079a1fd8:      	stur	x0, [x29, #-16]
fffffe00079a1fdc:      	ldrh	w1, [x0, #204]
fffffe00079a1fe0:      	mov	x0, x8
fffffe00079a1fe4:      	stur	w9, [x29, #-20]
fffffe00079a1fe8:      	str	x2, [sp, #16]
fffffe00079a1fec:      	stur	x4, [x29, #-8]
fffffe00079a1ff0:      	str	x3, [sp, #8]
fffffe00079a1ff4:      	bl	0xfffffe00079a2524 <_station_rsn_ok>
fffffe00079a1ff8:      	cbz	w0, 0xfffffe00079a2094 <_niwj_decode_identity+0x378>
fffffe00079a1ffc:      	ldur	x8, [x29, #-16]
fffffe00079a2000:      	ldrb	w9, [x8, #463]
fffffe00079a2004:      	tst	w9, #0xffffffdf
fffffe00079a2008:      	b.ne	0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a200c:      	ldur	x8, [x29, #-16]
fffffe00079a2010:      	mov	w10, #183
fffffe00079a2014:      	ldrb	w8, [x8, #464]
fffffe00079a2018:      	tst	w8, w10
fffffe00079a201c:      	b.ne	0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2020:      	ldur	x10, [x29, #-16]
fffffe00079a2024:      	ldrb	w10, [x10, #465]
fffffe00079a2028:      	cmp	w10, #2
fffffe00079a202c:      	b.hi	0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2030:      	tbz	w9, #5, 0xfffffe00079a203c <_niwj_decode_identity+0x320>
fffffe00079a2034:      	ldur	w9, [x29, #-20]
fffffe00079a2038:      	cbz	w9, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a203c:      	ldur	x9, [x29, #-16]
fffffe00079a2040:      	ldr	w9, [x9, #504]
fffffe00079a2044:      	orr	w9, w9, #0x4
fffffe00079a2048:      	cmp	w9, #4
fffffe00079a204c:      	b.ne	0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2050:      	ldur	x9, [x29, #-16]
fffffe00079a2054:      	ldrb	w9, [x9, #514]
fffffe00079a2058:      	cbnz	w9, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a205c:      	ldur	x9, [x29, #-16]
fffffe00079a2060:      	ldr	w9, [x9, #580]
fffffe00079a2064:      	cbnz	w9, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2068:      	ldur	x9, [x29, #-16]
fffffe00079a206c:      	ldr	w9, [x9, #584]
fffffe00079a2070:      	cbnz	w9, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2074:      	ldur	x9, [x29, #-16]
fffffe00079a2078:      	ldr	w9, [x9, #588]
fffffe00079a207c:      	cbz	w9, 0xfffffe00079a20c0 <_niwj_decode_identity+0x3a4>
fffffe00079a2080:      	ldur	x8, [x29, #-8]
fffffe00079a2084:      	cbz	x8, 0xfffffe00079a20b0 <_niwj_decode_identity+0x394>
fffffe00079a2088:      	mov	w9, #5
fffffe00079a208c:      	mov	w0, #-5
fffffe00079a2090:      	b	0xfffffe00079a20a4 <_niwj_decode_identity+0x388>
fffffe00079a2094:      	ldur	x8, [x29, #-8]
fffffe00079a2098:      	cbz	x8, 0xfffffe00079a20b8 <_niwj_decode_identity+0x39c>
fffffe00079a209c:      	mov	w9, #4
fffffe00079a20a0:      	mov	w0, #-4
fffffe00079a20a4:      	ldur	x8, [x29, #-8]
fffffe00079a20a8:      	str	w9, [x8, #16]
fffffe00079a20ac:      	b	0xfffffe00079a1f34 <_niwj_decode_identity+0x218>
fffffe00079a20b0:      	mov	w0, #-5
fffffe00079a20b4:      	b	0xfffffe00079a1f34 <_niwj_decode_identity+0x218>
fffffe00079a20b8:      	mov	w0, #-4
fffffe00079a20bc:      	b	0xfffffe00079a1f34 <_niwj_decode_identity+0x218>
fffffe00079a20c0:      	tbnz	w8, #3, 0xfffffe00079a2100 <_niwj_decode_identity+0x3e4>
fffffe00079a20c4:      	ldur	x9, [x29, #-16]
fffffe00079a20c8:      	ldr	w9, [x9, #536]
fffffe00079a20cc:      	tbnz	w8, #6, 0xfffffe00079a21bc <_niwj_decode_identity+0x4a0>
fffffe00079a20d0:      	cbnz	w9, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a20d4:      	ldp	x9, x8, [sp]
fffffe00079a20d8:      	strb	w9, [x8]
fffffe00079a20dc:      	cbz	w9, 0xfffffe00079a2378 <_niwj_decode_identity+0x65c>
fffffe00079a20e0:      	ldp	x10, x9, [sp]
fffffe00079a20e4:      	ldur	x8, [x29, #-16]
fffffe00079a20e8:      	cmp	w10, #8
fffffe00079a20ec:      	add	x8, x8, #16
fffffe00079a20f0:      	add	x9, x9, #1
fffffe00079a20f4:      	b.hs	0xfffffe00079a2200 <_niwj_decode_identity+0x4e4>
fffffe00079a20f8:      	ldr	x11, [sp]
fffffe00079a20fc:      	b	0xfffffe00079a2368 <_niwj_decode_identity+0x64c>
fffffe00079a2100:      	ldr	x9, [sp, #16]
fffffe00079a2104:      	cbz	x9, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2108:      	ldr	x9, [sp, #16]
fffffe00079a210c:      	ldrb	w9, [x9]
fffffe00079a2110:      	tbnz	w9, #0, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2114:      	ldr	x11, [sp, #16]
fffffe00079a2118:      	ldrb	w14, [x11, #1]
fffffe00079a211c:      	ldrb	w13, [x11, #2]
fffffe00079a2120:      	ldrb	w12, [x11, #3]
fffffe00079a2124:      	ldrb	w10, [x11, #4]
fffffe00079a2128:      	orr	w15, w14, w13
fffffe00079a212c:      	ldrb	w11, [x11, #5]
fffffe00079a2130:      	orr	w15, w15, w12
fffffe00079a2134:      	orr	w15, w15, w10
fffffe00079a2138:      	orr	w15, w15, w11
fffffe00079a213c:      	orr	w15, w15, w9
fffffe00079a2140:      	cbz	w15, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2144:      	ldur	x15, [x29, #-16]
fffffe00079a2148:      	ldrb	w15, [x15, #508]
fffffe00079a214c:      	and	w16, w15, #0x3
fffffe00079a2150:      	cmp	w16, #2
fffffe00079a2154:      	b.ne	0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2158:      	ldur	x17, [x29, #-16]
fffffe00079a215c:      	ldrb	w2, [x17, #509]
fffffe00079a2160:      	ldrb	w1, [x17, #510]
fffffe00079a2164:      	ldrb	w0, [x17, #511]
fffffe00079a2168:      	ldrb	w16, [x17, #512]
fffffe00079a216c:      	orr	w3, w2, w1
fffffe00079a2170:      	ldrb	w17, [x17, #513]
fffffe00079a2174:      	orr	w3, w3, w0
fffffe00079a2178:      	orr	w3, w3, w16
fffffe00079a217c:      	orr	w3, w3, w17
fffffe00079a2180:      	orr	w3, w3, w15
fffffe00079a2184:      	cbz	w3, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a2188:      	eor	w9, w9, w15
fffffe00079a218c:      	eor	w14, w14, w2
fffffe00079a2190:      	orr	w9, w9, w14
fffffe00079a2194:      	eor	w13, w13, w1
fffffe00079a2198:      	orr	w9, w9, w13
fffffe00079a219c:      	eor	w12, w12, w0
fffffe00079a21a0:      	orr	w9, w9, w12
fffffe00079a21a4:      	eor	w10, w10, w16
fffffe00079a21a8:      	orr	w9, w9, w10
fffffe00079a21ac:      	eor	w10, w11, w17
fffffe00079a21b0:      	orr	w9, w9, w10
fffffe00079a21b4:      	cbnz	w9, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a21b8:      	b	0xfffffe00079a20c4 <_niwj_decode_identity+0x3a8>
fffffe00079a21bc:      	sub	w8, w9, #37
fffffe00079a21c0:      	cmn	w8, #36
fffffe00079a21c4:      	b.lo	0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a21c8:      	ldur	x8, [x29, #-16]
fffffe00079a21cc:      	add	x8, x9, x8
fffffe00079a21d0:      	ldrb	w8, [x8, #540]
fffffe00079a21d4:      	cbnz	w8, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a21d8:      	mov	x8, xzr
fffffe00079a21dc:      	mov	w9, w9
fffffe00079a21e0:      	add	w11, w8, #540
fffffe00079a21e4:      	ldur	x10, [x29, #-16]
fffffe00079a21e8:      	ldrb	w10, [x10, w11, uxtw]
fffffe00079a21ec:      	cbz	w10, 0xfffffe00079a2080 <_niwj_decode_identity+0x364>
fffffe00079a21f0:      	add	x8, x8, #1
fffffe00079a21f4:      	cmp	x9, x8
fffffe00079a21f8:      	b.ne	0xfffffe00079a21e0 <_niwj_decode_identity+0x4c4>
fffffe00079a21fc:      	b	0xfffffe00079a20d4 <_niwj_decode_identity+0x3b8>
fffffe00079a2200:      	ldr	x10, [sp, #8]
fffffe00079a2204:      	ldur	x11, [x29, #-16]
fffffe00079a2208:      	sub	x10, x10, x11
fffffe00079a220c:      	sub	x10, x10, #15
fffffe00079a2210:      	cmp	x10, #32
fffffe00079a2214:      	b.hs	0xfffffe00079a2220 <_niwj_decode_identity+0x504>
fffffe00079a2218:      	ldr	x11, [sp]
fffffe00079a221c:      	b	0xfffffe00079a2368 <_niwj_decode_identity+0x64c>
fffffe00079a2220:      	ldr	x10, [sp]
fffffe00079a2224:      	cmp	w10, #32
fffffe00079a2228:      	b.hs	0xfffffe00079a2234 <_niwj_decode_identity+0x518>
fffffe00079a222c:      	mov	x10, xzr
fffffe00079a2230:      	b	0xfffffe00079a230c <_niwj_decode_identity+0x5f0>
fffffe00079a2234:      	ldur	x10, [x29, #-16]
fffffe00079a2238:      	ldp	q0, q1, [x10, #16]
fffffe00079a223c:      	ldp	x10, x11, [sp]
fffffe00079a2240:      	and	x10, x10, #0xe0
fffffe00079a2244:      	cmp	x10, #32
fffffe00079a2248:      	stur	q0, [x11, #1]
fffffe00079a224c:      	stur	q1, [x11, #17]
fffffe00079a2250:      	b.eq	0xfffffe00079a22f4 <_niwj_decode_identity+0x5d8>
fffffe00079a2254:      	ldur	x11, [x29, #-16]
fffffe00079a2258:      	cmp	x10, #64
fffffe00079a225c:      	ldp	q0, q1, [x11, #48]
fffffe00079a2260:      	ldr	x11, [sp, #8]
fffffe00079a2264:      	stur	q0, [x11, #33]
fffffe00079a2268:      	stur	q1, [x11, #49]
fffffe00079a226c:      	b.eq	0xfffffe00079a22f4 <_niwj_decode_identity+0x5d8>
fffffe00079a2270:      	ldur	x11, [x29, #-16]
fffffe00079a2274:      	cmp	x10, #96
fffffe00079a2278:      	ldp	q0, q1, [x11, #80]
fffffe00079a227c:      	ldr	x11, [sp, #8]
fffffe00079a2280:      	stur	q0, [x11, #65]
fffffe00079a2284:      	stur	q1, [x11, #81]
fffffe00079a2288:      	b.eq	0xfffffe00079a22f4 <_niwj_decode_identity+0x5d8>
fffffe00079a228c:      	ldur	x11, [x29, #-16]
fffffe00079a2290:      	cmp	x10, #128
fffffe00079a2294:      	ldp	q0, q1, [x11, #112]
fffffe00079a2298:      	ldr	x11, [sp, #8]
fffffe00079a229c:      	stur	q0, [x11, #97]
fffffe00079a22a0:      	stur	q1, [x11, #113]
fffffe00079a22a4:      	b.eq	0xfffffe00079a22f4 <_niwj_decode_identity+0x5d8>
fffffe00079a22a8:      	ldur	x11, [x29, #-16]
fffffe00079a22ac:      	cmp	x10, #160
fffffe00079a22b0:      	ldp	q0, q1, [x11, #144]
fffffe00079a22b4:      	ldr	x11, [sp, #8]
fffffe00079a22b8:      	stur	q0, [x11, #129]
fffffe00079a22bc:      	stur	q1, [x11, #145]
fffffe00079a22c0:      	b.eq	0xfffffe00079a22f4 <_niwj_decode_identity+0x5d8>
fffffe00079a22c4:      	ldur	x11, [x29, #-16]
fffffe00079a22c8:      	cmp	x10, #192
fffffe00079a22cc:      	ldp	q0, q1, [x11, #176]
fffffe00079a22d0:      	ldr	x11, [sp, #8]
fffffe00079a22d4:      	stur	q0, [x11, #161]
fffffe00079a22d8:      	stur	q1, [x11, #177]
fffffe00079a22dc:      	b.eq	0xfffffe00079a22f4 <_niwj_decode_identity+0x5d8>
fffffe00079a22e0:      	ldur	x11, [x29, #-16]
fffffe00079a22e4:      	ldp	q0, q1, [x11, #208]
fffffe00079a22e8:      	ldr	x11, [sp, #8]
fffffe00079a22ec:      	stur	q0, [x11, #193]
fffffe00079a22f0:      	stur	q1, [x11, #209]
fffffe00079a22f4:      	ldr	x11, [sp]
fffffe00079a22f8:      	cmp	x10, x11
fffffe00079a22fc:      	b.eq	0xfffffe00079a2378 <_niwj_decode_identity+0x65c>
fffffe00079a2300:      	ldr	x11, [sp]
fffffe00079a2304:      	tst	x11, #0x18
fffffe00079a2308:      	b.eq	0xfffffe00079a2358 <_niwj_decode_identity+0x63c>
fffffe00079a230c:      	ldp	x11, x13, [sp]
fffffe00079a2310:      	ldur	x14, [x29, #-16]
fffffe00079a2314:      	and	x12, x11, #0xf8
fffffe00079a2318:      	and	x11, x11, #0x7
fffffe00079a231c:      	add	x13, x10, x13
fffffe00079a2320:      	add	x14, x10, x14
fffffe00079a2324:      	add	x8, x8, x12
fffffe00079a2328:      	add	x9, x9, x12
fffffe00079a232c:      	add	x13, x13, #1
fffffe00079a2330:      	add	x14, x14, #16
fffffe00079a2334:      	sub	x10, x10, x12
fffffe00079a2338:      	ldr	d0, [x14], #8
fffffe00079a233c:      	adds	x10, x10, #8
fffffe00079a2340:      	str	d0, [x13], #8
fffffe00079a2344:      	b.ne	0xfffffe00079a2338 <_niwj_decode_identity+0x61c>
fffffe00079a2348:      	ldr	x10, [sp]
fffffe00079a234c:      	cmp	x12, x10
fffffe00079a2350:      	b.ne	0xfffffe00079a2368 <_niwj_decode_identity+0x64c>
fffffe00079a2354:      	b	0xfffffe00079a2378 <_niwj_decode_identity+0x65c>
fffffe00079a2358:      	ldr	x11, [sp]
fffffe00079a235c:      	add	x9, x9, x10
fffffe00079a2360:      	add	x8, x8, x10
fffffe00079a2364:      	and	x11, x11, #0x1f
fffffe00079a2368:      	ldrb	w10, [x8], #1
fffffe00079a236c:      	subs	x11, x11, #1
fffffe00079a2370:      	strb	w10, [x9], #1
fffffe00079a2374:      	b.ne	0xfffffe00079a2368 <_niwj_decode_identity+0x64c>
fffffe00079a2378:      	ldur	x8, [x29, #-16]
fffffe00079a237c:      	mov	w0, wzr
fffffe00079a2380:      	ldrb	w9, [x8, #48]
fffffe00079a2384:      	ldrb	w10, [x8, #49]
fffffe00079a2388:      	ldrb	w11, [x8, #50]
fffffe00079a238c:      	ldrb	w12, [x8, #51]
fffffe00079a2390:      	orr	w9, w10, w9
fffffe00079a2394:      	ldrb	w10, [x8, #52]
fffffe00079a2398:      	orr	w9, w9, w11
fffffe00079a239c:      	ldrb	w11, [x8, #53]
fffffe00079a23a0:      	orr	w9, w9, w12
fffffe00079a23a4:      	orr	w9, w9, w10
fffffe00079a23a8:      	orr	w9, w9, w11
fffffe00079a23ac:      	adrp	x11, 0xfffffe000702d000 <_niwi_scan_envelope+0xec>
fffffe00079a23b0:      	cmp	w9, #0
fffffe00079a23b4:      	ldr	x9, [sp, #8]
fffffe00079a23b8:      	cset	w10, ne
fffffe00079a23bc:      	ldr	q0, [x11, #2688]
fffffe00079a23c0:      	mov	w11, #3840
fffffe00079a23c4:      	movk	w11, #684, lsl #16
fffffe00079a23c8:      	strb	w10, [x9, #33]
fffffe00079a23cc:      	ldrb	w10, [x8, #48]
fffffe00079a23d0:      	strb	w10, [x9, #34]
fffffe00079a23d4:      	ldrb	w10, [x8, #49]
fffffe00079a23d8:      	strb	w10, [x9, #35]
fffffe00079a23dc:      	ldrb	w10, [x8, #50]
fffffe00079a23e0:      	strb	w10, [x9, #36]
fffffe00079a23e4:      	ldrb	w10, [x8, #51]
fffffe00079a23e8:      	strb	w10, [x9, #37]
fffffe00079a23ec:      	ldrb	w10, [x8, #52]
fffffe00079a23f0:      	strb	w10, [x9, #38]
fffffe00079a23f4:      	ldrb	w10, [x8, #53]
fffffe00079a23f8:      	strb	w10, [x9, #39]
fffffe00079a23fc:      	ldrb	w10, [x8, #72]
fffffe00079a2400:      	strb	w10, [x9, #40]
fffffe00079a2404:      	ldrb	w10, [x8, #73]
fffffe00079a2408:      	strb	w10, [x9, #41]
fffffe00079a240c:      	ldrb	w10, [x8, #74]
fffffe00079a2410:      	strb	w10, [x9, #42]
fffffe00079a2414:      	ldrb	w10, [x8, #75]
fffffe00079a2418:      	strb	w10, [x9, #43]
fffffe00079a241c:      	ldrb	w10, [x8, #76]
fffffe00079a2420:      	strb	w10, [x9, #44]
fffffe00079a2424:      	ldrb	w10, [x8, #77]
fffffe00079a2428:      	strb	w10, [x9, #45]
fffffe00079a242c:      	ldrb	w10, [x8, #78]
fffffe00079a2430:      	strb	w10, [x9, #46]
fffffe00079a2434:      	ldrb	w10, [x8, #79]
fffffe00079a2438:      	strb	w10, [x9, #47]
fffffe00079a243c:      	ldrb	w10, [x8, #80]
fffffe00079a2440:      	strb	w10, [x9, #48]
fffffe00079a2444:      	ldrb	w10, [x8, #81]
fffffe00079a2448:      	strb	w10, [x9, #49]
fffffe00079a244c:      	ldrb	w10, [x8, #82]
fffffe00079a2450:      	strb	w10, [x9, #50]
fffffe00079a2454:      	ldrb	w10, [x8, #83]
fffffe00079a2458:      	strb	w10, [x9, #51]
fffffe00079a245c:      	ldrb	w10, [x8, #84]
fffffe00079a2460:      	strb	w10, [x9, #52]
fffffe00079a2464:      	ldrb	w10, [x8, #85]
fffffe00079a2468:      	strb	w10, [x9, #53]
fffffe00079a246c:      	ldrb	w10, [x8, #86]
fffffe00079a2470:      	strb	w10, [x9, #54]
fffffe00079a2474:      	ldrb	w10, [x8, #87]
fffffe00079a2478:      	strb	w10, [x9, #55]
fffffe00079a247c:      	ldrb	w10, [x8, #88]
fffffe00079a2480:      	strb	w10, [x9, #56]
fffffe00079a2484:      	ldrb	w10, [x8, #89]
fffffe00079a2488:      	strb	w10, [x9, #57]
fffffe00079a248c:      	ldrb	w10, [x8, #90]
fffffe00079a2490:      	strb	w10, [x9, #58]
fffffe00079a2494:      	ldrb	w10, [x8, #91]
fffffe00079a2498:      	strb	w10, [x9, #59]
fffffe00079a249c:      	ldrb	w10, [x8, #92]
fffffe00079a24a0:      	strb	w10, [x9, #60]
fffffe00079a24a4:      	ldrb	w10, [x8, #93]
fffffe00079a24a8:      	strb	w10, [x9, #61]
fffffe00079a24ac:      	ldrb	w10, [x8, #94]
fffffe00079a24b0:      	strb	w10, [x9, #62]
fffffe00079a24b4:      	ldrb	w10, [x8, #95]
fffffe00079a24b8:      	strb	w10, [x9, #63]
fffffe00079a24bc:      	ldrb	w10, [x8, #96]
fffffe00079a24c0:      	strb	w10, [x9, #64]
fffffe00079a24c4:      	ldrb	w10, [x8, #97]
fffffe00079a24c8:      	strb	w10, [x9, #65]
fffffe00079a24cc:      	ldrb	w10, [x8, #98]
fffffe00079a24d0:      	strb	w10, [x9, #66]
fffffe00079a24d4:      	ldrb	w10, [x8, #99]
fffffe00079a24d8:      	strb	w10, [x9, #67]
fffffe00079a24dc:      	ldrb	w10, [x8, #100]
fffffe00079a24e0:      	strb	w10, [x9, #68]
fffffe00079a24e4:      	ldrb	w10, [x8, #101]
fffffe00079a24e8:      	strb	w10, [x9, #69]
fffffe00079a24ec:      	ldrb	w10, [x8, #102]
fffffe00079a24f0:      	strb	w10, [x9, #70]
fffffe00079a24f4:      	ldrb	w10, [x8, #103]
fffffe00079a24f8:      	stur	q0, [x9, #72]
fffffe00079a24fc:      	str	w11, [x9, #88]
fffffe00079a2500:      	mov	w11, #1
fffffe00079a2504:      	strh	wzr, [x9, #92]
fffffe00079a2508:      	strb	w10, [x9, #71]
fffffe00079a250c:      	ldrb	w10, [x8, #465]
fffffe00079a2510:      	str	w11, [x9, #96]
fffffe00079a2514:      	strb	w10, [x9, #94]
fffffe00079a2518:      	ldr	w8, [x8, #504]
fffffe00079a251c:      	str	w8, [x9, #100]
fffffe00079a2520:      	b	0xfffffe00079a1f34 <_niwj_decode_identity+0x218>
