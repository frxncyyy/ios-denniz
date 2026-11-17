
$NORD2_REPO/research/ci-39830672617/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7e48 <_retired_air_failure>:
fffffe00079a7e48:      	ldr	w9, [x0, #128]
fffffe00079a7e4c:      	cmp	w9, #2
fffffe00079a7e50:      	b.eq	0xfffffe00079a7e68 <_retired_air_failure+0x20>
fffffe00079a7e54:      	ldr	w10, [x0, #24]
fffffe00079a7e58:      	mov	w8, wzr
fffffe00079a7e5c:      	cbz	w10, 0xfffffe00079a7e94 <_retired_air_failure+0x4c>
fffffe00079a7e60:      	cmp	w9, #1
fffffe00079a7e64:      	b.ne	0xfffffe00079a7e94 <_retired_air_failure+0x4c>
fffffe00079a7e68:      	ldr	w8, [x0, #60]
fffffe00079a7e6c:      	cbz	w8, 0xfffffe00079a7e94 <_retired_air_failure+0x4c>
fffffe00079a7e70:      	ldr	w8, [x0, #136]
fffffe00079a7e74:      	cbz	w8, 0xfffffe00079a7e94 <_retired_air_failure+0x4c>
fffffe00079a7e78:      	ldr	w8, [x0, #56]
fffffe00079a7e7c:      	cbz	w8, 0xfffffe00079a7e88 <_retired_air_failure+0x40>
fffffe00079a7e80:      	mov	w0, wzr
fffffe00079a7e84:      	ret
fffffe00079a7e88:      	ldp	w8, w9, [x0, #92]
fffffe00079a7e8c:      	cmp	w8, w9
fffffe00079a7e90:      	cset	w8, eq
fffffe00079a7e94:      	mov	w0, w8
fffffe00079a7e98:      	ret
