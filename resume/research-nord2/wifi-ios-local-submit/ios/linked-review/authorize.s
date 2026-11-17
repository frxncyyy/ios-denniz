
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7d38 <_nst_authorize>:
fffffe00079a7d38:      	cbz	x0, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d3c:      	mov	x8, x0
fffffe00079a7d40:      	ldr	w9, [x0, #28]
fffffe00079a7d44:      	cbz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d48:      	ldr	x9, [x8]
fffffe00079a7d4c:      	cbz	x9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d50:      	ldr	x10, [x8, #8]
fffffe00079a7d54:      	cbz	x10, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d58:      	ldr	x10, [x8, #16]
fffffe00079a7d5c:      	cbz	x10, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d60:      	ldr	w10, [x8, #44]
fffffe00079a7d64:      	cbnz	w10, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d68:      	ldr	w10, [x9, #12]
fffffe00079a7d6c:      	cbnz	w10, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d70:      	ldr	w9, [x9, #1876]
fffffe00079a7d74:      	cbnz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d78:      	ldr	w9, [x8, #1108]
fffffe00079a7d7c:      	cbnz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d80:      	ldr	w9, [x8, #24]
fffffe00079a7d84:      	cbz	w9, 0xfffffe00079a7d90 <_nst_authorize+0x58>
fffffe00079a7d88:      	mov	w0, #-1
fffffe00079a7d8c:      	ret
fffffe00079a7d90:      	ldr	w9, [x8, #924]
fffffe00079a7d94:      	cbz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7d98:      	ldr	w9, [x8, #928]
fffffe00079a7d9c:      	cbz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7da0:      	ldr	w9, [x8, #632]
fffffe00079a7da4:      	cbz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7da8:      	ldr	w9, [x8, #776]
fffffe00079a7dac:      	cbz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7db0:      	ldr	w9, [x8, #56]
fffffe00079a7db4:      	cbnz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7db8:      	ldr	w9, [x8, #60]
fffffe00079a7dbc:      	cbnz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7dc0:      	ldr	w9, [x8, #952]
fffffe00079a7dc4:      	cbnz	w9, 0xfffffe00079a7d88 <_nst_authorize+0x50>
fffffe00079a7dc8:      	mov	w9, #1
fffffe00079a7dcc:      	mov	w0, wzr
fffffe00079a7dd0:      	str	w9, [x8, #920]
fffffe00079a7dd4:      	ret
