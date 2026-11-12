# HEVC_ENC_PROTOCOL.md -- what the vendor HEVC (H.265) encoder does with the MT6893 VENC

Owner: opus-hevc. 11 November 2026. Host-only analysis; nothing here was observed on the phone. Companion of
`facts/VENC_PROTOCOL.md` (opus-venc, H.264): read that first, only the differences are repeated here.

Sources: disassembly of the phone's own `libvcodecdrv.so` ("drv") and `libvcodec_utility.so` ("oal")
(`gpu/build/cam-vendor-20261109/vfs/lib`, read only, `llvm-objdump --triple=thumbv8a`; listings kept in /tmp only), and
complete host sessions of the unmodified vendor stack under our interpreter against the fake VENC
(`bash gpu/hwcodec/fakevenc/run_session_hevc.sh [--size WxH]`): INIT on `IPI_VENC_H265`, SET_PARAM, SEQ_HDR, one IDR,
EOS, DEINIT at 320x240, 640x480 and 1920x1080, streams validated by `tests/check/hevc_check.py` (ffprobe/ffmpeg).
Marks: **[V]** verified (code address, or seen in the host run: "run"); **[I]** inferred. Addresses are ELF offsets.

## 0. Summary

1. **Same block, same transport as H.264.** HEVC encode runs on the same VENC core window (0x17020000, core 0 in all
   runs), every register access is a GCE command (`VENC_WriteHW` in GCE mode), one `VCU_GCE_SET_CMD_FLUSH` +
   one `VCU_GCE_WAIT_CALLBACK` per frame, no `VCU_IPIMSG_ENC_POWER_ON`. [V] run (`mmio_r=0 mmio_w=0`, `gce_flushes=1`).
2. **Start = W 0x058 = 4 (FRM), done = GCE event VENC_EOF (0x20) on core 0 / VENC_EOF_C1 (0x24) on core 1**, then
   W 0x060 = 4 (ack), `CMD_MEM_MV` of 0x098 (BYTE_CNT) into the DRAM status block, W 0x038 = 0, W 0x0ec = 0.
   [V] `HEVC_EncodeFrame` drv 0x158a16 (trigger), 0x158aa4-0x158ab0 (`VENC_PollHW(h, 0x20|0x24, 4)` =
   wait event), 0x158b06 (ack; in GCE mode the value is forced to 4, 0x158afe), 0x158b8e (MEM_MV, source
   `regbase + 0x98`, 0x158b88), 0x158b98/0x158ba2.
