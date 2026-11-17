# Audio task — STATUS

Owner: Claude Opus 5.5 (audio worker). Workspace: research/nord2/audio/claude-opus only.

## Current: revision5 (2026-10-21) — IN PROGRESS, base = frozen revision4
Baseline: ../revision4 (manifest SHA 969df856e0ba12c110f997ea67905797561f5c3a695a54dcc72e777af3bd888d),
copied by the parent to candidate-next/. Revision4 supersedes my revision2 notes below
(and the DeepSeek revision3 claim that revision2 compiled empty, which was wrong).

Planned and being written:
1. Amp-ID (mode1 snapshot / mode2 I2C8 PIO REV probe): tighten, don't widen.
2. New separate component `audio_afe.{h,c}`: source-derived AFE "silent stream"
   (MTCMOS + AFE clocks + APLL2 + I2S3 + DL1 from audio SRAM, zeros only) programme,
   gate on measured prerequisites, executor and teardown. No call site executes it.
3. Host tests extended; exact pending command for parent:
   `python3 candidate-next/tools/test-nord2-audio-id.py`

## History (superseded)
- Revision1: GPIO bit-bang takeover — rejected by parent review (A1-A4, T1).
- Revision2 (mine): snapshot-only mode1 + I2C8 PIO mode2; later revised by DeepSeek r3 and
  parent r4 (I3C read at AP 0xec, NACK handling, HW timeout). Frozen in ../revision4.
