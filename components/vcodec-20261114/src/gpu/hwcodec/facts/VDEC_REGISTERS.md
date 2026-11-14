# VDEC registers used by the vendor H.264 decoder on MT6893 (bitstream reader, init, status), cost per frame, kernel needs

Owner: opus-dec2. Written 2026-11-11, host only; nothing here has run on the phone. Static reading of the Nord 2's own
`VFS/lib/libvcodecdrv.so` (= `L/drv`) and `VFS/lib/libvcodec_utility.so` (= `L/util`), both 32-bit Thumb. Method: `llvm-objdump -d
--triple=thumbv7a`, Thumb long-branch thunks (`movw/movt r12; add r12, pc; bx r12`) resolved to their PLT names, data tables
resolved with `llvm-readelf -r` (Android packed relocations). The scripts live in the opus-dec2 scratch dir only (no disassembly
in the tree). Only symbol names, addresses and constants are taken from the vendor objects.

Tags: **[V]** read directly from the code at the address given; **[I]** inferred (the reason is given).
Register numbers: the vendor passes a register *index*; the byte offset is `4 * index` from the sub-block base
(`vdec_hal_read_avc_vld` @util 0x2b894: `addr = hw->map_base(+0x24) + hw->tbl[module][core](+0xc4 + 4*core) + 4*reg`) [V].

## 0. Summary

1. **Header parsing is software, not the VLD.** [V] The live decode path is `eVDecDrvDecode` -> `gH264_DRV_IF` (drv 0x163980:
   `h264_dec_init`, `h264_dec_decode`, ...) -> `Vdec_Drv_H264_decode` @0xf65b4 -> `H264_decode` @0xfa574, which dispatches each
   NAL through handlers that `Vdec_Drv_H264_open` stores in the instance (0xf4700-0xf475c): `inst+0x08` = 0xf5038 (SPS:
   `initSPS`/`parseSPS`), `inst+0x10` = 0xf51e4 (PPS: `parsePPS`), `inst+0x20` = 0xf537c (SEI: `initRBSPBitStream`/`RBSPGetBits`),
   `inst+0x18` = `handleSLICENALU` @0xf8d44 (`parseSliceHeader`). `parseSPS`/`parsePPS`/`parseSliceHeader`/`parseSliceHeaderInfo`
   read the bytes from memory through `RBSPGetUE`/`RBSPGetBits` (98 call sites). The register-based readers (`h264_comhal_*` table
   at drv 0x163b68, `parseSliceHeaderHW` @0x110eb8, `vdec_util_h264_parse_syntax[Ex]`, `h264ParseCallbackInit` @0x10bcd0,
   `vdec_psr_h264_*`) have **no caller and no relocation** pointing at them in any vendor library = dead code in this build.
   This corrects `VDEC_PROTOCOL.md` §0 item 5.
2. **Consequence for the PC (DEC03):** a header-only START (SPS/PPS -> `vsi.pic`, `dpb_sz`) needs **no** register model at all;
   `Vdec_Drv_H264_open` touches no register (`H264_HAL_SetRegBase` only fetches the hal handle) [V]. If the DEC03 oracle fails,
   the cause is elsewhere (DEC01 input path, vsi layout), not a missing barrel shifter.
3. **Where the VLD *is* used** [V]: per slice in `handleSLICENALU` and the `vdec_com/hal_h264_*` frame path: bitstream FIFO + read
   pointer + init-fetch handshake (`H264_HAL_InitH264BarrelShift` @0x1001a0 -> `vdec_hal_h264_initbarrelshifter` @0x104dec ->
   util `vdec_hal_init_barrel_shift` @0x29514), hardware start-code search (`H264_HAL_GetStartCode_8530` @0xfd5fc), one `ue(v)`
   (`H264_HAL_UeCodeNum`, `first_mb_in_slice` of the next slice, call at 0xfa374) and read-pointer readback (`H264_HAL_Read_VldRPtr`
   -> `vdec_hal_h264_readrdptr` @0x108aa4 -> util `vdec_hal_read_vld_rptr` @0x2a7a8). The slice data itself is entropy-decoded
   by the block after the trigger (AVC_VLD 0x238).
