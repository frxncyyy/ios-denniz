
$NORD2_REPO/research/ci-39859151068/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b8c68 <_nwo_pump>:
fffffe00079b8c68:      	sub	sp, sp, #80
fffffe00079b8c6c:      	stp	x22, x21, [sp, #32]
fffffe00079b8c70:      	stp	x20, x19, [sp, #48]
fffffe00079b8c74:      	stp	x29, x30, [sp, #64]
fffffe00079b8c78:      	add	x29, sp, #64
fffffe00079b8c7c:      	adrp	x8, 0xfffffe0007c98000 <_submit_native+0x118>
fffffe00079b8c80:      	nop
fffffe00079b8c84:      	ldr	x8, [x8, #3440]
fffffe00079b8c88:      	str	x8, [sp, #24]
fffffe00079b8c8c:      	cbz	x0, 0xfffffe00079b8ca4 <_nwo_pump+0x3c>
fffffe00079b8c90:      	mov	x19, x0
fffffe00079b8c94:      	ldr	w8, [x0, #4792]
fffffe00079b8c98:      	cbnz	w8, 0xfffffe00079b8ca4 <_nwo_pump+0x3c>
fffffe00079b8c9c:      	ldr	w8, [x19, #4788]
fffffe00079b8ca0:      	cbz	w8, 0xfffffe00079b8cd8 <_nwo_pump+0x70>
fffffe00079b8ca4:      	mov	w21, #-1
fffffe00079b8ca8:      	adrp	x8, 0xfffffe0007c98000 <_submit_native+0x144>
fffffe00079b8cac:      	nop
fffffe00079b8cb0:      	ldr	x9, [sp, #24]
fffffe00079b8cb4:      	ldr	x8, [x8, #3440]
fffffe00079b8cb8:      	cmp	x8, x9
fffffe00079b8cbc:      	b.ne	0xfffffe00079b8ea8 <_nwo_pump+0x240>
fffffe00079b8cc0:      	mov	w0, w21
fffffe00079b8cc4:      	ldp	x29, x30, [sp, #64]
fffffe00079b8cc8:      	ldp	x20, x19, [sp, #48]
fffffe00079b8ccc:      	ldp	x22, x21, [sp, #32]
fffffe00079b8cd0:      	add	sp, sp, #80
fffffe00079b8cd4:      	ret
fffffe00079b8cd8:      	mov	x0, x19
fffffe00079b8cdc:      	mov	x20, x1
fffffe00079b8ce0:      	bl	0xfffffe00079b2798 <_stc_tick>
fffffe00079b8ce4:      	ldr	w8, [x19, #8]
fffffe00079b8ce8:      	mov	w21, w0
fffffe00079b8cec:      	cmp	w8, #2
fffffe00079b8cf0:      	b.lo	0xfffffe00079b8d50 <_nwo_pump+0xe8>
fffffe00079b8cf4:      	ldr	x8, [x19, #4752]
fffffe00079b8cf8:      	ldr	x0, [x19, #4760]
fffffe00079b8cfc:      	blr	x8
fffffe00079b8d00:      	mov	w8, #1
fffffe00079b8d04:      	cbz	w0, 0xfffffe00079b8d4c <_nwo_pump+0xe4>
fffffe00079b8d08:      	add	x0, x19, #1464
fffffe00079b8d0c:      	str	w8, [x19, #4792]
fffffe00079b8d10:      	bl	0xfffffe00079b5e58 <_nwp_cancel>
fffffe00079b8d14:      	mov	x8, xzr
fffffe00079b8d18:      	add	x9, x19, #4080
fffffe00079b8d1c:      	strb	wzr, [x9, x8]
fffffe00079b8d20:      	add	x8, x8, #1
fffffe00079b8d24:      	cmp	x8, #664
fffffe00079b8d28:      	b.ne	0xfffffe00079b8d1c <_nwo_pump+0xb4>
fffffe00079b8d2c:      	mov	x8, xzr
fffffe00079b8d30:      	add	x9, x19, #584
fffffe00079b8d34:      	mov	w21, #-1
fffffe00079b8d38:      	strb	wzr, [x9, x8]
fffffe00079b8d3c:      	add	x8, x8, #1
fffffe00079b8d40:      	cmp	x8, #640
fffffe00079b8d44:      	b.ne	0xfffffe00079b8d38 <_nwo_pump+0xd0>
fffffe00079b8d48:      	b	0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8d4c:      	str	w8, [x19, #4796]
fffffe00079b8d50:      	ldr	w8, [x19, #4]
fffffe00079b8d54:      	cmp	w8, #22
fffffe00079b8d58:      	b.ne	0xfffffe00079b8d6c <_nwo_pump+0x104>
fffffe00079b8d5c:      	mov	w8, #1
fffffe00079b8d60:      	mov	w21, #-1
fffffe00079b8d64:      	str	w8, [x19, #4792]
fffffe00079b8d68:      	b	0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8d6c:      	tbnz	w21, #31, 0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8d70:      	ldr	x8, [x19, #4776]
fffffe00079b8d74:      	cbz	x8, 0xfffffe00079b8d80 <_nwo_pump+0x118>
fffffe00079b8d78:      	mov	w21, wzr
fffffe00079b8d7c:      	b	0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8d80:      	mov	x0, x19
fffffe00079b8d84:      	bl	0xfffffe00079b3164 <_stc_next>
fffffe00079b8d88:      	mov	x21, x0
fffffe00079b8d8c:      	cbz	x0, 0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8d90:      	ldr	w22, [x21, #16]
fffffe00079b8d94:      	mov	w0, w22
fffffe00079b8d98:      	bl	0xfffffe00079b8f50 <_expected>
fffffe00079b8d9c:      	cbz	w0, 0xfffffe00079b8e50 <_nwo_pump+0x1e8>
fffffe00079b8da0:      	cmp	w22, #16
fffffe00079b8da4:      	strb	wzr, [sp, #16]
fffffe00079b8da8:      	str	xzr, [sp, #8]
fffffe00079b8dac:      	b.eq	0xfffffe00079b8db8 <_nwo_pump+0x150>
fffffe00079b8db0:      	cmp	w22, #8
fffffe00079b8db4:      	b.ne	0xfffffe00079b8dcc <_nwo_pump+0x164>
fffffe00079b8db8:      	add	x8, x19, #1684
fffffe00079b8dbc:      	ldr	w9, [x19, #2872]
fffffe00079b8dc0:      	ldr	x8, [x8]
fffffe00079b8dc4:      	strb	w9, [sp, #8]
fffffe00079b8dc8:      	stur	x8, [sp, #9]
fffffe00079b8dcc:      	ldr	x8, [x21]
fffffe00079b8dd0:      	mov	w9, #1
fffffe00079b8dd4:      	ldr	x10, [x19, #4744]
fffffe00079b8dd8:      	add	x2, sp, #8
fffffe00079b8ddc:      	ldr	x0, [x19, #4760]
fffffe00079b8de0:      	mov	x1, x21
fffffe00079b8de4:      	str	x8, [x19, #4768]
fffffe00079b8de8:      	ldr	x8, [x21, #8]
fffffe00079b8dec:      	str	x8, [x19, #4776]
fffffe00079b8df0:      	ldr	w8, [x21, #16]
fffffe00079b8df4:      	str	w9, [x19, #4788]
fffffe00079b8df8:      	str	w8, [x19, #4784]
fffffe00079b8dfc:      	blr	x10
fffffe00079b8e00:      	cmp	w0, #1
fffffe00079b8e04:      	str	wzr, [x19, #4788]
fffffe00079b8e08:      	strb	wzr, [sp, #8]
fffffe00079b8e0c:      	strb	wzr, [sp, #9]
fffffe00079b8e10:      	strb	wzr, [sp, #10]
fffffe00079b8e14:      	strb	wzr, [sp, #11]
fffffe00079b8e18:      	strb	wzr, [sp, #12]
fffffe00079b8e1c:      	strb	wzr, [sp, #13]
fffffe00079b8e20:      	strb	wzr, [sp, #14]
fffffe00079b8e24:      	strb	wzr, [sp, #15]
fffffe00079b8e28:      	strb	wzr, [sp, #16]
fffffe00079b8e2c:      	b.eq	0xfffffe00079b8e64 <_nwo_pump+0x1fc>
fffffe00079b8e30:      	cbnz	w0, 0xfffffe00079b8e74 <_nwo_pump+0x20c>
fffffe00079b8e34:      	ldp	x1, x2, [x21]
fffffe00079b8e38:      	mov	x0, x19
fffffe00079b8e3c:      	bl	0xfffffe00079b317c <_stc_submitted>
fffffe00079b8e40:      	mov	w21, #1
fffffe00079b8e44:      	tbz	w0, #31, 0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8e48:      	str	w21, [x19, #4792]
fffffe00079b8e4c:      	b	0xfffffe00079b8e58 <_nwo_pump+0x1f0>
fffffe00079b8e50:      	mov	w8, #1
fffffe00079b8e54:      	str	w8, [x19, #4792]
fffffe00079b8e58:      	mov	x0, x19
fffffe00079b8e5c:      	bl	0xfffffe00079b8bbc <_close_port>
fffffe00079b8e60:      	b	0xfffffe00079b8ca4 <_nwo_pump+0x3c>
fffffe00079b8e64:      	mov	w21, wzr
fffffe00079b8e68:      	str	xzr, [x19, #4776]
fffffe00079b8e6c:      	str	wzr, [x19, #4784]
fffffe00079b8e70:      	b	0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8e74:      	ldr	x1, [x19, #4768]
fffffe00079b8e78:      	mov	x0, x19
fffffe00079b8e7c:      	ldr	x2, [x19, #4776]
fffffe00079b8e80:      	mov	w3, wzr
fffffe00079b8e84:      	mov	x4, x20
fffffe00079b8e88:      	str	xzr, [x19, #4776]
fffffe00079b8e8c:      	str	wzr, [x19, #4784]
fffffe00079b8e90:      	bl	0xfffffe00079b31fc <_stc_receipt>
fffffe00079b8e94:      	mov	w1, w0
fffffe00079b8e98:      	mov	x0, x19
fffffe00079b8e9c:      	bl	0xfffffe00079b8eac <_sync_state>
fffffe00079b8ea0:      	mov	w21, w0
fffffe00079b8ea4:      	b	0xfffffe00079b8ca8 <_nwo_pump+0x40>
fffffe00079b8ea8:      	bl	0xfffffe0007a06354 <___stack_chk_fail>
