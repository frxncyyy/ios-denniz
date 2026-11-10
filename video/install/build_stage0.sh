#!/bin/bash
# SPDX-License-Identifier: MIT
# LEAD. The stage-0 products (PLAN.md section 3), built from a private copy of exactly the finished, reviewed sources so that
# packages still being written (avc/, kern/, probe/av/, jpeg/) cannot change or break what goes onto the phone:
#   Slim.videoencoder  = vt/n2vt_shim.c + vt/n2vt_log.c            (registers Apple's software HEVC encoder; nothing else)
#   n2vt_probe         = probe/*.c without probe/av                 (-DPROBE_NO_AV)
# usage: build_stage0.sh [outdir]     (default gpu/video/build/stage0)
set -euo pipefail
V=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$V/build/stage0}
T=$OUT/tree
rm -rf "$T"; mkdir -p "$T/vt" "$T/probe" "$OUT/products"
cp -r "$V/include" "$V/sdk" "$T/"
cp "$V/build_ios.sh" "$T/"
cp "$V/vt/n2vt_shim.c" "$V/vt/n2vt_log.c" "$V/vt/n2vt_log.h" "$T/vt/"
grep -v -E '_n2avc|CMDerivedObjectCreate|CMBaseObjectGetDerivedStorage' "$V/vt/symbols.txt" > "$T/vt/symbols.txt" || true
cp "$V"/probe/*.c "$V"/probe/*.h "$V/probe/symbols.txt" "$T/probe/"
mkdir -p "$T/vectors"; [ -f "$V/vectors/embedded_vectors.h" ] && cp "$V/vectors/embedded_vectors.h" "$T/vectors/"
if [ "${STAGE_AV:-0}" = 1 ]; then mkdir -p "$T/probe/av"; cp "$V"/probe/av/*.c "$V"/probe/av/*.h "$V/probe/av/symbols.txt" "$T/probe/av/"; export PROBE_NO_AV=0; else export PROBE_NO_AV=1; fi
bash "$T/build_ios.sh" "$OUT/products" > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
cp "$OUT/products/Slim.videoencoder" "$OUT/products/n2vt_probe" "$OUT/"
( cd "$OUT" && sha256sum Slim.videoencoder n2vt_probe | tee SHA256SUMS && ls -l Slim.videoencoder n2vt_probe )
