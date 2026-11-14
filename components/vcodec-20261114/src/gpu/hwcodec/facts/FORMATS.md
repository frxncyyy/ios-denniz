# MT6893 hardware codec formats: what the silicon + vendor stack can do, and through which blocks

Owner: opus-fmt. Written 2026-11-12, host only (nothing here ran on the phone unless marked PHONE). Short paths: `L` =
`gpu/build/cam-vendor-20261109/vfs/lib` (the phone's own vendor objects, 32-bit Thumb), `K` = the Android kernel tree
`work/nord2-current-kernel`, `KV` = `K/drivers/media/platform/mtk-vcodec`. Only symbol names, addresses, constants and log
strings are taken from the vendor objects; no vendor code is reproduced.

Tags: **[PHONE]** observed on the Nord 2; **[PC]** a full session of the phone's unmodified `vpud` + vendor libs under our
interpreter on the fake hardware completed (all pictures displayed, 0 kernel-parity refusals); **[V]** read in the vendor
object / kernel source at the address or line given; **[I]** inferred (reason given).

## 1. Matrix

### 1.1 Decode (VDEC, 0x16000000; vpud IPI = `mtk_vcu.h`:114-127; max sizes = `vfs/etc/media_codecs_c2.xml`)

| Format | HW? | Vendor driver (L/libvcodecdrv.so) | IPI | Cores | Sub-blocks touched (offset from 0x16000000) | GCE / direct | Out | Max | State |
|---|---|---|---|---|---|---|---|---|---|
| H.264 BP/MP/HP | yes | `gH264_DRV_IF` (`h264_dec_*`, `Vdec_Drv_H264_open`) | 2 | LAT+core | VLD 0x20000/0x11000, AVC_VLD 0x22000/0x12000, MC, AVC_MV, PP, MISC, larb4/5 | direct | MM21 | 4096x2176 | **[PHONE] bit-exact 640x480 (vc22)** |
| HEVC Main | yes | `gH265_DRV_IF` (`h265_dec_*`, `Vdec_Drv_H265_open`) | 3 | LAT+core | + HEVC_VLD 0x28000/0x15000 | direct | MM21 | 4096x2176 | [PC]; phone run pending (vc5-decall) |
| HEVC Main10 | yes | same | 3 | LAT+core | same | direct | **MT2T** | 4096x2176 | [PC] f320-hevc10: 10/10, fourcc MT2T, fb 1.25x of 8-bit |
| HEIF/HEIC (HEVC still) | yes | H.265 driver, open arg 0x1c (opus-hevcdec) | 4 | | as HEVC | GCE (opus-hevcdec) | MM21 | 16383x16383 | opus-hevcdec |
| VP9 profile 0 | yes | `gVP9_DRV_IF_HW` (`vp9_dec_*`, `Vdec_Drv_VP9_Open`) | 6 | LAT+core | VLD, VP9 0x28400 / LAT 0x15400, LAT_WDMA, MISC | direct | MM21 | 4096x2176 | [PC] 10/10 |
| VP9 profile 2 (10-bit) | yes | same | 6 | LAT+core | same | direct | **MT2T** | 4096x2176 | [PC] 10/10 |
| AV1 Main (8/10-bit) | yes | `g_av1_drv_if_hw` (`vdec_av1_*`, `vdec_hal_av1_*`) | 13 | LAT+core | VLD, AV1 0x28800 / LAT 0x15800 | direct | MM21 | 4096x2176 | [PC] 10/10 8-bit (needs the ion-table patch, section 5) |
| VP8 | yes | `gVP8_DRV_IF_HW` (`vp8_dec_*`, `VP8_*`) | 5 | core only | VLD 0x20000 (partition 1), BS2 0x26800 (partition 2), VP8 0x27800, MC 0x21000, MV, PP, MISC, GCON 0x2f000, SOC_GCON 0xf000, larb4 | direct | MM21 | 2048x1088 | [PC] 10/10 |
| MPEG-1/2 | yes | `gMPEG2_DRV_IF` (`mpeg2_dec_*`, `mpeg2_risc_settings`) | 9 | core only | VLD 0x20000, MC, PP, MISC | direct | MM21 | 2048x1088 | [PC] 10/10 (B-frames) |
| MPEG-4 part 2 SP/ASP | yes | `gMPEG4_DRV_IF` (`mpeg4_dec_*`, `mpeg4_part2_risc_settings`) | 7 | core only | VLD, MC, PP, MISC | direct | MM21 | 2048x1088 | [PC] 10/10 |
| H.263 | yes | MPEG-4 driver (`vp_mode_risc_settings` / part 2) | 8 | core only | as MPEG-4 | direct | MM21 | 1408x1152 | [PC] 10/10 (352x288) |
| VC-1 / WMV3 | **no** | `gVC1_DRV_IF` = a wrapper that dlopens `/vendor/lib/libvc1dec_sa.ca7.so` (`GetVC1DecoderAPI`) - a CPU decoder that is **not in this phone's vendor image**; WMV1/WMV2 refused ("unsupported WMV type") | 10 | - | none | - | - | (1920x1088 listed) | [V] strings + `find vfs`; [PC] INIT ok, header START refused |
| RealVideo 8/9 | **no** | no driver in libvcodecdrv (only vpud handler names `vdec_rv30/40`) | 11/12 | - | - | - | - | - | [V] |
| JPEG baseline | yes (separate block) | not vpud: `jpgdec@17040000`, 3 cores, SPI 310/311/315, kernel writes 21 user words (hybrid) | - | - | 0x17040000 (VEN domain) | kernel ioctl | YUV | - | J04 research (`jdec/`), no phone run |

Single-core formats (VP8, MPEG-1/2/4, H.263) send per picture LOCK_CORE, one WAITISR(0), UNLOCK_CORE; LAT+core formats
(H.264, HEVC, VP9, AV1) send LOCK_LAT, LOCK_CORE, WAITISR(1), WAITISR(0), unlocks [PC: `req` counters in the logs].

### 1.2 Encode

| Format | HW? | Path | State |
|---|---|---|---|
| H.264 | yes | VENC 0x17020000 (+ core 1 0x17820000), `gH264_ENC_DRV_HW_IF`, IPI_VENC_H264 (15), GCE lists | **[PHONE]** up to 4K (vc14-vc18) |
| HEVC | yes | VENC, `gHEVC_ENC_DRV_HW_IF`, IPI 16 | **[PHONE]** 1080p (vc17) |
| HEIF (HEVC intra still) | yes | IPI_VENC_HEIF (17) -> HEVC encoder (`venc_common_if.c`:263, :514), max 16383x16383 | not run |
| JPEG | yes | JPGENC 0x17030000, our kernel driver `/dev/redwood-jpeg` | **[PHONE]** (q10hw1) |
| MPEG-4 / H.263 | **no (CPU)** | `gMPEG4_ENC_DRV_SW_IF` -> dlopen `libmp4enc_sa.ca7.so`: imports no HW lock/ISR/mmap (only OAL cache/memory calls); Android lists it at 176x144 only. (`libmp4enc_xa.ca7.so` imports `VCodecLockHW/WaitISR/MMAP` = an old hybrid encoder, but libvcodecdrv never loads it) | [V] |
| VP8 | **no (CPU)** | `libvp8enc_sa.ca7.so` (no HW imports); the phone's Android exposes only `c2.android.vp8.encoder` | [V] |
| VP9 / AV1 | no | no encoder at all | [V] |

So the "encode everything" goal reduces to H.264 + HEVC (+ HEIF stills via HEVC, + JPEG); the other encoders would be CPU
encoders anyway (better done natively on iOS than under the interpreter).

## 2. Evidence

- Drivers per format: `llvm-nm -D L/libvcodecdrv.so` data objects `gH264_DRV_IF`, `gH265_DRV_IF`, `gMPEG2_DRV_IF`,
  `gMPEG4_DRV_IF`, `gVC1_DRV_IF`, `gVP8_DRV_IF_HW`, `gVP9_DRV_IF_HW`, `g_av1_drv_if_hw`, `gH264_ENC_DRV_HW_IF`,
  `gHEVC_ENC_DRV_HW_IF`, `gMPEG4_ENC_DRV_SW_IF` [V]. dlopen targets (strings of libvcodecdrv): `libvc1dec_sa.ca7.so`,
  `libvp8dec_sa.ca7.so`, `libvp9dec_sa.ca7.so`, `libmp4enc_sa.ca7.so`, `libvp8enc_sa.ca7.so`, `libh264*_sa.ca7.so`, ...
  (legacy software fall-backs; the [PC] runs show the HW drivers being used for VP8/VP9: `vp8_dec_init ChipName = 37`,
  VP8_*/VP9 register traffic).
- IPI -> codec: `KV/vdec/vdec_common_if.c`:97-138 (fourcc -> IPI), `KV/venc/venc_common_if.c`:236-263; the kernel is a pure
  relay for every format (`KV/vdec_drv_if.c`:35-52: all fourccs go to `vdec_common_if`).
- VP8 register map: `VP8_GetHWRegBase` @0x124e5e maps 0x16000000, 0x1602f000, 0x16025000, 0x16020000, 0x16020800,
  0x16021000, 0x16022000, 0x16023000, 0x16024000, 0x16027800 (VP8 VLD), 0x16026800 (VLD2/BS2), 0x1600f000, 0x1602e000 [V].
  `VP8_VLDGetBits` @0x125336: VP8 VLD +0 / +0x200 (partition 1 / 2), register 0 = peek, register n = shift n [V].
- Sub-block table for VP9/AV1/HEVC (core / LAT): `facts/VDEC_REGISTERS.md` section 1 (opus-dec2) [V].
- Every sub-block above lies inside RWVC_WIN_VDEC_ALL (0x16000000 + 0x30000); the [PC] runs recorded `mmio bad=0` and no
  EACCES in the kernel-parity fake for every format, i.e. the q10jf kernel window/page-power/larb rules already admit them.
- Media list: `vfs/etc/media_codecs_c2.xml` (c2.mtk.{avc,hevc,heif,vpx,vp9,av1,mpeg2,mpeg4,h263,vc1}.decoder;
  c2.mtk.{avc,hevc,heif,mpeg4,h263}.encoder) [V]; vc1/mpeg4/h263 encoders are software inside the vendor stack (above).

## 3. Cost per picture on the PC run (320x240; one `hw->regs()` = one RWVC_REGS ioctl on the phone, write-combining off)

| Format | regs calls / picture | reads | writes | WAITISR |
|---|---|---|---|---|
| MPEG-2 | ~90 | 31 | 58 | 1 |
| MPEG-4 | ~270 | 106 | 162 | 1 |
| H.263 | ~300 | 123 | 179 | 1 |
| AV1 | ~440 | 124 | 314 | 2 |
| HEVC Main10 | ~700 | 224 | 472 | 2 |
| VP9 | ~1070 | 204 | 863 | 2 |
| H.264 (opus-decint, 640x480) | ~1030 (498 with combining) | | | 2 |
| **VP8** | **~5020** | 1184 | 3834 | 1 |

VP8 is the outlier: ~1090 index/data writes per picture to MC 0x2193c/0x21940 (a table upload) plus 200 to 0x27900/0x27904.
At the vc22 cost (~12 ms for ~1030 single ops) VP8 would need ~50 ms per picture: run it with write-combining (DEC04) and
measure; the table upload is the place to batch [I].

## 4. Host model (fmt/n2vdec_fmt_fake.[ch], MIT, no vendor code)

Wraps opus-hevcdec's model (which wraps T02's kernel-parity fake); every op goes to the inner back end first, then this model
overrides the value for (offsets from 0x16000000):

