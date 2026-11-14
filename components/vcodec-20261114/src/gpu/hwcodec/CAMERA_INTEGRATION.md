# Camera / AVFoundation on the hardware codecs (opus-cam, 12 Nov 2026)

Goal: Camera video recording encodes through the MT6893 VENC (vcodecd) and playback decodes through the VDEC, with the
software path as the fallback. Everything here is **host work**: code, host tests, iOS builds, staged payloads and fake-root
rehearsals. **Nothing in this document has run in Camera on the phone yet.** Tags: **[V-phone]** seen on the phone,
**[V-host]** host test or build, **[V-cache]** read in the iOS 16.7 dyld cache, **[I]** inferred.

## 1. What is staged

| Payload (gpu/build/apps-20261104/) | Contents | Use |
|---|---|---|
| `payload-vcodec-prod` (tag vc-prod1) | vcodecd `f974e33f964e4bcd`, Slim.videoencoder `43e3cf7d5c9eeb7e`, Slim.videodecoder `3e19b9de21f76376`, the 5 pinned vendor objects (hard links) | **production**: hardware encode and decode on, software fallback |
| `payload-vcodec-prod-sync` | same files, plus switch `nord2-video-hwenc-sync` | A/B: the encoder object without the pipeline (one picture at a time) |
| `payload-vcodec-prod-nodaemon` | same files, gate `run` off (vcodecd installed, never started) | fallback test: claim-hw + hwdec on, daemon absent |

All three: launchd job **com.nord2.vcodecd only**: `/usr/local/libexec/vcodecd --gate /private/var/db/nord2-vcodec-run
--vendor /usr/local/lib/n2vendor --hw-timeout-ms 6000 --restart-on-switch --decoder`, RunAtLoad, KeepAlive
`{SuccessfulExit: false}`, ThrottleInterval 10, Jetsam priority 140 / limit 3072 MB (the daemon also raises its own limit).
No selftest/probe/dprobe/jpeg jobs or files (a previous test install's are removed by install.json hash, its jobs by label,
its one-shot gates deleted). Video switches: `nord2-video-claim-hw` and `nord2-video-hwdec` on, `nord2-video-hwenc-sync` off.
Owned list: `owned-vcodec-prod.json` (union of every vcodec owned list + the new switch).

Built from the tree at 01:13-01:22: `bash gpu/hwcodec/ios/build.sh /tmp/n2hw-opus-cam/ios` (IOS TOOLS OK; vcodecd has
`--restart-on-switch`, `--decoder`, the vc24 "fresh process after any HEVC session" rule, auto level, 4096-width refusal) and
`bash gpu/hwcodec/ios/build_plugins.sh /tmp/n2hw-opus-cam/plugins` (PLUGINS OK). Rehearsals (fake root with the real
launchd.plist): `rehearse-vcodec.sh` REHEARSAL_OK for all three; install of `payload-vcodec-decall` (a test install) then
`payload-vcodec-prod` over it, then undo: one job left with the right arguments, test files/jobs/gates gone, undo restores the
previous Slim.videoencoder and gpu/video's record byte-identical (REHEARSAL2_OK, script kept at
`/tmp/n2hw-opus-cam/rehearse2.keep`, not in the tree).

