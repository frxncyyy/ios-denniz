# Nord2 MT6893 audio startup from the REAL inherited state — findings

Scope: source `66fc36e`, phone image `8e6697a2`, evidence
`ios-startup/hardware-review/init.log` lines 1091-1162 and `status-initial.json`.
Android reference: `../nord2-current-kernel` @ `04d00c2`. All Android paths
below are relative to that tree. "Proven" = read directly from source or the
captured words; "inferred" = reasoning not directly observed.

## 1. What failed, exactly

`init.log:1091-1092`: `worker=-4 initialized=0 gate=248 (0xf8)`, afe/i2c 0/0,
no write, no I2C transfer. Status JSON agrees (`error -4, gate 248, amp0/1 0`).
The refusal is `amps_prepare()`'s dummy-evidence gate (`audio_hw.c:178-180`),
before the I2C lease, before any GPIO/I2C/AFE access. Amp revisions 0 mean
*not yet probed*.

`0xf8 = ADSP_ON(0x08) | PROTECT(0x10) | INFRA(0x20) | MUX(0x40) | APLL2_ON(0x80)`.
AUDIO_ON(0x04) and PINS(0x400) were clear. Each bit is a canonical assumption
that the whole audio clock tree is in a Linux-CCF-style "everything gated"
state. The real preloader/LK hand-off is not that state.

## 2. The real inherited state (69 words, decoded)

| idx | PA | value | decode (source) |
|---|---|---|---|
| 0/1 | 1000616c/170 | 0x10580e03 | AUDIO bit21 **off**, ADSP bit22 **on** both words, CAM bit23 off (`clk-mt6885-pg.c:563-565`) |
| 2 | 10006354 | 0x1112 | AUDIO exact power-down end state: ISO, CLK_DIS, SRAM_PDN+ACK (`pg.c:3282-3311`, bits `pg.c:267-271,638-640`) |
| 3 | 10006358 | 0x224d | ADSP exact power-up end state: RST_B, ON, ON_2ND, SRAM_ISOINT_B, SRAM_SLEEP_B, SLEEP_B_ACK; ISO/CLK_DIS/CKISO clear (`pg.c:3418-3458`, bits `:267-273,573-574`) |
| 4 | 10001094 | 0x4000 | ifrao_audio bit25 ungated; ap_dma_ps bit18 ungated |
| 5 | 100010d8 | 0 | apdma bit31 ungated → I2C preflight WILL read APDMA EN |
| 6 | 100010ac | 0x100 | ifrao_audio26m bit4 ungated |
| 7 | 10001724 | 0xe3 | AUDIO protect ack bit4 **clear**; ADSP bit3 clear (`pg.c:507-510`) |
| 8 | 10000080 | 0x01040101 | audio_sel ungated, parent1 mainpll_d5_d8 (`clk-mt6885.c:1031-1036,1834-1838`) |
| 9 | 10000090 | 0x05010001 | aud_intbus ungated, parent1 mainpll_d4_d4 (`:1038-1042,1840-1844`) |
| 10 | 100000b0 | 0x05050201 | i2c_sel ungated, parent2 univpll_d5_d4 (`:1102-1106,1886-1890`) |
| 12 | 100000d0 | 0x01040602 | aud_engen2 ungated parent2 **apll2_d4**; aud_1 ungated parent1 (`:1166-1171,1922-1926,1937-1941`) |
| 13 | 100000e0 | 0x0c030601 | aud_2 ungated parent1 **apll2_ck**; adsp_sel bits[10:8]=6 in same word (`:1198-1201,1943-1950`) |
| 14 | 10000100 | 0x01010103 | audio_h ungated parent3 apll2_ck (`:1290-1295,1985-1989`) |
| 20 | 1000c00c | 0x1e | APLL1 tuner_en bit0 and APLL2 tuner_en bit5 both clear (`:2527,2533`) |
| 22 | 1000c044 | 0x78fd5265 | APLL2 tuner = PCW+1 |
| 23-27 | 1000c318.. | 0x181/0x04000000/0x6f28bd4c/../0x80000001 | APLL1 enabled, powered, 180.6336 MHz |
| 28-32 | 1000c32c.. | 0x181/0x04000000/0x78fd5264/../0x80000001 | APLL2 enabled (CON0 bit0), PWR_ON, ISO clear, postdiv code4 (/16) |
| 36-38 | 1000c308.. | 0xff8002e1/0x180000/0x80000001 | UNIVPLL en+rstb, powered, 2496 MHz → i2c parent 124.8 MHz |
| 39 | 11d05e00 | 0 | IMPS I2C8 gate open |
| 40 | 10005340 | 0x44441111 | GPIO32/33/35 already function1 I2S3, GPIO34 I2S0_DI fn1 |
| 41/45/48 | GPIO104 mode/DIR96/DOUT96 | 0x777222/0x3c000/0x4000 | GPIO111 mode0, output, low (reset released) |
| 43/46/49 | GPIO144 mode/DIR128/DOUT128 | 0x11100000/0xb0178667/0 | GPIO148 mode0, output, low (reset released) |
| 42/51/56 | MODE120/DIN96/IES122 | 0x55111111/0xffc84180/0xfff7ffff | GPIO122/123 SCL8/SDA8 fn1, IES set, both pads high |
| 68 | 10006000 | 1 | POWERON_CONFIG_EN bit0 |

