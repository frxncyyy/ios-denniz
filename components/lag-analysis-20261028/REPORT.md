I couldn't write `REPORT.md`: the harness blocks this agent from writing report files. The full report is below, and the scripts and data are in `work/gpu/build/lag-analysis-20261028/`. Nothing under `gpu/metal` or `gpu/tests` was changed, and the phone, USB, adb and fastboot were not touched. **[M]** = measured from the logs, **[E]** = estimate, with the assumption stated.

Heavy interactions run at about 15–24 fps (0.25–0.40 of 60 Hz) because the CPU is the bottleneck, not the GPU. The GPU is busy only 15–20% of the time.
- **Jobs run one at a time.** Each CA frame is split into 14–27 kernel jobs, and each job is submitted and waited on before the next one starts. CA's thread waits until the GPU finishes the whole frame.
- **The CPU work is on slow cores.** The big A78 cores ran at 0.73 GHz in all three runs. The CPU-frequency image changed nothing (`verified clusters 0x0 … nothing will be written`), so the second C6o run is just a repeat.

## 0. Blur vs no blur, and the switcher with blur once the refusals are fixed

### 0.1 Measured [M], C6o

Each column is a type of 2 s backboardd window. "Flat" is the 60 fps no-blur scene from the ui60 run.

| per CA frame (= per command) | flat (no blur) | blur, rendered (≥12 passes) | blur refused (command aborted) |
|---|---|---|---|
| fresh frames/s | **60.3** | **16.3** | 20.5 commands/s; panel shows 8.6–12.3 fps |
| kernel jobs | **1.0** | **26.4** | 48.5 prepared, not run |
| render passes / draws | 1 / 6.8 | 15.1 / 74.6 | 14.5 / 27–46 |
| display-size passes | 1.0 | 4.0 | 4.2 |
| pixel work, in full screens of 1125x2436 | 1.0 | **11.25** (~31 MP) | not run |
| total execute time (serial) | **0.77 ms** | **34.2 ms** | 12.1 ms |
| — GPU running | 0.39 | **12.2** | 3.8 |
| — kernel overhead per job (total) | 0.13 | **2.8** | 0.8 |
| — CPU preparing and submitting jobs | 0.25 | **19.2** | 7.5 |

Fixed cost per kernel job, in µs, for blur windows vs flat:

| | blur | flat |
|---|---|---|
| queue (includes pre-job cache clean) | 54 | 76 |
| cache clean | 47 | 67 |
| cache invalidate | 41 | 57 |
| submit to wait | 87 | 117 |
| kernel overhead per job (return minus GPU run) | 113 | 149 |
| CPU preparing and submitting, per job | ~727 | ~500 |

Fitted over 307 heavy windows, the submit CPU is 0.27 ms per job plus 0.153 ms per draw.

- **Refused blur pyramids:** the refused commands had 50–155 jobs each (median 65). The refused pyramid levels are tiny (176x24, 88x12, 44x6), so their cost is the number of jobs, not the pixels.
- **Failures are under-logged:** only 16 command failures are printed per A/B phase. Windows with ≥35 jobs per frame but under 0.45 ms of execute per job are almost certainly silent failures too.
- **No "blur fails → 60 fps" switch in these logs:**
  - The 60 fps windows are flat: 1 job, 1 pass, 7 draws, with no failures in that A/B phase.
  - The windows where blur commands failed delivered only 8.6–16 fps.
  - The blur vs no-blur costs above still hold, and the user's smooth no-blur switcher matches the flat case.

### 0.2 Switcher with blur drawn every frame [E]

I assumed about 48 kernel jobs, about 15 passes, about 50 draws, and about 11 screens of pixel work (the same as the rendered blur frames).

| part | ms |
|---|---|
| GPU running | 12.1 |
| kernel overhead per job | 5.4 |
| CPU work per job | 13.0 |
| CPU work per draw | 7.7 |
| **total execute time** | **≈38–43** |
| our encode on CA's thread | 4–6 |
| CA's own CPU | 10–18 |
| **frame** | **≈52–60 ms, about 17–19 fps** |

