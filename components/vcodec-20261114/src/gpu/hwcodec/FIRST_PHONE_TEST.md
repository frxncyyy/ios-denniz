# First on-phone hardware H.264 encode: the procedure

Owner: opus-lead2 (was opus-lead). 11 November 2026, revised 18:45 for the **combined image q10jy**. For the main session (the
only one that touches the phone). One flash, one boot does the whole test hands-free: the kernel image carries
`/dev/redwood-vcodec` (0310, K02 rev 7: 273 MHz default + VDEC_ALL window + larb4/5 SLP_CON allow-list), the mic node (0320/0321)
and the camera/speaker I2C8 fix (0330/0331); the rootfs transaction installs vcodecd + the phone's own five vendor objects + a
boot-time probe client and (when staged with `--encoder-plugin`) the VideoToolbox plug-in with the hardware encoders OFF.

## 0. What this test proves (wording for the log/STATUS)

Write exactly one of these, by the outcome in section 4:

- **L3 (goal):** "The MT6893 VENC, driven by the phone's own unmodified vendor encoder libraries hosted in vcodecd on iOS 16.7,
  encoded a 640x480 test pattern to H.264 that decodes on the PC (N frames)." Requires `N2VCDP DONE ok=1` **and**
  the first access unit rebuilt from the log decoding with ffmpeg.
- **L2:** "The VENC ran a vendor-programmed encode job to completion on the phone (done interrupt status seen), the bytes did not
  (fully) decode." Proves power, clocks, larb7/IOMMU bypass, GCE-list emulation, register protocol; the stream format is the bug.
- **L1:** "The vendor encoder stack initialised on iOS against the real device (INIT_DONE, register writes reached the VENC)
  but no job completed." Proves the daemon side only.

Not proved by any outcome: frame rate, 1080p, P frames, HEVC, decode, VideoToolbox integration, 624 MHz operation.

## 1. Prerequisites (host, before the user is asked to flash)

