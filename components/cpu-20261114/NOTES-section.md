## 2026-11-14 ~21:40 CPU / memory / platform parity audit + 0641 (CCI follows the faster big cluster, LL 2000 MHz, vendor CCI perf map), images q10jcpu1 / q10jcpu1c (host build + host tests only, NOT on the phone) -- gpu/build/cpu-20261114
Audit (V = verified in log/code, I = inferred):
- [V-code+log] CPU DVFS is NOT missing: the MCUPM/CSRAM hand-over (0116) + the 0460 policy (nord2_gov.h, 20 ms tick, up at once, down after 40 ms,
  class floors ui L2200/B1998, anim+touch L2354/B2463, jank = cap) run in every image (n2dvfs=3000, kern.n2dvfs_mode 2). perf1a: L/B follow the load
  and the class (DECISION 372 rows: L 2600 11 % / 2354 12 % / 2200 49 % / 1740 24 %; B 3000 11 % / 2463 20 % / 1998 60 %), measured clocks match the asks.
- [V-log] big-core placement exists (0099 sched objects, n2schpri=37, mode 0xf): CA render 97 % on big cores; still 388 eligible-on-little-while-a-big-idles /s
  and the pick is the LOWEST idle big CPU (n2s_pick -> cpu4, 2.6 GHz) so the prime cpu7 (3.0 GHz) is the least busy core (loaded windows: A55 34 %,
  cpu4-6 42 %, cpu7 28 %; Maps: cpu4 60-70 %). Not changed: the placement code is the prebuilt ansuz-port 0099 objects (hash-pinned in the builder).
- [V-code+log] what was below vendor: (1) the CCI (DSU/L3 + interconnect) row followed the B row ONLY (n2dv_cci_row(LL, B)): with the UI floors (UI threads on
  cpu4-6 at 2200, B at 1998) the CCI ran at 1021 MHz 60 % of perf1a; (2) the LL cluster (cpu0-3, all non-UI-priority work, 26-41 % busy each under load) stays
  at the bootloader row 1800 MHz while its rail already measures 1000 mV = the vendor voltage of its 2000 MHz top row; (3) the vendor CCI 'perf' half of the
  map is never used: on Android cm_mgr (mtk_cm_mgr_platform.c, mt6893) calls mt_cpufreq_update_cci_mode(1) for 100 ms on every perf hint (plus a DDR QoS request).
- [V-log wall4] DRAM: DDRPHY decodes 3733 Mbps (shu_lv 0), DVFSRC disabled (basic_control en 0): the DRAM is FIXED at 3733 (vendor max 4266, -12.5 % peak).
  Not touched: 4266 needs the SPM shuffle switch + VCORE raise over pwrap (owned by ni_thread). Measured bus use (CC over the Photos grid) 6 GB/s read + 4 write of
  ~29.9 GB/s peak: not the limit today [I].
- [V-log] heat is the biggest sustained loss: battery ladder level 1 at 41.0 C (L 2354/B 2713/GPU 805), 2 at 43.0 (2200/2463/654), 3 at 45.0 (1740/1998/512):
  perf1a spent 24 % of its DECISION rows at level 3 and 26 % at level 2. SoC temperature: the LVTS code exists (nord2_gpu_lvts.c, boot-arg nord2_lvts=1..3) but
  has NEVER run on hardware (lvts=0 in every capture); mode 1 is read-only but the controllers are unconfigured without mode 2 (register writes); mapping 2 banks
  costs io_submap pages (3 free). Not enabled here; needs its own test image (see below).
- [V-log] memory (Safari 352-376 s): MEMGOV A down to 25-38k pages (16 KiB) < crit 39075 but "no victim in bands <= 30 (skipped: name 35-38 class 214-220)":
  nothing killable; wired 188-210k pages (= 2.9-3.3 GiB of the 5088 MiB window; GPU surfaces ~1 GiB + GPU private 0.7-1.5 GiB); compressor active (40k pages
  compressed); pageins up to 20k / 2 s = file-backed code/data re-read. The memory exception at ~352 s is that pressure. A bigger working set needs a bigger RAM
  window (loader/memory-map project) or less wired GPU memory (bigpool1 adds up to 100 MiB per GPU process while idle -- released on np_pressure); no kernel
  threshold change made (lowering crit would only make MEMGOV fail more often; raising the kill bands would kill user apps).
0641 (nord2_dvfs.h + nord2_cpufreq.c; patch sha 60370b61...; rebase 'identical (clean)'; touches nothing else):
- N2P_COL: CCI column = the B row whose clock is the first >= max(L clock, B clock). UI floors: CCI 1021 -> 1182 MHz; every CCI row it can pick was already picked
  by the old rule for some B row (host-proved for all 4096 row triples), same 1000 mV LL/CCI rail. Store order kept (faster CCI before the cluster store,
  slower after; with mode 0 the store sequence is bit-identical to the pre-0641 code, 200,000 random walks).
- N2P_LL: LL row 0 = 2000 MHz (vendor 160 x 6.25 mV = 1000 mV, the voltage the rail already reads). One row step through the existing n2d_set_row_fast path;
  the first time checked with the CPU clock measurement of cpu0 (+-8 %); a miss puts the bootloader row back and disables it for the boot
  ('NORD2 CPUPLAT: LL row 0 ... NOT reached'). Dropped at thermal level >= 2 (battery 43 C), back below it.
- N2P_PERF: the vendor perf half of the CCI map (rows 16..31) while the class is anim/touch/jank and thermal level < 2: UI floors -> CCI 1540 MHz, cap rows ->
  1700 MHz (CCI row 0 = NEW state for this hardware, same 1000 mV per the vendor table). Re-evaluated every 20 ms tick, CCI stored only on a change.
