I'll start by reading the constraints, the evidence, and the current code.

Now the evidence snapshot.

Let me read the core hardware files fully.

Reviewer findings already exist; let me read them alongside the hardware worker.

Both reviewers converge on the same ownership picture. Now the rest of the canonical hardware stack.

The gate logic is clear. Now let me verify the key source claims in the Android tree, starting with the power-domain and clock code.

PG_AUDIO has no PG parent and no ADSP dependency — confirmed. Now the AFE clock driver.

Android's runtime-resume reparents `audio_sel`/`aud_intbus` to 26 MHz. Let me verify the mux tables and update bits in `clk-mt6885.c`.

`set_parent_setclr` only writes on change and has no completion handshake. Let me check what consumes `audio_h` and the audsys gates.

`audio_h` feeds only hires DAC/ADC gates, not I2S3. But `aud_i2s3_bclk` (AUDIO_TOP_CON1 bit6) is a gate the current plan never touches — let me check how Android handles it.

Android never touches AUDIO_TOP_CON1 I2S BCLK gates (not in the AFE clock list), so not touching them matches the source. Now let me verify the AFE register fields the plan uses.

I2S3 config matches the source. Now the ADSP bit definitions so I can define a precise "stable on" shape.

ADSP `0x224d` is exactly the source's fully-on end state (RST_B|ON|ON_2ND|ISOINT_B|SLEEP_B|SLEEP_B_ACK). Now the amplifier, board and native layers.

Critical downstream check: the init gate requires AMPE==0 *at probe time right after reset*. Let me verify TFA9874 power-on defaults from the source.

The I2C model assumes amp1 reads `0x0019` (PWDN|AMPE|DCA) at probe. If TFA9874 POR has AMPE=1, the post-probe gate (`ampe==0`) would block init again. Let me look for POR evidence for register 0x00.

Bitfields check out (CLKS 0x11.7, NOCLK 0x10.7, TDMERR 0x10.6, MANSCONF 0x01.2, AUDFS 0x02[3:0]). Now the native MMIO layer — any address the new plan touches must be mapped and allowlisted there.

Native writes are restricted to `naf_write_allowed()` plus the reset/clock special cases, so any new plan must stay within that allowlist. Now the worker lifecycle callers.

Worker lifecycle confirms frequent stop/restart (750 ms idle), so post-stop state must be a valid baseline for the next start. Let me check one suspicious I2C timing detail before moving on.

Source does the same `timing | I2C_TIMEOUT_EN` (i2c-mtk.c:1051), so that matches. Now the existing tests, which I'll need to extend and whose old gate=0xf8 expectations will change.

Let me see the hardware reviewer's reconstructed fixture so my regression uses the same 69 words.

That's too broad; the prior PIN-AUDIT covers the board-specific mapping. Let me check its conclusions rather than re-derive.

DTS `oplus6893_20827_audio_extend.dtsi:12-25` confirms 0x34↔GPIO111 and 0x37↔GPIO148. Let me skim the board-profile apply path for any state assumptions that the inherited boot could violate.

I have the design settled. Before writing, let me check what the Python tests and other harness pieces reference so my candidate runner can build an overlay tree correctly.

I have everything needed. Writing the candidate, starting with `audio_afe.h` (ownership record, revised gate semantics).

An enum constant of `1u<<31` isn't representable as `int` in C11; I'll switch the ownership mask bits to `#define`s.

Now `audio_afe.c` — classification, inherited-aware up/down plans, cold path preserved byte-for-byte in behavior.

Let me make sure no existing test asserts the now-retired PROTECT/INFRA refusals or old ADSP semantics.

Now `audio_hw.h`/`audio_hw.c`: ownership summary, distinct amp-evidence error, and I2C failure context in status — all saved-state only, no new MMIO.

Now `audio_device.c` — only the saved-state diagnostic line and one new `OWN` log line; behavior untouched.

Now the regression harness. First the actual-snapshot fixture (identical to the 69 logged words, which I cross-checked against `init.log:1093-1161`).

Now the main regression: real snapshot through the actual `nau_hw_*` lifecycle with source-route assertions, restoration checks and fault injection.

Small trees — the overlay can copy `shim/nord2-xnu`, `tests/` and `tools/nord2-audio-*.py`. Writing the plan-dump tool:

Now the updated check tool so the new regressions become part of the normal suite after integration:

Now the top-level runner that overlays the candidate onto a temporary copy, runs the full suite, and diffs canonical-vs-candidate plans:

Citations confirmed (`adsp_parents` at clk-mt6885.c:1203-1211 has no APLL2; APLL2 tuner_en is AP_PLL_CON3 bit5 at :2533). Writing FINDINGS.md.

Let me verify the one citation I inherited rather than read myself (`tfa98xx.c:4740-4749` reset pulse).

Two `tfa98xx.c` copies with different reset timing (src/ uses 10 ms). I need to know which one the Nord2 build compiles.

The built `tfa98xx.c` (Makefile:26) is the top-level one with the 5 ms pulse at :4740-4749, so the canonical citation is valid (the `src/` copy with 10 ms isn't compiled). Now FINAL.md.

Since nothing could be compiled, let me do a careful desk-check read of the main candidate source for errors.

Two small hardening fixes from the desk-check: a local variable in `classify()` shadows the fixture function `cold()`, and plan-dump should cast narrow fields for `%u`/`%x`.

One clarifying note for FINDINGS: the built TFA driver is the top-level file (the `src/` copy with a 10 ms pulse is not compiled).

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
