# mm21 — STATUS (opus-mm21, 2026-11-11)

Host-tested and compiled for iOS. Not run on arm64, not run on the phone.

## Done
- `n2_mm21.h` / `n2_mm21.c` (MIT): MM21 -> NV12 (direct and staged/fetch-based), single-plane detile, NV12 -> MM21 inverse;
  portable C and arm64 NEON inner loop (`__ARM_NEON`).
- `n2_mm21_neon_emu.h`: test-only plain-C stand-in for `vld1q_u8`/`vst1q_u8` so the NEON code path runs on x86.
- `t_mm21.c` + `run_tests.sh`: see facts/MM21_LAYOUT.md section 3.
- `facts/MM21_LAYOUT.md`: layout facts (verified vs inferred), converter description, output-path design (section 5),
  phone checks (section 6).

## Commands and last lines
    bash gpu/hwcodec/mm21/run_tests.sh
    -> N2TEST MM21 PASS 5/5     (4 host builds x 167 checks + arm64-ios compile; ~7 s; artifacts /tmp/n2hw-opus-mm21/rt)
    bench line (i5-10210U, one core, 1080p cold): ~0.38-0.52 ms/frame direct, memcpy of the same bytes 0.29 ms

## Not done / risks
- That the phone's VDEC writes exactly this layout is inferred (agrees with libyuv's MM21, not yet with a real frame).
- Real NEON instructions never executed (no arm64 machine/emulator on the host); the NEON control flow is tested via the stand-in.
- Phone timing unknown; estimate 1-3 ms per 1080p frame including RWVC_DMA_RW.
- 10-bit formats (MT2T/MT2R) and the compressed MT21 (UFO) are not supported (by design: layout.mode=0 is required).
- Integration into DEC06's plug-in and DEC05's DEC_PICTURE_OK crop fields: proposed on the board, not landed.
