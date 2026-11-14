# HEVC_DEC_PROTOCOL.md -- what the vendor HEVC decode path does with the VDEC, per frame, and what the phone needs

Owner: opus-hevcdec. 11 November 2026. Host only: nothing here has run on the phone. Read `HEVC_DEC_NOTES.md` (opus-hevc),
`VDEC_PROTOCOL.md`, `VDEC_REGISTERS.md` first; this file supersedes HEVC_DEC_NOTES.md where they differ (marked).

Sources: static reading of the phone's `libvcodecdrv.so` (**drv**) and `libvcodec_utility.so` (**util**), 32-bit Thumb,
`llvm-objdump -d --triple=thumbv8a` (scratch, not in the tree); and the **host run**: the phone's unmodified vpud + vendor
libraries under the interpreter, driven by `fakevdec_hevc/vdec_hevc_ref.c`, every VDEC register access recorded by
`fakevdec_hevc/n2vdec_hevc_fake.c` (`VR_TRACE=1`). Tags: **[V]** read at the address given, **[V-run]** observed in the
host trace/counters, **[I]** inferred (reason given). Register notation: `BLOCK(n)` = word index n, byte offset 4n from the
sub-block base. Only symbol names, addresses, constants and vendor log text are taken from the vendor objects.

Reproduce: `bash gpu/hwcodec/fakevdec_hevc/run_hevc_ref.sh` -> `N2TEST HEVCDEC-REF PASS 3/3` (model unit test 30/30,
320x240 12 AUs with B-frames, 1920x1080 6 AUs with B-frames; display order checked against ffprobe). Under a minute on this host.

## 0. Summary

1. **HEVC video decode is direct MMIO, like H.264. No GCE.** [V-run] 0 GCE flushes / 0 commands in 18 HEVC frames.
   [V] the GCE switch `inst+0xd0` is cleared in `Vdec_Drv_H265_open` (drv 0x93bd6) and only set, to
   `atoi(property_get("vendor.mtk.vdec.h265.gce", "1"))`, when the open's 6th argument is 0x1c (0x93c44-0x93c72);
   `eVideoGCEInit` (0x958ae) and every `H265_GCE_*` (e.g. `H265_GCE_WaitEvent` 0xd4cf8 tests `inst+0xd0`) depend on it.
   Forcing that branch in a throwaway copy changed the stream geometry to 2560x240 (a grid/still-image mode) and still
   issued no GCE list before the first frame. **[I]: 0x1c is the HEIF/still-image format; GCE is the HEIF path, not video.**
   The userland (and therefore the 6th argument) is identical on the phone; HEVC_DEC_NOTES §2 "GCE used for waits" is
   correct only for that mode.
2. **LAT + core pipeline, one job each per frame.** [V-run] Per AU vpud sends LOCK_LAT, LOCK_CORE, WAITISR(1 = LAT),
   WAITISR(0 = core), UNLOCK_LAT, UNLOCK_CORE (counts 1/1/2/1/1 per AU), exactly the H.264 message pattern. The LAT unit
   parses the slice from the bitstream (VLD 0x16011000 + **LAT HEVC_VLD 0x16015000**); the core (VLD 0x16020000 +
   HEVC_VLD 0x16028000, MC, MV, PP, MISC) reconstructs from the LAT output and never reads the bitstream.
3. **Per frame**: 320x240 and 1080p identical in shape: ~425 register reads + 340-470 writes (n2vcu counters; reads
   include the read half of `_mask` RMWs), 17 pages; **17.7 % of the accesses are outside the ten frozen windows** (LAT
   HEVC_VLD alone is 1624 of 13487 in the 12-AU session) -> RWVC_WIN_VDEC_ALL is mandatory for HEVC.
4. **The "+0xe8 polled forever" blocker (opus-dec1)** is the LAT VLD init-fetch handshake: util `vdec_virt_hw_wfe` raw
   event 6 polls `VLD(58)` bit 0 up to 0x100000 times (util 0x2fd3c-0x2fd58), retried per slice. After that, four more
   things were missing before a picture came out (section 3): HEVC start-code search status, the LAT/core "done" (IRQ +
   finish flag), the last-LCU position check, and a driver bug in `dec1/vdec_ref.c` (START data[0]/data[1] swapped).