APLL2 rate, recomputed independently: PCW 0x78fd5264 = 2029867620;
26 MHz·PCW/2^24 = 3 145 727 999.21 → source `__mtk_pll_recalc_rate`
(clk-pll.c:118-142: ceil after the fraction, then ceil /16) = **196 608 000 Hz**
exactly, the AFE's required APLL2 rate (`mt6885-afe-clk.c:479-482`). The
captured tuner already equals PCW+1. This is the normal 48 k-family PLL that
the boot chain left running, not a mis-programmed one.

Interpretation (inferred): the preloader programs both APLLs and the audio
muxes to their target parents and leaves gates open; under Android, CCF's
`disable_unused` would later gate unused ones. XNU has no CCF, so the
hand-off state persists. ADSP is powered (probably by the boot chain); no
evidence it executes audio firmware. The AUDIO domain is off.

## 3. Source facts that decide ownership

* **ADSP is independent of AUDIO.** PG_AUDIO has no PG parent; its parents
  are `aud_intbus_sel`, `ifrao_audio26m`, `ifrao_audio`
  (`clk-mt6885-pg.c:5001-5003`); PG_ADSP is separate (`:5004`). AUDIO uses
  protect bit4, ADSP bit3 (`:507-510`). `spm_mtcmos_ctrl_audio` (`:3258-3357`)
  never touches ADSP_PWR_CON or bit22; `spm_mtcmos_ctrl_adsp_dormant`
  (`:3359-3467`) is a distinct procedure. `adsp_sel` parents contain no APLL
  (`clk-mt6885.c:1203-1211`). → ADSP-on is no reason to refuse, and we must
  never power it down. Only *incoherent/transitioning* ADSP is a reason.
* **AUDIO on-sequence has no protect precondition.** It powers up then
  releases bit4 and explicitly ignores the ack (`pg.c:3314-3355`, 3348-3353).
  Power-down sets bit4 and waits its ack first (`:3271-3281`). → inherited
  bit4 clear is acceptable; power-off must set it regardless.
* **AFE enable reparents audio_sel and aud_intbus to clk26m.**
  `mt6885_afe_enable_clock` (`mt6885-afe-clk.c:212-279`): SCP_SYS_AUD
  (PG power-on), infra gates, `audio_sel` enable + parent clk26m (:240-253),
  `aud_intbus` enable + parent clk26m (:255-262), `audio_h` parent apll2_ck
  (:264-271), then `aud_afe` gate (:273). Runtime resume then sets
  CPU_HD_ALIGN 0, CONN_24BIT, AFE_ON (`mt6885-afe-pcm.c:3442-3478`).
  CPU copies temporarily raise intbus to mainpll_d4_d4
  (`mt6885-afe-pcm.c:3480-3498`), so both intbus parents are operating points.
  The canonical code only *cleared gates* and *required* 26 MHz parents; it
  never reparented. Merely deleting the MUX check would run the AFE with
  audio_sel at mainpll_d5_d8 — not a source state (inferred: the 26 MHz AFE
  rate generators assume clk26m).
