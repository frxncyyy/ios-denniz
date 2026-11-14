# Critical path to the first on-phone hardware H.264 encode (and the one big build)

Owner of this file: opus-lead2 (after the 18:20 restart; was opus-lead). Updated: 2026-11-11 19:25. Status words: DONE (host-tested),
OPEN, RUNNING, PHONE (needs a phone cycle by the main session). On the phone so far: only the JPEG encoder (0298, q10hw1).
The procedure for the first encode is `FIRST_PHONE_TEST.md` (one transaction, hands-free probe at boot).

| # | Item | Owner | Status |
|---|---|---|---|
| 1-7 | Vendor set to /dev/vcu, host session, GET_LOG blocking, interpreter speed, kernel waits, VENC power per GCE flush, 273 MHz default | hw-iv, opus-venc, V40, opus-perf, K02 | DONE (see history below) |
| 8 | Image: **q10jy** 501d794c = q10jx + K02 rev 7 (VDEC_ALL + larb allow-list) + mic 0320/0321 + I2C8/camera 0330/0331 | opus-kern2 | DONE 18:4x, `FAILURES: none`, spare 6144 B. Fallback q10jx df4ce447 |
| 9a | V40 rev 6: VENC bitstream mirror refresh after each GCE list (header+zeros bug, opus-hevc HIGH) | hw-v40 | DONE 1536/1536, H.264 + HEVC sessions validate on the plain tree |
| 9b | vcodecd: separate SEQ_HDR job at OPEN (+ V41's n2vc_dma registration fix), wait_done, HEVC level 4.0 default | hw-ip | DONE rev 9 (vcodecd.c e95aa2aa, IP 854/854, IP-REAL 33/33: 2 real-vhost frames ffmpeg-decoded) |
| 10 | Rebuild + restage `vc2-encode` + rehearsal + run_session(_hevc) on the plain tree | opus-lead2 | DONE 19:2x: vcodecd c623fd58, Slim.videoencoder ae914887, REHEARSAL_OK; H.264 115841 B + HEVC 43947 B validated; IP suite 854/854 re-run on the tree with rev 3 |
| 10b | Plug-in set in the same transaction: Slim.videoencoder (gpu/video + P04 behind claim-hw) [+ Slim.videodecoder behind hwdec] | opus-lead2 | DONE for the encoder (opus-dec3 patches 1-3 applied, P04 5031/5031, WP01 31/31, `ios/build_plugins.sh`); decoder plug-in: staging ready, product not yet (DEC06) |
| 11 | Header rev 3 (RWVC_WIN_VDEC_ALL) in include/ + T02 fake + V40 dec04.patch | opus-lead2, hw-v40 | header + fake LANDED 19:1x (T02 6194/6194, V40 1536/1536); shim layout.mode=0 LANDED; dec04.patch: V40 RUNNING |
| 12 | Phone: q10jy flash + vcodec transaction, `N2VCDP DONE ok=1`, AU decodes on the PC | main session | PHONE: after 10 |
| 13 | VideoToolbox `avc1`/`hvc1` backed by vcodecd (P04) | hw-p04 | DONE host; enabled on the phone only by the switch file nord2-video-claim-hw, after item 12 passes |
| D1 | Decode: DEC01 ap/n2ap_vdec.c | hw-dec01 | RUNNING: opus-lead2 review 18:5x = 4 HIGH (default 1 plane, single-alloc chroma not synced, list_free-before-disp drop, layout check) + START data[0]/[1] swap (opus-hevcdec) |
| D2 | Decode: DEC05 daemon service + client | hw-dec05, hw-ip | DONE 1150/1150 but review = 7 HIGH (version bump breaks encoders -> VERSION stays 1; HELLO/CLOSE routing; NEED_MORE bit; ion share leak; one relay queue per process). vcodecd integration only behind `--decoder` |
| D3 | Decode: DEC06 Slim.videodecoder (+ opus-mm21 detile) | hw-dec06, opus-mm21 | RUNNING |

## Go/no-go for the one big build (opus-lead2, 19:25)

**GO** for: image q10jy (501d794c) + ONE rootfs transaction `vc2-encode` as staged (vcodecd c623fd58 + selftest + boot probe +
the five vendor objects + Slim.videoencoder ae914887 with the hardware encoders OFF). Procedure: FIRST_PHONE_TEST.md step 3.
**Not in it (NO-GO today, by design):** decoder support. vcodecd has no decoder service yet (DEC05 needs 7 HIGH fixes and IP
integration behind `--decoder`), DEC01 has 4 HIGH open, Slim.videodecoder (DEC06) does not exist. The kernel side of decode
(VDEC_ALL window, larb allow-list) is already in q10jy, so decode needs no new image: a later rootfs-only payload with
`--decoder-plugin` and a vcodecd built with the service (switch nord2-video-hwdec OFF) is enough.

Remaining risks of the big build, in order: (1) real VENC behaviour not modelled by the fake (RCPU handshake, +0x5c status,
bus hang on the never-driven larb7 ports) - one-shot gate + KeepAlive false bound the cost to one recovery; (2) the replaced
Slim.videoencoder: its default registration is identical to the proven f23ce9eb (host WP01 31/31, P04 not registered without
claim-hw), new imports are libSystem `_ioctl`/`_strerror` + weak `kVTVideoEncoder_Rating`; if Camera recording regresses,
the vcodec undo restores f23ce9eb byte-identically; (3) iOS process facilities in a real daemon (4 GiB reservation, fault
recovery, TLS) - the selftest reports them separately; (4) VEN at 624 MHz if /dev/redwood-jpeg powers it first (do not open
Camera before `N2VCDP DONE`); (5) q10jy boot spare 6144 B and the new mic/I2C8 patches are host-tested only (fallback q10jx).
Known limits, not blocking: P04 registration ignores nord2-video-no-avc/no-hevc (only -off stops it); mediaserverd's sandbox
may refuse /dev/redwood-vcodec-client when claim-hw is switched on later (falls back to n2avc).

## Biggest risks for item 12, in order
1. The real VENC needs behaviour the fake does not model (RCPU microcode handshake, status bits): only the phone answers.
   The first-done kernel line (+0x5c status, GIC 340 pending) and the RWVC trace around M-e..M-f are what to capture.
2. iOS process facilities never exercised in a real daemon (4 GiB reservation, SIGSEGV recovery, TLS): the selftest at the same
   boot reports them separately (N2VCDT).
3. Clock/vcore: 273 MHz is the safe step; if q10jw is flashed instead, 624 MHz on an unmanaged vcore.
4. Bus hang on first VENC job (larb7 VENC ports never driven by a master on this phone before): the probe gate is one-shot,
   KeepAlive false, so a hang costs one recovery, not a boot loop.

## Review round 2 (opus-lead, 17:30), first-encode path
Applied: `daemon/vcodecd.c` stuck-request watchdog (exit 71 -> kernel last-close powers VEN off); `guest-vcodec.sh`
`--hw-timeout-ms 6000` (was 2000 = 1 s per IPI half, too short for interpreted vendor INIT/first IDR; an AP timeout leaves the
guest mid-job and session_end's raw power-off loop would cut VEN under a running GCE list). `fakevenc/apply_patches.py` now
recognises the owners' merged forms (plain tree: all ALREADY).
Posted, not blocking the first encode: V40 clear VENC IRQ before each list + honour power-on errors (mitigated today because
VEN is power-cycled per list, which resets +0x5c); V41 abort use-after-free + power refs in ven_free; IP wait for the guest
before the raw power-off; kernel: wait for done before power-off on close (only if the daemon dies mid-job), clock log line.
Checked OK: kernel waits sleep + drop the mutex, 5 s max, POLL 1 s, batch 2 s; close releases every ref per file; JPEG/VENC
shared refcount; no zero-sleep spin; all cond waits timed; no fd leak on open errors; 4 GiB reservation made once per process;
vpud survives idle GET_OBJECT timeouts (host run idle > 40 s); probe timeouts (OPEN 30 s, frame 10 s) > daemon bounds.

## History: the table as opus-lead left it (17:30, before the restart)

| # | Item | Owner | Status |
|---|---|---|---|
| 1 | Vendor set under the interpreter to its first `/dev/vcu` ioctl (M-c) | hw-iv | DONE |
| 2 | Full host session INIT/SET_PARAM/SEQ_HDR/IDR/EOS on the fake VENC, **plain tree** (all 4 opus-venc patches merged by V34/V41/IV/V40; `fakevenc/apply_patches.py` prints only ALREADY) | opus-venc, owners | DONE 17:15 (opus-lead run: 320x240 stream validated, `power_on_venc=3`, `log_gets=0`) |
| 3 | `VCU_GET_LOG_OBJECT` blocks (no spin) | hw-v40 | DONE (rev 4/5, log_gets=0 in 5 s) |
| 4 | Interpreter speed | opus-perf | DONE: ~12k guest insns/frame, <1 ms/frame; not a risk |
| 5 | Kernel waits sleep, mutex dropped; window 0x3000; last-close releases all slots | hw-k02 / opus-kern | DONE in q10jw (0310) |
| 6 | VENC power around every GCE flush (no power IPI in GCE mode) + callback after a failed list | hw-v40 | DONE (V40 rev 5) |
| 7 | 273 MHz default at VENC power-on (root decision 17:08; dedicated forcing mux, only while VEN is off) | hw-k02 | DONE 17:27 (sha 7ded4040, K02 588891/588891 incl. -DN2VC_VENC_SLOW=0; 0311 compiles unchanged via a 3-arg macro) |
| 8 | Image q10jx = q10jw + item 7 | opus-kern | OPEN: build now (K02 says the 0311 build break is solved with a macro, so at.inc needs no edit). Fallback: q10jw (624 MHz), payload is ABI-compatible |
| 9 | vcodecd final (slbc_ready = 0 explicit, `--venc-slow` dropped) | hw-ip | DONE (IP 765/765; builds green 17:2x) |
| 10 | iOS products + payload `vc2-encode` staged + rehearsal | opus-lead | DONE 17:23: vcodecd e23f0691 (IP rev 6/7 + watchdog), launchd `--hw-timeout-ms 6000`, `REHEARSAL_OK` |
| 11 | Review of the first-encode path | opus-lead | DONE round 2 (below); applied: daemon watchdog, 6 s IPI bound; posted: V40 stale-IRQ clear + power-on result, V41 abort UAF + power refs, IP wait-before-power-off, K02 clock log line |
| 12 | Phone: the q10jx flash + vcodec transaction, `N2VCDP DONE ok=1`, AU decodes on the PC | main session | PHONE: next |
| 13 | VideoToolbox `avc1` backed by vcodecd | hw-p04, hw-p03 | RUNNING, not needed for item 12 |