3. **The bitstream size is 0x098, as for H.264.** [V] GCE path: MEM_MV above -> status word -> reported size (run:
   the fake's 0x098 value came back as the frame size, e.g. 43869 bytes); non-GCE path `VENC_ReadHW(0x98)` ->
   `bs->size` at 0x158c98/0x158c9e.
4. **VPS/SPS/PPS are written by software**: `hevc_enc_encode` (drv 0x1518ec) calls `HEVC_EncodeVPS` 0x154fbc,
   `HEVC_EncodeSPS` 0x155288, `HEVC_EncodePPS` 0x155ac0 (bit writer `HEVC_EncHdr_PutBits` 0x155f0c,
   `HEVC_EncHdr_Unsigned_Exp_Golomb` 0x15677c), no register access. [V] run (SEQ_HDR job: 78 bytes at 320x240, no
   VENC trigger, no GCE flush). The hardware header encoders `HEVC_EncodeVPS_HW` 0x158140 (trigger 0x058 = 0x20),
   `HEVC_EncodeSPS_HW` 0x158278 (= 0x1) and `HEVC_EncodePPS_HW` 0x1583b0 (= 0x2) exist but **have no caller and no
   PLT entry** (`grep 'Encode.PS_HW' drv.s` finds only their own bodies) = dead code. [V]
   So the IRQ status bits SPS 0x1 / PPS 0x2 / VPS 0x80 (kernel `mtk_vcodec_intr.h`) are never raised by this library.
5. **Register window: unchanged requirement.** All constant offsets of the HEVC functions are <= 0x12b0 [V]
   (`fakevenc/tools/wsites.py drv.s all`, the HEVC lines); the HEVC session used 65 distinct offsets, max 0x12b0 [V] run.
   A 0x2000 (or 0x3000) window per core covers H.264 and HEVC.
6. **Codec select bit: register 0x000 bit 2.** `HEVC_SetHWModeSel` (0x157d88) builds 0x000 from the base constant
   0x02000004 (0x157dd8-0x157dde), `H264_SetHWModeSel` from 0x0200bf60 (0x13e8b2-0x13e8b8). Run values: HEVC
   0x0f68fff5 (all three sizes), H.264 0x0f60bf70. [V] constants and values; "bit 2 = HEVC mode" [I].
7. **Joined headers (PREPEND, the daemon's mode)**: the frame is still encoded at the buffer start (0x064 = buffer base,
   run); afterwards `VENC_PrependHeader` (oal 0x228d0) memmoves the frame up by the header length on the CPU (0x2295c),
   copies VPS/SPS/PPS in front (0x22970-0x2298c) and cleans the range (`VENC_FlushRangeForIon`, 0x229a8). There is no
   invalidate before the memmove, so a stack whose guest view of the bitstream buffer is a copy (ours, `n2vc_dma.c`)
   must refresh that copy after the hardware wrote it, or the result is header + stale bytes. [V] run (header + zeros
   for H.264 and HEVC without the refresh; validated with it). Board post "opus-hevc HIGH" has the V40 patch.

## 1. Call path of one frame (differences from H.264)

    eVEncDrvEncode -> hevc_enc_encode (drv 0x1518ec; op switch: SEQ_HDR -> HEVC_EncodeVPS/SPS/PPS,
                                       FRAME -> HEVC_EncodeFrame 0x1584e8, PREPEND -> + VENC_PrependHeader)
      HEVC_EncodeFrame:
        VENC_ManageInputBuffer, HEVC_ResetVENC (0x156e9c), HEVC_SeqLvlMMRSetting (0x156fb8),
        HEVC_FrmLvlMMRSetting (0x15788c), W 0x068/0x064, HEVC_EncodeFrameEnc_SetBufs (0x158074),
        [W 0x108/0x10c = 3, conditional], eVideoQueueOutput, VENC_ManageFrameRef, W 0x074..0x088 (ref/MV),
        [W 0x1b0 0x1b4 0x11b8 0x11bc 0x1284 0x1280 0x1288, conditional], W 0x08c 0x090 0x1170 (RCPU),
        W 0x0a4, HEVC_SetHWModeSel (0x000), W 0x130 = 0xf (GCE mode),
        [MEM_MV + VENC_PollHWAddr when more than one core, 0x1589e2/0x1589f6], W 0x1030 = 0, W 0x0ec = 1, W 0x058 = 4,
        WAIT_EVENT 0x20, W 0x060 = 4, MEM_MV 0x098 -> status, W 0x038 = 0, W 0x0ec = 0,
        eVideoSetGCECmdFlush (0x158bee), [eVideoWaitGCECallBack 0x158c44 when the instance is synchronous]   [V]
      Non-GCE variant (not used): HEVC_WaitVENCDone (0x157f0c) = WaitISR(event struct, 1000 ms) then
        R 0x05c/0x1cc/0x1d0 on timeout, W 0x0f4 = 0; then R 0x098 0x03c 0x0bc..0x0d8.                        [V]

## 2. The GCE command list of one HEVC IDR frame [V] run (core 0)

78 commands at 320x240 and 640x480 (74 WRITE, 2 READ, 1 WAIT_EVENT, 1 MEM_MV), 80 at 1920x1080 (+ W 0x11c0 = 0,
W 0x11c4 = 0). Values: 320x240 / 640x480 / 1920x1080 (one value = same for all three).

| # | target | value | function |
|---|---|---|---|
| 0-3 | W/R/W/R 0x0a8 | 0, 1 | HEVC_ResetVENC 0x156f54-0x156f6e (same as H.264) |
| 4-34 | 0x1168 0x13c [0x11c0] 0x004 0x008 0x11b4 0x00c 0x020 0x024 0x028 0x1154 0x02c 0x114c 0x1150 0x115c 0x030 0x038 0x128 0x048 0x04c 0x050 0x054 0x1c0 0x1c4 0x12b0 0x12c 0x180 0x184 0x190 0x194 0x1240 0x1268 | section 3 | HEVC_SeqLvlMMRSetting 0x15706c-0x15760a |
| 35-52 | [0x11c4] 0x008 0x11b4 0x00c 0x010 0x014 0x018 0x01c 0x020 0x1244 0x1248 0x038 0x048 0x04c 0x050 0x054 0x0e4 0x1250 0x11c8 | | HEVC_FrmLvlMMRSetting 0x157924-0x157d24 |
| 53-54 | 0x068, 0x064 | 0x2000 (1 MiB >> 7), bitstream pa >> 4 | HEVC_EncodeFrame 0x158672/0x158680 |
| 55-57 | 0x06c 0x070 0x094 | luma, chroma pa >> 4, 0 | HEVC_EncodeFrameEnc_SetBufs 0x158084-0x158110 |
| 58-63 | 0x074 0x078 0x07c 0x080 0x084 0x088 | pa >> 4 of reference / MV buffers | 0x158794-0x1587ee |
| 64-66 | 0x08c 0x090 0x1170 | RCPU microcode copy, RC buffers, pa >> 4 | 0x15889c-0x1588c2 |
| 67-68 | 0x0a4 = 0, 0x000 = 0x0f68fff5 | | 0x1588d4, HEVC_SetHWModeSel 0x157e6a |
| 69-72 | 0x130 = 0xf, 0x1030 = 0, 0x0ec = 1, **0x058 = 4** | | 0x15897a, 0x158a02-0x158a16 |
| 73 | WAIT_EVENT 0x20 | VENC_EOF | 0x158ab0 |
| 74 | W 0x060 = 4 | ack FRM | 0x158b06 |
| 75 | MEM_MV 0x17020098 -> status block | BYTE_CNT | 0x158b8e |
| 76-77 | 0x038 = 0, 0x0ec = 0 | | 0x158b98, 0x158ba2 |

Compared with H.264 (68 commands): HEVC reads back only 0x098 (H.264 copies 7 result registers 0x098 0x064 0x014
0x0c0 0x0c8 0x0bc 0x1030) and has no DRAM semaphore MEM_MV/POLL_ADDR in the single-core case. [V] run

## 3. Registers (offsets from the core base); HEVC differences

HEVC-only offsets in the runs: 0x038 0x0e4 0x180 0x184 0x190 0x194 0x114c 0x1150 0x1154 0x115c 0x1240 0x1244 0x1248
0x1250. H.264-only: 0x124 0x16c 0x1204 (+ the read-back 0x0bc 0x0c0 0x0c8). Everything else is shared. [V] run

| Off | 320x240 / 640x480 / 1920x1080 | Meaning |
|---|---|---|
| 0x000 | 0x0f68fff5 | mode word; bit 2 set for HEVC (section 0 item 6) [V value, I meaning] |
| 0x004 | 0x7007801e | sequence parameters (H.264: 0x5807801e) [I] |
| 0x008 = 0x11b4 | 0x73a0 / 0x73a0 / 0x7000 (seq), same in frame | [I]; 0x11b4 = 0x573a0 at 1080p |
| 0x010 | 0x800e | frame type / QP [I] |
| 0x020 | 0x200100ff | [I] (H.264 0xff) |
| 0x024 | 0x000f0014 / 0x001e0028 / 0x00440078 | **(rows16 << 16) \| cols16** = (h/16, w/16) [V: 3 sizes; 1080p uses 1088/16 = 68] |
| 0x028 | 0x0140a000 / 0x02814000 / 0x0783c008 | **(luma pitch/16) << 20 \| (chroma pitch/16) << 11** (+ low bits at 1080p) [V: 3 sizes] |
| 0x02c | 0x80000050 / 0x8000012c / 0x811007f8 | bits 0-15 = number of 32x32 CTBs (80 / 300 / 2040) [V: 3 sizes]; high bits [I] |
| 0x114c | 0x00f00140 / 0x01e00280 / 0x04400780 | **(coded height << 16) \| width** (1088 at 1080p) [V: 3 sizes] |
| 0x1154 | 0x00140014 / 0x00280028 / 0x00780078 | (w/16) twice [V by value, meaning I] |
| 0x115c | 0x0000f014 / 0x0001e028 / 0x00044078 | (h/16) << 8 \| (w/16) [V by value] |
| 0x01c4 | 0x003e002e / 0x007b005c / 0x016e0112 | size dependent [I] |
| 0x048 | 0x01040099 / 0x01040266 / 0x01041033 | rate control (frame bits?) [I] |
| 0x038 | 0x2123 seq, 0x21bb frame, 0 after done (0x2923/0x29bb at 1080p) | [I] |
| 0x180 0x184 0x190 0x194 | 0x80000040, 1, 0x1e, 1 | [I] (0x190 = 30 = fps/GOP?) |
| 0x064 0x068 0x06c 0x070 0x094 0x074-0x08c 0x090 0x1170 | buffers, pa >> 4 | as H.264 [V] |
| 0x058 / 0x05c / 0x060 / 0x098 / 0x0a8 / 0x0ec | trigger 4, IRQ status, ack, BYTE_CNT, reset, enable | as H.264 [V] |

Picture geometry seen by the hardware is the coded size (1920x1088 for 1080p; the SPS crops 8 lines). The source
chroma plane started at luma + 1920*1080 in the run (the runner's NV12 has 1080 lines), so the hardware reads 8 luma
lines from the chroma plane for the padding rows; the crop hides them. [V] run (0x070 - 0x06c = 0x1fa400). A
1088-line source (kernel default `buf_height` 1088) avoids that.

## 4. VPS/SPS/PPS the library writes [V] run (parsed with a host script)

Identical at 320x240, 640x480, 1920x1080 except size and level_idc: Main profile (profile_idc 1), tier Main,
level_idc 60 / 90 / 120 (session asked for V4L2 level 4 = vendor 10; small sizes get a lower level), 4:2:0, 8 bit,
conformance window flag 1 (0,0,0,4 at 1088 -> 1080), log2_max_poc_lsb 8, **min CB 16, CTB 32, TB 8..16,
max_transform_hierarchy_depth inter 1 / intra 0**, no scaling list, AMP off, **SAO on, PCM off**, one short-term RPS
(one negative ref, delta 1), long-term off, TMVP on, strong intra smoothing on, VUI (video_signal_type: fmt 5,
limited range, BT.601-625 primaries 5 / transfer 6 / matrix 5; no timing). PPS: sign data hiding on, cabac_init_present
off, init_qp 26, constrained intra off, **transform skip off, cu_qp_delta on (depth 0)**, chroma offsets 0,
**transquant bypass off**, no tiles, no WPP, loop filter across slices on, deblocking control present with override
enabled (beta/tc 0), parallel merge level 5. SEQ_HDR job: 78 / 79 / 80 bytes.
Consequence for the fake: no PCM and no bypass -> the fake cannot emit a lossless picture under these headers; it
emits a real (lossy, QP 22) CABAC intra slice.

## 5. RCPU microcode (rate control firmware)

The HEVC rate-control firmware is chosen by `HEVC_GetRCCode` (0x1567d4) from 10 images of 0x8040 bytes each (drv
.data; `llvm-nm -S`), all named `ucHEVCRCPUCode_*` [V]:

| condition (instance fields) | image |
|---|---|
| +0x58ed5 == 1 (WFD), +0x2c != 1 / == 1 | `_2020_0908_FR_WFD_rmSkype_MT6873` (0x1ec298) / `_20190417_FR_WFD` (0x1ac098) |
| +0x58ed4 == 1 (ViLTE) | `_20190417_ViLTE` (0x1dc218) |
| +0x228 in {0, 2} | `_20191016_FR_CBR` (0x1a4058) |
| +0x228 == 1, by ROI type +0x470 = 1 / 2 / 3 / other | `_2020_0617_FR_CBR_FrmQpCtrl` (0x1f42d8) / `_2020_0826_ROI_QP_block_level_full_control_MT6873` (0x1fc318) / `_2022_0222_FR_CBR_ROI_rmSkype_MT6885` (0x20c398) / `_20200917_FR_CBR_ViLTE_rmSkype` (0x2143d8) |
| otherwise, +0x2c == 1 / != 1 | `_20191016_FR_ABR` (0x19c018) / `_20190417_ABR` (0x1bc118) |

(GOT slots 0x1612d8-0x1612fc, R_ARM_GLOB_DAT.) [V] for the table; which branch a camera/VideoToolbox session takes
[I] (+0x228 is probably the rate-control mode). The image is copied into an ion buffer and its pa >> 4 goes to 0x08c,
exactly as for H.264 [V] run (0x08c written with a 0x8040-byte pool buffer).

## 6. Buffers and messages

AP side identical to H.264 apart from the IPI id: `AP_IPIMSG_ENC_INIT` on `IPI_VENC_H265` (16); profile/level converted
by `venc_h265_get_profile/level` (`venc_common_if.c:22-60`: MAIN -> 2, level L, tier T -> 2L + T). V41's
`ven_h265_profile/level` already do this [V]. The library allocates the same kinds of ion buffers (RC 3 x 0x800,
microcode 0x8040, reference/reconstruction and MV buffers sized from the 32x32 CTB grid) [V] run.

## 7. Unknowns only the phone can answer

1. Whether VENC_EOF is also raised for HEVC on the real block exactly as for H.264 (same event per the code) and the
   real time per 1080p frame.
2. Meaning of 0x000 bits other than bit 2, 0x038, 0x048, 0x180-0x194, 0x1154/0x115c/0x114c beyond the geometry.
3. Which RCPU image a VideoToolbox session selects (section 5) and whether the RCPU needs anything besides 0x08c.
4. Behaviour when the bitstream buffer is too small (DRAM bit 0x8): the library reported the full buffer size as the
   frame size in the host run with an overflow (fake behaviour) -- the daemon must bound `bytes` by the capacity.
