# Nord 2 (MT6893) hardware codecs under iOS 16.7: the single entry document

Owner: opus-doc. Covers the work from 10 Nov 2026 (night) to 12 Nov 2026. **Last update: 12 Nov 2026, ~01:50; section 8 (dp4, HEVC VT decode, prod6) added 12 Nov ~07:00** (vc27-dec4k fetched and
checked bit-exact; jp1-jpeg running; vc27-reopen queued). This file consolidates; the detailed sources are listed in section 6. Re-generated each time new
run results land.

Evidence tags used everywhere below:
- **[PHONE]** seen on the phone: a run tag and the log/file that shows it are named next to the claim.
- **[HOST]** host test, fake hardware, iOS build or fake-root rehearsal only. Never ran on the phone.
- **[I]** inferred (reading of vendor code / Linux sources, or arithmetic on measured numbers).

Run tags: `vcN-xxx` = one rootfs transaction + one boot (`gpu/build/cc-txn-<tag>/events`, live console
`gpu/build/autocycle/runs/<tag>/`). The daemon/probe logs and output files of run N arrive with the snapshot of the NEXT transaction and
are fetched to `gpu/build/apps-20261104/vcodec-fetch/vcN/` (`probe.log`, `dprobe.log`, `vcodecd.log`, `out/`). The logs on the
phone are append-only, so each fetch contains all earlier runs too: a run's own lines are the ones after the previous fetch's line
count.

---------------------------------------------------------------------------------------------------------------------------------

## 0. State in ten lines

| Capability | State | Proof |
|---|---|---|
| JPEG encode (JPGENC 0x17030000), kernel-driven | **[PHONE]** 640x480 0.73 ms, 1920x1080 4.6 ms (autotest command 7) | q10hw1, `runs/q10hw1/` (11 Nov) |
| JPEG encode from iOS user space (`/dev/redwood-jpeg`) | [HOST] only; staged as jp1-jpeg (running at 01:36) | JPEG_PHONE_TEST.md |
| H.264 encode (VENC) via vendor libs in vcodecd | **[PHONE]** 640x480 .. 3840x2160 (and 4096x2160 once, vc24), Baseline/Main/High | vc14, vc16, vc18, vc21, vc24, vc25 |
| HEVC encode | **[PHONE]** 1080p (273 MHz), 3840x2160 at 29.4 fps (624 MHz); reopen in the same process failed on the phone (root cause found, fix [HOST], phone check vc27-reopen pending) | vc17, vc24, vc25 |
| H.264 decode (VDEC) | **[PHONE]** bit-exact 640x480 and 1080p (High, B-frames) | vc22, vc26 |
| HEVC decode | **[PHONE]** bit-exact 640x480 and 1080p (Main, B-frames) | vc26 |
| 4K / 1080p60 decode | **[PHONE]** bit-exact: H.264 High 3840x2160 and 4096x2176, HEVC Main 3840x2160, H.264/HEVC 1080p60 | vc27-dec4k (q10jh) |
| VideoToolbox / Camera on the hardware | [HOST] only (payload-vcodec-prod staged, never run in Camera) | CAMERA_INTEGRATION.md |
| HEVC decode through VideoToolbox (Slim.videodecoder) | [HOST] only: lazy DEC_HEADER (parameter sets + first IRAP), 8/8 at 176x144 and 10/10 at 1080p against the vendor objects on the host; payload-vcodec-prod6(-probe) staged, phone check dp4 pending (dp3: HEVC went to software) | section 8 |
| HEIC via hardware HEVC, JPEG decode, VP9/AV1/MPEG decode | design / [HOST] only | HEIC_HW_PLAN.md, FORMATS.md, p031x 0315 |
| Real interrupts | not implemented (polled waits; 0314 adaptive wait in q10ji, never booted) | kern/p031x/STATUS.md |

---------------------------------------------------------------------------------------------------------------------------------

## 1. Architecture

```
 iOS 16.7 user space                                                         XNU (our patches)                     MT6893
 ─────────────────────────────────────────────────────────────────────────   ─────────────────────────────         ───────────────
 mediaserverd / Camera / apps
   VideoToolbox
     /System/Library/VideoEncoders/Slim.videoencoder   (gpu/video plug-in + P04 hw objects 'avc1','hvc1';
                                                         registered only if /private/var/db/nord2-video-claim-hw)
     /System/Library/VideoDecoders/Slim.videodecoder   (DEC06 'avc1','hvc1'; only if nord2-video-hwdec)
        │  n2vcd protocol (include/n2vcd_proto.h; HELLO/OPEN/ENC/DEC/CLOSE; N2VCD_VERSION 1)
        ▼
   /dev/redwood-vcodec-client ── relay (IK, minor 1; client id = pid+uniqueid, no proc reference) ──┐
                                                                                                      │
 probes (one-shot launchd jobs):  n2vcd_probe (encode), n2vcd_dprobe (decode, MM21->NV12 NEON),       │
                                   n2vcd_selftest, n2jpeg_phone (JPEG)  ── same relay ────────────────┤
                                                                                                      ▼
 vcodecd (one process, launchd com.nord2.vcodecd, jetsam limit raised to 3072 MB by itself)   /dev/redwood-vcodec (K02, minor 0)
   n2vcd server (daemon/n2vcd_srv.c, daemon/dec = DEC05 decoder service)                        RWVC_REGS batches, RWVC_WAIT_IRQ
   AP side:  ap/n2ap_venc.c (VENC IPI messages), ap/n2ap_vdec.c (DEC01, VDEC IPI + LOCK/power)   (polled +0x5c / VDEC status),
   vcu/n2vcu.c (V40): /dev/vcu emulation, GCE command lists executed in the daemon as             RWVC_DMA_ALLOC (pool, PA < 4 GB),
        register batches / polls / waits (VENC_EOF -> RWVC_IRQ_VENC ...)                          RWVC_POWER (VEN/VEN_C1/VDE/VDE2,
   shim/ (bionic subset, ion table, heap), loader/ (n2elf), emu/ (n2a32: ARMv7 A32+T32+VFP+NEON    venc_sel 273 MHz default,
        interpreter, 4 GiB guest reservation, 4 KiB guest pages on 16 KiB host pages)             624 MHz via kern.n2vc.venc_fast),
   GUEST (AArch32, the phone's own unmodified vendor files, /usr/local/lib/n2vendor):              larb7/8 (VENC), larb4/5 (VDEC)
        bin/vpud, lib/libvcodecdrv.so, lib/libvpud_vcodec.so, lib/libvcodec_oal.so,               IOMMU bypass, VDEC_ALL window
        lib/libvcodec_utility.so  (hard-linked from gpu/build/cam-vendor-20261109/vfs, sha-pinned,  ─────────────►  VENC 0x17020000
        never committed)                                                                                          (+C1 0x17820000)
                                                                                              /dev/redwood-jpeg (K01)  VDEC 0x16000000
                                                                                              (24 MiB src + 16 MiB out  JPGENC 0x17030000
                                                                                               at 0xD8000000; shares VEN)
```

Key design facts:
- VENC/VDEC are programmed on Android by the closed `libvcodecdrv.so` inside `vpud`; the Linux kernel only relays. We host the
  phone's own five vendor objects under an AArch32 interpreter in vcodecd (PLAN.md section 4). [PHONE] since vc14.
- The vendor emits `struct gce_cmds` lists; V40 (`vcu/n2vcu.c gce_run_list()`) executes them in user space as RWVC register batches
  and polled waits. The kernel has no GCE parser (kern/p031x/STATUS.md, "GCE command lists").
- Encode output: Annex-B H.264/HEVC. Decode output: MM21 (MediaTek 8-bit block layout, facts/MM21_LAYOUT.md), converted to NV12 by
  `mm21/n2_mm21.c` (NEON) in the client.
- One codec session at a time in vcodecd (encode and decode are exclusive; others get BUSY).
- VDEC_CORE's larb4 hangs off the MDP SMI common: keep the display on during tests (the kernel refuses power otherwise).
- DMA pool: 0xDA800000 + 86 MiB up to q10jg; **0xA0000000 + 384 MiB from q10jh** (0313; needed for 4K decode, ~217 MB per 4K
  H.264 session [HOST measurement]).

---------------------------------------------------------------------------------------------------------------------------------

## 2. Results table

### 2.1 VERIFIED ON THE PHONE

