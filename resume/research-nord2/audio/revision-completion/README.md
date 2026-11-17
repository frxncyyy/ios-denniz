# Completed private audio revision2

This is a new private copy of the interrupted Opus revision under ../claude-opus/candidate. The original workspace remains unchanged. No canonical/active source, image, CI, device or hardware access occurred. The Opus source map in ../claude-opus/AUDIO-REPORT.md remains useful for board/AFE/TFA source anchors; its old bitbang milestone and integration directions are superseded here. Selected kernel HEAD04d00c2565764c3922c9b4826b5acff1106e533f is4.19.325, not4.14.

## Outcome and modes

Default nord2audioid absent/0/unsupported values return before any mapping/callback. Mode1 reads snapshots only: source allowlisted always-on SPM/infracfg/top/APMixed/GPIO/IOCFG registers, guarded AFE registers, I2C8 shadow/AP/CCU state only with existing mux/PLL/gate enabled, and APDMA only with its source gates open. There is no write callback in the snapshot core. Snapshot readiness is a captured report, not physical bus ownership or amplifier identification.

Mode2 adds a source-derived hardware I2Cv2 AP-channel PIO probe only if snapshot readiness succeeds; the transport repeats live preflight. This mode writes controller configuration/FIFO/W1C/START and target register pointers. It is not a wholly read-only operation, not approved for hardware, and does not initialize audio, move audio samples or make sound. No GPIO takeover/line drive/pinmux, gate enable, PMIC, reset, DMA, recovery pulses or writes to amplifier configuration exist. The copied hardware transport is a concrete private candidate, not an active driver.

## Shared bus source contract

Saved liveDT:9466–9551 identifies I2C8=11d04000, APDMA=11000780, AP channel100 and CCU channel200, GPIO122/123 fn1, amplifier34/37 and reset111/148. Enabled DDR/UFS bucks51/56 share it. Selected i2c-mtk.c:79–87 distinguishes channel-offset accesses from shadow;937–946 selects AP default or CCU;978–1203 defines transfer programming and multi-channel FIFO clear5;1203 writes MCU_INTR enable on the selected channel;1253ff handles resume arbitration. i2c-mtk.h V2 table supplies register offsets. Register model enforces those exact accesses and target address/pointer sequence, not a bitbang wire model.

Before writes: selected stable mux/PLL/rate and100k timing, existing GPIO mode1, existing input-enable bits3/13 at11d10070, observed pads122/123 high at10005230, CAM power off in both SPM status copies, existing controller gate bit4 ungated, no active APDMA when readable, no START/RESUME on shadow/AP/CCU, no DMA mode on any channel, no FIFO data on any channel, and AP IRQ0 or exact completion1. Exact completion1 uses narrow idle rechecks/W1C1/readback0 derived from the independently reviewed battery handoff; other status is never cleared. No gated aperture is enabled merely to inspect it. Gated DMA cannot currently run, but this does not prove its historical contents or a future owner's intent.

The CAM-off condition excludes the selected source CCU execution domain at that instant. Current XNU has no other I2C8 owner; one input thread owns this attempt. Pad-high/idle snapshots are prerequisites, not proof against unknown external masters. A new camera/CCU/I2C8 owner invalidates this policy. These remain explicit source/lifetime assumptions for independent review, not an electrical ownership claim. No pointer allowlist is used to excuse an unknown inherited frame.

PIO saves fourteen AP-channel config words, programs WRRD address plus one register pointer and two-byte read, polls exact completion1 for at most2000*10us, reads big-endian TFA data, clears only its completion/FIFO, restores saved configuration writes, and barriers through the native callback. No start/reset/DMA/clock/pinmux restore occurs. Restoration uses completed MMIO write callbacks; it does not independently read back all saved controller configuration or establish electrical line state. No absolute deadline can abort an MMIO bus fault. These hardware limits remain explicit.

Any non-clean result makes the bus offline: no replay, retries, recovery or next amplifier. A failure after a possibly committed write also latches uncertain; Linux return refuses through a pure state check with zero hardware callbacks, even if a later transfer could succeed. Before-write refusal leaves hardware untouched and may return. Distinguish that from uncertainty after inherited completion acknowledgement, which is itself a write.

## Variant contract

First read only REV03. Exact revisions0a74/0b74/0c74/0a73/0b73/1a73 have selected tfa_init.c tables and may then read SYS000 and STATUS14 (common field maps). Known other families, unlisted73/74 revisions and unknown REV retain raw identification and perform no further register-pointer reads on that amplifier. Any transport failure stops both-amplifier sequence. Report reads1 means SYS0/STATUS14 are NOT_READ; their zero-initialized storage is not a hardware value. No reset release or inferred amplifier supply is attempted.

## Changes completing the interrupted revision

Opus had replaced core/native sources and added the register model, but left v1 tests, runner and docs. This completion replaces the fixtures/runner with actual revision2 tests; adds existing IES/pad-high, shadow DMA and all-channel FIFO refusals; expands native allowlists only for those exact read-only preconditions; replaces the oversized PLL report with bounded lines; and creates a fresh three-site private sweet_touch integration diff. The old unused bitbang model is omitted. The original Opus source remains available unchanged for comparison; parent never needs to infer which version a test exercised.

## Validation and integration boundary

`CROSS=1 python3 candidate/tools/test-nord2-audio-id.py` passes ASan/UBSan core and actual native binding tests: defaultoff, snapshot zero writes, aperture gating, all snapshot read faults, full real controller-model REV/pointer reads, every callback failing along a clean PIO transaction, delay failure,2000-poll timeout, no subsequent callback/replay/slave on error, unknown-family firstread-only, inherited completion/stickyIRQ, CAM/CCU/shadow/APDMA/FIFO/pad/input-enable guards, sticky pure return and diagnostic lines<255 even raw snapshot wordsffffffff. Shared-bus bystanders are never addressed in modeled transactions. Host execution never calls native MMIO/SMC.

`compile/commands.json` records CI-derived flags39398655023 and active source hash. A private current sweet_touch copy with include/call/return-guard and both real helper C files compiles ARM64 with board_config-derived NORD2, no forced -DNORD2. Alternate T8103 compiles empty helper objects. This is actual native-binding compilation, not only a prototype mock. Host cross mode defines NORD2 to compile standalone helpers; it is not the production board-selection proof.

`candidate/patch/audio-id-integration.diff` is review material only, against the captured active sweet_touch snapshot. Parent must separately wire both new helper TUs and headers through its generator and bsd files list, regenerate patch, run full CI and verify an exact image before requesting hardware authorization. No integration or hardware approval is implied by these tests. Parent's current Wi-Fi work remains primary. A mode1 snapshot image can be reviewed separately before considering mode2; no audible output or iOS audio service is claimed.
