# MT2T / MT2R (MediaTek 10-bit block mode) on MT6893: layout, P010 conversion, how to get it

Owner: opus-mt2t. Written 2026-11-12, host only: nothing here has run on the phone, and no real 10-bit VDEC frame has been
seen. **[V]** = verified (path:line, command or oracle result given), **[V-oracle]** = verified against two independent
third-party implementations used as black boxes, **[V-host]** = seen with the phone's vendor userland under our PC
interpreter on the fake VDEC, **[I]** = inferred (reason given). Short paths: `K` = `work/nord2-current-kernel`,
`KV` = `K/drivers/media/platform/mtk-vcodec`, `L` = `gpu/build/cam-vendor-20261109/vfs/lib` (vendor objects; only strings
were read). No vendor code is reproduced here. Companion of facts/MM21_LAYOUT.md (8-bit) and mm21/n2_mm21.{h,c}.

## 1. The vendor fourccs (the 19 formats the decoder lists on the PC host)

All are defined in `K/include/uapi/linux/videodev2.h`:734-776 (the comment for each is on the line after the define;
duplicate defines at :684-699). "Two non-contiguous planes" = Y and C in separate buffers (fb_dma[0]/[1]); "contiguous" =
one buffer. "C" in the name = UFO-compressed (opaque, needs MDP/HW to read). "J" = "jump mode". [V] for names/comments.

| fourcc | V4L2 name | bits | layout | planes | CPU-readable? |
|---|---|---|---|---|---|
| `MM21` | V4L2_PIX_FMT_MT21 | 8 | block (16x32 Y / 16x16 C tiles) | 2 non-contig | yes (n2_mm21) |
| `MT21` | V4L2_PIX_FMT_MT21C | 8 | block, UFO compressed (:734) | 2 | no |
| `M21S` / `M2CS` / `MACS` | MT21S / MT21CS / MT21CSA | 8 | block / compressed / compressed "au offset" | 2 contig | M21S yes, others no |
| **`MT2T`** | **V4L2_PIX_FMT_MT2110T** | **10** | **"tile block mode"** (:741-742) | 2 non-contig | **yes (this doc)** |
| **`MT2R`** | **V4L2_PIX_FMT_MT2110R** | **10** | **"raster block mode"** (:743-744) | 2 non-contig | **yes (this doc)** |
| `MTST` / `MTSR` | MT21S10T / MT21S10R | 10 | tile / raster block (:753-756) | 2 contig | yes, same tiles [I] |
| `MTCT` / `MTCR` | MT21C10T / MT21C10R | 10 | tile / raster, compressed (:745-748) | 2 non-contig | no |
| `MCST` / `MCSR` | MT21CS10T / MT21CS10R | 10 | compressed (:757-760) | 2 contig | no |
| `MJST` / `MJSR` | MT21S10TJ / MT21S10RJ | 10 | tile / raster block **jump mode** (:763-766) | 2 contig | unknown (jump = HW-internal order) [I] |
| `JCST` / `JCSR` | MT21CS10TJ / MT21CS10RJ | 10 | compressed jump mode (:767-770) | 2 contig | no |
| `YV12`, `NV12` | | 8 | linear | 1 | yes (but SET_DECODE_LAYOUT refused for H.264, DECODE_PATH.md 2.3) |

So 14 of the 19 are 10-bit; the only uncompressed, non-jump, two-buffer 10-bit ones are **MT2T and MT2R**. "T" vs "R"
does **not** mean tiled vs linear picture: both are 16x32 block (tiled) pictures with identical 8-bit MSB tiles; they differ
only in how the 2 LSBs are packed inside each 16x4 group (section 2): T = the 4 LSB pairs of one byte come from 4 *rows*
of one column, R = from 4 *adjacent pixels* of one row [V-oracle]. The vendor utility library carries the strings
`nv12_blk_10bit_v` and `nv12_blk_10bit_h` (`strings -a L/libvcodec_utility.so`) [V], matching "vertical" (T) and
"horizontal" (R) LSB packing [I]. Upstream Linux kept the same fourccs as `V4L2_PIX_FMT_MT2110T/MT2110R` (host
`/usr/include/linux/videodev2.h`:789-790 [V]).

