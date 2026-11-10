#!/bin/bash
# SPDX-License-Identifier: MIT
# LEAD. Makes the transaction payload from built products.
# usage: stage_payload.sh TAG PRODUCT_DIR [PAYLOAD_DIR] [--no-probe] [--on SWITCH]... [--off SWITCH]...
#   PRODUCT_DIR has Slim.videoencoder and n2vt_probe (install/build_stage0.sh's output, or build/ios).
#   --no-probe: the gate file is removed, so the probe does not run at the next boots (the final state for the user).
set -euo pipefail
V=$(cd "$(dirname "$0")/.." && pwd)
TAG=$1; SRC=$2; shift 2
OUT=$V/install/payload
case "${1:-}" in --*|"") ;; *) OUT=$1; shift ;; esac
PROBE=true; TCC=false; CAMH264=false; ON=(); OFF=()
while [ $# -gt 0 ]; do case $1 in --no-probe) PROBE=false ;; --tcc) TCC=true ;; --camera-h264) CAMH264=true ;; --on) ON+=("$2"); shift ;; --off) OFF+=("$2"); shift ;; *) echo "unknown $1" >&2; exit 2 ;; esac; shift; done
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$SRC/Slim.videoencoder" "$SRC/n2vt_probe" "$V/probe/com.nord2.videoprobe.plist" "$OUT/"
chmod 755 "$OUT/Slim.videoencoder" "$OUT/n2vt_probe"
python3 - "$OUT" "$TAG" "$PROBE" "${ON[*]:-}" "${OFF[*]:-}" "$TCC" "$CAMH264" <<'PY'
import hashlib, json, sys, os
out, tag, probe, on, off, tcc, camh264 = sys.argv[1:8]
sha = lambda n: hashlib.sha256(open(os.path.join(out, n), 'rb').read()).hexdigest()
plan = {"purpose": "gpu/video: VideoToolbox encoder plug-in (/System/Library/VideoEncoders/Slim.videoencoder), the probe (/usr/libexec/n2vt_probe) and its launchd job com.nord2.videoprobe. Undo: gpu/video/install/guest-video-undo.sh.",
        "tag": tag, "probe": probe == 'true', "tcc": tcc == 'true', "camera_h264": camh264 == 'true', "switches_on": on.split(), "switches_off": off.split(),
        "sha256": {n: sha(n) for n in ("Slim.videoencoder", "n2vt_probe", "com.nord2.videoprobe.plist")}}
json.dump(plan, open(os.path.join(out, 'plan.json'), 'w'), indent=1); print(json.dumps(plan, indent=1))
PY
