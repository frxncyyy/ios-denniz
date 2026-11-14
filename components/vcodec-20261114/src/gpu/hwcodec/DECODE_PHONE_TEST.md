# First on-phone hardware H.264 decode: the procedure

Owner: opus-decint. 11 November 2026, ~23:55. For the main session (the only one that touches the phone). Image: **q10je**
(already carries the VDEC kernel side: K02 rev 7, RWVC_WIN_VDEC_ALL, larb4/larb5 SLP_CON bit-0 allow-list). This test needs
**only a rootfs transaction**: payload `gpu/build/apps-20261104/payload-vcodec-dec`, tag **vc3-decode**. Nothing in it has run
on the phone; the VDEC power/clock/IRQ code of the kernel runs for the first time in this boot.

## 0. What a run proves (wording for NOTES/STATUS)

- **D3 (goal):** "The MT6893 VDEC, driven by the phone's own vendor decoder libraries hosted in vcodecd, decoded H.264 on iOS
  16.7: N/N pictures bit-exact with ffmpeg" -- `N2VCDD DONE ok=1` for a stream AND `compare_dec.py` prints `BIT-EXACT N/N`.
- **D2:** "The VDEC ran vendor-programmed decode jobs to completion (done interrupts, every picture displayed) but the pixels
  differ / are zero" -- DONE ok=1, compare fails or `y_nonzero=0`. Hardware runs; the bug is in buffers/layout/cache/detile.
- **D1:** "The vendor decoder initialised and programmed the VDEC on the real device (LOCK/power/register traffic) but no job
  completed" -- `dec: ev wait_isr` with a non-zero second value / `dec: ev timeout`.

Not proved by any outcome: HEVC (second payload), VideoToolbox/Slim.videodecoder, frame rate, > 1080p.

## 0b. Run vc19b (12 Nov, q10je, payload vc3-decode b83b9109): what happened, what changed

