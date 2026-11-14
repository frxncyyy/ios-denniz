#!/bin/bash
# rtc-20261114: 0540 nord2-mt6359-rtc (2026-11-14).  Base a/ = the q10jcm stage (device-mmu-q10jcm-candidate/k/kernel/stage = the chain
# through 0531 with 0491 0492 0494 0495 0496 0500 0501 0502 0530 0531).
#   bsd/dev/nord2_rtc.h          new: MT6359P RTC register model, allow-list, read/write/unlock sequences, sync policy (host-tested)
#   bsd/dev/nord2_rtc.inc        new: the XNU side on the PWRAP owner thread (boot read, handshake with the first exec, 15 s sync, sysctls)
#   bsd/dev/sweet_touch.c        includes it; ni_thread maps ni_pwrap before its 2 s wait and reads the RTC first; n2rtc_service() per tick
#   bsd/kern/nord2_clockfloor.h  + n2ck_pick_rtc (RTC primary, 0530 floor = fallback / lower bound)
#   bsd/kern/kern_exec.c         the 0530 first-exec hook takes the RTC time (bounded wait) before the floor
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
  # io_submap: the one ml_io_map line in the patch is the existing ni_pwrap mapping moved up (net 0 call sites)
  add=$(grep '^+' "$out" | grep -v '^+++' | grep -c 'ml_io_map[_a-z]*(' || true); del=$(grep '^-' "$out" | grep -v '^---' | grep -c 'ml_io_map[_a-z]*(' || true)
  [ "$add" = "$del" ] || { echo "$n changes the number of ml_io_map lines (+$add -$del)"; exit 1; }
  sha256sum "$out"
}
mk 0540 nord2-mt6359-rtc bsd/dev/nord2_rtc.h bsd/dev/nord2_rtc.inc bsd/dev/sweet_touch.c bsd/kern/nord2_clockfloor.h bsd/kern/kern_exec.c
