# ansuz-metal -> Nord2 port ledger (2026-10-26, PRIVATE)

Source: `references/ansuz-metal-20261026/ansuz-metal` (rotten-apple on vidofnir MT6789 / Mali-G57 MC2 v9,
plus ansuz MT6878). Their stack: rottenapple-metal plugin + m2v (metal2vulkan) + PanVK, then a native JM
encoder (`userland/rottenapple-native-metal/nm`). Ours: tegu frontend (gpu/metal/tegu) + our JM bridge
(nord2_metal_bridge.c / n2r nord2_metal_render.c) + our Nord2 kernel series (xnu/patches 0001-0084, P-series
gpu/build/kernel-p1-20261026 0085-0098). Licences: their userland files carry `SPDX-License-Identifier: MIT`
(keep the header when copying); their kernel patches modify/add XNU files (APSL, same family as ours); Mesa
parts MIT. Nothing here leaves this machine.

Our device baseline (C6l + P7, `gpu/build/hardware-20261025/c6l-test`): ui350 analyze: 11.2 cmd/s,
26.6 ms/cmd, 15.8 kernel jobs/cmd, 9.95 full-screen jobs/cmd, 1.67 ms GPU per kernel job (=> ~26 ms GPU per
command: **the GPU time equals the command time; at 350 MHz the UI is GPU/bandwidth-bound**), encode
0.27 ms/draw (ui60 0.11), 59 % plan-memo misses, 94 PSO misses (Mali ~340 ms + AIR ~75 ms each, on the
draw/creation path), present 13 ms (copy 2.7 + xfer 10.3), panel capped 30 Hz (0097), GPU 350/654 MHz images.

Owners: **RL** render lead (lowering-opus: bridge/n2r/batching/pass merging/AFBC/preload), **EC** encode/compile
(encode-cpu-opus: render_pipeline.inc, pipeline_cache*, air_bridge.rs plan cache, submit pipelining),
**CS** compiler speed (compiler-speed-opus: Mesa/translator speed), **TX** textures (texture-opus: formats,
IOSurface import, YCbCr, mipgen), **KA** kernel agent (claude_frontend_audit: kernel patches/present/GPU DVFS),
**AP** this agent (ansuz-port: CPU frequency/scheduler/QoS, system freezes, UI metrics, CA preferences).
Status: `have` = equivalent exists (pointer), `partial`, `todo`, `n/a`, `done-host` (implemented, host-tested,
not on device), `measure` (needs a device number first). Gains are THEIR measurements unless marked [est].

## Top 15 by expected gain for us