The rendered blur windows measured 16.3 fps, which matches. So fixing the refusals alone takes the switcher from 60 fps without blur to about 17 fps with blur.

Which fix gets the blur frame under 16.7 ms:
- **Pixel work or bandwidth: not on its own.** The GPU part is about 12 ms, which already fits the budget, and the GPU is idle 80% of the time.
- **Removing per-job overhead alone: no.** The frame would still be about 34 ms.
- **Cutting jobs from 48 to about 15 alone: no.** The frame would still be about 40 ms.
- **What does it:**
  - Submit the whole command at once and wait once. Then CPU and GPU overlap, and the frame becomes the longest of CA's thread, the submit CPU and the GPU.
  - Cut jobs per command to about one per pass.
  - Run CA and our threads on fast cores, so CA's own thread is under about 14 ms.
- **Ranking:** the dominant cost is number of jobs × CPU cost per job and per draw (26 of the 38 ms). Pixel work only becomes the limit at about 12 ms, once the rest is fixed.

## 1. Delivery [M]

Delivery = fresh kernel presents ÷ (60 × window length), over windows of at least 5 s with at least 10 presents.

| run | flat: p50 fps (delivery) | heavy: pooled fps, p10/p50/p90 (delivery) | blur refused | swaps dropped/s |
|---|---|---|---|---|
| ui60 C6o | 60 (0.69) | 15.3, 8.5/17.1/25.4 (**0.26**) | 8.6 (0.14) | 0.2–0.5 |
| cpu C6o | 49 (0.81) | 16.7, 8.9/19.4/27.7 (**0.28**) | 12.3 (0.21) | 0.6–1.5 |
| C6n | – | 18.1, 7.7/18.6/26.0 (0.30) | 11.3 (0.19), from memory-handle cap and pass:92 failures | 0.5–0.7 |

- **Cross-check:** every command was a display present. Commands = presents + swaps dropped: ui60 8079 vs 7875 + 214, cpu run 5285 vs 5236 + 208.
- **No presents waited for the GPU:** `busy_skips=0` and `busy_forced=0` throughout. CA simply produces too few frames.

Per gesture, from the touch log (fps / execute ms per frame), ui60 C6o vs C6n:
- **App launch and close cycles:** 21.5 / 20.1 vs 24.3 / 18.0
- **Horizontal swipes (switcher or pages):** 23.6 / 22.1 vs 20.0 / 19.4
- **Pull-downs from the top:** 14.6 / 24.7 vs 14.5 / 32.0
- **Flat scene (ui60):** 60.3 fps at 0.77 ms

## 2. Per-frame budget, heavy windows (ui60 C6o)

| ms per frame | all heavy (19 fps, 52 ms) | sustained heavy (30 fps, 33.6 ms) | blur (16 fps, 61 ms) |
|---|---|---|---|
| total execute time, serial | 24.6 | 19.5 | 34.2 |
| — GPU running | 8.8 | 6.6 | 12.2 |
| — kernel overhead (about 0.11 ms per job) | 2.0 | 1.5 | 2.8 |
| — CPU preparing and submitting jobs | 13.8 | 11.4 | 19.2 |
| — of which CPU blits, mipmaps, YCbCr shadow decode | 0.36 | ~0.4 | ~0.5 |
| wait from commit to execute start | 0.7 | 0.8 | 0.3 |
| our encode on CA's thread (0.076–0.114 ms per draw) | 4.3–6.5 | 3.4–5.1 | 5.7–8.5 |
| CA's own CPU [E, the time left over; includes idle between gestures] | ~21 | ~8–10 | ~18 |
| present copy (kernel CPU) | 2.7 average, up to 56 ms; 8 windows over 10 ms | | |
| panel transfer (DSI) | 10.37, constant | | |
| GPU busy / execute thread busy | 0.17 / 0.47 | 0.20 / 0.58 | 0.20 / 0.56 |

- **Pipeline compiles (hitches, not steady state) [M]:**
  - Cold ui60 run, after the UI came up: 120 misses, 13.75 s compiling. 91 were over 16.7 ms, 36 over 100 ms, the longest 2.63 s. Draw-time variant builds took another 8.5 s.
  - Warm cpu run, with 110 disk-cache hits: 21 misses, 3.5 s.
  - Steady-state frame times are the same in both runs.