4. **Direct MMIO, not GCE, for H.264 decode** [V]: the "virtual hw" (GCE) object is consulted per core in every
   `vdec_hal_read/write_*`, but `vdec_set_vir_hw` is called only from `vdec_com_h264_deinit_hw`, and no library imports it;
   nothing installs a GCE object for H.264. Every register access is a plain load/store into the daemon's 0x40000 mmap of
   0x16000000 (on our stack: a V40 guest-MMIO hook -> one `RWVC_REGS` op).
5. **LAT core = core index 3** [V]: `Vdec_Drv_H264_open` sets two hal contexts, `inst+0x9a8` (index 3 at `+0xaf4`/`+0x7e8`) and
   `inst+0x9b0` (index 0) (0xf4e9c-0xf4ece). Index 3 selects the LAT sub-blocks (0x11000 VLD, 0x12000 AVC_VLD, 0x10000 MISC, ...).
   Which context `handleSLICENALU` uses for the barrel-shifter calls is [I] (likely LAT when LAT is enabled): **model both**.

## 1. Sub-block map (offsets from 0x16000000; `vdec_drv_init_hw_info` @util 0x1e1b8, hw struct slot -> value) [V]

| module (`vdec_hal_read_<m>`) | slot | core 0 | slot | LAT (core 3) |
|---|---|---|---|---|
| vld | 0x088 | 0x20000 | 0x094 | 0x11000 |
| vld_top | 0x0a0 | 0x20800 | 0x0ac | 0x11800 |
| mc | 0x0b8 | 0x21000 | - | - |
| avc_vld | 0x0c4 | **0x22000** | 0x0d0 | **0x12000** |
| avc_mv | 0x0dc | 0x23000 | 0x0e8 | 0x13000 |
| pp | 0x0f4 | 0x24000 | 0x100 | 0x16000 |
| misc | 0x10c | 0x25000 | 0x118 | 0x10000 (LAT_MISC) |
| vp6 / bs2 / avs / vp8 | 0x124/0x130/0x148/0x154 | 0x26000/0x26800/0x27000/0x27800 | 0x13c (bs2) | 0x14800 |
| hevc_vld / vp9 / avs2 / av1 | 0x160/0x178/0x190/0x1c0 | 0x28000/0x28400/0x29000/0x28800 | 0x16c/0x184/0x19c/0x1cc | 0x15000/0x15400/0x17000/0x15800 |
| smi_larb | 0x1f0 | 0x2e000 (larb4) | soc_smi_larblat (+0x258) | 0x0d000 (larb5) [I: slot value not decoded] |
| global_con / mbist | 0x1fc / 0x208 | 0x2f000 / 0x2f800 | | |
| soc_mcore_top / soc_mcore_ufo_enc | 0x22c / 0x230 | 0x00000 / 0x00800 | | |
| (others: 0x3800, 0xf000, 0xf800 and the 0x1000/0x2000/0xe000/0x18800 pages of VDEC_PROTOCOL §3) | 0x234, 0x260.. | | | |

Slots are 3 words (core 0..2) and the LAT entries follow a module's core slots, so `tbl[module] + 4*3` is the LAT instance
(the indexing in `vdec_hal_read_avc_vld` with core 0..5) [V for vld/vld_top/avc_vld/avc_mv/pp/misc/bs2/hevc_vld/vp9/av1/avs2;
the mc "core 3" slot is avc_vld core 0, i.e. mc has no LAT instance].

## 2. Registers of the bitstream reader (per unit: core uses 0x20000/0x20800/0x22000/0x25000, LAT 0x11000/0x11800/0x12000/0x10000)

