---
name: nord2-phone-test-procedure
description: "Pitfalls when running Nord2 phone tests (txn, log readers, display auto-lock, boot guards) learned 2026-11-13"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-13T05:19:23.386Z
---

Nord2 phone test procedure (learned during the 2026-11-13 overnight session):

- Run `phone.py reset fastboot` BEFORE `autocycle/txn_g.sh`; otherwise txn sits in its start loop forever (cost 16 min once).
- Use ONE log reader at a time (txn capture or `phone.py logs`); two readers split the stream and make PACE stats look empty. `phone.py logs` also replays the ring from the start.
- iOS auto-locks / turns the display off between test steps; touches do not wake an off screen. Use `phone.py wake` + swipe up + `home`, then keep interacting continuously. Never `phone.py unlock` with the display on (power key turns it off).
- Screenshots are black in HW overlay modes (kern.n2fbhw>=2); use payload-fbhwmode0 for visual checks.
- GPU cold-start OPP above 654 MHz (`n2mhz=805`) is refused when battery > 38 C (falls back to 350 MHz); cmdline limit 607 chars.
- build_q10jfi.sh needs an absolute OUTDIR, WARN_BUDGET=8 for the 0398 warnings, and no duplicate NNNN patch dirs in EXTRA_PATCH_DIRS.

**Why:** each of these wasted a test cycle overnight.
**How to apply:** follow before any new phone test on this project; see [[nord2-delegate-analysis-to-fable]] for analysis delegation.