| Item | Check | Who |
|---|---|---|
| Kernel image **q10jy** (opus-kern2 DONE 18:4x): q10hq S + 0296,0298,0299,0300,0310,0311,0320,0321,0330,0331 | `gpu/build/device-mmu-q10jy-candidate/ui-sched/nord2-ios-metal-q10jy-ui654fb60dvfs-cap3500-ram3776-sched.img`, sha256 `501d794c1f4c2f52...`, `FAILURES: none`, kernel `bb361901`; audio pin `autotest/pins/audio-q10jy.pkg` (`1aea9991`); details `kern/p031x/STATUS.md` | opus-kern2 |
| vcodecd + probe + selftest built from the current tree | `bash gpu/hwcodec/build_ios.sh` and `bash gpu/hwcodec/ios/build.sh` both end `rc 0` / `IOS TOOLS OK` | opus-lead / opus-ios |
| Host encode session on the plain tree | `bash gpu/hwcodec/fakevenc/run_session.sh` -> all `ALREADY`, last line `... validated (H.264 SPS/geometry/decode/PSNR ...)` | done 17:15 by opus-lead |
| Payload staged and rehearsed | below, steps 2a-2b | DONE 19:2x by opus-lead2: `payload-vcodec`, tag vc2-encode, vcodecd sha256 `c623fd58f75c5273...` (ios/build.sh from the tree: IP rev 9 vcodecd.c e95aa2aa = separate SEQ_HDR job at OPEN + n2vc_dma registration + wait_done + HEVC level 4.0 default; V40 rev 6 mirror refresh; header rev 3; shim layout.mode=0), n2vcd_selftest `c0a6c7b5...`, n2vcd_probe `20fb51f5...`, **Slim.videoencoder `ae914887355ae647...`** (gpu/video plug-in + P04 behind nord2-video-claim-hw, ios/build_plugins.sh), switches claim-hw/hwdec forced OFF, launchd `--hw-timeout-ms 6000`, KeepAlive false, probe `--bitrate 300000 --hex 16384`, `REHEARSAL_OK` (gpu/video's plug-in and record restored byte-identical by the undo). Host: IP 854/854 incl. IP-REAL 33/33 (2 real-vhost frames through vcodecd, ffmpeg-decoded); run_session.sh H.264 115841 B and run_session_hevc.sh 43947 B validated |

The 17:23 payload (vcodecd e23f0691) was STALE (no V40 rev 6, joined headers gave SPS/PPS + zeros) and has been replaced by the
19:2x payload above. Restage only with the exact command of step 2a (it includes `--encoder-plugin` and the probe args).

Kernel: q10jy contains K02 rev 7 (273 MHz default AND the VDEC_ALL window; the VDEC part is idle until a decoder client runs).
If JPEG (/dev/redwood-jpeg) powered VEN first, it selects 624 MHz and a vcodec job then runs at 624. The payload is ABI-compatible
with q10jx/q10jw (no new ioctl): q10jx (df4ce447...) is the fallback when the mic/I2C8 patches must be excluded; q10jw runs the
encode at 624 MHz on an unmanaged vcore OPP (say so in the result).

## 2. Commands, in order

    W=$NORD2_WORK
    IMG=$W/gpu/build/device-mmu-q10jy-candidate/ui-sched/nord2-ios-metal-q10jy-ui654fb60dvfs-cap3500-ram3776-sched.img
    sha256sum $IMG | cut -c1-16     # must be 501d794c1f4c2f52 (q10jx df4ce447... is the fallback without mic/I2C8)
    cd $W/gpu/build/apps-20261104

    # 2a. stage (products from section 1; the script refuses a vcodecd without --gate or without the shim tables)
    bash stage-vcodec.sh vc2-encode --tools /tmp/n2hw-opus-lead/out --vcodecd /tmp/n2hw-opus-lead/out/vcodecd \
      --encoder-plugin /tmp/n2hw-opus-lead/plugins/Slim.videoencoder \
      --probe-args "--delay 45 --wait-server 120 --codec avc1 --size 640x480 --frames 10 --bitrate 300000 --hex 16384"
    #   expect: "keepalive": false, gates_on [selftest, run, probe], plugins {Slim.videoencoder: ae914887...},
    #   video_switches_off [claim-hw, hwdec]. (/tmp products are rebuilt by: bash gpu/hwcodec/ios/build.sh /tmp/n2hw-opus-lead/out;
    #   bash gpu/hwcodec/ios/build_plugins.sh; check the shas against section 1 before staging)

    # 2b. rehearse on the real launchd.plist (fake root, no phone)
    bash rehearse-vcodec.sh | tail -3
    #   expect: "plug-ins installed ... : ok", "gpu/video plug-in + record restored byte-identical: ok",
    #   REHEARSAL_OK launchd.plist byte-identical after install+undo

    # 3. ONE transaction: snapshot -> install payload -> flash IMG -> boot -> 600 s USB console capture
    cd $W/gpu/build
    PAYLOAD=payload-vcodec GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec.json \
      timeout 900 bash autocycle/txn.sh $IMG vc2-encode > autocycle/runs-vc2-encode.out 2>&1
    tail -3 cc-txn-vc2-encode/events          # expect: committed / kernel flashed <sha16> / capture done

    # 4. read the result from the live log alone (no second transaction needed)
    L=$W/gpu/build/autocycle/runs/vc2-encode
    grep -a 'N2VCD\|NORD2 VCODEC' $L/usb.log $L/ui-usb/* | head -200
    python3 apps-20261104/vcodec_check.py log <the console file in $L/ui-usb> --out $L
    ffmpeg -v error -xerror -i $L/first-au-from-log.bin -f null - && echo AU_DECODES   # -xerror: a truncated AU must not pass

`gpu/build/phone_chain.sh IMG TAG` runs vt17 and still4096 first, then this vcodec transaction, all on the same image. It is
correct for q10jx (0310/0311 do nothing at boot, so the first two boots behave like q10jw/q10iu) but costs two extra boot
cycles before the encode, and a hang in either earlier step makes `back_to_fastboot` fail silently and the vcodec step then
fails at `to_fastboot.sh`. **For the encode priority run step 3 alone.** (Its last-line comment says `runs/TAG-final`; the
directory is actually `runs/TAG-vcodec`.)

**Do not open Camera (or anything that records/takes photos) before `N2VCDP DONE`**: /dev/redwood-jpeg shares VEN power and,
if it powers VEN first, the encode runs at 624 MHz. After DONE, the q10jy-only checks (kern/p031x/STATUS.md "q10jy phone test
plan" steps 2-5: speaker before camera, `NORD2 CAM: FAN53870 ok: I2C8 pins 0x..11...`, speaker after camera with no
`NORD2 AUDIO ERROR ... i2c=5/16` / `i2c=16/0` / `uncertain=1`, the 15 s mic recording with `NORD2 MIC: opens>=1 ...`) can be done
in the same boot; a recorded video in that step also proves the software fallback (`N2VIDEO ...: H.264: n2avc avc1=0`, no `n2hwenc:` line).

Optional, same boot, AFTER `N2VCDP DONE` appeared (never while the probe runs; the autotest command 8 steps take the same
VEN references): `python3 autotest/phone.py status` 60 s after DONE; if the encode failed, the kernel read-only state:
`python3 $W/gpu/hwcodec/tools/hwv.py step 0 $W/gpu/hwcodec/runs/vc2-encode` (read only).

## 3. Expected lines, in boot order (times after boot are approximate)

| t | Line (prefix) | Meaning |
|---|---|---|
| boot | `NORD2 VCODEC: /dev/redwood-vcodec made, /dev/redwood-vcodec-client made, JPEG VEN sharing on ...`, `NORD2 JPEG: /dev/redwood-jpeg made ...` (no `NORD2 VCODEC:` register line at boot) | 0310 loaded; nothing powered |
| boot | after `devfs redwood-audio`: `NORD2 MIC: devfs redwood-mic rate=48000 channels=1 bits=16 prime=2048 max=6144` | q10jy only (0320/0321); its absence = wrong image |
| boot | UI, touch, Wi-Fi as q10jx; if a sound plays: `NORD2 AUDIO AMP START seq=N`, never `NORD2 AUDIO ERROR` | 0330 regression check, not part of the encode verdict |
| ~10 s | `N2VIDEO <proc>[pid]: registered: hvc1=.. hev1=.. avc1=.. jpeg=..` then `N2VIDEO <proc>[pid]: hardware encoders: off (no claim-hw)` (only when the plug-in is in the payload, first process that loads VideoToolbox) | the P04 encoders are present but NOT registered: recording stays on n2avc |
| ~10 s | `N2VCDT ... gate ... removed`, then `N2VCDT T<n> ... PASS`, `N2VCDT DONE pass=N fail=0` | selftest: 4 GiB reservation, TLS, fault recovery, vendor set on the fake hardware (no device) |
| ~10 s | `N2VCD 0.0xx start dev=/dev/redwood-vcodec vendor=/usr/local/lib/n2vendor ...` | daemon up |
| | `N2VCD ... host set: vcu=... vendor=...`, `N2VCD ... server transport: relay`, `N2VCD ... serving /dev/redwood-vcodec-client` | vpud + libs loaded, M-a/M-b, relay registered |
| ~55 s | `N2VCDP ... start pid=.. codec=avc1 640x480 frames=10 ...`, `N2VCDP ... relay ... server_present=1 ...`, `N2VCDP ... HELLO_OK ...` | probe connected |
| | `N2VCD ... milestone M-c ...`, `M-d` (ENC_INIT -> INIT_DONE), `N2VCD ... session 1 open client=...`, `N2VCDP ... OPEN_OK ...` | vendor encoder initialised on the real device |
| first job | `NORD2 VCODEC: venc_sel ... (273 MHz)` (only if K02 adds the requested log line; otherwise no clock line: confirm later with `hwv.py step 0`, CLK_CFG_13 bits 27:24 = 4), VEN power-on lines (VEN is powered on and off around EVERY frame's GCE list: 10 frames = 10 pairs), `N2VCD ... milestone M-e first VENC window access`, `M-f wait_irq(RWVC_IRQ_VENC ...)` | power + first register write + wait for done |
| | `NORD2 VCODEC:` first-done line with +0x5c status (expect bit 2 FRM = 0x4; 0x1/0x2 SPS/PPS; 0x8 = bitstream buffer too small) and GICD pending of INTID 340 | answers VENC_PROTOCOL.md section 7 item 1 |
| per frame | `N2VCD ... session 1 frame k key=..`, `N2VCDP ... frame k bytes=N key=1 hdr=.. hw_us=.. nal=7,8,5` for frame 0, then `nal=1` or `5` | |
| | `N2VCDP ... hex 0000 00000001 67...` lines (up to 16384 bytes = 512 lines) | first AU in the log (SPS 67, PPS 68, IDR 65); complete only if `frame 0 bytes=` <= 16384, otherwise the whole stream comes with the next transaction (`vcodec_check.py fetch`) |
| end | `N2VCDP ... CLOSE_OK`, `N2VCDP ... DONE ok=1 frames=10/10 keys=.. bytes=.. hw_ms=..`, kernel VEN power-off line | PASS |

## 4. Failure modes: what each one means and what to capture

Always keep: `cc-txn-vc2-encode/` (events, logs), `autocycle/runs/vc2-encode/` (usb.log + ui-usb). The bit stream and the
daemon's own log file (`/private/var/db/nord2-vcodec/{vcodecd.log,out/probe-*.h264}`) come with the NEXT transaction's snapshot:
`python3 apps-20261104/vcodec_check.py fetch vc2-encode` after any later transaction.

| Symptom | Meaning | Next |
|---|---|---|
| no `N2VCD`/`N2VCDT` lines at all | launchd did not start the jobs (plist / receipt / gate) or console write blocked | check `catalog.log`, `offline.log`; guest-vcodec output in `offline/` |
| `N2VCDT ... FAIL` | an iOS process facility (reservation, TLS, SIGSEGV recovery) differs from the host | daemon may still work; send the T line to opus-ios |
| `N2VCD ... cannot open /dev/redwood-vcodec` | wrong image (not q10jx/q10jw) or the node permissions | check `kernel flashed <sha>` |
| `N2VCD ... n2vhost_start failed` / `guest set stopped before ...` / `fatal guest stop ... reason=...` | interpreter stopped in vendor code (UNDEF, unmapped access): reason names the address | host-reproducible: give reason + address to hw-iv/opus-perf; no phone needed for the fix |
| probe `vcodecd not serving ... within 120s` | daemon died or is still loading | look for the daemon's last N2VCD line |
| `OPEN failed` / `encoder open failed: -110` | ENC_INIT / SET_PARAM IPI not answered (guest thread blocked) | daemon log; host replay with the same parameters |
| `NORD2 VCODEC: ... not powered: the display domain is off` / `MDP ...` / `power status timeout` | power gate refused (screen off during the test?) | keep the display on; rerun |
| `NORD2 VCODEC: venc_sel ... did not take parent` | clock mux refused 273 MHz | kernel; opus-kern |
| `N2VCD ... watchdog: request stuck for N ms ... exiting` | one request exceeded 70 s (10 x 6000 ms + 10 s): something under it did not honour its bound; the exit makes the kernel power VEN off | the last N2VCD/NORD2 VCODEC lines before it name the stuck step |
| `session 1 hardware timeout: encode waited N ms` with +0x5c = 0 | VENC never finished: microcode/RCPU handshake, wrong buffer PA, larb/IOMMU routing | the most informative failure: capture the RWVC op trace lines around M-e..M-f; opus-venc + opus-kern |
| +0x5c = 0x8 (DRAM) | bitstream buffer too small/wrong size register | opus-venc |
| `frame 0 bytes=N` but `first AU from the log` does not decode / NAL types odd | L2: hardware ran; slice/headers differ (SPS from software vs hardware slice) | AU bytes + stream to opus-venc |
| phone stops answering during the encode | bus hang (larb/bus protection) or kernel wait | note the last kernel line; recovery = boot back to q10iu; the probe gate is one-shot, so the next boot will not repeat the encode |

The probe removes its gate before the first request and vcodecd runs with KeepAlive false: a crash or panic cannot loop at the
next boots. After the test, undo with `PAYLOAD=payload-vcodec GUEST=guest-vcodec-undo.sh OWNED=.../owned-vcodec-undo.json` in
the next convenient transaction (not required for the next test).
