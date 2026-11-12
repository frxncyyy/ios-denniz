# P04 — VideoToolbox hardware H.264/HEVC encoder objects backed by vcodecd

Owner: hw-p04. Files: `gpu/hwcodec/vt/**` only. Host-tested only; nothing was run on the phone, nothing published, no
commit. The suite does not touch `gpu/video`, `client`, `daemon` or the kernel; it reads `gpu/video/include`,
`gpu/video/fake`, `gpu/video/sdk/sysinc` and P03's final `client/n2vcd_client.h` from the tree.

## What is implemented

| File | What |
|---|---|
| `vt/annexb.c`, `vt/n2hwenc_annexb.h` | Annex B scanning (3- and 4-byte start codes, trailing-zero trimming, NAL/IRAP/parameter-set classification for AVC and HEVC), in-place Annex B -> length-prefixed conversion (parameter sets optionally stripped), and the avcC (ISO/IEC 14496-15 5.2.4.1.1, including the High-profile chroma/bit-depth extension) and hvcC (8.3.3.1.1, profile_tier_level copied from the SPS, arrays VPS/SPS/PPS) record builders. No third-party source. |
| `vt/n2hwenc_vt.c` | One `n2vt_abi.h` CMBaseObject subclass for `'avc1'` and `'hvc1'`/`'hev1'`: `startSession` (validate 16..4096 x 16..2304 even, announce 420v/420f + size + IOSurface attributes, connect to vcodecd and OPEN with the session dimensions and the properties known at that point), `encodeFrame` (lock the CVPixelBuffer, NV12 planes/strides -> the client, strip parameter sets, convert to AVCC, build the format description from the first key frame's SPS/PPS(/VPS), emit one CMSampleBuffer with NotSync/DependsOnOthers exactly as `n2avc_vt.c` and the frame token), `completeFrames` (0; synchronous), properties (AverageBitRate, ExpectedFrameRate, MaxKeyFrameInterval[Duration], RealTime, ProfileLevel, plus the n2avc_vt.c key set; bitrate/fps go to ENC_SET, structural properties before the first picture reopen the session once), `copySupportedPropertyDictionary`, `invalidate` (idempotent teardown) and `finalize`. Falls back with `kVTVideoEncoderNotAvailableNowErr` (-12915) when the daemon cannot be reached or OPEN is refused. |
| `vt/n2hwenc_client.c`, `vt/n2hwenc_client.h` | The function-pointer seam between the object and P03. The default table maps 1:1 on to P03's final `client/n2vcd_client.h` (`n2vcd_connect_path`, `n2vcd_status_errno`, `n2vcd_enc_open`, `n2vcd_session_{in,out}_bytes`, `n2vcd_enc_frame`, `n2vcd_enc_set`, `n2vcd_close`, `n2vcd_disconnect`), all weak. A link without `client/**` works: every call reports absent and StartSession returns -12915. |
| `vt/n2hwenc_register.c`, `vt/n2hwenc_vt.h` | `n2hwavc_vt_create` / `n2hwhevc_vt_create` with the frozen factory signature and `n2hwenc_register_all()` (ImplementationID ours, IsHardwareAccelerated=true, Rating=N2HWENC_RATING=100; registers avc1, hvc1 and hev1 when VideoToolbox is there). `n2hwenc_vt_available()` is the optional HELLO probe for a registration hook. The trace hook carries the only diagnostics (the library never prints). |
| `vt/symbols.txt` | Every framework/libSystem symbol the four objects import, plus the two HEVC CoreMedia calls proposed for `n2_apple.h`. |
| `vt/test/` | The host suite: `mp4_extract.py` (an independent MP4 parser: stsd avcC/hvcC, stsz/stsc/stco/co64/stss -> per-frame length-prefixed sample and its Annex B twin), `t_annexb.c`, `t_vt.c`, `t_fakeclient.c`/`.h` (canned vcodecd playback), `t_support.c`/`.h` (checks, vectors, test-local HEVC format-description calls), `t_link.c` (links the default seam against P03's real client library and runs it on the host, where the availability probe must answer 0), `run_tests.sh`. |

## Tested (host, today)

```
$ bash gpu/hwcodec/vt/test/run_tests.sh
== P04: ffmpeg vectors (the independent oracle) ==
mp4_extract: .../avc-high.mp4 AVC 176x144 extradata=45 params=[7, 8] frames=25 keys=3 bytes=48800
mp4_extract: .../avc-base.mp4 AVC 176x144 extradata=38 params=[7, 8] frames=25 keys=3 bytes=52334
mp4_extract: .../hevc-main.mp4 HEVC 176x144 extradata=111 params=[32, 33, 34] frames=25 keys=3 bytes=47833
== P04: annexb.c, ASan+UBSan ==
T_ANNEXB PASS 725
== P04: annexb.c, -O2 ==
T_ANNEXB PASS 725
== P04: encoder object, ASan+UBSan ==
T_VT PASS 1193
== P04: encoder object, -O2 ==
T_VT PASS 1193
== P04: encoder object, TSan (two sessions on two threads) ==
T_VT PASS 1193
== P04: link and run against P03's real client library (default seam, no fake client) ==
T_LINK registered=3 available=0
== P04: arm64-apple-ios16.0 compile check (build_ios.sh flags) ==
P04 ios compile: 4/4 objects for arm64-apple-ios16.0
P04 checks: 5031 (annexb x2 builds, encoder object x3 builds, P03 link 2)
N2TEST P04 PASS 5031/5031
```
Wall time about 37 s, all products under `/tmp/n2hw-P04/`.

What those checks establish, concretely:
- **avcC/hvcC equal ffmpeg's extradata byte for byte** for a High-profile AVC stream (with the High extension), a
  Baseline stream (without it) and an HEVC Main stream. The expected bytes are the `avcC`/`hvcC` boxes ffmpeg wrote into
  the MP4, copied out by `mp4_extract.py`; nothing is computed by `annexb.c` for the comparison.
- **Every access unit converts to exactly the length-prefixed sample ffmpeg wrote for that picture** (25 frames per
  stream, three key frames each), both with the parameter sets absent and with the vcodecd-style prefix prepended and
  stripped again.
- Literal unit tests (hand-written from the standards) cover 3/4-byte start codes, trailing zeros, the 256-NAL limit,
  malformed buffers, the capacity bound, the High-profile extension, and a hand-built HEVC SPS whose hvcC must match a
  literal record.
- The encoder object, against `gpu/video/fake` and the canned client: factory rejection of the wrong codec type; the
  announced pixel-buffer attributes (420v and 420f, width/height, empty IOSurface dict); one emit per submitted frame
  with the frame token, status 0, size and payload equal to the vector's sample, pts/duration/dts, NotSync and
  DependsOnOthers false on key frames and true otherwise; the format description's parameter sets (H.264 SPS/PPS, HEVC
  VPS/SPS/PPS) and NAL length size 4; the property mapping (AverageBitRate/ExpectedFrameRate -> ENC_SET; gop/ProfileLevel/
  RealTime before the first picture -> one reopen with gop 12, profile 100, level 41, flags 1; structural changes after the
  first picture accepted without a restart); read-only `UsingHardwareAcceleratedVideoEncoder` = true and
  `NumberOfPendingFrames` = 0; unknown keys kept, wrong types refused, the supported-property dictionary's types and
  read/write status; bad picture (y420, wrong size) -> one -12902 emit and the session still usable; an injected client
  failure -> one -12912 emit and the session fatal; missing parameter sets -> -12912; invalidate is idempotent and later
  frames return -12903; no fake objects leak; two sessions (AVC and HEVC) on two threads are TSan-clean; the default
  client with no P03 links returns -12915 at StartSession; registration produces avc1/hvc1/hev1 with the hardware flag and
  Rating 100; and the four objects link against P03's real `client/n2vcd_client.c`/`n2vcd_transport.c`/`n2vc_hw_dev.c`
  with no undefined weak reference, with `n2hwenc_vt_available()` answering 0 on the host where no relay exists.

## Not done / not verified

- **No iOS link yet.** The four objects compile for `arm64-apple-ios16.0` with `build_ios.sh`'s exact flags (including
  the gpu/video sysinc stubs), but hw-ip's `build_ios.sh` does not list them yet, so "compiled for iOS" is the claim,
  not "linked into Slim.videoencoder" and not "run on the phone". The exact compile lines are posted to hw-ip.
