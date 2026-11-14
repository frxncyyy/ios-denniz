100009634:     	sub	sp, sp, #0xe0
100009638:     	stp	d11, d10, [sp, #0x60]
10000963c:     	stp	d9, d8, [sp, #0x70]
100009640:     	stp	x28, x27, [sp, #0x80]
100009644:     	stp	x26, x25, [sp, #0x90]
100009648:     	stp	x24, x23, [sp, #0xa0]
10000964c:     	stp	x22, x21, [sp, #0xb0]
100009650:     	stp	x20, x19, [sp, #0xc0]
100009654:     	stp	x29, x30, [sp, #0xd0]
100009658:     	add	x29, sp, #0xd0
10000965c:     	mov	x20, x3
100009660:     	mov	x22, x0
100009664:     	nop
100009668:     	ldr	x8, 0x1000fc508 
10000966c:     	ldr	x8, [x8]
100009670:     	str	x8, [sp, #0x58]
100009674:     	bl	0x1000abe70 
100009678:     	mov	x28, x0
10000967c:     	bl	0x1000abe7c 
100009680:     	mov	x20, x0
100009684:     	ldr	x0, [x22, #0x58]
100009688:     	mov	x2, x20
10000968c:     	bl	0x1000b5160    ; sel objectForKeyedSubscript:
100009690:     	bl	0x1000abccc 
100009694:     	mov	x21, x0
100009698:     	bl	0x1000abda4 
10000969c:     	cbz	x21, 0x1000097c0 <__text+0x3770>
1000096a0:     	ldr	x0, [x22, #0x50]
1000096a4:     	mov	x2, x20
1000096a8:     	bl	0x1000b5160    ; sel objectForKeyedSubscript:
1000096ac:     	bl	0x1000abccc 
1000096b0:     	mov	x21, x0
1000096b4:     	mov	x0, x22
1000096b8:     	mov	x2, x28
1000096bc:     	mov	x3, x21
1000096c0:     	bl	0x1000aee00    ; sel computeSlotForDate:relativeToDate:
1000096c4:     	cmp	w0, #0x7
1000096c8:     	b.gt	0x100009768 <__text+0x3718>
1000096cc:     	mov	x23, x0
1000096d0:     	ldr	x0, [x22, #0x58]
1000096d4:     	mov	x2, x20
1000096d8:     	bl	0x1000b5160    ; sel objectForKeyedSubscript:
1000096dc:     	bl	0x1000abccc 
1000096e0:     	mov	x24, x0
1000096e4:     	sxtw	x2, w23
1000096e8:     	bl	0x1000b50e0    ; sel objectAtIndex:
1000096ec:     	bl	0x1000abccc 
1000096f0:     	mov	x23, x0
1000096f4:     	bl	0x1000b05a0    ; sel doubleValue
1000096f8:     	fmov	d8, d0
1000096fc:     	bl	0x1000abdbc 
100009700:     	bl	0x1000abdc8 
100009704:     	ldr	x22, [x22, #0x60]
100009708:     	mov	x0, x22
10000970c:     	mov	w1, #0x1                ; =1
100009710:     	bl	0x1000abf9c 
100009714:     	cbz	w0, 0x100009c9c <__text+0x3c4c>
100009718:     	mov	w8, #0x302              ; =770
10000971c:     	movk	w8, #0x840, lsl #16
100009720:     	str	w8, [sp, #0x20]
100009724:     	stur	x20, [sp, #0x24]
100009728:     	mov	w8, #0x840              ; =2112
10000972c:     	strh	w8, [sp, #0x2c]
100009730:     	stur	x21, [sp, #0x2e]
100009734:     	mov	w8, #0x800              ; =2048
100009738:     	strh	w8, [sp, #0x36]
10000973c:     	str	d8, [sp, #0x38]
100009740:     	adr	x0, 0x100000000    ; "����"
100009744:     	nop
100009748:     	adr	x3, 0x1000ec8c4    ; "Cached budget allocation prediction for widget %@ available from %@ : "
10000974c:     	nop
100009750:     	add	x4, sp, #0x20
100009754:     	mov	x1, x22
100009758:     	mov	w2, #0x1                ; =1
10000975c:     	mov	w5, #0x20               ; =32
100009760:     	bl	0x1000ab978 
100009764:     	b	0x100009c9c <__text+0x3c4c>
100009768:     	ldr	x23, [x22, #0x60]
10000976c:     	mov	x0, x23
100009770:     	mov	w1, #0x1                ; =1
100009774:     	bl	0x1000abf9c 
100009778:     	cbz	w0, 0x1000097bc <__text+0x376c>
10000977c:     	mov	w8, #0x202              ; =514
100009780:     	movk	w8, #0x840, lsl #16
100009784:     	str	w8, [sp, #0x20]
100009788:     	stur	x20, [sp, #0x24]
10000978c:     	mov	w8, #0x840              ; =2112
100009790:     	strh	w8, [sp, #0x2c]
100009794:     	stur	x21, [sp, #0x2e]
100009798:     	adr	x0, 0x100000000    ; "����"
10000979c:     	nop
1000097a0:     	adr	x3, 0x1000ec91f    ; "Cached budget allocation prediction for widget %@ too old, start at %@"
1000097a4:     	nop
1000097a8:     	add	x4, sp, #0x20
1000097ac:     	mov	x1, x23
1000097b0:     	mov	w2, #0x1                ; =1
1000097b4:     	mov	w5, #0x16               ; =22
1000097b8:     	bl	0x1000ab978 
1000097bc:     	bl	0x1000abda4 
1000097c0:     	mov	x19, x22
1000097c4:     	ldr	x8, [x19, #0x48]!
1000097c8:     	cbz	x8, 0x100009854 <__text+0x3804>
1000097cc:     	bl	0x1000abf0c 
1000097d0:     	mov	x21, x0
1000097d4:     	mov	x2, x28
1000097d8:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
1000097dc:     	nop
1000097e0:     	ldr	d9, 0x1000c6380 
1000097e4:     	fcmp	d0, d9
1000097e8:     	b.pl	0x100009824 <__text+0x37d4>
1000097ec:     	nop
1000097f0:     	ldr	d8, 0x1000c6360 
1000097f4:     	mov	x23, x21
1000097f8:     	mov	x0, x21
1000097fc:     	fmov	d0, d8
100009800:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100009804:     	bl	0x1000abccc 
100009808:     	mov	x21, x0
10000980c:     	bl	0x1000abdbc 
100009810:     	mov	x0, x21
100009814:     	mov	x2, x28
100009818:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
10000981c:     	fcmp	d0, d9
100009820:     	b.mi	0x1000097f4 <__text+0x37a4>
100009824:     	ldr	x0, [x22, #0x50]
100009828:     	mov	x2, x20
10000982c:     	bl	0x1000b5160    ; sel objectForKeyedSubscript:
100009830:     	bl	0x1000abccc 
100009834:     	mov	x23, x0
100009838:     	bl	0x1000abdbc 
10000983c:     	cbz	x23, 0x1000098a0 <__text+0x3850>
100009840:     	ldr	x0, [x22, #0x50]
100009844:     	mov	x2, x20
100009848:     	bl	0x1000b5160    ; sel objectForKeyedSubscript:
10000984c:     	bl	0x1000abccc 
100009850:     	b	0x1000098b0 <__text+0x3860>
100009854:     	adrp	x8, 0x100132000 
100009858:     	ldr	x21, [x8, #0xb10]
10000985c:     	mov	x0, x21
100009860:     	bl	0x1000b4ec0    ; sel now
100009864:     	bl	0x1000abccc 
100009868:     	mov	x23, x0
10000986c:     	fmov	d0, #-5.00000000
100009870:     	mov	x0, x21
100009874:     	mov	x2, x23
100009878:     	bl	0x1000afa00    ; sel dateWithTimeInterval:sinceDate:
10000987c:     	bl	0x1000abccc 
100009880:     	mov	x21, x0
100009884:     	bl	0x1000abdbc 
100009888:     	fmov	d0, xzr
10000988c:     	mov	x0, x21
100009890:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100009894:     	bl	0x1000abccc 
100009898:     	mov	x23, x0
10000989c:     	b	0x100009940 <__text+0x38f0>
1000098a0:     	fmov	d0, xzr
1000098a4:     	mov	x0, x21
1000098a8:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
1000098ac:     	bl	0x1000abccc 
1000098b0:     	mov	x25, x0
1000098b4:     	mov	x2, x28
1000098b8:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
1000098bc:     	nop
1000098c0:     	ldr	d8, 0x1000c6388 
1000098c4:     	fcmp	d0, d8
1000098c8:     	b.pl	0x100009908 <__text+0x38b8>
1000098cc:     	nop
1000098d0:     	ldr	d9, 0x1000c6390 
1000098d4:     	mov	x0, x25
1000098d8:     	fmov	d0, d9
1000098dc:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
1000098e0:     	bl	0x1000abccc 
1000098e4:     	mov	x23, x0
1000098e8:     	bl	0x1000abdd4 
1000098ec:     	mov	x0, x23
1000098f0:     	mov	x2, x28
1000098f4:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
1000098f8:     	fcmp	d0, d8
1000098fc:     	mov	x25, x23
100009900:     	b.mi	0x1000098d4 <__text+0x3884>
100009904:     	b	0x10000992c <__text+0x38dc>
100009908:     	mov	x23, x25
10000990c:     	b	0x10000992c <__text+0x38dc>
100009910:     	mov	x25, x23
100009914:     	mov	x0, x23
100009918:     	fmov	d0, d8
10000991c:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100009920:     	bl	0x1000abccc 
100009924:     	mov	x23, x0
100009928:     	bl	0x1000abdd4 
10000992c:     	mov	x0, x23
100009930:     	mov	x2, x28
100009934:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
100009938:     	fcmp	d0, #0.0
10000993c:     	b.gt	0x100009910 <__text+0x38c0>
100009940:     	mov	x0, x22
100009944:     	mov	x2, x20
100009948:     	mov	x3, x21
10000994c:     	bl	0x1000bb8e0    ; sel usageTimelineForWidgetBudgetID:withStartDate:
100009950:     	bl	0x1000abccc 
100009954:     	mov	x25, x0
100009958:     	bl	0x1000b3780    ; sel isUnavailable
10000995c:     	cbz	w0, 0x1000099cc <__text+0x397c>
100009960:     	ldr	x22, [x22, #0x60]
100009964:     	mov	x0, x22
100009968:     	mov	w1, #0x1                ; =1
10000996c:     	bl	0x1000abf9c 
100009970:     	cbz	w0, 0x1000099a8 <__text+0x3958>
100009974:     	mov	w8, #0x102              ; =258
100009978:     	movk	w8, #0x840, lsl #16
10000997c:     	str	w8, [sp, #0x20]
100009980:     	stur	x20, [sp, #0x24]
100009984:     	adr	x0, 0x100000000    ; "����"
100009988:     	nop
10000998c:     	adr	x3, 0x1000ec966    ; "Budget allocation predictions unavailable for widget %@, returning eve"
100009990:     	nop
100009994:     	add	x4, sp, #0x20
100009998:     	mov	x1, x22
10000999c:     	mov	w2, #0x1                ; =1
1000099a0:     	mov	w5, #0xc                ; =12
1000099a4:     	bl	0x1000ab978 
1000099a8:     	adrp	x8, 0x100137000 
1000099ac:     	ldrb	w8, [x8, #0xc78]
1000099b0:     	cmp	w8, #0x0
1000099b4:     	mov	x8, #0x7ff0000000000000 ; =9218868437227405312
1000099b8:     	fmov	d0, x8
1000099bc:     	nop
1000099c0:     	ldr	d1, 0x1000c6368 
1000099c4:     	fcsel	d8, d1, d0, ne
1000099c8:     	b	0x100009c94 <__text+0x3c44>
1000099cc:     	stp	x19, x28, [sp, #0x8]
1000099d0:     	mov	x0, x25
1000099d4:     	bl	0x1000ba240    ; sel startDate
1000099d8:     	bl	0x1000abccc 
1000099dc:     	mov	x26, x0
1000099e0:     	mov	x0, x25
1000099e4:     	mov	x2, x26
1000099e8:     	bl	0x1000bbaa0    ; sel valueAtDate:
1000099ec:     	bl	0x1000abccc 
1000099f0:     	mov	x27, x0
1000099f4:     	bl	0x1000b05a0    ; sel doubleValue
1000099f8:     	fmov	d8, d0
1000099fc:     	bl	0x1000abdec 
100009a00:     	bl	0x1000abde0 
100009a04:     	mov	x0, x25
100009a08:     	bl	0x1000ba240    ; sel startDate
100009a0c:     	bl	0x1000abccc 
100009a10:     	mov	x26, x0
100009a14:     	nop
100009a18:     	ldr	d9, 0x1000c6358 
100009a1c:     	fmov	d0, d9
100009a20:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100009a24:     	bl	0x1000abccc 
100009a28:     	mov	x27, x0
100009a2c:     	bl	0x1000abde0 
100009a30:     	mov	x0, x25
100009a34:     	bl	0x1000b0800    ; sel endDate
100009a38:     	bl	0x1000abccc 
100009a3c:     	mov	x26, x0
100009a40:     	mov	x0, x27
100009a44:     	mov	x2, x26
100009a48:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
100009a4c:     	fmov	d10, d0
100009a50:     	bl	0x1000abde0 
100009a54:     	fcmp	d10, #0.0
100009a58:     	str	x20, [sp, #0x18]
100009a5c:     	mov	x19, x22
100009a60:     	b.pl	0x100009ad0 <__text+0x3a80>
100009a64:     	mov	x0, x25
100009a68:     	mov	x2, x27
100009a6c:     	bl	0x1000bbaa0    ; sel valueAtDate:
100009a70:     	bl	0x1000abccc 
100009a74:     	mov	x26, x0
100009a78:     	bl	0x1000b05a0    ; sel doubleValue
100009a7c:     	fadd	d8, d8, d0
100009a80:     	bl	0x1000abde0 
100009a84:     	mov	x0, x27
100009a88:     	fmov	d0, d9
100009a8c:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100009a90:     	bl	0x1000abccc 
100009a94:     	mov	x26, x0
100009a98:     	bl	0x1000abdec 
100009a9c:     	mov	x0, x25
100009aa0:     	bl	0x1000b0800    ; sel endDate
100009aa4:     	bl	0x1000abccc 
100009aa8:     	mov	x27, x0
100009aac:     	mov	x0, x26
100009ab0:     	mov	x2, x27
100009ab4:     	bl	0x1000bac60    ; sel timeIntervalSinceDate:
100009ab8:     	fmov	d10, d0
100009abc:     	bl	0x1000abdec 
100009ac0:     	fcmp	d10, #0.0
100009ac4:     	mov	x27, x26
100009ac8:     	b.mi	0x100009a64 <__text+0x3a14>
100009acc:     	b	0x100009ad4 <__text+0x3a84>
100009ad0:     	mov	x26, x27
100009ad4:     	adrp	x8, 0x100132000 
100009ad8:     	ldr	x0, [x8, #0xa98]
100009adc:     	bl	0x1000abc84 
100009ae0:     	mov	x27, x0
100009ae4:     	mov	w24, #0x0               ; =0
100009ae8:     	mov	w20, #0x8               ; =8
100009aec:     	adrp	x22, 0x100132000 
100009af0:     	scvtf	d0, w24
100009af4:     	mov	x0, x23
100009af8:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100009afc:     	bl	0x1000abccc 
100009b00:     	mov	x28, x0
100009b04:     	mov	x0, x19
100009b08:     	mov	x2, x28
100009b0c:     	mov	x3, x25
100009b10:     	fmov	d0, d8
100009b14:     	bl	0x1000ae020    ; sel budgetProportionAtDate:withTimeline:withNormalizer:
100009b18:     	fmov	d9, d0
100009b1c:     	bl	0x1000abdf8 
100009b20:     	ldr	x0, [x22, #0xab8]
100009b24:     	fmov	d0, d9
100009b28:     	bl	0x1000b4f80    ; sel numberWithDouble:
100009b2c:     	bl	0x1000abccc 
100009b30:     	mov	x28, x0
100009b34:     	mov	x0, x27
100009b38:     	mov	x2, x28
100009b3c:     	bl	0x1000acc20    ; sel addObject:
100009b40:     	bl	0x1000abdf8 
100009b44:     	add	w24, w24, #0x384
100009b48:     	subs	w20, w20, #0x1
100009b4c:     	b.ne	0x100009af0 <__text+0x3aa0>
100009b50:     	ldr	x0, [x19, #0x58]
100009b54:     	mov	x2, x27
100009b58:     	ldr	x20, [sp, #0x18]
100009b5c:     	mov	x3, x20
100009b60:     	bl	0x1000b8e60    ; sel setObject:forKeyedSubscript:
100009b64:     	ldr	x0, [x19, #0x50]
100009b68:     	mov	x2, x23
100009b6c:     	mov	x3, x20
100009b70:     	bl	0x1000b8e60    ; sel setObject:forKeyedSubscript:
100009b74:     	ldr	x0, [sp, #0x8]
100009b78:     	mov	x1, x21
100009b7c:     	bl	0x1000abf48 
100009b80:     	ldr	x24, [x19, #0x60]
100009b84:     	mov	x0, x24
100009b88:     	mov	w1, #0x1                ; =1
100009b8c:     	bl	0x1000abf9c 
100009b90:     	tbz	w0, #0x0, 0x100009c14 <__text+0x3bc4>
100009b94:     	bl	0x1000abeac 
100009b98:     	mov	x24, x0
100009b9c:     	nop
100009ba0:     	ldr	d0, 0x1000c6390 
100009ba4:     	mov	x0, x23
100009ba8:     	bl	0x1000af9a0    ; sel dateByAddingTimeInterval:
100009bac:     	bl	0x1000abccc 
100009bb0:     	mov	x28, x0
100009bb4:     	mov	w8, #0x502              ; =1282
100009bb8:     	movk	w8, #0x840, lsl #16
100009bbc:     	str	w8, [sp, #0x20]
100009bc0:     	stur	x20, [sp, #0x24]
100009bc4:     	mov	w8, #0x840              ; =2112
100009bc8:     	strh	w8, [sp, #0x2c]
100009bcc:     	stur	x23, [sp, #0x2e]
100009bd0:     	strh	w8, [sp, #0x36]
100009bd4:     	str	x0, [sp, #0x38]
100009bd8:     	strh	w8, [sp, #0x40]
100009bdc:     	stur	x21, [sp, #0x42]
100009be0:     	strh	w8, [sp, #0x4a]
100009be4:     	stur	x27, [sp, #0x4c]
100009be8:     	adr	x0, 0x100000000    ; "����"
100009bec:     	nop
100009bf0:     	adr	x3, 0x1000ec9b9    ; "Computed fresh budget allocation predictions cache for widget %@, STAR"
100009bf4:     	nop
100009bf8:     	add	x4, sp, #0x20
100009bfc:     	mov	x1, x24
100009c00:     	mov	w2, #0x1                ; =1
100009c04:     	mov	w5, #0x34               ; =52
100009c08:     	bl	0x1000ab978 
100009c0c:     	bl	0x1000abdf8 
100009c10:     	bl	0x1000abdc8 
100009c14:     	mov	x0, x19
100009c18:     	ldr	x28, [sp, #0x10]
100009c1c:     	mov	x2, x28
100009c20:     	mov	x3, x23
100009c24:     	bl	0x1000aee00    ; sel computeSlotForDate:relativeToDate:
100009c28:     	mov	x24, x0
100009c2c:     	cmp	w0, #0x8
100009c30:     	b.lo	0x100009c6c <__text+0x3c1c>
100009c34:     	ldr	x22, [x19, #0x60]
100009c38:     	mov	x0, x22
100009c3c:     	mov	w1, #0x10               ; =16
100009c40:     	bl	0x1000abf9c 
100009c44:     	cbnz	w0, 0x100009cec <__text+0x3c9c>
100009c48:     	adrp	x8, 0x100137000 
100009c4c:     	ldrb	w8, [x8, #0xc78]
100009c50:     	cmp	w8, #0x0
100009c54:     	mov	x8, #0x7ff0000000000000 ; =9218868437227405312
100009c58:     	fmov	d0, x8
100009c5c:     	nop
100009c60:     	ldr	d1, 0x1000c6368 
100009c64:     	fcsel	d8, d1, d0, ne
100009c68:     	b	0x100009c8c <__text+0x3c3c>
100009c6c:     	mov	w2, w24
100009c70:     	mov	x0, x27
100009c74:     	bl	0x1000b5100    ; sel objectAtIndexedSubscript:
100009c78:     	bl	0x1000abccc 
100009c7c:     	mov	x22, x0
100009c80:     	bl	0x1000b05a0    ; sel doubleValue
100009c84:     	fmov	d8, d0
100009c88:     	bl	0x1000abdb0 
100009c8c:     	bl	0x1000abdec 
100009c90:     	bl	0x1000abde0 
100009c94:     	bl	0x1000abdd4 
100009c98:     	bl	0x1000abdbc 
100009c9c:     	bl	0x1000abda4 
100009ca0:     	bl	0x1000abd98 
100009ca4:     	bl	0x1000abdf8 
100009ca8:     	ldr	x8, [sp, #0x58]
100009cac:     	nop
100009cb0:     	ldr	x9, 0x1000fc508 
100009cb4:     	ldr	x9, [x9]
100009cb8:     	cmp	x9, x8
100009cbc:     	b.ne	0x100009d4c <__text+0x3cfc>
100009cc0:     	fmov	d0, d8
100009cc4:     	ldp	x29, x30, [sp, #0xd0]
100009cc8:     	ldp	x20, x19, [sp, #0xc0]
100009ccc:     	ldp	x22, x21, [sp, #0xb0]
100009cd0:     	ldp	x24, x23, [sp, #0xa0]
100009cd4:     	ldp	x26, x25, [sp, #0x90]
100009cd8:     	ldp	x28, x27, [sp, #0x80]
100009cdc:     	ldp	d9, d8, [sp, #0x70]
100009ce0:     	ldp	d11, d10, [sp, #0x60]
100009ce4:     	add	sp, sp, #0xe0
100009ce8:     	ret
100009cec:     	bl	0x1000abe94 
100009cf0:     	mov	x22, x0
100009cf4:     	mov	x0, x27
100009cf8:     	bl	0x1000af1e0    ; sel count
100009cfc:     	mov	w8, #0x302              ; =770
100009d00:     	movk	w8, #0x400, lsl #16
100009d04:     	stp	w8, w24, [sp, #0x20]
100009d08:     	mov	w8, #0x840              ; =2112
100009d0c:     	strh	w8, [sp, #0x28]
100009d10:     	stur	x28, [sp, #0x2a]
100009d14:     	mov	w8, #0x800              ; =2048
100009d18:     	strh	w8, [sp, #0x32]
100009d1c:     	stur	x0, [sp, #0x34]
100009d20:     	adr	x0, 0x100000000    ; "����"
100009d24:     	nop
100009d28:     	adr	x3, 0x1000eca33    ; "PLEASE FILE A RADAR - Requested out of bounds slot index - requestedSl"
100009d2c:     	nop
100009d30:     	add	x4, sp, #0x20
100009d34:     	mov	x1, x22
100009d38:     	mov	w2, #0x10               ; =16
100009d3c:     	mov	w5, #0x1c               ; =28
100009d40:     	bl	0x1000ab960 
100009d44:     	bl	0x1000abdb0 
100009d48:     	b	0x100009c48 <__text+0x3bf8>
100009d4c:     	bl	0x1000ab90c 
