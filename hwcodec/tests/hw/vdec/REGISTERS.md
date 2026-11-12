# VDEC VLD / AVC_VLD / HEVC_VLD registers as the vendor library uses them (DEC03)

Scope: the bitstream reader `VLD` at `0x16020000`, the H.264 syntax engine `AVC_VLD` at `0x16022000`,
the HEVC syntax engine `HEVC_VLD` at `0x16028000`, the LAT unit at `0x16011000`/`0x16012000`, and the
few MISC/larb words the per-slice path polls. Model: `tests/hw/vdec/n2vld.c`. Every row is marked
**[V]** (read from the vendor code and exercised by a host test with a literal expected value) or
**[I]** (inferred; the reason is stated). Nothing here ran on the phone.

Files are read at run time from `gpu/build/cam-vendor-20261109/vfs` (`VFS` below); they are never
copied into the tree. No vendor code or disassembly listing is reproduced here: facts are symbol
names, addresses, register numbers/offsets and constants.

Commands used (host only):

```sh
llvm-nm -D --defined-only $VFS/lib/libvcodecdrv.so
llvm-objdump -d --triple=thumbv7-linux-gnueabihf --disassemble-symbols=<name> $VFS/lib/libvcodecdrv.so
llvm-objdump -d --triple=thumbv7-linux-gnueabihf --disassemble-symbols=<name> $VFS/lib/libvcodec_utility.so
bash gpu/hwcodec/tests/hw/vdec/run_tests.sh
```

## 0. What is on the live path (a correction to the first DEC03 draft)

- **SPS, PPS and the slice header are parsed in software.** `parseSPS` @0x10d690, `parsePPS`
  @0x10ebf4 and `parseSliceHeader` use `initRBSPBitStream`/`RBSPGetBits`/`RBSPGetUE`.
  `parseSliceHeaderHW` @0x110eb8, the `h264_comhal_*` table @0x163b68, `h264ParseCallbackInit`
  @0x10bcd0 and `vdec_util_h264_parse_syntax*` have **no caller and no relocation** in this build
  (opus-dec2, `facts/VDEC_REGISTERS.md` §0.1; opus-dec1's header-only trace makes zero VDEC register
  accesses). They are dead code. **[V]**
- **The header-only START oracle therefore does not exercise this model.** It exercises DEC01/DEC05's
  software SPS/PPS path and the `vsi` layout. The correction matters: a passing oracle is not
  evidence that the VLD is right, and a missing VLD is not the cause of a header failure.
- **The per-slice path does use the hardware reader.** `H264_HAL_InitH264BarrelShift` @0x1001a0 (the
  exported `H264_HAL_InitH264BarrelShifter` is at 0xffd18; both write the same VLD registers) ->
  `vdec_hal_h264_initbarrelshifter` @0x104dec -> util `vdec_hal_init_barrel_shift` @0x29514 /
  `vdec_hal_vld_init_barrel_shift` @0x29460 programs the vfifo, read pointer and fetch handshake;
  `H264_HAL_GetStartCode_8530` @0xfd5fc searches the next `00 00 01`; `H264_HAL_UeCodeNum` @0xfd218
  reads `first_mb_in_slice` of the next slice; `H264_HAL_Read_VldRPtr` -> `vdec_hal_read_vld_rptr`
  @0x2a7a8 reads the position back. **[V]**
- **`AVC_VLD reg n` (1..32) shifts n bits; the returned word is modelled as the next 32-bit
  window.** `H264_HAL_ShiftGetBitStream` @0xfd186 and `H264_HAL_AVCVLDGetBitS` @0xfd174 mask the n
  argument and tail-call `vdec_hal_read_avc_vld(hw, core, n)`; `H264_HAL_GetBitStreamShift` @0xfd198
  returns the reg-0 peek before the shift; `H264_HAL_GetRealBitStream` @0xfd1c2 returns
  `peek >> (32-n)`. `vdec_hal_h264_trailingbits` @0x105244 tail-calls `read_avc_vld(8 - pos)` and
  returns whatever the hardware returns. **The "shift n" is [V]; the returned word being the
  post-shift 32-bit window is [I]**: the thunks forward it, but no live caller uses that value, so
  the vendor code proves the calling convention and the task specification, not the hardware
  window. The model implements the required post-shift window; `t_vendor.c` checks that the thunks
  forward it unchanged.
