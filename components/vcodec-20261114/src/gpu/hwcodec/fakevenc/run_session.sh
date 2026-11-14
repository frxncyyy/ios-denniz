#!/bin/bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/fakevenc/run_session.sh -- one reproducible host encode session against the fake VENC.
# Copies the track V/T sources of the work tree to /tmp, adds fakevenc/, applies fakevenc/apply_patches.py
# to the COPY (the work tree is never edited), builds the host runner and runs vpud + the vendor libraries
# through SEQ_HDR + one IDR frame + the EOS flush.  Host only, no phone; build products under $OUT.
#
#   bash gpu/hwcodec/fakevenc/run_session.sh [--san] [--size WxH] [--seconds N]
#
# Last line: the runner's "N2VHOST reached M-x" line; before it, the encode lines and the T02 H.264 check.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hw=$(cd "$here/.." && pwd)
root=$(cd "$hw/../.." && pwd)
OUT=${N2HW_OUT:-/tmp/n2hw-opus-venc}
src=$OUT/src/gpu/hwcodec
rm -rf "$OUT/src"
mkdir -p "$src/tests"
for d in emu loader shim vcu ap host include fakevenc; do cp -r "$hw/$d" "$src/"; done
for d in hw emu check; do cp -r "$hw/tests/$d" "$src/tests/"; done
python3 "$src/fakevenc/apply_patches.py" "$src" | sed 's/^/run_session: /'
export N2HW_VENDOR_DIR=${N2HW_VENDOR_DIR:-$root/gpu/build/cam-vendor-20261109/vfs}
export N2VHOST_AP_TIMEOUT_MS=${N2VHOST_AP_TIMEOUT_MS:-20000}
set +e
N2HW_OUT=$OUT bash "$src/host/run_vhost.sh" --seconds 60 "$@" > "$OUT/session.txt" 2>&1
rc=$?
set -e
grep -E '^N2VHOST (encode|bitstream|fake-venc|M-[a-f]|hw |vcu )|^run_vhost.sh: .*(validated|FAILED|not validated)' \
	"$OUT/session.txt" | sed 's/^/run_session: /' || true
# The guest log of a long run is large (vpud's log poll); keep the report, drop the log.
rm -f "$OUT/run/run-o2.log" "$OUT/run/run-san.log"
tail -1 "$OUT/session.txt"
exit $rc
