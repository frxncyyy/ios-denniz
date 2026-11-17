
$NORD2_REPO/research/nord2/wifi/next-stage/main-fw-transport/phy-action/module-evidence/connfem.ko:	file format elf64-littleaarch64

Disassembly of section .text:

0000000000000a9c <connfem_epaelna_get_fem_info>:
     a9c:      	stp	x29, x30, [sp, #-32]!
     aa0:      	str	x19, [sp, #16]
     aa4:      	mov	x29, sp
     aa8:      	cbnz	x0, 0xac8 <connfem_epaelna_get_fem_info+0x2c>
     aac:      	adrp	x0, 0x0 <connfem_epaelna_get_fem_info+0x10>
     ab0:      	adrp	x1, 0x0 <connfem_epaelna_get_fem_info+0x14>
     ab4:      	add	x0, x0, #0
     ab8:      	add	x1, x1, #0
     abc:      	bl	0xabc <connfem_epaelna_get_fem_info+0x20>
     ac0:      	mov	w0, #-22
     ac4:      	b	0xb40 <connfem_epaelna_get_fem_info+0xa4>
     ac8:      	stp	xzr, xzr, [x0, #120]
     acc:      	stp	xzr, xzr, [x0, #104]
     ad0:      	stp	xzr, xzr, [x0, #88]
     ad4:      	stp	xzr, xzr, [x0, #72]
     ad8:      	stp	xzr, xzr, [x0, #56]
     adc:      	stp	xzr, xzr, [x0, #40]
     ae0:      	stp	xzr, xzr, [x0, #24]
     ae4:      	stp	xzr, xzr, [x0, #8]
     ae8:      	str	xzr, [x0]
     aec:      	adrp	x8, 0x0 <connfem_epaelna_get_fem_info+0x50>
     af0:      	ldr	x8, [x8]
     af4:      	mov	x19, x0
     af8:      	cbnz	x8, 0xb18 <connfem_epaelna_get_fem_info+0x7c>
     afc:      	adrp	x0, 0x0 <connfem_epaelna_get_fem_info+0x60>
     b00:      	adrp	x1, 0x0 <connfem_epaelna_get_fem_info+0x64>
     b04:      	add	x0, x0, #0
     b08:      	add	x1, x1, #0
     b0c:      	bl	0xb0c <connfem_epaelna_get_fem_info+0x70>
     b10:      	mov	w0, #-95
     b14:      	b	0xb40 <connfem_epaelna_get_fem_info+0xa4>
     b18:      	add	x1, x8, #260
     b1c:      	mov	w2, #136
     b20:      	mov	x0, x19
     b24:      	bl	0xb24 <connfem_epaelna_get_fem_info+0x88>
     b28:      	adrp	x0, 0x0 <connfem_epaelna_get_fem_info+0x8c>
     b2c:      	add	x0, x0, #0
     b30:      	bl	0xb30 <connfem_epaelna_get_fem_info+0x94>
     b34:      	mov	x0, x19
     b38:      	bl	0xb38 <connfem_epaelna_get_fem_info+0x9c>
     b3c:      	mov	w0, wzr
     b40:      	ldr	x19, [sp, #16]
     b44:      	ldp	x29, x30, [sp], #32
     b48:      	ret

0000000000000b4c <connfem_epaelna_get_pin_info>:
     b4c:      	stp	x29, x30, [sp, #-32]!
     b50:      	str	x19, [sp, #16]
     b54:      	mov	x29, sp
     b58:      	cbnz	x0, 0xb78 <connfem_epaelna_get_pin_info+0x2c>
     b5c:      	adrp	x0, 0x0 <connfem_epaelna_get_pin_info+0x10>
     b60:      	adrp	x1, 0x0 <connfem_epaelna_get_pin_info+0x14>
     b64:      	add	x0, x0, #0
     b68:      	add	x1, x1, #0
     b6c:      	bl	0xb6c <connfem_epaelna_get_pin_info+0x20>
     b70:      	mov	w0, #-22
     b74:      	b	0xc5c <connfem_epaelna_get_pin_info+0x110>
     b78:      	str	wzr, [x0, #96]
     b7c:      	stp	xzr, xzr, [x0, #80]
     b80:      	stp	xzr, xzr, [x0, #64]
     b84:      	stp	xzr, xzr, [x0, #48]
     b88:      	stp	xzr, xzr, [x0, #32]
     b8c:      	stp	xzr, xzr, [x0, #16]
     b90:      	stp	xzr, xzr, [x0]
     b94:      	adrp	x8, 0x0 <connfem_epaelna_get_pin_info+0x48>
     b98:      	ldr	x8, [x8]
     b9c:      	mov	x19, x0
     ba0:      	cbnz	x8, 0xbc0 <connfem_epaelna_get_pin_info+0x74>
     ba4:      	adrp	x0, 0x0 <connfem_epaelna_get_pin_info+0x58>
     ba8:      	adrp	x1, 0x0 <connfem_epaelna_get_pin_info+0x5c>
     bac:      	add	x0, x0, #0
     bb0:      	add	x1, x1, #0
     bb4:      	bl	0xbb4 <connfem_epaelna_get_pin_info+0x68>
     bb8:      	mov	w0, #-95
     bbc:      	b	0xc5c <connfem_epaelna_get_pin_info+0x110>
     bc0:      	add	x9, x8, #396
     bc4:      	ldr	x9, [x9]
     bc8:      	add	x10, x8, #404
     bcc:      	add	x11, x8, #412
     bd0:      	add	x12, x8, #420
     bd4:      	add	x13, x8, #428
     bd8:      	str	x9, [x19]
     bdc:      	ldr	x13, [x13]
     be0:      	ldr	x12, [x12]
     be4:      	ldr	x10, [x10]
     be8:      	ldr	x11, [x11]
     bec:      	add	x14, x8, #436
     bf0:      	add	x15, x8, #444
     bf4:      	add	x16, x8, #452
     bf8:      	add	x9, x8, #460
     bfc:      	stp	x12, x13, [x19, #24]
     c00:      	stp	x10, x11, [x19, #8]
     c04:      	ldr	x9, [x9]
     c08:      	ldr	x11, [x16]
     c0c:      	ldr	x13, [x14]
     c10:      	ldr	x14, [x15]
     c14:      	add	x12, x8, #468
     c18:      	add	x10, x8, #476
     c1c:      	stp	x11, x9, [x19, #56]
     c20:      	add	x9, x8, #484
     c24:      	stp	x13, x14, [x19, #40]
     c28:      	ldr	w8, [x8, #492]
     c2c:      	ldr	x9, [x9]
     c30:      	ldr	x10, [x10]
     c34:      	ldr	x11, [x12]
     c38:      	adrp	x0, 0x0 <connfem_epaelna_get_pin_info+0xec>
     c3c:      	add	x0, x0, #0
     c40:      	str	w8, [x19, #96]
     c44:      	stp	x10, x9, [x19, #80]
     c48:      	str	x11, [x19, #72]
     c4c:      	bl	0xc4c <connfem_epaelna_get_pin_info+0x100>
     c50:      	mov	x0, x19
     c54:      	bl	0xc54 <connfem_epaelna_get_pin_info+0x108>
     c58:      	mov	w0, wzr
     c5c:      	ldr	x19, [sp, #16]
     c60:      	ldp	x29, x30, [sp], #32
     c64:      	ret

0000000000000c68 <connfem_epaelna_laa_get_pin_info>:
     c68:      	stp	x29, x30, [sp, #-32]!
     c6c:      	str	x19, [sp, #16]
     c70:      	mov	x29, sp
     c74:      	cbnz	x0, 0xc94 <connfem_epaelna_laa_get_pin_info+0x2c>
     c78:      	adrp	x0, 0x0 <connfem_epaelna_laa_get_pin_info+0x10>
     c7c:      	adrp	x1, 0x0 <connfem_epaelna_laa_get_pin_info+0x14>
     c80:      	add	x0, x0, #0
     c84:      	add	x1, x1, #0
     c88:      	bl	0xc88 <connfem_epaelna_laa_get_pin_info+0x20>
     c8c:      	mov	w0, #-22
     c90:      	b	0xd0c <connfem_epaelna_laa_get_pin_info+0xa4>
     c94:      	stp	xzr, xzr, [x0, #24]
     c98:      	stp	xzr, xzr, [x0, #8]
     c9c:      	str	xzr, [x0]
     ca0:      	adrp	x8, 0x0 <connfem_epaelna_laa_get_pin_info+0x38>
     ca4:      	ldr	x9, [x8]
     ca8:      	mov	x19, x0
     cac:      	cbnz	x9, 0xccc <connfem_epaelna_laa_get_pin_info+0x64>
     cb0:      	adrp	x0, 0x0 <connfem_epaelna_laa_get_pin_info+0x48>
     cb4:      	adrp	x1, 0x0 <connfem_epaelna_laa_get_pin_info+0x4c>
     cb8:      	add	x0, x0, #0
     cbc:      	add	x1, x1, #0
     cc0:      	bl	0xcc0 <connfem_epaelna_laa_get_pin_info+0x58>
     cc4:      	mov	w0, #-95
     cc8:      	b	0xd0c <connfem_epaelna_laa_get_pin_info+0xa4>
     ccc:      	ldr	x10, [x9, #496]
     cd0:      	adrp	x0, 0x0 <connfem_epaelna_laa_get_pin_info+0x68>
     cd4:      	add	x0, x0, #0
     cd8:      	str	x10, [x19]
     cdc:      	ldr	x10, [x9, #528]
     ce0:      	ldr	x11, [x9, #520]
     ce4:      	ldp	x9, x12, [x9, #504]
     ce8:      	stp	x11, x10, [x19, #24]
     cec:      	stp	x9, x12, [x19, #8]
     cf0:      	ldr	x8, [x8]
     cf4:      	ldr	w8, [x8]
     cf8:      	str	w8, [x19]
     cfc:      	bl	0xcfc <connfem_epaelna_laa_get_pin_info+0x94>
     d00:      	mov	x0, x19
     d04:      	bl	0xd04 <connfem_epaelna_laa_get_pin_info+0x9c>
     d08:      	mov	w0, wzr
     d0c:      	ldr	x19, [sp, #16]
     d10:      	ldp	x29, x30, [sp], #32
     d14:      	ret

# Only .rela.text relocations inside selected functions
0000000000000aac  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 388
0000000000000ab0  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 905
0000000000000ab4  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 388
0000000000000ab8  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 905
0000000000000abc  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000aec  0000008a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000008 connfem_ctx + 0
0000000000000af0  0000008a0000011e R_AARCH64_LDST64_ABS_LO12_NC 0000000000000008 connfem_ctx + 0
0000000000000afc  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + e91
0000000000000b00  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 905
0000000000000b04  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + e91
0000000000000b08  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 905
0000000000000b0c  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000b24  000000aa0000011b R_AARCH64_CALL26       0000000000000000 memcpy + 0
0000000000000b28  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 689
0000000000000b2c  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 689
0000000000000b30  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000b38  000000a10000011b R_AARCH64_CALL26       000000000000247c cfm_epaelna_feminfo_dump + 0
0000000000000b5c  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 388
0000000000000b60  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 6a4
0000000000000b64  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 388
0000000000000b68  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 6a4
0000000000000b6c  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000b94  0000008a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000008 connfem_ctx + 0
0000000000000b98  0000008a0000011e R_AARCH64_LDST64_ABS_LO12_NC 0000000000000008 connfem_ctx + 0
0000000000000ba4  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + e91
0000000000000ba8  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 6a4
0000000000000bac  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + e91
0000000000000bb0  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 6a4
0000000000000bb4  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000c38  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + b93
0000000000000c3c  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + b93
0000000000000c4c  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000c54  000000a50000011b R_AARCH64_CALL26       0000000000002f90 cfm_epaelna_pininfo_dump + 0
0000000000000c78  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 388
0000000000000c7c  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 1163
0000000000000c80  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 388
0000000000000c84  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 1163
0000000000000c88  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000ca0  0000008a00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000008 connfem_ctx + 0
0000000000000ca4  0000008a0000011e R_AARCH64_LDST64_ABS_LO12_NC 0000000000000008 connfem_ctx + 0
0000000000000cb0  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + e91
0000000000000cb4  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 1163
0000000000000cb8  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + e91
0000000000000cbc  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 1163
0000000000000cc0  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000cd0  0000007400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 5c7
0000000000000cd4  0000007400000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata.str1.1 + 5c7
0000000000000cf0  0000008a0000011e R_AARCH64_LDST64_ABS_LO12_NC 0000000000000008 connfem_ctx + 0
0000000000000cfc  000000990000011b R_AARCH64_CALL26       0000000000000000 printk + 0
0000000000000d04  000000a40000011b R_AARCH64_CALL26       000000000000326c cfm_epaelna_laainfo_dump + 0
