# Private audio revision3 (Nord2 MT6893, iOS 16)

Status: **PRIVATE SOURCE CANDIDATE. HOST TESTS PASS. ARM64 COMPILE WITH THE
REAL NORD2 BRANCH PASSES. NO HARDWARE, NO AUDIBLE AUDIO, NO MODE2
AUTHORIZATION.** This directory is the only thing changed; canonical sources,
`NOTES.md`, git state, Wi-Fi/UFS/battery work, CI, images and the device were
not touched.

## What this is

Revision3 is a new private copy of `../revision2-baseline` (the completed
revision2 candidate). It keeps revision2's mode contract:

- `nord2audioid` absent/0/other: return before mapping or any callback.
- `nord2audioid=1`: non-mutating snapshot. Reads only allowlisted SPM,
  infracfg, topckgen, apmixed, GPIO/IOCFG, guarded AFE, guarded I2C8
  controller shadow/AP/CCU and guarded APDMA registers. Zero MMIO writes.
- `nord2audioid=2`: the same snapshot, then a **source-derived hardware
  I2Cv2 AP-channel PIO REV probe** only if snapshot readiness succeeds. It
  writes AP-channel controller configuration/FIFO/W1C/START and the target
  register pointer. It does not initialize audio, move samples, make sound,
  take GPIOs, change pinmux, enable gates, touch the PMIC, reset anything or
  use DMA. **Not authorized for hardware. Review mode1 first.**

Snapshot readiness is a captured report, not physical bus ownership or
amplifier identification. The shared I2C8 bus also carries the always-on DDR
(0x51) and UFS (0x56) bucks; the transport never addresses them.

## Changes from revision2

1. **Clean NACK is bounded, not uncertain.** Exact
   `INTR_STAT == TRANSAC_COMP|ACKERR` now clears only its two status bits,
   clears the FIFO, restores the saved configuration and returns `1`; the
   other amplifier may still be probed because the controller is verified
   idle. This matches the selected source's `-EREMOTEIO` path
   (`i2c-mtk.c:1326-1330`) and stops treating an absent/unpowered amplifier
   as a poisoned bus. A NACK whose status clear is refused is still
   uncertain.
2. **Source V2 HW timeout added.** AP-channel `0x4c` is saved, computed like
   `i2c-mtk.c:963-974` and restored with the other 14 words.
3. **I3C refusal.** AP DEBUGSTAT `0xe4` bit 15 (`I3C_EN`) refuses with
   failure 21 instead of running the I2C-only transport into the source's
   HFIFO/I3C path (`i2c-mtk.c:1039-1046`).
4. **Binding setup-value allowlist.** The binding exact-checks the slave
   address (0x34/0x37 only) and the fixed CONTROL/LEN/TRANSAC/EXT/IO/MCU/AUX/
   clock-div values; arbitrary 16-bit values are accepted only during the
   saved-word restore phase. A core bug can no longer write 0x51/0x56 into
   SLAVE_ADDR through this binding.
5. **Real compile validation, and a buildtree warning.** Revision2's compile
   was genuine: its `ci-39398655023` objects are 24/27 KiB helpers and a
   275,576-byte `sweet_touch` object with `NORD2 AUDIO ID`. Revision3
   recompiles against the same `ci-39398655023` tree with no pre-include and
   gets real objects. Note: `research/ci-39158169212`'s *exported* `SWEET.h`
   predates `NORD2`, so compiling against that tree without regenerating
   `EXPORT_HDRS` silently yields 808-byte empty helpers and the `#else`
   branch. That is an environment trap, not a revision2 defect. See
   `REVIEW.md` D1 and `compile/commands.json`.
6. **Tests expanded.** AFE/I2C8/APDMA block bits, ungated-idle APDMA, clean
   NACK variants, sticky NACK-clear fault, I3C refusal, HW-timeout value,
   binding setup-value rejection and no-NORD2 empty-object control.
7. **Complete integration diff.** Current `sweet_touch.c` plus
   `bsd/conf/files` plus `tools/nord2-mkpatch.sh`, all against the current
   active source. Review material only.

The snapshot core (`audio_id.c`) is unchanged except for comments; the
snapshot register/bit audit found no wrong address or bit.

## Files

| Path | Role |
|---|---|
| `candidate/shim/nord2-xnu/audio_id.h`, `audio_id.c` | pure-C snapshot core (unchanged logic) |
| `candidate/shim/nord2-xnu/audio_i2c8.h`, `audio_i2c8.c` | guarded I2Cv2 PIO read transport (NACK, 0x4c, I3C) |
| `candidate/shim/nord2-xnu/audio_id_bind.inc` | XNU binding, mappings, report, pure return guard, setup allowlist |
| `candidate/tools/fixtures/nord2-audio-i2c8-model.h` | strict register/slave model, NACK and 0x4c checks |
| `candidate/tools/fixtures/nord2-audio-id-core.c` | core tests |
| `candidate/tools/fixtures/nord2-audio-id-bind.c` | actual native binding tests |
| `candidate/tools/test-nord2-audio-id.py` | host runner + optional ARM64/no-NORD2 compile |
| `candidate/patch/*.diff` | complete integration review material |
| `compile/` | exact commands, objects and results |
| `test.log` | host test output |
| `REVIEW.md` | independent review, findings, source anchors, residual gaps |