## 2. Byte layout of MT2T / MT2R

Same tile grid as MM21 (raster order of tiles, no Z order); each tile is 5/4 the size of the MM21 tile:

- **Y tile**: 16 pixels x 32 rows = **640 B** (MM21: 512). **C tile**: 16 bytes-of-NV12 (8 Cb,Cr pairs) x 16 rows = **320 B**.
- A tile is a sequence of **16x4 groups of 80 bytes**: `[16 LSB bytes][64 MSB bytes]`; Y tile = 8 groups, C tile = 4 groups.
- The 64 MSB bytes are the 8 high bits (`v >> 2`) of the 16x4 samples, row-major (`16*row + col`), i.e. exactly MM21's
  tile rows with a 16-byte LSB block inserted before every 4th row.
- Chroma columns are NV12 order: tile column xb even = Cb, odd = Cr; each Cb/Cr sample is treated as one "pixel".

```
tile_h  = 32 (Y) or 16 (C);  tile_sz = 16 * tile_h * 5/4  (640 / 320);  tiles_x = buf_w / 16
G(x,y)  = plane + ((y/tile_h) * tiles_x + x/16) * tile_sz + ((y % tile_h) / 4) * 80      // the 80-byte group
xi = x % 16,  yi = y % 4                    // x = luma column, or chroma byte column xb (Cb even, Cr odd); y = row (chroma row for C)
MSB byte : G + 16 + 16*yi + xi                                   (holds v[9:2])
LSB  MT2T: byte G + xi,          bits [2*yi+1 : 2*yi]            (byte xi = column xi, rows 0..3, row 0 in bits 1:0)
LSB  MT2R: byte G + 4*yi + xi/4, bits [2*(xi%4)+1 : 2*(xi%4)]    (byte = 4 adjacent pixels of row yi, leftmost in bits 1:0)
v(x,y)  = (MSB << 2) | LSB2                 P010 sample = v << 6  (little-endian uint16)
plane row stride (virtual, in bytes) = buf_w * 5/4;  one tile row = tiles_x * tile_sz bytes
```

Evidence **[V-oracle]**, two independent implementations used as black boxes (no source read):
1. **GStreamer 1.28.8** `GST_VIDEO_FORMAT_MT2110T/MT2110R` unpackers (host libgstvideo): single-byte and single-bit probes
   through `appsrc ! videoconvert chroma-mode=none dither=none matrix-mode=none ! P010_10LE` at 16x32 (all 960 bytes,
   bits 0..7 of the LSB bytes) and a 48x80 tile-order probe. Results: bytes 0-15 = LSB of rows 0-3, 16-79 = MSB rows 0-3,
   80-95 LSB rows 4-7, ...; T byte 0 bit0/1 -> Y(0,0), bit2 -> Y(0,1), bit4 -> Y(0,2), bit6 -> Y(0,3); R byte 0 bit2 ->
   Y(1,0), bit4 -> Y(2,0), bit6 -> Y(3,0), byte 1 -> Y(4..7,0), byte 4 -> Y(0..3,1); R chroma byte 640 -> Cb0,Cr0,Cb1,Cr1;
   tiles raster order, tile rows padded to 32/16 rows. GStreamer's own plane sizes agree (`GstVideoInfo` 1920x1080:
   Y offset 0, C offset 2611200, size 3916800; tile stride word 0x00220078 = 34 x 120 tiles).
2. **libyuv** (host /usr/lib/libyuv.so, `MT2TToP010`, src stride = `buf_w*5/4` bytes): a C reference written from the
   formula above matches it on random data for 16x32, 64x64, 320x240, 1920x1080, 3840x2160 and 100x50: 0 mismatches in
   Y and CbCr. Note libyuv fills the low 6 bits by bit replication (`v<<6 | v>>4`); ffmpeg's p010le has them zero.
