---
name: nord2-haptic-final-6a
description: "Nord2 haptic feel resolved 2026-11-14 with kernel q10hapd6a (short tick, fixed gain 0x57); status bar sbshift2 +22px also confirmed"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-14T10:03:09.942Z
---

On 2026-11-14 the user confirmed "il feedback aptico è perfetto" with kernel q10hapd6a (gpu/build/device-mmu-q10hapd6a-candidate/..., sha256 5d2cf34f...): AW8697 RAM waveform, every tap = 11.8 ms tick at FIXED gain 0x57. Earlier variants were "zanzarosa": CONT mode, long clicks (23.6/35.4 ms) and high gain (0.85) all buzzed. Status bar sbshift2 (+22 px) also confirmed OK by the user.

**Why:** the dry keyboard-like click needs a short, low-amplitude tick; strong amplitude rings like a mosquito.
**How to apply:** when flashing other kernels (GNSS tests use images without 0611+), restore q10hapd6a afterwards; any future combined image must carry 0610/0611/0613/0614/0616/0618(6a). See [[nord2-night-mandate-20261114]] for the phone-ownership context.
