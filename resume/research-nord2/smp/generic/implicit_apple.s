	.build_version macos, 14, 0
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_acquire                        ; -- Begin function acquire
	.p2align	2
_acquire:                               ; @acquire
	.cfi_startproc
; %bb.0:
	ldapur	w0, [x0, #72]
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_release                        ; -- Begin function release
	.p2align	2
_release:                               ; @release
	.cfi_startproc
; %bb.0:
	stlur	w1, [x0, #72]
	ret
	.cfi_endproc
                                        ; -- End function
.subsections_via_symbols
