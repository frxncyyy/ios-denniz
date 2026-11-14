# HEIC stills through the hardware HEVC encoder: plan

Owner: opus-enc4k. 12 November 2026. Design only, nothing built or run for HEIC yet. Tags: [V-phone] seen on the phone,
[V-host] host run or vendor-binary reading, [I] inference. Builds on opus-jpeg's `JPEG_PHONE_TEST.md` section 6a.

## 1. Facts

| What | Source |
|---|---|
| Camera HEIC path: `BWPhotoEncoderNode` -> CMPhoto -> `VTCompressionSessionCreate('hvc1')`; ImageIO HEIC write uses the same VT route. Software encode today = Apple VCPHEVC through our plug-in registration: 1024x768 498 ms, **4032x3024 1586 ms** | [V-phone] probe run 4, JPEG_PHONE_TEST.md 6 |
| Camera currently photographs JPEG (rwcam_diag strips HEVC/HEIF/PrefersHEVC from the still source attributes) | [V-phone] |
| What CMPhoto actually calls (frame session vs `VTTileEncoderSession`, session size, property keys) has **never been logged**: the run-4 kernel log has only the registration lines (`HEVC: VCPHEVC.videocodec hvc1=0`) and the ImageIO result line, no per-call/property lines | [V-phone] `gpu/video/probe-run4-kernel.log` |
| Our hardware VT object (P04, `vt/n2hwenc_vt.c`): frame API only (`startTileSession/prepareToEncodeTiles/encodeTile = NULL`), 16..4096 x 16..2304, properties AverageBitRate/ExpectedFrameRate/MaxKeyFrameInterval[Duration]/RealTime/ProfileLevel/Quality(accepted)/AllowFrameReordering | [V-host] |
| Vendor encoder capabilities (its own QUERY_CAP on the host runner): **H265 Main only, 16..3840 x 16..2176, max level 5.1**; and a separate **HEIF** encoder type: `'HEIF'` fourcc, IPI `VENC_HEIF` (17), **16..16383 x 16..16383 step 16**, profile mask 1, level enum 7; AP parameter `heif_grid_size` (`VENC_SET_PARAM_HEIF_GRID_SIZE`) exists in our AP layer (ap/n2ap_venc.c) | [V-host] cap.sh run, 12 Nov |
| Hardware speed at 624 MHz (q10jg, vc25): HEVC 3840x2160 25.6 ms/frame (~3.1 ms/MP incl. overhead), H.264 4K 30.2 ms, 1080p60 H.264 12.8 ms; at 273 MHz HEVC 1080p 26 ms | [V-phone] vc24/vc25 |
| A 12 MP picture cannot be one H265 job (3840x2176 max); it can be one HEIF-mode session (16383 max) or 48 tiles of 512x512 / 12 of 1024x1024 | [V-host] caps |
| Per-session vendor state now survives reopen (heap fix, 12 Nov): many sessions per daemon process are possible again | [V-host] |
| Intra tiles are independent, so two tile streams can run on VENC core 0 and core 1 concurrently (unlike P-frame video, where frame N+1 references frame N's reconstruction) | [I] |

## 2. Step 1: measure what CMPhoto asks (one boot, no encoder risk)

Wrap VCPHEVC's encoder object in `gpu/video/vt/n2vt_shim.c` with a logging proxy (every CMBaseObject/VTVideoEncoder entry:
`startSession` dims and pixel-buffer attributes, `encodeFrame` count/size/pts/frameProperties, `startTileSession`,
`prepareToEncodeTiles`, `encodeTile` with tile rects, every `copyProperty/setProperty` key and value, `completeFrames`), then
take: (a) the ImageIO probe `img.big.heic` (4032x3024), (b) one Camera photo with `nord2-camera-keephevc`. Output: N2VIDEO
lines in the kernel log + a per-call summary. This decides between the two designs below; it costs one rootfs transaction.

## 3. Step 2: the hardware path (two variants, pick after step 1)

### 3a. CMPhoto opens frame sessions of tile size (likely 512x512, all key frames)
- P04 as is fits the size; changes: every frame forced IDR (`force_key`), one hvcC for all tiles (same SPS/PPS: keep the
  first key frame's parameter sets and verify later ones are byte-identical), Quality -> constant QP (new OPEN field or ENC_SET:
  `qp_control_mode`/`frame_level_qp` exist in the vendor config; map Quality 0..1 -> QP 40..18), no rate control.
- One vcodecd session for all tiles of a picture (the daemon keeps the session open across `encodeFrame` calls of the same
  VT session; CMPhoto may open one VT session per picture).
- Concurrency: CMPhoto may encode tiles from several threads; the daemon is single-session today. Phase 2: two sessions in
  vcodecd (one per VENC core) and the VT object fans tiles out to both.

### 3b. CMPhoto uses the tile API (`VTTileEncoderSession`)
- Implement `startTileSession` / `prepareToEncodeTiles` / `encodeTile` in P04 on the same vcodecd session; each tile = one IDR,
  emitted with `VTEncoderSessionEmitEncodedTile`; same QP mapping.
- Optionally use the vendor **HEIF mode**: OPEN with fourcc `'HEIF'` at the full picture size and `heif_grid_size` = the tile
  size; the library then cuts the grid itself (Android's c2 HEIC encoder model) and returns one bitstream per tile from one
  12 MP NV12 input. Needs: a daemon OPEN path for `N2AP_FOURCC_HEIF` (codec id 3 in n2vcd_proto), the 18.3 MB input buffer
  (pool fits: H265 4K needs ~62 MiB; HEIF 12 MP guest buffers unknown -> measure with the host sweep first), and the output
  framing (per-tile access units: check on the host runner what the vendor returns per ENCODE for HEIF).

## 4. Estimate (12 MP, 4032x3024)

| Path | Time | Basis |
|---|---|---|
| Software VCPHEVC today | 1586 ms | [V-phone] |
| Hardware, 48 tiles 512x512, one core, 624 MHz | ~120-190 ms | 12.2 MP x 3.1 ms/MP = 38 ms of encode + 48 x (2-3 ms IPI/interpreter/power, less with `--venc-keep-power`) + one 18 MB copy (~3 ms) [I] |
| Hardware, two concurrent sessions (core 0 + core 1) | ~70-110 ms | intra tiles only [I] |
| Hardware HEIF mode (one session, vendor cuts the grid) | ~50-80 ms | if the per-tile overhead disappears [I] |

So 8-20x faster than today. Quality: verify with `vcodec_check.py stats` on decoded tiles (PSNR vs the source; the vendor HEVC at
1080p gave 55.6 dB on the test pattern, vc21) and compare file size against VCPHEVC at the same PSNR before Camera is switched
to HEIC.

## 5. Order of work
1. Logging proxy (section 2), one boot.
2. Host: HEIF-mode exploration on the PC runner (OPEN 'HEIF' 4032x3024, grid 512, what each ENCODE returns, DMA needs).
3. Daemon: codec HEIF (if 2 works) or forced-IDR + constant-QP for H265 tile sessions; P04 tile/forced-IDR mode; host tests.
4. Phone: ImageIO `img.big.heic` through the hardware (claim-hw switch), PSNR + timing; then Camera with keephevc.
5. Two-core concurrency (multi-session daemon) if 4 shows the per-tile overhead dominates.
