# n2mnld (GNSS M3): status 2026-11-12, host only

V = verified by running/disassembling, I = inferred.

## Done
* **Host harness `mnl_emu.py`** (`run_emu.sh`): MediaTek's real `libmnl.so` (sha 9a1e1be7) runs under the Unicorn AArch64
  emulator on this x86_64 host. Our own ELF loader: LOAD segments, ANDROID_RELR, the JUMP_SLOT GOT. Preemptible exports
  are resolved to libmnl itself; the 139 libc imports go to Python implementations (this list is the executable spec of
  the bionic shim). Linux-AAPCS64 variadics and va_list are decoded in `VA`/`cfmt`: on iOS the shim must do the same,
  because Apple passes variadics on the stack. Cooperative threads: pthread_create queues, blocking calls park.
  Fake /dev/gpsdl byte pipe.
* **Callback table (V, from mnld 0x3f148: `mtk_gps_sys_function_register(&table)`, table at mnld 0x60b80):** 20 slots of
  8 bytes; mnld fills 0..16, slots 17..19 stay NULL; libmnl copies them all into its own globals (0x57e6a8..0x57e740).
  * slot +0x00 = **NMEA out**, the only caller is `mtk_gps_sys_nmea_output_to_app_print` (V).
  * slot +0x08 = **log/debug text out** (buf, len): every "MNL-LOG" line in the harness comes from it (V).
  * the other slots are called from the AGPS/EPO/debug paths, clk extension, rtk interface (call sites listed by
    `mnl_emu.py`; their meaning is still I). mnld implementations are at mnld 0x3c6d4, 0x55d60, 0x55f0c, 0x56558, 0x566c0,
    0x56368, 0x3e0e8, 0x3c528, 0x3c52c, 0x3c530, 0x3c568, 0x3c59c, 0x52af0, 0x3c534, 0x34de4, 0x3e690, 0x3e950.
* **Run 2 result (V, runs/run2.log):** register returns 0. `mtk_gps_mnl_run(cfg1, cfg2)` with zeroed configs returns
  after creating its 5 threads (file_control, debug socket :7000, **thread_dsp_input_func**, the mnl main thread, the
  agent), validates the driver config ("m = 0xaa55/0x0000 ..."), opens the device (empty path from the zeroed config),
  issues GPSDL ioctls 7, 8, 9 (IC_HW_VERSION, IC_FW_VERSION, D1_EFUSE_GET) and reads MNL_Config.xml / cache.dat.
  The DSP input thread is polling the UART on fd 0 ("UART polling (length=-1, fd=0)"). The DSP code download is **not
  reached yet**: libmnl first needs (a) a valid config with the device name/fd and the chip id/HW version answers (the
  ioctl replies were 0: "ChipID,x,0,y,0,hw_ver,0"), and (b) the DSP's ROM handshake bytes on the pipe.
* **Config (partial, V from mnld 0x430e0):** cfg1 = mnld 0xa26b8, 0x74 bytes: +0x00 double (constant mnld 0x1ac08),
  +0x14 u16 = 100, +0x20 u32 = 115200 (baud), others zero, then filled from properties/XML. cfg2 = mnld 0xa272c, 0x434
  bytes, memset 0 then filled. The chip id string compares in that function ("0x6765/0x6768/..." style) choose the
  device node; MT6893 takes /dev/gpsdl0 (string mnld 0x93f0) (I).

