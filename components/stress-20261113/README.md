# stress-20261113: overnight UI stress + objective verdict (Nord 2, iOS 16.7 port)

Host only. The orchestrator runs it; nothing here was run on the phone while it was being written.

## Files

| file | role |
|---|---|
| `stress_run.py` | One capture window (at most 540 s) of seeded-random, fast, heavy interactions, using autotest/phone.py **in process** (one USB handle, worker thread with a hard deadline per call). Writes `steps.jsonl` (every step has a `log_offset`), checkpoint screenshots and `summary.json`. Writes `PHONE_LOST.txt` and exits with 3 (lost) or 4 (rebooted). Also has `--calibrate`, `--latency` and `--dry-run`. |
| `stress_capture.py` | Console capture for windows 2..N. It continues the kernel's 4 MiB live log where the previous window stopped (or after window 1's `xnu-live.log`) and rewinds the buffer at 1 MiB, using the same compare-and-swap rule as phone.py, so no bytes are lost. It records REBOOT, LOST, BUFFER FULL and rewinds in `console.log.events.jsonl`. It must be the only log reader while it runs. |
| `shotcheck.py` | Pure-PIL Photos-grid integrity check of 1/2-scale screenshots. Detects black cells, stripes/shear, R/B swap and a sheared tab bar. |
| `stress_verdict.py` | Turns console segments, steps and screenshots into a PASS/WARN/FAIL report per requirement and per phase, written to `verdict.txt` and `verdict.json`. |
| `night_lib.sh`, `run_night.sh`, `run_night_resume.sh` | One-command night: flash, then windows, then verdict. Resume after a phone loss. |
| `test/test_stress.py`, `test/synth.py`, `test/fake_phone.py` | Host self-tests: synthetic screenshots and logs, plus a fake phone that simulates the kernel log buffer, rewinds, reboot, loss and a hung gesture. |

## How to run (orchestrator)

```
H=$B/stress-20261113
# 0. once, phone up on the home screen: check the icon positions (Photos/Settings/Subway verified, App Store/Safari/Messages assumed)
python3 $H/stress_run.py /tmp/n2cal --calibrate            # table + calib-home.png / calib-cells.png; fix with --coords / COORDS=coords.json
python3 $H/stress_run.py /tmp/n2cal --latency              # 20 status reads + 10 home swipes: real per-gesture overhead
# 1. the night (pick one flash mode)
PAYLOAD=payload-... GUEST=guest-....sh OWNED=$B/apps-20261104/owned-....json $H/run_night.sh IMG TAG   # txn_g.sh, READS<=600
FLASH_ONLY=1 $H/run_night.sh IMG TAG       # phone.py reset fastboot + to_fastboot.sh + flash_boot.sh IMG, no payload
NOFLASH=1    $H/run_night.sh IMG TAG       # phone already runs IMG
#    env: WINDOWS=40 (about 6.2 h) WIN_MIN=8.5 SEED=1 PLANS="default;default;subway:400,photos:110,idle:20" COORDS=... VERDICT_ARGS=...
# 2. after a phone loss/reboot, once the phone is back (no reflash needed):
$H/run_night_resume.sh TAG
# 3. verdict again / with options:
python3 $H/stress_verdict.py $B/autocycle/runs/TAG/stress [--allow-crash parsecd] [--baseline good.png] [--strict-crash]
# dry runs / self-tests (no phone):
$H/run_night.sh x.img drytag --dry-run     # DRY_BAD=1 faults, DRY_LOSE_WINDOW=2 loss; then run_night_resume.sh drytag --dry-run
python3 $H/test/test_stress.py             # ALL OK, about 3 min
```

