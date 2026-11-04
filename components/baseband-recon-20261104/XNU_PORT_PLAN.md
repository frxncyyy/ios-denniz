# Plan: MT6893 modem under XNU/iOS

Oracle: the rescue Linux (4.19, MediaTek eccci driver) + `n2mdinit` + `nvram_daemon`; every XNU step is checked against the kernel log
`/proc/ccci_dump` sequence and the traces in this directory. Source of the kernel half: work/oneplus-mt6893/drivers/misc/mediatek/eccci (36k lines, sparse clone).

## Design: keep the Linux split
* **Kernel (new IOKit/BSD part "Nord2Modem")**: power-on, CCIF mailbox, FSM/handshake, queues, ports exposed as device nodes
  (ccci_monitor, ccci_fs, ccci_rpc, ccci_ccb_ctrl, ccci_ipc_5, ttyC0, ccmni0...), DPMAIF data path.
* **User space (launchd daemon `n2mdinit` for Darwin)**: the same 14 KB program, syscall layer swapped (open/read/write/ioctl/lseek/stat/getdents/unlink via libSystem).
  NVRAM/protect/mdota files live under /private/var/db/nord2-modem/{nvdata,protect_f,protect_s,vendor_etc}. This is the file-service contract in PROTOCOL.md.
* **Baseband bridge (user space, AppleBaseband-style, after the above)**: AT <-> CommCenter (QMI emulation selected via the ADT baseband-chipset), data via ccmni.

## Kernel work items (in order, each testable on the phone with the USB log)
1. Platform power-on (md_sys1_platform.c): PMIC vmd1 on, INFRA_AO MD_SRCCLKENA (0x10000F0C) |= 0x21, SPM sleep reg write/OR bit21, SCPSYS power domain "scp-sys-md1-main"
   + infra clocks (ccif ap/md, dpmaif), SMC (MTK_SIP_KERNEL_CCCI_CONTROL): MD_FLIGHT_MODE_SET, MD_POWER_CONFIG{MD_CHECK_DONE, MD_CHECK_FLAG, MD_BOOT_STATUS, MD_KERNEL_BOOT_UP}, MD_CLOCK_REQUEST.
   LK already loaded the image into the reserved region (0x8a000000 MD view 0x48000000; shared mem 0x8e000000 MD view 0x40000000). Goal: "modem started 0" in the XNU log.
2. CCIF (ap_ccif0 @0x10209000, ccif sram @0x1020a000, IRQ spi 0x4e/0xc2/0xc3): channel ring buffers, doorbell, SRAM message exchange.
3. FSM + runtime data (ccci_fsm.c, ccci_modem.c, modem_sys1.c, ccci_util/lk_info): prepare_runtime_data with 39 features, handshake 1/2, md_state 1->2->3->4.
4. Ports: CCCI_CONTROL, FS (14/15), RPC (32/33), IPC, AT (ttyC0), CCB smem (20 configs + 3 ctrl-info users).
5. DPMAIF (hif/dpmaif_drv.c, ccci_hif_dpmaif.c ~6.3k lines) for ccmni0..n.
6. Memory: keep 0x8a000000-0x8f800000 (80 MiB) out of the XNU RAM window; MD MPU is configured by ATF through the runtime-data exchange.

## Size estimate
Items 1-4 roughly 3-4k lines of new XNU code (CCIF + FSM are the intricate parts); DPMAIF alone ~2-3k. Several sessions with phone tests; the file-service contract and boot ioctl sequence are already settled.

## Known unknowns to resolve first
* exact PMIC (MT6359) writes behind vmd1_pmic_setting_on (not in the sparse clone: add drivers/misc/mediatek/pmic* and include/ to the sparse checkout)
* SCPSYS md1 power-domain bits (the project already has nord2_gpu_scpsys.c for MFG domains as a template)
* nvram_daemon replacement (what it writes to nvdata before the first boot)

## Resolved unknowns (2026-11-04)
* SMC: function id MTK_SIP_KERNEL_CCCI_CONTROL = 0xC2000505 (arm64 SMC64, x1 = sub id). Sub ids (enum CCCI_SECURE_REQ_ID): 0 MD_DBGSYS_REG_DUMP, 1-4 HW_REMAP, 5 MD_CLOCK_REQUEST, 6 MD_POWER_CONFIG, 7 MD_FLIGHT_MODE_SET.
  MD_POWER_CONFIG x2 sub: 0 KERNEL_BOOT_UP, 1 LK_BOOT_UP, 2 CHECK_FLAG, 3 CHECK_DONE (0 = BROM pass), 4 BOOT_STATUS.
  MD_CLOCK_REQUEST x2 sub: 0 MDSRC_REQ (x3 = lock), 1 ACK, 2 SETTLE (returns ms to wait). Flight mode: x2 = 1 (on) / 0 (off).
  Boot order in Linux: clock src lock -> power on -> md_start_platform (poll CHECK_DONE up to 100x20ms, read CHECK_FLAG/BOOT_STATUS) -> KERNEL_BOOT_UP.
* vmd1_pmic_setting_on() is NOT a power-up: it only re-applies the previously recorded VMODEM/VRFDIG vosel (MT6359: RG_BUCK_VMODEM_VOSEL + VSRAM_MD 0x4F; MT6359P: VPU vosel as VRFDIG).
  LK already powered the rails to load the image, so on iOS the first milestone can skip the PMIC (verify rail state; fall back to reading vosel only).