The VDEC DECODED: per frame `dec: ev wait_isr 0x1 0x0` (LAT done) and `wait_isr 0x0 0x0` (CORE done, ~8-12 ms), first
picture already in list_disp (`put_fb 0x1 ...`), on both streams. What failed: the VDEC_CORE power-OFF after the frame --
kernel `NORD2 VCODEC: VDEC_CORE bus protection 0x40 not acknowledged (0x103)` / `power-off failed (60)`, vcodecd
`hw power block=3 on=0 -> -60`; n2ap_vdec turned that into a hardware error and the START failed (`input 0 failed:
hardware detail=57` = errno ETIMEDOUT 60 path; the probe's detail is the elapsed ms of the failed request).
Root cause (kernel): our n2vc_hw_off() gated VDEC_CKEN/LAT_CKEN/LARB1_CKEN before setting the bus protection. Linux never
clears those three: in clk-mt6885-vde.c they are GATE_INV_DUMMY* = mtk_clk_gate_ops_setclr_inv_dummy, whose disable is a
no-op (clk-gate.c:310-314), so spm_mtcmos_ctrl_vde2() handshakes with the larb4/core clocks running.
Fixes: (1) daemon, rootfs only (payload restaged, vcodecd `82b97233`): a failed power-off is no longer a decode error, and
the adapter holds one VDEC_SOC + VDEC_CORE reference for the whole session (no per-frame power cycle; on q10je one failed
off per session remains, at CLOSE, harmless); PC replay `N2VC_FAKE_VDEC_CORE_OFF_FAIL=1 ENC=1 run_sim.sh` PASS 2x10.
(2) kernel q10jf (0310 `4cbc3bde`): no clock gating before the VDE/VDE2 protection; `kern.n2vctrace` default 0 (relay fix
kept). K02 588919 x4, IK 5909, make_series OK. Recipe `gpu/build/recipes-q10jf/build_q10jf.sh`.

## 0c. Run vc22 (12 Nov, q10jf, vcodecd 82b97233): H.264 640x480 BIT-EXACT 10/10; next payload vc5-decall

Stream 0 bit-exact (cmp with ffmpeg); stream 1 (1080p) killed n2vcd_dprobe: jetsam per-process limit (6 MB effective; Y+C+NV12
staging = 7.5 MB at 1080p). Fix: n2vcd_dprobe and n2vcd_probe raise their own limit at start (include/n2memlimit.h, 3072
MB, line `jetsam raised ...`), as vcodecd does; the launchd JetsamMemoryLimit key is still set but does not apply.
vc22 timing at 640x480: ~12 ms per START, of which LAT done ~6 ms and CORE done ~7 ms later; the hardware itself should
take far less, so the time is register traffic (~1030 single-op ioctls per frame) + 500 us wait polls.
**vc5-decall** (`payload-vcodec-decall`, vcodecd `0d725903`, dprobe `24f4d988`, probe `f5890716`; `vcodecd --dec-wc alt`):
8 sessions = each stream twice, write-combining OFF on the 1st and ON on the 2nd: 640x480 H.264, 1080p H.264, 480p HEVC,
1080p HEVC. vcodecd logs per START `dec: START n done ... (lock a lat b core c unlock d) regs r=.. w=.. ioctls=..` and
`dec: session N write-combining ON|off`; PC: combining halves the ioctls (1031 -> 498 per AU), pictures identical.
Compare with `compare_dec.py ... --stream N` (N = 0..7). vc3-decode / vc4-hevcdec restaged with the same binaries
(combining off).

## 0d. vc6-dec4k (for q10jh, 384 MiB pool): 4K / 1080p60 decode timing, H.264 + HEVC

`payload-vcodec-dec4k`: vcodecd `f3deb7c2` (`--decoder --guest-log 300 --dec-wc alt`), n2vcd_dprobe `7fd5e7cb` (up to 12
streams, jetsam raise), n2vcd_probe `8c801de2` (encode 640x480 x10 first). 9 decode sessions, write-combining off/on pairs:
1-2 `t2160-h264-high-bf2-20f` (3840x2160 x264 High, 2 B, 20 f), 3-4 `t1080p60-h264-high-60f` (60 f), 5-6
`t1080p60-hevc-main-60f` (60 f), 7-8 `t2160-hevc-main-bf2-20f` (x265 Main, 2 B, 20 f), 9 `t4096x2176-h264-high-10f` (DCI 4K,
level 5.1, off). `--no-yuv` (4K NV12 would be 250 MB per stream on the phone): bit-exactness from the per-picture hashes:
`python3 decint/compare_dec.py decint/streams/<stream> dprobe.log --stream N` (N = 0..8; above 720p the reference hashes are
computed by `decint/fnv_frames.c` while ffmpeg streams, ~1 s per 4K stream, no raw files). Owned list
`owned-vcodec-dec.json` (now names the new streams). Commands = section 2 with `payload-vcodec-dec4k` / tag `vc6-dec4k` and
the q10jh image.
Memory (PC measurement): 4K H.264 vendor DMA 65 MiB + 12 x 12.5 MB output pool + 2 MiB input = ~217 MB per session (q10jh
pool 384 MiB; does NOT fit q10jf's 86 MiB). Output buffers and the input buffer are RWVC_DMA_ALLOC (relay, kernel pool),
not process memory; the probe's own memory is Y+C+NV12 staging = 25 MB at 4K (jetsam raised to 3072 MB).
Changes for this payload: decoder max height 2160 -> 2304 (DCI 4K), the adapter zeroes 4 KiB after each AU in the input
buffer (the vendor's HEVC NAL search reads past the AU end), vcodecd per-START perf line (section 0c).
PC: 4K H.264 20/20 + 4096x2176 10/10 (fake irq), 4K HEVC 20/20 and 1080p60 HEVC 60/60 (HEVC model, real irq path),
1080p60 H.264 60/60. Known PC-only artefact: an HEVC session right after a 4K HEVC session times out on the HEVC model
(its register file keeps WRAP_TOP state across sessions; the real domain is powered off and reset at session close).

## 1. What is in the payload (all host-built from the tree, 11 Nov 23:4x)

| File | sha256 (16) | What |
|---|---|---|
| vcodecd | `82b97233d254d686` | `gpu/hwcodec/ios/build.sh` with `N2VCD_WITH_DECODER`: DEC05 service + DEC01 n2ap_vdec; decoder only with `--decoder` |
| n2vcd_dprobe | `3c2998ff6690048b` | NEW decode probe (`ios/n2vcd_dprobe.c`): DEC05 client over the relay, MM21->NV12 (`mm21/n2_mm21.c`, NEON) |
| n2vcd_probe | `fc604f393ece507a` | the encode probe from the tree (runs first, 640x480 x10, as vc14) |
| n2vcd_selftest | `9b52514877ff5188` | tree build |
| streams | `a092319a`, `473f46b0` | `vc14-probe43-640x480-baseline.h264` (the phone's own vc14 hardware-encoded stream, 10 frames), `t1080-high-bf2-10f.h264` (x264 High 1920x1080, 2 B-frames, 10 frames) -> `/usr/local/lib/n2vendor/streams/` |
| vendor | pins as vc2 | the five phone objects, hard-linked |

launchd (guest-vcodec.sh): `vcodecd --gate .. --vendor .. --hw-timeout-ms 6000 --decoder --guest-log 300` (KeepAlive false);
`com.nord2.vcodec-probe` as vc14; NEW `com.nord2.vcodec-dprobe`: `n2vcd_dprobe --gate /private/var/db/nord2-vcodec-dprobe
--out .../out --log .../dprobe.log --in <640x480> --in <1080p> --delay 50 --after .../probe.log --after-timeout 240
--wait-server 180 --mm21-first` (stdout -> `dprobe.stdout.log`). Gates on: selftest, run, probe, dprobe (all one-shot except run).

Fixes in this payload that a phone run would otherwise have hit (all found on the PC, see section 6):
1. **larb SLP_CON release** -- the vendor writes back `0x00010000` (bit 16 = read-only ack); q10je's allow-list accepts bit 0
   only -> EACCES -> larb left in sleep protect -> every VDEC DMA stalls. vcu now sends every store to larb5 0x1600d00c /
   larb4 0x1602e00c as a bit-0 WRITE_MASK (same hardware effect, accepted by the kernel).
2. **client bitstream never reached the vendor** -- the guest mirror of the client's input buffer was created zeroed and then
   synced TO the device before each START (DEC01's model) -> vendor parsed zeros (`There is no any H264 instance`,
   `Vdec_Drv_H264_open -10`) and later AUs were overwritten. The adapter now refreshes device -> guest before every START.
3. **racing words**: restore moved to the CORE lock (the kernel's VDEC_RACING window is a CORE window: the LAT-first restore
   was EACCES every AU), save moved to the CORE power-off (HEVC/timeout order saved with the core off -> EACCES).
4. **no read-before-write** on aligned VDEC stores (VENC path unchanged): decoder registers include read-sensitive words.
5. `vcodecd` QUERY_CAP before INIT, output fourcc pinned to MM21, VDEC power references dropped on the fatal path.
6. The adapter gives its published ion shares back at session close (new additive `n2shim_ion_unregister_share`): not
   needed by these 2-3 session runs (a 5-session sim passes without it) but keeps V34's 64-slot table from growing.

## 2. Commands

    W=$NORD2_WORK
    IMG=$W/gpu/build/device-mmu-q10je-candidate/ui-sched/nord2-ios-metal-q10je-ui654fb60dvfs-cap3500-ram3776-sched.img
    sha256sum $IMG | cut -c1-16      # must be e9f79c0cde88e309 (q10je)
    cd $W/gpu/build/apps-20261104

    # 2a. (already done, 23:5x) rebuild + stage; repeat only after a source change
    bash $W/gpu/hwcodec/ios/build.sh /tmp/n2hw-decint/ios          # IOS TOOLS OK; vcodecd must contain --decoder
    bash stage-vcodec.sh vc3-decode --tools /tmp/n2hw-decint/ios --vcodecd /tmp/n2hw-decint/ios/vcodecd \
      --payload $PWD/payload-vcodec-dec \
      --decoder $W/gpu/hwcodec/decint/streams/vc14-probe43-640x480-baseline.h264 \
      --decoder $W/gpu/hwcodec/decint/streams/t1080-high-bf2-10f.h264
    #   expect: gates_on [selftest, run, probe, dprobe], "decoder": true, stream_order [640x480, 1080p]

    # 2b. rehearse on the real launchd.plist (fake root)
    bash rehearse-vcodec.sh $PWD/payload-vcodec-dec | tail -3       # REHEARSAL_OK (done 23:5x: OK)

    # 3. ONE transaction (the vc14 recipe with this payload and owned list)
    cd $W/gpu/build
    PAYLOAD=payload-vcodec-dec GUEST=guest-vcodec.sh OWNED=$W/gpu/build/apps-20261104/owned-vcodec-dec.json \
      timeout 900 bash autocycle/txn.sh $IMG vc3-decode > autocycle/runs-vc3-decode.out 2>&1
    tail -3 cc-txn-vc3-decode/events          # expect: committed / kernel flashed e9f79c0c... / capture done

    # 4. results: the daemon/probe logs and the decoded frames come with the NEXT transaction's snapshot
    python3 apps-20261104/vcodec_check.py fetch vc3-decode     # -> apps-20261104/vcodec-fetch/vc3-decode/
    F=apps-20261104/vcodec-fetch/vc3-decode
    python3 $W/gpu/hwcodec/decint/compare_dec.py $W/gpu/hwcodec/decint/streams/vc14-probe43-640x480-baseline.h264 \
      $F/dprobe.log $F/out/dec-*-0-640x480.nv12.yuv --stream 0   # expect BIT-EXACT 10/10
    python3 $W/gpu/hwcodec/decint/compare_dec.py $W/gpu/hwcodec/decint/streams/t1080-high-bf2-10f.h264 \
      $F/dprobe.log $F/out/dec-*-1-1920x1080.nv12.yuv --stream 1 # expect BIT-EXACT 10/10

compare_dec.py works from the log alone too (omit the .yuv): the probe logs an FNV-1a-64 hash of every cropped NV12 picture;
`--stream N` selects the dprobe session (`stream N:` section of the log).

Keep the display ON during the test (VDEC_CORE's larb4 hangs off the MDP SMI common: the kernel refuses its power with
`... not powered: the MDP domain ...` otherwise). Do not open Camera until `N2VCDD ALL` (JPEG shares VEN; VDEC is separate,
but keep the boot quiet). The dprobe starts ~50 s after boot and waits for the encode probe's NEW `DONE` line.

## 3. Expected lines (vcodecd.log, probe.log, dprobe.log under /private/var/db/nord2-vcodec/)

vcodecd.log (prefix `N2VCD <s.ms>`), from the PC rehearsal (`decint/run_sim.sh`, fake hardware) in order:

| Line | Meaning |
|---|---|
| `jetsam raised ...`, `host set: vcu=yes vendor_hw=yes unresolved=0 milestone=M-b` | as vc14 |
| `dec: decoder service up (ipi timeout 6000 ms, write-combining off)` | --decoder honoured (phone: no "FAKE VDEC IRQ" text) |
| `serving /dev/redwood-vcodec-client (encoder + decoder)` | |
| `guest VPUD/4 ...` (up to 300 distinct vendor lines) | the vendor's own log: names its failures |
| encode session lines as vc14 (`session 1 open ...`, frames, close) | encode probe first |
| `dec: session 1 open client=N codec=0x31637661 fmt=MM21` | decoder OPEN: INIT + SET_PARAM done (QUERY_CAP first) |
| `dec: ev decode 0x<AU bytes> 0x200000 0x0`, `dec: START 0 done changed=0x23 ...` | header START: software parse, no register access |
| `dec: session 1 header 640x480 stride=640 buf_h=480 dpb=8 mm21` | geometry; 1080p: `1920x1080 stride=1920 buf_h=1088 dpb=11` |
| `dec: ev power 0x2 0x1 0x0`, `dec: ev lock 0x0 0x1 0x0`, `dec: ev power 0x3 0x1 0x0`, `dec: ev lock 0x1 0x2 0x0` | LOCK_LAT (VDEC_SOC on), LOCK_CORE (VDEC_CORE on) |
| `dec: ev wait_isr 0x1 0x0 0x0`, `dec: ev wait_isr 0x0 0x0 0x0` | LAT done then CORE done (second value != 0 = timeout/error) |
| `dec: ev racing_save 0x44 ...`, `dec: ev power 0x3 0x0`, `dec: ev power 0x2 0x0` | unlock: save at CORE off, power off |
| `dec: START k done changed=0x0 in N ms` | one per access unit (11 per stream incl. the header START) |
| `dec: session 1 close ...` | |
| must NOT appear: `hw regs ... -> -13` (EACCES), `hw power ... -> -` , `dec: ev timeout`, `fatal guest stop`, `watchdog:` | |

Kernel console (live USB log): `NORD2 VCODEC: ...vdec_sel...` / VDE + VDE2 power lines at the first LOCK, then
`NORD2 VCODEC: first VDEC_LAT done status ...` and `first VDEC done status ...` (K02's first-done lines, GIC INTID 424/425).

dprobe.log (prefix `N2VCDD`, fsync per line):

    start pid=.. streams=2 transport=/dev/redwood-vcodec-client delay=50s mm21=neon after=.../probe.log from byte N
    after: .../probe.log has DONE, starting
    stream 0: /usr/local/lib/n2vendor/streams/vc14-probe43-640x480-baseline.h264 codec=avc1
    input ...: 19367 bytes, 10 access units
    sysctl kern.n2vctrace=0 -> 0 errno 0
    relay /dev/redwood-vcodec-client abi=1 server_present=1 ...
    HELLO_OK version=1 dec=0x3 ...
    OPEN_OK session=.. in_buf=2097152
    HEADER 640x480 stride=640 buf_h=480 y=307200 c=153600 out=460800 dpb=8 fourcc=MM21
    pool 8 x 460800 bytes registered
    input k bytes=.. rtt=..ms hw_us=.. error_map=0 ready=1           (x10)
    raw MM21 of the first picture: .../out/dec-<pid>-0-first.mm21
    picture k index=.. pts=.. poc=.. 640x480 read=..ms detile+read=..ms y_mean=.. y_nonzero=../307200 fnv_y=.. fnv_uv=..   (x10)
    FLUSH_OK ready=0 / CLOSE OK
    DONE ok=1 codec=avc1 pictures=10/10 ... file=.../out/dec-<pid>-0-640x480.nv12.yuv
    stream 1: ... t1080-high-bf2-10f.h264 ... HEADER 1920x1080 stride=1920 buf_h=1088 ... dpb=11 ... DONE ok=1 ... pictures=10/10
    ALL ok=1 streams=2/2

PC rehearsal values (fake hardware, NOT phone numbers): ~2.5 ms per START, 1170 register ioctls per AU (560 R / 607 W, no
write-combining), vendor DMA 26.6 MiB (640x480) / 35.1 MiB (1080p) + client pool 3.6 / 34.4 MiB + 2 MiB input: 1080p uses
~72 of the 86 MiB pool. The pictures are all zero on the PC (no real VDEC), so `y_nonzero=0` there; on the phone y_mean of the
test streams is ~100-130 and y_nonzero ~ the full count.

## 4. Failure modes

| Symptom | Meaning | Next |
|---|---|---|
| no `dprobe.log` / `gate ... absent` | job not installed or gate missing | catalog/offline logs of the transaction |
| dprobe `daemon offers no avc1 decoder` | vcodecd started without --decoder (old plist) | check vcodecd.log `decoder service up` |
| `OPEN failed ... BUSY` after 60 s | the encode session never closed | vcodecd.log of the encode |
| `DEC_HEADER failed` + vcodecd `guest ... There is no any H264 instance` | the vendor saw no/zero bitstream | DMA_INFO / bs_refresh: vcodecd `dec: RWVC_DMA_INFO handle .. -> -..` |
| vcodecd `hw power block=3 on=1 -> -13` + kernel `not powered: the MDP domain` / `display domain is off` | screen off / MDP down | keep the display on, rerun |
| `hw power ... -> -5` / kernel `power status timeout` / `still isolated` | VDE/VDE2 MTCMOS or bus protection | kernel (opus-kern/K02): the first VDEC power-on ever |
| `hw regs ... -> -13` | a register op the kernel refuses (window/larb/power) | first_op/off in the line name it; the PC sim shows none |
| `dec: ev wait_isr 0x1 0x2 ...` / `dec: ev timeout 0x.. 0xa001 ...`, vendor `VLD Fetch Fail` / `Wait PicNum rst fail` | job never completed: IRQ (SPI 424/425) not routed/acked, clock, larb sleep handshake, or bitstream PA | kernel console `first VDEC* done` present? -> IRQ path; absent -> hardware never finished |
| `input k failed: hardware detail=6000` | n2ap_vdec IPI timeout (6 s): the vendor did not ack START | vendor lines before it |
| DONE ok=1 but `y_nonzero=0` | hardware did not write the frame buffers (wrong fb PA / IOMMU route) | raw `.mm21` file; kernel larb4 port routing |
| DONE ok=1, compare DIFF but PSNR high/structured | detile/stride/plane offset or cache | `.mm21` + `n2_mm21` against ffmpeg (opus-mm21) |
| `fatal guest stop` / `watchdog: request stuck` | interpreter stop / bound exceeded | reason + address: host-reproducible |
| phone stops answering | bus hang (larb/bus protection) | last kernel line; the dprobe gate is one-shot, next boot does not repeat |

## 5. Second payload: HEVC (vc4-hevcdec), staged + rehearsed

`gpu/build/apps-20261104/payload-vcodec-hevc`, same binaries as vc3-decode (vcodecd `82b97233`, dprobe `3c2998ff`), streams in
this order: `vc14-probe43-640x480-baseline.h264` (H.264 control, proves the session still works), `t480-hevc-main-10f.h265`
(x265 Main 640x480, 10 frames, sha `08a805bd`), `t1080-hevc-main-10f.h265` (x265 Main 1920x1080, 10 frames, `7b224956`).
Same owned list (it already names the HEVC stream paths). Commands = section 2 with `payload-vcodec-hevc` / tag
`vc4-hevcdec`; compare with `--stream 1` (480p HEVC) and `--stream 2` (1080p HEVC). HEVC differences that matter on the
phone (facts/HEVC_DEC_PROTOCOL.md, opus-hevcdec): no GCE for HEVC video, same LOCK/WAITISR/IRQs, the LAT parses through LAT
HEVC_VLD page 0x15000 (inside VDEC_ALL, SOC domain), the LAT reads the bitstream IN PLACE from the client buffer (bs_dma =
the client's pa: fine on the phone, the client wrote it through RWVC_DMA_RW), 1080p DMA peak ~68 MB. Vendor log lines to
look for on failure: `H265_HAL_GetStartCode_PicStart fail`, `VLD Fetch Fail VdecId 3`.
PC: `HEVC=1 bash gpu/hwcodec/decint/run_sim.sh decint/streams/t480-hevc-main-10f.h265 decint/streams/t1080-hevc-main-10f.h265`
-> PASS 10/10 + 10/10 with opus-hevcdec's VDEC model raising the REAL VDEC/VDEC_LAT interrupts (no fake WAITISR), every
model op first checked by T02's kernel-parity fake (0 EACCES). Can run in the same boot as H.264 if the main session
prefers (stage with all four --decoder streams; H.264 first, HEVC last so an HEVC hang costs nothing already logged).

## 6. Undo / next

The undo (`guest-vcodec-undo.sh`) knows the dprobe job, gate, streams and binary. A successful D3 makes the VideoToolbox
decoder plug-in (DEC06 Slim.videodecoder, `--decoder-plugin` + `--video-on hwdec`) the next payload.

## 7. PC evidence (opus-decint, 11 Nov)

- `bash gpu/hwcodec/decint/run_sim.sh` (and `ENC=1 ...`: encode probe first, decode probe waiting for its new DONE, exactly the
  boot order): `N2TEST DECINT-SIM PASS` -- real phone vendor set under the interpreter, T02 fake with the kernel's rules
  (flags, windows, per-page power, larb allow-list) + a modelled larb ack, `N2A32_MEM_GRANULE=16384`, 2 streams x 10 pictures
  displayed, 0 EACCES, 0 failing register calls; only the VDEC interrupt is faked (`--fake-vdec-irq`).
- HEVC: see section 5 (real IRQ path through the HEVC model). The sim also found: the HEVC LAT reads the client bitstream in
  place (the model needed V41's pa registry to see it), and after an IPI timeout the vendor keeps polling LAT HEVC_VLD while
  the AP has powered VDEC off (refused by the kernel, harmless, but floods `hw regs ... -13` in the log).
- Regressions after the changes: T02 6194/6194, V40 1539/1539, DEC04 2073/2073 (tests adapted to no-RMW + masked larb
  stores), DEC01 999/999 (racing expectations adapted), DEC05 1150/1150. Pre-existing, unrelated: IP `daemon/test` fails
  13-16 checks with the ORIGINAL vcodecd.c too (720p60 level + log checks), DEC06 t_obj 3 checks fail with the original header.