### 2.1 VLD sub-block

| off (idx) | R/W | vendor use (address) | meaning | tag |
|---|---|---|---|---|
| 0x0b4 (0x2d) | W, R | `vdec_hal_set_vld_vfifo` util 0x28bf4: `bs->vfifo_start >> 6`; read back in `vdec_hal_h264_vldrptr` 0x1050b4 and `read_vld_rptr` | vfifo start (PA/64) | V |
| 0x0b8 (0x2e) | W, R | same: `bs->vfifo_end >> 6` (`H264_HAL_InitH264BarrelShift` passes `(base+size+0x17f) & ~0x7f`) | vfifo end (PA/64), ring end | V |
| 0x0b0 (0x2c) | W | `vdec_hal_vld_init_barrel_shift` util 0x29460: `bs->rptr` (any alignment) | read pointer, byte PA | V |
| 0x108 (0x42) | W | same: 0x100 then 0 after writing 0x0b0 | load/reset reader at rptr | V use, I meaning |
| 0x0cc (0x33) | W | same: 0 when `hw+0x40` bit 0 | ? | V use |
| 0x08c (0x23) | W | `vdec_hal_init_fetch_done` util 0x29388: 0x100000 before the waits, 0x800000 after | start init fetch / load barrel shifter | V use, I meaning |
| 0x0e8 (0x3a) | R | raw `wfe` event 6 (`init_fetch_rdy`, util 0x2fd30): poll bit 0 == 1, <= 0x100000 reads | init fetch ready | V |
| 0x0f4 (0x3d) | R | raw event 7 (`process_flag`, util 0x30328): if bit 15 set, poll bit 0 == 1 | busy-flag valid / done | V |
| 0x0ec (0x3b) | R, W(mask) | `read_vld_rptr`: [7:0] sram read idx, [15:8] sram write idx, [25:24] word idx; init sets bit 28 (`init_barrel_shift`) | SRAM FIFO state | V use, I names |
| 0x0d0 (0x34) | R | `read_vld_rptr`: [23:16] subtracted when rd idx >= wr idx | SRAM level correction | V use |
| 0x0fc (0x3f) | R | `read_vld_rptr`: DRAM fetch pointer | byte PA fetched so far | V use |
| 0x140 (0x50) | W(mask) | `init_barrel_shift`: bit 20 when `bs[0]` (2-bitstream mode) | | V |

`read_vld_rptr` result (transcribed arithmetic, util 0x2a7a8): `d = (rd<wr) ? rd-wr : rd-wr-r34[23:16]`;
`rptr = r3f + 4*r3b[25:24] + 16*d - 0x1c + bitcnt/8` (bitcnt = AVC_VLD 0x084 [21:16]); `+ ring` if below start; callers subtract
`ring` if above end [V]. Waits before it: `wait_sram_stable` = event 7 then event 5.

### 2.2 AVC_VLD sub-block (0x22000 core / 0x12000 LAT)