3. That the MT6893 VDEC writes exactly this layout is **[I] (high)**: same mtk-vcodec IP family, same fourcc, same
   "MTK 10-bit tile block mode" naming as the ChromeOS MT8195/MT8188 decoders that GStreamer/libyuv support was written
   for, and the vendor fb size is exactly 1.25x of MM21 (section 3). The first phone frame settles it (section 6).

## 3. Plane sizes

The kernel does not compute 10-bit sizes: the VCU (vendor userland) fills `vsi.pic.{buf_w,buf_h,fb_sz[],bitdepth,
layout_mode,fourcc}` (`KV/vcodec_ipi_msg.h`:91-112, copied verbatim by `get_pic_info`, `KV/vdec/vdec_common_if.c`:39-53)
and the kernel just reports `sizeimage[i] = fb_sz[i]`, `bytesperline[i] = buf_w` (`KV/mtk_vcodec_dec.c`:1818-1826,
2292-2298) [V]. Note `bytesperline = buf_w` is in **pixels**, not the 5/4 byte stride. Vendor 10-bit fb = 1.25x the 8-bit
fb on the PC host (facts/FORMATS.md:20, f320-hevc10) [V-host]. Formula: `fb_sz[0] = buf_w*buf_h*5/4`, `fb_sz[1] = fb_sz[0]/2` [V-oracle for
the tile arithmetic, I for the vendor's exact padding: always take buf_w/buf_h/fb_sz from the vsi].

| picture | buf (assumed) | MT2T Y | MT2T C | MT2T total | MM21 total | P010 out (Y + UV) |
|---|---|---|---|---|---|---|
| 1920x1080 | 1920x1088 | 2,611,200 | 1,305,600 | 3,916,800 | 3,133,440 | 4,147,200 + 2,073,600 = 6,220,800 |
| 3840x2160 | 3840x2176 | 10,444,800 | 5,222,400 | 15,667,200 | 12,533,760 | 16,588,800 + 8,294,400 = 24,883,200 |

(1088 and 2176 are also multiples of 64, so HEVC's 64-alignment, MM21_LAYOUT.md 2, gives the same buffers.) 4K Main10
DPB (dpb 6-8 + 2) ~ 125-157 MB of MT2T buffers: does not fit the 86 MiB RWVC pool (MM21_LAYOUT.md 5) [I].

## 4. Conversion MT2T -> P010 (kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange, 'x420' / full range 'xf20')

The P010 sample for v is the LE uint16 `v << 6`: low byte = `LSB2 << 6`, high byte = `MSB`. So a whole 16-sample row is
**one `st2` of {lo, msb}** where lo is a byte vector with the 2 LSBs in bits 7:6. Chroma is identical (the C tile is
already Cb,Cr interleaved, which is the P010 UV plane order), only tile_h = 16. Low 6 bits = 0 (matches ffmpeg p010le).

```
// one 80-byte group -> 4 output rows of 16 samples (32 bytes) at d[r] = dst + (y0+r)*pitch + 2*x0
MT2T:  L = ld1q(g);  M_r = ld1q(g + 16 + 16*r), r = 0..3
       lo0 = shl(L, 6);  lo1 = and(shl(L, 4), 0xC0);  lo2 = and(shl(L, 2), 0xC0);  lo3 = and(L, 0xC0)
       st2q(d[r], {lo_r, M_r})                                   // 4 ld1 + 1 ld1, 3 shl, 3 and, 4 st2 per 64 samples
MT2R:  L = ld1q(g);  IDX_r = {4r,4r,4r,4r, 4r+1 x4, 4r+2 x4, 4r+3 x4};  SH = {6,4,2,0} x4
       lo_r = and(ushl(tbl1q(L, IDX_r), SH), 0xC0)               // + 4 tbl, 4 ushl, 4 and per group
       st2q(d[r], {lo_r, M_r})
Plane loop: as n2_mm21 (head partial tile, full tiles 4 per iteration, tail); a partial tile at the crop edge goes through
a 32-byte-per-row bounce buffer or a scalar path; crop x/y stay even (MM21_LAYOUT.md 2).
Staged (RWVC_DMA_RW) variant: fetch whole tile rows = tiles_x*640 (Y) + tiles_x*320 (C) bytes: 1080p 76,800 + 38,400 =
115,200 B staging, 4K 230,400 B (fits A78's 512 KB L2, not A55's 128-256 KB comfortably: fetch half tile rows there).
```

Inverse for tests / the fake VDEC: `MSB = v >> 2` into the MSB slot, OR `(v & 3) << shift` into the LSB byte (formula of
section 2), with edge replication outside w x h, exactly like `n2_nv12_to_mm21`. The scalar formula in section 2 is the
reference tiler for the test suite (it is what was checked against GStreamer and libyuv).

**Cost** [I]: compute is trivial (~1.3 ALU ops per output byte pair, NEON) so like MM21 it is bandwidth/copy bound. Bytes
moved per pixel: MM21->NV12 read 1.5 + write 1.5 = 3.0; MT2T->P010 read 1.875 + write 3.0 = 4.875, i.e. **1.6x**. From the
measured 8.8 ms (4K, MM21, one big core, NEON, phone): **~13-15 ms per 4K picture single-thread, ~3.5 ms at 1080p**; if the
8.8 ms is dominated by the RWVC copyout of the source (1.25x more) the low end applies, if by IOSurface writes (2x more)
the high end. 4K60 (16.7 ms budget) therefore needs **2 threads** (split by tile rows; the functions are thread-safe by
design like n2_mm21) or a Metal kernel; 4K30 and 1080p60 fit on one core. P010 IOSurface is 2x the NV12 one, which is
the dominant extra cost, not the LSB unpacking.

## 5. Making the vendor output it

- **Who picks the fourcc**: the VCU, at header parse. The kernel passes **no capture-format request** to the VCU (the vsi has
  no field for one, `KV/vdec_ipi_msg.h`:351-372; S_FMT on CAPTURE only sends `SET_PARAM_FB_NUM_PLANES`,
  `KV/mtk_vcodec_dec.c`:1662-1664). After the header the kernel overwrites the CAPTURE format with `vsi.pic.fourcc`
  (`KV/mtk_vcodec_dec.c`:1825-1826, 2293-2294) [V]. Read `vsi.pic.fourcc` and `vsi.pic.bitdepth` (`KV/vcodec_ipi_msg.h`:109-111)
  from GET_PARAM_PIC_INFO and dispatch: `MM21` -> n2_mm21, `MT2T`/`MT2R` -> this converter, anything else -> refuse.
- **What the vendor picked**: `MT2T` for HEVC Main10 (f320-hevc10, 10/10 pictures) and VP9 profile 2, with
  `vendor.mtk.vdec.frame.layout.mode=0` [V-host] (facts/FORMATS.md:20, :23, :119). The vendor log line format is
  `VDEC_DRV_PIXEL_FORMAT_YUV_420_PLANER_MTK set forcc %d(%c%c%c%c), %dx%d, bitdepth=%d, ufo=%d` (`L/libvpud_vcodec.so`
  strings) [V]: check `bitdepth=10, ufo=0` there. MT2R is not reachable by any knob we know; upstream Linux uses MT2R for
  AV1 10-bit and MT2T for HEVC/VP9 [I, recollection of mainline mtk_vcodec_dec_stateless.c, not on disk]. Support both
  (one shared loop, only the `lo_r` computation differs); it costs ~20 lines.
- **UFO must be off**: with layout mode 1 the vendor keeps UFO for >= 0x1fe000 pixels (DECODE_PATH.md 2.3) -> for 10-bit that
  would be `MTCT`/`MTCR` (compressed, unreadable) [I by symmetry with MT21 vs MM21]. `vendor.mtk.vdec.frame.layout.mode=0`
  is already forced in `shim/n2shim_props.c` (DECODE_PATH.md:30) and is what produced MT2T on the PC [V-host].
  `SET_PARAM_UFO_MODE` is a no-op in this kernel (`KV/vdec/vdec_common_if.c`:621-622) [V]. Also check `vsi.pic.layout_mode`
  is not `VDEC_DRV_UFO_AUO_ON` (1<<1, `KV/mt6885/mtk_vcodec_dec_pm.c`:39, :1086; MT6893 builds the mt6885 directory,
  `KV/Makefile`:7-8) [V].
- **Do not** select the `J` (jump) or `S` (single-buffer) variants: nothing selects them today; jump mode is a separate
  property (`vendor.mtk.vdec.h264.jump.mode` in `L/libvcodecdrv.so` strings) [V string, I effect].
- **eFuse**: libvcodecdrv logs `u410BitBond` and `"10 bit disabled by eFuse"` [V strings]: on the phone the fake's answer is
  replaced by the real eFuse; if 10-bit is fused off the header step fails and VT must fall back to software. Check the log.
- **Protocol** (out of scope here, for DEC05): DEC_HEADER_OK / DEC_PICTURE_OK must carry fourcc (or bitdepth) and the
  buffers must be sized from `fb_sz[]`, not `stride*buf_h`; the plug-in must create 'x420'/'xf20' pixel buffers for 10-bit.

## 6. Host test plan (no phone)

1. **Streams**: `ffmpeg -f lavfi -i testsrc2=size=WxH:rate=30 -frames:v 10 -pix_fmt yuv420p10le -c:v libx265 -profile:v main10
   -x265-params bframes=0 out.h265` for 320x240, 1920x1080, 3840x2160, plus odd crop (e.g. 1918x1078) and an HDR10 one with
   `-x265-params colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:hdr10=1` (fmt/make_streams.sh:18-19 already makes
   f320-hevc10). Reference: `ffmpeg -i out.h265 -pix_fmt p010le -f rawvideo ref.p010` (HEVC is bit-exact).
2. **Converter unit tests** (mirror mm21/run_tests.sh, ASan+UBSan, C and NEON-emulated builds): ref.p010 -> inverse packer ->
   MT2T (and MT2R) with garbage padding -> converter -> must equal ref.p010 byte for byte (low 6 bits 0); exact-size source
   allocations, guard bytes, random rectangles (even x/y, odd w/h), staged == direct with exact fetch counts.
3. **Independent oracles**: (a) libyuv `MT2TToP010` on the same MT2T bytes, compare `>>6` (libyuv replicates low bits);
   (b) GStreamer `appsrc caps=video/x-raw,format=MT2110T|MT2110R ! videoconvert chroma-mode=none dither=none matrix-mode=none
   ! video/x-raw,format=P010_10LE` (exact; the probes in section 2 used this). A mutation (swap T/R LSB rule, 64-byte
   groups, MSB/LSB order) must fail both.
4. **Vendor on the fake** (fmt/run_sim_fmt.sh): decode f320-hevc10 and a 1080p Main10 clip; assert fourcc `MT2T`,
   bitdepth 10, ufo 0, `fb_sz[0] == buf_w*buf_h*5/4`, `fb_sz[1] == fb_sz[0]/2`. (The fake VDEC writes whatever the
   fake produces, so this checks sizes and plumbing, not the real HW tile layout.)
5. **First phone frame**: f320-hevc10 with layout.mode=0, dump both planes via RWVC_DMA_RW, convert on the PC, compare with
   ref.p010: must be identical. Failure signatures: 16x4 bands with wrong low bits only (PSNR ~50-60 dB, max err 3) = T/R
   rule swapped; block scrambling = group/tile size wrong (refit 80-byte groups vs a separate LSB plane at the end).
   Then time the converter at 1080p and 4K on a big core (target: section 4 numbers).

Probe scripts (ephemeral, scratchpad of the 2026-11-12 session, not part of the tree): GStreamer byte/bit probe and
libyuv cross-check; re-create from section 6.3 if needed.
