	.build_version macos, 14, 0
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_acquire                        ; -- Begin function acquire
	.p2align	2
_acquire:                               ; @acquire
	.cfi_startproc
; %bb.0:
	add	x8, x0, #72
	ldar	w0, [x8]
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_release                        ; -- Begin function release
	.p2align	2
_release:                               ; @release
	.cfi_startproc
; %bb.0:
	add	x8, x0, #72
	stlr	w1, [x8]
	ret
	.cfi_endproc
                                        ; -- End function
.subsections_via_symbols