| off (idx) | R/W | vendor use | meaning | tag |
|---|---|---|---|---|
| 0x000 (0) | R | every `GetBitStream`/peek (`H264_HAL_GetBitStreamShift` 0xfd198, `h264_comhal_get_bits` 0x10bbf8) | the next 32 bits, MSB first, no shift | V |
| 0x004..0x080 (n = 1..32) | R | `get_bits(n)` = read(0) then read(n), value = read(0) >> (32-n); `H264_HAL_ShiftGetBitStream` = read(n) | **shift n bits, return the next 32 bits** (after the shift) | V (shift); I (returned value is the post-shift window: name "ShiftGetBitStream", no caller uses it otherwise) |
| 0x084 (0x21) | W, R | init writes `0xa880c007` (+bits 25/26 if `pure_score`); reads: [21:16] (`vldshiftbits` 0x105268, `vldrptr`), [18:16] (`readrdptr`), bit 31 must be 0 for the "already at a start code" shortcut (`GetStartCode_8530`) | control / barrel bit count (bitcnt & 7 = bit in byte) | V use; I that [21:16] counts mod 32 |
| 0x23c (0x8f) | R | `vdec_hal_h264_uecodenum` 0x1051d4 (after read(0) != 0), `H264_HAL_UeCodeNum` 0xfd218 | ue(v), consumes the code | V |
| 0x240 (0x90) | R | `vdec_hal_h264_secodenum` 0x10520c, `H264_HAL_SeCodeNum` | se(v) | V |
| 0x2d8 (0xb6) | W, R | `GetStartCode_8530`: write 1, poll bit 0 == 0 (<= 0x100000) | search the next 00 00 01; window then starts at it | V use, I semantics |
| 0x2c8 (0xb2) | W(mask 3), R | init sets bits 0-1; after a search that timed out, bit 11 set = accept | start-code status | V use |
| 0x2d0 (0xb4) / 0x2d4 (0xb5) | W | init: 0x101 / 0x00204008 | | V |
| 0x28c (0xa3) | W | init: 1 then 0 | AVC VLD reset | V use |
| 0x238 (0x8e) | W | `vdec_hal_h264_trigdec` 0x108a7e: 1, then `wfe_decode_done` | decode trigger | V |
| 0x270, 0x280, 0x284 (0x9c, 0xa0, 0xa1) | R | `H264_HAL_GetErrMsg` | error status | V use |
| 0x274 (0x9d), 0x278 (0x9e), 0x2e0 (0xb8) | R | `ReadFinishFlag` (+ MISC 0x0a4), `DecPicComplete`, `GetMbxMby` (+ MC 0x028/0x02c) | done / position | V use |
| 0x280 (0xa0) | W | `vdec_hal_h264_init_hw`: 0xbdff then value | error-conceal mask | V |
| 0x08c (0x23), 0x098 (0x26), 0x090 (0x24) | R/W | `SetSHDRAVLD1` RMW 0x08c / 0x098; `SetPPSAVLD` 0x090 | SPS/PPS/slice-header fields for the core | V use |
| 0x0ec (0x3b) | W(mask) | bit 28 when `pure_score` | | V |

Other blocks written by `vdec_hal_h264_initbarrelshifter`: VLD_TOP 0x054 = 1, AVC_MV 0x210 = 4, MC 0x024 = 6 [V].

### 2.3 Polled conditions the fake must satisfy (raw mode, util `vdec_virt_hw_wfe` 0x2fc84)

| wait | condition | where |
|---|---|---|
| event 7 `process_flag` | VLD 0x0f4 bit 15 clear, or bit 0 set | 0x30328 |
| event 5 `all_dram_and_bits_proc` | event 7 ok and every MISC 0x110..0x13c reads 0 (each polled <= 0xea60 times) | 0x3036c |
| event 6 `init_fetch_rdy` | VLD 0x0e8 bit 0 set (VLD or BS2 by `bs[2]`) | 0x2fd30 |
| larb sleep protect (`vdec_hal_wait_larb_sleep_protect` 0x292e8, only if `chgnote_com_larb_iso_protect_1_4_0`) | write larb+0x00c bit 0 = 1 (mask), poll larb+0x00c bit 16 set (<= 50000); release writes bit 0 = 0. Core: larb4 0x2e00c; LAT (core 3/4): `soc_smi_larblat` (larb5 0x0d00c [I]) | called from `vdec_hal_init_vdec_hw` |
| start code search | AVC_VLD 0x2d8 bit 0 clear | 0xfd640 |
| decode done | `WaitISR` (VCU WAITISR -> kernel IRQ; core 0 SPI 424 / LAT SPI 425) | `H264_HAL_WaitISR` |

### 2.4 Exact init sequence for one slice (`vdec_hal_h264_initbarrelshifter` 0x104dec + util) [V]

