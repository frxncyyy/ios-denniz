
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007521fd8 <_njs_rejoin_refresh>:
fffffe0007521fd8:      	sub	sp, sp, #128
fffffe0007521fdc:      	stp	x20, x19, [sp, #96]
fffffe0007521fe0:      	stp	x29, x30, [sp, #112]
fffffe0007521fe4:      	add	x29, sp, #112
fffffe0007521fe8:      	adrp	x8, 0xfffffe0008526000 <_ncd_run+0x280>
fffffe0007521fec:      	ldr	x19, [x8, #2648]
fffffe0007521ff0:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe0007521ff4:      	cmp	x19, x0
fffffe0007521ff8:      	b.ne	0xfffffe0007522168 <_njs_rejoin_refresh+0x190>
fffffe0007521ffc:      	adrp	x8, 0xfffffe00084dd000 <_ncd_run+0x170>
fffffe0007522000:      	add	x8, x8, #2464
fffffe0007522004:      	ldar	w8, [x8]
fffffe0007522008:      	cmp	w8, #1
fffffe000752200c:      	b.ne	0xfffffe0007522168 <_njs_rejoin_refresh+0x190>
fffffe0007522010:      	adrp	x19, 0xfffffe00084dd000 <_ncd_run+0x180>
fffffe0007522014:      	add	x19, x19, #2456
fffffe0007522018:      	ldr	x0, [x19]
fffffe000752201c:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe0007522020:      	adrp	x8, 0xfffffe0008516000 <_ncd_run+0x274>
fffffe0007522024:      	add	x8, x8, #1984
fffffe0007522028:      	add	x9, x8, #52
fffffe000752202c:      	ldur	d0, [x19, #12]
fffffe0007522030:      	adrp	x10, 0xfffffe00084f8000 <_ncd_run+0x20c>
fffffe0007522034:      	mov	x0, sp
fffffe0007522038:      	ldr	x19, [x8]
fffffe000752203c:      	str	xzr, [sp]
fffffe0007522040:      	ld1.s	{ v0 }[1], [x9]
fffffe0007522044:      	adrp	x9, 0xfffffe000700d000 <_nss_owner_loop+0x273c>
fffffe0007522048:      	ldr	d1, [x10, #552]
fffffe000752204c:      	stur	xzr, [x29, #-24]
fffffe0007522050:      	mov.d	v0[1], v1[0]
fffffe0007522054:      	ldr	q1, [x9, #3632]
fffffe0007522058:      	cmeq.4s	v0, v0, v1
fffffe000752205c:      	mvn.16b	v0, v0
fffffe0007522060:      	xtn.4h	v0, v0
fffffe0007522064:      	umaxv.4h	h0, v0
fffffe0007522068:      	fmov	w20, s0
fffffe000752206c:      	bl	0xfffffe00073375cc <_clock_get_uptime>
fffffe0007522070:      	ldr	x0, [sp]
fffffe0007522074:      	sub	x1, x29, #24
fffffe0007522078:      	bl	0xfffffe00074b4854 <_absolutetime_to_nanoseconds>
fffffe000752207c:      	tbz	w20, #0, 0xfffffe000752208c <_njs_rejoin_refresh+0xb4>
fffffe0007522080:      	adrp	x8, 0xfffffe00084dd000 <_ncd_run+0x1f0>
fffffe0007522084:      	ldr	x0, [x8, #2456]
fffffe0007522088:      	b	0xfffffe0007522164 <_njs_rejoin_refresh+0x18c>
fffffe000752208c:      	ldur	x8, [x29, #-24]
fffffe0007522090:      	mov	x10, #63439
fffffe0007522094:      	movk	x10, #58195, lsl #16
fffffe0007522098:      	adrp	x9, 0xfffffe0008516000 <_ncd_run+0x2ec>
fffffe000752209c:      	movk	x10, #39845, lsl #32
fffffe00075220a0:      	lsr	x8, x8, #3
fffffe00075220a4:      	movk	x10, #8388, lsl #48
fffffe00075220a8:      	ldr	x9, [x9, #1992]
fffffe00075220ac:      	umulh	x8, x8, x10
fffffe00075220b0:      	lsr	x8, x8, #4
fffffe00075220b4:      	subs	x8, x8, x9
fffffe00075220b8:      	b.lo	0xfffffe000752213c <_njs_rejoin_refresh+0x164>
fffffe00075220bc:      	mov	w9, #50048
fffffe00075220c0:      	movk	w9, #457, lsl #16
fffffe00075220c4:      	cmp	x8, x9
fffffe00075220c8:      	b.hs	0xfffffe000752213c <_njs_rejoin_refresh+0x164>
fffffe00075220cc:      	adrp	x20, 0xfffffe00084dd000 <_ncd_run+0x23c>
fffffe00075220d0:      	ldr	x0, [x20, #2456]
fffffe00075220d4:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe00075220d8:      	movi.2d	v0, #0000000000000000
fffffe00075220dc:      	adrp	x1, 0xfffffe0008516000 <_ncd_run+0x330>
fffffe00075220e0:      	add	x1, x1, #1984
fffffe00075220e4:      	adr	x3, #2204
fffffe00075220e8:      	nop
fffffe00075220ec:      	mov	x0, sp
fffffe00075220f0:      	sub	x4, x29, #24
fffffe00075220f4:      	mov	x2, x19
fffffe00075220f8:      	str	wzr, [sp, #80]
fffffe00075220fc:      	stp	q0, q0, [sp, #16]
fffffe0007522100:      	stp	q0, q0, [sp, #48]
fffffe0007522104:      	str	q0, [sp]
fffffe0007522108:      	stur	xzr, [x29, #-24]
fffffe000752210c:      	bl	0xfffffe0007511b7c <_nord2_wifi_scan_enqueue>
fffffe0007522110:      	mov	w19, w0
fffffe0007522114:      	ldr	x0, [x20, #2456]
fffffe0007522118:      	bl	0xfffffe0007351cc4 <_lck_mtx_lock>
fffffe000752211c:      	cmp	w19, #2
fffffe0007522120:      	b.eq	0xfffffe0007522160 <_njs_rejoin_refresh+0x188>
fffffe0007522124:      	cbnz	w19, 0xfffffe0007522154 <_njs_rejoin_refresh+0x17c>
fffffe0007522128:      	ldur	x9, [x29, #-24]
fffffe000752212c:      	adrp	x10, 0xfffffe0008516000 <_ncd_run+0x380>
fffffe0007522130:      	mov	w8, #2
fffffe0007522134:      	str	x9, [x10, #2000]
fffffe0007522138:      	b	0xfffffe0007522158 <_njs_rejoin_refresh+0x180>
fffffe000752213c:      	adrp	x8, 0xfffffe00084dd000 <_ncd_run+0x2ac>
fffffe0007522140:      	adrp	x9, 0xfffffe0008516000 <_ncd_run+0x394>
fffffe0007522144:      	mov	w10, #3
fffffe0007522148:      	ldr	x0, [x8, #2456]
fffffe000752214c:      	str	w10, [x9, #2036]
fffffe0007522150:      	b	0xfffffe0007522164 <_njs_rejoin_refresh+0x18c>
fffffe0007522154:      	mov	w8, #3
fffffe0007522158:      	adrp	x9, 0xfffffe0008516000 <_ncd_run+0x3ac>
fffffe000752215c:      	str	w8, [x9, #2036]
fffffe0007522160:      	ldr	x0, [x20, #2456]
fffffe0007522164:      	bl	0xfffffe0007351fb4 <_lck_mtx_unlock>
fffffe0007522168:      	ldp	x29, x30, [sp, #112]
fffffe000752216c:      	ldp	x20, x19, [sp, #96]
fffffe0007522170:      	add	sp, sp, #128
fffffe0007522174:      	ret