5. **Host result**: with `fakevdec_hevc/` the vendor library completes every HEVC frame decode and returns all pictures
   in display order (320x240: decode order 0..11 displayed as 0,3,2,1,5,4,8,7,6,11,10,9 = ffprobe; 1080p: 0,2,1,5,4,3);
   no vendor error log line; no pixels (fake hardware).

## 1. Call structure and the unit split

| What | Where | Tag |
|---|---|---|
| HEVC decode entry | `h265_dec_decode` -> `Vdec_Drv_H265_decodeEx` (drv 0x979e4) -> `H265_decode` (0xa3cf4); core worker thread `CoreTh265` = `Vdec_Drv_H265_decodeCore` (0xa1104) | V names, V-run thread name |
| LAT core index | 3 (`H265_HAL_*` pass the core byte from `inst+0x43284`, e.g. `bH265_VDecWaitVldFetchOk` 0xe29d8) | V |
| Slice header source | software on this path: the LAT HEVC_VLD reads seen are the start-code/NAL-header handling below, not `ue/se` per element [V-run: no reads of HEVC_VLD(38)/(39) in the trace] | V-run |
| Register access | util `vdec_hal_read/write_*` -> direct load/store into the 0x40000 mmap (no virtual-hw object installed) | V-run |

## 2. Register protocol per frame (order as in the trace; LAT first, then core)

### 2.1 LAT: power/clock, larb, init

`SOC_GCON 0x0f018 |= 0x111`, `0x0f218 |= 0x111`, `0x0f0f8`, `LAT_MISC(61)` (0x100f4), `LAT_MISC(59) = 1` (0x100ec), then the
waits of 2.4 (event 7, then event 5 = LAT_MISC 0x110..0x13c all 0), `RACING 0x04080 = 1`, **larb5 SLP_CON 0x1600d00c bit0 = 1,
poll bit16** (util `vdec_hal_wait_larb_sleep_protect` 0x292e8), LAT_VLD(66) = 0x101/0, LAT_MISC 0xc8/0xcc/0x178/0x84
(`vdec_hal_h265_lat_init_vdec_hw` drv 0xe8dbc), larb5 bit0 = 0, `RACING 0x04080 = 0`. [V-run sequence; V function names]

### 2.2 LAT: barrel shifter init (`vdec_hal_h265_init_barrel_shifter_impl` drv 0xe90dc + util `vdec_hal_init_barrel_shift`)

1. `HEVC_VLD(33) = 1` (0x15084) [V 0xe9130]; `VLD(59)` bit 28 [V-run]; `VLD(45) = vfifo_start >> 6`, `VLD(46) = vfifo_end >> 6`
   (0x110b4/0x110b8); event 7 + event 5; `VLD(44) = rptr` (0x110b0); `VLD(66) = 0x100, 0` (0x11108); `VLD(35) = 0x100000`
   (0x1108c, start init fetch); event 7 + 5; **event 6 = poll `VLD(58)` (0x110e8) bit 0 = 1**; `VLD(35) = 0x800000`. [V-run]
2. **The hardware latches the read pointer 16-byte aligned; the vendor then skips `rptr & 15` bytes itself**: loop of
   `read HEVC_VLD(33)` + `read HEVC_VLD(8)` (consume one byte), where HEVC_VLD(33) bits 24..27 flag emulation-prevention
   bytes the reader dropped in the current word (counted, 0xe9156-0xe91a4). [V] `vdec_hal_h265_init_auto_fetch`
   (0xe9296) writes `rptr & ~0xf` to VLD(44) and `(rptr << 3) & 0x78` to VLD_TOP(7) for the same purpose. [V]
3. `vfifo_end` = AU end (+slack), from START data[0] (see 3.4). [V-run: 0x110b8 = 0x036a408e for a 4481-byte AU at 0xda901000]

### 2.3 LAT: picture start + slice (`H265_HAL_GetStartCode_PicStart` drv 0xd88d4)

