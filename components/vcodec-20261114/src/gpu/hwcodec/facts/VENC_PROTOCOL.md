# VENC_PROTOCOL.md -- what the vendor H.264 encoder does with the MT6893 VENC hardware

Owner: opus-venc. 11 November 2026. Host-only analysis. Nothing in this file was observed on the phone.

Sources: disassembly of the phone's own `libvcodecdrv.so`, `libvcodec_utility.so` (the OAL), `libvpud_vcodec.so` and
`bin/vpud` (`gpu/build/cam-vendor-20261109/vfs`, read only, `llvm-objdump --triple=thumbv8a`), the Linux kernel tree
(`K` = `nord2-current-kernel`), and a complete host session of the unmodified vendor stack under our interpreter against
the fake VENC (`gpu/hwcodec/fakevenc/`, `bash gpu/hwcodec/fakevenc/run_session.sh`): SEQ_HDR + one IDR frame, 320x240 and
640x480, decoded by ffmpeg.

Marks: **[V]** verified (code address given, or seen in the host run of the real library: "run"); **[I]** inferred
(reasoned from names, values or the kernel, not proven). Library addresses are offsets in the ELF file (Thumb code).
"drv" = libvcodecdrv.so, "oal" = libvcodec_utility.so, "vv" = libvpud_vcodec.so.

## 0. Summary (for the kernel driver and the phone bring-up)

1. **All VENC register traffic of an encode goes through GCE command lists** (`VCU_GCE_SET_CMD_FLUSH`), never through
   the MMIO mapping. `VENC_WriteHW` (oal 0x26cd0) tests the GCE flag at handle+0x58eda and then calls
   `eVideoGCEAddCmdWrite(cmdbuf, regbase_pa + off, value, 0xffffffff)`; only without that flag would it store to the
   mapped VA (oal 0x26d28-0x26d32). Run: `mmio_r=0 mmio_w=0` while 62 register writes reached the hardware model.
   **[V]** vpud still mmaps both VENC windows (0x17020000 and 0x17820000, length 0x3000) at start-up. **[V]** run.
2. **Register offsets used by the whole library (H.264 + HEVC, every `VENC_WriteHW/ReadHW` call site with a
   constant offset): 0x000 ... 0x12b0** (103 distinct offsets, list in section 3). The H.264 session of the run
   used 61 of them, all < 0x1300. **The kernel must expose each VENC core window as at least 0x0000-0x12FF; exposing
   0x0000-0x1FFF (the DT size 0x2000) covers everything; nothing above 0x12b0 is ever addressed.** [V]
3. **Buffer addresses are written as `pa >> 4`** (16-byte units, so 36-bit reach): bitstream 0x064, source luma 0x06c,
   source chroma 0x070, RCPU microcode 0x08c, ... [V] run (e.g. bitstream pa 0xda81d000 -> `W 0x064 0x0da81d00`).
   The values are exactly the physical addresses our `/dev/vcu` model hands out (V41 DMA pool 0xda800000..), so
   **with IOMMU bypass the PA goes to the register unchanged; the library never calls the M4U itself** (no M4U import;
   it asks ion for `ION_MM_GET_IOVA` and uses what comes back). All VENC DMA ports of larb7 (core 0) must therefore be
   in PA mode (`SMI_LARB_NON_SEC_CON` bit 0 = 0), exactly as M1 did for the JPEG ports. [V] for the values, [I] for
   which larb ports (see section 7).
4. **Done = GCE event `VENC_EOF` (32)**; the library then copies 7 result registers into a DRAM status block with GCE
   `CMD_MEM_MV`, acks the interrupt (`W 0x060 = 4`) and disables (`W 0x0ec = 0`). The bitstream size it reports is
   **0x098 (BYTE_CNT, bytes)**. [V]