- **Registration hook not applied.** `gpu/video/vt/n2vt_shim.c` owns `SLIMEncoderRegister()` and is read-only for this
  package. The proposal (two lines: a weak extern for `n2hwenc_register_all` and its call) is posted to hw-ip/hw-lead;
  until it is applied the objects are never created.
- **The fallback error is [I], not [V].** `kVTVideoEncoderNotAvailableNowErr = -12915` is the documented "encoder not
  available now" status (VTErrors.h in the iPhoneOS16.5 SDK), and the gpu/video plan describes the selection loop as
  trying the next candidate when one cannot be created. I could not verify which statuses that loop retries on: the
  cache tools need an `allsyms.pkl` that is not in this tree and the local dsec header layout does not match. On a phone
  run, a missing vcodecd must show the software encoder still being selected; that is the measurement.
- **HEVC format description call is not in `n2_apple.h`.** The object calls the real
  `CMVideoFormatDescriptionCreateFromHEVCParameterSets`; the host suite supplies a test-local implementation. Proposed
  to hw-lead (also for `gpu/video/fake`). The symbols list has both names.
- **NV12 only.** `'y420'` (I420) pictures are refused with -12902; the VENC input is NV12 and converting in the plug-in
  would cost what the hardware saves. 420v and 420f both work (the range is fixed by the first picture).