All encode numbers: `hw_ms`/`hw_us` = hardware time per frame as measured by vcodecd around the GCE list (from the probe's per-frame
lines / `stats` line); "fps" = 1000 / avg hw ms unless marked "wall". PSNR from `vcodec_check.py stats` (corrected checker, see bug
B9). Clock 273 MHz unless stated.

**JPEG (kernel autotest, command 7)**

| Date | Image / run | Case | Result | Source |
|---|---|---|---|---|
| 11 Nov | q10hw1 | 640x480 Q90 kernel test picture | 0.73 ms, valid JFIF, PSNR_Y 43.6 dB | `runs/q10hw1/`, STATUS.md, NOTES 11 Nov morning |
| 11 Nov | q10hw1 | 1920x1080 Q85 | 4.6 ms, 263936 B, valid | `runs/q10hw1/check.json` |

**H.264 / HEVC encode**

| Date | Run (tag) / image | Case | Result | Source |
|---|---|---|---|---|
| 11 Nov 22:42 | vc14-encode / q10je | H.264 Baseline 640x480, 10 f, 300 kbit/s | `DONE ok=1 frames=10/10 keys=1 bytes=19367 hw_ms=73`; ~7.2 ms/frame (frame 0 8.6 ms); decodes with `ffmpeg -xerror`; kernel `venc_sel 0xc030601 -> 0x4030601 (273 MHz)`, `first VENC done status 0x4` | fetch vc14 probe.log, `out/probe-43.h264` |
| 11 Nov 23:04 | vc16-1080p / q10je | H.264 1920x1080 (buffer 1088), 30 f, 8 Mbit/s | `ok=1 30/30 bytes=982042 hw_ms=882`; 29.2-29.6 ms/frame (frame 0 30.7); ~7.9 Mbit/s; ffmpeg decodes | fetch vc16 |
| 11 Nov 23:17 | vc17-hevc / q10je | HEVC Main 1920x1080, 30 f, 8 Mbit/s | `ok=1 30/30 bytes=867010 hw_ms=782`; ~25-26.7 ms/frame | fetch vc17 `out/probe-43.h265` |
| 11 Nov 23:29 | vc18-ladder / q10je | H.264 1080p30 auto bitrate | 30/30, avg 29.34 / max 30.93 ms, 8116 kbit/s | fetch vc18 |
| | | H.264 3840x2160 30 f (level 5.1) | 30/30, avg **68.8** ms (14.5 fps), 33.8 Mbit/s, 4219489 B; PSNR_Y 46.1 avg (min 43.0) | fetch vc18; board 00:35 |
| | | HEVC 1080p / HEVC 4K / H.264 4096x2160 in the same process after 2 H.264 sessions | all `ok=0` (HEVC SEQ_HDR IPI -60, then guest stop): bug B11 | fetch vc18, board 00:41 |
| 12 Nov 00:17 | vc21-q / q10je | H.264 quality/size bisect, 7 sessions, 30 f each | all ok=1. hw ms avg: 1920x1080 Baseline 29.45, 1920x480 16.57, 640x1080 12.94, 1280x720 16.45, 640x480 7.16, 1080p Main 29.22, 1080p High 29.30. PSNR_Y (corrected checker): 1080p Baseline 51.6 avg (min 47.8), Main 51.6, **High 60.9** (min 48.2), 1920x480 51.4, 1280x720 51.2, 640x1080 51.3, 640x480 51.1; HEVC 1080p (vc17 stream) 55.6 | fetch vc21; board 00:35 (opus-enc4k) |
| 12 Nov 00:45 | vc24-rs / q10jf, `--restart-on-switch` | H.264 High 1080p30 | 30/30, avg 29.37 ms, 7232 kbit/s | fetch vc24 |
| | | H.264 High 3840x2160 | 30/30, avg **59.4** ms (14.7 fps wall), 31.5 Mbit/s | fetch vc24 |
| | | HEVC Main 1080p (after BUSY/RSTR restart + client retry) | 30/30, avg **26.2** ms, 7190 kbit/s, PSNR 56 dB | fetch vc24; NOTES 01:00 |
| | | HEVC Main 4K (second HEVC session, same process) | ok=0, frame 0 HW timeout (bug B11) | fetch vc24 |
| | | H.264 High 4096x2160, 10 f @25 | 10/10, avg 63.3 ms (only run where 4096 was accepted; later refused by policy, see 5) | fetch vc24 |
| 12 Nov 00:58 | vc25-fast / q10jg, `kern.n2vc.venc_fast` | H.264 High 4K30 at 273 MHz | avg 59.6 ms (14.6 fps wall) | fetch vc25 |
| | | H.264 High 4K30 at **624 MHz** | avg **30.2** ms / max 33.7 (905 ms hw for 30 f, ~33 fps by hw time; 25.8 fps wall in the probe) | fetch vc25 |
| | | H.264 4096x2160 | refused by vcodecd (width > 3840, vendor QUERY_CAP), ok=0 by design | fetch vc25 |
| | | H.264 High 1080p60 at 624 MHz | 60/60, avg **12.8** ms / max 14.3, **66.7 fps wall**, 8.0 Mbit/s, PSNR_Y avg 51 | fetch vc25; NOTES 01:20 |
| | | HEVC Main 4K30 at 624 MHz (fresh process after restart) | 30/30, avg **25.6** ms / max 29.1, **29.4 fps wall**, 31.2 Mbit/s, PSNR_Y 51 | fetch vc25 |
| | | HEVC 1080p60 (HEVC reopen in the same process) | ok=0, HW timeout (bug B11; fix B12 is [HOST] only so far) | fetch vc25 |

Probe-side per-frame overhead at 640x480 (vc19/20/22/26 probes, 10 f): hw avg 7.28-7.45 ms, wall 79-80 ms for 10 frames (~126 fps).
Daemon start to serving: 0.26 s after a launchd restart (board 00:41, vc24).

**H.264 / HEVC decode** (all through the relay; rtt = request round trip per picture incl. MM21->NV12 in the probe; bit-exactness by
`decint/compare_dec.py` against ffmpeg)

| Date | Run / image | Streams | Result | Source |
|---|---|---|---|---|
| 11 Nov 23:52 | vc19b-dec / q10je | H.264 640x480, 1080p | VDEC **decoded** (LAT + CORE done irqs, picture in list_disp, 8-12 ms) but every START failed on the VDEC_CORE power-off (`bus protection 0x40 not acknowledged (0x103)`, -60): bug B7. `DONE ok=0` x2 | fetch vc19 dprobe.log/vcodecd.log; DECODE_PHONE_TEST.md 0b |
| 12 Nov 00:04 | vc20-hevcdec / q10je, old vcodecd | H.264 640x480, HEVC 640x480, HEVC 1080p | all `ok=0`, same power-off -60 (`hw power block=3 on=0 -> -60`) | fetch vc20 |
| 12 Nov 00:30 | vc22-decall / **q10jf** | H.264 Baseline 640x480 (the vc14 hardware stream) | **`ok=1 pictures=10/10 error_maps=0`, BIT-EXACT 10/10** (cmp, PSNR inf), avg rtt 12.58 ms; first hardware decode | fetch vc22; NOTES 00:50 |
| | | H.264 1080p | dprobe killed by jetsam (bug B8) | DECODE_PHONE_TEST.md 0c |
| 12 Nov 01:11 | vc26-decall / **q10jh**, `--dec-wc alt` | 8 sessions, each stream twice (write-combining off, on) | **all `ok=1 10/10 error_maps=0`, all BIT-EXACT 10/10** | fetch vc26; NOTES 01:35 |
| | | H.264 Baseline 640x480 | avg rtt 13.58 -> **9.60** ms (wc on) | |
| | | H.264 High 1080p, 2 B-frames | 14.77 -> **10.72** ms | |
| | | HEVC Main 640x480 | 12.54 -> **9.56** ms | |
| | | HEVC Main 1080p, B-frames | 13.47 -> **10.66** ms (~93 fps single stream); detile 33-47 ms per 10 pictures (3.4-4.7 ms/picture) | |
| 12 Nov 01:24 | vc27-dec4k / q10jh, `--dec-wc alt`, `--no-yuv` (tag vc6-dec4k) | 9 sessions, pairs WC off / on | **all `ok=1`, `error_maps=0`, all BIT-EXACT** (compare_dec.py from the log hashes, re-run by opus-doc 01:50: 20/20, 20/20, 60/60, 60/60, 60/60, 60/60, 20/20, 20/20, 10/10) | fetch vc27 dprobe.log |
| | | H.264 High 3840x2160, 2 B-frames, 20 f | avg rtt 12.82 (max 25) -> **9.05** ms (max 16) | |
| | | H.264 High 1080p60, 60 f | 14.33 -> **10.88** ms | |
| | | HEVC Main 1080p60, 60 f | 13.52 -> **10.71** ms | |
| | | HEVC Main 3840x2160, 2 B-frames, 20 f | 9.37 -> **7.72** ms | |
| | | H.264 High 4096x2176 (DCI), 10 f, WC off | 11.00 ms (max 12) | |
| | | MM21->NV12 detile (NEON, in the probe, logged separately as detile_ms) | 3840x2160 **8.8 ms/picture** (172-177 ms / 20), 1080p **2.0 ms** (120 ms / 60), 4096x2176 **10.0 ms** (100 ms / 10). At 4K the client conversion costs about as much as the decode round trip, so it bounds single-stream 4K throughput [I] | |