- **The LAT unit is separate.** Vendor core index 3 selects `LAT_VLD` `0x11000` and `LAT_AVC_VLD`
  `0x12000` (`vdec_drv_init_hw_info` @0x1e1b8 slot table; opus-dec2 §1). Which context
  `handleSLICENALU` uses is **[I]** (likely LAT when enabled), so the model keeps both units
  independent and tests both.

## 1. Block and window map

| Window | Base | Size | Model use | Mark |
|---|---|---|---|---|
| `RWVC_WIN_VDEC_VLD` (4) | `0x16020000` | `0x1000` | core VLD; only the first `0x400` bytes are claimed | [V] |
| `RWVC_WIN_VDEC_MISC` (7) | `0x16025000` | `0x4000` | core MISC wait words, HEVC_VLD @+0x3000 | [V] |
| `RWVC_WIN_VDEC_LAT_MISC` (8) | `0x16010000` | `0x1000` | LAT MISC wait words | [V] |
| `RWVC_WIN_VDEC_LAT_VLD` (9) | `0x16011000` | `0x400` | LAT VLD | [V] |
| `RWVC_WIN_VDEC_ALL` (12, DEC07 rev 3) | `0x16000000` | `0x30000` | all of the above by absolute offset | [V] |

`n2vld_create()` installs these five entries; `n2vld_map_window()` overrides or adds (max six).
The hook is called by the fake **after** it has validated the window and powered the block; a
powered-off access returns `-EACCES` before the model sees it.

## 2. VLD sub-block (core `0x20000`, LAT `0x11000`)

| Reg | Byte off | Access | Meaning | Evidence | Mark |
|---|---|---|---|---|---|
| 0x2c | 0xb0 | W/R | read pointer, byte PA; latched by 0x42 bit 8 or 0x23 bit 23 | `vdec_hal_vld_init_barrel_shift` @0x29460 writes `bs->rptr` to 0x2c, then 0x42 = 0x100, 0; `vdec_hal_init_fetch_done` @0x29388 writes 0x23 = 0x800000 after 0x100000 | [V] use; [I] "byte PA" vs hardware unit |
| 0x2d | 0xb4 | W/R | vfifo start `>> 6` | `vdec_hal_set_vld_vfifo` @0x28bf4 | [V] |
| 0x2e | 0xb8 | W/R | vfifo end `>> 6` (ring end) | same | [V] |
| 0x23 | 0x8c | W | bit 20 (`0x100000`) starts the init fetch; bit 23 (`0x800000`) loads the barrel shifter | `vdec_hal_init_fetch_done` @0x29388 | [V] |
| 0x42 | 0x108 | W | written 0x100 then 0; bit 8 latches the reader at 0x2c | `vdec_hal_vld_init_barrel_shift` @0x2948a-0x294a8 | [V] |
| 0x3a | 0xe8 | R | bit 0: init fetch complete; `bVDecWaitVldFetchOk` polls it, <= 4096 reads. The model also sets it when the reader latches (the LAT trace writes 0x42 = 0x101 and then polls without an explicit 0x8c write) | `bVDecWaitVldFetchOk` @0xffcb8; `vdec_hal_init_fetch_done`; DEC02 LAT trace `0x11108`/`0x110e8` | [V] poll; [I] latch implies ready |
| 0x3d | 0xf4 | R | bit 15 "busy flag valid"; the vendor returns at once when it is clear | `vdec_virt_hw_wfe_process_flag` @0x30328 | [V] use; [I] meaning |
| 0x3b | 0xec | R/W | `[7:0]` sram rd index, `[15:8]` sram wr index, `[25:24]` word index; read back with bits 0..15 and 24..25 masked | `vdec_hal_read_vld_rptr` @0x2a7a8; `vdec_hal_init_barrel_shift` sets bit 28 | [V] use; [I] names |
| 0x34 | 0xd0 | R/W | `[23:16]` sram level correction; read back with 16..23 masked | `vdec_hal_read_vld_rptr` | [V] use; [I] unit |
| 0x3f | 0xfc | R | DRAM fetch pointer; model returns `start + ((abit/32*4 + 0x1c) % ring)` so the vendor formula below lands on `start + abit/8` | `vdec_hal_read_vld_rptr`; our `t_vld.c` vendor-formula test | [V] formula; [I] hardware unit |
| 0x50 | 0x140 | W(mask) | bit 20 for dual-pipe/field mode | `vdec_hal_init_barrel_shift` @0x29522-0x29542 | [V] use; [I] meaning |
| 0x33 | 0xcc | W | 0 when `hw+0x40` bit 0 | `vdec_hal_vld_init_barrel_shift` @0x294b6 | [V] use; [I] meaning |

