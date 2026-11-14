# The 21 hybrid JPEG-decoder words, offset by offset

Package J04. Written 11 November 2026 from the open sources only; **nothing here was run on the phone** except where
`gpu/hwcodec/runs/q10hw1/` is named. Verdicts: **[V]** = the register name, field layout and formula are in the tree at the
cited line; **[I]** = the hybrid driver writes this offset but its meaning/value is not documented anywhere in the tree and
is a hypothesis (with the competing candidates listed). Every word below has one of the two marks; there is no unmarked
word.

Sources (all paths relative to `/home/.../nord2-current-kernel` unless noted):

| Short | File |
|---|---|
| `DEC` | `drivers/misc/mediatek/jpeg/v2/jpeg_drv_dec.c` |
| `DRV` | `drivers/misc/mediatek/jpeg/v2/jpeg_drv.c` |
| `DH` | `drivers/misc/mediatek/jpeg/v2/jpeg_drv.h` |
| `REG` | `drivers/misc/mediatek/jpeg/v2/jpeg_drv_reg.h` |
| `COMMON` | `drivers/misc/mediatek/jpeg/v2/jpeg_drv_common.h` |
| `ION` | `drivers/misc/mediatek/jpeg/v2/jpeg_ion.c` |
| `HW` | `drivers/media/platform/mtk-jpeg/mtk_jpeg_dec_hw.c` |
| `DECREG` | `drivers/media/platform/mtk-jpeg/mtk_jpeg_dec_reg.h` |
| `CORE` | `drivers/media/platform/mtk-jpeg/mtk_jpeg_core.c` |
| `DTS` | `arch/arm64/boot/dts/mediatek/mt6893.dts` |
| `VENCLK` | `drivers/clk/mediatek/clk-mt6885-ven.c` |
| `PGCLK` | `drivers/clk/mediatek/clk-mt6885-pg.c` |
| `SMI` | `drivers/misc/mediatek/smi/mt6885/smi_conf.h` |
| `VFS` | `gpu/build/cam-vendor-20261109/vfs` (the phone's vendor partition; read only, never copied) |

## 0. The one-paragraph model

The block is `jpgdec@17040000` with two more cores at `0x17050000` and `0x17840000` (`DTS`:5788-5803; `HW_CORE_NUMBER` is 3,
`DH`:96). All three windows have the same register layout. The *open* 2015 driver (`HW`) programs one core in full-frame mode,
and its register map is the same 0x090..0x320 map used by the first half of the hybrid words. The *closed* vendor library
`libjpeg-alpha_vendor.so` computes 21 `data[]` words and the kernel's hybrid ioctl (`DEC`:30-85) writes 20 of them to
0x090..0x344 plus four addresses derived from a kernel-allocated index buffer, then triggers at 0x240. The extra registers
0x31C..0x374 exist to support a tile/pause mode with a huffman/index table; `libjpeg-alpha_vendor.so` exports the tile API and
the OAL exports `jpeg_oal_hybrid_lock/wait/get_prog_n_stat` (§5). The 0x0FC..0x270 words are the same header-derived values
the open driver computes, which is the strongest evidence that they keep their 2015 meaning; 0x31C..0x374 do not appear in
any open header.

## 1. What `jpeg_drv_hybrid_dec_start()` writes, in source order

`DEC`:30-85. `data[]` is `struct JPEG_DEC_DRV_HYBRID_TASK.data[21]` (`DH`:438-443); `data[20]` is not a register.

| # | `data[]` | Offset | `REG` macro line | Register name (`DECREG`) | Verdict |
|---|---|---|---|---|---|
| 1 | `data[0]` | 0x090 | 156 | `JPGDEC_REG_RESET` (soft) | **[I]** value 0x00 |
| 2 | `data[1]` | 0x090 | 156 | `JPGDEC_REG_RESET` (hard) | **[I]** value 0x10 |
| 3 | `data[2]` | 0x0FC | 157 | `JPGDEC_REG_DU_NUM` | **[V]** |
| 4 | `data[3]` | 0x14C | 158 | `JPGDEC_REG_DEST_ADDR1_Y` | **[V]** name; output use [I] |
| 5 | `data[4]` | 0x150 | 159 | `JPGDEC_REG_DEST_ADDR1_U` | **[V]** name; output use [I] |
| 6 | `data[5]` | 0x154 | 160 | `JPGDEC_REG_DEST_ADDR1_V` | **[V]** name; output use [I] |
| 7 | `data[6]` | 0x17C | 161 | `JPGDEC_REG_OPERATION_MODE` | **[I]** value 0 (frame mode) |
| 8 | `data[7]` | 0x200 | 162 | `JPGDEC_REG_FILE_ADDR` | **[V]** (an ION **fd**, translated) |
| 9 | `data[8]` | 0x20C | 163 | `JPGDEC_REG_COMP_ID` | **[V]** |
| 10 | `data[9]` | 0x210 | 164 | `JPGDEC_REG_TOTAL_MCU_NUM` | **[V]** |
| 11 | `data[10]` | 0x224 | 165 | `JPGDEC_REG_COMP0_DATA_UNIT_NUM` | **[V]** |
| 12 | `data[11]` | 0x23C | 166 | `JPGDEC_REG_DU_CTRL` | **[V]** |
| 13 | `data[12]` | 0x24C | 168 | `JPGDEC_REG_FILE_TOTAL_SIZE` | **[V]** formula; exact input [I] |
| 14 | `data[13]` | 0x270 | 169 | `JPGDEC_REG_QT_ID` | **[V]** |
| 15 | `data[14]` | 0x31C | 171 | *(no name in any header)* | **[I]** |
| 16 | `data[15]` | 0x330 | 172 | *(no name)* | **[I]** |
| 17 | `data[16]` | 0x334 | 173 | *(no name)* | **[I]** |
| — | *(index iova)* | 0x338 | 174 | *(no name)* | **[I]** |
| — | *(index iova)* | 0x36C | 179 | *(no name)* | **[I]** |
| — | *(index iova + data[20])* | 0x370 | 180 | *(no name)* | **[I]** |
| — | *(index iova + 2*data[20])* | 0x374 | 181 | *(no name)* | **[I]** |
| 18 | `data[17]` | 0x33C | 175 | *(no name)* | **[I]** |
| 19 | `data[18]` | 0x344 | 177 | *(no name)* | **[I]** |
| 20 | `data[19]` | 0x240 | 167 | `JPGDEC_REG_TRIG` | **[I]** value 0 |
| 21 | `data[20]` | — | — | index-buffer size in bytes | **[V]** (code fact) |

Two corrections to the task text's offset list: **0x33C and 0x344 are written by the driver** (`data[17]`, `data[18]`, `DEC`:80-81)
and are not in the 2015 map; and **0x338/0x36C/0x370/0x374 do not come from `data[]` at all** — they are the index-buffer pointers
(`DEC`:72-79). Counting writes: 20 `data[]` writes + 4 index writes = 24 register writes; 21 `data[]` values, of which
`data[20]` is a size.

### 1.1 The memory the block reads and writes

| Memory | Who allocates it | Address the block gets | Alignment / size | Evidence |
|---|---|---|---|---|
| JPEG bitstream | user space (ION) in Android; our kernel's claim on iOS | `data[7]` is an **fd**, translated to an IOVA on M4U port 236 (larb7 port 12); ours is a physical address because the IOMMU is bypassed | 16-byte aligned (`HW`:309); size in `data[12]` = multiple of 128 (`HW`:310) | `DEC`:61; `ION`:221-239 |
| Y, Cb, Cr output | user space (vendor) / our kernel claim | `data[3..5]` are raw addresses (no fd translation) written to `DEST_ADDR1_Y/U/V` 0x14C/0x150/0x154 | 16-byte aligned (`HW`:255-260); plane height padded to whole MCU rows; stride is not a register (0x330/0x334 hypothesis) | `DEC`:57-59; `DECREG`:28-30; `HW`:151-169,409-411 |
| Index / huffman table area | **the kernel**, `jpg_ion_alloc_handle(data[20], 128, 0)`, zeroed, shared back as an fd | `data[20]` size; pointers 0x338=0x36C=base, 0x370=base+size, 0x374=base+2*size | 128-byte aligned; the allocation is only `size` bytes, so allocate 3× for our driver | `DEC`:40-52,72-79; `ION`:81-100 |
| Huffman/working table ("Huffman Data ION") | the **vendor library** (not the kernel; there is no kernel allocation for it) | no confirmed register; candidate 0x31C (`data[14]`) | unknown | vendor strings, exported huffman-index/tile API; see §3 |

That is the complete memory model of `jpeg_drv_hybrid_dec_start`: one bitstream fd, three output addresses, one kernel index
buffer, and (only if the vendor's huffman path is used) a user-space "Huffman Data" buffer whose register is not in the open
tree. Our driver needs at most the first three.

## 2. The words with a [V] field layout

All formulas below are my own C (`jdec/n2jdec_words.c`) rewritten from the cited kernel lines; the parser supplies the
T.81 header fields.

### `data[2]` — 0x0FC, `JPGDEC_REG_DU_NUM` [V]

Sampling factors, four bits per component, each component's nibble is `(H-1)` in bits 3:2 and `(V-1)` in bits 1:0:
`(mask(h0)<<2|mask(v0))<<8 | (mask(h1)<<2|mask(v1))<<4 | (mask(h2)<<2|mask(v2))`, `mask(x)=((x)-1)&3`;
exactly 0 for a one-component frame (`HW`:364-378, `MTK_JPEG_DUNUM_MASK` `HW`:13). Examples: 4:2:0 → 0x500, 4:2:2 → 0x400,
4:4:4 → 0x000, 4:2:2V → 0x100, grey → 0x000.

### `data[3..5]` — 0x14C/0x150/0x154, `DEST_ADDR1_Y/U/V` [V name]

The output plane addresses, 16-byte aligned (`DECREG`:28-30; the 2015 driver checks the alignment in
`mtk_jpeg_dec_set_dst_bank0()`, `HW`:252-261). **The open driver writes bank 0 (0x140/0x144/0x148) and zeroes bank 1**
(`HW`:409-411). The hybrid writes its output addresses to bank 1. Whether the hybrid uses bank 1 as "the" output or as the
second buffer of a ping-pong pair is **[I]**; experiment E2 writes the same addresses to both banks so either reading works.

### `data[7]` — 0x200, `JPGDEC_REG_FILE_ADDR` [V]

Physical address of the JPEG bitstream, 16-byte aligned (`HW`:307-313). The hybrid arg is **not** an address: `data[7]` is an
ION **file descriptor**, turned into an IOVA by `jpg_translate_fd(data[7], 0, 236)` (`DEC`:61), i.e. mapped for M4U port 236 =
larb7 port 12 (`ION`:221-239; `235 = 7*32+11`, `236 = 7*32+12`). Our kernel has no ION: it copies the JPEG into its own buffer
and passes that physical address, with the IOMMU bypassed (§4).

### `data[8]` — 0x20C, `JPGDEC_REG_COMP_ID` [V]

`(id0&0xff)<<24 | (id1&0xff)<<16 | (id2&0xff)<<8` (`HW`:315-323). Grey: `id0<<24`, the other bytes 0.

### `data[9]` — 0x210, `JPGDEC_REG_TOTAL_MCU_NUM` [V]

`total_mcu - 1` (`HW`:325-328). T.81 count: `ceil(X/(8*Hmax)) * ceil(Y/(8*Vmax))`; examples: 17x19 4:2:0 → 2*2 = 4,
640x480 4:4:4 → 80*60 = 4800, 33x9 grey → 5*2 = 10.

### `data[10]` — 0x224, `JPGDEC_REG_COMP0_DATA_UNIT_NUM` [V]

`unit_num - 1` with `unit_num = ceil(X/8) * ceil(Y/8)` (`HW`:83,330-333). This is the number of **visible** luma data units,
not the MCU-padded count: 17x19 → 3*3 = 9 (the MCU grid stores 16), 640x480 → 4800, 33x9 → 5*2 = 10. Do not replace it with
`mcu_w*mcu_h*H0*V0`; the kernel deliberately does not.

### `data[11]` — 0x23C, `JPGDEC_REG_DU_CTRL` [V]

`membership | (gmc<<30) | (isgray<<31)`, with `gmc = 1` and `membership` built as 3 bits per block slot (`HW`:94-108,335-342):

- for each of the 10 block slots: if the slot belongs to block `b` of component `c`, the field is `4 + (c & 3)`; otherwise 7;
- grey sets `membership = 0x3FFFFFFC` first (`HW`:338-339).

Therefore: 4:2:0 with ids 1,2,3 and four Y, one Cb, one Cr block → `0x3FFF5924`, plus gmc → `0x7FFF5924`;
4:4:4 one block each → `0x3FFFFFAC`, plus gmc → `0x7FFFFFAC`; grey → `0xFFFFFFFC` (0x3FFFFFFC | gmc | isgray).
`jdec/t_jdec.c` checks exactly these literals for 17x19/640x480/33x9.

### `data[12]` — 0x24C, `JPGDEC_REG_FILE_TOTAL_SIZE` [V formula]

A multiple of 128 (`HW`:307-313 verifies the alignment). The 2015 V4L2 driver passes `align(V4L2 buffer size, 128)`, not the
JPEG length (`CORE`:1331-1334: `bs->size = mtk_jpeg_align(vb2_plane_size(...), 128)`). The vendor library's own string
`[HYBRID] jpeg_hy_set_src buffer size is not alignment (%d)` (VFS `libjpeg-alpha_vendor.so`, 32- and 64-bit) says it checks
the **source buffer size** for alignment before the start, which is direct evidence that `data[12]` is the aligned *buffer*
size, not the exact file length. **Our generator passes `align(file bytes, 128)`**, the minimum valid value if the buffer is
exactly the file; a vendor allocation rounded up to a larger aligned size would pass a larger value. This choice is **[I]**;
E2/E3 can distinguish the two only by checking that a larger aligned value still decodes.

### `data[13]` — 0x270, `JPGDEC_REG_QT_ID` [V]

`(tq0&0xf)<<8 | (tq1&0xf)<<4 | (tq2&0xf)` (`HW`:344-351). cjpeg's usual 4:2:0/4:2:2/4:4:4 files use tq 0,1,1 → 0x0011;
grey uses 0x0000. (The tables themselves are in the bitstream; the hardware reads them.)

### `data[19]` — 0x240, `JPGDEC_REG_TRIG` [I value, V register]

Written last (`DEC`:82). The 2015 driver starts the block by writing 0 to 0x240 (`HW`:218-221). The hybrid's `data[19]` is
therefore most likely 0; no open source says whether the hybrid needs a different value. Our default is 0.

### `data[20]` — index-buffer size [V code fact]

`data[20]` is the size passed to `jpg_ion_alloc_handle(data[20], 128, 0)` (`DEC`:40; `ION`:81-100), i.e. a 128-byte-aligned
kernel allocation, zeroed (`DEC`:48-51), shared back to user space as an fd (`DEC`:46), and used as the stride between three
index pointers (`DEC`:76-79). **The allocation is only `data[20]` bytes but the third pointer is `base + 2*data[20]`**: as
written, either `data[20]` is the size of one of three tables and the allocation is a vendor bug (only the first table is
inside the buffer), or the hardware only writes the first table and the other pointers are never dereferenced. Our kernel
must allocate **3 × size** (or learn the real field meaning) before it trusts 0x370/0x374. This is an observation, not a
measurement; E1's read-back of 0x370/0x374 and a host run of the vendor library (§5) settle it.

## 3. The [I] words, with the candidate meanings and why

These nine `data[]` slots (plus the four index pointers) are the unknown part. `jdec/n2jdec_words.c:n2jdec_unknown_words()`
is the only place that guesses; it returns the default value and the text below.

### `data[0]`, `data[1]` — 0x090 twice [I]

0x090 is `JPGDEC_REG_RESET` with bit 0 = soft reset and bit 4 = hard reset (`REG`:95-97). The open decoder's reset is
`0x00` then `0x01` (soft) followed by `0x00` then `0x10` (hard) (`HW`:223-240); the hybrid driver's own reset helpers write
only `0x00`/`0x10` (`DRV`:301-302, `DEC`:1699-1700, and the hybrid WAIT ioctl repeats `0x00`/`0x10` after every job). The
hybrid start's two writes are therefore most likely `0x00` then `0x10`. **Hypothesis: `data[0]=0`, `data[1]=0x10`.** E1
reads the post-reset value (bit 4 should read 1); E3 tries both 0x00/0x10 and 0x00/0x01 if it fails.

### `data[6]` — 0x17C, `JPGDEC_REG_OPERATION_MODE` [I]

Bits 1:0, written as `mode & 3` (`HW`:290-293). The open driver writes 0 = full-frame mode (`HW`:386). The hybrid has a
progress register (0x340), a status register (0x348) and index tables, which is the shape of a row/pause engine; it may need
`1` (or a vendor-specific mode) instead of 0. **Hypothesis: 0.** E4 tries 0..3 and logs 0x278/0x340/0x348 after each.

### `data[14]` — 0x31C [I]

Not in `DECREG`, not in `REG`. Three candidates, in order of preference:

1. **Huffman/working-table base.** The vendor library prints `Huffman Data ION va: %p, pa: %p, size: 0x%X` and
   `Huffman Builder binding core: failed (%s)` / `Huffman Builder run in subthread` (VFS `libjpeg-alpha_vendor.so`,
   printable strings; exported names include `jpeg_create_huffman_index_ALPHA`, `jpeg_build_huffman_index_ALPHA`,
   `jpeg_configure_huffman_index_scan_ALPHA`). A software Huffman builder that runs in a subthread needs to hand its output
   to the block somewhere, and 0x31C is the only unassigned address-sized word. The 2015 decoder's only huffman control is
   `JPGDEC_REG_ST_HUFFMAN_EN` at 0x320 (`DECREG`:49), used when the stream has no DHT (`HW`:295-299), which is a different
   function; the hybrid's builder path is a stronger reason for an address here.
2. **BRZ_FACTOR** (0x0F8 in the 2015 map, `HW`:242-250,385). The hybrid 21 words never write 0x0F8, yet 4:4:4 needs
   `uv_brz_w = 1` (`HW`:46-49) to decimate chroma to 4:2:2. If the hybrid supports 4:4:4, the factor must be somewhere; 0x31C
   is a candidate.
3. **`FILE_BRP`** (0x248 in the 2015 map, `HW`:301-305). The hybrid never writes 0x248 either; the bitstream end pointer could
   have moved to 0x31C, with 0x24C still giving the size.

**Hypothesis: 0** (unused). E4 tries 0, `uv_brz_w<<8`, `src_pa + align16(size)`, and a 4 KiB table address, and compares a
4:4:4 decode with a 4:2:0 one.

### `data[15]`, `data[16]` — 0x330, 0x334 [I]

The hybrid word list contains **no stride register**: the 2015 map's `STRIDE_Y/UV` 0x158/0x15C and `IMG_STRIDE_Y/UV`
0x160/0x164 (`DECREG`:31-34, `HW`:271-283,405-408) are absent. A DMA engine that writes Y/Cb/Cr planes must know the stride,
so these two words are almost certainly the two strides (or the width/height from which the hardware derives them). The
2015 driver's layout is: `comp_w = align((mcu_w*8*H) >> brz, 8)`, `stride_y = align(comp_w,32)`, `stride_uv = align(comp_w,16)`
(`HW`:151-169), and the plane height is `comp_h = 8*V*mcu_h`, i.e. **padded to whole MCU rows** (1088 rows for 1080).
Buffer capacities must use `stride * comp_h`, not `stride * visible height`. **Hypothesis: `data[15]=stride_y`,
`data[16]=stride_uv`.** E4 tries `(stride_y,stride_uv)`, `(width,height)`, `(0,0)` and `(stride_y,stride_uv)` with the 2015
stride registers also written.

### `data[17]`, `data[18]` — 0x33C, 0x344 [I]

They sit in the middle of a natural cluster: `0x33C`, **0x340 = progress**, `0x344`, **0x348 = status** (`DEC`:87-96). The
progress word is `(0x340 - 1) << 4 | 0x348`, i.e. 0x340 counts MCUs (1-based) and 0x348 holds a 4-bit status. The two
unassigned words are therefore most likely the first and last MCU index of the run:

- **Hypothesis: `data[17] = 0` (first MCU of this job), `data[18] = total_mcu - 1` (last MCU), i.e. decode the whole frame.**
- Alternatives: 0x33C = `PAUSE_MCU_NUM` (the 2015 0x170, `HW`:285-288,414) and 0x344 = a slice count; or both are part of a
  resume token set by the vendor's tile scheduler.
E5 decodes with `(0,total-1)`, `(0,0)`, `(1,total-1)` and reads 0x340 before/after; a resume that starts at MCU k and
produces rows k.. is the confirmation.

### Why these candidates: the 2015 registers the hybrid does not write

The hybrid list is exactly the 2015 list minus several registers plus the index cluster. That correspondence is the main
reason for the hypotheses above:

| 2015 register the hybrid does **not** write | Most likely replacement in the hybrid list |
|---|---|
| 0x0F8 `BRZ_FACTOR` (`HW`:242-250,385) | 0x31C (candidate 2) |
| 0x158/0x15C `STRIDE_Y/UV` and 0x160/0x164 `IMG_STRIDE_Y/UV` (`HW`:271-283,405-408) | 0x330/0x334 (candidate strides) |
| 0x170 `PAUSE_MCU_NUM` (`HW`:285-288,414) | 0x33C and/or 0x344 (start/end MCU) |
| 0x248 `FILE_BRP` end pointer (`HW`:301-305,390) | 0x31C (candidate 3), or nothing: 0x24C gives the size |
| 0x320 `ST_HUFFMAN_EN` (`HW`:295-299,397) | 0x31C or the index tables (0x338/0x36C/0x370/0x374) |
| 0x140/0x144/0x148 `DEST_ADDR0_Y/U/V` | 0x14C/0x150/0x154 `DEST_ADDR1_Y/U/V` (the hybrid writes bank 1) |

Nothing in the open tree assigns 0x31C, 0x330, 0x334, 0x33C or 0x344; the table is the full set of functional gaps left by
the hybrid word list, which is why the candidates above are the ones to test first in E4.

### The four index pointers 0x338/0x36C/0x370/0x374 [I]

`DEC`:72-79 writes the same 128-aligned IOVA to 0x338 and 0x36C and the +size/+2*size values to 0x370/0x374. The vendor
strings `Huffman Data ION ...`, the exported tile API (`jpeg_start_tile_decompress_ALPHA`, `jpeg_read_tile_scanline_ALPHA`,
`jpeg_create_huffman_index_ALPHA`, `jpeg_build_huffman_index_ALPHA`, `jpeg_configure_huffman_index_scan_ALPHA`,
`jpeg_init_read_tile_scanline_ALPHA`, `jpeg_destroy_huffman_index_ALPHA`) and the OAL's `jpeg_oal_hybrid_lock/wait/
get_prog_n_stat` are consistent with **a software-built huffman index that the three cores consume, one table per core or per
slice**. Whether the hardware reads the tables or writes them cannot be told from the open tree: the kernel zeroes the buffer
and triggers in the same ioctl, so user space cannot fill it before the trigger, but the fd is returned immediately after the
trigger, so a vendor thread could stream entries while the block runs. **Hypothesis: 0x338/0x36C = base of table 0,
0x370 = table 1, 0x374 = table 2; our kernel allocates 3*size and zeroes it.** E1 (defaults), E3 (zeroed tables) and E5
(progress) distinguish "required input" from "optional output".

## 4. Power, clocks, bus and IOMMU around a hybrid job

What `DRV` does (all [V]):

| Step | Register / action | Source |
|---|---|---|
| lock | pick a free core 0..2 (`HW_CORE_NUMBER` = 3), mark it, zero its software status | `DRV`:772-807, `DH`:96 |
| power (cores 0/1) | `smi_bus_prepare_enable(SMI_LARB7)`, `clk_prepare_enable(clk_venc_jpgDec)`, `clk_prepare_enable(clk_venc_jpgDec_c1)`; the pair is enabled only once, hence the `(id+1)%2` test | `DRV`:484-495 |
| power (core 2) | `smi_bus_prepare_enable(SMI_LARB8)`, `clk_prepare_enable(clk_venc_c1_jpgDec)` | `DRV`:496-502 |
| DVFS | highest VENC frequency while a job runs, 0 after | `DRV`:438-474 |
| 34-bit ports | set `Virtuality=1` for larb7/larb8 ports 11, 12, 23..26 (the decoder's DMA ports) | `DRV`:506-528 |
| IRQ | `enable_irq(hybriddecIrqId[id])`; handler reads/clears 0x274 and wakes the wait queue | `DRV`:412-436, 794 |
| start | the 24 register writes of §1, trigger 0x240 last | `DEC`:30-85 |
| wait | poll 0x274 once; else `wait_event_interruptible_timeout(..., 3000 ms)`; on timeout 0x274 stays 0 | `DRV`:1626-1688 |
| progress | `(read 0x340 - 1) << 4 | read 0x348` | `DEC`:87-96, `DRV`:1690-1697 |
| cleanup | write 0x00 then 0x10 to 0x090, unlock: power off and `disable_irq` | `DRV`:1699-1702, 809-821 |

The gates are inverted (`sta_ofs` 0x0, `set_ofs` 0x4 = **on**, `clr_ofs` 0x8 = off; `VENCLK`:19-23, 48-62, 108-115):

| Block | Gate register | Bit | Name |
|---|---|---|---|
| core 0 and 1 | 0x17000000 | 12 | `CLK_VEN1_CKE3_JPGDEC` ("ven1_cke3_jpgdec") |
| core 1 (second clock) | 0x17000000 | 16 | `CLK_VEN1_CKE4_JPGDEC_C1` ("ven1_cke4_jpgdec_c1") |
| core 2 | 0x17800000 | 12 | `CLK_VEN2_CKE3_JPGDEC` ("ven2_cke3_jpgdec") |

The VEN power domain and `venc_sel` are the ones the JPEG encoder already uses (`PLAN.md` §1.3, `PGCLK`:2646, `:1176`;
phone log `runs/q10hw1/04-step1.log`: VEN on, `CLK_CFG_13 0x0c030601`, gates `0x10000111`). The decoder adds **larb7 ports
11 and 12** for cores 0/1 (M4U ids 235/236, `DEC`:47,61) and **larb8 ports 11/12** for core 2 (`DRV`:506-528). These are not
the encoder's ports 7..10, whose OSTDL values were tuned on the phone (`runs/q10hw1/05-step2.log`); a decoder bring-up must
not touch them. The vendor sets the ports to "virtual" (34-bit); with the IOMMU bypassed (the camera's proven method,
`PLAN.md` §1.3) our driver instead clears `NON_SEC_CON(port)` bit 0 and bits 8..15 for larb7 ports 11/12 (and larb8 11/12
for core 2), and all buffers must stay below 4 GiB.

The three windows and IRQs: 0x17040000 SPI 310, 0x17050000 SPI 311, 0x17840000 SPI 315 (`DTS`:5788-5803). larb7 is
0x17010000, larb8 0x17810000, `venc_c1_gcon` 0x17800000 (`DTS`:5739-5751; larb nodes in the same file).

## 5. Completion, status and the ioctl surface

`0x274 JPGDEC_REG_INTERRUPT_STATUS` (`DECREG`:47, `REG`:133-141):

| Bit | Mask | Meaning |
|---|---|---|
| 0 | 0x01 | EOF (decode finished) |
| 1 | 0x02 | underflow (bitstream too short) |
| 2 | 0x04 | overflow (output/resource) |
| 4 | 0x10 | pause |
| 5 | 0x20 | bitstream error |
| — | 0x37 | `ALLIRQ` mask used by both drivers |
| 31 | 0x80000000 | `BIT_INQST_MASK_TYPE` in `REG`:134 (not in the 2015 header; meaning unknown) |

Reading returns the pending bits; **writing the value back clears them** (`DEC`:15-28; `HW`:191-200). `0x278` is
`JPGDEC_REG_STATUS` with state bits 26:24 (`REG`:146-154: idle, DMA, header, VLD, reset, prog, IDCT).

Ioctl surface (`DH`:689-694, `DRV`:1570-1734; `JPEG_IOCTL_MAGIC` = `'x'` `DH`:659):

| ioctl | `_IOWR('x', n, struct)` | What it does |
|---|---|---|
| HYBRID_START | 18, `JPEG_DEC_DRV_HYBRID_TASK` (timeout long, hwid ptr, index_buf_fd ptr, `data[21]`) | requires `timeout == 3000`; locks a core, powers it, runs §1, returns hwid and index fd |
| HYBRID_WAIT | 19, `JPEG_DEC_DRV_HYBRID_P_N_S` (hwid, progress ptr) | polls 0x274, waits up to 3 s on the IRQ queue, reads 0x340/0x348, writes 0x090 = 0x00/0x10, unlocks |
| HYBRID_GET_PROGRESS_STATUS | 20, same struct | reads 0x340/0x348 without waiting or unlocking |

`jpeg_drv_hybrid_dec_get_p_n_s()` computes `(0x340 - 1) << 4 | 0x348` (`DEC`:87-96). With 0x340 = 1 based MCU count, a full
frame should end at 0x340 = total_mcu + 1. The low four bits are the status; the `<<4` is why 0x348 is assumed to be 4 bits
wide.

The hybrid start does **not** clear 0x274; the lock only zeroes the software copy (`DRV`:792). Any new driver must write
0x274 = 0xFF (or the value read) before triggering, or a stale EOF from a previous job will be taken for completion.

## 6. The open 2015 decoder's complete set (supplementary words)

For experiment E2 the generator also produces the full `mtk_jpeg_dec_set_config()` set (`HW`:380-415), including the
registers the hybrid does not use. This is the closest thing to a documented "frame mode" sequence and is the first thing to
try on the hardware:

| Offset | Value | Source |
|---|---|---|
| 0x090 | 0x00 then 0x10 (hard reset; the 2015 soft-reset 0x01 is omitted, as `DRV`:301-302 also omits it) | `HW`:230-240 |
| 0x0F8 | `uv_brz_w << 8` | `HW`:242-250,385 |
| 0x140/0x144/0x148 | Y/U/V output addresses (bank 0) | `HW`:252-261,409 |
| 0x158/0x15C | `stride_y`, `stride_uv` | `HW`:271-276,405 |
| 0x160/0x164 | same strides (img stride) | `HW`:278-283,407 |
| 0x16C | `(dma_mcu-1)<<16 | (dma_group-1)<<8 | (dma_last_mcu-1)` | `HW`:353-362,412 |
| 0x170 | `total_mcu` (pause index) | `HW`:285-288,414 |
| 0x17C | 0 (frame mode) | `HW`:290-293,386 |
| 0x200 | bitstream address | `HW`:307-313,389 |
| 0x20C | comp id word | `HW`:315-323,393 |
| 0x210 | `total_mcu - 1` | `HW`:325-328,388 |
| 0x224 | `unit_num - 1` | `HW`:330-333,387 |
| 0x23C | `du_ctrl` | `HW`:335-342,391 |
| 0x240 | 0 (trigger) | `HW`:218-221 |
| 0x248 | `src + align16(file_bytes)` (end pointer) | `HW`:301-305,390; `CORE`:1331-1333 |
| 0x24C | `align(file_bytes,128)` (we pass the file length; V4L2 passes the buffer size) | `HW`:307-313; `CORE`:1332-1334 |
| 0x270 | q-table ids | `HW`:344-351,395 |
| 0x320 | 1 only when the stream has no DHT | `HW`:295-299,397 |

`dma_mcu/group/last` follow `HW`:111-135: factor 4 for 4:4:4→4:2:2, 4:2:2V→4:2:0 and grey, 2 for 422X2, else 3. For a
17x19 4:2:0 frame: dma_mcu 8, group 1, last 2 → 0x00070001.

## 7. What the 21-word generator refuses, and why

`jdec/n2jdec_words.c` returns `N2JDEC_E_UNSUPPORTED` for:

- **`MTK_JPEG_COLOR_422X2`** (`0x00412121`, Y 4x1, C 2x1, C 2x1): the open `mcu_w` formula uses `factor_w = 2 + H0`
  (`HW`:78), which gives a 64-pixel MCU for H0=4, while T.81 gives 8*Hmax = 32. Either the open formula is wrong for H=4 or
  that format is not really supported. Rather than emit a wrong `total_mcu`, the generator refuses it and the parser still
  reports the T.81 geometry.
- **any 4:1:1 / 4-component / 2-component / H0>2 stream** (the MTK table has no entry; `HW`:35-68).
- A baseline frame with H or V > 4, sum(H*V) > 10 (`HW`:blocks limit), a 16-bit DQT, a non-SOF0 frame, multiple scans,
  non-interleaved scans, or a scan that references a missing DHT.

All of these are listed in `jdec/STATUS.md`; the parser still gives their geometry where it can.

## 8. Verdict

The **first eleven `data[]` words** (0x0FC, 0x14C/0x150/0x154, 0x200, 0x20C, 0x210, 0x224, 0x23C, 0x24C, 0x270) are fully
derivable from T.81 plus `HW`, and `jdec/` implements them with hand-checked literals. The **nine remaining `data[]` words**
(0x090 ×2, 0x17C, 0x31C, 0x330, 0x334, 0x33C, 0x344, 0x240) and the **four index pointers** are hypotheses. Hardware decode is
therefore **feasible** with the open 2015 sequence in the best case (E2: one write experiment after the proven power steps), or needs
the vendor library's exact words (host capture, §5 of `PHONE_EXPERIMENTS.md`) in the worst case. `STATUS.md` carries the
honest verdict and the experiment count.
