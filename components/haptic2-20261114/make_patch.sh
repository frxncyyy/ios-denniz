#!/bin/bash
# haptic2-20261114: 0611 nord2-aw8697-ram-clicks (2026-11-14) + the alternative strength pieces 0612 (strong/ soft/: one define).
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
mk $H 0611 nord2-aw8697-ram-clicks bsd/dev/nord2_haptic.h bsd/dev/nord2_haptic.inc bsd/dev/nord2_haptic_ram.h
for v in strong:1 soft:2; do
  name=${v%:*}; prof=${v#*:}; d=$H/$name
  rm -rf $d/a $d/b; mkdir -p $d/a/bsd/dev $d/b/bsd/dev
  cp $H/b/bsd/dev/nord2_haptic_ram.h $d/a/bsd/dev/
  sed "s/^#define N2H_RAM_PROFILE         0$/#define N2H_RAM_PROFILE         $prof       \/* 0612 ($name) *\//" $H/b/bsd/dev/nord2_haptic_ram.h > $d/b/bsd/dev/nord2_haptic_ram.h
  ! cmp -s $d/a/bsd/dev/nord2_haptic_ram.h $d/b/bsd/dev/nord2_haptic_ram.h
  mk $d 0612 nord2-haptic-$name bsd/dev/nord2_haptic_ram.h
done