The model reads the ring lazily through the `n2vld_bus` callback (`read_pa`) and caches it per unit;
a start/end write or a latch drops the cache, so a ring refilled between slices is re-read. A ring
larger than 8 MiB or a bus error makes the model return zeros and counts `bus_errors`; it does not
abort. The six vendor DMA rings seen so far are <= 5 MiB.

## 3. AVC_VLD sub-block (core `0x22000`, LAT `0x12000`)

| Reg | Byte off | Access | Meaning | Evidence | Mark |
|---|---|---|---|---|---|
| 0x00 | 0x000 | R | next 32 bits, MSB first, no advance | every `GetBitStream`/peek; `H264_HAL_GetBitStreamShift` @0xfd198 | [V] |
| 0x01..0x20 | 0x004..0x080 | R | shift n bits, return the **next** 32-bit window | `H264_HAL_ShiftGetBitStream` @0xfd186, `H264_HAL_AVCVLDGetBitS` @0xfd174; `vdec_hal_h264_trailingbits` forwards it | [V] shift; [I] returned window (no live caller uses the value) |
| 0x21 | 0x084 | W/R | bits 16..21: bit position inside the barrel; bit 31 reads 0 | `vdec_hal_h264_vldshiftbits` @0x105268, `vdec_hal_h264_vldrptr` @0x1050f0 | [V] |
| 0x8f | 0x23c | R | `ue(v)`, consumes the code | `vdec_hal_h264_uecodenum` @0x1051d4, `H264_HAL_UeCodeNum` @0xfd218 | [V] |
| 0x90 | 0x240 | R | `se(v)` | `vdec_hal_h264_secodenum` @0x10520c, `H264_HAL_SeCodeNum` @0xfd226 | [V] |
| 0xa3 | 0x28c | W | written 1 then 0 by the init sequence. The model stores it and does **not** move the reader position; the position is latched by VLD 0x2c/0x42/0x8c, and the LAT trace writes 0xa3 = 0 after the latch | `vdec_hal_h264_initbarrelshifter` @0x104ec0-0x104ed4; DEC02 LAT trace step 106 (`0x1228c = 0`) | [V] sequence; [I] no position change |
| 0x8e | 0x238 | W | decode trigger; the fake keeps the register/IRQ, the model moves the reader to the next start code | `vdec_hal_h264_trigdec` @0x108a7e | [V] use; [I] "consumed the slice" |
| 0xb6 | 0x2d8 | W/R | write 1: search the next `00 00 01`; bit 0 busy (model never busy) | `H264_HAL_GetStartCode_8530` @0xfd5fc | [V] |
| 0xb2 | 0x2c8 | W/R | start-code status; init sets bits 0-1; bit 11 = accept after a timeout | same | [V] use; [I] bit meaning |

Everything else in the 4 KiB AVC page (SPS/PPS/slice-header parameter and error/status words written
by `vdec_hal_h264_setspsavld` @0x105276 and friends) is left to the fake's register array. That is
deliberate: the model answers only the bit-reader commands.

## 4. HEVC_VLD `0x28000` (inside the core MISC window)

`vdec_drv_init_hw_info` @0x1e1b8 writes `0x28000` to `hw+0x160`; `vdec_hal_read_hevc_vld` @0x2c7dc
addresses `hw + 4*core + 0x160`. `H265_parseSPS`/`H265_parsePPS` are software, but
`H265_parseSliceHeader` @0xccfe0 calls `H265_HAL_UeCodeNum` @0xd854a, `SeCodeNum` @0xd855c,
`GetRealBitStream` @0xd8520 and `WriteHEVCVLD`.