| Register | Rule | Evidence |
|---|---|---|
| VLD 0x20000 / LAT_VLD 0x11000 / BS2 0x26800: 0xb4, 0xb8, 0xb0, 0x108, 0x8c | vfifo start/end (<<6), rptr latch (16-byte aligned), reset on 0x108 bit 8 / 0x8c bit 23, fetch-ok after 0x8c bit 20 | `VP8_InitBarrelShifter` @0x1207dc |
| same +0xe8 / +0xf4 | fetch ok bit 0 / process flag idle | `VP8_WaitVLDFetchOK` @0x12070c |
| VP8 0x27800 + 4n (n 0..32), +0x200 for partition 2 | bit reader (peek / shift-n), raw bytes | `VP8_VLDGetBits` @0x125336 |
| VP8 +0x148 / +0x14c | probability update done at once (reads 0) | `VP8_HwAccCoefProbUpdate` @0x120dd8, `...MVProbUpdate` @0x120e10 |
| VP8 +0x170 / +0x174 bit 0 | picture finished | `VP8_ReadFinishFlag` @0x124300 |
| VP9 0x28400 / 0x15400, AV1 0x28800 / 0x15800, + 4n | bit readers (core VLD / LAT VLD fed) | `vdec_hal_av1_read_literal_raw` @0x146724 |
| VP9 +0x1a8 | counts-table clear done (reads 0) | vendor log `vdec_hal_vp9_clear_counts_table timeout` (50000 reads) |
| VP9 +0x110 bit 16 | bool decoder ready | `vdec_hal_vp9_init_bool` @0x12da5c |
| VP9 +0x0cc bits 0..13 | per-table probability update done | `VP9_update*Probs`, `vdec_hal_vp9_update_probs` |
| VP9 +0x1ec bit 0 | counts / prob table WDMA done | `vdec_hal_vp9_count_tbl_wdma`, `_prob_tbl_wdma` |
| larb SLP_CON, MISC 0x104 HW-break ready, MISC 0x110..0x13c idle | as fakevdec_hevc | VDEC_PROTOCOL.md |

