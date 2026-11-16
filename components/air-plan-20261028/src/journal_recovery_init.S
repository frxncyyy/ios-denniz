/* Static RAM-root journal recovery. Mount userdata with normal HFS journal replay, copy only the
 * fixed probe journal to console, then unmount through a clean reboot. */
.text
.globl _start
.p2align 2
_start:
    mov x9, sp
    and x9, x9, #~15
    mov sp, x9
    sub sp, sp, #8192
    mov x22, sp
    bl open_stdio
    adr x1, banner
    bl puts
    /* Zero the user hfs_mount_args; fspec is its initial 64-bit pointer. */
    stp xzr, xzr, [x22]
    stp xzr, xzr, [x22, #16]
    stp xzr, xzr, [x22, #32]
    stp xzr, xzr, [x22, #48]
    adr x9, device_path
    str x9, [x22]
    adr x0, fs_type
    adr x1, mount_path
    mov x2, #0 /* Normal mount: replay the dirty HFS journal before reading. */
    mov x3, x22
    mov x16, #167 /* mount */
    svc #0x80
    b.cs mount_failed
    adr x1, mounted
    bl puts
    adr x0, journal_path
    mov x1, #0
    mov x2, #0
    mov x16, #5
    svc #0x80
    b.cs open_failed
    mov x25, x0
    mov x26, #0
    adr x1, begin
    bl puts
read_more:
    mov x0, x25
    add x1, x22, #4096
    mov x2, #4096
    mov x16, #3
    svc #0x80
    b.cs read_failed
    cbz x0, complete
    add x26, x26, x0
    cmp x26, #16384
    b.hi read_failed
    mov x2, x0
    add x1, x22, #4096
    mov x0, x19
    mov x16, #4
    svc #0x80
    b.cs read_failed
    b read_more
complete:
    adr x1, end
    bl puts
    b reboot
mount_failed:
    adr x1, mount_error
    bl puts
    b reboot
open_failed:
    adr x1, open_error
    bl puts
    b reboot
read_failed:
    adr x1, read_error
    bl puts
reboot:
    bl sleep_one
    mov x0, #0
    mov x1, #0
    mov x16, #55
    svc #0x80
    b reboot
open_stdio:
    mov x24, x30
    mov x21, #0
close_stdio:
    mov x0, x21
    mov x16, #6
    svc #0x80
    add x21, x21, #1
    cmp x21, #3
    b.lo close_stdio
open_console:
    adr x0, console_path
    mov x1, #2
    mov x2, #0
    mov x16, #5
    svc #0x80
    b.cs retry_open
    mov x19, x0
    mov x21, #0
dup_stdio:
    mov x0, x19
    mov x1, x21
    mov x16, #90
    svc #0x80
    add x21, x21, #1
    cmp x21, #3
    b.lo dup_stdio
    ret x24
retry_open:
    bl sleep_one
    b open_console
puts:
    mov x2, #0
1:  ldrb w9, [x1, x2]
    cbz w9, 2f
    add x2, x2, #1
    b 1b
2:  mov x0, x19
    mov x16, #4
    svc #0x80
    ret
sleep_one:
    mov x9, #1
    add x4, x22, #768
    stp x9, xzr, [x4]
    mov x0, #0
    mov x1, #0
    mov x2, #0
    mov x3, #0
    mov x16, #93
    svc #0x80
    ret

console_path: .asciz "/dev/console"
fs_type: .asciz "hfs"
device_path: .asciz "/dev/ufsroot0"
mount_path: .asciz "/tmp"
journal_path: .asciz "/tmp/private/var/tmp/tegu-metal-probe.result"
banner: .asciz "TEGU_JOURNAL_RECOVERY_BEGIN hfs_journal_replay\n"
mounted: .asciz "TEGU_JOURNAL_MOUNT_OK\n"
begin: .asciz "TEGU_JOURNAL_SAVED_BEGIN\n"
end: .asciz "TEGU_JOURNAL_SAVED_END\n"
mount_error: .asciz "TEGU_JOURNAL_FAIL mount\n"
open_error: .asciz "TEGU_JOURNAL_FAIL open\n"
read_error: .asciz "TEGU_JOURNAL_FAIL read\n"
