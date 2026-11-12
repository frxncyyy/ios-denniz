# MM21 (MediaTek 8-bit block mode) on MT6893: layout, converter, decode output path

Owner: opus-mm21. Written 2026-11-11, host only: nothing here has run on the phone, and no real VDEC frame has been seen.
**[V]** = verified (path:line, command or oracle result given), **[I]** = inferred. Short paths: `K` = the Android kernel
`work/nord2-current-kernel` (Linux 4.19.325), `KV` = `K/drivers/media/platform/mtk-vcodec`. Code: `gpu/hwcodec/mm21/`.

## 1. Names and formats

| Fact | | Evidence |
|---|---|---|
| fourcc `'MM21'` (0x3132_4d4d) is called `V4L2_PIX_FMT_MT21` in this kernel; comment: "MTK 8-bit block mode, two non-contiguous planes" | [V] | `K/include/uapi/linux/videodev2.h`:684 and :739-740 (upstream Linux later calls the same fourcc `V4L2_PIX_FMT_MM21`) |
| fourcc `'MT21'` = `V4L2_PIX_FMT_MT21C` = the UFO-**compressed** variant ("Compressed two-planar YVU420 format used by Mediatek MT8173 ... opaque intermediate format and the MDP hardware must be used to convert"); not CPU-readable | [V] | `videodev2.h`:734; `K/Documentation/media/uapi/v4l/pixfmt-reserved.rst`:237-245 |
| The vendor decoder reports `MM21` (2 planes) and `MT21` among its output formats; also `NV12`/`YV12` (1 plane) | [V-host] | facts/VDEC_PROTOCOL.md 2.4 (QUERY_CAP probe) |
| Default layout mode keeps UFO (MT21) for pictures of >= 0x1fe000 pixels; `vendor.mtk.vdec.frame.layout.mode=0` gives MM21 (`ufo=0`) | [V-host] | DECODE_PATH.md 2.3 (libvpud_vcodec 0x107f4). Note 0x1fe000 = 2088960 = 1920 x 1088: **1080p is exactly at the threshold**, so the property is mandatory for 1080p |
| Linear NV12 direct output: `SET_DECODE_LAYOUT` fails for H.264 | [V-host] | DECODE_PATH.md 2.3 |
| Nothing in the phone's vendor userland detiles MM21 on the CPU (no `DetilePlane`/`MM21ToNV12`/block-to-linear symbol in any `lib`/`lib64` object); on Android the MDP (libdpframework) blits it | [V] symbol scan / [I] MDP role | `llvm-nm -D` + `grep -a` over `gpu/build/cam-vendor-20261109/vfs`; `K/drivers/media/platform/mtk-mdp/mtk_mdp_regs.c`:33 (`MDP_COLOR_420_MT21`, "Mediatek proprietary format") |

## 2. The layout

```
Y  plane: byte(x, y)   = (y / 32) * y_stride * 32  +  (x / 16) * 512  +  (y % 32) * 16  +  (x % 16)
UV plane: byte(xb, yc) = (yc / 16) * uv_stride * 16 + (xb / 16) * 256  +  (yc % 16) * 16 +  (xb % 16)
          xb = byte column of the interleaved Cb,Cr row (Cb at even xb, Cr at odd: NV12 order), yc = chroma row = y / 2
```

