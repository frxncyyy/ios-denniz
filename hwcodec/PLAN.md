# Nord 2 / iOS 16.7 — hardware video and JPEG codecs (MediaTek MT6893 blocks under XNU)

Started 11 November 2026. Owner of `gpu/hwcodec/`: the hwcodec agent. `gpu/video/` (software codecs, VideoToolbox plug-in) is read only
for this work. Every claim is **[V]** verified today (path / line / symbol given) or **[I]** inferred. **Nothing in this file has been
run on the phone** unless a log file is named next to it.

Paths: `K` = `$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel` (OnePlus Android
kernel), `VFS` = `gpu/build/cam-vendor-20261109/vfs` (the phone's own `/vendor`, extracted), `ST` =
`gpu/build/autotest/q10at3/kernel/stage` (our XNU sources as built into the last good image q10ik).

## 0. Summary

| Block | Who programs the registers on Android | Open register code in `K`? | Verdict |
|---|---|---|---|
| **JPEG encoder** 0x17030000 | the kernel (`mtk-jpeg` V4L2 driver) | **yes, all of it** (230 lines) | **Milestone 1-3. Days.** |
| JPEG decoder ("hybrid") 0x17040000 / 0x17050000 / 0x17840000 | user space computes 21 register words, kernel writes them | register *numbers* yes, their meaning only for the 2015 decoder | Milestone 6, uncertain |
| **VENC** H.264 / HEVC 0x17020000 (+ core 1 0x17820000) | closed user-space library `libvcodecdrv.so` inside the daemon `vpud` | no (kernel is a relay) | Milestone 4-5: run the vendor library, see §4. **Weeks.** |
| **VDEC** H.264 / HEVC / VP9 / AV1 / MPEG 0x16000000 | same library | no | after VENC, same mechanism |
| MDP (scaler / colour converter) 0x1f000000 | kernel CMDQ driver replays command buffers built by closed `libdpframework` | engine register headers yes (`mdp_reg_*.h` not in this tree: only base tables) | not planned; the Mali does this job today |

Chosen architecture: **(2) a VideoToolbox plug-in in user space + a small XNU driver per block** (`/dev/redwood-jpeg` first, then
`/dev/redwood-vcodec`). Not (1): imitating AppleAVD / AppleAVE2 / AppleJPEGDriver user clients means reproducing private protocols spoken
by 240 KB / 850 KB of Apple user-land code (`gpu/video/PLAN.md` §1.4), while a plug-in is a four-function object whose ABI is already
frozen and tested in `gpu/video/include/n2vt_abi.h`.

Earliest hardware proof: **M1 = a JPEG produced by the MT6893 encoder from a test picture generated in the kernel, read back over USB
and decoded on the PC.** No user-space change, no root file system change, one flash. The image for it is built (see §7).

## 1. How the blocks are driven on this SoC (Android side)

### 1.1 Which drivers the Nord 2 kernel really builds [V]

`K/arch/arm64/configs/cupida_defconfig`: `CONFIG_VIDEO_MEDIATEK_JPEG=y` (:3317), `CONFIG_VIDEO_MEDIATEK_VCU=y` (:3318),
`# CONFIG_VIDEO_MEDIATEK_VPU is not set` (:3319), `CONFIG_VIDEO_MEDIATEK_VCODEC=y` (:3321), `CONFIG_MTK_JPEG=y` (:1761),
`# CONFIG_MTK_VDEC_FMT is not set` (:1687), `CONFIG_MTK_PSEUDO_M4U=y`, `CONFIG_MTK_IOMMU_V2=y`, `CONFIG_MTK_SMI_EXT=y`,
`CONFIG_MTK_CMDQ_MBOX_EXT=y`.

### 1.2 VDEC / VENC: a kernel relay and a closed user-space codec [V]

- `K/drivers/media/platform/mtk-vcodec/` (25k lines) is V4L2 glue + per-SoC power (`mt6885/mtk_vcodec_{dec,enc}_pm.c`, 1211 + 891
  lines) + IPI message structs (`vdec_ipi_msg.h` 374 lines, `venc_ipi_msg.h` 502 lines, `vcodec_ipi_msg.h`). It contains no register
  programming of the codec cores.
- `K/drivers/media/platform/mtk-vcu/mtk_vcu.c` (2627 lines): the "firmware" is the process that opens `/dev/vpud` (`VCU_PATH`, :64). The
  ioctl ABI is `K/include/uapi/linux/mtk_vcu_controls.h`:177-190 — `VCU_SET_OBJECT` / `VCU_GET_OBJECT` (the IPI messages, `struct
  share_obj`), `VCU_MVA_ALLOCATION/FREE`, `VCU_PA_ALLOCATION/FREE`, `VCU_CACHE_*`, `VCU_GCE_SET_CMD_FLUSH` (`struct gce_cmdq_obj`: up to
  2048 {cmd, addr, data, mask} entries replayed through the GCE mailbox), `VCU_GCE_WAIT_CALLBACK`, `VCU_SET_MMAP_TYPE`; `mmap` gives the
  daemon the register windows (`VCU_MAP_HW_REG_NUM 4`, :133) and the shared memory (`MAP_SHMEM_*`, :100-113).
- IPI ids `IPI_VDEC_H264/H265/HEIF/VP8/VP9/MPEG4/MPEG12/WMV/RV30/RV40/AV1`, `IPI_VENC_H264/H265/VP8/MPEG4/HYBRID_H264`, `IPI_MDP`,
  `IPI_CAMERA`: `mtk-vcu/mtk_vcu.h`:113+.
- **The SCP plays no part.** `grep -ri "vdec\|venc\|vcodec\|vcu" K/drivers/misc/mediatek/scp/` finds nothing; `VIDEO_MEDIATEK_VPU` is off.
  Our kernel's `NORD2 SCP` driver (`ST/bsd/dev/nord2_scp.inc`, sensor hub over mailbox 2, IPIs CHRE/CHREX/SENSOR) is therefore not a
  transport for codecs, and there is no codec firmware image to load: nothing in `scp.img` is needed.
- The daemon and its libraries, from the phone's own vendor partition [V] (`VFS`, all **32-bit ARM**; no 64-bit copies exist):
  `bin/vpud` (15 KB, `service vpud /vendor/bin/vpud -f`, `etc/init/vpud.rc`), `lib/libvpud_vcodec.so` (562 KB: the IPI service loops,
  symbols `vdec_codec_msg_handler`, `venc_vcodec_msg_handler`, `md32_ipi_send`, …), **`lib/libvcodecdrv.so` (2.2 MB, 1692 exported
  symbols: the codec HAL)**, `lib/libvcodec_utility.so` (223 KB: `eVideo*` — register mmap, HW lock, wait ISR, GCE command building),
  `lib/libvcodec_oal.so` (30 KB: `VCodec*` pthread / memory wrappers).
- What `libvcodecdrv.so` holds, by exported names [V]: decoders `Vdec_Drv_H264_*`, `Vdec_Drv_H265_*`, VP9, AV1, MPEG (`vdec_hal_h264*`
  53+ functions, `vdec_hal_av1*` 53, `vdec_hal_vp9*` 25, `H264_HAL_*`); encoders `H264_EncodeFrame*`, `H264_EncodeSPS/PPS`,
  `H264_FrmLvlMMRSetting*`, `HEVC_Encode*`; and the **encoder's RCPU microcode as data** (`ucH264RCPUCode_*` 7 symbols,
  `ucHEVCRCPUCode_*` 16 symbols). Its public entry points are `eVDecDrvCreate/Init/Decode/GetParam/SetParam/Release` and `eVEncDrv*`
  (imported by `libvpud_vcodec.so`).
