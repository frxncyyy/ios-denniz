#!/bin/bash
# bltorch-20261114: 0501 = brightness (CoreBrightness legacy "brightness" requests, safe floor, n2bl=2 new architecture),
# 0502 = flashlight capability (IODeviceTree:/product/camera flash).  a/ = the q10jch stage (chain + 0491 0492 0494r 0495 0496 0500),
# b/ = a/ + 0501, c/ = b/ + 0502.  Format = what integ-20261114/merge.sh regenerates (diff -u, new files from /dev/null).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d -p "$H" .mk.XXXXXX); trap 'rm -rf "$T"' EXIT
mk() {
  local n=$1 name=$2 from=$3 to=$4; shift 4; local out=$H/$n-$name.patch; : > "$out"
  for f in "$@"; do
    if [ -f "$from/$f" ]; then diff -u "$from/$f" "$to/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true
    else diff -u /dev/null "$to/$f" | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; fi
  done
  rm -rf "$T/s"; mkdir "$T/s"; cp -a $from/. "$T/s/"
  (cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
  ! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
  rm -rf "$H/patched-sources-$n"
  for f in "$@"; do cmp "$T/s/$f" "$to/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "$to/$f" "$H/patched-sources-$n/$f"; done
  sha256sum "$out"
}
mk 0501 nord2-backlight-legacy a b iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_backlight.h
mk 0502 nord2-torch-capability b c iokit/Kernel/arm/SweetFramebuffer.cpp
