#!/bin/bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/fakevenc/run_session_hevc.sh -- one host HEVC encode session (INIT, SET_PARAM, SEQ_HDR with
# VPS/SPS/PPS, one IDR, EOS, DEINIT) of the unmodified vendor stack against the fake VENC.  Owner: opus-hevc.
# Copies the sources to /tmp, applies fakevenc/apply_patches.py (opus-venc) and apply_hevc_patches.py
# (opus-hevc) to the COPY, builds the host runner and runs it with N2VHOST_CODEC=hevc; the bitstream is
# checked with tests/check/hevc_check.py (ffprobe/ffmpeg).  Host only, no phone.
#
#   bash gpu/hwcodec/fakevenc/run_session_hevc.sh [--san] [--size WxH]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hw=$(cd "$here/.." && pwd)
root=$(cd "$hw/../.." && pwd)
OUT=${N2HW_OUT:-/tmp/n2hw-opus-hevc/session}
src=$OUT/src/gpu/hwcodec
rm -rf "$OUT/src"
mkdir -p "$src/tests"
for d in emu loader shim vcu ap host include fakevenc; do cp -r "$hw/$d" "$src/"; done
for d in hw emu check; do cp -r "$hw/tests/$d" "$src/tests/"; done
python3 "$src/fakevenc/apply_patches.py" "$src" | sed 's/^/run_session_hevc: /'
python3 "$src/fakevenc/apply_hevc_patches.py" "$src" | sed 's/^/run_session_hevc: /'
export N2HW_VENDOR_DIR=${N2HW_VENDOR_DIR:-$root/gpu/build/cam-vendor-20261109/vfs}
export N2VHOST_AP_TIMEOUT_MS=${N2VHOST_AP_TIMEOUT_MS:-20000}
export N2VHOST_CODEC=hevc
set +e
N2HW_OUT=$OUT nice -n 10 bash "$src/host/run_vhost.sh" --seconds 60 "$@" > "$OUT/session.txt" 2>&1
rc=$?
set -e
grep -E '^N2VHOST (encode|bitstream|fake-venc|M-[a-f]|hw |vcu )|^run_vhost.sh: .*(validated|FAILED|not validated)' \
	"$OUT/session.txt" | sed 's/^/run_session_hevc: /' || true
rm -f "$OUT/run/run-o2.log" "$OUT/run/run-san.log"
tail -1 "$OUT/session.txt"
exit $rc