- **Its imports are small and clean [V]**: about 60 libc functions (malloc, memcpy, pthread_*, sem_*, fopen/fprintf for dumps, usleep,
  dlopen), `__android_log_print`, `property_get`, `atrace_*`, and its own OAL (`VCodecDrv*`, `eVideo*`, `VENC_ReadHW/WriteHW/PollHW`,
  `vdec_hal_read_*/write_*`, `eVideoGCEAddCmdWrite`, `eVideoLockHW`, `WaitISR`). No binder, no HIDL, no ION calls of its own.
- No register specification of the VDEC/VENC cores exists in `K`: `mt6885/mtk_vcodec_dec_pm.c` has a few power/clock registers only.
  Upstream Linux's stateless `mtk-vcodec` (MT8192/MT8195) does not have it either: there the SCP firmware programs the cores **[I]**.

### 1.3 JPEG encoder: fully in the kernel [V]

- Device tree `K/arch/arm64/boot/dts/mediatek/mt6893.dts`:5771: `jpgenc@17030000`, `compatible = "mediatek,jpgenc"`, 0x10000 bytes,
  `GIC_SPI 309`, larb7, clock `venc_gcon_clk CLK_VEN1_CKE2_JPGENC`, M4U ports L7 7 (Y RDMA), 8 (C RDMA), 9 (Q table), 10 (BSDMA).
  The driver matching that string is `K/drivers/media/platform/mtk-jpeg/mtk_jpeg_core.c`:2231 (the older
  `drivers/misc/mediatek/jpeg/v2` is built with `-DJPEG_HYBRID_DEC_DRIVER` only on mt6885/mt6893: `v2/Makefile`).