## STATUS 2026-11-04 — milestone: the modem boots from XNU (q10c, patch chain ...0203,0205,0213 in gpu/build/kernel-p1-20261026)
Working power-on recipe (all verified on hardware, see log gpu/build/hardware-20261025/q10c-test/ui-usb/xnu-live.log):
 1. MD1 MTCMOS power cycle (LK leaves it on; Linux has it off): bus protect step1 (PROTECTEN_SET 0x2A0 b7, ack STA1 0x228) / step2_0 (VDNR_SET 0xB84, ack 0xB90, mask b10|b21|b29) / step2_1 (SUB_INFRA_VDNR_SET 0xBB8, ack 0xBC4, mask b6|b7); MD1_PWR_CON(0x300) &= ~PWR_ON; wait PWR_STATUS(0x16C) b0 = 0; EXT_BUCK_ISO(0x398) |= 3; PWR_CON &= ~RST_B.
 2. Clear the safe smem regions (MD_WIFI_PROXY 0x8e100000+0x10000, 0x8e110000+0x18000, 0x8e129000+0x3000). CCISM_MCU (0x8e139000+0xd2800) and the cacheable banks HANG when written/read from AP in the current MPU state - still to be located (probably EMI-MPU domains).
 3. SMC 0xC2000505 sub 6: CHECK_DONE(3)=0, CHECK_FLAG(2)=1111, BOOT_STATUS(4)=0 0 0 after the cycle.
 4. infracfg_ao+0xF0C |= 0x21 (already set by LK), SPM +0 = 0x0B160001, SPM+8 |= 1<<21, SMC flight mode off (sub 7, arg 0).
 5. MD1 domain on: ISO &= ~3, RST_B=1, PWR_ON=1, wait PWR_STATUS b0, release protects (CLR regs 0xB88, 0xBBC, 0x2A4).
 6. SMC MD_POWER_CONFIG/MD_KERNEL_BOOT_UP (sub 6, arg 0) -> 0 0 0.
 -> within 250 ms the MD sends its first CCIF message: RCHNUM(ap_ccif0+0x10)=0x8000, SRAM(+0x100): dl_header {ffffffff,0,0,5555ffff(MD_INIT_CHK_ID)} then md_query_ap_feature {head 0x49434343, feature_set[64] nibbles (03/02...), tail pattern}.
Next (kernel side of the handshake, all from Linux ccci_modem.c / hif_ccif.c): ACK the channel (APCCIF_ACK 0x14 = 0x8000), parse md_query_ap_feature, build ap_query_md_feature via config_ap_side_feature()/ccci_md_prepare_runtime_data() (39 features, runtime data in smem RAW_RUNTIME_DATA 0x8e11e800 + per-feature structs, share_memory_support, ap_runtime_data_addr in MD view 0x4011e800...), write into SRAM up_header+ap_rt_data and kick CCIF TX channel; then MD answers MD_CCCI_READY etc. -> fsm md_state 2->3->4; then ports (fs ch14/15, rpc, ccb), userland n2mdinit, AT, DPMAIF.

## Status 2026-11-05 morning: modem + SIM + network + data working inside the iOS kernel

Final daily image: `gpu/build/autocycle/q10bo-FINAL-image` (sha256 feb2d28e7ad708cf2ef060e9284755948692e99a4f3f74ab8639a8f0a9372216),
kernel patch series ...0205,0276 (`kernel-p1-20261026/0276-nord2-modem-p2f.patch`, source in `patched-sources-0276/bsd/kern/`).

What runs at every boot (bsd/kern/sweet_amfi.c, thread at +10 s):
1. MD1 power cycle (SPM/bus protect) + SMC BOOT_UP; DPMAIF hw start inside the MD1-off window (remap OK there).
2. CCIF rings (16 main + 16 exception, ONE wcomb mapping 0x8e139000..0x8e20c000), CCB ctrl headers (20 x 64 B at 0x8e128000).
3. Runtime-data handshake, CCCI service (control/HS2, sysmsg, RPC with Linux GPIO/EINT/DT answers).
4. In-kernel NVRAM FS server: 277 KB embedded blob + raw blob n2nvdisk.bin at userdata+64 GiB (read via /dev/rufsroot0); ~4000 ops; READY at ~1.5 s.
5. AT client (ch 12/10 = ttyC0): CFUN=1, SIM READY, registration (VERY 222/88, LTE), PDP internet.it.
6. Data path: DPMAIF DL/UL polling, interface pdp_ip0 (IPv4 /32, default route only if none), self-test ping+DNS 8.8.8.8.
7. Persistent service + status line every 60 s; /dev/n2at (single-open AT bridge for userspace); sysctl net.nord2.pdp.

Hard lessons: unanswered LK modem takes iOS down ~38-48 s after boot -> start early; Wi-Fi bring-up (~21-25 s) must not overlap the MD1 cycle;
per-ring ml_io_map exhausted the I/O VA -> kernel panic; Device mappings of smem hang; CCIF hw reset via infracfg_ao 0x150 hangs.
Automated loop: RTC PDN1 bit13 (fastboot) armed at boot -> any crash returns to LK fastboot; scripts in gpu/build/autocycle.
Not done yet: CommCenter / iOS telephony UI integration (status bar, calls, SMS UI); iOS still prefers Wi-Fi for its default route.
