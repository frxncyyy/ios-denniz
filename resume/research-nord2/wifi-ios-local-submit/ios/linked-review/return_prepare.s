
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe000799704c <_nct_scan_return_prepare>:
fffffe000799704c:      	stp	x22, x21, [sp, #-48]!
fffffe0007997050:      	stp	x20, x19, [sp, #16]
fffffe0007997054:      	stp	x29, x30, [sp, #32]
fffffe0007997058:      	add	x29, sp, #32
fffffe000799705c:      	cbz	x3, 0xfffffe000799706c <_nct_scan_return_prepare+0x20>
fffffe0007997060:      	mov	x19, x3
fffffe0007997064:      	ldr	w8, [x3, #32]
fffffe0007997068:      	cbz	w8, 0xfffffe0007997074 <_nct_scan_return_prepare+0x28>
fffffe000799706c:      	mov	w0, #-1
fffffe0007997070:      	b	0xfffffe0007997104 <_nct_scan_return_prepare+0xb8>
fffffe0007997074:      	mov	w8, #1
fffffe0007997078:      	mov	x20, x2
fffffe000799707c:      	mov	x21, x1
fffffe0007997080:      	mov	x22, x0
fffffe0007997084:      	str	x0, [x19]
fffffe0007997088:      	str	w8, [x19, #32]
fffffe000799708c:      	bl	0xfffffe00079968d4 <_basic_receipt>
fffffe0007997090:      	cbz	x21, 0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe0007997094:      	cbnz	w0, 0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe0007997098:      	ldr	w8, [x21, #32]
fffffe000799709c:      	cmp	w8, #1
fffffe00079970a0:      	b.ne	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe00079970a4:      	ldr	x8, [x21, #56]
fffffe00079970a8:      	cbz	x8, 0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe00079970ac:      	ldr	x8, [x21]
fffffe00079970b0:      	cmp	x8, x22
fffffe00079970b4:      	b.ne	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe00079970b8:      	ldr	x8, [x21, #8]
fffffe00079970bc:      	ldr	x9, [x22]
fffffe00079970c0:      	cmp	x8, x9
fffffe00079970c4:      	b.ne	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe00079970c8:      	ldr	x9, [x21, #16]
fffffe00079970cc:      	ldr	x10, [x22, #8]
fffffe00079970d0:      	cmp	x9, x10
fffffe00079970d4:      	b.ne	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe00079970d8:      	ldr	x9, [x21, #24]
fffffe00079970dc:      	ldr	x10, [x22, #16]
fffffe00079970e0:      	cmp	x9, x10
fffffe00079970e4:      	b.ne	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe00079970e8:      	ldr	w9, [x21, #36]
fffffe00079970ec:      	add	w9, w9, #1
fffffe00079970f0:      	cmp	w9, #6
fffffe00079970f4:      	b.hs	0xfffffe0007997114 <_nct_scan_return_prepare+0xc8>
fffffe00079970f8:      	mov	w8, #113
fffffe00079970fc:      	mov	w0, #1
fffffe0007997100:      	str	w8, [x19, #56]
fffffe0007997104:      	ldp	x29, x30, [sp, #32]
fffffe0007997108:      	ldp	x20, x19, [sp, #16]
fffffe000799710c:      	ldp	x22, x21, [sp], #48
fffffe0007997110:      	ret
fffffe0007997114:      	ldr	w9, [x21, #40]
fffffe0007997118:      	cmp	w9, #1023
fffffe000799711c:      	b.hi	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe0007997120:      	ldr	w9, [x21, #44]
fffffe0007997124:      	ldr	w10, [x22, #432]
fffffe0007997128:      	cmp	w9, w10
fffffe000799712c:      	b.ne	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe0007997130:      	ldr	w10, [x21, #48]
fffffe0007997134:      	sub	w9, w9, #1
fffffe0007997138:      	cmp	w10, w9
fffffe000799713c:      	b.ne	0xfffffe00079970f8 <_nct_scan_return_prepare+0xac>
fffffe0007997140:      	str	x8, [x19, #8]
fffffe0007997144:      	ldr	x8, [x22, #8]
fffffe0007997148:      	str	x8, [x19, #16]
fffffe000799714c:      	ldr	x9, [x22, #16]
fffffe0007997150:      	str	x9, [x19, #24]
fffffe0007997154:      	ldr	w9, [x8, #1920]
fffffe0007997158:      	ldr	w10, [x21, #36]
fffffe000799715c:      	cmp	w9, w10
fffffe0007997160:      	b.ne	0xfffffe00079971a8 <_nct_scan_return_prepare+0x15c>
fffffe0007997164:      	ldr	w9, [x8, #2864]
fffffe0007997168:      	ldr	w10, [x21, #40]
fffffe000799716c:      	cmp	w9, w10
fffffe0007997170:      	b.ne	0xfffffe00079971a8 <_nct_scan_return_prepare+0x15c>
fffffe0007997174:      	ldrb	w9, [x8, #2876]
fffffe0007997178:      	ldrb	w10, [x21, #52]
fffffe000799717c:      	cmp	w9, w10
fffffe0007997180:      	b.ne	0xfffffe00079971a8 <_nct_scan_return_prepare+0x15c>
fffffe0007997184:      	ldr	w9, [x8, #1908]
fffffe0007997188:      	cbnz	w9, 0xfffffe00079971a8 <_nct_scan_return_prepare+0x15c>
fffffe000799718c:      	ldr	w9, [x8, #1912]
fffffe0007997190:      	cbnz	w9, 0xfffffe00079971a8 <_nct_scan_return_prepare+0x15c>
fffffe0007997194:      	ldr	w9, [x8, #1916]
fffffe0007997198:      	cbnz	w9, 0xfffffe00079971a8 <_nct_scan_return_prepare+0x15c>
fffffe000799719c:      	ldr	w8, [x8, #2868]
fffffe00079971a0:      	cmp	w8, #16
fffffe00079971a4:      	b.lo	0xfffffe00079971b0 <_nct_scan_return_prepare+0x164>
fffffe00079971a8:      	mov	w8, #114
fffffe00079971ac:      	b	0xfffffe00079970fc <_nct_scan_return_prepare+0xb0>
fffffe00079971b0:      	cbz	x20, 0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe00079971b4:      	ldr	x8, [x20]
fffffe00079971b8:      	cbz	x8, 0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe00079971bc:      	ldr	x9, [x20, #16]
fffffe00079971c0:      	cbz	x9, 0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe00079971c4:      	ldr	w9, [x20, #8]
fffffe00079971c8:      	cmp	w9, #1
fffffe00079971cc:      	b.ne	0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe00079971d0:      	ldrb	w9, [x8, #2]
fffffe00079971d4:      	cmp	w9, #3
fffffe00079971d8:      	b.ne	0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe00079971dc:      	ldrh	w9, [x8]
fffffe00079971e0:      	cmp	w9, #1252
fffffe00079971e4:      	b.ne	0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe00079971e8:      	ldrb	w8, [x8, #3]
fffffe00079971ec:      	cmp	w8, #1
fffffe00079971f0:      	b.ne	0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe00079971f4:      	ldr	x8, [x20, #32]
fffffe00079971f8:      	mov	x9, #-34561
fffffe00079971fc:      	movk	x9, #64620, lsl #16
fffffe0007997200:      	add	x10, x8, x9
fffffe0007997204:      	add	x9, x9, #1
fffffe0007997208:      	cmp	x10, x9
fffffe000799720c:      	b.lo	0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe0007997210:      	ldr	x9, [x20, #40]
fffffe0007997214:      	mov	w11, #57791
fffffe0007997218:      	movk	w11, #228, lsl #16
fffffe000799721c:      	sub	x10, x9, #1
fffffe0007997220:      	cmp	x10, x11
fffffe0007997224:      	b.hi	0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe0007997228:      	cmp	x9, x8
fffffe000799722c:      	b.hs	0xfffffe0007997258 <_nct_scan_return_prepare+0x20c>
fffffe0007997230:      	mov	w8, #2
fffffe0007997234:      	str	x21, [x19, #4592]
fffffe0007997238:      	mov	x0, x19
fffffe000799723c:      	mov	x1, x20
fffffe0007997240:      	ldp	x29, x30, [sp, #32]
fffffe0007997244:      	str	w8, [x19, #4576]
fffffe0007997248:      	ldp	x20, x19, [sp, #16]
fffffe000799724c:      	ldr	w2, [x21, #44]
fffffe0007997250:      	ldp	x22, x21, [sp], #48
fffffe0007997254:      	b	0xfffffe0007996ae8 <_seal>
fffffe0007997258:      	mov	w8, #115
fffffe000799725c:      	b	0xfffffe00079970fc <_nct_scan_return_prepare+0xb0>
