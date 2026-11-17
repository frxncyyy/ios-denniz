# Independent review of revision2, and revision3 changes

Reviewer: DeepSeek V4.1 Flash (private audio worker), 2026-10-21.
Scope: read-only review of the completed revision2 candidate against the
selected Android kernel `04d00c2565764c3922c9b4826b5acff1106e533f`, the saved
live DT `dtb/nord2/live.dts`, the current XNU `sweet_touch.c`, and the parent
review `research/nord2/audio/parent-review/REVIEW.md`. No canonical source,
git state, image, device, CI or hardware was touched. Revision3 is a new
private copy under `work/revision3`; revision2 remains unchanged.

## 1. Bottom line

Revision2's snapshot core and its I2C8 safety model are substantially correct:
I re-derived the bus/channel/gate/pin/PLL/rail facts from the selected source
and found no wrong register address or bit in the snapshot or PIO preflight.
The real candidate defects are three bounded source-fidelity cases and three
integration/defence-in-depth gaps, not the shared-bus exclusion logic. There is
also one buildtree hazard that is not a revision2 defect:

1. **Buildtree hazard (environment, not a revision2 defect).** Revision2's
   CI-flag compile was **genuine**: `audio_id.c.o` is 24,152 bytes with
   `_nau_run`, `audio_i2c8.c.o` is 27,184 bytes with `_na8_probe`, and
   `sweet_touch.audio-review.c.o` is 275,576 bytes with three
   `NORD2 AUDIO ID` and thirteen `NORD2 INPUT` strings and zero
   `SWEET TOUCH` strings. Its `commands.json` used
   `research/ci-39398655023`, whose exported `SWEET.h` **does** define
   `NORD2`. I initially checked `research/ci-39158169212`, whose exported
   `SWEET.h` predates the NORD2 edit; compiling against that tree without
   regenerating headers silently produces 808-byte empty helpers and the
   `#else` sweet_touch branch. That is a **future build-environment trap**, not
   evidence that revision2's validation was fake. Revision3 compiles against
   `ci-39398655023` without any pre-include and gets real objects; the
   `ci-39158169212` pre-include variant is kept only as a secondary check.
2. **A clean NACK was treated as an uncertain bus fault.** The selected source
   explicitly returns `-EREMOTEIO` for a completed multi-channel transfer
   whose only error is ACKERR, *without* `mt_i2c_init_hw()` or RESUME_ARBIT
   (`i2c-mtk.c:1326-1330`). Revision2's `INTR_STAT == 3` path instead latched
   `uncertain` and refused Linux return. That is over-conservative and hides
   the other amplifier on the common "one amp absent" case.
3. **The source V2 HW timeout at AP-channel `0x4c` was not programmed.** The
   source computes and writes it on every V2 transfer (`i2c-mtk.c:963-974`);
   revision2 saved/restored only 14 words and omitted it.
4. **AP-channel I3C mode was not refused.** The source reads DEBUGSTAT
   (`0xe4`) bit 15 and runs a separate HFIFO/I3C setup when set
   (`i2c-mtk.c:1039-1046`). Revision2's I2C-only transport did not check it.
5. **The binding's write allowlist did not constrain the setup values.**
   `nab_bus_write()` accepted any 16-bit value for `SLAVE_ADDR` and the other
   configuration words. A bug in the core (or a future edit) could therefore
   write `0x51`/`0x56`, the DDR/UFS bucks. The core is the only caller today,
   but the layered safety argument was weaker than documented.
6. **The integration diff was incomplete.** Revision2's
   `candidate/patch/audio-id-integration.diff` contained only the three
   `sweet_touch.c` call-site lines. `INTEGRATION.md` also requires
   `bsd/conf/files` and `tools/nord2-mkpatch.sh` changes; those were absent.

Revision3 fixes the five candidate issues (items 2–6) in a private copy, adds
host tests, and documents the buildtree hazard (item 1). It does **not** change
the mode1 snapshot contract, does **not** add any GPIO/recovery/DMA/clock/
power write, and does **not** authorize mode2 or any hardware run.

## 2. What revision2 got right (independently re-derived)

