/* SPDX-License-Identifier: MIT
 * n2mnl_image.S -- MediaTek libmnl.so (flattened by mkimage.py) as part of the signed n2mnld executable. No runtime code
 * generation: the engine's code is in the r-x segment __MNLT, its data (RELRO + RW + bss) in the rw segment __MNLD.
 * libmnl's code reaches its data PC-relatively (adrp), so __MNLD must start exactly ELF 0x576000 after the image base:
 * __MNLT = [0x2000 pad][ELF 0 .. 0x576000) = 0x578000 bytes (16 KiB multiple), __MNLD follows at the next 16 KiB page
 * = base + 0x576000. build_ios.sh checks it on the linked file, n2mnld.c again at run time.
 */
	.section __MNLT,__mnl,regular,pure_instructions
	.p2align 14
	.space 0x2000
	.globl _n2mnl_base
_n2mnl_base:
	.incbin "build/mnl_text.bin"
	.section __MNLD,__mnl
	.p2align 14
	.globl _n2mnl_data
_n2mnl_data:
	.incbin "build/mnl_data.bin"