- **No B-frame/reorder control.** The object emits one sample per submitted frame synchronously; it never reorders.
  `AllowFrameReordering` is accepted, as in n2avc_vt.c, without effect.
- **One encoder session per object; the daemon allows one at a time.** A second concurrent object gets E_BUSY from OPEN,
  which correctly becomes -12915 (software fallback) but means only one hardware encode session per process at a time.
  Not measured with the real daemon.
- **Property timing.** StartSession opens the stream at once; a structural property (gop/profile/level/RealTime) set after
  StartSession but before the first picture costs one close+reopen at that first picture. Measured on the fake client,
  not on vcodecd.
- **No phone test** of any kind.

## Open questions (on the board)

1. hw-lead: add the two HEVC CoreMedia calls to `n2_apple.h`/`fake_apple.c`, or keep the declarations in
   `vt/n2hwenc_vt.h`? (posted; no answer yet)
2. hw-lead/hw-ip: is -12915 the status the encoder-selection loop actually retries on, or is there a verified code?
3. hw-ip: the two-line registration hook and the four compile lines for `build_ios.sh` (posted).
4. hw-p03: the final client header is integrated in `vt/n2hwenc_client.c`; no further interface needs.

## Review

R2 accepted rev 1 (`N2TEST P04 PASS 5031/5031`) with the two findings as stated: F32 (-12915 retry set [I]) documented,
with the attempted cache-tool decode and the switch-file mitigation; F33 (build/registration lines) is hw-ip's. On R2's
request the registration gate was filed as an interface item to hw-ip and hw-lead (reuse `N2VT_SWITCH_CLAIM_HW` or a
dedicated `/private/var/db/nord2-video-hwenc`, one condition in `n2vt_shim.c`; `n2hwenc_register_all()` stays
unconditional). No code change in `vt/` followed from the review.

## opus-cam revision (12 Nov 2026, Camera integration) — supersedes the synchronous design above

Details, design facts and the phone procedure: `gpu/hwcodec/CAMERA_INTEGRATION.md`. In short: worker-thread pipeline (2
pictures in flight, emits deferred to VideoToolbox's own encodeFrame/completeFrames calls; VT facts [V-cache]), bounded
StartSession with BUSY/restart retries (600 ms; first-picture reopen 1.5 s), no ENC_SET before the first picture and a refused
ENC_SET is non-fatal, stride repack when the CVPixelBuffer pitch differs from align16(width), idle-reopen guard (8 s),
MaxFrameDelayCount 0 -> synchronous, switch `/private/var/db/nord2-video-hwenc-sync`, process-wide relay lock in the seam,
timeouts connect 400 ms / OPEN 1 s / frame 1 s. `bash gpu/hwcodec/vt/test/run_tests.sh` -> `N2TEST P04 PASS 8517/8517`.
Still host-only: not run in Camera.
