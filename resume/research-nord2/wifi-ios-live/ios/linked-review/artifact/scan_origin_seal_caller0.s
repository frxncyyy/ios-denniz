
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007515f80 <_nct_native_owner_window>:
fffffe0007515f80:      	sub	sp, sp, #48
fffffe0007515f84:      	stp	x20, x19, [sp, #16]
fffffe0007515f88:      	stp	x29, x30, [sp, #32]
fffffe0007515f8c:      	add	x29, sp, #32
fffffe0007515f90:      	mov	x8, x0
fffffe0007515f94:      	mov	w0, wzr
fffffe0007515f98:      	adrp	x9, 0xfffffe0007c5f000 <_ni_bus_read+0x1c8>
fffffe0007515f9c:      	add	x9, x9, #1624
fffffe0007515fa0:      	cmp	x8, x9
fffffe0007515fa4:      	b.ne	0xfffffe00075161c0 <_nct_native_owner_window+0x240>
fffffe0007515fa8:      	adrp	x8, 0xfffffe0008528000 <_ncc_ap_check+0x1c0>
fffffe0007515fac:      	adrp	x9, 0xfffffe0008526000 <_ncc_ap_check+0x1bc>
fffffe0007515fb0:      	add	x9, x9, #2660
fffffe0007515fb4:      	ldr	d0, [x8, #3224]
fffffe0007515fb8:      	adrp	x8, 0xfffffe000700d000 <_ni_thread+0x10e4>
fffffe0007515fbc:      	ldr	d1, [x9]
fffffe0007515fc0:      	mov.d	v0[1], v1[0]
fffffe0007515fc4:      	ldr	q1, [x8, #3424]
fffffe0007515fc8:      	cmeq.4s	v0, v0, v1
fffffe0007515fcc:      	mvn.16b	v0, v0
fffffe0007515fd0:      	xtn.4h	v0, v0
fffffe0007515fd4:      	umov.h	w8, v0[1]
fffffe0007515fd8:      	umov.h	w10, v0[2]
fffffe0007515fdc:      	umov.h	w11, v0[0]
fffffe0007515fe0:      	umov.h	w12, v0[3]
fffffe0007515fe4:      	and	w8, w8, #0x1
fffffe0007515fe8:      	and	w10, w10, #0x1
fffffe0007515fec:      	bfi	w11, w8, #1, #1
fffffe0007515ff0:      	ldr	w8, [x9, #8]
fffffe0007515ff4:      	bfi	w11, w10, #2, #1
fffffe0007515ff8:      	bfi	w11, w12, #3, #29
fffffe0007515ffc:      	and	w9, w11, #0xf
fffffe0007516000:      	cmp	w9, #0
fffffe0007516004:      	ccmp	w8, #0, #0, eq
fffffe0007516008:      	b.ne	0xfffffe00075161c0 <_nct_native_owner_window+0x240>
fffffe000751600c:      	adrp	x8, 0xfffffe0008526000 <_ncc_ap_check+0x218>
fffffe0007516010:      	ldr	x19, [x8, #2648]
fffffe0007516014:      	bl	0xfffffe00074ae6dc <_current_thread>
fffffe0007516018:      	cmp	x19, x0
fffffe000751601c:      	b.eq	0xfffffe0007516028 <_nct_native_owner_window+0xa8>
fffffe0007516020:      	mov	w0, wzr
fffffe0007516024:      	b	0xfffffe00075161c0 <_nct_native_owner_window+0x240>
fffffe0007516028:      	adrp	x8, 0xfffffe0008528000 <_ncc_ap_check+0x23c>
fffffe000751602c:      	ldr	w9, [x8, #3700]
fffffe0007516030:      	cbz	w9, 0xfffffe0007516110 <_nct_native_owner_window+0x190>
fffffe0007516034:      	adrp	x10, 0xfffffe0008527000 <_ncc_ap_check+0x244>
fffffe0007516038:      	mov	w8, wzr
fffffe000751603c:      	ldr	x11, [x10, #3176]
fffffe0007516040:      	adrp	x10, 0xfffffe0008528000 <_ncc_ap_check+0x254>
fffffe0007516044:      	add	x10, x10, #3232
fffffe0007516048:      	cmp	x11, x10
fffffe000751604c:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe0007516050:      	adrp	x11, 0xfffffe0008524000 <_ncc_ap_check+0x254>
fffffe0007516054:      	add	x11, x11, #428
fffffe0007516058:      	ldr	d0, [x11]
fffffe000751605c:      	adrp	x11, 0xfffffe00084f8000 <_ncc_ap_check+0x1b0>
fffffe0007516060:      	add	x11, x11, #556
fffffe0007516064:      	ld1.s	{ v0 }[1], [x11]
fffffe0007516068:      	ldr	d1, [x11, #40]
fffffe000751606c:      	mov.d	v0[1], v1[0]
fffffe0007516070:      	cmtst.4s	v0, v0, v0
fffffe0007516074:      	xtn.4h	v0, v0
fffffe0007516078:      	umov.h	w11, v0[1]
fffffe000751607c:      	umov.h	w12, v0[2]
fffffe0007516080:      	umov.h	w13, v0[0]
fffffe0007516084:      	umov.h	w14, v0[3]
fffffe0007516088:      	and	w11, w11, #0x1
fffffe000751608c:      	and	w12, w12, #0x1
fffffe0007516090:      	bfi	w13, w11, #1, #1
fffffe0007516094:      	bfi	w13, w12, #2, #1
fffffe0007516098:      	bfi	w13, w14, #3, #29
fffffe000751609c:      	and	w11, w13, #0xf
fffffe00075160a0:      	cbnz	w11, 0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075160a4:      	ldr	w11, [x10, #464]
fffffe00075160a8:      	cmp	w11, #1
fffffe00075160ac:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075160b0:      	cmp	w9, #1
fffffe00075160b4:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075160b8:      	ldr	w9, [x10, #32]
fffffe00075160bc:      	cmp	w9, #1
fffffe00075160c0:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075160c4:      	adrp	x8, 0xfffffe00084f8000 <_ncc_ap_check+0x218>
fffffe00075160c8:      	ldr	w8, [x8, #552]
fffffe00075160cc:      	cbz	w8, 0xfffffe00075161d0 <_nct_native_owner_window+0x250>
fffffe00075160d0:      	adrp	x8, 0xfffffe00084f8000 <_ncc_ap_check+0x224>
fffffe00075160d4:      	adrp	x9, 0xfffffe0008516000 <_npa_xnu_proof+0x14>
fffffe00075160d8:      	add	x9, x9, #1984
fffffe00075160dc:      	ldr	x8, [x8, #752]
fffffe00075160e0:      	ldr	x10, [x9, #32]
fffffe00075160e4:      	cmp	x10, x8
fffffe00075160e8:      	b.ne	0xfffffe00075161d0 <_nct_native_owner_window+0x250>
fffffe00075160ec:      	ldr	x9, [x9]
fffffe00075160f0:      	cmp	x8, x9
fffffe00075160f4:      	b.ls	0xfffffe00075161d0 <_nct_native_owner_window+0x250>
fffffe00075160f8:      	adrp	x8, 0xfffffe000850b000 <_npa_xnu_proof+0xc>
fffffe00075160fc:      	ldr	w8, [x8, #788]
fffffe0007516100:      	cmp	w8, #21
fffffe0007516104:      	b.eq	0xfffffe0007516210 <_nct_native_owner_window+0x290>
fffffe0007516108:      	mov	w8, wzr
fffffe000751610c:      	b	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe0007516110:      	adrp	x8, 0xfffffe0008524000 <_npa_xnu_write+0x8>
fffffe0007516114:      	add	x8, x8, #424
fffffe0007516118:      	adrp	x10, 0xfffffe0008527000 <_npa_xnu_write+0x1c>
fffffe000751611c:      	add	x10, x10, #3160
fffffe0007516120:      	ldp	w9, w8, [x8]
fffffe0007516124:      	ldr	w11, [x10]
fffffe0007516128:      	orr	w8, w9, w8
fffffe000751612c:      	cmp	w8, #0
fffffe0007516130:      	ldr	x8, [x10, #16]
fffffe0007516134:      	ccmp	w11, #2, #4, eq
fffffe0007516138:      	ccmp	x8, #0, #0, ne
fffffe000751613c:      	cset	w8, eq
fffffe0007516140:      	mov	w0, wzr
fffffe0007516144:      	cbz	w8, 0xfffffe00075161c0 <_nct_native_owner_window+0x240>
fffffe0007516148:      	adrp	x8, 0xfffffe0008527000 <_npa_xnu_write+0x4c>
fffffe000751614c:      	add	x8, x8, #3184
fffffe0007516150:      	adrp	x10, 0xfffffe0008526000 <_npa_xnu_write+0x50>
fffffe0007516154:      	add	x10, x10, #2680
fffffe0007516158:      	ldr	x9, [x8]
fffffe000751615c:      	cmp	x9, x10
fffffe0007516160:      	b.ne	0xfffffe00075161c0 <_nct_native_owner_window+0x240>
fffffe0007516164:      	ldr	x9, [x8, #16]
fffffe0007516168:      	adrp	x10, 0xfffffe0008525000 <_npa_xnu_write+0x64>
fffffe000751616c:      	add	x10, x10, #1592
fffffe0007516170:      	cmp	x9, x10
fffffe0007516174:      	b.ne	0xfffffe00075161c0 <_nct_native_owner_window+0x240>
fffffe0007516178:      	ldr	x8, [x8, #8]
fffffe000751617c:      	adrp	x9, 0xfffffe000721b000 <_ni_thread+0x1adc>
fffffe0007516180:      	add	x9, x9, #1232
fffffe0007516184:      	cmp	x8, x9
fffffe0007516188:      	b.ne	0xfffffe00075161c0 <_nct_native_owner_window+0x240>
fffffe000751618c:      	adrp	x4, 0xfffffe0007c9c000 <_npa_pmic_source_enable+0x138>
fffffe0007516190:      	adrp	x0, 0xfffffe0008526000 <_npa_base>
fffffe0007516194:      	add	x0, x0, #2680
fffffe0007516198:      	adrp	x1, 0xfffffe0008524000 <_npa_base>
fffffe000751619c:      	add	x1, x1, #1216
fffffe00075161a0:      	adrp	x2, 0xfffffe0008525000 <_npa_base+0xc>
fffffe00075161a4:      	add	x2, x2, #1592
fffffe00075161a8:      	adrp	x3, 0xfffffe0007c5f000 <_npa_pmic_source_enable+0x60>
fffffe00075161ac:      	add	x3, x3, #1624
fffffe00075161b0:      	add	x4, x4, #648
fffffe00075161b4:      	bl	0xfffffe000799cdc8 <_nct_lease_window>
fffffe00075161b8:      	cmp	w0, #0
fffffe00075161bc:      	cset	w0, ne
fffffe00075161c0:      	ldp	x29, x30, [sp, #32]
fffffe00075161c4:      	ldp	x20, x19, [sp, #16]
fffffe00075161c8:      	add	sp, sp, #48
fffffe00075161cc:      	ret
fffffe00075161d0:      	adrp	x9, 0xfffffe00084f8000 <_npa_xnu_write+0x18>
fffffe00075161d4:      	add	x9, x9, #584
fffffe00075161d8:      	mov	w8, wzr
fffffe00075161dc:      	ldr	w10, [x9, #108]
fffffe00075161e0:      	cbz	w10, 0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075161e4:      	ldr	w10, [x9]
fffffe00075161e8:      	cbnz	w10, 0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075161ec:      	ldr	x10, [x9, #304]
fffffe00075161f0:      	cbnz	x10, 0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075161f4:      	ldr	x10, [x9, #312]
fffffe00075161f8:      	cbnz	x10, 0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe00075161fc:      	mov	w10, #12492
fffffe0007516200:      	movk	w10, #1, lsl #16
fffffe0007516204:      	ldr	w9, [x9, x10]
fffffe0007516208:      	cmp	w9, #21
fffffe000751620c:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe0007516210:      	adrp	x9, 0xfffffe00084f8000 <_npa_xnu_write+0x58>
fffffe0007516214:      	add	x9, x9, #572
fffffe0007516218:      	mov	w8, wzr
fffffe000751621c:      	ldr	w10, [x9, #20]
fffffe0007516220:      	cmp	w10, #1
fffffe0007516224:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe0007516228:      	ldr	w10, [x9]
fffffe000751622c:      	cmp	w10, #1
fffffe0007516230:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe0007516234:      	mov	w10, #22232
fffffe0007516238:      	movk	w10, #1, lsl #16
fffffe000751623c:      	ldr	w9, [x9, x10]
fffffe0007516240:      	cmp	w9, #1
fffffe0007516244:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe0007516248:      	adrp	x9, 0xfffffe0008524000 <_npa_base+0xb0>
fffffe000751624c:      	ldr	w9, [x9, #424]
fffffe0007516250:      	cmp	w9, #1
fffffe0007516254:      	b.ne	0xfffffe0007516140 <_nct_native_owner_window+0x1c0>
fffffe0007516258:      	adrp	x8, 0xfffffe0008528000 <_npa_base+0xd0>
fffffe000751625c:      	adrp	x9, 0xfffffe0008516000 <_npa_base+0x8c>
fffffe0007516260:      	ldr	x8, [x8, #3288]
fffffe0007516264:      	ldr	x9, [x9, #1984]
fffffe0007516268:      	cmp	x8, x9
fffffe000751626c:      	b	0xfffffe000751613c <_nct_native_owner_window+0x1bc>
