---
name: nord2-night-mandate-20261115
description: "User's second full-autonomy night (2026-11-15 ~00:40): phone back in fastboot, run perf sessions autonomously, complete report due 08:00"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-14T23:38:40.117Z
---

On 2026-11-15 ~00:40 the user reconnected the phone in fastboot and went to sleep: "gestisci tutto tu autonomamente, hai pieni poteri; per le 8:00 devi darmi il rapporto completo." Same rules as [[nord2-night-mandate-20261114]]: only Opus 5.5 agents (never Fable), phone and PC fully authorised. The plan is the runbook gpu/build/RUNBOOK_next_session.md (sessions 1-6: q10jfinal2f + gfxall5, perf3, perf4, MDP probe, RAM, shader seed); the user cannot reproduce scenarios while asleep, so drive the phone with autotest/phone.py (launch app, tap, swipe, screenshot, logs).

**Why:** user wants the performance/GPU/RAM/MDP work validated on hardware overnight.
**How to apply:** run sessions sequentially with unique txn tags, keep a safe fallback image (q10jfinal1c / q10jall3 / q10jfin), restore a working kernel before the morning if possible, write the full report into NOTES.md and the final answer by 08:00; see [[nord2-gnss-working]] and [[nord2-haptic-final-6a]] for the features that must keep working.