## Validation run (this host)

```sh
cd .../work/revision3
CC=cc    python3 candidate/tools/test-nord2-audio-id.py
CC=clang python3 candidate/tools/test-nord2-audio-id.py
CROSS=1 CLANG=clang python3 candidate/tools/test-nord2-audio-id.py
```

All passed. The runner builds the core and the **actual binding** with
ASan/UBSan, `-Wall -Wextra -Werror`, and no physical MMIO/GPIO/I2C. The CROSS
step compiles both helpers freestanding for `arm64-apple-macos14.0` and checks
that a no-NORD2 build has no `nau_`/`na8_` symbols while a NORD2 build does.

The **primary** current-source compile uses the CI flag set from
`tools/nord2-xnu-cc.py` against `research/ci-39398655023/buildtree` (the tree
revision2 used), with no pre-include. The source text is the current copy from
`ci-39158169212`; the two trees' audio-relevant headers are the same. It
produced:

- `audio_id.39398655023.o` 24,192 bytes, 39 `nau_`/`na8_` symbol strings;
- `audio_i2c8.39398655023.o` 28,424 bytes, 17 symbol strings;
- `sweet_touch.audio-current.39398655023.o` 348,800 bytes, containing
  `NORD2 AUDIO ID` and referencing `_nau_run`, `_nau_i2c8_ready`,
  `_nau_amp_reset`, `_nau_pin_decode`, `_na8_probe`, `_na8_return_ok`.

A **secondary** compile against `research/ci-39158169212` pre-includes the
patched source `SWEET.h` (source-derived `NORD2`, no `-DNORD2`) because that
tree's exported header is stale. Without the pre-include it produces the
68,192-byte `#else` object; that is the buildtree control, not a revision2
result.

This is native-binding compilation, not a link of the full kernel and not a
boot/image test.

## Integration boundary

The parent must, separately:

1. copy the five helper sources into canonical `shim/nord2-xnu/`;
2. apply `candidate/patch/audio-id-sweet_touch.diff` to the **current**
   active `sweet_touch.c`, add the two `bsd/conf/files` lines and the
   `tools/nord2-mkpatch.sh` changes (or apply the combined diff);
3. run `tools/nord2-mkpatch.sh`, the host suite, the real CI build and an
   exact-image review;
4. decide whether mode1 is worth a boot with `nord2audioid=1` only, after the
   current Wi-Fi work. Mode2 needs a separate shared-bus review and explicit
   authorization.

No integration or hardware approval is implied by these tests.

## Explicit assumptions / blockers

- No hardware result, no audible audio, no device validation. The expected
  AFE state is still "cold, snapshot skipped" (inference).
- Mode2's CAM-off, all-channel-idle, gate-ungated, PLL-running and pad-high
  preconditions are source/lifetime assumptions. Pad-high is not electrical
  ownership. A new camera/CCU/I2C8 owner invalidates the policy.
- No absolute deadline can abort an MMIO bus fault; a failed write may have
  committed and is latched uncertain with no cleanup/replay.
- The transport deliberately uses the source's **100 kHz** ST/FT timing
  (`EXT_CONF=0x8001`, `CLOCK_DIV=0x404`), not the DT's 400 kHz. That is
  slower and electrically safe for I2C, but it is not the rate Android used.
- Configuration restoration uses completed write callbacks; it does not
  independently read back every word or prove electrical line state.
- A clean NACK is source-consistent but its physical STOP is inferred from
  START clear and exact status.
- The 0x4c HW-timeout value is source-derived, not measured.
- The I3C refusal may reject a controller that could still do plain I2C.
- `INTR_MASK=0` is a deliberate deviation from the source's 0x129 so the
  diagnostic does not assert the controller IRQ; the source's own IRQ handler
  masks then reads INTR_STAT (`i2c-mtk.c:1695-1698`), which is the evidence
  that status latching does not depend on the mask.
- `research/ci-39158169212`'s exported `SWEET.h` is stale (no `NORD2`); the
  parent must regenerate `EXPORT_HDRS` or fix `tools/nord2-xnu-cc.py` include
  order, or future compile evidence against that tree will silently test the
  wrong branch. Revision2 used `ci-39398655023`, which is patched; revision3's
  primary compile uses it too.
- M2/M3 (AFE/APLL/I2S/TFA init/tone) and M4 (HAL/IOUserClient) remain
  unimplemented and blocked as in `AUDIO-REPORT.md` §6.

## Privacy

No device dump, serial, calibration, MTP/FAIM or `tfa98xx.cnt` content is in
this directory. The report prints only revision/status/ADC-like values, and
raw snapshot words in the one-line-per-register format.
