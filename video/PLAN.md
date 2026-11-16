# Nord 2 / iOS 16.7 — video and photo codecs: plan for the night of 10→11 November 2026

Written 01:45 CET by the technical lead (planner now, reviewer later). Deadline 08:00 CET.
Nothing here has been run on the phone. Every claim is marked **[V]** verified tonight (path / address given) or **[I]** inferred.

## 0. Status line — what I believe is true at 08:00

| | Expectation |
|---|---|
| **Decode** H.264, HEVC, HEIC/HEIF, JPEG, PNG, AV1, MPEG-4, H.263, AAC inside iOS | **Likely already works** with Apple's own software decoders that are in the stock root file system and are registered by VideoToolbox without any hardware check. Nobody has measured it. The first phone cycle measures it; no new code is needed for it. |
| **Encode HEVC / HEIC** | **Likely by 04:00.** Apple's software HEVC encoder is in the cache with an exported factory but its registration function is an empty `ret`. A 60-line plug-in registers it. |
| **Encode H.264** (what Camera's movie recording and most apps ask for) | **Likely by 06:00 on the host, probable on the phone.** There is no software H.264 encoder in this iOS. We write one (Constrained Baseline, I + P, CAVLC) and register it natively in VideoToolbox. Real time at 720p30 is expected, 1080p30 is not promised. |
| **MP4/MOV + AAC** | Stock software (MediaToolbox writer, AudioToolbox codecs). Expected to work once a video encoder exists; measured by the probe. |
| **Movie recording / playback through the camera stack** | **Uncertain (about even odds)** for a headless recording through `AVCaptureMovieFileOutput`; each failure in CMCapture's movie graph costs one 10-minute cycle plus analysis. |
| **Camera.app's own record button** | **Cannot be confirmed tonight**: nobody is there to touch the screen. A 3-minute checklist for the morning is in §9. |
| **Codec stages on the Mali GPU** | **Host-verified kernels at best.** Not on the phone by 08:00 (needs a new `libredwoodair.dylib`, which collides with the camera agent's work on the same library). |
| **MediaTek hardware VDEC / VENC** (what Android really uses) | **Out of reach tonight**, and for weeks: see §1.3. |
| RAW / DNG capture | Not attempted (the camera plist advertises no Bayer RAW format). |

**An instruction conflict the caller must settle before starting workers.** `work/.claude/CLAUDE.md` (26 October, after a thermal shutdown)
says: at most 2 DeepSeek workers and 1 Claude subagent at a time, one CPU-heavy job, and no phone operations for that resume. The user's
later note (memory, 10 November) lifts the CPU quota **for compilations only**. This task asks for 10 workers and phone cycles. I cannot see
what the user said tonight, so the plan works both ways: §6 gives the order in which packages start if only 2 run at a time (stage 1 then
slips by about two hours and stage 2 is dropped). The thermal guardian (78 °C reduce, 85 °C freeze) stays on in every case.

## 1. Decision record

### 1.1 The premise: "video on the GPU, as on Android" is not what Android does

- **[V]** On the Nord 2 Android decodes and encodes video on MediaTek's dedicated VDEC / VENC blocks, not on the Mali.
  `nord2-current-kernel/arch/arm64/boot/dts/mediatek/mt6893.dts`: vdec at :6281 (`mediatek,mt6885-vcodec-dec`, core 0x16000000, VLD
  0x16020000, MC 0x16021000, LAT 0x16010000/0x16011000, IRQ SPI 424/425, larb4), venc at :5753 (0x17020000 / 0x17820000, SPI 308,
  larb7), JPEG encoder at :5771 (0x17030000, SPI 309), JPEG decoder at :5788. `arch/arm64/configs/cupida_defconfig`:
  `VIDEO_MEDIATEK_VCODEC=y`, `VIDEO_MEDIATEK_VCU=y`, `VIDEO_MEDIATEK_VPU` unset (:3317-3321).
- **[V]** The Mali driver has no codec role: nothing in `drivers/gpu/mediatek/gpu_mali/mali_valhall/mali-r32p1` mentions
  vcodec/vdec/venc/h264/hevc. Valhall has no codec engine **[I]**.
- **[V]** The kernel driver is a thin shell. The codec logic (bit-stream parsing, register programming) is the proprietary user-space
  daemon `/vendor/bin/vpud`: `drivers/media/platform/mtk-vcu/mtk_vcu.c:64` (`VCU_PATH "/dev/vpud"`), `mtk_vcu_mmap` maps the register
  windows into the daemon (:1693-1698, :1776), `vcu_set_gce_cmd` replays its register writes (:695-745); `mtk_vcodec_dec.c` /
  `_enc.c` contain no `readl`/`writel`. The vendor image (`ios-sweet/research/nord2/wifi/vendor-read/vendor.img`) has
  `service vpud /vendor/bin/vpud -f`, `libcodec2_mtk_vdec.so`, `libcodec2_mtk_venc.so`.
- **[V]** Formats the driver routes (the real list is decided by `vpud`): decode H.264, H.265, HEIF, VP8, VP9, AV1, MPEG-1/2/4, H.263,
  WMV/VC-1, RV (`vdec_common_if.c:~98-137`); encode H.264, H.265, HEIF, VP8, MPEG-4, H.263 (`venc_common_if.c:238-263`).
- **[V]** iOS on an iPhone does the same: hardware blocks behind kernel drivers, with the GPU doing only colour conversion, scaling and
  rotation. The plug-ins register only when their driver exists: `AVDRegister` @0x1f2e107e4 calls `AppleAVDCheckPlatform` first;
  `H264H8Register` @0x1f17b8fec → `AppleD5500CheckPlatform`; `H264H9EncoderRegister` @0x200959c54 and `H9EncoderRegister` @0x1af3c5e58 →
  `AppleAVEVA_CheckPlatform` (`IOServiceMatching("AppleAVE2Driver")`); `JPEGH1EncoderRegister` @0x20519cb70 →
  `IOServiceNameMatching("AppleJPEGDriver")`.
- **So:** what the user describes ("on the GPU, as on Android") does not exist on either system. What is true is "hardware accelerated,
  not on the CPU". This plan does not quietly swap the goal: it delivers working codecs first (software, natively wired into iOS), moves
  what a GPU can honestly do onto the Mali second, and says plainly that the Android-equivalent (the VDEC/VENC blocks) is a separate,
  long project.

### 1.2 What the phone's iOS already has **[V]**

Our kernel is XNU built from source with built-in drivers (`xnu/patches`, no Apple kernelcache): there is no AppleAVD, AppleAVE2,
AppleJPEGDriver, AppleD5500 or IOSurfaceAccelerator service. The user land is stock. In the dyld shared cache
(`$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld`, image list read with `research/macho.py`):

| Image in the cache | What it is | Registered on our phone? |
|---|---|---|
| `/System/Library/VideoCodecs/H264SW.videocodec` (text 1.3 MB; imports only CF/CM/CV/VT) | Apple software **H.264 decoder**. `VCPAVCRegisterDecoder` @0x1f0f39fc0 → `VTRegisterVideoDecoderWithInfo('avc1', {IsHardwareAccelerated: false}, …)`, no condition. `VCPAVCRegisterEncoder` @0x1f0f3d110 is a single `ret`, and the image calls no `VTEncoderSession*` function: **there is no software H.264 encoder**. | decoder: yes **[V by code, not run]** |
| `/System/Library/VideoCodecs/VCPHEVC.videocodec` (text 1.5 MB) | Apple software **HEVC decoder and encoder** (frames and tiles: it calls `VTEncoderSessionEmitEncodedFrame`, `…EmitEncodedTile`, `VTTileEncoderSession*`). Decoder registered unconditionally (once-function @0x1bf94da04). `VCPHEVCRegisterEncoder` @0x1bf94ada4 is a single `ret`, **but `HEVCVideoEncoder_CreateInstance` @0x1bf9517c4 is exported** and is a real factory (`CMDerivedObjectCreate` with `VTVideoEncoderGetClassID()`). | decoder: yes. Encoder: **no — one call away** |
| `AV1SW.videodecoder`, `VCPMP4V.videodecoder`, `VCH263.videodecoder` / `.videoencoder` | software AV1, MPEG-4, H.263 | yes **[I]** (register functions not read) |
| built into VideoToolbox | `com.apple.videotoolbox.videodecoder.jpeg.applejpeg` and `.jpeg` (software JPEG decoders), registered in `vtCreateAndPopulateVideoDecoderRegistry` @0x194ed0bbc | yes |
| `AVD.videodecoder`, `H264H8.videodecoder`, `MP4VH8`, `JPEGH1.videodecoder/.videoencoder`, `H9.videoencoder`, `H264H9.videoencoder` | hardware plug-ins | no (platform checks fail) |
| `AppleJPEG.framework` | software JPEG codec; CMPhoto calls `applejpeg_encode_*` and `applejpeg_decode_*` directly (41 call sites) as well as the IOKit driver | n/a |
| `AudioToolbox.framework/AudioCodecs` | software AAC etc. | n/a |

Resulting gaps on our phone: **no H.264 encoder, no HEVC encoder, no JPEG encoder inside VideoToolbox.** Decoders exist for everything.

**A likely cause of "stills do not save" (for the agent working on that; [I], to be measured by the probe):** Camera's default photo
format on an iPhone X is HEIC; CMPhoto makes HEIC with a VideoToolbox `hvc1` (tile) compression session; no HEVC encoder is registered.
For JPEG, `-[BWIntermediateJPEGCompressor _setupJPEGCompressionOptions]` sets `kCMPhotoCompressionOption_RequireHardwareCodec` **[V]**,
and CMPhoto also uses `IOSurfaceAcceleratorCreate` (2 call sites) — neither a hardware JPEG codec nor that scaler exists here.
`CMPhotoCodecSessionPoolCreateEncoderSpecification` sets `EnableHardwareAcceleratedVideoEncoder` and, on one path,
`RequireHardwareAcceleratedVideoEncoder` **[V]**: a software HEVC encoder may have to be registered as "hardware accelerated" to be
taken (switch file `nord2-video-claim-hw`, §4.4).

### 1.3 How iOS takes a third-party codec: the native plug-in mechanism **[V]**

- `VTLoadVideoEncoders` @0x194ccd114 and `VTLoadVideoDecoders` @0x194cc7ea4 walk a **fixed list of paths**; for each:
  `dlopen(path, RTLD_LOCAL)`, `dlsym(handle, name)`, call it (helper @0x194cc807c). Several paths of the list exist neither in the cache
  nor on our root file system (`hfsx.py … ls /System/Library/VideoEncoders` shows only `AppleVideoEncoder.bundle`; `VideoDecoders` only
  `AV1SW.metallib`), among them:
  - encoders: `/System/Library/VideoEncoders/Slim.videoencoder` → `SLIMEncoderRegister`; also `ExampleIPB.videoencoder`,
    `H264H7/H4/H2.videoencoder`, `HEVCH9.videoencoder`;
  - decoders: `/System/Library/VideoDecoders/Slim.videodecoder` → `SLIMRegister`; also `ExampleIPB`, `BritannicusDecoder`, `HEVCH8`, `H8`.
- **A Mach-O dylib file placed at `/System/Library/VideoEncoders/Slim.videoencoder` exporting `SLIMEncoderRegister` is loaded by stock
  VideoToolbox in every process that builds the encoder registry** (mediaserverd for the camera, any app using VideoToolbox, CMPhoto's
  host process). No injection, no kernel change, no patched Apple binary.
- Registration: `VTRegisterVideoEncoder(codecType, createInstance)` / `VTRegisterVideoEncoderWithInfo(codecType, info, createInstance)`
  — both exported. Selection (`VTSelectAndCreateVideoEncoderInstanceInternal` @0x194ed7c2c): filter by codec type and specification
  (`EncoderID`, `RequireHardwareAcceleratedVideoEncoder`, `RequiredPropertySupport`; filter @0x194ed8910), sort by rating, then a loop
  over the candidates (0x194ed80ec … 0x194ed82ec) — it goes on to the next candidate when one cannot be created **[I from the loop]**.
- The encoder object's layout is read out of the cache and frozen in `include/n2vt_abi.h` (vtable of the stock software H.263 encoder
  @0x20ee20068; the names of the later slots from VideoToolbox's own paravirtualized encoder @0x1d69e8be8; argument registers from
  `_ParavirtualizedVideoEncoder_StartSession/EncodeFrame` and `_VTEncoderSessionEmitEncodedFrame` @0x194cd282c).
- Who asks for what **[V]**: CMCapture has exactly one `VTCompressionSessionCreate` call (0x1a213f7bc) and sets no
  Require/EnableHardware key itself (it passes `kVTVideoEncoderSpecification_CMSession`); MediaToolbox's
  `assetWriter_AddVideoTrackWithCompressionAndMediaType` sets `EncoderID`, and on some paths the hardware keys.

### 1.4 The three architectures, judged

**(A) Drive the MT6893 VDEC/VENC blocks from XNU — not tonight, not this month.** The kernel side (clocks, SMI larb, M4U, two IRQs, a GCE
replay channel: about 13.5k lines built for this SoC, of which the hardware-touching files are 1.6k) is portable. The codec is not there:
it is `vpud`, a closed Android daemon (bionic, binder, Codec2), and there is no public register documentation for the MT6893 decoder. The
only open starting point is upstream Linux's stateless `mtk-vcodec` for MT8192/MT8195 (same LAT + core generation **[I]**; not in any
tree on this machine). On the iOS side it would then still be a VideoToolbox plug-in like ours — **not** a fake `AppleAVD`/`AppleAVE2`
kext: those user clients speak private protocols to 240 KB / 850 KB of Apple user-land code (`AVD.videodecoder`, `H9.videoencoder`), far
more to imitate than the 4-function plug-in class. The selectors of those user clients were deliberately **not** enumerated: the
decision does not need them. One block is realistic as a later project: the **JPEG encoder** at 0x17030000, whose register programming
is in the kernel (`drivers/media/platform/mtk-jpeg/mtk_jpeg_enc_hw.c`, 230 lines, 22 `writel`) — a few days with clocks, larb7 and the
IOMMU. Stage 3 of this plan is a written brief for that, nothing more.

