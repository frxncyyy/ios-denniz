100008f80:     	sub	sp, sp, #0x1c0
100008f84:     	stp	d15, d14, [sp, #0x120]
100008f88:     	stp	d13, d12, [sp, #0x130]
100008f8c:     	stp	d11, d10, [sp, #0x140]
100008f90:     	stp	d9, d8, [sp, #0x150]
100008f94:     	stp	x28, x27, [sp, #0x160]
100008f98:     	stp	x26, x25, [sp, #0x170]
100008f9c:     	stp	x24, x23, [sp, #0x180]
100008fa0:     	stp	x22, x21, [sp, #0x190]
100008fa4:     	stp	x20, x19, [sp, #0x1a0]
100008fa8:     	stp	x29, x30, [sp, #0x1b0]
100008fac:     	add	x29, sp, #0x1b0
100008fb0:     	mov	x19, x5
100008fb4:     	mov	x21, x4
100008fb8:     	mov	x20, x3
100008fbc:     	mov	x22, x0
100008fc0:     	nop
100008fc4:     	ldr	x8, 0x1000fc508 
100008fc8:     	ldr	x8, [x8]
100008fcc:     	stur	x8, [x29, #-0xa0]
100008fd0:     	bl	0x1000abe70 
100008fd4:     	mov	x24, x0
100008fd8:     	bl	0x1000abe64 
100008fdc:     	mov	x23, x0
100008fe0:     	movi.16b	v0, #0x0
100008fe4:     	stp	q0, q0, [sp, #0x30]
100008fe8:     	stp	q0, q0, [sp, #0x50]
100008fec:     	add	x2, sp, #0x30
100008ff0:     	add	x3, sp, #0x90
100008ff4:     	mov	x0, x24
100008ff8:     	mov	w4, #0x10               ; =16
100008ffc:     	bl	0x1000af200    ; sel countByEnumeratingWithState:objects:count:
100009000:     	str	x0, [sp, #0x28]
100009004:     	cbz	x0, 0x10000940c <__text+0x33bc>
100009008:     	ldr	x8, [sp, #0x40]
10000900c:     	ldr	x27, [x8]
100009010:     	scvtf	d10, w21
100009014:     	fmov	d8, #-1.00000000
100009018:     	fmov	d11, xzr
10000901c:     	mov	w25, #0x800             ; =2048
100009020:     	fmov	d12, #24.00000000
100009024:     	fmov	d13, #1.00000000
100009028:     	nop
10000902c:     	ldr	d14, 0x1000c6378 
100009030:     	fmov	d9, xzr
100009034:     	str	w21, [sp, #0x24]
100009038:     	stp	x24, x20, [sp, #0x10]
10000903c:     	str	x27, [sp, #0x8]
100009040:     	mov	x28, #0x0               ; =0
100009044:     	ldr	x8, [sp, #0x40]
100009048:     	ldr	x8, [x8]
10000904c:     	cmp	x8, x27
100009050:     	b.eq	0x10000905c <__text+0x300c>
100009054:     	mov	x0, x24
100009058:     	bl	0x1000abcfc 
10000905c:     	ldr	x8, [sp, #0x38]
100009060:     	ldr	x26, [x8, x28, lsl #3]
100009064:     	mov	x0, x26
100009068:     	bl	0x1000ad220    ; sel allocationType
10000906c:     	cmp	w0, #0x4
100009070:     	b.hi	0x100009320 <__text+0x32d0>
100009074:     	mov	w8, w0
100009078:     	adr	x11, 0x1000c63a8    ; ""
10000907c:     	nop
100009080:     	adr	x9, 0x100009090    ; "��c���"
100009084:     	ldrb	w10, [x11, x8]
100009088:     	add	x9, x9, x10, lsl #2
10000908c:     	br	x9
100009090:     	mov	x0, x26
100009094:     	bl	0x1000ae620    ; sel capacity
100009098:     	cbz	x20, 0x10000928c <__text+0x323c>
10000909c:     	fmul	d0, d0, d10
1000090a0:     	adrp	x8, 0x100137000 
1000090a4:     	ldrb	w8, [x8, #0xc78]
1000090a8:     	cmp	w8, #0x0
1000090ac:     	mov	x8, #0x4058000000000000 ; =4636455816377925632
1000090b0:     	fmov	d1, x8
1000090b4:     	fcsel	d1, d1, d11, ne
1000090b8:     	fdiv	d9, d0, d1
1000090bc:     	b	0x100009324 <__text+0x32d4>
1000090c0:     	cbz	x20, 0x100009284 <__text+0x3234>
1000090c4:     	mov	x0, x26
1000090c8:     	bl	0x1000bbc40    ; sel widgetBudgetID
1000090cc:     	bl	0x1000abccc 
1000090d0:     	mov	x19, x0
1000090d4:     	cmp	w21, #0x1
1000090d8:     	b.lt	0x1000092a4 <__text+0x3254>
1000090dc:     	mov	w20, #0x0               ; =0
1000090e0:     	fmov	d15, xzr
1000090e4:     	mov	x24, x21
1000090e8:     	scvtf	d0, w20
1000090ec:     	mov	x0, x23
1000090f0:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
1000090f4:     	bl	0x1000abccc 
1000090f8:     	mov	x27, x0
1000090fc:     	mov	x0, x22
100009100:     	mov	x2, x27
100009104:     	mov	x3, x19
100009108:     	bl	0x1000bb1a0    ; sel unlocked_budgetAllocationProportionAtDate:forWidgetBudgetID:
10000910c:     	fmov	d8, d0
100009110:     	fadd	d15, d15, d0
100009114:     	ldr	x21, [x22, #0x60]
100009118:     	mov	x0, x21
10000911c:     	mov	w1, #0x1                ; =1
100009120:     	bl	0x1000abf9c 
100009124:     	cbz	w0, 0x10000916c <__text+0x311c>
100009128:     	mov	w8, #0x302              ; =770
10000912c:     	movk	w8, #0x840, lsl #16
100009130:     	str	w8, [sp, #0x70]
100009134:     	stur	x27, [sp, #0x74]
100009138:     	strh	w25, [sp, #0x7c]
10000913c:     	stur	d8, [sp, #0x7e]
100009140:     	strh	w25, [sp, #0x86]
100009144:     	str	d15, [sp, #0x88]
100009148:     	add	x4, sp, #0x70
10000914c:     	adr	x0, 0x100000000    ; "����"
100009150:     	nop
100009154:     	mov	x1, x21
100009158:     	mov	w2, #0x1                ; =1
10000915c:     	adr	x3, 0x1000ec7dc    ; "Relative usage at slot date %@ is %5.5f, total proportion until now is"
100009160:     	nop
100009164:     	mov	w5, #0x20               ; =32
100009168:     	bl	0x1000ab978 
10000916c:     	bl	0x1000abdec 
100009170:     	sub	w20, w20, #0x384
100009174:     	sub	w24, w24, #0x1
100009178:     	cbnz	w24, 0x1000090e8 <__text+0x3098>
10000917c:     	b	0x1000092a8 <__text+0x3258>
100009180:     	mov	x0, x26
100009184:     	bl	0x1000ae620    ; sel capacity
100009188:     	fmul	d0, d0, d10
10000918c:     	adrp	x8, 0x100137000 
100009190:     	ldrb	w8, [x8, #0xc78]
100009194:     	cmp	w8, #0x0
100009198:     	mov	x8, #0x4058000000000000 ; =4636455816377925632
10000919c:     	fmov	d1, x8
1000091a0:     	fcsel	d1, d1, d11, ne
1000091a4:     	fdiv	d9, d0, d1
1000091a8:     	b	0x100009320 <__text+0x32d0>
1000091ac:     	fcmp	d8, #0.0
1000091b0:     	b.pl	0x1000091dc <__text+0x318c>
1000091b4:     	mov	x0, x22
1000091b8:     	bl	0x1000bb8c0    ; sel usageTimeline
1000091bc:     	bl	0x1000abccc 
1000091c0:     	mov	x19, x0
1000091c4:     	mov	x0, x22
1000091c8:     	mov	x2, x23
1000091cc:     	mov	x3, x19
1000091d0:     	bl	0x1000b6ca0    ; sel relativeUsageAtDate:withTimeline:
1000091d4:     	fmov	d8, d0
1000091d8:     	bl	0x1000abd8c 
1000091dc:     	ldr	x19, [x22, #0x60]
1000091e0:     	mov	x0, x19
1000091e4:     	mov	w1, #0x1                ; =1
1000091e8:     	bl	0x1000abf9c 
1000091ec:     	cbz	w0, 0x100009230 <__text+0x31e0>
1000091f0:     	mov	x8, #0x4059000000000000 ; =4636737291354636288
1000091f4:     	fmov	d0, x8
1000091f8:     	fmul	d0, d8, d0
1000091fc:     	mov	w8, #0x100              ; =256
100009200:     	movk	w8, #0x800, lsl #16
100009204:     	str	w8, [sp, #0x70]
100009208:     	stur	d0, [sp, #0x74]
10000920c:     	add	x4, sp, #0x70
100009210:     	adr	x0, 0x100000000    ; "����"
100009214:     	nop
100009218:     	mov	x1, x19
10000921c:     	mov	w2, #0x1                ; =1
100009220:     	adr	x3, 0x1000ec7b0    ; "Allocating %2.2f%% of budget based on usage"
100009224:     	nop
100009228:     	mov	w5, #0xc                ; =12
10000922c:     	bl	0x1000ab978 
100009230:     	mov	x0, x26
100009234:     	bl	0x1000ae620    ; sel capacity
100009238:     	fmul	d9, d8, d0
10000923c:     	b	0x100009320 <__text+0x32d0>
100009240:     	fcmp	d8, #0.0
100009244:     	b.pl	0x100009270 <__text+0x3220>
100009248:     	mov	x0, x22
10000924c:     	bl	0x1000bb8c0    ; sel usageTimeline
100009250:     	bl	0x1000abccc 
100009254:     	mov	x19, x0
100009258:     	mov	x0, x22
10000925c:     	mov	x2, x23
100009260:     	mov	x3, x19
100009264:     	bl	0x1000b6ca0    ; sel relativeUsageAtDate:withTimeline:
100009268:     	fmov	d8, d0
10000926c:     	bl	0x1000abd8c 
100009270:     	mov	x0, x26
100009274:     	bl	0x1000ae620    ; sel capacity
100009278:     	fsub	d1, d13, d8
10000927c:     	fmul	d9, d0, d1
100009280:     	b	0x100009320 <__text+0x32d0>
100009284:     	fmov	d15, d14
100009288:     	b	0x1000092b8 <__text+0x3268>
10000928c:     	adrp	x8, 0x100137000 
100009290:     	ldrb	w8, [x8, #0xc78]
100009294:     	cmp	w8, #0x0
100009298:     	fcsel	d1, d12, d11, ne
10000929c:     	fdiv	d9, d0, d1
1000092a0:     	b	0x100009384 <__text+0x3334>
1000092a4:     	fmov	d15, xzr
1000092a8:     	bl	0x1000abd8c 
1000092ac:     	ldr	w21, [sp, #0x24]
1000092b0:     	ldp	x24, x20, [sp, #0x10]
1000092b4:     	ldr	x27, [sp, #0x8]
1000092b8:     	mov	x0, x26
1000092bc:     	bl	0x1000ae620    ; sel capacity
1000092c0:     	fmul	d9, d15, d0
1000092c4:     	ldr	x19, [x22, #0x60]
1000092c8:     	mov	x0, x19
1000092cc:     	mov	w1, #0x1                ; =1
1000092d0:     	bl	0x1000abf9c 
1000092d4:     	cbz	w0, 0x100009320 <__text+0x32d0>
1000092d8:     	mov	x8, #0x4059000000000000 ; =4636737291354636288
1000092dc:     	fmov	d0, x8
1000092e0:     	mov	w8, #0x200              ; =512
1000092e4:     	movk	w8, #0x800, lsl #16
1000092e8:     	str	w8, [sp, #0x70]
1000092ec:     	fmul	d0, d15, d0
1000092f0:     	stur	d0, [sp, #0x74]
1000092f4:     	strh	w25, [sp, #0x7c]
1000092f8:     	stur	d9, [sp, #0x7e]
1000092fc:     	add	x4, sp, #0x70
100009300:     	adr	x0, 0x100000000    ; "����"
100009304:     	nop
100009308:     	mov	x1, x19
10000930c:     	mov	w2, #0x1                ; =1
100009310:     	adr	x3, 0x1000ec829    ; "Allocating %5.5f%% of widget budget based on usage, resulting in a adj"
100009314:     	nop
100009318:     	mov	w5, #0x16               ; =22
10000931c:     	bl	0x1000ab978 
100009320:     	cbz	x20, 0x100009384 <__text+0x3334>
100009324:     	ldr	x19, [x22, #0x60]
100009328:     	mov	x0, x19
10000932c:     	mov	w1, #0x1                ; =1
100009330:     	bl	0x1000abf9c 
100009334:     	cbz	w0, 0x100009374 <__text+0x3324>
100009338:     	mov	w8, #0x202              ; =514
10000933c:     	movk	w8, #0x840, lsl #16
100009340:     	str	w8, [sp, #0x70]
100009344:     	stur	x26, [sp, #0x74]
100009348:     	strh	w25, [sp, #0x7c]
10000934c:     	stur	d9, [sp, #0x7e]
100009350:     	add	x4, sp, #0x70
100009354:     	adr	x0, 0x100000000    ; "����"
100009358:     	nop
10000935c:     	mov	x1, x19
100009360:     	mov	w2, #0x1                ; =1
100009364:     	adr	x3, 0x1000ec8a5    ; "Increment budget %@ by %10.2lf"
100009368:     	nop
10000936c:     	mov	w5, #0x16               ; =22
100009370:     	bl	0x1000ab978 
100009374:     	mov	x0, x26
100009378:     	fmov	d0, d9
10000937c:     	bl	0x1000b21c0    ; sel incrementBy:
100009380:     	b	0x1000093e0 <__text+0x3390>
100009384:     	ldr	x19, [x22, #0x60]
100009388:     	mov	x0, x19
10000938c:     	mov	w1, #0x1                ; =1
100009390:     	bl	0x1000abf9c 
100009394:     	cbz	w0, 0x1000093d4 <__text+0x3384>
100009398:     	mov	w8, #0x202              ; =514
10000939c:     	movk	w8, #0x840, lsl #16
1000093a0:     	str	w8, [sp, #0x70]
1000093a4:     	stur	x26, [sp, #0x74]
1000093a8:     	strh	w25, [sp, #0x7c]
1000093ac:     	stur	d9, [sp, #0x7e]
1000093b0:     	add	x4, sp, #0x70
1000093b4:     	adr	x0, 0x100000000    ; "����"
1000093b8:     	nop
1000093bc:     	mov	x1, x19
1000093c0:     	mov	w2, #0x1                ; =1
1000093c4:     	adr	x3, 0x1000ec880    ; "Set budget balance for %@ to %10.2lf"
1000093c8:     	nop
1000093cc:     	mov	w5, #0x16               ; =22
1000093d0:     	bl	0x1000ab978 
1000093d4:     	mov	x0, x26
1000093d8:     	fmov	d0, d9
1000093dc:     	bl	0x1000b8100    ; sel setBalance:
1000093e0:     	add	x28, x28, #0x1
1000093e4:     	ldr	x8, [sp, #0x28]
1000093e8:     	cmp	x28, x8
1000093ec:     	b.ne	0x100009044 <__text+0x2ff4>
1000093f0:     	add	x2, sp, #0x30
1000093f4:     	add	x3, sp, #0x90
1000093f8:     	mov	x0, x24
1000093fc:     	mov	w4, #0x10               ; =16
100009400:     	bl	0x1000af200    ; sel countByEnumeratingWithState:objects:count:
100009404:     	str	x0, [sp, #0x28]
100009408:     	cbnz	x0, 0x100009040 <__text+0x2ff0>
10000940c:     	bl	0x1000abdbc 
100009410:     	bl	0x1000abdc8 
100009414:     	ldur	x8, [x29, #-0xa0]
100009418:     	nop
10000941c:     	ldr	x9, 0x1000fc508 
100009420:     	ldr	x9, [x9]
100009424:     	cmp	x9, x8
100009428:     	b.ne	0x10000945c <__text+0x340c>
10000942c:     	ldp	x29, x30, [sp, #0x1b0]
100009430:     	ldp	x20, x19, [sp, #0x1a0]
100009434:     	ldp	x22, x21, [sp, #0x190]
100009438:     	ldp	x24, x23, [sp, #0x180]
10000943c:     	ldp	x26, x25, [sp, #0x170]
100009440:     	ldp	x28, x27, [sp, #0x160]
100009444:     	ldp	d9, d8, [sp, #0x150]
100009448:     	ldp	d11, d10, [sp, #0x140]
10000944c:     	ldp	d13, d12, [sp, #0x130]
100009450:     	ldp	d15, d14, [sp, #0x120]
100009454:     	add	sp, sp, #0x1c0
100009458:     	ret
10000945c:     	bl	0x1000ab90c 
