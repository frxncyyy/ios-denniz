# First userland test of `/dev/redwood-jpeg` on the phone + the photo integration design

Owner: opus-jpeg. 12 November 2026. For the main session (the only one that touches the phone). **Nothing in this file has
run on the phone.** Host tests, an arm64-apple-ios16.0 build (compiled and linked, not run) and a rehearsal of the install
on a fake root with the real launchd.plist are all that is verified. Kept separate below: [V-phone] seen on the phone
before today, [V-host] host test, [I] inference.

## 0. What a run proves (wording for NOTES/STATUS)

| Outcome | Claim |
|---|---|
| `N2JPGP DONE ... fail=0` and `phone_check.py` prints `JPEG_CHECK PASS` | "The MT6893 hardware JPEG encoder works from iOS user space through `/dev/redwood-jpeg` (K01 driver, J02 library): valid baseline JFIF files from 640x480 to 4032x3024, PSNR within the libjpeg reference at the same quality, N ms per MP." |
| `enc 4032x3024 ... PASS` | the 12 MP single-job question is answered: **no strips needed** for camera stills (the device's 24 MiB source area holds 4032x3024 NV12, 18.3 MB; the 8 MiB limit was autotest command 7's, not the device's). |
| `enc 4032x3024 ... FAIL` + the `limit` lines | the block's real limit is bracketed (3840x2160 / 3264x2448 / 2592x1944); K01 must add horizontal strips with restart markers (K01 STATUS item 2). |
| decode lines `SKIP ... ENOTSUP` | expected today: the kernel has no `RWJ_DECODE`. The user-space side (parser + 21 words + query/decode/compare) is ready and logged its words for every input. |

Not proved by any outcome: hardware JPEG **decode** (needs a kernel change, section 5), Camera/ImageIO integration (section 6),
the EXIF flag, restart intervals, YUYV.

## 1. What is new (all host-tested, MIT, original)

| File | What |
|---|---|
| `jpeg/phone/n2jpeg_phone.c` | the on-phone test program. Includes J02's `rwjpeg_tool.c` unchanged (its test picture, marker checker, selftest and bench verbs; `main` renamed) and links `jpeg/rwjpeg.c` + J04's `jdec/n2jdec_parse.c`/`n2jdec_words.c`. Verbs: `run` (the job), `pattern WxH OUT` (the picture, for the PC), `parse FILE`. Steps S0-S8 below. |
| `jpeg/phone/build_ios.sh` | arm64-apple-ios16.0 build (iPhoneOS16.5 SDK, ld64.lld, libSystem only, ad-hoc signature) -> `/tmp/n2hw-opus-jpeg/ios/n2jpeg_phone` |
| `jpeg/phone/phone_check.py` | the PC verdict (section 4) |
| `jpeg/phone/gen_dec_inputs.sh` | the three decode inputs (ffmpeg testsrc2 -> cjpeg; synthetic, no camera content) |
| `jpeg/phone/test/run_tests.sh`, `test/t_fake.c` | host suite: fake device = K01's validation rules + J02's cjpeg-backed encoder + power/idle model + block-limit injection + an ffmpeg-backed decoder with J04's proposed `RWJ_DECODE` semantics |
| `gpu/build/apps-20261104/stage-vcodec.sh` | new options `--jpeg-test FILE`, `--jpeg-dec FILE` (<= 4), `--jpeg-args "..."`; gate name `jpeg` |
| `guest-vcodec.sh`, `guest-vcodec-undo.sh`, `rehearse-vcodec.sh` | install `/usr/local/libexec/n2jpeg_phone`, `/usr/local/lib/n2vendor/jpeg/*.jpg`, the job `com.nord2.jpeg-selftest`, gate `/private/var/db/nord2-vcodec-jpeg`; the undo removes them; older payloads (no `jpeg` in plan.json) install exactly as before and remove a stale JPEG job (rehearsed: payload-vcodec-decall REHEARSAL_OK) |
| `owned-vcodec-jpeg.json` | owned list = owned-vcodec-dec.json + the six new paths |

### `n2jpeg_phone run` steps (every line `N2JPGP ...`, the library's lines `N2JPG ...`; one log, fsync per line)

- **S0** gate removed before anything (one-shot), `--delay`, jetsam limit raised to 512 MB (`include/n2memlimit.h`: the launchd
  key does not apply, vc22 dprobe was killed at 6 MB; a 12 MP job needs ~37 MB), `kern.n2vc.venc_fast` read (q10jg/0312).
- **S1** `RWJ_INFO`: abi 1, caps bit 0, `max_pixels >= 4032*3024`. No node -> `NODEV`, exit 2.
- **S2** J02 selftest 640x480 through the library: eleven hardware quality codes + the size ladder (`N2JPG selftest 640x480 PASS 12/12`).
- **S3** encode matrix through the raw ioctl (so `errno`, `out_status`, `out_hw_us` are logged): 640x480, 1920x1080, 4032x3024 x
  q 95/90/60 of the kernel's own test picture (`n2j_pattern`, byte-identical to `hwc.py pattern`, checked), each saved as
  `out/jpeg-WxH-qQ.jpg`, log line with bytes, bpp, hw_us, wall_us, hw ms/MP, FNV-1a-64. **S3b**: 12 MP passed -> try the ABI
  maximum 4096x3072; 12 MP failed -> bracket with 3840x2160, 3264x2448, 2592x1944 (stop at the first pass), `RWJ_INFO` after
  every failure.
- **S4** `bench 1920x1080 N q90` (library) and `bench 4032x3024 3` when 12 MP passed.
- **S5** negatives: width 15, quality 0, stride < width -> EINVAL; capacity one byte below `w*h/2+4096` -> ENOSPC; ioctl nr 9 ->
  ENOTTY; `jobs_done`/`jobs_failed` unchanged.
- **S6** idle 3.5 s > `RWJ_IDLE_OFF_MS`: `RWJ_INFO.powered == 0`, then an encode powers on again (wall time incl. power-on logged).
- **S7** decode: the phone's own `out/jpeg-640x480-q90.jpg` and every `--dec` file: jdec geometry, the **21 hybrid words** for the
  proposed claim layout (section 5; `?` marks J04's [I] slots), the 4 index pointers; then `RWJ_DECODE` query (`dst_stride 0`).
  ENOTSUP -> `SKIP`. When a kernel answers: the query must agree with the user-space parser, then decode, write
  `out/jdec-NAME-WxH.{420p,422p,grey}.yuv` (cropped planes) and log its FNV.
- **S8** `N2JPGP DONE pass=P fail=F skip=S`, exit 0 iff F == 0.

## 2. Payloads (staged + rehearsed 12 Nov; rootfs only, no kernel change)

Kernel: **q10jg** (`device-mmu-q10jg-candidate/ui-sched/nord2-ios-metal-q10jg-...img`, sha256 `d57fc0950e7b97ea...`) or q10jf
(`7acf035f...`). Both carry 0310 = K01 b57f87f9; every boot since q10jy logs [V-phone]
`NORD2 JPEG: /dev/redwood-jpeg made (major 20, 0666), 24 MiB source + 16 MiB output at 0xd8000000` and
`... JPEG VEN sharing on`. The node has never been opened.

| Payload | Tag | Gates | What runs |
|---|---|---|---|
| `payload-vcodec-jpeg` (main) | jp1-jpeg | on: jpeg; off: run, probe, dprobe, selftest | **JPEG only**: vcodecd stays installed but does not start (run gate removed), so VEN belongs to the JPEG driver and the clock is K02's JPEG rule (624 MHz). Job at boot + 20 s. Full matrix + 3 decode inputs. |
| `payload-vcodec-jpegvc` (optional, second) | jp2-jpegvc | on: jpeg, run, probe | VEN **sharing** test: probe preset 1080p30 (H.264, starts +45 s) while n2jpeg_phone starts at +50 s with `--sizes 1920x1080,4032x3024 --q 90 --bench 30 --no-ladder`. JPEG then finds VEN already on at VENC's 273 MHz (a running domain is never re-muxed): the bench measures JPEG at 273 MHz and the interleaving of `n2vc_ven_get/put`. |

Contents (sha256, 16): `n2jpeg_phone 6fc9b0f8df55ffc3` (restaged 12 Nov with the RWJD decode path; on q10jh it takes the old SKIP path), `vcodecd 481a147b5e80bdb6` / `n2vcd_probe 8c801de2bfc25975` /
`n2vcd_selftest bbb15a4dd37ee1b7` (= payload-vcodec-fast, the last installed set, so the install changes nothing else), the five
vendor objects (pins, hard-linked), `dec640.jpg 1e545c693cdc66f1` (640x480 4:2:0 q90), `dec4032.jpg 53361cbf39677908`
(4032x3024 4:2:0 q92, 687 KB), `dec1920r.jpg 03767bedb0a03a9a` (1920x1080 4:2:2 q85, DRI 120). No Slim.videoencoder in the
payload: the installed one is kept (the guest prints `kept (not in this payload, undo restores it)`).

launchd (from the rehearsal):

    /usr/local/libexec/n2jpeg_phone run --gate /private/var/db/nord2-vcodec-jpeg --out /private/var/db/nord2-vcodec/out
      --log /private/var/db/nord2-vcodec/jpeg-selftest.log --dec /usr/local/lib/n2vendor/jpeg/dec640.jpg
      --dec /usr/local/lib/n2vendor/jpeg/dec4032.jpg --dec /usr/local/lib/n2vendor/jpeg/dec1920r.jpg --delay 20
    (root, RunAtLoad, no KeepAlive; stdout/stderr -> jpeg-stdout.log, normally empty because the log is dup2'd)

## 3. Commands

    W=$NORD2_WORK
    IMG=$W/gpu/build/device-mmu-q10jg-candidate/ui-sched/nord2-ios-metal-q10jg-ui654fb60dvfs-cap3500-ram3776-sched.img
    sha256sum $IMG | cut -c1-16                  # d57fc0950e7b97ea (q10jg); q10jf 7acf035f66021372 also works
    cd $W/gpu/build/apps-20261104

    # 3a. (done 12 Nov) host tests, build, stage, rehearse -- repeat only after a source change
    bash $W/gpu/hwcodec/jpeg/phone/test/run_tests.sh | tail -1         # N2TEST JPGP PASS 65/65
    bash $W/gpu/hwcodec/jpeg/phone/build_ios.sh | tail -1              # IOS JPEG TOOL OK /tmp/n2hw-opus-jpeg/ios/n2jpeg_phone
    bash $W/gpu/hwcodec/jpeg/phone/gen_dec_inputs.sh $PWD/jpeg-inputs  # (deterministic; already there)
    T=/tmp/n2hw-opus-jpeg/tools; mkdir -p $T
    cp payload-vcodec-fast/{n2vcd_selftest,n2vcd_probe,vcodecd} /tmp/n2hw-opus-jpeg/ios/n2jpeg_phone $T/
    bash stage-vcodec.sh jp1-jpeg --tools $T --vcodecd $T/vcodecd --payload $PWD/payload-vcodec-jpeg \
      --jpeg-test $T/n2jpeg_phone --jpeg-dec $PWD/jpeg-inputs/dec640.jpg --jpeg-dec $PWD/jpeg-inputs/dec4032.jpg \
      --jpeg-dec $PWD/jpeg-inputs/dec1920r.jpg --on jpeg --off run --off probe --off dprobe --off selftest
    bash rehearse-vcodec.sh $PWD/payload-vcodec-jpeg | tail -2          # REHEARSAL_OK
    #   (jpegvc: same + --preset 1080p30 --jpeg-args "--delay 50 --sizes 1920x1080,4032x3024 --q 90 --bench 30 --no-ladder"
    #    --on jpeg --on run --on probe --off dprobe --off selftest, tag jp2-jpegvc, payload payload-vcodec-jpegvc)

    # 3b. ONE transaction (the vc14 recipe with this payload and owned list)
    cd $W/gpu/build
    PAYLOAD=payload-vcodec-jpeg GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-jpeg.json \
      timeout 900 bash autocycle/txn.sh $IMG jp1-jpeg > autocycle/runs-jp1-jpeg.out 2>&1
    tail -3 cc-txn-jp1-jpeg/events              # committed / kernel flashed d57fc095... / capture done
    grep -E 'N2JPGP? |NORD2 JP' autocycle/runs/jp1-jpeg/ui-usb/xnu-live.log | head -80   # live result, before any fetch

    # 3c. files: they come with the NEXT transaction's snapshot (as for the vcodec runs)
    python3 apps-20261104/vcodec_check.py fetch jp1-jpeg     # -> apps-20261104/vcodec-fetch/jp1-jpeg/{jpeg-selftest.log,out/}
    python3 $W/gpu/hwcodec/jpeg/phone/phone_check.py apps-20261104/vcodec-fetch/jp1-jpeg \
      --inputs apps-20261104/jpeg-inputs                     # JPEG_CHECK PASS

    # 3d. the result chain into THIS file (section 9, block per run, idempotent; re-run after the fetch)
    python3 $W/gpu/hwcodec/jpeg/phone/phone_report.py jp1-jpeg           # right after txn.sh: live console summary
    python3 $W/gpu/hwcodec/jpeg/phone/phone_report.py jp1-jpeg --fetch   # after the next transaction: files + phone_check

Keep the display on and do not open Camera while the job runs (+20 s .. ~+40 s). Do not run `hwc.py` autotest command 7 in
the same boot (it shares the model's `n2j` mapping, K01 risk 9).

## 4. Expected lines and the PC verdict

Kernel console (K01): `NORD2 JPEG: 640x480 fmt 2 q 97 -> N bytes in M us` per job; with K02's hook the VEN power lines of the
shared path; 3 s after the last job the off line. `phone_check.py`, per `out/jpeg-WxH-qQ.jpg`:
structure (SOI, APP0 JFIF first, **no APP1/Exif**, DQT, SOF0 WxH 3 comps 0x22/0x11/0x11, DHT, SOS, EOI last, nothing after EOI);
`djpeg` exit 0 with empty stderr; PSNR Y/U/V vs the regenerated picture (ffmpeg psnr, yuvj420p against yuvj420p: no colour
conversion); the libjpeg reference = the same YCbCr picture through libjpeg-turbo (PIL hands YCbCr to libjpeg unconverted,
4:2:0) at the hardware code's nominal quality. PASS = psnr_y >= 30 dB and >= reference - 5 dB. Host numbers for the reference
on this picture: q90 Y 42.8 dB, q60 40.4-40.7 dB, q95 44.7 dB (U/V 47-66 dB). Per `out/jdec-*.yuv`: Y vs libjpeg's raw YCbCr
(PIL draft) >= 45 dB, RGB vs `djpeg -dct int -nosmooth` >= 35 dB.

Timing to compare with: [V-phone] M1 (0298, autotest) 640x480 0.73 ms, 1920x1080 4.6 ms at Q85 (~2.2 ms/MP at 624 MHz) ->
~27 ms hardware for 12 MP if linear. Wall time adds the kernel copyin of the source (18.3 MB at 12 MP), the cache clean and the
copyout; `wall_us` vs `hw_us` per line shows that overhead (the zero-copy improvement would be an IOSurface/claim-mapped source).

Failure triage:
- `NODEV open ... Permission denied`/`No such file`: node missing -> check the boot line above; the job runs as root anyway.
- `enc ... errno=5 out_status=1 (TIMEOUT)` only at >= some size: the block limit; give the `limit` lines to K01 (strips).
- `out_status=4 (BUS)` on the first job: larb7/bus protection on power-up; capture the `NORD2 JPG`/`NORD2 VCODEC` lines.
- `out_status=5 (SIZE)`: DMA size outside the claim or no EOI; save the log line (bytes/hw_us).
- any FAIL in `S5`: an ABI disagreement between J02/K01 (report the errno pair).
- `idle off ... FAIL`: K01's thread call absent (risk 11) or VENC holds the domain (jpegvc); not a JPEG failure.
- no `DONE` line: the process died; `jpeg-stdout.log` and the console show where (jetsam line from S0 first).

## 5. Hardware JPEG decode: what is done, what the kernel still needs

Done (user space, original, host-tested): J04's `jdec/` = the T.81 baseline parser + all 21 hybrid words, verified against
djpeg/ffprobe on 52 generated files and hand literals (`bash gpu/hwcodec/jdec/run_tests.sh` -> `N2TEST J04 PASS 5296/5296`,
re-run 12 Nov). I did not write a second implementation; n2jpeg_phone uses J04's, and the decode oracle is libjpeg-turbo
(djpeg + PIL): the host suite's decode path (fake decoder) gives Y 64-77 dB / RGB 51-53 dB vs libjpeg on 640x480, 4032x3024
4:2:0 and 1920x1080 4:2:2+DRI, and a corrupted plane is caught. The vendor HAL (`ro.vendor.jpeg_decode_sw_opt`, the 32-bit
libjpeg-alpha) is **not** needed by this design; J04's verdict stands: 11 words [V], 9 slots + 4 index pointers [I].

**Claim layout for decode (corrects the encoder split):** the decoder output for 12 MP 4:2:0 is 18.3 MB, more than the
encoder's 16 MiB output area. Proposed inside K01's 40 MiB claim (0xd8000000..0xda800000, vcodec pool from 0xda800000 -- adjacent,
no overlap, [V] boot line `pool 0xda800000+0x5600000`): bitstream <= 7 MiB at 0xd8000000, three 16 KiB index tables at
0xd8700000 (J04: 3x), planes from 0xd8800000 (32 MiB; 4096x3072 4:2:2 = 25.2 MB fits). n2jpeg_phone logs the words for
exactly this layout and warns if an input does not fit. Encode and decode are serialised by K01's one-job lock, so they
share the claim.

Kernel work package (K01 file, next image), in J04's order, each one boot:
1. **E0/E1 read-only** (one `RWJ_DECODE`-less sysctl or autotest step): jpgdec gates 0x17000000 bits 12/16, larb7 ports
   11/12 state, the decoder window's power-on values of 0x31C..0x374.
2. **`RWJ_DECODE` query mode** (no hardware): parse with J04's freestanding parser in the kernel, answer geometry/strides
   (= what S7 checks against user space).
3. **E2 frame mode** (the open 2015 register set, `n2jdec_supplementary`, both DEST banks), then E3 the 21 hybrid words; output
   copied out as Y + Cb + Cr (`dst_c` = Cb, Cr at `dst_capacity_c/2`, J04's proposal); `out_status` RWJ_E_*.
   n2jpeg_phone's S7 needs **no change** for any of these: it already queries, decodes, writes the planes and the PC compares.

## 5b. Hardware decode on q10ji (kernel 0315 RWJD_JOB): payloads staged + rehearsed 12 Nov, NOT run

Kernel: **q10ji** `device-mmu-q10ji-candidate/ui-sched/nord2-ios-metal-q10ji-...img`, sha256 `a1098364ad520e02...`
(0315: `include/redwood_jpegdec.h`, `RWJD_JOB` + `RWJD_PROBE`, detection `RWJ_INFO.reserved[0] == 1`; decoder claim
0xDA800000: bitstream 16 MiB, index 1 MiB (64 KiB zeroed per job), planes 0xDB900000 +64 MiB).
`n2jpeg_phone` (sha `6fc9b0f8...`) S7 with the kernel decoder, every step one `RWJD_JOB`, all logged to jpeg-selftest.log:
- **E0/E1** `RWJD_PROBE`: 39 offsets (0x090 .. 0x374 incl. 0x278/0x340/0x348), power-on values, no decoder write.
- on the first input (dec640.jpg, 640x480 4:2:0): **E2** = J04's open 2015 frame-mode set (n2jdec_supplementary + bank 1 +
  hybrid registers 0, `0x274=0x37`, `0x240` last); if no picture **E3** = the 21 hybrid words in slot order + index pointers
  (3x16 KiB at 0xDB800000) + 2015 strides + bank 0, `0x240` last; if no picture **E4a..E4i** (0x17C=1/2/3, 0x330/0x334 =
  dims / 0, 0x31C = BRZ (skipped for 4:2:0) / zeroed table / FILE_BRP, 0x33C/0x344 bounds, index 0, 0x090 0x00/0x01), one
  change each, stop at the first EOF with a plausible Y plane (mean 2..253, variance > 4). A failed search step is a SKIP
  ("search miss"); only "no step decoded the first input" is a FAIL.
- the found step on **dec1920r.jpg** (1920x1080 4:2:2, DRI) and **dec4032.jpg** (12 MP), and on the phone's own
  `jpeg-640x480-q90.jpg` when the encode steps ran.
- per job: the full write list (`hwdec NAME STEP regs(n): off=value ...`), rc/errno, out_status, hw_us, 0x274/0x278/0x340 vs
  total_mcu, Y statistics, the cropped planes `out/jdec-NAME-STEP-WxH.{420p,422p}.yuv`; `phone_check.py` compares each with
  libjpeg-turbo (Y vs libjpeg raw YCbCr >= 45 dB, RGB vs `djpeg -dct int -nosmooth` >= 35 dB).
Host: `N2TEST JPGP PASS 65/65` (fake 0315 kernel with the same allow-list/address/scalar checks: E2-works, E3-only, E4b-only
and nothing-works runs; every list passes the checks, misses are clean EIO/TIMEOUT).

| Payload | Tag | Base | Job |
|---|---|---|---|
| `payload-vcodec-jpegdec` (**first on q10ji**) | jp2-jpegdec | = cam1-prod (`--prod`: vcodecd f974e33f running, KeepAlive, --decoder; Slim.videoencoder 43e3cf7d + Slim.videodecoder 3e19b9de; claim-hw + hwdec on): installing it changes nothing of the production state | `--delay 30 --decode-only`: S1 + S7 only, **no encoder job** |
| `payload-vcodec-jpeg-prod` (second) | jp3-jpeg | same production base | full: encode matrix + decode (the self JPEG last) |
| `payload-vcodec-jpeg` (queued jp1 on q10jh) | jp1-jpeg | payload-vcodec-fast binaries, run gate removed | full; on q10jh S7 takes the old path (SKIP) |

Owned list for jp2/jp3: `owned-vcodec-jpegdec.json` (= owned-vcodec-prod.json, which already covers the JPEG paths).

    IMG=$W/gpu/build/device-mmu-q10ji-candidate/ui-sched/nord2-ios-metal-q10ji-ui654fb60dvfs-cap3500-ram3776-sched.img
    sha256sum $IMG | cut -c1-16                         # a1098364ad520e02
    cd $W/gpu/build
    PAYLOAD=payload-vcodec-jpegdec GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-jpegdec.json \
      timeout 900 bash autocycle/txn.sh $IMG jp2-jpegdec > autocycle/runs-jp2-jpegdec.out 2>&1
    python3 $W/gpu/hwcodec/jpeg/phone/phone_report.py jp2-jpegdec            # live console summary -> section 9
    # next transaction (e.g. jp3-jpeg, same IMG, PAYLOAD=payload-vcodec-jpeg-prod, same OWNED) brings the files:
    python3 $W/gpu/hwcodec/jpeg/phone/phone_report.py jp2-jpegdec --fetch    # + phone_check (djpeg) on every jdec-*.yuv
    #   (the snapshot of a transaction holds the PREVIOUS boot's files: fetch jp2 right after the jp3 transaction, before
    #    any later one -- jp3's own job rewrites the same out/jdec-* names)

Reading the result: `hwdec search on dec640.jpg: E2|E3|E4x` names the working register set; the 12 MP line's hw_us is the
decode time; a step with `errno 1/14/22` (EPERM/EFAULT/EINVAL) is a kernel refusal of the list (a bug on our side, nothing
was written); `out_status 2 (STALL)` with 0x274 bits names the failing stage (J04: 0x02/0x04 underflow/overflow, 0x20 BS error).

## 5c. After jp4-jpegdec (q10jj): the decoder DOES start; jp6 payload staged + rehearsed 12 Nov, NOT run

Reading of jp4 (analysis only): the user-space `irq=0 state=0 mcu=0` of a failed step is **not** the hardware's: since 0316
nothing is copied out on EIO. The kernel lines (console, in step order) are the result: [V-phone] probe `state 0x88011000`
(bits 26:24 = 0 idle); E2 and E4g2 (the two steps with `0x344 = 0`) `0x8a051030` = state 2 **HEADER**; E3, E4a1..E4g1, E4h
(`0x344 = 0x4af/0x4b0`) `0x8b611003` = state 3 **VLD**; E4i (reset 0x00/0x01) stays idle. Power, clock gate, window base
and trigger are therefore right (reset values match the 2015 map: 0x090=0x10, 0x270=0x11, 0x23c=0x3fffffff).
[V] upstream Linux `mtk_jpeg_dec_reg.h`: `JPGDEC_REG_BIT_STREAM_SIZE 0x0344`, written next to FILE_TOTAL_SIZE; power-on
value here 0xffffff00. J04's guess (0x344 = last MCU) told the block the stream is 1199 bytes (or 0). Also [V]
`n2jdec_supplementary()` lacks 0x0FC DU_NUM, and E2/E3 overwrite the power-on values of 0x31C (0x17f), 0x330 (0xffc1), 0x33C (0x10).

`n2jpeg_phone` sha `48529bb4...` (host `N2TEST JPGP PASS 72/72`), payload `payload-vcodec-jpegdec6`, tag **jp6-jpegdec**
(= payload-vcodec-jpegdec + the new tool; same kernel q10jj, same OWNED list). New, before E2/E3/E4, 500 ms per search step:
- **F0** 2015 set + 0x0FC, both banks, no hybrid-only register written; **F1** F0 + `0x344 = file bytes`; **F2** F0 +
  `0x344 = align128`; **F3** E3 with `0x344 = file bytes`; **F4** F3 without 0x31C/0x330/0x334/0x33C.
- after every failed F/E2/E3 step a post-reset probe (`hwdec after STEP`): does 0x090 0x00/0x10 restore the power-on values?
- last: `hwdec window 000-0fc .. 300-3fc`, the whole window read-only (non-zero words).
- `phone_report.py` now lists every kernel `NORD2 JPEG: dec ... state ...` line (the per-step state).

    PAYLOAD=payload-vcodec-jpegdec6 GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-jpegdec.json \
      timeout 900 bash autocycle/txn.sh $IMG jp6-jpegdec > autocycle/runs-jp6-jpegdec.out 2>&1     # IMG = q10jj 3b00238e

## 5d. After jp6-jpegdec: F3 decodes 640x480 correctly; jp8 payload staged + rehearsed 12 Nov, NOT run

Reading of jp6 (analysis only):
- **dec640 F3 is correct [V host]**: the fetched `jdec-dec640-F3-640x480.420p.yuv` (packed planar 4:2:0, 460800 B) vs
  `ffmpeg -pix_fmt yuv420p` of the same file: Y 74.8 / U 72.7 / V 70.1 dB, max |d| = 1 (1415 bytes differ); phone_check
  vs libjpeg Y 75.0 dB, RGB 53.0 dB.
- **dec1920r / dec4032 did run [V-phone]**: user space prints `irq 0 state 0` only because nothing is copied out on EIO; the
  kernel lines are `dec 30 regs -> 1 irq 0 state 0x86e11002` after 2 s = 0x278 bits 26:24 = 6 (IDCT, doc ordering [I]):
  header + VLD ran, the output side stalled. Not a stuck block: 0315 resets 0x090 after every job (timeout included) and the
  last probe reads idle 0x88011000.
- **Carry-over [V-phone]**: the post-reset probes show the 0x090 reset clears 0x200..0x24C and restores 0x31C/0x330/0x33C/
  0x344, but KEEPS 0x0FC..0x170. F3 writes none of 0x0F8/0x160/0x164/0x16C/0x170, so all three F3 jobs ran with F0's
  640x480 values (final window: `160=280 164=140 16c=70407 170=4b0`). Needed: dec1920r 0x16C 0x70e07 / 0x170 16200 /
  strides 1920/960; dec4032 0x71f03 / 47628 / 4032/2016. Right for 640 by coincidence, wrong for both large ones = the
  leading hypothesis (H1).
- Not different between the jobs [V]: 0x338/0x36C..0x374 (same index base), 0x330/0x334 and 0x158/0x15C (per picture,
  right), 0x0FC (0x400 / 0x500), 0x23C. Inputs (ffprobe + marker walk): all three baseline SOF0 with standard tables; dec1920r
  4:2:2 DRI=120 (134 RSTn), dec4032 4:2:0 without DRI = dec640's format, so DRI/subsampling cannot explain both failures.
- Weaker hypotheses: H2 a 64 KiB limit (only dec640's bitstream / 0x344 is < 2^16); H3 index tables 16 KiB apart too
  small for 16200/47628 MCUs; H4 DRI (dec1920r only).

`n2jpeg_phone` sha `47799481f66e35b2...` (source `8c512e51...`; host `N2TEST JPGP PASS 94/94`), payload
`payload-vcodec-jpegdec7`, tag **jp8-jpegdec**: production base exactly as jpegdec6 (`--prod`, vcodecd f974e33f, both
plug-ins, gates on run+jpeg, off selftest/probe/dprobe, `--delay 30 --decode-only`); plan.json differs from jpegdec6 only in
the tag, the tool and two inputs. Order of jobs on the first input (dec640): probe, **G0** (= the F3 list as the first
decoder job of the boot, power-on 0x160..0x170: does F3 need the 2015 registers at all?), **F5** (F3 + this picture's
0x0F8/0x160/0x164/0x16C/0x170); only if F5 misses the jp6 order F0..F4, E2, E3, E4. Each further input with F5; if F5
misses there: F6 (+0x248/0x320), F5n (0x170 = 0x7ffffff), F5i (index tables 0x5000 apart, H3). After a larger input decoded
with F5: diagnostics F5p/F5s/F5w put back ONE of dec640's values (0x170 / 0x160+0x164 / 0x16C); a diagnostic whose value
equals the input's own is skipped. Inputs: dec640, dec1920r, dec4032 (jp6) + **dec640n.jpg** (640x480 noise q100, 421314 B,
5f5203ac) and **dec1920f.jpg** (1920x1088 gradient 4:2:0, no DRI, 57900 B, c0e7483d) for H2 (`gen_dec_inputs.sh`, deterministic).
Also changed: `phone_check.py` strips `-G0` step names; `stage-vcodec.sh` allows 8 `--jpeg-dec` (was 4; the tool takes 8);
`owned-vcodec-jpegdec.json` + the two new input paths (a superset; rehearsal: all 16 installed/removed paths are owned).

Reading the result: `PASS hwdec dec1920r.jpg F5` + `PASS hwdec dec4032.jpg F5` = H1; then F5p/F5s/F5w name the register
(a SKIP "diagnostic: no decode" = that stale value alone breaks it). F5 PASS on dec1920f but FAIL on dec640n = H2.
Only F5i decodes = H3. G0 PASS = F3 does not need the 2015 geometry registers at all (then F5 only matters after other jobs).

    T=/tmp/n2hw-opus-jpeg/tools8; P6=payload-vcodec-jpegdec6     # (done 12 Nov) in gpu/build/apps-20261104
    bash $W/gpu/hwcodec/jpeg/phone/test/run_tests.sh | tail -1        # N2TEST JPGP PASS 94/94
    bash $W/gpu/hwcodec/jpeg/phone/build_ios.sh | tail -1             # n2jpeg_phone 47799481f66e35b2
    bash $W/gpu/hwcodec/jpeg/phone/gen_dec_inputs.sh $PWD/jpeg-inputs
    mkdir -p $T; cp $P6/vcodecd $P6/Slim.videoencoder $P6/Slim.videodecoder /tmp/n2hw-opus-jpeg/ios/n2jpeg_phone $T/
    bash stage-vcodec.sh jp8-jpegdec --prod --vcodecd $T/vcodecd --encoder-plugin $T/Slim.videoencoder \
      --decoder-plugin $T/Slim.videodecoder --payload $PWD/payload-vcodec-jpegdec7 --jpeg-test $T/n2jpeg_phone \
      --jpeg-dec jpeg-inputs/dec640.jpg --jpeg-dec jpeg-inputs/dec1920r.jpg --jpeg-dec jpeg-inputs/dec4032.jpg \
      --jpeg-dec jpeg-inputs/dec640n.jpg --jpeg-dec jpeg-inputs/dec1920f.jpg \
      --jpeg-args "--delay 30 --decode-only" --on run --on jpeg --off selftest --off probe --off dprobe
    bash rehearse-vcodec.sh $PWD/payload-vcodec-jpegdec7 | tail -2   # REHEARSAL_OK

    IMG=$W/gpu/build/device-mmu-q10jk-candidate/ui-sched/nord2-ios-metal-q10jk-ui654fb60dvfs-cap3500-ram3776-sched.img
    sha256sum $IMG | cut -c1-16                                        # 36aca6cf8bddd242
    cd $W/gpu/build
    PAYLOAD=payload-vcodec-jpegdec7 GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-jpegdec.json \
      timeout 900 bash autocycle/txn.sh $IMG jp8-jpegdec > autocycle/runs-jp8-jpegdec.out 2>&1
    python3 $W/gpu/hwcodec/jpeg/phone/phone_report.py jp8-jpegdec            # live console summary -> section 9

Kernel side (described only, not made): 0315 could write 0 to 0x0F8..0x17C before every user list so a job no longer
depends on the previous one; with F5 in user space it is not needed.

## 6. Photos: how hardware JPEG and hardware HEVC can reach CMPhoto / ImageIO (design, not built)

Facts: [V] cache (J02 ROUTING.md, camera-userland rwcam_diag.c): CMPhoto's JPEG hardware lever is the IOKit service
`AppleJPEGDriver` (`IOServiceMatching` at 0x1a5d99058; properties `AppleJPEGNumCores`, `...SupportsAppleInterchangeFormats`,
`...SupportsMissingEOI`, `...SupportsRSTLogging`), otherwise it calls `applejpeg_encode_*` of AppleJPEG.framework in software
(0x1a5d6a990); ImageIO does not link VideoToolbox; no Apple component builds the `'jpeg'` FourCC. HEIC stills:
BWPhotoEncoderNode -> CMPhoto -> `VTCompressionSessionCreate('hvc1')` (0x1a5dad298). [V-phone] probe run 4: ImageIO HEIC write
works through our plug-in's registration of Apple's VCPHEVC software encoder: 1024x768 498 ms, **4032x3024 1586 ms**, JPEG
1024x768 16 ms (software applejpeg). Camera currently photographs **JPEG** (rwcam_diag removes HEVC/HEIF/PrefersHEVC from the
source attributes). (Side note for gpu/video: the HEIC round trips show large `absdiff_quadrants` (99/85/55/41 at 1024x768)
while the golden HEIC decodes right -- worth a look before HEIC is made the default.)

### 6a. HEIC through the hardware HEVC encoder (best return, no Apple hook)
CMPhoto already asks VideoToolbox, so the route is our plug-in: register P04's `n2hwenc` for `hvc1` when CMPhoto asks
(the `claim-hw` switch exists). Two things decide the design and must be **measured first** (one boot, no code risk):
wrap VCPHEVC's encoder object in `n2vt_shim.c` with a logging proxy and take one HEIC (ImageIO probe `img.big.heic`, then
Camera with `nord2-camera-keephevc`): which entry points CMPhoto calls (`startTileSession`/`encodeTile` vs `startSession` +
`encodeFrame`), the session size (512x512 grid tiles vs whole picture), and every property key (Quality, profile "Main Still",
`AllowFrameReordering`, max key-frame interval, bit depth).
- If CMPhoto uses **frame sessions of tile size** (512x512): P04 already fits (16..4096 x 16..2304); make every frame an IDR
  (HEIF grid items share one hvcC), map `kVTCompressionPropertyKey_Quality` 0..1 to a constant-QP/quality mode instead of
  AverageBitRate, keep one vcodecd session for all 48 tiles of a 12 MP picture (4096x3072 grid). Estimate from vc16/vc18:
  ~16 ms fixed + ~1.6 ms hardware per 512x512 frame at 273 MHz -> ~0.8 s for 48 tiles, 0.5 s with `--venc-keep-power`/624 MHz,
  vs 1.59 s software today [I]. Pipelining the two VENC cores could halve it again.
- If CMPhoto uses the **tile API** (`VTTileEncoderSession`): P04 has `startTileSession = NULL`; implement the four tile
  callbacks on the same vcodecd session (each tile = one IDR, `VTEncoderSessionEmitEncodedTile`). Same estimate.
- A whole 4032x3024 frame is **not** possible in one vendor HEVC job (P04 max 4096x2304); tiles are required anyway.
Quality: the vendor encoder at constant QP 22-27 on intra-only 512x512 should match VCPHEVC's default still quality; verify with
the PSNR tool (`vcodec_check.py stats`) on decoded tiles before switching Camera to HEIC.

### 6b. JPEG through CMPhoto / ImageIO
- **The VT `'jpeg'` plug-in (J02's `n2jpeg_hw_vt`) is not reached by Camera or ImageIO** [V]; it serves VT clients only.
  Register it (only when `n2jpeg_hw_vt_available()`) for apps that use `VTCompressionSession('jpeg')`.
- **Superseded by `JPEG_INTEGRATION.md` (12 Nov, read out of the cache):** the two routes below do not work as written --
  CMPhoto's `applejpeg_encode_*` calls go through read-only stub islands that branch straight to AppleJPEG (no GOT on the
  call path), and the kernel DYLD_INSERT/AMFI mechanism is not in the tree. The recommended hook is one level up: wrap
  `-[BWCMPhotoCompressionSession addMainImage:metadata:options:]` in mediaserverd with the camera bundle's existing
  `method_setImplementation` wrap() (rwcam_diag.c:374), encode with rwjpeg, hand the JPEG to CMPhoto
  (`CMPhotoCompressionSessionAddCompressedImage`, to be verified). Original sketch kept for the record:
- **Interpose `applejpeg_encode_*` in mediaserverd** (smallest real route, [I] feasible): the kernel already injects
  `DYLD_INSERT_LIBRARIES` into a named program and sets the AMFI dyld-policy bits for it (precedent: Camera +
  `libredwoodcamui.dylib`, camera-userland REPORT.md). The same for mediaserverd with a `libredwoodjpeg.dylib` that interposes
  the AppleJPEG encode entry points CMPhoto calls (50 stub targets; the ABI of the options/output structs must be read from the
  cache first), sends 4:2:0/NV12 inputs up to 12.6 MP to `rwjpeg` (the library, `/dev/redwood-jpeg` is 0666) and calls the
  original for everything else (other subsampling, progressive, odd sizes, `RWJPEG_E_FALLBACK`). EXIF: CMPhoto writes its own
  APP1; the hook must hand back a stream without the hardware's APP0 where CMPhoto expects none (`rwjpeg_insert_segments` does
  that surgery). Thumbnails (160x120 / 320x240 embedded) can stay software. Gain: 12 MP JPEG ~30 ms hardware + copy vs
  software applejpeg's few hundred ms [I, the 12 MP software time is not measured]. Same dylib serves ImageIO in app processes
  only where the kernel injects it (Photos export) -- lower priority.
- **An IOKit-visible `AppleJPEGDriver` replacement** (assessment only): feasible in principle (we build XNU and already
  publish IOKit/devfs services), but it is the largest option: (1) an IOService named `AppleJPEGDriver` with the four
  properties so CMPhoto selects "hardware"; (2) its IOUserClient external-method ABI, which is **undocumented** and must be
  reverse-engineered from AppleJPEG.framework's hardware path (selectors, scalar/struct layouts, IOSurface IDs for input and
  output, decode as well as encode, scaling/thumbnail requests the MT6893 block cannot do -> must fail cleanly into CMPhoto's
  software path); (3) IOSurface -> physical pages for the block (IOMMU bypass means a copy into the claim, ~5-10 ms at 12 MP).
  Risk: CMPhoto treats `RequireHardwareCodec` as "fail if hardware fails" (ROUTING.md section 3), so an incomplete service makes
  Camera worse than today's software path. Estimate: weeks of RE + kernel C++; do 6b-interpose first and keep this as the
  long-term option.

## 7. Risks / open points

1. Nothing has run on the phone; the fake device enforces K01's rules but is not the kernel (copyin layout, cache clean,
   EOI cut, power sequence are host-modelled only by K01's own suite).
2. 12 MP is the open question by design; a failure there is a result, not a test bug (the ladder brackets it).
3. jpegvc: if the JPEG job powers VEN first, VENC runs at 624 MHz for that session (K02 never re-muxes a running domain);
   the vc probe results of that boot are then not comparable with vc16's 273 MHz numbers.
4. `RWJ_INFO.powered` means "this driver holds its VEN reference", not the SPM bit (K01 risk 8): in jpegvc the idle check
   can pass while VENC keeps the domain on.
5. The decode words use J04's [I] hypotheses; they are logged, never written to hardware by this payload.
6. The S3 matrix writes ~10 MB of JPEGs and S7 (when a decoder exists) ~25 MB of planes into `/private/var/db/nord2-vcodec/out`;
   the fetch limit is 150 MB per file. Remove old `out/` files between runs if the data partition is tight.
7. `stage-vcodec.sh` hard-links the vendor objects; the payload must stay on the work disk (96 % full: 23 GB free).

## 8. CMPhoto HEIC measurement boot (`payload-heic-measure`, tag hm1-heic; staged + rehearsed 12 Nov, NOT run)

Question: when CMPhoto writes a HEIC (Camera photo, ImageIO export), does it give the `hvc1` encoder **tiles or frames**, of
**which sizes**, with **which properties**? That decides the hardware-HEVC design of section 6a.

What it installs (rootfs only; any kernel of the q10j* line):
- `Slim.videoencoder` sha `20ee0aa6...` = the proven plug-in build (`gpu/hwcodec/ios/build_plugins.sh`: gpu/video shim + log +
  n2avc + P04) **plus** `gpu/hwcodec/heicm/n2vt_trace.c`, built with
  `N2HW_PLUGIN_EXTRA_SRC=heicm/n2vt_trace.c N2HW_PLUGIN_EXTRA_SYMS=heicm/symbols.txt bash ios/build_plugins.sh /tmp/n2hw-opus-jpeg/plug-trace`.
  The shim (gpu/video/vt/n2vt_shim.c, weak hook `n2vt_trace_wrap`, 4 lines; gpu/video/build_ios.sh `-U` like n2hwenc) wraps
  only the VCPHEVC factory and only while `/private/var/db/nord2-video-trace` exists: a CMDerivedObject proxy with a copied
  vtable that forwards every slot to the real encoder (real object as self, VideoToolbox's own session token) and logs
  create, startSession (dims), encodeFrame (W x H, pixel format, planes, pts, frame properties), completeFrames,
  copySupportedPropertyDictionary, every setProperty/copyProperty with the value's CFCopyDescription, invalidate/finalize with
  frame/tile counts, and the class's non-NULL slot list; tile slots and unknown slots are forwarded with x1..x7 unchanged and
  logged as hex (startTileSession's x2 also as lo/hi 32 bits = packed dims if it is like startSession). Log:
  `/private/var/db/nord2-video/trace/<prog>-<pid>.log` (dir 1777 made by the guest, O_APPEND per line, <= 3000 lines per
  process) + the first 30 lines on the N2VIDEO console channel. Host test `bash gpu/hwcodec/heicm/test/run_tests.sh` ->
  `N2TEST HEICM PASS` (proxy around gpu/video's JPEG encoder: identical emits, no protocol error, no leak; tile slots with a
  made-up class-v4 encoder); gpu/video's own shim suite still `N2TEST WP01 PASS 31/31`.
- switches: `nord2-video-trace` on, `nord2-video-probe` on (gpu/video's `com.nord2.videoprobe` = n2vt_probe at boot: its
  `img.write/read.heic` 1024x768 and `img.big.heic` 4032x3024 cases are the ImageIO path), `/private/var/db/nord2-camera-keephevc`
  on (the camera bundle then leaves HEVC/HEIF/PrefersHEVC in the source attributes: Camera offers HEIC), claim-hw and hwdec off
  (software VCPHEVC, as on every run so far). All vcodec gates off (no vcodecd, no JPEG job). Undo (`guest-vcodec-undo.sh`)
  restores Slim.videoencoder ae914887 and removes the three switches.

Commands:

    cd $W/gpu/build
    PAYLOAD=payload-heic-measure GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-heic-measure.json \
      timeout 900 bash autocycle/txn.sh $IMG hm1-heic > autocycle/runs-hm1-heic.out 2>&1
    # the n2vt_probe runs by itself at boot (ImageIO HEIC). Then, phone unlocked, display on, ~2 min after boot:
    python3 autotest/phone.py scenario photo autotest/runs/hm1-heic-photo --wait-s 8     # Camera, one photo (HEIC if offered)
    python3 autotest/phone.py logs autotest/runs/hm1-heic-photo/after.log
    #   if the Camera shows its format as JPEG: Settings > Camera > Formats > High Efficiency, then the scenario again
    python3 $W/gpu/hwcodec/heicm/heic_report.py hm1-heic --no-fetch --live autotest/runs/hm1-heic-photo/after.log  # live
    # files: with the next transaction's snapshot
    python3 $W/gpu/hwcodec/heicm/heic_report.py hm1-heic

Reading it: one trace file per process. `mediaserverd-<pid>.log` = Camera (BWPhotoEncoderNode/CMPhoto), `n2vt_probe-<pid>.log`
= ImageIO. Per encoder instance the report prints **TILES** or **FRAMES**, the startSession size, frame sizes x count (a 12 MP
grid of 512x512 frames shows as `512x512 ... x48`), every property set with its value and status, and the frame
properties. The camera diag line must say `HEIC STILL OFFERED` for the Camera half to be meaningful.
Risks: a sandboxed mediaserverd may be denied the trace file (then only the first 30 console lines, still enough for
TILES/FRAMES and sizes); the console channel has 40 lines per process in total (n2vt_log), shared with the registration lines;
a proxy bug would make HEIC writes fail in that boot only (the switch file off = untraced; undo restores the old plug-in).

## 9. Phone results (generated: jpeg/phone/phone_report.py)

<!-- JPEG-RESULT jp4-jpegdec BEGIN -->
### Run jp4-jpegdec

_generated 2026-11-12 03:49 by jpeg/phone/phone_report.py jp4-jpegdec_

- [V-phone live console] `autocycle/runs/jp4-jpegdec/ui-usb/xnu-live.log`: `N2JPGP DONE pass=2 fail=1 skip=15`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000325 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000256 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000177 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000238 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000204 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000196 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000166 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - kernel: 22 `NORD2 JPEG/JPG` lines; first/last: `NORD2 JPEG: /dev/redwood-jpeg made (major 20, 0666), 24 MiB source + 16 MiB output at 0xd8000000` / `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x88011000 mcu 0 in 2000033 us`
- fetch: rc 0, 10 files
- [V-phone file] `apps-20261104/vcodec-fetch/jp4-jpegdec/jpeg-selftest.log`: `N2JPGP DONE pass=2 fail=1 skip=15`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000325 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000256 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000177 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000238 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000204 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000196 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000166 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
- PC check (phone_check.py, rc 1):
  - `JPEG_CHECK FAIL (1 failure)`
<!-- JPEG-RESULT jp4-jpegdec END -->

<!-- JPEG-RESULT jp5-jpeg BEGIN -->
### Run jp5-jpeg

_generated 2026-11-12 04:04 by jpeg/phone/phone_report.py jp5-jpeg_

- [V-phone live console] `autocycle/runs/jp5-jpeg/ui-usb/xnu-live.log`: `N2JPGP DONE pass=15 fail=10 skip=15`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - encodes (size q bytes hw_us wall_us hw_ms/MP):
    - 640x480 q95: 71656 B, hw 964 us, wall 1197 us, 3.138 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=946
    - 640x480 q60: 14938 B, hw 859 us, wall 1056 us, 2.796 ms/MP
    - 1920x1080 q95: 450419 B, hw 4825 us, wall 6469 us, 2.327 ms/MP
    - 1920x1080 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=5365
    - 1920x1080 q60: 81379 B, hw 4927 us, wall 5359 us, 2.376 ms/MP
    - 4032x3024 q95: 2481768 B, hw 28523 us, wall 33233 us, 2.339 ms/MP
    - 4032x3024 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=35326
    - 4032x3024 q60: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=31163
    - 3840x2160 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=26474
    - 3264x2448 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=23996
    - 2592x1944 q90: 614859 B, hw 11721 us, wall 13136 us, 2.326 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=1806
  - **12 MP (4032x3024) single job: FAIL: strips needed (see limit lines)**
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP PASS limit 2592x1944 q=90`
  - `N2JPG bench 1920x1080: encode 0 failed (fallback)`
  - `N2JPGP FAIL library selftest 640x480 (11 quality codes + ladder)`
  - `N2JPGP FAIL enc 640x480 q=90`
  - `N2JPGP FAIL enc 1920x1080 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=60`
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP FAIL bench 1920x1080 q90`
  - `N2JPGP FAIL after idle (power on again) 640x480 q=90`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2318 us`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000266 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000287 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000248 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000142 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000221 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000146 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000181 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000224 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000147 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000214 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - kernel: 46 `NORD2 JPEG/JPG` lines; first/last: `NORD2 JPEG: /dev/redwood-jpeg made (major 20, 0666), 24 MiB source + 16 MiB output at 0xd8000000` / `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x88011000 mcu 0 in 2000100 us`
  - kernel `NORD2 JPEG: dec probe 39 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec 36 regs -> 1 irq 0 state 0x8a051030 mcu 0 in 2000116 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000006 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000045 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000095 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000120 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000007 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000067 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000009 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000047 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000087 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8a051030 mcu 0 in 2000004 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000054 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x88011000 mcu 0 in 2000100 us`
- fetch: rc 0, 10 files
- [V-phone file] `apps-20261104/vcodec-fetch/jp5-jpeg/jpeg-selftest.log`: `N2JPGP DONE pass=15 fail=10 skip=15`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - encodes (size q bytes hw_us wall_us hw_ms/MP):
    - 640x480 q95: 71656 B, hw 964 us, wall 1197 us, 3.138 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=946
    - 640x480 q60: 14938 B, hw 859 us, wall 1056 us, 2.796 ms/MP
    - 1920x1080 q95: 450419 B, hw 4825 us, wall 6469 us, 2.327 ms/MP
    - 1920x1080 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=5365
    - 1920x1080 q60: 81379 B, hw 4927 us, wall 5359 us, 2.376 ms/MP
    - 4032x3024 q95: 2481768 B, hw 28523 us, wall 33233 us, 2.339 ms/MP
    - 4032x3024 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=35326
    - 4032x3024 q60: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=31163
    - 3840x2160 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=26474
    - 3264x2448 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=23996
    - 2592x1944 q90: 614859 B, hw 11721 us, wall 13136 us, 2.326 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=1806
  - **12 MP (4032x3024) single job: FAIL: strips needed (see limit lines)**
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP PASS limit 2592x1944 q=90`
  - `N2JPG bench 1920x1080: encode 0 failed (fallback)`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP FAIL library selftest 640x480 (11 quality codes + ladder)`
  - `N2JPGP FAIL enc 640x480 q=90`
  - `N2JPGP FAIL enc 1920x1080 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=60`
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP FAIL bench 1920x1080 q90`
  - `N2JPGP FAIL after idle (power on again) 640x480 q=90`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2318 us`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000325 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000256 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000177 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000238 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000204 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000196 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000166 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000266 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000287 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000248 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000142 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000221 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000146 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000181 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000224 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000147 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000214 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
- PC check (phone_check.py, rc 1):
  - `ENC PASS 1920x1080 q=60  bytes=81379    bpp=0.314 psnr y/u/v=40.65/52.44/54.94 libjpeg(q60) y/u/v=40.62/50.40/51.20 bytes=42522`
  - `ENC PASS 1920x1080 q=95  bytes=450419   bpp=1.738 psnr y/u/v=44.66/59.02/64.94 libjpeg(q95) y/u/v=44.68/58.83/63.18 bytes=475723`
  - `ENC PASS 2592x1944 q=90  bytes=614859   bpp=0.976 psnr y/u/v=43.65/59.88/61.40 libjpeg(q90) y/u/v=42.86/58.76/60.34 bytes=574947`
  - `ENC PASS 4032x3024 q=95  bytes=2481768  bpp=1.628 psnr y/u/v=44.78/64.36/65.61 libjpeg(q95) y/u/v=44.66/65.95/66.40 bytes=2680319`
  - `ENC PASS  640x480  q=60  bytes=14938    bpp=0.389 psnr y/u/v=40.67/54.15/54.15 libjpeg(q60) y/u/v=40.42/47.62/48.68 bytes=9520`
  - `ENC PASS  640x480  q=95  bytes=71656    bpp=1.866 psnr y/u/v=44.75/58.92/99.00 libjpeg(q95) y/u/v=44.81/58.92/57.16 bytes=74961`
  - `JPEG_CHECK FAIL (1 failure)`
<!-- JPEG-RESULT jp5-jpeg END -->

<!-- JPEG-RESULT jp6-jpegdec BEGIN -->
### Run jp6-jpegdec

_generated 2026-11-12 04:29 by jpeg/phone/phone_report.py jp6-jpegdec_

- [V-phone live console] `autocycle/runs/jp6-jpegdec/ui-usb/xnu-live.log`: `N2JPGP DONE pass=3 fail=2 skip=3`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - `N2JPGP SKIP hwdec dec640.jpg F0 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F2 (search miss: next step)`
  - `N2JPGP FAIL hwdec dec1920r.jpg F3`
  - `N2JPGP FAIL hwdec dec4032.jpg F3`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, F0..F4, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg F0 regs(28): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F0: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F0 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F1 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=75f3 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=595 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F1 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F2 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=7600 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=614 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F2 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F3: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=514 wall_us=775 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg F3: Y mean 126.9 stddev 64.9 fnv=e79606ae38680ec1 -> /private/var/db/nord2-vcodec/out/jdec-dec640-F3-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec search on dec640.jpg: F3`
  - `N2JPGP hwdec dec1920r.jpg F3 regs(30): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000242 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec4032.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000754 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec window 000-0fc: rc=0 errno=0 non-zero 2/64: 090=10 0fc=500`
  - `N2JPGP hwdec window 100-1fc: rc=0 errno=0 non-zero 16/64: 12c=13 134=ffffffff 140=db900000 144=dc4a1000 148=dc78a000 14c=db900000 150=dc4a1000 154=dc78a000 158=fc0 15c=7e0 160=280 164=140 16c=70407 170=4b0 180=11 184=80`
  - `N2JPGP hwdec window 200-2fc: rc=0 errno=0 non-zero 6/64: 23c=3fffffff 244=1 248=ffffffff 24c=ffffffff 270=11 278=88011000`
  - `N2JPGP hwdec window 300-3fc: rc=0 errno=0 non-zero 8/64: 318=ffffffff 31c=17f 32c=ffffff 330=ffc1 33c=10 344=ffffff00 350=ffff 354=ffffffff`
  - kernel: 27 `NORD2 JPEG/JPG` lines; first/last: `NORD2 JPEG: /dev/redwood-jpeg made (major 20, 0666), 24 MiB source + 16 MiB output at 0xd8000000` / `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 39 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec 28 regs -> 1 irq 0 state 0x89011000 mcu 0 in 500090 us`
  - kernel `NORD2 JPEG: dec 28 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec probe 39 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec 29 regs -> 2 irq 0x6 state 0x63911000 mcu 0 in 472 us`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec probe 39 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec 29 regs -> 2 irq 0x6 state 0x63911000 mcu 0 in 484 us`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec probe 39 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 514 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x86e11002 mcu 0 in 2000020 us`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x86e11002 mcu 0 in 2000120 us`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
- fetch: rc 0, 10 files
- [V-phone file] `apps-20261104/vcodec-fetch/jp6-jpegdec/jpeg-selftest.log`: `N2JPGP DONE pass=3 fail=2 skip=3`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - encodes (size q bytes hw_us wall_us hw_ms/MP):
    - 640x480 q95: 71656 B, hw 964 us, wall 1197 us, 3.138 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=946
    - 640x480 q60: 14938 B, hw 859 us, wall 1056 us, 2.796 ms/MP
    - 1920x1080 q95: 450419 B, hw 4825 us, wall 6469 us, 2.327 ms/MP
    - 1920x1080 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=5365
    - 1920x1080 q60: 81379 B, hw 4927 us, wall 5359 us, 2.376 ms/MP
    - 4032x3024 q95: 2481768 B, hw 28523 us, wall 33233 us, 2.339 ms/MP
    - 4032x3024 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=35326
    - 4032x3024 q60: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=31163
    - 3840x2160 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=26474
    - 3264x2448 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=23996
    - 2592x1944 q90: 614859 B, hw 11721 us, wall 13136 us, 2.326 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=1806
  - **12 MP (4032x3024) single job: FAIL: strips needed (see limit lines)**
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP PASS limit 2592x1944 q=90`
  - `N2JPG bench 1920x1080: encode 0 failed (fallback)`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP FAIL library selftest 640x480 (11 quality codes + ladder)`
  - `N2JPGP FAIL enc 640x480 q=90`
  - `N2JPGP FAIL enc 1920x1080 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=60`
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP FAIL bench 1920x1080 q90`
  - `N2JPGP FAIL after idle (power on again) 640x480 q=90`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec640.jpg F0 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F2 (search miss: next step)`
  - `N2JPGP FAIL hwdec dec1920r.jpg F3`
  - `N2JPGP FAIL hwdec dec4032.jpg F3`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2318 us`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000325 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000256 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000177 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000238 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000204 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000196 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000166 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000266 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000287 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000248 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000142 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000221 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000146 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000181 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000224 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000147 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000214 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, F0..F4, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg F0 regs(28): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F0: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F0 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F1 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=75f3 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=595 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F1 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F2 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=7600 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=614 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F2 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F3: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=514 wall_us=775 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg F3: Y mean 126.9 stddev 64.9 fnv=e79606ae38680ec1 -> /private/var/db/nord2-vcodec/out/jdec-dec640-F3-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec search on dec640.jpg: F3`
  - `N2JPGP hwdec dec1920r.jpg F3 regs(30): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000242 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec4032.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000754 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec window 000-0fc: rc=0 errno=0 non-zero 2/64: 090=10 0fc=500`
  - `N2JPGP hwdec window 100-1fc: rc=0 errno=0 non-zero 16/64: 12c=13 134=ffffffff 140=db900000 144=dc4a1000 148=dc78a000 14c=db900000 150=dc4a1000 154=dc78a000 158=fc0 15c=7e0 160=280 164=140 16c=70407 170=4b0 180=11 184=80`
  - `N2JPGP hwdec window 200-2fc: rc=0 errno=0 non-zero 6/64: 23c=3fffffff 244=1 248=ffffffff 24c=ffffffff 270=11 278=88011000`
  - `N2JPGP hwdec window 300-3fc: rc=0 errno=0 non-zero 8/64: 318=ffffffff 31c=17f 32c=ffffff 330=ffc1 33c=10 344=ffffff00 350=ffff 354=ffffffff`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - kernel: 5 `NORD2 JPEG/JPG` lines; first/last: `N2JPGP hwdec dec640.jpg F0: failed job: status/irq/state/mcu are only in the kernel line 'NORD2 JPEG: dec 28 regs -> ...' (console, in order)` / `N2JPGP hwdec dec4032.jpg F3: failed job: status/irq/state/mcu are only in the kernel line 'NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 28 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
- PC check (phone_check.py, rc 1):
  - `ENC PASS 1920x1080 q=60  bytes=81379    bpp=0.314 psnr y/u/v=40.65/52.44/54.94 libjpeg(q60) y/u/v=40.62/50.40/51.20 bytes=42522`
  - `ENC PASS 1920x1080 q=95  bytes=450419   bpp=1.738 psnr y/u/v=44.66/59.02/64.94 libjpeg(q95) y/u/v=44.68/58.83/63.18 bytes=475723`
  - `ENC PASS 2592x1944 q=90  bytes=614859   bpp=0.976 psnr y/u/v=43.65/59.88/61.40 libjpeg(q90) y/u/v=42.86/58.76/60.34 bytes=574947`
  - `ENC PASS 4032x3024 q=95  bytes=2481768  bpp=1.628 psnr y/u/v=44.78/64.36/65.61 libjpeg(q95) y/u/v=44.66/65.95/66.40 bytes=2680319`
  - `ENC PASS  640x480  q=60  bytes=14938    bpp=0.389 psnr y/u/v=40.67/54.15/54.15 libjpeg(q60) y/u/v=40.42/47.62/48.68 bytes=9520`
  - `ENC PASS  640x480  q=95  bytes=71656    bpp=1.866 psnr y/u/v=44.75/58.92/99.00 libjpeg(q95) y/u/v=44.81/58.92/57.16 bytes=74961`
  - `DEC PASS dec640-F3 640x480 420p: Y vs libjpeg psnr=75.01 max|d|=1, RGB vs djpeg -nosmooth psnr=52.99`
  - `JPEG_CHECK FAIL (1 failure)`
<!-- JPEG-RESULT jp6-jpegdec END -->

<!-- JPEG-RESULT jp7-jpeg BEGIN -->
### Run jp7-jpeg

_generated 2026-11-12 04:43 by jpeg/phone/phone_report.py jp7-jpeg_

- [V-phone live console] `autocycle/runs/jp7-jpeg/ui-usb/xnu-live.log`: `N2JPGP DONE pass=23 fail=1 skip=16`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - `N2JPG selftest 640x480 PASS 12/12`
  - encodes (size q bytes hw_us wall_us hw_ms/MP):
    - 640x480 q95: 71656 B, hw 800 us, wall 989 us, 2.604 ms/MP
    - 640x480 q90: 42116 B, hw 750 us, wall 979 us, 2.441 ms/MP
    - 640x480 q60: 14938 B, hw 753 us, wall 866 us, 2.451 ms/MP
    - 1920x1080 q95: 450419 B, hw 4696 us, wall 5229 us, 2.265 ms/MP
    - 1920x1080 q90: 258695 B, hw 4658 us, wall 5126 us, 2.246 ms/MP
    - 1920x1080 q60: 81379 B, hw 4638 us, wall 5062 us, 2.237 ms/MP
    - 4032x3024 q95: 2481768 B, hw 27080 us, wall 34660 us, 2.221 ms/MP
    - 4032x3024 q90: 1441852 B, hw 26999 us, wall 29623 us, 2.214 ms/MP
    - 4032x3024 q60: 428605 B, hw 27216 us, wall 29363 us, 2.232 ms/MP
    - 4096x3072 q90: 1484387 B, hw 27346 us, wall 34842 us, 2.173 ms/MP
    - 640x480 q90: 42116 B, hw 729 us, wall 1722 us, 2.373 ms/MP
  - **12 MP (4032x3024) single job: PASS: no strips needed**
  - `N2JPGP PASS limit 4096x3072 q=90`
  - `N2JPG bench 1920x1080 n=10 q=90 code=0x7 bytes=258695 wall_ms_frame=5.784 hw_ms_frame=4.651 wall_ms_per_mp=2.789 hw_ms_per_mp=2.243 wall_fps=172.9`
  - `N2JPG bench 4032x3024 n=3 q=90 code=0x7 bytes=1441852 wall_ms_frame=32.527 hw_ms_frame=27.245 wall_ms_per_mp=2.668 hw_ms_per_mp=2.234 wall_fps=30.7`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec jpeg-640x480-q90.jpg (no working step found on dec640.jpg)`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2440 us`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000226 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000244 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000247 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000216 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000278 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000240 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000227 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000179 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000231 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000207 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - kernel: 38 `NORD2 JPEG/JPG` lines; first/last: `NORD2 JPEG: /dev/redwood-jpeg made (major 20, 0666), 24 MiB source + 16 MiB output at 0xd8000000` / `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x88011000 mcu 0 in 2000048 us`
  - kernel `NORD2 JPEG: dec probe 39 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec 36 regs -> 1 irq 0 state 0x8a051030 mcu 0 in 2000100 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000088 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000031 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000069 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000075 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000085 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000001 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000084 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000029 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000081 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8a051030 mcu 0 in 2000032 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x8b611003 mcu 0 in 2000070 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 1 irq 0 state 0x88011000 mcu 0 in 2000048 us`
- fetch: rc 0, 10 files
- [V-phone file] `apps-20261104/vcodec-fetch/jp7-jpeg/jpeg-selftest.log`: `N2JPGP DONE pass=23 fail=1 skip=16`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - `N2JPG selftest 640x480 PASS 12/12`
  - encodes (size q bytes hw_us wall_us hw_ms/MP):
    - 640x480 q95: 71656 B, hw 964 us, wall 1197 us, 3.138 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=946
    - 640x480 q60: 14938 B, hw 859 us, wall 1056 us, 2.796 ms/MP
    - 1920x1080 q95: 450419 B, hw 4825 us, wall 6469 us, 2.327 ms/MP
    - 1920x1080 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=5365
    - 1920x1080 q60: 81379 B, hw 4927 us, wall 5359 us, 2.376 ms/MP
    - 4032x3024 q95: 2481768 B, hw 28523 us, wall 33233 us, 2.339 ms/MP
    - 4032x3024 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=35326
    - 4032x3024 q60: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=31163
    - 3840x2160 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=26474
    - 3264x2448 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=23996
    - 2592x1944 q90: 614859 B, hw 11721 us, wall 13136 us, 2.326 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=1806
    - 640x480 q95: 71656 B, hw 800 us, wall 989 us, 2.604 ms/MP
    - 640x480 q90: 42116 B, hw 750 us, wall 979 us, 2.441 ms/MP
    - 640x480 q60: 14938 B, hw 753 us, wall 866 us, 2.451 ms/MP
    - 1920x1080 q95: 450419 B, hw 4696 us, wall 5229 us, 2.265 ms/MP
    - 1920x1080 q90: 258695 B, hw 4658 us, wall 5126 us, 2.246 ms/MP
    - 1920x1080 q60: 81379 B, hw 4638 us, wall 5062 us, 2.237 ms/MP
    - 4032x3024 q95: 2481768 B, hw 27080 us, wall 34660 us, 2.221 ms/MP
    - 4032x3024 q90: 1441852 B, hw 26999 us, wall 29623 us, 2.214 ms/MP
    - 4032x3024 q60: 428605 B, hw 27216 us, wall 29363 us, 2.232 ms/MP
    - 4096x3072 q90: 1484387 B, hw 27346 us, wall 34842 us, 2.173 ms/MP
    - 640x480 q90: 42116 B, hw 729 us, wall 1722 us, 2.373 ms/MP
  - **12 MP (4032x3024) single job: FAIL: strips needed (see limit lines)**
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP PASS limit 2592x1944 q=90`
  - `N2JPGP PASS limit 4096x3072 q=90`
  - `N2JPG bench 1920x1080: encode 0 failed (fallback)`
  - `N2JPG bench 1920x1080 n=10 q=90 code=0x7 bytes=258695 wall_ms_frame=5.784 hw_ms_frame=4.651 wall_ms_per_mp=2.789 hw_ms_per_mp=2.243 wall_fps=172.9`
  - `N2JPG bench 4032x3024 n=3 q=90 code=0x7 bytes=1441852 wall_ms_frame=32.527 hw_ms_frame=27.245 wall_ms_per_mp=2.668 hw_ms_per_mp=2.234 wall_fps=30.7`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP FAIL library selftest 640x480 (11 quality codes + ladder)`
  - `N2JPGP FAIL enc 640x480 q=90`
  - `N2JPGP FAIL enc 1920x1080 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=60`
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP FAIL bench 1920x1080 q90`
  - `N2JPGP FAIL after idle (power on again) 640x480 q=90`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec640.jpg F0 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F2 (search miss: next step)`
  - `N2JPGP FAIL hwdec dec1920r.jpg F3`
  - `N2JPGP FAIL hwdec dec4032.jpg F3`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec jpeg-640x480-q90.jpg (no working step found on dec640.jpg)`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2318 us`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2440 us`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000325 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000256 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000177 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000238 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000204 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000196 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000166 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000266 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000287 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000248 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000142 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000221 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000146 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000181 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000224 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000147 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000214 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, F0..F4, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg F0 regs(28): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F0: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F0 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F1 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=75f3 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=595 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F1 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F2 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=7600 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=614 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F2 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F3: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=514 wall_us=775 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg F3: Y mean 126.9 stddev 64.9 fnv=e79606ae38680ec1 -> /private/var/db/nord2-vcodec/out/jdec-dec640-F3-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec search on dec640.jpg: F3`
  - `N2JPGP hwdec dec1920r.jpg F3 regs(30): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000242 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec4032.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000754 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec window 000-0fc: rc=0 errno=0 non-zero 2/64: 090=10 0fc=500`
  - `N2JPGP hwdec window 100-1fc: rc=0 errno=0 non-zero 16/64: 12c=13 134=ffffffff 140=db900000 144=dc4a1000 148=dc78a000 14c=db900000 150=dc4a1000 154=dc78a000 158=fc0 15c=7e0 160=280 164=140 16c=70407 170=4b0 180=11 184=80`
  - `N2JPGP hwdec window 200-2fc: rc=0 errno=0 non-zero 6/64: 23c=3fffffff 244=1 248=ffffffff 24c=ffffffff 270=11 278=88011000`
  - `N2JPGP hwdec window 300-3fc: rc=0 errno=0 non-zero 8/64: 318=ffffffff 31c=17f 32c=ffffff 330=ffc1 33c=10 344=ffffff00 350=ffff 354=ffffffff`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000226 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000244 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000247 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000216 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000278 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000240 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000227 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000179 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000231 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000207 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - kernel: 5 `NORD2 JPEG/JPG` lines; first/last: `N2JPGP hwdec dec640.jpg F0: failed job: status/irq/state/mcu are only in the kernel line 'NORD2 JPEG: dec 28 regs -> ...' (console, in order)` / `N2JPGP hwdec dec4032.jpg F3: failed job: status/irq/state/mcu are only in the kernel line 'NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 28 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
- PC check (phone_check.py, rc 1):
  - `ENC PASS 1920x1080 q=60  bytes=81379    bpp=0.314 psnr y/u/v=40.65/52.44/54.94 libjpeg(q60) y/u/v=40.62/50.40/51.20 bytes=42522`
  - `ENC PASS 1920x1080 q=90  bytes=258695   bpp=0.998 psnr y/u/v=43.66/58.29/60.79 libjpeg(q90) y/u/v=42.84/57.67/59.76 bytes=243854`
  - `ENC PASS 1920x1080 q=95  bytes=450419   bpp=1.738 psnr y/u/v=44.66/59.02/64.94 libjpeg(q95) y/u/v=44.68/58.83/63.18 bytes=475723`
  - `ENC PASS 2592x1944 q=90  bytes=614859   bpp=0.976 psnr y/u/v=43.65/59.88/61.40 libjpeg(q90) y/u/v=42.86/58.76/60.34 bytes=574947`
  - `ENC PASS 4032x3024 q=60  bytes=428605   bpp=0.281 psnr y/u/v=40.65/56.92/58.34 libjpeg(q60) y/u/v=40.67/51.33/51.53 bytes=236481`
  - `ENC PASS 4032x3024 q=90  bytes=1441852  bpp=0.946 psnr y/u/v=43.68/60.84/63.39 libjpeg(q90) y/u/v=42.88/60.34/62.31 bytes=1348774`
  - `ENC PASS 4032x3024 q=95  bytes=2481768  bpp=1.628 psnr y/u/v=44.78/64.36/65.61 libjpeg(q95) y/u/v=44.66/65.95/66.40 bytes=2680319`
  - `ENC PASS 4096x3072 q=90  bytes=1484387  bpp=0.944 psnr y/u/v=43.68/62.36/63.25 libjpeg(q90) y/u/v=42.87/60.96/62.27 bytes=1393380`
  - `ENC PASS  640x480  q=60  bytes=14938    bpp=0.389 psnr y/u/v=40.67/54.15/54.15 libjpeg(q60) y/u/v=40.42/47.62/48.68 bytes=9520`
  - `ENC PASS  640x480  q=90  bytes=42116    bpp=1.097 psnr y/u/v=43.63/55.91/58.71 libjpeg(q90) y/u/v=42.75/57.62/55.47 bytes=39267`
  - `ENC PASS  640x480  q=95  bytes=71656    bpp=1.866 psnr y/u/v=44.75/58.92/99.00 libjpeg(q95) y/u/v=44.81/58.92/57.16 bytes=74961`
  - `DEC PASS dec640-F3 640x480 420p: Y vs libjpeg psnr=75.01 max|d|=1, RGB vs djpeg -nosmooth psnr=52.99`
  - `JPEG_CHECK FAIL (1 failure)`
<!-- JPEG-RESULT jp7-jpeg END -->

<!-- JPEG-RESULT jp8-jpegdec BEGIN -->
### Run jp8-jpegdec

_generated 2026-11-12 05:17 by jpeg/phone/phone_report.py jp8-jpegdec_

- [V-phone live console] `autocycle/runs/jp8-jpegdec/ui-usb/xnu-live.log`: `N2JPGP DONE pass=9 fail=0 skip=9`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - `N2JPGP SKIP hwdec dec1920r.jpg F5p (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec1920r.jpg F5s (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec1920r.jpg F5w (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec4032.jpg F5p (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec4032.jpg F5s (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec4032.jpg F5w (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec1920f.jpg F5p (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec1920f.jpg F5s (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec1920f.jpg F5w (diagnostic: EOF + plausible Y)`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, G0, F5 (F0..F4, E2, E3, E4a..i)`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg G0 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg G0: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3766 wall_us=4000 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg G0: Y mean 103.1 stddev 69.7 fnv=c1449211beb99d03 -> /private/var/db/nord2-vcodec/out/jdec-dec640-G0-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec dec640.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 0f8=0 160=280 164=140 16c=70407 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=492 wall_us=715 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg F5: Y mean 126.9 stddev 64.9 fnv=e79606ae38680ec1 -> /private/var/db/nord2-vcodec/out/jdec-dec640-F5-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec search on dec640.jpg: F5`
  - `N2JPGP hwdec dec1920r.jpg F5 regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=780 164=3c0 16c=70e07 170=3f48 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=4733 wall_us=5885 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5: Y mean 127.8 stddev 64.5 fnv=67cd7d7dddc7a925 -> /private/var/db/nord2-vcodec/out/jdec-dec1920r-F5-1920x1080.422p.yuv (plausible)`
  - `N2JPGP hwdec dec1920r.jpg F5p regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=780 164=3c0 16c=70e07 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5p: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500379 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5s regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=280 164=140 16c=70e07 170=3f48 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5s: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=4684 wall_us=5893 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5s: Y mean 127.9 stddev 64.4 fnv=f955d893faea5b53 -> (not saved) (plausible)`
  - `N2JPGP hwdec dec1920r.jpg F5w regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=780 164=3c0 16c=70407 170=3f48 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5w: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=4725 wall_us=5890 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5w: Y mean 127.8 stddev 64.5 fnv=3c6375fd90830bb9 -> (not saved) (plausible)`
  - `N2JPGP hwdec dec4032.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=fc0 164=7e0 16c=71f03 170=ba0c 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=19775 wall_us=23000 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec4032.jpg F5: Y mean 127.9 stddev 64.5 fnv=a431d8af34e26993 -> /private/var/db/nord2-vcodec/out/jdec-dec4032-F5-4032x3024.420p.yuv (plausible)`
  - `N2JPGP hwdec dec4032.jpg F5p regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=fc0 164=7e0 16c=71f03 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5p: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=501069 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec4032.jpg F5s regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=280 164=140 16c=71f03 170=ba0c 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5s: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=19868 wall_us=24565 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec4032.jpg F5s: Y mean 129.5 stddev 64.4 fnv=ed1970db59c0e1e0 -> (not saved) (plausible)`
  - `N2JPGP hwdec dec4032.jpg F5w regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=fc0 164=7e0 16c=70407 170=ba0c 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5w: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500580 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec640n.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=66e00 270=11 31c=0 330=280 334=140 33c=0 344=66dc2 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 0f8=0 160=280 164=140 16c=70407 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec640n.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=1504 wall_us=1798 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640n.jpg F5: Y mean 126.3 stddev 61.3 fnv=41ee4fdb78bfb525 -> /private/var/db/nord2-vcodec/out/jdec-dec640n-F5-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec dec1920f.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=780 164=3c0 16c=70e07 170=1fe0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3161 wall_us=4064 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5: Y mean 127.0 stddev 48.5 fnv=f6a0f791719db5d5 -> /private/var/db/nord2-vcodec/out/jdec-dec1920f-F5-1920x1088.420p.yuv (plausible)`
  - `N2JPGP hwdec dec1920f.jpg F5p regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=780 164=3c0 16c=70e07 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5p: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500242 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5s regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=280 164=140 16c=70e07 170=1fe0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5s: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3160 wall_us=4039 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5s: Y mean 124.2 stddev 47.1 fnv=f132bcfd2d63d94d -> (not saved) (plausible)`
  - `N2JPGP hwdec dec1920f.jpg F5w regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=780 164=3c0 16c=70407 170=1fe0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5w: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3228 wall_us=3750 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5w: Y mean 108.1 stddev 46.6 fnv=5b9e1f5f7f855717 -> (not saved) (plausible)`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=a500 270=11 31c=0 330=280 334=140 33c=0 344=a484 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 0f8=0 160=280 164=140 16c=70407 170=4b0 274=37 240=0`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=486 wall_us=660 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F5: Y mean 129.0 stddev 55.7 fnv=01011bf467f8fd11 -> /private/var/db/nord2-vcodec/out/jdec-jpeg-640x480-q90-F5-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec window 000-0fc: rc=0 errno=0 non-zero 2/64: 090=10 0fc=500`
  - `N2JPGP hwdec window 100-1fc: rc=0 errno=0 non-zero 16/64: 12c=13 134=ffffffff 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 180=11 184=80`
  - `N2JPGP hwdec window 200-2fc: rc=0 errno=0 non-zero 6/64: 23c=3fffffff 244=1 248=ffffffff 24c=ffffffff 270=11 278=88011000`
  - `N2JPGP hwdec window 300-3fc: rc=0 errno=0 non-zero 8/64: 318=ffffffff 31c=17f 32c=ffffff 330=ffc1 33c=10 344=ffffff00 350=ffff 354=ffffffff`
  - kernel: 33 `NORD2 JPEG/JPG` lines; first/last: `NORD2 JPEG: /dev/redwood-jpeg made (major 20, 0666), 24 MiB source + 16 MiB output at 0xd8000000` / `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 39 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec 30 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 3766 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 492 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 4733 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 1 irq 0 state 0x86e11002 mcu 0 in 500079 us`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 4684 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 4725 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 19775 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 1 irq 0 state 0x86e11002 mcu 0 in 500080 us`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 19868 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 1 irq 0 state 0x90011000 mcu 0 in 500007 us`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 1504 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 3161 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 1 irq 0 state 0x86e11002 mcu 0 in 500033 us`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 3160 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 3228 us`
  - kernel `NORD2 JPEG: dec 35 regs -> 0 irq 0x1 state 0x98011000 mcu 0 in 486 us`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
  - kernel `NORD2 JPEG: dec probe 64 regs -> 0 irq 0 state 0x88011000 mcu 0 in 0 us`
- [V-phone file] `apps-20261104/vcodec-fetch/jp8-jpegdec/jpeg-selftest.log`: `N2JPGP DONE pass=9 fail=0 skip=9`
  - `N2JPGP jetsam memlimit before active=6(attr 0, effective 6 MB) inactive=6(attr 1) get=0/0; set 512 MB fatal -> 0 errno=0 (ok); after active=512 inactive=512 get=0/0`
  - `N2JPGP info boot abi=1 max_pixels=12582912 jobs=0 failed=0 powered=0 last_hw_us=0 caps=0x1`
  - `N2JPG selftest 640x480 PASS 12/12`
  - encodes (size q bytes hw_us wall_us hw_ms/MP):
    - 640x480 q95: 71656 B, hw 964 us, wall 1197 us, 3.138 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=946
    - 640x480 q60: 14938 B, hw 859 us, wall 1056 us, 2.796 ms/MP
    - 1920x1080 q95: 450419 B, hw 4825 us, wall 6469 us, 2.327 ms/MP
    - 1920x1080 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=5365
    - 1920x1080 q60: 81379 B, hw 4927 us, wall 5359 us, 2.376 ms/MP
    - 4032x3024 q95: 2481768 B, hw 28523 us, wall 33233 us, 2.339 ms/MP
    - 4032x3024 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=35326
    - 4032x3024 q60: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=31163
    - 3840x2160 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=26474
    - 3264x2448 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=23996
    - 2592x1944 q90: 614859 B, hw 11721 us, wall 13136 us, 2.326 ms/MP
    - 640x480 q90: **FAILED** rc=-1 errno=5 (Input/output error) out_status=0 (ok) out_bytes=0 out_hw_us=0 wall_us=1806
    - 640x480 q95: 71656 B, hw 800 us, wall 989 us, 2.604 ms/MP
    - 640x480 q90: 42116 B, hw 750 us, wall 979 us, 2.441 ms/MP
    - 640x480 q60: 14938 B, hw 753 us, wall 866 us, 2.451 ms/MP
    - 1920x1080 q95: 450419 B, hw 4696 us, wall 5229 us, 2.265 ms/MP
    - 1920x1080 q90: 258695 B, hw 4658 us, wall 5126 us, 2.246 ms/MP
    - 1920x1080 q60: 81379 B, hw 4638 us, wall 5062 us, 2.237 ms/MP
    - 4032x3024 q95: 2481768 B, hw 27080 us, wall 34660 us, 2.221 ms/MP
    - 4032x3024 q90: 1441852 B, hw 26999 us, wall 29623 us, 2.214 ms/MP
    - 4032x3024 q60: 428605 B, hw 27216 us, wall 29363 us, 2.232 ms/MP
    - 4096x3072 q90: 1484387 B, hw 27346 us, wall 34842 us, 2.173 ms/MP
    - 640x480 q90: 42116 B, hw 729 us, wall 1722 us, 2.373 ms/MP
  - **12 MP (4032x3024) single job: FAIL: strips needed (see limit lines)**
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP PASS limit 2592x1944 q=90`
  - `N2JPGP PASS limit 4096x3072 q=90`
  - `N2JPG bench 1920x1080: encode 0 failed (fallback)`
  - `N2JPG bench 1920x1080 n=10 q=90 code=0x7 bytes=258695 wall_ms_frame=5.784 hw_ms_frame=4.651 wall_ms_per_mp=2.789 hw_ms_per_mp=2.243 wall_fps=172.9`
  - `N2JPG bench 4032x3024 n=3 q=90 code=0x7 bytes=1441852 wall_ms_frame=32.527 hw_ms_frame=27.245 wall_ms_per_mp=2.668 hw_ms_per_mp=2.234 wall_fps=30.7`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP FAIL library selftest 640x480 (11 quality codes + ladder)`
  - `N2JPGP FAIL enc 640x480 q=90`
  - `N2JPGP FAIL enc 1920x1080 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=90`
  - `N2JPGP FAIL enc 4032x3024 q=60`
  - `N2JPGP FAIL limit 3840x2160 q=90`
  - `N2JPGP FAIL limit 3264x2448 q=90`
  - `N2JPGP FAIL bench 1920x1080 q90`
  - `N2JPGP FAIL after idle (power on again) 640x480 q=90`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec640.jpg F0 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F2 (search miss: next step)`
  - `N2JPGP FAIL hwdec dec1920r.jpg F3`
  - `N2JPGP FAIL hwdec dec4032.jpg F3`
  - `N2JPGP SKIP hwdec dec640.jpg E2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4a3 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4b (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4c (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4e (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4f (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4g2 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4h (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg E4i (search miss: next step)`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP SKIP hwdec dec1920r.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec4032.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec jpeg-640x480-q90.jpg (no working step found on dec640.jpg)`
  - `N2JPGP SKIP hwdec dec640.jpg F0 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F1 (search miss: next step)`
  - `N2JPGP SKIP hwdec dec640.jpg F2 (search miss: next step)`
  - `N2JPGP FAIL hwdec dec1920r.jpg F3`
  - `N2JPGP FAIL hwdec dec4032.jpg F3`
  - `N2JPGP SKIP hwdec dec1920r.jpg F5p (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec1920r.jpg F5s (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec1920r.jpg F5w (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec4032.jpg F5p (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec4032.jpg F5s (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec4032.jpg F5w (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec1920f.jpg F5p (diagnostic: no decode)`
  - `N2JPGP SKIP hwdec dec1920f.jpg F5s (diagnostic: EOF + plausible Y)`
  - `N2JPGP SKIP hwdec dec1920f.jpg F5w (diagnostic: EOF + plausible Y)`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2318 us`
  - `N2JPGP PASS idle off: RWJ_INFO.powered 0`
  - `N2JPGP after idle: wall incl. power-on 2440 us`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000325 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000256 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000177 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000238 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000204 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000196 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000208 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000166 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000266 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000287 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000248 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000142 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000221 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000146 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000181 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000224 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000147 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000214 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000263 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, F0..F4, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg F0 regs(28): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F0: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500225 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F0 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F1 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=75f3 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=595 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F1 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F2 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=7600 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=614 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F2 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F3: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=514 wall_us=775 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg F3: Y mean 126.9 stddev 64.9 fnv=e79606ae38680ec1 -> /private/var/db/nord2-vcodec/out/jdec-dec640-F3-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec search on dec640.jpg: F3`
  - `N2JPGP hwdec dec1920r.jpg F3 regs(30): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000242 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec4032.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000754 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec window 000-0fc: rc=0 errno=0 non-zero 2/64: 090=10 0fc=500`
  - `N2JPGP hwdec window 100-1fc: rc=0 errno=0 non-zero 16/64: 12c=13 134=ffffffff 140=db900000 144=dc4a1000 148=dc78a000 14c=db900000 150=dc4a1000 154=dc78a000 158=fc0 15c=7e0 160=280 164=140 16c=70407 170=4b0 180=11 184=80`
  - `N2JPGP hwdec window 200-2fc: rc=0 errno=0 non-zero 6/64: 23c=3fffffff 244=1 248=ffffffff 24c=ffffffff 270=11 278=88011000`
  - `N2JPGP hwdec window 300-3fc: rc=0 errno=0 non-zero 8/64: 318=ffffffff 31c=17f 32c=ffffff 330=ffc1 33c=10 344=ffffff00 350=ffff 354=ffffffff`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg E2 regs(36): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 31c=0 330=0 334=0 338=0 33c=0 344=0 36c=0 370=0 374=0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000226 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000244 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=1 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000247 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=2 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000216 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4a3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=3 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4a3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000278 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4b regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=1e0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4b: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000240 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4c regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=0 334=0 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4c: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000163 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4e regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=db80c000 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4e: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000227 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4f regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=da807600 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4f: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000157 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g1 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4b0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000217 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4g2 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=0 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4g2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000179 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4h regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=0 36c=0 370=0 374=0 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4h: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000231 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg E4i regs(30): 090=0 090=1 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=4af 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg E4i: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000207 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec search on dec640.jpg: no step gave a picture`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, F0..F4, E2, E3, E4a..i`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg F0 regs(28): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F0: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500236 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F0 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F1 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=75f3 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F1: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=603 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F1 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F2 regs(29): 090=0 090=10 0f8=0 140=db900000 144=db94b000 148=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 248=da807600 24c=7600 270=11 320=0 14c=db900000 150=db94b000 154=db95e000 0fc=500 344=7600 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F2: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=568 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec after F2 (post-reset probe): rc=0 errno=0 -> 090=10 0f8=0 0fc=500 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP hwdec dec640.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F3: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=493 wall_us=658 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg F3: Y mean 126.9 stddev 64.9 fnv=e79606ae38680ec1 -> /private/var/db/nord2-vcodec/out/jdec-dec640-F3-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec search on dec640.jpg: F3`
  - `N2JPGP hwdec dec1920r.jpg F3 regs(30): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000311 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec4032.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F3: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=2000741 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F3 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=a500 270=11 31c=0 330=280 334=140 33c=0 344=a484 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F3: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=468 wall_us=704 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F3: Y mean 129.0 stddev 55.7 fnv=01011bf467f8fd11 -> /private/var/db/nord2-vcodec/out/jdec-jpeg-640x480-q90-F3-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec window 000-0fc: rc=0 errno=0 non-zero 2/64: 090=10 0fc=500`
  - `N2JPGP hwdec window 100-1fc: rc=0 errno=0 non-zero 16/64: 12c=13 134=ffffffff 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 180=11 184=80`
  - `N2JPGP hwdec window 200-2fc: rc=0 errno=0 non-zero 6/64: 23c=3fffffff 244=1 248=ffffffff 24c=ffffffff 270=11 278=88011000`
  - `N2JPGP hwdec window 300-3fc: rc=0 errno=0 non-zero 8/64: 318=ffffffff 31c=17f 32c=ffffff 330=ffc1 33c=10 344=ffffff00 350=ffff 354=ffffffff`
  - `N2JPGP S7 kernel decoder present (RWJ_INFO.reserved[0] = 1): RWJD_JOB steps E0/E1, G0, F5 (F0..F4, E2, E3, E4a..i)`
  - `N2JPGP hwdec E0/E1 probe: rc=0 errno=0 (ok) out_status=0 -> 090=10 0f8=0 0fc=0 140=0 144=0 148=0 14c=0 150=0 154=0 158=0 15c=0 160=0 164=0 16c=0 170=7ffffff 17c=0 200=0 20c=0 210=0 224=0 23c=3fffffff 240=0 248=ffffffff 24c=ffffffff 270=11 274=0 278=88011000 31c=17f 320=0 330=ffc1 334=0 338=0 33c=10 340=0 344=ffffff00 348=0 36c=0 370=0 374=0`
  - `N2JPGP PASS hwdec E0/E1 probe (power-on register values, no decoder write)`
  - `N2JPGP hwdec dec640.jpg G0 regs(30): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg G0: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3766 wall_us=4000 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg G0: Y mean 103.1 stddev 69.7 fnv=c1449211beb99d03 -> /private/var/db/nord2-vcodec/out/jdec-dec640-G0-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec dec640.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=7600 270=11 31c=0 330=280 334=140 33c=0 344=75f3 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 0f8=0 160=280 164=140 16c=70407 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec640.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=492 wall_us=715 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640.jpg F5: Y mean 126.9 stddev 64.9 fnv=e79606ae38680ec1 -> /private/var/db/nord2-vcodec/out/jdec-dec640-F5-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec search on dec640.jpg: F5`
  - `N2JPGP hwdec dec1920r.jpg F5 regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=780 164=3c0 16c=70e07 170=3f48 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=4733 wall_us=5885 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5: Y mean 127.8 stddev 64.5 fnv=67cd7d7dddc7a925 -> /private/var/db/nord2-vcodec/out/jdec-dec1920r-F5-1920x1080.422p.yuv (plausible)`
  - `N2JPGP hwdec dec1920r.jpg F5p regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=780 164=3c0 16c=70e07 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5p: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500379 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5s regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=280 164=140 16c=70e07 170=3f48 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5s: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=4684 wall_us=5893 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5s: Y mean 127.9 stddev 64.4 fnv=f955d893faea5b53 -> (not saved) (plausible)`
  - `N2JPGP hwdec dec1920r.jpg F5w regs(35): 090=0 090=10 0fc=400 14c=db900000 150=dbafb000 154=dbbf9000 17c=0 200=da800000 20c=1020300 210=3f47 224=7e8f 23c=7ffffd64 24c=1da00 270=11 31c=0 330=780 334=3c0 33c=0 344=1d99e 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafb000 148=dbbf9000 0f8=0 160=780 164=3c0 16c=70407 170=3f48 274=37 240=0`
  - `N2JPGP hwdec dec1920r.jpg F5w: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=4725 wall_us=5890 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=16200`
  - `N2JPGP hwdec dec1920r.jpg F5w: Y mean 127.8 stddev 64.5 fnv=3c6375fd90830bb9 -> (not saved) (plausible)`
  - `N2JPGP hwdec dec4032.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=fc0 164=7e0 16c=71f03 170=ba0c 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=19775 wall_us=23000 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec4032.jpg F5: Y mean 127.9 stddev 64.5 fnv=a431d8af34e26993 -> /private/var/db/nord2-vcodec/out/jdec-dec4032-F5-4032x3024.420p.yuv (plausible)`
  - `N2JPGP hwdec dec4032.jpg F5p regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=fc0 164=7e0 16c=71f03 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5p: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=501069 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec4032.jpg F5s regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=280 164=140 16c=71f03 170=ba0c 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5s: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=19868 wall_us=24565 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec4032.jpg F5s: Y mean 129.5 stddev 64.4 fnv=ed1970db59c0e1e0 -> (not saved) (plausible)`
  - `N2JPGP hwdec dec4032.jpg F5w regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dc4a1000 154=dc78a000 17c=0 200=da800000 20c=1020300 210=ba0b 224=2e82f 23c=7fff5924 24c=a7b00 270=11 31c=0 330=fc0 334=7e0 33c=0 344=a7aee 338=db800000 36c=db800000 370=db804000 374=db808000 158=fc0 15c=7e0 140=db900000 144=dc4a1000 148=dc78a000 0f8=0 160=fc0 164=7e0 16c=70407 170=ba0c 274=37 240=0`
  - `N2JPGP hwdec dec4032.jpg F5w: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500580 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=47628`
  - `N2JPGP hwdec dec640n.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=66e00 270=11 31c=0 330=280 334=140 33c=0 344=66dc2 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 0f8=0 160=280 164=140 16c=70407 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec640n.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=1504 wall_us=1798 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec dec640n.jpg F5: Y mean 126.3 stddev 61.3 fnv=41ee4fdb78bfb525 -> /private/var/db/nord2-vcodec/out/jdec-dec640n-F5-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec dec1920f.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=780 164=3c0 16c=70e07 170=1fe0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3161 wall_us=4064 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5: Y mean 127.0 stddev 48.5 fnv=f6a0f791719db5d5 -> /private/var/db/nord2-vcodec/out/jdec-dec1920f-F5-1920x1088.420p.yuv (plausible)`
  - `N2JPGP hwdec dec1920f.jpg F5p regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=780 164=3c0 16c=70e07 170=4b0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5p: rc=-1 errno=5 (Input/output error) out_status=0 (ok) hw_us=0 wall_us=500242 irq(0x274)=0 state(0x278)=0 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5s regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=280 164=140 16c=70e07 170=1fe0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5s: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3160 wall_us=4039 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5s: Y mean 124.2 stddev 47.1 fnv=f132bcfd2d63d94d -> (not saved) (plausible)`
  - `N2JPGP hwdec dec1920f.jpg F5w regs(35): 090=0 090=10 0fc=500 14c=db900000 150=dbafe000 154=dbb7e000 17c=0 200=da800000 20c=1020300 210=1fdf 224=7f7f 23c=7fff5924 24c=e280 270=11 31c=0 330=780 334=3c0 33c=0 344=e22c 338=db800000 36c=db800000 370=db804000 374=db808000 158=780 15c=3c0 140=db900000 144=dbafe000 148=dbb7e000 0f8=0 160=780 164=3c0 16c=70407 170=1fe0 274=37 240=0`
  - `N2JPGP hwdec dec1920f.jpg F5w: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=3228 wall_us=3750 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=8160`
  - `N2JPGP hwdec dec1920f.jpg F5w: Y mean 108.1 stddev 46.6 fnv=5b9e1f5f7f855717 -> (not saved) (plausible)`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F5 regs(35): 090=0 090=10 0fc=500 14c=db900000 150=db94b000 154=db95e000 17c=0 200=da800000 20c=1020300 210=4af 224=12bf 23c=7fff5924 24c=a500 270=11 31c=0 330=280 334=140 33c=0 344=a484 338=db800000 36c=db800000 370=db804000 374=db808000 158=280 15c=140 140=db900000 144=db94b000 148=db95e000 0f8=0 160=280 164=140 16c=70407 170=4b0 274=37 240=0`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F5: rc=0 errno=0 (ok) out_status=0 (ok) hw_us=486 wall_us=660 irq(0x274)=0x1 state(0x278)=0x98011000 mcu(0x340)=0 total_mcu=1200`
  - `N2JPGP hwdec jpeg-640x480-q90.jpg F5: Y mean 129.0 stddev 55.7 fnv=01011bf467f8fd11 -> /private/var/db/nord2-vcodec/out/jdec-jpeg-640x480-q90-F5-640x480.420p.yuv (plausible)`
  - `N2JPGP hwdec window 000-0fc: rc=0 errno=0 non-zero 2/64: 090=10 0fc=500`
  - `N2JPGP hwdec window 100-1fc: rc=0 errno=0 non-zero 16/64: 12c=13 134=ffffffff 140=db900000 144=db94b000 148=db95e000 14c=db900000 150=db94b000 154=db95e000 158=280 15c=140 160=280 164=140 16c=70407 170=4b0 180=11 184=80`
  - `N2JPGP hwdec window 200-2fc: rc=0 errno=0 non-zero 6/64: 23c=3fffffff 244=1 248=ffffffff 24c=ffffffff 270=11 278=88011000`
  - `N2JPGP hwdec window 300-3fc: rc=0 errno=0 non-zero 8/64: 318=ffffffff 31c=17f 32c=ffffff 330=ffc1 33c=10 344=ffffff00 350=ffff 354=ffffffff`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - `N2JPGP FAIL hwdec search: no step (E2, E3, E4) decoded the first input`
  - kernel: 14 `NORD2 JPEG/JPG` lines; first/last: `N2JPGP hwdec dec640.jpg F0: failed job: status/irq/state/mcu are only in the kernel line 'NORD2 JPEG: dec 28 regs -> ...' (console, in order)` / `N2JPGP hwdec dec1920f.jpg F5p: failed job: status/irq/state/mcu are only in the kernel line 'NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 28 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 28 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 29 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 30 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
  - kernel `NORD2 JPEG: dec 35 regs -> ...' (console, in order)`
- PC check (phone_check.py, rc 1):
  - `ENC PASS 1920x1080 q=60  bytes=81379    bpp=0.314 psnr y/u/v=40.65/52.44/54.94 libjpeg(q60) y/u/v=40.62/50.40/51.20 bytes=42522`
  - `ENC PASS 1920x1080 q=90  bytes=258695   bpp=0.998 psnr y/u/v=43.66/58.29/60.79 libjpeg(q90) y/u/v=42.84/57.67/59.76 bytes=243854`
  - `ENC PASS 1920x1080 q=95  bytes=450419   bpp=1.738 psnr y/u/v=44.66/59.02/64.94 libjpeg(q95) y/u/v=44.68/58.83/63.18 bytes=475723`
  - `ENC PASS 2592x1944 q=90  bytes=614859   bpp=0.976 psnr y/u/v=43.65/59.88/61.40 libjpeg(q90) y/u/v=42.86/58.76/60.34 bytes=574947`
  - `ENC PASS 4032x3024 q=60  bytes=428605   bpp=0.281 psnr y/u/v=40.65/56.92/58.34 libjpeg(q60) y/u/v=40.67/51.33/51.53 bytes=236481`
  - `ENC PASS 4032x3024 q=90  bytes=1441852  bpp=0.946 psnr y/u/v=43.68/60.84/63.39 libjpeg(q90) y/u/v=42.88/60.34/62.31 bytes=1348774`
  - `ENC PASS 4032x3024 q=95  bytes=2481768  bpp=1.628 psnr y/u/v=44.78/64.36/65.61 libjpeg(q95) y/u/v=44.66/65.95/66.40 bytes=2680319`
  - `ENC PASS 4096x3072 q=90  bytes=1484387  bpp=0.944 psnr y/u/v=43.68/62.36/63.25 libjpeg(q90) y/u/v=42.87/60.96/62.27 bytes=1393380`
  - `ENC PASS  640x480  q=60  bytes=14938    bpp=0.389 psnr y/u/v=40.67/54.15/54.15 libjpeg(q60) y/u/v=40.42/47.62/48.68 bytes=9520`
  - `ENC PASS  640x480  q=90  bytes=42116    bpp=1.097 psnr y/u/v=43.63/55.91/58.71 libjpeg(q90) y/u/v=42.75/57.62/55.47 bytes=39267`
  - `ENC PASS  640x480  q=95  bytes=71656    bpp=1.866 psnr y/u/v=44.75/58.92/99.00 libjpeg(q95) y/u/v=44.81/58.92/57.16 bytes=74961`
  - `DEC PASS dec1920f-F5 1920x1088 420p: Y vs libjpeg psnr=59.94 max|d|=1, RGB vs djpeg -nosmooth psnr=49.61`
  - `DEC PASS dec1920r-F5 1920x1080 422p: Y vs libjpeg psnr=76.59 max|d|=1, RGB vs djpeg -nosmooth psnr=52.90`
  - `DEC PASS dec4032-F5 4032x3024 420p: Y vs libjpeg psnr=77.09 max|d|=1, RGB vs djpeg -nosmooth psnr=52.94`
  - `DEC PASS dec640-F3 640x480 420p: Y vs libjpeg psnr=75.01 max|d|=1, RGB vs djpeg -nosmooth psnr=52.99`
  - `DEC PASS dec640-F5 640x480 420p: Y vs libjpeg psnr=75.01 max|d|=1, RGB vs djpeg -nosmooth psnr=52.99`
  - `DEC FAIL dec640-G0 640x480 420p: Y vs libjpeg psnr=8.29 max|d|=241, RGB vs djpeg -nosmooth psnr=4.38`
  - `DEC PASS dec640n-F5 640x480 420p: Y vs libjpeg psnr=65.66 max|d|=1, RGB vs djpeg -nosmooth psnr=52.11`
  - `DEC PASS jpeg-640x480-q90-F3 640x480 420p: Y vs libjpeg psnr=64.95 max|d|=1, RGB vs djpeg -nosmooth psnr=51.17`
  - `DEC PASS jpeg-640x480-q90-F5 640x480 420p: Y vs libjpeg psnr=64.95 max|d|=1, RGB vs djpeg -nosmooth psnr=51.17`
  - `JPEG_CHECK FAIL (1 failure)`
<!-- JPEG-RESULT jp8-jpegdec END -->