## 2. Commands for the main session

    W=$NORD2_WORK
    cd $W/gpu/build
    # kernel: any image with the relay fix (q10je and later). q10jf = encode + bit-exact decode proven (7acf035f66021372);
    # q10jh (33deeaba5b53e093, pool at 0xA0000000) if vc26 passed. The payload is rootfs-only and kernel-independent.
    IMG=$W/gpu/build/device-mmu-q10jf-candidate/ui-sched/nord2-ios-metal-q10jf-ui654fb60dvfs-cap3500-ram3776-sched.img
    PAYLOAD=payload-vcodec-prod GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-prod.json \
      timeout 900 bash autocycle/txn.sh $IMG cam1-prod > autocycle/runs-cam1-prod.out 2>&1
    tail -3 cc-txn-cam1-prod/events          # committed / kernel flashed / capture done

    # Camera test, phone booted (sections 6/7). Live log first: it is the only way to see the plug-in lines now.
    cd $W/gpu/build/autotest
    python3 phone.py wait && python3 phone.py logs /dev/null                # log offset up to date
    python3 phone.py scenario video runs/cam1-video --wait-s 20            # launch camera, VIDEO, record ~20 s, stop
    python3 phone.py logs runs/cam1-video/after.log --seconds 20            # file finalisation + thumbnail decode
    python3 $W/gpu/hwcodec/cam/cam_check.py log runs/cam1-video/kernel.log   # VERDICT line (section 6)
    python3 phone.py tap 114 2086; sleep 3; python3 phone.py tap 540 1200; sleep 8     # open the clip, play it (decode)
    python3 phone.py logs runs/cam1-video/play.log; python3 $W/gpu/hwcodec/cam/cam_check.py log runs/cam1-video/play.log

    # the .MOV and vcodecd.log arrive with the NEXT transaction's snapshot (mirror), e.g. the undo or the next payload:
    bash $W/gpu/video/install/fetch_media.sh cam1                              # -> gpu/video/build/phone/cam1/DCIM/...
    python3 $W/gpu/hwcodec/cam/cam_check.py movie $W/gpu/video/build/phone/cam1/DCIM/*/IMG_*.MOV
    python3 $W/gpu/build/apps-20261104/vcodec_check.py fetch cam1             # -> vcodec-fetch/cam1/vcodecd.log

    # undo (back to the software plug-in, no daemon):
    PAYLOAD=payload-vcodec-prod GUEST=guest-vcodec-undo.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-prod.json \
      timeout 900 bash autocycle/txn.sh $IMG cam1-undo > autocycle/runs-cam1-undo.out 2>&1

Rebuild/restage after a source change (host only):

    bash $W/gpu/hwcodec/vt/test/run_tests.sh                         # N2TEST P04 PASS 8517/8517
    bash $W/gpu/hwcodec/ios/build.sh /tmp/n2hw-opus-cam/ios && bash $W/gpu/hwcodec/ios/build_plugins.sh /tmp/n2hw-opus-cam/plugins
    cd $W/gpu/build/apps-20261104 && P=/tmp/n2hw-opus-cam
    bash stage-vcodec.sh vc-prod1 --prod --vcodecd $P/ios/vcodecd --encoder-plugin $P/plugins/Slim.videoencoder \
      --decoder-plugin $P/plugins/Slim.videodecoder --payload $PWD/payload-vcodec-prod
    #   -sync variant: add --video-on claim-hw --video-on hwdec --video-on hwenc-sync (payload-vcodec-prod-sync)
    #   -nodaemon:     add --off run --off selftest --off probe --off dprobe --off jpeg (payload-vcodec-prod-nodaemon)
    N2_REHEARSE_DIR=/tmp/n2hw-opus-cam/rh bash rehearse-vcodec.sh payload-vcodec-prod | tail -2      # REHEARSAL_OK

## 3. How a recording reaches the VENC

1. **Registration.** VideoToolbox calls `SLIMEncoderRegister()` of Slim.videoencoder once per process (mediaserverd
   [V-phone: `N2VIDEO mediaserverd[300]: registered ...`]). With `nord2-video-claim-hw` the shim registers P04's
   `avc1`/`hvc1`/`hev1` objects (IsHardwareAccelerated, Rating 100) and re-registers n2avc / VCPHEVC as hardware too, so a
   client that requires a hardware encoder still finds the software one when the VENC path fails. Expected line:
   `hardware encoders registered: 3` [V-host]. VideoToolbox sorts by Rating, then hardware, then descending ID [V-cache].
2. **Selection and fallback.** `VTCompressionSessionCreate` calls our StartSession; on **any** non-zero status it releases
   our instance and tries the next candidate (n2avc for avc1, VCPHEVC for hvc1) [V-cache 0x194cd5b20..0x194cd5c70]. Our
   StartSession connects to the relay, sends OPEN and returns `-12915` when that fails, bounded to about 2 s (section 5).
3. **Camera's calls** [V-phone, user6 live log 11 Nov, software path]: at recording start mediaserverd first opens and closes a
   640x480 session with no pictures, then the 1920x1080 session; the first picture had `bitrate 15303168, fps 30/1, keyint
   30`, pixel format `420v`; the session is invalidated right after "Stopping recording", before the file is finalised and
   before any thumbnail. ProfileLevel, RealTime, AllowFrameReordering and MaxFrameDelayCount values are **not** in that log
   (n2avc did not print them) [I]: the hardware object prints them in its first-picture line.
4. **Parameters** (OPEN = what vcodecd's encoder session is opened with):

   | Camera property | OPEN field | Notes |
   |---|---|---|
   | AverageBitRate (15.3 Mbit/s seen) | bitrate | 0 = vcodecd default w*h*4 |
   | ExpectedFrameRate | fps_num/fps_den (x1000, reduced) | vcodecd uses fps_num/fps_den for rate control |
   | MaxKeyFrameInterval (30) / ...Duration | gop = min(interval, duration*fps) | 0 = vcodecd default (fps) |
   | ProfileLevel `H264_<Baseline|Main|High>_<x_y|AutoLevel>`, `HEVC_Main_...` | profile_idc 66/77/100, HEVC 1; level_idc or 0 | level 0 = vcodecd's auto level from size*fps [V-host]; vendor: H.264 Baseline/Main/High up to 5.1, HEVC Main only [V-host QUERY_CAP]; Main10 is refused by vcodecd -> fallback |
   | RealTime | flags bit 0 | stored, no daemon effect |
   | AllowFrameReordering=false | — | the VENC never makes B pictures: DTS = PTS always |
   | MaxFrameDelayCount | pipeline depth | 0 forces one picture at a time |

   Camera sets its properties **after** VTCompressionSessionCreate, i.e. after our StartSession OPEN. Before the first
   picture nothing is sent; at the first picture the object reopens the daemon session if any OPEN field changed (always,
   with Camera's bitrate) and also if the StartSession OPEN is older than 8 s (vcodecd drops a session idle for 10 s).
   **ENC_SET has never run on the phone**, so bitrate/fps changes before the first picture go into that OPEN, and an ENC_SET
   refused later is logged and ignored (the stream keeps its rate) instead of killing the recording.
5. **Pictures.** CVPixelBuffer `420v`/`420f` (IOSurface backed; the object announces both, width/height and an empty
   IOSurface dictionary). The daemon wants NV12 at stride align16(width), chroma at stride*align16(height): 1080p
   in_buf = 3133440 = 1920x1088x1.5 [V-phone vc16 OPEN_OK]. P03's client copies a plane with ONE `RWVC_DMA_RW` (copyin + cache
   clean) when the source pitch equals that stride, and with one ioctl **per row** otherwise (1620 ioctls per 1080p
   picture). The object therefore repacks into a session-stride staging buffer whenever a pitch differs (the first-picture line
   says `direct copy` or `repacked` and prints the pitches; the camera's pitch is unknown [I]). Rows 1080..1087 of the
   hardware buffer are not written: they only feed the cropped last macroblock row [I].
6. **Access units.** vcodecd returns Annex B with 4-byte start codes and prepends SPS/PPS(/VPS) before every IDR. The object
   strips the parameter sets, converts start codes to 4-byte lengths in place, builds the format description once from the
   first key frame's sets (`CMVideoFormatDescriptionCreateFromH264/HEVCParameterSets`, NAL length 4; our avcC/hvcC builder is
   byte-identical to ffmpeg's extradata [V-host]), and emits a CMSampleBuffer: PTS and duration as given, DTS = PTS,
   `NotSync`/`DependsOnOthers` false on key frames and true otherwise. The vendor writes BT.709 colour in the VUI for widths
   >= 1280 and BT.601 below [V-host/phone streams]; the full-range flag follows the vendor, not `420f` [I].
7. **Pipeline** (new). VideoToolbox runs `encodeFrame` synchronously on its serial compression queue with **no VT lock held**,
   and `completeFrames` the same way; emit takes VT's pending-frame mutex only briefly and never across the client callback;
   **VT never waits for pending frames after completeFrames returns** and ignores `infoFlagsOut`; invalidate does not call
   completeFrames [V-cache, Fable 5.2 report: encode slot call 0x194cd6d24, emit 0x194cd282c, CompleteFrames 0x194cd2ae8,
   Invalidate 0x194cd2c14]. Hence:
   * a worker thread owns all client calls; `encodeFrame` validates, retains the CVPixelBuffer, queues it and returns;
   * finished pictures are emitted **only from VT's own calls** (the next `encodeFrame`, or `completeFrames`), in order,
     outside the object's lock; the worker never touches VideoToolbox;
   * at most 2 pictures in flight (one in the daemon, one queued); a full queue makes `encodeFrame` wait for the oldest;
   * `completeFrames` drains everything; every wait is bounded (2.5 s per picture), after which each outstanding picture gets
     one malfunction emit and the session is fatal; `invalidate` stops and joins the worker and releases queued pictures
     without emitting.
   Throughput: the daemon serialises pictures; 1080p H.264 was 29 ms hw + copy = one picture per 31-32 ms in the probe
   (vc21 timestamps) [V-phone, daemon only], so ~30 fps is the ceiling at 273 MHz; the pipeline hides the latency from
   Camera's thread and overlaps the emit/conversion with the next encode, it cannot overlap two hardware encodes. HEVC 1080p
   ~25 ms. 4K H.264 69 ms/picture = ~14 fps at 273 MHz (624 MHz via q10jg kern.n2vc.venc_fast, untested, ~25 fps) [V-phone
   probe].
8. **Playback.** In apps, VTDecompressionSession goes to the media server [V-cache]; Slim.videodecoder (DEC06) registers
   only with `nord2-video-hwdec`, Rating 100, and its CreateInstance probes vcodecd with a 250 ms HELLO (unavailable ->
   -12913 -> Apple's H264SW/VCPHEVC). A **StartSession** failure is not retried for decoders [V-cache], see risks.

## 4. Code changes (all host-tested; marked opus-cam)

* `gpu/hwcodec/vt/n2hwenc_vt.c` (P04): pipeline + deferred emission; bounded StartSession (`open_with_retry`: no retry when
  the daemon is down (ENXIO), BUSY and the restart answer retried for 600 ms; the first-picture reopen retries for 1.5 s,
  needed for HEVC since vcodecd restarts for any open after an HEVC session); no ENC_SET before the first picture, non-fatal
  after; stride repack; idle-reopen guard; MaxFrameDelayCount; `NumberOfPendingFrames` real; console lines
  `n2hwenc: ... hardware session open in N ms`, `first picture ... pitch ... in flight N`, `session ... ended: ... client N
  us/picture ... (X fps submitted)`; `n2hwenc_set_max_in_flight()`; switch `/private/var/db/nord2-video-hwenc-sync`.
* `gpu/hwcodec/vt/n2hwenc_client.c/.h`: timeouts connect 400 ms / OPEN 1 s / frame 1 s; `N2HWENC_CLIENT_BUSY` and
  `N2HWENC_CLIENT_RESTART` (BUSY + `N2VCD_DETAIL_RESTART`); a process-wide lock around every relay request of the encoder
  objects (the relay's client identity is the PROCESS, so two waiters in mediaserverd could take each other's replies).
* `gpu/hwcodec/vt/test/` (t_vt.c, t_fakeclient.*): ENC_SET expectations updated; new checks: the daemon receives the session
  stride and exactly the picture's bytes on both the repack and the direct path (FNV hashes), pipelined streams with 2 and 3
  in flight (AVC and HEVC, order/tokens/timing/samples exact, emits really deferred), ENC_FRAME failure mid-pipeline (one
  emit per token), invalidate with pictures in flight (no emit after, all buffers released), a 3.2 s hang (completeFrames
  returns within the bound, late answer discarded), BUSY x2 and restart retries, restart at the first-picture reopen, BUSY
  forever (fallback in 0.5-2 s). `bash gpu/hwcodec/vt/test/run_tests.sh` -> `N2TEST P04 PASS 8517/8517` (ASan+UBSan, -O2,
  TSan clean, P03 link, arm64-ios compile). `vt/symbols.txt` lists the new libSystem imports.
* `gpu/hwcodec/ios/build_plugins.sh`: the decoder product was never built (the `^SLIMRegister` pattern missed DEC06's
  one-line definition); it now builds Slim.videodecoder with DEC05's client (`client/dec`, the vc22 bit-exact path),
  opus-mm21's converter, `n2vt_log.c` and supplementary decoder TBDs.
* `gpu/build/apps-20261104/stage-vcodec.sh --prod`, `guest-vcodec.sh` (plan `prod`: vcodecd only, no test jobs; switch
  `hwenc-sync` allowed), `rehearse-vcodec.sh` (works for payloads without a probe; `N2_REHEARSE_DIR`), `owned-vcodec-prod.json`.
* `gpu/hwcodec/cam/cam_check.py`: log and movie verdicts (section 6).

## 5. Failure behaviour (mediaserverd must never hang or crash)

| Situation | What happens | Bound |
|---|---|---|
| vcodecd not running (gate off, crashed, between restarts) | relay send answers ENXIO -> StartSession -12915 -> n2avc/VCPHEVC | < 50 ms |
| daemon busy (another session, decoder open) | OPEN BUSY retried 600 ms, then -12915 | ~0.7 s |
| daemon restarting (codec switch / after HEVC) | restart answer -> reconnect + OPEN until 600 ms (StartSession) / 1.5 s (first-picture reopen) | <= 2 s |
| daemon hung during HELLO/OPEN | client timeouts 400 ms + 1 s -> -12915 | <= 2.0 s total |
| daemon hung during a picture | client timeout 1 s -> malfunction emit for that and later pictures, session fatal, recording stops with an error (no mid-stream fallback) | 1 s per call, 2.5 s per drain wait |
| first-picture reopen refused | session fatal at picture 0 (no fallback possible after StartSession) | <= 1.5 s |
| ENC_SET refused | logged once, ignored | - |
| vendor guest stopped (E_GUEST) | session fatal; vcodecd exits 70, launchd restarts it; the next recording gets the hardware again | ThrottleInterval 10 s |
| relay errors / malformed replies | P03 client marks the connection dead -> E_SESSION -> session fatal | - |
| Camera never calls completeFrames | pictures still queued are released at invalidate, never emitted (VT does not wait for them) | - |
| daemon-side switch for A/B | `nord2-video-hwenc-sync` -> synchronous object; remove `nord2-video-claim-hw` -> software only | next process start |

## 6. Camera test plan and pass criteria

Run on a fresh boot (gpu/video's console channel prints at most **40 lines per process**; mediaserverd's budget is spent
after ~4 recordings). Per clip: 15-30 s, back camera 1080p30 first, then front, then 4K30 if offered.

Live log (cam_check.py log):
1. `N2VIDEO mediaserverd[..]: hardware encoders registered: 3` and `N2VIDEO mediaserverd[..]: N2VDEC registered 2 hardware
   decoder(s)` (or `(avc1...)`) — plug-ins loaded with the switches.
2. vcodecd up before Camera: `N2VCD ... serving /dev/redwood-vcodec (encoder + decoder)` ~0.3 s after launch (vc10..vc22:
   0.29-0.33 s, footprint 4.2 MB).
3. Recording start: `n2hwenc: avc 640x480: hardware session open in N ms` then the same for 1920x1080 (N < 200),
   `N2VCD ... session K open client=... h264 1920x1080@30/1 ...bps`, then `n2hwenc: first picture 1920x1080 420v pitch P/P
   (session stride 1920, direct copy|repacked): avc, bitrate 15303168, fps 30/1, gop 30, ProfileLevel ... in flight 2`.
   **FAIL** if instead `n2avc: session 1920x1080 started` (software chosen) — read the `falls back` line for the reason.
4. Recording stop: `n2hwenc: session 1920x1080 ended: N pictures ..., 0 failures; client U us/picture (longest L), ...
   (F fps submitted)`; `N2VCD ... session K close frames=N ...`. **PASS**: failures 0, U < 33000 at 1080p30, F close to the
   camera's delivered rate; compare with the software baseline **21 fps** (user6: 814 pictures in 38.7 s, n2avc 22.6 ms per
   picture). Note the camera itself may deliver < 30 fps in low light; F is what Camera submitted, not an encoder limit.
5. VERDICT line: `hw_encoder_used=yes software_fallbacks=0 hw_errors=0`.

Movie (next transaction's snapshot; `fetch_media.sh NAME`, then `cam_check.py movie`): codec h264, profile = what Camera asked
(High expected [I]; Baseline if it asked nothing), 1920x1080, `encoder: hardware VENC (1 slice/picture)` (n2avc writes 4),
host decode OK, fps = frames/duration vs 21. The audio track is unrelated (silent since 11 Nov).

Decode (Photos/AVPlayer through Camera's thumbnail; `phone.py tap 114 2086`, then play at the centre):
`N2VIDEO mediaserverd[..]: N2VDEC session start 61766331 1920x1080 ...`, `N2VDEC first picture pts=...`,
`N2VCD ... dec: session K open client=... codec=0x61766331 fmt=MM21` ... `close`. The clip plays without artefacts (a
screenshot during playback). `N2VDEC avc1 decoder not offered: daemon unavailable` means the 250 ms probe failed
(software decode, still a pass for playback, a fail for the hardware path).

Fallback tests: (a) `payload-vcodec-prod-nodaemon`: recording must work through n2avc (`falls back` line, then `n2avc:
session 1920x1080 started`, clip valid), playback through Apple's decoder; (b) during a production run, `phone.py` cannot
stop vcodecd, so kill tests stay on the PC (test_async_failures).

A/B: `payload-vcodec-prod-sync` gives the synchronous object; compare F and `longest wait` with the pipelined run.

## 7. Risks and open items

1. **Never run in Camera.** First contact may show: Camera's CVPixelBuffer pitch (repack cost ~2 ms/picture at 1080p if
   padded), ProfileLevel strings we parse differently, a property Camera insists on (an unknown key is accepted and printed).
2. **Relay identity is per process.** The encoder objects serialise their requests (process-wide lock in
   n2hwenc_client.c), but Slim.videodecoder is another image: an encode and a decode in mediaserverd at the same time could
   take each other's replies (connection marked dead -> that session fails). The camera log shows the encoder invalidated
   before the file and thumbnail are written, so recording + thumbnail do not overlap [V-phone timing, software path].
   Proper fix (proposed to P03/DEC05 owners): an `flock` on a lock file held from send to reply in `client/n2vcd_transport.c`'s
   relay path, or per-descriptor client ids in the kernel relay.
3. **Decoder StartSession is not retried by VT**: if vcodecd is busy with an encoder session or restarting when a decode
   starts, DEC06's StartSession fails and that playback fails (no fallback). DEC06 requests use 5 s timeouts (DEC05 default).
   Proposal: vcodecd reports busy in HELLO_OK.reserved and DEC06's CreateInstance returns -12913 then.
4. **One session at a time in vcodecd**: encode and decode are exclusive (BUSY), so a second encoder in the same moment falls
   back to software (by design).
5. **HEVC recording**: every HEVC stream costs one vcodecd restart at the first-picture reopen (~0.3-0.5 s on the first
   picture, inside encodeFrame); the 640x480 probe session + the real session = two restarts per HEVC recording; launchd's
   ThrottleInterval 10 s can delay a respawn if the daemon ran < 10 s. Camera recorded H.264 so far [V-phone].
6. **4K30** encodes at ~14 fps at 273 MHz: Camera would record 4K at about half rate.
7. **vcodecd.log grows without bound** (StandardOutPath, phases line every 10th picture); propose a size check at start.
8. **No mid-stream fallback**: a daemon failure during a recording ends that recording with an error.
9. gpu/video's log budget of 40 lines per process hides later sessions' lines; vcodecd.log keeps the daemon side.
10. Full range: `420f` pictures are encoded with whatever range flag the vendor writes [I].
