# p031x — VENC kernel audit, patches 0310/0311, phone test plan (opus-kern, opus-kern2)

State: **host-tested and compiled only; nothing here has run on the phone.** Date: 11 November 2026.

## Image q10jk (12 Nov): q10jj + 0317 (JPEG encoder EOI trim: the block rounds to 128, not 64)

Bug in 0310 [V-phone jp5-jpeg]: n2jd_cut() searched FF D9 only in the last 64 bytes ("the block rounds its byte count up to
64"). The block rounds to 128 (every jp5 size a multiple of 128), so padding is 0..127 bytes and 6/13 encodes with > 62 bytes
of padding failed `NORD2 JPEG: no FF D9 in the last 64 bytes of N` (RWJ_E_SIZE/EIO; 42240, 258816, 1441920, 428672, 981248,
958592). `0317-nord2-jpeg-encode-eoi-trim-128.patch` (after 0316, 0310/0315/0316 untouched): the FIRST FF D9 from
max(2, size - 256) (entropy data has no FF D9; forward search skips a stale EOI left in the padding by a longer job); log
text `...in the last 256 B of %u` (2 bytes shorter). Host model (t_jpegdev.c): pads to 128 (+128 on an exact multiple),
no_eoi wipes the last 256 bytes, new cases n=42116 -> 42240 (124 bytes of padding) and a stale FF D9 in the padding
(n=42116 and n=42200). The old n2jd_cut fails the new model broadly. Results: K01 PASS 829/829, JPEGDEC PASS 886/886
(ASan/UBSan, -O2, TSan), P031X PASS 589041/589041, IK PASS 5909/5909, make_series --check OK.
Image (`gpu/build/build_q10jk.sh`): `gpu/build/device-mmu-q10jk-candidate/ui-sched/nord2-ios-metal-q10jk-ui654fb60dvfs-cap3500-ram3776-sched.img`,
sha256 `36aca6cf8bddd2423f8c387c571f32f648f57addbb262b18eed46691044209e7`, kernel `e55b966e...`, boot spare 2048 B,
FAILURES none. Not flashed.

## Image q10jj (12 Nov): q10ji + 0316 (/dev/redwood-jpeg ioctl `data` is a kernel buffer)

Bug in 0310/0315 [V source]: XNU ioctl() (bsd/kern/sys_generic.c:875-1015) copies IOC_IN structs into a kernel buffer,
passes that to d_ioctl and does the IOC_OUT copyout itself, only when d_ioctl returns 0. The driver copied in and out of
`data` as if it were a user address, so every RWJ_INFO / RWJ_ENCODE / RWJD_JOB returned EFAULT on q10ji and earlier.
`0316-nord2-jpeg-ioctl-kernel-buffer.patch` (after 0315) uses memcpy on `data` in n2jd_info, the RWJ_ENCODE path and
n2jd_dec_ioctl. The pointers inside the structs (src_y/src_c/dst, src/regs/dst) stay copyin/copyout. On an error, user space
gets errno only (out_* are not copied out). The ABI stays as it was ("errno is EIO then"), and rwjpeg.c / n2jpeg_phone.c
decide on errno. The status of a failed job is in the kernel log: the decoder line now also prints state (0x278).
Host harness: t_jpegdev.c now calls the driver through h_sys_ioctl (a model of XNU ioctl()), flags any copyin/copyout on
the kernel buffer, and checks failed-job words in the kernel copy (K_ENC/K_DEC). test_k01/run_tests.sh now runs on
kernel/b + 0310..0316 (the K01 source bsd/dev/nord2_jpegdev.inc is the pinned 0310 input and still has the bug; with
N2K01_LEGACY=1 it reports 86 kernel-buffer violations). Results: K01 PASS 825/825, JPEGDEC PASS 882/882 (ASan/UBSan,
-O2, TSan), P031X PASS 589041/589041, make_series --check OK. Image (build_q10jj.sh): sha256 3b00238e360aee96…de20dedf,
boot spare 2048 B, FAILURES none. Not flashed.

## Image q10ji (opus-pool, 12 Nov): q10jh + 0314 (vcodec adaptive wait) + 0315 (JPEG hardware-decoder path)

`gpu/build/device-mmu-q10ji-candidate/ui-sched/nord2-ios-metal-q10ji-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33552384 bytes,
SHA-256 `a1098364ad520e02f17b75a9462a07295b80a59b7b90e3f600c199399d734576`, kernel `3063b5af...`, FAILURES none,
**boot spare 2048** (q10jh 4096: the next patch must cut log strings first); recipe `gpu/build/build_q10ji.sh`.

**0314 vcodec wait** (`0314-nord2-vcodec-adaptive-wait.patch`): RWVC_WAIT_IRQ and RWVC_OP_POLL re-read the status every 20 us
(delay(), n2vc_mtx dropped around it) for the first `kern.n2vc.spin_us` (default 2000) of a wait, then msleep in
`kern.n2vc.sleep_us` steps (default 200, clamped 50..1000); was a flat 500 us msleep (+ timer-coalescing leeway) from the first
poll. Worst-case return latency after the hardware finishes: 20 us inside 2 ms, ~200 us + wake latency after it (was >= 500 us).
Cost: up to 2 ms of one CPU spinning per wait; `sysctl kern.n2vc.spin_us=0` restores sleep-only for an A/B. Also a per-window
VA cache (no n2vc_pa() list walk per register op / status poll). Host: t_p031x test_adaptive_wait (latency bounds for 300 us /
1.99 ms / 8 ms waits, spin 0, both clamps, POLL op, revalidation) 17 new checks.
Not changed (measured, not worth it): RWVC_REG_MAX_OPS is 2048 and vcodecd batches up to 256 (VCU_WC_MAX_OPS) - the cap is the
daemon's flush policy, not the kernel; copyin/out is 1 KiB per 64 ops; per-op validation is a table lookup. The real per-AU
cost is the ioctl count (~500 per AU with --dec-wc alt, ~5-10 us each): that is a vcodecd/V40 batching job.

**Real interrupts: assessed, NOT implemented.** sleh_irq (osfmk/arm64/sleh.c:1860-1895) forwards every non-timer/non-IPI INTID to
the ONE cdp->interrupt_handler of the CPU that took it (ml_install_interrupt_handler overwrites it), and calls
nord2_irq_seen(intid) (osfmk/arm64/sweet_early.c) first for every IRQ. Options: (a) a hook in nord2_irq_seen: needs osfmk
recompiled (not in this build flow's object list) and a cross-module dispatcher table; (b) install a vcodec handler on a dedicated
CPU with the SPI routed there: breaks silently if Nord2GPU's optional irqSetup (boot-arg nord2_gpu_irq) ever runs on the same CPU,
and then the level line storms (sleh EOIs, nothing clears the source). A safe version must (1) route SPI 308/313 (VENC) and the
VDEC lines to one CPU, (2) in the top half mask the line at the GICD (ICENABLER) before EOI and wake the waiter, (3) re-enable
only after the waiter acked the block, (4) keep a storm guard (Nord2GPU's nord2_irq_guard) and polling as fallback, (5) learn the
INTID as Nord2GPU does (pending while the status is set) before enabling. Expected gain over 0314: ~0.1-0.2 ms per wait.
Verdict: not worth the hang risk now; revisit only if vc26/27 per-frame data show the wait (not the ioctl count) dominating.

**0315 JPEG decode** (`0315-nord2-jpeg-hw-decode-path.patch`, new ABI `include/redwood_jpegdec.h`, additive; redwood_jpeg.h
unchanged, RWJ_DECODE still ENOTSUP, RWJ_INFO.reserved[0] = RWJD_REV = 1 when present). One ioctl `RWJD_JOB` (nr 4):
user space (J04 n2jdec) computes the register words exactly as Android's vendor library does for the hybrid driver; the kernel
copies the bitstream into its claim (zero padded), zeroes 64 KiB of index tables, powers VEN + JPGENC as for an encode
(shared K02 reference), adds vencsys bit 12 (jpgdec core 0) and larb7 ports 11/12 (OSTDL 1/4, NON_SEC_CON bypass), writes the
words in order (the list ends with the 0x240 trigger), polls 0x274 (20 us steps for 2 ms, then 100 us; <= timeout_ms <= 2 s),
returns 0x274/0x278/0x340, clears 0x274, resets 0x090 0x00/0x10, gates bit 12, copies dst_bytes of the plane area out.
`RWJD_PROBE` writes no decoder register and returns the listed offsets (J04 E0/E1: power-on defaults). Safety: allow-list of
the 36 offsets J04 uses; the 13 DMA-address registers (0x140-0x154 banks, 0x200, 0x248, 0x31C, 0x338, 0x36C-0x374) must be 0 or
16-aligned inside the decoder claim below the guard; the [I] scalar slots (0x17C, 0x330, 0x334, 0x33C, 0x344) < 2^24; 0x340/0x348
read-only. Decoder claim (moved pool frees it): bitstream 0xDA800000 +16 MiB, index 0xDB800000 +1 MiB, planes 0xDB900000 +64 MiB,
guard 0xDF900000..0xDFC00000 (DPM 0xDFE70000). The encoder claim 0xD8000000 +40 MiB is untouched, so encode and decode do not
share buffers; they share the one-job lock. What it does NOT prove: that the hybrid words decode (J04's 13 [I] words); the
phone order is J04 E0/E1 (probe) -> E2 (supplementary frame-mode set) -> E3 (21 words) -> E4, each one RWJD_JOB.
Host: p031x/test/t_jpegdec.c = K01's whole suite on the patched file (822/822) + 54 decoder checks (probe writes nothing, every
refusal before any register access, order/trigger/EOF/reset/gate, bitstream padding, index zeroing, exact copyout, error and
timeout paths, encode after decode, copyout fault); K01's harness got N2JD_DEC_MODEL hooks (inert by default, K01 822/822).
User space still to do (opus-jpeg): n2jpeg_phone S7 -> RWJD_JOB (words from n2jdec with RWJD_SRC_PA/RWJD_IDX_PA/RWJD_OUT_PA).

Tests 12 Nov: p031x 589041/589041 x2 + JPEGDEC 876/876 x2; N2P_UPTO=0311 589024; K01 822/822 x3; K02 588919 x4;
make_series --check OK (0312-0315 apply on the q10in stage after 0310/0311); staged CI compiles of sweet_touch.c clean
(__TEXT +456 B for 0314, +2556 B for 0315).

## Image q10jh (opus-pool, 12 Nov): q10jg + 0313, vcodec DMA pool 0xA0000000 + 384 MiB (was 0xDA800000 + 86 MiB)

`gpu/build/device-mmu-q10jh-candidate/ui-sched/nord2-ios-metal-q10jh-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33550336 bytes,
SHA-256 `33deeaba5b53e09332d69f9e807bdf2b5f5c6f0478745a08d7cfdbd1ced995d6`, kernel `c7c8f561...`, boot spare 4096, FAILURES none;
recipe `gpu/build/build_q10jh.sh` (= build_q10jg.sh + 0313 after 0312). 0313 sha `8ca2a7e1...` (0312 `154df639...`).
Why: 4K decode needs ~110-250 MB of pool (DPB up to 17 x 12.4 MB + vendor working buffers); 86 MiB fits 1080p only.
Memory map [V]: XNU owns 0x1_42000000 + 0xEC000000 (loader NORD2_XNU_PHYS_BASE, pack_with_args BIG_MEM); everything below
0x1_42000000 is outside XNU's managed RAM, so the fixed claims (cameras 0xD0000000, JPEG 0xD8000000, the RWVC pool) cost iOS
nothing and need no pmap_steal/ml_static carve-out: the driver maps the PA lazily with ml_io_map_unmappable (kernel_map
KMA_DATA VA; 384 MiB = 24576 16K PTEs ~192 KiB of page tables). Free below 4 GB = live.dts mblocks minus loader
(0x40000000+0x14000000), Wi-Fi CONN/DMA (0x8fb00000+0x1500000), DVFS share (0x70000000+2M), cameras/JPEG; the
largest gap is 0x9FF00000 (SCP-reserved end) .. 0xBD6C0000 (vpu_binary). The old range cannot grow upward (DPM 0xDFE70000,
md_mem_usage 0xE0000000) nor downward (JPEG/cameras), so the pool moved; 0xDA800000..0xDFE70000 is now unused.
Change: redwood_vcodec.h RW_CLAIM_VCODEC_PA 0xa0000000 / BYTES 0x18000000 (+ comment); nord2_vcodec.inc a _Static_assert
(pool inside the gap, < 4 GB, whole bitmap words) and n2vc_pool_free_pages counts by popcount (98304 pages). Bitmap grows
9.5 KiB in __bss (no image bytes); sweet_touch.o __TEXT -160 bytes. Daemons take pool_pa/pool_bytes from RWVC_INFO (nothing
in daemon/vcu hardcodes 0xda800000/0x5600000; only host fakes/tests use the header constants). No boot-arg: the size costs
no iOS RAM, so there is nothing to trade.
The live sources (kern/bsd/dev, include/) stay at 86 MiB so make_series.sh --check keeps 0310 pinned; 0312/0313 are
hand-made deltas, now checked to apply by make_series.sh and applied by test/run_tests.sh (N2P_UPTO=0311 = old behaviour).
Tests: p031x/test/run_tests.sh (0310-0313, 384 MiB pool, incl. whole-pool alloc) PASS 589024/589024 x2; test_k02 588919 x4
(t_k02.c fake pool VA moved 0x70000000 -> 0x10_00000000 so a >256 MiB pool cannot alias the 0x80000000 window VAs);
test_ik 5909/5909; make_series --check OK; staged CI compile of sweet_touch.c (q10in stage + 0310-0313) clean.
Phone check: boot log `NORD2 VCODEC: ... pool 0xa0000000+0x18000000`; `rwvc_probe` INFO pool_pa 0xa0000000 pool_bytes 402653184.
Risk: 0xA0000000.. was never DMA'd on this phone (cameras/JPEG used 0xD...); it is free in the live FDT and in every claim
list here, but a firmware using undeclared DRAM there would show as corrupted pictures, not a crash. Recovery: q10jg.

## Images q10jd (bisect) and q10je (relay without process references) (opus-relaycrash, 11 Nov)

vc8 (q10jc): `o1 p43 #29, r0 r1 r2 r3 r5 r6 r7, r10, r11, r12`, reset; the death point moved with the removed work, i.e. it is
time-based (~0.5-1 s after the relay open returned) and the probe's `i1` never printed.
- **q10jd** (diagnostic only, never ship): minor-1 open/close/ioctl only print `NORD2 VCT stub o1|c1|i1 p<pid> [cmd]` and
  return 0/0/ENOTTY (no proc_find, lock, slot or delay); minor 0 unchanged.
  `gpu/build/device-mmu-q10jd-candidate/ui-sched/nord2-ios-metal-q10jd-ui654fb60dvfs-cap3500-ram3776-sched.img`,
  SHA-256 `f5bb27cc7ee73b48097da99bd74eef40c516a4b31a67bad6c8391578a0339aa0`, spare 4096, FAILURES none.
- **q10je** (live sources): relay open takes no proc_find() reference (slot = pid + uniqueid + matched pointer); the reaper
  uses proc_find()/proc_rele() transiently (dead = NULL); all q10jc breadcrumbs kept.
  `gpu/build/device-mmu-q10je-candidate/ui-sched/nord2-ios-metal-q10je-ui654fb60dvfs-cap3500-ram3776-sched.img`,
  SHA-256 `e9f79c0cde88e3089c5aa5b8229642f82b16dbcc9d29070a94d193b3e46d9498`, spare 4096, FAILURES none. 0310 = af4dfed4.

## Image q10jc (opus-relaycrash, 11 Nov): dead-client check without proc_uniqueid/proc_exiting (supersedes q10jb)

`gpu/build/device-mmu-q10jc-candidate/ui-sched/nord2-ios-metal-q10jc-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33550336 bytes,
SHA-256 `743e5eda732a5a3f6cb315808e590829aaef48fc11e68aef42e9113e7980db31`, kernel `e7b8c69d...`, boot spare 4096, FAILURES none.
vc7 (q10jb): probe open r0-r7 complete, then vcodecd's receive: `r10`, (`=0` = the open's result), `r11`, reset. n2vr_reap()
now decides liveness by proc_find() alone (NULL for an exited/exiting process); no wait after r11; other breadcrumbs kept.
Both removed calls are real exported functions in the running kernel (not header inlines); proc_uniqueid(p) also runs in open.

## Image q10jb (opus-relaycrash, 11 Nov): q10ja + step breadcrumbs inside the relay (supersedes q10ja)

`gpu/build/device-mmu-q10jb-candidate/ui-sched/nord2-ios-metal-q10jb-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33550336 bytes,
SHA-256 `e266ef936f600975729b4d6125e45b35da773fe14c7f28ea26a63b9253ca941d`, kernel `54276da8...`, boot spare 4096, FAILURES none.
vc6b (q10ja): last lines `VCT o1 p43 #29`, `i0 p25 #30`, `=0` (the open's result, read as such), `=60`, then reset. q10jb adds
`NORD2 VCT r<n>` + 300 ms at each step of relay open (r0-r7), reap with a client (r10-r16), INFO (r20-r22), close (r30-r33),
60 steps from the first minor-1 entry; minor-1 entry wait 300 ms.

## Image q10ja (opus-relaycrash, 11 Nov): q10jz + flush-safe trace (supersedes q10jz)

`gpu/build/device-mmu-q10ja-candidate/ui-sched/nord2-ios-metal-q10ja-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33550336 bytes,
SHA-256 `4e41f979fff271fcc2971cf418138b74e7ebfa9d74cf35485d7b5dbd152348da`, kernel `0b2650f1...`, **boot spare 4096**, FAILURES none;
recipe `device-mmu-q10ja-candidate/build_q10ja.sh`. q10jz (run vc3) died again at the probe's wake-up with no `VCT o1` line.
Now: from trace entry #4 on, 1.5 s delay() after the entry line, 0.5 s after the result line (cdevsw wrapper, no lock held);
kern.n2vctrace is a SYSCTL_PROC logging `NORD2 VCT sysctl ...` with 1.5 s waits around the write. Matching probe (guest,
`device-mmu-q10ja-candidate/guest/n2vcd_probe`, sha 74b9da43...) logs `woke after 45s`, the sysctl and the open step, 2 s apart.

## Image q10jz (opus-relaycrash, 11 Nov): q10jy + vcodec breadcrumbs (supersedes q10jy for the next flash)

`gpu/build/device-mmu-q10jz-candidate/ui-sched/nord2-ios-metal-q10jz-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33548288 bytes,
SHA-256 `792042e3df86b4b87807b6195c0ef9dec5884ec4a39a77c8b137a9d4e6b79c30`, kernel `b8cb2bd3...`, boot spare 6144, check.log
`FAILURES: none`; recipe `device-mmu-q10jz-candidate/build_q10jz.sh` (= build_q10jy.sh, same series/pin/command line).
Why: q10jy reset the phone ~45 s after n2vcd_probe started (its first open of /dev/redwood-vcodec-client), no panic text.
Code audit of the whole path (vcodecd open/INFO/close on minor 0; probe open/INFO/close on minor 1 every 1 s, server absent)
found no defect; host replay `test_ik/t_relay.c test_probe_replay` passes under ASan/UBSan/TSan. Root cause NOT verified.
Change (0310 only): every one of the first `kern.n2vctrace` (sysctl RW, default 64) entries into either node prints
`NORD2 VCT o|c|i<minor> p<pid> [ioctl]` before the handler and `NORD2 VCT =<err>` after it. n2vcd_probe sets it to 64
before its first open (needs the iOS rebuild). Reading the next log: last line `NORD2 VCT i1 ...` with no `=` -> that
handler; no `VCT o1` at all -> the crash is not in the vcodec nodes.

## Image for the single big flash: q10jy (opus-kern2, supersedes q10jx)

`gpu/build/device-mmu-q10jy-candidate/ui-sched/nord2-ios-metal-q10jy-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33548288 bytes,
SHA-256 `501d794c1f4c2f5212a7a8eecfb230af19aad034037ae15fb376300f2a9951d7`; **boot spare 6144 bytes** (q10jx 10240, q10iu 28672);
pack `check.log`: `FAILURES: none`. Kernel SHA-256 `bb36190123c4add912b6fa3d8d0f6000930932d1e19539f800271efe11e2d002`
(22770016 bytes, kernel_gz 25915134; kernel dir `device-mmu-q10jy-candidate/k`, receipt `k/kernel/receipt.json`, staged-source
hashes `k/kernel/stage-sources.sha256`). Recipe: `device-mmu-q10jy-candidate/build_q10jy.sh` (= the q10jx/INTEGRATION.md command
with the four new patches and two new patch dirs). Same command line as q10jx/q10iu. ADT `962aa6c9…` (differs from q10jx only by the
audio package). Audio pin: **`gpu/build/autotest/pins/audio-q10jy.pkg`** = `camera-userland-20261109/out/audio.pkg`, SHA-256
`1aea9991c88a2d71440768d8256e9f501647339a49d98099f1bc4e39c199131a` (verified; adds "a camera starts as it was last", REPORT.md).
Nothing had to be dropped to fit.

Series: q10hq `S=` (0088…0294) + `0296,0298,0299,0300` (= q10in) + `0310,0311,0320,0321,0330,0331`. **0301 is NOT included.**

| Patch | Dir | What | Objects |
|---|---|---|---|
| 0310 | kern/p031x | regenerated 17:25 from K02 rev 7 (`nord2_vcodec.inc` 800fd77c: VDEC_ALL 0x16000000+0x30000 window, larb4/larb5 SLP_CON bit-0 allow-list), K01 b57f87f9, IK a2e90ed5; patch sha cd57b0a6 (MANIFEST.txt) | sweet_touch.o |
| 0311 | kern/p031x | unchanged (529aeda8) | sweet_touch.o |
| 0320, 0321 | build/audio-capture-20261111/patches | `/dev/redwood-mic` (AFE mic path without a call + devfs node/worker) | audio_call.o, audio_device.o |
| 0330 | build/i2c8cam-20261111 | binary nop of the three CAM-bit-23 refusals in the CI members audio_i2c8.o (0x8e8) / audio_hw.o (0x568, 0x23a0), done by `build_p1_candidate.py` (receipt `audio_objs_0330`) + audio_hw.c source record | archive members |
| 0331 | build/i2c8cam-20261111 | camera takes the I2C8 lease per FAN53870 transaction; audio waits <= 4 ms for it | sweet_touch.o |

Conflicts: none. The six new patches touch disjoint files (0310/0311: sweet_touch.c, nord2_autotest.inc + new .inc/.h; 0320/0321:
audio_call.[ch], audio_device.c + audio_mic.inc; 0330: audio_hw.c; 0331: nord2_cam.inc, audio_hw_native.inc); every apply log is
clean (no offset/fuzz); no duplicate patch number across the 22 patch dirs; the staged files equal the owners' `src/` copies
(audio-capture and i2c8cam MANIFEST sha256 match).

Tests re-run 11 Nov 18:30-18:45 (opus-kern2, host only):
- `p031x/make_series.sh --check` OK after regeneration; `audio-capture-20261111/make_series.sh --check`, `i2c8cam-20261111/make_series.sh --check`: up to date
- `p031x/test/run_tests.sh`: N2TEST P031X PASS 589024/589024 (ASan/UBSan + -O2, 0311 checks 105/105)
- `test_k02/run_tests.sh`: N2TEST K02 PASS 588919/588919 x3 (incl. N2VC_VENC_SLOW=0)
- `test_ik/run_tests.sh`: N2TEST IK PASS 4439/4439 (incl. 0299 KERNEL COMPILE OK, 0 unknown symbols)
- `compile_check.sh`: KERNEL COMPILE OK, new undefined symbols: 0 unknown
- `audio-capture-20261111/test/run.sh`: call regression, 0320 mic, ring, worker all pass; call-path write trace identical (334 writes)
- `i2c8cam-20261111/test/run.sh <tfa98xx.cnt>`: unpatched shim FAILS with the phone signature (expected), patched PASS 0330; nord2-audio-check 14/14 OK.
  0331 has no host model test (compile only).
- Staged compile in the build with the CI flags (`-Wall -Wextra -Werror`): sweet_touch.c, audio_device.c, audio_call.c no diagnostics; link OK.

## Image q10jx (superseded by q10jy; kept as the pre-mic/pre-I2C8 reference)

`gpu/build/device-mmu-q10jx-candidate/ui-sched/nord2-ios-metal-q10jx-ui654fb60dvfs-cap3500-ram3776-sched.img`, 33544192 bytes,
SHA-256 `df4ce447360ebe6a141435a6d8caa7a13059228405f6382bc28d67e38b2e1914`; kernel SHA-256
`572376569db349bc3cfc32d2f501baaa6e6d8a7803d56350a09d5c2797b06857` (kernel dir `device-mmu-q10jx-candidate/k`); pack `check.log`:
`FAILURES: none`, boot spare 10240 bytes. Same command line, ADT and audio pin (`autotest/pins/audio-q10iu.pkg`) as q10iu.
Inputs: K02 nord2_vcodec.inc 7ded4040 (273 MHz VENC default, N2VC_VENC_SLOW=1), IK relay a2e90ed5, K01 jpegdev b57f87f9 (MANIFEST.txt).
Clock: a VENC power-on from vcodecd (RWVC_POWER) or autotest step 1 sets venc_sel to mainpll_d4_d2 = 273 MHz while VEN/VENC_C1
are off; `hwv.py step 1 R --fast` selects univpll_d4 = 624 MHz instead; the JPEG driver's shared reference keeps 624 MHz;
a running domain is never re-muxed.

## GCE command lists (where they are executed)

The vendor library emits `struct gce_cmds` (parallel cmd/addr/data/mask arrays, mtk_vcu_abi.h) through VCU_GCE_SET_CMD_FLUSH,
not raw 64-bit GCE instructions (Linux's mtk_vcu.c vcu_set_gce_cmd builds the cmdq packet from them). They are interpreted in
user space by V40: `gpu/hwcodec/vcu/n2vcu.c` `gce_run_list()` (CMD_READ/WRITE/POLL_REG -> RWVC_REGS batches, CMD_WAIT_EVENT
-> RWVC_WAIT_IRQ via `gce_event_irq()` (VENC_EOF -> IRQ_VENC, VENC_EOF_C1 -> IRQ_VENC_C1), CMD_MEM_MV / CMD_POLL_ADDR on pool
memory), tested in `vcu/test_v40/t_v40.c` (GCE section). The kernel needs no GCE parser: it gives windows (0x3000 >= the 0x1300
used), polled wait on +0x5c with ack at +0x60 (a second ack by the list is a harmless W1C of nothing), the pool (page-aligned
PAs < 4 GB, so < 64 GiB and 16-byte aligned), and IOMMU bypass + no MMSRAM on all 17 VENC ports of larb7/larb8
(mt6885-larb-port.h:91-141; this answers VENC_PROTOCOL.md section 7 item 2). Open (section 7 item 1): whether +0x5c bit 2 latches
when GCE is not used; the first-done log line (status + GIC INTID 340 pending) answers it on the first encode.

## Patches (kernel series of the phone image q10in/q10iu + these two)

| Patch | What | Source of truth |
|---|---|---|
| `0310-nord2-jpegdev-vcodec.patch` | K01 `/dev/redwood-jpeg`, K02 `/dev/redwood-vcodec` (incl. the audit fixes below), IK relay `/dev/redwood-vcodec-client`, the two ABI headers, sweet_touch.c hooks. Exactly what `kern/make_patch.sh` calls 0299, renumbered (`0299` tags → `0310`) because 0299/0300 are the surface-budget patches already in q10in. | `kern/bsd/dev/*` (K01/K02/IK), `include/*.h` |
| `0311-nord2-vcodec-venc-autotest.patch` | new `bsd/dev/nord2_vcodec_at.inc` (VENC steps 0-4), its include, `nord2_autotest.inc`: command 8 + JPEG command 7 step 4 refused (result 30) while anyone holds VEN | `p031x/bsd/dev/*` |

`bash make_series.sh` regenerates both from the live sources and checks that they apply to the **q10in stage**
(`gpu/build/device-mmu-q10in-candidate/k/kernel/stage`; kernel/b is byte-identical to it in the three touched files);
`bash make_series.sh --check` fails if the pinned patches are stale. `MANIFEST.txt` holds the input/output sha256.
Series of the image: q10hq `S=` + `0296,0298,0299,0300,0310,0311` (0301, the crash-log thread, is NOT included).

## Audit (K = nord2-current-kernel; [V] read in source or seen on the phone, [I] inferred)

Accepted as correct [V]: VEN/VEN_CORE1/VDE/VDE2 PWR_CON, status bits, bus-protection bits and order (clk-mt6885-pg.c:387-410,
2418-2919); venc_sel/vdec muxes; vencsys gates 0/4/28 + post-power 0x4 write (pg.c:1176-1182); larb7/8 conf + OSTDL
(smi_conf.h:95-99, 247-305); VENC IRQ status +0x5c / ack +0x60, bits 0xbf (mt6885/mtk_vcodec_intr.h:22-31, intr.c:189-215);
VDEC 0xa4 / 0x10000 / 0x11 / 0x10 (intr.c:61-153).

Found and fixed (hunks applied by K02 in his file, tested here):
1. **Window size** [V]: vpud maps VENC and VENC_C1 with length 0x3000 (host/PROGRESS.md mmap list); the driver allowed 0x2000. Now 0x3000.
2. **SMI parent of core 1** [V source]: larb8 (VENC core 1) and larb4 (VDEC core) hang off the MDP SMI common
   (smi_drv.c:198-232, mdp_smi_common@1f002000, iommu1 m4u@1f027000), larb7/larb5 off the DISP one. VENC_C1/VDEC_CORE now
   refuse (EACCES, nothing written) unless SPM bit 19 (MDP) is on; every block refuses if disp_sel (CLK_CFG_1 bit 7) is gated,
   the MDP ones also if mdp_sel (bit 15) is gated (clk-mt6885.c:1693-1702).
3. **Last close** [V source]: XNU calls d_close only at the last close (spec_vnops.c spec_close, vcount == 0), d_open on every
   open; the old close freed one slot → slot leak + `server_present` stuck. Now every slot of the minor is ended.
4. **MMSRAM routing** [V source]: Linux sets NON_SEC_CON bits 16-19 on VENC ports 5,6,13,14,21,22 (smi_sysram_enable from
   mt6885/mtk_vcodec_enc_pm.c:308-317). We do not power MMSRAM: the VENC larb mask is 0xfff01 (IOMMU off, addr bits 32+ zero,
   no MMSRAM). **vcodecd must give the library `slbc_ready = 0`** (venc_ipi_msg.h:404) [I: the library then keeps NBM in DRAM].
5. GIC evidence: first VENC done logs GICD_ISPENDR of INTID 340/345 (read only).

Not changed, reported: Linux powers BOTH VENC cores for any job (mtk_vcodec_enc_pm.c:271-282) [V]; whether the library
touches core 1 for a single-core 1080p encode is [I] (it maps the C1 window). A real VENC interrupt cannot be added by a second
`ml_install_interrupt_handler` (one handler per CPU, osfmk/arm64/machine_routines.c:801-821 [V]; Nord2GPU owns it): it needs a
chained dispatcher; polling (msleep) stays. Relay: concurrent clients get no per-process close notice (same XNU rule) — IK.

IOMMU bypass for VENC [I, strong]: larb7 sits on iommu0 (m4u@1411a000) like the JPEG ports that worked with MMU_EN=0 on the phone
(runs/q10hw1); the VENC ports use the same NON_SEC_CON bits (pseudo_m4u_v2.c:563-575). The library gets physical pool addresses
(0xDA800000..0xDFE00000, < 4 GB, no address-MSB bits needed). Bus protection: VEN release/set order identical to the
phone-verified 0298 sequence; the DIS (and, for core 1, MDP) parent and their clock roots are checked before any release.

## Tests

    bash gpu/hwcodec/kern/p031x/test/run_tests.sh    # pinned 0310+0311 on kernel/b in /tmp; K02's t_k02.c + t_p031x.c, ASan/UBSan + -O2
    bash gpu/hwcodec/kern/p031x/make_series.sh --check

Kernel compile: the q10in stage + 0310 + 0311, `gpu/tools/xnu_staged_compile.py sweet_touch.c --extra=-Wno-unsafe-buffer-usage`
(CI flags, -Wall -Wextra -Werror): clean; new undefined symbols vs the q10in sweet_touch.o all defined in the q10in kernel.

## q10jy phone test plan (one flash, main session, needs the user's approval of sha 501d794c…)

Flash `boot` only with the exact q10jy image; never stock Android. From `gpu/build/autotest`:

1. **Boot check**: `python3 phone.py wait && python3 phone.py logs <R>/boot.log`. Must look like q10iu/q10jx plus, once each:
   `NORD2 VCODEC: /dev/redwood-vcodec made, /dev/redwood-vcodec-client made, JPEG VEN sharing on ...`, `NORD2 JPEG: /dev/redwood-jpeg made ...`,
   `NORD2 MIC: devfs redwood-mic rate=48000 channels=1 bits=16 prime=2048 max=6144` (after `devfs redwood-audio`). No `NORD2 JPG:` /
   `NORD2 VCODEC:` register line at boot. UI, touch, Wi-Fi as q10jx.
2. **Speaker before camera**: a ringtone/keyboard click -> `NORD2 AUDIO AMP START seq=N`, no `NORD2 AUDIO ERROR`.
3. **Camera + FAN53870 (0331)**: open Camera. Expect `NORD2 CAM: FAN53870 ok: I2C8 pins 0x..11...` / `... all camera LDOs on`, SENINF CAM domain
   on, preview as q10jx (both cameras, switch once).
4. **Speaker after camera (0330)**: shutter sound and a ringtone after step 3 -> `NORD2 AUDIO AMP START seq=N+k`; **no**
   `NORD2 AUDIO ERROR ... i2c=5/16 i2cpa=10006170`, no `i2c=16/0`, no `uncertain=1`.
5. **Mic (0320/0321)**: record a 15 s video speaking near the bottom mic. During it `NORD2 CALL: phase 8` then `phase 7 ... uplink: mic N
   frames peak P` (N growing); after it `NORD2 MIC: opens>=1 sessions>=1 ... in~720000 out~in underruns<=2 peak>500 silent=1`. Pull the
   .MOV and check the AAC level. Decision table: audio-capture-20261111/PLAN.md section 3 step 2 (opens=0 -> VirtualAudio routing; in=0
   -> MT6359/AFE; out=0 -> plug-in read; quiet -> `kern.n2call.pga` / `kern.n2mic.gain`). Kill switch `sysctl kern.n2mic.enable=0`.
6. **VENC kernel steps (hwv.py)**: the "Phone test plan" below, steps 0-5 (`python3 $V run $R --upto 0` ... `step 4`, 60 s + `phone.py
   status` after steps 1 and 4; core 1 only if MDP and mdp_sel are on).
7. **JPEG regression**: `python3 ../../hwcodec/tools/hwc.py run $R3` (as q10hw1) -> check.json PASS; then `STAGE=jpeg n2hw_phone_tests`
   (`N2JPG selftest 640x480 PASS 12/12`, jpeg_check.py PSNR >= 30 dB).
8. **Daemon tests** (after installing the five guest files, INTEGRATION.md "Phone test order"): `STAGE=preflight`, `STAGE=vcodec`
   (`N2VC verdict PASS`, 60 s, status), then `STAGE=daemon ENABLE_DAEMON=1` -> `out.h264` through h264_check.py. Never run hwc.py command 7
   concurrently with a VEN user.

Pass for q10jy: 1-4 as described (speaker alive after the camera), 5 gives a non-silent track or a decision-table answer, 6/7 as below.
**Recovery image: q10iu**, `gpu/build/device-mmu-q10iu-candidate/ui-sched/nord2-ios-metal-q10iu-ui654fb60dvfs-cap3500-ram3776-sched.img`,
SHA-256 `a7640bf5449c28deb281be8a0d259aecf5d06aa050d5ef05bcf01402ccc4eaa0` (present and verified 11 Nov 18:30; no rebuild needed), or the
RAM ADB rescue.

q10jy risks: boot spare only 6144 bytes (any further patch needs a size cut first: 0311 autotest strings ~1.3 KB, nord2_vcodec.inc log
strings ~1.8 KB are the cheapest); 0331 is compile-tested only (no host model); 0330 edits CI objects by exact sha/word/context (a wrong
member would have failed the build); the mic needs a working speaker path (PLAN.md risk 1, addressed by 0330); the new audio.pkg
(1aea9991) changes camera start behaviour in userland at the same time, so a camera regression in this flash can come from either side.

## Phone test plan (main session; host tool `gpu/hwcodec/tools/hwv.py`, autotest command 8)

Nothing in 0310/0311 runs at boot: the boot must look like q10iu plus the device-node lines. Every step is one USB command
on the autotest thread, logs `NORD2 VCODEC:` lines, and is bounded (20 ms power/ack waits; the host gives up after 20 s).

    R=../../hwcodec/runs/<NAME>; V=../../hwcodec/tools/hwv.py      # from gpu/build/autotest after flashing
    python3 phone.py wait && python3 phone.py logs /dev/null
    0. python3 $V run $R --upto 0          # READ ONLY. Expect: spm status with bits 19 (MDP) and 20 (DIS), VEN 0x1112 (off),
                                           #   CLK_CFG_13 0xc030601, CLK_CFG_1 disp_sel on (+ mdp_sel state), "VENC domain not usable".
       python3 $V step 0 $R --c1           # also reports core 1 (read only)
    1. python3 $V run $R --upto 1          # VEN on at 273 MHz (CLK_CFG_13 0xc030601 -> 0x4030601; --fast keeps 624): VEN 0xd, gates 0x10000011, larb7 0x24 0x300256 0x40 0x1 0x524 0x300256,
                                           #   "VENC ports nonsec or 0", "ostdl mismatch 0", jpeg nonsec7-10 unchanged, +0x5c 0.
                                           #   Then WAIT 60 s and `python3 phone.py status` (the camera died 27-44 s after a bad release).
    2. python3 $V run $R --upto 2 --no-off # larb read-back of all 17 VENC ports + interrupt status -> result 1
    3. python3 $V step 3 $R                # reads VENC +0x5c only. Optional, once a plain R/W register is known (opus-venc):
                                           #   python3 $V step 3 $R --reg 0xOFF [--write 0x1234]   (write, read back, restore)
    4. python3 $V step 4 $R                # off: VEN 0x1112, prot mm 0xc000000 mm2 0x3 as after JPEG step 4. Wait 60 s, status.
    5. only if step 0 showed MDP on and mdp_sel on:  python3 $V run $R2 --c1 --upto 2, then python3 $V step 4 $R2 --c1; 60 s; status.
    6. JPEG regression: python3 ../../hwcodec/tools/hwc.py run $R3 (as q10hw1) -> check.json PASS.
    7. then the T02 user-space tests (tests/phone/20-vcodec-readonly.md, 30-venc-power.md) and vcodecd.

Pass for the VENC kernel milestone: steps 0-4 return 1, the phone answers `status` 60 s after steps 1 and 4, and step 6 passes.
Result codes: see `hwv.py --help`. Recovery: flash boot back to q10iu (sha256 a7640bf5…ccc4eaa0) or the RAM ADB rescue.

## Risks / open

- Clock/voltage [V source, I phone]: the phone boots with venc_sel = parent 12 univpll_d4 = 624 MHz (CLK_CFG_13 0x0c030601, q10hw1).
  mt6893.dts mmdvfs_pmqos: venc_step0..3 = 624/416/312/273 MHz with vopp_steps <1 2 3 4>, i.e. Linux raises vcore to OPP 1 before
  running VENC at 624 MHz; nothing in this kernel manages vcore (DVFSRC). JPEG ran at 624 MHz on the phone, so registers and
  short jobs work, but a long encode at 624 MHz on a low vcore OPP could fail [I]. Since q10jx 273 MHz is the default (K02 N2VC_VENC_SLOW=1);
  `hwv.py ... --fast` (arg bit 10) selects 624 MHz once 273 MHz is shown to work.
- Two cores [V source]: Linux enables larb7+VENC0 and larb8+VENC1 for every job (mtk_vcodec_enc_pm.c:271-282) and the VCU maps
  both windows; one IRQ line (SPI 308, VENC_SYS base) serves the encoder. vcodecd should power VENC only first; if the library
  writes the C1 window (it maps it) power VENC_C1 too, which needs MDP on (bit 19 seen on at boot in q10hw1) [I].
- Core 1 / larb8 / iommu1 bypass: never exercised on the phone.
- The GCE command lists of the library are emulated by vcodecd (polled waits, 500 µs sleep slices).