5. **Microcode ("RCPU") is not written to a register window**: `ucH264RCPUCode_AQ` (32832 = 0x8040 bytes, drv data
   0x16be0c) is `memcpy`'d into an ion buffer of 0x8040 bytes, cache-flushed, and its `pa >> 4` is written to **0x08c**;
   the VENC's RCPU fetches it by DMA. [V] run (`memcpy(ion_va, 0x16be0c+base, 0x8040)` at oal 0x1869b, then
   `W 0x08c 0x0da92c00` for that buffer's pa 0xda92c000).
6. **Power/clock**: in GCE mode the daemon sends **no** `VCU_IPIMSG_ENC_POWER_ON`; the Linux kernel powers the encoder
   core in `vcu_gce_set_cmd_flush()` (`venc_encode_prepare()` when the core's first job is flushed, `mtk_vcu.c:994`)
   and unprepares it in the flush callback (`:822`). Our replacement for that kernel code must power VEN (+ venc gates,
   larb7) around every flushed list. [V] run (no POWER_ON IPI), kernel source.
7. **Sequence header (SPS/PPS) is software**: `h264_enc_encode_Header` (drv 0x134cc8) calls `H264_EncodeSPS` /
   `H264_EncodePPS` and writes them with the CPU, then `VENC_FlushRangeForIon`; no register access. [V] run
   (29 bytes for 320x240: Baseline profile_idc 66, level_idc 10, CAVLC).

## 1. Software layers and the call path of one frame

    AP (kernel V4L2 or our n2ap_venc)  --AP_IPIMSG_ENC_ENCODE-->  vpud venc_vcodec_msg_handler (vv 0x11ac4)
      -> ion_import(input_fd / bs_fd) + ion_mmap + ION_MM_GET_IOVA      (vv 0x13dcc helper)              [V]
      -> eVEncDrvEncode (drv 0x79478) -> h264_enc_encode -> H264_EncodeFrame / H264_EncodeFrameStart       [V]
         -> eVideoGceAquireBuf, eVideoGceResetCmdBuf  (one 0xe910-byte command buffer per job)          [V]
         -> H264_ResetVENC, H264_SeqLvlMMRSetting(_EncInfo), H264_FrmLvlMMRSetting(_EncInfoOthers),
            H264_EncodeFrameEnc_SetRC / _SetBufs / _SetMV / _TriggerHW, H264_EncodeFrameEnc_WaitHW,
            H264_EncodeFrameEnc_ReadResult   -> each register access = one GCE command                  [V]
         -> eVideoSetGCECmdFlush  -> ioctl VCU_GCE_SET_CMD_FLUSH (0x40207608)                           [V]
      -> ENCODE answered at once with VCU_IPIMSG_ENC_ENCODE_ACK (0xd005) -- asynchronous                 [V] run
    VENC_gceWaitEventThread (oal 0x22248): eVideoWaitGCECallBack -> ioctl VCU_GCE_WAIT_CALLBACK          [V]
      -> VENC_gceEncodeEnd (oal 0x21ab8): status block word[idx*8] (= BYTE_CNT) -> bitstream size,
         eVideoInvalidateCache(bitstream), ring list_free entry, VCU_IPIMSG_ENC_PUT_BUFFER (0xe003)       [V]

The SEQ_HDR job is synchronous: answered with ENCODE_ACK after the CPU wrote the header; the size is left in
`venc_vsi.list_free.bs_size[]` (no PUT_BUFFER), so the AP must drain `list_free` after every encode, exactly like the
kernel's `mtk_enc_put_buf()` in synchronous mode. [V] run (without the drain the header size reads 0 and the frame's
first PUT_BUFFER returns the header's 29 bytes).

## 2. The GCE command list of one H.264 IDR frame (core 0, 320x240) [V] run

68 commands, `enum gce_cmd_id` (`K/include/uapi/linux/mtk_vcu_controls.h`): 0 READ, 1 WRITE, 3 WAIT_EVENT, 4 MEM_MV,
5 POLL_ADDR. Register addresses are absolute: 0x17020000 + offset (core 0; core 1 would be 0x17820000 + offset).

| # | cmd | target | value | from |
|---|---|---|---|---|
| 0-3 | WRITE/READ/WRITE/READ | 0x0a8 | 0, (0), 1, (1) | H264_ResetVENC drv 0x13cfd0-0x13cfea |
| 4-6 | WRITE | 0x004, 0x008, 0x00c | 0x5807801e 0x626e5faf 0x102f8000 | _SeqLvlMMRSetting_EncInfo 0x13d58a.. |
| 7-23 | WRITE | 0x024 0x028 0x02c 0x030 0x048 0x04c 0x050 0x11c8 0x054 0x12c 0x1c0 0x1c4 0x1168 0x13c 0x1204 0x11c0 0x1268 | see section 3 | H264_SeqLvlMMRSetting 0x13d6da.. |
| 24-33 | WRITE | 0x010 0x16c 0x014 0x018 0x11c4 0x01c 0x020 0x11b4 0x124 0x128 | | _FrmLvlMMRSetting(_EncInfoOthers) |
| 34-36 | WRITE | 0x08c 0x090 0x1170 | RCPU code / RC buffers, pa>>4 | _EncodeFrameEnc_SetRC 0x13e116.. |
| 37-41 | WRITE | 0x068 0x064 0x06c 0x070 0x094 | bs size, bs, luma, chroma, plane 3 | _EncodeFrameEnc_SetBufs 0x13e168.. |
| 42-47 | WRITE | 0x074 0x078 0x07c 0x080 0x084 0x088 | frame/MV buffers, pa>>4 | _EncodeFrameEnc_SetMV 0x13e4b4.. |
| 48-52 | WRITE | 0x1030=0, 0x130=0xf, 0x0a4=0, 0x000=0x0f60bf70, 0x12b0=0x1ff | | _TriggerHW 0x13e73a.., H264_SetHWModeSel 0x13e95e |
| 53 | MEM_MV | 0xda923000 -> 0xda928000 | DRAM word copy | _TriggerHW 0x13e7ee |
| 54-55 | WRITE | 0x0ec = 1, **0x058 = 4 (start FRM)** | | _TriggerHW 0x13e77a, 0x13e784 |
| 56-57 | MEM_MV, POLL_ADDR | 0xda927000 -> 0xda928000; poll 0xda928000 == 1 | DRAM semaphore | _TriggerHW 0x13e86a, VENC_PollHWAddr |
| 58 | WAIT_EVENT | 32 = VENC_EOF | frame done | _EncodeFrameEnc_WaitHW 0x13eaca -> VENC_PollHW (oal 0x26f40) |
| 59-65 | MEM_MV | 0x098 0x064 0x014 0x0c0 0x0c8 0x0bc 0x1030 -> status block +0x00..+0x30 | result registers | _ReadResult 0x13eba6.. |
| 66-67 | WRITE | 0x060 = 4 (ack FRM), 0x0ec = 0 | | _ReadResult 0x13ecb8, 0x13ecc2 |

The status block and the semaphores are 4 KiB buffers from `VCU_PA_ALLOCATION` (pa 0xda920000.., 12 pages) that the
GCE reads/writes by physical address. In a single-core encode the POLL_ADDR is satisfied immediately (the CPU left 1 in
0xda927000). [V] run. With two cores the same words synchronise the cores. [I]

`VENC_ReadHW` in GCE mode only queues a READ (oal 0x26c7a) and returns 0 -- the library never consumes the value of a
GCE READ; results come back through MEM_MV into DRAM. [V]

## 3. Register map (core window, offset from 0x17020000 / 0x17820000)

Values are those of the 320x240 run (640x480 differences in brackets). Meaning column: [V] only where the code or the
kernel proves it.

| Off | Written by | Value (320x240) | Meaning |
|---|---|---|---|
| 0x000 | H264_SetHWModeSel | 0x0f60bf70 | mode / enable bits [I] |
| 0x004 0x008 0x00c | _SeqLvlMMRSetting_EncInfo | 0x5807801e 0x626e5faf 0x102f8000 | sequence coding parameters (profile, level, entropy...) [I] |
| 0x010 | _FrmLvlMMRSetting | 0x0332b406 | frame type / QP parameters [I] |
| 0x014 0x018 0x01c 0x020 | _FrmLvlMMRSetting_EncInfoOthers | 0 0 0 0xff | frame parameters; 0x014 is read back after done [I] |
| 0x024 | _SeqLvlMMRSetting | 0x000f0014 [0x001e0028] | `(mb_rows << 16) | mb_cols` [V by the two sizes] |
| 0x028 | _SeqLvlMMRSetting | 0x0140a000 [0x02814000] | `(luma_pitch/16) << 20 | (chroma_pitch/16) << 11` [V by the two sizes] |
| 0x02c | | 0x12c | MB count (300) [V by value] |
| 0x030 | | 0x02800000 (both sizes) | [I] |
| 0x048 0x04c 0x050 0x054 | | 0x00040099 0x001e0503 0x03003300 0x808 | GOP / rate / slice parameters [I] |
| 0x058 | _TriggerHW | 4 | **start: 1 SPS, 2 PPS, 4 frame** [V write 4; bits = kernel IRQ bits] |
| 0x05c | (kernel) | -- | **IRQ status: SPS 0x1, PPS 0x2, FRM 0x4, DRAM(bitstream full) 0x8, PAUSE 0x10, SWITCH 0x20, VPS 0x80** [V] `K/.../mt6885/mtk_vcodec_intr.h:22-31` |
| 0x060 | _ReadResult | 4 | **IRQ ack (write 1s)** [V] same header (`MTK_VENC_IRQ_ACK_OFFSET`) |
| 0x064 | _SetBufs | bs pa >> 4 | **bitstream buffer** [V]; read back after done [V], meaning of the read value [I] |
| 0x068 | _SetBufs | 0x2000 (1 MiB buffer) | bitstream buffer size, probably `bytes >> 7` [I] |
| 0x06c | _SetBufs | luma pa >> 4 | **source luma** [V] |
| 0x070 | _SetBufs (3 variants by format) | chroma pa >> 4 | **source chroma (NV12 UV plane)** [V] |
| 0x094 | _SetBufs | 0 | third plane (0 for 2-plane formats) [I] |
| 0x074 0x078 0x07c 0x080 | _SetMV, H264_SearchReference | pa >> 4 of 0x1f200/0xa000-byte buffers | reference/reconstruction luma+chroma [I] |
| 0x084 0x088 | _SetMV, H264_SearchReference | pa >> 4 of 0xc00-byte buffers | motion-vector save/load [I] |
| 0x08c | _SetRC | pa >> 4 of the 0x8040 microcode copy | **RCPU microcode base** [V] |
| 0x090 0x1170 | _SetRC | pa >> 4 of 0x800-byte buffers | RCPU data / rate-control buffers [I] |
| 0x098 | read by _ReadResult | -- | **bitstream byte count** [V]: non-GCE path `VENC_ReadHW(0x98)` -> bs size (drv 0x13ec38); GCE path -> status word -> `bs->size` (oal 0x21bd6) |
| 0x0a4 | _TriggerHW | 0 (or 0x3f) | [I] |
| 0x0a8 | H264_ResetVENC | 0 then 1 | **soft reset** (write/read 0, write/read 1) [V sequence], polarity [I] |
| 0x0bc 0x0c0 0x0c8 | read by _ReadResult | -- | statistics read after done (logged) [I] |
| 0x0ec | _TriggerHW / _ReadResult | 1 before start, 0 after | engine enable / IRQ enable [I]; the kernel's timeout dump also writes 0xEC=1 (`mtk_vcodec_intr.c:315`) |
| 0x12c 0x130 0x13c 0x16c 0x124 0x128 0x1c0 0x1c4 | | 0x200 0xf 0x10000 0 0 0 0x11 0x3e002e | [I] |
| 0x1030 | _TriggerHW (=0), read after done | 0 | [I] |
| 0x1168 0x11b4 0x11c0 0x11c4 0x11c8 0x1204 0x1268 0x12b0 | | 0x11a 0x26f 0 0 6 0 0 0x1ff | [I] |

Every offset with a constant at a call site in drv (H.264, HEVC, HEIF, WP): 0x0 0x4 0x8 0xc 0x10 0x14 0x18 0x1c 0x20
0x24 0x28 0x2c 0x30 0x38 0x3c 0x48 0x4c 0x50 0x54 0x58 0x5c 0x60 0x64 0x68 0x6c 0x70 0x74 0x78 0x7c 0x80 0x84 0x88 0x8c
0x90 0x94 0x98 0xa4 0xa8 0xbc 0xc0 0xc4 0xc8 0xcc 0xd0 0xd4 0xd8 0xe0 0xe4 0xec 0xf4 0x108 0x10c 0x124 0x128 0x12c 0x130
0x13c 0x144 0x16c 0x180 0x184 0x190 0x194 0x1b0 0x1b4 0x1c0 0x1c4 0x1cc 0x1d0 0x1ec 0x1f0 0x1030 0x114c 0x1150 0x1154
0x115c 0x1160 0x1164 0x1168 0x1170 0x11a0 0x11b4 0x11b8 0x11bc 0x11c0 0x11c4 0x11c8 0x11cc 0x11d0 0x11fc 0x1204 0x1240
0x1244 0x1248 0x1250 0x1268 0x1280 0x1284 0x1288 0x129c 0x12b0 (251 call sites; the 6 the script cannot resolve are 0x11fc in `H264_WP_Stage2` (drv 0x13f046),
`VENC_PollHW` event numbers (below) and `VENC_PollHWAddr` DRAM-semaphore indexes, i.e. no further register). [V] `python3 gpu/hwcodec/fakevenc/tools/wsites.py drv.s all` (drv.s = llvm-objdump of drv, kept in /tmp).

GCE events used by the encoder (`enum gce_event_id`): `H264_EncodeFrameEnc_WaitHW` waits for 0x20 = 32 VENC_EOF on
core 0 and 0x24 = 36 VENC_EOF_C1 on core 1 (drv 0x13eabc-0x13eaca); 37 VENC_WP_2ND_DONE / 38 VENC_WP_3ND_DONE belong to
the weighted-prediction passes (`H264_WP_Stage2/3`). In GCE mode `VENC_PollHW` is only `eVideoGCEAddCmdWaitEvent`
(oal 0x26f40). [V]; WP did not run in the session [V], whether it is ever enabled for camera encodes [I].

## 4. Buffers the library allocates for an H.264 session (320x240) [V] run

Through ion (`ion_alloc_camera`, heap flags 3, then `ION_MM_GET_IOVA` -> pa): 0x428, 0x100, 0x120 (daemon init);
per session 3 x 0x800 (RC), 0x8040 (microcode copy), 2 x 0x1f200 + 2 x 0xa000 (frame buffers), 2 x 0xc00 (MV),
3 x 0x5000, 2 x 0xc00, 0x80. Through `VCU_PA_ALLOCATION` + mmap: 12 x 4 KiB (GCE status/semaphores). The source frame
and the bitstream come from the AP as dma-buf fds (`input_fd[]`, `bs_fd` of `venc_ap_ipi_msg_enc`) and are resolved
with `ion_import` (vv 0x13e00); fd 0 fails the job ("ion_import failed, ion_buf_fd = 0"). [V]

## 5. /dev/vcu traffic of one session [V] run (n2vcu counters)

`VCU_SET_OBJECT` 7, `VCU_GET_OBJECT` 5 (IPI INIT, SET_PARAM, ENCODE x2, DEINIT; the daemon answers 0xd000, 0xd001,
0xd005 x2, 0xe003 PUT_BUFFER, 0xd003), `VCU_PA_ALLOCATION` 9, mmap 14 (DMEM alloc + commit, VDEC 0x16000000/0x40000,
VENC 0x17020000/0x3000, VENC_C1 0x17820000/0x3000, PA pages), `VCU_GCE_SET_CMD_FLUSH` 1 and `VCU_GCE_WAIT_CALLBACK` 1
per frame; cache maintenance goes through ion (`ION_SYS_CACHE_SYNC`, clean after CPU writes, invalidate before the CPU
reads the bitstream). DMEM: vpud's `remap_shmem` (vpud 0x3312-0x334c) maps the COMMIT range MAP_FIXED over
libvpud_vcodec's .data, so `vcu_inst_addr` points into that segment (kernel `VCU_DMEM0_VMA`). [V]

## 6. What the kernel driver (/dev/redwood-vcodec) must allow and forward

- Register windows: VENC core 0 0x17020000 + [0, 0x1300) (allow 0x2000), core 1 0x17820000 + [0, 0x1300) if the
  library ever selects core 1 (it did not for 320x240/640x480; `q.core_id` 0). Read, write, masked write, poll. [V]
- One interrupt per finished job on the VENC line, with the status the handler read from 0x05c (and the ack at
  0x060). The library acks 0x060 again itself (GCE WRITE 0x060=4): a double ack must be harmless. [V]/[I]
- Power: VEN domain + venc_gcon gates + larb7 around every job (the library sends no power IPI in GCE mode). [V]
- DMA: physical addresses below 64 GiB, 16-byte aligned (the registers take pa>>4); pool buffers for source, bitstream,
  microcode, reference frames, MV, RC and the GCE status pages. [V] value form, [I] larb port list.
- No M4U/IOMMU call is made by the library itself; ion `ION_MM_GET_IOVA` must return the PA. [V]

## 7. Unknowns only the real hardware can answer

1. Whether frame-done raises GIC SPI 308 with 0x05c bit 2 when GCE is not used (Linux takes it as a GCE event); and
   whether 0x0ec=1 is what enables that interrupt.
2. The larb7 port numbers of VENC (RCPU, REC, BSDMA, SV_COMV, RD_COMV, CUR_LUMA, CUR_CHROMA, REF_LUMA, REF_CHROMA) and
   whether all of them honour PA mode; whether core 1 uses larb8.
3. Units of 0x068 (bitstream size) and the DRAM/overflow behaviour when the buffer is too small.
4. What 0x064 reads back after a frame (base or end pointer) and the meaning of 0x014/0x0bc/0x0c0/0x0c8/0x1030.
5. Whether the RCPU really boots from the 0x08c copy without any further handshake (no register poll exists for it in
   the code path above).
6. Whether reset 0x0a8 polarity (0 = reset asserted) is right and whether the block needs a delay after it.
7. Timing: the encoder's real frame time at the `venc_sel` parent found on the phone (CLK_CFG_13 parent 12).
8. Any register the hardware needs that Linux sets outside this library (DCM, OSTD/ultra of larb7 VENC ports,
   `VENC_CG`), i.e. what `venc_encode_prepare()` / mtk_vcodec_enc_pm.c do besides clocks.

## 8. The host model of this protocol

`gpu/hwcodec/fakevenc/n2venc_fake.c` implements sections 2-3 for the host: reset, trigger 0x058 bit 2 -> an IDR slice of
I_PCM macroblocks (lossless, valid H.264 for the library's own SPS/PPS) written at 0x064<<4 from the NV12 source at
0x06c/0x070<<4, BYTE_CNT 0x098, IRQ status 0x05c, one interrupt, ack 0x060, polls on the model. It does not model rate
control, references or motion, so P frames would also come out as IDR-like I_PCM slices (not exercised).
