ENTRY(_head)
SECTIONS {
    . = 0;
    .text : { *(.text.head) *(.text*) }
    .rodata : { *(.rodata*) }
    _end = .;
    /* No writable data: the stub runs wherever the packer puts it, with its stack at image + 64 KiB. */
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.data*) *(.bss*) *(COMMON) }
}