- Registers (`mtk_jpeg_enc_reg.h`, base + offset): RSTB 0x100, CTRL 0x104, QUALITY 0x108, BLK_NUM 0x10C, BLK_CNT 0x110, INT_STS 0x11C,
  DST_ADDR0 0x120, DMA_ADDR0 0x124, STALL_ADDR0 0x128, OFFSET_ADDR 0x138, RST_MCU_NUM 0x150, IMG_SIZE 0x154, DEBUG_INFO0/1 0x160/0x164,
  TOTAL_CYCLE 0x168, BYTE_OFFSET_MASK 0x16C, SRC_LUMA_ADDR 0x170, SRC_CHROMA_ADDR 0x174, STRIDE 0x178, IMG_STRIDE 0x17C, DCM_CTRL 0x300,
  CODEC_SEL 0x314, ULTRA_THRES 0x318. CTRL bits: enable 0, int enable 2, YUV format 3-4 (0 YUYV, 1 YVYU, 2 NV12, 3 NV21), JFIF/EXIF 5,
  restart 10, RDMA padding 20, right padding 29, padding-with-0 30. INT_STS: done 1, stall 2, vcodec 0x10.
- One encode (`mtk_jpeg_enc_hw.c`, `mtk_jpeg_core.c`:1112-1195, :1366-1432): reset (RSTB 0, 1; CODEC_SEL 0); IMG_SIZE = w<<16 | h;
  format; BLK_NUM = (pad16(w)/16) * (pad16(h)/16) * 6 - 1 for 4:2:0; IMG_STRIDE = align(even(w), 16); STRIDE = bytes per line;
  luma / chroma addresses; OFFSET_ADDR 0; BYTE_OFFSET_MASK = dst & 15; DST_ADDR0 = dst & ~15; STALL_ADDR0 = (dst + size) & ~15;
  QUALITY low 16 bits = a code (Q30 4, Q38 0, Q44 0xE, Q52 0xA, Q60 1, Q72 0xF, Q78 6, Q85 2, Q90 7, Q95 3, Q97 0x10:
  `mtk_jpeg_core.h`:76-92); restart interval; CTRL |= int enable | enable | padding bits. Done interrupt; the file size is
  DMA_ADDR0 - DST_ADDR0. The block writes a complete JPEG file (headers included) **[I from the V4L2 driver returning the buffer as
  V4L2_PIX_FMT_JPEG untouched]**. Fixed quantisation tables per quality code; no arbitrary tables through this driver.
