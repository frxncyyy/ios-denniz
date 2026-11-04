# MT6893 modem (MD1) bring-up: what a host OS has to do

Verified 2026-11-04 on a OnePlus Nord 2 (MT6893), rescue Linux 4.19 with the MediaTek CCCI driver, by `n2mdinit/n2mdinit.c`
(a 14 KB libc-free program that replaces the vendor `ccci_mdinit` + `ccci_rpcd`). Result: `md1:4` (READY), `AT`/`ATI` answer
(MOLY.NR15.R3.TC16.PR6.SP.V8.P69), about 10 000 file-service requests served per boot.

## Preconditions
* LK has already loaded the modem image (`/sys/kernel/ccci/lk_md`: "LK load MD success"); the kernel only powers MD1 on
  (PMIC vmd1, clocks, SMC to TF-A `MD_POWER_CONFIG`, `md_cd_let_md_go`), then exchanges runtime data over CCIF.
* Files the modem reads/writes live on the Android partitions `nvdata` (/NVRAM/...), `nvcfg`, `protect1`/`protect2` (the "X:"/"Y:"
  copies of /md), plus `/vendor/etc/mdota` and `/vendor/etc/md/cacerts`.

## Boot ioctls (on /dev/ccci_monitor, process name must be `ccci_mdinit` for DO_START_MD)
1. `/dev/ccci_ipc_5` open + ioctl 0x5006 (keep open)
2. DO_STOP_MD `_IO('C',12)` with a pointer to u32 0 (resets a modem left in exception state, md1:5)
3. SET_MD_IMG_EXIST `_IOW('C',29,u32)` = 0
4. SET_BOOT_DATA `_IOW('C',47,u8[64])`: 8 zero bytes, 0xffffffff, 8 zero bytes, "N/C/Lf/Lt/W/G" then zeros
5. GET_MD_TYPE (returns 25), sleep, DO_START_MD `_IO('C',13)`; then poll GET_MD_STATE
Services that must answer while it boots: `/dev/ccci_fs` (file service) and `/dev/ccci_rpc` (system property reads, op 0x400f -> empty string).

## /dev/ccci_fs wire format
Packet = 16-byte header {u32 data0, u32 len, u32 chseq (bits0-15 channel, 16-30 seq, 31 assert), u32 reserved(client id)} + payload.
Channel 14 = request (read), 15 = reply (write); reply copies seq/reserved/data0. Payload = u32 op, u32 argc, args [u32 len, bytes, pad to 4].
Messages larger than 3476 bytes (both directions) are split into several packets: data0 bit31 set on all but the last, payload continues.
Reply op = request op | 0xffff0000. Strings are UTF-16LE, DOS-style: `X:\\name` -> protect_f/md, `Y:` -> protect_s/md, `Z:\NVRAM\..` -> nvdata/md,
`R:`/`Q:` -> /vendor/etc/md, `S:` -> /vendor/etc, `T:` -> /vendor/etc/mdota.
| op | meaning | args -> reply |
|----|---------|----------------|
| 1001 | open | path, flags -> handle or -18. flags >= 0x10000 (0x10400, 0x20400, 0x20010400, 0x21010400...) = read/write/create, otherwise read-only; 0x900 = directory |
| 1002 | seek | h, off, whence -> position |
| 1003 | read | h, n -> [0, nread, data] |
| 1004 | write | h, data, [n] -> [0, written] (argc 2 for big writes: size = data length) |
| 1005 | close | h -> 0 |
| 1007 | (mkdir/access probe) | path -> -18 |
| 1009 | file size | h -> [0, size] |
| 100e | disk info | (u64, u32) -> [0, 84 bytes captured verbatim] |
| 1010 | get attributes | path -> 0x10 dir, 0x20 file, -9 missing |
| 1012/1013/1014 | find first/next/close | "dir\*.*" -> [handle, 52-byte info {w3=0xbe790001,w4=0x5d7b,w5=0xbe790000,w6=0x5d7b,w7=size}, name]; end of list status -6 |
| 101a | drive query | (4,2,12) -> 0x5a ; others -2 |
| 101c | ? | -> 0 |
| 1022 | secure whole-file read | path, 40-byte descriptor -> [descriptor[0..8], filesize, nread, data]; data starts at descriptor word 5 (=64, past the LID header), length = descriptor word 8 |
| 1025 | stat times | path -> [0, 24 bytes] or -9 |
Unhandled at the moment (never reached the boot-critical path): 0x100f (delete), 0x1016, 0x1017, 0x1021.

## Pitfalls found
* arm64 O_DIRECTORY is 040000 (not x86's 0200000).
* The modem asserts (mcu/service/hif/ncccisrv/ccci_fs/src/ccci_fs.c:1317, para2 = op) when an operation it relies on fails: look at `/proc/ccci_dump`.
* A previous crashed run leaves md1:5; always DO_STOP_MD first. Restore the nv partitions from backup between experiments.

## Update (end of 2026-11-04): complete, verified boot + SIM + LTE with our own daemon
* `nvram_daemon` (vendor) must run once BEFORE the modem is started: it prepares/validates the NVRAM files and sets the "nvram ready"
  property that `ccci_mdinit` waits for. Without it the modem asks the same ~800 file operations and then waits forever (md1:3 -> exception).
  Sequence that works from the pristine partitions: `nvram_daemon` (exits after ~10 s) -> `n2mdinit` -> md1:4 after ~10 s, 3 749 fs ops.
  (`nv_then_n2.sh`). For the iOS port the equivalent is: ship NVRAM files that are already prepared (the state Android leaves) or reimplement the daemon.
* Fragments (both directions): every packet = 16-byte header + the 4-byte op id again + a chunk of the body; data0 bit31 = more packets follow.
* Op 0x1022 first descriptor word is a command: 0x13 size query -> [cmd, filesize, 0, empty]; 0x19 whole-file read from offset 0 -> [cmd, descriptor word 4 (the modem's buffer address), nread, data];
  0x1f read at offset word 5, length word 8 -> [cmd, filesize, nread, data]. The echoed u64 is {cmd, 0}.
* Op 0x100f = delete file. Op 0x1025 stat: first word = file mtime (vendor image: 2026-09-27), then atime, ctime.
* Drive S: must not find anything (the vendor daemon answers -9); T: = /vendor/etc/mdota.
* Up to 16 KiB per read/write (buffers >= 128 KiB). CCB handshake: open /dev/ccci_ccb_ctrl 3 times, GET_CCB_CONFIG_LENGTH, 20x GET_CCB_CONFIG (id in word 0), then CCB_CTRL_INFO for user 0/1/2.
* Verified end state (SIM inserted, no PIN): AT+CPIN? READY, AT+CEREG? 3,1 (registered, LTE), AT+COPS? "VERY", AT+CGATT? 1, data PDP + ping work (vendor boot) .
