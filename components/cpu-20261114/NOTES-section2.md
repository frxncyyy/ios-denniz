## 2026-11-14 ~23:00 CPU parity step 2: 0642 prime-first placement + 0643 SoC (LVTS) thermal ladder, 0641 bit moved; DRAM/RAM verdicts; images q10jfinal1 / q10jfinal1f on q10jall3 (host build + host tests only, NOT on the phone) -- gpu/build/cpu-20261114
Bit change (0641): the conservative switch is now n2gdv +16 (65536 belongs to 0633's 886 MHz voltage). Every n2gdv bit up to 65536 is now taken
  (1..8 GPU mode, 16 cpu-20261114, 32..512 older pieces, 1024/2048 0620.., 4096 0630, 8192/16384 0631, 32768 0632, 65536 0633); 131072+ no longer fits the boot line.
  n2gdv +16 = ALL cpu-20261114 extras conservative: 0641 CCI column rule only, 0642 prime-first + B floors off, 0643 LVTS off + battery-only ladder.
  (16 was 0493's DRAM-probe bit; 0493 is not in this chain -- do not merge 0493 with this bit.)
0642 (nord2_dvfs_sched.h + nord2_cpufreq.c + a RECOMPILED nord2_sched.o):
- Evidence the old rule is the problem [V-code+log]: nord2_sched_policy.h n2s_pick = hint, else the LOWEST idle fast CPU -> cpu4 first, cpu7 (3.0 GHz) last;
  perf1a loaded windows: cpu7 28 % vs cpu4-6 42 %, cpu4 60-70 % in Maps.
- The scheduler code is SOURCE (ansuz-port-20261026/0099/src, compiled locally by compile_0099.py with the CI flags), not a CI member. Copy in cpu-20261114/sched0099;
  the unmodified nord2_sched.c rebuilt there gives an object whose __text and __cstring are identical to the pinned one (only DWARF paths differ) -> toolchain
  reproducible. Only nord2_sched.c is rebuilt (sha b87033b8...); sched_prim.o / sched_clutch.o / nord2_sched_sysctl.o are the pinned 0099 objects (hashes kept in
  sched0099/objs/compile-result.json). The image builder is a private copy (cpu-20261114/build_p1_candidate_integ_cpu.py, only S99 changed) driven through
  cpu-20261114/run_integ_cam_cpu.sh: the shared integ/ansuz-port files are untouched.
- Policy: n2s_choose2(..., prime_first): an eligible thread (pri >= n2schpri 37) placed on an idle big CPU takes cpu7 when idle (before its hint), else its hint,
  else the fastest idle big CPU (highest number). Everything else (PREEMPT, SPILL, KEEPFREE, PULL) unchanged. TUNABLE n2prime (default 1).
- B (cpu7) floors raised so the prime core is never slower than cpu4-6 under the same class: ui 1998 -> 2463, anim/touch 2463 -> 2713 MHz (L stays 2200/2354/2354).
- Log: 'NORD2 CPUPLAT: prime-first placement on ...' at boot; every SCHED report adds 'NORD2 SCHED prime cpu7 (n2prime=1): busy P%, ca on prime C%, prime picks N/s'.
- Expected [I]: the hottest UI thread (CA render / app main) on the 3.0 GHz core: up to -13 % run time per frame for that thread; cost: one A78 at 2.46-3.0 GHz
  more often (~100-300 mW while animating).
0643 (Nord2GPU.cpp + nord2_gov.h + nord2_dvfs_sched.h + nord2_cpufreq.c):
- LVTS on by default in mode 4 'auto' (controllers cpu-big, cpu-big2, cpu-little, gpu = mask 0x17): read-only first (infracfg gate checked; a gated block is never
  touched); only if no sensor is sensing, the existing vendor-order start (n2lvts_start, written and host-tested by an earlier agent, never on hardware) WITHOUT
  opening the clock gate. Both LVTS banks are mapped with ml_io_map_unmappable (no io_submap page). After setup only the GPU worker thread reads LVTS (every 2 s,
  Nord2GPU::lvtsTick, also when the GPU is idle) into a cache; JOBS3 prints the cache (no LVTS access from client threads any more).
  Boot lines 'NORD2 GPU LVTS: mode=4 mask=17 start=... state=... pre=N' + one line per sensor (raw, ok/invalid, mC).
- Ladder (CPU and GPU both, n2g_therm_update2): SoC usable -> battery steps 43.0/44.5/45.5/46.0 C + SoC steps 85/90/95/100 C (+10 C while the calibration is the
  vendor default, which it is: the efuse is not read); SoC not usable -> the old battery steps 41/43/45 and level 4 at 46.0 C (was 47.0). SoC counts only between
  battery-15 C and 125 C. Down only when every source allows. CPU caps per level unchanged (L 2354/2200/1740, B 2713/2463/1998), GPU caps unchanged (0633's 886 only at level 0).
  Lines 'NORD2 DVFS thermal ladder: level N, SoC xx.xx C (default calibration: +10 C margin) used: ...' on every change; CPUPLAT lines add soc_cC/soc_used.
- Expected [I]: with LVTS readable, levels 1/2 start 2 / 1.5 C later on the battery -> in a perf1a-like session roughly half of the level-2/3 time becomes
  level 0/1 (more sustained CPU and GPU clock, battery up to 46 C). Without LVTS: same as before except level 4 at 46 instead of 47 C (slower above 46 C).
- Risk: first LVTS register writes ever (thermal controller only; protection/interrupt registers never written); a bad decode is filtered by the plausibility window,
  a real hot SoC lowers clocks earlier than before. Kill: n2gdv +16.
DRAM 4266 [verdict: not done, V-code/log]: the DRAM rate is fixed at 3733 Mbps with DVFSRC disabled (basic_control en 0, sw_req 0). Moving it needs the DVFSRC
  configuration of helio-dvfsrc-mt6893.c (QoS/request tables), the SPM vcore-DVFS firmware + the DRAMC shuffle switch, and VCORE through the SPM's pwrap channel;
  0633's pwrap queue serves single MT6359P register writes on ni_thread, not this. A wrong step = DRAM hang = instant reset with no log. Peak gain +14 % bandwidth,
  measured need 10 of 30 GB/s. Not attempted.
RAM [verdict: no safe gain, V-notes/code]: physical 8 GiB = 0x40000000..0x240000000. iOS gets 0x100000000..0x23e000000 (5088 MiB) = the whole upper bank except the
  top 32 MiB; the low bank 0x40000000..0xffffffff is the vendor carve-outs and our own pools (CCCI 0x8a000000, Wi-Fi 0x8fb00000, vcodec 0xA0000000+384M, display pool
  0xC0000000-0xD0000000, cameras 0xD0000000-0xD8000000, JPEG 0xD8000000-0xDFC00000, GNSS 0xDFE00000, DVFS share 0x70000000, ...). XNU here takes one contiguous
  range (gPhysBase/gPhysSize; the 5088 window needed binary patches of wifi_rom_xnu.o / sweet_touch.o and the ram5g loader). At most +32 MiB is reachable: not worth
  a loader change. The real lever is the 2.9-3.3 GiB WIRED (GPU surfaces ~1 GiB + GPU private 0.7-1.5 GiB): GPU/userland agents' area (bigpool1 holds <= 100 MiB more
  per GPU process while idle, released on pressure); no kernel change made.
Host tests: cpu-20261114/test/run.sh -> CPU20261114_HOST_TESTS_OK (0641 suite as before; therm_test 3.4 M checks: fixed points of both step sets, SoC margin,
  plausibility, kill, unreadable, hysteresis, random walks: level 4 whenever battery >= 46 C, level 0 when everything is cool; prime_test 4.3 M checks: prime first, then
  hint, then fastest idle, reason and every non-FAST_IDLE decision identical to the 0099 rule; the original 0099 policy test PASS 43).
Images = q10jall3 recipe (q10jperf1 + 0630 0631 0632 0633 + camprev1 camera package e86a64f2) + 0641 + 0642 + 0643 (+ sched0099 nord2_sched.o b87033b8);
  build: cd gpu/build/cpu-20261114 && EXTRA_ARGS_OVERRIDE=n2gdv=... bash build_q10jfinal.sh NAME. Rebase: 0630-0633/0641/0642 identical (clean), 0643 3-way merged
  into Nord2GPU.cpp next to 0632/0633 without conflict (stage differs from q10jall3 only in nord2_cpufreq.c, nord2_dvfs.h, nord2_dvfs_sched.h, nord2_gov.h,
  Nord2GPU.cpp). All: KERNEL OK, FAILURES none, BUILD_EXIT 1 = the known 0399 IMAGECHECK stop, SCANPATH_OK (no new extras), WIFI_MEMBERS_OK, 15206400 B;
  commandlines identical to q10jall3 / q10jall3f.
  q10jfinal1  n2gdv=9     GPU decisions only (as q10jall3), all cpu-20261114 extras on
     device-mmu-q10jfinal1-candidate/ui-sched/nord2-ios-metal-q10jfinal1-ui654fb60dvfs-cap3500-ram5088-sched.img  sha256 a800644a94b05237439a449c230849306da9d9cae03959b880f2e66d2ca75b2f (cmdlen 603)
  q10jfinal1f n2gdv=73738 GPU clock acts, fast ramp, 886 MHz allowed (as q10jall3f), all extras on
     device-mmu-q10jfinal1f-candidate/ui-sched/nord2-ios-metal-q10jfinal1f-ui654fb60dvfs-cap3500-ram5088-sched.img sha256 34334637c3a509eba306aa4d49b3b5dac91b87a3b9e5a5b4c396bc803742e689 (cmdlen 607, 0 free)
  q10jfinal1c n2gdv=25    as q10jfinal1 with +16 = cpu-20261114 conservative (CCI column only, no LL/perf-CCI/prime-first, LVTS off, battery ladder)
     device-mmu-q10jfinal1c-candidate/ui-sched/nord2-ios-metal-q10jfinal1c-ui654fb60dvfs-cap3500-ram5088-sched.img sha256 bcbad013c9d67b55e873c1ae6af3652b6e73b3bab7ab0cb60b069214fc39a236 (cmdlen 604)
  (q10jcpu1 / q10jcpu1c of the previous section are superseded: they used bit 65536, now 0633's.)
What to read: cpuplat_check.py LOG -> LVTS_READING (else LVTS_NO_VALID_SENSOR / LVTS_FAILED: the ladder then is the battery one, nothing else changes), the
  per-sensor lines (plausible 35-60 C idle?), 'thermal ladder ... SoC ... used'; SCHED prime: 'ca on prime' well above 0 and cpu7 busy up vs perf1a (28 %);
  CPUPLAT_ACTIVE + LL verified; no hang right after 'NORD2 GPU LVTS' (LVTS start) or after 'NORD2 CPUPLAT: on' (LL step); battery max must stay <= 46 C.
  If anything misbehaves: q10jfinal1c (n2gdv +16), then q10jall3.