MPEG-1/2/4 and H.263 needed no rule beyond the HEVC model's VLD (their decode completes with WAITISR answered). The model
produces no pixels; every value it returns after a "decode" is a stand-in, so the [PC] state proves the protocol, buffer and
register-window path, not the decode itself.

## 5. Findings that change other packages

1. **dec1/vdec_ref.c swapped START data[0]/data[1]** (sent buffer size as `bs->size`): harmless for H.264, fatal for AV1 (the
   OBU parser walks into the zero tail: `av1_error: non-annexb bitstream has no obu size field`). `ap/n2ap_vdec.c` is
   already right (data[0] = AU bytes). Fixed in `fmt/vdec_fmt.c`.
2. **The shim's ion table (`shim/n2shim_android.c` N2AND_ION_BUFS = 64) overflows**: vpud holds ~34 (H.264) to ~53 (AV1) ion
   buffers itself and every client buffer adds a share; an AV1 session after a VP9 session in the same vcodecd fails
   (`ion_import failed`, `Y framebuf get_ion_buffer_addr fail`). `fmt/patches/0002-shim-ion256.patch` (64 -> 256); with it
   VP9 then AV1 pass, without it AV1 fails [PC, `ION256=0 fmt/run_sim_fmt.sh`].
3. **10-bit output is MT2T** (V4L2_PIX_FMT_MT2110T, 8-bit tiles + separate 2-bit planes, fb 1.25x): HEVC Main10, VP9
   profile 2 (and AV1 10-bit [I]). `mm21/n2_mm21.c` detiles MM21 only; the protocol has no 10-bit output format yet;
   10-bit clips are therefore not in the phone payload.
