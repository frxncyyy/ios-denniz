
research/nord2/wifi/next-stage/main-fw-transport/phy-action/module-evidence/wlan_drv_gen4m.ko:	file format elf64-littleaarch64

Disassembly of section .text:

000000000004d124 <nicRxWaitResponseByWaitingInterval>:
   4d124: fd 7b bc a9  	stp	x29, x30, [sp, #-64]!
   4d128: f7 0b 00 f9  	str	x23, [sp, #16]
   4d12c: f6 57 02 a9  	stp	x22, x21, [sp, #32]
   4d130: f4 4f 03 a9  	stp	x20, x19, [sp, #48]
   4d134: fd 03 00 91  	mov	x29, sp
   4d138: 17 00 40 f9  	ldr	x23, [x0]
   4d13c: f5 03 04 aa  	mov	x21, x4
   4d140: f4 03 02 aa  	mov	x20, x2
   4d144: f6 03 00 aa  	mov	x22, x0
   4d148: 00 00 00 94  	bl	0x4d148 <nicRxWaitResponseByWaitingInterval+0x24>
		000000000004d148:  R_AARCH64_CALL26	halRxWaitResponse
   4d14c: f3 03 00 2a  	mov	w19, w0
   4d150: a0 02 00 34  	cbz	w0, 0x4d1a4 <nicRxWaitResponseByWaitingInterval+0x80>
   4d154: 08 15 80 52  	mov	w8, #168
   4d158: 48 02 a0 72  	movk	w8, #18, lsl #16
   4d15c: c9 6a 68 b8  	ldr	w9, [x22, x8]
   4d160: e0 03 16 aa  	mov	x0, x22
   4d164: 29 11 1f 32  	orr	w9, w9, #0x3e
   4d168: c9 6a 28 b8  	str	w9, [x22, x8]
   4d16c: 00 00 00 94  	bl	0x4d16c <nicRxWaitResponseByWaitingInterval+0x48>
		000000000004d16c:  R_AARCH64_CALL26	halPrintHifDbgInfo
   4d170: 08 00 00 90  	adrp	x8, 0x4d000 <nicRxWaitResponseByWaitingInterval+0x4c>
		000000000004d170:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
   4d174: 08 01 00 91  	add	x8, x8, #0
		000000000004d174:  R_AARCH64_ADD_ABS_LO12_NC	aucDebugModule
   4d178: 08 15 40 39  	ldrb	w8, [x8, #5]
   4d17c: e8 05 00 36  	tbz	w8, #0, 0x4d238 <nicRxWaitResponseByWaitingInterval+0x114>
   4d180: 08 41 38 d5  	mrs	x8, SP_EL0
   4d184: 01 b9 45 b9  	ldr	w1, [x8, #1464]
   4d188: 00 00 00 90  	adrp	x0, 0x4d000 <nicRxWaitResponseByWaitingInterval+0x64>
		000000000004d188:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x49bb8
   4d18c: 02 00 00 90  	adrp	x2, 0x4d000 <nicRxWaitResponseByWaitingInterval+0x68>
		000000000004d18c:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x897
   4d190: 00 00 00 91  	add	x0, x0, #0
		000000000004d190:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x49bb8
   4d194: 42 00 00 91  	add	x2, x2, #0
		000000000004d194:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x897
   4d198: e3 03 13 2a  	mov	w3, w19
   4d19c: 00 00 00 94  	bl	0x4d19c <nicRxWaitResponseByWaitingInterval+0x78>
		000000000004d19c:  R_AARCH64_CALL26	kalPrintLog
   4d1a0: 26 00 00 14  	b	0x4d238 <nicRxWaitResponseByWaitingInterval+0x114>
   4d1a4: 16 00 00 90  	adrp	x22, 0x4d000 <nicRxWaitResponseByWaitingInterval+0x80>
		000000000004d1a4:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
   4d1a8: d6 02 00 91  	add	x22, x22, #0
		000000000004d1a8:  R_AARCH64_ADD_ABS_LO12_NC	aucDebugModule
   4d1ac: c8 16 40 39  	ldrb	w8, [x22, #5]
   4d1b0: 68 02 20 36  	tbz	w8, #4, 0x4d1fc <nicRxWaitResponseByWaitingInterval+0xd8>
   4d1b4: 08 41 38 d5  	mrs	x8, SP_EL0
   4d1b8: 01 b9 45 b9  	ldr	w1, [x8, #1464]
   4d1bc: a3 02 40 b9  	ldr	w3, [x21]
   4d1c0: 00 00 00 90  	adrp	x0, 0x4d000 <nicRxWaitResponseByWaitingInterval+0x9c>
		000000000004d1c0:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x1c6f3
   4d1c4: 02 00 00 90  	adrp	x2, 0x4d000 <nicRxWaitResponseByWaitingInterval+0xa0>
		000000000004d1c4:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x897
   4d1c8: 00 00 00 91  	add	x0, x0, #0
		000000000004d1c8:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x1c6f3
   4d1cc: 42 00 00 91  	add	x2, x2, #0
		000000000004d1cc:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x897
   4d1d0: 00 00 00 94  	bl	0x4d1d0 <nicRxWaitResponseByWaitingInterval+0xac>
		000000000004d1d0:  R_AARCH64_CALL26	kalPrintLog
   4d1d4: c8 16 40 39  	ldrb	w8, [x22, #5]
   4d1d8: 28 01 20 36  	tbz	w8, #4, 0x4d1fc <nicRxWaitResponseByWaitingInterval+0xd8>
   4d1dc: 00 00 00 90  	adrp	x0, 0x4d000 <nicRxWaitResponseByWaitingInterval+0xb8>
		000000000004d1dc:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x235e3
   4d1e0: 01 00 00 90  	adrp	x1, 0x4d000 <nicRxWaitResponseByWaitingInterval+0xbc>
		000000000004d1e0:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x897
   4d1e4: 00 00 00 91  	add	x0, x0, #0
		000000000004d1e4:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x235e3
   4d1e8: 21 00 00 91  	add	x1, x1, #0
		000000000004d1e8:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x897
   4d1ec: 00 00 00 94  	bl	0x4d1ec <nicRxWaitResponseByWaitingInterval+0xc8>
		000000000004d1ec:  R_AARCH64_CALL26	kalPrintLog
   4d1f0: a1 02 40 b9  	ldr	w1, [x21]
   4d1f4: e0 03 14 aa  	mov	x0, x20
   4d1f8: 00 00 00 94  	bl	0x4d1f8 <nicRxWaitResponseByWaitingInterval+0xd4>
		000000000004d1f8:  R_AARCH64_CALL26	dumpMemory8
   4d1fc: 08 00 00 90  	adrp	x8, 0x4d000 <nicRxWaitResponseByWaitingInterval+0xd8>
		000000000004d1fc:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
   4d200: 08 01 40 39  	ldrb	w8, [x8]
		000000000004d200:  R_AARCH64_LDST8_ABS_LO12_NC	aucDebugModule
   4d204: a8 01 20 36  	tbz	w8, #4, 0x4d238 <nicRxWaitResponseByWaitingInterval+0x114>
   4d208: e8 56 40 b9  	ldr	w8, [x23, #84]
   4d20c: 09 41 38 d5  	mrs	x9, SP_EL0
   4d210: 21 b9 45 b9  	ldr	w1, [x9, #1464]
   4d214: 00 00 00 90  	adrp	x0, 0x4d000 <nicRxWaitResponseByWaitingInterval+0xf0>
		000000000004d214:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x235f2
   4d218: 88 02 08 8b  	add	x8, x20, x8
   4d21c: 03 11 40 39  	ldrb	w3, [x8, #4]
   4d220: 04 15 40 39  	ldrb	w4, [x8, #5]
   4d224: 05 01 40 79  	ldrh	w5, [x8]
   4d228: 02 00 00 90  	adrp	x2, 0x4d000 <nicRxWaitResponseByWaitingInterval+0x104>
		000000000004d228:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x897
   4d22c: 00 00 00 91  	add	x0, x0, #0
		000000000004d22c:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x235f2
   4d230: 42 00 00 91  	add	x2, x2, #0
		000000000004d230:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x897
   4d234: 00 00 00 94  	bl	0x4d234 <nicRxWaitResponseByWaitingInterval+0x110>
		000000000004d234:  R_AARCH64_CALL26	kalPrintLog
   4d238: e0 03 13 2a  	mov	w0, w19
   4d23c: f4 4f 43 a9  	ldp	x20, x19, [sp, #48]
   4d240: f6 57 42 a9  	ldp	x22, x21, [sp, #32]
   4d244: f7 0b 40 f9  	ldr	x23, [sp, #16]
   4d248: fd 7b c4 a8  	ldp	x29, x30, [sp], #64
   4d24c: c0 03 5f d6  	ret

00000000000cc0a0 <halRxWaitResponse>:
   cc0a0: ff 03 02 d1  	sub	sp, sp, #128
   cc0a4: fd 7b 02 a9  	stp	x29, x30, [sp, #32]
   cc0a8: fc 6f 03 a9  	stp	x28, x27, [sp, #48]
   cc0ac: fa 67 04 a9  	stp	x26, x25, [sp, #64]
   cc0b0: f8 5f 05 a9  	stp	x24, x23, [sp, #80]
   cc0b4: f6 57 06 a9  	stp	x22, x21, [sp, #96]
   cc0b8: f4 4f 07 a9  	stp	x20, x19, [sp, #112]
   cc0bc: fd 83 00 91  	add	x29, sp, #32
   cc0c0: 08 48 96 52  	mov	w8, #45632
   cc0c4: e4 07 00 f9  	str	x4, [sp, #8]
   cc0c8: 48 00 a0 72  	movk	w8, #2, lsl #16
   cc0cc: 09 00 00 90  	adrp	x9, 0xcc000 <halRxWaitResponse+0x2c>
		00000000000cc0cc:  R_AARCH64_ADR_PREL_PG_HI21	jiffies
   cc0d0: f7 03 00 aa  	mov	x23, x0
   cc0d4: 18 68 68 f8  	ldr	x24, [x0, x8]
   cc0d8: 20 01 40 f9  	ldr	x0, [x9]
		00000000000cc0d8:  R_AARCH64_LDST64_ABS_LO12_NC	jiffies
   cc0dc: f3 03 06 2a  	mov	w19, w6
   cc0e0: f5 03 05 2a  	mov	w21, w5
   cc0e4: f6 03 03 2a  	mov	w22, w3
   cc0e8: f9 03 02 aa  	mov	x25, x2
   cc0ec: 00 00 00 94  	bl	0xcc0ec <halRxWaitResponse+0x4c>
		00000000000cc0ec:  R_AARCH64_CALL26	jiffies_to_msecs
   cc0f0: fa 03 00 2a  	mov	w26, w0
   cc0f4: e0 03 17 aa  	mov	x0, x23
   cc0f8: 00 00 00 94  	bl	0xcc0f8 <halRxWaitResponse+0x58>
		00000000000cc0f8:  R_AARCH64_CALL26	wlanIsChipNoAck
   cc0fc: 1f 1c 00 72  	tst	w0, #0xff
   cc100: a0 02 00 54  	b.eq	0xcc154 <halRxWaitResponse+0xb4>
   cc104: 08 00 00 90  	adrp	x8, 0xcc000 <halRxWaitResponse+0x64>
		00000000000cc104:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
   cc108: 08 01 00 91  	add	x8, x8, #0
		00000000000cc108:  R_AARCH64_ADD_ABS_LO12_NC	aucDebugModule
   cc10c: 08 05 40 39  	ldrb	w8, [x8, #1]
   cc110: 08 01 00 36  	tbz	w8, #0, 0xcc130 <halRxWaitResponse+0x90>
   cc114: 08 41 38 d5  	mrs	x8, SP_EL0
   cc118: 01 b9 45 b9  	ldr	w1, [x8, #1464]
   cc11c: 00 00 00 90  	adrp	x0, 0xcc000 <halRxWaitResponse+0x7c>
		00000000000cc11c:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x3a200
   cc120: 02 00 00 90  	adrp	x2, 0xcc000 <halRxWaitResponse+0x80>
		00000000000cc120:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x52b00
   cc124: 00 00 00 91  	add	x0, x0, #0
		00000000000cc124:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x3a200
   cc128: 42 00 00 91  	add	x2, x2, #0
		00000000000cc128:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x52b00
   cc12c: 00 00 00 94  	bl	0xcc12c <halRxWaitResponse+0x8c>
		00000000000cc12c:  R_AARCH64_CALL26	kalPrintLog
   cc130: e0 0b 02 32  	mov	w0, #-1073741823
   cc134: f4 4f 47 a9  	ldp	x20, x19, [sp, #112]
   cc138: f6 57 46 a9  	ldp	x22, x21, [sp, #96]
   cc13c: f8 5f 45 a9  	ldp	x24, x23, [sp, #80]
   cc140: fa 67 44 a9  	ldp	x26, x25, [sp, #64]
   cc144: fc 6f 43 a9  	ldp	x28, x27, [sp, #48]
   cc148: fd 7b 42 a9  	ldp	x29, x30, [sp, #32]
   cc14c: ff 03 02 91  	add	sp, sp, #128
   cc150: c0 03 5f d6  	ret
   cc154: e8 03 15 2a  	mov	w8, w21
   cc158: a8 83 1f f8  	stur	x8, [x29, #-8]
   cc15c: a8 ca 00 11  	add	w8, w21, #50
   cc160: e8 0b 00 f9  	str	x8, [sp, #16]
   cc164: 68 ba 89 52  	mov	w8, #19923
   cc168: 48 0c a2 72  	movk	w8, #4194, lsl #16
   cc16c: a8 7e a8 9b  	umull	x8, w21, w8
   cc170: fc 03 3a 2a  	mvn	w28, w26
   cc174: fb 03 1a 4b  	neg	w27, w26
   cc178: 14 fd 66 d3  	lsr	x20, x8, #38
   cc17c: 08 00 00 14  	b	0xcc19c <halRxWaitResponse+0xfc>
   cc180: a0 83 5f f8  	ldur	x0, [x29, #-8]
   cc184: e1 0b 40 f9  	ldr	x1, [sp, #16]
   cc188: 00 00 00 94  	bl	0xcc188 <halRxWaitResponse+0xe8>
		00000000000cc188:  R_AARCH64_CALL26	usleep_range
   cc18c: e0 03 17 aa  	mov	x0, x23
   cc190: 00 00 00 94  	bl	0xcc190 <halRxWaitResponse+0xf0>
		00000000000cc190:  R_AARCH64_CALL26	wlanIsChipNoAck
   cc194: 1f 1c 00 72  	tst	w0, #0xff
   cc198: 61 fb ff 54  	b.ne	0xcc104 <halRxWaitResponse+0x64>
   cc19c: 21 00 80 52  	mov	w1, #1
   cc1a0: 04 e0 81 52  	mov	w4, #3840
   cc1a4: e0 03 18 aa  	mov	x0, x24
   cc1a8: e2 03 16 2a  	mov	w2, w22
   cc1ac: e3 03 19 aa  	mov	x3, x25
   cc1b0: 00 00 00 94  	bl	0xcc1b0 <halRxWaitResponse+0x110>
		00000000000cc1b0:  R_AARCH64_CALL26	kalDevPortRead
   cc1b4: 1f 1c 00 72  	tst	w0, #0xff
   cc1b8: e1 01 00 54  	b.ne	0xcc1f4 <halRxWaitResponse+0x154>
   cc1bc: 08 00 00 90  	adrp	x8, 0xcc000 <halRxWaitResponse+0x11c>
		00000000000cc1bc:  R_AARCH64_ADR_PREL_PG_HI21	jiffies
   cc1c0: 00 01 40 f9  	ldr	x0, [x8]
		00000000000cc1c0:  R_AARCH64_LDST64_ABS_LO12_NC	jiffies
   cc1c4: 00 00 00 94  	bl	0xcc1c4 <halRxWaitResponse+0x124>
		00000000000cc1c4:  R_AARCH64_CALL26	jiffies_to_msecs
   cc1c8: 1f 00 1a 6b  	cmp	w0, w26
   cc1cc: 88 33 9b 1a  	csel	w8, w28, w27, lo
   cc1d0: 08 01 00 0b  	add	w8, w8, w0
   cc1d4: 1f 01 13 6b  	cmp	w8, w19
   cc1d8: c8 fa ff 54  	b.hi	0xcc130 <halRxWaitResponse+0x90>
   cc1dc: 28 e2 84 52  	mov	w8, #10001
   cc1e0: bf 02 08 6b  	cmp	w21, w8
   cc1e4: e3 fc ff 54  	b.lo	0xcc180 <halRxWaitResponse+0xe0>
   cc1e8: e0 03 14 2a  	mov	w0, w20
   cc1ec: 00 00 00 94  	bl	0xcc1ec <halRxWaitResponse+0x14c>
		00000000000cc1ec:  R_AARCH64_CALL26	msleep
   cc1f0: e7 ff ff 17  	b	0xcc18c <halRxWaitResponse+0xec>
   cc1f4: e8 07 40 f9  	ldr	x8, [sp, #8]
   cc1f8: e0 03 1f 2a  	mov	w0, wzr
   cc1fc: 16 01 00 b9  	str	w22, [x8]
   cc200: cd ff ff 17  	b	0xcc134 <halRxWaitResponse+0x94>

00000000000d26c4 <kalDevPortRead>:
   d26c4: ff c3 01 d1  	sub	sp, sp, #112
   d26c8: fd 7b 01 a9  	stp	x29, x30, [sp, #16]
   d26cc: fc 6f 02 a9  	stp	x28, x27, [sp, #32]
   d26d0: fa 67 03 a9  	stp	x26, x25, [sp, #48]
   d26d4: f8 5f 04 a9  	stp	x24, x23, [sp, #64]
   d26d8: f6 57 05 a9  	stp	x22, x21, [sp, #80]
   d26dc: f4 4f 06 a9  	stp	x20, x19, [sp, #96]
   d26e0: fd 43 00 91  	add	x29, sp, #16
   d26e4: fa 03 01 2a  	mov	w26, w1
   d26e8: 08 08 88 52  	mov	w8, #16448
   d26ec: 19 c0 19 91  	add	x25, x0, #1648
   d26f0: 5b 3f 40 92  	and	x27, x26, #0xffff
   d26f4: 28 00 a0 72  	movk	w8, #1, lsl #16
   d26f8: 76 67 08 9b  	madd	x22, x27, x8, x25
   d26fc: 88 52 90 52  	mov	w8, #33428
   d2700: e8 00 a0 72  	movk	w8, #7, lsl #16
   d2704: dc 02 08 8b  	add	x28, x22, x8
   d2708: 81 03 40 b9  	ldr	w1, [x28]
   d270c: 08 4f 90 52  	mov	w8, #33400
   d2710: e8 00 a0 72  	movk	w8, #7, lsl #16
   d2714: d4 02 08 8b  	add	x20, x22, x8
   d2718: f8 03 02 2a  	mov	w24, w2
   d271c: e2 03 14 aa  	mov	x2, x20
   d2720: e3 07 00 f9  	str	x3, [sp, #8]
   d2724: f3 03 00 aa  	mov	x19, x0
   d2728: 17 0a 80 52  	mov	w23, #80
   d272c: 00 00 00 94  	bl	0xd272c <kalDevPortRead+0x68>
		00000000000d272c:  R_AARCH64_CALL26	kalDevRegRead
   d2730: 89 50 90 52  	mov	w9, #33412
   d2734: 88 02 40 b9  	ldr	w8, [x20]
   d2738: e9 00 a0 72  	movk	w9, #7, lsl #16
   d273c: c9 6a 69 b8  	ldr	w9, [x22, x9]
   d2740: e0 03 13 aa  	mov	x0, x19
   d2744: 08 05 00 11  	add	w8, w8, #1
   d2748: e1 03 1a 2a  	mov	w1, w26
   d274c: 0a 09 c9 1a  	udiv	w10, w8, w9
   d2750: 55 a1 09 1b  	msub	w21, w10, w9, w8
   d2754: 08 50 88 52  	mov	w8, #17024
   d2758: b7 5a b7 9b  	umaddl	x23, w21, w23, x22
   d275c: c8 00 a0 72  	movk	w8, #6, lsl #16
   d2760: f6 6a 68 f8  	ldr	x22, [x23, x8]
   d2764: 00 00 00 94  	bl	0xd2764 <kalDevPortRead+0xa0>
		00000000000d2764:  R_AARCH64_CALL26	halWpdmaGetRxDmaDoneCnt
   d2768: 60 12 00 34  	cbz	w0, 0xd29b4 <kalDevPortRead+0x2f0>
   d276c: 08 08 88 52  	mov	w8, #16448
   d2770: 28 00 a0 72  	movk	w8, #1, lsl #16
   d2774: 09 1d 89 52  	mov	w9, #18664
   d2778: 68 4f 08 9b  	madd	x8, x27, x8, x19
   d277c: c9 00 a0 72  	movk	w9, #6, lsl #16
   d2780: 01 01 09 8b  	add	x1, x8, x9
   d2784: e0 03 13 aa  	mov	x0, x19
   d2788: e2 03 16 aa  	mov	x2, x22
   d278c: e3 03 1a 2a  	mov	w3, w26
   d2790: 93 00 00 94  	bl	0xd29dc <kalWaitRxDmaDone>
   d2794: c0 07 00 36  	tbz	w0, #0, 0xd288c <kalDevPortRead+0x1c8>
   d2798: c8 06 40 b9  	ldr	w8, [x22, #4]
   d279c: 08 01 f0 36  	tbz	w8, #30, 0xd27bc <kalDevPortRead+0xf8>
   d27a0: 09 08 88 52  	mov	w9, #16448
   d27a4: 29 00 a0 72  	movk	w9, #1, lsl #16
   d27a8: 0a 1f 91 52  	mov	w10, #35064
   d27ac: 69 4f 09 9b  	madd	x9, x27, x9, x19
   d27b0: ea 00 a0 72  	movk	w10, #7, lsl #16
   d27b4: 29 69 6a 38  	ldrb	w9, [x9, x10]
   d27b8: 89 09 00 34  	cbz	w9, 0xd28e8 <kalDevPortRead+0x224>
   d27bc: 09 00 00 90  	adrp	x9, 0xd2000 <kalDevPortRead+0xf8>
		00000000000d27bc:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
   d27c0: 29 01 00 91  	add	x9, x9, #0
		00000000000d27c0:  R_AARCH64_ADD_ABS_LO12_NC	aucDebugModule
   d27c4: 29 05 40 39  	ldrb	w9, [x9, #1]
   d27c8: 69 01 08 36  	tbz	w9, #1, 0xd27f4 <kalDevPortRead+0x130>
   d27cc: 09 41 38 d5  	mrs	x9, SP_EL0
   d27d0: 21 b9 45 b9  	ldr	w1, [x9, #1464]
   d27d4: 00 00 00 90  	adrp	x0, 0xd2000 <kalDevPortRead+0x110>
		00000000000d27d4:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x4b451
   d27d8: 02 00 00 90  	adrp	x2, 0xd2000 <kalDevPortRead+0x114>
		00000000000d27d8:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x3a274
   d27dc: 03 75 10 53  	ubfx	w3, w8, #16, #14
   d27e0: 04 79 1e 53  	ubfx	w4, w8, #30, #1
   d27e4: 00 00 00 91  	add	x0, x0, #0
		00000000000d27e4:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x4b451
   d27e8: 42 00 00 91  	add	x2, x2, #0
		00000000000d27e8:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x3a274
   d27ec: 00 00 00 94  	bl	0xd27ec <kalDevPortRead+0x128>
		00000000000d27ec:  R_AARCH64_CALL26	kalPrintLog
   d27f0: c8 06 40 b9  	ldr	w8, [x22, #4]
   d27f4: 09 08 88 52  	mov	w9, #16448
   d27f8: 2a 00 80 52  	mov	w10, #1
   d27fc: 29 00 a0 72  	movk	w9, #1, lsl #16
   d2800: 48 79 68 0a  	bic	w8, w10, w8, lsr #30
   d2804: 0a 1f 91 52  	mov	w10, #35064
   d2808: 69 4f 09 9b  	madd	x9, x27, x9, x19
   d280c: ea 00 a0 72  	movk	w10, #7, lsl #16
   d2810: f7 03 1f 2a  	mov	w23, wzr
   d2814: 28 69 2a 38  	strb	w8, [x9, x10]
   d2818: 08 08 88 52  	mov	w8, #16448
   d281c: 28 00 a0 72  	movk	w8, #1, lsl #16
   d2820: 09 1e 91 52  	mov	w9, #35056
   d2824: e9 00 a0 72  	movk	w9, #7, lsl #16
   d2828: ca 06 40 b9  	ldr	w10, [x22, #4]
   d282c: 78 4f 08 9b  	madd	x24, x27, x8, x19
   d2830: 08 6b 69 b8  	ldr	w8, [x24, x9]
   d2834: e9 ff b7 12  	mov	w9, #1073807359
   d2838: 49 01 09 0a  	and	w9, w10, w9
   d283c: e0 03 13 aa  	mov	x0, x19
   d2840: 09 35 10 33  	bfi	w9, w8, #16, #14
   d2844: c9 06 00 b9  	str	w9, [x22, #4]
   d2848: 95 02 00 b9  	str	w21, [x20]
   d284c: 81 03 40 b9  	ldr	w1, [x28]
   d2850: 08 97 89 52  	mov	w8, #19640
   d2854: e8 01 a0 72  	movk	w8, #15, lsl #16
   d2858: e2 03 15 2a  	mov	w2, w21
   d285c: 74 02 08 8b  	add	x20, x19, x8
   d2860: 00 00 00 94  	bl	0xd2860 <kalDevPortRead+0x19c>
		00000000000d2860:  R_AARCH64_CALL26	kalDevRegWrite
   d2864: 08 22 91 52  	mov	w8, #35088
   d2868: e8 00 a0 72  	movk	w8, #7, lsl #16
   d286c: 1f 6b 28 38  	strb	wzr, [x24, x8]
   d2870: 88 02 40 f9  	ldr	x8, [x20]
   d2874: 89 82 81 52  	mov	w9, #3092
   d2878: 49 02 a0 72  	movk	w9, #18, lsl #16
   d287c: 20 00 80 52  	mov	w0, #1
   d2880: 01 01 09 8b  	add	x1, x8, x9
   d2884: 00 00 00 94  	bl	0xd2884 <kalDevPortRead+0x1c0>
		00000000000d2884:  R_AARCH64_CALL26	__ll_sc_atomic_add
   d2888: 4c 00 00 14  	b	0xd29b8 <kalDevPortRead+0x2f4>
   d288c: 08 08 88 52  	mov	w8, #16448
   d2890: 28 00 a0 72  	movk	w8, #1, lsl #16
   d2894: 09 22 91 52  	mov	w9, #35088
   d2898: 68 4f 08 9b  	madd	x8, x27, x8, x19
   d289c: e9 00 a0 72  	movk	w9, #7, lsl #16
   d28a0: 13 01 09 8b  	add	x19, x8, x9
   d28a4: 68 02 40 39  	ldrb	w8, [x19]
   d28a8: 88 01 00 35  	cbnz	w8, 0xd28d8 <kalDevPortRead+0x214>
   d28ac: 08 00 00 90  	adrp	x8, 0xd2000 <kalDevPortRead+0x1e8>
		00000000000d28ac:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
   d28b0: 08 01 00 91  	add	x8, x8, #0
		00000000000d28b0:  R_AARCH64_ADD_ABS_LO12_NC	aucDebugModule
   d28b4: 08 05 40 39  	ldrb	w8, [x8, #1]
   d28b8: 08 01 00 36  	tbz	w8, #0, 0xd28d8 <kalDevPortRead+0x214>
   d28bc: 08 41 38 d5  	mrs	x8, SP_EL0
   d28c0: 01 b9 45 b9  	ldr	w1, [x8, #1464]
   d28c4: 00 00 00 90  	adrp	x0, 0xd2000 <kalDevPortRead+0x200>
		00000000000d28c4:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x55fff
   d28c8: 02 00 00 90  	adrp	x2, 0xd2000 <kalDevPortRead+0x204>
		00000000000d28c8:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x3a274
   d28cc: 00 00 00 91  	add	x0, x0, #0
		00000000000d28cc:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x55fff
   d28d0: 42 00 00 91  	add	x2, x2, #0
		00000000000d28d0:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x3a274
   d28d4: 00 00 00 94  	bl	0xd28d4 <kalDevPortRead+0x210>
		00000000000d28d4:  R_AARCH64_CALL26	kalPrintLog
   d28d8: 28 00 80 52  	mov	w8, #1
   d28dc: f7 03 1f 2a  	mov	w23, wzr
   d28e0: 68 02 00 39  	strb	w8, [x19]
   d28e4: 35 00 00 14  	b	0xd29b8 <kalDevPortRead+0x2f4>
   d28e8: 03 75 10 53  	ubfx	w3, w8, #16, #14
   d28ec: 7f 00 18 6b  	cmp	w3, w24
   d28f0: c9 01 00 54  	b.ls	0xd2928 <kalDevPortRead+0x264>
   d28f4: 08 00 00 90  	adrp	x8, 0xd2000 <kalDevPortRead+0x230>
		00000000000d28f4:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
   d28f8: 08 01 00 91  	add	x8, x8, #0
		00000000000d28f8:  R_AARCH64_ADD_ABS_LO12_NC	aucDebugModule
   d28fc: 08 05 40 39  	ldrb	w8, [x8, #1]
   d2900: 68 05 08 36  	tbz	w8, #1, 0xd29ac <kalDevPortRead+0x2e8>
   d2904: 08 41 38 d5  	mrs	x8, SP_EL0
   d2908: 01 b9 45 b9  	ldr	w1, [x8, #1464]
   d290c: 00 00 00 90  	adrp	x0, 0xd2000 <kalDevPortRead+0x248>
		00000000000d290c:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x56033
   d2910: 02 00 00 90  	adrp	x2, 0xd2000 <kalDevPortRead+0x24c>
		00000000000d2910:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x3a274
   d2914: 00 00 00 91  	add	x0, x0, #0
		00000000000d2914:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x56033
   d2918: 42 00 00 91  	add	x2, x2, #0
		00000000000d2918:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x3a274
   d291c: e4 03 18 2a  	mov	w4, w24
   d2920: 00 00 00 94  	bl	0xd2920 <kalDevPortRead+0x25c>
		00000000000d2920:  R_AARCH64_CALL26	kalPrintLog
   d2924: 22 00 00 14  	b	0xd29ac <kalDevPortRead+0x2e8>
   d2928: 68 5e 43 f9  	ldr	x8, [x19, #1720]
   d292c: 48 02 00 b4  	cbz	x8, 0xd2974 <kalDevPortRead+0x2b0>
   d2930: 09 4f 88 52  	mov	w9, #17016
   d2934: 0b 08 88 52  	mov	w11, #16448
   d2938: c9 00 a0 72  	movk	w9, #6, lsl #16
   d293c: 2b 00 a0 72  	movk	w11, #1, lsl #16
   d2940: 0a 0a 80 52  	mov	w10, #80
   d2944: e1 02 09 8b  	add	x1, x23, x9
   d2948: 69 4f 0b 9b  	madd	x9, x27, x11, x19
   d294c: e4 07 40 f9  	ldr	x4, [sp, #8]
   d2950: a9 26 0a 9b  	madd	x9, x21, x10, x9
   d2954: 0a 23 89 52  	mov	w10, #18712
   d2958: ca 00 a0 72  	movk	w10, #6, lsl #16
   d295c: 23 01 0a 8b  	add	x3, x9, x10
   d2960: e0 03 19 aa  	mov	x0, x25
   d2964: e2 03 16 aa  	mov	x2, x22
   d2968: e5 03 18 2a  	mov	w5, w24
   d296c: 00 01 3f d6  	blr	x8
   d2970: 20 02 00 36  	tbz	w0, #0, 0xd29b4 <kalDevPortRead+0x2f0>
   d2974: 09 08 88 52  	mov	w9, #16448
   d2978: 29 00 a0 72  	movk	w9, #1, lsl #16
   d297c: 08 0a 80 52  	mov	w8, #80
   d2980: 0a 25 89 52  	mov	w10, #18728
   d2984: 69 4f 09 9b  	madd	x9, x27, x9, x19
   d2988: ca 00 a0 72  	movk	w10, #6, lsl #16
   d298c: a8 26 08 9b  	madd	x8, x21, x8, x9
   d2990: 09 69 6a f8  	ldr	x9, [x8, x10]
   d2994: 8a 25 89 52  	mov	w10, #18732
   d2998: ca 00 a0 72  	movk	w10, #6, lsl #16
   d299c: c9 02 00 b9  	str	w9, [x22]
   d29a0: 08 69 6a b8  	ldr	w8, [x8, x10]
   d29a4: 08 0d 00 12  	and	w8, w8, #0xf
   d29a8: c8 0a 00 b9  	str	w8, [x22, #8]
   d29ac: 37 00 80 52  	mov	w23, #1
   d29b0: 9a ff ff 17  	b	0xd2818 <kalDevPortRead+0x154>
   d29b4: f7 03 1f 2a  	mov	w23, wzr
   d29b8: e0 03 17 2a  	mov	w0, w23
   d29bc: f4 4f 46 a9  	ldp	x20, x19, [sp, #96]
   d29c0: f6 57 45 a9  	ldp	x22, x21, [sp, #80]
   d29c4: f8 5f 44 a9  	ldp	x24, x23, [sp, #64]
   d29c8: fa 67 43 a9  	ldp	x26, x25, [sp, #48]
   d29cc: fc 6f 42 a9  	ldp	x28, x27, [sp, #32]
   d29d0: fd 7b 41 a9  	ldp	x29, x30, [sp, #16]
   d29d4: ff c3 01 91  	add	sp, sp, #112
   d29d8: c0 03 5f d6  	ret

000000000015e3cc <wlanConfigWifiFuncStatus>:
  15e3cc: ff 83 01 d1  	sub	sp, sp, #96
  15e3d0: fd 7b 01 a9  	stp	x29, x30, [sp, #16]
  15e3d4: f9 13 00 f9  	str	x25, [sp, #32]
  15e3d8: f8 5f 03 a9  	stp	x24, x23, [sp, #48]
  15e3dc: f6 57 04 a9  	stp	x22, x21, [sp, #64]
  15e3e0: f4 4f 05 a9  	stp	x20, x19, [sp, #80]
  15e3e4: fd 43 00 91  	add	x29, sp, #16
  15e3e8: 08 00 00 90  	adrp	x8, 0x15e000 <wlanConfigWifiFuncStatus+0x1c>
		000000000015e3e8:  R_AARCH64_ADR_PREL_PG_HI21	__stack_chk_guard
  15e3ec: 08 01 40 f9  	ldr	x8, [x8]
		000000000015e3ec:  R_AARCH64_LDST64_ABS_LO12_NC	__stack_chk_guard
  15e3f0: 18 41 38 d5  	mrs	x24, SP_EL0
  15e3f4: 0b 04 80 52  	mov	w11, #32
  15e3f8: 0c 18 80 52  	mov	w12, #192
  15e3fc: e8 07 00 f9  	str	x8, [sp, #8]
  15e400: 19 00 40 f9  	ldr	x25, [x0]
  15e404: 0b 09 a0 72  	movk	w11, #72, lsl #16
  15e408: 0c 0c a0 72  	movk	w12, #96, lsl #16
  15e40c: f5 03 01 2a  	mov	w21, w1
  15e410: 28 5b 40 b9  	ldr	w8, [x25, #88]
  15e414: 29 63 40 b9  	ldr	w9, [x25, #96]
  15e418: 0a 1b 40 b9  	ldr	w10, [x24, #24]
  15e41c: f6 03 00 aa  	mov	x22, x0
  15e420: 08 01 09 0b  	add	w8, w8, w9
  15e424: 5f 31 18 72  	tst	w10, #0x1fff00
  15e428: 14 11 00 11  	add	w20, w8, #4
  15e42c: 81 01 8b 1a  	csel	w1, w12, w11, eq
  15e430: e0 03 14 aa  	mov	x0, x20
  15e434: 00 00 00 94  	bl	0x15e434 <wlanConfigWifiFuncStatus+0x68>
		000000000015e434:  R_AARCH64_CALL26	__kmalloc
  15e438: c0 06 00 b4  	cbz	x0, 0x15e510 <wlanConfigWifiFuncStatus+0x144>
  15e43c: 08 48 96 52  	mov	w8, #45632
  15e440: 48 00 a0 72  	movk	w8, #2, lsl #16
  15e444: f3 03 00 aa  	mov	x19, x0
  15e448: c0 6a 68 f8  	ldr	x0, [x22, x8]
  15e44c: 00 00 00 94  	bl	0x15e44c <wlanConfigWifiFuncStatus+0x80>
		000000000015e44c:  R_AARCH64_CALL26	kalIsCardRemoved
  15e450: 08 1c 00 12  	and	w8, w0, #0xff
  15e454: 1f 05 00 71  	cmp	w8, #1
  15e458: f7 0b 02 32  	mov	w23, #-1073741823
  15e45c: 40 05 00 54  	b.eq	0x15e504 <wlanConfigWifiFuncStatus+0x138>
  15e460: 08 00 00 90  	adrp	x8, 0x15e000 <wlanConfigWifiFuncStatus+0x94>
		000000000015e460:  R_AARCH64_ADR_PREL_PG_HI21	fgIsBusAccessFailed
  15e464: 08 01 40 39  	ldrb	w8, [x8]
		000000000015e464:  R_AARCH64_LDST8_ABS_LO12_NC	fgIsBusAccessFailed
  15e468: 1f 05 00 71  	cmp	w8, #1
  15e46c: c0 04 00 54  	b.eq	0x15e504 <wlanConfigWifiFuncStatus+0x138>
  15e470: e4 13 00 91  	add	x4, sp, #4
  15e474: 21 00 80 52  	mov	w1, #1
  15e478: e0 03 16 aa  	mov	x0, x22
  15e47c: e2 03 13 aa  	mov	x2, x19
  15e480: e3 03 14 2a  	mov	w3, w20
  15e484: 00 00 00 94  	bl	0x15e484 <wlanConfigWifiFuncStatus+0xb8>
		000000000015e484:  R_AARCH64_CALL26	nicRxWaitResponse
  15e488: c0 03 00 35  	cbnz	w0, 0x15e500 <wlanConfigWifiFuncStatus+0x134>
  15e48c: 28 5b 40 b9  	ldr	w8, [x25, #88]
  15e490: 16 00 00 90  	adrp	x22, 0x15e000 <wlanConfigWifiFuncStatus+0xc4>
		000000000015e490:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
  15e494: 68 02 08 8b  	add	x8, x19, x8
  15e498: 09 11 40 39  	ldrb	w9, [x8, #4]
  15e49c: 3f 05 00 71  	cmp	w9, #1
  15e4a0: c1 01 00 54  	b.ne	0x15e4d8 <wlanConfigWifiFuncStatus+0x10c>
  15e4a4: 09 15 40 39  	ldrb	w9, [x8, #5]
  15e4a8: 3f 01 35 6b  	cmp	w9, w21, uxtb
  15e4ac: 61 01 00 54  	b.ne	0x15e4d8 <wlanConfigWifiFuncStatus+0x10c>
  15e4b0: 03 21 40 39  	ldrb	w3, [x8, #8]
  15e4b4: c3 05 00 34  	cbz	w3, 0x15e56c <wlanConfigWifiFuncStatus+0x1a0>
  15e4b8: c8 02 40 39  	ldrb	w8, [x22]
		000000000015e4b8:  R_AARCH64_LDST8_ABS_LO12_NC	aucDebugModule
  15e4bc: e8 00 00 36  	tbz	w8, #0, 0x15e4d8 <wlanConfigWifiFuncStatus+0x10c>
  15e4c0: 01 bb 45 b9  	ldr	w1, [x24, #1464]
  15e4c4: 00 00 00 90  	adrp	x0, 0x15e000 <wlanConfigWifiFuncStatus+0xf8>
		000000000015e4c4:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x37eb5
  15e4c8: 02 00 00 90  	adrp	x2, 0x15e000 <wlanConfigWifiFuncStatus+0xfc>
		000000000015e4c8:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0xe551
  15e4cc: 00 00 00 91  	add	x0, x0, #0
		000000000015e4cc:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x37eb5
  15e4d0: 42 00 00 91  	add	x2, x2, #0
		000000000015e4d0:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0xe551
  15e4d4: 00 00 00 94  	bl	0x15e4d4 <wlanConfigWifiFuncStatus+0x108>
		000000000015e4d4:  R_AARCH64_CALL26	kalPrintLog
  15e4d8: c8 02 40 39  	ldrb	w8, [x22]
		000000000015e4d8:  R_AARCH64_LDST8_ABS_LO12_NC	aucDebugModule
  15e4dc: 28 01 08 36  	tbz	w8, #1, 0x15e500 <wlanConfigWifiFuncStatus+0x134>
  15e4e0: 00 00 00 90  	adrp	x0, 0x15e000 <wlanConfigWifiFuncStatus+0x114>
		000000000015e4e0:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x2d661
  15e4e4: 01 00 00 90  	adrp	x1, 0x15e000 <wlanConfigWifiFuncStatus+0x118>
		000000000015e4e4:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0xe551
  15e4e8: 00 00 00 91  	add	x0, x0, #0
		000000000015e4e8:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x2d661
  15e4ec: 21 00 00 91  	add	x1, x1, #0
		000000000015e4ec:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0xe551
  15e4f0: 00 00 00 94  	bl	0x15e4f0 <wlanConfigWifiFuncStatus+0x124>
		000000000015e4f0:  R_AARCH64_CALL26	kalPrintLog
  15e4f4: e0 03 13 aa  	mov	x0, x19
  15e4f8: e1 03 14 2a  	mov	w1, w20
  15e4fc: 00 00 00 94  	bl	0x15e4fc <wlanConfigWifiFuncStatus+0x130>
		000000000015e4fc:  R_AARCH64_CALL26	dumpMemory8
  15e500: f7 0b 02 32  	mov	w23, #-1073741823
  15e504: e0 03 13 aa  	mov	x0, x19
  15e508: 00 00 00 94  	bl	0x15e508 <wlanConfigWifiFuncStatus+0x13c>
		000000000015e508:  R_AARCH64_CALL26	kvfree
  15e50c: 0b 00 00 14  	b	0x15e538 <wlanConfigWifiFuncStatus+0x16c>
  15e510: 08 00 00 90  	adrp	x8, 0x15e000 <wlanConfigWifiFuncStatus+0x144>
		000000000015e510:  R_AARCH64_ADR_PREL_PG_HI21	aucDebugModule
  15e514: 08 01 40 39  	ldrb	w8, [x8]
		000000000015e514:  R_AARCH64_LDST8_ABS_LO12_NC	aucDebugModule
  15e518: e8 00 00 36  	tbz	w8, #0, 0x15e534 <wlanConfigWifiFuncStatus+0x168>
  15e51c: 01 bb 45 b9  	ldr	w1, [x24, #1464]
  15e520: 00 00 00 90  	adrp	x0, 0x15e000 <wlanConfigWifiFuncStatus+0x154>
		000000000015e520:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x78fa
  15e524: 02 00 00 90  	adrp	x2, 0x15e000 <wlanConfigWifiFuncStatus+0x158>
		000000000015e524:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0xe551
  15e528: 00 00 00 91  	add	x0, x0, #0
		000000000015e528:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x78fa
  15e52c: 42 00 00 91  	add	x2, x2, #0
		000000000015e52c:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0xe551
  15e530: 00 00 00 94  	bl	0x15e530 <wlanConfigWifiFuncStatus+0x164>
		000000000015e530:  R_AARCH64_CALL26	kalPrintLog
  15e534: f7 0b 02 32  	mov	w23, #-1073741823
  15e538: 09 00 00 90  	adrp	x9, 0x15e000 <wlanConfigWifiFuncStatus+0x16c>
		000000000015e538:  R_AARCH64_ADR_PREL_PG_HI21	__stack_chk_guard
  15e53c: e8 07 40 f9  	ldr	x8, [sp, #8]
  15e540: 29 01 40 f9  	ldr	x9, [x9]
		000000000015e540:  R_AARCH64_LDST64_ABS_LO12_NC	__stack_chk_guard
  15e544: 3f 01 08 eb  	cmp	x9, x8
  15e548: 61 01 00 54  	b.ne	0x15e574 <wlanConfigWifiFuncStatus+0x1a8>
  15e54c: e0 03 17 2a  	mov	w0, w23
  15e550: f4 4f 45 a9  	ldp	x20, x19, [sp, #80]
  15e554: f6 57 44 a9  	ldp	x22, x21, [sp, #64]
  15e558: f8 5f 43 a9  	ldp	x24, x23, [sp, #48]
  15e55c: f9 13 40 f9  	ldr	x25, [sp, #32]
  15e560: fd 7b 41 a9  	ldp	x29, x30, [sp, #16]
  15e564: ff 83 01 91  	add	sp, sp, #96
  15e568: c0 03 5f d6  	ret
  15e56c: f7 03 1f 2a  	mov	w23, wzr
  15e570: e5 ff ff 17  	b	0x15e504 <wlanConfigWifiFuncStatus+0x138>
  15e574: 00 00 00 94  	bl	0x15e574 <wlanConfigWifiFuncStatus+0x1a8>
		000000000015e574:  R_AARCH64_CALL26	__stack_chk_fail
