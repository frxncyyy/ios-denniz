# Phone experiments for the hybrid JPEG decoder (J04)

The main session runs these; **no worker touches the phone**. Everything here follows the proven 0298 pattern
(`gpu/hwcodec/kernel/b/bsd/dev/nord2_hwcodec.inc`): the VEN domain and bus protection are the encoder's verified sequence,
every wait is bounded, only this block's registers are written, and the buffers live in the existing claim. The decoder adds
three things the encoder never touched: the jpgdec clock gates (0x17000000 bits 12/16, 0x17800000 bit 12), larb7 ports 11/12
(larb8 11/12 for core 2), and the three decoder windows.

**Preconditions / abort rules**

- One flash of a kernel image that contains the experiment command; the image must be built and verified by the main
  session, and the phone must be recovered to a known-good iOS boot image if it stops answering (the usual q10ik image).
- Do not boot stock Android over the iOS `userdata`.
- Never write another block's registers, the display SMI common (0x1411f000) or the encoder's larb7 ports 7-10.
- Keep every buffer below 4 GiB and inside 0xD8000000..0xD9000000.
- Power off in reverse order after every experiment; leave the VEN domain as found.
- Run E0 and E1 first. E2 is the first write to a decoder register; if the phone dies there, the next boot is the recovery
  image and the log line name says which step died.

## Address map used below

All addresses are physical and inside the 0298 claim (source 8 MiB, output 8 MiB):

| Use | 640x480 4:2:0 | 1920x1080 4:2:0 | Size |
|---|---|---|---|
| JPEG bitstream | 0xD8000000 | 0xD8000000 | file bytes, 16-aligned |
| Y | 0xD8800000 | 0xD8800000 | stride_y × comp_h (`jdec/n2jdec_tool info` prints it) |
| Cb | 0xD8850000 | 0xD8A00000 | stride_uv × comp_h |
| Cr | 0xD8864000 | 0xD8A80000 | same |
| index tables | 0xD8F00000 | 0xD8F00000 | **3 × index_size**, 128-aligned, zeroed |
| second source | 0xD8B00000 | 0xD8B00000 | (E5 resume test) |

`jdec/n2jdec_tool words FILE --src-pa ... --out-y ... --out-c ... --index-pa ... --index-size 16384` prints the 21 words
for a JPEG; `info` prints the strides and plane sizes. Build it on the PC (it is host C, no phone).

## E0 — read-only state (no decoder register is read unless its gate is already on)

| Read | Where | Expected |
|---|---|---|
| SPM status, `VEN_PWR_CON`, DIS status | 0x10006000 + 0x16C/0x170/0x344/0x350 | VEN may be off; DIS must be on (otherwise stop) |
| bus protection status | infracfg 0x2EC/0xDD8 | any value; record |
| `CLK_CFG_13` | topckgen 0xE0 | the phone read 0x0c030601 before and after M1 |
| vencsys CG status | 0x17000000 | bits 12/16 tell whether jpgdec is gated (expected gated) |
| venc_c1 CG status | 0x17800000 | bit 12 expected gated |
| larb7/larb8 status, OSTDL 11/12, NON_SEC_CON 11/12 | 0x17010000, 0x17810000 | record; **do not write** |

If bits 12/16 happen to be on, additionally read the three decoder windows' 0x090, 0x0FC, 0x14C..0x154, 0x17C, 0x200,
0x20C, 0x210, 0x224, 0x23C, 0x24C, 0x270, 0x274, 0x278, 0x31C, 0x330, 0x334, 0x338, 0x33C, 0x340, 0x344, 0x348, 0x36C,
0x370, 0x374 and log them. **This settles the reset defaults of the unknowns without any write** (the values are whatever
the previous owner left, but on iOS nothing has ever run the decoder, so they are true power-on values).

## E1 — power + gates, then read the decoder's defaults (writes only the proven power registers)

Repeat 0298 steps 1-2 (VEN on, `venc_sel` left as found, gates 0x17000004 |= bits 12/16, larb7 ports 11/12 non-secure;
for core 2 also 0x17800004 |= bit 12 and larb8 11/12). Then, without touching a decoder register, read all offsets of E0 on
cores 0, 1 and 2. Then power off.

