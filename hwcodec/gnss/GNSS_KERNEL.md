# Nord2 real GNSS: kernel side (patch 0350) and full path (2026-11-12)

V = verified (source, vendor files, host test). I = inferred. Source pin: `nord2-current-kernel` (MT6893 vendor kernel
in the repo), `drivers/misc/mediatek/connectivity/gps/data_link` with platform **plat/v010** ("SUPPORT:MT6885,MT6893",
V) and `conninfra/platform/mt6893`.

## 1. Where the GNSS firmware is (V)
* **Not a file.** vendor/firmware (82 files) has Wi-Fi/BT/MCU soc3_0 blobs and FM, nothing GNSS. The GPS kernel driver never
  calls request_firmware.
* The GNSS **DSP code is downloaded by libmnl.so itself over the data link** after the DSP reports RESET_DONE: the
  "MVCD" state of `hal/inc/gps_dsp_fsm.h` ("DSP raw code download"), plus libmnl strings `_BootDSP_UART_V2(): MEND,code_frag_num`,
  `Send_DspCode fail,L1`, `mtk_gps_clean_mvcd_recv_state`. The rest is in the GNSS core's ROM (I).
* So the kernel only powers the core and moves bytes. All the "firmware" travels with `libmnl.so` (sha 9a1e1be7).

## 2. Power-on sequence for MT6893 (V, host-tested model: `gnss_pwr.c`)
Order of `gps_dl_hw_gps_common_on()` (hw/gps_dl_hw_power_ctrl.c:182) with the plat/v010 dep functions. Steps the vendor
delegates to the conninfra driver are inlined from `mt6893_pos.c` and marked [ci]. Addresses were resolved by compiling
plat/v010/hw/inc on the host.

| # | step | register (AP physical) | value |
|---|------|------------------------|-------|
| 0 | wait 3T 32 kHz | - | 200 us |
| 1 | poll conn_infra version | 0x18001000 | == 0x20010101 (MT6893) or 0x20010000 (MT6885); our phone reports HW20010101 (NOTES) |
| 2 | restore-done check, conn2ap peri remap | - | no-ops on v010 |
| 3 | GPS EMI remap | 0x180601d0 [19:0] | phys(DMA buffers) >> 16, 64 KiB aligned |
| 4 | [ci] BGFSYS_ON_TOP power | 0x18000008 | key 0x4254 in [31:16], [7]=1 (mask 0xffff0080) |
| 5 | GP_FUNCTION_EN | 0x18001878 [0] | 1 |
| 6 | poll BGF power acks | 0x18000114 [24] then [25] | 1 |
| 7 | wait | - | 200 us |
| 8 | sleep protection off (rx, tx; conn2gps then gps2conn) | 0x18001618 / 0x1800161c: EN [4]/[0] -> 0, wait RDY [7]/[3] == 0 | |
| 9 | gps_top_off ACTIVE | 0x180600b0 = 0x80, poll 0x18060240 [15:0] == 2 (up to 3 rounds) | |
| 10 | [ci] A-die top clock (GPS vote) | 0x18001900 [1] = 1, poll 0x18005008 [26] == 0 | |
| 11 | PLL driver | GPS bus 0x8000103c [4] = AP 0x18C0103C | 1 (BPLL: no-op on MT6893) |
| fail | rollback | 0x18001878 [0] = 0, 0x18001c1c [0] = 0 | |
| DSP | L1 on (pwr_stat_ctrl GPS_L1_DSP_ON) | GPS bus 0x80073108 = AP 0x18C73108: [1:0]=0, [3]=0, [4] 1 then 0 | |

The GPS bus maps 0x80000000+0x90000 to AP 0x18C00000 (`g_gps_addr_table`, V; DT `gps@18C00000` reg `conn_gps_base`).
The DT also gives the IRQs: GIC SPI 383..387 (V).

## 3. What our Wi-Fi bring-up already provides (V from shim/nord2-xnu/WIFI-*.md and bsd/dev/wifi_*)
* **Persistent conninfra init** (`wifi_persistent*.c`, WIFI-PERSISTENT.md): EMI remap registers, D-die setup, common
  register table, SYSRAM clear, A-die E3 setup, efuse, AFE calibration, PLL, RC low-power, top-clock ownership. GNSS needs
  all of this and must not redo it.
* **PMIC rails** VCN18/VCN13 (persistent path) and VCN33 (`wifi_vcn33.c`, Wi-Fi only). GNSS has no rail of its own:
  `consys_plt_pmic_gps_power_ctrl()` returns 0 on MT6893 (V).
* **AP wake / HOST1a0 vote** (`wifi_wake.c`): conn_infra awake. GNSS must run inside the same owner window, or hold its own vote.
* **A-die SPI / semaphores / TOP-SPI** (`wifi_adie_*`, persistent adapter): the GPS RF registers (CONN_RF_SPI_MST 0x18004000,
  semaphore 11 M3 for GPS) go through the same mechanism.
* **Not reusable as is**: WFSYS CPU release and firmware loader (Wi-Fi MCU). GNSS has no MCU firmware to load.