Output goes to `$B/autocycle/runs/TAG/`. txn_g.sh writes `ui-usb/` (window 1's console). The harness writes `stress/wNN/` with `steps.jsonl`, `summary.json`, `console.log` (+ `.events.jsonl`), `shots/`, `run.out`, `capture.out`, `PHONE_LOST.txt`. It also writes `stress/night.json` (resume state), `stress/night.events` and `stress/verdict.txt|json`. Exit code: 0 PASS, 1 FAIL, 3 WARN only (or phone lost), 4 rebooted, 2 usage or no data.

## Windows and the console

- Window 1 runs inside txn_g.sh's `nord2-usb-console.py` capture (READS at most 600 s). It starts after the first `NORD2 FB PRESENT` plus `UI_SETTLE` = 20 s and uses what is left of READS (about 6 min).
- Windows 2..N each run `stress_capture.py` (WIN_MIN x 60 + 45 s, at most 600 s) in parallel with `stress_run.py`.
- The real logs are about 3 MB per 10-15 min (run fbhw2). Without rewinds the 4 MiB buffer is full after about one window. stress_capture rewinds at 1 MiB; phone.py logs is never called.
- Between windows there are about 5-10 s without stress. Nothing is lost there: the buffer persists and nobody rewinds it.
- Phone loss: before each window `phone.py wait LOST_WAIT` is run. Inside a window, any failed or over-deadline call triggers a status probe on a fresh handle; with no answer for `--lost-wait` s the result is `PHONE LOST at step N` plus the last 40 console lines. A changed status `boot` id gives `PHONE REBOOTED`. The night stops there; `run_night_resume.sh` continues it. With `CONTINUE_AFTER_REBOOT=1` the night goes on after a reboot.

## Interaction mix (per window; `PLANS` cycles per window)

Default plan, scaled to WIN_MIN:

| phase | seconds | what it does |
|---|---|---|
| photos | 110 | fling bursts in both directions (80-250 ms), tab hopping, pinch out/in, long press followed by a home swipe, leave/return |
| spam | 80 | launch/close of the 6 apps, 0.1-1.2 s apart; 15 % also kill the card in the switcher |
| switcher | 45 | app switcher pans and open/kill |
| ccnc | 40 | Control Center / Notification Center pulls |
| settings | 35 | list flings and edge-back |
| apps | 40 | Safari / Messages / App Store / Settings, scrolling |
| rotate | 35 | bottom-edge app switching |
| subway | 130 | launch, 25 s load, 3 taps to play, then left/right/up/down swipes every 0.25-0.7 s with a restart tap every 15 s |
| idle | 25 | home screenshot, steady state for the leak trend |

- About 950 steps per 9 min in the dry model.
- Each window uses `random.Random(seed*1000+window)`, so a window can be replayed exactly with the same seed and window number (tested).
- **Per-call overhead**: the phone.py CLI costs a median 0.10-0.13 s per call on top of the gesture time (0.25 s at most), measured from run fbhw2's steps.jsonl over 232 calls. The in-process backend removes the interpreter start and libusb init. Measure it with `--latency`.
- A gesture cannot be batched further: one kernel command takes at most 2 fingers (`gesture` with 2 paths is used for pinch). before/after holds are 0-20 ms for flings.

**Safety (the user's own photos and settings).**
- Taps only go to whitelisted targets: home icons, the Photos tab bar, a switcher card, the game centre. There are no taps inside the Photos grid or viewer, Settings rows or App Store.
- Long presses are always followed by a home swipe, never a tap, so a context-menu item (Delete / Remove App) can't be hit.
- Before every tab-bar tap burst, a downward swipe from mid-screen closes a photo viewer if one opened.
- Residual risk: if a photo viewer were open anyway, the "Search" tab position (945,2220) is near the viewer's trash button. iOS then still asks for confirmation (Delete Photo / Cancel), and the next tab tap at y 2220 lands on Cancel. Deleted photos stay in Recently Deleted for 30 days.

## Verdict rules (stress_verdict.py; record formats checked against the 0396/0397/0398 sources and real logs)

| requirement | rule |
|---|---|
| **R1** UI at a fixed 90 fps | FIXED90 of the `NORD2 FB PACE` records of every UI phase (pace_report.py rule: panel 88.5-90.5 Hz, at least 50 active, k1 at least 98 %, active_fps at least 87; WARN if k1 is at least 90 %). The first record of each phase is skipped because it straddles the previous phase. Any `NORD2 FB PANEL: 90 Hz reverted\|fault`, a forced 60 Hz, or `p90_reverts` > 0 is a FAIL. |
| **R2** Subway Surfers | PACE active_fps of at least 60 from the `subway play` event to the end of the phase. CRASH / F CRASH / jetsam of `SubwaySurf` (`--game-proc`) is a FAIL. Game never seen in the log is a WARN. |
| **R3** Photos | FIXED90 of the photos phase, plus screenshot checkpoints (below), plus proxies: `SURF freeze` (WARN), PHOTOS frame sampled black texels and `NORD2 PERF PHOTOS A ... fps=` (info). |
| **R4** no crash/glitch/leak | See the list below. |

R4 in detail:
- **Crashes.** Kernel `CRASH p[pid] sig N` is a FAIL for the tested apps, UI daemons (SpringBoard, backboardd, ...) and any SIGSEGV/SIGBUS. Other daemons are a WARN; sig 9 (switcher kill, launchd, jetsam) is INFO. Userland `F CRASH` is folded into the kernel line that follows it; if no kernel line follows, it is a FAIL.
- **Kills, hangs, resets.** Jetsam `memorystatus: killing_*` of a tested process is a FAIL. `NORD2 HANG: p[pid] main thread stack` is a FAIL, as are panic, reboot, console loss and missing logs.
- **GPU.** `NORD2 GPU JOBS fault>0`, `NORD2 PRFCNT: AS0 fault` and `NORD2 GPU HW: error!=0` are FAILs. Nord2GPU prints no per-event hang/MMU/timeout lines, only these counters.
- **HW overlay.** `AUTO-DISABLE`, `BACKOFF exhausted` and growth of the HW line's `faults` are FAILs; `BACKOFF` alone is a WARN.
- **Surfaces and memory.** `SURF USE` imbalance and `LEAK` are FAILs. A GPU private MiB slope above 60 MiB/h with growth above 150 MiB and r2 above 0.5, over at least 1 h, is a FAIL. Above 20 MiB/h it is a WARN, and over shorter spans only growth counts. FB POOL `full for` is a WARN, as is growth of pool `used`.
- **Stutter evidence (WARN).** `slow draw` / `slow: X ms commit` / blit / completion of 100 ms or more in an active phase, and ALLOC SLOW above 20 ms.

Repeated items are aggregated (first message, count, last message). Records before window 1's `start` event (boot time) are only reported as info. Use `--allow-crash proc,...` for known daemon crash loops (run fbhw2 has a `parsecd sig 5` loop).

## Screenshots and the HW overlay (lowest-risk procedure)

Facts, verified in the 0396/0397/0398 sources:
- `phone.py screenshot` uses autotest command 3, then `sweet_fb_snap`, then `AppleCLCD::snap`, which **always copies the CPU panel buffer**.
- In kern.n2fbhw 2/3 the overlay reads the app surfaces directly for HW frames (`hw_frames`), and the panel buffer keeps the last CPU-path frame. A screenshot is then **stale, not the screen**, and nothing in snap reports it.
- There is **no mirror mode**. `kern.n2fbhwtest` only shows test patterns.
- `NORD2 FB HW: frame N shown by the overlay` is logged only for the first frames; it is not a pixel probe.
- The host cannot set sysctls: the console only takes gestures, keys, snapshots and logs.

How the harness handles it:
1. **Recommended, lowest risk:** run the night (or every other night) at **kern.n2fbhw=0**, the boot default, with no fbhwctl mode job or with `payload-fbhwmode0`. Every screenshot is then the real screen and the grid check is authoritative. The verdict needs no switch: it takes the mode from the log.
2. In modes 2/3 the verdict marks each screenshot **INVALID** when the last `NORD2 FB HW: mode=` record before it is 2 or 3 **and** `hw_frames` grows across the shot. Those pictures are kept for review only; R3 then rests on FIXED90 plus the driver proxies, and the verdict says "none valid". With fbhwctl's `payload-fbhwcycle` (0/1/2/3 x 120 s), the shots taken in its mode-0/1 phases are valid automatically.
3. Kernel follow-up for track C (proposed, not done): set a "HW frame shown" bit in the snap status (info word, read-only, `fHwOn && last frame HW`), so phone.py and stress_run can flag or retry the picture. Later, compose the plan in snap() with `sweet_fb_blend`, as DISPLAY_HW_COMPOSE.md 7.3 requested.

Checkpoint protocol (stress_run.py):
1. A downward swipe (closes a viewer), Library tab, Library tab again (scrolls to the newest photos), then 2 s settle.
2. Two 1/2-scale shots 1 s apart, at the start, middle and end of each photos phase. A home-screen shot is taken in idle.

How shotcheck judges a checkpoint:
- **Baseline**: `--baseline FILE`, or the medoid by symmetric defect score of the first 6 clean, self-consistent, valid checkpoints. A corrupted first checkpoint is outvoted (tested).
- **Comparability**: a picture whose content differs from the baseline (mean |G diff| at 54x120 above 25, e.g. new photos or another scroll position) only gets the baseline-free rules.
- **Persistence**: a defect counts as FAIL only if it is present in **both** pictures of the checkpoint. A defect in one picture only is a WARN (transient).
- **Detectors**: black cell (with non-dark neighbours, and the baseline cell not black); stripes (row-to-row difference spike clusters: at least 3 more than the baseline, or at least 5 without a baseline); R/B swap (cells matching the baseline only after swapping R and B, plus the selected tab tint orange instead of iOS blue, which is the 0399 bug); sheared tab bar (rows of the bar match the baseline while others mismatch badly, each at least 15 %).
- The grid geometry comes from the 1-pt gap lines, in dark or light mode.

## Limits

- Not run on the phone yet.
- Icon positions other than Photos, Settings and Subway (approximately 975,855) are assumptions: run `--calibrate`. The Photos tab x positions (135/405/680/945) assume the iOS 16 four-tab bar (Library and Albums are verified in NOTES).
- The game state is not read from the screen. "Gameplay" is assumed after 25 s plus 3 taps (`game_load_s` in coords). R2 measures the panel's distinct presents (PACE active_fps), not the game's internal frame rate.
- R1/R2/R3 fps need the 0397 PACE lines (fbpace kernel). Without them the verdict reports "no FIXED90 data" as a WARN.
- Userland PERF lines are lower bounds: Nord2GPU's 256 KiB per-connection SEL_LOG budget silences a process after it is used up (NOTES 22:50), and long records are split into 170-byte pieces.
- Stats records come about every 5 s and log offsets resolve to about 0.25-1 s, so phases shorter than about 30 s have little data.
- `pace_report.py` (fbpace-20261112, not mine and not modified) raises on PACE records cut by interleaving (`k=1/2/` gives `float('')`). stress_verdict uses a tolerant parser with the same formula. pace_report itself crashes on such live logs: fix it there.
- The watchdog abandons a hung USB worker thread (daemon) and reopens a handle. At most a few handles leak per night.
- Window 1's capture is txn_g.sh's, without rewinds. If boot plus window 1 exceed 4 MiB before window 2 starts, the tail is lost and the verdict reports `BUFFER FULL` as a WARN.