| # | item (row) | why it matters for us | owner | status |
|---|---|---|---|---|
| 1 | K3 GPU clock to OPP0 (893->our 886 MHz class) | ui350 is GPU-bound (GPU ms == cmd ms); their K6 364->893 MHz: GPU ms after frame 31->4.2, presents 37->60/s | KA | partial (n2mhz=654 image exists; 886 + thermal guard todo) |
| 2 | P1 frame pipelining: scheduled-at-submit + display-frame throttle depth 2 | our commitAndWaitUntilSubmitted waits for GPU *completion* (compute_frontend.m:1687 + teguExecute comment): CA thread serialised with ~26 ms GPU; theirs 0.24->0.70 home with throttle | EC | todo |
| 3 | C1 translator constant fold of uber shaders (ours) vs their post-inline DCE loop | 91 Mali misses = 40.2 s of 40.7 s are fixed_frag; fold 528 KB->3.4 KB SPIR-V | EC | in progress (side patch) |
| 4 | C2 background compile at pipeline creation (utility QoS) + prewarm known variants | hides AIR misses (26 = 19.8 s) at creation; draw-time Mali variants need exact-key prewarm | EC | todo (after C1) |
| 5 | S1 core-aware placement of UI-priority threads on big cores (0212) | our ADT: all 8 CPUs cluster 0 SMP, cpu0-3 A55, 4-7 A78; XNU fills lowest idle CPU first => CA render thread (which also runs our encode+submit: null submission target) mostly on A55s; theirs render thread CPU halved | AP | done-host (0099 level b, opt-in n2sch=0xf / n2schab A/B) |
| 6 | S2 CPU frequency: measure, then MCUPM DVFS request (0140/0420/0424) | their A76 were left at 1.2 GHz by LK (2.2 max); nobody has ever measured our clusters (no clock line in c6l log) | AP | done-host (0099 level a: NORD2 CPUCLK + n2csram); DVFS write after first numbers |
| 7 | E3 fragment-job tile bounding to union of draw scissors/viewports | n2r always bounds full target (nord2_metal_render.c:449,586); LOAD passes with small draws pay full-target load+store (bandwidth-bound 1.7 ms each) | RL | todo |
| 8 | E4 pass merging (Load/DontCare passes on same attachments, hazard-checked) | 9.95 full-screen jobs/cmd; their merge 12->10 passes | RL | partial (TF1 tile-fetch, gated batching C6k) |
| 9 | E1 AFBC for private render targets / intermediates | halves bandwidth; their clear-only 0.8->0.19 ms, 38->43 fps | RL+TX | todo |
| 10 | E2 constant reads from WC buffers with wide loads (FAU spans) | CPU reads of write-combined buffers ~82 MB/s word-wise; their native encode 8.1->6.9 ms/submission; our 0.27 ms/draw encode | EC | measure (check whether encode reads uniforms from WC mappings) |
| 11 | P4 present on swap / present pacing (0440, 0123) and GPU-scaled present/flip (0152/0153/0156) | present 13 ms, panel 30 Hz; GLES p50 swap->panel -5.5 ms with 0440 | KA | partial (0097 present thread, GPU-complete gating) |
| 12 | K2 Mach-IPC cost: IOConnectTrap path (0120) instead of IOConnectCallMethod | their MIG call 15-25 us vs trap 1.6 us; we do ~2 calls per kernel job x 16 jobs/cmd | KA+RL | todo |
| 13 | C3 Mesa NIR validation off / release Mesa for the device compiler | their phone uber variant 1.3->0.66 s; our mesa-build is debugoptimized + b_ndebug=if-release (validation on) | CS | in progress (measure on top of fold) |
| 14 | T1 YCbCr 420v -> RGBA shadow by GPU pass (not CPU decode / compute) | their app-launch zoom 0.32-0.44 -> 0.60-0.87 fresh; ours decodes on CPU per seed change (render_encoder.inc:963) | TX | partial |
| 15 | T2 mipmaps as GPU render passes | ours = CPU box filter jobs (blit_encoder.inc:261, blit=4 CPU blits); CA regenerates 512x16/776x24 chains per frame | TX | partial |

## Full ledger

### Compile path