### 2.2 HOST-ONLY (never on the phone)

| Item | Evidence |
|---|---|
| HEVC reopen fix (guest heap zeroing, B12): 3x HEVC and h264,hevc,h264,hevc in one process OK | V30 277637/277637, IP 1088/1088 (board 01:33); staged `payload-vcodec-reopen` (vcodecd 8ba58f7c) |
| `--venc-keep-power` (per-frame power ops 2 -> 0) | host real-vendor check (board 00:53) |
| Decode of MPEG-2, MPEG-4, H.263, VP8, VP9 p0/p2, AV1, HEVC Main10 by the phone's vpud on the fake VDEC; product path 7 streams | `fmt/run_fmt.sh`, `N2TEST FMT-SIM PASS 7` (facts/FORMATS.md); staged vc7-fmt |
| VideoToolbox encoder objects (P04, pipelined) and decoder plug-in (DEC06) | P04 8517/8517, DEC06 2553/2553, DEC08 289/289 |
| `/dev/redwood-jpeg` from user space (n2jpeg_phone, J02) | N2TEST JPGP 45/45, J02 1483/1483 |
| JPEG decode words (J04) + kernel path 0315 (RWJD_JOB) | J04 5296/5296; p031x JPEGDEC 876/876 (q10ji, not booted) |
| 0314 adaptive wait (20 us spin for 2 ms, then 200 us sleeps) | p031x 589041/589041 (q10ji, not booted) |
| Vendor encoder capabilities (QUERY_CAP on the host runner): H.264 Baseline/Main/High, max level 5.1, 16..3840 x 16..2176; HEVC Main only, same sizes; HEIF 16..16383; MPEG-4 <= 640x480; H.263 176x144 | board 00:53 (opus-enc4k) |
| Write-combining halves VDEC ioctls (1031 -> 498 per AU), identical pictures | board 00:50; phone timing effect in vc26 above |

### 2.3 INFERRED / estimates

| Claim | Basis |
|---|---|
| HEIC 12 MP through hardware: ~120-190 ms (one core, 48 tiles), ~50-80 ms in vendor HEIF mode, vs 1586 ms software today | HEIC_HW_PLAN.md section 4 (3.1 ms/MP from vc25) |
| Real interrupts would save ~0.1-0.2 ms per wait vs 0314 | kern/p031x/STATUS.md |
| 4K30 H.264 in Camera at 273 MHz would record at about half rate (~14 fps) | vc24/vc25 timing |
| IOMMU bypass of VENC ports is correct (same as the JPEG ports that worked) | p031x audit [I, strong]; confirmed in practice by every [PHONE] encode |

---------------------------------------------------------------------------------------------------------------------------------

## 3. How to build and run

All commands from `W=$NORD2_WORK`.
Only the main session touches the phone; every flash needs the user's approval of the exact image.

### 3.1 Images (boot partition only; series `S=` of q10hq + the listed patches; `FAILURES: none` for all)

| Image | = | sha256 (16) | boot spare | Recipe | Phone use |
|---|---|---|---|---|---|
| q10hw1 | q10ik + 0296 + 0298 (JPEG autotest) | `5efacdfeb184582c` | | `gpu/hwcodec/build_image.sh q10hw1` | JPEG M1 (11 Nov) |
| q10jy | + 0310,0311 (jpegdev/vcodec/relay, VENC autotest), 0320/0321 (mic), 0330/0331 (I2C8) | `501d794c1f4c2f52` | 6144 | `device-mmu-q10jy-candidate/build_q10jy.sh` | vc2: reset ~0.5 s after the probe's relay open (bug B4). Not usable |
| q10jz/ja/jb/jc/jd | relay breadcrumb / bisect images | see kern/p031x/STATUS.md | | | diagnostics only (vc3-vc9) |
| **q10je** | q10jy + relay without proc references + VCT trace | `e9f79c0cde88e308` | 4096 | `device-mmu-q10je-candidate/build_q10je.sh` | vc10-vc21 (first encodes) |
| **q10jf** | q10je + 0310 `4cbc3bde` (no VDE clock gating before bus protection, `kern.n2vctrace` default 0) | `7acf035f66021372` | 4096 | `recipes-q10jf/build_q10jf.sh` | vc22, vc24 |
| **q10jg** | q10jf + 0312 (`kern.n2vc.venc_fast` 0=273/1=624 MHz) | `d57fc0950e7b97ea` | 4096 | `gpu/build/build_q10jg.sh` | vc25 |
| **q10jh** | q10jg + 0313 (pool 0xA0000000 + 384 MiB) | `33deeaba5b53e093` | 4096 | `gpu/build/build_q10jh.sh` | vc26, vc27 |
| q10ji | q10jh + 0314 (adaptive wait) + 0315 (JPEG decoder path RWJD_JOB) | `a1098364ad520e02` | **2048** (next patch must cut log strings first) | `gpu/build/build_q10ji.sh` | never booted |

Series of q10ji: `S=,0296,0298,0299,0300,0310,0311,0312,0313,0314,0315,0320,0321,0330,0331` (0301 never included). Patches:
`gpu/hwcodec/kern/p031x/` (0310-0315, `make_series.sh --check`), `gpu/build/audio-capture-20261111/patches`,
`gpu/build/i2c8cam-20261111`. Recovery image: q10iu (`a7640bf5...`) or the RAM ADB rescue.

### 3.2 Build (host)

    bash $W/gpu/hwcodec/ios/build.sh /tmp/n2hw-X/ios          # vcodecd (+decoder), n2vcd_probe, n2vcd_dprobe, n2vcd_selftest -> "IOS TOOLS OK"
    bash $W/gpu/hwcodec/ios/build_plugins.sh /tmp/n2hw-X/plugins   # Slim.videoencoder + Slim.videodecoder -> "PLUGINS OK"
    bash $W/gpu/hwcodec/jpeg/phone/build_ios.sh               # n2jpeg_phone
    # host suites: fakevenc/run_session.sh (+_hevc), decint/run_sim.sh, vt/test/run_tests.sh (P04), fmt/run_sim_fmt.sh,
    # kern/p031x/test/run_tests.sh, mm21/run_tests.sh, jpeg/phone/test/run_tests.sh

### 3.3 Stage + rehearse a payload (`gpu/build/apps-20261104/stage-vcodec.sh TAG [options]`)

Main options: `--tools DIR --vcodecd FILE`, `--payload DIR`, `--keepalive` (+ `--restart-on-switch` if the binary has it; `--no-restart`
to omit), `--vcodecd-arg ARG`, `--probe-args "..."`, `--preset NAME`, `--decoder STREAM` (repeatable, up to 8; adds dprobe),
`--dprobe-args`, `--encoder-plugin`, `--decoder-plugin`, `--video-on/--video-off NAME`, `--on/--off GATE` (run, selftest, probe,
dprobe, jpeg), `--jpeg-test FILE --jpeg-dec FILE --jpeg-args`, `--prod` (production Camera payload).

Presets (probe sessions; `--then` chains sessions in one boot): `720p30 1080p30 1080p60 4k30 4kdci hevc1080p30 hevc4k30 ladder
ladder-lvl ladder-rs q fast reopen hevc4k`.

    cd $W/gpu/build/apps-20261104
    bash stage-vcodec.sh vc4k-fast --tools T --vcodecd T/vcodecd --payload $PWD/payload-vcodec-fast --keepalive --preset fast
    bash rehearse-vcodec.sh $PWD/payload-vcodec-fast | tail -2       # must end REHEARSAL_OK (fake root, real launchd.plist)