* **Mux parent change is CLR(field)/SET(index)/UPDATE, only when it differs,
  no completion handshake** (`clk-mux.c:178-224`). Gate enable = CLR alias,
  disable = SET alias (`clk-mux.c:78-118`). Update bits: audio 0, intbus 1,
  engen2 17, aud_2 21, audio_h 29 of CLK_CFG_UPDATE1 0x10000008
  (`clk-mt6885.c:33-35,123-152`). Gate variant is the active table
  (`MT_CCF_MUX_DISABLE 0`, `:26`).
* **APLL2 consumers.** `apll2_mux_setting` enables aud_2 and engen2 and sets
  apll2_ck / apll2_d4 (`mt6885-afe-clk.c:149-184`); `mt6885_apll2_enable`
  then opens `aud_24m`, `aud_apll2_tuner`, programs `AFE_APLL2_TUNER_CFG`
  and `AFE_24M_ON` (`:421-451`). AUDSYS gates: aud_afe bit2, aud_24m bit9
  (parent aud_engen2), aud_apll2_tuner bit18 (`clk-mt6885-audsys.c:76-83`).
  The apmixed tuner_en for APLL2 is AP_PLL_CON3 bit5 (`clk-mt6885.c:2533`);
  the AFE tuner loop only acts through that enable (inferred).
* **audio_h** feeds only hires DAC/ADC gates (`clk-mt6885-audsys.c:113-130`),
  never used by I2S3/DL1. It is already on apll2_ck.
* **No I2S BCLK gate is ever touched by Android.** The AFE clock list
  (`mt6893.dts:7830-7887`) has no `aud_i2s*_bclk`; AUDIO_TOP_CON0/1 are
  CCF-owned volatile registers (`mt6885-afe-pcm.c:3094-3095`). So AUDIO-domain
  reset defaults must already permit I2S3 (inferred, consistent with Android
  working). No new write is justified.
* **TFA reset/addresses**: `oplus6893_20827_audio_extend.dtsi:12-25`: 0x34 ↔
  GPIO111, 0x37 ↔ GPIO148 (matches `audio_hw.c` and `audio_id.c`). The built
  driver is `sound/soc/codecs/audio/tfa98xx/tfa98xx.c` (Makefile:26; built
  with `-DTFA_NON_DSP_SOLUTION`), whose `tfa98xx_ext_reset` pulses 5 ms high /
  5 ms low (`tfa98xx.c:4740-4749`) — what `amps_prepare` does. The unbuilt
  `src/tfa98xx.c:2868-2877` copy uses 10 ms; irrelevant.
* **TFA SYS_CONTROL0 POR**: tfa9874 bitfields PWDN 0x0000, AMPE 0x0030,
  NOCLK 0x1070, TDMERR 0x1060, CLKS 0x1170, MANSTATE 0x1433, AUDFS 0x0203
  (`tfa9874_tfafieldnames.h:17-68`) match `audio_hw.c`. TFA9872 (same ProBus
  family) documents `0x00 POR=0x0001` (`tfa_init.c:611`): PWDN=1, AMPE=0.
  For 9874 POR is *inferred* identical; not proven.

## 4. Ownership model implemented (candidate)

`naf_ownership()` (candidate `audio_afe.c`) decodes, purely from the boot
snapshot, whether each shared element is **acquired** (we enable it and
re-gate it on stop) or **borrowed** (already live: never gated, never
retuned, restored to its exact inherited value). Plans are built from that
record; every watched field is re-read before each start and verified after
each stop against the *boot snapshot* (bit-exact restoration policy).