| Area | Confirmation |
|---|---|
| I2C8 identity | DT `live.dts:9466-9488`: `0x11d04000`, APDMA `0x11000780`, `ch_offset_default=0x100`, `ch_offset_ccu=0x200`; children 0x51 DDR / 0x56 UFS are `regulator-always-on` (`:9509-9529`). |
| I2C8 gate | `clk-mt6885-i2c.c:113-135`: `imps_ap_i2c8_ro` bit 4, status at `0x11d05000+0xe00`. |
| I2C mux/PLL | `clk-mt6885.c:1102-1106`: `i2c_sel` parents 26 MHz / `mainpll_d4_d8` / `univpll_d5_d4`; `CLK_CFG_10` lsb 8, pdn 15 (`:1886-1890`); main/univ PLL CON0/CON1/CON3 at `0x340/0x344/0x34c` and `0x308/0x30c/0x314`. The 32/20 dividers in the transport match `FACTOR(...D4_D8)` and `FACTOR(...D5_D4)`. |
| I2C transfer | `i2c-mtk.h:252-282` V2 offsets (DATA 0x0, SLAVE 0x4, MASK 0x8, STAT 0xc, CONTROL 0x10, LEN 0x14, TRANSAC 0x18, DELAY 0x1c, TIMING 0x20, START 0x24, EXT 0x28, LTIMING 0x2c, HS 0x30, IO 0x34, AUX 0x44, CLKDIV 0x48, HW_TIMEOUT 0x4c, DEBUGSTAT 0xe4, MCU_INTR 0x40, FIFO 0xf4). Revision2's 14-word list and values (0x3a control, 0x8001 ST start, open drain 3, MCU_INTR 1, AUX 2, 0x404 clock div) match the source for a deliberate 100 kHz timing. |
| Pin table | `pinctrl-mt6885.c`: mode `0x340/0x3d0/0x3f0/0x420`, DIR `0x10/0x30/0x40`, DOUT `0x110/0x130/0x140`, DIN `0x210/0x230/0x240`; IES 122/123 at `0x11d10070` bits 3/13, 111 at `0x11d10060` bit 24, 148 at `0x11c20020` bit 5; 32-35 at `0x11f20040` bits 3/5/2/4. All revision2 snapshot addresses match. |
| AFE guards | `clk-mt6885-pg.c:563-565` audio/cam bits 21/23; `AUDIO_PWR_CON` bits RST_B/ISO/ON/ON_2ND/CLK_DIS/SRAM_PDN/ACK; `ifrao_audio` IFRAO1 bit 25, `ifrao_audio26m` IFRAO3 bit 4; protect STA1_2 0x724 bit 4; `clk-mt6885.c:1886/...` mux pdn. Revision2's 0x111f == 0xd check is correct for the source's power-on end state. |
| Variant policy | `tfa_init.c:807-847` (9873: 0x0a73/0x0b73/0x1a73) and `:903-961` (9874: 0x0a74/0x0b74/0x0c74); `tfa98xx.c:5076-5143` low-byte dispatch. Revision2 reads REV first and only the exact six revisions get SYS0/STATUS14. `MANSTATE=0x1433` means register 0x14 bits 6:3 (`tfa_get_bf` encoding in `tfa_dsp.c:2682-2711`); PWDN bit0 and AMPE bit3 match `tfa9874_tfafieldnames.h`. |
| Sticky policy | No recovery, no replay, no second amp after an uncertain write; `na8_return_ok()` is a pure state check; pre-write refusals leave hardware untouched. The parent A2/A3 requirements are met. |
| No unsafe writes | The PIO path writes only AP-channel config/FIFO/status/START/DATA; it never writes GPIO/IOCFG, clock gate, mux/PLL, pinmux, shadow/CCU, APDMA, SOFTRESET or RESUME_ARBIT. Revision2's claim is accurate. |
| Compile validation | Revision2's `ci-39398655023` objects are real (sizes/strings above), not empty. The earlier `ci-39158169212` empty control was my buildtree mistake. |

## 3. Findings and fixes in revision3

### D1 — buildtree header hazard, not a revision2 defect (environment)

Revision2's compile is **real**. Its `commands.json` points at
`research/ci-39398655023`, whose exported `SWEET.h` contains
`#define NORD2 1`; the objects confirm it:
`audio_id.c.o` 24,152 bytes with `_nau_run`, `audio_i2c8.c.o` 27,184 bytes
with `_na8_probe`, `sweet_touch.audio-review.c.o` 275,576 bytes with three
`NORD2 AUDIO ID` and thirteen `NORD2 INPUT` strings and zero `SWEET TOUCH`
strings.

The trap is that **`research/ci-39158169212`'s exported**
`build/xnu.obj/EXPORT_HDRS/pexpert/pexpert/arm64/SWEET.h` is older than the
NORD2 edit and does **not** define `NORD2`. Compiling the same source against
that tree without regenerating `EXPORT_HDRS` produces 808-byte empty helper
objects and the `#else` sweet_touch branch; I reproduced that control before
correcting the buildtree. This is not evidence about revision2.

Revision3 therefore uses two compiles:
- **primary:** `research/ci-39398655023` (the tree revision2 used), no
  pre-include. This yields `audio_id.39398655023.o` 24,192 B,
  `audio_i2c8.39398655023.o` 28,424 B, and
  `sweet_touch.audio-current.39398655023.o` 348,800 B with three
  `NORD2 AUDIO ID` strings and the six expected helper references.