| id | technique | ansuz where | their gain | ours | applicability | owner | status |
|---|---|---|---|---|---|---|---|
| C1 | Kill dead uber-shader code before Mali compile | nm/rottenapple_nm.c:776 (DCE loop after inlining, NATIVE-METAL 14.4) | 5.4->0.66 s/variant | translator const_loads fold (gpu/build/compile-stall-20261026/translator-const-loads-fold.patch, side copy) | ours is stronger (removes at source; also shrinks AIR) | EC | in progress |
| C2 | Background native compile at pipeline creation, utility QoS worker, cache on disk incl. refusals | rottenapple_metal_native.c:815 (QOS_CLASS_UTILITY worker), NATIVE-METAL 14 | cold costs moved off the frame (Mindustry 55 PSOs, 60 fps throughout) | pipeline packs + disk cache (pipeline_cache.c, make_pipeline_pack.py); compile is synchronous on caller thread | creation-time default variant + observed format sets; draw waits only if not ready | EC | todo |
| C3 | NIR validation off in the device compiler | nm/rottenapple_nm.c:776-782 | 1.3->0.66 s per uber variant | mesa-build debugoptimized, validation on; our own nir_validate_shader calls in mali_graphics_compiler.c:342,460,513,608 | measure after C1 (encode reports 8 %) | CS | in progress |
| C4 | Record pipelines CA creates, prewarm at next launch (.rec) | rottenapple_metal_shaders.m:450-560 | lock screen at confirmation from 4th launch | pipeline packs accumulate corpus (1ef65de) and ship in addon; 308 entries / 234 hits on c6l | have (cache of compiled results is better than descriptor replay) | EC | have |
| C5 | Native constant model: offsets in descriptors, constant UBO loads promoted to FAU | NATIVE-METAL 5.3 | fill shader 1.0->0.16 cycles | buffer table / push windows (TDG2, set*Bytes sub-allocation) | check Mali stats of CA fixed_frag for LD per fragment | CS | measure |
| C6 | Fast-mode float assumptions (denormal flush, half quantize emulation dropped) | VULKAN-METAL-RESEARCH 9.11 item 3 | GPU 22.5->8.9 ms/frame | translator profile notes already Native*/Device* (air_bridge.rs:725,836) | have; confirm no `iand/bcsel` denorm chains in Mali disasm of a CA shader | CS | have (verify) |
| C7 | Refusal cache + per-phase compile timing env | rottenapple_metal_native.c, ROTTENAPPLE_NM_TIMING | diagnostics | TEGU_PSO_REC / compiler_trace.h | have | - | have |

### Encoding / submission

| id | technique | ansuz where | their gain | ours | applicability | owner | status |
|---|---|---|---|---|---|---|---|
| P1 | Display-frame throttle (<=2 unfinished CA display frames, wait <=100 ms at commit) | rottenapple_metal_ca.c:395-450, commands.m:1117 | home 0.24->0.70, cc 0.33->0.69 | none; commit waits for full GPU completion instead (depth 1, CPU and GPU serial) | needs P2 first, then throttle bounds the queue | EC | todo |
| P2 | commitAndWaitUntilSubmitted returns at kernel hand-off; kernel holds present until surface jobs done (0430 surface hold / 0126) | CA-METAL 8 "Pipelining", 0430 | 7-9 ms off CA thread per frame | kernel 0097(c) GPU-complete present gating exists (nord2_gpu_surface_busy via write_mask); WAIT_SEQ 0090 async chains | make "scheduled" = submitted for display commands when the import write_mask is declared | EC+KA | todo |
| P3 | One kernel SUBMIT per command buffer (chains), not per pass/job | NATIVE-METAL 7.2, 18.4 | Mindustry SUBMIT 0.63 ms (2 ioctls/pass) | 15.8 kernel jobs/cmd, batching 35-40 % of draws (C6j), batch-features-v9 | more batching = fewer ioctls and fewer full-target maintenance | RL | partial |
| E1 | AFBC render targets (private, no host transfer) | m2v patch 0003, VULKAN-METAL 9.11.1 | clear-only 0.8->0.19 ms; 38->43 fps | linear everywhere | intermediates (blur pyramids, backdrop captures) are private | RL+TX | todo |
| E2 | Copy pushed-constant spans once with wide loads (no per-word WC reads) | nm/rottenapple_nm_enc.c:657 | encode 8.1->6.9 ms/submission | lazy CPU mapping (0096); unknown whether encode reads CA uniform buffers through WC/uncached maps | profile encode; memcpy span once | EC | measure |
| E3 | Bound fragment job tiles by union of draw viewport/scissor | NATIVE-METAL 17/18, CA-METAL 8 | display passes 100 % anyway; small passes win | n2r full target always (nord2_metal_render.c:449,586) | LOAD/DontCare passes only; CLEAR must cover target | RL | todo |
| E4 | Render pass merging with hazard checks | rottenapple_metal_passmerge.c, test/passmerge_hostcheck.c | 12->10 passes, +0.5 fps (bandwidth case) | draw_reorder.inc, TF1, gated batching | merge CA's split display passes where no sampling between | RL | partial |
| E5 | ZS preload (pre-frame shader for Load depth/stencil) and colourless passes | NATIVE-METAL 18.2 | Mindustry fully native, CPU 89->58 % | MSAA4/depth rare in CA; preframe load exists for colour | low for CA | RL | n/a (low) |
| E6 | Per-pass descriptor caches (SPD per shader, texture descriptor per texture) | NATIVE-METAL 18.2 | Terraria encode 5.0->1.4 ms | plan memo (59 % misses on ui60) | fix memo key (59 % miss) | EC | todo |
| E7 | Linear textures over memory sampled natively (no per-use shadow) | CA-METAL 8 | native passes 58->95 % | sampled-only IOSurface shadows only where formats differ (texture-opus P1) | have mostly | TX | have |
| E8 | CPU cache clean only of the span draws reach (index scan) | NATIVE-METAL 18.2 | 4 MB->0.12 MB/frame | kernel 0087/0096 scoped maintenance, per job clean 12 us | have | KA | have |
| E9 | No-copy buffers keep caller pointer | CA-METAL 10.4 | correctness + fewer copies | check newBufferWithBytesNoCopy path | small | EC | measure |
| E10 | Clears as fast clears / attachment_clear | - | - | attachment_clear_* packages | have | RL | have |
| E11 | Submission limits (BOs 192, images 192) | NATIVE-METAL 18.2 | Terraria world native | >32 handles refuse (audit) | raise if refusals seen | RL | measure |

