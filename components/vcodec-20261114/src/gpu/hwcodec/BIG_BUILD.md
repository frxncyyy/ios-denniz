# BIG_BUILD: the one final combined build (prepared 12 Nov 2026, ~05:00, host only)

Status: **prepared and checked on the host, NOT flashed, never run as a whole.** Revised 12 Nov 08:39: step 3 DEFAULT = **payload-vcodec-prod8** (hardware encode + hardware decode, bit-exact through VideoToolbox at real sizes, dr6); safe fallback prod5-nohwdec (software decode). prod5/6/7 superseded (wrong pictures). The user flashes it themselves.
Script: `gpu/build/big_build_chain.sh`. Tags: **[PHONE]** = seen on the phone (run named), **[HOST]** = host
check/rehearsal only, **[I]** = inferred.

    W=$NORD2_WORK

## 1. What the build is

| Part | Artifact | sha256 | Where it goes |
|---|---|---|---|
| Kernel + ADT + audio/camera bundle | `gpu/build/device-mmu-q10jk-candidate/ui-sched/nord2-ios-metal-q10jk-ui654fb60dvfs-cap3500-ram3776-sched.img` (33552384 B, boot spare 2048) | `36aca6cf8bddd2423f8c387c571f32f648f57addbb262b18eed46691044209e7` | `boot` partition (flashed by every txn) |
| (inside the image) audio.pkg = camera + audio bundle | `gpu/build/camera-userland-20261109/out/audio.pkg` = `gpu/build/autotest/pins/audio-q10jy.pkg` (253952 B) | `1aea9991c88a2d71440768d8256e9f501647339a49d98099f1bc4e39c199131a` | ADT `/chosen nord2-audio-package`; the kernel installs it at boot. No rootfs step |
| Step 1: GPU driver | `gpu/build/apps-20261104/payload-vt17/Nord2Metal` (y2s57) | `e25c5ecf650bf55726e076f653054db3415de8113f1f4464d9b78d49469cb63c` | `/System/Library/Extensions/Nord2Metal.bundle/Nord2Metal` (replaces vt16 `346ccd1b...`) |
| Step 2: camera plist | `payload-still4096/D22/AVCaptureSession.plist` | `8e90184ffdf634c03dbf3f317f28aa4b35b98546210457d2a257cb1a3c25edfa` | `CMCapture.framework/D22/AVCaptureSession.plist` (replaces dual-wide `b66f008b...`) |
| Step 3: hardware codecs | **default** `payload-vcodec-prod8/` (tag big-prod8; staged by the coordinator). Safe fallback `payload-vcodec-prod5-nohwdec/` | see below |

`payload-vcodec-prod8` files (each equals its `plan.json` hash; verified). prod5-nohwdec has the same vcodecd, encoder and vendor files, decoder `3874f1fe...` (not registered: switch `hwdec` off) and plan `d7563955...`:

| File | sha256 |
|---|---|
| plan.json (tag `big-prod8`) | `f57bdd4213c4e26ecb33f0ab86558aebf3906fb15aee2ab8d2dae64c92a5ec27` |
| vcodecd (log rotation + heap-zero fix B12) | `8fbbd9ab8e10769e843aa54e6549c4950f0562c9d8ed59320dbadecfb9dc6e93` |
| Slim.videoencoder (RealTime fix) | `91bd53afc41bc5726eb7f656790331e3cd7e0aaccd1906c282f4216d3f0e08fc` |
| Slim.videodecoder (H.264 + HEVC hardware, ring of 8 held pictures up to 4K, 104 MB cap; dr6) | `68f71610492faffa59d9d036d7641a0600af686c2fb4b620169d9a71234843de` |
| vendor/bin/vpud | `82afe7221d1e906c6d76fdbd8cbe437c96d223a51952c5ff1152ec5250e9597b` |
| vendor/lib/libvpud_vcodec.so | `07130d2a0d6cea60d148763c5ed8722a1abacaa0f0f7a1174dd71321f7274eae` |
| vendor/lib/libvcodecdrv.so | `5e69ccfca0728d28b4e991d27a30926c0243abf4d3255d9e9619169dbc00b091` |
| vendor/lib/libvcodec_utility.so | `e26673ed13ee73351f1b1cf6dcba1d58762fd7dfc585eda97c50d78805b54fd3` |
| vendor/lib/libvcodec_oal.so | `cd5c5b6c40f23acff1c1936113b8c4604f7e8d46429d6c260ff8f5a5cb7029d6` |

