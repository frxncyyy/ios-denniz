# VDEC (H.264 / HEVC hardware decode) on MT6893: what the vendor stack does, and what our stack needs

Owner: opus-vdec. Written 2026-11-11 (host only; nothing here has run on the phone). Every line is **[V]** verified today
(path:line, symbol, command or probe output given) or **[I]** inferred. Short paths: `K` = the Android kernel
`$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel`,
`KV` = `K/drivers/media/platform/mtk-vcodec`, `VFS` = `gpu/build/cam-vendor-20261109/vfs`, `L` = `VFS/lib`.
No vendor code is reproduced here: the only things taken from the vendor objects are symbol names, addresses, literal constants
and the vendor's own log text.

**Probe used for the [V-probe] lines**: a scratch C program (outside the tree; method in section 9) that links the current
`libn2vhost.a` (`make -C gpu/hwcodec/host` o2 build, 33/33 objects), starts the phone's `vpud` under the interpreter on T02's fake
hardware and plays the AP (V4L2 driver) role for one H.264 decoder instance through V40's `/dev/vcu` model.

## 0. Summary (read this if nothing else)

1. **Same architecture as VENC.** On Android the decoder is driven exactly like the encoder: the kernel V4L2 driver
   (`KV/vdec/vdec_common_if.c` + `KV/vdec_vcu_if.c`) is a message relay; the closed 32-bit `vpud` + `libvpud_vcodec.so` +
   `libvcodecdrv.so` + `libvcodec_utility.so` parse the stream, manage the DPB and program the VDEC registers from user space
   through an mmap of `0x16000000 + 0x40000` and GCE command lists. [V] (`KV/Makefile`: with `CONFIG_VIDEO_MEDIATEK_VCU=y` only
   `vdec_common_if.o` + `vdec_vcu_if.o` are built; `vdec_h264_if.c` is VPU-only and not built.)
2. **Our stack already speaks the decoder protocol up to the first decode.** [V-probe] With the unmodified tree, vpud answers
   `AP_IPIMSG_DEC_QUERY_CAP` (formats + frame sizes, section 2.4), `AP_IPIMSG_DEC_INIT` -> `VCU_IPIMSG_DEC_INIT_ACK` with
   `vcu_inst_addr = 0x4009ed88` (the vsi, mapped through `n2vcu_map_dm_addr`), and two `AP_IPIMSG_DEC_SET_PARAM`
   (FRAME_SIZE 320x240 — vendor log `VDEC_DRV_SET_TYPE_SET_FRAME_SIZE width : 320, height : 240` — and DECODE_MODE), all status 0.
3. **First blocker (host):** `AP_IPIMSG_DEC_START` with the bitstream handed over like the encoder's (pa in `vsi.dec.bs_dma`,
   an ion share id registered with `n2shim_ion_register_share()` in `vsi.dec.bs_fd`) is answered at once with status -1 and the
   vendor log `[ERROR] get_ringbuf input handle NULL !!` (`libvpud_vcodec.so` `get_ringbuf` @0x8c70, the NULL test at 0x8c76).
   No register access, no GCE, no allocation happened. The decoder's input-buffer import differs from the encoder's; package
   DEC01 resolves it (section 7, first item). [V-probe]
4. **The frozen register-window list is not enough for decode.** [V-static] `vdec_drv_init_hw_info` (`L/libvcodec_utility.so`
   @0x1e1b8) builds its sub-block table from the one 0x40000 mapping and uses sub-blocks at 0x16012000, 0x16013000,
   0x16014800-0x16017fff, 0x16018800, 0x16022000 (AVC_VLD — the H.264 entropy/syntax block), 0x16024000, 0x16029000,
   0x16001000-0x16003fff, 0x1600e000, 0x16011800, plus the larb pages 0x1600d000 / 0x1602e000. Neither `include/redwood_vcodec.h`
   (12 windows) nor V40's `vcu_windows[]` nor T02's fake cover them. Section 4 lists them and proposes the fix (one 0x30000 VDEC
   window, ABI rev 3).