### Textures / formats

| id | technique | ansuz where | their gain | ours | owner | status |
|---|---|---|---|---|---|---|
| T1 | 420v/420f -> RGBA8 shadow by one render pass per seed change, prewarmed pipeline | rottenapple_metal_ycbcr.m:96 | app zoom fresh 0.32-0.44 -> 0.60-0.87 | CPU decode ycbcr_decode.h / render_encoder.inc:963 | TX | partial |
| T2 | generateMipmaps as bilinear render passes | rottenapple_metal_mipgen.m:119 | per-frame CA chains off compute | CPU blit jobs blit_encoder.inc:261 | TX | partial |
| T3 | B5G6R5, ASTC hardware formats | NATIVE-METAL 18.2 | Terraria native | ASTC LDR (91fd34f), f43 BGR5A1 | TX | have |
| T4 | Display surface BGRA (CIF10 off) | U2 7 | prerequisite | CA prefs CAEnableDeepFramebuffer=false (package_metal_addon.py:55) | - | have |
| T5 | Cache per-display-surface state across CA surface recreation | CA-METAL 7 gap 1 | fewer imports | surface import lease (surface_import_lease) | TX | partial |

### Present path / display

| id | technique | ansuz where | their gain | ours | owner | status |
|---|---|---|---|---|---|---|
| D1 | Present pacing: displayMinRefreshInterval, SwapWait retire only after copy | 0123 | stable vsync pacing | 0097 absolute grid, 30 Hz default | KA | have |
| D2 | Present only GPU-complete surfaces, poll 0.5 ms, forced after 200 ms | 0124-0126 | no stale tiles | 0097(c) | KA | have |
| D3 | Present on swap (kick present in the interval it was swapped) | 0440 | GLES swap->panel p50 -5.5 ms; Metal no gain | none | KA | todo (after P2) |
| D4 | Retire-on-GPU: replaced-unseen swaps wait for their GPU work (back-pressure) | 0235 | cc p99 frame->GPU 80-96 -> 50 ms | none | KA | todo (needed once P2 lets CA run ahead) |
| D5 | GPU-scaled present into scan-out frame + flip, early retire, no older steal | 0152/0153/0156/0265 | copy 3.4 ms -> 0, needs GPU headroom + 4th frame | n/a: our panel is single-buffered command-mode DSI (copy then xfer 10.3 ms) | KA | n/a now |
| D6 | Command-mode panel pump with TE, polled vblank | 0374/0378/0382/0385 (ansuz MT6878) | 60 Hz on command-mode panel | 0097 polled xfer (xferStart, 500 us poll) | KA | partial (TE sync todo) |
| D7 | Bilinear NEON scaled copy | 0150/0151 | 8.6->3.4 ms copy | copy 2.7 ms (no scaling 1125x2436 -> panel?) | KA | have/check |
| D8 | Direct scan-out via IOMMU (M4U own table) | 0252/0253, IOMMU.md | removes copy | n/a until M4U on MT6893 | KA | n/a |
| D9 | Luma boost pass off (CADeviceNeedsLumaBoost true for iPhone10,3) | CA-METAL 11.3 | ~0.7 ms GPU/frame theirs; ours a full-screen pass ~1.7 ms | luma_boost_lph seen in c6l logs (std_vert1_lph + luma_boost_lph, targets 80/15+80/0 -> generic-mrt fallback) | AP | blocked: no QuartzCore binary / shared cache on this host to find a preference or flag |
| D10 | 60 Hz panel | 0154 | - | n2fbhz=60 image exists, xfer 10.3 ms caps ~40-75 Hz | KA | measure |

