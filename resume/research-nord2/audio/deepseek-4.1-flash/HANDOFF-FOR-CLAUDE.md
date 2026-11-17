# Handoff for Claude — Nord2 audio revision3 (2026-10-21)

Worker: DeepSeek V4.1 Flash, private source-only session. No hardware, USB,
fastboot, flashing, live MMIO/SMC, CI dispatch, image or third-party message.
Canonical/active sources, `NOTES.md`, `AGENTS.md`, git state and Wi-Fi/UFS/
battery work were not touched. The interactive session is still running for
human inspection on desktop 3.

## 1. Most important result

**Revision2's "actual native-binding compilation" is genuine — I initially
misread it by checking the wrong buildtree.** Its `commands.json` uses
`research/ci-39398655023`, whose exported
`build/xnu.obj/EXPORT_HDRS/pexpert/pexpert/arm64/SWEET.h` **does** define
`NORD2`. The objects confirm it: `audio_id.c.o` 24,152 B with `_nau_run`,
`audio_i2c8.c.o` 27,184 B with `_na8_probe`, and
`sweet_touch.audio-review.c.o` 275,576 B with three `NORD2 AUDIO ID`,
thirteen `NORD2 INPUT` and zero `SWEET TOUCH` strings.

The real trap is `research/ci-39158169212`: its exported `SWEET.h` predates
the NORD2 edit, so compiling against that tree **without regenerating
`EXPORT_HDRS`** silently yields 808-byte empty helpers and the `#else`
sweet_touch branch. Do not use that tree's stale exported headers as compile
evidence. Revision3's primary compile uses `ci-39398655023` (no pre-include,
real 348,800-byte NORD2 `sweet_touch`, 24/28 KiB helpers); a secondary
`ci-39158169212` compile pre-includes the patched source `SWEET.h` as a
workaround.

## 2. Where the work is

Everything new is under
`research/nord2/audio/deepseek-4.1-flash/work/revision3/` (private copy).
The untouched revision2 candidate is also copied to
`.../work/revision2-baseline/` for side-by-side comparison.

| Path | What |
|---|---|
| `revision3/REVIEW.md` | independent review, findings D1–D8, source anchors, residual gaps |
| `revision3/README.md` | scope, modes, changes, validation, integration boundary |
| `revision3/candidate/shim/nord2-xnu/audio_id.{h,c}` | snapshot core (logic unchanged except comments) |
| `revision3/candidate/shim/nord2-xnu/audio_i2c8.{h,c}` | PIO transport: clean NACK, 0x4c HW timeout, I3C refusal |
| `revision3/candidate/shim/nord2-xnu/audio_id_bind.inc` | binding, pure return guard, setup-value allowlist, report |
| `revision3/candidate/tools/fixtures/*` | strict register/slave model + core/bind tests |
| `revision3/candidate/tools/test-nord2-audio-id.py` | host/ARM64 runner |
| `revision3/candidate/patch/*.diff` | complete current integration material |
| `revision3/compile/` | exact commands, objects, controls, result log |
| `revision3/manifest.json`, `provenance.json` | hashes and policy |

## 3. Concrete changes vs revision2

1. **Clean NACK is bounded, not uncertain.** Exact `INTR_STAT == 3`
   (`TRANSAC_COMP|ACKERR`) rechecks START, clears exactly bits 1:0, clears
   the FIFO, restores the 15 saved config words, returns `1`, increments
   `nacks`, and leaves Linux return allowed. The other amp may still be
   probed. This matches `i2c-mtk.c:1326-1330` (source returns `-EREMOTEIO`
   for the multi-channel `TRANSAC_COMP && !BUS_ERR` case without
   `mt_i2c_init_hw`/RESUME_ARBIT). A refused status clear or START still set
   remains uncertain.
2. **V2 HW timeout 0x4c** is saved, computed as `i2c-mtk.c:963-974`
   (`tv1=2*hclk`, `tv2` from post-decrement LTIMING, `DIV_ROUND_UP`) and
   restored. Default 26 MHz/5/100 kHz value is 386.
3. **I3C refusal.** AP DEBUGSTAT `0xe4` bit 15 refuses with failure 21
   instead of entering the source's HFIFO/I3C path (`i2c-mtk.c:1039-1046`).
4. **Binding setup allowlist.** `na8_state.restoring` plus
   `nab_setup_value()` exact-check slave 0x68/0x6e, INTR_MASK 0, CONTROL
   0x3a, LEN 1, TRANSAC 2, DELAY 10, EXT 0x8001, HS 0, IO 3, MCU_INTR 1,
   AUX 2, CLOCK_DIV 0x404; arbitrary saved words only during restore. A core
   bug cannot write 0x51/0x56 into SLAVE_ADDR through this binding.
5. **Tests** add AFE/I2C8/APDMA block bits, ungated-idle APDMA, NACK variants
   (single, first, second, both, sticky clear, START stuck), I3C, 0x4c,
   binding setup rejection, and the no-NORD2 empty-object control.
6. **Complete integration diff** against current
   `research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c`, plus
   `bsd/conf/files` and `tools/nord2-mkpatch.sh`. All Wi-Fi includes/calls
   are preserved; the diff applies cleanly in a temp tree.

## 4. Commands and results (exact)

```sh
cd .../work/revision3
CC=cc    python3 candidate/tools/test-nord2-audio-id.py   # PASS rc=0 (test.log)
CC=clang python3 candidate/tools/test-nord2-audio-id.py   # PASS rc=0 (test-clang.log)
CROSS=1 CLANG=clang python3 candidate/tools/test-nord2-audio-id.py  # PASS rc=0 (test-cross.log)
```