- **Tiles**: luma 16 bytes x 32 rows (512 B), chroma 16 bytes (8 CbCr pairs) x 16 rows (256 B), each tile row-major
  inside, tiles left to right, then the next tile row (no Z/zigzag order, unlike Samsung's NV12MT). One luma tile row
  (= `stride * 32` bytes) and the matching chroma tile row (`stride * 16` bytes) cover the same 32 picture rows, so both
  planes have the same number of tiles. **[V-oracle]** for the formula as an implementation of "MM21": the host's libyuv
  (r2921, `MM21ToNV12`, linked as a black box, source not read) gives byte-identical NV12 to n2_mm21 on 13 geometries
  including 1920x1080/1088, 3840x2160/2176, 1280x720/736, 320x240/256 and odd sizes (`mm21/run_tests.sh`). That this
  phone's VDEC writes exactly this layout is **[I]** (same mtk-vcodec IP family and fourcc as the ChromeOS MT8183/8192
  decoders libyuv's MM21 support was written for); the first phone frame settles it (section 6).
- **Strides**: both planes use the same stride = `vsi.pic.buf_w` (`bytesperline[0] = bytesperline[1] = coded_width`,
  `sizeimage[1] = sizeimage[0] / 2`) **[V]** `KV/mtk_vcodec_dec.c`:1044-1054, :1819-1821. Stride is a multiple of 16.
- **Height alignment**: the default coded size is aligned to 16 (width, 2^4) x 32 (height, 2^5) = the luma tile **[V]**
  `KV/mtk_vcodec_dec.c`:1037-1042. Vendor (host runs on the fake): 320x240 -> buf 320x256, 1920x1080 -> buf 1920x1088, Y
  2088960 = 1920*1088, C 1044480 = Y/2 **[V-host]** DECODE_PATH.md 1. HEVC caps step 64 (16..4096/64 x 16..2176/64,
  VDEC_PROTOCOL.md 2.4), so HEVC may align buf_w/buf_h to 64 **[I]**: always take `buf_w`/`buf_h` from the vsi; never derive them.
- **Plane placement**: two separate buffers (the kernel hands the vendor `fb_dma[0]`, `fb_dma[1]` per plane,
  `KV/vdec/vdec_common_if.c`:200-223) **[V]**; whether a plane may start at `stride*buf_h` inside one buffer is up to the
  AP (DEC05's two-handle amendment uses two buffers). The converter takes two plane pointers either way.
- **Crop**: H.264 frame cropping and HEVC conformance windows are in chroma units for 4:2:0 (CropUnitX = CropUnitY = 2),
  so crop left/top are always even **[V]** (H.264 7.4.2.1.1, HEVC 7.4.3.2.1). The converter requires even x/y and accepts
  odd w/h (chroma rounds up).
- **Padding** contents (columns >= width, rows >= height inside buf_w x buf_h) are undefined **[I]**; the converter never copies them.

## 3. The converter: `gpu/hwcodec/mm21/n2_mm21.{h,c}` (MIT)

- `n2_mm21_to_nv12(src, dst, x, y, w, h)`: rectangle of a CPU-addressable MM21 picture -> NV12 with arbitrary destination
  pitches (CVPixelBuffer bytesPerRow). Writes exactly w x h luma and 2*ceil(w/2) x ceil(h/2) chroma bytes.
- `n2_mm21_to_nv12_staged(src, fetch, ctx, staging, ...)`: for sources only reachable by copy (RWVC DMA buffers): per tile
  row one `fetch(plane, offset, bytes)` of the contiguous range covering the rectangle's tile columns, then detile from the
  L2-resident staging buffer. 1080p: 34 luma + 34 chroma fetches, staging 61440 bytes (4K: 131072).
- `n2_mm21_detile_plane` (one plane, any tile height 16/32), `n2_nv12_to_mm21` (inverse with edge replication: for tests and
  for a fake VDEC that must produce MM21), `n2_mm21_impl()` ("neon"/"c").
- Inner loop: head (partial tile) + full tiles + tail; under `__ARM_NEON` four tiles per iteration with q-register
  loads/stores (`ldr q` x4, `stp q` x2 in the arm64-ios object), else memcpy(16). No allocation, no output, thread-safe.

Tests (`bash gpu/hwcodec/mm21/run_tests.sh` -> `N2TEST MM21 PASS 5/5`, ~7 s): 167 checks per build x 4 builds
(portable C and the NEON code path compiled against a plain-C intrinsic stand-in, each ASan+UBSan -O1 and -O2): an
independent per-pixel tiler (written from the formula above) round trip, padding filled with garbage, guard bytes around
every destination row/plane, exact-size source allocations (over-reads trip ASan), staged == expected with the exact fetch
count and in-bounds fetches, 13 libyuv comparisons, 21 fixed geometries + 40 random ones (offsets, odd sizes, partial head/tail
tiles, 1x1, 1080 in 1088), inverse function checks, the error cases; plus the arm64-apple-ios16.0 compile with the real SDK
(NEON path selected). A mutation check (luma tile height 16, or a wrong NEON group advance) drops the count to 102/167
and 111/167. **Host-tested and compiled for iOS; not run on arm64.** The real NEON instructions have not executed anywhere.

Speed (host i5-10210U, one core, 1920x1080 from buf 1920x1088, cold source = 8 rotating frames > LLC): 0.38-0.52 ms per
frame direct, 0.38 ms staged with a memcpy fetch; a plain memcpy of the same 3,133,440 bytes takes 0.29 ms, i.e. the
detile runs at ~75% of copy speed. Phone estimate **[I]**: memory-bound, A78 single-core copy bandwidth ~8-15 GB/s ->
0.5-1 ms; A55 ~3-5 GB/s -> 1.5-2.5 ms; plus RWVC_DMA_RW (dc civac of 3.1 MB + copyout) ~0.5-1 ms. Target < 4 ms: met with margin
on paper; measure on the phone (section 6).

## 4. Constraints the output path must live with [V]

- Pixels never travel in relay messages (<= 1024 B); they sit in RWVC DMA buffers named by handle
  (`include/n2vcd_proto.h`:4-7). Client (plug-in) buffers are allocated by the client with `RWVC_DMA_SHARED`, the daemon
  learns their pa with `RWVC_DMA_INFO`, and they are freed when the client process's last open closes (crash-safe)
  (`include/redwood_vcodec.h`:56-68).
- **No mmap**: user space reaches a DMA buffer only by copy, `RWVC_DMA_RW` (`redwood_vcodec.h`:57). The kernel maps the pool
  cached (`ml_io_map_unmappable(..., VM_WIMG_DEFAULT)`), does `dc civac` + `dsb sy` per chunk before a read, copies at most
  1 MiB per copyout, and holds the device mutex for the whole call (`kern/bsd/dev/nord2_vcodec.inc`:55, 111, 297-303,
  1149-1176). So a read via RWVC_DMA_RW is cache-correct after a device write without any extra sync.
- The decoder keeps output buffers as references and returns pictures in display order (rev-2 proposal,
  `dec1/n2vcd_proto_dec_rev2.h.txt`); a picture's buffer goes back to the vendor only after DEC_RELEASE.
- CoreVideo has no MM21 pixel format; VT clients get NV12 (420v/420f) CVPixelBuffers from
  `VTDecoderSessionCreatePixelBuffer` (pool built from `VTDecoderSessionSetPixelBufferAttributes`, facts/VT_DECODER_ABI.md:59-61).
  An IOSurface lives in the VT decoder process and cannot be handed to vcodecd over the relay.

## 5. Proposal: the output path (lowest copy that works with the relay/daemon architecture)

**Where: in the plug-in (Slim.videodecoder, in the VT decoder process), not in the daemon.** The daemon has no faster access
to client buffers than the plug-in (both copy through RWVC_DMA_RW), and whatever it produced would still have to be copied
into the IOSurface by the plug-in: a daemon-side detile adds one full-frame copy and an NV12 buffer per picture.

**Buffers** (DEC05 two-handle form): the plug-in allocates the pool after DEC_HEADER_OK, `dpb_frames` entries x
{Y `stride*buf_h`, C `stride*buf_h/2`}, `RWVC_DMA_SHARED`, and registers them with DEC_ADD_BUFFER. The pool belongs to the
plug-in process (freed automatically if mediaserverd / VTDecoderXPCService dies); the daemon only holds handles + pa.
1080p: 3,133,440 B per entry; dpb 8 + 3 = 11 entries = 34.5 MB of the 86 MiB pool (VENC shares it; 4K does not fit).

**Per picture** (DEC_PICTURE_OK index i, crop rectangle):
1. `VTDecoderSessionCreatePixelBuffer` -> NV12 CVPixelBuffer of the **display size** (attributes: 420v, or 420f when the VUI
   says full range; width/height = crop w/h; IOSurface properties {}), `CVPixelBufferLockBaseAddress(pb, 0)`.
2. `n2_mm21_to_nv12_staged()` with `fetch = transport->buf_rw(ctx, plane ? c_handle[i] : y_handle[i], offset, staging,
   bytes, 0)` (one-buffer form: plane 1 offset += stride*buf_h), dst = the two plane base addresses and bytesPerRow, rect =
   (crop_left, crop_top, w, h). One 60 KB staging buffer per session.
3. `DEC_RELEASE(i)` **immediately** (before emitting): the vendor gets its reference buffer back after ~1-2 ms, so the pool
   never needs more than dpb+2 entries.
4. Unlock, `VTDecoderSessionEmitDecodedFrame(pb, pts)`.

Copies per frame: DMA buffer -> staging (kernel copyout, 60 KB at a time, stays in L2) -> IOSurface (detile). DRAM traffic
= read 3.1 MB + write 3.1 MB, the minimum for any CPU path; 68 ioctls per 1080p frame (~0.2-0.4 ms of syscall overhead),
each holding the device mutex for one 60/30 KB copy (tens of µs), so the daemon's register ioctls are never starved.
**No kernel, ABI or protocol change is needed**, and the identical code runs on the host socket transport (buf_rw over the
shared file) so DEC06 can test it end to end on the fake.

**Protocol detail needed (to DEC05 / opus-lead2):** DEC_PICTURE_OK carries width/height (crop size) but not crop left/top.
Propose `DEC_PICTURE_OK.reserved = crop_left | crop_top << 16` (layout unchanged; both 0 for nearly all streams) and stating
that `stride` applies to both planes. If omitted, the plug-in must assume (0,0), which is wrong for streams with a top/left
crop.

**Rejected alternatives** (for the record):
- *Daemon detiles into an NV12 client buffer*: +1 frame copy, +1 buffer per picture, more relay traffic. No benefit.
- *mmap of client DMA buffers into the plug-in* (new `RWVC_DMA_MAP` + `RWVC_DMA_SYNC`): saves only the copyout into the
  L2-resident staging buffer (~0.3 ms/1080p), but maps physical pool memory into mediaserverd and needs new kernel
  code and review. Revisit only if phone timing shows the RWVC_DMA_RW path above ~3 ms.
- *Zero-copy (VDEC writes into the IOSurface)*: impossible today: CoreVideo cannot consume MM21, the vendor refuses linear
  NV12 for H.264, and the decoder needs physically contiguous pool memory below 4 GB, which IOSurface pages are not.
- *MDP hardware blit* (Android's MM21->NV12 path): offloads the CPU but is a new block to bring up (power, larb, GCE), and
  its destination is pool memory again, so the copy into the IOSurface remains.
- *Metal compute detile*: the GPU would need the MM21 bytes in a CPU-visible MTLBuffer first (the same copy), plus a GPU
  wake-up per frame; consider only if the CPU budget becomes the limit (4K60).

## 6. What the first phone decode must check

1. Decode the 320x240 H.264 test stream (DECODE_PATH.md 1) on the phone with layout.mode=0; read both planes with
   RWVC_DMA_RW; on the PC run `n2_mm21_to_nv12` and compare with `ffmpeg -i stream -pix_fmt nv12 -f rawvideo`:
   H.264 decoding is bit-exact, so PSNR must be infinite (identical bytes). Any tile-order/height error shows up
   as 16x32 block scrambling: if so, dump plane 0 raw and fit the tile parameters (16x32 vs 32x32 vs 64x32, row-major vs Z).
2. Repeat at 1920x1080 (buf 1088, crop 8 rows) and check that `fb_sz[0]` = 2088960 and the C plane = Y/2.
3. Time `n2_mm21_to_nv12_staged` with the RWVC transport on a big and on a little core (target < 4 ms single core).