| Reg | Byte off | Access | Meaning | Evidence | Mark |
|---|---|---|---|---|---|
| 0x00 | 0x000 | R | next 32 bits, no advance | `vdec_hal_h265_get_real_bit_stream` @0xe8e7a | [V] |
| 0x01..0x20 | 0x004..0x080 | R | shift n bits, return the next 32-bit window | `vdec_hal_h265_get_bits` @0xe8c48; our vendor test | [V] |
| 0x21 | 0x084 | R | bits 8..10: bit position inside the barrel | `vdec_hal_h265_trailing_bits` @0xe955c | [V] |
| 0x26 | 0x098 | R | `ue(v)` | `vdec_hal_h265_ue_code_num` @0xe8ec6 | [V] |
| 0x27 | 0x09c | R | `se(v)` | `vdec_hal_h265_se_code_num` @0xe8ed0 | [V] |

The HEVC engine shares the unit's bit position and ring with AVC: the vendor reads a slice with the
same barrel shifter after the syntax engine selection. The model stores HEVC writes in a separate
shadow array so the AVC and HEVC parameter words cannot alias.

## 5. LAT, MISC waits and larb

- **LAT VLD/AVC** (`0x11000`/`0x12000`) are a second independent unit with the same VLD and AVC
  semantics. The DEC02 trace writes `LAT_VLD 0x11108 = 0x101` (latch) and `0x11140 = 0x100000`,
  `0x110ec = 0x10000000`, `0x110b4/0x110b8/0x110b0` (vfifo/rptr) but no `0x1108c`; the model therefore
  treats a latched reader as fetch-ready so a later `+0xe8` poll does not depend on an explicit
  `0x23 = 0x100000` write. **[I]** `t_vld.c::test_lat_unit` programs both units and checks that their bit positions do not
  interfere. **[V]** for the register use (opus-dec2 §1); **[I]** for "the slice header uses LAT on
  this stream" (we model both, so either is safe).
- **MISC wait words**: the vendor polls every core MISC register `0x110..0x13c` and expects 0
  (`vdec_virt_hw_wfe_all_dram_and_bits_proc` @0x3036c). The model claims those reads and returns 0
  for both core MISC and LAT MISC. **[V]** read pattern; **[I]** exact hardware meaning.
- **larb sleep protect**: writes larb4 `0x1602e00c` / larb5 `0x1600d00c` bit 0, then polls bit 16
  (`vdec_hal_wait_larb_sleep_protect` @0x292e8). The model mirrors bit 0 into bit 16. **[V]** use;
  **[I]** the ack timing (the real SMI may need a delay).
- **Not modelled:** `VLD_TOP 0x20800`, `AVC_MV 0x23000`, `MC 0x21000`, `PP 0x24000`, the power/UFO
  words, the error/finish status words after a decode, and the VLD_TOP/MC/AVC_MV writes in
  `vdec_hal_h264_initbarrelshifter`. They stay with the fake's register array. The model therefore
  cannot make a real decode complete; it only makes the reader part behave.

## 6. Bit reader, emulation prevention and the ring

- Bits are MSB-first. The reader position is a raw bit offset from `vfifo_start`; the ring is
  `[start, end)` and reads wrap modulo the ring size. `t_vld.c::test_ring_wrap_and_rptr` places data
  across the end and checks both the peek and the vendor rptr arithmetic. **[V]** behavior; **[I]**
  the real block's exact fetch granularity.
- Emulation-prevention bytes are removed on the fly: a `0x03` at a byte boundary is skipped when the
  two previous bytes are `00 00` and the following byte is `<= 03` (H.264 7.4.1). The context starts
  at the latch position (or at the start-code found by the search), so a `00 00 03` that belongs to
  the previous NAL is not misread. `t_vld.c::test_epb` checks `00 00 03 00..03` removed,
  `00 00 03 04` kept, a trailing `00 00 03` before the next start code removed, and the bit-level
  peek/consume across an EPB. **[V]** for all tested byte patterns; **[I]** for a `00 00 03` that is
  the last byte of the whole ring (no following byte, not removed).
- End of a direct test buffer reads as zero and still advances; `ue(v)` returns `N2VLD_ERR`
  (`0xffffffff`) after 32 leading zeros or at the end. The vendor never asks in that state:
  `vdec_hal_h264_uecodenum` first reads AVC 0 and returns `-1` when the word is zero. **[V]**

