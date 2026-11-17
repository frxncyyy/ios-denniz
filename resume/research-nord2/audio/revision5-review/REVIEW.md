# Private revision 5 audio salvage review

This is a source-only candidate, based on the stopped Claude Opus `candidate-next` snapshot in `input/` (SHA manifest `547baeebe2244e46316a5778d966a0ea1f2a604304ff1d00b194ba240516f949`). The selected Android tree is `../nord2-current-kernel` at `04d00c2565764c3922c9b4826b5acff1106e533f`. The Claude tree and frozen revision 4 were not edited. There is no AFE execution call site, device run, CI, or claim that audio works.

## Concrete corrections in `candidate/`

* `audio_afe.c/h`: `POWERON_CONFIG_EN` now has to equal the **whole** keyed word `(0xb16 << 16) | 1` before the plan writes it, and the live check compares the whole word. This avoids replacing a distinct inherited high half while reporting a cold restore. The selected PG path writes that whole word in `clk-mt6885-pg.c:3267-3269, 3369-3371`.
* `audio_afe.c/h`: ADSP must have both status bits clear and the selected `ADSP_PWR_CON` cold fields (`0x227f` mask, `0x32` value); the live watch includes that register. `clk-mt6885-pg.c:3370-3416` clears ON/ON_2ND/RST_B, sets ISO/CLK_DIS/SRAM_CKISO and clears SRAM_ISOINT_B/SLEEP_B/ACK during power-down. Status alone can be zero during an unfinished transition. This is a stricter refusal gate and changes no ADSP write.
* Revision 5's new binder calls `naf_gate` and `naf_pll_pcw`, but its copied test link and integration patch omitted `audio_afe.c`. The private test script and three integration patch copies now include `audio_afe.c` (and its header in the packaging list). No active build files were edited. The integration diff is a proposal against an earlier active context and still needs rebase by the sole integrator.
* The copied binding fixture assumed any value could be written in I2C8 restore mode. The candidate binder instead accepts only the same transaction's saved stage-9 word. The fixture now proves uncaptured values fail, captured values pass, and a changed value fails. No production I2C8 bus behavior was broadened.
* The SRAM source reference in `audio_afe.h` now points to selected `mt6893.dts:8059-8065`, which specifies base `0x11212000`, size `0x18000` and block size `0x1000`.

## Verification

* `python3 research/nord2/audio/revision5-review/candidate/tools/test-afe.py` — ASan/UBSan PASS. A fixed register model checks clean gate, source-order plan (64 up, 43 down), allowlisted writes, 4096-byte zero fill, cold teardown, bounded pointer advance, non-advancing functional failure with teardown, first and final write failure latches, prewrite refusal and no replay. The model is not a hardware timing or clock proof.
* `CROSS=1 python3 research/nord2/audio/revision5-review/candidate/tools/test-nord2-audio-id.py` — ASan/UBSan PASS for prior snapshot/I2C8 binding and same-transaction restore, plus freestanding ARM64 guard compiles.
* `python3 tools/nord2-xnu-cc.py research/ci-39398655023/buildtree bsd research/nord2/audio/revision5-review/candidate/shim/nord2-xnu/audio_afe.c /tmp/audio-afe-rev5.arm64.o -I<absolute candidate/shim/nord2-xnu>` — PASS with normal CI-derived XNU headers and NORD2 board selection; object exports `naf_build`, `naf_cycle`, `naf_gate`, `naf_return_ok`. `audio_id.c` was also compiled with that normal wrapper.

## Pending gates

The `naf_cycle` component is intentionally uncalled. It has no reviewed native callback/mapping for the on-chip SRAM page or full stage-aware AFE write/read permissions. The current binder maps one AFE register page for read-only ID and reports `NAF_G_*` from already captured data; it does not authorize silent streaming. A future integrator must first review an adapter and exact current-device cold/amp/APLL2 snapshot. A gate refusal on real hardware is an expected possible outcome. The shared I2C8 DDR/UFS rail, amplifiers, PMIF, DRAM DMA and interrupt controller remain outside this AFE plan. No audio playback, speaker output, firmware readiness or safe hardware execution has been shown.
