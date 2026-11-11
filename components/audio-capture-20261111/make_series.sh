#!/bin/bash
# SPDX-License-Identifier: MIT
# make_series.sh -- the microphone patches for the phone's kernel series (after 0310/0311 of q10jw; numbers 0320/0321).
#   0320-nord2-mic-capture-afe.patch   bsd/dev/audio_call.c/.h: capture-only AFE, async MT6359 tables, VUL12 drain
#   0321-nord2-mic-devfs-worker.patch  bsd/dev/audio_mic.inc (new) + audio_device.c: /dev/redwood-mic, jitter buffer,
#                                      the worker's NM_* state machine, NORD2 MIC status line
# base/ = the q10in stage files (checked here), src/ = the patched files.  Checks that both apply in order with
# `patch -p1` (as build_p1_candidate.py applies them) to a copy of the q10in stage's audio files and reproduce src/.
#   bash make_series.sh          write patches/ + MANIFEST.txt
#   bash make_series.sh --check  fail if patches/ is stale
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); W=$(cd "$D/../../.." && pwd)
STAGE=$W/gpu/build/device-mmu-q10in-candidate/k/kernel/stage
T=${TMPDIR:-/tmp}/n2mic-series; rm -rf "$T"; mkdir -p "$T/out"
for f in audio_call.c audio_call.h audio_device.c; do
  cmp -s "$D/base/bsd/dev/$f" "$STAGE/bsd/dev/$f" || { echo "base/$f differs from the q10in stage" >&2; exit 1; }
done
[ ! -e "$STAGE/bsd/dev/audio_mic.inc" ] || { echo "the stage already has audio_mic.inc" >&2; exit 1; }
mk() { # name files...
  local name=$1; shift; : > "$T/out/$name"
  for f in "$@"; do
    if [ -e "$D/base/bsd/dev/$f" ]; then (cd "$D" && diff -u --label "a/bsd/dev/$f" --label "b/bsd/dev/$f" "base/bsd/dev/$f" "src/bsd/dev/$f") >> "$T/out/$name" || true
    else (cd "$D" && diff -u --label /dev/null --label "b/bsd/dev/$f" /dev/null "src/bsd/dev/$f") >> "$T/out/$name" || true; fi
  done
}
mk 0320-nord2-mic-capture-afe.patch audio_call.c audio_call.h
mk 0321-nord2-mic-devfs-worker.patch audio_device.c audio_mic.inc
mkdir -p "$T/tree/bsd/dev"; cp "$STAGE"/bsd/dev/audio_call.[ch] "$STAGE/bsd/dev/audio_device.c" "$T/tree/bsd/dev/"
for p in 0320-nord2-mic-capture-afe.patch 0321-nord2-mic-devfs-worker.patch; do (cd "$T/tree" && patch -p1 -s -i "$T/out/$p"); done
for f in audio_call.c audio_call.h audio_device.c audio_mic.inc; do cmp "$T/tree/bsd/dev/$f" "$D/src/bsd/dev/$f"; done
if [ "${1:-}" = --check ]; then
  for p in 0320-nord2-mic-capture-afe.patch 0321-nord2-mic-devfs-worker.patch; do cmp "$T/out/$p" "$D/patches/$p"; done
  echo "patches up to date"; exit 0
fi
mkdir -p "$D/patches"; cp "$T/out/"*.patch "$D/patches/"
(cd "$D" && sha256sum base/bsd/dev/* src/bsd/dev/* patches/*.patch) > "$D/MANIFEST.txt"
echo "wrote:"; wc -l "$D"/patches/*.patch
