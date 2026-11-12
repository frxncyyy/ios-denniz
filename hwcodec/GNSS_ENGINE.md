# Nord2 GNSS engine: running MediaTek's libmnl.so natively on the iOS port (2026-11-12, host analysis)

V = verified on the extracted vendor files / kernel source. I = inferred.

## 1. What the Android stack is (V)
* `vendor/bin/mnld` (sha ea3c0346...) = daemon: owns `/dev/gpsdl0` (L1), `/dev/gpsdl1` (L5), `/dev/gps_pwr`, `/dev/gps_emi`,
  AGPS/EPO download (libcurl/libssl), HIDL GNSS HAL socket, sensor fusion (HIDL sensors). 212 imports, 19 NEEDED libs: **do not port**.
* `vendor/lib64/mt6893/libmnl.so` (sha 9a1e1be7...) = the positioning engine (MNL), AArch64 ELF, SONAME libmnl.so,
  **139 imports, all libc/libm** (no C++ ABI symbols although libc++.so is NEEDED, no libdl calls), 289 exports.
  API seen from mnld's imports: `mtk_gps_sys_function_register` (mnld hands the engine its callback table),
  `mtk_gps_mnl_run` / `mtk_gps_mnl_stop`, `mtk_gps_data_input` (bytes from the data link), `mtk_gps_nmea_input`,
  `mtk_gps_set_param`/`get_param`, `mtk_gps_inject_ntp_time`, `mtk_gps_inject_nlp_location`, `mtk_gps_get_position_accuracy`,
  `mtk_gnss_get_measurement`/`clock`/`navigation_event`, EPO/QEPO file update (`mtk_agps_agent_*_file_update`),
  `mtk_gps_get_MNL_Config_XML_param` (reads `/vendor/etc/gnss/MNL_Config.xml` or `/data/vendor/gps/MNL_Config.xml`).
  NMEA leaves the engine through its sys layer (`mtk_gps_sys_nmea_output`, I: it ends in a callback registered by mnld).
* Loader-friendliness of libmnl.so (V): no TLS segment and no `mrs tpidr_el0` (no bionic TLS/stack-guard slot), **no `svc`**
  (all syscalls through imports), no INIT_ARRAY/constructors, `BIND_NOW`, relocations = 0x108 bytes of `ANDROID_RELR`
  (relative) + 300 `R_AARCH64_JUMP_SLOT` (the 139 imports). LOAD: R 0x3de574 @0, RX 0x196c00 @0x3df000, RW (RELRO) 0x2f70
  @0x576000, RW 0x1f38/0x13ae44 (1.3 MB bss) @0x579f70.

## 2. How to run it on iOS (plan)
iOS refuses unsigned executable pages, so **convert, don't load**: a host tool `elf2macho.py` makes a Mach-O dylib whose
segments keep the ELF's relative layout (code is PC-relative: adrp/add/ldr into its own data keep working), exports
the `mtk_*` symbols, and is signed by our signer (gpu/hwcodec/locbridge/sign_ent.py pattern; every signed rootfs binary
is a platform binary here). At start our host program (`n2mnld`, replaces mnld) applies the RELR relocations (slide)
and writes the 300 JUMP_SLOT GOT entries with addresses of **shim functions** (bionic ABI -> Darwin), then calls
`mtk_gps_sys_function_register(our table)`, `mtk_gps_mnl_run(cfg)`, feeds `/dev/gpsdl0` bytes into `mtk_gps_data_input`,
and writes the NMEA from the callback into `/private/var/run/n2gnss.nmea` -> **n2locbridge** (payload-locbridge1) -> locationd.
Risks (I): the callback-table layout and `mtk_gps_mnl_run`'s config struct must be recovered from mnld's disassembly
(mnld is not needed at runtime, only as documentation); errno values differ (EAGAIN 11 vs 35: translate in `__errno`);
FILE* is used opaquely (no bionic FILE internals referenced: no `__sF` import), OK.

### Shim list (generated from `llvm-nm -D --undefined-only libmnl.so`)
Imports of lib64/mt6893/libmnl.so: 139 (all from libc/libm; no C++ ABI symbols, no libdl calls)

**direct (same C ABI on Darwin, pass-through to libSystem)** (74)

abort, access, acos, asin, atan2, atan, atof, atoi, chmod, close, closedir, cos, exit, exp2, exp, fclose, fflush, fgets, fmod, fmodf, fopen, fprintf, fputc, fputs, fread, free, fscanf, fseek, ftell, fwrite, inet_ntoa, log10, log, malloc, memcpy, memset, mkdir, modf, pipe, pow, printf, pthread_equal, pthread_exit, pthread_join, pthread_self, puts, qsort, rand, read, remove, rename, sched_get_priority_max, sched_get_priority_min, setbuf, sin, sleep, srand, sscanf, strchr, strcmp, strcpy, strerror, strlen, strncat, strncmp, strncpy, strrchr, strstr, strtok, strtoul, tan, usleep, vsnprintf, write

**fortify/bionic-internal (tiny wrappers)** (17)

- __cxa_finalize
- __errno
- __libc_current_sigrtmin
- __memcpy_chk
- __memset_chk
- __open_2
- __read_chk
- __stack_chk_fail
- __strcat_chk
- __strcpy_chk
- __strlen_chk
- __strncat_chk
- __strncpy_chk2
- __strncpy_chk
- __vsnprintf_chk
- __vsprintf_chk
- __write_chk

**struct/ABI translation needed** (40)

