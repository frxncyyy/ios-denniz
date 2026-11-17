
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079afe78 <_nwn_event>:
fffffe00079afe78:      	sub	sp, sp, #144
fffffe00079afe7c:      	stp	x26, x25, [sp, #64]
fffffe00079afe80:      	stp	x24, x23, [sp, #80]
fffffe00079afe84:      	stp	x22, x21, [sp, #96]
fffffe00079afe88:      	stp	x20, x19, [sp, #112]
fffffe00079afe8c:      	stp	x29, x30, [sp, #128]
fffffe00079afe90:      	add	x29, sp, #128
fffffe00079afe94:      	cbz	x0, 0xfffffe00079b00b0 <_nwn_event+0x238>
fffffe00079afe98:      	mov	x20, x2
fffffe00079afe9c:      	mov	x19, x0
fffffe00079afea0:      	mov	x22, x1
fffffe00079afea4:      	str	x2, [x0, #3632]
fffffe00079afea8:      	cbz	x1, 0xfffffe00079b004c <_nwn_event+0x1d4>
fffffe00079afeac:      	ldr	x1, [x22, #8]
fffffe00079afeb0:      	ldr	x8, [x19, #1240]
fffffe00079afeb4:      	cmp	x1, x8
fffffe00079afeb8:      	b.ne	0xfffffe00079b004c <_nwn_event+0x1d4>
fffffe00079afebc:      	ldr	w8, [x22]
fffffe00079afec0:      	mov	w21, wzr
fffffe00079afec4:      	sub	w8, w8, #1
fffffe00079afec8:      	cmp	w8, #13
fffffe00079afecc:      	b.hi	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079afed0:      	adrp	x9, 0xfffffe000702d000 <_nwp_begin_ap+0x1ac>
fffffe00079afed4:      	add	x9, x9, #2886
fffffe00079afed8:      	adr	x10, #16
fffffe00079afedc:      	ldrb	w11, [x9, x8]
fffffe00079afee0:      	add	x10, x10, x11, lsl #2
fffffe00079afee4:      	br	x10
fffffe00079afee8:      	ldr	x2, [x22, #16]
fffffe00079afeec:      	ldr	x8, [x19, #3560]
fffffe00079afef0:      	cmp	x2, x8
fffffe00079afef4:      	b.ne	0xfffffe00079b004c <_nwn_event+0x1d4>
fffffe00079afef8:      	ldr	w25, [x22, #4]
fffffe00079afefc:      	mov	w21, #2
fffffe00079aff00:      	ldr	w24, [x19, #3568]
fffffe00079aff04:      	cmp	w25, #0
fffffe00079aff08:      	cset	w23, eq
fffffe00079aff0c:      	cmp	w24, #10
fffffe00079aff10:      	b.hi	0xfffffe00079b01e0 <_nwn_event+0x368>
fffffe00079aff14:      	mov	w8, #1
fffffe00079aff18:      	mov	w9, #1112
fffffe00079aff1c:      	lsl	w8, w8, w24
fffffe00079aff20:      	tst	w8, w9
fffffe00079aff24:      	b.eq	0xfffffe00079b01d4 <_nwn_event+0x35c>
fffffe00079aff28:      	cbz	w25, 0xfffffe00079b004c <_nwn_event+0x1d4>
fffffe00079aff2c:      	mov	x0, x19
fffffe00079aff30:      	mov	w3, #3
fffffe00079aff34:      	mov	w4, wzr
fffffe00079aff38:      	b	0xfffffe00079aff54 <_nwn_event+0xdc>
fffffe00079aff3c:      	ldr	w8, [x22, #4]
fffffe00079aff40:      	mov	x0, x19
fffffe00079aff44:      	ldr	x2, [x22, #16]
fffffe00079aff48:      	mov	w3, #3
fffffe00079aff4c:      	cmp	w8, #0
fffffe00079aff50:      	cset	w4, eq
fffffe00079aff54:      	mov	x5, x20
fffffe00079aff58:      	bl	0xfffffe00079af1c0 <_nwo_complete>
fffffe00079aff5c:      	b	0xfffffe00079affe0 <_nwn_event+0x168>
fffffe00079aff60:      	ldr	w8, [x22, #4]
fffffe00079aff64:      	mov	x0, x19
fffffe00079aff68:      	ldrb	w2, [x22, #28]
fffffe00079aff6c:      	mov	x7, x20
fffffe00079aff70:      	ldrb	w3, [x22, #30]
fffffe00079aff74:      	cmp	w8, #0
fffffe00079aff78:      	ldrb	w4, [x22, #31]
fffffe00079aff7c:      	ldrb	w5, [x22, #32]
fffffe00079aff80:      	cset	w6, eq
fffffe00079aff84:      	bl	0xfffffe00079af628 <_nwo_grant>
fffffe00079aff88:      	mov	w21, w0
fffffe00079aff8c:      	cmp	w0, #1
fffffe00079aff90:      	b.lt	0xfffffe00079affe4 <_nwn_event+0x16c>
fffffe00079aff94:      	ldr	w8, [x22, #36]
fffffe00079aff98:      	subs	w8, w8, #10
fffffe00079aff9c:      	csel	w8, wzr, w8, lo
fffffe00079affa0:      	cmn	x8, x20
fffffe00079affa4:      	b.lo	0xfffffe00079b01bc <_nwn_event+0x344>
fffffe00079affa8:      	ldr	x1, [x22, #8]
fffffe00079affac:      	mov	x0, x19
fffffe00079affb0:      	mov	x2, x20
fffffe00079affb4:      	bl	0xfffffe00079af2f0 <_nwo_link_lost>
fffffe00079affb8:      	b	0xfffffe00079affe0 <_nwn_event+0x168>
fffffe00079affbc:      	ldp	x2, x3, [x22, #40]
fffffe00079affc0:      	mov	x0, x19
fffffe00079affc4:      	mov	x4, x20
fffffe00079affc8:      	bl	0xfffffe00079af6f0 <_nwo_key24>
fffffe00079affcc:      	b	0xfffffe00079affe0 <_nwn_event+0x168>
fffffe00079affd0:      	ldp	x2, x3, [x22, #40]
fffffe00079affd4:      	mov	x0, x19
fffffe00079affd8:      	mov	x4, x20
fffffe00079affdc:      	bl	0xfffffe00079af560 <_nwo_management>
fffffe00079affe0:      	mov	w21, w0
fffffe00079affe4:      	tbz	w21, #31, 0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079affe8:      	b	0xfffffe00079b00b4 <_nwn_event+0x23c>
fffffe00079affec:      	ldr	x24, [x22, #40]
fffffe00079afff0:      	mov	w21, wzr
fffffe00079afff4:      	cbz	x24, 0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079afff8:      	ldr	x8, [x22, #48]
fffffe00079afffc:      	subs	x23, x8, #14
fffffe00079b0000:      	b.lo	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b0004:      	ldrb	w8, [x24, #12]
fffffe00079b0008:      	cmp	w8, #136
fffffe00079b000c:      	b.ne	0xfffffe00079b004c <_nwn_event+0x1d4>
fffffe00079b0010:      	ldrb	w8, [x24, #13]
fffffe00079b0014:      	cmp	w8, #142
fffffe00079b0018:      	b.ne	0xfffffe00079b004c <_nwn_event+0x1d4>
fffffe00079b001c:      	ldr	x8, [x19, #3584]
fffffe00079b0020:      	mov	x0, x24
fffffe00079b0024:      	mov	w2, #6
fffffe00079b0028:      	add	x1, x8, #192
fffffe00079b002c:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe00079b0030:      	cbnz	w0, 0xfffffe00079b004c <_nwn_event+0x1d4>
fffffe00079b0034:      	ldr	x8, [x19, #3584]
fffffe00079b0038:      	add	x0, x24, #6
fffffe00079b003c:      	mov	w2, #6
fffffe00079b0040:      	add	x1, x8, #198
fffffe00079b0044:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe00079b0048:      	cbz	w0, 0xfffffe00079b0224 <_nwn_event+0x3ac>
fffffe00079b004c:      	mov	w21, wzr
fffffe00079b0050:      	ldr	w8, [x19, #4]
fffffe00079b0054:      	cmp	w8, #16
fffffe00079b0058:      	b.lo	0xfffffe00079b00b4 <_nwn_event+0x23c>
fffffe00079b005c:      	ldr	w8, [x19, #3624]
fffffe00079b0060:      	cbnz	w8, 0xfffffe00079b00b4 <_nwn_event+0x23c>
fffffe00079b0064:      	mov	x10, #42991
fffffe00079b0068:      	ldr	x8, [x19, #1272]
fffffe00079b006c:      	movk	x10, #19398, lsl #16
fffffe00079b0070:      	movk	x10, #35127, lsl #32
fffffe00079b0074:      	movk	x10, #65, lsl #48
fffffe00079b0078:      	cmp	x8, x10
fffffe00079b007c:      	b.hi	0xfffffe00079b00b0 <_nwn_event+0x238>
fffffe00079b0080:      	ldr	x9, [x19, #3632]
fffffe00079b0084:      	cmp	x9, x10
fffffe00079b0088:      	b.hi	0xfffffe00079b00b0 <_nwn_event+0x238>
fffffe00079b008c:      	ldr	x0, [x19, #3584]
fffffe00079b0090:      	mov	w10, #1000
fffffe00079b0094:      	mul	x9, x9, x10
fffffe00079b0098:      	ldr	x11, [x0, #168]
fffffe00079b009c:      	mul	x2, x8, x10
fffffe00079b00a0:      	cmp	x9, x11
fffffe00079b00a4:      	csel	x1, x9, x11, hi
fffffe00079b00a8:      	bl	0xfffffe00079a5dfc <_nst_cleanup_begin>
fffffe00079b00ac:      	cbz	w0, 0xfffffe00079b01b0 <_nwn_event+0x338>
fffffe00079b00b0:      	mov	w21, #-1
fffffe00079b00b4:      	mov	w0, w21
fffffe00079b00b8:      	ldp	x29, x30, [sp, #128]
fffffe00079b00bc:      	ldp	x20, x19, [sp, #112]
fffffe00079b00c0:      	ldp	x22, x21, [sp, #96]
fffffe00079b00c4:      	ldp	x24, x23, [sp, #80]
fffffe00079b00c8:      	ldp	x26, x25, [sp, #64]
fffffe00079b00cc:      	add	sp, sp, #144
fffffe00079b00d0:      	ret
fffffe00079b00d4:      	ldrb	w2, [x22, #30]
fffffe00079b00d8:      	mov	x0, x19
fffffe00079b00dc:      	mov	x3, x20
fffffe00079b00e0:      	bl	0xfffffe00079ac240 <_stc_channel_released>
fffffe00079b00e4:      	mov	w21, w0
fffffe00079b00e8:      	cmp	w0, #1
fffffe00079b00ec:      	b.lt	0xfffffe00079affe4 <_nwn_event+0x16c>
fffffe00079b00f0:      	ldr	w8, [x19, #4]
fffffe00079b00f4:      	cmp	w8, #15
fffffe00079b00f8:      	b.hi	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b00fc:      	ldr	x8, [x19, #3640]
fffffe00079b0100:      	str	x8, [x19, #1264]
fffffe00079b0104:      	b	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b0108:      	mov	x0, x19
fffffe00079b010c:      	mov	x2, x20
fffffe00079b0110:      	strb	wzr, [x19, #3608]
fffffe00079b0114:      	strb	wzr, [x19, #3609]
fffffe00079b0118:      	strb	wzr, [x19, #3610]
fffffe00079b011c:      	strb	wzr, [x19, #3611]
fffffe00079b0120:      	strb	wzr, [x19, #3612]
fffffe00079b0124:      	strb	wzr, [x19, #3613]
fffffe00079b0128:      	strb	wzr, [x19, #3614]
fffffe00079b012c:      	strb	wzr, [x19, #3615]
fffffe00079b0130:      	strb	wzr, [x19, #3616]
fffffe00079b0134:      	str	x20, [x19, #3632]
fffffe00079b0138:      	bl	0xfffffe00079af3c4 <_nwo_cancel>
fffffe00079b013c:      	mov	w21, w0
fffffe00079b0140:      	tbnz	w0, #31, 0xfffffe00079b00b4 <_nwn_event+0x23c>
fffffe00079b0144:      	ldr	w8, [x19, #4]
fffffe00079b0148:      	cmp	w8, #16
fffffe00079b014c:      	b.lo	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b0150:      	ldr	w8, [x19, #3624]
fffffe00079b0154:      	cbnz	w8, 0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b0158:      	mov	x10, #42991
fffffe00079b015c:      	ldr	x8, [x19, #1272]
fffffe00079b0160:      	movk	x10, #19398, lsl #16
fffffe00079b0164:      	movk	x10, #35127, lsl #32
fffffe00079b0168:      	movk	x10, #65, lsl #48
fffffe00079b016c:      	cmp	x8, x10
fffffe00079b0170:      	b.hi	0xfffffe00079b00b0 <_nwn_event+0x238>
fffffe00079b0174:      	ldr	x9, [x19, #3632]
fffffe00079b0178:      	cmp	x9, x10
fffffe00079b017c:      	b.hi	0xfffffe00079b00b0 <_nwn_event+0x238>
fffffe00079b0180:      	ldr	x0, [x19, #3584]
fffffe00079b0184:      	mov	w10, #1000
fffffe00079b0188:      	mul	x9, x9, x10
fffffe00079b018c:      	ldr	x11, [x0, #168]
fffffe00079b0190:      	mul	x2, x8, x10
fffffe00079b0194:      	cmp	x9, x11
fffffe00079b0198:      	csel	x1, x9, x11, hi
fffffe00079b019c:      	bl	0xfffffe00079a5dfc <_nst_cleanup_begin>
fffffe00079b01a0:      	cbnz	w0, 0xfffffe00079b00b0 <_nwn_event+0x238>
fffffe00079b01a4:      	mov	w8, #1
fffffe00079b01a8:      	str	w8, [x19, #3624]
fffffe00079b01ac:      	b	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b01b0:      	mov	w8, #1
fffffe00079b01b4:      	str	w8, [x19, #3624]
fffffe00079b01b8:      	b	0xfffffe00079b00b4 <_nwn_event+0x23c>
fffffe00079b01bc:      	ldr	x9, [x19, #1264]
fffffe00079b01c0:      	add	x8, x8, x20
fffffe00079b01c4:      	cmp	x8, x9
fffffe00079b01c8:      	b.hs	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b01cc:      	str	x8, [x19, #1264]
fffffe00079b01d0:      	b	0xfffffe00079b0050 <_nwn_event+0x1d8>
fffffe00079b01d4:      	cmp	w24, #8
fffffe00079b01d8:      	b.ne	0xfffffe00079b01e0 <_nwn_event+0x368>
fffffe00079b01dc:      	mov	w21, #5
fffffe00079b01e0:      	mov	x0, x19
fffffe00079b01e4:      	bl	0xfffffe00079ab070 <_stc_next>
fffffe00079b01e8:      	cbnz	w25, 0xfffffe00079b0288 <_nwn_event+0x410>
fffffe00079b01ec:      	cbz	x0, 0xfffffe00079b0288 <_nwn_event+0x410>
fffffe00079b01f0:      	ldr	x8, [x0, #8]
fffffe00079b01f4:      	mov	w23, #1
fffffe00079b01f8:      	ldr	x9, [x22, #16]
fffffe00079b01fc:      	cmp	x8, x9
fffffe00079b0200:      	b.ne	0xfffffe00079b0288 <_nwn_event+0x410>
fffffe00079b0204:      	sub	w8, w24, #7
fffffe00079b0208:      	cmp	w8, #1
fffffe00079b020c:      	b.hi	0xfffffe00079b0288 <_nwn_event+0x410>
fffffe00079b0210:      	ldr	w1, [x19, #3620]
fffffe00079b0214:      	ldr	x0, [x19, #3584]
fffffe00079b0218:      	cbz	w1, 0xfffffe00079b0274 <_nwn_event+0x3fc>
fffffe00079b021c:      	mov	w2, wzr
fffffe00079b0220:      	b	0xfffffe00079b0278 <_nwn_event+0x400>
fffffe00079b0224:      	movi.2d	v0, #0000000000000000
fffffe00079b0228:      	add	x21, x24, #14
fffffe00079b022c:      	ldr	x1, [x22, #8]
fffffe00079b0230:      	mov	x0, sp
fffffe00079b0234:      	ldr	x2, [x19, #3560]
fffffe00079b0238:      	mov	x3, x21
fffffe00079b023c:      	mov	x4, x23
fffffe00079b0240:      	str	xzr, [sp, #48]
fffffe00079b0244:      	stp	q0, q0, [sp, #16]
fffffe00079b0248:      	str	q0, [sp]
fffffe00079b024c:      	bl	0xfffffe00079aa3fc <_nstd_eapol>
fffffe00079b0250:      	mov	x0, sp
fffffe00079b0254:      	bl	0xfffffe00079aa590 <_nstd_log>
fffffe00079b0258:      	ldr	x1, [x22, #8]
fffffe00079b025c:      	mov	x0, x19
fffffe00079b0260:      	mov	x2, x21
fffffe00079b0264:      	mov	x3, x23
fffffe00079b0268:      	mov	x4, x20
fffffe00079b026c:      	bl	0xfffffe00079af498 <_nwo_eapol>
fffffe00079b0270:      	b	0xfffffe00079affe0 <_nwn_event+0x168>
fffffe00079b0274:      	ldrb	w2, [x19, #3608]
fffffe00079b0278:      	add	x3, x19, #3609
fffffe00079b027c:      	bl	0xfffffe00079a6a78 <_nst_replay_install>
fffffe00079b0280:      	cmp	w0, #0
fffffe00079b0284:      	cset	w23, eq
fffffe00079b0288:      	ldp	x1, x2, [x22, #8]
fffffe00079b028c:      	mov	x0, x19
fffffe00079b0290:      	mov	w3, w21
fffffe00079b0294:      	mov	w4, w23
fffffe00079b0298:      	mov	x5, x20
fffffe00079b029c:      	bl	0xfffffe00079af1c0 <_nwo_complete>
fffffe00079b02a0:      	mov	w21, w0
fffffe00079b02a4:      	strb	wzr, [x19, #3608]
fffffe00079b02a8:      	strb	wzr, [x19, #3609]
fffffe00079b02ac:      	strb	wzr, [x19, #3610]
fffffe00079b02b0:      	strb	wzr, [x19, #3611]
fffffe00079b02b4:      	strb	wzr, [x19, #3612]
fffffe00079b02b8:      	strb	wzr, [x19, #3613]
fffffe00079b02bc:      	strb	wzr, [x19, #3614]
fffffe00079b02c0:      	strb	wzr, [x19, #3615]
fffffe00079b02c4:      	strb	wzr, [x19, #3616]
fffffe00079b02c8:      	b	0xfffffe00079affe4 <_nwn_event+0x16c>