| element | accepted inherited states | refused | action (real snapshot) |
|---|---|---|---|
| AUDIO MTCMOS | exact off 0x1112 masked 0x111f, bit21 clear both | anything else (`AUDIO_ON`) | full source on/off sequence |
| ADSP MTCMOS | exact off (0x0032, bit22 clear both) or exact on (0x224d, bit22 set both) | transition/incoherent (`ADSP_ON`) | **never written**; drift refused |
| AUDIO protect bit4 | set or clear | — | CLR+poll on start; SET+poll before SRAM/ISO on stop; CLR+poll **after** domain off if inherited clear |
| ifrao_audio / audio26m | gated or ungated | — | borrowed: no write |
| aud_intbus | parent 0..2, gated or not | parent 3 (`MUX`) | reparent →clk26m after power-up; restore parent1 after power-off |
| audio_sel | any parent, gated or not | — | reparent →clk26m before AFE; restore parent1 after AFE gate closes |
| APLL2 | exact cold (en0, ISO1/PWR0, tuner_en0) **or** live (en1, PWR1/ISO0) at exactly 196.608 MHz; if tuner_en set, tuner==PCW+1 | other power shapes (`APLL2_ON`), live wrong rate (`APLL2_RATE`), bad tuner (`APLL2_TUNER`) | live: **no** PCW/postdiv/power/en/tuner write, never powered down |
| aud_2 / aud_engen2 | cold PLL: gated on 26 MHz; live PLL: on apll2_ck / apll2_d4 (gated or not) or gated on 26 MHz | live PLL with another live parent, or cold PLL with live consumers (`MUX`) | borrowed: no write |
| AFE tuner loop | — | — | live PLL: aud_apll2_tuner and AFE_APLL2_TUNER_CFG left off (PLL runs at plain PCW, −0.00025 ppm); cold PLL: source tuner sequence unchanged |
| audio_h | anything | — | watched read-only, never written |
| I2S3 pins | unchanged canonical rule | unchanged | unchanged |

Cold-path identity: for the original fully-gated fixture every condition
evaluates to "acquire", producing *exactly* the canonical programme
(73 up / 49 down ops). `tests/nord2-audio/plan-dump.c` prints every op and is
built against both canonical and candidate sources by `run-candidate.py`,
which fails unless the two cold dumps are byte-identical.

## 5. Ordered startup / restore contract for the real snapshot

**nau_hw_init** (worker, once per boot): read 69 snapshot words → decode
ownership (`status.ownership`, expected `0x800651ff`) → gate with dummy amp
evidence (expected 0) → I2C8 lease → bus clocks already open (no write) →
preflight `REV` read on 0x34 (full transport preflight, §6) → reset pulse
GPIO111/148 high 5 ms, low 5 ms → probe REV/SYS0/STATUS14 on 0x34 and 0x37 →
evidence gate (`NAH_E_AMPS` if only amp evidence fails) → initialized.
No AFE/clock write happens in init.

**nau_hw_start** (48 up ops, then amps). Phases (`plan-candidate.txt` from the
runner lists every op):
1. POWER: POWERON key; AUDIO PWR_ON, PWR_ON_2ND; poll status21 both
   (≤10 ms); CLK_DIS 0, ISO 0, RST_B 1, SRAM_PDN 0; poll SRAM ack 0; protect
   CLR bit4; poll ack 0 (already 0).
2. AFECLK: audio_sel CLR[25:24], SET 0, UPDATE bit0; aud_intbus CLR[1:0],
   SET 0, UPDATE bit1; AFE_TOP_CON0 bit2 (aud_afe) 0.
3. AFE: CPU_HD_ALIGN 0, CONN_24BIT(_1) all ones, AFE_ON.
4. I2S3 cfg: rate 48k (10), I2S fmt, WLEN 32.
5. DL1: verify off, base/end 0x11212000..0x11212fff, stereo, 48k, HD0,
   pbuf/minlen, normal mode, CPU normal mode, zero-fill 4 KiB SRAM.
6. APLL2 (borrowed): AFE_TOP_CON0 bit9 (aud_24m) 0, AFE_HD_ENGEN bit1
   (AFE_24M_ON) 1. No topckgen/apmixed write.
7. I2S_ON: HD_EN, pins fn1 (no change), bias off (no change), I2S3_EN.
8. STREAM: CONN28 bit5, CONN29 bit6, DL1_ON.
Then (non-silent): per amp amp_cold (AMPE 0, PWDN 1, MANSTATE 0) → tfa_init
errata for the exact REV → 52-op SHA-pinned board profile → key lock →
AUDFS=8 → verify PWDN1/AMPE0; then per amp PWDN 0, MANSCONF 1, wait CLKS
≤50 ms, clear historical NOCLK, refuse current TDM/NOCLK, AMPE 1.

**nau_hw_tick** (2 ms cadence): DL1_CUR within ring, ≤10 ms per tick,
progress within 12 ms, refill consumed words, amp 0x10 monitor every 4 ticks.

