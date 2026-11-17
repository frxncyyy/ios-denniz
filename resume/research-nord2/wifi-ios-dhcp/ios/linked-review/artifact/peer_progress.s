
$NORD2_REPO/research/ci-39865241324/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079aeabc <_peer_progress>:
fffffe00079aeabc:      	sub	sp, sp, #432
fffffe00079aeac0:      	stp	x24, x23, [sp, #368]
fffffe00079aeac4:      	stp	x22, x21, [sp, #384]
fffffe00079aeac8:      	stp	x20, x19, [sp, #400]
fffffe00079aeacc:      	stp	x29, x30, [sp, #416]
fffffe00079aead0:      	add	x29, sp, #416
fffffe00079aead4:      	adrp	x8, 0xfffffe0007c98000 <_nst_peer_begin+0x278>
fffffe00079aead8:      	nop
fffffe00079aeadc:      	ldr	x8, [x8, #3440]
fffffe00079aeae0:      	stur	x8, [x29, #-56]
fffffe00079aeae4:      	ldr	w8, [x0, #56]
fffffe00079aeae8:      	cbnz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aeaec:      	mov	x19, x0
fffffe00079aeaf0:      	ldr	w8, [x0, #60]
fffffe00079aeaf4:      	cbz	w8, 0xfffffe00079aeb2c <_peer_progress+0x70>
fffffe00079aeaf8:      	mov	w0, wzr
fffffe00079aeafc:      	adrp	x8, 0xfffffe0007c98000 <_nst_peer_begin+0x2a0>
fffffe00079aeb00:      	nop
fffffe00079aeb04:      	ldur	x9, [x29, #-56]
fffffe00079aeb08:      	ldr	x8, [x8, #3440]
fffffe00079aeb0c:      	cmp	x8, x9
fffffe00079aeb10:      	b.ne	0xfffffe00079af148 <_peer_progress+0x68c>
fffffe00079aeb14:      	ldp	x29, x30, [sp, #416]
fffffe00079aeb18:      	ldp	x20, x19, [sp, #400]
fffffe00079aeb1c:      	ldp	x22, x21, [sp, #384]
fffffe00079aeb20:      	ldp	x24, x23, [sp, #368]
fffffe00079aeb24:      	add	sp, sp, #432
fffffe00079aeb28:      	ret
fffffe00079aeb2c:      	ldr	w9, [x19, #1552]
fffffe00079aeb30:      	add	x12, sp, #96
fffffe00079aeb34:      	cbz	w9, 0xfffffe00079aebf0 <_peer_progress+0x134>
fffffe00079aeb38:      	mov	x11, #72339069014638592
fffffe00079aeb3c:      	ldrh	w13, [x19, #1640]
fffffe00079aeb40:      	ldrb	w8, [x19, #204]
fffffe00079aeb44:      	cmp	w9, #2
fffffe00079aeb48:      	ldrb	w10, [x19, #209]
fffffe00079aeb4c:      	mov	x20, x2
fffffe00079aeb50:      	stp	x11, xzr, [sp, #144]
fffffe00079aeb54:      	ldrb	w11, [x19, #210]
fffffe00079aeb58:      	ldrb	w14, [x19, #1652]
fffffe00079aeb5c:      	ubfx	w15, w13, #5, #1
fffffe00079aeb60:      	ubfx	w13, w13, #10, #1
fffffe00079aeb64:      	strb	w8, [sp, #144]
fffffe00079aeb68:      	strb	w10, [sp, #145]
fffffe00079aeb6c:      	ldr	x16, [x19, #1616]
fffffe00079aeb70:      	strb	w11, [sp, #146]
fffffe00079aeb74:      	mov	x21, x1
fffffe00079aeb78:      	strb	w14, [sp, #147]
fffffe00079aeb7c:      	add	x14, x19, #1656
fffffe00079aeb80:      	strb	w15, [sp, #148]
fffffe00079aeb84:      	cset	w15, eq
fffffe00079aeb88:      	strb	w13, [sp, #149]
fffffe00079aeb8c:      	cmp	w10, #2
fffffe00079aeb90:      	ldr	x13, [sp, #144]
fffffe00079aeb94:      	str	x16, [sp, #112]
fffffe00079aeb98:      	movi.2d	v0, #0000000000000000
fffffe00079aeb9c:      	ldrb	w16, [x19, #208]
fffffe00079aeba0:      	str	xzr, [sp, #352]
fffffe00079aeba4:      	stp	x13, x14, [sp, #96]
fffffe00079aeba8:      	ldrb	w13, [x19, #206]
fffffe00079aebac:      	ldr	w14, [x19, #1556]
fffffe00079aebb0:      	str	xzr, [sp, #128]
fffffe00079aebb4:      	stp	q0, q0, [sp, #160]
fffffe00079aebb8:      	stp	q0, q0, [sp, #192]
fffffe00079aebbc:      	stp	q0, q0, [sp, #224]
fffffe00079aebc0:      	stp	q0, q0, [sp, #256]
fffffe00079aebc4:      	stp	q0, q0, [sp, #288]
fffffe00079aebc8:      	stp	q0, q0, [sp, #320]
fffffe00079aebcc:      	strb	w15, [sp, #126]
fffffe00079aebd0:      	strb	w13, [sp, #127]
fffffe00079aebd4:      	strb	w16, [sp, #128]
fffffe00079aebd8:      	b.ne	0xfffffe00079aec84 <_peer_progress+0x1c8>
fffffe00079aebdc:      	ldrh	w16, [x19, #1702]
fffffe00079aebe0:      	mov	w1, #3
fffffe00079aebe4:      	mov	w0, #8
fffffe00079aebe8:      	and	w17, w16, #0x3fc0
fffffe00079aebec:      	b	0xfffffe00079aec98 <_peer_progress+0x1dc>
fffffe00079aebf0:      	ldr	w8, [x19, #1732]
fffffe00079aebf4:      	cbz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aebf8:      	ldr	w8, [x19, #1724]
fffffe00079aebfc:      	cbnz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aec00:      	ldr	w8, [x19, #1728]
fffffe00079aec04:      	cbnz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aec08:      	ldr	w8, [x19, #24]
fffffe00079aec0c:      	cbnz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aec10:      	ldr	w8, [x19, #1708]
fffffe00079aec14:      	cbnz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aec18:      	ldr	w8, [x19, #1524]
fffffe00079aec1c:      	cbz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aec20:      	ldrh	w8, [x19, #1700]
fffffe00079aec24:      	cbz	w8, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079aec28:      	ldrb	w9, [x19, #204]
fffffe00079aec2c:      	lsr	w11, w8, #8
fffffe00079aec30:      	ldr	w10, [x19, #1736]
fffffe00079aec34:      	add	x3, sp, #160
fffffe00079aec38:      	mov	x0, x19
fffffe00079aec3c:      	mov	x1, #-1
fffffe00079aec40:      	strb	w9, [sp, #160]
fffffe00079aec44:      	ldrh	w9, [x19, #1648]
fffffe00079aec48:      	mov	w2, #22
fffffe00079aec4c:      	mov	w4, #12
fffffe00079aec50:      	mov	w5, #2
fffffe00079aec54:      	strh	wzr, [sp, #170]
fffffe00079aec58:      	stur	wzr, [x12, #70]
fffffe00079aec5c:      	strb	w10, [sp, #161]
fffffe00079aec60:      	strb	w8, [sp, #162]
fffffe00079aec64:      	strb	w11, [sp, #163]
fffffe00079aec68:      	strh	w9, [sp, #164]
fffffe00079aec6c:      	bl	0xfffffe00079abd54 <_command>
fffffe00079aec70:      	cbnz	w0, 0xfffffe00079aeafc <_peer_progress+0x40>
fffffe00079aec74:      	mov	w8, #1
fffffe00079aec78:      	str	wzr, [x19, #1732]
fffffe00079aec7c:      	str	w8, [x19, #1724]
fffffe00079aec80:      	b	0xfffffe00079aeafc <_peer_progress+0x40>
fffffe00079aec84:      	ldrh	w16, [x19, #1702]
fffffe00079aec88:      	mov	w0, #3
fffffe00079aec8c:      	ands	w17, w16, #0x3fc0
fffffe00079aec90:      	cset	w1, ne
fffffe00079aec94:      	csinc	w0, w0, wzr, ne
fffffe00079aec98:      	ldrb	w7, [x19, #207]
fffffe00079aec9c:      	cmp	w9, #2
fffffe00079aeca0:      	mov	w22, #3
fffffe00079aeca4:      	strb	w0, [sp, #130]
fffffe00079aeca8:      	csinc	w23, w22, wzr, eq
fffffe00079aecac:      	cmp	w17, #0
fffffe00079aecb0:      	strb	w7, [sp, #66]
fffffe00079aecb4:      	mov	w7, #16895
fffffe00079aecb8:      	csinc	w17, w22, wzr, ne
fffffe00079aecbc:      	cmp	w10, #2
fffffe00079aecc0:      	mov	w0, #4
fffffe00079aecc4:      	strb	w1, [sp, #129]
fffffe00079aecc8:      	sturh	w7, [sp, #67]
fffffe00079aeccc:      	mov	w7, #8
fffffe00079aecd0:      	csel	w17, w7, w17, eq
fffffe00079aecd4:      	ldrb	w7, [x19, #1650]
fffffe00079aecd8:      	ldrh	w1, [x19, #1704]
fffffe00079aecdc:      	sturh	w0, [x12, #35]
fffffe00079aece0:      	ldrh	w2, [x19, #218]
fffffe00079aece4:      	strb	w15, [sp, #70]
fffffe00079aece8:      	ldrb	w12, [x19, #198]
fffffe00079aecec:      	strb	w17, [sp, #77]
fffffe00079aecf0:      	ldrb	w0, [x19, #199]
fffffe00079aecf4:      	strb	w7, [sp, #78]
fffffe00079aecf8:      	ldrb	w3, [x19, #200]
fffffe00079aecfc:      	cmp	w9, #1
fffffe00079aed00:      	ldrb	w4, [x19, #201]
fffffe00079aed04:      	strb	wzr, [sp, #133]
fffffe00079aed08:      	ldrb	w5, [x19, #202]
fffffe00079aed0c:      	strh	w16, [sp, #134]
fffffe00079aed10:      	ldrb	w6, [x19, #203]
fffffe00079aed14:      	strh	w1, [sp, #136]
fffffe00079aed18:      	ldrb	w15, [x19, #1651]
fffffe00079aed1c:      	strh	w2, [sp, #138]
fffffe00079aed20:      	ldrh	w17, [x19, #1700]
fffffe00079aed24:      	str	wzr, [sp, #140]
fffffe00079aed28:      	ldrh	w7, [x19, #1642]
fffffe00079aed2c:      	strb	w12, [sp, #120]
fffffe00079aed30:      	strb	w0, [sp, #121]
fffffe00079aed34:      	strb	w3, [sp, #122]
fffffe00079aed38:      	strb	w4, [sp, #123]
fffffe00079aed3c:      	strb	w5, [sp, #124]
fffffe00079aed40:      	strb	w6, [sp, #125]
fffffe00079aed44:      	strh	wzr, [sp, #80]
fffffe00079aed48:      	strb	w8, [sp, #64]
fffffe00079aed4c:      	strb	w13, [sp, #65]
fffffe00079aed50:      	strb	w23, [sp, #69]
fffffe00079aed54:      	strb	w22, [sp, #79]
fffffe00079aed58:      	strb	w15, [sp, #80]
fffffe00079aed5c:      	strh	w17, [sp, #82]
fffffe00079aed60:      	strh	w7, [sp, #84]
fffffe00079aed64:      	strh	w16, [sp, #86]
fffffe00079aed68:      	strh	w1, [sp, #88]
fffffe00079aed6c:      	strh	w2, [sp, #90]
fffffe00079aed70:      	strb	w12, [sp, #71]
fffffe00079aed74:      	strb	w0, [sp, #72]
fffffe00079aed78:      	strb	w3, [sp, #73]
fffffe00079aed7c:      	strb	w4, [sp, #74]
fffffe00079aed80:      	strb	w5, [sp, #75]
fffffe00079aed84:      	strb	w6, [sp, #76]
fffffe00079aed88:      	b.ne	0xfffffe00079aeec0 <_peer_progress+0x404>
fffffe00079aed8c:      	cbnz	w14, 0xfffffe00079aeec0 <_peer_progress+0x404>
fffffe00079aed90:      	strb	wzr, [sp, #160]
fffffe00079aed94:      	mov	w8, #13
fffffe00079aed98:      	strb	wzr, [sp, #161]
fffffe00079aed9c:      	add	x3, sp, #160
fffffe00079aeda0:      	strb	wzr, [sp, #162]
fffffe00079aeda4:      	mov	x0, x19
fffffe00079aeda8:      	strb	wzr, [sp, #163]
fffffe00079aedac:      	mov	w2, #10
fffffe00079aedb0:      	strb	wzr, [sp, #164]
fffffe00079aedb4:      	mov	w4, #68
fffffe00079aedb8:      	strb	wzr, [sp, #165]
fffffe00079aedbc:      	strb	wzr, [sp, #166]
fffffe00079aedc0:      	strb	wzr, [sp, #167]
fffffe00079aedc4:      	strb	wzr, [sp, #168]
fffffe00079aedc8:      	strb	wzr, [sp, #169]
fffffe00079aedcc:      	strb	wzr, [sp, #170]
fffffe00079aedd0:      	strb	wzr, [sp, #171]
fffffe00079aedd4:      	strb	wzr, [sp, #172]
fffffe00079aedd8:      	strb	wzr, [sp, #173]
fffffe00079aeddc:      	strb	wzr, [sp, #174]
fffffe00079aede0:      	strb	wzr, [sp, #175]
fffffe00079aede4:      	strb	wzr, [sp, #176]
fffffe00079aede8:      	strb	wzr, [sp, #177]
fffffe00079aedec:      	strb	wzr, [sp, #178]
fffffe00079aedf0:      	strb	wzr, [sp, #179]
fffffe00079aedf4:      	strb	wzr, [sp, #180]
fffffe00079aedf8:      	strb	wzr, [sp, #181]
fffffe00079aedfc:      	strb	wzr, [sp, #182]
fffffe00079aee00:      	strb	wzr, [sp, #183]
fffffe00079aee04:      	strb	wzr, [sp, #184]
fffffe00079aee08:      	strb	wzr, [sp, #185]
fffffe00079aee0c:      	strb	wzr, [sp, #186]
fffffe00079aee10:      	strb	wzr, [sp, #187]
fffffe00079aee14:      	strb	wzr, [sp, #188]
fffffe00079aee18:      	strb	wzr, [sp, #189]
fffffe00079aee1c:      	strb	wzr, [sp, #190]
fffffe00079aee20:      	strb	wzr, [sp, #191]
fffffe00079aee24:      	strb	wzr, [sp, #192]
fffffe00079aee28:      	strb	wzr, [sp, #193]
fffffe00079aee2c:      	strb	wzr, [sp, #194]
fffffe00079aee30:      	strb	wzr, [sp, #195]
fffffe00079aee34:      	strb	wzr, [sp, #196]
fffffe00079aee38:      	strb	wzr, [sp, #197]
fffffe00079aee3c:      	strb	wzr, [sp, #198]
fffffe00079aee40:      	strb	wzr, [sp, #199]
fffffe00079aee44:      	strb	wzr, [sp, #200]
fffffe00079aee48:      	strb	wzr, [sp, #201]
fffffe00079aee4c:      	strb	wzr, [sp, #202]
fffffe00079aee50:      	strb	wzr, [sp, #203]
fffffe00079aee54:      	strb	wzr, [sp, #204]
fffffe00079aee58:      	strb	wzr, [sp, #205]
fffffe00079aee5c:      	strb	wzr, [sp, #206]
fffffe00079aee60:      	strb	wzr, [sp, #207]
fffffe00079aee64:      	strb	wzr, [sp, #208]
fffffe00079aee68:      	strb	wzr, [sp, #209]
fffffe00079aee6c:      	strb	wzr, [sp, #210]
fffffe00079aee70:      	strb	wzr, [sp, #211]
fffffe00079aee74:      	strb	wzr, [sp, #212]
fffffe00079aee78:      	strb	wzr, [sp, #213]
fffffe00079aee7c:      	strb	wzr, [sp, #214]
fffffe00079aee80:      	strb	wzr, [sp, #215]
fffffe00079aee84:      	strb	wzr, [sp, #216]
fffffe00079aee88:      	strb	wzr, [sp, #217]
fffffe00079aee8c:      	strb	wzr, [sp, #218]
fffffe00079aee90:      	strb	wzr, [sp, #219]
fffffe00079aee94:      	strb	wzr, [sp, #220]
fffffe00079aee98:      	strb	wzr, [sp, #221]
fffffe00079aee9c:      	strb	wzr, [sp, #222]
fffffe00079aeea0:      	strb	wzr, [sp, #223]
fffffe00079aeea4:      	strb	wzr, [sp, #224]
fffffe00079aeea8:      	strb	wzr, [sp, #225]
fffffe00079aeeac:      	strb	wzr, [sp, #226]
fffffe00079aeeb0:      	strb	wzr, [sp, #227]
fffffe00079aeeb4:      	ldr	x1, [x19, #1568]
fffffe00079aeeb8:      	str	w8, [sp, #160]
fffffe00079aeebc:      	b	0xfffffe00079af12c <_peer_progress+0x670>
fffffe00079aeec0:      	cmp	w9, #1
fffffe00079aeec4:      	cset	w12, eq
fffffe00079aeec8:      	cmp	w9, #3
fffffe00079aeecc:      	sub	w12, w14, w12
fffffe00079aeed0:      	b.ne	0xfffffe00079aef08 <_peer_progress+0x44c>
fffffe00079aeed4:      	cbz	w12, 0xfffffe00079aef40 <_peer_progress+0x484>
fffffe00079aeed8:      	cmp	w12, #1
fffffe00079aeedc:      	b.eq	0xfffffe00079aef60 <_peer_progress+0x4a4>
fffffe00079aeee0:      	cmp	w12, #2
fffffe00079aeee4:      	b.ne	0xfffffe00079aef84 <_peer_progress+0x4c8>
fffffe00079aeee8:      	strb	wzr, [sp, #160]
fffffe00079aeeec:      	mov	w9, #4
fffffe00079aeef0:      	strb	wzr, [sp, #161]
fffffe00079aeef4:      	mov	w2, #23
fffffe00079aeef8:      	strb	wzr, [sp, #162]
fffffe00079aeefc:      	strb	wzr, [sp, #163]
fffffe00079aef00:      	strb	w8, [sp, #160]
fffffe00079aef04:      	b	0xfffffe00079aef58 <_peer_progress+0x49c>
fffffe00079aef08:      	cmp	w12, #2
fffffe00079aef0c:      	b.eq	0xfffffe00079af018 <_peer_progress+0x55c>
fffffe00079aef10:      	cmp	w12, #1
fffffe00079aef14:      	b.eq	0xfffffe00079af03c <_peer_progress+0x580>
fffffe00079aef18:      	cbnz	w12, 0xfffffe00079af068 <_peer_progress+0x5ac>
fffffe00079aef1c:      	add	x0, sp, #160
fffffe00079aef20:      	add	x2, sp, #152
fffffe00079aef24:      	add	x3, sp, #96
fffffe00079aef28:      	mov	w1, #200
fffffe00079aef2c:      	bl	0xfffffe00079aa488 <_sw_bss>
fffffe00079aef30:      	cbz	w0, 0xfffffe00079af0cc <_peer_progress+0x610>
fffffe00079aef34:      	mov	x0, x19
fffffe00079aef38:      	mov	w1, #31
fffffe00079aef3c:      	b	0xfffffe00079af05c <_peer_progress+0x5a0>
fffffe00079aef40:      	mov	w9, #4
fffffe00079aef44:      	mov	w2, #20
fffffe00079aef48:      	strb	wzr, [sp, #160]
fffffe00079aef4c:      	strb	w13, [sp, #161]
fffffe00079aef50:      	strb	w8, [sp, #162]
fffffe00079aef54:      	strb	wzr, [sp, #163]
fffffe00079aef58:      	str	x9, [sp, #152]
fffffe00079aef5c:      	b	0xfffffe00079af11c <_peer_progress+0x660>
fffffe00079aef60:      	add	x0, sp, #160
fffffe00079aef64:      	add	x2, sp, #152
fffffe00079aef68:      	add	x3, sp, #96
fffffe00079aef6c:      	mov	w1, #200
fffffe00079aef70:      	bl	0xfffffe00079aa488 <_sw_bss>
fffffe00079aef74:      	cbz	w0, 0xfffffe00079af0cc <_peer_progress+0x610>
fffffe00079aef78:      	mov	x0, x19
fffffe00079aef7c:      	mov	w1, #30
fffffe00079aef80:      	b	0xfffffe00079af05c <_peer_progress+0x5a0>
fffffe00079aef84:      	mov	w8, #9776
fffffe00079aef88:      	ldr	x1, [x19, #152]
fffffe00079aef8c:      	add	x0, x19, x8
fffffe00079aef90:      	bl	0xfffffe00079b2520 <_nsdd_summary>
fffffe00079aef94:      	movi.2d	v0, #0000000000000000
fffffe00079aef98:      	add	x9, x19, #1524
fffffe00079aef9c:      	mov	w8, #632
fffffe00079aefa0:      	str	wzr, [x19, #1552]
fffffe00079aefa4:      	str	d0, [x9]
fffffe00079aefa8:      	strb	wzr, [x19, x8]
fffffe00079aefac:      	add	x8, x8, #1
fffffe00079aefb0:      	cmp	x8, #776
fffffe00079aefb4:      	b.ne	0xfffffe00079aefa8 <_peer_progress+0x4ec>
fffffe00079aefb8:      	strb	wzr, [x19, x8]
fffffe00079aefbc:      	add	x8, x8, #1
fffffe00079aefc0:      	cmp	x8, #920
fffffe00079aefc4:      	b.ne	0xfffffe00079aefb8 <_peer_progress+0x4fc>
fffffe00079aefc8:      	strb	wzr, [x19, x8]
fffffe00079aefcc:      	add	x8, x8, #1
fffffe00079aefd0:      	cmp	x8, #1496
fffffe00079aefd4:      	b.ne	0xfffffe00079aefc8 <_peer_progress+0x50c>
fffffe00079aefd8:      	ldp	x1, x2, [x19, #152]
fffffe00079aefdc:      	movi.2d	v0, #0000000000000000
fffffe00079aefe0:      	mov	x0, sp
fffffe00079aefe4:      	str	xzr, [sp, #48]
fffffe00079aefe8:      	stp	q0, q0, [sp, #16]
fffffe00079aefec:      	str	q0, [sp]
fffffe00079aeff0:      	bl	0xfffffe00079b1500 <_nstd_summary>
fffffe00079aeff4:      	mov	x0, sp
fffffe00079aeff8:      	bl	0xfffffe00079b145c <_nstd_log>
fffffe00079aeffc:      	movi.2d	v0, #0000000000000000
fffffe00079af000:      	ldr	x9, [x19, #1568]
fffffe00079af004:      	mov	w8, #11
fffffe00079af008:      	str	xzr, [sp, #48]
fffffe00079af00c:      	stp	q0, q0, [sp]
fffffe00079af010:      	movi.2d	v0, #0000000000000000
fffffe00079af014:      	b	0xfffffe00079af0ec <_peer_progress+0x630>
fffffe00079af018:      	add	x0, sp, #160
fffffe00079af01c:      	add	x2, sp, #152
fffffe00079af020:      	add	x3, sp, #64
fffffe00079af024:      	mov	w1, #200
fffffe00079af028:      	bl	0xfffffe00079aa798 <_sw_sta>
fffffe00079af02c:      	cbz	w0, 0xfffffe00079af110 <_peer_progress+0x654>
fffffe00079af030:      	mov	x0, x19
fffffe00079af034:      	mov	w1, #33
fffffe00079af038:      	b	0xfffffe00079af05c <_peer_progress+0x5a0>
fffffe00079af03c:      	add	x0, sp, #160
fffffe00079af040:      	add	x2, sp, #152
fffffe00079af044:      	add	x3, sp, #144
fffffe00079af048:      	mov	w1, #200
fffffe00079af04c:      	bl	0xfffffe00079aa354 <_sw_rlm>
fffffe00079af050:      	cbz	w0, 0xfffffe00079af118 <_peer_progress+0x65c>
fffffe00079af054:      	mov	x0, x19
fffffe00079af058:      	mov	w1, #32
fffffe00079af05c:      	bl	0xfffffe00079afe2c <_fail>
fffffe00079af060:      	mov	w0, #-1
fffffe00079af064:      	b	0xfffffe00079aeafc <_peer_progress+0x40>
fffffe00079af068:      	cmp	w9, #2
fffffe00079af06c:      	b.ne	0xfffffe00079af0d4 <_peer_progress+0x618>
fffffe00079af070:      	ldr	w9, [x19, #1524]
fffffe00079af074:      	cbz	w9, 0xfffffe00079aeaf8 <_peer_progress+0x3c>
fffffe00079af078:      	ldrb	w9, [x19, #1585]
fffffe00079af07c:      	cbz	w9, 0xfffffe00079af138 <_peer_progress+0x67c>
fffffe00079af080:      	strb	w8, [sp]
fffffe00079af084:      	mov	w8, #1
fffffe00079af088:      	ldrb	w9, [x19, #1584]
fffffe00079af08c:      	add	x0, sp, #160
fffffe00079af090:      	add	x2, sp, #152
fffffe00079af094:      	mov	x3, sp
fffffe00079af098:      	strb	w8, [sp, #2]
fffffe00079af09c:      	mov	w8, #4
fffffe00079af0a0:      	mov	w1, #200
fffffe00079af0a4:      	strb	w10, [sp, #3]
fffffe00079af0a8:      	strb	w9, [sp, #1]
fffffe00079af0ac:      	stp	wzr, wzr, [sp, #4]
fffffe00079af0b0:      	strb	w11, [sp, #4]
fffffe00079af0b4:      	strb	w8, [sp, #5]
fffffe00079af0b8:      	bl	0xfffffe00079aa200 <_sw_channel>
fffffe00079af0bc:      	cbz	w0, 0xfffffe00079af140 <_peer_progress+0x684>
fffffe00079af0c0:      	mov	x0, x19
fffffe00079af0c4:      	mov	w1, #34
fffffe00079af0c8:      	b	0xfffffe00079af05c <_peer_progress+0x5a0>
fffffe00079af0cc:      	mov	w2, #18
fffffe00079af0d0:      	b	0xfffffe00079af11c <_peer_progress+0x660>
fffffe00079af0d4:      	mov	w8, #9
fffffe00079af0d8:      	movi.2d	v0, #0000000000000000
fffffe00079af0dc:      	ldr	x9, [x19, #1568]
fffffe00079af0e0:      	str	wzr, [x19, #1552]
fffffe00079af0e4:      	str	xzr, [sp, #48]
fffffe00079af0e8:      	stp	q0, q0, [sp]
fffffe00079af0ec:      	mov	x3, sp
fffffe00079af0f0:      	mov	x0, x19
fffffe00079af0f4:      	mov	x1, x21
fffffe00079af0f8:      	mov	x2, x20
fffffe00079af0fc:      	str	q0, [sp, #32]
fffffe00079af100:      	str	w8, [sp]
fffffe00079af104:      	str	x9, [sp, #16]
fffffe00079af108:      	bl	0xfffffe00079ae194 <_emit>
fffffe00079af10c:      	b	0xfffffe00079aeafc <_peer_progress+0x40>
fffffe00079af110:      	mov	w2, #19
fffffe00079af114:      	b	0xfffffe00079af11c <_peer_progress+0x660>
fffffe00079af118:      	mov	w2, #25
fffffe00079af11c:      	ldr	x1, [x19, #1568]
fffffe00079af120:      	add	x3, sp, #160
fffffe00079af124:      	ldr	x4, [sp, #152]
fffffe00079af128:      	mov	x0, x19
fffffe00079af12c:      	mov	w5, #1
fffffe00079af130:      	bl	0xfffffe00079abd54 <_command>
fffffe00079af134:      	b	0xfffffe00079aeafc <_peer_progress+0x40>
fffffe00079af138:      	mov	w8, #10
fffffe00079af13c:      	b	0xfffffe00079af0d8 <_peer_progress+0x61c>
fffffe00079af140:      	mov	w2, #28
fffffe00079af144:      	b	0xfffffe00079af11c <_peer_progress+0x660>
fffffe00079af148:      	bl	0xfffffe0007a06554 <___stack_chk_fail>