- **Memory pressure:** none in C6o. C6n had 17 commands fail at the 256-buffer cap (error 0xe00002d8, `bos=256`), which C6o fixed.
- **Command failures:**
  - C6o: blur-pyramid refusals at `nord2_render_pass:153/163`, 64 logged (logging is capped).
  - C6n: 20 failures at pass:92 (mip levels).
  - Also: 8 refused format-550 textures in backboardd (not drawn, a correctness issue), and 3 refused half-float IOSurface targets in SpringBoard.
  - Photos was killed in each run (0x8badf00d, "failed to terminate gracefully after 5.0s").
- **Not logged separately:** texture uploads and IOSurface copies are inside the submit CPU time. The `NORD2 SCHED` statistics read 0% everywhere, so they can't tell which cores CA runs on.

**What is serial and what overlaps:**
- **Serial within a frame:**
  - CA prepares the frame (our encode runs inside), then commits.
  - Our executor then runs each job in turn: CPU prep, submit, GPU run, wait for completion, next job. The evidence is one wait per job (`waits=n`), about 87 µs from submit to wait, and `seqw=0`.
  - The early "scheduled" signal never fired (`early_wait=0` in every window), so CA waits until the GPU finishes.
  - Consistency check [E]: if CA overlapped execution, a 19.5 ms execute would allow about 50 fps. Measured is 30 fps, which fits execute + encode + about 8 ms of CA in series.
- **Overlapping:** only the kernel present thread (copy 2.7 ms + DSI transfer 10.4 ms) runs alongside the next frame. It adds latency (about 21 ms after the swap), not lost frames.

**Critical path of a sustained heavy frame (33.6 ms):**

| step | ms |
|---|---|
| CA layer and render CPU [E] | 8–10 |
| our encode | 3.4–5 |
| commit wait | 0.8 |
| 14 job round trips: CPU 11.4 + GPU 6.6 + kernel 1.5 | 19.5 |
| swap | – |

CPU is 27 of the 33.6 ms (80%).

### Ranked fixes [E]

Gains are from the serial model above and don't simply add up.

| # | Fix | Owner | Sustained heavy (now 33.6 ms, 30 fps) | Blur frames (now 61 ms, 16 fps) |
|---|---|---|---|---|
| 1 | Submit the whole command at once, wait once, report "scheduled" at submission, allow 2 frames in flight | render jobs + kernel wait | −17 to −20 ms → 55–60 fps | −34 ms → ~37 fps, then limited by CA's thread |
| 2 | Get CPU frequency scaling working (big cores stuck at 730 MHz) and put CA, our submit/finish threads and the present thread on A78s at 2.0–2.6 GHz; assumes CPU work is 2.5× faster | kernel CPU DVFS/scheduler | −16 ms (~57 fps); with fix 1, 60 fps | −29 ms (~31 fps); with fix 1, ~60 fps (GPU-bound at ~12 ms) |
| 3 | About one kernel job per pass; batch the blur pyramid levels | render jobs | −3 to −3.5 ms | −4.3 to −5 ms; switcher at 48→15 jobs, −12.6 ms |
| 4 | Halve the per-draw submit CPU (0.153 ms/draw; check reads from write-combined memory, which ansuz found slow) | render jobs | −3.5 ms | −5.7 ms |
| 5 | Halve draw-time encode and plan lookups | compile/encode | −1.5 to −2.5 ms | −2 to −3 ms |
| 6 | Less pixel work (limit backdrop passes to their area) and raise the GPU clock from 654 MHz | render jobs / kernel GPU DVFS | −1.7 to −2.5 ms | −3 to −4 ms; the ~12 ms floor |
| 7 | Ship prewarmed pipelines | compile | no fps change; removes 91 stalls >16.7 ms per cold session | same |
| 8 | Present on swap, and let the GPU scale into the scan-out buffer instead of the CPU copy | present path | no fps change below 60; latency −5 to −8 ms; removes copy spikes | same |
| 9 | Fix the blur-pyramid refusals | render jobs | correctness only | without fixes 1–3, switcher frames become ~50 ms |