- Power, clocks, bus (`mtk_jpeg_clk_on_ctx` :1554): `smi_bus_prepare_enable(larb7)`, the JPGENC gate, `m4u_config_port` for ports 7-10.
  - VEN power domain: `K/drivers/clk/mediatek/clk-mt6885-pg.c`:2646 `spm_mtcmos_ctrl_ven`: SPM 0x10006000, POWERON_CONFIG_EN 0x000 =
    0x0B160001, `VEN_PWR_CON` 0x344, status bit 17 in PWR_STATUS 0x16C / PWR_STATUS_2ND 0x170; on = PWR_ON(2), PWR_ON_2ND(3), wait,
    clear PWR_CLK_DIS(4), clear PWR_ISO(1), set PWR_RST_B(0), clear SRAM_PDN(8), wait ack(12) = 0; then release bus protection:
    infracfg 0x10001000 `PROTECTEN_MM_CLR` 0x2D8 bit 27, `PROTECTEN_MM_2_CLR` 0xDD0 bit 1, `MM_CLR` bit 26, `MM_2_CLR` bit 0
    (status registers 0x2EC and 0xDD8). Parent domain DIS (the display: on), clock `venc_sel` (:4997). After power-on the subsystem
    hook sets `VENC_CG_SET` 0x17000004 = 0x4 (:1176).
  - `venc_sel`: topckgen 0x10000000 `CLK_CFG_13` 0xE0 (SET 0xE4, CLR 0xE8), bits 24-27, gate bit 31, update `CLK_CFG_UPDATE1` 0x008
    bit 24 (`clk-mt6885.c`:1958, :147); parents :1221 (0 = 26 MHz, 1 mmpll_d7, 2 mainpll_d6, 3 univpll_d4_d2 = 312 MHz, …).
  - vencsys gates 0x17000000 (`clk-mt6885-ven.c`): status 0x0, SET 0x4 = clock **on** (inverted gates), CLR 0x8 = off; bit 0 larb, 4 venc,
    8 jpgenc, 12 jpgdec, 16 jpgdec_c1, 28 GALS.
  - SMI: larb7 at 0x17010000 (`mt6893.dts`:6060), values `K/drivers/misc/mediatek/smi/mt6885/smi_conf.h`:94 (CMD_THRT_CON 0x24 =
    0x300256, SW_FLAG 0x40 = 1, 0x524 = 0x300256) and :247 (OSTDL 0x200 + 4*port: ports 7, 8 = 1, 9, 10 = 4). The port names end in
    `_DISP`: larb7 goes out through the display's SMI common 0x1411f000 and the display IOMMU.
  - IOMMU: `SMI_LARB_NON_SEC_CON(port)` = larb + 0x380 + 4*port, bit 0 = translate (`smi_reg.h`:102). With bit 0 clear the port uses
    physical addresses — **the method our camera driver already uses on larbs 13/14, proven on the phone**
    (`ST/bsd/dev/nord2_seninf.inc`:98-103, `nord2_camsv.inc`:18-33). The address registers are 32 bits wide: buffers below 4 GB.

### 1.4 JPEG decoder [V]

`jpgdec@17040000` (:5788; three cores, SPI 310/311/315). The kernel side is `K/drivers/misc/mediatek/jpeg/v2/jpeg_drv_dec.c`:30
`jpeg_drv_hybrid_dec_start`: it writes 21 words supplied by user space (`struct JPEG_DEC_DRV_HYBRID_TASK`, `jpeg_drv.h`:438) to offsets
0x090 (twice), 0x0FC, 0x14C, 0x150, 0x154, 0x17C, 0x200, 0x20C, 0x210, 0x224, 0x23C, 0x24C, 0x270, 0x31C, 0x330-0x348, 0x36C-0x374, then
0x240 (trigger). The words are computed by the closed `VFS/lib64/libjpeg-alpha_vendor.so`. The offsets up to 0x274 are those of the 2015
MediaTek decoder that `K/drivers/media/platform/mtk-jpeg/mtk_jpeg_dec_hw.c` (415 lines, open) programs; 0x31C-0x374 are new and
undocumented. Feasible only by reading the vendor library or tracing Android — after the encoders.

### 1.5 MDP

`K/drivers/misc/mediatek/mdp/` (45k lines) is a CMDQ relay; the register sequences come from the closed `libdpframework`. iOS scales and
converts on the Mali through VideoToolbox's Metal path today. Not planned.