`AVC 0x084 = 0xa880c007`; `VLD_TOP 0x054 = 1`; `AVC 0x2c8 |= 3`; `AVC 0x2d0 = 0x101`; `AVC 0x2d4 = 0x00204008`; `AVC_MV 0x210 = 4`;
`MC 0x024 = 6`; `AVC 0x28c = 1, 0`; [`AVC 0x0ec |= 1<<28` if pure_score]; then either `vdec_hal_h264_initautofetch` (if
`chgnote_com_ini_fetch_1_4_0` and `bs->f`) or `vdec_hal_init_barrel_shift`: [`VLD 0x140 |= 1<<20`] [`VLD 0x0ec |= 1<<28`]
`VLD 0x0b4 = start>>6; VLD 0x0b8 = end>>6`; wait event 7+5; `VLD 0x0b0 = rptr; VLD 0x108 = 0x100; = 0`; [`VLD 0x0cc = 0`];
`VLD 0x08c = 0x100000`; wait 7+5; wait 6; `VLD 0x08c = 0x800000`. If the init failed: skip bytes with `read(8)` until
`vldrptr >= target` (<= 0x10002 times).

`H264_HAL_GetStartCode_8530`: `w = read(0); c = AVC 0x084`; if `(w & ~0xff) == 0x100 && c >= 0` -> done; else `AVC 0x2d8 = 1`, poll
bit 0, read `AVC 0x2c8`; then `w = read(0); read(0x20)` (shift 32) and return `w` (low 5 bits = nal_unit_type) [V].

## 3. The model (for DEC03): `gpu/hwcodec/facts/vdec_vld/n2_vdec_vld.{h,c}`