**History of the vcodec payload:** (a) switches: hm1-heic left `nord2-video-trace`, `nord2-video-probe`, `nord2-camera-keephevc` on;
prod2 only turns off `hwenc-sync`, prod3..prod8 also turn off `trace, probe, camera-keephevc` (final state = the cam2..cam6 camera state).
(b) decoder: prod3 `61c1bed1` broken for H.264 (dp1: 0 pictures); prod5 `3874f1fe` H.264 hw, HEVC software (dp3); prod6 `9f23d1e5`
both in hardware, bit-exact on 176x144 probe vectors (dp4) but WRONG P/B pictures on real streams (dr1/dr2: H.264 1080p 42/60,
HEVC 1080p 27/60, HEVC 4K 5/20) because VideoToolbox's pool reused buffers the plug-in had already delivered; prod7 added a ring
of held buffers; **prod8 `68f71610`** = ring of 8 pictures up to 4K with a 104 MB cap: bit-exact at real sizes (dr6).
(c) vcodecd `8fbbd9ab` (prod5..prod8): log rotation + heap-zero fix B12. Encoder `91bd53af` and vendor objects unchanged since prod2.

**Step 3 options** (pinned in `--check`; select with `VCODEC_PAYLOAD=...`, optionally `START_AT=vcodec`):
| VCODEC_PAYLOAD | plan.json sha256 | Decode | Status |
|---|---|---|---|
| `prod8` (**default**) | `f57bdd4213c4e26ecb33f0ab86558aebf3906fb15aee2ab8d2dae64c92a5ec27` | H.264 + HEVC hardware, Slim.videodecoder `68f71610` | bit-exact through VT at real sizes (dr6) |
| `prod5-nohwdec` (safe fallback) | `d75639555670b5f4268b2d6f626aa3e2fd80be432f2581ce3cfb7962d4f54725` | Apple software (switch `hwdec` off) | hardware encode + Camera video, software decode |
prod5, prod6, prod7: **superseded** (wrong pictures on real streams); the script refuses them. Both options: vcodecd `8fbbd9ab`,
encoder `91bd53af`, same vendor objects.

Other pinned inputs: guest-vt17.sh `e0b1cdab...`, guest-still4096.sh `1e63b2f7...`, guest-vcodec.sh `ec14b557...`,
owned-vt17.json `eb2525e3...`, owned-still4096.json `6d61ceb4...`, owned-vcodec-jpegdec.json `24b41822...`, txn.sh
`3a52df3e...`, RAM ADB rescue image (`research/nord2/rescue/nord2-adb-rescue.img`) `d78bf9fe...`. The full hashes are in
the `PIN` list in big_build_chain.sh, and `--check` verifies all of them.

### Kernel q10jk contents
q10jk = q10ji + 0316 (JPEG ioctl kernel buffer) + 0317 (JPEG EOI trim at 128 bytes). Recipe `gpu/build/build_q10jk.sh`, series
= q10hq `S=` + `0296,0298,0299,0300,0310,0311,0312,0313,0314,0315,0316,0317,0320,0321,0330,0331` (0301 is not included).
`device-mmu-q10jk-candidate/k/kernel/apply-03{10..17,20,21,30,31}.log` all exist and show the expected files patched:
0320 audio_call.[ch], 0321 audio_device.c + audio_mic.inc (`/dev/redwood-mic`), 0330 audio_hw.c (CAM-bit-23 refusals removed),
0331 nord2_cam.inc + audio_hw_native.inc (FAN53870 I2C8 lease per transaction). Kernel `e55b966e...`, receipt
`device_tested: false` (q10jk itself has booted on the phone, in jp7-jpeg, jp7-jpegdec and jp8-jpegdec). There is no newer
image: q10jk is the newest `device-mmu-*-candidate`.