- Applies only in the policy loop (kern.n2dvfs_mode >= 2); mode 0/1 put LL and the CCI rule back. Bring-up runs with n2p_mode 0 (unchanged).
- Switches: sysctl kern.n2cpuplat (bits 1/2/4, run time); boot default 7; n2gdv +65536 = 1 (column only). 32768 is taken by gpusched (n2gp) -- do not reuse.
  Full off = the q10jperf1 image (no 0641).
- Log: boot 'NORD2 CPUPLAT: on, kern.n2cpuplat=...'; once 'NORD2 CPUPLAT: LL row 0 verified: cpu0 measures N MHz'; every 2 s next to DECISION
  'NORD2 CPUPLAT: mode= ll=MHz cci=MHz(row) perf=on|off | 2s: perf_ms= ll_fast_ms= cci_writes= ll_moves= | ll_bad='.
Expected [I]: LL work +11 % clock (~half of all CPU time in UI phases runs there); UI threads on cpu4-6 get a 16 % faster L3/interconnect in 'ui' and 50 % faster
  while animating/touched (memory-latency-bound parts of CA/app frames: guess 5-10 % shorter CPU frame time); jank class CCI 1540 -> 1700. Cost [I]: a few
  hundred mW while animating (A55 cluster at the same voltage +11 % f; CCI f up at the same voltage); both extras stop at 43 C battery. NOT a fix for the
  GPU-bound Magnifier (camprev1) or for Maps' buffer churn (bigpool1).
Host tests: cpu-20261114/test/run.sh -> CPUPLAT_HOST_TEST_OK (ASan/UBSan; rule properties over all 16^3 rows, mode-0 store-sequence identity, CCI order and
  cci_last tracking in all 8 modes x 200,000 random walks, refresh stores only on change, never when stopped). Mutants: 4/6 killed, 1 no-op, 1 equivalent (ordering
  by cluster direction = ordering by CCI direction because both CCI maps are monotone).
Kernel compile: nord2_cpufreq.c with the CI flags rc 0, warning categories as before (+ noise: unsafe-buffer 7, decl-after-statement 4).
Images (q10jperf1 recipe + 0641; cpu-20261114/build_q10jcpu.sh <name> [EXTRA_ARGS_OVERRIDE=...]; private integ copy cpu-20261114/integ):
  q10jcpu1  n2gdv=9     (all three bits)  device-mmu-q10jcpu1-candidate/ui-sched/nord2-ios-metal-q10jcpu1-ui654fb60dvfs-cap3500-ram5088-sched.img 15198208 B
            sha256 4a07f5ea83ab452028456b70ec4cc55129892dd73d6e9dd60ea1b0f8075d0c1c, cmdlen 603 (commandline identical to q10jperf1)
  q10jcpu1c n2gdv=65545 (CCI column only) device-mmu-q10jcpu1c-candidate/ui-sched/nord2-ios-metal-q10jcpu1c-ui654fb60dvfs-cap3500-ram5088-sched.img 15198208 B
            sha256 2433cb283e32f4e6395042ada29a0d0335443b96576cfe254ac9a64e5c399e49, cmdlen 607 (0 free)
  both: KERNEL OK, FAILURES none, BUILD_EXIT 1 = the known 0399 IMAGECHECK stop, SCANPATH_OK (q10jperf1's list, no new extras), WIFI_MEMBERS_OK; stage differs from
  q10jperf1 only in nord2_dvfs.h + nord2_cpufreq.c; 'NORD2 CPUPLAT' strings in the kernel. Contains GNSS 0583-0593, haptic 0611-0618 v6a, 0620/0621/0640;
  NOT gpusched 0630-0633 (0641 touches no file of theirs: merge = add the piece "0641 gpu/build/cpu-20261114/piece-0641" to their PIECES list).
Tool: python3 gpu/build/cpu-20261114/tools/cpuplat_check.py XNU_LIVE_LOG -> CCI/LL/perf shares, cluster clock shares, per-core busy (A55 / cpu4-6 / prime),
  SCHED watch averages; on pre-0641 logs it prints which CCI the old and the 0641 rules pick for the same rows (perf1a: old 1021 MHz 60 %; 0641 1700 23 % /
  1255 13 % / 1182 35 % / 1021 27 % (the 1021 rows are thermal level >= 2)). Verdict CPUPLAT_ACTIVE / CPUPLAT_LL_BAD / CPUPLAT_OFF / CPUPLAT_NOT_SEEN.
What to read in the next capture (same perf1a scenario): CPUPLAT_ACTIVE and 'LL row 0 verified' (cpu0 ~2000 MHz); CPUPLAT lines cci=1182/1540/1700 while
  animating, ll=2000 below 43 C; no new hang / watchdog at the first LL step (it happens right after 'NORD2 CPUPLAT: on'); n2perf.py Maps/home phases fps,
  on-time share and 'nosw' drops vs perf1a; battery slope vs perf1a (38.6 -> 45.5 C in ~8 min); if anything is off: q10jcpu1c (column only), then q10jperf1.
Not done / next: (a) prime-first placement (n2s_pick prefers cpu4; needs a rebuild of the hash-pinned 0099 objects -- worth +15 % on the hottest UI thread);
  (b) LVTS SoC temperature as the thermal input so the battery ladder (41/43/45 C) can be relaxed safely -- separate test image with nord2_lvts=1 (read-only
  probe: gate state + raw MSR) first; (c) DRAM 4266 / DVFSRC (pwrap-owned VCORE); (d) RAM window > 5088 MiB.
