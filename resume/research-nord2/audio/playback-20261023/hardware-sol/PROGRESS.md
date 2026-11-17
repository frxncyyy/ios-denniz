# Hardware candidate completion — 2026-10-23

Written and tested privately; no canonical edits, device, USB, flash, CI or git
commands were used. This replaces the initial missing-container uncertainty:
profile-sol recovered the matching-address Nord2 tfa98xx.cnt (635300fb…), and
provides a private ADT blob plus a generic SHA-gated runtime decoder. Hardware
transport now delegates profile register/masked-change policy to that decoder.
No proprietary profile operand table is needed in the promoted hardware source.

Complete hardware path implemented: cold SPM AUDIO power-up; infrastructure/AFE
clocks; actual cold APLL2 postdivider, PCW and tuner programming with readback;
DL1 stereo S16 SRAM DMA; I2S3 32-bit serial slots; exact revision TFA silicon
initialization; board profile; AUDFS48k; stable clock check; AMPE; continuous
ring refill; amp monitoring/mute/powerdown; source teardown and snapshot verify.
Initial tests can be <=2s. duration_us=0 supports HAL streaming with a 750ms
producer-idle watchdog. All MMIO is confined to the single owning audio worker.

Cold I2C8 clock gating is handled with source clock-enable writes preserving the
inherited global parent. The shared global I2C mux is retained on if enabled by
this component: gating it later could interrupt another I2C controller without
global CCF refcounts. The exclusive I2C8 gate is restored after clean stop. No
controller-wide reset, RESUME_ARBIT or bitbang; no slaves except0x34/0x37.

Reset GPIO111/148 must already be GPIO mode. Original direction/latch snapshots
are preserved; source high5ms/low5ms pulses follow cold AFE + idle-bus checks,
and GPIO fields restore on stop/failure. No takeover of a non-GPIO peripheral.

Verification: verify.py passes three ASan/UBSan host suites and seven normal
NORD2 ARM64 translation units. Full modeled path uses1595 amp transfers across three lifecycles,
>2s of ongoing PCM, then silence+750ms idle-stop, without touching rail bucks or
MTP programming commands. Results and compile commands are in verification/.
Models establish software state/order/bounds, not actual sound or electrical
behavior. No hardware test is claimed. Parent owns integration, exact image
review/authorization and any subsequent device activity.

Post-freeze follow-up: localized amp transport fail-safe implemented in
hardware.c/h (actual filenames audio_hw.c/h). An uncertain I2C transfer now
asserts both known-owned local amp reset GPIOs, verifies output/high, records
amp_reset_held1, then stops digital DMA/clocks. It retains those reset outputs
only in the latched error state; normal teardown still restores original GPIOs.
Updated full host suite has2009 amp transfers, three successful lifecycles plus
live bus-fault injection with AMPE latched. Its MMIO callback asserts reset is
already high before the first DL1-disable write. No next-amp/buck transaction
occurs after the fault. All three sanitized suites and seven ARM64 units pass.

Read-only parent audio_device.c review: periodic printf while streaming could
block on console output beyond the ring headroom. Private
`audio-device-periodic-log.patch` defers only that periodic log until idle;
`audio_device.review.c` compiled with normal NORD2 ARM64 flags successfully.
No hard realtime policy was guessed: default kernel worker already has kernel
priority, and source PIO initialization includes bounded busy delays. Actual
cadence/latency should be measured before assigning a hard realtime budget.
