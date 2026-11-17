# Inherited I2S3 pin audit — narrow correction

The old GPIO-mode-only gate concretely rejects MODE32 fields0x1011, including
the source-valid inherited I2S3 state. Whether those are the actual LK handoff
registers is unmeasured. The DT property itself cannot establish current MMIO.

Evidence:
- dtb/nord2/live.dts:1386 gpio_init_default rows32/33/35 each have mode1;
  only these rows are recorded in pin-audit-dt-rows.json.
- No gpio_init_default consumer was found under selected kernel drivers/include.
  Therefore labeling that property Android-only would be unsupported. No claim
  is made that LK actually executed it.
- Selected drivers/pinctrl/mediatek/pinctrl-mtk-mt6885.h:377–418 explicitly maps
  GPIO32/33/35 function1 to I2S3_BCK/LRCK/DO. Functions2/3 select other I2S
  peripherals and functions4/5 include PCM/I2S0; none are newly permitted.
- live.dts:1715–1760 has explicit I2S3 off(mode0 input) and on(mode1) pinctrl
  states; mt6885-afe-gpio.c:237–243 selects these for the I2S3 DAI.

Correction: on each owned pin accept either mode0 plus input direction, or
exact mode1. GPIO direction bits do not drive the GPIO output when the pad is
selected to the I2S peripheral; they are left untouched. All existing cold
AUDIO/ADSP/power/clock/amp guards remain. Source up-plan already selects exactly
mode1 and teardown restores the original mode/pull fields. It does not take over
an alternate function or a GPIO output.

Only audio_afe.c's pin predicate changes functionally; audio_afe.h has a comment
correction. Minimal patches are audio_afe.c.inherited-i2s3.patch and matching.h
patch. Other inherited mux/PLL/active-domain assumptions have not been widened:
there is no new live register evidence establishing those states here.

Verification:
- 32768 combinations of three16-way modes and three direction bits check exact
  acceptance/rejection; a source-valid inherited mode1 fixture is rejected by
  the original canonical source (expected regression recorded separately).
- Active AUDIO continues to fail the cold gate even with inherited I2S3 pins.
- The full2009-transaction model starts from I2S3 mux plus output-direction bits,
  covers three lifecycle cycles and fault isolation, and verifies original pin
  mode/direction restoration on clean stops.
- Three ASan/UBSan suites and seven normal NORD2 ARM64 translation units pass.
  Commands/results: verification/results.json; original rejection:
  verification/pin-regression-before.json.

Canonical files, CI and hardware were not modified by this audit.