- **secondary:** `research/ci-39158169212` with the patched source `SWEET.h`
  pre-included (source-derived `NORD2`, no `-DNORD2`), because that tree is
  the one `tools/nord2-xnu-cc.py` and the patch generator currently point at.

Parent action: either regenerate `EXPORT_HDRS` after applying the patched
`SWEET.h`, or add `srcroot/pexpert` **before** `EXPORT_HDRS/pexpert` in
`tools/nord2-xnu-cc.py`, and use a tree whose exported headers match the
patched source. Otherwise future compile evidence against `ci-39158169212`
will silently test the wrong branch.

### D2 — clean NACK was uncertain (medium)

Source: `i2c-mtk.c:1326-1330` returns `-EREMOTEIO` when
`TRANSAC_COMP && ch_offset && !BUS_ERR`, and does not call `mt_i2c_init_hw()`
or RESUME_ARBIT. The IRQ handler has already masked and cleared all status
(`:1695-1698`).

Revision2: `INTR_STAT == 3` (TRANSAC_COMP|ACKERR) fell into `v != 1`, set
`failure=14`, `uncertain=1`, and `na8_return_ok()` false.

Revision3: exact `v == 3` is a **clean NACK**:
- re-read AP START, require bits 0/1 clear;
- clear exactly bits 1:0 (`W1C 3`), read back 0;
- clear the FIFO;
- do **not** read DATA_PORT;
- restore the 15 saved config words with completed write callbacks;
- set `nack=1`, increment `nacks`, leave `uncertain=0`, return `1`;
- `na8_probe()` stops that amplifier, keeps raw REV as NOT_READ (0), and
  probes the other amplifier only because the controller/bus were verified
  idle and restored.
- A sticky/refused status clear remains `uncertain`; status `0x101`
  (BUS_ERR), timeouts and any other value remain uncertain.

Residual: a `v == 3` completion is source-consistent but not a physical
STOP/electrical proof; an independent reviewer should keep that caveat.

### D3 — V2 HW timeout 0x4c missing (medium)

Source: `i2c-mtk.c:963-974` computes
`tv1=(clk_main/clk_div/1000)*2`, `tv2=((LTIMING&0x3f)+1)*(((LTIMING&0x1c0)>>6)+1)`,
`tv=DIV_ROUND_UP(tv1,tv2)` and writes `tv` to `OFFSET_HW_TIMEOUT` (0x4c) on
every V2 transfer.

Revision2: not saved, not written.

Revision3: the transport saves/restores 15 words, computes the same `tv` from
its own post-decrement LTIMING and writes it at AP-channel `0x4c`. The model
checks the value; for 26 MHz/5/100 kHz it is 386 (`10400/27`).

Residual: the value is source-derived, not hardware-observed.

### D4 — I3C-enabled AP channel not refused (medium)

Source: `i2c-mtk.c:1039-1046` reads DEBUGSTAT (0xe4) bit 15; when set and the
speed is high enough, it writes HFIFO unlock/ninth-bit/master-code. This
transport implements only the I2C WRRD path.

Revision3: preflight reads `0xe4`, records `i3c`, and refuses with failure 21
if bit 15 is set. The model exposes the register; the test proves no write or
START occurs.

Residual: a false refusal is possible on an I3C-capable controller even at
100 kHz; that is intentional conservatism.

### D5 — binding setup-value allowlist too wide (medium/low)

Revision2's `nab_bus_write()` allowed any `v <= 0xffff` for every config
offset, including `SLAVE_ADDR` 0x04. The core only writes 0x68/0x6e, but the
binding did not enforce it.

Revision3:
- `na8_state.restoring` marks the saved-word restore phase.
- `nab_setup_value()` exact-checks the setup values: slave 0x68/0x6e,
  INTR_MASK 0, CONTROL 0x3a, LEN 1, TRANSAC 2, DELAY 10, EXT 0x8001, HS 0,
  IO 3, MCU_INTR 1, AUX 2, CLOCK_DIV 0x404; TIMING/LTIMING/HW_TIMEOUT are
  range-checked.
- During restore any 16-bit saved word is allowed, because that is the value
  read back from the same channel.
- The W1C allowlist follows the current transaction (`1` on success, `3` on
  a clean NACK).

This closes the "core bug writes 0x51/0x56" path at the binding layer.

### D6 — integration diff incomplete (medium)