**(B) Codec stages on the Mali through our Metal stack — second, and only for our own codecs.** Apple's software decoders are closed
objects; nothing can be inserted into them. Only code we write can use the GPU. What a Valhall compute/fragment pipeline does well
(whole-picture, data-parallel, integer): motion search (SAD), picture pyramids, forward DCT + quantisation of JPEG, deblocking, colour
conversion and scaling (the last already runs on the Mali through VideoToolbox's own Metal transfer path). What it cannot do: entropy
coding (CAVLC/CABAC/Huffman are serial bit-level work), intra prediction (each block depends on its neighbours' reconstruction). Honest
expectation: with four Cortex-A78 cores and NEON beside it, the GPU does not multiply frame rates; see §3 for numbers. Our GPU kernels
are hand-written NIR inside `libredwoodair.dylib` (model: `gpu/build/y2s46-vt6/src/air_isp.c`, `rwa_isp_*`); there is no Metal shader
compiler on this machine, so "write a .metal file" is not an option.

**(C) Software codecs natively inside VideoToolbox — the baseline, and what tonight delivers.** Decoders: Apple's own, already there.
HEVC encoder: Apple's own, registered by us. H.264 encoder: original code (`gpu/video/avc`). JPEG: Apple's `applejpeg` where CMPhoto
accepts software; an original VideoToolbox JPEG encoder is the reserve package R1.

**Chosen: C now, B for our H.264/JPEG encoders as stage 2, A written up only.**

### 1.5 Licences and provenance

- Nothing of Apple's is copied or shipped: we call code that is already in the phone's stock root file system (`dlopen` + exported symbols),
  and our declarations are hand-written (`include/n2_apple.h`), as in `camera-userland-20261109`.
- The H.264 and JPEG code is written from ITU-T H.264 and T.81. **No source of x264, x265, FFmpeg, OpenH264, libde265, JM or
  libjpeg-turbo may be opened or reproduced by a worker** (GPL/LGPL, or simply not needed); the installed binaries are black-box test
  oracles on the build machine and are never linked into anything that goes onto the phone.
- `/references` (= `overnight-20261024/references`) holds no codec source **[V]**: hackios-kernel (APSL-2.0), hackios-userland and
  snapintosh (MIT), mesa (MIT and others; its VA/Vulkan-video front ends are AMD/Intel only, Panfrost has none), metal2vulkan and
  reims-vgpu (GPL: not usable in our product), ios-tegu/ios-topaz/ansuz-metal (no licence file: read-only inspiration).
- New files carry `SPDX-License-Identifier: MIT`.

### 1.6 What iOS needs for Camera capture and playback, and what touches hardware on a real iPhone X

| Use | Format | On an iPhone X | On our phone after this plan |
|---|---|---|---|
| Photo, default | HEIC (HEVC Main Still, 512x512 tiles) | CMPhoto → VideoToolbox → AppleAVE2 | Apple's software HEVC encoder via our plug-in |
| Photo, "Most Compatible" | JPEG + EXIF | CMPhoto → AppleJPEGDriver (software `applejpeg` as fallback) | `applejpeg` if CMPhoto accepts software **[I]**; else R1 |
| Photo decode (Photos, thumbnails) | HEIC, JPEG, PNG, GIF, TIFF, DNG | ImageIO/CMPhoto: AppleAVD / AppleJPEGDriver for big pictures, software otherwise; PNG/GIF/TIFF/DNG always software | all software, stock |
| Live Photo / video, default | MOV, HEVC + AAC (or LPCM) | AppleAVE2 | software HEVC (speed unknown, measured by the probe) or H.264 |
| Video, "Most Compatible" | MOV/MP4, H.264 High + AAC | AppleAVE2 | n2avc (Constrained Baseline) |
| Playback | H.264, HEVC, MPEG-4, AV1 | AppleAVD | Apple software decoders |
| Audio | AAC-LC, LPCM | software (AudioCodecs) | same |
| Scaling / rotation / colour | — | AppleM2ScalerCSC (IOSurfaceAccelerator), GPU | VideoToolbox's Metal path on the Mali (works since y2s45/46), software fallback |
| H11ANE (neural engine) | — | scene analysis, not a codec | absent; not needed |

## 2. Architecture

```
 Camera.app / Photos / AVFoundation / any app            mediaserverd (CMCapture, FigAssetWriter, FigPlayer)
        |  VTCompressionSession / VTDecompressionSession / CMPhoto / ImageIO  (stock)
        v
 VideoToolbox registry (stock)  --dlopen-->  /System/Library/VideoEncoders/Slim.videoencoder   <-- OURS (one dylib)
   decoders: H264SW, VCPHEVC, AV1SW, ...          SLIMEncoderRegister():
   (stock, software, already registered)            'hvc1' -> dlsym(VCPHEVC.videocodec, "HEVCVideoEncoder_CreateInstance")   (Apple's code)
                                                    'avc1' -> n2avc_vt_create()   -> avc/  (original encoder)  -> n2vk kernels
                                                    'jpeg' -> n2jpeg_vt_create()  -> jpeg/ (reserve R1)        -> n2vk kernels
                                                                                               |
                                               n2vk backend: processor (kern/n2vk_ref.c)  or  GPU (rwa_vid_* in libredwoodair.dylib, stage 2)
```

One dylib, about 150-300 KB. It never touches a process that does not use VideoToolbox's encoder registry. Every part can be switched
off by a file in `/private/var/db` (§4.4), and removing the one file restores stock behaviour exactly.

## 3. Staged delivery

Times assume 10 workers from 02:00 (T0). With 2 at a time see §6.

### Stage 0 — measure what already works (02:00 → 03:15; one phone cycle)
Deliverables: plug-in v0 (registers Apple's HEVC encoder only), probe v1, installer. Cycle C1 answers: which encoders/decoders
VideoToolbox lists on the phone; whether H.264/HEVC/JPEG/AV1 decode; whether HEVC encode works and how fast; whether ImageIO writes and
reads JPEG/HEIC/PNG; whether AAC converts. Everything after depends on these answers.

### Stage 1 — the common formats end to end (02:00 → 06:00)
1. H.264 encoder, intra only (WP03) → VideoToolbox glue (WP05) → cycle C2: `VTCompressionSession('avc1')` round trip on the phone.
2. P pictures and rate control (WP04) → cycle C3.
3. MP4 + AAC through `AVAssetWriter`, read back through `AVAssetReader` (WP06, in the same cycles).
4. Photos: HEIC through the registered HEVC encoder; JPEG through `applejpeg`. If the probe shows CMPhoto refusing software:
   `nord2-video-claim-hw`, then R1.
Exit test: on the phone, the probe writes a 3 s 1280x720 H.264 + AAC `.mp4` and a HEIC and a JPEG picture and reads all three back.

### Stage 2 — heavy stages on the Mali (starts 02:00 on the host, phone only if stage 1 is done by 06:00)
Priority and expected effect at 1080p (estimates, to be measured; "CPU" = one A78 core, autovectorised C):

| # | Stage | Why the GPU | Expected |
|---|---|---|---|
| 1 | Motion search, full search ±16 for all 8160 macroblocks (`n2vk_me16`) | 2.4·10⁹ sample differences per picture, perfectly parallel | CPU full search ≈ 250-400 ms/picture; a diamond search on the CPU ≈ 15 ms. GPU full search ≈ 10-25 ms **[I]**. The gain is **compression quality at the diamond search's speed**, not frame rate. |
| 2 | Half-size pictures for a two-level search (`n2vk_half`) | trivial texture pass | ≈ 1 ms instead of ≈ 3 ms |
| 3 | JPEG forward DCT + quantisation of a 12 MP still (`n2vk_fdct8_quant`) | 187k independent blocks | CPU ≈ 120 ms scalar / ≈ 30 ms NEON; GPU ≈ 15-30 ms including upload **[I]**: about break-even |
| 4 | H.264 in-loop deblocking (reserve R2) | per-edge filters, parallel in two passes | CPU ≈ 8 ms; GPU ≈ 3 ms: small |
| 5 | Colour conversion, scaling, rotation | already on the Mali via VideoToolbox's Metal transfer (y2s45/46) | done |
| – | Entropy coding, intra prediction, bit-stream parsing | serial | stay on the processor |

The honest summary for the user: on this chip the GPU can take roughly a third of an encoder's work; the processor keeps the rest. It
does not make this "like Android"; only §1.4 (A) would.

### Stage 3 — hardware blocks
Not tonight. If time remains after 07:00 the lead writes `STAGE3_JPEG_ENCODER_BRIEF.md` (registers, clocks, larb, IOMMU port, the 230
lines to port, the XNU side). No worker package.

### Stage 4 — recording and playback in the camera stack (04:30 → 08:00, lead-driven)
Headless test in the probe (WP06): `AVCaptureSession` + back camera + `AVCaptureMovieFileOutput`, 3 s to `/var/tmp`, read back.
Expected failure points, each analysed when it appears (by a Fable 5.2 subagent at medium effort, as the user asked for new problems in
this project): TCC for the probe process; the movie file sink's graph (`captureSession_fileStartRecording` @CMCapture sets `ProfileLevel`
and `RealTime` on the compression session — our encoder must accept both); audio capture; properties our encoder does not list;
real-time behaviour (frames dropped when encoding is slower than capture). Playback: `AVAssetReader` in the probe; `AVPlayer` on screen
is part of the morning checklist.

## 4. File layout, interfaces, builds, packaging

### 4.1 Tree (everything under `work/gpu/video/`; `V` below)

```
PLAN.md  work_packages.json
include/     n2_apple.h  n2vt_abi.h  n2avc.h  n2avc_int.h  n2vk.h        LEAD (frozen; already written, they compile)
research/    dis.py macho.py calls.py xr.py vtab.py syms.py               LEAD (the tools behind every address in this file)
sdk/         symbols.txt  make_tbds.py  lib/*.tbd  sysinc/                WP01
build_ios.sh build_host.sh run_all_tests.sh                               WP01
vt/          n2vt_shim.c  n2vt_log.c  n2vt_log.h  test/                   WP01
fake/        fake_apple.c  fake_apple.h  fake_vt.c  test/                 WP02
avc/         n2avc_bits.c n2avc_ps.c n2avc_xform.c n2avc_cavlc.c n2avc_tables.h n2avc_intra.c n2avc_mb.c n2avc_enc.c  test/core/   WP03
avc/         n2avc_inter.c n2avc_me.c n2avc_rc.c  test/inter/             WP04
vt/          n2avc_vt.c  n2avc_vt.h  test_avc_vt/                         WP05
probe/av/    probe_av.c  probe_objc.h  symbols.txt  test/                 WP06
probe/       probe_core.c probe_core.h probe_vt.c probe_img.c probe_audio.c probe_apple.h symbols.txt com.nord2.videoprobe.plist test/   WP07
kern/        n2vk_ref.c  n2vk_dispatch.c  test/                           WP08
gpu/         air_video.c  rwa_vid_api.h  vid_gpu_host_test.c  build_host_test.sh   WP08
vectors/     gen_yuv.py  make_vectors.sh  manifest.json  embedded_vectors.h  (+ small generated files)     WP09
oracle/      check_h264.py check_mp4.py check_abi.py yuv.py parse_probe.py run_oracle.sh  test/            WP10
install/     guest-video.sh guest-video-undo.sh owned-video.json stage_payload.sh fetch_probe.sh           LEAD
build/       host/ ios/ (products; nothing here is source)                everybody writes only build/<own package>/
```
No two packages own the same file. A worker edits only its own files; anything else is asked for on the board.

### 4.2 Interfaces (read the headers; this is the map)
- `n2_apple.h` — the Apple calls we use, hand-declared; `-DN2_HOST_FAKE` switches to WP02's implementation. `N2STR("…")` for constant strings.
- `n2vt_abi.h` — the plug-in ABI with its evidence, the plug-in path and entry, encoder IDs, the switch files.
- `n2avc.h` — the encoder as a library: `n2avc_open / headers / encode / reconfigure / reconstruction / close`, and the exact stream it writes.
- `n2avc_int.h` — the seam between WP03 and WP04 (bit writer, slice state, who writes `mb_skip_run`, the three results of `n2avc_p_mb`, rate control).
- `n2vk.h` — three kernels in exact integer arithmetic, and the backend switch.
- Between WP05 and WP01: `vt/n2avc_vt.h` declares `OSStatus n2avc_vt_create(uint32_t codecType, CFAllocatorRef, VTVideoEncoderRef *)`;
  the shim refers to it weakly (`__attribute__((weak_import))`), so the plug-in links and runs with or without the encoder objects.
- Between WP07 and WP06: `probe/probe_core.h` (given in WP07's text) — `struct probe_test { const char *name; int (*fn)(struct probe_ctx *); unsigned timeout_s; }`,
  arrays `probe_vt_tests[]` (WP07) and `probe_av_tests[]` (WP06); `-DPROBE_NO_AV` builds without the second.
- Between WP09 and everybody: `vectors/manifest.json` (name, width, height, frames, md5 per frame, how made) and
  `vectors/embedded_vectors.h` (tiny streams as C arrays for the probe).

### 4.3 Builds
- **Host** (`build_host.sh <package>`; each package also has its own `run_tests.sh`): `clang -std=c11 -O2 -g -Wall -Wextra
  -fsanitize=address,undefined -fno-sanitize-recover=undefined -I include`, glue code additionally `-DN2_HOST_FAKE -I fake` and linked
  with `fake/*.c`; threaded code once more with `-fsanitize=thread`. A test prints `N2TEST <package> PASS <n>/<n>` and exits 0, or
  prints `N2TEST <package> FAIL …` and exits non-zero.
- **iOS arm64** (`build_ios.sh [out]`, default `build/ios`), on the model of `gpu/build/camera-userland-20261109/build_camui.sh`:
  `clang -c --target=arm64-apple-ios16.0 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc -isystem $(clang -print-resource-dir)/include -isystem sdk/sysinc -I include`,
  link `ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -dylib -install_name /System/Library/VideoEncoders/Slim.videoencoder
  -adhoc_codesign -o build/ios/Slim.videoencoder … sdk/lib/*.tbd` (frameworks other than libSystem and CoreFoundation as
  `-weak_library`); the probe: `ld64.lld … -o build/ios/n2vt_probe` as a main executable. The only exported symbols of the dylib are
  `_SLIMEncoderRegister` and `_n2vt_version` (check with `llvm-nm -gU`). TBD stubs list exactly the imported symbols; the **lead**
  checks each against the cache's export tries (`RWCAM_WORK=<cache work dir> python3 research/macho.py '<image>=^_<symbol>$'`) —
  workers cannot (the decompressed cache lives in the lead's session scratchpad, 1.2 GB, not visible in their sandbox).
- Compilations may use all cores (the user lifted the quota for compilations on 10 November); tests that encode video count as the one
  CPU-heavy job and are kept small (§7).

### 4.4 How it gets onto the phone
**A root file system transaction, not `audio.pkg`.** `audio.pkg` is limited to 262144 bytes and is nearly full (the camera bundle is
144 KB), and it is loaded by one host process, while the plug-in must be a file at a fixed path that any process can `dlopen`. The
route is the one of `Nord2Metal.bundle`: `gpu/build/autocycle/txn.sh <kernel image> <tag>` with
`PAYLOAD=../../video/install/payload GUEST=../../video/install/guest-video.sh OWNED=…/video/install/owned-video.json` (txn.sh resolves
both relative to `gpu/build/apps-20261104`). The guest script (lead-written, modelled on `guest-vt6.sh` and `guest-addcc.sh`) installs:

| Path on the phone | What |
|---|---|
| `/System/Library/VideoEncoders/Slim.videoencoder` | the plug-in (mode 755, root:wheel) |
| `/usr/libexec/n2vt_probe` | the probe |
| `/System/Library/xpc/launchd.plist` → job `com.nord2.videoprobe` | runs the probe once per boot, 45 s after load, only when `/private/var/db/nord2-video-probe` exists (and keeps `receipt.json`'s `installed_launchd_sha256` consistent, as `guest-addcc.sh` does) |
| `/private/var/db/nord2-video/` (mode 1777) | probe results `probe-<n>.jsonl`, logs |
| `/private/var/db/nord2-video-*` | switches (`n2vt_abi.h`) |
| TCC rows for the probe (camera, microphone), as `guest-tcc.sh` writes them | stage 4 only |

The kernel image does not change: every cycle flashes the same known-good image that is on the phone now (q10ic). Undo:
`guest-video-undo.sh` removes exactly the owned paths.

Results come back two ways: lines `N2VIDEO …` on `/dev/console` (opened per line, as `rwcam_log.c` does) captured live by
`tools/nord2-usb-console.py`, and the `.jsonl` files read from the mirror after the next cycle's snapshot
(`autocycle/hfsx.py mirror/rootfs-mirror.img get /private/var/db/nord2-video …`).

## 5. On-phone test plan (lead only)

The phone is in fastboot. One cycle = rescue → snapshot → offline edit in the VM → catalog check → commit → flash → capture ≈ 10 min +
the run (probe: 2-4 min). Budget: **8 cycles**. All use the unchanged q10ic kernel image.

| Cycle | Earliest | Installs | Question answered |
|---|---|---|---|
| C1 | 02:50 | plug-in v0 (HEVC registration + log), probe v1 (T01-T05), job, directories | Stage 0. Also: does VideoToolbox load our file at all (console line from `SLIMEncoderRegister`, per process)? |
| C2 | 04:00 | plug-in v1 (+ n2avc intra), probe v2 (+ `avc1` round trip, AVAssetWriter MP4) | H.264 encode natively; MP4 + AAC writing |
| C3 | 05:00 | plug-in v2 (+ P pictures, rate control), fixes from C2 | bitrate, speed (fps at 720p / 1080p), threads |
| C4 | 05:40 | probe v3 (+ headless camera recording), TCC rows | stage 4, first failure |
| C5-C7 | → 07:30 | fixes | stage 4 iterations; `claim-hw` experiments for CMPhoto |
| C8 | 07:40 | final plug-in, probe job disabled (`nord2-video-probe` removed) | the state the user finds: verified boot to SpringBoard |

Rules: a cycle is started only with all host suites green and the payload's hashes recorded; a cycle whose capture shows no boot to
SpringBoard is followed immediately by the undo payload; the last cycle must end before 07:50; nothing is flashed but the q10ic image and
the rescue image; the mirror's 17 GB file is never copied. No cycle while the thermal guardian has frozen work.
Disk: 1.8 GB free and each cycle leaves about 115 MB (`overlay.qcow2`, `initrd.gz`, `delta.bin`). After a cycle is verified the lead
deletes **that cycle's own** `offline/overlay.qcow2` and `offline/initrd.gz` (keeps receipts and logs). Stop and report if less than
500 MB is free.

## 6. Work packages

Full texts are in `work_packages.json` (each is self-contained: a worker reads its own entry, this file and the headers).

| ID | Title | Depends on | First deliverable due |
|---|---|---|---|
| WP01 | Platform: SDK stubs, builds, plug-in shim, log | — | T0+40 min (shim v0 + `build_ios.sh`) |
| WP02 | Fake Apple runtime for host tests | — | T0+60 min |
| WP03 | H.264 encoder core (intra, CAVLC, loop, public API) | — | T0+90 min intra-only stream that ffmpeg decodes bit-exactly |
| WP04 | H.264 inter prediction, motion search, rate control | WP03 seam (header only), WP08 optional | T0+150 min |
| WP05 | VideoToolbox glue for the H.264 encoder | WP02; WP03's API (header only) | T0+100 min against a stub encoder, then the real one |
| WP06 | Probe: AVFoundation tests (MP4 write/read, headless camera recording) | WP07's `probe_core.h` | T0+120 min |
| WP07 | Probe: runner + VideoToolbox / ImageIO / AudioToolbox tests + launchd job | WP09 (tiny streams; a placeholder is specified) | **T0+45 min (v1: this is on the critical path of C1)** |
| WP08 | n2vk kernels: reference + GPU (NIR) | — | T0+45 min reference, GPU after |
| WP09 | TEST: vectors | — | T0+30 min |
| WP10 | TEST: oracle harness | WP09 manifest format (specified) | T0+60 min |
| R1 | (reserve) JPEG baseline encoder + VideoToolbox glue, using `n2vk_fdct8_quant` | WP02, WP08 | when a worker is free |
| R2 | (reserve) H.264 in-loop deblocking (`N2AVC_F_DEBLOCK`) | WP03, WP04 | when a worker is free |
| R3 | (reserve) Intra 4x4 and half/quarter-sample motion | WP03, WP04 | when a worker is free |

**If only two workers may run at a time**, the order is: {WP07, WP01} → {WP03, WP09} → {WP05, WP02} → {WP04, WP10} → {WP06, WP08}.
Stage 0 then lands about 03:30, intra-only H.264 on the phone about 05:30, P pictures about 07:00; stage 2 and the reserves are dropped.

### 6.1 Board conventions
Board: `gpu/video/board/post.py` (the one root prepared for this run; workers are started by `gpu/video/dsh/launch_all.py`, each in a
sandbox that can write only inside the work tree and does not see the host's `/tmp`).
`python3 gpu/video/board/post.py post --from video-wp03 --to video-wp04 --subject "[video] …" --body "…"`; `… read --limit 40`.
Senders `video-wp01` … `video-wp10`, `video-lead`. A worker posts: at start (what it will do first), at each milestone (what now
passes, the command that shows it), **immediately** on a blocker or a wish to change a frozen header (to `video-lead`), and at the end
(`[video] WPnn READY` with the test command and its last line). It reads the board at start and before every milestone.

Who talks to whom:
- WP03 ↔ WP04: the seam of `n2avc_int.h`. WP03 posts when `n2avc_inter_residual`, `n2avc_write_inter_residual`, `n2avc_intra_mb` in P
  slices and the P slice header exist; until then WP04 tests against its own harness.
- WP03 → WP05, WP10: when `n2avc_encode` gives a stream (intra only first).
- WP02 → WP05, WP01: when the fake runtime links (list of what is faked).
- WP05 → WP01: `n2avc_vt.h` exists; WP01 → all: `build_ios.sh` works, how to add a symbol (`<package>/symbols.txt`).
- WP07 → WP06: `probe_core.h` frozen. WP09 → WP07, WP10: `manifest.json` and `embedded_vectors.h` exist.
- WP08 → WP04: `n2vk_ref_me16` exists.
- Everybody → `video-lead`: anything about the phone, the root file system, frozen headers, or a doubt about a clause of the standard.

### 6.2 What only the lead does
1. Freeze the headers (done) and answer change requests.
1a. Verify every symbol of every `symbols.txt` against the cache before the first install (workers cannot).
2. Check the three **[I]** items of `n2vt_abi.h` that C1 can answer, and before C2 read `VCH263`'s `StartSession`/`EncodeFrame` once more
   against WP05's code (argument order, who retains what).
3. Write `install/*` (root file system changes are not delegated) and rehearse them offline against the mirror.
4. Integration build of the plug-in from the packages, hashes, the payload.
5. Every phone cycle (§5) and reading its captures.
6. Reviews (§8) and corrections of what comes back; re-running every package's tests himself.
7. Analysis of new failures on the phone: handed to a Fable 5.2 subagent at medium effort with the log paths and facts (the user's
   standing instruction for this project); mechanical steps stay with the lead.
8. Stage 2 integration: a library build with `air_video.c` in a directory of our own, from a **copy** of the installed sources; never
   edits `gpu/build/y2s4x`, `y2s47-nr`, or `rwcam_*.c`. Agrees the library version with the camera agent before any install of it.
9. Tell the camera/stills agent what §1.2 found (no HEVC encoder registered; `RequireHardwareCodec` on the JPEG path).
10. `NOTES.md` handoff entry and the morning report.

## 7. Test vectors and oracles (WP09, WP10)

Host tools present **[V]**: ffmpeg/ffprobe (libx264, libx265, aac, mjpeg), x264, x265, cjpeg/djpeg (libjpeg-turbo), heif-enc/heif-convert,
dav1d, aomenc, vpxenc, gst-launch; Python 3 with Pillow; **no numpy, no OpenCV, no qemu-aarch64** (arm64 code cannot be run on the host:
iOS objects are compile- and link-checked only, logic is tested in the host build of the same sources).

- Sources of pictures: `vectors/gen_yuv.py` only (pure Python, deterministic: gradients, moving squares, a 32-bit LCG for noise, a
  hard-edged text-like pattern). No downloaded media.
- Sizes: 176x144, 320x240, 640x480, 1280x720, 1920x1080, and the awkward 178x146 and 1918x1078 (cropping). Lengths: 2 to 30 pictures; the
  whole `vectors/` directory stays **under 40 MB**, generated files are reproducible from the scripts and may be deleted.
- **The H.264 oracle is exact**: decoding is normative, so for every packet `ffmpeg`'s decoded picture must equal
  `n2avc_reconstruction()` byte for byte. One wrong table entry or rounding shows up as a mismatch in the first macroblock that uses
  it. Second check: `ffmpeg -bsf:v trace_headers` parses SPS/PPS/slice headers without error and reports the values `n2avc.h` promises.
  Third: PSNR against the source above a floor per QP (so that "bit-exact but garbage" cannot pass), and a size ceiling.
- Decoders on the phone: golden md5 of the first frames of tiny x264/x265 streams (normative, so the phone must match exactly).
- MP4: `ffprobe -show_streams` on files the phone writes (pulled from the mirror): codec, size, frame count, duration, audio stream.
- ABI: `oracle/check_abi.py` compiles a C file printing `sizeof`/`offsetof` of the `n2vt_abi.h` structures and compares with the table of
  §1.3; and checks the plug-in's exported and imported symbols.

## 8. Review checklist (applied to every package when it returns)

Correctness
- [ ] The package's `run_tests.sh` passes when **I** run it from a clean `build/`; the oracle (not the package's own mirror of its logic) agrees.
- [ ] H.264: bit-exact against ffmpeg on every vector incl. the odd sizes, 1 and 4 slices, QP 10/26/40/51, intra-only and P; `trace_headers` clean; first/last macroblock rows and columns exercised; a static scene gives P_Skip runs (and the stream is tiny), a noise scene does not overflow (I_PCM fallback or larger buffer, never a truncated NAL).
- [ ] Emulation prevention tested with payloads full of zeros; AVCC lengths equal the NAL sizes; SPS/PPS identical between `n2avc_headers` and the in-band copies.
- [ ] Glue: the vtable matches `n2vt_abi.h` (static asserts compile); every OSStatus path returns; sample buffer has timing, `NotSync` on non-IDR, a format description made from the encoder's own SPS/PPS; nothing is emitted twice or not at all for a submitted frame.
Memory and bounds
- [ ] No write without a capacity check (bit writer overflow flag honoured; plane accesses within stride × height + padding; motion vectors clamped to the padded area as specified).
- [ ] ASan/UBSan clean; no allocation in the per-picture path after open; every allocation freed in close (leak check); integer overflow considered for 4096x2304.
- [ ] Inputs from iOS are distrusted: NULL pixel buffer, wrong pixel format, unlocked/odd strides, zero dimensions, properties of the wrong CF type → an error code, never a crash (a crash here kills mediaserverd).
Threads
- [ ] Slice threads share nothing writable but their own rows; TSan clean; the VideoToolbox entry points are serialised by a mutex per encoder; registration is once per process.
Honesty
- [ ] No stub, `TODO`, `return 0` placeholder or disabled test behind a "done"; anything unfinished is listed in `STATUS.json` under `not_done`.
- [ ] Tests are not copies of the implementation (expected values come from the standard, the oracle tools, or hand-computed cases).
- [ ] Claims in `STATUS.json` are reproduced by the commands it lists; "host-tested" and "built for iOS" are kept apart; nothing says "works on the phone".
Product hygiene
- [ ] Only owned files changed (`git status`/timestamps); no file outside `gpu/video`; no Apple or GPL/LGPL source text; SPDX line present.
- [ ] iOS objects: only allowed imports (each in a TBD and in the cache), only the two exports, no absolute host paths, no `printf` to stdout in the plug-in, console lines limited (once per process + errors, at most 40 lines per process).
- [ ] Every new behaviour has a switch file or is inert without its caller; with `nord2-video-off` the plug-in registers nothing.

## 9. Risks, and the morning checklist

Biggest risks, in order:
1. **VideoToolbox does not load or accept the plug-in** (sandbox profile of some process refusing the path, code-signing of an on-disk
   dylib in that process, a wrong guess about the registry's process). Precedents say it loads (`Nord2Metal.bundle`,
   `libredwoodair.dylib`, `RedwoodAudio.driver` are loaded unsigned or ad-hoc signed by path) but this exact path is untried. C1 answers
   it per process. Fallback: the other free paths of the list; the kernel's sandbox policy patch.
2. **An ABI detail inferred, not read** (ownership of the emitted sample buffer, the two unnamed class slots, what
   `CopySupportedPropertyDictionary` must contain for CMCapture and AVAssetWriter to proceed). A wrong one crashes mediaserverd. Mitigation:
   the probe exercises the encoder in its own process first; the switch files; undo payload ready.
3. **The H.264 encoder is late or subtly wrong.** The exact oracle bounds "wrong"; "late" is bounded by doing intra-only first, which
   already gives valid (large) recordings. HEVC through Apple's encoder is the alternative for recording if it is fast enough.
4. **Speed.** Software HEVC encoding at camera resolutions may be far from real time; n2avc at 1080p30 is not promised. CMCapture drops
   frames or fails the recording when the encoder is late; lowering the camera's movie format is a camera-plist change that belongs to
   the camera agent.
5. **Clients that insist on hardware** (CMPhoto, some asset-writer paths). `claim-hw` is the lever; it may have side effects (hardware
   encoders are assumed to take IOSurfaces of particular formats).
6. **Stage 4 is a chain of unknowns** in closed code, with 10 minutes per attempt and nobody to touch the screen.
7. **Disk** (1.8 GB) and **cycles** (8): either can end the night early. **Thermal**: the host's guardian may freeze work.
8. **Two agents, one phone**: the stills agent also needs cycles and the same `mediaserverd`. One cycle queue, kept by the lead; every
   payload names what it changed.

Morning checklist for the user (3 minutes): open Camera → Video → record 5 s → stop → open the clip from the thumbnail → it plays with
sound; Settings → Camera → Formats → Most Compatible, repeat; take one photo in each format and open it in Photos; Photos → play an older
H.264 and an HEVC clip. Then read `/private/var/db/nord2-video/` through the next snapshot.
