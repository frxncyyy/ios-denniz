
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b8140 <_nwp_security_submitted>:
fffffe00079b8140:      	mov	w8, wzr
fffffe00079b8144:      	cbz	x0, 0xfffffe00079b81f8 <_nwp_security_submitted+0xb8>
fffffe00079b8148:      	cbz	x2, 0xfffffe00079b81f8 <_nwp_security_submitted+0xb8>
fffffe00079b814c:      	ldr	x8, [x0]
fffffe00079b8150:      	cmp	x8, x1
fffffe00079b8154:      	b.ne	0xfffffe00079b81d8 <_nwp_security_submitted+0x98>
fffffe00079b8158:      	ldr	x8, [x0, #16]
fffffe00079b815c:      	cmp	x8, x2
fffffe00079b8160:      	b.ne	0xfffffe00079b81d8 <_nwp_security_submitted+0x98>
fffffe00079b8164:      	ldr	w8, [x0, #36]
fffffe00079b8168:      	str	xzr, [x0, #16]
fffffe00079b816c:      	str	wzr, [x0, #36]
fffffe00079b8170:      	cbz	w3, 0xfffffe00079b819c <_nwp_security_submitted+0x5c>
fffffe00079b8174:      	cmp	w8, #4
fffffe00079b8178:      	b.ne	0xfffffe00079b81b8 <_nwp_security_submitted+0x78>
fffffe00079b817c:      	ldr	w8, [x0, #32]
fffffe00079b8180:      	cmp	w8, #5
fffffe00079b8184:      	b.ne	0xfffffe00079b81e0 <_nwp_security_submitted+0xa0>
fffffe00079b8188:      	ldr	w8, [x0, #228]
fffffe00079b818c:      	cmp	w8, #2
fffffe00079b8190:      	b.ne	0xfffffe00079b81e0 <_nwp_security_submitted+0xa0>
fffffe00079b8194:      	str	wzr, [x0, #228]
fffffe00079b8198:      	b	0xfffffe00079b81d8 <_nwp_security_submitted+0x98>
fffffe00079b819c:      	mov	x8, xzr
fffffe00079b81a0:      	strb	wzr, [x0, x8]
fffffe00079b81a4:      	add	x8, x8, #1
fffffe00079b81a8:      	cmp	x8, #2616
fffffe00079b81ac:      	b.ne	0xfffffe00079b81a0 <_nwp_security_submitted+0x60>
fffffe00079b81b0:      	mov	w0, #-1
fffffe00079b81b4:      	ret
fffffe00079b81b8:      	and	w8, w8, #0xfffffffe
fffffe00079b81bc:      	cmp	w8, #2
fffffe00079b81c0:      	b.ne	0xfffffe00079b81d8 <_nwp_security_submitted+0x98>
fffffe00079b81c4:      	ldr	w8, [x0, #32]
fffffe00079b81c8:      	cmp	w8, #3
fffffe00079b81cc:      	b.ne	0xfffffe00079b81d8 <_nwp_security_submitted+0x98>
fffffe00079b81d0:      	ldr	w8, [x0, #40]
fffffe00079b81d4:      	cbz	w8, 0xfffffe00079b8200 <_nwp_security_submitted+0xc0>
fffffe00079b81d8:      	mov	w0, wzr
fffffe00079b81dc:      	ret
fffffe00079b81e0:      	mov	x9, xzr
fffffe00079b81e4:      	mov	w8, #-1
fffffe00079b81e8:      	strb	wzr, [x0, x9]
fffffe00079b81ec:      	add	x9, x9, #1
fffffe00079b81f0:      	cmp	x9, #2616
fffffe00079b81f4:      	b.ne	0xfffffe00079b81e8 <_nwp_security_submitted+0xa8>
fffffe00079b81f8:      	mov	w0, w8
fffffe00079b81fc:      	ret
fffffe00079b8200:      	mov	w9, #1
fffffe00079b8204:      	mov	w10, #4
fffffe00079b8208:      	mov	w8, #2
fffffe00079b820c:      	str	x2, [x0, #24]
fffffe00079b8210:      	str	w9, [x0, #40]
fffffe00079b8214:      	str	w10, [x0, #32]
fffffe00079b8218:      	mov	w0, w8
fffffe00079b821c:      	ret
