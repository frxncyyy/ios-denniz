# HEVC_DEC_NOTES.md -- what differs for H.265 decode (vs facts/VDEC_PROTOCOL.md, facts/VDEC_REGISTERS.md)

Owner: opus-hevc. 11 November 2026. Static reading of the phone's `libvcodecdrv.so` ("drv") and `libvcodec_utility.so`
("util") only; no HEVC decode was run on the host or the phone. Marks: **[V]** read at the address given, **[I]** inferred.
Read `VDEC_PROTOCOL.md` (opus-vdec) and `VDEC_REGISTERS.md` (opus-dec2) first; this file lists only the HEVC differences.

## 1. Answer to the open question: where the HEVC syntax parser gets its bits

1. **VPS/SPS/PPS/SEI/VUI/HRD/PTL/scaling lists: software, from memory.** `H265_parseSPS/PPS/VUI/HRD/PTL/SEI/
   ScalingList` call only `H265_RBSPGetBits` / `H265_RBSPGetUE` (call counts: parseSPS 20+24, parsePPS 24+15, parseVUI
   27+12, parseHRD 17+6, parsePTL 25, parseSEI 18) and never a HAL reader. [V] (call graph over drv.s). As for H.264
   (`VDEC_REGISTERS.md` §0 item 1) a header-only START needs no register model; `Vdec_Drv_H265_open` itself calls
   `H265_parseSPS` [V].
2. **Slice header: per element, either software or the HEVC_VLD barrel shifter.** `H265_parseSliceHeader`
   (drv 0xccfe0) tests its 5th argument before every element: non-zero -> `H265_HAL_UeCodeNum` / `H265_HAL_SeCodeNum`
   / `H265_HAL_GetRealBitStream` (hardware), zero -> `H265_RBSPGetUE` / `H265_RBSPGetBits` (software), e.g. 0xcda6a-
   0xcda7a. [V] The caller `H265_handleSLICENALU` (0x8fc9e-0x8fcc8) passes `hw = !(inst[+0x195] != 0 || x[+0x28] < 2)`
   (x = a context at `[sp+0x50]`). [V] for the expression; the meaning of the two fields [I] (a "software parse" switch
   and probably the decode mode / number of cores). `H265_parseRPS` has the same dual readers (6 HAL + 6/6 RBSP). [V]
   **Model both** on the PC; on the phone the hardware variant means one register read per syntax element.
3. **The HEVC bit reader is the HEVC_VLD sub-block**, not AVC_VLD: `vdec_hal_h265_get_bits(hw, core, n)`
   (drv 0xe8c48) = `vdec_hal_read_hevc_vld(hw, core, 32)` for each full 32 bits, then `read_hevc_vld(.., n & 31)`;
   `vdec_hal_h265_ue_code_num` (0xe8ec6) = `read_hevc_vld(.., 0x26)`; `se_code_num` (0xe8ed4) = index 0x27;
   `get_bit_stream_shift` / `get_real_bit_stream` read index 0 (peek) [and 1]; `get_bit_stream_flg` index 0x21 [V].
   `vdec_hal_read_hevc_vld` (util 0x2c7dc) = load from `map_base + tbl[0x160 + 4*core] + 4*idx` (or the virtual-hw
   hook when one is installed) [V]; the sub-block base is 0x16028000 (core 0) / 0x16015000 (LAT) per
   `VDEC_REGISTERS.md` §1 [V there]. So the read convention is the AVC one: register index n (1..32) = take n bits,
   0x98 = ue(v), 0x9c = se(v), 0x00 = peek 32 bits. [V indices; I that reading index n consumes n bits, by analogy
   with the AVC VLD of `VDEC_REGISTERS.md` §2]
4. **Start-code search is hardware**: `H265_HAL_GetStartCode(_PicStart)` -> `vdec_hal_h265_shift_get_bit_stream`
   [V]; the kernel's GCE event list names the status: `VDEC_EVENT_8` "search_start_code_done HEVC_VLD(37) bit8=0",
   `VDEC_EVENT_9` "ref_reorder_done HEVC_VLD(37) bit4=0", `VDEC_EVENT_10` "wp_tble_done HEVC_VLD(37) bit0=0"
   (`K/include/uapi/linux/mtk_vcu_controls.h:106-140`) [V text] -> HEVC_VLD index 37 (0x94) is the status of the hardware
   start-code search, reference-list reordering (`H265_HAL_RPL_Modification`) and weighted-prediction table
   (`H265_HAL_PredWeightTable`) helpers [I that these helpers run in hardware on this path].

