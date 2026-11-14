# Nord2 iOS port -- GPU performance: where the time goes, what is still missing, in what order

Agent a7c44d21 (Opus 5.5, analysis only), 2026-11-14 ~22:00-24:00. Nothing was flashed or run on the phone; no other agent's tree was edited.
Deliverables are in `gpu/build/gpu-analysis-20261114/` (this file, `tools/`, `shaderstats/`, `perf1a-gpuprof.{txt,csv}`, `afbc_model.txt`).

Conventions: **[V-log]** read in a phone capture (perf1a unless named), **[V-code]** read in the source that ships (kernel stage
`gpusched-20261114/integ/chain/s` = q10jall3 chain, userland `gfx200-20261114/all2-src` = gfxall2, vendor `nord2-current-kernel`),
**[V-host]** measured by a host run in this directory, **[I]** inferred (reasoning given). Clock for perf1a: GPU 654 MHz the whole
capture (decisions-only DVFS), battery 38.6 -> 45.5 C.

---------------------------------------------------------------------------------------------------------------------------------

## 1. Executive summary

### 1.1 What the counters say (new: the PRFCNT block was already being sampled; nobody had read it per phase)

`tools/gpuprof.py` over perf1a (258 two-second windows that carry Mali counters) [V-log]. "Active" = windows with GPU HW busy
>= 10 % (the interaction itself; phase means are diluted by idle stretches and are in `perf1a-gpuprof.txt`):

| phase (perf1a) | active windows | HW busy median (p90) | GPU ms per 11.2 ms refresh | tex starved / frag-active | core-cycles per quad | overdraw (phase mean) | DRAM rd+wr GB/s | jobs/s (phase) | irq+handoff us/job |
|---|---|---|---|---|---|---|---|---|---|
| idle home | (0) | 1.4 % | 0.15 | 90 % | 8.3 | 1.1 | 0.26 | 23 | 142 |
| home + App Library scroll | 9 | 35 % (48 %) | 3.9 | 63 % | 5.6 | 3.3 | 4.0 | 327 | 122 |
| Maps | 27 | 38 % (46 %) | 4.2 (~8 ms per shown frame at ~47 fps) | 41 % | 7.7 | **10.9** | 4.6 | **2323** | 77 |
| Magnifier | 15 | **77 %** (78 %) | 8.7 | 68 % | 8.7 | 27.2 | 4.5 | 695 | **1683** |
| Safari | 21 | 39 % (66 %) | 4.4 | 59 % | 4.7 | 8.6 | 3.8 | 1369 | 55 |
| Photos / switcher | 6 | **55 %** (64 %) | 6.2 | 61 % | 4.6 | 7.6 (10-25 in the switcher windows) | 5.0 | 1174 | 120 |

Read together with the JOBS/LAT/LAT2 lines, ten facts drive everything below:

1. **The GPU's raw throughput is not yet what limits most of the UI, but the headroom is thin in the switcher.** In active windows
   the GPU is busy 35-39 % (home scroll, Maps, Safari; p90 46-66 %) and 55 % in Photos/switcher (p90 64 %) at 654 MHz, i.e.
   3.9-6.2 ms of every 11.2 ms refresh; the app switcher (blurs + snapshots, overdraw 10-25) is the scene closest to GPU-bound for
   90 Hz. Fill rate needed at 90 Hz is 7-23 % of the G77 MC9 peak (section 3). [V-log]
2. **The shader cores are latency-bound, not ALU-bound**: the texture filter unit sits idle waiting for data 41-68 % of
   fragment-active cycles in active windows (up to 90 % at idle); FMA density is 0.02-0.09 warp-instructions per active core-cycle;
   a 2x2 quad costs 4.6-8.7 core-cycles of fragment activity (a G77 core can rasterise/shade ~2 cycles per quad for simple shaders). Mean DRAM read latency seen by the L2 is 310-370 ns
   (200-240 GPU cycles). [V-log] => **raising the GPU clock scales sub-linearly** (latency in ns does not shrink); every item that
   removes a DRAM round trip or a dependent load counts more than its share of "cycles". [I]
3. **Bandwidth is not the limit**: 0.3-4.7 GB/s of ~29.9 GB/s peak (3733 MT/s fixed); tile-buffer write-back is 91-98 % of all
   GPU DRAM writes. [V-log] (=> AFBC is an energy/latency item here, not a throughput one.)
4. **Per-job turnaround is a large tax in multi-pass apps**: Maps submits each frame as a burst of ~40 jobs (every render pass a
   vertex/tiler + a fragment job, strictly serial per context); per frame ~42 x 199 us running + ~42 x 77 us (irq 19 + bottom-half
   handoff 58) turnaround = ~8.4 + 3.2 ms: **~28 % of the GPU-side frame latency** (~18 % of wall time) is the kernel starting an
   already-queued job. The 1.5-2.2 ms mean queue wait per job is the burst draining, not saturation (Little's law: ~3.4 jobs in the
   system on average). [V-log averages; the per-frame split is I]
5. **The kernel's GPU path stalls for milliseconds**: driver-lock hold max 2.5 ms (median of per-window maxima, Maps) / 3.4 ms
   (Magnifier), absolute max 8.8 / 11.6 ms; lock waits up to 14.9 ms; bottom-half wake max 1.4 ms (Maps) and **5-10 ms
   (Magnifier)**; one n2c_poll up to 5 ms. [V-log LAT2]
6. **CPU cache maintenance on the GPU critical path**: in Magnifier the kernel cleans **~4.1 GiB/s** of CPU cache by VA
   (8.6 GB per 2 s window = ~87 cleans/s of 48 MiB = one per ISP pass of the 4096x3072 picture) plus invalidates; per job 174 us
   clean + 139 us invalidate on average, 3.6 ms max, ~22 % of one CPU core inside the bottom half under the driver lock. Maps:
   ~0.7 GiB/s = every newly created 44 MB MSAA texture cleaned once (zero-filled pages are cpu_dirty). [V-log JOBS clean_KiB,
   clean_us/inv_us; V-code try_start/job_finish]
7. **backboardd's jobs list more than 32 buffers => whole-context maintenance on every compositor job, and never "precise"**: at
   idle (only backboardd draws) every job is `full` (46 of 46 per window); the ABI carries at most 32 handles (N2C_JOB_BOS) and a
   full list is excluded from 0632's overlap. So the compositor -- the client that decides UI smoothness -- gets the slowest
   maintenance path and none of the pass overlap now being shipped. [V-log JOBS full=n; V-code n2c_submit]
8. **L2 is cleaned+invalidated at the start of every job** (START_FLUSH_CLEAN_INVALIDATE, no FLUSH_REDUCTION): each pass starts
   with a cold L2, which matches fact 2. kbase r32p1 on the same tTRx enables flush reduction and skips start flushes for
   same-context follow-up atoms. [V-code both]
9. **Power/voltage are far from vendor parity**: every OPP runs at VGPU 750 mV; the vendor table puts 654 MHz at 643.75 mV, 512 at
   600, 350 at 550 => **+36 % dynamic power at 654 MHz** (V^2) plus more leakage; shader cores are never power-gated and the GPU is
   never powered down while iOS runs (vendor: cores off after 0.4-0.8 ms idle, MTCMOS + buck off when idle). Heat is the largest
   sustained loss (perf1a: 24 % of the time at thermal level 3 = GPU 512 MHz, 26 % at level 2). [V-code vendor table + rail code,
   V-log]
10. **Scheduling is global FIFO by job id**: no compositor priority, compute shares JS1 with vertex/tiler, no preemption, a single
   128-record job table shared by all contexts with a 96-job window per context. Magnifier's 36.6 ms compute jobs block backboardd
   (queue wait 41.6 ms avg). [V-code try_start, V-log]

Host experiment (shader codegen) [V-host, `shaderstats/`]: compiling the iOS QuartzCore library with the shipping translator,
**Metal constant buffers other than index 0 are read with LD_BUFFER (load/store messages) instead of being pushed into FAU
uniform registers**; Mesa's own Valhall cost model calls 75 % of CoreAnimation's fragment shaders (all blur/downsample passes)
load/store-bound. Recompiled with every constant buffer pushable, the blur chain loses all its loads (e.g. downsample_4: 61 -> 48
instructions, bound load/store 6.0 -> texture 1.0; downsample_8 regains full occupancy); modelled QuartzCore cycles fall x1.92,
VectorKit x1.36. The model is pessimistic for uniform loads, so the real gain lies between ~1.0x and that -- see U1.

### 1.2 Top 10 actions (rank = expected value x confidence / risk, given what is already in flight)

