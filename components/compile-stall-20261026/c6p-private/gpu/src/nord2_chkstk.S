/* Local ARM64 Darwin kernel stack probe, same observed ABI as the preserved
 * kernel: x9 = byte count; x9/x10/flags may be clobbered; SP is unchanged.
 * Touch every 4 KiB down to the last byte so a large frame cannot skip the
 * unmapped guard. Not a no-op shim. See docs/kernel-link-support.md. */
.text
.p2align 2
.globl ___chkstk_darwin
___chkstk_darwin:
    mov x10, sp
1:  cmp x9, #4096
    b.lo 2f
    sub x10, x10, #4096
    ldrb wzr, [x10]
    sub x9, x9, #4096
    b 1b
2:  sub x10, x10, x9
    ldrb wzr, [x10]
    ret
