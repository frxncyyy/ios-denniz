#!/bin/bash
# camflash-20261114: 0560 = camera flash as an MT6360 STROBE (/dev/redwood-torch 12-byte request + read() status).
# a/ = device-mmu-q10jcp-candidate/k/kernel/stage (the q10jcp chain), b/ = patched.  Format = what integ-20261114/merge.sh regenerates.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d -p "$H"); trap 'rm -rf "$T"' EXIT
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
mk 0560 nord2-camera-flash-strobe bsd/dev/sweet_touch.c bsd/dev/nord2_torch.h