## 2. Why three transactions, not one

`autocycle/txn.sh IMG TAG` takes one `PAYLOAD`, one `GUEST` and one `OWNED`, and the guests read their files from the
payload root (`/src/...`). To put the three in one transaction we would need a new combined guest and a combined payload
layout, and neither has been run. Chaining the three guests that already exist is the reliable minimum. All three guests:
- check the exact state before they write (vt17: Nord2Metal `346ccd1b` + libredwoodair `6df1af45`; still4096: plist
  `b66f008b`; vcodec: every replaced file must be in its own install.json), and stop the offline step before commit if
  the state is not what they expect, so nothing is written;
- are idempotent (they recognise an installed state: `VT17_ALREADY`, the 4096 plist with its `.pre-still4096` backup, and
  vcodec's install.json).

Each txn = rescue boot -> snapshot -> offline install (QEMU) -> catalog check -> commit -> flash q10jk -> 600 s console
capture. That is about 12.5 min each (cam2-prod: 03:18:50 -> 03:31:09), so about 40-45 min for the chain with the two resets.
The two middle boots are throwaway. The final boot is the production state.

`gpu/build/phone_chain.sh` is **out of date**. It ends with `payload-vcodec` (the vc2-encode TEST payload: old vcodecd
c623fd58 + probe/selftest jobs, and no plug-ins) and `owned-vcodec.json`. Over the phone's current jpegdec install the
catalog check would also refuse it, because the JPEG test files are not in owned-vcodec.json. Do not use it. The vt17 and
still4096 steps there are still right.

Owned list for step 3: `owned-vcodec-jpegdec.json`. It is the union of every `owned-vcodec*.json` (checked: the union is
byte-identical to it). It is needed because the prod install removes the JPEG test files that jp8 installed
(`dec640n.jpg`, `dec1920f.jpg` are not in owned-vcodec-prod.json).

## 3. Exact commands

Run from a host shell (not from an agent), with the phone connected. **Wait until the night queue is finished**: no
`queue_run.sh`/`txn.sh` running (`pgrep -af 'queue_run|txn.sh'` empty). The script refuses to start otherwise.

    cd $W/gpu/build
    bash big_build_chain.sh --check        # must end PREFLIGHT_OK (hashes, tools, nothing running, mirror pre-state)
    bash big_build_chain.sh big1           # phone in fastboot; ~45 min; log per step in autocycle/runs-big1-<step>.out
    # resume after a failed or interrupted step (the earlier steps stay committed):
    START_AT=still4096 bash big_build_chain.sh big1b     # also the way to SKIP vt17 entirely
    START_AT=vcodec    bash big_build_chain.sh big1c

Each step prints the last 3 lines of `cc-txn-big1-<step>/events`, which should be `committed`, `kernel flashed 36aca6cf8bddd242`
and `capture done`. The chain stops at the first step that does not reach `capture done`.

After the chain (final boot):

    grep -a 'NORD2 VCODEC\|N2VCD\|NORD2 MIC\|NORD2 JPEG' autocycle/runs/big1-vcodec/ui-usb/xnu-live.log | head
    # Camera video test = CAMERA_INTEGRATION.md section 2 (phone.py scenario video ..., cam_check.py log -> VERDICT
    # hw_encoder_used=yes software_fallbacks=0 hw_errors=0); movie via fetch_media.sh after the NEXT snapshot.

Preflight result (re-run after the switch to the prod8 default, queue running): every hash OK (prod8, prod5-nohwdec). The mirror shows Nord2Metal = vt16 and
the D22 plist = dual-wide 4032, so vt17 and still4096 will install. Only `BUSY` failed, because the night queue was running.

Rehearsals [HOST] (fake root with the real launchd.plist from the mirror; no phone):
- `rehearse-vcodec.sh payload-vcodec-prod8`: REHEARSAL_OK (install, idempotent re-run, foreign-file refusal, undo).
- Switching chains (same script as below): heic-measure -> prod5-nohwdec -> prod8 -> undo: PROD STATE OK (decoder 3874f1fe ->
  68f71610, `nord2-video-hwdec` switched on) + REHEARSAL2_OK; heic-measure -> prod8 -> prod5-nohwdec -> undo (variant
  `/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/rehearse4.sh`, expects only `nord2-vcodec-run,
  nord2-video-claim-hw`): PROD STATE OK + REHEARSAL2_OK.
- Chained rehearsal heic-measure -> jpegdec7 -> prod8 -> undo (also passed earlier with prod3, prod5, prod6) (script kept at
  `/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/rehearse3.sh`): `PROD STATE OK`, meaning one job
  `vcodecd --gate ... --hw-timeout-ms 6000 --restart-on-switch --decoder`, gates/switches exactly `nord2-vcodec-run,
  nord2-video-claim-hw, nord2-video-hwdec`, keephevc/trace/probe removed, n2jpeg_phone + JPEG inputs + jpeg-selftest job
  removed (an empty `/usr/local/lib/n2vendor/jpeg/` directory stays). Plug-ins 43e3cf7d/3e19b9de -> 91bd53af/68f71610. Undo
  restored the plug-in and its record byte-identical (REHEARSAL2_OK).
- vt17 and still4096: not re-rehearsed. Their pre-state was read from the mirror (it matches) and both guests are unchanged
  since they were written (11 Nov). Neither has ever been installed on the phone.

## 4. Verified vs untested

| Item | State |
|---|---|
| q10jk boots; JPEG **encode** in hardware from user space, 11/11 sizes up to 4096x3072 (12 MP 27 ms hw), selftest 12/12 | **[PHONE]** jp7-jpeg (q10jk) |
| Camera VIDEO 1080p30 on the VENC, H.264 High, no fallback | **[PHONE]** cam2-prod (q10ji + prod2): 1263 pictures, 0 failures, 30.02 fps. **cam3 (q10jk + prod3): 1080p 27.5 fps, 0 failures, audio -47 dB**; cam5 (q10jk + prod4): same; **cam6 (q10jk + prod5): 418 frames, 27.5 fps hw, IMG_0056.MOV checked on the PC (hw VENC, 1 slice/picture, decodes OK)**. Encoder 91bd53af = prod6's; vcodecd 8fbbd9ab = prod6's |
| Microphone in Camera recordings (0320/0321) | **[PHONE]** cam2-prod (q10ji): `NORD2 MIC: devfs redwood-mic rate=48000`, `opens=1 reads=765 ... peak=2419 silent=1`. IMG_0052.MOV has AAC mono 44.1 kHz, mean -51.2 dB / max -37.1 dB (before 0320/0321: -91 dB, digital silence). Content not checked by ear; what `silent=1` means is not checked |
| FAN53870 / I2C8 camera power (0330/0331) | **[PHONE]** cam2-prod: `FAN53870 ok: I2C8 pins 0x55111100 ... lease timeouts 0`, `all camera LDOs on` |
| Hardware encode H.264/HEVC up to 4K, decode H.264/HEVC up to 4K/1080p60 bit-exact (probe tools) | **[PHONE]** vc14..vc27, vc7-dec4k2 (HARDWARE_CODECS.md) |
| HEVC reopen in one process (heap-zero fix) | **[PHONE]** vc27-reopen (q10ji) |
| **Real streams through VideoToolbox, hardware decode** | **[PHONE] dr6 (q10jk + prod8-decrun2, decoder 68f71610): VERDICT `HARDWARE DECODE OK at real sizes`**: all 8 runs bit-exact through VT with the plug-in hardware decoder, 0 N2VTDEC_CHANGED: **Camera clip IMG_0056 1080p 418/418**, H.264 1080p with B-frames 60/60, HEVC 1080p 60/60, HEVC 4K 20/20 (unpaced and with 40 ms rehash). Per-frame rate in a synchronous tool (latency, not throughput): Camera 1080p 18.5 fps, H.264 1080p 27.8, HEVC 1080p 25.1, HEVC 4K 7.0. Before: dr1/dr2 (prod6) H.264 1080p 42/60, HEVC 1080p 27/60, HEVC 4K 5/20 (VT pool reused delivered buffers); prod7 = first ring fix |
| H.264 + HEVC hardware decode through VideoToolbox, probe vectors (Slim.videodecoder 9f23d1e5) | **[PHONE]** dp4 (q10jk + prod6-probe): `HARDWARE DECODE OK (both codecs on Slim.videodecoder hardware, bit-exact)`: avc1 176x144 first picture, md5 8/8; hvc1 176x144 `session configured at the first IRAP ... stride 192 buf_h 192 dpb 12, output 176x144`, md5 8/8. Before: dp3 (prod5 decoder 3874f1fe) H.264 hw OK, HEVC fell back to software; dp1 (prod3 decoder 61c1bed1) 0 pictures |
| JPEG decode in hardware (kernel + test tool, F5 path) | **[PHONE]** jp8 (q10jk): all sizes including 12 MP (19.8 ms). Test tool only; no Camera/ImageIO user |
| HEIC written by ImageIO | **[PHONE]** hm1-heic: 512x512 TILES (startTileSession/encodeTile/completeTiles), not frames |
| audio.pkg 1aea9991 (camera starts as it was last) | inside every q10j* image since q10jy; Camera ran with it in cam2-prod **[PHONE]** |
| vt17 Nord2Metal (y2s57) | **never installed.** DIAGNOSTIC build: vt16 + the Photos shader dump in 360-character base64 lines. It adds console output when Photos renders. Skip it with `START_AT=still4096` if you want no diagnostics |
| still4096 plist (4096x3072 back photo format) | **never installed.** Expected to fix 0.5x and good-light one-picture photos failing in GNR (REPORT.md); not seen |
| Playback in Photos by hand; mediaserverd extra memory (~100 MB at 4K) under real use; consumers holding frames longer than 8 pictures; open-GOP streams and seeks; HEVC Camera recording; thumbnails | **untested.** Camera Roll thumbnails were missing in cam2 (then with the broken 61c1bed1 decoder); not re-checked |
| JPEG stills through CMPhoto/Camera on the hardware | **not built.** JPEG_INTEGRATION.md is design only (hook on `-[BWCMPhotoCompressionSession addMainImage:...]`). Only the kernel driver and the test tool `n2jpeg_phone` exist. Camera photos stay on Apple's software JPEG. The build installs no JPEG user-space piece (prod3 removes the test tool) |
| HEIC through hardware HEVC | **not done.** ImageIO writes HEIC as 512x512 tiles (hm1-heic). Our hardware VT object has no tile API (`startTileSession`... NULL). With claim-hw on, a HEIC write that picks the hardware hvc1 object is **untested** [I: it may fail instead of falling back]. Camera offered only JPEG in these runs (keephevc off) |

## 5. Known limitations

- **No software fallback once a session is running.** If the first-picture reopen or the daemon fails mid-recording,
  that recording ends with an error (CAMERA_INTEGRATION.md section 5). A fallback exists only at StartSession (daemon absent or busy -> n2avc).
- One codec session at a time in vcodecd (encode and decode are exclusive). The VT decoder StartSession is not retried
  (busy -> that playback fails). The relay identity is per process.
- VENC runs at 273 MHz (q10jk default; 624 MHz only via `sysctl kern.n2vc.venc_fast=1`, never in production): 4K30
  recording would run at ~14 fps. HEVC recordings cost a vcodecd restart (restart-on-switch).
- JPEG decode/encode in hardware: proven with the test tool only (jp7 encode 11/11, jp8 decode all sizes); no production
  user (Camera stills stay on Apple's software JPEG; the build installs no JPEG user-space piece).
- MM21->NV12 detile in the client costs 8.8 ms per 4K picture. The decoder plug-in holds a ring of 8 delivered pictures (up to 104 MB at 4K) in the client process; a consumer that keeps more than 8 frames alive is untested.
- gpu/video's console budget is 40 lines per process (later sessions' plug-in lines are hidden).
- Interrupts are polled (0314 adaptive wait). Real IRQs are not implemented.

## 6. Rollback

Rule (AGENTS.md): `fastboot boot` does not work on Nord 2. Recovery = flash **only an exact prepared, verified image** to
`boot`. Never boot stock Android (iOS is on userdata). Keep bootloaders/modem/persist/GPT untouched.

- **A step fails after "rescue up"** (offline/catalog/commit error): boot holds the RAM ADB rescue image. If `committed` is
  not in events, the rootfs is unchanged. Flash `q10jk` (`36aca6cf8bddd242...`) to boot, or rerun the step.
- **Kernel rollback** (rootfs stays): q10ji `a1098364ad520e02f17b75a9462a07295b80a59b7b90e3f600c199399d734576`
  (`device-mmu-q10ji-candidate/...q10ji...img`; the last kernel with Camera video proven. It lacks only the JPEG
  fixes 0316/0317). Older recovery image: q10iu `a7640bf5449c28deb281be8a0d259aecf5d06aa050d5ef05bcf01402ccc4eaa0`
  (no vcodec driver: the plug-ins fall back to software through ENXIO, and vcodecd exits and is respawned every 10 s [I]).
  Do not use q10jy..q10jd (relay reset bug B4).
- **Undo payloads** (each one is a txn with the same image; all exist):

      cd $W/gpu/build
      # vcodec -> back to gpu/video's software plug-in (f23ce9eb), no daemon, decoder plug-in removed
      PAYLOAD=payload-vcodec-prod8 GUEST=guest-vcodec-undo.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-jpegdec.json \
        timeout 900 bash autocycle/txn.sh $IMG big-undo-vcodec > autocycle/runs-big-undo-vcodec.out 2>&1
      # still4096 -> dual-wide 4032 plist (from the .pre-still4096 backup)
      PAYLOAD=payload-still4096-undo GUEST=guest-still4096-undo.sh OWNED=$W/gpu/build/apps-20261104/owned-still4096-undo.json \
        timeout 900 bash autocycle/txn.sh $IMG big-undo-still > autocycle/runs-big-undo-still.out 2>&1
      # vt17 -> vt16 Nord2Metal (346ccd1b, from Nord2Metal.pre-vt17)
      PAYLOAD=payload-vt17-undo GUEST=guest-vt17-undo.sh OWNED=$W/gpu/build/apps-20261104/owned-vt17-undo.json \
        timeout 900 bash autocycle/txn.sh $IMG big-undo-vt17 > autocycle/runs-big-undo-vt17.out 2>&1

  (`IMG` = the q10jk path above. Undo plans: payload-vt17-undo `4b7b240f...`, payload-still4096-undo `47f09cf0...`. Undo
  guests: guest-vcodec-undo.sh `34a5181e...`, guest-still4096-undo.sh `66e7bc17...`, guest-vt17-undo.sh `18a51ccc...`.)
- **If hardware decode misbehaves** (playback, memory, thumbnails), keep hardware encode: `VCODEC_PAYLOAD=prod5-nohwdec
  START_AT=vcodec bash big_build_chain.sh TAG2` (software decode). Back to prod8 with `START_AT=vcodec bash big_build_chain.sh TAG3`.
  Both directions rehearsed (section 3). prod3-nohwdec, prod5, prod6, prod7 are superseded.
