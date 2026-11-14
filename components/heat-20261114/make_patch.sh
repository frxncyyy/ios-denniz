#!/bin/bash
# heat-20261114: kernel side of the standby-heat fix (2026-11-13).  Base a/ = the q10jch stage (device-mmu-q10jch-candidate/k/kernel/stage).
#   0530 nord2-clock-floor     bsd/kern/kern_exec.c + bsd/kern/nord2_clockfloor.h: the first execve raises the 1970 calendar to the root
#                              volume's last modification time (kill switch n2ck=0 / n2gdv +64).  Root cause of coreduetd's endless CPU.
#   0531 nord2-scp-motion-off  bsd/dev/nord2_scp.inc: accelerometer/gyroscope/magnetometer (dropped by SweetHID anyway) not enabled,
#                              100 ms polling (kern.n2scp_motion=1 / n2scpm=1 / n2gdv +128 = the old behaviour).
# The two patches touch different files.  Host tests: test/run.sh.  Nothing here writes outside this directory.
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
  ! grep '^+' "$out" | grep -q 'ml_io_map' || { echo "$n adds an ml_io_map call"; exit 1; }
  sha256sum "$out"
}
mk 0530 nord2-clock-floor bsd/kern/kern_exec.c bsd/kern/nord2_clockfloor.h
mk 0531 nord2-scp-motion-off bsd/dev/nord2_scp.inc