## 7. The vendor read-pointer formula

`vdec_hal_read_vld_rptr` @0x2a7a8 (transcribed arithmetic):

```
d  = rd < wr ? rd - wr : rd - wr - r34[23:16]
rp = r3f + 4*r3b[25:24] + 16*d - 0x1c + (AVC 0x21[21:16] / 8)
if rp < start*64: rp += (end - start)*64
if rp > end*64:   rp -= (end - start)*64
```

The model returns `r3b` with bits 0..15 and 24..25 masked, `r34` with 16..23 masked, and
`r3f = start + ((abit/32*4 + 0x1c) % ring)`, so `rp == start + abit/8`. `t_vld.c` runs the formula
both at an ordinary position and across the ring end; see `test_ring_wrap_and_rptr`. **[V]** for the
formula; **[I]** for the hardware units of `r3b`/`r34`.

## 8. Tests and the vendor differential

```
$ bash gpu/hwcodec/tests/hw/vdec/run_tests.sh
== build t_vld (san)
== run t_vld (san)
N2VLD checks 927 pass 927
== build t_vendor (san)
== run t_vendor (san)
N2VLDV checks 496 pass 496
== build t_vld (o2)
== run t_vld (o2)
N2VLD checks 927 pass 927
== build t_vendor (o2)
== run t_vendor (o2)
N2VLDV checks 496 pass 496
DEC03 arm64 iOS compile: n2vld.c ok
N2TEST DEC03 PASS 2847/2847
```

`t_vld.c` checks (literal values from the specifications, a separate reference reader for the 600-op
differential):

| Test | Literal / source |
|---|---|
| peek/advance | `12 34 56 78 9a`: peek `0x12345678`; read(8) -> next window `0x3456789a`; read(16) -> `0x789a0000`; read(32) at bit 24 -> `0`; `u(4)=2`, `u(12)=0x468` |
| ue | 20 codes for `0,1,2,3,4,5,6,7,8,9,15,16,31,32,63,64,100,255,256,1000`; 180 bits, bytes `a6 42 98 e2 04 8a 08 04 41 00 21 02 00 10 40 ca 01 00 00 80 80 3e 90` |
| se | 11 codes for `0,1,-1,2,-2,3,-3,127,-128,1000,-1000`; 101 bits, bytes `a6 42 98 e0 3f 80 20 20 07 d0 00 3e 88` |
| SPS | first NAL of a libx264 320x240 Main stream: `00 00 01 67 4d 40 0d da ...`; profile 77, constraints 64, level 13, ue `0,0,2`, ue `1`, gap 0, ue `19`, ue `14`, frame_mbs_only 1 -> 320x240 |
| EPB | `00 00 03 00..03` removed, `00 00 03 04` kept, trailing `00 00 03` before `00 00 01` removed; bit-level `00 00 03 00 01 ff` -> peek `0x00000001`, next window `0xff000000` |
| fetch/latch | VLD 0x23 bit 20 sets fetch-ok; bit 23 and VLD 0x42 bit 8 latch 0x2c; 0x3b/0x34 readback masks; 0x3f formula |
| ring | `start = pa`, `end = pa+0x400`, rptr `pa+0x3fe`, data `12 34 56 78` across the end; peek `0x12345678`, rptr `pa+1`, vendor formula agrees |
| start code | `00 00 01 67 aa bb 00 00 01 68 cc 80`: search finds the second start code, peek `0x00000168`, then `0xcc`, `0x80`; trigger also moves to it |
| LAT | separate unit state, `LAT_AVC` at `0x12000`, independent bit positions |
| HEVC | the same 20 ue / 11 se arrays through HEVC regs 0x26/0x27; `read(8)` -> `0x3456789a`; 0x21 bits 8..10 |
| misc/larb | MISC/LAT_MISC 0x110..0x13c read 0; larb4/5 bit 0 -> bit 16 |
| differential | 600 operations over 512 deterministic bytes against a separate reference reader; `read(n)` compares the post-shift window |