4. **MPEG-2/MPEG-4/H.263 are not bit-exact by specification** (IDCT accuracy per IEEE 1180): judge them by PSNR
   (`decint/compare_dec.py STREAM dprobe.log YUV --stream N`), expect >= 40 dB with small drift; VP8/VP9/AV1 must be
   bit-exact.
5. AV1/VP9 DPB: dpb 16 / 20 output buffers (+2) per session: at 4K that is ~22 x 12.5 MB; the RWVC pool must be sized for it.

## 6. Product path (patch set, PC-rehearsed)

`fmt/patches/0001-fmt-decoders.patch` (proposal; owners on the board): protocol codecs `'vp08' 'vp09' 'av01' 'mp2v' 'mp4v'
'h263'` (CMVideoCodecType fourccs, little-endian like `'avc1'`), HELLO_OK.dec_codecs bits 2..7, client/daemon/adapter
acceptance, `n2ap_vdec` fourcc -> IPI (VP8 5, VP9 6, MPEG-4 7, H.263 8, MPEG-1/2 9, AV1 13), `vcodecd --fake-vdec-fmt`
(test builds), `n2vcd_dprobe` codec from extension / IVF fourcc and the IVF / MPEG ES / H.263 splitters.
`bash gpu/hwcodec/fmt/run_sim_fmt.sh` -> `N2TEST FMT-SIM PASS 7 streams` (H.264 control + MPEG-2, MPEG-4, H.263, VP8, VP9, AV1
in ONE vcodecd, 10/10 pictures each).

