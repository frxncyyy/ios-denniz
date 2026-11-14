100008df8:     	sub	sp, sp, #0x60
100008dfc:     	stp	x24, x23, [sp, #0x20]
100008e00:     	stp	x22, x21, [sp, #0x30]
100008e04:     	stp	x20, x19, [sp, #0x40]
100008e08:     	stp	x29, x30, [sp, #0x50]
100008e0c:     	add	x29, sp, #0x50
100008e10:     	mov	x20, x0
100008e14:     	nop
100008e18:     	ldr	x8, 0x1000fc508 
100008e1c:     	ldr	x8, [x8]
100008e20:     	str	x8, [sp, #0x18]
100008e24:     	adrp	x22, 0x100132000 
100008e28:     	ldr	x0, [x22, #0xb10]
100008e2c:     	bl	0x1000af980    ; sel date
100008e30:     	bl	0x1000abccc 
100008e34:     	mov	x19, x0
100008e38:     	ldr	x2, [x20, #0x18]
100008e3c:     	cbz	x2, 0x100008e94 <__text+0x2e44>
100008e40:     	mov	x0, x20
100008e44:     	mov	x3, x19
100008e48:     	bl	0x1000b4f40    ; sel numberOfModulationFromDate:toDate:
100008e4c:     	mov	x21, x0
100008e50:     	cbnz	w0, 0x100008e98 <__text+0x2e48>
100008e54:     	ldr	x20, [x20, #0x60]
100008e58:     	mov	x0, x20
100008e5c:     	mov	w1, #0x1                ; =1
100008e60:     	bl	0x1000abf9c 
100008e64:     	cbz	w0, 0x100008f48 <__text+0x2ef8>
100008e68:     	strh	wzr, [sp]
100008e6c:     	adr	x0, 0x100000000    ; "����"
100008e70:     	nop
100008e74:     	adr	x3, 0x1000ec73d    ; "Last updated budgets too recently. Exiting."
100008e78:     	nop
100008e7c:     	mov	x4, sp
100008e80:     	mov	x1, x20
100008e84:     	mov	w2, #0x1                ; =1
100008e88:     	mov	w5, #0x2                ; =2
100008e8c:     	bl	0x1000ab978 
100008e90:     	b	0x100008f48 <__text+0x2ef8>
100008e94:     	mov	w21, #0x1               ; =1
100008e98:     	ldr	x0, [x22, #0xb10]
100008e9c:     	bl	0x1000b4ec0    ; sel now
100008ea0:     	bl	0x1000abccc 
100008ea4:     	mov	x22, x0
100008ea8:     	ldr	x23, [x20, #0x60]
100008eac:     	mov	x0, x23
100008eb0:     	mov	w1, #0x1                ; =1
100008eb4:     	bl	0x1000abf9c 
100008eb8:     	cbz	w0, 0x100008f00 <__text+0x2eb0>
100008ebc:     	ldr	x8, [x20, #0x18]
100008ec0:     	mov	w9, #0x202              ; =514
100008ec4:     	movk	w9, #0x840, lsl #16
100008ec8:     	str	w9, [sp]
100008ecc:     	stur	x8, [sp, #0x4]
100008ed0:     	mov	w8, #0x400              ; =1024
100008ed4:     	strh	w8, [sp, #0xc]
100008ed8:     	stur	w21, [sp, #0xe]
100008edc:     	adr	x0, 0x100000000    ; "����"
100008ee0:     	nop
100008ee4:     	adr	x3, 0x1000ec769    ; "Last modulated budgets at %@, modulating for %d slots from then to now"
100008ee8:     	nop
100008eec:     	mov	x4, sp
100008ef0:     	mov	x1, x23
100008ef4:     	mov	w2, #0x1                ; =1
100008ef8:     	mov	w5, #0x12               ; =18
100008efc:     	bl	0x1000ab978 
100008f00:     	ldr	x2, [x20, #0x28]
100008f04:     	ldr	x3, [x20, #0x18]
100008f08:     	mov	x0, x20
100008f0c:     	mov	x4, x21
100008f10:     	mov	x5, x22
100008f14:     	bl	0x1000b4b60    ; sel modulateBudgets:lastModulatedAt:forNumberOfModulationSlots:atDate:
100008f18:     	ldr	x23, [x20, #0x18]
100008f1c:     	str	x22, [x20, #0x18]
100008f20:     	bl	0x1000abe94 
100008f24:     	mov	x21, x0
100008f28:     	bl	0x1000abdbc 
100008f2c:     	ldr	x0, [x20, #0x8]
100008f30:     	ldr	x2, [x20, #0x28]
100008f34:     	bl	0x1000b7a00    ; sel saveBudgets:
100008f38:     	ldr	x0, [x20, #0x8]
100008f3c:     	mov	x2, x21
100008f40:     	bl	0x1000b7a80    ; sel saveModulationDate:
100008f44:     	bl	0x1000abda4 
100008f48:     	bl	0x1000abd8c 
100008f4c:     	ldr	x8, [sp, #0x18]
100008f50:     	nop
100008f54:     	ldr	x9, 0x1000fc508 
100008f58:     	ldr	x9, [x9]
100008f5c:     	cmp	x9, x8
100008f60:     	b.ne	0x100008f7c <__text+0x2f2c>
100008f64:     	ldp	x29, x30, [sp, #0x50]
100008f68:     	ldp	x20, x19, [sp, #0x40]
100008f6c:     	ldp	x22, x21, [sp, #0x30]
100008f70:     	ldp	x24, x23, [sp, #0x20]
100008f74:     	add	sp, sp, #0x60
100008f78:     	ret
100008f7c:     	bl	0x1000ab90c 