Staged payloads (all REHEARSAL_OK): vcodec (vc2-encode), -4k, -4k30, -q, -lvl, -rs, -fast, -fastkp, -hevc1080, -hevc4k, -reopen,
-dec, -hevc, -decall, -dec4k, -fmt, -jpeg, -jpegvc, -prod, -prod-sync, -prod-nodaemon, heic-measure. Owned lists `owned-vcodec*.json`.

### 3.4 Run on the phone (main session)

    cd $W/gpu/build
    PAYLOAD=payload-vcodec-decall GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-dec.json \
      timeout 900 bash autocycle/txn.sh $IMG vc26-decall > autocycle/runs-vc26-decall.out 2>&1
    tail -3 cc-txn-vc26-decall/events        # committed / kernel flashed <sha16> / capture done

`autocycle/txn.sh IMG TAG` = snapshot rootfs -> install payload offline -> flash IMG to boot -> boot -> 600 s USB console capture.
`gpu/build/queue_run.sh WAIT_TAG "TAG PAYLOAD OWNED IMGNAME" ...` waits for `capture done` of WAIT_TAG, then for each job:
`phone.py reset fastboot`, txn.sh, wait. This is how vc19b..vc27 ran back to back (~13 min per run).
Phone control: `gpu/build/autotest/phone.py` (`wait`, `status`, `logs`, `reset fastboot`, `tap`, `scenario photo|video`;
README.md there). Undo: same txn with `GUEST=guest-vcodec-undo.sh`.

Rules: keep the display on (MDP domain for VDEC_CORE); do not open Camera before `N2VCDP DONE` (the JPEG driver would power VEN
first at 624 MHz); never run hwc.py command 7 concurrently with a VEN user.

### 3.5 Results

    python3 apps-20261104/vcodec_check.py fetch vc26          # after the NEXT transaction -> apps-20261104/vcodec-fetch/vc26/
    python3 apps-20261104/vcodec_check.py log <console file> --out DIR       # live: first AU from the hex lines
    python3 apps-20261104/vcodec_check.py stats <stream> --log <probe.log>  # PSNR per frame vs the regenerated pattern
    python3 $W/gpu/hwcodec/decint/compare_dec.py <stream> F/dprobe.log [F/out/dec-*-N-WxH.nv12.yuv] --stream N   # BIT-EXACT n/n
    ffmpeg -v error -xerror -i F/out/probe-43.h264 -f null -
    python3 $W/gpu/hwcodec/jpeg/phone/phone_check.py vcodec-fetch/jp1-jpeg --inputs jpeg-inputs   # JPEG_CHECK PASS
    python3 $W/gpu/hwcodec/cam/cam_check.py log|movie ...     # Camera verdict (CAMERA_INTEGRATION.md 6)

compare_dec.py works from the log alone (dprobe logs an FNV-1a-64 hash per cropped NV12 picture; `decint/fnv_frames.c` for > 720p).

---------------------------------------------------------------------------------------------------------------------------------

## 4. Bugs found and their root causes

| # | Symptom (run) | Root cause | Fix | Where |
|---|---|---|---|---|
| B1 | selftest/vhost "guest memory exhausted" on the phone | iOS arm64 pages are 16 KiB; `mprotect` on a 4 KiB-aligned guest range returns EINVAL (kern_mman.c) | protection per host page = union of the 4 KiB guest page states; `N2A32_MEM_GRANULE=16384` simulates it on Linux | emu/n2a32_mem.c (board 19:42) |
| B2 | vcodecd killed while loading the first vendor library (vc3/vc4, reason ns 1 code 7) | jetsam per-process memory limit (~6 MB effective); launchd `JetsamMemoryLimit` not applied | `memorystatus_control(SET_MEMLIMIT_PROPERTIES, 3072 MB)` at start (include/n2memlimit.h); no pre-touching of guest pages | board 20:34 |
| B3 | `RWVC_DMA_ALLOC` EINVAL | ION heap flags forwarded to the kernel | flags 0 | NOTES 11 Nov night |
| B4 | phone reset ~0.5-1 s after the probe's relay open (q10jy..q10jd, vc2-vc9) | the relay `/dev/redwood-vcodec-client` held a `proc_find` reference in its slot until close | client = pid + uniqueid, transient proc_find in the reaper (q10je) | kern/p031x/STATUS.md q10je |
| B5 | 2 s per ioctl, timeouts | daemon-side debug trace delays (`kern.n2vctrace`) left on | probe sets it 0; default 0 from q10jf | NOTES; q10jf |
| B6 | VENC bitstream = SPS/PPS + zeros (host) | bitstream mirror not refreshed after each GCE list | V40 rev 6 | CRITICAL_PATH.md 9a |
| B7 | decode done but START fails (vc19b, vc20) | `n2vc_hw_off` gated VDEC_CKEN/LAT_CKEN/LARB1_CKEN before VDE2 bus protection; Linux never gates them (GATE_INV_DUMMY), so the handshake needs the clocks running | kernel q10jf; daemon treats a failed power-off as non-fatal and holds one VDEC ref per session | DECODE_PHONE_TEST.md 0b |
| B8 | dprobe killed at the 1080p stream (vc22) | same jetsam limit as B2 in the probes (7.5 MB staging at 1080p) | probes raise their own limit | DECODE_PHONE_TEST.md 0c |
| B9 | H.264 PSNR "27 dB" at width >= 1280 | checker bug: vendor writes BT.709 VUI for widths >= 1280; ffmpeg's filter graph inserted a BT.709 -> BT.601 conversion against the untagged reference | stats filter strips the tags; real PSNR 51-61 dB | board 00:35 |
| B10 | 4K decode would not fit | 86 MiB pool; 4K needs ~110-250 MB | 0313 pool 0xA0000000 + 384 MiB (q10jh) | kern/p031x/STATUS.md q10jh |
| B11 | HEVC after another session in the same process hangs (vc18 SEQ_HDR -60, vc24 HEVC 4K frame 0, vc25 HEVC 1080p60) | vendor `hevc_enc_init` clears only 0x424 of its 0x5ab10-byte handle; our guest heap recycled the previous handle with stale GCE buffer slots ("too many GCE buffers") | workaround on the phone: `--restart-on-switch` / restart after any HEVC session (vc24/vc25 [PHONE]); real fix B12 | board 01:02, 01:33 |
| B12 | (root fix of B11) | Android's malloc of 370 KB gives a fresh zeroed mapping | `shim/n2shim_heap.c` zeroes recycled blocks >= 64 KiB | [HOST] only; payload-vcodec-reopen staged |
| B13 | VDEC DMA stalls (host) | vendor writes back larb SLP_CON 0x00010000; kernel allow-list accepts bit 0 only | vcu sends bit-0 WRITE_MASK | DECODE_PHONE_TEST.md 1 |
| B14 | vendor parsed zeros (host) | client input mirror synced TO the device before START | refresh device -> guest before every START | DECODE_PHONE_TEST.md 1 |
| B15 | START size fields swapped | dec1/vdec_ref.c sends data[0]=buffer size, data[1]=AU length (AV1 fails) | fixed in n2ap_vdec; vdec_ref reported | board 19:03, 01:25 |
| B16 | AV1 after VP9 fails in one vcodecd (host) | shim ion table 64 slots | proposed 0002-shim-ion256.patch (not landed) | board 01:25 |
| B17 | `0x1300` register window too small | vpud maps VENC with 0x3000 | window 0x3000 | p031x audit 1 |

---------------------------------------------------------------------------------------------------------------------------------

## 5. Known limitations and open items