5. **Header parsing is hardware-assisted.** [V-static] The H.264 syntax readers go through the VDEC VLD barrel shifter:
   `H264_HAL_UeCodeNum` (`L/libvcodecdrv.so` @0xfd218) = `vdec_hal_read_avc_vld(hw, core, 0x8f)` (AVC_VLD register 0x23c),
   `H264_HAL_ShiftGetBitStream` / `H264_HAL_AVCVLDGetBitS` (@0xfd186 / @0xfd174) -> `vdec_hal_read_vld(hw, core, n)`,
   `h264_comhal_get_ue` (@0x10bbe4) -> `vdec_hal_h264_uecodenum`. So SPS/PPS/slice-header parsing is **not** pure ARM32 work:
   every `ue(v)`/`se(v)`/`u(n)` is a register read. Consequences: (a) on the PC the fake hardware must model the VLD barrel
   shifter, or no stream can even be parsed (package DEC03); (b) on the phone every syntax element read is one register access
   through `/dev/redwood-vcodec` (section 6, cost). HEVC has software-named parsers (`H265_parseSPS/PPS/SliceHeader`,
   `H265_handle*NALU`), whose bit source is **[I]** unverified (could be the same barrel shifter, `vdec_hal_read_hevc_vld`).
6. **Feasibility today (2026-11-11):** host-side: DEC_INIT/SET_PARAM/QUERY_CAP are proven; a first `DEC_START` that reaches the
   VDEC registers is reachable today once the input-buffer import is fixed (hours). A decoded picture on the PC is **not**
   reachable today (needs a faithful VLD barrel-shifter model and then the full core's output: the fake can never produce pixels;
   only the real block can). On the phone: nothing today; the earliest real-hardware decode needs the window fix in the kernel,
   one image, and the VDEC power/clock path (already in K02/p031x), i.e. after the VENC phone cycles.

## 1. Device tree, power, clocks, interrupts (kernel facts) [V]

| Item | Value | Evidence |
|---|---|---|
| V4L2 decoder node | `vdec@16000000`, `mediatek,mt6885-vcodec-dec`; windows VDEC_SYS 0x1602f000+0x1000, VDEC_BASE 0x16000000+0x1000, VLD 0x16020000, MC 0x16021000, MV 0x16023000, MISC 0x16025000+0x4000, LAT_MISC 0x16010000, LAT_VLD 0x16011000+0x400, RACING_CTRL 0x16004000, SOC_GCON 0x1600f000 | `K/arch/arm64/boot/dts/mediatek/mt6893.dts`:6281-6292 |
| VCU node | `vcu@16000000`: daemon-mappable windows 0x16000000+0x40000, 0x17020000+0x10000, 0x17820000+0x10000 | `mt6893.dts`:6212-6218; FACTS 1.24 |
| Interrupts | index 0 = SPI 424 = `MTK_VDEC_CORE` (0), index 1 = SPI 425 = `MTK_VDEC_LAT` (1) | `mt6893.dts`:6297-6298; `KV/vcodec_ipi_msg.h`:19-20; `KV/mt6885/mtk_vcodec_intr.c`:225-240 |
| IRQ status / ack | core: `VDEC_MISC+0xA4` bit 16 (0x10000) = DEC_SUCCESS; ack = write(read \| 0x11) then write(read & ~0x10). LAT: same at `VDEC_LAT_MISC+0xA4`. "HW active" check: core `VDEC_SYS` bit 4, LAT `VDEC_BASE` bit 4 | `KV/mt6885/mtk_vcodec_intr.h`:16-20; `mtk_vcodec_intr.c`:61-153 |
| Clocks | `MT_CG_SOC` = `CLK_VDE1_VDEC_CKEN` (vdec_soc_gcon 0x1600f000), `MT_CG_VDEC0` = `CLK_VDE2_VDEC_CKEN` (vdec_gcon 0x1602f000, core), `MT_CG_VDEC1` = `CLK_VDE1_LAT_CKEN` (LAT) | `mt6893.dts`:6301-6308 |
| Per-hw clock-on | CORE: `smi_bus_prepare_enable(SMI_LARB4)`, MT_CG_SOC, MT_CG_VDEC0; LAT: `SMI_LARB5`, MT_CG_SOC, MT_CG_VDEC1; then on the first active user restore 68 words at `VDEC_RACING_CTRL+0x100`; with pseudo-M4U, all larb4 (11) / larb5 (8) ports configured + port `L5_VDEC_UFO_ENC_EXT` | `KV/mt6885/mtk_vcodec_dec_pm.c`:154-251 |
| Per-hw clock-off | save the 68 racing words on the last user; `mtk_vdec_hw_break()`; gates off; larb off | `mtk_vcodec_dec_pm.c`:253-288 |
| HW break (before clock-off) | CORE: if `VDEC_SYS` reads non-zero: `MISC+0x100 \|= 1`, poll `MISC+0x104 & 0x11 == 0x11` (+ UFO `BASE+0x800+0x8C` bits 0x11000 when UFO is on; UFO `+0x1C` bit 1 cleared/restored), then `VLD+0x108 = 1, = 0`. LAT: if `SOC_GCON+0x200` non-zero: `LAT_MISC+0x100 \|= 1`, poll `LAT_MISC+0x104` bits 0 and 4, then `LAT_VLD+0x108 = 1, = 0` | `mtk_vcodec_dec_pm.c`:296-460 |
| Power domains | VDE (SPM 0x33C, status bit 15) = SOC/LAT side, larb5 0x1600d000; VDE2 (0x340, bit 16) = core, larb4 0x1602e000; bus protection VDE MM bits 24/25, VDE2 MM_2 bits 6/7; larb4 sits on the MDP SMI common (MDP domain must be on) | FACTS 1.9/1.10; `kern/VCODEC_HW.md` §1, §1.1 (K02, accepted) |
| Muxes | `vdec_sel` topckgen 0x0F0 bits 3:0, `vdec_lat_sel` 0x0F0 bits 11:8 | `kern/VCODEC_HW.md` §2 |
| GCE events | decoder threads use `CMDQ_EVENT_VDEC_CORE0_*`, `CMDQ_EVENT_VDEC_LAT_*` (SOF, FRAME_DONE_0..6, LAT_0..3/7); V40 maps vcu event ids 0-15 -> `RWVC_IRQ_VDEC`, 16-31 -> `RWVC_IRQ_VDEC_LAT` | `mt6893.dts`:6240-6278; `vcu/n2vcu.c` `gce_event_irq` |