- accept: sockaddr
- bind: sockaddr (sa_family u16 vs sa_len+sa_family)
- cfsetispeed: speed_t B* constants
- cfsetospeed: idem
- clock_gettime: clockid values (CLOCK_MONOTONIC 1 vs 6, BOOTTIME 7)
- ctime: ok
- fcntl: F_* / O_* flag values differ (O_NONBLOCK 04000 vs 4 ...)
- gettimeofday: struct timeval same (64-bit)
- gmtime: struct tm same fields (tm_gmtoff, tm_zone)
- ioctl: request numbers are the Linux gps_dl ones -> forward to our /dev/gpsdl driver unchanged
- listen: same
- localtime: idem
- mktime: idem
- opendir: DIR opaque: ok
- pthread_cond_destroy: idem
- pthread_cond_init: pthread_cond_t 48 B both but different contents: indirect
- pthread_cond_signal: idem
- pthread_cond_timedwait: idem (CLOCK_REALTIME abstime)
- pthread_cond_wait: idem
- pthread_create: pthread_attr_t layout (56 B bionic vs 64 B): translate/ignore attr
- pthread_getschedparam: sched_param same; policy values differ
- pthread_kill: signal numbers
- pthread_mutex_destroy: idem
- pthread_mutex_init: pthread_mutex_t 40 B (bionic) vs 64 B: keep a pointer to a heap Darwin mutex in the bionic object
- pthread_mutex_lock: idem
- pthread_mutex_unlock: idem
- pthread_setschedparam: policy values
- readdir: struct dirent layout differs
- setsockopt: SOL_SOCKET/SO_* values differ
- sigaction: struct sigaction + SA_* flags + signal numbers differ
- sigaddset: sigset_t 8 bytes vs 4
- sigemptyset: sigset_t
- signal: signal numbers (Linux vs BSD)
- sigprocmask: sigset_t + how values
- socket: AF_/SOCK_ constants
- stat: struct stat layout (bionic arm64 = Linux generic) -> convert
- tcflush: queue selectors
- tcgetattr: struct termios differs (Linux c_cc[19], u32 flags)
- tcsetattr: termios
- time: ok

**missing on Darwin (emulate)** (7)

- epoll_create (emulate with kqueue)
- epoll_ctl (kqueue)
- epoll_pwait (kevent)
- sincos (__sincos on Darwin)
- timer_create (POSIX timers: emulate with a thread or dispatch_source)
- timer_delete (idem)
- timer_settime (idem)

**Android-only** (1)

- property_get (return defaults (or a small key table: ro.vendor.* etc.))


## 3. Kernel side (milestone after this one): /dev/gpsdl0..1 on XNU
* Source: `nord2-current-kernel/drivers/misc/mediatek/connectivity/gps/data_link` (V): linux/ (char devices, EMI reserved
  memory, ctrld), link/ (per-link state machine, IRQ, event proc, subsys reset), hal/ (conn_infra, power_ctrl, dma, isr,
  mcub = MCU mailbox, zbus, dsp_fsm), hw/ (bgf, conn_infra, gps, power_ctrl, usrt_apb, semaphore). Buffers 4-8 KB TX/RX.
* ioctls libmnl/mnld use (V, gps_dl_linux.c): GPSDL_IOC_GPS_HWVER 6, IC_HW_VERSION 7, IC_FW_VERSION 8, D1_EFUSE_GET 9,
  RTC_FLAG 10, CO_CLOCK_FLAG 11, TRIGGER_ASSERT 12, QUERY_STATUS 13, TAKE/GIVE_WAKELOCK 14/15, GET_GPS_LNA_PIN 16,
  GPS_FWCTL 17, HW_SUSPEND/RESUME 18/19, LISTEN_RST_EVT 20, GET_MD_STATUS 21.
* No firmware file is loaded by gps_dl (no request_firmware, V): the GNSS DSP code comes from ROM or from libmnl through
  the data link (`mtk_gps_dsp_init`, `mtk_gps_dsp2_init`: I).
* Reuse from our Wi-Fi bring-up: conninfra power-on/clock/EMI/PMIC (the code that prints `NORD2 WIFI FW probe status=0`);
  GNSS adds `consys_hw_gps_power_ctl`, `pmic_mng_gps_power_ctrl`, the GPS EMI region (emi_mng.h) and the BGF (BT/GPS) power domain.
* The char devices follow the nord2_* device pattern of our kernel (to be written as patch 0350; skeleton not written yet).

## 4. Order of work
1. elf2macho.py + n2mnld with a **fake data link** (replay of bytes captured on Android: needs one capture of /dev/gpsdl0
   traffic from the stock ROM, e.g. strace -e read,write of mnld). If libmnl then outputs NMEA from the replay on the iPhone
   side, the engine port is proven without any kernel work.
2. Kernel 0350: power the GNSS core, implement /dev/gpsdl0 read/write/poll/ioctl over the EMI rings.
3. Live: n2mnld on /dev/gpsdl0 -> NMEA FIFO -> n2locbridge -> locationd.

---
## 5. M3 runtime decision + what libmnl needs at runtime (2026-11-12 ~20:30, host analysis; V = checked on the binary/logs)

### 5.1 Architecture: AArch64 -> run it NATIVELY, not in the A32 interpreter (V)
* Only one libmnl exists in the vendor tree: `vendor/lib64/mt6893/libmnl.so` (ELF64 AArch64, Android 30). There is no
  `vendor/lib/` 32-bit copy, so vcodecd's n2a32 interpreter (gpu/hwcodec/emu, AArch32/Thumb only) cannot run it, and it
  is not needed: the MT6893 cores run AArch64 natively, and the iOS port executes arm64 userland.