Expected: all reads complete; the three cores read the same values; 0x090 bit 4 = 1 if the block comes out of reset in the
hard-reset state. **Settles**: the power-on values of 0x31C/0x330/0x334/0x338/0x33C/0x340/0x344/0x348/0x36C/0x370/0x374;
whether 0x31C..0x374 exist at all on this IP; whether the three windows are identical. A bus hang here would mean the
gates/power sequence is wrong, not the word hypotheses; stop and report the step.

## E2 — the go/no-go: the open 2015 decoder's full frame-mode sequence on core 0

Use the **1920x1080 JPEG produced by the M1 encoder** (`runs/q10hw1/hw.jpg`, known valid) or a 640x480 one. Copy it to
0xD8000000. Write **exactly** the supplementary set of `REGISTERS.md` §6 (the `n2jdec_supplementary()` array), with:

- 0x090: 0x00, then 0x10;
- both banks 0x140/0x144/0x148 and 0x14C/0x150/0x154 = Y/Cb/Cr addresses (covers the bank ambiguity);
- 0x0F8 = `uv_brz_w<<8`; 0x158/0x15C/0x160/0x164 = `stride_y/stride_uv`;
- 0x16C = `dma` word; 0x170 = total_mcu; 0x17C = 0; 0x200 = 0xD8000000; 0x20C/0x210/0x224/0x23C/0x270 as printed;
- 0x248 = src + align16(file size); 0x24C = align128(file size); 0x320 = 0 (DHT present);
- 0x31C/0x330/0x334/0x338/0x33C/0x344/0x36C/0x370/0x374 = 0 (the hybrid-only registers unused);
- clear 0x274 with 0x37, then 0x240 = 0.

Poll 0x274 up to 100 ms (bounded; the encoder poll was 10 µs steps). Log 0x274, 0x278, 0x340, 0x348 on timeout.

Expected outcome per hypothesis:

- **H-frame (best case):** 0x274 bit 0 (EOF) within a few ms; 0x340 ends at total_mcu+1; the Y/Cb/Cr planes decode to the
  same picture as `djpeg` on the PC (PSNR ≥ 30 dB on all planes, the M1 checker can do this). **This proves hardware decode
  with no vendor library and no unknown words; the remaining work is the driver, not reverse engineering.**
- **H-noEOF:** 0x274 = 0 after 100 ms: the hybrid needs a mode/extra word; continue with E3/E4.
- **H-error:** 0x274 has 0x02/0x04/0x20: record which; underflow means the bitstream size/address is wrong, overflow means
  the output geometry is wrong, ERROR_BS means the block cannot parse the stream (then the extra huffman/index words are
  required).
- **H-hang:** no answer to the read after the trigger. Recovery as above; do not repeat E2 without changing one variable.

E2 is the **single most valuable write experiment**: it needs 1 decoder-register write cycle and settles whether the open
sequence is sufficient.

## E3 — the 21-word hybrid sequence with our best-guess unknowns

Only if E2 does not produce EOF. Write exactly what `jpeg_drv_hybrid_dec_start()` writes: `data[0..19]` from
`n2jdec_tool words` (0x31C=0, 0x330=stride_y, 0x334=stride_uv, 0x33C=0, 0x344=total_mcu-1, 0x17C=0, 0x240=0), plus
0x338=0x36C=index, 0x370=index+size, 0x374=index+2*size, with the index area at 0xD8F00000, **3 × 16 KiB zeroed**. Also
write the 2015 strides 0x158/0x15C (some vendors write both), and both output banks. Poll as E2.

Expected: EOF + correct pixels if our hypotheses are right; otherwise the status bits name the failing stage (underflow =
address/size, overflow = geometry/stride, ERROR_BS = huffman/index). **Settles**: whether the 21-word path works at all.

## E4 — one unknown at a time (only if E3 fails)

Change one word per run, log 0x274/0x278/0x340/0x348, and stop at the first EOF with correct pixels.

