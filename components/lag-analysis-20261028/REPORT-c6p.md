I couldn't write `REPORT-c6p.md`: the harness blocks this agent from writing report files. The full report is below. The scripts and data are in `work/gpu/build/lag-analysis-20261028/`, and nothing in production sources or on the phone was touched. **[M]** = measured from the logs, **[E]** = estimate.

1. **First interaction:** it is slow because work that happens once per element runs on CoreAnimation's render thread, synchronously. Pipeline compiles and draw-time variant compiles dominate (often 0.3–4 s per first interaction), followed by CPU decodes of new surfaces (about 150–240 ms each). The disk cache never hit in this run (0 hits), so nothing was saved from earlier use.
2. **Steady state:** heavy frames are now limited by the submit CPU, which still blocks CA's thread for about 16–34 ms per frame. The GPU is close behind at about 19–31 ms per frame, and the big cores are still at 730 MHz.

## Ranked fixes [E]

**A. First interaction (per new element)**

| # | Fix | Owner | Expected gain per first interaction |
|---|---|---|---|
| A1 | Ship prewarmed pipelines for each build, and make the disk cache survive new builds (0 hits / 323 misses in C6p) | compile | −0.3 to −4.1 s; C6p average 480 ms (backboardd + SpringBoard); worst episode 59 misses / 4.06 s |
| A2 | Make draw-time variant compiles truly asynchronous (stand-in, then swap), or prewarm them with the pipelines | compile/encode | −0.1 to −2.4 s; average ~310 ms |
| A3 | Convert new snapshot/XR/YCbCr surfaces on the GPU (as ansuz's `rottenapple_metal_ycbcr` does), not on the CPU inside the submit path | render jobs | −150 to −230 ms per new ≥4 MB surface; seen once to three times per episode |
| A4 | Cache the GPU mapping of each IOSurface across CA's surface re-creation, and time it (168 maps of ~11 MB, 1.8 GB) | kernel + render jobs | unmeasured; 2–20 ms per map [E, weak fit] |
| A5 | Plan-memo cold misses | encode | small: 0.17 ms per miss, 5–14 ms per episode |

**B. Steady-state heavy frames**

| # | Fix | Owner | Expected gain |
|---|---|---|---|
| B1 | Release CA at the hand-off to the submit thread instead of after submission (keep the depth-2 throttle) | render jobs | removes 16–34 ms from CA's thread. Frame becomes the longest of CA, submit and GPU: ~38 → ~50 fps on swipe-up/switcher-type frames, ~15 → ~30 fps on the heaviest frames |
| B2 | Remove GPU waits inside submission: the pending wait before shadow sync (4–7 ms/frame) and mid-command reaps (15–33 per frame in some contexts) | render jobs | −4 to −7 ms/frame; −15 to −25 ms in reap-heavy contexts |
| B3 | Plan-memo offset misses: 96k misses × 0.072 ms on CA's thread | encode | −3 to −7 ms/frame on heavy frames |
| B4 | GPU work: clock 654 → ~850 MHz, fewer full-screen passes and jobs | kernel GPU DVFS / render jobs | −5 to −8 ms; this becomes the limit after B1 |
| B5 | CPU frequency scaling and placing CA and our threads on the A78s (still 730 MHz) | kernel CPU DVFS/scheduler | CA's thread work ×1/2.5, about −10 to −20 ms/frame |
| B6 | Present path: 775 swaps dropped (12.5% of rendered frames wasted), 2325 vsync ticks skipped waiting for the GPU, copy spikes of 33–48 ms. Use GPU-scaled present and throttle on GPU completion | present path | hitch and latency removal; fewer wasted GPU frames |

## Q1. What is paid only on the first interaction

I flagged windows as "first use" when they had a pipeline miss, ≥50 ms of variant compile, fallback draws, or a ≥1.5 MB surface decode with a scheduling stall of ≥90 ms. That gives 22 episodes after the UI came up.

Two episodes are identified by touch [M]:

| Episode | Frames/s | Repeat of the same gesture | Costs |
|---|---|---|---|
| E1, t=18–36 s: first swipe-up and first horizontal swipes | 3.0 (84 commands in 27.6 s) | 30–40 frames/s at t=37–39 s, with 0 misses and ~12 ms of variant compile per window | 26 pipeline misses / 1.40 s; variants 2.42 s, 21 fallback draws; 1.7 MB surface decode; 144 disk-cache misses; plan cold ~12 ms |
| E2, t=47–101 s: first gestures after an idle gap (window at line 9263) | 12.4 (2 frames in 4.7 s at the worst point) | 36–38 frames/s at t=111–129 s, with 0 misses and ~10 ms of variant compile per window | 59 misses / 4.06 s (worst window 47 misses / 3.49 s; 107 ms execute per frame); variants 1.33 s; 5.6 MB decode; one frame held CA for 173 ms |

Totals over the 22 episodes [M]:
- **Pipeline misses:** 10.5 s in backboardd and 1.0 s in SpringBoard. Lookups were 136 memory hits, 349 pack hits and 166 compiles.
- **Draw-time variant compiles:** 6.9 s.
- **CPU surface decodes:** 55 MB. Every decode of ≥4 MB coincides with one command holding CA's thread for 170–236 ms (lines 10163, 11533, 11607, 11885, 11973, 12049, 17243). That is roughly 40 ms per MB [E].
- **GPU maps:** 65 of the run's 168.
- **Plan cold misses:** 1,429 in the run, only ~0.24 s in total.

Where these costs sit:
- Pipeline and variant compiles are inside CA's commit/draw path, so CA's thread blocks [inferred: variant time is part of our draw time].
- Surface decodes run inside the submit path. CA waits for submission to finish (`early` = every command), so CA's thread blocks there too.
- **Cache:** the disk cache had 0 hits in both C6o and C6p, because each new build is cold. In C6o's second boot of the same build there were 110 hits, and pipeline time dropped from 13.8 s to 3.5 s.
- **Comparison with C6o:** pipeline time 13.8 s, variants 8.5 s, shadow 94 MB. So these first-use costs did not get smaller with C6p.

## Q2. Steady heavy frames under async: critical path [M, with the CA share estimated]

`sched_us` (time from commit until CA is released) shows CA is still released only after the CPU submission finishes. So CA's thread time per frame ≈ CA's own CPU + our encode + `sched_us`, and the GPU runs in parallel.

| Context (repeat windows) | Frames/s | Period (ms) | sched (ms) | encode (ms; of which plan) | CA own, residual [E] (ms) | GPU = jobs × run (ms) | GPU busy | Jobs queued in kernel (ms) |
|---|---|---|---|---|---|---|---|---|
| swipe-up / switcher type, t=111–129 | 35–40 | 26 | 16.5 | 3.3–4.1 (1.2–2.1) | ~6 | 19 × 1.0 = ~19 | 0.74–0.76 | 6–7 |
| heavier, t=180–185 (28 jobs, 106–128 draws) | 28 | 35.5 | 18.2 | 7–11 (3.8–7.1) | ~6–9 | 28 × 0.85 = ~24 | 0.6–0.7 | 10–12 |
| heaviest, CC-like signature, t=251–272 (31–43 jobs, 120–173 draws) | 13–17 | 58–73 | 30–44 | 10–14 (6–7.8) | ~15–25 | 31–43 × 0.93 = ~30–40 | 0.44–0.59 | 9–12 |
| reap-bound, t≥287 (15–33 reaps per frame) | 10–27 | 37–100 | 24–45 | 6–14 | – | low; queue ~0.07 ms | 0.1–0.3 | – |

What limits these frames now:
1. **Submit CPU on CA's critical path (sched 16–45 ms).** About 4–7 ms of that is waiting for the previous frame's GPU work (`cpu_ms` per frame is 4–5 ms, against 0.36 in C6o). In the reap-bound contexts, per-command reaps make execution serial again.
2. **The GPU, right behind.** It is 60–76% busy at 654 MHz now that blur renders. Run time per job is ~0.85–1.0 ms (0.5 in C6o), and jobs wait 6–12 ms in the kernel queue.
3. **Plan offset misses in our encode:** 3–8 ms per frame.
4. **CA's own work:** 6–25 ms on slow cores (big cores 730 MHz, scheduler mode 0).

**Present path [M]:**
- Copy 2.8 ms + DSI transfer 10.4 ms.
- 2325 vsync ticks were skipped waiting for the GPU, and 775 swaps were dropped (6185 rendered vs 5533 shown).
- The depth-2 throttle hardly engaged: 7 waits.
- 9 windows had copy spikes of 33–48 ms.

**App launch and Control Center** aren't touch-identified in C6p: touch is only logged until t=125 s and contains only swipe-ups and horizontal swipes. The "heaviest" row is labelled by its signature only.

**Delivery [M]:**
- Heavy windows: 14.2 fps pooled, p50 16.2, p90 27.9 (C6o: p50 17.1).
- Repeated swipe contexts render 35–40 frames/s against 19–30 in C6o, but GPU waits and dropped swaps eat part of that gain.

## Files

Everything is in `$NORD2_WORK/gpu/build/lag-analysis-20261028/`.
- **New script:** `firsts.py` (first-use costs per window).
- **Updated script:** `perfwin.py` now records the async counters and kernel wait counts.
- **C6p data:** `c6p-test-ui60-usb-{perf,windows,gestures,firsts}.*`, `c6p-steady.{json,txt}`, `c6p-first-episodes.txt`, `c6p-onetime-fits.txt`.
- **C6o comparison:** `c6o-test-ui60-usb-firsts.*`.