# GNSS milestone 2: the data link (/dev/gpsdl0,1 read/write) -- design from the vendor gps_dl source (2026-11-12)

V = read in `nord2-current-kernel/drivers/misc/mediatek/connectivity/gps/data_link` (plat/v010 = MT6885/MT6893) or the
MT6893 DT. I = inferred. AP addresses: GPS bus 0x8000_0000+x -> AP 0x18C0_0000+x (V).

## Resources (V)
| what | where |
|---|---|
| reserved memory | DT `gps-reserve-memory`: 1 MiB, 1 MiB aligned, no-map, in 0x4000_0000..0xC000_0000. On XNU: carve a 1 MiB window out of our reserved-memory layout (like RW_CLAIM_JPEG_PA). |
| EMI remap | CONN_HOST_CSR_TOP 0x180601d0 [19:0] = window phys >> 16 (done by M1's `n2g_common_on`, today pointing at the Wi-Fi CONN window 0x8a000000; M2 points it at the GPS window). The GPS DMA then sees the window at the bus address given by `gps_dl_emi_remap_phy_to_bus_addr` (phys - window base + remap bus base). |
| buffers per link | TX and RX rings, `GPS_DL_TX/RX_BUF_SIZE` 8 KiB (the 4 KiB variant is for other platforms), plus the read/write indexes the HAL keeps in software (gdl_dma_buf_entry). |
| DMA | BGF_GPS_DMA bus 0x80010000 (AP 0x18C10000). Channels: DMA1 = L1 A2D TX, DMA2 = L1 D2A RX, DMA3/4 = L5. Per channel: WPPT +0x108 (len to wrap), WPTO +0x10C (buffer start bus addr), COUNT +0x110 (max len), CON +0x114 (DMA1 0x00128014 or 0x00128016 with the 1-byte-mode flag, DMA2 0x00078018 / 0x0007801A), START +0x118 [STR], INTSTA +0x11C, ACKINT +0x120, PGMADDR +0x12C (data start bus addr), STATE +0x148. `gps_dl_hw_set_dma_start` order: CON, PGMADDR, WPTO, WPPT, COUNT, START.STR=1. |
| USRT APB (mailbox and flow control) | L1 0x80073000 (AP 0x18C73000; same page as the AON DSLEEP_CTL), L5 0x80083000 (AP 0x18C83000): APB_CTRL +0x04, MONF +0x10, MCU_A2D0 +0x30 ... (`gps_usrt_apb.h`): RX irq enable, "has data / no data" flags, MCU<->DSP mailbox (MCUB) for DSP register reads and wake. |
| IRQs | GIC SPI 383..389, level high, in this order: LINK0_DATA, LINK0_NODATA, LINK0_MCUB, LINK1_DATA, LINK1_NODATA, LINK1_MCUB, DMA (`gps_dl_isr.h` enum order vs DT order: V for both lists, the pairing is I). |

## Flow (V, gps_each_link.c / gps_dl_hal.c / gps_dsp_fsm.c)
1. open -> link power on (M1: wake vote, common_on, DSP on) -> DSP state TURNED_ON.
2. DSP raises MCUB -> RESET_DONE: the HAL holds data routing until here. Then **MVCD**: libmnl writes the DSP code
   ("_BootDSP_UART_V2", fragments) through write(), the kernel moves it by A2D TX DMA. Timeout GPS_DSP_MVCD_TIMEOUT_MS 1000.
3. WORKING: user write() -> copy into the TX ring -> A2D DMA (DMA1). The DSP signals "has data" (USRT LINK0_DATA irq) ->
   start D2A DMA (DMA2) into the RX ring -> "no data" or DMA-done irq -> read index advanced -> read() wakes up and copies out.
4. Sleep modes (HW_SLEEP / HW_STOP) via the GPS_AON_TOP DSLEEP_CTL PWR_STAT values (M1 has the table) and the
   TAKE/GIVE wakelock ioctls (no-ops on XNU).
5. close of the last user -> DSP off, `common_off` (M1), wake vote released.

## XNU implementation plan
* One worker thread per link (thread_call is not enough: the HAL polls with timeouts). IRQs through our existing GIC hook
  pattern (as the camera/vcodec drivers do), top half = mask + wakeup, bottom half in the link thread.
* uio-based read/write into a per-link kernel bounce buffer, then into the uncached (device) mapping of the ring.
* select/poll: `selrecord` on the RX ring non-empty / TX ring not full.
* Host test: extend the fake register file with a DMA engine model (copies between the rings and a fake DSP that answers
  the MVCD handshake and echoes frames). That lets read/write/MVCD be checked before any phone run.
* Effort (I): 1-2 weeks including host model; phone validation needs n2mnld (M3) or a raw MVCD replay captured from Android.

## Implementation status (2026-11-12, host only)
* `gnss_dl.h/.c`: USRT on/off (gps_dl_hw_usrt_ctrl in DMA, 4-byte mode, DSP config 0xE, A2DF3, poll D2AF3 = ROM ok). DMA
  start/stop/done exactly as gps_dl_hw_set_dma_start/stop/wait (CON 0x00128016/0x0007801A L1, 0x00328016/0x0027801A L5;
  PGMADDR/WPTO = 0x78000000 + (phys - remap), WPPT/COUNT in words). TX ring -> A2D. RX starts on USRT TX_IND and ends on
  DMA done or NODAINTB/REGE (RLCT gives the words left, NODAINTB cleared w1c). Writes are zero padded to whole words (I:
  in 4-byte mode the vendor counts words). Register addresses resolved from plat/v010/hw/inc (V).
* `test_gnss_dl.c`: model of the DMA engine (wrap at WPTO after WPPT words), the USRT flags and an echo DSP. It checks the
  USRT sequence, the CON/COUNT values, the ROM-flag timeout, 200 random frames (157 KB) through both rings with wrap and
  backpressure, NODATA clearing, the L5 link on DMA3/4, and that no access leaves the allow-list (extended for the DMA and USRT blocks).
* Kernel **0352** (`gpu/build/gnss-m2-20261112/`, make_patch.sh checks the sources). Files: `nord2_gnss_dl.inc`, the
  extended allow-list in `nord2_gnss_pwr.inc`, and in `nord2_gpsdl.inc` the rings, USRT on at open, blocking read and
  write via uiomove, read with IO_NDELAY -> EAGAIN, the status ioctl bit 1 = link up, and a poll worker thread (1 ms while
  a link is open, 100 ms idle). In `sweet_touch.c`: ring window 64 KiB at phys 0xDFE00000, mapped uncached
  (VM_WIMG_IO); this is the gap after the vcodec pool, which redwood_vcodec.h lists as not carved out (I: unused). Plus 2
  more register pages (0x18c10000 DMA, 0x18c83000 L5 USRT). The GPS EMI remap now points at 0xDFE00000.
* `sweet_touch.c` with 0350+0351+0352 compiles under xnu_staged_compile -Werror (V).
* Image **q10jr2** = q10jr + 0352 (`build_q10jr2_gnss.sh`, TMPDIR on disk because /tmp hit the user quota).
* User-space byte-pipe tool `probe/n2gpsdl_pipe` + payload **gnsspipe1** (REHEARSAL_OK).
* Not done: IRQs (polling only), DSP sleep/stop states, power-off on last close, select().

## q10jr9 / patch 0362 "V9" (2026-11-12, host-built, NOT phone-tested yet)
* gl8b (q10jr8) verdict: frame 1 (`aa f0 09 00 05 fe ...`) is a RUNTIME command for the RAM code (API_Set_AIC_CMD), not a ROM
  command: the ROM is expected to stay silent (V, n2mnld/mnl_emu2.py). The ROM speaks libmnl's `_BootDSP_UART_V2` download:
  header 0xFE08 -> ack 0xFE31 seq 0; COMD4 0xFE05 `04 00`; 50 fragments 0xFE0C -> ack 0xFE32 seq k (runs/bootdsp_replay.json).
* Vendor deviations closed (V, gps_dl source): MCUB D2AF RESET_DONE now acked W1C (gps_dl_hal_mcub_flag_handler);
  EMI_REQ_GPS 0x18001c1c[0] claimed around DMA (gps_dl_hal_emi_usage_claim, TIA=0 on v010); USRT INTEN TXIEN|NODAIEN set
  (gps_dl_hw_usrt_ctrl); RX DMA2 armed on a 0xA5-filled ring BEFORE TX (vendor arms it from the has-data IRQ, I: pre-arming
  is equivalent and cannot miss a reply); RX-round bug fixed: REGE [5] (set at idle) was treated as no-data.
* DMA register order (CON, PGMADDR, WPTO, WPPT, COUNT, START), word units, padding and channel mapping already matched the vendor (V).
* DIS_HW_RST_CNT (0x18c73108[4]) is written 1 as the vendor does but reads back 0 (observation).
* Ladder K=23 (ioctl 163, payload gnssl9a) = capture of frame 1 with RX armed; K=24 (ioctl 164, payload gnssl9b) = V9 HOLD +
  user-space replay of the boot download (probe/n2gpsdl_replay.c, frames embedded from libmnl via n2gpsdl_bootdata.h).
* gl9b (q10jr9 K=24 HOLD + boot replay): phone RESET 2..4 s after "V9 HOLD ... N2GRPL hold result=0x1", before any reply line.
  q10jr10 / patch 0363 splits it: S10a..e = K 25..29 (ioctl 165..169, payloads gnssl10a..e): arm-only, arm+TX header,
  TX header with no RX DMA (INTEN off / on), arm + wake vote released. RX capped at 16 words, 2 s "about to" before each action,
  GIC ISENABLER/ISPENDR of the GPS INTIDs 415..421 printed.
* gl10c: header TX with RX DMA idle -> STA 0x110008 TX_IND, no reset. gl10b: RX DMA2 armed 16 words + header -> 20 bytes
  `aa f0 0e 00 31 fe 00 00 13 19 00 00 00 00 00 00 69 01 aa 0f` = ROM header ack 0xFE31 (V on phone). RX DMA write to 0xDFE02000 works.
* q10jr11 / patch 0364 (gnss-dl4-20261112): S11 = full DSP code download in one ladder ioctl (K=30 rollback, K=31 HOLD with the
  wake vote held). Frames are uploaded by user space (N2GD_IOC_CHUNK _IOW 'G' 180, 4 KiB chunks, validated) because 70.7 KB > boot spare.
  Payloads gnssl11a / gnssl11b (tool probe/n2gpsdl_replay.c --kboot 30|31).

## q10jfg merge (2026-11-12, host-built, NOT phone-tested)
* q10jfg = q10jfe (camera/display: 0390 fbblend, 0391 fbdiag, 0392 fbretain + AE2 bundle cam60-20261112/audio-q10jae.pkg 94fb2f9b..., 255216 B)
  + the q10jr11 GNSS set 0350,0351,0353,0354,0355,0356,0357,0358,0359,0362,0363,0364. Script gpu/build/build_q10jfg.sh.
* Lineage: both lines = hq series + 0296..0340 (q10jm kernel). q10jfe-only: 0390..0392. q10jr11-only: the GNSS set above.
  0352 (gnss-m2) and 0360/0361 (q10jq line) are in neither line and stay out.
* Conflicts: none textual. 0390..0392 touch only iokit/Kernel/arm/SweetFramebuffer.cpp, sweet_fb_blend.h, sweet_fb_scene.h;
  the GNSS set only bsd/dev/nord2_gpsdl.inc, nord2_gnss_pwr.inc, sweet_touch.c. All hunks applied clean (no offset/fuzz).
  Order: GNSS first, 0390..0392 last: build_p1_candidate.py checks tracked sources against patched-sources-NNNN with NNNN <= series[-1],
  so with 0364 last the 0392 SweetFramebuffer.cpp check would not hold.
* Boot-args: q10jfe 606 chars (limit 607); keepsyms=1 dropped (symbols stripped by the packer anyway, as in q10jr) + n2gps=1 -> 603.
* Budget (boot_spare = 32 MiB - image bytes): q10jp (q10jm kernel + 255216 B pkg) 81920; fb set -4096 (q10jfe 77824); GNSS set -24576
  (q10jr11 57344 with the same-size q10jp pkg; the AE2 pkg is not additive, it replaces a pkg of identical size). Predicted 53248,
  measured 53248. No trim needed. Possible trims if ever needed: the K=1..30 ladder / probe / map / single-access ioctls (0354..0363
  debug paths); the final path only needs K=31 HOLD (ioctl 171), N2GD_IOC_CHUNK _IOW('G',180) and /dev/gpsdl0 read/write/N2GD_IOC(n).
* Result: IMAGECHECK_OK, check.log FAILURES: none, kernel 870e0c49..., image
  gpu/build/device-mmu-q10jfg-candidate/ui-sched/nord2-ios-metal-q10jfg-ui654fb60dvfs-cap3500-ram3776-sched.img
  33501184 B, sha256 967ecf50d9e4f77514e0ca00e130597977339907f452f0e14b93108c8c1c9797.
* Risk: sweet_touch.c (GNSS bind/allow-list) is not touched by 0390..0392; no shared file. Untested combination on the phone:
  GNSS conninfra/DMA activity alongside the 60 Hz fb composition + camera load; and spare is 53 KB, so the next ~50 KB of kernel
  growth needs a trim.
