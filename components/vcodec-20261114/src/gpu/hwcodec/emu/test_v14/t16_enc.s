@ SPDX-License-Identifier: MIT
@ V14 test encodings.  Every line that carries "@ E_<name>" is a 16-bit T32
@ instruction whose encoding is extracted by run_tests.sh (llvm-mc -> object ->
@ llvm-objdump); no encoding is ever written by hand.  Unannotated instructions
@ (nop) are branch targets for the CBZ/CBNZ/B tests.  The parser asserts that
@ every instruction in this file assembles to exactly two bytes.
	.syntax unified
	.thumb
	.arch armv8-a

@ ---- shift (immediate) ----
	lsls	r0, r1, #0		@ E_LSL0
	lsls	r0, r1, #1		@ E_LSL1
	lsls	r0, r1, #31		@ E_LSL31
	lsls	r7, r3, #4		@ E_LSL4B
	lsrs	r0, r1, #1		@ E_LSR1
	lsrs	r0, r1, #31		@ E_LSR31
	lsrs	r0, r1, #32		@ E_LSR32
	lsrs	r6, r2, #8		@ E_LSR8B
	asrs	r0, r1, #1		@ E_ASR1
	asrs	r0, r1, #31		@ E_ASR31
	asrs	r0, r1, #32		@ E_ASR32
	asrs	r5, r7, #16		@ E_ASR16B

@ ---- add/subtract register and imm3 ----
	adds	r0, r1, r2		@ E_ADDREG
	adds	r3, r4, r5		@ E_ADDREG2
	subs	r0, r1, r2		@ E_SUBREG
	subs	r6, r7, r0		@ E_SUBREG2
	adds	r0, r1, #3		@ E_ADD3
	adds	r7, r6, #7		@ E_ADD7B
	subs	r0, r1, #3		@ E_SUB3
	subs	r5, r4, #7		@ E_SUB7B

@ ---- mov/cmp/add/sub immediate 8 ----
	movs	r0, #0x42		@ E_MOV42
	movs	r7, #0			@ E_MOV0B
	movs	r3, #0xff		@ E_MOVFFB
	cmp	r0, #0x42		@ E_CMP42
	cmp	r7, #0x80		@ E_CMP80B
	adds	r0, #0x42		@ E_ADD42
	adds	r6, #0xff		@ E_ADDFFB
	subs	r0, #0x42		@ E_SUB42
	subs	r5, #0x80		@ E_SUB80B

@ ---- data-processing (two low registers) ----
	ands	r0, r1			@ E_AND
	ands	r7, r5			@ E_AND2
	eors	r0, r1			@ E_EOR
	eors	r3, r6			@ E_EOR2
	lsls	r0, r1			@ E_LSLREG
	lsrs	r0, r1			@ E_LSRREG
	asrs	r0, r1			@ E_ASRREG
	adcs	r0, r1			@ E_ADC
	sbcs	r0, r1			@ E_SBC
	rors	r0, r1			@ E_ROR
	tst	r0, r1			@ E_TST
	rsbs	r0, r1, #0		@ E_RSB
	cmp	r0, r1			@ E_CMPREG
	cmn	r0, r1			@ E_CMN
	orrs	r0, r1			@ E_ORR
	muls	r0, r1, r0		@ E_MUL
	muls	r7, r2, r7		@ E_MUL2
	bics	r0, r1			@ E_BIC
	mvns	r0, r1			@ E_MVN
	eors	r7, r2			@ E_EOR3

@ ---- two high registers / branch and exchange ----
	add	r8, r9			@ E_ADDHH
	add	r0, r8			@ E_ADDLH
	add	r8, r0			@ E_ADDHL
	add	r0, sp			@ E_ADDSP1
	add	sp, r0			@ E_ADDSP2
	add	r8, sp			@ E_ADDSP3
	add	sp, r8			@ E_ADDSP4
	cmp	r8, r9			@ E_CMPHH
	cmp	r0, r8			@ E_CMPLH
	cmp	r8, r0			@ E_CMPHL
	cmp	r0, sp			@ E_CMPSP
	mov	r8, r9			@ E_MOVHH
	mov	r0, r8			@ E_MOVLH
	mov	r8, r0			@ E_MOVHL
	mov	r0, sp			@ E_MOVSP1
	mov	sp, r0			@ E_MOVSP2
	mov	pc, r8			@ E_MOVPC
	add	pc, r8			@ E_ADDPC
	bx	r9			@ E_BX
	blx	r9			@ E_BLX
	bx	pc			@ E_BXPC