- **Combinations:** fixes 1 + 2 bring sustained heavy and blur frames to about 60 fps. Fixes 1 + 3 + 4 without the CPU fix leave blur at about 35–40 fps, limited by CA's own thread on slow cores.
- **120 Hz with a TE lead** (ansuz's approach) isn't reachable while one panel transfer takes 10.4 ms.

## 3. What ansuz-metal does that we don't

ansuz's 0.9–1.0 figures are mostly from its GLES path: vidofnir home 0.95–0.96, ansuz at 120 Hz 0.96–1.00. Its Metal path reached 0.94 on vidofnir home, 0.86–0.88 fresh on app launch and 0.64–0.85 in Control Center. On ansuz itself, Metal is at 0.05–0.18. Their Metal frames were limited by the GPU (96–98% busy); ours are limited by the CPU and by serial execution (GPU 15–20% busy on a bigger Mali-G77 MC9).

Structural differences, most important first:
1. **One submission per frame.** Each command buffer goes to the GPU in a single submission with exact completion, instead of one CPU round trip per job.
2. **CA throttle at depth 2,** so CA prepares the next frame while the GPU draws this one. Home went from 0.26–0.35 to 0.70, Control Center from 0.50–0.60 to 0.69.
3. **CA on the big cores at real clocks.**
   - `sched_mode 0xf` puts the render thread 96–98% on the big cores and halves its CPU time. Delivery went from 0.80/0.63/0.70/0.72 to 0.91/0.84/0.88/0.86.
   - A CPU frequency governor with a UI floor: A78 2.0 GHz, A55 1.5 GHz.
4. **GPU frequency governor.** Their lowest GPU clock cost them 10–16 points.
5. **App-launch snapshots and mipmaps on the GPU.** The YCbCr snapshot is converted once per change (launch zoom went from 15 fps to 0.74–0.88 fresh), and mipmaps are render passes. Ours do this on the CPU inside the serial execute: small today (0.3–0.6 ms per frame), but on the critical path.
6. **Pipelines recorded and prewarmed** before first use.
7. **Present path:** 120 Hz with a 2 ms TE lead, present on swap, GPU-scaled flips. They also saw the CPU copy slow from 3.4 to 11–12 ms under GPU load; our 30–56 ms copy spikes look similar.

The decisive ones for us are 1–3.

## 4. C6o vs C6n [M]

Delivery is the same: 0.29–0.30 on C6o vs 0.27 on C6n, and launch cycles run at 18–21 vs 24 fps. Heavy frames use 16–17 vs 13 jobs per frame. C6o removed the memory-handle-cap failures, but frame cost and delivery are set by serial job execution and the slow CPU, not by those fixes.

## 5. Method and limits

- **Parsing:** lines are split at the `|proc|` tags and the `NORD2 ` prefixes. A segment that was interrupted loses its last key=value, since the console may have cut it at about 256 bytes.
- **Clock:** SpringBoard's NSLog timestamps (1 s resolution), interpolated by line. Windows without a timestamp count as 5 s, so their fps is an upper bound.
- **Log caps:** failures (16 per A/B phase) and refusals (16) are capped, and the encode records stop near the end of ui60.
- **Pooling:** the A/B phases inside each run are pooled.
- **CA's own CPU** isn't logged, so it is an estimate from the time left over.

Files are in `$NORD2_WORK/gpu/build/lag-analysis-20261028/`. Run each script with `nice -n 10 python3 -I`.
- **Scripts:** `lagparse.py`, `windows.py`, `perfwin.py`, `gestures.py`, `classify_fit.py`, `delivery.py`
- **Data:** `*-windows.{json,txt}`, `*-perf.{json,csv}`, `*-gestures.{json,txt}`, `classes-fits.{json,txt}`, `delivery.{json,txt}`, `blur-split.{json,txt}`, `scenarios.txt`, `pass-sizes.txt`
- **Report:** the text above should go into `REPORT.md` in that directory; I couldn't create the file.