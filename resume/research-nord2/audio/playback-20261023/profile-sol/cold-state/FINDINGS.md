# Independent actual inherited-state audit — 2026-10-23

Scope: read-only interpretation of immutable `ios-startup/hardware-review/init.log`
and `status-initial.json`, current `shim/nord2-xnu/audio_afe.c`, and selected Android
`../nord2-current-kernel` source. No phone actions or implementation changes.

## Observed refusal is five simultaneous assumptions

`gate=0xf8` is ADSP_ON(8) + PROTECT(16) + INFRA(32) + MUX(64) +
APLL2_ON(128). AUDIO_ON(4), pin rejection(1024), snapshot failure(1) are absent.
This occurred in amps_prepare's dummy-amp preflight before reset/I2C/AFE writes;
amp IDs zero mean unmeasured, not absent or unsupported. Removing only ADSP gate
will expose four further assumptions immediately. Do not conflate gate refusal
with failed hardware power-on.

| Physical register | Captured value | Interpretation |
|---|---:|---|
| SPM PWR_STATUS + second | 0x10580e03 both | AUDIO bit21 off, ADSP bit22 on, CAM/CCU bit23 off |
| AUDIO_PWR_CON 10006354 | 0x1112 | Exact existing AUDIO cold state: reset/ON/ON2 clear, ISO/CLK_DIS/SRAM_PDN/ACK set |
| ADSP_PWR_CON 10006358 | 0x224d | ADSP powered state, ON/ON2/RST_B set; not the demanded off value0x32 |
| PROTECTEN_STA1_2 10001724 | 0xe3 | AUDIO protection bit4 clear; ADSP bit3 clear |
| IFR1 CG 10001094 | 0x4000 | audio bit25 already ungated |
| IFR3 CG 100010ac | 0x100 | audio26m bit4 already ungated |
| CFG7 10000080 | 0x01040101 | audio_sel enabled, parent1 mainpll_d5_d8 |
| CFG8 10000090 | 0x05010001 | aud_intbus enabled, parent1 mainpll_d4_d4 |
| CFG12 100000d0 | 0x01040602 | engen2 enabled, parent2 apll2_d4; aud1 enabled parent1 |
| CFG13 100000e0 | 0x0c030601 | aud2 enabled, parent1 apll2_ck |
| CFG15 10000100 | 0x01010103 | audio_h enabled, parent3 apll2_ck |
| APLL2_CON0 1000c32c | 0x181 | PLL enabled bit0 |
| APLL2_CON1 1000c330 | 0x04000000 | postdivider code4 => divide16 |
| APLL2_CON2/3 1000c334/338 | 0x78fd5264 both | Existing 48k-family PCW |
| APLL2_CON4 1000c33c | 0x80000001 | power on bit0, isolation bit1 clear |
| APLL2_TUNER 1000c044 | 0x78fd5265 | Exactly PCW+1 |
| AP_PLL_CON3 1000c00c | 0x1e | APLL2 tuner enable bit5 currently clear |
| GPIO32 mode 10005340 | 0x44441111 | GPIO32/33/35 already exact function1 I2S3 |
| GPIO32 direction | 0x400 | Relevant bits0/1/3 clear; irrelevant under function1 anyway |

APLL1 is also enabled (CON0=0x181, power=0x80000001). Its presence reinforces
that inherited clocks are not the old fixture's fully shut-down state. It must
not be disabled or retuned by an APLL2-only audio path.

## Exact APLL2 rate, including fractional value

Selected source `drivers/clk/mediatek/clk-mt6885.c:2529-2535` defines 32 PCW bits,
8 integer bits, postdiv at CON1 bit24, 26MHz input. Source
`drivers/clk/mediatek/clk-pll.c:118-142` implements the two ceiling steps.

With captured PCW0x78fd5264 and divide16, mathematical output is exactly
`103079215078125 / 524288 Hz = 196607999.9506473541259765625 Hz`.
This is about0.049352646Hz below196608000, or -0.000251ppm. The selected kernel's
ceil(VCO), then ceil(/16), returns **196608000Hz**, and its set-rate arithmetic
produces exactly the captured PCW. At /4096, theoretical48k rate is
47999.999987951014Hz. This is the normal fractional encoding, not a mismatched
clock needing correction. Captured tuner also already equals computedPCW+1.

