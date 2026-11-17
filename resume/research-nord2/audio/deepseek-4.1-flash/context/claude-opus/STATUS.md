# Audio task — STATUS

Owner: Claude Opus 5.5 (audio worker). Workspace: research/nord2/audio/claude-opus only.
Snapshot: source-snapshot @ 3d3725a28a238b140d49ce8c42815e5d25ba30a6.
Bash: only simple `ls` allowed. **No build or test was executed by this worker.**

## Revision 2 (2026-10-21) — in progress
Responding to ../parent-review/REVIEW.md (A1-A4, T1).
- Decision: delete the GPIO bit-bang I2C8 takeover entirely (no GPIO/IOCFG writes remain).
- `nord2audioid=1`: non-mutating snapshot only (zero MMIO writes). Adds read-only I2C8
  controller/APDMA/clock-gate observation, read only when the clocks are proven ungated.
- `nord2audioid=2`: snapshot + hardware I2Cv2 PIO REV probe derived from the reviewed
  battery transport (canonical shim/nord2-xnu/mtk_i2c_read.c, read-only reference),
  re-derived for bus 8 (multi-channel AP 0x100 / CCU 0x200 / shadow 0). Sticky offline on
  any non-clean result; no recovery, no replay, no next-amp continuation. Not authorized
  for hardware; for review only.