**nau_hw_stop** (34 down ops after amps off): amps AMPE0/PWDN1 + key lock
(if bus uncertain: hold both resets high first) → DL1 off, CONN off → I2S3_EN
off, pins/bias back to snapshot fields, HD_EN off → AFE_24M_ON 0, aud_24m
gate 1 → AFE_ON 0, poll DAC_MON, aud_afe gate 1 → audio_sel CLR, SET
1<<24, UPDATE bit0 → POWERON key, protect SET bit4, poll ack 1, SRAM_PDN 1,
poll ack, ISO 1, CLK_DIS 1, RST_B 0, PWR_ON 0, PWR_ON_2ND 0, poll status21
clear both → protect CLR bit4, poll ack 0 → aud_intbus CLR, SET 1, UPDATE
bit1 → POWERON key with inherited bit0 → reset GPIOs back to snapshot →
re-read all watched fields == boot snapshot, else uncertain.

**Restart**: identical; live drift (including ADSP and audio_h) refuses with
zero writes and leaves the object retryable.

## 6. Downstream review after the gate (nothing beyond amps_prepare ran)

* **I2C8 preflight with the captured words** (`audio_i2c8.c` stages 1-8):
  mux ungated, select 2, UNIVPLL en/rstb/power OK, PCW stable, parent
  124.8 MHz, hclk 24 960 kHz → sample 5, step 25 → 99.8 kHz (same
  `timing|I2C_TIMEOUT_EN` as `i2c-mtk.c:1049-1052`); GPIO122/123 fn1, IES and
  both pads high; CAM off; IMPS gate open; APDMA clocks open so DMA EN at
  0x11000788 is read. **Not captured**: APDMA EN, shadow/AP/CCU START,
  CONTROL DMA_EN, FIFO, AP INTR_STAT, I3C_EN. Any non-idle value refuses
  cleanly (`NAH_E_IO`, i2c stage/failure now logged with `i2cpa/i2cv`). Not
  relaxed: no evidence they are idle, and I2C8 carries the DDR/UFS bucks.
* **GPIO111/148**: mode0, output, low = released active-high reset. Source
  pulse (`tfa98xx.c:4740-4749`) then restore to the same fields. No blocker.
* **TFA identification/profile**: revisions unknown until probe; board
  accepts only 0a74/0b74/0c74 (`audio_board.c:14-15`). A different family is
  a hard stop by design (do not substitute 9874 tuning).
* **Amp evidence** after the reset pulse requires PWDN1/AMPE0/MANSTATE0. AMPE0
  is expected from the sibling POR (`tfa_init.c:611`) but unproven; refusal is
  now `NAH_E_AMPS` with SYS_CONTROL0 in `st=`, distinguishable from a clock
  refusal. Kept as a reset-effectiveness sanity check.
* **Domain power / SRAM**: source sequence; bounded polls; ack loss latches
  uncertain without forcing (regression `powerup-timeout`, `protect-timeout`).
* **48 kHz route**: borrowed APLL2 196.608 MHz → aud_2 apll2_ck → engen2
  apll2_d4 49.152 MHz → aud_24m → AFE_24M_ON → I2S3 HD_EN, rate code 10,
  WLEN 32 (BCK 3.072 MHz). MCLK dividers (AUDDIV) are only used when the BE
  requests an MCLK (`mt6885-afe-clk.c:688-782`, `mt6885-dai-i2s.c:1101-1128`);
  none here. No divider write added.
* **DMA/stream/stop/restart**: unchanged logic; restart now proven against
  the real tree including idle-driven stop and post-idle restart.
* **Unchanged residual policies** (not startup blockers): a ≥20 ms worker
  scheduling gap or a stalled pointer stops playback and latches the error
  (no auto-restart) — by existing design.

## 7. Independent-review reconciliation

* Both reviewer FINDINGS agree with §3-4; I verified each cited line.
* Policy choice: protection is *restored* to the inherited clear state after
  the domain is fully off (both reviewers allowed this; it keeps verification
  bit-exact against the boot snapshot, which the whole boot already ran in).
* `hardware-sol/cold-state/test-inherited.c` would abort in its `cycles`
  case on any implementation: it seeds `cm` with the snapshot but leaves the
  bus model's `bm.ifr1 = 1<<18` from `m_init`, so the model's APDMA read
  check (`test-i2c-model.h:179-181`) fails once the transport legitimately
  reads DMA EN. Candidate `tests/nord2-audio/test-inherited.c` seeds
  `bm.ifr1/ifr2` from the snapshot and covers that harness's
  cycles/rate/status/adsp/drift intents plus more.
