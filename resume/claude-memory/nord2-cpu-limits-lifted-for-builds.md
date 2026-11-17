---
name: nord2-cpu-limits-lifted-for-builds
description: Nord2/ios-sweet project — user lifted the CLAUDE.md CPU quota for compilations on 2026-11-10
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-10T11:33:37.778Z
---

On 2026-11-10 the user said (Italian): "puoi anche togliere i limiti cpu per le prossime compilazioni" — the CPU limits in the Nord2 work/.claude/CLAUDE.md (nord2-metal.slice, CPUQuota 75% of one core, affinity 0,1, jobs 1) may be dropped for upcoming compilations (kernel builds, image packing).

**Why:** kernel/image builds under the quota were slow while debugging the camera; the limits were written on 2026-10-26 after a thermal shutdown.

**How to apply:** for compile/pack jobs in that project, running outside the slice or with more jobs is allowed. My interpretation: this covers compilations only; the thermal guardian (reduce at 78°C, freeze at 85°C) and the "no phone operations without approval" rule were not mentioned, so keep those unless the user says otherwise.
