
$NORD2_REPO/research/nord2/wifi/next-stage/main-fw-transport/phy-action/module-evidence/wlan_drv_gen4m.ko:	file format elf64-littleaarch64

Disassembly of section .text:

000000000015c098 <wlanPhyAction>:
  15c098:      	stp	x29, x30, [sp, #-32]!
  15c09c:      	str	x19, [sp, #16]
  15c0a0:      	mov	x29, sp
  15c0a4:      	mov	x19, x0
  15c0a8:      	mov	x0, xzr
  15c0ac:      	mov	w1, wzr
  15c0b0:      	bl	0x15c0b0 <wlanPhyAction+0x18>
  15c0b4:      	cmp	w0, #0
  15c0b8:      	mov	w8, #1
  15c0bc:      	cinc	w2, w8, eq
  15c0c0:      	mov	w1, #1
  15c0c4:      	mov	x0, x19
  15c0c8:      	bl	0x15c0c8 <wlanPhyAction+0x30>
  15c0cc:      	ldr	x19, [sp, #16]
  15c0d0:      	ldp	x29, x30, [sp], #32
  15c0d4:      	ret

000000000015f9e8 <wlanDownloadFW>:
  15f9e8:      	sub	sp, sp, #96
  15f9ec:      	stp	x29, x30, [sp, #32]
  15f9f0:      	stp	x24, x23, [sp, #48]
  15f9f4:      	stp	x22, x21, [sp, #64]
  15f9f8:      	stp	x20, x19, [sp, #80]
  15f9fc:      	add	x29, sp, #32
  15fa00:      	adrp	x8, 0x15f000 <wlanDownloadFW+0x18>
  15fa04:      	ldr	x8, [x8]
  15fa08:      	stur	x8, [x29, #-8]
  15fa0c:      	cbz	x0, 0x15fab4 <wlanDownloadFW+0xcc>
  15fa10:      	ldr	x22, [x0]
  15fa14:      	adrp	x21, 0x15f000 <wlanDownloadFW+0x2c>
  15fa18:      	ldrb	w8, [x21]
  15fa1c:      	mov	w9, #45632
  15fa20:      	ldr	x23, [x22, #8]
  15fa24:      	movk	w9, #2, lsl #16
  15fa28:      	mov	x19, x0
  15fa2c:      	add	x20, x0, x9
  15fa30:      	mov	x24, x22
  15fa34:      	tbz	w8, #5, 0x15fa5c <wlanDownloadFW+0x74>
  15fa38:      	mrs	x8, SP_EL0
  15fa3c:      	ldr	w1, [x8, #1464]
  15fa40:      	ldr	w3, [x22, #60]
  15fa44:      	adrp	x0, 0x15f000 <wlanDownloadFW+0x5c>
  15fa48:      	adrp	x2, 0x15f000 <wlanDownloadFW+0x60>
  15fa4c:      	add	x0, x0, #0
  15fa50:      	add	x2, x2, #0
  15fa54:      	bl	0x15fa54 <wlanDownloadFW+0x6c>
  15fa58:      	ldr	x24, [x19]
  15fa5c:      	str	wzr, [sp, #8]
  15fa60:      	ldr	x0, [x20]
  15fa64:      	ldr	w1, [x24, #56]
  15fa68:      	add	x2, sp, #8
  15fa6c:      	bl	0x15fa6c <wlanDownloadFW+0x84>
  15fa70:      	ldp	w8, w9, [x24, #60]
  15fa74:      	ldr	w10, [sp, #8]
  15fa78:      	lsl	w8, w8, w9
  15fa7c:      	bics	wzr, w8, w10
  15fa80:      	b.ne	0x15fac4 <wlanDownloadFW+0xdc>
  15fa84:      	ldrb	w8, [x21]
  15fa88:      	tbz	w8, #5, 0x15faa8 <wlanDownloadFW+0xc0>
  15fa8c:      	mrs	x8, SP_EL0
  15fa90:      	ldr	w1, [x8, #1464]
  15fa94:      	adrp	x0, 0x15f000 <wlanDownloadFW+0xac>
  15fa98:      	adrp	x2, 0x15f000 <wlanDownloadFW+0xb0>
  15fa9c:      	add	x0, x0, #0
  15faa0:      	add	x2, x2, #0
  15faa4:      	bl	0x15faa4 <wlanDownloadFW+0xbc>
  15faa8:      	mov	x0, x19
  15faac:      	bl	0x15faac <wlanDownloadFW+0xc4>
  15fab0:      	cbz	w0, 0x15fabc <wlanDownloadFW+0xd4>
  15fab4:      	mov	w20, #-1073741823
  15fab8:      	b	0x15fbbc <wlanDownloadFW+0x1d4>
  15fabc:      	mov	x0, x19
  15fac0:      	bl	0x15fac0 <wlanDownloadFW+0xd8>
  15fac4:      	mov	w1, #1
  15fac8:      	mov	x0, x19
  15facc:      	bl	0x15facc <wlanDownloadFW+0xe4>
  15fad0:      	ldr	x8, [x23, #16]
  15fad4:      	cbz	x8, 0x15fae0 <wlanDownloadFW+0xf8>
  15fad8:      	mov	x0, x19
  15fadc:      	blr	x8
  15fae0:      	ldrb	w8, [x22, #504]
  15fae4:      	tbnz	w8, #0, 0x15faf0 <wlanDownloadFW+0x108>
  15fae8:      	mov	w20, wzr
  15faec:      	b	0x15fb74 <wlanDownloadFW+0x18c>
  15faf0:      	add	x0, sp, #8
  15faf4:      	bl	0x15faf4 <wlanDownloadFW+0x10c>
  15faf8:      	ldr	x8, [sp, #16]
  15fafc:      	mov	x9, #63439
  15fb00:      	movk	x9, #58195, lsl #16
  15fb04:      	movk	x9, #39845, lsl #32
  15fb08:      	ldr	w2, [sp, #8]
  15fb0c:      	movk	x9, #8388, lsl #48
  15fb10:      	smulh	x8, x8, x9
  15fb14:      	lsr	x9, x8, #63
  15fb18:      	lsr	x8, x8, #7
  15fb1c:      	add	w3, w8, w9
  15fb20:      	mov	w1, #1
  15fb24:      	mov	x0, x19
  15fb28:      	bl	0x15fb28 <wlanDownloadFW+0x140>
  15fb2c:      	mov	w20, w0
  15fb30:      	cbz	w0, 0x15fb60 <wlanDownloadFW+0x178>
  15fb34:      	ldrb	w8, [x21]
  15fb38:      	tbz	w8, #1, 0x15fb74 <wlanDownloadFW+0x18c>
  15fb3c:      	mrs	x8, SP_EL0
  15fb40:      	ldr	w1, [x8, #1464]
  15fb44:      	adrp	x0, 0x15f000 <wlanDownloadFW+0x15c>
  15fb48:      	adrp	x2, 0x15f000 <wlanDownloadFW+0x160>
  15fb4c:      	add	x0, x0, #0
  15fb50:      	add	x2, x2, #0
  15fb54:      	mov	w3, #9
  15fb58:      	bl	0x15fb58 <wlanDownloadFW+0x170>
  15fb5c:      	b	0x15fb74 <wlanDownloadFW+0x18c>
  15fb60:      	ldr	x8, [sp, #8]
  15fb64:      	mov	w9, #3288
  15fb68:      	movk	w9, #18, lsl #16
  15fb6c:      	add	x9, x19, x9
  15fb70:      	str	w8, [x9]
  15fb74:      	ldr	x8, [x23, #56]
  15fb78:      	cbz	x8, 0x15fb88 <wlanDownloadFW+0x1a0>
  15fb7c:      	mov	x0, x19
  15fb80:      	blr	x8
  15fb84:      	mov	w20, w0
  15fb88:      	cbz	w20, 0x15fbec <wlanDownloadFW+0x204>
  15fb8c:      	ldrb	w8, [x21]
  15fb90:      	tbz	w8, #4, 0x15fbb0 <wlanDownloadFW+0x1c8>
  15fb94:      	mrs	x8, SP_EL0
  15fb98:      	ldr	w1, [x8, #1464]
  15fb9c:      	adrp	x0, 0x15f000 <wlanDownloadFW+0x1b4>
  15fba0:      	adrp	x2, 0x15f000 <wlanDownloadFW+0x1b8>
  15fba4:      	add	x0, x0, #0
  15fba8:      	add	x2, x2, #0
  15fbac:      	bl	0x15fbac <wlanDownloadFW+0x1c4>
  15fbb0:      	mov	x0, x19
  15fbb4:      	mov	w1, wzr
  15fbb8:      	bl	0x15fbb8 <wlanDownloadFW+0x1d0>
  15fbbc:      	adrp	x9, 0x15f000 <wlanDownloadFW+0x1d4>
  15fbc0:      	ldur	x8, [x29, #-8]
  15fbc4:      	ldr	x9, [x9]
  15fbc8:      	cmp	x9, x8
  15fbcc:      	b.ne	0x15fcfc <wlanDownloadFW+0x314>
  15fbd0:      	mov	w0, w20
  15fbd4:      	ldp	x20, x19, [sp, #80]
  15fbd8:      	ldp	x22, x21, [sp, #64]
  15fbdc:      	ldp	x24, x23, [sp, #48]
  15fbe0:      	ldp	x29, x30, [sp, #32]
  15fbe4:      	add	sp, sp, #96
  15fbe8:      	ret
  15fbec:      	ldr	x8, [x22, #432]
  15fbf0:      	cbz	x8, 0x15fbfc <wlanDownloadFW+0x214>
  15fbf4:      	mov	x0, x19
  15fbf8:      	blr	x8
  15fbfc:      	ldrb	w8, [x21]
  15fc00:      	tbz	w8, #5, 0x15fc20 <wlanDownloadFW+0x238>
  15fc04:      	mrs	x8, SP_EL0
  15fc08:      	ldr	w1, [x8, #1464]
  15fc0c:      	adrp	x0, 0x15f000 <wlanDownloadFW+0x224>
  15fc10:      	adrp	x2, 0x15f000 <wlanDownloadFW+0x228>
  15fc14:      	add	x0, x0, #0
  15fc18:      	add	x2, x2, #0
  15fc1c:      	bl	0x15fc1c <wlanDownloadFW+0x234>
  15fc20:      	ldr	x8, [x22, #400]
  15fc24:      	cbz	x8, 0x15fc54 <wlanDownloadFW+0x26c>
  15fc28:      	blr	x8
  15fc2c:      	cbz	w0, 0x15fc54 <wlanDownloadFW+0x26c>
  15fc30:      	ldrb	w8, [x21]
  15fc34:      	tbz	w8, #1, 0x15fc54 <wlanDownloadFW+0x26c>
  15fc38:      	mrs	x8, SP_EL0
  15fc3c:      	ldr	w1, [x8, #1464]
  15fc40:      	adrp	x0, 0x15f000 <wlanDownloadFW+0x258>
  15fc44:      	adrp	x2, 0x15f000 <wlanDownloadFW+0x25c>
  15fc48:      	add	x0, x0, #0
  15fc4c:      	add	x2, x2, #0
  15fc50:      	bl	0x15fc50 <wlanDownloadFW+0x268>
  15fc54:      	ldr	x8, [x23, #24]
  15fc58:      	cbz	x8, 0x15fc7c <wlanDownloadFW+0x294>
  15fc5c:      	mov	x0, x19
  15fc60:      	mov	w1, wzr
  15fc64:      	blr	x8
  15fc68:      	ldr	w8, [x22, #72]
  15fc6c:      	mov	w20, w0
  15fc70:      	cbz	w8, 0x15fcd8 <wlanDownloadFW+0x2f0>
  15fc74:      	cbnz	w20, 0x15fb8c <wlanDownloadFW+0x1a4>
  15fc78:      	b	0x15fce4 <wlanDownloadFW+0x2fc>
  15fc7c:      	ldrb	w8, [x21]
  15fc80:      	tbz	w8, #1, 0x15fca0 <wlanDownloadFW+0x2b8>
  15fc84:      	mrs	x8, SP_EL0
  15fc88:      	ldr	w1, [x8, #1464]
  15fc8c:      	adrp	x0, 0x15f000 <wlanDownloadFW+0x2a4>
  15fc90:      	adrp	x2, 0x15f000 <wlanDownloadFW+0x2a8>
  15fc94:      	add	x0, x0, #0
  15fc98:      	add	x2, x2, #0
  15fc9c:      	bl	0x15fc9c <wlanDownloadFW+0x2b4>
  15fca0:      	ldr	x8, [x22, #408]
  15fca4:      	cbz	x8, 0x15fcd0 <wlanDownloadFW+0x2e8>
  15fca8:      	blr	x8
  15fcac:      	ldrb	w8, [x21]
  15fcb0:      	tbz	w8, #4, 0x15fcd0 <wlanDownloadFW+0x2e8>
  15fcb4:      	mrs	x8, SP_EL0
  15fcb8:      	ldr	w1, [x8, #1464]
  15fcbc:      	adrp	x0, 0x15f000 <wlanDownloadFW+0x2d4>
  15fcc0:      	adrp	x2, 0x15f000 <wlanDownloadFW+0x2d8>
  15fcc4:      	add	x0, x0, #0
  15fcc8:      	add	x2, x2, #0
  15fccc:      	bl	0x15fccc <wlanDownloadFW+0x2e4>
  15fcd0:      	mov	w20, wzr
  15fcd4:      	b	0x15fb8c <wlanDownloadFW+0x1a4>
  15fcd8:      	cbnz	w20, 0x15fb8c <wlanDownloadFW+0x1a4>
  15fcdc:      	ldr	w8, [x22, #76]
  15fce0:      	cbz	w8, 0x15fb8c <wlanDownloadFW+0x1a4>
  15fce4:      	ldr	x8, [x23, #24]
  15fce8:      	mov	w1, #1
  15fcec:      	mov	x0, x19
  15fcf0:      	blr	x8
  15fcf4:      	mov	w20, w0
  15fcf8:      	b	0x15fb8c <wlanDownloadFW+0x1a4>
  15fcfc:      	bl	0x15fcfc <wlanDownloadFW+0x314>

0000000000162e6c <wlanConnacPccifon>:
  162e6c:      	stp	x29, x30, [sp, #-16]!
  162e70:      	mov	x29, sp
  162e74:      	mov	w0, #13076
  162e78:      	movk	w0, #4096, lsl #16
  162e7c:      	mov	w1, wzr
  162e80:      	bl	0x162e80 <wlanConnacPccifon+0x14>
  162e84:      	mov	w0, #13076
  162e88:      	movk	w0, #4096, lsl #16
  162e8c:      	mov	w1, #1
  162e90:      	bl	0x162e90 <wlanConnacPccifon+0x24>
  162e94:      	mov	w0, wzr
  162e98:      	ldp	x29, x30, [sp], #16
  162e9c:      	ret

0000000000162fa0 <wlanCoAntVFE28En>:
  162fa0:      	stp	x29, x30, [sp, #-32]!
  162fa4:      	stp	x20, x19, [sp, #16]
  162fa8:      	mov	x29, sp
  162fac:      	adrp	x8, 0x162000 <wlanCoAntVFE28En+0xc>
  162fb0:      	ldr	w8, [x8]
  162fb4:      	cmp	w8, #1
  162fb8:      	b.ne	0x163000 <wlanCoAntVFE28En+0x60>
  162fbc:      	mov	w8, #45632
  162fc0:      	movk	w8, #2, lsl #16
  162fc4:      	ldr	x8, [x0, x8]
  162fc8:      	ldr	x8, [x8, #1632]
  162fcc:      	ldrb	w8, [x8, #243]
  162fd0:      	cbz	w8, 0x163020 <wlanCoAntVFE28En+0x80>
  162fd4:      	adrp	x19, 0x162000 <wlanCoAntVFE28En+0x34>
  162fd8:      	ldrb	w8, [x19]
  162fdc:      	cbz	w8, 0x163054 <wlanCoAntVFE28En+0xb4>
  162fe0:      	adrp	x8, 0x162000 <wlanCoAntVFE28En+0x40>
  162fe4:      	ldrb	w8, [x8]
  162fe8:      	tbz	w8, #5, 0x163048 <wlanCoAntVFE28En+0xa8>
  162fec:      	mrs	x8, SP_EL0
  162ff0:      	ldr	w1, [x8, #1464]
  162ff4:      	adrp	x0, 0x162000 <wlanCoAntVFE28En+0x54>
  162ff8:      	add	x0, x0, #0
  162ffc:      	b	0x16303c <wlanCoAntVFE28En+0x9c>
  163000:      	adrp	x8, 0x163000 <wlanCoAntVFE28En+0x60>
  163004:      	ldrb	w8, [x8]
  163008:      	tbz	w8, #5, 0x163048 <wlanCoAntVFE28En+0xa8>
  16300c:      	mrs	x8, SP_EL0
  163010:      	ldr	w1, [x8, #1464]
  163014:      	adrp	x0, 0x163000 <wlanCoAntVFE28En+0x74>
  163018:      	add	x0, x0, #0
  16301c:      	b	0x16303c <wlanCoAntVFE28En+0x9c>
  163020:      	adrp	x8, 0x163000 <wlanCoAntVFE28En+0x80>
  163024:      	ldrb	w8, [x8]
  163028:      	tbz	w8, #5, 0x163048 <wlanCoAntVFE28En+0xa8>
  16302c:      	mrs	x8, SP_EL0
  163030:      	ldr	w1, [x8, #1464]
  163034:      	adrp	x0, 0x163000 <wlanCoAntVFE28En+0x94>
  163038:      	add	x0, x0, #0
  16303c:      	adrp	x2, 0x163000 <wlanCoAntVFE28En+0x9c>
  163040:      	add	x2, x2, #0
  163044:      	bl	0x163044 <wlanCoAntVFE28En+0xa4>
  163048:      	ldp	x20, x19, [sp, #16]
  16304c:      	ldp	x29, x30, [sp], #32
  163050:      	ret
  163054:      	mov	w0, #8
  163058:      	mov	w2, #1
  16305c:      	mov	w1, wzr
  163060:      	mov	w3, wzr
  163064:      	mov	w20, #1
  163068:      	bl	0x163068 <wlanCoAntVFE28En+0xc8>
  16306c:      	adrp	x8, 0x163000 <wlanCoAntVFE28En+0xcc>
  163070:      	ldrb	w8, [x8]
  163074:      	tbz	w8, #5, 0x163094 <wlanCoAntVFE28En+0xf4>
  163078:      	mrs	x8, SP_EL0
  16307c:      	ldr	w1, [x8, #1464]
  163080:      	adrp	x0, 0x163000 <wlanCoAntVFE28En+0xe0>
  163084:      	adrp	x2, 0x163000 <wlanCoAntVFE28En+0xe4>
  163088:      	add	x0, x0, #0
  16308c:      	add	x2, x2, #0
  163090:      	bl	0x163090 <wlanCoAntVFE28En+0xf0>
  163094:      	strb	w20, [x19]
  163098:      	b	0x163048 <wlanCoAntVFE28En+0xa8>

# Only .rela.text relocations inside selected functions
000000000015c0b0  0000188f0000011b R_AARCH64_CALL26       000000000015b170 wlanAccessCalibrationEMI + 0
000000000015c0c8  000018950000011b R_AARCH64_CALL26       000000000015bb60 wlanSendPhyAction + 0
000000000015fa00  0000088900000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 __stack_chk_guard + 0
000000000015fa04  000008890000011e R_AARCH64_LDST64_ABS_LO12_NC 0000000000000000 __stack_chk_guard + 0
000000000015fa14  0000088a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000011a10 aucDebugModule + 0
000000000015fa18  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fa44  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 50739
000000000015fa48  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fa4c  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 50739
000000000015fa50  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fa54  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015fa6c  00000a1d0000011b R_AARCH64_CALL26       00000000000d2288 kalDevRegRead + 0
000000000015fa84  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fa94  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 29c5a
000000000015fa98  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fa9c  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 29c5a
000000000015faa0  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015faa4  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015faac  00000b1e0000011b R_AARCH64_CALL26       000000000001b420 wlanPowerOffWifi + 0
000000000015fac0  00000e540000011b R_AARCH64_CALL26       000000000004db0c nicpmWakeUpWiFi + 0
000000000015facc  00000a110000011b R_AARCH64_CALL26       00000000000cf918 halEnableFWDownload + 0
000000000015faf4  00000a420000011b R_AARCH64_CALL26       0000000000000000 ktime_get_real_ts64 + 0
000000000015fb28  000010cc0000011b R_AARCH64_CALL26       000000000006f6cc kalSyncTimeToFW + 0
000000000015fb34  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fb44  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 492f2
000000000015fb48  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fb4c  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 492f2
000000000015fb50  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fb58  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015fb8c  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fb9c  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 30d9b
000000000015fba0  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fba4  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 30d9b
000000000015fba8  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fbac  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015fbb8  00000a110000011b R_AARCH64_CALL26       00000000000cf918 halEnableFWDownload + 0
000000000015fbbc  0000088900000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 __stack_chk_guard + 0
000000000015fbc4  000008890000011e R_AARCH64_LDST64_ABS_LO12_NC 0000000000000000 __stack_chk_guard + 0
000000000015fbfc  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fc0c  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 399d
000000000015fc10  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fc14  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 399d
000000000015fc18  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fc1c  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015fc30  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fc40  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 50776
000000000015fc44  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fc48  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 50776
000000000015fc4c  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fc50  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015fc7c  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fc8c  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 2d703
000000000015fc90  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fc94  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 2d703
000000000015fc98  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fc9c  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015fcac  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
000000000015fcbc  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 959f
000000000015fcc0  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fcc4  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 959f
000000000015fcc8  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5aa5c
000000000015fccc  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
000000000015fcfc  000008880000011b R_AARCH64_CALL26       0000000000000000 __stack_chk_fail + 0
0000000000162e80  0000111d0000011b R_AARCH64_CALL26       00000000000d3940 wf_ioremap_write + 0
0000000000162e90  0000111d0000011b R_AARCH64_CALL26       00000000000d3940 wf_ioremap_write + 0
0000000000162fac  00000f5700000113 R_AARCH64_ADR_PREL_PG_HI21 00000000000119f0 g_NvramFsm + 0
0000000000162fb0  00000f570000011d R_AARCH64_LDST32_ABS_LO12_NC 00000000000119f0 g_NvramFsm + 0
0000000000162fd4  000018ba00000113 R_AARCH64_ADR_PREL_PG_HI21 000000000002c148 gCoAntVFE28En + 0
0000000000162fd8  000018ba00000116 R_AARCH64_LDST8_ABS_LO12_NC 000000000002c148 gCoAntVFE28En + 0
0000000000162fe0  0000088a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000011a10 aucDebugModule + 0
0000000000162fe4  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
0000000000162ff4  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 1f52f
0000000000162ff8  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 1f52f
0000000000163000  0000088a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000011a10 aucDebugModule + 0
0000000000163004  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
0000000000163014  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 45539
0000000000163018  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 45539
0000000000163020  0000088a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000011a10 aucDebugModule + 0
0000000000163024  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
0000000000163034  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 30df1
0000000000163038  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 30df1
000000000016303c  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + e56a
0000000000163040  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + e56a
0000000000163044  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
0000000000163068  000018b40000011b R_AARCH64_CALL26       0000000000000000 connectivity_export_pmic_ldo_vfe28_lp + 0
000000000016306c  0000088a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000011a10 aucDebugModule + 0
0000000000163070  0000088a00000116 R_AARCH64_LDST8_ABS_LO12_NC 0000000000011a10 aucDebugModule + 0
0000000000163080  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 29cc6
0000000000163084  0000088100000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + e56a
0000000000163088  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 29cc6
000000000016308c  0000088100000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + e56a
0000000000163090  000008920000011b R_AARCH64_CALL26       0000000000068448 kalPrintLog + 0
0000000000163094  000018ba00000116 R_AARCH64_LDST8_ABS_LO12_NC 000000000002c148 gCoAntVFE28En + 0
