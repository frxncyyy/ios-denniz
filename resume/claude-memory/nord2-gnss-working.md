---
name: nord2-gnss-working
description: "Nord2 GNSS achieved first valid fix 2026-11-14 17:04 with image q10jfin + payload gnssfix22 (root causes: Device-memory unaligned loads, mnl config clock 0 Hz)"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-14T16:04:45.149Z
---

On 2026-11-14 17:04 the Nord2 GNSS gave its first valid fix (GGA q1, 7 sats used, GP12 GL9 GB4) with kernel q10jfin (q10jcz + GNSS 0583-0593 + haptic chain) and payload gnssfix22 (gpu/build/gnss-20261114/pl22). Root causes: (1) pipe read() copied the RX ring (mapped Device) with unaligned vector loads -> silent alignment-fault freeze, fixed by bytewise access (0592); (2) mnl config struct nearly empty: reference clock 0 Hz instead of 26 MHz and empty log/config dirs (n2mnld --cfg-full). The user's hint about the memory allocation type pointed at the right area.

**Why:** the user insisted "il gnss deve obbligatoriamente funzionare"; 20+ phone experiments before this.
**How to apply:** keep q10jfin + gnssfix22 + sbshift2 as the baseline; if a restart of n2mnld happens reboot the phone; remaining: confirm CoreLocation shows the position in apps, L5/LNA tuning. See [[nord2-haptic-final-6a]].