@ ---- load/store literal ----
	ldr	r0, [pc, #16]		@ E_LDRLIT16
	ldr	r4, [pc, #4]		@ E_LDRLIT4B
	ldr	r7, [pc, #1020]		@ E_LDRLIT1020B
	ldr	r2, [pc, #0]		@ E_LDRLIT0B

@ ---- load/store register offset ----
	str	r0, [r1, r2]		@ E_STRREG
	str	r7, [r6, r0]		@ E_STRREG2
	strh	r0, [r1, r2]		@ E_STRHREG
	strb	r0, [r1, r2]		@ E_STRBREG
	ldrsb	r0, [r1, r2]		@ E_LDRSBREG
	ldr	r0, [r1, r2]		@ E_LDRREG
	ldrh	r0, [r1, r2]		@ E_LDRHREG
	ldrb	r0, [r1, r2]		@ E_LDRBREG
	ldrsh	r0, [r1, r2]		@ E_LDRSHREG

@ ---- load/store immediate offset ----
	str	r0, [r1, #0]		@ E_STRIMM0
	str	r0, [r1, #124]		@ E_STRIMM124
	str	r7, [r6, #8]		@ E_STRIMM8B
	ldr	r0, [r1, #0]		@ E_LDRIMM0
	ldr	r3, [r5, #124]		@ E_LDRIMM124B
	strb	r0, [r1, #3]		@ E_STRBIMM3
	strb	r7, [r1, #31]		@ E_STRBIMM31B
	ldrb	r0, [r1, #3]		@ E_LDRBIMM3
	ldrb	r2, [r4, #31]		@ E_LDRBIMM31B
	strh	r0, [r1, #6]		@ E_STRHIMM6
	strh	r5, [r2, #62]		@ E_STRHIMM62B
	ldrh	r0, [r1, #6]		@ E_LDRHIMM6
	ldrh	r6, [r7, #62]		@ E_LDRHIMM62B

@ ---- load/store SP-relative ----
	str	r0, [sp, #0]		@ E_STRSP0
	str	r0, [sp, #1020]		@ E_STRSP1020
	ldr	r0, [sp, #0]		@ E_LDRSP0
	ldr	r7, [sp, #1020]		@ E_LDRSP1020B
	ldr	r3, [sp, #4]		@ E_LDRSP4B

@ ---- adr / add sp ----
	adr	r0, #0			@ E_ADR0
	adr	r3, #4			@ E_ADR4B
	adr	r7, #1020		@ E_ADR1020B
	add	r0, sp, #16		@ E_ADDSPIMM16
	add	r7, sp, #1020		@ E_ADDSPIMM1020B
	add	sp, #12		@ E_ADDSP12
	add	sp, #508		@ E_ADDSP508B
	sub	sp, #12		@ E_SUBSP12
	sub	sp, #508		@ E_SUBSP508B

@ ---- extend and reverse bytes ----
	sxth	r0, r1			@ E_SXTH
	sxth	r7, r5			@ E_SXTH2
	sxtb	r0, r1			@ E_SXTB
	sxtb	r3, r6			@ E_SXTB2
	uxth	r0, r1			@ E_UXTH
	uxth	r2, r4			@ E_UXTH2
	uxtb	r0, r1			@ E_UXTB
	uxtb	r6, r7			@ E_UXTB2
	rev	r0, r1			@ E_REV
	rev	r7, r2			@ E_REV2
	rev16	r0, r1			@ E_REV16
	rev16	r5, r3			@ E_REV16B
	revsh	r0, r1			@ E_REVSH
	revsh	r4, r6			@ E_REVSHB

@ ---- compare and branch on zero ----
	cbz	r0, 1f			@ E_CBZ0
	nop
1:
	cbz	r0, 2f			@ E_CBZ2
	nop
	nop
2:
	cbnz	r5, 3f			@ E_CBNZ0
	nop
3:
	cbnz	r7, 4f			@ E_CBNZ2
	nop
	nop
4:

@ ---- push/pop ----
	push	{r0}			@ E_PUSH1
	push	{r0, r2, r7}		@ E_PUSH3
	push	{r0-r7, lr}		@ E_PUSHALL
	push	{lr}			@ E_PUSHLR
	push	{r1, r3, r5}		@ E_PUSHSPARSE
	pop	{r0}			@ E_POP1
	pop	{r0, r2, r7}		@ E_POP3
	pop	{r0-r7, pc}		@ E_POPALL
	pop	{pc}			@ E_POPPC
	pop	{r1, r3, r5}		@ E_POPSPARSE

@ ---- breakpoint, IT, hints, exceptions ----
	bkpt	#0x12			@ E_BKPT
	bkpt	#0			@ E_BKPT0
	it	eq			@ E_ITEQ
	moveq	r0, r1
	itt	eq			@ E_ITTEQ
	moveq	r0, r1
	moveq	r2, r3
	ite	eq			@ E_ITEEQ
	moveq	r0, r1
	movne	r2, r3
	ittt	eq			@ E_ITTTEQ
	moveq	r0, r1
	moveq	r2, r3
	moveq	r4, r5
	itte	eq			@ E_ITTEEQ
	moveq	r0, r1
	moveq	r2, r3
	movne	r4, r5
	it	ne			@ E_ITNE
	movne	r0, r1
	it	gt			@ E_ITGT
	movgt	r0, r1
	nop				@ E_NOP
	yield				@ E_YIELD
	wfe				@ E_WFE
	wfi				@ E_WFI
	sev				@ E_SEV
	sevl				@ E_SEVL
	svc	#0x34			@ E_SVC
	udf	#0x56			@ E_UDF

@ ---- unconditional and conditional branches ----
	b.n	1f			@ E_B0
	nop
1:
	b.n	2f			@ E_B2
	nop
	nop
2:
1:
	nop
	b.n	1b			@ E_BBACK
	beq	1f			@ E_BEQ
	nop
1:
	bne	1f			@ E_BNE
	nop
1:
	bcs	1f			@ E_BCS
	nop
1:
	bcc	1f			@ E_BCC
	nop
1:
	bmi	1f			@ E_BMI
	nop
1:
	bpl	1f			@ E_BPL
	nop
1:
	bvs	1f			@ E_BVS
	nop
1:
	bvc	1f			@ E_BVC
	nop
1:
	bhi	1f			@ E_BHI
	nop
1:
	bls	1f			@ E_BLS
	nop
1:
	bge	1f			@ E_BGE
	nop
1:
	blt	1f			@ E_BLT
	nop
1:
	bgt	1f			@ E_BGT
	nop
1:
	ble	1f			@ E_BLE
	nop
1:
	beq	1f			@ E_BEQ2
	nop
	nop
1:
1:
	nop
	beq	1b			@ E_BEQBACK

@ ---- load/store multiple ----
	stm	r0!, {r1, r2}		@ E_STM
	ldm	r0!, {r1, r2}		@ E_LDM
	stm	r3!, {r0, r7}		@ E_STM2
	ldm	r5!, {r0-r3}		@ E_LDM2
	stm	r7!, {r0-r6}		@ E_STM3

@ ---- additional operand variants (coverage) ----
	mov	r0, pc			@ E_MOVPCVAL
	add	r0, pc			@ E_ADDPCVAL
	add	r1, sp			@ E_ADDSP5
	mov	r1, sp			@ E_MOVSP3
	cmp	r1, sp			@ E_CMPSP2
	str	r3, [sp, #4]		@ E_STRSP4B
	ldr	r1, [sp, #8]		@ E_LDRSP8B
	adr	r1, #8			@ E_ADR8B
	adr	r6, #16			@ E_ADR16B
	push	{r2}			@ E_PUSH1B
	pop	{r7}			@ E_POP1B
	rev	r3, r4			@ E_REV3
	rev16	r2, r6			@ E_REV16C
	revsh	r1, r5			@ E_REVSHC
	sxth	r2, r3			@ E_SXTH3
	sxtb	r5, r0			@ E_SXTB3
	uxth	r6, r1			@ E_UXTH3
	uxtb	r4, r2			@ E_UXTB3
	cbz	r3, 1f			@ E_CBZ3
	nop
1:
	cbnz	r3, 1f			@ E_CBNZ3
	nop
1:
	ldr	r5, [r6, r7]		@ E_LDRREG2
	strh	r2, [r3, r4]		@ E_STRHREG2
	strb	r6, [r7, r0]		@ E_STRBREG2
	ldrh	r4, [r5, r6]		@ E_LDRHREG2
	ldrb	r1, [r2, r3]		@ E_LDRBREG2
	str	r2, [r3, #4]		@ E_STRIMM4B
	ldr	r6, [r7, #12]		@ E_LDRIMM12B
	strb	r2, [r3, #7]		@ E_STRBIMM7B
	ldrb	r6, [r7, #9]		@ E_LDRBIMM9B
	strh	r2, [r3, #10]		@ E_STRHIMM10B
	ldrh	r4, [r5, #12]		@ E_LDRHIMM12B
	itt	al			@ E_ITTAL
	moval	r0, r1
	moval	r2, r3
	blx	r3			@ E_BLX2
	bx	r2			@ E_BX2
	add	r5, r6			@ E_ADDHH2
	cmp	r5, r6			@ E_CMPHH2
	mov	r5, r6			@ E_MOVHH2
	add	sp, #4			@ E_ADDSP4B
	sub	sp, #4			@ E_SUBSP4B
	add	r1, sp, #4		@ E_ADDSPIMM4B
	stm	r1!, {r0, r3}		@ E_STM4
	ldm	r2!, {r5, r6}		@ E_LDM4
	movs	r4, #0x80		@ E_MOV80B
	cmp	r4, #0			@ E_CMP0B
	adds	r4, #1			@ E_ADD1B
	subs	r4, #1			@ E_SUB1B