| step | registers | tag |
|---|---|---|
| peek | `HEVC_VLD(0)` (0x15000) = next 32 bits, no advance | V (`vdec_hal_h265_get_bit_stream_shift` 0xe8e54 reads 0 then consumes) |
| if peek != 0x000001xx: search | `HEVC_VLD(33) |= 0x10` (WriteHEVCVLD 0x84 mask 0x10, 0xd8a2a), `HEVC_VLD(37) = 0x100` (0xd8a3e), **poll `HEVC_VLD(37)` bit 8 = 0, <= 0x20000 reads** (0xd8b3a-0xd8b62). The window then starts at the `00 00 01` | V regs; I position (only reading consistent with the NAL type extraction below) |
| NAL header | consume 8 (+8 if the peek was `00000001`), then `get_bit_stream_shift(32)`: nal_unit_type = bits 9..14 of the 32-bit peek (`ubfx r11, r0, #9, #6` 0xd8b94) | V |
| read n | `HEVC_VLD(n)`, n = 1..32: consume n bits, return the **next** 32-bit window; `vdec_hal_h265_get_bits` (0xe8c48) = read(32) per full word + read(n & 31) | V call shape; I return value (consistent with every vendor use) |
| ue / se | `HEVC_VLD(38)` / `(39)` (0xe8ec6 / 0xe8ed0) | V (DEC03 vendor-tested) |
| bit count | `HEVC_VLD(33)` bits 8..13 (`vdec_hal_h265_shift_bits` 0xe8c80 `ubfx 8,6`); bits 8..10 = trailing-bit position | V |
| next picture search | `vdec_hal_h265_(lat_)get_start_code_pic_start` (0xe8f60 / 0xe900c): search, consume 8, peek, consume 32; loop while `nal_type >> 1 <= 10` (VCL) and the bit after the header (first_slice_segment_in_pic_flag) is 0 | V |
| slice/picture registers | ~110 writes: LAT HEVC_VLD 0x150a0..0x151ec (SPS/PPS/slice fields, scaling/ref lists), LAT_VLD_TOP 0x11868 = `(h_ctb-1) << 16 | (w_ctb-1)`, 0x11870 = `height << 16 | width`, 0x11894 = CTB counts, UBE/trans buffer 0x11a30/0x11aac/0x11b98, RACING 0x04054/0x0405c/0x04060, LAT_MISC 0x81c/0x844/0x850 | V-run values (meanings of 0x11868/0x11870 [I] from 320x240 -> 0x00030004 / 0x00f00140) |
| **LAT trigger** | `HEVC_VLD(36)` (0x15090) bit 0 = 1 (`vdec_hal_h265_dec_start` 0xea5e8, single-core form) | V |
| done | WAITISR(1) -> kernel IRQ SPI 425; vendor then reads `SOC_GCON 0x200`, `LAT_MISC 0x878`, **`HEVC_VLD(75)` (0x1512c) = last LCU `x | y << 16`, must equal VLD_TOP(26)** (else "[LAT][Err] Decoding Error ... at LCU (x,y)=(0/4, 0/3)"), `HEVC_VLD(57/58/56)` 0x150e4/0x150e8/0x150e0 (error message / mask), LAT_VLD 0x120..0x12c, LAT_MISC 0x854, `HEVC_VLD(72)` 0x15120 | V (`get_mbx_mby` 0xea646 reg 0x4b, `get_err_msg` 0xea66c reg 0x39), V-run reads, vendor log text |
| finish flag | `vdec_hal_h265_wait_lat_done` (0xea5fc): `LAT_MISC(41)` bit 16 (util `vdec_hal_read_finish_flag` 0x2ab9e), then `clr_isr` (util 0x29be6): `MISC(41) |= 0x10`, `&= ~0x10` | V |
| LAT read pointer | LAT_VLD 0xf4/0xec/0xfc/0xd0/0xb4/0xb8 after the slice (rptr arithmetic of VDEC_REGISTERS §2.1) | V-run |
| wrap reset | util `vdec_hal_reset_wrap_top_1` (0x29148): **poll `WRAP_TOP(1)` (0x16002004) bits 5..0 != 0, <= 202 reads; 0x20 = idle -> write `WRAP_TOP(7)` (0x1600201c) = 1,1,1,0** | V |

### 2.4 Poll conditions (raw mode, util `vdec_virt_hw_wfe` 0x2fc84; same words for LAT at 0x11000/0x10000/0x15000)