`t_vendor.c` loads the phone's `libvcodecdrv.so` under the A32 interpreter and supplies
`vdec_hal_read_avc_vld`/`vdec_hal_read_hevc_vld` from the model. It also finds the first slice NAL
(type 5 or 1) of each of the three streams, consumes the NAL header with the vendor's
`vdec_hal_h264_getrealshtbitstream`, and reads `first_mb_in_slice` with `vdec_hal_h264_uecodenum`
(every first slice starts at MB 0). It calls the vendor's own
`vdec_hal_h264_uecodenum`, `secodenum`, `getrealshtbitstream`, `getshtbitstream`, `getbitstreamflg`,
`trailingbits`, `vldshiftbits`, `shift_startcode`, `H264_HAL_UeCodeNum`, `SeCodeNum`,
`GetRealBitStream`, `GetBitStreamShift`, `ShiftGetBitStream`, `AVCVLDGetBitS`, `GetBitStreamFlg`,
the `vdec_hal_h265_*` readers, and the software `parseSPS` + `vdec_com_h264_getinfofromsps` on the
three real streams. For `ShiftGetBitStream`/`AVCVLDGetBitS`/`trailingbits` the vendor thunk returns
our `read(n)` value unchanged; the test's literals are the post-shift windows the model is required
to produce, so this checks the calling convention and our model, not the real hardware window. For
the `ue`/`se`/peek/`getrealshtbitstream` functions the vendor computes the result itself from the
model's peek and shift, so those are independent checks of the model's bit engine. The differential
does **not** prove the real MT6893 register bits behave identically, and it does not prove the live
path uses LAT rather than core.

## 9. The header-only START oracle (corrected)

`oracle_ref.sh` drives opus-dec1's reference AP driver (`dec1/vdec_ref.c`) on the fake, first START
with `index = 0xFF` and no frame buffer. It reports **coded** sizes in `vsi.pic.pic_*`/`buf_*`; the
display rectangle after SPS cropping is `vsi.crop = {left, top, width, height}`:

| Stream (ffprobe display) | `pic` | `buf` | `crop` | `dpb_sz` |
|---|---|---|---|---|
| 320x240 | 320x240 | 320x256 | 0,0,320,240 | 7 |
| 1920x1080 (coded 1088) | 1920x1088 | 1920x1088 | 0,0,1920,1080 | 7 |
| 1920x808 (coded 816) | 1920x816 | 1920x832 | 0,0,1920,808 | 7 |

So the task text "`pic_h` equals ffprobe for a cropped stream" is false: `pic_h` is the coded height.
The corrected oracle is `vsi.crop[2..3] == ffprobe width/height`, `pic`/`buf` as above, and
`changed & 1` (RES_CHANGE) set. `bash tests/hw/vdec/oracle_ref.sh` -> `N2TEST DEC03-oracle PASS
12/12`. `vsi.dec.dpb_sz` has no ffprobe equivalent; it depends on the SPS level/reorder info.
This test exercises the software SPS path, not this model.

## 10. Open questions / not modelled

1. **T02 hook not landed.** `tests/hw/n2vc_hw_fake.c` has no `n2vc_hw_fake_set_reg_hook`; the suite
   drives the exact hook signature through `t_vld.c`'s bridge. Integration is one setter plus a
   call before the fake's switch (and a hook read inside the POLL loop). Proposed text is on the
   board to `hw-t02`.
2. **LAT vs core.** Which hal context `handleSLICENALU` uses is `[I]`. The model has both units, but
   the fake's power model must power the right domain (VDE for LAT, VDE2 for core).
3. **Decode completion.** The model does not produce a done interrupt, error/finish status, MC/MV
   output or pixels. A decode START can still stall after the reader part.
4. **Exact hardware units** of VLD `0x3b`/`0x34`/`0x3f`, the `0x50`/`0x33` bits and the MISC wait
   words are `[I]`; only the vendor's use and the formula are verified.
5. **`read(n)` return** is the post-shift 32-bit window because the vendor thunk returns it; the
   live callers mostly ignore it. The exact bit alignment inside the hardware barrel is `[I]`.
6. **EPB across a ring wrap** and a trailing `00 00 03` with no following byte are not fully
   specified; the model uses the H.264 rule and the tests in §8.
7. **No phone, no publish.** Every statement here is host-only; the real block could differ.