## 4. Missing pieces and effort (I)
| piece | what | effort |
|---|---|---|
| M1 (done, host only) | `gnss_pwr.c` power core + `kern/bsd/dev/nord2_gpsdl.inc` nodes, gating, status ioctls; `run_tests.sh` | done |
| M1b | bind `n2gd_conninfra_ready()` to the Wi-Fi persistent/wake owner, map the 4 allow-listed pages (0x18000000, 0x18001000, 0x18005000, 0x18060000) + 0x18C00000/0x18C73000, phone test: `NORD2 GNSS: powered (conn_infra 0x20010101)` | 1-2 days |
| M2 | data link: reserved EMI window (2 links x TX/RX, 8 KiB rings), BGF_GPS_DMA (bus 0x80010000) channel setup, USRT APB mailbox (gps_usrt_apb.h), IRQs SPI 383..387, DSP fsm (RESET_DONE/MVCD/WORKING), read/write/select | 1-2 weeks |
| M3 | userland `n2mnld`: elf2macho conversion of libmnl.so + bionic shim (GNSS_ENGINE.md: 139 imports), Linux ioctl-number mapping to `N2GD_IOC(n)`, NMEA out to `/private/var/run/n2gnss.nmea` | 1-2 weeks (callback-table RE is the risk) |
| M4 | bridge: `n2locbridge --fifo` (payload-locbridge1, already built and host-tested) | done |
| M5 | assistance: EPO/QEPO files, NTP time inject (mtk_gps_inject_ntp_time), needs network | later |

## 5. Files
* `gnss_pwr.h/.c`: power core. `test_gnss_pwr.c`: fake CONNAC2 register file covering the exact write sequence, the
  failure paths and the allow-list.
* `kern/bsd/dev/nord2_gpsdl.inc`: patch 0350 skeleton. `test_gpsdl.c`: kernel shims plus a fake conn_infra; checks gating
  (no MMIO without conninfra), bad version, the powered state, the ioctls, and ENODEV on read/write.
* `run_tests.sh`: runs both and compiles `gnss_pwr.c` for arm64-apple-ios16 freestanding with -Werror; ends with `GNSS M1 TESTS OK`.

## 6. M1b: the real integration (kernel q10jr) and the phone test
* Series dir `gpu/build/gnss-20261112/`: `a/` = q10jm stage `sweet_touch.c`, `b/` = patched, `make_patch.sh` builds:
  - **0350** `bsd/dev/nord2_gnss_pwr.inc`, which is gnss_pwr.h + gnss_pwr.c verbatim with `N2G_API static __attribute__((unused))`
    (make_patch.sh refuses if it differs from the host-tested sources), and `bsd/dev/nord2_gpsdl.inc`.
  - **0351** `sweet_touch.c`: include after nord2_jpegdev.inc. Predicate `n2gd_conninfra_ready()` = Wi-Fi retained
    persistent owner (`npa_persistent_lifetime && !fatal && phase == 10`; the boot logs say "retained=1 ... stage10").
    6 pages mapped with ml_io_map on the first open: 0x18000000, 0x18001000, 0x18005000, 0x18060000, 0x18c01000, 0x18c73000.
    Accessors refuse any address outside gnss_pwr's allow-list (16 registers). The GPS EMI remap points at the Wi-Fi
    CONN window 0x8a000000 until M2. `nord2_gpsdl_start()` is called from `nord2_input_init()`.
  - The power path is gated by boot-arg `nord2gnss=1` or its short alias `n2gps=1`. The image uses `n2gps=1` because
    the boot-args are capped at 607 characters; to make room `keepsyms=1` is dropped, since the packer strips the kernel
    symbol table anyway.
* `sweet_touch.c` with both patches compiles under `xnu_staged_compile.py` (-Wall -Wextra -Werror) (V).
* Build: `gpu/build/build_q10jr_gnss.sh`. It uses q10jm's series + 0350,0351 and q10jp's packing (audio-q10jp.pkg). The
  image check requires `FAILURES` clean, `n2gps=1` in the image and the GNSS strings in the kernel.
* Phone test, payload **gnssprobe1** (`gpu/build/apps-20261104/`, rehearsed REHEARSAL_OK): `/usr/local/libexec/n2gpsdl_probe`
  + one-shot launchd job `com.nord2.gnssprobe` (gate `/private/var/db/nord2-gnssprobe`). 90 s after boot it opens
  /dev/gpsdl0 and /dev/gpsdl1 and prints `N2GPS /dev/gpsdl0 open ok status=0x1 (powered=1 err=0) ... hwver=0x20010101`.
  Expected kernel lines: `NORD2 GNSS: /dev/gpsdl0,1 made (major N), power path enabled`, `NORD2 GNSS: hw_ver 0x20010101 0`,
  `NORD2 GNSS: common_on_ok 0x20010101 0x8a000000`, `NORD2 GNSS: powered (conn_infra 0x20010101)`.
  Failure modes: `power on failed, step N` (N = enum n2g_err: 2/3 BGF acks, 4 sleep protection, 5 gps_top, 6 A-die clock,
  8 wake), or ENODEV when the Wi-Fi owner is not retained.