Everything in this table is already implemented host-side in K02 (`kern/VCODEC_HW.md`, accepted by R2) except the racing-word
save/restore and the HW-break sequence, which belong to whoever plays the LOCK/UNLOCK role (section 3).

## 2. AP <-> VCU protocol of one H.264 decoder instance

### 2.1 Message ids and layouts [V] (`KV/vdec_ipi_msg.h`)

AP -> VCU on `IPI_VDEC_H264` (= 2; `IPI_VDEC_H265` = 3; QUERY_CAP on `IPI_VDEC_COMMON` = 1):

| msg | id | struct (bytes) | fields |
|---|---|---|---|
| INIT | 0xA000 | `vdec_ap_ipi_init` (16) | msg_id, reserved (= svp_mode), ap_inst_addr (u64, AP cookie) |
| START | 0xA001 | `vdec_ap_ipi_dec_start` (24) | msg_id, vcu_inst_addr, data[0] = bs->size, data[1] = bs->length, data[2] = bs->flags, reserved |
| END | 0xA002 | `vdec_ap_ipi_cmd` (8) | msg_id, vcu_inst_addr |
| DEINIT | 0xA003 | (8) | " |
| RESET | 0xA004 | (8) | " — sent by vdec_decode(bs == NULL) = flush |
| SET_PARAM | 0xA005 | `vdec_ap_ipi_set_param` (44) | msg_id, vcu_inst_addr, id, data[8] |
| QUERY_CAP | 0xA006 | `vdec_ap_ipi_query_cap` (24) | msg_id, id (8 formats, 9 frame sizes), ap_inst_addr u64, ap_data_addr u64 |

VCU -> AP acknowledgements 0xB000-0xB006 (`vdec_vcu_ipi_ack`: msg_id, status, ap_inst_addr u64; INIT_ACK adds `vcu_inst_addr`
at +16; QUERY_CAP_ACK has id at +16 and `vcu_data_addr` at +32). [V-probe: INIT_ACK len 24, QUERY_CAP_ACK data at +32 worked.]