| event | condition the vendor polls | tag |
|---|---|---|
| 5 all_dram_and_bits_proc | event 7, then MISC 0x110..0x13c each 0 | V (VDEC_REGISTERS §2.3), V-run |
| 6 init_fetch_rdy | VLD(58) bit 0 = 1, <= 0x100000 reads (0x2fd3c) | V |
| 7 process_flag | VLD(61) bit 15 = 0 (or bit 0 per the kernel text) | V-run (bit 15 = 0 suffices) |
| 8 search_start_code_done | HEVC_VLD(37) bit 8 = 0, after writing HEVC_VLD(37) = 0x100 (util 0x3020c: `write_hevc_vld(0x25, 0x100)` + wfe 8) | V |
| 9 / 10 ref reorder / wp table | HEVC_VLD(37) bit 4 / bit 0 = 0 after writing that bit (util 0x30274 / 0x3027c) | V |
| HW break before clock-off | MISC(64) bit 0 = 1, poll MISC(65) & 0x11 = 0x11 (0x25100/0x25104), then VLD(66) = 1, 0 | V-run (= kernel `mtk_vdec_hw_break`) |
| larb sleep protect | larb5 / larb4 +0x00c bit 0 -> bit 16 | V |

### 2.5 Core

Same power/larb4 (0x1602e00c)/init pattern on the core blocks; `VLD_TOP(26)` 0x20868 etc. copied from the LAT; core<->LAT
buffer addresses `VLD 0xa30..0xa40` (`vdec_hal_h265_set_core_lat_buf_addr` 0xea830); MC (78-142 writes: reference
pictures, grows with the number of references), MV (35-100), PP 0x24040/0x24630/0x24b80/0x24cac, HEVC_VLD 0x28088 = 0x101, 0;
**core trigger `HEVC_VLD(36)` (0x28090) bit 0**; WAITISR(0) -> SPI 424; `vdec_hal_h265_wait_core_done` (0xea61c):
`MISC(41)` bit 16 (single-core) or `SOC_GCON(8)` bit 16 (multi-core form, `MCORE_TOP(25)` bit 0 trigger, 0xea5c0) [V];
status reads `VDEC_SYS 0x2f000`, `HEVC_VLD(75)` 0x2812c, `(58)/(57)/(56)`, VLD 0x120..0x12c, MISC 0x17c..0x198, `HEVC_VLD(72)`;
`HEVC_VLD(158)` bit 31 = picture complete (`vdec_hal_h265_dec_pic_complete` 0xea65e) [V]; HW break; wrap reset.

## 3. Why the earlier host runs failed, and what `fakevdec_hevc/` models (each item verified by the run going further)

1. LAT `VLD(58)` fetch-ok after `VLD(35)` bit 20 (the +0xe8 poll) and the larb SLP_CON ack. Plain fake: ~1M reads per
   attempt and "VLD Fetch Fail VdecId 3", "Wait Larb Sleep Protect fail", "init BS fail" [V-run, vendor log].
2. 16-byte read-pointer latch (2.2): without it the vendor's skip loop consumes the start code. [V-run]
3. Start-code search `HEVC_VLD(37)` bit 8 (2.3): without it 131072 polls per slice. [V-run]
4. LAT/core done: `HEVC_VLD(36)` bit 0 -> finish flag `(LAT_)MISC(41)` bit 16 + IRQ (`RWVC_IRQ_VDEC_LAT` / `_VDEC`), and the
   last-LCU word `HEVC_VLD(75)` = VLD_TOP(26). Without the latter: "[LAT][Err] Decoding Error 0x00000000 at LCU (x,y)=(0/4,
   0/3)" and the vendor runs two VP-mode (copy) jobs per frame (VLD_TOP(65) = 0x101, polls "VLD_TOP_79", ISR timeout).
5. `WRAP_TOP(1)` = 0x20 (idle): otherwise 202 reads per frame. Same poll exists for H.264 (DEC02: "BASE+0x2004").
6. **Driver bug in `dec1/vdec_ref.c`**: START data[0] must be `bs->size` = `planes[0].bytesused` (the AU length) and
   data[1] `bs->length` (buffer size) (K `vdec_common_if.c`:267-268, `mtk_vcodec_dec.c`:670-671); vdec_ref.c sends
   `{buffer size, AU length}`. vpud then logs "flush size 1048581 (1048576+5) out of range" and sets the vfifo end at the
   buffer end; H.264 survives, HEVC searches the next picture start in the stale bytes after the AU, decodes old slices
   again and answers `VDEC_NEED_MORE_OUTPUT_BUF` (changed bit 4). Fixed in `vdec_hevc_ref.c`; the H.264 reference, DEC01
   and DEC05 must swap too. [V kernel lines, V-run]
7. `VDEC_NEED_MORE_OUTPUT_BUF`: the kernel keeps the source buffer and decodes it again with the next capture buffer
   (`mtk_vcodec_dec.c`:710-790); vdec_hevc_ref.c implements that (0 occurrences once item 6 is fixed).

