#!/bin/bash
# sbshift-20261114: 0551 = 0550 band rotation off by default + single-layer eligibility + measured geometry (SweetFramebuffer.cpp, sweet_fb_cutout.h).
#  a/ = the q10jcp stage files (full chain incl. 0550), b/ = a/ + 0551.
# Format = bltorch-20261114/make_patch.sh (diff -u, new files from /dev/null, applied back with --fuzz=0).
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
mk 0551 nord2-fb-cutout-off-by-default a b iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_cutout.h
