
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7054 <_before>:
fffffe00079a7054:      	sub	sp, sp, #64
fffffe00079a7058:      	stp	x22, x21, [sp, #16]
fffffe00079a705c:      	stp	x20, x19, [sp, #32]
fffffe00079a7060:      	stp	x29, x30, [sp, #48]
fffffe00079a7064:      	add	x29, sp, #48
fffffe00079a7068:      	mov	x21, x1
fffffe00079a706c:      	ldr	x1, [x0]
fffffe00079a7070:      	mov	x20, x2
fffffe00079a7074:      	mov	x19, x0
fffffe00079a7078:      	str	xzr, [sp, #8]
fffffe00079a707c:      	bl	0xfffffe00079a5788 <_nst_lease_window>
fffffe00079a7080:      	cbz	w0, 0xfffffe00079a70bc <_before+0x68>
fffffe00079a7084:      	ldr	x8, [x21]
fffffe00079a7088:      	mov	x0, x20
fffffe00079a708c:      	ldr	x8, [x8]
fffffe00079a7090:      	blr	x8
fffffe00079a7094:      	cbnz	w0, 0xfffffe00079a70bc <_before+0x68>
fffffe00079a7098:      	ldr	x8, [x21, #8]
fffffe00079a709c:      	add	x1, sp, #8
fffffe00079a70a0:      	mov	x0, x20
fffffe00079a70a4:      	blr	x8
fffffe00079a70a8:      	cbnz	w0, 0xfffffe00079a70bc <_before+0x68>
fffffe00079a70ac:      	ldr	x20, [sp, #8]
fffffe00079a70b0:      	ldr	x8, [x19, #168]
fffffe00079a70b4:      	cmp	x20, x8
fffffe00079a70b8:      	b.hs	0xfffffe00079a70e0 <_before+0x8c>
fffffe00079a70bc:      	mov	w1, #1
fffffe00079a70c0:      	mov	x0, x19
fffffe00079a70c4:      	bl	0xfffffe00079a9104 <_fail>
fffffe00079a70c8:      	mov	w0, #-1
fffffe00079a70cc:      	ldp	x29, x30, [sp, #48]
fffffe00079a70d0:      	ldp	x20, x19, [sp, #32]
fffffe00079a70d4:      	ldp	x22, x21, [sp, #16]
fffffe00079a70d8:      	add	sp, sp, #64
fffffe00079a70dc:      	ret
fffffe00079a70e0:      	ldr	w8, [x19, #920]
fffffe00079a70e4:      	str	x20, [x19, #168]
fffffe00079a70e8:      	cbnz	w8, 0xfffffe00079a70f8 <_before+0xa4>
fffffe00079a70ec:      	ldr	x8, [x19, #176]
fffffe00079a70f0:      	cmp	x20, x8
fffffe00079a70f4:      	b.hs	0xfffffe00079a7154 <_before+0x100>
fffffe00079a70f8:      	ldr	w8, [x19, #56]
fffffe00079a70fc:      	cbnz	w8, 0xfffffe00079a7108 <_before+0xb4>
fffffe00079a7100:      	ldr	w8, [x19, #60]
fffffe00079a7104:      	cbz	w8, 0xfffffe00079a7128 <_before+0xd4>
fffffe00079a7108:      	ldr	w8, [x19, #132]
fffffe00079a710c:      	cbz	w8, 0xfffffe00079a7128 <_before+0xd4>
fffffe00079a7110:      	ldr	x8, [x19, #184]
fffffe00079a7114:      	cmp	x20, x8
fffffe00079a7118:      	b.lo	0xfffffe00079a7128 <_before+0xd4>
fffffe00079a711c:      	mov	x0, x19
fffffe00079a7120:      	bl	0xfffffe00079a7e48 <_retired_air_failure>
fffffe00079a7124:      	cbz	w0, 0xfffffe00079a715c <_before+0x108>
fffffe00079a7128:      	ldrb	w8, [x19, #985]
fffffe00079a712c:      	cbz	w8, 0xfffffe00079a714c <_before+0xf8>
fffffe00079a7130:      	ldr	x8, [x19, #976]
fffffe00079a7134:      	mov	w0, wzr
fffffe00079a7138:      	cbz	x8, 0xfffffe00079a70cc <_before+0x78>
fffffe00079a713c:      	cmp	x20, x8
fffffe00079a7140:      	b.lo	0xfffffe00079a70cc <_before+0x78>
fffffe00079a7144:      	ldr	w8, [x19, #24]
fffffe00079a7148:      	cbz	w8, 0xfffffe00079a7164 <_before+0x110>
fffffe00079a714c:      	mov	w0, wzr
fffffe00079a7150:      	b	0xfffffe00079a70cc <_before+0x78>
fffffe00079a7154:      	mov	w1, #2
fffffe00079a7158:      	b	0xfffffe00079a70c0 <_before+0x6c>
fffffe00079a715c:      	mov	w1, #3
fffffe00079a7160:      	b	0xfffffe00079a70c0 <_before+0x6c>
fffffe00079a7164:      	mov	w1, #4
fffffe00079a7168:      	b	0xfffffe00079a70c0 <_before+0x6c>
