# Final integration audit and narrowly demonstrated corrections

Canonical audio_hw/AFE/amp/device code was read together with the selected
Android driver and actual parent2ms worker loop. The inherited-I2S3 correction
is already canonical. This audit did not widen pin, power, mux or ownership
preconditions and did not add an unsupported9873 board/playback route.

Apply ONLY these new patches against current canonical:

- audio_hw.c.integration-audit.patch
- audio_hw.h.integration-audit.patch

The header patch adds only last_progress_us/pointer_waiting. It preserves the
parent's current ADT-provenance comment. Do not overwrite the canonical header
with an older private whole-file copy.

Demonstrated issues and corrections:

1. The old stall counter advanced only for individual tick gaps>=4ms; the actual
   worker runs every2ms. A frozen in-range DMA pointer therefore never timed
   out under the real cadence. A focused2ms test failed before correction.
   The new watchdog measures12ms since actual pointer progress.
2. The selected Android mtk-afe-platform-driver.c:105–110 treats CUR0 as a zero
   PCM position. An immediate first read of0 previously stopped with POINTER.
   Startup now leaves the already-zero ring untouched while awaiting the first
   valid pointer for at most12ms. A later zero/out-of-range pointer still fails.
3. Selected tfa98xx.c:956–957 explicitly clears historical NOCLK sticky state
   after cold start. Our first runtime monitor previously misclassified it as
   a new fault. A clear-on-read model with CLKS ready and historicalNOCLK failed
   before correction. Startup now reads once for that baseline, rejects TDMERR,
   then verifies current NOCLK and CLKS before AMPE. Persistent NOCLK and TDM
   errors remain rejected before either output is enabled.
4. A preflight refusal can set bus.offline without bus.uncertain because no
   controller writes occurred. Old stop skipped the offline amps and reported
   a clean stop even though AMPE could remain latched. A gated-controller fault
   test reproduced this. Unverified amp disable now records a stop error and
   holds only GPIO111/148 reset high before DL1 stops. No I2C transaction occurs
   after that preflight failure; DDR/UFS devices remain untouched.
5. The9873 read-only monitor's source NOCLK field is bit8, not bit11. Its mask
   is corrected from0xc00 to0x500. The pinned board still refuses9873 audible
   starts; a silent-AFE monitor unit branch checks this mask independently.
6. Initial zero-ring setup/amp startup delay is excluded from max_tick_gap_us,
   so that metric measures actual active-stream cadence rather than startup.

Focused test files (no raw firmware is embedded): test-stream.c, test-full.c,
test-i2c.c and test-i2c-model.h. test-full requires the exact private4163B pinned
container as an argv file; do not commit that blob. It loads the generic runtime
board decoder. Existing full lifecycle tests remain: three successful stream
cycles including>2s continuous traffic, idle shutdown, partial startup refusal,
source profile/TDM setup and post-AMPE bus-error reset isolation.

Verification:

- verify.py:3 ASan/UBSan suites,7 normal NORD2 ARM64 translation units.
- verify-patched-canonical.py: copies ONLY current canonical audio source files
  into verification/patched-canonical, applies ONLY the two patches there,
  and runs the same three sanitized suites plus seven normal ARM64 units,
  including the parent's current audio_device.c with queue-race/log/diagnostic
  changes. This passed. Results are in that directory's results.json.
- Full lifecycle model main run has2033 amp transfers; additional focused
  startup/current-fault/preflight/9873-monitor branches also pass.

The parent worker review found no additional concrete issue after its queue
freshness and active-stream console-log corrections. The source matches FE
S16 packing with the selected BE32-bit I2S fixup; no guessed TDM/POR changes
were made. All hardware polls remain bounded. No hard-realtime scheduling
policy was invented for the PIO startup/monitor path. Models/compiles establish
software behavior, not actual sound, clocks or speaker safety performance.

No canonical files, CI, USB, device or flash operations were changed here.