Consequently a borrowed-running-APLL2 branch can require exact PCW/postdiv,
power/en/isolation/tuner-state shape and preserve the inherited values. It must
not use the cold branch's set_rate, prepare/unprepare, powerdown, or unconditional
`tuner disable` operation on an inherited live clock. Enable only separately
owned AFE engine/tuner gates if required and restore only those owned fields.

## Source facts and consequences for a safe dynamic plan

* `clk-mt6885-pg.c:507-510` gives AUDIO protectbit4 and ADSP protectbit3 separately.
  `spm_mtcmos_ctrl_audio` at3260-3358 only touches AUDIO domain and AUDIO protection.
  `spm_mtcmos_ctrl_adsp_dormant` begins3360 and is a distinct procedure. AUDIO
  ownership does not inherently require turning ADSP off. Preserve all ADSP
  power/protection/clock state. ADSP powered does not prove it executes firmware,
  but it prevents assuming all shared clock consumers are absent.
* AUDIO domain itself is proven off, so initial snapshot rightly avoids AFE MMIO.
  Source AUDIO-on sequence does not demand the inherited protectbit already set;
  it powers up then releases protectbit4. A deliberate bounded protection-set
  prelude while AUDIO remains off is an alternative if reviewer requires an
  explicit protected starting point; it must not touch ADSP bit3. On shutdown,
  use source protection-before-SRAM/poweroff ordering regardless of inheritedbit.
* `mt6885-afe-clk.c:212-279` enables existing AUDIO/infra clocks and explicitly
  reparents audio_sel/intbus to26MHz and audio_h toapll2. Current implementation
  only clears gate bits and relies on a26MHz inherited-parent gate. Merely
  deleting NAF_G_MUX therefore leaves a different clock tree than its comments
  claim. Review must explicitly select source reparent+restore, or justify
  retaining valid inherited parents without touching possible shared users.
* Clock parent tables: `clk-mt6885.c:1031-1042`,1166-1170,1198-1201,1290-1295;
  mux/gate layout1834-1845. Engen2/aud2/audio_h already have source-required APLL2
  parents. Reparenting or gating them unnecessarily is avoidable.
* `mt6885-afe-clk.c:150-205` does APLL2 mux enable/parent selection and reverse
  disable. Linux CCF has shared refcounts; port does not. Unconditional existing
  down-plan would gate already-enabled infra/parents, forceAPLL2 off, reparent
  aud2/engen2 away fromAPLL2 and break inheritedaudio_h's live parent. This is a
  real unsafe restoration path if gates are loosened without dynamic down-plan.
  The down plan and post-verify masks must reflect borrowed versus acquired state.
* AUDIO protection was originally clear while AUDIO off. Normal source shutdown
  leaves it set. Either restoring exact inheritedclear after poweroff or retaining
  the safer protected state needs an explicit policy and matching verification;
  do not let old byte-exact watch masks turn the intended policy into false error.
* `mt6885-dai-i2s.c:1030-1051` OPLUS path enablesHD when not low_power, and
  `:136-159` low-power mask initializes0. `mt6885-afe-clk.c:419-451` enables the
  APLL2 24M engine/tuner path; `:481` declares196608000Hz. Normal/SRC fallback
  (HD_EN0) should not be introduced just to avoid inheritedPLL handling. The
  source-backed default path uses the already-correct48k family. Current AFE
  internal clock family and bus clocks are different concerns.

## Next concrete checks before another image

One host fixture should reconstruct ALL69 actual snapshot words, not only patch
five failing fields. It should show: acceptedAUDIOoff + preservedADSPon; no AFE
read before poweron; no writes to inheritedAPLL power/en/rate; only exact owned
AUDIO protection bit; sharedgate/parent retention; successfulstop under inherited
state; secondstart with poststop expected state. A separate cold fixture remains
necessary so borrowed support does not weaken unknown/transitioning states.

After this gate is fixed, amplifier silicon/revision and I2C preflight remain
unmeasured on real phone. Actual snapshot GPIO111/148 are GPIOmode0 and resetlow
(released for active-high reset); DIR bothoutput. This is compatible with the
source reset pulse, but no inference of9874 identity is justified until probe.
The immutable source board container intentionally rejects9873; do not substitute
9874 tuning if probe discovers a different family. CAM/CCU poweroff prerequisite
already passes capturedstatusbit23, while controller/APDMA idleness still needs
normal bounded transport checks at runtime.