### Kernel / system

| id | technique | ansuz where | their gain | ours | owner | status |
|---|---|---|---|---|---|---|
| K1 | Job state page + completion without syscalls | 0120 (K5.2) | - | WAIT_SEQ (0090), irq completion opt-in (0092) | KA | partial |
| K2 | IOConnectTrap fast path for GPU requests | 0120 | 15-25 us -> 1.6 us per call | IOConnectCallMethod (nord2_transport.inc:31) | KA+RL | todo |
| K3 | GPU DVFS/OPP to max under load, governor down when idle | 0127 (K6), 0386/0421 | 364->893 MHz: GPU ms 31->4.2 | 0091 cold-start OPP, 0095 n2mhz alias (654 tried) | KA | partial |
| K4 | WC BO mappings + cache maintenance policy | 0115/0118/0155 | per job 418->76 us | 0087/0096 (clean 12 us, inv 8 us) | KA | have |
| S1 | Core-aware placement of pri>=47 threads on big cores (PLACE/PREEMPT/KEEPFREE/PULL), runtime sysctl + in-boot A/B | 0212 (+0236 TOP fix) | render thread CPU halved; scroll +5 pts; cc -8 without D4 | 0099: osfmk/kern/nord2_sched.c + nord2_sched_policy.h, hooks in sched_clutch.c/sched_prim.c, bsd/dev/nord2_sched_sysctl.c | AP | done-host (D4/P1 back-pressure advisable before default-on) |
| S2 | CPU clock measurement (dependent-add chain vs timer) + cpufreq readout | 0095/0140 top_mhz_here / rottenapple_cpu_mhz | found A76 at 1.2 of 2.2 GHz | 0099 NORD2 CPUCLK line, kern.nord2_cpuclk, n2csram cpuhvfs CSRAM readout | AP | done-host |
| S3 | CPU DVFS via MCUPM engine + governor + boost, verify by measurement, thermal guard | 0140, 0420, 0424 | A76 1.2->2.2 GHz | none; MT6893 uses cpufreq_v1 hybrid (CSRAM 0x0011bc00 + MCUPM IPI) unlike MT6789 cpufreq-hw | AP | measure first (S2) |
| S4 | Thermal driver + ceilings | 0260/0261/0422 | safety for S1/S3/K3 | 0094 LVTS sensing opt-in | KA | partial |
| S5 | Idle pollers / kernel_task idle CPU | 0122 | kernel_task idle CPU down | ? (not audited) | AP | measure (TOP kthreads not ported) |
| S6 | IOKit daemon busy release fix | 0141 | boot/quiet hang avoidance | not seen in our logs | AP | n/a |
| S7 | MobileGestalt 5 s IONetworkController wait per question (LS rebuild freeze) | U2 9, ROOTFS ("mac-address-wifi0") | 818 s rebuild freeze | /chosen/mac-address-wifi0 present in baseline ADT (playback.adt); bluetooth MAC path does not wait | AP | have (verified) |
| S8 | LaunchServices rebuild must not run with SpringBoard up; lsd jetsam 512 MB for rebuild | ROOTFS | freeze avoidance | no rebuild tool in our flow | AP | n/a |
| S9 | PRIV_WORK_INTERVAL for CA | 0064 | CA work interval | in our series (0064) | - | have |
| S10 | Mach kmsg SHA-256 signing cost (ikm_sign 1.8 us/sig) | GPU-PLAN K5.2 | 4-11 us/round trip | same XNU; security change, user decision | AP | user decision |

