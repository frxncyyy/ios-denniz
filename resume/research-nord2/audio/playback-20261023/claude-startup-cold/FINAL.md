I've written a candidate fix for the audio startup failure, with regression tests. Nothing was compiled or run (this session had no shell), so every test PASS message is a claim the parent still has to check. No canonical file, git state, CI, device, USB or network was touched.

**Root cause.** `nau_hw_init` refused at gate 0xf8 in `amps_prepare`, before any I/O. The gate demanded a state where every audio clock is gated, which the phone never has. On the real boot only the AUDIO power domain is off (exactly `0x1112`), and the source power-up sequence needs nothing more. Around it, the boot chain leaves:
- **ADSP:** fully powered (`0x224d`, the source's exact "on" state). It is a separate domain: own protection bit, no parent relation to AUDIO, and no APLL among `adsp_sel`'s parents.
- **AUDIO bus protection:** released. The source power-up sequence has no precondition on it.
- **Infra audio gates and muxes:** open; audio_sel and aud_intbus run on PLL-derived parents.
- **APLL2:** running at exactly 196.608 MHz under the source's own rate formula, with aud_2/engen2 already on their playback parents.

Both reviewers' conclusions hold up against the Android source.

**Fix** (`candidate/shim/nord2-xnu/audio_{afe,hw,device}.*`). The code now reads the boot state and decides, for each shared element, whether to switch it on (and back off on stop) or leave it as found:
- **Never written:** ADSP, the live APLL2 and its two consumers.
- **Source-exact clock route:** audio_sel and aud_intbus are moved to 26 MHz before any AFE access, as Android's `mt6885_afe_enable_clock` does, then put back on stop.
- **Bus protection:** set before power-off as the source requires, then released again once the domain is fully off.
- **Restore check:** every watched field must match the boot snapshot after stop. Any change by another owner before a start is refused with zero writes.
- **Cold path:** unchanged; the old fully-gated fixture produces the identical 73/49-op plan.
- **Diagnostics:** an amplifier-evidence refusal now returns `-5` instead of `-4`. The error line also records the last I2C register/value, an ownership summary and the amplifiers' post-reset status.

**Tests.** `test-inherited.c` runs the real 69 logged words through the actual `nau_hw_*` code:
- three playback cycles, a silent cycle, an idle-driven stop and a restart, each followed by a bit-exact check of all 69 words;
- checks that clocks are on the expected 26 MHz/APLL2 parents at the first AFE access, at I2S3 enable and at DMA start;
- refusals: wrong PLL rate, incoherent ADSP, cold PLL with live consumers, concurrent changes before start;
- injected timeouts: power-up acknowledge and power-off protection acknowledge;
- the AMPE-latched amplifier case.

`plan-dump.c` builds against both canonical and candidate code; `run-candidate.py` requires byte-identical cold plans, the canonical refusal with gate f8, and the candidate's 48/34-op plan for the real snapshot.

**Not proven, and possible blockers on the next boot:**
- **I2C8 controller and APDMA state** were never captured. If any is busy, init stops with `-3`, and the new log fields name the exact register and value. I left these checks in place because the DDR/UFS power rails share that bus.
- **Amplifier revisions and power-on state** are unknown until probed. The board profile accepts only 0a74/0b74/0c74. That AMPE is 0 after reset is inferred from the TFA9872 sibling, not proven.
- **Whether the DMA pointer advances and the amplifiers lock to the clock** can only be seen on the device. The silent test would report a stalled pointer as `-10`.
- **Physical sound** is unverified.

One more thing: the hardware reviewer's harness (`hardware-sol/cold-state/test-inherited.c`) would abort in its `cycles` case for any implementation, because it doesn't seed the bus model's APDMA clock state from the snapshot. My test does. Line-level evidence is in `FINDINGS.md`; proven/inferred/remaining, the expected log lines and the recommended order (silent test, quiet tone, app playback) are in `FINAL.md`.

To verify:
```
python3 -I research/nord2/audio/playback-20261023/claude-startup-cold/candidate/run-candidate.py
```
It needs clang with ASan/UBSan and libcrypto, and writes a receipt under `claude-startup-cold/verification/`.

Everything is in `research/nord2/audio/playback-20261023/claude-startup-cold/`:
- FINDINGS.md
- FINAL.md
- candidate/shim/nord2-xnu/audio_afe.h, audio_afe.c, audio_hw.h, audio_hw.c, audio_device.c
- candidate/tests/nord2-audio/test-inherited.c, actual-snapshot.inc, plan-dump.c
- candidate/tools/nord2-audio-check.py
- candidate/run-candidate.py