* The interpreter exists for vcodecd only because XNU arm64 cannot execute A32 code; libmnl has no such problem.
* libmnl is clean for native execution (llvm-objdump over the whole 0x3df000..0x575c00 text): **0 uses of x18** (Darwin's
  reserved platform register), **0 `svc`**, ~~0 `mrs tpidr_el0`~~ **WRONG, see section 7: 858 `mrs Xt, TPIDR_EL0` (bionic stack-protector canary reads; the grep was case-sensitive and objdump prints TPIDR_EL0 in capitals)**, **0 exclusive/LSE atomics**
  (all locking through pthread imports), no DT_TEXTREL, no INIT_ARRAY (FINI_ARRAY only, never needed), BIND_NOW,
  relocations = RELR (relative) + 300 JUMP_SLOT. All 139 imports are functions (no data imports, no `__sF`/stdout).
* Option (b) "re-implement MNL" rejected: the DSP (I: correlation/tracking only) sends ~8 small frames per second
  (~380 B/s: 0xfe28 13 B, 0xfe29 71 B, 0xfe2c 58 B, 0xfe30 63 B, 0xfe37 65 B, 0xfe4e 20 B, 0xfe61 28 B, 0xfe62 12 B; the
  len field's top nibble is a per-epoch counter: 0x0d, 0x100d, 0x200d...). The whole navigation side (DSP channel control,
  nav-message decoding, ephemeris/almanac, measurement processing, PVT/Kalman, NMEA) is libmnl's 5.7 MB. The payload is
  scrambled (idle bytes 0xCA; I: XOR 0xCA whitening, since 0xCA -> 00 and the unscrambled ROM acks are 00-padded). Writing
  that from scratch is months of RE; the native port reuses all of it.

### 5.2 Chosen runtime: "elf2macho" static embedding + native bionic->Darwin shim (n2mnld)
* No runtime code generation: libmnl's bytes become part of a **signed Mach-O dylib** `libn2mnl.dylib`, built from one
  assembly file with `.incbin`: __TEXT section `__mnl` (ELF vaddr 0..0x575c00, R+RX parts) placed at __TEXT offset 0x2000,
  and __DATA section `__mnl` = ELF 0x576000..0x6b4db4 (RELRO + RW + 1.29 MB bss as zeros), which then lands 16 KiB aligned
  at ELF vaddr + 0x2000 = 0x578000 (16 KiB pages). A build check verifies `addr(__DATA,__mnl) - addr(__TEXT,__mnl) ==
  0x576000` (otherwise every adrp breaks). Signed like every other binary (sign_ent.py, platform binary). Rejected:
  a runtime ELF loader with MAP_JIT (needs `dynamic-codesigning` + kernel behaviour nobody has tested on this port).
* At start n2mnld applies RELR (+slide) and fills the 300 GOT slots (GOT lives in the RELRO part = our __DATA, writable)
  with shim addresses; exports (`mtk_gps_*`) are called at fixed ELF offsets from the base.