Primary real NORD2 ARM64 compile against `research/ci-39398655023/buildtree`
(the tree revision2 used), with the CI flag set from
`tools/nord2-xnu-cc.py` and no pre-include:

```sh
B=research/ci-39398655023/buildtree
X="-I $B/xnu/bsd/dev -I .../shim/nord2-xnu -I .../work/revision3/compile"
python3 tools/nord2-xnu-cc.py "$B" bsd .../compile/sweet_touch.audio-current.c \
  .../compile/sweet_touch.audio-current.39398655023.o $X
python3 tools/nord2-xnu-cc.py "$B" bsd .../candidate/shim/nord2-xnu/audio_id.c \
  .../compile/audio_id.39398655023.o
python3 tools/nord2-xnu-cc.py "$B" bsd .../candidate/shim/nord2-xnu/audio_i2c8.c \
  .../compile/audio_i2c8.39398655023.o
```

Results: `audio_id.39398655023.o` 24,192 B / 39 helper symbol strings;
`audio_i2c8.39398655023.o` 28,424 B / 17;
`sweet_touch.audio-current.39398655023.o` 348,800 B, 3 `NORD2 AUDIO ID`,
13 `NORD2 INPUT`, references `_nau_run`, `_nau_i2c8_ready`, `_nau_amp_reset`,
`_nau_pin_decode`, `_na8_probe`, `_na8_return_ok`.

Secondary compile against `research/ci-39158169212` needs
`-include .../xnu/pexpert/pexpert/arm64/SWEET.h` because that tree's exported
`SWEET.h` is stale; it yields 348,584 B. Without the pre-include it yields the
68,192 B `#else` object (buildtree control, not a revision2 result). Full
detail in `revision3/compile/commands.json` and `result.log`.

Integration diff test:

```sh
cd /tmp/opencode/revision3-patch-test
patch -p1 < .../revision3/candidate/patch/audio-id-integration.diff
# then diff against compile/sweet_touch.audio-current.c, compile/patch/files.new,
# compile/patch/nord2-mkpatch.sh.new -> byte-identical
```

## 5. Source anchors (short)

- I2C8/APDMA/channels/rails: `dtb/nord2/live.dts:9466-9551`, `:8630-8632`.
- Gate: `clk-mt6885-i2c.c:113-135` (`imps_ap_i2c8_ro` bit 4, `0x11d05e00`).
- Mux/PLL: `clk-mt6885.c:1102-1106, 1567-1569, 1886-1890, 2523-2534`;
  `CLK_CFG_10` lsb 8/pdn 15; main/univ PLL `0x340/0x344/0x34c`,
  `0x308/0x30c/0x314`; /32 and /20.
- V2 offsets: `i2c-mtk.h:252-282` (HW timeout 0x4c, DEBUGSTAT 0xe4,
  FIFO 0xf4, MCU_INTR 0x40).
- Transfer: `i2c-mtk.c:963-974` (HW timeout), `:978-1081` (programming),
  `:1326-1330` (clean NACK), `:1039-1046` (I3C), `:1695-1698`
  (mask-then-read status).
- Pins: `pinctrl-mt6885.c:26-85, 172-291, 293-453, 520-705`.
- AFE/SPM: `clk-mt6885-pg.c:563-568, 3266-3355`; `mt6885-reg.h:2355-2377,
  2564-2565`.
- TFA: `tfa_init.c:807-961`; `tfa98xx.c:5076-5143`;
  `tfa_dsp.c:2682-2711` (`MANSTATE=0x1433` = reg 0x14 bits 6:3).
- NORD2 source: `xnu/pexpert/pexpert/arm64/SWEET.h:18-22`.

## 6. Remaining gaps / blockers

- **No hardware, no audible audio, no device validation.** Mode2 is still a
  private candidate and has **no hardware authorization**. Do not run it.
- The clean NACK is source-consistent but the physical STOP is inferred from
  START clear + exact status; independent review still needed.
- 0x4c value is source-derived, not measured.
- I3C refusal may false-refuse a controller that could still do plain I2C.
- `INTR_MASK=0` is a deliberate deviation from source 0x129 so the diagnostic
  does not assert the controller IRQ; source evidence for status latching is
  the IRQ handler's mask-then-read.
- The transport uses source 100 kHz timing (`EXT_CONF=0x8001`), not Android's
  DT 400 kHz.
- `research/ci-39158169212`'s exported `SWEET.h` is stale (no `NORD2`). The
  parent must regenerate `EXPORT_HDRS` or fix `tools/nord2-xnu-cc.py` include
  order; otherwise future compile evidence against that tree can silently test
  the `#else` branch. Revision2 used `ci-39398655023`, which is patched, and
  its compile was real.
- No AFE/APLL/I2S/DMA/TFA init/tone/HAL work. M2/M3/M4 remain as in
  `AUDIO-REPORT.md` §6.
- The complete diff is review material. Parent must copy the five helpers to
  canonical `shim/nord2-xnu/`, apply the hunks, regenerate 0082, run real CI
  and verify an exact image.

## 7. What not to do

- Do not integrate or boot revision3 mode2 without a separate shared-bus
  review and explicit user authorization. Keep mode1 separate.
- Do not edit canonical sources from this work dir; the parent integrates.
- Do not claim the revision2 compile evidence was real; it was not.
- Do not remove the `restoring`/setup allowlist or the I3C/NACK guards to make
  a test pass.
- Do not write the 0x51/0x56 DDR/UFS bucks, GPIO/IOCFG, clock gates, mux/PLL,
  PMIC, reset or DMA from this diagnostic.