Not modelled (not needed for completion; [I] what real hardware adds): pixels, the UBE/trans buffer contents, error
status other than "none", HEVC_VLD(37) bits 4/0 semantics beyond "done at once", multi-slice and tiles/WPP streams,
10-bit, the multi-core (`MCORE_TOP`) form beyond its done bit.

## 4. Kernel/phone needs for HEVC decode beyond H.264 decode

| # | Need | Detail | Tag |
|---|---|---|---|
| 1 | **RWVC_WIN_VDEC_ALL (rev 3)** | HEVC pages: 0x00000, 0x02000 (WRAP_TOP 0x004 R, 0x01c W), 0x04000, 0x0d000 (+0x00c only), 0x0f000, 0x10000, 0x11000 **incl. 0x11400-0x11fff** (LAT_VLD_TOP 0x11800.., UBE 0x11a30/0x11aac/0x11b98), **0x15000 (LAT HEVC_VLD, 1624 ops/12 AUs)**, 0x16000 (0x16cac only), 0x20000 (+0x800), 0x21000, 0x23000, 0x24000, 0x25000, **0x28000** (inside the frozen MISC window), 0x2e000 (+0x00c only), 0x2f000. HEVC uses neither 0x12000 nor 0x13000 (AVC only). All pages < 0x20000 VDE, >= 0x20000 VDE2 (DEC07 rule holds). DEC02's H.264 list + 0x15000 covers HEVC | V-run |
| 2 | Power / clocks | Same as H.264: VDE + VDE2, LAT and core used for every frame | V-run |
| 3 | Interrupts | Same lines: SPI 425 (LAT) then SPI 424 (core) per frame; status/ack `(LAT_)MISC+0xa4` bit 16 / 0x11, as K02 implements | V-run, V (util 0x2ab9e, 0x29be6) |
| 4 | GCE decode events | **Not needed for HEVC video** (section 0 item 1). Needed only if HEIF decode is wanted: V40's `gce_run_list` maps every VDEC event to an IRQ wait (`gce_event_irq`); events 5-10 (and LAT 21-26) are register conditions, not interrupts, so a HEIF list would time out at its first start-code or fetch wait. Patch proposal: `fakevdec_hevc/v40-gce-vdec-events.patch` (polls the conditions of 2.4; applies to n2vcu.c c70a4bca; `N2TEST V40 PASS 1536/1536` with it). Event 1 (decode done) stays an IRQ wait | V (V40 code), I (HEIF uses GCE) |
| 5 | DMA pool | 1080p HEVC session peak 68.4 MB through the back end (vendor working buffers + 4 x 1 MiB bitstream + 12 x 3.1 MB output buffers of the test driver); 320x240: 22.3 MB. Same budget question as H.264 (86 MiB RWVC pool shared with VENC); 4K does not fit | V-run |
| 6 | Register round trips | ~425 R / ~470 W per frame; same order as H.264 (DEC02 490/574); write combining (DEC04) applies unchanged | V-run |
| 7 | Driver side | data[0] = AU length (3.6); handle VDEC_NEED_MORE_OUTPUT_BUF by re-sending the same AU (3.7); HEVC AU splitting as in `vdec_hevc_ref.c` (`au_len_hevc`) | V |

## 5. Files

- `gpu/hwcodec/fakevdec_hevc/n2vdec_hevc_fake.{h,c}` -- the model (n2vc_hw wrapper, MIT, no vendor code).
- `gpu/hwcodec/fakevdec_hevc/vdec_hevc_ref.c` -- AP-side HEVC reference driver (from `dec1/vdec_ref.c`, data[0]/[1] fix,
  NEED_MORE_OUTPUT_BUF, display-order oracle, model IRQs instead of `VR_FAKEIRQ`).
- `gpu/hwcodec/fakevdec_hevc/t_fakevdec_hevc.c` -- model unit test (hand-encoded H.265 bytes), ASan/UBSan.
- `gpu/hwcodec/fakevdec_hevc/run_hevc_ref.sh` -- builds a /tmp copy of the tree (stages VDEC_ALL in n2vcu.c and the V34
  layout property, as DEC02/dec1 do), runs the unit test and both streams inside the project CPU slice.
- `gpu/hwcodec/fakevdec_hevc/v40-gce-vdec-events.patch` -- proposal for V40 (section 4 item 4).