Revision2's diff had only the `sweet_touch.c` hunks, while its own
`INTEGRATION.md` described `bsd/conf/files` and `tools/nord2-mkpatch.sh`
changes. Revision3 provides:
- `candidate/patch/audio-id-sweet_touch.diff` — against the **current**
  `research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c`, preserving
  every Wi-Fi include/call;
- `candidate/patch/audio-id-conf-files.diff` — the two helper TUs;
- `candidate/patch/audio-id-mkpatch.diff` — generator loop, NEW_FILES and
  `audio_id_bind.inc` copy;
- `candidate/patch/audio-id-integration.diff` — the three concatenated.

The patch remains review material. The parent must copy the helper sources
into canonical `shim/nord2-xnu/`, apply the hunks, regenerate 0082, run the
real CI build and verify the image.

### D7 — test coverage gaps (low)

Revision2's tests were real but compressed; they did not cover:
- each AFE/I2C8/APDMA block bit separately;
- the ungated-idle APDMA path;
- clean NACK (single, first-amp, second-amp, both, sticky-clear fault);
- I3C refusal;
- the 0x4c HW timeout value;
- binding setup-value rejection.

Revision3 adds these. It also adds a `strings`-based check that the NORD2
guard produces a non-empty helper object and the no-NORD2 guard an empty one,
because `llvm-nm` is absent on this host.

### D8 — correction to this review (not a candidate defect)

My first pass compared revision2's objects against `research/ci-39158169212`
and concluded the compile was empty. That was wrong: revision2 used
`research/ci-39398655023`, whose exported header defines `NORD2`, and its
objects are real. The correction is recorded here, and revision3's
`commands.json`/`result.log` keep both the primary `ci-39398655023` compile and
the `ci-39158169212` stale-header control so the distinction is explicit. The
parent should not discard revision2's compile evidence; it only needs to avoid
the stale exported headers in `ci-39158169212` for future runs.

## 4. Remaining gaps and explicit blockers

- **No hardware, no audible audio, no device validation.** Mode2 remains a
  private source candidate only. The user has not authorized it.
- **Mode2 still writes controller state.** It is not read-only: AP config,
  FIFO, W1C, DATA_PORT, START and restores. No electrical bus-ownership proof;
  the CAM-off + all-channel-idle + pad-high preconditions are source/lifetime
  assumptions, not measurements.
- **Clean NACK** is a source-consistent bounded result, but the physical STOP
  is inferred from START clear and exact status; independent review is still
  required.
- **HW timeout 0x4c** is source-derived; its unit/behavior is not measured.
- **I3C refusal** may reject a controller that could still do plain I2C.
- **The exported-header issue** means the parent's compile harness must be
  fixed or the exported headers regenerated; otherwise validation is false.
- **No AFE bring-up, I2S, DMA, TFA init/profile or HAL work** was added.
  M2/M3 remain blocked as described in `AUDIO-REPORT.md` §6.
- **No canonical/canonical-adjacent file was edited.** All changes are under
  `work/revision3`.

## 5. Exact source anchors used

| Fact | Anchor |
|---|---|
| I2C8/APDMA/channels/rails | `dtb/nord2/live.dts:9466-9551`; `:8630-8632` |
| I2C8 clock gate | `drivers/clk/mediatek/clk-mt6885-i2c.c:113-135` |
| I2C mux/PLL | `drivers/clk/mediatek/clk-mt6885.c:1102-1106, 1567-1569, 1886-1890, 2523-2534` |
| V2 registers | `drivers/i2c/busses/i2c-mtk.h:252-282` |
| Transfer programming | `drivers/i2c/busses/i2c-mtk.c:963-974, 978-1081, 1203` |
| Clean NACK | `drivers/i2c/busses/i2c-mtk.c:1326-1330` |
| I3C path | `drivers/i2c/busses/i2c-mtk.c:1039-1046` |
| IRQ mask/status | `drivers/i2c/busses/i2c-mtk.c:1695-1698` |
| Pin fields | `drivers/pinctrl/mediatek/pinctrl-mt6885.c:26-85, 172-291, 293-453, 520-705` |
| SPM/CAM/audio | `drivers/clk/mediatek/clk-mt6885-pg.c:563-568, 3266-3355` |
| AFE registers | `sound/soc/mediatek/mt6885/mt6885-reg.h:2355-2377, 2564-2565` |
| TFA revisions/fields | `sound/soc/codecs/audio/tfa98xx/tfa_init.c:807-961`; `tfa98xx.c:5076-5143`; `tfa_dsp.c:2682-2711`; `tfa9874_tfafieldnames.h` |
| CPU flags / NORD2 | `research/ci-39158169212/buildtree/xnu/pexpert/pexpert/arm64/SWEET.h:18-22` |
| Current active source | `research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c` |
| Parent review | `research/nord2/audio/parent-review/REVIEW.md` |