Limitations (by design or measured):
- **One session at a time** in vcodecd; encode and decode exclusive (BUSY). VT does not retry a decoder StartSession. Since prod4 ([HOST], section 7) the decoder falls back to Apple's software decoder inside StartSession instead of failing.
- Encoder width max 3840 (vendor caps): vcodecd refuses 4096 since 00:53 (it worked once in vc24 before the refusal was added).
- HEVC encode: Main only. 10-bit decode outputs MT2T (MT2110T), which n2_mm21 and the protocol cannot carry yet.
- Synchronous per-picture requests; no pipelining between frames in the daemon.
- Polled waits (500 us msleep up to q10jh; 0314 adaptive spin in q10ji, not booted); ~500 ioctls per decoded AU with `--dec-wc alt`.
- 624 MHz runs on an unmanaged vcore OPP (no DVFSRC); worked in vc25 for 30-60 frame sessions [PHONE], long-run stability unknown.
- VENC core 1 / larb8 never exercised.
- Relay client identity is per process (an encode and a decode in mediaserverd at once could take each other's replies).
- vcodecd.log grows without bound under KeepAlive (fixed in prod4 [HOST]: 4 MiB cap + .1, section 7).

Open items (in rough priority):
1. **Client-side MM21 conversion at 4K** (8.8 ms/picture, vc27): the VT decoder should hand MM21 to the GPU or detile in fewer passes; otherwise it bounds 4K playback.
2. **jp1-jpeg** (`/dev/redwood-jpeg` user-space matrix up to 4032x3024, q10jg/q10jf): running at 01:36.
3. **HEVC reopen on the phone** with the heap fix (vc27-reopen = payload-vcodec-reopen, `--keepalive --no-restart`; pending), then drop restart-on-switch.
4. **Camera integration** (cam1-prod: payload-vcodec-prod, claim-hw + hwdec on): never run in Camera.
5. **Async / pipelining**, `--venc-keep-power` on the phone (payload-vcodec-fastkp staged).
6. **Real interrupts**: assessed, not implemented (one handler per CPU, storm risk); revisit only if waits dominate.
7. **q10ji** (0314 wait + 0315 JPEG decode kernel path): build done, never booted; boot spare 2048 B.
8. **JPEG decode**: J04 words + 0315 [HOST]; phone order E0/E1 probe -> E2 -> E3 -> E4.
9. **HEIC via hardware HEVC**: step 1 = hm1-heic measurement boot (staged, not run), then tile or HEIF-mode path (HEIC_HW_PLAN.md).
10. **Other formats** (VP9, AV1, VP8, MPEG-1/2/4, H.263 decode): vc7-fmt staged; needs 0001-fmt-decoders.patch + ion256 landed.
11. JPEG stills through CMPhoto: hook `-[BWCMPhotoCompressionSession addMainImage:...]` (JPEG_INTEGRATION.md), not built.

---------------------------------------------------------------------------------------------------------------------------------

## 6. File map

| Path (under `gpu/`) | What |
|---|---|
| `hwcodec/HARDWARE_CODECS.md` | this file |
| `hwcodec/PLAN.md` | original plan, Android-side analysis, architecture choice, milestones M1-M6 |
| `hwcodec/STATUS.md` | M1 (JPEG, q10hw1) status, 11 Nov 11:00 |
| `hwcodec/CRITICAL_PATH.md` | first-encode critical path and go/no-go (11 Nov 19:25) |
| `hwcodec/FIRST_PHONE_TEST.md`, `DECODE_PHONE_TEST.md` | encode / decode phone procedures, expected lines, failure modes, run analyses (0b-0d) |
| `hwcodec/CAMERA_INTEGRATION.md` | Camera/AVFoundation design, prod payloads, test plan |
| `hwcodec/JPEG_PHONE_TEST.md`, `JPEG_INTEGRATION.md`, `HEIC_HW_PLAN.md` | JPEG user-space test, CMPhoto hook analysis, HEIC plan |
| `hwcodec/facts/` | VENC_PROTOCOL, HEVC_ENC_PROTOCOL, VDEC_PROTOCOL, VDEC_REGISTERS, HEVC_DEC_PROTOCOL/NOTES, FORMATS, MM21_LAYOUT, VT_DECODER_ABI, vcu_usage |
| `hwcodec/kern/p031x/` | patches 0310-0315, STATUS.md (images q10jy..q10ji, audit, interrupts assessment), tests |
| `hwcodec/kern/bsd/dev/`, `hwcodec/include/` | live kernel sources (K01 jpeg, K02 vcodec, IK relay) and ABI headers (redwood_vcodec.h, redwood_jpeg(dec).h, n2vcd_proto.h, n2memlimit.h) |
| `hwcodec/daemon/` | vcodecd.c, n2vcd_srv.c, dec/ (DEC05 service), selftest |
| `hwcodec/ap/` | n2ap_venc.c, n2ap_vdec.c (DEC01), n2vc_dma.c, IPI headers |
| `hwcodec/vcu/` | n2vcu.c (V40: /dev/vcu emulation, GCE list execution) |
| `hwcodec/emu/`, `hwcodec/loader/`, `hwcodec/shim/` | AArch32 interpreter, ELF loader, bionic/ion/heap shim |
| `hwcodec/client/` | n2vcd client library, transport, dec/ (DEC05 client) |
| `hwcodec/vt/` | P04 VideoToolbox encoder objects (n2hwenc_vt.c), vt/dec (DEC06 Slim.videodecoder, DEC08 ABI tools) |
| `hwcodec/mm21/` | MM21 -> NV12 converter (NEON) |
| `hwcodec/ios/` | iOS builds (build.sh, build_plugins.sh), n2vcd_probe.c, n2vcd_dprobe.c |
| `hwcodec/decint/` | compare_dec.py, fnv_frames.c, run_sim.sh, test streams |
| `hwcodec/jpeg/`, `hwcodec/jdec/`, `hwcodec/heicm/`, `hwcodec/fmt/`, `hwcodec/cam/` | JPEG library + phone test, JPEG decode words, HEIC trace proxy, extra formats, Camera checker |
| `hwcodec/board/post.py` | coordination board (`read --limit N`) |
| `build/apps-20261104/` | stage-vcodec.sh, rehearse-vcodec.sh, guest-vcodec(-undo).sh, vcodec_check.py, payload-vcodec-*, owned-vcodec-*.json, `vcodec-fetch/<run>/` |
| `build/autocycle/txn.sh`, `build/queue_run.sh`, `build/autotest/phone.py` | transaction, run queue, phone control |
| `build/cc-txn-<tag>/events`, `build/autocycle/runs/<tag>/` | per-run transaction events and live console capture |
| `build/device-mmu-q10j?-candidate/`, `build/build_q10j{g,h,i}.sh`, `build/recipes-q10jf/` | images and recipes |
| `build/cam-vendor-20261109/vfs` | the phone's own vendor partition extract (source of the five guest objects; private, never committed) |

---------------------------------------------------------------------------------------------------------------------------------

## 7. Robustness update: decoder software fallback, BUSY handling, vcodecd.log cap (12 Nov 2026, opus-robust)

All **[HOST]**. Nothing was flashed or run on the phone. The new payload is `gpu/build/apps-20261104/payload-vcodec-prod4`
(tag `big-prod4`). It is prod3 with the same gates, job arguments and switches: only `vcodecd` and `Slim.videodecoder` change.

### 7.1 What changed

**(1) Slim.videodecoder: StartSession no longer kills playback** (`vt/dec/n2vtd_obj.c`, `n2vtd_client_dec05.c`, `n2vtd_client.h`,
`n2vtd_apple.h`, `n2vtd_vt.h`)
- The problem: VideoToolbox calls a decoder's StartSession once. A failure there fails the whole `VTDecompressionSession`, and
  unlike the encoder VT does not try the next candidate (vt/dec/ABI.md section 5, [V]). Before this change, playback failed in
  each of these cases: vcodecd gone since the factory's 2 s probe, BUSY (an encoder session or a second decode session), the
  restart-on-switch answer, OPEN or DEC_HEADER refused for this size or profile. 10-bit, 4:2:2 and interlaced streams failed on
  every frame.
- The fix is an in-object software fallback. When the hardware session cannot be opened in StartSession, the object calls
  `VTSelectAndCreateVideoDecoderInstance(codecType, ...)`. This is the exported selector wrapper @0x194cc73d0. It was
  disassembled on 12 Nov: it calls the internal selector @0x194cc7408 with start index 0, and that selector calls each factory as
  `(codecType, allocator, out)` and moves on after any error. While the call runs, this image's two factories refuse **on that
  thread only** (`n2vtd_vt_fallback_in_progress`, an atomic owner thread). The selector therefore returns the next decoder, which
  is Apple's H264SW for avc1 and VCPHEVC for hvc1. The object then:
  - replays the base-class SetProperty calls that VT made before StartSession (VT makes two of them);
  - starts that decoder on the **same** `VTVideoDecoderSession`;
  - forwards every slot to it from then on: decodeFrame, finishDelayedFrames, canAcceptFormatDescription,
    copySupportedPropertyDictionary, setProperties, copySerializableProperties, base copyProperty and setProperty, invalidate,
    and the release in finalize.

  The inner decoder emits VT's frame tokens itself. If no other decoder starts, StartSession returns the hardware error as before.
  10-bit, 4:2:2 and interlaced streams now go to the software decoder at StartSession, without the daemon. The old per-frame
  refusal is kept only when no software decoder exists.
- **BUSY/RESTART retry**: the DEC05 adapter now reports BUSY as `N2VTD_CLIENT_E_BUSY`, and BUSY + `N2VCD_DETAIL_RESTART` as
  `N2VTD_CLIENT_E_RESTART`. StartSession retries connect + OPEN every 100 ms for up to 1000 ms. This is P04's open_with_retry
  pattern; the encoder uses 600 ms. After a BUSY or RESTART answer, failed connects are retried too, because the daemon is
  between processes. A daemon that is simply absent (connect fails on the first try) is not retried, so the fallback starts at once.
- **Second decode session while one is open** (two players, a thumbnail during playback): the second session gets BUSY. After
  the retry budget it runs on the software decoder, and the first session is not disturbed. Every request/reply pair of the
  DEC05 adapter is now serialised by one process-wide lock, because the relay identifies clients by **process**: before, two
  sessions in mediaserverd could read each other's replies and break both connections. `buffer_rw` (a DMA ioctl with no reply)
  stays outside the lock. The encoder is another image with its own lock, so **an encode and a decode in the same process can
  still collide** (CAMERA_INTEGRATION.md risk 2; unchanged).
- **CLOSE after a failed StartSession**: if StartSession failed after OPEN_OK (DEC_HEADER refused, pool, attributes), the object
  used to disconnect without CLOSE. The relay gives the daemon no close notice, so vcodecd held that session, and answered BUSY
  to everyone, until its 10 s idle drop. CLOSE is now sent whenever OPEN succeeded.
- New imports (all weak or libSystem): `_VTSelectAndCreateVideoDecoderInstance`, `_CMBaseObjectGetVTable`, `_pthread_self`,
  `_pthread_equal`, `_nanosleep`. They are listed in vt/dec/symbols.txt, and the TBDs in build_plugins.sh and vt/dec/run_tests.sh
  have them. If either Apple symbol were missing, only the fallback would be disabled.

**(2) vcodecd.log is capped** (`daemon/n2vcd_logcap.h` new, `daemon/vcodecd.c`)
- launchd opens `/private/var/db/nord2-vcodec/vcodecd.log` as the job's StandardOutPath and StandardErrorPath
  (guest-vcodec.sh). The daemon writes only to fd 2.
- At start, if fd 2 **is** that file (same dev/inode; a terminal or pipe is left alone) and the file is larger than 4 MiB, the
  daemon renames it to `vcodecd.log.1` (replacing the old one) and continues in a fresh file through dup2 on fd 1 and fd 2.
- While running, every line is counted with one atomic add, and the same rotation happens at 4 MiB. Disk use is bounded at about
  8 MiB. If a rotation fails (read-only or full disk), the daemon stops trying and keeps logging.
- New options `--log-file PATH` and `--log-max BYTES` (0 = no cap). The default on iOS is the phone path above, so **the launchd
  job arguments are unchanged**.
- Hot path: the only per-picture decode line was `dec: START n done ...`, about 200 B per picture. It went to vcodecd.log **and**
  /dev/console, so 30-60 synchronous writes per second during playback and about 27 MB per hour of 4K. That line is now limited
  to the first 16 STARTs, then every 60th, plus any START slower than 40 ms. The encoder's per-frame lines were already every
  30th frame (phases every 10th). The hardware-call log is bounded (first 64), and so is the n2ap trace (first 96).

### 7.2 Host evidence

| Check | Result |
|---|---|
| `N2HW_DEC06_OUT=/tmp/n2hw-DEC06 bash gpu/hwcodec/vt/dec/run_tests.sh` | `N2TEST DEC06 PASS 2880/2880` (was 2553). New `test/t_fallback.c`: **T_FALLBACK PASS 109** each in ASan+UBSan -O1, -O2 and **TSan**. T_OBJ 166 unchanged. iOS link and import audit: 72 undefined, all covered |
| t_fallback covers | the hardware path is untouched when the daemon works (no selector call, UsingHardwareAccelerated = true). OPEN refused leads to the software decoder: same session, every token emitted once, Finish/CanAccept/Set/CopyProperty forwarded, the 2 pre-StartSession properties replayed, our factory refused inside the selector (no recursion), no leaked objects. Daemon gone after the probe: software in < 300 ms with 1 connect. BUSY x2 is retried and gives the hardware (3 OPENs, 150-1000 ms). BUSY forever gives software after the budget (timed 300-900 ms with a 300 ms budget). RESTART, then 2 refused connects, then the hardware. A second session with an exclusive daemon goes to software while the first keeps decoding, and a third session gets the hardware again. DEC_HEADER refused: CLOSE sent exactly once. 10-bit HEVC goes to software with 0 OPENs. Software decoder failing too returns the hardware error with no leaks. Two threads (hardware decoding while the other thread falls back) |
| `bash gpu/hwcodec/daemon/test/run_logcap.sh` (new) | `N2TEST LOGCAP PASS 115/115`: unit test (t_logcap.c) 34/34 in ASan+UBSan, -O2 and TSan (start rotation, runtime rotation with no torn or reordered lines, 4 writer threads, read-only directory). Real test-build vcodecd: a 5 MiB log is rotated at start (.1 = 5242887 B, the daemon serves into the new file); `--log-max 256` rotates while running; `--log-max 0` and a pipe on fd 2 leave the file alone |
| `bash gpu/hwcodec/daemon/test/run_tests.sh` (IP, unchanged suite) | `N2TEST IP PASS 1088/1088` (ASan, -O2, TSan, selftest, real vhost 33/33) |
| `OUT=/tmp/n2r-dec bash gpu/hwcodec/decint/run_sim.sh` (vendor decoder under the interpreter) | `N2TEST DECINT-SIM PASS`; 16 `dec: START` lines logged |
| iOS builds | `ios/build.sh` gives IOS TOOLS OK, and `ios/build_plugins.sh` gives PLUGINS OK, both into `/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/rb/{ios,plugins}` |
| Plug-in build is reproducible | Building the original sources (a copy of the tree with the pre-change files) gives exactly prod3's 91bd53af and 61c1bed1. The prod4 encoder is byte-identical (91bd53af) |
| What else is in the new vcodecd | Object comparison against opus-cam's prod build (`/tmp/n2hw-opus-cam/ios`): all `vc_*.o` are identical except `vc_vcodecd.o` (this change). In IV's archive only `n2shim_heap.o` differs: the **B12 heap-zero fix** (shim/n2shim_heap.c, 01:26). It is in the tree but was not in f974e33f, which was built at 01:13-01:22. B12 ran on the phone in vc27-reopen [PHONE]. Linked binaries also differ by the embedded build path |
| Staging | `stage-vcodec.sh big-prod4 --prod --tools RB/ios --vcodecd RB/ios/vcodecd --encoder-plugin RB/plugins/Slim.videoencoder --decoder-plugin RB/plugins/Slim.videodecoder --payload $PWD/payload-vcodec-prod4 --video-on claim-hw --video-on hwdec --video-off hwenc-sync --video-off trace --video-off probe --video-off camera-keephevc`. plan.json differs from prod3 only in tag, the two hashes and the purpose text |
| Rehearsal | `rehearse-vcodec.sh payload-vcodec-prod4`: **REHEARSAL_OK**. Chained heic-measure, jpegdec7, prod3, **prod4 over prod3**, undo (`scratchpad/rehearse4.sh`): PROD STATE OK after prod3 and after prod4 (job `vcodecd --gate ... --hw-timeout-ms 6000 --restart-on-switch --decoder`, gates and switches `nord2-vcodec-run, nord2-video-claim-hw, nord2-video-hwdec`), PROD4 FILES OK, REHEARSAL2_OK after undo |

payload-vcodec-prod4 sha256:

| File | sha256 |
|---|---|
| plan.json (tag `big-prod4`) | `7a9b6c3bd7e47d4296630fd6fd3319312bde470f9f2e0a9161e9607e77223ba3` |
| vcodecd | `8fbbd9ab8e10769e843aa54e6549c4950f0562c9d8ed59320dbadecfb9dc6e93` |
| Slim.videoencoder (= prod3) | `91bd53afc41bc5726eb7f656790331e3cd7e0aaccd1906c282f4216d3f0e08fc` |
| Slim.videodecoder | `75caf5738005390548c3bee64633ee84d7bf2547edd8046c5bdd936ff85a3213` |
| vendor/* (= prod3, hard links) | vpud `82afe722...`, libvpud_vcodec `07130d2a...`, libvcodecdrv `5e69ccfc...`, libvcodec_utility `e26673ed...`, libvcodec_oal `cd5c5b6c...` |

Install like prod3 (BIG_BUILD.md step 3), with `PAYLOAD=payload-vcodec-prod4 GUEST=guest-vcodec.sh
OWNED=$W/gpu/build/apps-20261104/owned-vcodec-jpegdec.json`. It has the same file set as prod3, so it installs over prod3 (rehearsed).
Undo is the same as prod3's.

### 7.3 Must be tested on the phone (none of this has run there)

1. **Playback of a camera movie in Photos** (hardware path). Open a recorded .MOV in Photos and play it. Expected: the
   `N2VIDEO`/n2vtd console lines `session start 31637661 1920x1080 ... MM21` and `first picture`; vcodecd.log shows `dec: session N
   open` and at most 16 + 1/60 START lines. The picture is correct. Still open: the Camera Roll thumbnails that did not appear in cam2.
2. **Daemon absent, software decode**: remove the gate (`nord2-vcodec-run` off: `payload-vcodec-prod-nodaemon` style, or kill
   vcodecd between the probe and the playback). The same movie must play. Console: either `avc1 decoder not offered: daemon
   unavailable` (factory, VT picks H264SW) or `hardware unusable (daemon absent, ...): session runs on the software decoder`
   (in-object fallback).
3. **Busy, software decode**: play a movie **while recording** (the encoder holds vcodecd) or open two players. Expected: `hardware
   unusable (daemon busy, -12913): session runs on the software decoder` after about 1 s, and the recording is not disturbed.
   Watch for the same-process collision risk if mediaserverd hosts both the encoder and the decoder.
4. **Unknowns of the fallback mechanism [I]**:
   - that `VTSelectAndCreateVideoDecoderInstance` from inside our StartSession, in mediaserverd, returns H264SW/VCPHEVC (they
     register with rating 0, not hardware);
   - that H264SW/VCPHEVC accept starting on a session that VT created for our object (they call
     `VTDecoderSessionSetPixelBufferAttributes`/`EmitDecodedFrame` on it);
   - that the forwarded `UsingHardwareAcceleratedVideoDecoder = false` causes no trouble.

   A crash or an error here shows up as a failed playback with the `session runs on the software decoder` line just before it.
   The rollback is `payload-vcodec-prod3` (or `-prod3-nohwdec`).
5. **10-bit HEVC** (if available): it must now play through VCPHEVC instead of failing on every frame.
6. **vcodecd.log**: after a few boots it must stay under 4 MiB, with `vcodecd.log.1` beside it. The first boot with a large old
   log shows `N2VCD log rotated: previous N bytes ...` and `log cap 4194304 bytes on /private/var/db/nord2-vcodec/vcodecd.log:
   rotated N bytes at start`. **vcodec_check.py fetch** and the "lines after the previous fetch's line count" rule (top of this
   file) assume an append-only log. After a rotation, the older lines are in `.1` or gone.
7. Not changed and still true: the hardware session has no mid-stream fallback (a daemon failure during playback fails the
   frames). The encode and decode collision across the two images in one process is not fixed. vcodecd still runs one session
   at a time.

---------------------------------------------------------------------------------------------------------------------------------

## 8. HEVC hardware decode through VideoToolbox: lazy DEC_HEADER (12 Nov 2026, dp4; payload-vcodec-prod6)

All **[HOST]**. Nothing was flashed or run on the phone. Before this change: prod5's decoder `3874f1fe` decodes H.264 through VT
on the phone (**[PHONE]** dp3, md5 8/8). HEVC fell back to Apple's software decoder, `DEC_HEADER/pool refused (-12911)`: the
vendor refuses a DEC_HEADER that holds only VPS/SPS/PPS (`eVDecDrvInit fail (6)`). Host decint shows that VPS/SPS/PPS **plus
the first frame** in DEC_HEADER returns the geometry and decodes every picture.

### 8.1 What changed (only `vt/dec/n2vtd_obj.c`, `vt/dec/n2vtd_vt.h`; the prod5 sources are kept in `scratchpad/dp4/orig-prod5/`)

- **hvc1 configures lazily.** StartSession does connect + OPEN, with the same BUSY/RESTART retry and the same software fallback
  as before. It then sends the pixel buffer attributes (420v/420f, the **SPS display size**, IOSurface) and returns without a
  DEC_HEADER. Log: `session start 31637668 WxH (SPS) video-range: DEC_HEADER waits for the first IRAP`.
- **First DecodeFrame.** If the sample has no IRAP NAL (types 16..23), it is emitted with `FrameDropped` (status 0). There is no
  DEC_HEADER and no INPUT, and the log says `first sample is not an IRAP: dropped until one arrives`. At most 30 samples are
  dropped this way. The first IRAP is converted to Annex B, and the object sends **DEC_HEADER = VPS/SPS/PPS (from the hvcC) + that
  access unit** and registers the pool. The **same AU** then goes in as the first DEC_INPUT, with the parameter sets in front:
  prod5's `prepend_params` is unchanged. This is the decint-proven `hdrau` pattern. Log: `session configured at the first IRAP:
  31637668 WxH stride S buf_h B dpb N, output WxH`.
- **Geometry (ABI).** VT reads the output size from the attributes; the pool is rebuilt lazily after each SetPixelBufferAttributes
  (facts/VT_DECODER_ABI.md). The attributes therefore exist from StartSession on, as for H.264. At the header, the attributes are
  kept when the vendor size equals the SPS size. They are also kept when the vendor reports up to 63 px more in either dimension
  (coded-size padding): the copy then crops to the SPS size (`vendor geometry ..., output stays ... (padding cropped)`). Any
  other difference means the vendor size wins and the attributes are sent again (`... replaces ... from the SPS`). If the SPS
  could not be read, the attributes are sent at the header. For H.264 nothing changed: configure at StartSession, and the
  attributes come from the vendor geometry.
- **Software handover at the first frame.** It happens when the header with the IRAP is refused, the pool fails, the IRAP does
  not fit `in_buf_bytes`, or 30 samples pass without an IRAP. The object then sends CLOSE, calls `start_software` (prod4's
  mechanism) on the **same** session with the retained format description, and replays the properties. These now include the
  ones set after StartSession and before that frame. The current frame is forwarded to the software decoder; its token is not
  emitted by us. From then on every slot is forwarded. No picture has been emitted at that point, so the handover happens before
  the first picture: the software decoder sets its own attributes and emits every token. If no software decoder starts, that
  frame is emitted with the error, later frames return `kVTVideoDecoderNotAvailableNowErr`, and nothing leaks.
- Other: FinishDelayedFrames before the header sends no FLUSH. A reconfigure after `changed` sends parameter sets + the current
  AU for HEVC. New counters: `headers_deferred`, `headers_with_slice`, `pre_irap_dropped`, `sw_handovers`. **No new imports**:
  the import list equals prod5's (checked with `llvm-nm -u`).

### 8.2 Host evidence

| Check | Result |
|---|---|
| `N2HW_DEC06_OUT=/tmp/n2hw-DEC06-dp4 bash gpu/hwcodec/vt/dec/run_tests.sh` | **`N2TEST DEC06 PASS 4020/4020`** (prod5: 2952). T_OBJ 414 and T_FALLBACK 241, each in ASan+UBSan -O1, -O2 and **TSan**; iOS link: 73 undefined, all covered |
| What the new tests cover | t_fakeclient_dec `vendor_rules` (already present: an HEVC DEC_HEADER without a slice fails E_HW, nothing is displayed until an INPUT carries an SPS). t_obj `test_hevc_lazy`: no DEC_HEADER or pool at StartSession, attributes 176x144 at StartSession, DEC_HEADER = parameter sets + IDR (exact bytes), first INPUT = parameter sets + IDR, 5/5 pictures pixel-checked. Three vendor geometries: equal, padded 176x152 (output 176x144 cropped) and 320x180 (attributes re-sent). t_fallback: HEVC under vendor_rules stays on the hardware with 5/5 (the old test asserted software); skip-to-IRAP; header refused at the first IRAP (handover: CLOSE once, 4/4 frames from the software decoder including the failed one, 3 properties replayed of which 1 was set after StartSession, no leaks); no IRAP within 30 samples (31st goes to software); handover with a failing software decoder (frame emitted with the error, then NotAvailableNow, no leaks) |
| Negative control | The same t_obj/t_fallback built against prod5's `n2vtd_obj.c`: T_FALLBACK FAIL 38/241, T_OBJ FAIL. The tests detect the dp3 behaviour |
| **Real object + real vendor** (`scratchpad/dp4/hostrun/plugobj.c`, `runobj.sh`). This is the production `n2vtd_obj.c` + `n2vtd_client_dec05.c` + `client/dec/n2vcd_dec.c` driven through the DEC06 VT stand-in, against a host vcodecd running the phone's vendor objects under the interpreter (`--fake-vdec-hevc`). The only seam change is the socket path | HEVC 176x144 (the dp1/dp3 phone stream): `PLUGOBJ hw=1 images=8/8`, the daemon logs `header 176x144 stride=192 buf_h=192 dpb=12`. **HEVC 1920x1080** (`decint/streams/t1080-hevc-main-10f.h265`): **10/10**, `header 1920x1080 stride=1920 buf_h=1088`, output 1920x1080. GOP-4 stream starting at sample 1: 3 dropped, configured at the IRAP (pts 4), 8/8 after it. **H.264 176x144 regression: 8/8** (configured at StartSession as in prod5) |
| dp1 protocol tools (`scratchpad/dp1/plugpat.c`, `runpat.sh`, mode `hdrau` = the new pattern) | HEVC 176x144 `HEADER ok 176x144` **8/8**; HEVC 1080p **10/10**. Old pattern (`withps`, parameter sets only): `HEADER hardware detail=1` (= dp3) |
| Fake hardware | Pixels are not checked against real hardware output (the fake VDEC writes none). The pixel path is the unchanged prod5 path, which is bit-exact on the phone for H.264 (dp3) |
| iOS build | `ios/build_plugins.sh` gives PLUGINS OK into `scratchpad/dp4/plugins`: encoder `91bd53af` (= prod5), decoder **`9f23d1e5`**. `ios/build.sh` gives IOS TOOLS OK into `scratchpad/dp4/ios`: vcodecd `ae1be6c9`, all 49 objects byte-identical to prod4/prod5's build (`scratchpad/rb/ios`), so only the embedded build path differs. The payloads therefore keep prod5's vcodecd **`8fbbd9ab`** |
| Staging | `stage-vcodec.sh big-prod6 --prod --tools DP4/ios --vcodecd DP4/vcd/vcodecd(=8fbbd9ab) --encoder-plugin DP4/plugins/Slim.videoencoder --decoder-plugin DP4/plugins/Slim.videodecoder --video-on claim-hw --video-on hwdec --video-off hwenc-sync --video-off trace --video-off probe --video-off camera-keephevc --payload $PWD/payload-vcodec-prod6`. The probe variant is the same with `--video-on probe` instead of `--video-off probe` (tag `dp4-probe`). The nohwdec variant uses prod5's own plug-ins with `--video-off hwdec` (tag `big-prod5-nohwdec`). Each plan.json differs from prod5's only in tag, purpose, decoder hash (prod6) and switches (probe / nohwdec) |
| Rehearsal | `rehearse-vcodec.sh`: **REHEARSAL_OK** for payload-vcodec-prod6, -prod6-probe and -prod5-nohwdec. The prod6 log equals prod5's except for the decoder hash and size. Chained (`scratchpad/rehearse5.sh`): heic-measure, jpegdec7, prod5 (PROD STATE OK), **prod6 over prod5** (PROD STATE OK, files OK: decoder 9f23d1e5), prod6-probe (switch probe on, idempotent), prod6 again (probe off), undo: **REHEARSAL2_OK** |

### 8.3 Payloads (new dirs in `gpu/build/apps-20261104`; owned list `owned-vcodec-jpegdec.json`, same file set as prod5)

| Payload | plan.json sha256 | Slim.videodecoder | Switches on |
|---|---|---|---|
| `payload-vcodec-prod6` (tag big-prod6) | `4b2b9a2d548877dae36bddfc78071d24a2f8704e6805a16bbe5277c38cd3f441` | `9f23d1e5879fcaca3fc4fbbc2955e03c6f20b2fdf0b1d86d6fac0e0720b315b3` | claim-hw, hwdec |
| `payload-vcodec-prod6-probe` (tag dp4-probe) | `a030d074a9be69d38d88589c73be926ec2c7ff8014c6be509788e989c31fccd7` | `9f23d1e5...` | claim-hw, hwdec, **probe** |
| `payload-vcodec-prod5-nohwdec` (tag big-prod5-nohwdec) | `d75639555670b5f4268b2d6f626aa3e2fd80be432f2581ce3cfb7962d4f54725` | `3874f1fe...` (= prod5) | claim-hw (hwdec **off**) |

In all three: vcodecd `8fbbd9ab8e10769e843aa54e6549c4950f0562c9d8ed59320dbadecfb9dc6e93`, Slim.videoencoder
`91bd53afc41bc5726eb7f656790331e3cd7e0aaccd1906c282f4216d3f0e08fc`, and vendor objects equal to prod5's (hard links).

### 8.4 Phone run dp4 (q10jk `36aca6cf8bddd242`, payload-vcodec-prod6-probe, `owned-vcodec-jpegdec.json`): what it should show

    PAYLOAD=payload-vcodec-prod6-probe GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-jpegdec.json \
      timeout 900 bash autocycle/txn.sh $IMG dp4 > autocycle/runs-dp4.out 2>&1
    python3 $W/gpu/hwcodec/cam/decode_probe_check.py autocycle/runs/dp4/ui-usb/xnu-live.log

- `vt.decode.avc1`: unchanged from dp3. `session start 31637661 176x144 stride 176 buf_h 160 MM21`, `first picture pts=0`,
  frames 8/8, md5 match, verdict HARDWARE.
- `vt.decode.hvc1` (**the new result**): `N2VDEC session start 31637668 176x144 (SPS) video-range: DEC_HEADER waits for the first
  IRAP`, then `session configured at the first IRAP: 31637668 176x144 stride 192 buf_h 192 dpb 12, output 176x144` and `first
  picture pts=0 176x144`. vcodecd shows `dec: session N open ... codec=0x31637668`, then `header 176x144 stride=192 buf_h=192
  dpb=12 mm21` and 8 STARTs. The probe line should read frames=8/8 with md5 match. There must be **no** `hardware unusable`
  line and no `DEC_HEADER/pool refused`. The checker should say HARDWARE for both cases and exit 0.
- If it says `hardware unusable (DEC_HEADER/pool refused at the first IRAP, ...)`: the vendor refused even the header with the
  slice on the phone. Playback still works on VCPHEVC (the md5 must still match), so this is no regression from prod5.
- Production after dp4: `payload-vcodec-prod6` (the same files without the probe switch) replaces prod5 as BIG_BUILD step 3. The
  rollback for decoder trouble is `payload-vcodec-prod5-nohwdec` (prod5's vcodecd, no hardware decoder registered) or
  `payload-vcodec-prod5` (H.264 on the hardware, HEVC on software).

### 8.5 Verified vs untested

- [HOST] verified: the lazy configure against the phone's own vendor objects (176x144 and 1080p HEVC, all pictures returned on
  the hardware path), the handover paths against the fake, sanitizers, iOS link and imports, staging and rehearsals.
- Untested on the phone:
  - that the phone's vendor accepts parameter sets + IRAP in DEC_HEADER as the interpreter run does (the same vendor binaries;
    only the hardware is faked on the host);
  - that HEVC pixels are bit-exact through VT (dp4's md5);
  - that the handover inside DecodeFrame works with Apple's VCPHEVC in mediaserverd: the StartSession fallback of prod4 has
    never run on the phone either;
  - real movies: Photos/AVPlayer with an IRAP that is a CRA with RASL pictures, or a seek that lands on a non-IRAP sample (those
    samples are dropped and the session starts at the next IRAP);
  - 1080p and 4K HEVC playback through VT. A 4K IRAP may approach 1 MB [I]. `in_buf_bytes` is 2 MiB, and parameter sets + IRAP
    above that go to software.
- Unchanged limits: no mid-stream fallback after the first picture; one vcodecd session at a time; the encode/decode lock is
  still split across the two images (section 7.3 item 7).