## 2. Other differences from the H.264 decoder

| Item | H.264 (`VDEC_REGISTERS.md`) | H.265 | Tag |
|---|---|---|---|
| IPI id | `IPI_VDEC_H264` = 2 | `IPI_VDEC_H265` = 3 | V (`VDEC_PROTOCOL.md` §1) |
| driver table | `h264_dec_*` | `h265_dec_init/decode/...` at drv .data 0x163924.. (R_ARM_ABS32) | V |
| syntax sub-block | AVC_VLD 0x22000 / LAT 0x12000 | **HEVC_VLD 0x28000 / LAT 0x15000** (plus VLD 0x20000 / 0x11000 for the FIFO + read pointer) | V |
| HAL layering | `vdec_hal_h264_*` | legacy `H265_HAL_*` is a thin layer over `vdec_util_h265_*` / `vdec_hal_h265_*` (e.g. `H265_HAL_DecStart` -> `vdec_util_h265_trigger_decode`, `H265_HAL_InitVDecHW` -> `vdec_util_h265_init_hw` -> `vdec_hal_h265_(lat_)init_vdec_hw`, `H265_HAL_SetReg` -> `vdec_util_h265_set_register`) | V |
| GCE | not used (direct MMIO) | **used for waits when `inst+0xd0` is set**: `H265_GCE_WaitEvent` (drv 0xd4cf8) -> `eVideoGCEAddCmdWaitEvent`; `Vdec_Drv_H265_open` calls `eVideoGCEInit` [V]. Decode done = `H265_GCE_WaitEvent(inst, 1, core)` = `VDEC_EVENT_1` "decode done, VDEC_TOP(41) bit16" (`H265_decode` 0xab5ec, `H265DecEndProc` 0x881b2); reset waits event 7 (process_flag) after `H265_GCE_WTF(7)`, LAT reset uses 0x17 (= `VDEC_LAT_EVENT_7`) (`H265_HAL_ResetHW` 0xda7e4, 0xda8ca-0xda8d8). `H265_GCE_WTF` (0xd4c8c), when GCE is on, calls `H265_HAL_WriteMISC(inst, 0x3e8, 1 << (ev & 15), 0xffff, core)` (whether 0x3e8 is a register index or a timeout is not decoded) | V calls/values; I: whether `inst+0xd0` is 1 on this phone, and the MISC write meaning (event arm) |
| register writes | direct | `H265_HAL_WriteHEVCVLD` in GCE mode -> `eVideoGCEAddCmdWrite`, else `vdec_hal_write_hevc_vld(_mask)` | V |
| LAT / core split | LAT = core index 3 | same tables; `Vdec_Drv_H265_decodeCore` is taken by address (GOT 0x161044) = the core-side worker of the LAT/core pipeline; UBE buffers `H265_InitUBEWorkBuf` / `H265_HAL_LatUBEResume` / `H265_HAL_UpdateUBEWPtr` | V names/refs, I roles |
| still image / HEIF | - | `H265_HAL_SetStillImageInfo`, `SetHEIFECModeReg`, `SetGridModeSubPicInfo` (same decoder serves HEIF) | V names |

## 3. What this means for the work

1. The PC model of DEC03 (VLD barrel shifter) must also cover **HEVC_VLD** (index n = n bits, 0x26 ue, 0x27 se,
   0 peek, 0x21 flag, status 0x94) at 0x16028000 and 0x16015000 if the slice-header hardware path is taken.
2. Because HEVC decode issues GCE lists (waits, and writes when `inst+0xd0`), V40's `gce_run_list()` must accept
   `codec_type == VCU_VDEC` lists with `CMD_WAIT_EVENT` VDEC_EVENT_1/7/LAT_7 and the 0x16000000 window (ABI rev 3
   `RWVC_WIN_VDEC_ALL`, DEC04/DEC07). [I] until a host HEVC decode run shows the list.
3. The kernel (`/dev/redwood-vcodec`) needs the VDEC "decode done" (VDEC_TOP index 41 bit 16) and "process flag"
   conditions for HEVC exactly as for H.264; no extra window beyond rev 3's 0x30000 (0x28000 and 0x15000 are inside).
4. Next host step (not done here): play `AP_IPIMSG_DEC_INIT` on `IPI_VDEC_H265` with a stream from the fake encoder
   (`fakevenc/run_session_hevc.sh` output: Main, CTB 32, I slice) once DEC01's input path works for H.264; then
   record the HEVC register/GCE trace with DEC02's harness.
