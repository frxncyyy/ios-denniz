
## 2026-11-14 ~20:30 PERFORMANCE: capture perf1a analysed, log analyser n2perf.py, fixes bigpool1 (userland) + 0620/0621 (image q10jperf1) -- gpu/build/perf-20261114
Capture: autocycle/runs/perf1a (T0 19:21:13, user: home, App Library, Maps pan/zoom, Magnifier, Safari, switcher, CC/NC, free use) on q10jfin + fin1/sbshift2/api99a/final3/prod9/shader99b/gnssfix22.
Tool: `python3 gpu/build/perf-20261114/tools/n2perf.py <xnu-live.log> [--peaks N] [--csv out.csv] [--json out.json] [--from T --to T] [--all|--events-only]`
(0.3 s for a 28k-line capture). Clock = sum of 'NORD2 SYS: win_ms' (2 s rows; wall ~ T0 + t, +-10 s). Prints: per-row timeline (CPU, L/B MHz, GPU
MHz/cap, GPU busy, jobs, queue wait avg/max, job total max, presents, active fps, short gaps, drops ready/nosw, late ticks, touch Hz/gap, memory,
process launches, foreground FB client, events), phases by foreground FB client, the N worst peaks with the events around them, a summary
(on-time share, touch rate, GPU, respawn loops, battery/thermal, memory/AS/IOMMU, userland messages), and (with 0621) every stutter event
with its uptime and cause plus presents per second. Cumulative counters (GPU AS, FB IOMMU, SWAPWAIT, FB HW, FENCE) become per-row deltas.
Derived: HW-active = PRFCNT gpu cycles / (win x clock) vs driver busy (in flight); Mpix/s; bus GB/s.

Findings perf1a [V-log unless marked]:
- Phases: home/App Library 0-226 s (static 11.3/s status animation; scroll windows 60-89 fps); Maps 226-306 s: 47 fps median, 52 % of
  intervals on time, drops 'nosw' (app late) 3061 vs 'ready' 73, GPU in flight ~40 %, HW active ~35-47 %, CPU 31 % (one core 60-70 %);
  Magnifier 308-350 s: 10-14 fps, GPU in flight 87-89 %, HW active 76-79 %, mean queue wait per job 40-49 ms (max 205 ms), job run max 36.6 ms
  every window; Safari 352-432 s: 80-89 fps (good), memory avail down to 708 MiB with pageins up to 20k / 2 s at 360-376 s, one
  ReportMemoryException at ~352 s, MEMGOV 'no victim'; Photos/switcher 432-468 s 46-77 fps; battery 38.6 -> 45.5 C, thermal level 1 at
  41.4 C, 2 at 43.5 C, 3 at 45.5 C (GPU cap 836 -> 805 -> 654 -> 512, CPU caps too).
- 1 MAPS (cause, [V-log]): Nord2Metal buffer churn. Maps re-creates big GPU buffers all the time: np stats pid 13832 creates 4573 /
  closes 3707 in ~80 s; 'NORD2 GPU MAP' ~50 maps/s of 12-44 MiB (~1.1 GB/s of fresh, zeroed, page-walked memory; 0400 zero-skip is off for
  the boot: LAT2 vfail=1); userland SLOW lines: newTextureWithDescriptor 316 x avg 16 ms (5.1 s), newBufferWithLength 99 x 12.5 ms (1.2 s),
  on the render thread -> the 'app late' drops. The userland pool (nord2_pan.c) never kept anything above its largest class (16 MiB):
  'np pool: allocation of 43452 KiB refused'. [I] the 44 MiB buffers = full-screen 4x MSAA colour (1125x2436x4x4), the 12 MiB = full-screen
  depth/intermediates that Apple GPUs keep memoryless.