## 2. What is public, derivable, closed

| Item | Status |
|---|---|
| JPEG encoder register map, sequence, quality codes | public, GPL-2.0, in `K` |
| VEN/VDE power domains, clocks, SMI values, larb ports | public, in `K` |
| IPI message structs VDEC/VENC, VCU ioctls | public, in `K` (`vdec_ipi_msg.h`, `venc_ipi_msg.h`, `mtk_vcu_controls.h`) |
| VDEC / VENC core register semantics, encoder RCPU microcode, rate control | **closed**: only inside `libvcodecdrv.so` (ARM32, exported names kept) |
| JPEG hybrid decoder register words | closed (`libjpeg-alpha_vendor.so`) |
| Codec blobs on the phone | present: Android's `super` (vendor) partition is untouched by the iOS install **[I: iOS occupies userdata only, AGENTS.md]**; extracted copies are in `VFS` [V]. They are the user's own phone's files: usable on that phone, **not to be published**. |
| Firmware images to load into the blocks | none needed except the RCPU microcode, which is data inside `libvcodecdrv.so` |

## 3. The iOS side

Verified last night by the video lead (`gpu/video/PLAN.md` §1.2-1.3, addresses there) and on the phone by `gpu/video/probe-run4-kernel.log`
(`registered: hvc1=ok hev1=ok avc1=ok jpeg=absent`):

- Stock VideoToolbox `dlopen`s a fixed list of plug-in paths and calls a named register function in each: encoders
  `/System/Library/VideoEncoders/Slim.videoencoder` → `SLIMEncoderRegister`; **decoders have the same path**:
  `/System/Library/VideoDecoders/Slim.videodecoder` → `SLIMRegister` (`VTLoadVideoDecoders` @0x194cc7ea4), then
  `VTRegisterVideoDecoder` / `VTRegisterVideoDecoderWithInfo(codecType, info, createInstance)`.
- Apple's own hardware plug-ins (`AVD.videodecoder`, `H9.videoencoder`, `JPEGH1.videoencoder/.videodecoder`, …) register only when
  their IOKit service exists (`AppleAVD`, `AppleAVE2Driver`, `AppleJPEGDriver`); none does here, and they must stay absent.
- Selection: by codec type, `EncoderID`, `RequireHardwareAcceleratedVideoEncoder`, rating. A plug-in declares
  `IsHardwareAccelerated` in its registration info, so ours can truthfully say "hardware" once it is.
- Photos: `-[BWIntermediateJPEGCompressor _setupJPEGCompressionOptions]` sets `kCMPhotoCompressionOption_RequireHardwareCodec`; CMPhoto
  talks to `AppleJPEGDriver` directly or falls back to software `applejpeg`. Whether CMPhoto can be steered to a VideoToolbox `'jpeg'`
  encoder instead of `AppleJPEGDriver` is **not known** [I]: M3 measures it. If it cannot, the JPEG block is still reachable from
  our own camera path (`gpu/build/camera-userland-20261109`) and from `VTCompressionSession('jpeg')` clients.
- A working software `'jpeg'` VideoToolbox encoder object already exists in `gpu/video/vt/n2jpeg_vt.c` (host-tested, 345 checks;
  not yet on the phone). Its entropy coder is the part the hardware replaces: the hardware back end goes behind the same object.

## 4. Architecture

```
 iOS process (mediaserverd, Camera, apps)                         kernel (XNU, ours)                    MT6893
 VideoToolbox ── Slim.videoencoder / Slim.videodecoder
                  ├ 'jpeg' enc ───────── ioctl ────────────────►  /dev/redwood-jpeg   (M1-M3)  ─────►  JPGENC 0x17030000
                  ├ 'avc1','hvc1' enc ─┐
                  └ 'avc1','hvc1',…dec ┤  XPC/Mach to one daemon
 n2vcodecd (one process)               ▼
   ARM32 interpreter running the phone's libvcodecdrv.so
   + our OAL (libc subset, eVideo*, VCodecDrv*) ── ioctl/mmap ─►  /dev/redwood-vcodec (M4-M5)  ─────►  VENC 0x17020000, VDEC 0x16000000
```