Reference implementation of §2, MIT, no vendor code; `bash gpu/hwcodec/facts/vdec_vld/run_tests.sh` -> `N2TEST VDECVLD PASS 2/2`
(280 checks, ASan/UBSan -O1 and -O2; expected values hand-encoded Exp-Golomb / H.264 byte strings; the test's "driver" repeats the
vendor sequences of §2.4 and the vendor's rptr arithmetic of §2.1). Interface: `n2vld_init(m, mem_read(pa), ctx)`, then the
fake forwards every aligned 32-bit access in 0x16000000..0x1602ffff to `n2vld_access(m, off, is_write, &val)`; return 1 =
claimed. It models two independent units (core and LAT) with:

- vfifo ring `[VLD 0x0b4<<6, VLD 0x0b8<<6)`; latch of `rptr` on `VLD 0x108` bit 8 and `VLD 0x08c` bit 23; raw bit position.
- AVC 0x000 peek, 0x004..0x080 shift-then-peek, 0x23c ue, 0x240 se; emulation-prevention bytes (00 00 03) removed in these
  reads [I: needed for slice data, plausible that the shifter does it; switch `m->epb`]; start-code search scans raw bytes.
- `AVC 0x084` read = written value with [21:16] = raw bit position mod 32 and bit 31 = 0.
- `VLD 0x0fc` = `vstart + ((abit/32*4 + 0x1c) mod ring)`, `VLD 0x0ec` = written & ~0x0300ffff, `VLD 0x0d0` = written & ~0x00ff0000,
  so the vendor's own rptr formula returns exactly `vstart + abit/8` (tested, including unaligned start and ring wrap).
- `VLD 0x0e8` bit 0 after `VLD 0x08c` bit 20; `VLD 0x0f4` = 0; MISC/LAT_MISC 0x110..0x13c read 0; larb +0x00c bit 16 mirrors bit 0.
- `AVC 0x238` bit 0 (decode trigger): not claimed (the fake keeps its done/IRQ logic) but the reader moves to the next start code,
  standing in for "the block consumed the slice" [I]. No pixels, no MV/UBE output.

DEC03 integration: a hook in `tests/hw/n2vc_hw_fake.c` (T02) called before the plain register array for VDEC offsets, plus a
`mem_read` that resolves PAs through the fake DMA pool / ion share table (DEC01 knows where the bitstream lives). The window list
of the fake must first cover 0x12000/0x22000 (DEC07). What remains [I] and must be checked against DEC02's trace: the context
(core vs LAT) of the per-slice reads; whether the vendor reads AVC 0x000 after a trigger to find the next slice; the error/finish
registers it expects after "decode done" (0x270/0x274/0x278/0x280/0x284, MISC 0x0a4), which the fake must answer as "no error,
finished, mb position = last MB" for the flow to continue.

## 4. Register traffic per frame and the batching the daemon should do

Static count of `vdec_hal_*` call sites on the per-slice path (`regs.py` over drv): `vdec_hal_init_vdec_hw` ~28 writes (+ larb
poll), barrel-shifter init 10 + 13 writes / ~8 reads, `SetSPSAVLD` 9 W / 7 R (read-modify-write), `SetPPSAVLD` 2, `SetSHDRAVLD1`
2 RMW, ref lists (`vdec_hal_h264_setreflist` 40 sites, `setbrefpiclistreg` 31, `setprefpiclistreg` 13; loops over the list),
`vdec_hal_h264_decproc` 48 writes (16 MC, 7 VLD_TOP masks), error conceal 8, UFO 5, MVC 6, scaling lists 6 sites (loops: up to
~200 writes when matrices are sent), LAT path (`laedecproc` 15, `setmcorerowinfo` 8, `inittransbuff` 9, UBE ~14), status after
done: GetErrMsg 3, MbxMby 3, finish 2, rptr ~8, start-code search ~8.
**Estimate [I], 1 slice, LAT+CORE, no scaling lists: 350-550 writes and 70-130 reads per frame**, of which ~40-60 reads are
the read half of `_mask` read-modify-writes and ~10 are polls that succeed on the first read on real hardware. Multi-slice
streams add ~60-100 ops per extra slice. (DEC02 replaces this with counters.)

Without batching (one `RWVC_REGS` ioctl per access, 2-5 us each on XNU): ~1.5-3 ms per frame -> 30 fps fine, 60 fps 1080p
marginal. Plus guest cost: each `vdec_hal_*` call is 25-40 guest instructions -> ~20k instructions per frame (< 1 ms).

**Design (for DEC04 / V40, ordered by gain):**

1. **Write-combining in the V40 MMIO hook** (DEC04 as posted): aligned 32-bit stores queue into one `rwvc_reg_op[]`; flush before
   (a) any guest MMIO read, (b) any poll/wait/IRQ wait, GCE flush, power change, (c) any guest call into a host import or
   syscall (usleep, ioctl, futex, log), (d) at 256 ops. Order preserved. Expected: ~70-130 ioctls/frame (one per read).
2. **OAL interposition (HLE) of `libvcodec_utility.so` exports** - the strongest lever: every register access of `libvcodecdrv.so`
   goes through PLT imports `vdec_hal_read_<m>/write_<m>/write_<m>_mask(hw, core, reg, val[, mask])` (and util calls its own
   exports through its PLT). Bind those imports to host functions in the loader: `write` -> queue `RWVC_OP_WRITE`;
   `write_mask` -> queue `RWVC_OP_WRITE_MASK` (the kernel does the RMW: the 40-60 read halves disappear and so do their
   flushes); `read` -> flush + `RWVC_OP_READ`; the raw waits (`vdec_virt_hw_wfe_init_fetch_rdy`, `_process_flag`,
   `_all_dram_and_bits_proc`) -> one batch with `RWVC_OP_POLL` ops (kernel-bounded). Address = sub-block table of §1 + 4*reg;
   `core` 0 / 3 picks core / LAT. Also removes the 25-40 guest instructions per call. Expected: ~25-40 ioctls per frame
   (the true reads: rptr, start code, status), ~0.1-0.2 ms. Must keep `hw->0x4b0/0x4b4` semantics for any access not going
   through these exports (none found in drv: it has no direct MMIO of its own, FACTS 5.6a).
3. **Never touch side-effect registers speculatively**: AVC_VLD 0x000-0x080, 0x23c, 0x240 (core 0x22000, LAT 0x12000) consume
   bits on every read; no read-back verification, no retry, no `RWVC_OP_POLL` on them, no prefetch; exactly one 32-bit read
   per guest read, in order.
4. Fast path for the decode-done wait already exists (VCU WAITISR -> `RWVC_WAIT_IRQ`, DEC01/V40).

## 5. Kernel proposal for the NEXT image (not q10jx; VENC unchanged)

ABI rev 3 (hw-lead decides; DEC07 drafts the text) - what decode needs, from §1-§2:

- `RWVC_WIN_VDEC_ALL = 12`: base 0x16000000, size 0x30000. Per 4 KiB page: 0x00000-0x1ffff requires VDE (VDEC_SOC) on and
  unprotected; 0x20000-0x2ffff requires VDE2 (VDEC_CORE) **and** VDE. `EACCES` otherwise; POLL re-checks the domain each
  iteration.
- Larb pages: 0x0d000 (larb5) and 0x2e000 (larb4) refuse everything except offset **0x00c**: READ, and WRITE_MASK with mask = 1
  (bit 0 sleep request; the vendor polls bit 16). This is the only larb register the decoder touches [V: util 0x292e8 /
  0x2935c; the other larb configuration stays with K02].
- VDEC_SYS 0x2f000 / SOC_GCON 0x0f000: the vendor writes 0x000, 0x008, 0x018 (core gcon) and 0x000, 0x008, 0x018, 0x0a0, 0x0a8,
  0x0b0, 0x200, 0x218 (soc gcon) in `vdec_hal_init_vdec_hw` (CKEN *set* registers, value 1 / 0x111) [V]; allow them (they can
  only enable clocks K02 already enabled); deny the clear registers 0x004/0x00c unless DEC02 shows them.
- `RWVC_REG_MAX_OPS` 2048 suffices (a frame is < 700 ops). The `RWVC_OP_POLL` timeout cap (`poll_timeout_us`) must allow
  ~10 ms for start-code search on large slices.
- No new ioctl. IRQ lines `RWVC_IRQ_VDEC` / `RWVC_IRQ_VDEC_LAT` and status at MISC/LAT_MISC +0x0a4 are already in K02.
- Pages the decoder addresses (for an allow-list once DEC02 confirms): core 0x20-0x26, 0x29 (AVS2: not H.264), 0x2e (+0x00c only),
  0x2f; LAT/SOC 0x00, 0x01, 0x02, 0x03, 0x04, 0x0d (+0x00c only), 0x0e, 0x0f, 0x10-0x18.

## 6. Feasibility (2026-11-11)

- PC: header decode needs no VLD model -> reachable as soon as DEC01 fixes the input import. A full frame decode on the fake can
  run the vendor control flow (with this model + "no error / finished" status answers) but can never produce pixels; its value
  is the exact register trace (DEC02) and the op counts for §4.
- Phone: first real decode needs ABI rev 3 + the kernel window change (§5) in the next image, the LAT/CORE LOCK + WAITISR path
  (DEC01), and the DMA pool budget (VDEC_PROTOCOL §5 item 8). Register round-trip cost is not a blocker (§4: 0.1-3 ms/frame
  depending on batching).
- Open [I]: core vs LAT context of the barrel-shifter calls; EPB handling by the shifter; the meaning of AVC 0x084 bit 31 and
  0x2c8 bit 11; whether `initautofetch` (instead of `init_barrel_shift`) is taken on this chip (`chgnote_com_ini_fetch_1_4_0`).
