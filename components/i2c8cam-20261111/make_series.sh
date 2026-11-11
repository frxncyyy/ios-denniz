#!/bin/bash
# SPDX-License-Identifier: MIT
# make_series.sh -- the speaker-after-camera fix (opus-i2c, 2026-11-11), kernel series numbers 0330/0331.
#   0330-nord2-audio-cam-power-not-a-refusal.patch  bsd/dev/audio_hw.c (source record) + the build pipeline's binary
#        edit of the CI members audio_i2c8.o / audio_hw.o (patch_audio_objs_0330.py, run by build_p1_candidate.py
#        when 0330 is in --series): the three "CAM MTCMOS on -> refuse" branches become nops.
#   0331-nord2-cam-fan-i2c8-per-transaction-lease.patch  bsd/dev/nord2_cam.inc + audio_hw_native.inc (both compiled
#        in sweet_touch.o): the camera takes the I2C8 lease per FAN53870 transaction, gives the pins back each time
#        (bus clear first if SDA is stuck low); the audio worker waits up to 4 ms for a camera-held lease.
# base/ = the q10in stage files (checked here), src/ = the patched files. Checks that both apply in order with
# `patch -p1` (as build_p1_candidate.py applies them) to a copy of the q10in stage files and reproduce src/.
#   bash make_series.sh          write patches + MANIFEST.txt
#   bash make_series.sh --check  fail if the patches are stale
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); W=$(cd "$D/../../.." && pwd)
STAGE=$W/gpu/build/device-mmu-q10in-candidate/k/kernel/stage
T=${TMPDIR:-/tmp}/n2i2c8-series; rm -rf "$T"; mkdir -p "$T/out"
FILES="audio_hw.c nord2_cam.inc audio_hw_native.inc"
for f in $FILES; do
  cmp -s "$D/base/bsd/dev/$f" "$STAGE/bsd/dev/$f" || { echo "base/$f differs from the q10in stage" >&2; exit 1; }
done
mk() { # name files...
  local name=$1; shift; : > "$T/out/$name"
  for f in "$@"; do
    (cd "$D" && diff -u --label "a/bsd/dev/$f" --label "b/bsd/dev/$f" "base/bsd/dev/$f" "src/bsd/dev/$f") >> "$T/out/$name" || true
  done
}
mk 0330-nord2-audio-cam-power-not-a-refusal.patch audio_hw.c
mk 0331-nord2-cam-fan-i2c8-per-transaction-lease.patch nord2_cam.inc audio_hw_native.inc
mkdir -p "$T/tree/bsd/dev"; for f in $FILES; do cp "$STAGE/bsd/dev/$f" "$T/tree/bsd/dev/"; done
for p in 0330-nord2-audio-cam-power-not-a-refusal.patch 0331-nord2-cam-fan-i2c8-per-transaction-lease.patch; do
  (cd "$T/tree" && patch -p1 -s -i "$T/out/$p")
done
for f in $FILES; do cmp "$T/tree/bsd/dev/$f" "$D/src/bsd/dev/$f"; done
if [ "${1:-}" = --check ]; then
  for p in "$T"/out/*.patch; do cmp "$p" "$D/$(basename "$p")"; done
  echo "patches up to date"; exit 0
fi
cp "$T/out/"*.patch "$D/"
(cd "$D" && sha256sum base/bsd/dev/* src/bsd/dev/* 03*.patch patch_audio_objs_0330.py shim-0330.diff test/test-cam-power.c) > "$D/MANIFEST.txt"
echo "wrote:"; wc -l "$D"/03*.patch