VCU -> AP requests during START (the kernel answers them synchronously in the daemon's VCU_SET_OBJECT, returns "no ack"):

| msg | id | what the kernel does (`KV/vdec_vcu_if.c` `vcu_dec_ipi_handler`) |
|---|---|---|
| WAITISR | 0xC000 | `status` = core id (0 CORE, 1 LAT); waits for that core's done IRQ (`WAIT_INTR_TIMEOUT_MS`), writes the result back into `status` |
| GET_FRAME_BUFFER | 0xC001 | blocks up to 1 s for a free output buffer; fills `vsi.dec.vdec_fb_va`, `index`, `fb_dma[]`, `fb_fd[]` (dma-buf fds installed in vpud), `general_buf_*`; index 0xFF if none |
| PUT_FRAME_BUFFER | 0xC002 | `mtk_vdec_put_fb()`: harvest `vsi.list_disp` / `list_free` (display order, POC, timestamp) |
| LOCK_CORE / UNLOCK_CORE | 0xC003 / 0xC004 | `vdec_decode_prepare/unprepare(ctx, MTK_VDEC_CORE)`: hardware lock + power + clock-on/off of the core (section 1) |
| LOCK_LAT / UNLOCK_LAT | 0xC005 / 0xC006 | same for the LAT core |

### 2.2 The vsi (shared memory, allocated by vpud in its DMEM, mapped by the AP) [V]

`struct vdec_vsi` (`KV/vdec_ipi_msg.h`:349-372), 64-bit-aligned u64 in both kernels and in the 32-bit daemon:
`list_free_bs` (ring of 32 u64 + 4 u32, 272 B) at 0, `list_free` (ring of 32 `vdec_ipi_fb` of 48 B + 16, 1552 B) at 272,
`list_disp` at 1824, `dec` (`vdec_dec_info`, 200 B) at 3376: dpb_sz, vdec_changed_info, bs_dma, bs_fd, fb_dma[8], fb_fd[8],
vdec_bs_va, vdec_fb_va, fb_num_planes, index, wait_key_frame, error_map, timestamp, queued_frame_buf_count; then `pic`
(pic_w, pic_h, buf_w, buf_h, fb_sz[8], bitdepth, layout_mode, fourcc) at 3576, color_desc, crop, 128 formats, 128 frame sizes,
aspect_ratio, fix_buffers(_svp), interlacing, codec_type, crc_path[256], golden_path[256], input_driven, general_buf_fd/dma/size.
[V-probe] vpud prints `vdec_get_addr 4009ed88 444672 4010b688 1536 4010bc88 4608`: the decoder instance area is 444672 bytes from
0x4009ed88 (the INIT_ACK `vcu_inst_addr` = `mem_inst[0]`), and the QUERY_CAP areas are 1536 (128 x 12-byte formats) and 4608
(128 x 36-byte frame sizes) bytes. The whole DMEM is `shmem_start 4009e000 .. shmem_end 40114000` (483328 B).

### 2.3 The call sequence the V4L2 driver uses [V] (`KV/mtk_vcodec_dec.c`)

1. `vdec_if_init` -> INIT (svp_mode 0) -> vsi pointer (:1645). `SET_PARAM_FB_NUM_PLANES` is vsi-only (`vsi.dec.fb_num_planes`,
   `vdec_common_if.c`:216-219); `SET_PARAM_DECODE_MODE` / `FRAME_SIZE` / `NAL_SIZE_LENGTH` / `WAIT_KEY_FRAME` / `OPERATING_RATE`
   are SET_PARAM messages (:1073-1149, :2207-2210). CRC/GOLDEN paths are vsi-only strings.
2. Header decode: `vdec_if_decode(src, fb = NULL)` (:2212): vsi.dec.bs_dma / bs_fd / vdec_bs_va set, index = 0xFF, START.
   Result: `vsi.dec.vdec_changed_info` bit 0 RES_CHANGE (pic info valid), bit 3 NEED_SEQ_HEADER, bit 2 HW_NOT_SUPPORT.
3. `GET_PARAM_PIC_INFO` (vsi.pic), `GET_PARAM_DPB_SIZE` (vsi.dec.dpb_sz + 1), CROP, COLOR_DESC — all read from the vsi.
4. Per access unit: fill bs fields and one free fb (fb_dma[0..planes-1], fb_fd, index), START; during it vpud may send
   LOCK_LAT, WAITISR(1), UNLOCK_LAT, LOCK_CORE, WAITISR(0), UNLOCK_CORE, GET/PUT_FRAME_BUFFER; then START_ACK. Output: frames
   appear in `vsi.list_disp`, reusable buffers in `vsi.list_free`, consumed bitstreams in `list_free_bs`.
5. EOS: `vdec_if_decode(NULL, NULL)` = RESET (flush); DEINIT.

### 2.4 Capabilities reported by this phone's vpud [V-probe]

QUERY_CAP id 8 (formats): output (type 2) `M21S, M2CS, MTSR, MTST, MCSR, MCST, MACS, MJSR, MJST, JCSR, JCST` (1 plane),
`MM21, MT21, MT2R, MT2T, MTCR, MTCT` (2 planes), **`YV12` and `NV12` (1 plane, linear)**; input (type 0) `H264, H265, HEIF,
MPG2, MPG4, H263, VP80, VP90, WVC1, AV10`.
QUERY_CAP id 9 (frame sizes): H264 profile 55 level 16, 16..4096 step 16 x 16..2176 step 32; H265 profile 15 level 8,
16..4096/64 x 16..2176/64; HEIF up to 16383x16383; VP9 4096x2176; AV1 4096x2176; MPEG-2/4, VP8 2048x1088.
Consequence: a linear NV12 output exists in the capability list ([I] whether H.264 at every size can write it directly or only
via the MTK tiled MM21 + a conversion; `vendor.mtk.vdec.frame.layout.mode` and `VDEC_DRV_SET_TYPE_SET_DECODE_LAYOUT` strings
in `libvpud_vcodec.so` select it). If only MM21 works, iOS needs a detile pass (CPU NEON or Metal) per frame.

## 3. What vpud does with the hardware for one H.264 frame (static reading; [V] names/addresses, [I] semantics)

- **Layers.** `eVDecDrvDecode` (`L/libvcodecdrv.so` @0x7a2a0) -> `Vdec_Drv_H264_decode` (@0xf65b4) -> `h264_dec_decode`
  (@0xec75c) / `H264_decode` (@0xfa574); syntax via `h264_comhal_*` / `H264_HAL_*`; register access only through the OAL in
  `libvcodec_utility.so` (`vdec_hal_read_*` / `vdec_hal_write_*` / `*_mask`, about 120 exports): `libvcodecdrv.so` makes no
  ioctl/mmap itself (FACTS 5.6a).
- **Register access mechanism** (`vdec_hal_read_avc_vld` @0x2b894 / `vdec_hal_write_avc_vld` @0x2b918, same shape for all):
  `(hw, core_index 0..5, reg_number)` -> address = `hw->map_base (+0x24) + hw->subblock_offset[module][core] (+0xc4.. table) +
  4*reg`, performed through function pointers at `hw+0x4b0` (read) / `+0x4b4` (write) — direct MMIO by default — unless a
  "virtual hw" object is installed for that core (`vdec_get_virt_hw`, `vdec_set_vir_hw`, `vdec_virt_hw_wfe_*`), in which case
  the access becomes a GCE command (`eVideoGCEAddCmdWrite/PollReg/WaitEvent` imported by `libvcodecdrv.so`). Which of the two
  modes the H.264 path uses on MT6893 is **[I]**; both are already served by V40 (MMIO hooks and `VCU_GCE_SET_CMD_FLUSH`).
- **Sub-block table** (`vdec_drv_init_hw_info` @0x1e1b8; offsets from 0x16000000; names [I] from the `vdec_hal_*` module names
  and the Linux window names): 0x20000 VLD, 0x20800 VLD_TOP, 0x21000 MC, **0x22000 AVC_VLD**, 0x23000 AVC_MV, 0x24000 PP,
  0x25000 MISC, 0x26000/0x26400/0x26800/0x27000/0x27800/0x28000/0x28400/0x28800 (inside the Linux MISC 0x4000 window: UFO/wrapper/
  HEVC/VP9/AV1 blocks [I]), 0x29000, 0x2e000 (larb4), 0x2f000 (VDEC_SYS/GCON), 0x2f800; LAT side 0x10000 LAT_MISC, 0x10800,
  0x11000 LAT_VLD, 0x11800 LAT_VLD_TOP, 0x12000 LAT_AVC_VLD, 0x13000 LAT_MV, 0x14800, 0x15000, 0x15400, 0x15800 (LAT WDMA/UBE [I]),
  0x16000, 0x17000, 0x18800; SOC side 0x0000 (BASE), 0x0800 (UFO), 0x1000, 0x2000, 0x3800, 0x4000/0x4400 (racing), 0xd000 (larb5),
  0xe000, 0xf000 (SOC_GCON), 0xf800. All inside the 0x40000 the daemon maps (`mmap(NULL, 0x40000, RW, SHARED, fd, 0x16000000)`
  at 0x1e1ea, FACTS 5.5).
- **LAT + CORE pipeline** [I from exports]: `vdec_hal_is_spt_feature_lat`, `H264_getLatInfo`, `H264_packLatInfo`,
  `H264_syncKeyInfoToCore`, `H264_HAL_LatUBEResume`, `vdec_hal_lat_*`, `vdec_hal_*_inner_racing_*`, `drvSliceDecode*`: the LAT
  core entropy-decodes into a UBE/"trans" buffer (working memory), the CORE core reconstructs from it; LOCK_LAT/WAITISR(1) then
  LOCK_CORE/WAITISR(0) in the message flow.
- **Bitstream parsing** = VLD barrel shifter (section 0 item 5): `vdec_hal_init_barrel_shift`, `H264_HAL_InitH264BarrelShifter`
  (@0xffd18), `vdec_hal_set_vld_vfifo` (bitstream FIFO = the DMA bitstream buffer window), reads of `VLD reg n` = shift/peek,
  `AVC_VLD reg 0x8f` = ue(v). DPB/reference management is ARM code (`H264SlidingWindowProce`, `H264AdapRefPicmarkingProce`,
  `H264_RefPicReorderAndMark`, `H264GetFrameBufferFromFree`, `H264PutFrameBufferToDisp`, `bDPBSizeBySPS`).
- **Memory** [I from imports and messages]: working buffers (MV buffers, UBE/LAT trans buffers, PP/ UFO buffers) through
  `eVideoMemAlloc` -> `VCU_PA_ALLOCATION` (FACTS 5.3) -> V40 -> `hw->dma_alloc` (RWVC pool); bitstream and frame buffers are AP
  buffers imported by fd (`ion_import`, `ion_mmap`, `ion_share` call sites at `libvpud_vcodec.so` 0xaebe/0xb0bc/0xb0f2);
  cache maintenance `eVideoFlushCache` / `eVideoInvalidateCache` -> `VCU_CACHE_*` (and `H264BufferFlushRange`,
  `H264BufferInvalidateCache` in the codec library).

## 4. Register windows: the gap (for opus-kern / hw-lead / V40 / T02)

| Range the vendor uses (offset from 0x16000000) | Covered by `redwood_vcodec.h` windows? |
|---|---|
| 0x00000-0x00fff BASE (+0x800 UFO) | yes (VDEC_BASE) |
| 0x01000, 0x02000, 0x03800 | **no** |
| 0x04000-0x04fff racing (0x4000, 0x4400) | yes (VDEC_RACING) |
| 0x0d000 larb5 | **no** (kernel-owned; vendor has `vdec_hal_wait_larb_sleep_protect` / `release_larb_sleep_protect`) |
| 0x0e000 | **no** |
| 0x0f000-0x0ffff SOC_GCON (+0xf800) | yes |
| 0x10000-0x10fff LAT_MISC (+0x10800) | yes |
| 0x11000-0x113ff LAT_VLD | yes |
| 0x11800 LAT_VLD_TOP | **no** (window is only 0x400) |
| 0x12000 LAT_AVC_VLD, 0x13000 LAT_MV, 0x14800, 0x15000, 0x15400, 0x15800, 0x16000, 0x17000, 0x18800 | **no** |
| 0x20000-0x20fff VLD (+0x20800 VLD_TOP) | yes |
| 0x21000 MC | yes |
| **0x22000 AVC_VLD** | **no** — needed for every H.264 syntax element |
| 0x23000 MV | yes |
| 0x24000 PP | **no** |
| 0x25000-0x28fff MISC (+0x26000..0x28800) | yes |
| 0x29000 | **no** |
| 0x2e000 larb4 | **no** (kernel-owned; see larb5) |
| 0x2f000-0x2ffff VDEC_SYS (+0x2f800) | yes |

**Proposal (ABI rev 3, hw-lead decides):** add `RWVC_WIN_VDEC_ALL` = 12: base 0x16000000, size 0x30000, offsets checked per
4 KiB page against the domain that powers it (0x00000-0x1ffff needs VDE, 0x20000-0x2ffff needs VDE2), larb pages 0x0d000 and
0x2e000 read-only for the user (the kernel keeps the larb configuration) **or** writable only at the sleep-protect registers once
DEC02 has traced which offsets the vendor writes there. Mirror it in V40 `vcu_windows[]` (one entry replacing the ten VDEC ones,
or added after them) and in T02's fake (`VDEC_ALL` window name). Keep the ten old windows for K02's own sequences. Nothing in the
VENC path changes. Before the phone run, a host trace (DEC02) gives the exact set of offsets touched for one H.264 frame; the
kernel can then shrink the allowance to that set.

## 5. Pieces a decode path needs beyond the encoder ones

| # | Piece | Where | Status |
|---|---|---|---|
| 1 | VDEC window coverage (section 4) | `include/redwood_vcodec.h` (hw-lead), `kern/bsd/dev/nord2_vcodec.inc` (K02/opus-kern), `vcu/n2vcu.c` (V40), `tests/hw/n2vc_hw_fake.c` (T02) | missing |
| 2 | VDE + VDE2 domains, `vdec_sel`/`vdec_lat_sel`, VDE1/VDE2 CKEN gates (0x0/0x200/0x8 SET), larb4 (needs MDP on) / larb5 config, IOMMU bypass for larb4 ports 0-10 and larb5 ports 0-7 | K02 | implemented host-side (`kern/VCODEC_HW.md`), not run on the phone |
| 3 | IRQ status/ack for SPI 424/425 | K02 | implemented host-side |
| 4 | Racing words: restore 68 words at RACING+0x100 on the first LOCK, save on the last UNLOCK | AP decode client (DEC01) through `hw->regs` | missing |
| 5 | HW break before clock-off (section 1) | AP decode client (DEC01) or kernel power-off path | missing |
| 6 | LOCK/UNLOCK_CORE/LAT = power ref + clock on/off per core; WAITISR with core id; GET/PUT_FRAME_BUFFER | AP decode client (DEC01) | missing (probe has a stand-in) |
| 7 | Bitstream / frame-buffer hand-over: share ids for bs, fb planes, `general_buf`; the decoder's `get_ringbuf` path | DEC01 (+ V34 shim if the ion semantics differ) | **blocker today** |
| 8 | DMA pool size: 1080p H.264 needs DPB (up to 16+1) x 3.1 MB + MV buffers + UBE/LAT buffers ≈ 60-70 MB; the RWVC pool is 86 MiB shared with VENC; 4K (17 x 12.5 MB) does not fit | hw-lead decision (pool size / claim) | open |
| 9 | Cache maintenance: bitstream to_device before START; output planes from_device after each PUT_FRAME_BUFFER / disp-list entry; `VCU_CACHE_FLUSH/INVALIDATE` from vpud already mapped by V40 to `n2vc_dma_sync` | V40/V41 (exists) + DEC01 for AP buffers | partly |
| 10 | Output conversion: request NV12 linear if the vendor accepts it for the stream (layout SET_PARAM), else MM21 detile; copy into an IOSurface-backed CVPixelBuffer | DEC05/DEC06 | missing |
| 11 | Daemon protocol (vcodecd <-> plug-in): decoder messages (open/decode/frame-out/flush/close) next to the encoder ones in `include/n2vcd_proto.h` | hw-lead (frozen header) + DEC05 | missing |
| 12 | VideoToolbox decoder plug-in `Slim.videodecoder` / `SLIMRegister` | DEC06 | missing |

## 6. Cost of the software part (static estimate; no decode has run yet) [I]

- Per frame the ARM32 work is: NAL scan + slice-header parse through the barrel shifter (about 30-80 syntax elements per slice,
  each a `vdec_hal_read_*` chain of roughly 25-40 guest instructions + one MMIO), DPB/ref-list management (a few thousand
  instructions), register programming (H.264 frame: estimated 300-1500 register writes through `vdec_hal_write_*`, 25-40
  instructions each, plus the `fgAVCDumpRegister`/log-level tests), two LOCK/WAITISR/UNLOCK rounds through `/dev/vcu`. Estimate:
  **0.1-0.3 million guest instructions per 1080p frame** (one slice), up to ~1 M with many slices or verbose logging.
- At the measured 25-80 guest MIPS on this host (`tests/perf/STATUS.md`:68) that is 1.5-12 ms per frame on the PC; on the
  phone's big cores similar, little cores ~3x. 30 fps 1080p is plausible; 60 fps 4K is not assured.
- **The larger phone cost is register round trips**: every guest MMIO access to a VDEC register becomes one `RWVC_REGS` ioctl
  (V40 MMIO hook -> P03 `n2vc_hw_open_dev`). 500-2000 accesses per frame x 2-5 µs = 1-10 ms per frame. Mitigation (DEC04):
  write-combining in V40 (queue writes, flush at the next read/poll/wait/GCE flush) cuts it to the reads only (barrel-shifter
  reads are inherently synchronous).
- How to measure (DEC02): run the AP client on the fake with the VLD model and count `n2vcu_stats.mmio_reads/writes` and guest
  icount per START.

## 7. Next steps, in order (each is a package in `gpu/hwcodec/wave3_decode_packages.json`)

1. DEC01: make `DEC_START` get past `get_ringbuf` (trace the host calls `ion_import`/`ion_mmap`/`ion_share` around
   `libvpud_vcodec.so` 0xae86-0xb2c0 during START, compare with what the kernel provides — `get_mapped_fd()` installs real fds
   for bs and every fb plane, `vdec_vcu_if.c`:72-158, `vdec_common_if.c`:217-260 — and with V41's encoder solution).
2. DEC02: register-access trace of one header decode on the fake (expect the first accesses in AVC_VLD/VLD: this confirms
   section 4 empirically and gives the exact page list for the kernel).
3. DEC03: VLD barrel-shifter model in the fake (ue/se/u(n), read pointer, vfifo), oracle = vsi.pic.pic_w/pic_h and dpb_sz equal
   to ffprobe's values for the same stream.
4. Kernel/ABI window fix; then a phone run of header decode (pic size from real hardware) before full frames.

## 8. Verified vs inferred, one line each

[V-probe] QUERY_CAP, INIT, SET_PARAM work on our stack; START fails at `get_ringbuf`. [V-static] sub-block offsets, barrel-shifter
call chains, mmap of 0x40000, `IPI_VDEC_*` ids, message layouts, IRQ/clock/power facts. [I] module names of the sub-blocks, GCE vs
direct MMIO for H.264, LAT/CORE split details, NV12-direct output for H.264, per-frame instruction count, HEVC bit source.

## 9. Probe method (re-run it)

Build `make -C gpu/hwcodec/host /tmp/n2hw-vdec/o2/libn2vhost.a OUT=/tmp/n2hw-vdec`; link a C file against it with
`-Wl,--whole-archive libn2vhost.a -Wl,--no-whole-archive` (without whole-archive the strong instruction classes are not pulled
in and vpud stops with UNDEF on its first ARM-mode PLT entry after 21 instructions — the same trap as R2 F15); in it:
`n2vc_hw_open_fake`, `n2vhost_start`, `n2vcu_ap_register(vcu, 2 and 1, handler)`, sleep 1 s, then send the messages of section
2.1 with `n2vcu_ap_send` + condition variable for the ACK + `n2vcu_ap_wait_dispatched`, return 1 from the handler for 0xC00x
requests. The scratch probe is in the opus-vdec session scratchpad (not in the tree); DEC01 turns it into `ap/n2ap_vdec.c`.
