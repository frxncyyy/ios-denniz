
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007996e34 <_nct_continue_prepare>:
fffffe0007996e34:      	stp	x22, x21, [sp, #-48]!
fffffe0007996e38:      	stp	x20, x19, [sp, #16]
fffffe0007996e3c:      	stp	x29, x30, [sp, #32]
fffffe0007996e40:      	add	x29, sp, #32
fffffe0007996e44:      	cbz	x3, 0xfffffe0007996e54 <_nct_continue_prepare+0x20>
fffffe0007996e48:      	mov	x19, x3
fffffe0007996e4c:      	ldr	w8, [x3, #32]
fffffe0007996e50:      	cbz	w8, 0xfffffe0007996e5c <_nct_continue_prepare+0x28>
fffffe0007996e54:      	mov	w0, #-1
fffffe0007996e58:      	b	0xfffffe0007996ed8 <_nct_continue_prepare+0xa4>
fffffe0007996e5c:      	mov	w8, #1
fffffe0007996e60:      	mov	x20, x2
fffffe0007996e64:      	mov	x21, x1
fffffe0007996e68:      	mov	x22, x0
fffffe0007996e6c:      	str	x0, [x19]
fffffe0007996e70:      	str	w8, [x19, #32]
fffffe0007996e74:      	bl	0xfffffe00079968d4 <_basic_receipt>
fffffe0007996e78:      	cmp	x21, x19
fffffe0007996e7c:      	b.eq	0xfffffe0007996ecc <_nct_continue_prepare+0x98>
fffffe0007996e80:      	cbnz	w0, 0xfffffe0007996ecc <_nct_continue_prepare+0x98>
fffffe0007996e84:      	mov	x0, x21
fffffe0007996e88:      	bl	0xfffffe0007996cb0 <_terminal>
fffffe0007996e8c:      	cbz	w0, 0xfffffe0007996ecc <_nct_continue_prepare+0x98>
fffffe0007996e90:      	ldr	x8, [x21]
fffffe0007996e94:      	cmp	x8, x22
fffffe0007996e98:      	b.ne	0xfffffe0007996ecc <_nct_continue_prepare+0x98>
fffffe0007996e9c:      	ldr	x8, [x21, #8]
fffffe0007996ea0:      	ldr	x9, [x22]
fffffe0007996ea4:      	cmp	x8, x9
fffffe0007996ea8:      	b.ne	0xfffffe0007996ecc <_nct_continue_prepare+0x98>
fffffe0007996eac:      	ldr	x9, [x21, #16]
fffffe0007996eb0:      	ldr	x10, [x22, #8]
fffffe0007996eb4:      	cmp	x9, x10
fffffe0007996eb8:      	b.ne	0xfffffe0007996ecc <_nct_continue_prepare+0x98>
fffffe0007996ebc:      	ldr	x9, [x21, #24]
fffffe0007996ec0:      	ldr	x10, [x22, #16]
fffffe0007996ec4:      	cmp	x9, x10
fffffe0007996ec8:      	b.eq	0xfffffe0007996ee8 <_nct_continue_prepare+0xb4>
fffffe0007996ecc:      	mov	w8, #110
fffffe0007996ed0:      	mov	w0, #1
fffffe0007996ed4:      	str	w8, [x19, #56]
fffffe0007996ed8:      	ldp	x29, x30, [sp, #32]
fffffe0007996edc:      	ldp	x20, x19, [sp, #16]
fffffe0007996ee0:      	ldp	x22, x21, [sp], #48
fffffe0007996ee4:      	ret
fffffe0007996ee8:      	str	x8, [x19, #8]
fffffe0007996eec:      	ldr	x8, [x22, #8]
fffffe0007996ef0:      	str	x8, [x19, #16]
fffffe0007996ef4:      	ldr	x9, [x22, #16]
fffffe0007996ef8:      	str	x9, [x19, #24]
fffffe0007996efc:      	ldr	w10, [x21, #132]
fffffe0007996f00:      	ldr	w9, [x21, #68]
fffffe0007996f04:      	add	w10, w9, w10
fffffe0007996f08:      	cmn	w10, #1
fffffe0007996f0c:      	b.eq	0xfffffe0007996f6c <_nct_continue_prepare+0x138>
fffffe0007996f10:      	ldr	w11, [x8, #1920]
fffffe0007996f14:      	cmp	w11, w10
fffffe0007996f18:      	b.ne	0xfffffe0007996f6c <_nct_continue_prepare+0x138>
fffffe0007996f1c:      	ldr	w10, [x21, #128]
fffffe0007996f20:      	ldr	w11, [x8, #2864]
fffffe0007996f24:      	add	w10, w10, w9
fffffe0007996f28:      	and	w10, w10, #0x3ff
fffffe0007996f2c:      	cmp	w11, w10
fffffe0007996f30:      	b.ne	0xfffffe0007996f6c <_nct_continue_prepare+0x138>
fffffe0007996f34:      	ldrb	w10, [x21, #136]
fffffe0007996f38:      	ldrb	w11, [x8, #2876]
fffffe0007996f3c:      	add	w9, w10, w9
fffffe0007996f40:      	cmp	w11, w9, uxtb
fffffe0007996f44:      	b.ne	0xfffffe0007996f6c <_nct_continue_prepare+0x138>
fffffe0007996f48:      	ldr	w9, [x8, #1908]
fffffe0007996f4c:      	cbnz	w9, 0xfffffe0007996f6c <_nct_continue_prepare+0x138>
fffffe0007996f50:      	ldr	w9, [x8, #1912]
fffffe0007996f54:      	cbnz	w9, 0xfffffe0007996f6c <_nct_continue_prepare+0x138>
fffffe0007996f58:      	ldr	w9, [x8, #1916]
fffffe0007996f5c:      	cbnz	w9, 0xfffffe0007996f6c <_nct_continue_prepare+0x138>
fffffe0007996f60:      	ldr	w8, [x8, #2868]
fffffe0007996f64:      	cmp	w8, #16
fffffe0007996f68:      	b.lo	0xfffffe0007996f74 <_nct_continue_prepare+0x140>
fffffe0007996f6c:      	mov	w8, #111
fffffe0007996f70:      	b	0xfffffe0007996ed0 <_nct_continue_prepare+0x9c>
fffffe0007996f74:      	cbz	x20, 0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996f78:      	ldr	x8, [x20]
fffffe0007996f7c:      	cbz	x8, 0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996f80:      	ldr	x9, [x20, #16]
fffffe0007996f84:      	cbz	x9, 0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996f88:      	ldr	w9, [x20, #8]
fffffe0007996f8c:      	cmp	w9, #1
fffffe0007996f90:      	b.ne	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996f94:      	ldrb	w9, [x8, #2]
fffffe0007996f98:      	cmp	w9, #3
fffffe0007996f9c:      	b.ne	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996fa0:      	ldrh	w9, [x8]
fffffe0007996fa4:      	cmp	w9, #1252
fffffe0007996fa8:      	b.ne	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996fac:      	ldrb	w8, [x8, #3]
fffffe0007996fb0:      	cmp	w8, #1
fffffe0007996fb4:      	b.ne	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996fb8:      	ldr	x8, [x20, #32]
fffffe0007996fbc:      	mov	x9, #-34561
fffffe0007996fc0:      	movk	x9, #64620, lsl #16
fffffe0007996fc4:      	add	x10, x8, x9
fffffe0007996fc8:      	add	x9, x9, #1
fffffe0007996fcc:      	cmp	x10, x9
fffffe0007996fd0:      	b.lo	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996fd4:      	ldr	x9, [x20, #40]
fffffe0007996fd8:      	mov	w11, #57791
fffffe0007996fdc:      	movk	w11, #228, lsl #16
fffffe0007996fe0:      	sub	x10, x9, #1
fffffe0007996fe4:      	cmp	x10, x11
fffffe0007996fe8:      	b.hi	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996fec:      	cmp	x9, x8
fffffe0007996ff0:      	b.hs	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007996ff4:      	ldr	w8, [x21, #100]
fffffe0007996ff8:      	ldr	w9, [x22, #432]
fffffe0007996ffc:      	cmp	w8, w9
fffffe0007997000:      	b.ne	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007997004:      	ldr	w9, [x21, #108]
fffffe0007997008:      	sub	w8, w8, #1
fffffe000799700c:      	cmp	w9, w8
fffffe0007997010:      	b.ne	0xfffffe0007997044 <_nct_continue_prepare+0x210>
fffffe0007997014:      	mov	w8, #1
fffffe0007997018:      	str	x21, [x19, #4584]
fffffe000799701c:      	mov	x0, x19
fffffe0007997020:      	mov	x1, x20
fffffe0007997024:      	ldp	x29, x30, [sp, #32]
fffffe0007997028:      	str	w8, [x19, #4576]
fffffe000799702c:      	ldr	x8, [x21, #4592]
fffffe0007997030:      	str	x8, [x19, #4592]
fffffe0007997034:      	ldp	x20, x19, [sp, #16]
fffffe0007997038:      	ldr	w2, [x21, #100]
fffffe000799703c:      	ldp	x22, x21, [sp], #48
fffffe0007997040:      	b	0xfffffe0007996ae8 <_seal>
fffffe0007997044:      	mov	w8, #112
fffffe0007997048:      	b	0xfffffe0007996ed0 <_nct_continue_prepare+0x9c>
