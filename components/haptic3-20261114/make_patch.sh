#!/bin/bash
# haptic3-20261114: 0613 nord2-haptic-train-paths (on 0611).
# Base a/ = the final3 chain (q10jcz) nord2_haptic.{h,inc} (== haptic-20261114/b, 0610).  Host tests: test/run.sh.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d -p "$H" .mk.XXXXXX); trap 'rm -rf "$T"' EXIT
mk() { d=$1; n=$2; name=$3; shift 3; out=$d/$n-$name.patch; : > "$out"
  for f in "$@"; do
    if [ -f "$d/a/$f" ]; then diff -u "$d/a/$f" "$d/b/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true
    else diff -u /dev/null "$d/b/$f" | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; fi
  done
  rm -rf "$T/s"; mkdir -p "$T/s"; cp -a $d/a/. "$T/s/"
  (cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$d/apply-$n.log"
  ! grep -E 'offset|fuzz|FAILED|Reversed' "$d/apply-$n.log"
  rm -rf "$d/patched-sources-$n"
  for f in "$@"; do cmp "$T/s/$f" "$d/b/$f"; mkdir -p "$d/patched-sources-$n/$(dirname "$f")"; cp "$d/b/$f" "$d/patched-sources-$n/$f"; done
  add=$(grep '^+' "$out" | grep -v '^+++' | grep -c 'ml_io_map[_a-z]*(' || true); del=$(grep '^-' "$out" | grep -v '^---' | grep -c 'ml_io_map[_a-z]*(' || true)
  [ "$add" = "$del" ] || { echo "$n changes the number of ml_io_map lines (+$add -$del)"; exit 1; }
  sha256sum "$out"
}
mk $H 0613 nord2-haptic-train-paths bsd/dev/nord2_haptic.h bsd/dev/nord2_haptic.inc bsd/dev/nord2_haptic_ram.h
