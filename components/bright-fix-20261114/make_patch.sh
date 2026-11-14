#!/bin/bash
# bright-fix-20261114: 0495 = panel brightness (AppleARMBacklight for CoreBrightness, DCS 0x51), 0496 = torch (/dev/redwood-torch ->
# MT6360 FLED).  a/ = integ-20261114/chain/s (= the q10jia/q10jib chain; 0491..0494 touch none of these files), b/ = patched.
# Format = what integ-20261114/merge.sh regenerates (diff -u, new files from /dev/null), so merge_check.sh can compare byte for byte.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mk() {
  local n=$1 name=$2; shift 2; local out=$H/$n-$name.patch; : > "$out"
  for f in "$@"; do
    if [ -f "a/$f" ]; then diff -u "a/$f" "b/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true
    else diff -u /dev/null "b/$f" | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; fi
  done
  rm -rf "$T/s"; mkdir "$T/s"; cp -a a/. "$T/s/"
  (cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
  ! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
  rm -rf "$H/patched-sources-$n"
  for f in "$@"; do cmp "$T/s/$f" "b/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "b/$f" "$H/patched-sources-$n/$f"; done
  sha256sum "$out"
}
mk 0495 nord2-display-backlight iokit/Kernel/arm/SweetFramebuffer.cpp iokit/Kernel/arm/sweet_fb_backlight.h
mk 0496 nord2-torch-mt6360 bsd/dev/sweet_touch.c bsd/dev/nord2_torch.h
