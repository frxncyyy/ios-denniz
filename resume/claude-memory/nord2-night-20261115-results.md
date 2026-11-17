---
name: nord2-night-20261115-results
description: "Result of the 2026-11-15 night: final kernel q10jfinal8a + userland gfxall11-seeded on the phone, what works, what is open"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-15T05:33:47.738Z
---

Night of 2026-11-15 (user asleep, full autonomy, rule: screenshot before every tap). Phone left on **kernel q10jfinal8a** (sha 69f0991e..., gpu/build/device-mmu-q10jfinal8a-candidate) + **userland gfxall11-seeded** (txn: `VARIANT=gfxall11-seeded READS=600 gfx200-20261114/shipall/txn_gfxall.sh KERNEL_IMG TAG`, fresh tag each time).

Verified on the phone: Maps stress 0 GPU faults; Magnifier ~80 presents/s; App Library blur fixed (SpringBoard snapshot swap refused by gfxall10/11); MDP hardware works (copy, rotate, NV12->BGRA 126 us/tile, single-tile scale; S1+S2 PASS); RAM8 pool +585 MiB ready; shader cache seeded (miss=0).

Open: iOS does not use the MDP yet (service 0672 off, S3/M4U1 untested); ram8c1 does not boot; L2 policy 0664/0667 discarded (faults, slower); Safari untestable (no Wi-Fi); full 8 GB not used yet (6412 MiB usable).

**Why:** a lot of A/B bisecting led to these choices; the full report is the last section of work/NOTES.md.
**How to apply:** start from q10jfinal8a + gfxall11-seeded; fallback q10jfinal2f/q10jperf6f + gfxall8; never combine 0664/0667; see [[nord2-night-mandate-20261115]], [[nord2-phone-tap-scenarios-need-screenshot-check]], [[nord2-gnss-working]], [[nord2-haptic-final-6a]].

**Update (user awake, morning 2026-11-15, device clock ~08:10):** the user now asks to finish everything open: (1) MDP: multi-tile scale, RGB-source scale, S3 (M4U1), iOS service so VideoToolbox uses it; (2) full extra RAM (ram8c1 boot failure, safe 0651, DRAM map); (3) FIX SAFARI: with Wi-Fi now connected, apple.com shows white page with only thin grey lines (content tiles not drawn). Agents: userland ab3084b (Safari), MDP a8ae8eee, kernel a95706ec (RAM). Safari lead: MTSupportsDeepColor=YES -> WebKit tiles deep-colour format served wrongly by the kernel MiniIOSurface/webxr alias.