* AAPCS64 Linux vs Darwin arm64 differences the shim MUST handle (V by ABI docs + import list):
  1. **variadic imports pass args in registers on Linux, on the stack on Darwin**: printf, fprintf, sscanf, fscanf, fcntl,
     ioctl -> per-import asm trampolines save x0..x7/q0..q7, build a Linux `va_list` (struct {stack, gr_top, vr_top,
     gr_offs, vr_offs}) and call C. va_list imports (vsnprintf, __vsnprintf_chk, __vsprintf_chk) get that struct by pointer.
     Formatting/scanning is ours: **reuse gpu/hwcodec/shim/n2shim_format.c** (n2fmt, exact float printing, tested for
     vcodecd) with a new N2FMT_ARGS_AAPCS64 argument source. Nothing is ever forwarded to Darwin printf with guest args.
  2. Darwin callees assume char/short args are extended by the caller, Linux callers do not: every shim parameter is
     declared int/long and truncated inside.
  3. long double is 128-bit on Linux: no `__*tf3` imports, so unused (V).
  4. structs/constants: stat, dirent, termios, sigaction/sigset, sockaddr (sa_len), O_*/F_* flags, errno values
     (`__errno` returns a per-thread Linux errno filled by translating Darwin errno), clock ids, signal numbers.
  5. pthread objects: bionic mutex 40 B / cond 48 B with all-zero static initialisers, Darwin mutex 64 B with a
     signature: store a pointer to a heap Darwin object in word 0 (lazy CAS on first use). pthread_create: ignore the
     bionic attr except stacksize (min 1 MiB, Darwin's 512 KiB default is too small for a bionic-compiled stack user).
  6. epoll_* -> kqueue; timer_create (only SIGEV_THREAD = 2 is used, V at 0x4144a0) -> one worker thread per timer.
* Rejected/unneeded: libc++ (no C++ ABI imports), libdl (no calls), JNI (none: libmnl has no JNI symbols; JNI is in the
  HAL, not ported).

### 5.3 What libmnl does at runtime (V from the harness runs runs/e6.log, e8.log + disassembly)
* **Entry sequence (as mnld):** `mtk_gps_sys_function_register(&table20)` -> `mtk_gps_mnl_run(cfg1 0x74 B, cfg2 0x434 B)`
  (decoded in n2mnld/STATUS.md: cfg1 word0 = 1 enables mtk_gps_dsp_init; MT6893 chip fields 0x250 / 0xFFFF6893;
  cfg2+0x280 = L1 fd, +0x284 = L5 fd, +0xfd port name "UseCallback" = no open()). Callback slot 0 = NMEA out, slot 1 = log.
* **Threads (5, all pthread_create, V):** 0x411120 file_control (NV files), 0x410930 debug socket server, 0x40f67c
  thread_dsp_input_func (blocking `read(L1fd, 512)` loop -> frame parser), 0x410478 mnl main (mtk_gps_run: message queue,
  usleep(1 ms) polling, jump table for ids > 1000), 0x4148c4 AGPS/EPO agent. Each thread installs a SIGUSR1 (Linux 10)
  SA_SIGINFO handler; mnl_stop uses pthread_kill to end them (V 0x40f7b0, 0x411c30) -> shim: accept and ignore, n2mnld never
  calls mtk_gps_mnl_stop (launchd restarts the job instead).
* **Device node:** none opened by libmnl itself; it uses the fds from cfg2: `read`/`write`(__write_chk)/`ioctl` on the L1
  fd. Mapping to our XNU /dev/gpsdl0: read/write = byte stream of AA F0 ... AA 0F frames (identical semantics, the 0352..0364
  driver keeps them); **ioctl: libmnl passes Linux bare numbers (7, 8, 9, 13, 14/15, 18/19, 20) with the arg in x2; XNU
  would drop the argument (no IOC_IN/IOC_VOID bits), so the shim re-encodes to `N2GD_IOC(n) = _IOWR('G', n, uint32_t)`**
  (nord2_gpsdl.inc:45) and copies the u32. 7/8/9 must return -EINVAL as on the real MT6893 kernel (V: libmnl carries on).
  L5 fd: pass -1/unused (cfg L1-only, flag 0x66 = 1).
* **DSP boot:** libmnl itself runs `_BootDSP_UART_V2` (header 0xFE08, COMD4 0xFE05, 50 x 0xFE0C) through write(), expecting
  0xFE31/0xFE32 acks. On the phone the download only works today from the kernel (S11, K=31 HOLD). Plan: n2mnld does the
  kernel boot first (upload the same 70746-byte blob with N2GD_IOC_CHUNK, then K=31; = n2gpsdl_replay --kboot 31 code), then
  starts libmnl with a **boot proxy in the write/read shim**: while libmnl is in its boot phase, frames with ids 0xFE08 /
  0xFE0C / the boot 0xFE05 are not sent to the device; the shim queues the recorded acks (0xFE31 payload `00 00 13 19 00 00
  00 00 00 00` from gl11a, 0xFE32 seq k) on libmnl's read side, and checks that libmnl's frames are byte-identical to the
  uploaded blob (sha 0x2ed207a4 sum; else log N2MNLD BOOTDIFF and stop). After the last fragment the proxy turns into a
  pass-through, and the DSP stream already running since K=31 reaches thread_dsp_input_func.
* **Files (all relative to the CWD or to cfg2 paths, V):** `MNL_Config.xml` (fopen x4; vendor copy from vfs/etc/MNL_Config.xml),
  `MNL_Config_Tmp.xml`, NV `mtkgps.dat` (+ CRC/version side files), `cache.dat`, `almanac.dat` (`/nvcfg/almanac.dat`,
  LGE path, only if configured), EPO/QEPO files (agent, absent -> no assistance), debug log dir (empty -> 10 errors/s
  "GpsLogDirCheck": set a log dir or disable debug logging in cfg2). n2mnld chdir()s to `/private/var/db/n2gnss/` (created
  0700), and maps the Android absolute paths (`/data/vendor/gps/...`, `/vendor/etc/...`) into it in the fopen/access/stat shim.
* **Android property calls:** only `property_get("ro.build.type")` (V, the only ro./persist. string) -> return "user".
* **Sockets:** debug server `socket/bind/listen/accept` on **TCP port 7000** (log "listening debug port: 7000") plus a second
  server with epoll at 0x3e5624.. (not reached in the runs). Shim binds them to 127.0.0.1 only (never expose on Wi-Fi), or
  fails socket() for them if they cause trouble.
* **Time:** gettimeofday/clock_gettime(MONOTONIC/BOOTTIME), timer_settime (SIGEV_THREAD timers: debug port + sys timers),
  pthread_cond_timedwait with CLOCK_REALTIME deadlines.
* **termios** (tcsetattr x2 at 0x40bcbc) only for a real UART port name; not on our "UseCallback"/fd path.

### 5.4 Plan (ordered; host first, each step has a check)
1. **Phone (now, staged): gnssl11b** = K=31 HOLD + 20 s user-space read of /dev/gpsdl0 with full hex (16 KiB budget,
   ~7.6 KB expected). Gives (a) proof that read() works after the kernel boot (the n2mnld data path), (b) the **full
   byte stream** of the DSP (gl11a logs only 16 payload bytes per frame) = the replay file for step 3.
2. **Host: libn2mnl.dylib + n2mnld + shim** (C + asm, `gnss/n2mnld/native/`), build for iOS with the locbridge toolchain
   (clang --target=arm64-apple-ios16.0, ld64.lld, sign_ent.py) + the layout check. Unit tests of the varargs trampolines
   and of the struct translations on the host (x86_64, the C parts).
3. **Host run of the real arm64 binaries**: there is no qemu-aarch64 on this host and no Darwin, so the test runs the
   built Mach-O n2mnld + libn2mnl.dylib in Unicorn (extend mnl_emu2.py with a minimal Mach-O loader and Darwin libSystem
   stubs that use the DARWIN calling convention, so a wrong varargs trampoline fails visibly) against the fake DSP =
   the gl11b capture replayed after the recorded boot acks. Pass = NMEA lines out of callback slot 0 ($GPGGA/$GNRMC with
   no fix, $GPGSV if the frames carry channel status). Fallback if Unicorn+Mach-O is too slow to build: the existing
   ELF harness (mnl_emu2.py) with the replay proves the engine part only (NMEA from real frames), not the shim.
4. **payload-gnssd1** (+ guest/owned/undo/rehearsal): /usr/libexec/n2mnld + /usr/lib/libn2mnl.dylib (or next to it),
   MNL_Config.xml, LaunchDaemon `com.nord2.gnssd` (RunAtLoad, KeepAlive false at first: one run per boot, 60 s throttle),
   writes NMEA to the FIFO `/private/var/run/n2gnss.nmea` that n2locbridge (payload-locbridge1, already staged) reads;
   console lines `N2MNLD ...` (boot result, frames/s, first NMEA, GSV summary every 10 s).

### 5.5 First-fix expectations
* No assistance (no EPO/QEPO, no NTP/NLP injection, RTC flag unknown): a true cold start. Outdoors with open sky
  TTFF ~35-60 s (GPS L1 nav message frames need 18-30 s of clean tracking after acquisition), longer for GLONASS/BeiDou.
* Indoors (where the phone sits for these runs): **no fix**. Expected NMEA: $G?GGA with fix quality 0, $G?RMC status V,
  $G?GSA mode 1 and $G?GSV with 0 satellites, or a few SVs with low C/N0 if a window is near. locbridge forwards only valid
  fixes (n2locbridge.c: `if (!g_fix.valid) return`), so locationd sees nothing until a real fix.
* Later improvements: inject time (`mtk_gps_inject_ntp_time` from the iOS clock), last position
  (`mtk_gps_inject_nlp_location` from the last fix in NV), EPO download (needs network): hot/warm start in a few seconds.

---
## 6. n2mnld built + payload-gnssd1 staged (2026-11-12 ~21:10; host only, NOT run on the phone, NOT executed on the host)
V = checked on the built file / rehearsal. I = inferred. Sources: `gnss/n2mnld/native/` (n2mnld.c, n2shim.c, n2shim_va.S,
n2sys.h, n2mnl_image.S, mkimage.py, libSystem-n2mnld.tbd, build_ios.sh).

### 6.1 What was built (V)
* `build_ios.sh` -> `native/build/n2mnld`, one arm64 Mach-O executable (no separate dylib), 7294464 B, ad-hoc signed by
  ld64.lld, sha256 **2e82776003412ef45bbaad68e95d2b958df4b1644eac29dc4a19fd54ffa348a0**. libmnl (sha 9a1e1be7) flattened
  by mkimage.py and `.incbin`'d: code in segment `__MNLT` (r-x, -segprot), data+bss in `__MNLD` (rw). Build checks:
  `_n2mnl_data - _n2mnl_base == 0x576000` and 16 KiB aligned (base 0x10002e000, data 0x1005a4000), protections, every
  undefined symbol in the tbd (130 libSystem symbols), every one of the 139 imports present in the shim table.
* At run time n2mnld rechecks the layout, applies 1328 RELR, writes 139 GOT slots (shims) + 161 self-bound PLT slots.
* Shim (section 5.2 plan, implemented): D() = Darwin function directly (string/memory/math/FILE*/time/tm/qsort ...);
  S() = wrappers; V() = asm trampolines for printf/fprintf/sscanf/fscanf/fcntl/ioctl that save x0-x7/q0-q7 and build a
  real Linux AAPCS64 va_list. printf-family formatting is per conversion with typed values (no guest format forwarded
  with guest args; %m and %n handled locally); scanf gets a Darwin va_list made of the pointer arguments only.
  Not pass-through on purpose: gettimeofday (Darwin tv_usec is 32-bit), pthread_join (EDEADLK 11 vs 35), strerror
  (Linux errno numbers). Linux errno per thread (pthread key); pthread mutex/cond = Darwin object pointer|1 in word 0
  (bionic recursive static initializer honoured); pthread_create min 1 MiB stack; signals accepted, never installed;
  pthread_kill ignored; SIGPIPE ignored; timers SIGEV_THREAD only (thread per timer); epoll -> kqueue; sockets AF_INET
  forced to 127.0.0.1, AF_UNIX paths into the config dir, AF_INET6 refused; stat/dirent/open-flag/clock-id translation;
  Android absolute paths -> /private/var/db/n2gnss/<basename>, /dev /proc /sys refused (libmnl opens no device itself).
* Data link (n2mnld.c): libmnl gets the real /dev/gpsdl0 fd in cfg2+0x280 (L5 = /dev/null fd, reads park forever; L1-only
  config). Reads are ALWAYS non-blocking + 10 ms poll (V in nord2_gpsdl.inc: a blocking read holds n2gd_lock up to 1 s
  and would stall libmnl's writes). libmnl's bare Linux ioctls: 13/14/15 -> N2GD_IOC(n) = _IOWR('G',n,u32) to the kernel,
  0 returned to libmnl; 6-10 and 20 -> EINVAL (compiled out on MT6893, as the harness); 11/12/18/19 -> 0 locally.
* DSP boot: QUERY_STATUS bit0 set -> skip (restart; the kernel also answers K=31 "already held"). Else the 70746-byte
  blob (probe/n2gpsdl_bootdata.h, the gl11a one) via N2GD_IOC_CHUNK + ladder K=31 (ioctl 171); a Wi-Fi-guard refusal
  (result bit 24) is retried every 30 s up to 6 times; failure -> exit 3, engine not started.
* Boot proxy: while libmnl runs _BootDSP_UART_V2 its writes are compared with the blob frames: header -> inject the gl11a
  FE31 ack (20 B), COMD4 swallowed, fragment k -> inject FE32 seq k, after fragment 50 the identical post-boot 0xFE05 is
  swallowed (the kernel sent it) -> pass-through. Different frames -> `BOOTDIFF` lines, still acked. No header in 120 s ->
  pass-through anyway. Only during the boot phase the device is not read (the DSP stream waits in the driver/DSP).
* Config dir /private/var/db/n2gnss (0700): MNL_Config.xml is installed EMPTY on purpose: the ELF harness that produced
  the kernel boot blob gave libmnl an empty MNL_Config.xml (relative path, no prefix match in mnl_emu2.py), so the same
  config should give byte-identical boot frames (I; BOOTDIFF lines would show otherwise). Vendor copy installed as
  MNL_Config.vendor.xml (rename it to MNL_Config.xml for a later test).
* Not done / not verified: no execution of the built binary anywhere (host emulation of the Mach-O was dropped; the partial
  test/darwin_emu.py was deleted). Unverified on hardware: exec of an r-x non-__TEXT segment under this port's AMFI,
  read() after K=31 HOLD (gl11b never ran), libmnl behaviour with real frames (only 256 real post-boot bytes exist, gl11a).

### 6.2 payload-gnssd1 (V: rehearse-gnssd1.sh REHEARSAL_OK)
* Files: payload-gnssd1/{n2mnld, MNL_Config.xml (0 B), MNL_Config.vendor.xml, plan.json}; guest-gnssd1.sh,
  guest-gnssd1-undo.sh, payload-gnssd1-undo/plan.json, owned-gnssd1.json / owned-gnssd1-undo.json (= owned-gnssstep1 +
  10 paths), rehearse-gnssd1.sh/.log.
* Install: /usr/local/libexec/n2mnld; job com.nord2.gnssd = `n2mnld --gate /private/var/db/nord2-gnssd --delay 60 --kboot 31`
  (RunAtLoad, root, no KeepAlive = one run per boot); receipt /private/var/db/nord2-gnssd.json; Metal receipt launchd sha
  updated. The gnssstep probe gate /private/var/db/nord2-gnssstep is moved to `.off-gnssd1` (undo moves it back), so no
  ladder probe opens /dev/gpsdl0 at boot. Rehearsal: install, idempotent re-install, undo == start state (gate restored),
  idempotent undo, refusal on Metal-receipt mismatch.
* Kernel: **q10jfg** (q10jfe display + AE2 + the GNSS set 0350..0364, boot-arg n2gps=1; image sha 967ecf50) or q10jr11.
  q10jfh (0393 fb sync) has NO GNSS set: /dev/gpsdl0 missing -> `N2MNLD FATAL open /dev/gpsdl0`.
* NMEA reaches locationd only with n2locbridge in FIFO mode (payload-locbridge1 args `--fifo /private/var/run/n2gnss.nmea`);
  the mirror (20:36) has neither com.nord2.locbridge nor com.nord2.gnssstep installed. Without a reader n2mnld counts the
  lines as "dropped" and still prints them in its stats.

---
## 7. gnssd1b crash -> root cause, fix, gnssd2 (2026-11-12 ~21:35; host analysis + build; NOT yet run on the phone)
V = verified on the binary/crash report/rehearsal, I = inferred.

### 7.1 What happened on the phone (gnssd1b, kernel q10jfg) (V: console xnu-live.log, n2mnld.log, crash report n2mnld-1969-12-31-160132.0002.ips)
* Start, image load (base 0x100776000, 1328 RELR, 139 GOT), delay 60 s, **kernel DSP boot K=31 OK (result 0x1)**,
  `mtk_gps_sys_function_register -> 0`, then SIGSEGV on the MAIN thread right at `mtk_gps_mnl_run` (no engine thread was
  created, no pthread_create line, no libmnl log callback). EXC_BAD_ACCESS byte read at 0x2d (ESR data abort, translation fault).
* pc 0x100b8870c = libmnl **0x41270c**; lr = main+0xbe0 (the return address of `run(cfg1, cfg2)`): the export
  `mtk_gps_mnl_run` 0x414c88 is a single `b 0x4126e0` (tail call), so 0x4126e0 is the real body (both offsets in the docs were right).

### 7.2 Root cause (V)
```
4126e0 (mtk_gps_mnl_run body):  ...  412708: mrs x21, TPIDR_EL0   41270c: ldr x8, [x21, #0x28]   412710: stur x8, [x29, #-8]
```
* bionic's **stack protector**: the canary lives in TLS slot 5 = [TPIDR_EL0 + 0x28]. On iOS arm64 the kernel keeps the
  **CPU number** in TPIDR_EL0 (the Darwin thread pointer is TPIDRRO_EL0): x21 = 5 (crash report) -> 5 + 0x28 = **0x2d = far**.
  Nothing to do with cfg1/cfg2, a shim return value or config files. register() worked only because it has no canary.
* libmnl has **858** `mrs Xt, TPIDR_EL0`, 0 `msr`, 0 TPIDRRO uses; every traceable use is `ldr [Xt, #0x28]` (746 checked
  within 12 instructions, the rest save Xt to the stack and reload it in the epilogue). No TLS relocations (errno is `__errno`).
* Why the host harness never saw it: mnl_emu2.py maps a TLS page and sets TPIDR_EL0 = TLS+0x100 (zero canary). Section 5.1's
  "0 mrs tpidr_el0" came from a case-sensitive grep (objdump prints `TPIDR_EL0`).

### 7.3 Fix (V: built)
* `mkimage.py` rewrites every `mrs Xt, TPIDR_EL0` (0xd53bd040|Rt) -> `mrs Xt, TPIDRRO_EL0` (bit 5, op2 2 -> 3) in the
  embedded text, `assert count == 858`; `N2MNL_TPIDR_PATCHED` in mnl_tab.h, logged at start. The canary then is
  [Darwin TSD base + 0x28] = TSD slot 5, a valid per-thread word that is the same in prologue and epilogue (the shim's
  pthread_create makes real Darwin pthreads, so every libmnl thread has a TSD).
* Not possible at run time: XNU rewrites TPIDR_EL0 on every context switch; libmnl's text is signed r-x.
* Risk (I): if TSD slot 5 ever changes under a running function (or TPIDRRO_EL0 had CPU bits in its low bits) libmnl
  would abort with `N2MNLD FATAL __stack_chk_fail: buffer overflow detected in libmnl`. n2mnld now logs
  `thread registers: TPIDRRO_EL0 ... canary word [+0x28] ...; TPIDR_EL0 ...` at start (and "LOW BITS SET" if bits 0..2 are set).
  Fallback then: patch the following `ldr [Xt,#0x28]` to `#0` (slot 0 = pthread self, constant).
* Config files: unchanged. MNL_Config.xml stays EMPTY on purpose (same as the harness that produced the kernel boot blob;
  libmnl got past the config read with it there). The trace will show every fopen/access/stat with path and result.

### 7.4 Safety net (V: built; behaviour on the phone not yet seen)
* **Import trace** (`n2trace.c` + 160 thunks in `n2shim_va.S`): every libmnl GOT slot points to a thunk
  (`adr x17,. ; b common`). From just before `mtk_gps_sys_function_register` for `--trace-seconds` (default 10) each call is
  written to n2mnld.log at once (write(2) per line): `> name(x0, x1, x2, x3) @mnl+<caller> t<tid> #n` (paths/format strings
  printed as strings for fopen/access/stat/__open_2/opendir/mkdir/remove/rename/chmod/property_get/printf/puts/fputs/atoi/
  atof/strtoul/socket/sscanf/fscanf/fprintf), and `< name = <x0>` on return (strings for fgets/strerror/strstr/strchr/strrchr/strtok).
  Max 120 logged calls per import, 30000 lines in total; at the end `N2MNLD import trace off: N lines; calls per import: ...`
  (console). Outside the window: one load + branch per call. sp is never moved, so stacked variadic args are intact.
* **Fault handler** (SIGSEGV/SIGBUS/SIGILL/SIGFPE, alternate stack for the main thread): console + log
  `N2MNLD FAULT sig .. far .. pc 0x..(mnl+0x..) lr .. ; last import of this thread: NAME (pending k)`, `N2MNLD FAULT regs x0=..`,
  `N2MNLD FAULT fp-chain ...`; then SIG_DFL + return, so the fault repeats and the normal crash report is still written.
* Build: `native/build/n2mnld` 7295440 B, sha256 **1e0871c133fd447d1ca0d4d5d6bb5103a9508ce9cd58dbae89d307e3d6acddbd**
  (build_ios.sh: layout ok base 0x100042000 data 0x1005b8000, r-x/rw, 139 imports covered; tbd + _sigaction/_sigaltstack).
  Checked in the Mach-O: mnl+0x412708 = `mrs x21, TPIDRRO_EL0`; the only TPIDR_EL0 read left is n2mnld's own diagnostic.

### 7.5 payload-gnssd2 (V: rehearse-gnssd2.sh REHEARSAL_OK, rehearse-gnssd2.log)
* apps-20261104: payload-gnssd2/{n2mnld (1e0871c1), MNL_Config.xml (0 B), MNL_Config.vendor.xml, plan.json},
  guest-gnssd2.sh, payload-gnssd2-undo/plan.json, guest-gnssd2-undo.sh, owned-gnssd2.json / owned-gnssd2-undo.json (= gnssd1's).
* Same install as gnssd1 (paths, job com.nord2.gnssd, gate, receipt /private/var/db/nord2-gnssd.json + `"payload": "gnssd2"`,
  gnssstep gate -> `.off-gnssd1`); args `--delay 60 --kboot 31 --trace-seconds 10`. Installs over gnssd1 in place (binary +
  job args replaced, the gnssd1b n2mnld.log kept, the new run appends after a `start gnssd2` line).
* Rehearsal: fresh install, idempotent, gnssd2 undo == start, idempotent undo; gnssd1 + gnssd1b log -> gnssd2 over it ->
  **guest-gnssd1-undo.sh == start**; gnssd1 -> gnssd2 -> gnssd2 undo == start; refusal on a Metal-receipt mismatch.
* txn (kernel q10jfg, sha 967ecf50):
  `cd gpu/build && PAYLOAD=payload-gnssd2 GUEST=guest-gnssd2.sh OWNED=$PWD/apps-20261104/owned-gnssd2.json READS=600 autocycle/txn_g.sh device-mmu-q10jfg-candidate/ui-sched/nord2-ios-metal-q10jfg-ui654fb60dvfs-cap3500-ram3776-sched.img gnssd2`
  undo: `cd gpu/build && PAYLOAD=payload-gnssd2-undo GUEST=guest-gnssd2-undo.sh OWNED=$PWD/apps-20261104/owned-gnssd2-undo.json autocycle/txn_g.sh <next image> gnssd2u`
  (guest-gnssd1-undo.sh with payload-gnssd1-undo / owned-gnssd1-undo.json is equivalent).
* What to look for (console `grep -a N2MNLD autocycle/runs/gnssd2/ui-usb/xnu-live.log`; n2mnld.log from the next snapshot):
  1. `start gnssd2 ... import trace 10 s, fault handler on`, `libmnl image ok ... 858 TPIDR_EL0 canary reads -> TPIDRRO_EL0`,
     `thread registers: TPIDRRO_EL0 0x... canary word ...` (no "LOW BITS SET").
  2. after kboot: `import trace on`, `mtk_gps_sys_function_register -> 0`, then **`mtk_gps_mnl_run -> N (engine threads running)`**
     = the gnssd1b crash is fixed. Then `boot proxy: libmnl sent all 50 fragments` / `boot proxy -> pass-through`, ioctl lines,
     and the stats lines every 10 s (rx frames from the DSP, NMEA count).
  3. `import trace off: ... calls per import` after 10 s.
  4. on any new crash: `N2MNLD FAULT ...` lines (pc as mnl+offset, last import) + the n2mnld.log trace tail (last `>` line
     without its `<` = the call that never returned). `FATAL __stack_chk_fail` = the canary-slot risk of 7.3.

### 7.6 gnssd2 phone result -> SIGBUS in the pthread shim -> gnssd3 (2026-11-12 ~21:50; host build + rehearsal; NOT yet run)
* gnssd2 (phone, q10jfg) [V, coordinator report from runs/gnssd2/ui-usb/xnu-live.log]: **the TPIDR fix works**
  (`thread registers: TPIDRRO_EL0 0x1f536cea0, canary word [+0x28] 0; TPIDR_EL0 0x1`), kboot K=31 0x1, mnl_run got further, then
  `N2MNLD FAULT sig 10 (SIGBUS) code 1 far 0x102cff76c esr 0x92000021 pc n2mnld+0xc754 lr n2mnld+0xad94; last import
  pthread_mutex_init (pending 1); fp-chain n2mnld+0xad94, n2t_ret, mnl+0x412ec4, main`. The new fault handler and trace did their job.
* Cause [V disassembly of the gnssd2 binary]: n2mnld+0xc754 = `ldar x22, [x0]` in obj_get: a 64-bit load-acquire on the
  guest mutex word. bionic arm64 `pthread_mutex_t` = int32_t[10], `pthread_cond_t` = int32_t[12]: only **4-byte aligned**
  (0x...76c); DFSC 0x21 = alignment fault (acquire/exclusive accesses need natural alignment; ARMv8.2 cores, no LSE2).
* Fix [V built]: n2shim.c pthread objects = **side table** owned by n2mnld (chunked array of 64-byte calloc'd Darwin objects,
  16-aligned; free list; allocation under an n2mnld mutex, lookup lock-free). The guest word 0 holds only a 32-bit handle
  `0x80000000 | index`, loaded/CAS'd with **32-bit atomics** (legal at 4-byte alignment); bionic static initialisers
  (0 / 0x4000 recursive / 0x8000 errorcheck: bits 16..31 = 0) are recognised and materialised on first use (CAS race: loser
  frees its object). `pthread_mutex_init`/`cond_init` overwrite without trusting old content (a stale handle in reused malloc
  memory could name another live object); destroy = 32-bit exchange to 0 + free + bytewise clear.
  Audit of the other shims: every 8-byte value read from / written to libmnl memory now goes through memcpy or an aligned
  local: clock_gettime, gettimeofday, pthread_cond_timedwait (timespec copied), pthread_create (*thread), pthread_join (*retval),
  sigemptyset/sigaddset/sigprocmask (sigset_t), timer_create (*timerid), timer_settime (itimerspec), sincos, printf %n.
  Layouts re-checked: timespec 16 B (same), timeval Linux {long,long} (translated), stat/dirent/sockaddr translated (section 6),
  pthread_attr_t (bionic: flags @0, stack_size @16), sigevent (notify @12, function @16), sigaction 32 B, termios 36 B, struct tm
  identical (tm_gmtoff/tm_zone). Remaining atomics in n2mnld's own code [V objdump of n2shim.o/n2trace.o]: on guest memory
  only 32-bit (`ldar w`, `ldaxr w`/`stlxr w`, `stlr w`); 64-bit ones only on n2mnld's table/globals.
* Build: native/build/n2mnld 7295680 B, sha256 **4d91d81955dbcf6cbf793300e1f0a75ecd75ae38d01a11763b24a8afe70ce550**
  (TPIDR patch 858, trace 10 s and fault handler kept).
* payload-gnssd3 (+ -undo), guest-gnssd3(-undo).sh, owned-gnssd3(-undo).json (= gnssd1's), rehearse-gnssd3.sh/.log
  **REHEARSAL_OK**. The mirror (21:36) now IS the phone with gnssd2 installed, so the rehearsal starts there: (P) gnssd3 over
  the mirrored gnssd2, idempotent, guest-gnssd2-undo.sh -> clean; then from clean + a gnssstep gate: (a) fresh/idempotent/undo,
  (b) gnssd1 -> gnssd3 -> gnssd1 undo, (c) gnssd1 -> gnssd3 -> gnssd3 undo, (e) gnssd1 -> gnssd2 -> gnssd3 -> gnssd2 undo,
  (d) refusal. Note: rehearse-gnssd1.sh / rehearse-gnssd2.sh assume a mirror WITHOUT gnssd and now stop at their start check
  (start-state assumption only; the gnssd1/gnssd2 undo scripts themselves are exercised by rehearse-gnssd3.sh).
* txn (kernel q10jfg 967ecf50; installs over the phone's gnssd2):
  `cd gpu/build && PAYLOAD=payload-gnssd3 GUEST=guest-gnssd3.sh OWNED=$PWD/apps-20261104/owned-gnssd3.json READS=600 autocycle/txn_g.sh device-mmu-q10jfg-candidate/ui-sched/nord2-ios-metal-q10jfg-ui654fb60dvfs-cap3500-ram3776-sched.img gnssd3`
  undo: `cd gpu/build && PAYLOAD=payload-gnssd3-undo GUEST=guest-gnssd3-undo.sh OWNED=$PWD/apps-20261104/owned-gnssd3-undo.json autocycle/txn_g.sh <next image> gnssd3u`
  (gnssd1/gnssd2 undo equivalent).
* Look for: no FAULT after `starting mtk_gps_mnl_run`; `mtk_gps_mnl_run -> N (engine threads running)`; pthread_create lines in
  n2mnld.log; boot proxy (`libmnl sent all 50 fragments`, `-> pass-through`); stats every 10 s; `import trace off`. A new
  crash: `N2MNLD FAULT` (pc as mnl+/n2mnld+ offset, last import) + the last `>` without `<` in n2mnld.log (fetch the log from
  the next txn snapshot). `FATAL pthread shim ...` = table full / OOM (not expected).
