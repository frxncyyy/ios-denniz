
$NORD2_REPO/research/ci-39841085457/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079a7848 <_nst_eapol>:
fffffe00079a7848:      	cbz	x0, 0xfffffe00079a786c <_nst_eapol+0x24>
fffffe00079a784c:      	mov	w5, w4
fffffe00079a7850:      	cmp	w4, #1
fffffe00079a7854:      	b.hi	0xfffffe00079a786c <_nst_eapol+0x24>
fffffe00079a7858:      	cbz	w5, 0xfffffe00079a7864 <_nst_eapol+0x1c>
fffffe00079a785c:      	ldr	w8, [x0, #928]
fffffe00079a7860:      	cbz	w8, 0xfffffe00079a786c <_nst_eapol+0x24>
fffffe00079a7864:      	mov	w4, #2
fffffe00079a7868:      	b	0xfffffe00079a7338 <_frame>
fffffe00079a786c:      	mov	w0, #-1
fffffe00079a7870:      	ret