**JPEG encoder: a native driver** (§1.3 is the whole specification).

**VENC / VDEC: host the vendor's codec library instead of rewriting it.** Rewriting means reverse engineering 2.2 MB of ARM32 code per
codec with no register documentation: months, and the result would still be a derived work. Hosting is tractable because
`libvcodecdrv.so` is already written against an OS abstraction (§1.2: ~60 libc + ~130 OAL imports) and does control work only —
header parsing, register writes, rate control — not pixel work, so it tolerates being interpreted. XNU for arm64 has no AArch32 user
mode, so the library runs under an **ARMv7/Thumb-2 interpreter inside one daemon** (`n2vcodecd`), which supplies the imports itself:
memory and threads natively, register access and interrupts through `/dev/redwood-vcodec` (mmap of the register windows for reads and
writes outside GCE, a wait-for-interrupt ioctl, DMA buffer allocation below 4 GB with physical addresses, the GCE command lists
executed by the driver as plain register writes/polls in order). The daemon speaks `eVEncDrv*` / `eVDecDrv*` directly — the IPI layer
(`libvpud_vcodec.so`) and the V4L2 layer are not needed. The blobs are read from the phone's own vendor partition at install time.
Measured so far [V]: build attributes ARMv8 AArch32, ARM + Thumb-2 mixed (8 of 1692 exports are Thumb entry points), VFP + NEON
present; `.text` 934 KB, `.data` 756 KB (tables, microcode); 1401 PLT relocations; the file is stripped of mapping symbols, so an exact
instruction census needs a recursive walk from the exports (M4a).
Risks, stated plainly: (a) the interpreter must be exact for every instruction the library uses, A32 and T32, with VFP/NEON;
(b) the GCE lists may rely on hardware events (`CMDQ_EVENT_VDEC_LAT_*`, device tree :6270) that a software executor must emulate by
polling; (c) secure-world paths (`*_sec`, `*SecureVdecCA`) are out of scope; (d) performance of the interpreted rate-control path at
30 fps is unknown until measured.

Why not the alternatives: an in-kernel AArch32 EL0 mode for XNU (the A78/A55 cores have it) is a larger and riskier kernel project than
an interpreter and would need a 32-bit bionic process environment; binary translation to arm64 is an optimisation of the interpreter,
later, if (d) demands it.

## 5. Milestones

| # | What | Proof | Needs |
|---|---|---|---|
| **M1** | Kernel: VEN domain, `venc_sel`, vencsys gates, larb7 (IOMMU off for ports 7-10), JPGENC register encode of a kernel-generated NV12 picture, polled. Trigger and result over the autotest USB channel. | Kernel log `NORD2 JPG:` lines + the JPEG bytes fetched to the PC, decoded there, PSNR against the same picture generated on the PC. | **1 flash (boot only).** Built: §7 |
| M2 | Interrupt (SPI 309) instead of polling; `/dev/redwood-jpeg` (write NV12, ioctl encode, read JPEG) with two fixed buffers; sizes up to 12 MP; power the domain down when idle; timing. | user tool on the phone encodes camera frames; ms per megapixel | 1-2 cycles, root fs: one tool |
| M3 | `'jpeg'` hardware back end in the VideoToolbox plug-in (coordinate with the `gpu/video` owner: their `n2jpeg_vt.c` object, our back end); EXIF/JFIF handling; measure whether CMPhoto / Camera use it. | Camera photo in "Most Compatible" written by the block; `IsHardwareAccelerated` true | 2-3 cycles |
| M4a | Host: ARM32 interpreter + loader for `libvcodecdrv.so`, fake OAL with a register recorder; run `eVEncDrvCreate/Init/SetParam` and one `H264_EncodeFrame` up to its first hardware wait; list every instruction class, import and register touched. | host log; go / no-go for the hosting approach | no phone |
| M4b | Kernel `/dev/redwood-vcodec`: VEN domain (shared with M1), VENC gates, larb7 ports 0-6/13-22, SPI 308, register mmap, DMA buffers, wait-ISR. Read-only identification first. | kernel log | 1 cycle |
| M4c | `n2vcodecd` on the phone: H.264 encode of a test sequence by the VENC; bit stream decoded on the PC. | first hardware H.264 frames | several cycles |
| M5 | VideoToolbox `'avc1'` / `'hvc1'` encoder objects backed by the daemon; replaces the software H.264 encoder for the camera. | Camera video recorded by the VENC | |
| M6 | VDEC through the same daemon (VDE/VDE2 domains 0x33C/0x340, larb4/5, SPI 424/425, LAT + core); `Slim.videodecoder`. JPEG decoder if the vendor library yields its words. | playback through the VDEC | |

