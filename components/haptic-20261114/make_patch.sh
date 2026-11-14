#!/bin/bash
# haptic-20261114: 0610 nord2-aw8697-haptics (2026-11-14).  Base a/ = the q10jcp stage (device-mmu-q10jcp-candidate/k/kernel/stage).
# Host tests: test/run.sh.  Nothing here writes outside this directory.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d -p "$H" .mk.XXXXXX); trap 'rm -rf "$T"' EXIT
mk() { n=$1; name=$2; shift 2; out=$H/$n-$name.patch; : > "$out"
  for f in "$@"; do
    if [ -f "a/$f" ]; then diff -u "a/$f" "b/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true
    else diff -u /dev/null "b/$f" | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; fi
  done
  rm -rf "$T/s"; mkdir "$T/s"; cp -a a/. "$T/s/"
  (cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
  ! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
  rm -rf "$H/patched-sources-$n"
  for f in "$@"; do cmp "$T/s/$f" "b/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "b/$f" "$H/patched-sources-$n/$f"; done
  # io_submap: no ml_io_map call is added
  add=$(grep '^+' "$out" | grep -v '^+++' | grep -c 'ml_io_map[_a-z]*(' || true); del=$(grep '^-' "$out" | grep -v '^---' | grep -c 'ml_io_map[_a-z]*(' || true)
  [ "$add" = "$del" ] || { echo "$n changes the number of ml_io_map lines (+$add -$del)"; exit 1; }
  sha256sum "$out"
}
mk 0610 nord2-aw8697-haptics bsd/dev/nord2_haptic.h bsd/dev/nord2_haptic.inc bsd/dev/sweet_touch.c iokit/Kernel/arm/SweetFramebuffer.cpp