| Run | Change | Expected if the candidate is right |
|---|---|---|
| E4a | 0x17C = 1, then 2, then 3 | EOF only for the correct mode; 0 will fail if a row mode is required |
| E4b | 0x330/0x334 = (width,height) instead of strides | EOF + pixels only if these are dimensions |
| E4c | 0x330/0x334 = 0,0 | EOF if the block derives strides itself |
| E4d | 0x31C = `uv_brz_w<<8` (BRZ) | 4:4:4 input decodes correctly; 4:2:0 unchanged |
| E4e | 0x31C = a 4 KiB zeroed table address (huffman/working) | EOF only if a table is required; a wrong address gives ERROR_BS/underflow |
| E4f | 0x31C = `src+align16(size)` (FILE_BRP) | EOF only if the end pointer moved there |
| E4g | 0x33C/0x344 = (0,total_mcu) and (0,0) | 0x340/0x348 progress tells whether they are MCU bounds |
| E4h | index pointers = 0,0,0,0 | EOF if the index tables are optional for frame mode |
| E4i | 0x090 = 0x00/0x01 instead of 0x00/0x10 | EOF if the hybrid wants the soft-reset form |

Each run is one bounded write cycle. Record the full register dump before and after; a run that changes 0x278's state
bits but not 0x274 is informative (the block is running and stuck).

## E5 — progress and resume semantics (settles 0x33C/0x344/0x340/0x348)

After a successful E2/E3, read 0x340/0x348 immediately after the trigger and every 100 µs until EOF. Expected: 0x340
counts 1..(total_mcu+1), 0x348 = 0 while running and 1 (EOF) at the end. Then try a **resume**: set 0x33C = k, 0x344 =
total_mcu-1, point Y/Cb/Cr at the k-th MCU row and trigger. Expected if 0x33C is a start index: the output contains rows
k.. and 0x340 starts at k+1. If it instead decodes the whole frame, 0x33C is not a start index (candidate: PAUSE_MCU_NUM).
The `GET_PROGRESS_STATUS` ioctl (`0xC0087814` on 32-bit) can be polled without waiting; the WAIT ioctl returns the same word.

## E6 — the other two cores (only after core 0 decodes)

Repeat E2 on 0x17050000 (core 1) and 0x17840000 (core 2). Core 2 needs `0x17800000` bit 12 and larb8 ports 11/12; core 1
uses the same larb7 as core 0 but its own gate bit 16. Expected: identical output. If core 2 differs, the index-table layout
(0x338/0x36C/0x370/0x374) is per-core and E3's 3×size allocation is the right guess.

## Is tracing Android possible? No.

The phone boots iOS; there is no Android kernel, no `vpud`, no `libjpeg-alpha` process to trace, and no binder/HIDL. The
vendor partition is intact but not executing. Options, best first:

1. **Host capture of the exact words (no phone).** `VFS/lib/libjpeg-alpha_vendor.so` and
   `VFS/lib/libjpeg-alpha-oal_vendor.so` are 32-bit ARM (ELF32, ARM, Thumb-2) and the OAL imports only libc/liblog
   (`malloc/free/memcpy/read/syscall/...`). The vendor library computes the 21 words itself and hands them to the OAL, whose
   exported entry points are `jpeg_oal_hybrid_lock`, `jpeg_oal_hybrid_wait`, `jpeg_oal_hybrid_get_prog_n_stat` (plus
   `jpeg_oal_binding_core`, `jpeg_oal_ion_malloc/flush/invalidate/free`). Running the pair under the A32 interpreter
   (track V's loader/core) with a fake `syscall`/`read` shim captures the ioctl argument: the request value for the 32-bit
   `struct JPEG_DEC_DRV_HYBRID_TASK` (96 bytes) is **0xC0607812**, HYBRID_WAIT is 0xC0087813, GET_PROGRESS is 0xC0087814
   (computed from `DH`:689-694 and `sizeof`), magic `'x'`, nr 18/19/20. The OAL imports `syscall` and `read`, not `ioctl`,
   so the trace should log every `syscall` number and argument; [I] the OAL uses `syscall(__NR_ioctl, ...)`. The OAL's
   `jpeg_oal_hybrid_lock` must carry the task because the kernel's HYBRID_START is the lock+start ioctl; [I] the vendor
   library may reach it through the `jpeg_queryOAL` function table rather than a direct call. This path gives all 21 words
   **and** the index-buffer size for any JPEG with **zero phone experiments**. It needs the 32-bit libs, the OAL's function
   table from `jpeg_queryOAL`, and a fake device (ioctl returns 0, read returns 0). Status: proposed to hw-iv/hw-lead; not
   built by J04.
2. **Host C-model.** Both libraries contain the strings `[INFO] Hybrid-JPGDEC HW C-model: disabled` (vendor) and
   `[INFO] Hybrid-JPGDEC: by property` (OAL): the vendor library has a software model of the block and selects the path by
   a property. The OAL also contains the property name **`ro.vendor.jpeg_decode_sw_opt`** (printable string; value/meaning
   [I]), so a fake `__system_property_get` can return each candidate value and the same fake OAL captures the words. This is
   a variant of (1), not a separate phone experiment.
3. **Static reading of the two library functions** (not done here): `jpeg_oal_hybrid_lock` in the OAL and its caller in
   the vendor library; every `data[]` store is visible in the disassembly. It is definitive but slower and stays inside the
   "facts only, no copied code" rule.
4. **A spare Android device** would not be this SoC/IP and would not answer the register questions.

## What in `libjpeg-alpha_vendor.so` confirms which hypothesis (exported names only)

The 32-bit library has 117 readable exports (plus 148 obfuscated `_libjpeg64_*` names). Relevant readable names:

| Hypothesis | Exported name(s) that must be on that path |
|---|---|
| index tables 0x338/0x36C/0x370/0x374 are a huffman/tile index | `jpeg_create_huffman_index_ALPHA`, `jpeg_build_huffman_index_ALPHA`, `jpeg_configure_huffman_index_scan_ALPHA`, `jpeg_destroy_huffman_index_ALPHA`, `jpeg_start_tile_decompress_ALPHA`, `jpeg_read_tile_scanline_ALPHA`, `jpeg_init_read_tile_scanline_ALPHA` |
| 0x17C is a tile/row mode, not frame mode | the tile entry points above vs `jpeg_start_decompress_ALPHA`; `jpeg_calc_output_dimensions_ALPHA` for the tile geometry |
| 0x31C is a huffman/working-table address | `jpeg_utils_ion_malloc`/`jpeg_utils_ion_flush`/`jpeg_utils_ion_invalidate`/`jpeg_utils_ion_free` (the "Huffman Data ION" buffer), `jpeg_dump_reg` (dumps the registers the library programmed) |
| 0x200 is a physical/ION address and must be 16-aligned | `jpeg_mem_pa_src_ALPHA`, `jpeg_mem_src_ALPHA`, plus the string `[HYBRID] jpeg_hy_set_src buffer size is not alignment (%d)` |
| 0x340/0x348 progress and the trigger/wait sequence | OAL: `jpeg_oal_hybrid_get_prog_n_stat`, `jpeg_oal_hybrid_wait`, `jpeg_oal_hybrid_lock`; vendor strings `[HYBRID] Read progress n status failed`, `[Hybrid] Wait IRQ ...` |
| 0x330/0x334 are strides (or dimensions) | `jpeg_read_tile_scanline_ALPHA` / `jpeg_calc_output_dimensions_ALPHA` expose the output geometry; `jpeg_dump_reg` prints what was programmed |
| the exact library release | `version_tag`, `jpeg_get_oal_version` (`libjpeg-alpha release on W2118.2`) |

These are names and log strings only; no code was copied. A host run with a fake OAL (option 1) turns every one of them
into a measurement.

## Verdict in experiment counts

- **If E2 works:** hardware decode is feasible with **1 write experiment** (plus E1 read) and no vendor library; the four
  unknown registers stay unused.
- **If only E3 works:** feasible with **2 write experiments**; the unknowns are confirmed by E3's success.
- **If E3 fails:** each remaining hypothesis costs one E4 run (9 rows, some with several values: ~12 write cycles), or **zero
  phone experiments** if the host capture (option 1) succeeds first. Without either, hardware decode is **not** feasible
  from the open sources alone; the closed word computation is then required.
