/*
 * Nord 2 "no stock kernel" stub (bootcompress-20261113). Takes the place of the Android kernel Image that
 * tools/nord2-pack-xnu.py keeps at 0x40280000 for the chain-back (18.5 MB of the 32 MiB boot partition).
 *
 * Entered exactly like the Android kernel was: EL1, MMU and caches off, x0 = live FDT, from
 *   - the loader's chain_stock() (every loader fatal()/exception path), or
 *   - XNU's nord2_leave_xnu trampoline (nord2_leave_to(0): early panic, nord2stopat stop points).
 * It never returns: it appends two lines to the pmsg ring, sets the RTC fast-boot bit (so that LK stops
 * in fastboot instead of booting this image again) and resets the SoC through TOPRGU -- the sequence of
 * sweet_arm_fastboot()/sweet_reset_now() in bsd/kern/sweet_amfi.c (phone.py "reset fastboot").
 *
 * Position independent (linked at 0, runs at 0x40280000), no data relocations, own stack inside its
 * image, own exception vectors: a fault in the C part goes straight to the reset, a fault in the reset
 * parks the CPU (the AP watchdog LK left running then resets the phone).
 */
#define STUB_IMAGE_SIZE_DEFAULT 0x100000

.section .text.head, "ax"
.global _head
_head:
    b entry
    .long 0
    .quad 0x80000               /* text_offset, as the stock Image */
    .quad STUB_IMAGE_SIZE_DEFAULT /* image_size: the packer rewrites it (payload placement) */
    .quad 0xa
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0
    .ascii "NORD2STUB1\0\0\0\0\0\0"    /* 0x40: identity for the packer/checker */
.text
entry:
    msr daifset, #15
    msr spsel, #1
    adr x1, _head
    add x1, x1, #0x10, lsl #12  /* stack top = image + 64 KiB (image_size is >= 1 MiB, zero padded) */
    mov sp, x1
    adr x1, vectors
    msr vbar_el1, x1
    isb
    bl stub_main
    /* stub_main does not return; if it does, reset anyway. */
    b reset_now

/* TOPRGU (0x10007000): MODE = (MODE & ~IRQ) | key 0x22, then SWRST = 0x1209 for ever. No stack. */
.global reset_now
reset_now:
    msr daifset, #15
    adr x1, vectors_park        /* a fault from here on parks */
    msr vbar_el1, x1
    isb
    movz x1, #0x7000
    movk x1, #0x1000, lsl #16
    ldr w2, [x1]
    bic w2, w2, #8
    movz w5, #0x2200, lsl #16
    orr w2, w2, w5
    str w2, [x1]
1:  movz w3, #0x1209
    str w3, [x1, #0x14]
    movz x4, #0x4, lsl #16
2:  subs x4, x4, #1
    b.ne 2b
    b 1b

park:
    msr daifset, #15
3:  wfe
    b 3b

.macro vec target
    .balign 128
    b \target
.endm
.balign 2048
vectors:
    .rept 16
    vec reset_now
    .endr
.balign 2048
vectors_park:
    .rept 16
    vec park
    .endr