### UI harness / metrics

| id | technique | ansuz where | ours | owner | status |
|---|---|---|---|---|---|
| U1 | Present ring + capture (0142), touch injection, fresh/even/jank pacing metrics | UI-HARNESS.md, userland/ui-harness | analyze_ab_run.py (cmd/s, jobs, encode, PSO), NORD2 FB PRESENT aggregate line | AP | partial (needs per-present ring in kernel; device-only) |
| U2 | TOP sched / TOP cpu / TOP cpufreq lines | 0212/0236/0140 | 0099 NORD2 CPUCLK + NORD2 SCHED lines | AP | done-host |
| U3 | Safety: opt-in budget + watchdog for CA-on-Metal launches | rottenapple_metal_ca.c (CA-METAL 3) | AB_BUILTIN auto-disable phases, installer rollback | - | have (different design) |

## What each owner still has to deliver for the next build (from this ledger)

- KA: integrate 0099 (this agent; files below) into P8 link (new object + 2 replaced osfmk members), K3 OPP0 image with
  thermal ceiling, D4 retire-on-GPU before P2 ships, K2 trap selector.
- EC: P2 + P1 (scheduled at submit for display commands, depth-2 throttle, opt-out env), C1 land, C2, E2 profile, E6 memo key.
- RL: E3 tile bounding (n2r fragment bound_min/max from scissor union for LOAD/DontCare), E4/P3 merge+batching, E1 AFBC plan with TX.
- CS: C3/C5/C6 measurements on top of C1.
- TX: T1 GPU YCbCr pass, T2 GPU mipgen.
- AP: S1/S2 (done-host, below), D9 luma boost analysis, S3 after the first CPUCLK numbers.

## Implemented by AP (host-only, nothing on device)

`gpu/build/ansuz-port-20261026/0099/`: `0099-nord2-core-aware-sched-cpuclk.patch` (sha256 6d9dbb13..., after 0084),
sources `src/`, pristine copies `orig/`, `compile_0099.py` (CI build.log flags, nice 19, j1), objects `objs/`
(nord2_sched.o f73d8d43, nord2_sched_sysctl.o d0a0c7e3, sched_clutch.o fbf1f1a4, sched_prim.o 9dd27394),
`tests/nord2_sched_policy_test.c` (ASan+UBSan, 43 checks PASS), `tests/test_analyze_nord2_sched.py` (PASS),
`analyze_nord2_sched.py` (CPUCLK verdict vs nominal 2000/2600/3000 MHz; SCHED + TEGU_PERF per A/B phase; on the c6l
log it reproduces 26.5 ms/cmd, 0.268 ms/draw). Link level (a) adds 2 objects (measurement only); level (b) also replaces
archive members sched_clutch.o + sched_prim.o (placement). Default behaviour unchanged (n2sch=0).