- 2 MAGNIFIER (cause, [V-log]): the camera preview is processed at the full 4096x3072 sensor size: 'camera ISP: 5.1 s of 4096x3072 from
  4096x3072 ... the ISP pass over 4096x3072 places: 51 renders' (~10 renders/s, 8.9 passes, wait 39 ms each) + mediaserverd
  'VTMTSComputeFunction' dispatches with 38-41 ms commits (VideoToolbox pixel transfer of that picture). One 36.6 ms compute job at a time on
  JS1 (the vertex/tiler/compute slot, nord2_gpu_hw.c n2hw_select_slots: slot_compute = slot_vertex_tiler = 1) and no preemption
  (try_start: one job per slot, oldest id first) -> backboardd's own vertex/tiler jobs wait behind it -> the WHOLE UI at 10-14 fps.
  Fix belongs to the camera pipeline [not done here]: preview from the binned 2048x1536 'cells' picture (the ISP already did 'the ISP pass
  over 2048x1536 places' for a 4096x3072 output in an earlier session) or a 1080p-class output for preview-only sessions; GPU clock acting
  (decisions-only today: 654 MHz while busy 87 %, cap 805-836) would give at most +23-28 % and more heat.
- 3 TOUCH JUDDER EVERYWHERE (structural, [V-log + V-code]): touch reports 54 Hz median (gap_avg 18.5-18.7 ms, max 36 ms) on an 89 Hz panel;
  each report is a 62-byte bit-banged I2C read of 7.4 ms (sweet_touch.c ni_line delay(4) x 3 per bit) + ni_wait(10) after the work.
  Finger-driven scrolling gets a new position on ~60 % of the frames only (uneven steps = judder) and up to ~19 ms of extra input age.
- 4 GPU job turnaround [V-log, [I] cost]: jobs strictly serial per context, one per slot, completion -> next start through IRQ + bottom half
  (hand_us 60-100 avg, irq 20-25 us) with every job doing L2 clean+invalidate: in Maps 1750-4400 jobs/s at run 0.15-0.25 ms -> the GPU idles
  roughly 30-50 % with 15-22 jobs queued (in flight 40-58 % vs HW active 35-47 %). Not changed (JS_HEAD_NEXT pipelining / cross-pass
  overlap = deep scheduler change, untestable without the phone).
- 5 Heat: 7 C in ~8 min of Maps/Magnifier/Safari -> thermal level 3 caps (GPU 512 MHz) for the rest of the session [V-log].
- 6 Respawn loops [V-log]: 2.56 process launches/s all the time: lskdd ~1/s (launchd job ThrottleInterval 0, MachServices com.apple.lskdd,
  launch event; [I] something asks it every second and it exits), bluetoothd / fairplayd.H2 / wifip2pd every 10 s (heat-20261114
  block-respawn1.py already exists, not applied). Measured cost small: CPU avg 14.2 % in windows with a respawn vs 14.6 % without -> not the
  stutter source; worth removing for power only.
- NOT causes here [V-log]: AS eviction thrash (evictions 2, binds 4, waits 0 in the whole capture), IOMMU map/unmap storms (8 maps, 0
  unmaps), CPU-composed frames (FB HW cpu_frames 0), TE misses 0, GPU faults 0.

Fixes delivered (host-built and host-tested only; NOT on the phone):
- bigpool1 (userland, y2s99c-perf, libredwoodair.dylib only, d1c90b06...; Nord2Metal fe4ecc9f unchanged): nord2_pan.c keeps closed GPU
  buffers > 16 MiB in 4 slots (<= 100 MiB, oldest released first) and hands one to the next request of the same size or up to 1/8 smaller once
  the GPU has finished with it (last_seq <= completed); emptied with the pool on an allocation refusal and on memory pressure (np_pressure).
  Log: 'NORD2 GPU USER: NORD2_RW np bigpool: hits= miss= parked= evicted= held= MiB off=' every 5 s per process. Expected: Maps' create/map
  rate and the 16 ms newTextureWithDescriptor stalls mostly gone (hits ~ creates of those sizes). Kill switch: file
  /private/var/db/nord2-metal/no-bigpool (payload-bigpool1-off) or guest-bigpool1-undo.sh. Control build of base-src = the phone's 7c0c5aba
  (bit-identical); host test host/bigpool_host_test.c 2,000,000 random steps under ASan/UBSan (slot invariants, never a busy buffer, size
  window, cap) + mutants 6/7 caught (1 equivalent). Rehearsals: ship/rehearse-bigpool1.sh (fake roots from the mirror: install/again/undo/again byte-identical, -off switch, foreign switch,
  fallback undo, 8 refusals, shader99b's own undo still works after ours) REHEARSAL_OK; ship/rehearse-vm.sh (txn path in the VM on the
  current mirror, root ba2f4e2f): bigpool1 / bigpool1-off offline EXIT 0 + catalog 3 / 4 changed, 0 outside owned; undo no-op 0 changed.
  Payload: y2s99c-perf/ship/{guest-bigpool1.sh, guest-bigpool1-undo.sh, owned-bigpool1.json, payload-bigpool1{,-off,-undo}}; accepts exactly
  shader99b (Nord2Metal fe4ecc9f + library 7c0c5aba). Risk [I]: reused buffers are not zeroed (same as the existing <= 16 MiB pool);
  up to 100 MiB more held per GPU process while idle (released on pressure).
- 0620 (sweet_touch.c, kernel): while a finger is down, the touch reads use a 2 us half-bit (was 4) and the loop runs on a fixed 8 ms period
  from the iteration start (125 Hz) instead of 10 ms after the work; other buses (MT6360 torch, AW8697 haptics, slider) unchanged; 8 read
  errors in fast mode -> back to 4 us for the boot. Kill: boot-arg n2gdv +2048 (n2gdv=2057) or n2tp=0; sysctl kern.n2tpace / n2tp_half_us
  (1..3) / n2tp_period_us (4000..10000). Log: 'NORD2 INPUT PACE: on= half_us= period_us= fast_errors= fallbacks=' next to INPUT RATE.
  Expected: INPUT RATE gap_avg ~8 ms (~120 Hz), read_avg ~4 ms. Cost [I]: ~50 % of one little core while a finger is down (was ~40 %).
- 0621 (SweetFramebuffer.cpp, kernel): 'NORD2 JANK: n= lost= ev=<uptime ms>:<refreshes><r|n>,...' (every present interval of >= 2 refreshes
  inside an animation; r = swap ready, lost by presenter/TE/fence; n = app/CA late; <= 12 per 5 s window) and 'NORD2 PERF T: t_ms= pres=a/b/..'
  (presents in each second); present thread only, printed with the FB PACE line. n2perf.py lists them.
- bigpool1x = the same change built on webxr1 (webxr-20261114 library 9f263e6f + Nord2Metal 36f755ea): library f271fcbe...; use it instead
  of bigpool1 if webxr1 gets installed first (each accepts exactly its base). Rehearsal ship/rehearse-bigpool1x.sh REHEARSAL_OK (fake roots
  in the webxr1 state). Transaction: `cd gpu/build && [VARIANT=bigpool1|bigpool1-off|bigpool1x|bigpool1x-off] READS=600
  y2s99c-perf/ship/txn_bigpool.sh KERNEL_IMG TAG` (UNDO=1 to go back; copy of webxr-20261114/txn_webxr.sh, NOT executed by its author).
- Image q10jperf1 = q10jfin recipe + 0620 + 0621 (perf-20261114/build_q10jperf1.sh = private copy of the gnss integrator, perf-20261114/integ):
  device-mmu-q10jperf1-candidate/ui-sched/nord2-ios-metal-q10jperf1-ui654fb60dvfs-cap3500-ram5088-sched.img 15198208 B sha256
  cbd72ffe195a51fec311215121994ed9c9d0337caf26e7bd36b307c2683f8bb7; FAILURES none, BUILD_EXIT 1 (the known 0399 IMAGECHECK stop),
  SCANPATH_OK (extras: q10jfin's list + _ni_send _ni_start _ni_stop, the bit-bang functions 0620 changes through ni_line), WIFI_MEMBERS_OK,
  cmdlen 603, commandline and ADT identical to q10jfin; rebase: 0583..0620 identical (fuzz 0), 0621 and 0640 3-way merged in
  SweetFramebuffer.cpp without conflict. Strings 'NORD2 INPUT PACE' / 'NORD2 JANK' / 'NORD2 PERF T' present in the kernel.
  NOT verified on hardware: the FT3518 at a 2 us half-bit (auto fallback to 4 us after 8 errors), the 8 ms loop, the jank lines.
What to read in the next capture: n2perf summary 'touch sampling ... Hz' (expect ~120), Maps phase fps/ontime and 'SLOW ... newTexture'
count, 'np bigpool: hits=', JANK causes per phase, Magnifier queue wait (unchanged until the camera preview size is fixed).
