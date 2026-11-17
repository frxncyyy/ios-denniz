
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079acf08 <_nst_step>:
fffffe00079acf08:      	sub	sp, sp, #160
fffffe00079acf0c:      	stp	x28, x27, [sp, #64]
fffffe00079acf10:      	stp	x26, x25, [sp, #80]
fffffe00079acf14:      	stp	x24, x23, [sp, #96]
fffffe00079acf18:      	stp	x22, x21, [sp, #112]
fffffe00079acf1c:      	stp	x20, x19, [sp, #128]
fffffe00079acf20:      	stp	x29, x30, [sp, #144]
fffffe00079acf24:      	add	x29, sp, #144
fffffe00079acf28:      	cbz	x0, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf2c:      	mov	x19, x0
fffffe00079acf30:      	ldr	w8, [x0, #28]
fffffe00079acf34:      	cbz	w8, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf38:      	ldr	x8, [x19]
fffffe00079acf3c:      	cbz	x8, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf40:      	ldr	x9, [x19, #8]
fffffe00079acf44:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf48:      	ldr	x9, [x19, #16]
fffffe00079acf4c:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf50:      	ldr	w9, [x19, #44]
fffffe00079acf54:      	cbnz	w9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf58:      	ldr	w9, [x8, #12]
fffffe00079acf5c:      	cbnz	w9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf60:      	ldr	w8, [x8, #1884]
fffffe00079acf64:      	cbnz	w8, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acf68:      	ldr	w8, [x19, #36]
fffffe00079acf6c:      	cbz	w8, 0xfffffe00079acf94 <_nst_step+0x8c>
fffffe00079acf70:      	mov	w0, #-1
fffffe00079acf74:      	ldp	x29, x30, [sp, #144]
fffffe00079acf78:      	ldp	x20, x19, [sp, #128]
fffffe00079acf7c:      	ldp	x22, x21, [sp, #112]
fffffe00079acf80:      	ldp	x24, x23, [sp, #96]
fffffe00079acf84:      	ldp	x26, x25, [sp, #80]
fffffe00079acf88:      	ldp	x28, x27, [sp, #64]
fffffe00079acf8c:      	add	sp, sp, #160
fffffe00079acf90:      	ret
fffffe00079acf94:      	mov	x23, x1
fffffe00079acf98:      	mov	w0, #-1
fffffe00079acf9c:      	cbz	x1, 0xfffffe00079acf74 <_nst_step+0x6c>
fffffe00079acfa0:      	ldr	w8, [x19, #148]
fffffe00079acfa4:      	cbnz	w8, 0xfffffe00079acf74 <_nst_step+0x6c>
fffffe00079acfa8:      	ldr	x8, [x23]
fffffe00079acfac:      	cbz	x8, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acfb0:      	ldr	x9, [x23, #8]
fffffe00079acfb4:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acfb8:      	ldr	x9, [x8]
fffffe00079acfbc:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acfc0:      	ldr	x9, [x8, #8]
fffffe00079acfc4:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acfc8:      	ldr	x9, [x8, #16]
fffffe00079acfcc:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acfd0:      	ldr	x9, [x8, #24]
fffffe00079acfd4:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acfd8:      	ldr	x9, [x8, #32]
fffffe00079acfdc:      	cbz	x9, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079acfe0:      	mov	w24, w3
fffffe00079acfe4:      	mov	w0, #-1
fffffe00079acfe8:      	sub	w9, w3, #17
fffffe00079acfec:      	cmn	w9, #16
fffffe00079acff0:      	b.lo	0xfffffe00079acf74 <_nst_step+0x6c>
fffffe00079acff4:      	ldr	x8, [x8, #40]
fffffe00079acff8:      	cbz	x8, 0xfffffe00079acf74 <_nst_step+0x6c>
fffffe00079acffc:      	movi.2s	v0, #1
fffffe00079ad000:      	mov	x0, x19
fffffe00079ad004:      	mov	x1, x23
fffffe00079ad008:      	mov	x20, x5
fffffe00079ad00c:      	mov	x21, x4
fffffe00079ad010:      	mov	x22, x2
fffffe00079ad014:      	str	wzr, [x19, #40]
fffffe00079ad018:      	str	d0, [x19, #32]
fffffe00079ad01c:      	bl	0xfffffe00079ad1d8 <_before>
fffffe00079ad020:      	cbnz	w0, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079ad024:      	mov	x0, x19
fffffe00079ad028:      	mov	x1, x23
fffffe00079ad02c:      	mov	x2, x22
fffffe00079ad030:      	bl	0xfffffe00079ad2f0 <_coherent>
fffffe00079ad034:      	cbnz	w0, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079ad038:      	mov	x0, x19
fffffe00079ad03c:      	mov	x1, x23
fffffe00079ad040:      	mov	x2, x22
fffffe00079ad044:      	mov	x3, x21
fffffe00079ad048:      	mov	x4, x20
fffffe00079ad04c:      	bl	0xfffffe00079ad3c4 <_transmit>
fffffe00079ad050:      	cbnz	w0, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079ad054:      	mov	w25, wzr
fffffe00079ad058:      	adrp	x27, 0xfffffe0007031000 <_txd+0x88>
fffffe00079ad05c:      	add	x27, x27, #1116
fffffe00079ad060:      	b	0xfffffe00079ad080 <_nst_step+0x178>
fffffe00079ad064:      	mov	w8, #1
fffffe00079ad068:      	cmp	w28, #0
fffffe00079ad06c:      	mov	w9, #3
fffffe00079ad070:      	csel	w9, w9, wzr, eq
fffffe00079ad074:      	cmp	w0, #0
fffffe00079ad078:      	csel	w8, w8, w9, lt
fffffe00079ad07c:      	cbnz	w8, 0xfffffe00079ad0d8 <_nst_step+0x1d0>
fffffe00079ad080:      	cmp	w25, w24
fffffe00079ad084:      	b.hs	0xfffffe00079ad0e0 <_nst_step+0x1d8>
fffffe00079ad088:      	mov	x26, xzr
fffffe00079ad08c:      	mov	w28, wzr
fffffe00079ad090:      	ldr	w3, [x27, x26, lsl #2]
fffffe00079ad094:      	mov	x0, x19
fffffe00079ad098:      	mov	x1, x23
fffffe00079ad09c:      	mov	x2, x22
fffffe00079ad0a0:      	mov	x4, x21
fffffe00079ad0a4:      	mov	x5, x20
fffffe00079ad0a8:      	bl	0xfffffe00079add60 <_receive>
fffffe00079ad0ac:      	tbnz	w0, #31, 0xfffffe00079ad064 <_nst_step+0x15c>
fffffe00079ad0b0:      	cmp	w0, #0
fffffe00079ad0b4:      	mov	w8, #4
fffffe00079ad0b8:      	cinc	w25, w25, ne
fffffe00079ad0bc:      	cinc	w28, w28, ne
fffffe00079ad0c0:      	cmp	x26, #3
fffffe00079ad0c4:      	b.hi	0xfffffe00079ad068 <_nst_step+0x160>
fffffe00079ad0c8:      	add	x26, x26, #1
fffffe00079ad0cc:      	cmp	w25, w24
fffffe00079ad0d0:      	b.lo	0xfffffe00079ad090 <_nst_step+0x188>
fffffe00079ad0d4:      	b	0xfffffe00079ad068 <_nst_step+0x160>
fffffe00079ad0d8:      	cmp	w8, #3
fffffe00079ad0dc:      	b.ne	0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079ad0e0:      	ldr	w8, [x19, #60]
fffffe00079ad0e4:      	cbz	w8, 0xfffffe00079ad178 <_nst_step+0x270>
fffffe00079ad0e8:      	ldr	w8, [x19, #136]
fffffe00079ad0ec:      	cbz	w8, 0xfffffe00079ad178 <_nst_step+0x270>
fffffe00079ad0f0:      	ldr	w8, [x19, #140]
fffffe00079ad0f4:      	cbz	w8, 0xfffffe00079ad15c <_nst_step+0x254>
fffffe00079ad0f8:      	ldr	w8, [x19, #144]
fffffe00079ad0fc:      	ldr	x9, [x19, #160]
fffffe00079ad100:      	cmp	w8, #256
fffffe00079ad104:      	stp	xzr, xzr, [sp, #40]
fffffe00079ad108:      	strh	wzr, [sp, #32]
fffffe00079ad10c:      	stp	xzr, x9, [sp, #16]
fffffe00079ad110:      	ldp	w10, w9, [x19, #120]
fffffe00079ad114:      	str	wzr, [sp, #36]
fffffe00079ad118:      	strb	wzr, [sp, #41]
fffffe00079ad11c:      	str	xzr, [sp, #56]
fffffe00079ad120:      	strb	w10, [sp, #34]
fffffe00079ad124:      	mov	w10, #2
fffffe00079ad128:      	strb	w9, [sp, #35]
fffffe00079ad12c:      	str	wzr, [x19, #60]
fffffe00079ad130:      	stp	w10, w8, [sp, #8]
fffffe00079ad134:      	b.ne	0xfffffe00079ad140 <_nst_step+0x238>
fffffe00079ad138:      	mov	x0, x19
fffffe00079ad13c:      	bl	0xfffffe00079ab318 <_nst_close_port>
fffffe00079ad140:      	add	x3, sp, #8
fffffe00079ad144:      	mov	x0, x19
fffffe00079ad148:      	mov	x1, x21
fffffe00079ad14c:      	mov	x2, x20
fffffe00079ad150:      	bl	0xfffffe00079ae188 <_emit>
fffffe00079ad154:      	cbnz	w0, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079ad158:      	b	0xfffffe00079ad178 <_nst_step+0x270>
fffffe00079ad15c:      	mov	x0, x19
fffffe00079ad160:      	bl	0xfffffe00079ae144 <_retired_air_failure>
fffffe00079ad164:      	cbz	w0, 0xfffffe00079ad178 <_nst_step+0x270>
fffffe00079ad168:      	ldr	x8, [x19, #168]
fffffe00079ad16c:      	ldr	x9, [x19, #184]
fffffe00079ad170:      	cmp	x8, x9
fffffe00079ad174:      	b.hs	0xfffffe00079ad1d0 <_nst_step+0x2c8>
fffffe00079ad178:      	ldr	w8, [x19, #56]
fffffe00079ad17c:      	cbz	w8, 0xfffffe00079ad1b4 <_nst_step+0x2ac>
fffffe00079ad180:      	ldr	w8, [x19, #10252]
fffffe00079ad184:      	cbz	w8, 0xfffffe00079ad1b4 <_nst_step+0x2ac>
fffffe00079ad188:      	ldr	w8, [x19, #128]
fffffe00079ad18c:      	and	w8, w8, #0xfffffffe
fffffe00079ad190:      	cmp	w8, #2
fffffe00079ad194:      	b.ne	0xfffffe00079ad1b4 <_nst_step+0x2ac>
fffffe00079ad198:      	mov	x0, x19
fffffe00079ad19c:      	mov	x1, x23
fffffe00079ad1a0:      	mov	x2, x22
fffffe00079ad1a4:      	mov	x3, x21
fffffe00079ad1a8:      	mov	x4, x20
fffffe00079ad1ac:      	bl	0xfffffe00079ae1f0 <_data_transmit>
fffffe00079ad1b0:      	cbnz	w0, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079ad1b4:      	mov	x0, x19
fffffe00079ad1b8:      	mov	x1, x21
fffffe00079ad1bc:      	mov	x2, x20
fffffe00079ad1c0:      	bl	0xfffffe00079aeab0 <_peer_progress>
fffffe00079ad1c4:      	cbnz	w0, 0xfffffe00079acf70 <_nst_step+0x68>
fffffe00079ad1c8:      	stp	wzr, wzr, [x19, #36]
fffffe00079ad1cc:      	b	0xfffffe00079acf74 <_nst_step+0x6c>
fffffe00079ad1d0:      	mov	w8, #256
fffffe00079ad1d4:      	b	0xfffffe00079ad0fc <_nst_step+0x1f4>