| # | action | layer / who | expected gain (bounds) | risk | order |
|---|---|---|---|---|---|
| 1 | **Ship the in-flight stack in two attributable steps**: q10jfinal1 (clock decisions-only) + gfxall2, then q10jfinal1f (clock acts, 886) + gfxall2/3 | orchestrator | Maps 47 -> >= 60 fps likely (bigpool1/rtpool/msaartt remove the 16 ms texture creations and 34-51 ms resolve blits); Magnifier 10-14 -> ~30 fps (camprev1); touch 54 -> ~125 Hz; clock +0-28 % on GPU-bound phases | medium (first live clock switch + LVTS writes + LL step) | now |
| 2 | **T1 telemetry patch** (more PRFCNT counters, per-context GPU time, flush/maintenance reasons, job-size histogram) + `gpuprof.py` | kernel GPU agent + this tool | 0 % by itself; it is what ranks items 4-9 on the phone | none (log only) | with step 1 or right after |
| 3 | **Take CPU cache maintenance and big-buffer work off the GPU critical path**: (a) no per-job 48 MiB cleans for camera/ISP surfaces (tracked or GPU-only buffers), (b) raise the per-job buffer list 32 -> 128 (ABI v4) so compositor jobs are precise, (c) do clean/zero/page-table work outside the global lock | kernel GPU + userland/camera | removes the 1-10 ms bottom-half/lock stalls (fact 5/6); Magnifier BH time -0.2 s/s; compositor jobs -100+ us each and eligible for 0632 overlap | low-medium (a missed CPU write = stale pixels) | after step 1 |
| 4 | **Compositor-first scheduling** (priority pick, take back a lower-priority NEXT chain, job-table reservation) + **chunked compute dispatches** (<= 2-4 ms per job) | kernel GPU + userland | backboardd queue wait under a heavy app from 2-42 ms to ~one job (<= 2-4 ms after chunking); fewer 'nosw' drops in Maps/Photos/Magnifier | low (aging needed against starvation) | with 3 |
| 5 | **Per-OPP vendor voltages + idle shader-core power gating** (on 0633's live rail code), then full GPU power-down after long idle | kernel GPU | GPU power at 654 MHz about -25-30 % (V^2), idle GPU leakage removed -> later throttling; in a perf1a-like session roughly one thermal step more clock for the GPU-heavy half (+23-28 % there) | medium (undervolt -> faults; keep +12.5-25 mV margin, fallback exists) | after 3/4 are stable |
| 6 | **kbase-parity L2 policy**: FLUSH_REDUCTION + no start invalidate for same-context follow-ups without CPU/foreign-dirty buffers; no end clean for chains that write only GPU-private buffers | kernel GPU | -5-30 us per job + a warm L2 across passes: 5-15 % of fragment time in multi-pass scenes (blur, Maps) [I] | medium (stale data if a foreign write is missed; needs T1 + kill switch) | after T1 |
| 7 | **Push constants for every CPU-mapped Metal constant buffer** (FAU promotion beyond UBO 0) | userland | host recompile: all loads gone from CA's blur chain, modelled x1.92 (QuartzCore) / x1.36 (VectorKit); realistic 10-30 % on blur/effect passes, ~5 % on plain copies [I] | medium (GPU-written constant buffers; WC reads) | after gfxall3 |
| 8 | **GPU IRQ on a big core + bottom half next to it**, start-next path shortened | kernel GPU | handoff 55-120 us -> ~30 us [I]; up to ~10 % of wall time back in serialized phases, partly overlapping 0630/0632 | low | with 3 |
| 9 | **Read-only shader cache readable by sandboxed apps** (in the Nord2Metal bundle, seeded from daemon entries or built on the host) on top of gfxall3's /var/db cache | userland | first-use hitches (227 pipelines x ~5 ms in perf1a) gone for apps too, from the first launch | low | after gfxall3 |
| 10 | **One memory budget for all GPU pools** + pressure release; block the respawn loops | userland + orchestrator | avoids jetsam / memory-exception stutter that the new pools would otherwise cause in Safari-class sessions; ~0.5 % CPU and some heat | low | with gfxall2 |

Order of APPLICATION (not rank): 1 -> 2 -> 3+4+8 (one kernel step) -> 6 -> 7 (+9) -> 5. Rationale in section 6.

Realistic targets after 1-10 (section 3): UI at 87-89 fps with drops limited to first-use and app-side stalls; Safari 85-89 fps;
Maps 60 fps with headroom, 80-90 fps plausible on simple tiles; Magnifier UI ~45-60 fps with a 30 fps preview; Subway Surfers
already 89 fps median (night run) -- the risk there is sustained heat, which items 5 and 3 address. "Equal to the Android Mali
driver" is reachable for the GPU kernel/driver path; the camera path stays heavier than Android because the ISP/MDP work runs on
the GPU here (section 4.4, S2).

---------------------------------------------------------------------------------------------------------------------------------

## 2. Evidence in detail

### 2.1 Tools written for this analysis

- `tools/gpuprof.py LOG [--phases name:t0-t1,...] [--csv out] [--rows]` -- per-phase GPU profile from PRFCNT A..E (and F..I once the
  T1 patch prints them) + JOBS/LAT/GDVFS/FB lines. 0.5 s per capture. Output for perf1a: `perf1a-gpuprof.txt`, per window:
  `perf1a-gpuprof.csv`. Column meanings are in the tool's docstring.
- `tools/shaderstats.py HARNESS LIB.metallib OUT` -- compiles a metallib with the shipping translator (y2s99b host harness:
  AIR -> NIR -> Mesa 25.0.7 Valhall) with `BIFROST_MESA_DEBUG=shaders,shaderdb`, attributes the stats and the final disassembly to
  each function, counts LD_BUFFER/TEX, and prints Mesa's per-unit bound now and if constant-buffer loads were pushed. Results:
  `shaderstats/{quartzcore,vectorkit,videotoolbox,magnifier,alloy}.tsv`.
- `tools/afbc_model.py IMG...` -- AFBC-like lossless size model on screenshots (`afbc_model.txt`).

### 2.2 Counter facts (perf1a, 654 MHz) [V-log]

- Fragment work dominates every phase except Magnifier: JS0 active 84-87 % of GPU-active time, JS1 13-17 %; Magnifier JS1 59 %
  (compute ISP + VideoToolbox).
- Tiles shaded per shown frame (in full-screen 16x16-tile equivalents, 10 863 tiles for 1125x2436): home 1.8, Photos 3.7, Safari
  4.6, Maps 8.6, Magnifier 23. Full-screen RGBA8 surfaces written back per frame: 1.5 / 2.2 / 2.6 / 6.3 / 12.
- Texture beats received by the cores ~ equal to DRAM read beats (100-112 %): texture data is nearly all streamed from DRAM, little
  L2 reuse (1:1 compositing + L2 invalidated at every job start).
- External write stalls 5-7 % of L2 cycles in Maps/Magnifier (MSAA stores, ISP), 4-5 % in Safari/Photos, < 1.5 % at idle/home; read
  stalls 0.
- Caveat on per-frame numbers: the frame count comes from the 5 s `FB CLIENT` fresh count of the foreground client, the counters
  from 2 s windows; "GPU ms per refresh" (busy share x 11.2 ms) is the robust number, per-shown-frame figures are approximate.
- Idle after the session ("idle-hot", 580-745 s): ~80 render passes/s from a client that is not the compositor kept the GPU 3.5 %
  busy and the bottom half running at the home screen (unidentified: T1's per-context time would name it).

### 2.3 Driver-path facts [V-log JOBS/LAT/LAT2, per phase]

| phase | CPU clean us/job avg (max) | invalidate us/job avg (max) | BH maintenance ms per s | lock hold max (median/abs) | BH wake max (median/abs) | full-maintenance jobs |
|---|---|---|---|---|---|---|
| idle | 58 (182) | 79 (289) | 3 | 0.06 / 0.08 ms | 0.02 / 0.05 ms | **100 %** |
| home | 48 (826) | 66 (473) | 37 | 0.06 / 3.6 ms | 0.02 / 2.1 ms | 19 % |
| Maps | 11 (2722) | 8 (2311) | 43 | **2.5 / 8.8 ms** | 1.4 / 4.6 ms | 6 % |
| Magnifier | **174 (3875)** | **139 (3830)** | **217** | **3.4 / 11.6 ms** | **5.0 / 10.0 ms** | 7 % |
| Safari | 12 (4115) | 17 (766) | 39 | 0.25 / 7.7 ms | 0.35 / 2.0 ms | 10 % |
| Photos | 43 (2688) | 60 (872) | 51 | 0.17 / 3.3 ms | 0.04 / 2.3 ms | 12 % |

### 2.4 Shader codegen (host) [V-host]

QuartzCore (`/System/Library/Frameworks/QuartzCore.framework/default.metallib`), 93 fragment/compute stages compiled:

| shader | inst | Mesa bound now | LD_BUFFER | TEX | bound if CB loads pushed |
|---|---|---|---|---|---|
| downsample_4_frag_lpf | 61 | ls 6.0 | 6 | 4 | t 1.0 |
| downsample_8_frag_lpf | 173 | ls 5.0 (1 thread: >32 regs) | 5 | 16 | t 4.0 |
| narrow_blur_15_frag_lpf | 115 | ls 7.0 | 7 | 8 | t 2.0 |
| narrow_blur_27_frag_lpf | 183 | ls 11.0 | 11 | 14 | t 3.5 |
| variable_blur_frag_lpf | 370 | ls 11.0 (1 thread) | 11 | 12 | t 3.0 |
| radial_shading_frag_lpf | 65 | ls 4.0 | 4 | 2 | v 0.75 |

Totals: LS-bound 70 of 93 (75 %), sum of modelled cycles 515 -> 138 (x3.74) by the simple "remove the loads" estimate; 5 shaders
at half occupancy; no spills. VectorKit (Maps): LS-bound 58 of 137 (42 %), x1.37 estimate.

**Real recompile with every constant buffer pushable** (`tools/build-harness-push.sh`: the same harness with
`pushable_ubos = ~0` when PUSH_ALL is set; the control build without PUSH_ALL reproduces the original numbers exactly) [V-host]:

| shader | now: inst / Mesa bound / LD_BUFFER / threads | all CBs pushable: inst / bound / LD_BUFFER / threads |
|---|---|---|
| downsample_4_frag_lpf | 61 / ls 6.0 / 6 / 2 | 48 / t 1.0 / 0 / 2 |
| downsample_8_frag_lpf | 173 / ls 5.0 / 5 / **1** | 164 / t 4.0 / 0 / **2** (full occupancy back) |
| narrow_blur_7 / 15 / 27_frag_lpf | 72 / 115 / 183, ls 5 / 7 / 11 | 63 / 107 / 173, t 1.0 / 2.0 / 3.5, 0 loads |
| variable_blur_downsample_frag_lpf | 67 / ls 2.0 / 2 | 62 / t 1.0 / 0 |
| variable_blur_frag_lpf | 370 / ls 11.0 / 11 / 1 | 345 / ls 4.0 / 4 / 1 (dynamic offsets stay loads) |
| luma_boost_lpf | 33 / ls 4.0 / 4 | 25 / t 0.25 / 0 |

QuartzCore total modelled cycles 515.5 -> 268.3 (x1.92), LS-bound stages 70 -> 20; VectorKit 335.4 -> 245.8 (x1.36), LS-bound 58 -> 22.
Every pass of CoreAnimation's blur chain loses all its constant-buffer loads and ~6-21 % of its instructions; one of them regains
full occupancy. (Compiled for Valhall v9 -- the harness uses the G57 id, same ISA as the phone's G77.)
Disassembly of downsample_4 (excerpt): `IADD_IMM r0, 0x0, #0x1` (buffer index 1) then six `LD_BUFFER.i64/.i128 ... wait` feeding the
texture coordinates and weights of each tap -- a dependent load before every TEX. Why: `pan_shader.c` sets `pushable_ubos =
BIT(0)` (+ sysval UBO) and Metal places CoreAnimation's uniforms at buffer index 1; air_metal.c already supplies `user_buffer` for
slot 0 only. Caveat (important): Mesa's model charges a load/store op one cycle *per thread*; for a uniform address the real unit is
likely much cheaper but each load still adds an L1 round trip in front of a texture fetch. The Arm DDK always pre-loads such data
into FAU, so this is a parity gap; the size of the gain must be measured (T1 counters + an A/B switch).

### 2.5 AFBC model (host) [V-host, model only]

On seven kernel screenshots (1/3 scale, which compresses worse than full resolution): 10-36 % of linear RGBA8 (Settings 10 %, lock
editor 15 %, Photos 22 %, home/CC/gallery 27-28 %, game 36 %). Real AFBC (payload granularity) does worse; 40-60 % saving on UI is a
fair expectation [I]. Applies to tile write-back (91-98 % of GPU writes) only where the target is AFBC: private textures already are
(gfx200 notes), IOSurfaces (drawables, backing stores, scanout) are linear.

---------------------------------------------------------------------------------------------------------------------------------

## 3. Theoretical ceiling and realistic expectations

Hardware (Arm public per-core rates for Mali-G77, not measured here [I]): 2 pixels/clk, 4 bilinear texels/clk, 32 FP32 FMA/clk per
core; 9 cores (present mask 0x660067 [V-code]); 4 L2 slices [V-code dump layout]. LPDDR4X 4x16 bit at 3733 MT/s = 29.9 GB/s peak,
~18-21 GB/s practical [I].

| | 654 MHz | 836 MHz | 886 MHz |
|---|---|---|---|
| pixel fill | 11.8 Gpix/s | 15.0 | 15.9 |
| bilinear texels | 23.5 Gtex/s | 30.1 | 31.9 |
| FP32 | 377 GFLOPS | 481 | 510 |

What the workloads need at 90 Hz, from measured overdraw (1125x2436 = 2.74 Mpx per frame; iOS renders at iPhone X size):

| scene | px shaded per frame (phase mean overdraw x 2.74 Mpx) | at 90 fps | share of 654 MHz fill peak | DRAM measured in active windows |
|---|---|---|---|---|
| home / App Library | 8.9 Mpx | 0.80 Gpix/s | 7 % | 4.0 GB/s |
| Safari | 23.7 Mpx | 2.13 Gpix/s | 18 % | 3.8 GB/s |
| Photos / switcher | 20.8 Mpx (switcher windows up to ~70) | 1.87 Gpix/s (up to ~6) | 16 % (up to ~50 %) | 5.0 GB/s |
| Maps (before msaartt) | 29.7 Mpx | 2.68 Gpix/s | 23 % | 4.6 GB/s (at ~47 fps) |

So the average fill/bandwidth ceiling is 2-14x above need (the switcher's heaviest windows are the exception); the binding
constraint is the **cost per quad** (4.6-8.7 core-cycles measured vs ~2 for a simple shader at peak), which comes from DRAM latency
(cold L2 per job, linear surfaces), dependent constant-buffer loads and per-job overheads -- all addressable in software. GPU time
at 654 MHz in active windows: 3.9-4.4 ms per 11.2 ms refresh for home scroll/Safari/Maps (Maps ~8 ms per shown frame incl. the MSAA
store/resolve that msaartt removes), 6.2 ms (p90 7.2) in Photos/switcher. If cycles per quad fall by ~30 % (items 3, 6, 7) and the
clock runs at 836-886 when hot work arrives (0631/0633), those land near 2.5-3 ms and ~4.5 ms per refresh [I] -- inside a 90 Hz
budget with margin, so frame pacing then depends on CPU (CA commit, app) and scheduling, which items 4/8 address.

Sustained ceiling: in perf1a the battery went 40.0 -> 44.9 C during Maps/Magnifier/Safari/switcher (226-468 s, 1.2 C/min) and hit the
level-3 cap (GPU 512 MHz) at ~45.5 C about 4.5 min after the heavy segment began. At 512 MHz,
GPU time per frame scales up by 654/512 = 1.28x at worst-case linear scaling (less for the latency-bound part), which pushes the
switcher (6.2 ms -> ~7.5-8 ms per refresh) close to the 90 Hz limit. The sustained target is therefore set by
power: items 5 (voltage, power gating), 3 (CPU time in the bottom half), S5 (idle loads) and 0643 (SoC sensor ladder) decide
whether long sessions stay at 654-836 MHz.

Camera / Magnifier: Android does preview scaling, rotation and colour conversion in the ISP/MDP hardware; here the GPU runs our ISP
(8.9 passes per picture) and VideoToolbox's compute converter. Even after camprev1 (4x fewer pixels) expect ~16-20 ms of GPU per
camera frame at 654 MHz -> a 30 fps preview costs ~50-60 % of the GPU. Parity in this one place requires moving work off the GPU
(S2), not driver tuning.

---------------------------------------------------------------------------------------------------------------------------------

## 4. Catalogue by layer

Each item: **what** / **evidence** / **gain** / **cost-risk** / **interactions** / **who** / **host test** / **phone: what to read**.
"In flight" items are listed with what is still missing about them.

### 4.1 Clocks, voltage, power gating, thermal, memory system

**C1 -- Clock acting (0631 fast ramp, 0633 886 MHz) -- IN FLIGHT (q10jall3f / q10jfinal1f).**
Evidence: perf1a ran at 654 MHz while 72 of 370 windows had busy max >= 90 % [V-log]; vendor GED jumps to the top OPP at >= 80 %
loading [V-code]. Gain: <= +28 % (836/654) on GPU-bound phases (Magnifier, Maps peaks), <= +35 % at 886; real gain lower because
41-90 % of fragment-active cycles wait for texel data (fact 2) -- estimate +10-20 % per frame on latency-bound UI passes, close to
linear on ALU/compute-heavy ISP passes [I]. Risk: the first live clock switch ever on this hardware; more heat earlier (each switch
holds job starts until the slots drain -- with 0630 NEXT + 0632 overlap the drain is longer). Missing: (a) the 886 step only at
thermal level 0 is right; (b) the fast profile ignores memory-boundness -- once T1 gives `starve%`, the governor should not chase
886 for latency-bound loads (GED does not either, so this is beyond parity, optional). Phone: `NORD2 GDVFS switch ... measures
~836000/886000`, GDVFS2 ms_at shares, no `GDVFS FALLBACK`, battery slope vs perf1a (1.2 C/min in the 226-468 s segment: 40.0 -> 44.9 C).

**C2 -- Per-OPP vendor voltages (NEW, high value for sustained performance).**
What: run each OPP at the vendor signed-off (PTPOD-disabled) voltage instead of 750 mV for everything: 350 MHz @ 550 mV,
512 @ 600, 654 @ 643.75, 805 @ 731.25, 836 @ 750 (VSRAM 750), 886 @ 800/800 (0633). Evidence: vendor table
`gpufreq/v1/include/mt6893/mtk_gpufreq_internal_plat.h` lines 348-388 [V-code]; boot rails read 750/750 mV, writes=0 [V-log
gpusched notes]; 0633's `n2rail_set` already performs vendor-ordered, read-back-verified live VGPU/VSRAM changes but is limited
to VGPU 650..800 mV and used only for 886 [V-code]. Gain: dynamic power at 654 MHz x(643.75/750)^2 = 0.74 (-26 %), at 512 MHz
x0.64 (-36 %); leakage falls faster than linearly with V [I]. Battery heating in GPU-heavy segments should drop accordingly; with
the thermal ladder this buys time at 654-836 instead of 512 -- in perf1a the 50 % of time spent at levels 2-3 would shrink
[I, magnitude needs a capture]. Cost/risk: undervolt instability if this silicon needs more than the sign-off table (it is the
conservative non-PTPOD table, so unlikely) -> start with +25 mV margin, then +12.5; a GPU fault within 500 ms of a change already
triggers the existing fallback (back to cold-start OPP) -- extend the fallback to "back to 750 mV for the boot". Rail ops go
through the input thread (PWRAP owner); for OPPs <= 836 only VGPU (MT6315 over SPMI/PMIF) changes, so VSRAM/PWRAP is untouched;
keep switches rare (>= 100 ms hysteresis) to not disturb 0620's 125 Hz touch loop on the same thread. Interactions: needs 0633;
pairs with C1 (clock up = V up first, the 0633 order); with C3 (power-off loses nothing: rails restore on power-up). Who: kernel GPU
agent (0633 lineage). Host test: extend `rail_set_test.c` to all six OPPs (order, BUCK_DIFF, settle sums), a model of
"fault -> 750 mV fallback"; Phone: `NORD2 GDVFS VOLT` lines per OPP, GPU JOBS fault = 0 for 30 min incl. Subway Surfers, battery
slope and thermal-level shares vs perf1a.

**C3 -- Shader-core power gating when idle, GPU power-down after long idle (NEW).**
What: SHADER_PWROFF after ~1 ms without queued/running jobs, SHADER_PWRON before the next start (the CI member already exports
`n2hw_power_on_cores/n2hw_power_off_cores` [V-code]); later, MTCMOS+buck off after >= 100-500 ms idle (screen static, standby).
Evidence: cores powered from n2c_start to n2c_stop, never in between [V-code]; idle home: GPU HW busy 1.4 %, 23 jobs/s [V-log];
kbase: shader cores off after 2 x 400 us ticks, platform power_off cuts CG, MTCMOS and BUCK [V-code vendor]. Gain: removes idle
leakage of 9 cores at 750 mV (tens to a few hundred mW at 40-45 C [I]) during the ~94-98 % of UI time the GPU is idle; cooler
start into heavy phases. Risk: first job after idle pays the power-up (cores: tens of us, OK; full power-down: ms -> must not be
used during animations: gate it on "no present for 500 ms"). Interactions: PRFCNT sampler already drops a window across a power
cycle; 0500 AS state (unbind on unpowered exists); 0630 NEXT must be empty before power-off; C2 voltage restore on power-up.
Who: kernel GPU agent. Host: extend the 0630/0632 core model with a power state (no start without cores, no power-off with
current/NEXT chains). Phone: residency counters (T1), first-job latency after idle (LAT q/run of the first job), standby battery.

**C4 -- Lower GPU floor at low load once C2 exists.** Today the floor is the cold-start 654 MHz [V-code 0631 notes]. At fixed 750 mV
a higher clock and racing to idle is fine; with per-OPP voltage, GED-like down-steps (350-512 at < 15 % loading) save power in
light UI. Gain: small per frame, meaningful for standby/idle heat. Do after C2.

**C5 -- Thermal ladder from SoC sensors (0643, IN FLIGHT in q10jfinal1).** Missing/notes: 0643's first LVTS register writes are in
the same image as the first live clock switch (1f) and the first LL 2000 MHz step -- test q10jfinal1 (decisions-only GPU) first so
a hang can be attributed. Keep battery 46 C as the hard stop (battery safety).

**C6 -- DRAM / EMI / DVFSRC.** DRAM fixed at 3733 Mbps, DVFSRC disabled [V-log, cpu agent]. 4266 = +14 % peak bandwidth that is not
needed (4.7 GB/s used of 30) and maybe -5-8 % latency [I]; a wrong step hangs the SoC with no log. **Do not do now.** Note the other
side: DRAM never scales down either, so its idle power is part of the heat budget (Android drops to 1600-1866 at idle) -- a future
power item, not a GPU-speed item.

**C7 -- CCI / DSU (0641, IN FLIGHT).** Affects CPU-side frame preparation only: the Mali here is non-coherent (COHERENCY_NONE
[V-code]) and reaches EMI through its own ports, so CCI frequency does not change GPU memory latency [I from topology].

**C8 -- Memory attributes, LLC/SLC.** AS_MEMATTR = kbase's six indices (0x88/0x8f/0x8d/0x88/0x8d/0x4c), BOs mapped IMPL_DEF inner
shareable, page-table walks write-back + read-allocate [V-code]: parity, nothing to gain. MT6893 has no GPU-usable system cache
(the vendor `slbc` driver is the multimedia SRAM) [I]. Nothing to do.

**C9 -- 2 MiB GPU pages.** Mapping is 4 KiB only (level-3 entries; block entries never written) [V-code]. A 1125-px RGBA row is 4.5 KB,
so 2-D sampling of a linear surface touches a new 4 KiB page per row; MMU walks are cached in the L2 (R_ALLOCATE) but the L2 is
invalidated per job (fact 8). Gain unknown -> **measure first** (T1 adds MMU_REQUESTS). Cost: needs physically contiguous 2 MiB
chunks from XNU (a reserved pool); medium-high effort. Rank low until the counter says otherwise.

**C10 -- EMI QoS / bandwidth limiter for the GPU master.** Read stalls 0, write stalls 5-7 % only in MSAA/ISP phases [V-log]. Not a
limiter; leave the preloader settings.

**C11 -- TLB/ASID/AS eviction.** 0500 dynamic AS: 2 evictions, 4 binds, 0 waits in perf1a [V-log]. Nothing to do; re-check once
0632 keeps more jobs in flight per context.

### 4.2 Kernel scheduler, submission path, kernel memory management

**K1 -- JS NEXT pipelining (0630) + precise vertex/tiler over fragment overlap (0632 + gpupipe1) -- IN FLIGHT.**
What is still missing: (a) compositor jobs are never precise (> 32 buffers, see K3), so backboardd gets neither precise maintenance
nor overlap; (b) the overlap only helps when consecutive passes do not conflict -- blur chains (pass k+1 samples what pass k wrote)
stay serial, which is correct; (c) together, 0630+0632 turn Maps' c/f/c/f chain into a stream: JS1 runs tiler jobs up to 4
fragments ahead and JS0 runs fragments back to back from NEXT (the "same" rule) -- so the expected gain in Maps is larger than the
0630 author's "small" estimate once gpupipe1 sets are precise [I]: up to the ~18 % turnaround tax of fact 4. Phone: LAT3
`next_same`, `overlap`, `held`, `promoted`; `np pipe: v3=`; Maps jobs/s and HW busy up, drv-hw gap down.

**K2 -- Bottom half and interrupt placement (NEW).**
What: route the GPU SPI (GICD IROUTER) to a big core instead of the PE that ran irqSetup at boot, bind/hint the bottom-half thread
(BASEPRI_PREEMPT 92, unbound today) to the same cluster; keep everything else. Evidence: irq 9-20 us avg but max 2-5 ms, handoff
45-157 us avg, BH wake max up to 10 ms [V-log LAT/LAT2]; IRQ routed with `nord2_gic_irouter_for_mpidr(MPIDR_EL1)` of the configuring
CPU [V-code]; which CPU that is at boot is not logged [I: likely a little core]. Gain: handoff -30-60 % on a 2.6-3.0 GHz A78 vs a
1.8-2.0 GHz A55 [I]; at 2323 jobs/s that is up to ~5-8 % of wall time in Maps-like phases before 0630/0632 hide part of it.
Risk: low (one GIC register; IRQ must follow CPU hotplug/idle -- the big cores here never go offline [I]). Who: kernel GPU agent.
Phone: LAT irq_us / hand_us avg and max per phase; T1 adds the IRQ CPU.

**K3 -- Cache maintenance and big-buffer work off the critical path (NEW, the largest stall source).**
(a) Camera/ISP surfaces: ~87 x 48 MiB cleans per second in Magnifier [V-log]. The IOSurface lock protocol exists (acquire =
invalidate, unlock without read-only = dirty) [V-code surfaceHook], so either the pipeline unlocks read-write after reading
(CVPixelBufferLockBaseAddress(pb, 0) is the common Apple idiom), or the ISP's own buffers are CPU-visible without explicit tracking
(cleaned before every job by rule). Fixes, cheapest first: make the ISP's intermediates GPU-only (n2ext "GPU-only" allocation
already exists); for imported surfaces, decide "dirty" from the CPU page modified state at unlock (XNU pmap ref/mod emulation)
instead of the lock flag [I feasibility]; log the reason of every clean > 4 MiB (T1). (b) Compositor jobs: raise N2C_JOB_BOS 32 ->
128 with a v4 submit (handles array + 128-bit masks) so backboardd's jobs are listed, cleaned/invalidated per buffer and eligible for
0632; today every one of them walks the whole context's 8192-slot table twice and invalidates every GPU-writable buffer (137 us/job
at idle, more under load) [V-log full=n, V-code]. (c) Zero-fill, page-table population and cache maintenance of new/imported buffers
happen under the global driver lock (lock hold up to 8.8-11.6 ms, waits 14.9 ms) [V-log LAT2]; prepare them outside the lock and
publish under it; clean at submit time in the submitting thread (as kbase leaves it to the userland sync ioctl) instead of in
try_start in the bottom half. Gain: removes the multi-ms GPU idle bubbles and frame spikes in Maps/Magnifier/camera; -0.2 s/s of BH
CPU time in Magnifier (less heat); compositor jobs ~-100 us each + overlap eligibility. Risk: (a)/(c) a missed CPU write = stale
pixels: keep conservative defaults (imported + untracked = clean), add a sysctl to fall back; (b) ABI change: v3/v2 kept, library
picks v4 only when the kernel accepts it (gpupipe1 pattern). Who: kernel GPU agent (a, b, c), userland agent (v4 lists, GPU-only ISP
buffers, read-only locks where we control them), camera agent (ISP buffers). Host: core model test with 128-handle jobs; a
maintenance-accounting test (every CPU-written buffer cleaned exactly once before the GPU reads it). Phone: JOBS clean_us/inv_us
max < 0.5 ms, clean_KiB per window in Magnifier < 1 GiB, LAT2 hold_max < 1 ms, full jobs at idle 0.

**K4 -- Compositor-first scheduling (NEW).**
What: a priority class per context set at open (backboardd = high; foreground app = normal; mediaserverd/VideoToolbox/background =
low; by process name, the kernel already knows the owner), the pick in try_start takes the highest class first with aging
(e.g. a low job older than 50 ms is promoted), a high-class job may take back a not-yet-started lower-class chain from NEXT (0630
already has `n2hw_js_evict_next`), and the 128-record job table keeps >= 16 records for the compositor (per-context window 96 ->
64 for others). Evidence: pick = lowest job id across contexts [V-code]; mean queue wait per job 1.7-2.3 ms in Maps/Photos/Safari,
max 27-30 ms, 41.6 ms avg in Magnifier where "every client (also backboardd) waits behind the running jobs" [V-log]; Maps reached
its 96-job window (depth max 96) [V-log]; on KIO_BUSY the library waits for ALL its submitted work before retrying [V-code
nord2_pan.c] -- no BUSY seen in perf1a (JOBS2 waits=0) but nothing prevents an app from leaving the compositor < 32 records. Gain:
the compositor's frame stops queuing behind app work -> fewer drops where the swap was not ready in time ('nosw' also counts
GPU-late compositor frames); bounded by the running job's length (hence K5). Android parity: kbase has per-context priorities and
50 ms time slices [V-code defaults]. Risk: low; starvation handled by aging. Interactions: 0630 NEXT (eviction path), 0632 (overtake
rules stay per context), C1 (clock policy should look at the compositor's queue). Who: kernel GPU agent. Host: extend
core_test_0632.c with classes (no high job waits behind a not-started low job; low jobs finish within the aging bound).
Phone: T1 per-context queue wait (backboardd p95 < 4 ms under Maps/Magnifier), JANK 'n' events per window.

**K5 -- Bounded job length: chunked compute dispatches (NEW, userland side of K4).**
What: the library splits a dispatch whose estimated cost exceeds ~2-4 ms into several compute jobs over workgroup ranges (base
workgroup id as a sysval added in NIR -- Mesa's nir_lower_compute_system_values supports a base workgroup id). Evidence: Magnifier
compute jobs 36.6 ms each, no preemption [V-log, V-code]; kbase would only soft-stop after 100 ms ticks [V-code defaults], so
chunking is the realistic tool. Gain: the compositor waits at most one chunk. Cost: ~100 us handoff per extra chunk (hidden by
0630 NEXT for same-slot chunks). Risk: medium -- kernels that use workgroup_id/global id must all go through the lowering; atomics
and threadgroup memory unaffected. Who: userland agent. Host: softpipe numeric tests of chunked vs whole dispatches (ISP, VT-like
kernels). Phone: Magnifier `job run max` per window < 5 ms, backboardd queue wait.

**K6 -- Compute on JS2 (experiment only).** JS2 exists on G77 (3 slots [I]), neither kbase (single core group) nor panfrost uses it
[V-code]. Concurrent compute + vertex/tiler would help Magnifier, but the path is not validated by Arm's driver on this silicon:
behind a sysctl, after K4/K5 if they are not enough. Rank low.

**K7 -- kbase-parity L2 flush policy (NEW).** See exec item 6. Specifics: JS_CONFIG_ENABLE_FLUSH_REDUCTION + JS_FLUSH_ID_NEXT =
LATEST_FLUSH sampled at submit (kbase_jd: `katom->flush_id = latest_flush`); START = FLUSH_INV_SHADER_OTHER (invalidates the
per-core L1/texture caches, keeps the L2; a tTRx feature) when the previous chain on the slot was the same context and no listed
buffer was written by the CPU or another master (imports) since the last L2 invalidate -- the kernel has the per-buffer
`cpu_dirty`/imported state to decide; NO_ACTION only where the access sets prove no GPU write->read hazard either (kbase leaves that
call to the DDK's SKIP_CACHE_START flag); END = NO_ACTION when
the chain writes no CPU-visible/imported/scanout buffer (keep CLEAN otherwise; CLEAN_ONLY_SAFE is a tTRx feature). tTRx feature list
in the vendor r32p1: FLUSH_REDUCTION, CLEAN_ONLY_SAFE, FLUSH_INV_SHADER_OTHER all present [V-code]. Today: START
CLEAN_INVALIDATE + END CLEAN on every chain [V-code n2hw_js_start]. Implementation note: n2hw_js_start lives in the CI member
nord2_gpu_hw.o; write the start registers from nord2_gpu_core.c as 0630 did for its NEXT helpers. Gain [I]: shorter jobs (no
start flush wait), warm L2 across passes (a 1/4-screen blur intermediate is 0.7 MB; L2 size not verified -- 4 slices, likely >= 1 MiB [I]; tile
lists, descriptors) ->
5-15 % fragment time in multi-pass scenes. Risk: stale data on a missed foreign write -> visual corruption only the user sees;
mitigations: conservative "dirty epoch" rule, sysctl kill switch, T1 counters for flush types, and a GPU-reads-CPU-write probe test
in the next capture. Needs T1 first to see the effect.

**K8 -- Submission back-pressure.** On KIO_BUSY the library sleeps on its last submitted seqno [V-code]: a drain of its own queue,
not just a free slot. Make it wait for the oldest outstanding job instead. Small, low-risk userland change; matters once K4 caps
windows.

**K9 -- Kernel zero-fill of new buffers.** 0400 zero-skip is off (LAT2 `vfail=1`) [V-log]; Maps made ~1.1 GB/s of fresh zeroed,
walked memory [V-log perf notes]. bigpool1/rtpool/pools (in flight) remove most of it; what remains is the first allocation of each
size. After gfxall2, check `NORD2 GPU MAP` rate; if still > 10 maps/s of > 8 MiB, revisit zero-skip for pool-recycled pages.

**K10 -- Tiler heap.** heap=6/192 MiB pre-sized, fallback=1-4 [V-log]; gpupipe1 adds a 2nd 32 MB heap after 240 passes. No
grow-on-fault needed; watch `heap` and `fallback` in MEM GPU after gpupipe1.

**K11 -- Fences/syncfile, timeouts.** FENCE lines: hits ~= records, waits 0 [V-log]; nothing to gain. Soft/hard-stop timeouts only
matter for hangs.

### 4.3 Userland driver (Nord2Metal + libredwoodair + Panfrost backend)

**U0 -- IN FLIGHT: webxr1, gfx100a (rtcopy, astcedge), bigpool1, gpupipe1, gfx200 (msaartt, zsdc, rtpool, pools) = gfxall1/2;
gfxall3 (shader file cache, per-draw state cache, MapKit upload batching, set*Bytes ring; no async compile, no warm-up).** What is
still missing about them:
(a) a combined memory budget (S4); (b) msaartt needs Mesa's batch access flags to list the resolve texture as written for
gpupipe1's sets -- expected through the cbuf path, verify in the capture (no `cannot be drawn as multisample render-to-texture`,
no Maps corruption) [I]; (c) rtcopy's CPU write-back at completion requires the END clean of chains that write a CPU-visible copy --
keep that rule when K7 lands; (d) gfxall3's statecache keeps the framebuffer bound across draws (big CPU win per draw, U7) -- any
glitch that disappears with gfxall3-nostate is it; ship gfxall3 one step after gfxall2 so the two can be told apart.

**U1 -- Push constants for every CPU-mapped constant buffer (NEW).** Evidence and bounds in 2.4. What: `pushable_ubos` = all UBOs
whose binding has a CPU mapping (all Metal buffers here are mapped for their lifetime [V-code air_metal.c comment]); air_metal.c
sets `user_buffer = b->map` for every constant-buffer slot (today slot 0 only); Panfrost's `panfrost_emit_const_buf` already copies
pushed words from the user buffer at draw time; Nord2Metal records Metal commands and replays them at -commit [V-code], so the copy
happens after the app's CPU writes, matching Metal semantics. Limit: PAN_MAX_PUSH = 128 words per stage. Host proof of the codegen
side: with `pushable_ubos = ~0` Mesa removes every LD_BUFFER from the blur chain (table in 2.4) [V-host]. Gain: between 1.0x and
~1.9x (Mesa model, real recompile) on CA's blur/downsample/effect passes; realistic 10-30 % on those passes and a few % on plain
composition [I].
Risk: a constant buffer written by the GPU earlier in the same command buffer (rare in CA; detect by "buffer written by an earlier
pass of this CB" and fall back to LD_BUFFER for that draw); reads of write-combined buffers on the CPU are slow (128 words max, OK).
Who: userland agent. Host: shaderstats.py before/after (LD_BUFFER count -> 0 for CA blur, bound -> t), softpipe numeric tests,
blur_nir/blur_chain host tests unchanged. Phone: T1's LS_MEM_READ and EXEC_STARVE, cycles per quad in Control Center/switcher
phases with an A/B switch file.

**U2 -- Occupancy.** 5 of 93 CA shaders use > 32 registers (half the warps in flight) incl. downsample_8 and variable_blur
[V-host]; with latency-bound texturing, occupancy matters. Try Mesa's pressure-aware scheduling for those; small, low risk; host
check = `threads` column back to 2.

**U3 -- fp16.** CA ships _lpf and _lph variants; the half variants have ~10-25 % fewer instructions but the same bound [V-host].
Which one backboardd picks was not visible in perf1a (its early lines are lost). Low priority until U1 removes the LS bound.

**U4 -- Pipeline compile hitches: gfxall3 file cache (IN FLIGHT; no async, no warm-up) + a read-only cache for sandboxed apps (NEW).** perf1a: 227 pipelines x ~5 ms
first use [V-log notes]; the first App Library interaction windows (t=42-52 s) ran at 53-60 fps with long present gaps while the GPU
was 0.3-1.9 % busy (one window CPU 43 %, max core 70 %) [V-log] -- CPU first-use costs, not GPU. gfxall3 (ready since ~23:30, rehearsed) adds a file cache in /var/db/nord2-metal/cache keyed like Mesa
(NIR SHA-1 + variant key) -- but by its own notes sandboxed apps get mode 0/1 (cannot write, maybe not read /var/db), there is no
asynchronous creation and no warm-up: it helps root daemons (backboardd, SpringBoard) from their second launch, not Maps/Safari/third
party apps. Missing piece: a READ-ONLY cache in a location every process can read (inside the Nord2Metal bundle, which every Metal
process already maps), seeded either by copying the daemon-written entries into the bundle at the next payload, or generated on the
host -- the host harness runs the same translator and Mesa, but must go through panfrost_shader_get's key computation to produce
matching keys. Low risk (a key mismatch is a miss).

**U5 -- AFBC.** Private non-MSAA textures already AFBC [gfx200 notes]. IOSurfaces (app drawables, CA backing stores, the scanout
surface) are linear and carry most tile write-back. AFBC there needs every consumer to decode: CPU locks (decompress blit), the
overlay (whether MT6893 OVL decodes AFBC was not verified), screenshots. Gain mainly DRAM energy (~0.15-0.25 W at Maps-level
traffic [I]) and some latency. **Not now**; revisit after the higher items, starting with backboardd-internal surfaces.

**U6 -- Render-pass shape, load/store, transient attachments.** zsdc (depth/stencil DontCare) and msaartt are in flight; render-pass
merging exists for same-target draws [V-code notes]; Maps' ~33 passes/frame are distinct targets. Memoryless attachments map to
"never stored" (zsdc) on a tiler -- covered. Nothing extra.

**U7 -- Per-draw CPU cost.** ~30-45 us per draw (driver's own estimate), backboardd 25 draws/CB at 1.1 ms per commit [V-log notes].
gfxall3 state diffing addresses it; a native driver is ~2-5 us/draw [I]. For 90 Hz the compositor's 1.1 ms is acceptable; games
with many draws gain more.

**U8 -- MapKit buffer->texture copy batching (gfxall3, IN FLIGHT)** -- 120-346 blits per command buffer in snapshots [V-log notes].

**U9 -- Compute workgroup heuristics.** App-chosen sizes (VT: 16x16 groups) are fine for Valhall's 16-wide warps; nothing to do.

**U10 -- Texture formats.** ASTC native (astcedge fixed the edge); linear IOSurfaces sampled 1:1 are cheap; scaled/blurred samples of
linear surfaces are the costly case (fact 2) -- partially helped by K7 (warm L2).

**U11 -- CAMetalLayer drawable pool / present path.** Drawables, pool size and present timing are Apple's QuartzCore; the port's part
is completion latency (fence queue -> completed handlers). FB: 99-100 % overlay frames, TE-locked, 0 underruns [V-log]. Nothing big
to gain; keep completion handlers off slow paths (the y2s85 note already moved probes out).

### 4.4 System level

**S1 -- CoreAnimation render server load and blur.** CA already blurs by downsample (4/8) + separable narrow blurs (7-27 taps) at
reduced size [V-host function list]; the "cheap blur not implemented" item is better replaced by making those passes cheaper (U1,
K7) than by substituting Apple's shaders (visual parity risk). backboardd's frame cost at home is 1.5 full-screen write-backs per
frame [V-log] -- low.

**S2 -- Magnifier / camera.** camprev1 (IN FLIGHT, q10jcam1 package, in every q10jall*/q10jfinal1 image) halves the VDO size ->
~4x fewer ISP/VT pixels. Still missing: (a) the ISP's 8.9 passes per picture could be fused (camera agent); (b) K3a (no per-pass
48/12 MiB cleans); (c) K4/K5 so the preview cannot starve the compositor; (d) long term: preview scaling/rotation/colour
conversion on the MDP like Android (large effort, true parity).

**S3 -- WebKit GPU process / Safari.** webxr1 fixes black pages; Safari phase is already 80-89 fps with ~4.4 ms GPU per refresh
(median of active windows, p90 7.4 ms) [V-log]; its risk is memory (avail down to 708 MiB, pageins 20k/2 s, one memory exception) -> S4.

**S4 -- Memory budget (NEW, interaction guard).** Wired 2.9-3.3 GiB of 5088 MiB; MEMGOV had "no victim" at 25-38k free pages in
Safari [V-log]. The new pools add, per GPU process: bigpool1 <= 100 MiB, rtpool <= 256 MiB, 2nd tiler heap 32 MiB, plus buffer/texture
pools in every process (gfx200 'pools') [V-code notes]. Give them one per-process budget that shrinks for background processes and a
dispatch memory-pressure source that empties them (bigpool1 has np_pressure; rtpool/pools only empty on an allocation refusal or
1.5 s idle). Who: userland agent. Phone: MEMGOV lines, ReportMemoryException count, `held=MiB` in GFX200/bigpool lines.

**S5 -- Background GPU load and respawn loops.** 2.56 launches/s (lskdd, bluetoothd, fairplayd, wifip2pd) [V-log] -- cheap power
win (block-respawn1 exists, not applied). The unidentified ~80 passes/s at idle after the session (2.2) -- name it with T1 and stop
it if it is a leftover (camera session, widget).

**S6 -- Display path.** Overlay + IOMMU, CPU-free, 99-100 % HW frames, tear 0-0.2/1k, panel 89.1 Hz, TE-locked presents [V-log].
Nothing to gain for GPU speed. Touch: 0620 (54 -> ~125 Hz, IN FLIGHT) is the biggest perceived-smoothness item in this layer;
verify the HID timestamps are taken at sample time so UIKit's prediction uses the new rate [I, not checked].

**S7 -- CPU feeding the GPU.** 0641 CCI/LL, 0642 prime-first, 0643 ladder (IN FLIGHT). Note for GPU: the BH runs on whichever CPU;
K2 puts it on a big core. Remaining CPU-side GPU costs are the driver's own (K3c, U4, U7).

**S8 -- Thermal/power budget.** Biggest sustained loss [V-log]. Items that cut heat without cutting speed: C2, C3, K3 (CPU in BH),
S5, and later DRAM DVFS (C6, power only).

### 4.5 Measurement gaps and the instrumentation patch (T1)

What cannot be ranked today: per-process GPU time, why fragment work stalls (JS wait reasons), tiler/geometry load, early-Z/FPK
efficiency, blend-shader use, LS load and texture L2 misses, MMU walks, flush cost, power-state residency.

**T1a -- PRFCNT lines F/G/H** (Nord2GPU::prfcntSample; the dump is already summed per block -- only the print changes; short lines
because the console cuts long ones). Indices = Mesa G77.xml offsets [V-code]:

```
NORD2 PRFCNT F js0j=jm[8] js0t=jm[9] js0wr=jm[12] js0wi=jm[13] js0wd=jm[14] js0wf=jm[15] js1j=jm[16] js1wr=jm[20] js1wi=jm[21] js1wd=jm[22] js1wf=jm[23] js2=jm[26]
NORD2 PRFCNT G tri=ti[6] vis=ti[11] cull=ti[12] clip=ti[13] tbrd=ti[17] tbwr=ti[19] mmu=ms[4] rdstall=ms[17] xrd=ms[29] xwr=ms[42] arq1=ms[34] arq2=ms[35] arq3=ms[36]
NORD2 PRFCNT H ezt=sh[12] ezk=sh[14] lzt=sh[15] lzk=sh[16] r64=sh[17] te=sh[19] fpkk=sh[20] comp=sh[22] cstarv=sh[25] astarv=sh[33] blsh=sh[34] icm=sh[32]
NORD2 PRFCNT I lsrf=sh[44] lsrs=sh[45] lsw=sh[46] rdlsc=sh[56] rdlscx=sh[57] rdtexx=sh[59] rdftcx=sh[55] v32=sh[50] v16=sh[51]
```
Also: re-arming after 6 samples loses every 7th window (258 of 362 windows had counters in perf1a) -- rewrite BASE per sample is
already done, so the re-arm can go (or happen only on a fault). `gpuprof.py` already parses F..I lines (adds: JS wait shares,
tiler cull rate, EZS kill rate, transaction-elimination share, blend-shader calls, texture L2 miss = rdtexx/rdtex, MMU walks per
quad) -- extend `derive()` when the patch lands.

**T1b -- Per-context GPU time** (core: accumulate run_us and queue_us per context at completion; Nord2GPU prints every 2 s the top 4
by run time with the owner's process name): `NORD2 GPU CTX: backboardd run=12.3ms q=0.4ms/p95 1.2 jobs=180 | Maps run=...`.
This names the idle-hot client and gives K4's pass/fail number.

**T1c -- Job-size histogram and flush/maintenance reasons**: per slot, counts of run_us in <100/<250/<500/<1000/<4000/<16000/>=16000
us; counters of START/END flush types issued (after K7); clean/invalidate bytes by reason (cpu_dirty via unlock, untracked
cpu_visible, zero-fill, full list) and the biggest single clean per window.

**T1d -- Power residency**: ms with cores powered, ms per OPP and per VGPU voltage (GDVFS2 has ms_at per OPP), first-job-after-idle
latency.

**T1e -- Frame-level compositor GPU latency**: for backboardd, time from the first job of a command buffer to the last completion,
p50/p95 per window, printed next to FB PACE.

Cost: log-only; one IOLog line per item per 2 s window; no behaviour change. Who: kernel GPU agent; this directory's gpuprof.py
for analysis (n2perf.py stays as is; gpuprof.py can be called on the same log).

---------------------------------------------------------------------------------------------------------------------------------

## 5. Interactions of the in-flight changes (what can go wrong together)

| combination | risk | why | guard |
|---|---|---|---|
| 0632 overlap + gpupipe1 sets + msaartt | corruption if the resolve texture is missing from the fragment chain's write set | msaartt renders into the single-sample resolve texture through a 4-sample pipe_surface; gpupipe1 builds sets from Mesa's batch flags | expected listed via the cbuf path [I]; switch files no-gpupipe / no-gfx200-msaartt (boot line is full: file switches only) |
| 0632 precise maintenance + rtcopy | CPU reads a stale copy | rtcopy copies GPU output back on the CPU at completion; precise jobs invalidate only touched buffers | the copy is the render target, so it is in the set [I]; K7 must keep END clean for CPU-visible outputs |
| rtpool + 0632 conflict checks | none expected | conflicts are by buffer identity; a reused texture keeps its BO | -- |
| bigpool1 + rtpool + pools + 2nd tiler heap | memory pressure -> jetsam, ReportMemoryException, app relaunch stutter | up to ~390 MiB more held per busy GPU process; Safari already at 708 MiB available | S4; watch MEMGOV |
| 0631/0633 live clock + 0632/0630 | longer clock-switch stalls | a switch holds starts until the slots drain; NEXT + overlap keep more in flight | GDVFS2 switches count should stay low (fast-ramp holds 500 ms) |
| 0633 rail ops + 0620 125 Hz touch | touch jitter if rail ops become frequent | both run on the input thread (PWRAP owner) | keep voltage changes rare (C2 hysteresis); VGPU-only steps could move to the SPMI channel on the GPU worker |
| q10jfinal1f = first live clock + first LVTS writes + first LL 2000 step | a hang is not attributable | three firsts in one boot | test q10jfinal1 (decisions-only) first, then 1f |
| K7 (L2 policy) + camera/VDEC imports | stale input if a foreign master's write is not seen | imports are written by ISP/VDEC/CPU outside the GPU | imported buffers always force START invalidate; kill switch |
| K4 priorities + 0632 overtake | priority inversion inside a context | overtake is per context; priority is across contexts | keep both rules independent; host model test |
| C3 power gating + PRFCNT sampler | lost windows | the sampler drops a window across a power cycle [V-code] | count residency in T1d |
| webxr1 (8-bit for XR formats) + CPU writers of 10-bit layouts | tinted pages | a CPU path writing 'w30r' layout into the 'BGRA' surface | user check; switch no-webxr |

---------------------------------------------------------------------------------------------------------------------------------

## 6. Sequencing plan and validation protocol

### 6.1 Order (why: attribution first, low-risk structural fixes next, risky silicon-level changes last)

1. **Step A -- q10jfinal1 (n2gdv=9) + gfxall2** (one txn). Brings 0620/0621 (touch, jank lines), 0630/0632 (with gpupipe1 sets),
   0641/0642/0643, camprev1, webxr1/gfx100a/bigpool1/gfx200. GPU clock still decisions-only, so the delta vs perf1a is software only.
   Capture perf2a (the perf1a 8-step protocol, same order and durations) + 10 min Subway Surfers.
   Fallbacks: corruption in Maps -> VARIANT=gfxall2-nortt; overlap suspect -> gfxall1-nopipe or switch file no-gpupipe; LVTS/LL hang ->
   q10jfinal1c.
2. **Step B -- q10jfinal1f (n2gdv=73738) + same userland**. Capture perf2b, same protocol. Delta B-A = clock/heat effect. Keep 1f
   only if the battery slope does not erase the gain over 10 min (level-3 share must not grow). **Step B2 -- + gfxall3**
   (`VARIANT=gfxall3`, userland only) -> perf2c; delta = shader cache (second launches), statecache (CPU per draw), upload batching.
   If phone time is short, merge B and B2 and use `gfxall3-off` as the in-place A/B.
3. **Step C -- T1 telemetry kernel** (on the chosen A/B base; log-only). Can be folded into the K-step image if the kernel agent is
   faster than the phone queue.
4. **Step D -- one kernel step with K3 (a, b, c) + K4 + K2 (+ K8, K5 in the userland)**: these share the scheduler/maintenance code,
   are low-to-medium risk and each has a sysctl/file switch. Capture with Magnifier emphasis.
5. **Step E -- K7 (L2 policy)** alone, A/B by sysctl in one boot if possible (first half off, second half on), so stale-data artefacts
   can be correlated by the user with the switch moment.
6. **Step F -- U1 push constants (+ U2, U4 prebuilt cache)** -- userland only; A/B with a switch file.
7. **Step G -- C2 per-OPP voltages, then C3 power gating** -- silicon-level; soak test (30 min mixed + 10 min game) before keeping.

Do not combine E or G with anything else new: they are the two changes whose failure looks like "random" artefacts or hangs.

### 6.2 Captures and scenarios

Every step: the perf1a protocol (idle 30 s, home + App Library scroll 60 s, Maps pan/zoom 80 s, Magnifier 40 s, Safari apple.com
scroll 80 s, switcher + Photos 40 s, CC/NC, free use), then Subway Surfers 10 min for heat. Analyse with
`n2perf.py LOG` and `gpu-analysis-20261114/tools/gpuprof.py LOG --phases <phase times from the user's T0 notes>`.

### 6.3 Pass/fail numbers (vs perf1a)

| metric | perf1a | step A/B pass | stretch |
|---|---|---|---|
| touch sampling while finger down | 54 Hz | >= 110 Hz | 125 |
| home/App Library scroll windows active_fps median | 78-86 | >= 86 | 88+ |
| JANK 'n' events per 5 s in scroll windows | (no 0621) | <= 2 | 0-1 |
| Maps fps median / on-time share | 47 / 52 % | >= 60 / 75 % | 80 / 90 % |
| Maps 'SLOW newTexture' per phase | 316 | <= 30 | ~0 |
| Maps GPU job total max per window | 17-20 ms | < 10 ms | < 5 ms |
| Maps GPU busy per shown frame (gpuprof: HW busy / fps, approx.) | ~8 ms | <= 6 ms | <= 4 ms |
| Photos/switcher GPU ms per 11.2 ms refresh (active median / p90) | 6.2 / 7.2 | <= 5 / 6 (with 1f) | <= 3.5 / 4.5 (after E/F) |
| Magnifier UI fps / mean queue wait | 10-14 / 41.6 ms | >= 30 / < 10 ms | 60 / < 3 ms (after D) |
| Magnifier clean_KiB per window | 8.6 GB | < 2 GB (camprev) | < 0.2 GB (after D) |
| Safari fps median | 80-89 | >= 85, page renders (user) | 89 |
| LAT2 lock hold max (median of windows), Maps | 2.5 ms | < 1 ms (after D) | < 0.3 ms |
| idle compositor full-maintenance jobs | 100 % | 0 % (after D) | -- |
| Subway Surfers fps over 10 min | 88.8 median (short run) | >= 60 throughout | >= 85 |
| thermal level-3 share in the protocol | 24 % | <= 24 % with 1f | < 10 % (after G) |
| GPU JOBS fault / AS resets / GDVFS FALLBACK | 0 / 0 / - | 0 / 0 / 0 | -- |

Visual checks only the user can do: Maps edges antialiased and no black frames (msaartt), switcher cards and App Library background
(gfx100a), Safari colours (webxr1), any flicker/garbage after step E (L2 policy) or after F (push constants).

---------------------------------------------------------------------------------------------------------------------------------

## 7. What I would NOT do (and why)

- **DRAM 4266 / DVFSRC now**: +14 % peak bandwidth that is not used (<= 4.7 of 30 GB/s), small latency gain, and a wrong SPM/DRAMC step
  is an instant reset with no log.
- **Soft-stop preemption**: kbase itself soft-stops on 100 ms ticks (too coarse for 36 ms jobs); resume semantics per job type are
  intricate. Chunking (K5) + priorities (K4) give the same benefit with far less risk.
- **JS2 compute by default**: unvalidated by Arm's driver on this configuration -> experiment only (K6).
- **AFBC IOSurfaces now**: every CPU/overlay/screenshot consumer would need a decode path; bandwidth is not the limiter.
- **2 MiB GPU pages before measuring** MMU walks (T1): needs a contiguous allocator for an unknown gain.
- **A home-made replacement for CoreAnimation's blur**: CA already downsamples and uses separable kernels; the cost is codegen/L2,
  and replacing Apple's shaders risks visual differences. Do U1/K7 instead.
- **Always-max clock images ("p" variants) for daily use**: they heat the battery into level 2-3 sooner, lowering sustained clocks;
  fast-ramp (1f) is the right default.
- **Disabling GPU cache maintenance wholesale** or skipping L2 invalidates without the dirty-epoch rule: corruption.
- **Strict priorities without aging** (starvation), **bigger per-context job windows** (latency, table exhaustion), **more pooled
  memory without a global budget** (jetsam).
- **Raising the battery thresholds beyond 0643** without validated SoC sensors and efuse calibration: battery safety.

---------------------------------------------------------------------------------------------------------------------------------

## 8. Appendix

### 8.1 Files

- `GPU_PERF_ANALYSIS.md` -- this document.
- `tools/gpuprof.py` -- per-phase GPU profile (PRFCNT/JOBS/LAT/FB); `perf1a-gpuprof.txt`, `perf1a-gpuprof.csv`.
- `tools/shaderstats.py` -- shader stats/bound of a metallib with the shipping translator; `shaderstats/*.tsv`
  (`quartzcore`, `vectorkit` = shipping harness; `quartzcore-ctl` = push-variant harness without PUSH_ALL, identical numbers;
  `quartzcore-push`, `vectorkit-push` = every constant buffer pushable).
- `tools/build-harness-push.sh` + `pushsrc/` -- builds that harness variant (private copy of y2s99b's build-harness.sh writing only
  into this directory; `pushsrc/` = symlinks to y2s99b merged-src + one edited test file). Rebuild:
  `flock $HOME/nord2-heavy.lock bash tools/build-harness-push.sh pushsrc hpush` (~1 min, small).
- `tools/afbc_model.py` -- AFBC size model; `afbc_model.txt`.
- `gpuprof.py` already derives the T1 metrics (JS0 wait shares, cull rate, early-ZS kill rate, transaction-elimination share,
  blend-shader calls, texture L2 miss share, MMU walks and LS reads per quad) when the F..I lines exist (checked on synthetic lines).

Reproduce: `python3 tools/gpuprof.py ../autocycle/runs/perf1a/ui-usb/xnu-live.log`;
`python3 tools/shaderstats.py ../y2s99b-shaders/host/h-new/metallib_compile_host_test <lib.metallib> shaderstats/<name>`
(QuartzCore = `ui-corpus-20261025/libraries/11c74cc4...metallib`, VectorKit = `96d8a4c6...`); a library runs in 1-5 s, < 20 MB RAM.

### 8.2 Source pointers for the implementers

- Job start config: `nord2_gpu_hw.c` n2hw_js_start (START_FLUSH_CLEAN_INVALIDATE | END_FLUSH_CLEAN | THREAD_PRI(8)) -- CI member;
  0630's own register helpers at the top of `nord2_gpu_core.c`.
- Pick and maintenance: `nord2_gpu_core.c` try_start (oldest id first; clean before start), job_finish (invalidate), n2c_submit
  (N2C_JOB_BOS 32 -> full_maint; precise only without full_maint), limits in `nord2_gpu_core.h` (N2C_MAX_JOBS 128, N2C_CTX_JOBS 96).
- Surfaces: `Nord2GPU.cpp` surfaceHook (lock -> acquire/dirty), import path n2c_bo_track; IRQ setup irqSetup (IROUTER of the
  configuring PE), bottom half bh_main at priority 92; PRFCNT prfcntSample (counter indices in the comment = Mesa G77.xml).
- Rails/OPP: `nord2_gpu_rail.{c,h}` (0633), vendor table `nord2-current-kernel/drivers/gpu/mediatek/gpufreq/v1/include/mt6893/
  mtk_gpufreq_internal_plat.h`; vendor power callbacks `.../mali-r32p1/.../platform/mt6893/mali_kbase_config_mt6893.c`;
  kbase flush policy `.../backend/gpu/mali_kbase_jm_hw.c` lines 225-290, features `mali_base_hwconfig_features.h` (tTRx).
- Push constants: Mesa `src/gallium/drivers/panfrost/pan_shader.c` (pushable_ubos), `pan_cmdstream.c` panfrost_emit_const_buf;
  `bi_opt_push_ubo.c`; library `air_metal.c` bind_stage (slot-0 user_buffer comment near line 5055).