M1-M3 are days. M4-M6 are weeks, with M4a as the early decision point.

## 6. M1 design (kernel patch 0298)

Files: `gpu/hwcodec/kernel/` (patch, sources), host tests `gpu/hwcodec/test/`, host tool `gpu/hwcodec/tools/hwc.py`.

- `bsd/dev/nord2_hwcodec.inc`, included by `sweet_touch.c` after the autotest include (the object the build already recompiles; it has
  the MMIO helpers `ni_rd/ni_wr/ni_mask` and the camera's precedent). Nothing runs at boot: every step is an autotest command
  (USB vendor request 0x72, **new command 7**, argument = step), executed on the autotest thread, logging `NORD2 JPG:` lines.
- Steps, each refusing to run unless the previous state is what it expects, each bounded by timeouts (no unbounded `while` as in the
  Linux code):
  0. **read only**: SPM status 0x16C/0x170, `VEN_PWR_CON`, bus protection status, `CLK_CFG_13`; and only if the domain is already on:
     vencsys gates, larb7, JPGENC registers.
  1. `venc_sel` on (parent left as found unless it is the 26 MHz parent or gated: then 3 = univpll_d4_d2), VEN domain on (the
     `spm_mtcmos_ctrl_ven` sequence, with timeouts; a power request that is not acknowledged is withdrawn), bus protection released,
     vencsys SET = 0x4 (what `clk-mt6885-pg.c`:1176 writes after power-on, kept as is) then bits 0 larb, 4 venc, 8 jpgenc, 28 GALS; dump.
  2. larb7: the three `smi_conf.h` values, OSTDL for ports 7-10, `NON_SEC_CON` bit 0 and bits 8-15 (address bits 32+,
     `pseudo_m4u_v2.c`:563-569) cleared for ports 7-10 only; JPGENC reset; dump of
     the register defaults.
  3. encode: picture generated in the kernel (size from USB registers, default 640x480 NV12, a deterministic formula the PC repeats),
     registers as §1.3, poll INT_STS up to 2 s, log status / size / cycle count / first bytes; the output is exposed through the
     autotest picture channel (request 0x73) so the PC can fetch it.
  4. everything off again in reverse (gates, bus protection with ack wait, domain).
- Memory: a fixed claim **0xD8000000..0xD9000000** (16 MiB: source 8 MiB, output 8 MiB), below 4 GB, directly above the camera's
  0xD0000000..0xD8000000 claims (`ST/bsd/dev/nord2_camdev.inc`:30-33, :103) and below 0xDFE70000, where the live device tree has no
  carve-out (`nord2_camsv.inc`:26). XNU's own memory window starts at 0x1_42000000, so nothing else lives there.
- Known hazards and what the code does about them: the camera bring-up lost the system 27-44 s after releasing a domain's bus
  protection while its bus parent (MDP) was off (`nord2_seninf.inc`:152). VEN's parent is DIS, which the running display keeps on;
  step 1 still checks that the DIS status bit is set and refuses otherwise. The display's SMI common is shared with the panel
  scan-out: M1 does not write it (the camera code only writes it "when not set up"); it logs its values.

## 7. Status

See `gpu/hwcodec/STATUS.md` (kept current: what is built, what was run where, logs).