## 7. Phone payload vc7-fmt (`gpu/build/apps-20261104/payload-vcodec-fmt`, staged 2026-11-12, rehearsal REHEARSAL_OK)

Binaries built by `fmt/stage_fmt.sh` from the patched /tmp copy (0001 + 0002): vcodecd `59aab13f87f28f0a`
(`--decoder --guest-log 300 --dec-wc off`), n2vcd_dprobe `46053b3f0237ae6c`, n2vcd_probe `8c801de2bfc25975` (encode 640x480 x10
first, as every decode payload). Streams in this order (one session each, ONE boot): `vc14-probe43-640x480-baseline.h264`
(control, bit-exact in vc22), `f320-mpeg2.m2v`, `f320-mpeg4.m4v`, `f352-h263.h263`, `f320-vp8.ivf`, `f320-vp9.ivf`,
`f320-av1.ivf` (all 10 frames; 320x240, H.263 352x288). Owned list `apps-20261104/owned-vcodec-fmt.json`. Needs no kernel
change beyond the image that ran vc22 (q10jf: VDEC_ALL window + VDE clock fix); every sub-block is inside VDEC_ALL.
`stage-vcodec.sh` / `guest-vcodec.sh` now accept `.ivf .m2v .m4v .h263` stream names (additive).

    W=$NORD2_WORK
    # (host, done) bash $W/gpu/hwcodec/fmt/stage_fmt.sh ; bash $W/gpu/build/apps-20261104/rehearse-vcodec.sh payload-vcodec-fmt
    cd $W/gpu/build
    PAYLOAD=payload-vcodec-fmt GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-fmt.json \
      timeout 900 bash autocycle/txn.sh $IMG vc7-fmt > autocycle/runs-vc7-fmt.out 2>&1      # IMG = the vc22 image (q10jf)
    python3 apps-20261104/vcodec_check.py fetch vc7-fmt;  F=apps-20261104/vcodec-fetch/vc7-fmt;  S=$W/gpu/hwcodec/fmt/streams
    C=$W/gpu/hwcodec/decint/compare_dec.py
    python3 $C $W/gpu/hwcodec/decint/streams/vc14-probe43-640x480-baseline.h264 $F/dprobe.log --stream 0   # BIT-EXACT 10/10
    python3 $C $S/f320-mpeg2.m2v $F/dprobe.log $F/out/dec-*-1-320x240.nv12.yuv --stream 1   # PSNR (IDCT tolerance)
    python3 $C $S/f320-mpeg4.m4v $F/dprobe.log $F/out/dec-*-2-320x240.nv12.yuv --stream 2   # PSNR
    python3 $C $S/f352-h263.h263 $F/dprobe.log $F/out/dec-*-3-352x288.nv12.yuv --stream 3   # PSNR
    python3 $C $S/f320-vp8.ivf  $F/dprobe.log --stream 4                                    # BIT-EXACT expected
    python3 $C $S/f320-vp9.ivf  $F/dprobe.log --stream 5                                    # BIT-EXACT expected
    python3 $C $S/f320-av1.ivf  $F/dprobe.log --stream 6                                    # BIT-EXACT expected

Expected in dprobe.log: `stream N: ... codec=mp2v|mp4v|h263|vp08|vp09|av01`, `HELLO_OK ... dec=0xff`, `DONE ok=1 ...
pictures=10/10` per stream, `ALL ok=1 streams=7/7`. Failure reading: `OPEN failed: unsupported` = an unpatched vcodecd;
`START ... -110` on one format with the others fine = a status bit the real block sets differently from section 4 (send
vcodecd.log's guest log lines of that session to opus-fmt); `ion_import failed` = 0002 missing. A failing format does not
stop the next session (one session per stream); the H.264 control first proves the boot's decoder path.