## Update (second session, V unless marked)
* **cfg2 decoded enough to reach the device** (libmnl mtk_gps_mnl_run = 0x4126e0: cfg1 (0x74) and cfg2 (0x434) are
  memcpy'd into libmnl globals). Then:
  - cfg2+0x280 int = the L1 DSP device fd (mnld opens /dev/gpsdl0 itself); goes to libmnl global 0x57b264 and is used
    for read and ioctl.
  - cfg2+0x284 int = the L5 fd.
  - cfg2+0xfd char[] = port name; the magic "UseCallback" means no open().
  - cfg2+0x04 flags, bit 3 = a second port named at +0x11b.
  - cfg2+0x394..0x39a u16 and +0x3a5/0x3a6 bytes are logged; +0x05.. is a string.
  The harness now passes fake fds 10/11, and libmnl logs "NMEA output by callback!!!".
* **ioctls like the real MT6893 kernel**: in gps_each_device.c only 11, 12, 13, 14, 15, 18, 19, 20 exist; 6..9
  (HWVER, IC_HW/FW_VERSION, EFUSE) are compiled out, so they return -EINVAL. libmnl then logs
  "ChipID,x,0,y,0,hw_ver,0,fw_ver,0,gnss_mode_flag,FFFFFFFF" and carries on. Note: our 0350 answers HWVER with the
  conn_infra version, which the real driver does not; harmless, since libmnl does not use it on this path.
* **First DSP frame**: libmnl writes 15 bytes to the L1 pipe: `aa f0 09 00 05 fe 32 00 00 01 00 3f 01 aa 0f`
  (preamble AA F0, length 9, body, end AA 0F; I: a "query/boot" command to the DSP ROM). The writer is the
  __write_chk wrapper at libmnl 0x3f6da4.
* **Where it waits now**: thread_dsp_input_func (0x40f67c) blocks in read(fd 10) through 0x3f6d6c, waiting for the
  DSP's answer. The main loop mtk_gps_run (0x416448) polls its message queue (0x3f6dc8; usleep 1 ms when empty) and
  dispatches message ids > 1000 through a jump table at 0x397854. `_BootDSP_UART_V2` (L1 0x3e10xx..0x3e1fcc, L5
  0x3e2cxx..) is **not reached**: the harness has markers there (REACHED lines) and none fired.
* runs/r4.log and r5.log are the reference traces.

## Remaining for M3 (I, estimates)
1. (mostly done, see the update) Finish the config structs (decode the rest of mnld 0x430e0.., the XML parser and the property reads) and answer the
   ioctls like the kernel (chip id 0x6893, HW/FW versions from 0350's HWVER). Then the harness should reach the DSP
   boot (`_BootDSP_UART_V2`) and show the MVCD frames on the fake pipe. ~1 day.
2. Fake DSP ROM handshake: decode the parser behind 0x3f6d6c (the DSP-input thread's frame decoder) and the answer it
   expects to the AA F0 ... 05 fe ... AA 0F frame, then which message (id > 1000) makes mtk_gps_run call
   _BootDSP_UART_V2. Alternative: capture the real exchange on the phone once M2 (gnsspipe1) is up. A 'read' of a few
   seconds after the first write would show the DSP ROM's own answer. ~1 day.
3. iOS runtime: elf2macho (blob in __TEXT at +0x2000 so the RW part lands 16 KiB aligned at 0x578000; RELR + GOT patched
   at load) + the C shim (the Python implementations above, plus a Linux-varargs trampoline in assembly, pthread/epoll->kqueue,
   errno/flag/struct translations) + n2mnld main (callbacks: slot 0 -> the locbridge FIFO). 1-2 weeks.
4. Payload payload-n2mnld1: not staged, since no binary exists yet.

## Update 2026-11-12 evening (agent, V = harness run / disassembly)
* cfg1 word 0 is a u32 = 1 (mnld 0x1ac08 bytes `01 00 00 00 00 00 00 00`), not a double: with it 0 libmnl skips
  mtk_gps_dsp_init and never boots the DSP. MT6893 chip fields: cfg1+0x2c = +0x30 = 0x250, +0x34 = +0x3c = 0xFFFF6893 (mnld 0x45d2c).
* mnl_emu2.py (clock, fake DSP `fake_dsp_rom.py`) runs `_BootDSP_UART_V2` to "finished" (runs/e6.log). Protocol + replay:
  runs/bootdsp_replay.json, runs/bootdsp_tx.bin. Frame format AA F0 len id payload cks16 AA 0F, stuffing AA->DE DF, DE->DE E0.
* Header RF/config bytes come from the zeroed harness config (I: real mnld may differ; L1-only flag 0x66 = 1).

## 2026-11-12 ~21:10 native n2mnld (iOS) built, payload-gnssd1 staged: see ../../GNSS_ENGINE.md section 6
* native/build/n2mnld sha 2e827760..., host-built only (never executed); payload-gnssd1 REHEARSAL_OK; kernel q10jfg.

## 2026-11-12 ~21:35 gnssd1b crash fixed on the host, payload-gnssd2 staged: see ../../GNSS_ENGINE.md section 7
* gnssd1b: SIGSEGV at libmnl 0x41270c (mtk_gps_mnl_run body 0x4126e0) = bionic stack-protector `mrs TPIDR_EL0; ldr [#0x28]`;
  TPIDR_EL0 is the CPU number on iOS. mkimage.py now rewrites the 858 reads to TPIDRRO_EL0. New n2trace.c: import-call
  trace (10 s) + fault handler. native/build/n2mnld sha 1e0871c1...; payload-gnssd2 REHEARSAL_OK (gnssd1 undo still works).
* ~21:50 gnssd2 on the phone: TPIDR fix confirmed; new SIGBUS = 64-bit `ldar` on a 4-byte-aligned bionic mutex in the pthread
  shim -> side table + 32-bit handles, memcpy for all 8-byte guest values. n2mnld sha 4d91d819...; payload-gnssd3 REHEARSAL_OK.
  GNSS_ENGINE.md 7.6.
