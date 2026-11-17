
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b0eec <_nwn_event>:
fffffe00079b0eec:      	sub	sp, sp, #144
fffffe00079b0ef0:      	stp	x26, x25, [sp, #64]
fffffe00079b0ef4:      	stp	x24, x23, [sp, #80]
fffffe00079b0ef8:      	stp	x22, x21, [sp, #96]
fffffe00079b0efc:      	stp	x20, x19, [sp, #112]
fffffe00079b0f00:      	stp	x29, x30, [sp, #128]
fffffe00079b0f04:      	add	x29, sp, #128
fffffe00079b0f08:      	cbz	x0, 0xfffffe00079b1118 <_nwn_event+0x22c>
fffffe00079b0f0c:      	mov	x20, x2
fffffe00079b0f10:      	mov	x19, x0
fffffe00079b0f14:      	mov	x22, x1
fffffe00079b0f18:      	str	x2, [x0, #3632]
fffffe00079b0f1c:      	cbz	x1, 0xfffffe00079b10b4 <_nwn_event+0x1c8>
fffffe00079b0f20:      	ldr	x1, [x22, #8]
fffffe00079b0f24:      	ldr	x8, [x19, #1240]
fffffe00079b0f28:      	cmp	x1, x8
fffffe00079b0f2c:      	b.ne	0xfffffe00079b10b4 <_nwn_event+0x1c8>
fffffe00079b0f30:      	ldr	w8, [x22]
fffffe00079b0f34:      	mov	w21, wzr
fffffe00079b0f38:      	sub	w8, w8, #1
fffffe00079b0f3c:      	cmp	w8, #14
fffffe00079b0f40:      	b.hi	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b0f44:      	adrp	x9, 0xfffffe000702d000 <_nwp_begin_ap+0x1a8>
fffffe00079b0f48:      	add	x9, x9, #2742
fffffe00079b0f4c:      	adr	x10, #16
fffffe00079b0f50:      	ldrb	w11, [x9, x8]
fffffe00079b0f54:      	add	x10, x10, x11, lsl #2
fffffe00079b0f58:      	br	x10
fffffe00079b0f5c:      	ldr	x2, [x22, #16]
fffffe00079b0f60:      	ldr	x8, [x19, #3560]
fffffe00079b0f64:      	cmp	x2, x8
fffffe00079b0f68:      	b.ne	0xfffffe00079b10b4 <_nwn_event+0x1c8>
fffffe00079b0f6c:      	ldr	w26, [x22, #4]
fffffe00079b0f70:      	mov	w23, #2
fffffe00079b0f74:      	ldr	w25, [x19, #3568]
fffffe00079b0f78:      	cmp	w26, #0
fffffe00079b0f7c:      	cset	w24, eq
fffffe00079b0f80:      	sub	w8, w25, #3
fffffe00079b0f84:      	cmp	w8, #7
fffffe00079b0f88:      	b.hi	0xfffffe00079b126c <_nwn_event+0x380>
fffffe00079b0f8c:      	adrp	x9, 0xfffffe000702d000 <_nwp_begin_ap+0x1f0>
fffffe00079b0f90:      	add	x9, x9, #2757
fffffe00079b0f94:      	mov	w21, #-1
fffffe00079b0f98:      	adr	x10, #16
fffffe00079b0f9c:      	ldrb	w11, [x9, x8]
fffffe00079b0fa0:      	add	x10, x10, x11, lsl #2
fffffe00079b0fa4:      	br	x10
fffffe00079b0fa8:      	cbz	w26, 0xfffffe00079b10b4 <_nwn_event+0x1c8>
fffffe00079b0fac:      	mov	x0, x19
fffffe00079b0fb0:      	mov	w3, #3
fffffe00079b0fb4:      	mov	w4, wzr
fffffe00079b0fb8:      	b	0xfffffe00079b1230 <_nwn_event+0x344>
fffffe00079b0fbc:      	ldr	w8, [x22, #4]
fffffe00079b0fc0:      	mov	x0, x19
fffffe00079b0fc4:      	ldr	x2, [x22, #16]
fffffe00079b0fc8:      	mov	w3, #3
fffffe00079b0fcc:      	b	0xfffffe00079b1228 <_nwn_event+0x33c>
fffffe00079b0fd0:      	ldr	w8, [x22, #4]
fffffe00079b0fd4:      	mov	x0, x19
fffffe00079b0fd8:      	ldrb	w2, [x22, #28]
fffffe00079b0fdc:      	mov	x7, x20
fffffe00079b0fe0:      	ldrb	w3, [x22, #30]
fffffe00079b0fe4:      	cmp	w8, #0
fffffe00079b0fe8:      	ldrb	w4, [x22, #31]
fffffe00079b0fec:      	ldrb	w5, [x22, #32]
fffffe00079b0ff0:      	cset	w6, eq
fffffe00079b0ff4:      	bl	0xfffffe00079b069c <_nwo_grant>
fffffe00079b0ff8:      	mov	w21, w0
fffffe00079b0ffc:      	cmp	w0, #1
fffffe00079b1000:      	b.lt	0xfffffe00079b123c <_nwn_event+0x350>
fffffe00079b1004:      	ldr	w8, [x22, #36]
fffffe00079b1008:      	subs	w8, w8, #10
fffffe00079b100c:      	csel	w8, wzr, w8, lo
fffffe00079b1010:      	cmn	x8, x20
fffffe00079b1014:      	b.lo	0xfffffe00079b1250 <_nwn_event+0x364>
fffffe00079b1018:      	ldr	x1, [x22, #8]
fffffe00079b101c:      	mov	x0, x19
fffffe00079b1020:      	mov	x2, x20
fffffe00079b1024:      	bl	0xfffffe00079b0364 <_nwo_link_lost>
fffffe00079b1028:      	b	0xfffffe00079b1238 <_nwn_event+0x34c>
fffffe00079b102c:      	ldp	x2, x3, [x22, #40]
fffffe00079b1030:      	mov	x0, x19
fffffe00079b1034:      	mov	x4, x20
fffffe00079b1038:      	bl	0xfffffe00079b0764 <_nwo_key24>
fffffe00079b103c:      	b	0xfffffe00079b1238 <_nwn_event+0x34c>
fffffe00079b1040:      	ldp	x2, x3, [x22, #40]
fffffe00079b1044:      	mov	x0, x19
fffffe00079b1048:      	mov	x4, x20
fffffe00079b104c:      	bl	0xfffffe00079b05d4 <_nwo_management>
fffffe00079b1050:      	b	0xfffffe00079b1238 <_nwn_event+0x34c>
fffffe00079b1054:      	ldr	x24, [x22, #40]
fffffe00079b1058:      	mov	w21, wzr
fffffe00079b105c:      	cbz	x24, 0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1060:      	ldr	x8, [x22, #48]
fffffe00079b1064:      	subs	x23, x8, #14
fffffe00079b1068:      	b.lo	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b106c:      	ldrb	w8, [x24, #12]
fffffe00079b1070:      	cmp	w8, #136
fffffe00079b1074:      	b.ne	0xfffffe00079b10b4 <_nwn_event+0x1c8>
fffffe00079b1078:      	ldrb	w8, [x24, #13]
fffffe00079b107c:      	cmp	w8, #142
fffffe00079b1080:      	b.ne	0xfffffe00079b10b4 <_nwn_event+0x1c8>
fffffe00079b1084:      	ldr	x8, [x19, #3584]
fffffe00079b1088:      	mov	x0, x24
fffffe00079b108c:      	mov	w2, #6
fffffe00079b1090:      	add	x1, x8, #192
fffffe00079b1094:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe00079b1098:      	cbnz	w0, 0xfffffe00079b10b4 <_nwn_event+0x1c8>
fffffe00079b109c:      	ldr	x8, [x19, #3584]
fffffe00079b10a0:      	add	x0, x24, #6
fffffe00079b10a4:      	mov	w2, #6
fffffe00079b10a8:      	add	x1, x8, #198
fffffe00079b10ac:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe00079b10b0:      	cbz	w0, 0xfffffe00079b12b0 <_nwn_event+0x3c4>
fffffe00079b10b4:      	mov	w21, wzr
fffffe00079b10b8:      	ldr	w8, [x19, #4]
fffffe00079b10bc:      	cmp	w8, #16
fffffe00079b10c0:      	b.lo	0xfffffe00079b111c <_nwn_event+0x230>
fffffe00079b10c4:      	ldr	w8, [x19, #3624]
fffffe00079b10c8:      	cbnz	w8, 0xfffffe00079b111c <_nwn_event+0x230>
fffffe00079b10cc:      	mov	x10, #42991
fffffe00079b10d0:      	ldr	x8, [x19, #1272]
fffffe00079b10d4:      	movk	x10, #19398, lsl #16
fffffe00079b10d8:      	movk	x10, #35127, lsl #32
fffffe00079b10dc:      	movk	x10, #65, lsl #48
fffffe00079b10e0:      	cmp	x8, x10
fffffe00079b10e4:      	b.hi	0xfffffe00079b1118 <_nwn_event+0x22c>
fffffe00079b10e8:      	ldr	x9, [x19, #3632]
fffffe00079b10ec:      	cmp	x9, x10
fffffe00079b10f0:      	b.hi	0xfffffe00079b1118 <_nwn_event+0x22c>
fffffe00079b10f4:      	ldr	x0, [x19, #3584]
fffffe00079b10f8:      	mov	w10, #1000
fffffe00079b10fc:      	mul	x9, x9, x10
fffffe00079b1100:      	ldr	x11, [x0, #168]
fffffe00079b1104:      	mul	x2, x8, x10
fffffe00079b1108:      	cmp	x9, x11
fffffe00079b110c:      	csel	x1, x9, x11, hi
fffffe00079b1110:      	bl	0xfffffe00079a6e28 <_nst_cleanup_begin>
fffffe00079b1114:      	cbz	w0, 0xfffffe00079b1244 <_nwn_event+0x358>
fffffe00079b1118:      	mov	w21, #-1
fffffe00079b111c:      	mov	w0, w21
fffffe00079b1120:      	ldp	x29, x30, [sp, #128]
fffffe00079b1124:      	ldp	x20, x19, [sp, #112]
fffffe00079b1128:      	ldp	x22, x21, [sp, #96]
fffffe00079b112c:      	ldp	x24, x23, [sp, #80]
fffffe00079b1130:      	ldp	x26, x25, [sp, #64]
fffffe00079b1134:      	add	sp, sp, #144
fffffe00079b1138:      	ret
fffffe00079b113c:      	ldrb	w2, [x22, #30]
fffffe00079b1140:      	mov	x0, x19
fffffe00079b1144:      	mov	x3, x20
fffffe00079b1148:      	bl	0xfffffe00079ad2b4 <_stc_channel_released>
fffffe00079b114c:      	mov	w21, w0
fffffe00079b1150:      	cmp	w0, #1
fffffe00079b1154:      	b.lt	0xfffffe00079b123c <_nwn_event+0x350>
fffffe00079b1158:      	ldr	w8, [x19, #4]
fffffe00079b115c:      	cmp	w8, #15
fffffe00079b1160:      	b.hi	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1164:      	ldr	x8, [x19, #3640]
fffffe00079b1168:      	str	x8, [x19, #1264]
fffffe00079b116c:      	b	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1170:      	mov	x0, x19
fffffe00079b1174:      	mov	x2, x20
fffffe00079b1178:      	strb	wzr, [x19, #3608]
fffffe00079b117c:      	strb	wzr, [x19, #3609]
fffffe00079b1180:      	strb	wzr, [x19, #3610]
fffffe00079b1184:      	strb	wzr, [x19, #3611]
fffffe00079b1188:      	strb	wzr, [x19, #3612]
fffffe00079b118c:      	strb	wzr, [x19, #3613]
fffffe00079b1190:      	strb	wzr, [x19, #3614]
fffffe00079b1194:      	strb	wzr, [x19, #3615]
fffffe00079b1198:      	strb	wzr, [x19, #3616]
fffffe00079b119c:      	str	x20, [x19, #3632]
fffffe00079b11a0:      	bl	0xfffffe00079b0438 <_nwo_cancel>
fffffe00079b11a4:      	mov	w21, w0
fffffe00079b11a8:      	tbnz	w0, #31, 0xfffffe00079b111c <_nwn_event+0x230>
fffffe00079b11ac:      	ldr	w8, [x19, #4]
fffffe00079b11b0:      	cmp	w8, #16
fffffe00079b11b4:      	b.lo	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b11b8:      	ldr	w8, [x19, #3624]
fffffe00079b11bc:      	cbnz	w8, 0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b11c0:      	mov	x10, #42991
fffffe00079b11c4:      	ldr	x8, [x19, #1272]
fffffe00079b11c8:      	movk	x10, #19398, lsl #16
fffffe00079b11cc:      	movk	x10, #35127, lsl #32
fffffe00079b11d0:      	movk	x10, #65, lsl #48
fffffe00079b11d4:      	cmp	x8, x10
fffffe00079b11d8:      	b.hi	0xfffffe00079b1118 <_nwn_event+0x22c>
fffffe00079b11dc:      	ldr	x9, [x19, #3632]
fffffe00079b11e0:      	cmp	x9, x10
fffffe00079b11e4:      	b.hi	0xfffffe00079b1118 <_nwn_event+0x22c>
fffffe00079b11e8:      	ldr	x0, [x19, #3584]
fffffe00079b11ec:      	mov	w10, #1000
fffffe00079b11f0:      	mul	x9, x9, x10
fffffe00079b11f4:      	ldr	x11, [x0, #168]
fffffe00079b11f8:      	mul	x2, x8, x10
fffffe00079b11fc:      	cmp	x9, x11
fffffe00079b1200:      	csel	x1, x9, x11, hi
fffffe00079b1204:      	bl	0xfffffe00079a6e28 <_nst_cleanup_begin>
fffffe00079b1208:      	cbnz	w0, 0xfffffe00079b1118 <_nwn_event+0x22c>
fffffe00079b120c:      	mov	w8, #1
fffffe00079b1210:      	str	w8, [x19, #3624]
fffffe00079b1214:      	b	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1218:      	ldr	w8, [x22, #4]
fffffe00079b121c:      	mov	x0, x19
fffffe00079b1220:      	ldr	x2, [x22, #16]
fffffe00079b1224:      	mov	w3, #8
fffffe00079b1228:      	cmp	w8, #0
fffffe00079b122c:      	cset	w4, eq
fffffe00079b1230:      	mov	x5, x20
fffffe00079b1234:      	bl	0xfffffe00079b0234 <_nwo_complete>
fffffe00079b1238:      	mov	w21, w0
fffffe00079b123c:      	tbz	w21, #31, 0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1240:      	b	0xfffffe00079b111c <_nwn_event+0x230>
fffffe00079b1244:      	mov	w8, #1
fffffe00079b1248:      	str	w8, [x19, #3624]
fffffe00079b124c:      	b	0xfffffe00079b111c <_nwn_event+0x230>
fffffe00079b1250:      	ldr	x9, [x19, #1264]
fffffe00079b1254:      	add	x8, x8, x20
fffffe00079b1258:      	cmp	x8, x9
fffffe00079b125c:      	b.hs	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1260:      	str	x8, [x19, #1264]
fffffe00079b1264:      	b	0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1268:      	mov	w23, #5
fffffe00079b126c:      	mov	x0, x19
fffffe00079b1270:      	bl	0xfffffe00079ac0e4 <_stc_next>
fffffe00079b1274:      	cbnz	w26, 0xfffffe00079b1314 <_nwn_event+0x428>
fffffe00079b1278:      	cbz	x0, 0xfffffe00079b1314 <_nwn_event+0x428>
fffffe00079b127c:      	ldr	x8, [x0, #8]
fffffe00079b1280:      	mov	w24, #1
fffffe00079b1284:      	ldr	x9, [x22, #16]
fffffe00079b1288:      	cmp	x8, x9
fffffe00079b128c:      	b.ne	0xfffffe00079b1314 <_nwn_event+0x428>
fffffe00079b1290:      	sub	w8, w25, #7
fffffe00079b1294:      	cmp	w8, #1
fffffe00079b1298:      	b.hi	0xfffffe00079b1314 <_nwn_event+0x428>
fffffe00079b129c:      	ldr	w1, [x19, #3620]
fffffe00079b12a0:      	ldr	x0, [x19, #3584]
fffffe00079b12a4:      	cbz	w1, 0xfffffe00079b1300 <_nwn_event+0x414>
fffffe00079b12a8:      	mov	w2, wzr
fffffe00079b12ac:      	b	0xfffffe00079b1304 <_nwn_event+0x418>
fffffe00079b12b0:      	movi.2d	v0, #0000000000000000
fffffe00079b12b4:      	add	x21, x24, #14
fffffe00079b12b8:      	ldr	x1, [x22, #8]
fffffe00079b12bc:      	mov	x0, sp
fffffe00079b12c0:      	ldr	x2, [x19, #3560]
fffffe00079b12c4:      	mov	x3, x21
fffffe00079b12c8:      	mov	x4, x23
fffffe00079b12cc:      	str	xzr, [sp, #48]
fffffe00079b12d0:      	stp	q0, q0, [sp, #16]
fffffe00079b12d4:      	str	q0, [sp]
fffffe00079b12d8:      	bl	0xfffffe00079ab470 <_nstd_eapol>
fffffe00079b12dc:      	mov	x0, sp
fffffe00079b12e0:      	bl	0xfffffe00079ab604 <_nstd_log>
fffffe00079b12e4:      	ldr	x1, [x22, #8]
fffffe00079b12e8:      	mov	x0, x19
fffffe00079b12ec:      	mov	x2, x21
fffffe00079b12f0:      	mov	x3, x23
fffffe00079b12f4:      	mov	x4, x20
fffffe00079b12f8:      	bl	0xfffffe00079b050c <_nwo_eapol>
fffffe00079b12fc:      	b	0xfffffe00079b1238 <_nwn_event+0x34c>
fffffe00079b1300:      	ldrb	w2, [x19, #3608]
fffffe00079b1304:      	add	x3, x19, #3609
fffffe00079b1308:      	bl	0xfffffe00079a7aa0 <_nst_replay_install>
fffffe00079b130c:      	cmp	w0, #0
fffffe00079b1310:      	cset	w24, eq
fffffe00079b1314:      	ldp	x1, x2, [x22, #8]
fffffe00079b1318:      	mov	x0, x19
fffffe00079b131c:      	mov	w3, w23
fffffe00079b1320:      	mov	w4, w24
fffffe00079b1324:      	mov	x5, x20
fffffe00079b1328:      	bl	0xfffffe00079b0234 <_nwo_complete>
fffffe00079b132c:      	mov	w21, w0
fffffe00079b1330:      	strb	wzr, [x19, #3608]
fffffe00079b1334:      	strb	wzr, [x19, #3609]
fffffe00079b1338:      	strb	wzr, [x19, #3610]
fffffe00079b133c:      	strb	wzr, [x19, #3611]
fffffe00079b1340:      	strb	wzr, [x19, #3612]
fffffe00079b1344:      	strb	wzr, [x19, #3613]
fffffe00079b1348:      	strb	wzr, [x19, #3614]
fffffe00079b134c:      	strb	wzr, [x19, #3615]
fffffe00079b1350:      	strb	wzr, [x19, #3616]
fffffe00079b1354:      	tbz	w21, #31, 0xfffffe00079b10b8 <_nwn_event+0x1cc>
fffffe00079b1358:      	b	0xfffffe00079b111c <_nwn_event+0x230>
