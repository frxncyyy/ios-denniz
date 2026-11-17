
$NORD2_REPO/research/ci-39924876139/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe00079b391c <_nsdd_pid_finish>:
fffffe00079b391c:      	sub	sp, sp, #144
fffffe00079b3920:      	stp	x24, x23, [sp, #80]
fffffe00079b3924:      	stp	x22, x21, [sp, #96]
fffffe00079b3928:      	stp	x20, x19, [sp, #112]
fffffe00079b392c:      	stp	x29, x30, [sp, #128]
fffffe00079b3930:      	add	x29, sp, #128
fffffe00079b3934:      	cbz	x0, 0xfffffe00079b39b0 <_nsdd_pid_finish+0x94>
fffffe00079b3938:      	mov	x19, x0
fffffe00079b393c:      	mov	x21, #-126
fffffe00079b3940:      	mov	w22, #1
fffffe00079b3944:      	adrp	x23, 0xfffffe0008544000 <_cleanup_next+0x174>
fffffe00079b3948:      	add	x23, x23, #3532
fffffe00079b394c:      	adrp	x20, 0xfffffe0007134000 <_group_slot+0x128>
fffffe00079b3950:      	add	x20, x20, #1643
fffffe00079b3954:      	b	0xfffffe00079b3964 <_nsdd_pid_finish+0x48>
fffffe00079b3958:      	adds	x21, x21, #1
fffffe00079b395c:      	add	x23, x23, #72
fffffe00079b3960:      	b.hs	0xfffffe00079b39b0 <_nsdd_pid_finish+0x94>
fffffe00079b3964:      	ldur	x8, [x23, #-68]
fffffe00079b3968:      	cmp	x8, x19
fffffe00079b396c:      	b.ne	0xfffffe00079b3958 <_nsdd_pid_finish+0x3c>
fffffe00079b3970:      	ldr	w8, [x23]
fffffe00079b3974:      	cbnz	w8, 0xfffffe00079b3958 <_nsdd_pid_finish+0x3c>
fffffe00079b3978:      	ldp	w8, w9, [x23, #-56]
fffffe00079b397c:      	ldp	w11, w12, [x23, #-16]
fffffe00079b3980:      	ldp	w13, w15, [x23, #-8]
fffffe00079b3984:      	ldur	w10, [x23, #-24]
fffffe00079b3988:      	add	x14, x21, #127
fffffe00079b398c:      	mov	x0, x20
fffffe00079b3990:      	str	w22, [x23]
fffffe00079b3994:      	stp	x11, x12, [sp, #40]
fffffe00079b3998:      	stp	x13, x15, [sp, #56]
fffffe00079b399c:      	stp	x9, x10, [sp, #24]
fffffe00079b39a0:      	stp	x14, x8, [sp, #8]
fffffe00079b39a4:      	str	x19, [sp]
fffffe00079b39a8:      	bl	0xfffffe0007361ae4 <_printf>
fffffe00079b39ac:      	b	0xfffffe00079b3958 <_nsdd_pid_finish+0x3c>
fffffe00079b39b0:      	ldp	x29, x30, [sp, #128]
fffffe00079b39b4:      	ldp	x20, x19, [sp, #112]
fffffe00079b39b8:      	ldp	x22, x21, [sp, #96]
fffffe00079b39bc:      	ldp	x24, x23, [sp, #80]
fffffe00079b39c0:      	add	sp, sp, #144
fffffe00079b39c4:      	ret
